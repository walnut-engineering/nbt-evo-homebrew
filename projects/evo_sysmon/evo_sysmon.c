/* evo_sysmon - a full-screen system monitor for the NBT EVO ID5 head unit.
 *
 * A homebrew launcher tile (the "System" / CPU monitor).  It replaces the old
 * cpu_monitor demo, which was built against a different legacy graphics library
 * (a 360x200 overlay) and a Linux-style /proc that QNX does not have, so it did
 * not run under the launcher.  This one uses the SAME stack the terminal/DOOM/
 * launcher use (evo_graphics_lib + font8x8 straight to the framebuffer, HMI
 * hidden via presentation mode) and the SAME knob behaviour (iDrive BACK = exit,
 * via evo_appinput.h) - so it integrates cleanly and needs no keyboard.
 *
 * ALL data is read with native QNX syscalls - NO spawning of pidin/hogs (those
 * wedge in REPLY when spawned from inside this multithreaded graphics process):
 *   - /dev/cputemp                     per-core temperature
 *   - syspage num_cpu + cpuinfo.speed  core count + MHz
 *   - syspage asinfo "ram"             total RAM
 *   - posix_typed_mem("/memory")       free (allocatable) RAM
 *   - /proc/<pid>/as DCMD_PROC_INFO    per-process utime/stime/num_threads
 *                                      -> process+thread counts, CPU load, top list
 *   - CLOCK_MONOTONIC                  system uptime (since boot on QNX)
 *
 * Data is gathered on a background thread; the render/input thread never blocks,
 * so the UI stays live and the BACK button always exits.
 */
#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/procfs.h>
#include <sys/syspage.h>
#include <sys/neutrino.h>
#include <devctl.h>

#include "evo_graphics_lib.h"
#include "evo_context.h"
#include "evo_display.h"
#include "font8x8_basic.h"   /* shared 8x8 font (-I../evo_terminal) */
#include "evo_appinput.h"    /* iDrive BACK = exit (-I../../shared/include) */

#define DISPLAY_W 1440
#define DISPLAY_H 540

/* colours 0xAARRGGBB (framebuffer is BGRX; greys are swap-safe, accents cosmetic) */
#define C_BG     0xFF0A0A0CU
#define C_PANEL  0xFF16161AU
#define C_EDGE   0xFF2C2C32U
#define C_ACCENT 0xFFE08020U
#define C_TXT    0xFFFFFFFFU
#define C_DIM    0xFFA8A8B0U
#define C_GREEN  0xFF46C86EU
#define C_AMBER  0xFFE0A020U
#define C_RED    0xFFE05050U
#define C_BAR_BG 0xFF242430U

/* ---------- framebuffer drawing (same primitives as evo_launcher) ---------- */
static inline void put(uint32_t *fb, int x, int y, uint32_t c) {
    if ((unsigned)x < DISPLAY_W && (unsigned)y < DISPLAY_H) fb[y * DISPLAY_W + x] = c;
}
static void fill(uint32_t *fb, int x, int y, int w, int h, uint32_t c) {
    int i, j; for (j = 0; j < h; j++) for (i = 0; i < w; i++) put(fb, x + i, y + j, c);
}
static void frame(uint32_t *fb, int x, int y, int w, int h, int t, uint32_t c) {
    fill(fb, x, y, w, t, c); fill(fb, x, y + h - t, w, t, c);
    fill(fb, x, y, t, h, c); fill(fb, x + w - t, y, t, h, c);
}
static void glyph(uint32_t *fb, int x, int y, char ch, int s, uint32_t c) {
    const char *g = font8x8_basic[(unsigned char)ch & 0x7f];
    int r, b, sy, sx;
    for (r = 0; r < 8; r++) { unsigned char bits = (unsigned char)g[r];
        for (b = 0; b < 8; b++) if (bits & (1u << b))
            for (sy = 0; sy < s; sy++) for (sx = 0; sx < s; sx++)
                put(fb, x + b * s + sx, y + r * s + sy, c);
    }
}
static void text(uint32_t *fb, int x, int y, const char *str, int s, uint32_t c) {
    for (; *str; str++) { glyph(fb, x, y, *str, s, c); x += 8 * s; }
}
static int textw(const char *str, int s) { return (int)strlen(str) * 8 * s; }

/* ---------- stats ---------- */
#define MAXPROC 10
#define MAXPID  2048
struct stats {
    int ncpu, mhz;
    int temp[8], ntemp;
    int mem_total, mem_free;            /* MB */
    int procs, threads;
    int cpu_busy;                       /* % of total capacity (all cores) */
    int up_h, up_m, up_s;
    struct { int pid; int pct; char name[32]; } top[MAXPROC];  /* pct = % of one core */
    int ntop;
};

static void read_temps(struct stats *s) {
    s->ntemp = 0;
    FILE *f = fopen("/dev/cputemp", "r");
    if (!f) return;
    char line[160];
    while (s->ntemp < 8 && fgets(line, sizeof(line), f)) {
        int cpu, t;
        if (sscanf(line, "CPU #%d: Temperature: %d", &cpu, &t) == 2 ||
            sscanf(line, "CPU %d: %d", &cpu, &t) == 2)
            s->temp[s->ntemp++] = t;
    }
    fclose(f);
}

static void read_uptime(struct stats *s) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    long u = t.tv_sec;
    s->up_h = (int)(u / 3600); s->up_m = (int)((u % 3600) / 60); s->up_s = (int)(u % 60);
}

/* core count + MHz (syspage), total RAM (asinfo), free RAM (typed memory) */
static void read_sys(struct stats *s) {
    s->ncpu = _syspage_ptr ? _syspage_ptr->num_cpu : 1;
    if (s->ncpu < 1) s->ncpu = 1; if (s->ncpu > 8) s->ncpu = 8;
    struct cpuinfo_entry *ci = (struct cpuinfo_entry *)SYSPAGE_ENTRY(cpuinfo);
    s->mhz = ci ? (int)ci[0].speed : 0;

    s->mem_total = 0; s->mem_free = 0;
    struct asinfo_entry *as = (struct asinfo_entry *)SYSPAGE_ENTRY(asinfo);
    char *str = (char *)SYSPAGE_ENTRY(strings);
    unsigned n = (unsigned)(SYSPAGE_ENTRY_SIZE(asinfo) / sizeof(struct asinfo_entry)), i;
    unsigned long long ram = 0;
    for (i = 0; i < n; i++) { const char *nm = str + as[i].name; if (!strcmp(nm, "ram")) ram += as[i].end - as[i].start + 1ULL; }
    s->mem_total = (int)(ram / (1024 * 1024));

    int fd = posix_typed_mem_open("/memory", O_RDONLY, POSIX_TYPED_MEM_ALLOCATE);
    if (fd < 0) fd = posix_typed_mem_open("/ram", O_RDONLY, POSIX_TYPED_MEM_ALLOCATE);
    if (fd >= 0) {
        struct posix_typed_mem_info info;
        if (posix_typed_mem_get_info(fd, &info) == 0)
            s->mem_free = (int)((unsigned long long)info.posix_tmi_length / (1024 * 1024));
        close(fd);
    }
}

/* QNX has no /proc/<pid>/exefile; the executable path comes from DCMD_PROC_MAPDEBUG
 * on the already-open /proc/<pid>/as fd, using the process base_address (how pidin
 * does it). Called only from the stats thread, so the static scratch buffer is safe. */
static void resolve_name_fd(int fd, unsigned long long base, int pid, char *out, int n) {
    static char buf[sizeof(procfs_debuginfo) + 512];
    procfs_debuginfo *dbg = (procfs_debuginfo *)buf;
    out[0] = 0;
    if (pid == 1) { snprintf(out, n, "procnto"); return; }
    dbg->vaddr = base;
    if (devctl(fd, DCMD_PROC_MAPDEBUG, dbg, sizeof(buf), NULL) == EOK && dbg->path[0]) {
        char *b = strrchr(dbg->path, '/');
        snprintf(out, n, "%s", b ? b + 1 : dbg->path);
        return;
    }
    snprintf(out, n, "pid%d", pid);
}

/* walk /proc: process + thread counts, overall CPU load, top processes by CPU.
 * CPU% from the delta of each process's (utime+stime) over the wall interval. */
static struct { int pid; unsigned long long cpu; char name[32]; } g_prev[MAXPID];
static int g_nprev = 0;
static unsigned long long g_prev_wall = 0;
static struct { int pid; unsigned long long cpu, delta; char name[32]; } g_cur[MAXPID];

static void read_procs(struct stats *s) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long wall = (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    unsigned long long wdelta = g_prev_wall ? (wall - g_prev_wall) : 0;

    DIR *d = opendir("/proc");
    int nc = 0, procs = 0, threads = 0;
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL && nc < MAXPID) {
            if (!isdigit((unsigned char)de->d_name[0])) continue;
            int pid = atoi(de->d_name);
            char path[48]; snprintf(path, sizeof(path), "/proc/%d/as", pid);
            int fd = open(path, O_RDONLY);
            if (fd < 0) continue;
            procfs_info info;
            if (devctl(fd, DCMD_PROC_INFO, &info, sizeof(info), NULL) == EOK) {
                procs++; threads += (int)info.num_threads;
                unsigned long long cpu = info.utime + info.stime;
                unsigned long long pc = 0; int j, found = -1;
                for (j = 0; j < g_nprev; j++) if (g_prev[j].pid == pid) { pc = g_prev[j].cpu; found = j; break; }
                g_cur[nc].pid = pid; g_cur[nc].cpu = cpu;
                g_cur[nc].delta = cpu >= pc ? cpu - pc : 0;
                if (found >= 0 && g_prev[found].name[0])        /* name cached from a prior cycle */
                    snprintf(g_cur[nc].name, sizeof(g_cur[nc].name), "%s", g_prev[found].name);
                else                                            /* new pid: resolve once */
                    resolve_name_fd(fd, info.base_address, pid, g_cur[nc].name, sizeof(g_cur[nc].name));
                nc++;
            }
            close(fd);
        }
        closedir(d);
    }
    s->procs = procs; s->threads = threads;

    unsigned long long sum = 0; int i;
    for (i = 0; i < nc; i++) if (g_cur[i].pid != 1) sum += g_cur[i].delta;  /* pid 1 = procnto idle */
    s->cpu_busy = (wdelta > 0 && s->ncpu > 0) ? (int)(100ULL * sum / (wdelta * (unsigned)s->ncpu)) : 0;
    if (s->cpu_busy > 100) s->cpu_busy = 100;

    s->ntop = 0;
    if (wdelta > 0) {
        static char used[MAXPID];
        memset(used, 0, (size_t)nc);
        int k;
        for (k = 0; k < MAXPROC; k++) {
            int best = -1; unsigned long long bd = 0;
            for (i = 0; i < nc; i++) if (!used[i] && g_cur[i].pid != 1 && g_cur[i].delta > bd) { bd = g_cur[i].delta; best = i; }
            if (best < 0 || bd == 0) break;
            used[best] = 1;
            int pct = (int)(100ULL * g_cur[best].delta / wdelta);   /* % of one core */
            s->top[s->ntop].pid = g_cur[best].pid;
            s->top[s->ntop].pct = pct;
            snprintf(s->top[s->ntop].name, sizeof(s->top[s->ntop].name), "%s", g_cur[best].name);
            s->ntop++;
        }
    }

    g_nprev = 0;
    for (i = 0; i < nc && i < MAXPID; i++) {
        g_prev[i].pid = g_cur[i].pid; g_prev[i].cpu = g_cur[i].cpu;
        snprintf(g_prev[i].name, sizeof(g_prev[i].name), "%s", g_cur[i].name);
        g_nprev++;
    }
    g_prev_wall = wall;
}

static uint32_t temp_color(int t) { return t >= 85 ? C_RED : t >= 70 ? C_AMBER : C_GREEN; }
static uint32_t load_color(int p) { return p >= 90 ? C_RED : p >= 70 ? C_AMBER : C_GREEN; }

/* ---------- render ---------- */
static void bar(uint32_t *fb, int x, int y, int w, int h, int pct, uint32_t fg) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    fill(fb, x, y, w, h, C_BAR_BG);
    fill(fb, x, y, w * pct / 100, h, fg);
    frame(fb, x, y, w, h, 1, C_EDGE);
}
static void stat_tile(uint32_t *fb, int x, int y, int w, int h, const char *label,
                      const char *value, uint32_t vcol) {
    fill(fb, x, y, w, h, C_PANEL);
    frame(fb, x, y, w, h, 2, C_EDGE);
    text(fb, x + 16, y + 14, label, 2, C_DIM);
    text(fb, x + 16, y + 44, value, 3, vcol);
}
static void labelled_bar(uint32_t *fb, int y, const char *label, const char *right,
                         int pct, uint32_t fg) {
    text(fb, 24, y, label, 2, C_DIM);
    if (right) text(fb, DISPLAY_W - textw(right, 2) - 24, y, right, 2, C_DIM);
    char pc[16]; snprintf(pc, sizeof(pc), "%d%%", pct);
    bar(fb, 24, y + 24, DISPLAY_W - 48 - 80, 24, pct, fg);
    text(fb, DISPLAY_W - 24 - 66, y + 27, pc, 2, C_TXT);
}

static void render(uint32_t *fb, const struct stats *s) {
    int i;
    for (i = 0; i < DISPLAY_W * DISPLAY_H; i++) fb[i] = C_BG;

    fill(fb, 0, 0, DISPLAY_W, 56, C_PANEL);
    fill(fb, 0, 56, DISPLAY_W, 3, C_ACCENT);
    text(fb, 24, 16, "SYSTEM MONITOR", 4, C_TXT);
    const char *hint = "iDrive BACK = exit";
    text(fb, DISPLAY_W - textw(hint, 2) - 24, 22, hint, 2, C_DIM);

    int tx = 24, ty = 72, tw = (DISPLAY_W - 24 * 5) / 4, th = 104;
    char v[96];

    if (s->mhz > 0) snprintf(v, sizeof(v), "%dx%dMHz", s->ncpu, s->mhz);
    else            snprintf(v, sizeof(v), "%d cores", s->ncpu);
    stat_tile(fb, tx, ty, tw, th, "CPU", v, C_TXT);

    if (s->ntemp >= 2)      snprintf(v, sizeof(v), "%d/%dC", s->temp[0], s->temp[1]);
    else if (s->ntemp == 1) snprintf(v, sizeof(v), "%dC", s->temp[0]);
    else                    snprintf(v, sizeof(v), "n/a");
    stat_tile(fb, tx + (tw + 24), ty, tw, th, "CORE TEMP", v,
              s->ntemp ? temp_color(s->temp[0]) : C_DIM);

    int used = s->mem_total - s->mem_free;
    if (s->mem_total > 0) snprintf(v, sizeof(v), "%d/%dMB", used, s->mem_total);
    else                  snprintf(v, sizeof(v), "n/a");
    stat_tile(fb, tx + 2 * (tw + 24), ty, tw, th, "MEMORY", v, C_TXT);

    snprintf(v, sizeof(v), "%dp/%dt", s->procs, s->threads);
    stat_tile(fb, tx + 3 * (tw + 24), ty, tw, th, "TASKS", v, C_TXT);

    /* CPU load + memory bars */
    int by = ty + th + 20;
    char up[64]; snprintf(up, sizeof(up), "UPTIME %02d:%02d:%02d", s->up_h, s->up_m, s->up_s);
    labelled_bar(fb, by, "CPU LOAD", up, s->cpu_busy, load_color(s->cpu_busy));

    int mpct = s->mem_total ? used * 100 / s->mem_total : 0;
    labelled_bar(fb, by + 62, "MEMORY USAGE", NULL, mpct,
                 mpct >= 90 ? C_RED : mpct >= 75 ? C_AMBER : C_GREEN);

    /* top processes */
    int py = by + 124;
    text(fb, 24, py, "TOP PROCESSES (CPU)", 2, C_DIM);
    py += 28;
    int row_h = 32, maxrows = (DISPLAY_H - py - 12) / row_h;
    if (maxrows > s->ntop) maxrows = s->ntop;
    if (s->ntop == 0) text(fb, 24, py + 6, "gathering... (or system idle)", 2, C_DIM);
    for (i = 0; i < maxrows; i++) {
        int ry = py + i * row_h;
        char nm[40]; snprintf(nm, sizeof(nm), "%-18.18s", s->top[i].name);
        text(fb, 24, ry + 6, nm, 2, C_TXT);
        int bx = 24 + 18 * 16 + 16;
        int bw = DISPLAY_W - bx - 24 - 70;
        bar(fb, bx, ry + 3, bw, 20, s->top[i].pct > 100 ? 100 : s->top[i].pct, C_ACCENT);
        char sv[16]; snprintf(sv, sizeof(sv), "%d%%", s->top[i].pct);
        text(fb, DISPLAY_W - 24 - 60, ry + 6, sv, 2, C_TXT);
    }
}

/* ---------- threads + main ---------- */
static volatile int g_running = 1;
static struct stats  g_live;
static void on_sig(int s) { (void)s; g_running = 0; }

static void *stats_thread(void *arg) {
    (void)arg;
    while (g_running) {
        struct stats s; memset(&s, 0, sizeof(s));
        read_sys(&s); read_temps(&s); read_procs(&s); read_uptime(&s);
        memcpy((void *)&g_live, &s, sizeof(g_live));
        int k; for (k = 0; k < 50 && g_running; k++) usleep(20000);   /* ~1 s */
    }
    return NULL;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGPIPE, SIG_IGN);

    evo_graphics_init();
    evo_context_t *ctx = evo_context_create(DISPLAY_W, DISPLAY_H, 0, 0);
    if (!ctx) { printf("[SYSMON] context create failed\n"); return 1; }
    if (evo_context_show(ctx) != 0) { printf("[SYSMON] show failed\n"); evo_context_destroy(ctx); return 1; }
    { evo_display_t *d = evo_context_get_display(ctx);
      if (d && evo_display_enable_presentation_mode(d) == 0) printf("[SYSMON] presentation mode (HMI hidden)\n"); }

    if (evo_ai_start(&g_running) == 0) printf("[SYSMON] iDrive BACK = exit (knob thread up)\n");

    memset((void *)&g_live, 0, sizeof(g_live));
    read_sys((struct stats *)&g_live);                 /* seed static fields for frame 1 */
    read_temps((struct stats *)&g_live);
    pthread_t stid; pthread_create(&stid, NULL, stats_thread, NULL);
    printf("[SYSMON] running\n"); fflush(stdout);

    while (g_running) {
        struct stats cur;
        memcpy(&cur, (void *)&g_live, sizeof(cur));
        read_uptime(&cur);
        uint32_t *fb = evo_context_get_framebuffer(ctx);
        if (fb) { render(fb, &cur); evo_context_present(ctx); }
        usleep(300000);                                /* ~3 Hz (data ticks 1 Hz) - light CPU; BACK < 300 ms */
    }

    printf("[SYSMON] exit\n"); fflush(stdout);
    evo_context_destroy(ctx);
    return 0;
}
