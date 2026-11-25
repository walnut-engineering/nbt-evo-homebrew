/* evo_retro.c - a small libretro frontend for the NBT EVO head unit.
 *
 * One binary per system: this frontend linked statically with one emulator core
 * (cores.sh builds them; the Makefile picks one with SYSTEM=genesis|snes). The core
 * does the emulation behind the ten-function libretro interface; everything that
 * knows about the head unit is here and in the retro_*.c files beside it:
 *
 *   display : an evo_graphics_lib full-screen context, BMW HMI hidden while we run
 *             (retro_video.c scales the core's picture onto the 1440x540 panel)
 *   input   : gamepads through evo_padd, the iDrive knob, a USB keyboard (retro_input.c)
 *   sound   : QSA through the car's speakers, resampled and kept in step (retro_audio.c)
 *   screens : the ROM list and the in-game menu (retro_ui.c)
 *   games   : your own ROM files in SYS_DIR (/fs/sda1/evo/genesis or .../snes), plain
 *             or one game per .zip (retro_zip.c); `evo_genesis /path/game.md` skips
 *             the list
 *   settings: picture fit, sound level and a few core options (retro_opts.c)
 *   saves   : in SYS_DIR/saves - the cartridge's battery memory as <game>.srm, written
 *             a moment after the game changes it (a car can lose power at any time),
 *             and save states as <game>.st1 .. .st4 from the in-game menu
 *
 * Environment (the launch script sets none of these; they are for testing):
 *   EVO_RETRO_STATS=1     show frames per second and per-frame cost under the picture
 *   EVO_RETRO_FRAMES=n    leave by itself after n frames
 *   EVO_RETRO_TURBO=1     do not pace to real time (measures the whole pipeline)
 *   EVO_RETRO_AUTOSTART=n tap Start every n frames (gets a timing run past title screens)
 *   EVO_RETRO_SHADOW=1    draw through the graphics library's shadow frame (the slow,
 *                         plain path) instead of straight into the off-screen buffer
 *   EVO_RETRO_SNAP=1      screenshots on request: `echo name > /dev/shmem/retro_snap_req`
 *                         writes the next frame we show to /dev/shmem/name.ppm (1440x540)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "evo_graphics_lib.h"
#include "evo_context.h"
#include "evo_display.h"
#include "libretro.h"
#include "retro_fe.h"

volatile int fe_running = 1;
static evo_context_t *g_ctx = NULL;

static struct retro_system_av_info g_av;
static double g_fps = 60.0;
static int g_av_changed = 0;
static void *g_rom = NULL;

static int g_stats = 0, g_turbo = 0;
static unsigned long g_frame_limit = 0, g_autostart = 0;
static unsigned long g_frames = 0;          /* retro_run() calls */
static unsigned long g_shown = 0;           /* frames the core actually drew */
static unsigned long g_audio_frames = 0;
static unsigned long long g_scale_ns = 0;   /* this period: scaling the picture ... */
static unsigned long long g_show_ns = 0;    /* ... and handing the frame to the display */

/* Where a frame is drawn. Normally straight into the scan-out buffer that is not on
 * screen (only the picture is written each frame). The two buffers alternate, so after
 * anything else was on screen the surroundings are drawn again for two frames. */
static int g_direct = 1;
static int g_full = 2;                      /* frames that still need everything redrawn */
static int g_snap = 0;                      /* EVO_RETRO_SNAP */
static int g_bpp = 2;                       /* bytes per pixel of the core's picture */
static int g_loaded = 0;                    /* a game is loaded in the core */
static char g_core_err[100] = "";           /* the core's last complaint: why a game would not load */

/* the newest core frame, kept (rows packed) as the background of the in-game menu */
static unsigned char *g_still = NULL;
static size_t g_still_cap = 0;
static unsigned g_still_w = 0, g_still_h = 0;

/* a game that came out of an archive, described to a core that wants a file name */
static struct retro_game_info_ext g_ext;
static int g_ext_valid = 0;
static char g_ext_dir[512], g_ext_name[256], g_ext_file[272], g_ext_ext[8];
static char g_stat_line[112] = "", g_stat_line2[128] = "";
static int g_log_lines = 0;

/* ------------------------------------------------------------------- common */
static void on_sig(int s) { (void)s; fe_running = 0; }

static unsigned long long now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000000ull + (unsigned long long)t.tv_nsec;
}

unsigned fe_now_ms(void)
{
    struct timespec t;
    unsigned ms;
    clock_gettime(CLOCK_MONOTONIC, &t);
    ms = (unsigned)((unsigned long)t.tv_sec * 1000u + (unsigned long)t.tv_nsec / 1000000u);
    return ms ? ms : 1;
}

/* Test aid: when asked through /dev/shmem/retro_snap_req, write the frame that is about
 * to be shown as a PPM file. The panel-dump tool of the unit is a scarce resource (it
 * leaks flash until the next boot), and this shows exactly what we drew. */
static void snap_check(const uint32_t *frame)
{
    static const char req[] = "/dev/shmem/retro_snap_req";
    char name[48] = "", path[96];
    unsigned char *row;
    FILE *f;
    int x, y;

    if (!g_snap || !frame || !(f = fopen(req, "r"))) return;
    if (fgets(name, sizeof name, f)) name[strcspn(name, " \r\n/")] = 0;
    fclose(f);
    unlink(req);
    snprintf(path, sizeof path, "/dev/shmem/%s.ppm", name[0] ? name : "snap");
    if (!(row = (unsigned char *)malloc(FE_W * 3)) || !(f = fopen(path, "wb"))) { free(row); return; }
    fprintf(f, "P6\n%d %d\n255\n", FE_W, FE_H);
    for (y = 0; y < FE_H; y++) {
        const uint32_t *p = frame + y * FE_W;
        for (x = 0; x < FE_W; x++) { uint32_t v = p[x]; row[3 * x] = (unsigned char)(v >> 16); row[3 * x + 1] = (unsigned char)(v >> 8); row[3 * x + 2] = (unsigned char)v; }
        fwrite(row, 1, FE_W * 3, f);
    }
    fclose(f);
    free(row);
}

uint32_t *fe_fb(void) { return g_ctx ? evo_context_get_framebuffer(g_ctx) : NULL; }
void fe_present(void) { if (g_ctx) { snap_check(evo_context_get_framebuffer(g_ctx)); evo_context_present(g_ctx); } g_full = 2; }
void fe_redraw_all(void) { g_full = 2; }

/* the log is a RAM-backed file on the unit: keep it short */
static void say(const char *fmt, ...)
{
    va_list ap;
    if (g_log_lines > 400) return;
    g_log_lines++;
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    fflush(stdout);
}

/* ---------------------------------------------------------- libretro callbacks */
static void core_log(enum retro_log_level level, const char *fmt, ...)
{
    va_list ap;
    if (level >= RETRO_LOG_ERROR) {                     /* kept for the "would not load" screen */
        va_start(ap, fmt); vsnprintf(g_core_err, sizeof g_core_err, fmt, ap); va_end(ap);
        g_core_err[strcspn(g_core_err, "\r\n")] = 0;
    }
    if (level < RETRO_LOG_WARN || g_log_lines > 400) return;
    g_log_lines++;
    fputs(SYS_TAG " core: ", stdout);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    fflush(stdout);
}

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        fe_video_format((int)*(const enum retro_pixel_format *)data);
        g_bpp = *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888 ? 4 : 2;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:        /* BIOS files, if a system needs one */
        *(const char **)data = SYS_DIR "/system";
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = SYS_DIR "/saves";
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {              /* core options: ours if set, else the core's default */
        struct retro_variable *v = (struct retro_variable *)data;
        if (!v || !v->key) return false;
        v->value = fe_opts_core_value(v->key);
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = fe_opts_core_changed() ? true : false;
        return true;
    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:           /* only for a game out of an archive */
        if (!g_ext_valid || !data) return false;
        *(const struct retro_game_info_ext **)data = &g_ext;
        return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        *(int *)data = 3;                               /* picture and sound, please */
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        g_av.geometry = *(const struct retro_game_geometry *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        g_av = *(const struct retro_system_av_info *)data;
        g_av_changed = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        if (data && ((const struct retro_message *)data)->msg)
            say(SYS_TAG " core says: %s\n", ((const struct retro_message *)data)->msg);
        return true;
    /* told, not needed */
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
        return true;
    default:
        return false;                                   /* everything else: not offered */
    }
}

static void stats_overlay(uint32_t *fb)
{
    if (!g_stats || !g_stat_line[0] || !fb) return;
    fe_fill(fb, 0, FE_H - 40, FE_W, 40, C_BG);
    fe_text(fb, (FE_W - fe_textw(g_stat_line, 2)) / 2, FE_H - 38, g_stat_line, 2, C_DIM);
    fe_text(fb, (FE_W - fe_textw(g_stat_line2, 2)) / 2, FE_H - 18, g_stat_line2, 2, C_DIM);
}

/* keep a copy of the frame for the menu's background (rows packed: a core's pitch can
 * be several times the picture's width) */
static void keep_still(const void *data, unsigned w, unsigned h, size_t pitch)
{
    size_t rowb = (size_t)w * (size_t)g_bpp, need = rowb * h;
    unsigned y;
    if (need > g_still_cap) {
        unsigned char *n = (unsigned char *)realloc(g_still, need);
        if (!n) { g_still_w = g_still_h = 0; return; }
        g_still = n; g_still_cap = need;
    }
    for (y = 0; y < h; y++) memcpy(g_still + y * rowb, (const char *)data + y * pitch, rowb);
    g_still_w = w; g_still_h = h;
}

void fe_backdrop(uint32_t *fb)
{
    int i;
    if (!fb) return;
    fe_clear(fb, C_BG);
    if (g_still && g_still_w) {
        fe_video_prepare(g_still_w, g_still_h);
        fe_video_blit(fb, g_still, g_still_w, g_still_h, (size_t)g_still_w * (size_t)g_bpp);
        for (i = 0; i < FE_W * FE_H; i++) fb[i] = 0xFF000000u | ((fb[i] >> 2) & 0x003F3F3Fu);     /* dimmed */
    }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    unsigned long long t0, t1;
    evo_display_t *disp;
    uint32_t *dst = NULL;
    int stride = 0, full;

    if (!data) return;                                  /* the core repeated the last frame */
    t0 = now_ns();
    if (fe_video_prepare(w, h)) g_full = 2;
    keep_still(data, w, h, pitch);

    disp = evo_context_get_display(g_ctx);
    if (g_direct && disp) dst = (uint32_t *)evo_display_direct_begin(disp, &stride);
    if (dst && stride == FE_W * 4) {
        int x, y, rw, rh;
        full = g_full > 0;
        if (full) { fe_clear(dst, C_BG); g_full--; }
        fe_video_blit(dst, data, w, h, pitch);
        stats_overlay(dst);
        t1 = now_ns();
        snap_check(dst);
        fe_video_rect(&x, &y, &rw, &rh);
        if (full || g_stats) evo_display_direct_post(disp, 0, 0, FE_W, FE_H);
        else evo_display_direct_post(disp, x, y, rw, rh);
    } else {                                            /* plain path: shadow frame, copied whole */
        uint32_t *fb = fe_fb();
        if (!fb) return;
        if (g_full > 0) fe_clear(fb, C_BG);
        fe_video_blit(fb, data, w, h, pitch);
        stats_overlay(fb);
        t1 = now_ns();
        snap_check(fb);
        evo_context_present(g_ctx);
        g_full = 0;
    }
    g_shown++;
    g_scale_ns += t1 - t0;
    g_show_ns += now_ns() - t1;
}

static size_t audio_batch_cb(const int16_t *d, size_t n)
{
    g_audio_frames += n;
    fe_audio_push(d, n);
    return n;
}

/* a core that hands over one sample at a time: collect them into batches */
static int16_t g_one[256 * 2];
static unsigned g_one_n = 0;
static void audio_flush(void) { if (g_one_n) { audio_batch_cb(g_one, g_one_n); g_one_n = 0; } }
static void audio_cb(int16_t l, int16_t r)
{
    g_one[2 * g_one_n] = l; g_one[2 * g_one_n + 1] = r;
    if (++g_one_n == 256) audio_flush();
}
static void input_poll_cb(void) {}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    unsigned m;
    (void)index;
    if ((device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
    m = fe_input_pad((int)port);
    if (g_autostart && port == 0 && (g_frames % g_autostart) < 4) m |= FE_START;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)m;
    return (id < 16 && (m & (1u << id))) ? 1 : 0;
}

/* ------------------------------------------------------------------- a game */
static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* -------------------------------------------------------------------- saves */
static char g_stem[600];                    /* SYS_DIR/saves/<game without extension> */
static uint32_t g_sram_saved = 0;           /* hash of what is in the .srm file */
static uint32_t g_sram_prev = 0;            /* hash at the previous check */
static unsigned g_sram_dirty_since = 0;     /* when it first differed from the file (ms), 0 = clean */

static void set_stem(const char *rom)
{
    char *dot;
    snprintf(g_stem, sizeof g_stem - 8, SYS_DIR "/saves/%s", base_name(rom));
    dot = strrchr(g_stem, '.');
    if (dot && dot > strrchr(g_stem, '/')) *dot = 0;
}

static uint32_t hash_mem(const void *p, size_t n)       /* FNV-1a */
{
    const unsigned char *b = (const unsigned char *)p;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

/* write through a temporary file so a power cut cannot leave half a save */
int fe_write_file(const char *path, const void *data, size_t n)
{
    char tmp[640];
    int fd, ok;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    ok = write(fd, data, n) == (ssize_t)n;
    if (ok) fsync(fd);
    close(fd);
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) unlink(tmp);
    return ok;
}

static void sram_load(void)
{
    void *mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    char path[640];
    FILE *f;

    g_sram_dirty_since = 0;
    if (!mem || !size) { g_sram_saved = g_sram_prev = 0; return; }
    snprintf(path, sizeof path, "%s.srm", g_stem);
    if ((f = fopen(path, "rb")) != NULL) {
        size_t got = fread(mem, 1, size, f);
        fclose(f);
        say(SYS_TAG " battery save loaded (%u bytes)\n", (unsigned)got);
    }
    g_sram_saved = g_sram_prev = hash_mem(mem, size);
}

/* Called once a second while playing, and with force = 1 when leaving or pausing.
 * Writes when the memory has changed and then stayed the same for one check (a game's
 * save is finished), or after 30 s for a game that never stops touching it. */
static void sram_tick(int force)
{
    void *mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    uint32_t h;
    char path[640];

    if (!mem || !size) return;
    h = hash_mem(mem, size);
    if (h == g_sram_saved) { g_sram_dirty_since = 0; g_sram_prev = h; return; }
    if (!g_sram_dirty_since) g_sram_dirty_since = fe_now_ms();
    if (force || h == g_sram_prev || (unsigned)(fe_now_ms() - g_sram_dirty_since) > 30000u) {
        snprintf(path, sizeof path, "%s.srm", g_stem);
        if (fe_write_file(path, mem, size)) { g_sram_saved = h; g_sram_dirty_since = 0; say(SYS_TAG " battery save written (%u bytes)\n", (unsigned)size); }
        else say(SYS_TAG " could not write %s\n", path);
    }
    g_sram_prev = h;
}

int fe_state_exists(int slot)
{
    char path[640];
    snprintf(path, sizeof path, "%s.st%d", g_stem, slot);
    return access(path, R_OK) == 0;
}

int fe_state_save(int slot)
{
    size_t size = retro_serialize_size();
    void *buf;
    char path[640];
    int ok;

    if (!size || !(buf = malloc(size))) return 0;
    snprintf(path, sizeof path, "%s.st%d", g_stem, slot);
    ok = retro_serialize(buf, size) && fe_write_file(path, buf, size);
    free(buf);
    say(SYS_TAG " state %d %s (%u bytes)\n", slot, ok ? "saved" : "NOT saved", (unsigned)size);
    return ok;
}

int fe_state_load(int slot)
{
    char path[640];
    FILE *f;
    long size;
    void *buf;
    int ok = 0;

    snprintf(path, sizeof path, "%s.st%d", g_stem, slot);
    if (!(f = fopen(path, "rb"))) return 0;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size > 0 && size < 64L * 1024 * 1024 && (buf = malloc((size_t)size)) != NULL) {
        if (fread(buf, 1, (size_t)size, f) == (size_t)size) ok = retro_unserialize(buf, (size_t)size);
        free(buf);
    }
    fclose(f);
    say(SYS_TAG " state %d %s\n", slot, ok ? "loaded" : "NOT loaded");
    return ok;
}

static void set_timing(void)
{
    g_fps = g_av.timing.fps;
    if (!(g_fps > 20.0 && g_fps < 130.0)) g_fps = 60.0;
    g_av_changed = 0;
    fe_audio_set_rate(g_av.timing.sample_rate, g_fps);
}

static int has_ext(const char *path, const char *ext)
{
    const char *dot = strrchr(path, '.');
    return dot && strcasecmp(dot + 1, ext) == 0;
}

/* The pad type the player chose (Genesis: 3 or 6 buttons), handed to the core. With
 * "auto" nothing is forced and the core decides from the cartridge header. */
void fe_core_pad_changed(void)
{
#if defined(EVO_RETRO_GENESIS)
    int pad = fe_opts_pad();
    unsigned dev = pad == 3 ? RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0) :
                   pad == 6 ? RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1) : RETRO_DEVICE_JOYPAD;
    if (!g_loaded) return;
    retro_set_controller_port_device(0, dev);
    retro_set_controller_port_device(1, dev);
#endif
}

/* Read the game into memory: out of a .zip, or as it is for a core that wants it that
 * way. 1 = fine (g_rom may still be NULL: the core reads the file itself). A core will
 * happily "run" a file of a few bytes as a blank screen, so those are refused here. */
#define MIN_GAME_BYTES 256
static int read_game(const char *path, int need_fullpath, struct retro_game_info *gi)
{
    FILE *f;
    long size = 0;

    gi->path = path;
    if (has_ext(path, "zip")) {
        size_t zsize = 0;
        const char *slash = strrchr(path, '/');
        char *dot;
        if (!fe_zip_read(path, &g_rom, &zsize, g_ext_ext, sizeof g_ext_ext)) {
            snprintf(g_core_err, sizeof g_core_err, "%s", fe_zip_error());
            return 0;
        }
        if (zsize < MIN_GAME_BYTES) { snprintf(g_core_err, sizeof g_core_err, "the file is too small to be a game"); return 0; }
        gi->data = g_rom; gi->size = zsize;
        /* "<folder>/<archive name>.<the game's extension>" is what the game is called */
        snprintf(g_ext_dir, sizeof g_ext_dir, "%.*s", slash ? (int)(slash - path) : 1, slash ? path : ".");
        snprintf(g_ext_name, sizeof g_ext_name, "%s", slash ? slash + 1 : path);
        if ((dot = strrchr(g_ext_name, '.')) != NULL) *dot = 0;
        snprintf(g_ext_file, sizeof g_ext_file, "%s.%s", g_ext_name, g_ext_ext);
        memset(&g_ext, 0, sizeof g_ext);
        g_ext.archive_path = path; g_ext.archive_file = g_ext_file;
        g_ext.dir = g_ext_dir; g_ext.name = g_ext_name; g_ext.ext = g_ext_ext;
        g_ext.data = g_rom; g_ext.size = zsize;
        g_ext.file_in_archive = true; g_ext.persistent_data = true;
        g_ext_valid = 1;
        return 1;
    }
    if (!has_ext(path, "cue")) {                        /* (a .cue is a few lines of text naming the tracks) */
        struct stat st;
        if (stat(path, &st) != 0) { snprintf(g_core_err, sizeof g_core_err, "the file is not there"); return 0; }
        if (st.st_size < MIN_GAME_BYTES) { snprintf(g_core_err, sizeof g_core_err, "the file is too small to be a game"); return 0; }
    }
    if (need_fullpath) return 1;
    f = fopen(path, "rb");
    if (f) { fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET); }
    if (!f || size <= 0 || size > 64L * 1024 * 1024 || !(g_rom = malloc((size_t)size)) ||
        fread(g_rom, 1, (size_t)size, f) != (size_t)size) {
        if (f) fclose(f);
        snprintf(g_core_err, sizeof g_core_err, "the file could not be read");
        return 0;
    }
    fclose(f);
    gi->data = g_rom; gi->size = (size_t)size;
    return 1;
}

/* how a game ended */
enum { RUN_FAILED = -1,     /* it would not load */
       RUN_QUIT = 0,        /* "Quit": back to the launcher */
       RUN_LIST,            /* "Change game": back to the list */
       RUN_RELOAD };        /* a reset that has to load the game again (a changed option) */

static int run_game(const char *path)
{
    struct retro_system_info si;
    struct retro_game_info gi;
    unsigned long long next, period, t_stat, run_ns = 0, worst_ns = 0, all_ns = 0, all_worst = 0;
    unsigned long slow = 0;
    unsigned long f_stat = 0, shown_stat = 0;
    int first_report = 1, ret = RUN_QUIT;

    g_frames = g_shown = 0;
    g_core_err[0] = 0;
    g_ext_valid = 0;
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    memset(&si, 0, sizeof si);
    retro_get_system_info(&si);
    memset(&gi, 0, sizeof gi);
    if (!read_game(path, si.need_fullpath, &gi)) {
        say(SYS_TAG " cannot read %s: %s\n", path, g_core_err);
        free(g_rom); g_rom = NULL;
        retro_deinit();
        return RUN_FAILED;
    }
    if (!retro_load_game(&gi)) {
        say(SYS_TAG " the core refused %s\n", path);
        free(g_rom); g_rom = NULL;
        g_ext_valid = 0;
        retro_deinit();
        return RUN_FAILED;
    }
    g_loaded = 1;
    fe_opts_game_loaded();
    if (fe_opts_pad()) fe_core_pad_changed();
    memset(&g_av, 0, sizeof g_av);
    retro_get_system_av_info(&g_av);
    set_timing();
    set_stem(path);
    sram_load();
    say(SYS_TAG " %s %s: %s - %.2f fps, sound %.0f Hz\n", si.library_name, si.library_version, base_name(path),
        g_fps, g_av.timing.sample_rate);

    g_still_w = g_still_h = 0;                          /* no picture of this game yet */
    fe_redraw_all();
    fe_input_block_held();
    fe_audio_resume();
    (void)fe_input_menu(); (void)fe_input_rotary();
    period = (unsigned long long)(1e9 / g_fps);
    next = t_stat = now_ns();

    while (fe_running) {
        unsigned long long t0, t1, now;

        fe_input_poll();
        if (fe_input_menu() > 0) {
            int r;
            fe_audio_pause();
            sram_tick(1);
            r = fe_ui_pause(base_name(path));
            if (r == FE_MENU_QUIT) break;
            if (r == FE_MENU_LIST) { ret = RUN_LIST; break; }
            if (r == FE_MENU_RESET) {
                if (fe_opts_reload_wanted()) { ret = RUN_RELOAD; break; }
                retro_reset();
            }
            fe_redraw_all();
            fe_input_block_held();
            fe_audio_resume();
            next = t_stat = now_ns(); run_ns = worst_ns = 0; g_scale_ns = g_show_ns = 0;   /* the pause is not a slow frame */
            f_stat = g_frames; shown_stat = g_shown;
        }

        t0 = now_ns();
        retro_run();
        audio_flush();
        t1 = now_ns();
        g_frames++;
        run_ns += t1 - t0; all_ns += t1 - t0;
        if (t1 - t0 > worst_ns) worst_ns = t1 - t0;
        if (g_frames > 60 && t1 - t0 > all_worst) all_worst = t1 - t0;
        if (t1 - t0 > period) slow++;
        if (g_av_changed) { set_timing(); period = (unsigned long long)(1e9 / g_fps); }
        if (g_frame_limit && g_frames >= g_frame_limit) break;

        /* once a second: what it costs */
        if (t1 - t_stat >= 1000000000ull) {
            unsigned long n = g_frames - f_stat;
            double secs = (double)(t1 - t_stat) / 1e9, ms = n ? (double)run_ns / 1e6 / n : 0.0;
            double sms = n ? (double)g_scale_ns / 1e6 / n : 0.0, pms = n ? (double)g_show_ns / 1e6 / n : 0.0;
            snprintf(g_stat_line, sizeof g_stat_line, "%.1f fps (%.1f drawn)  core %.2f + scale %.2f + show %.2f ms  worst %.1f ms",
                     n / secs, (g_shown - shown_stat) / secs, ms - sms - pms, sms, pms, (double)worst_ns / 1e6);
            sram_tick(0);
            fe_audio_stats(g_stat_line2, sizeof g_stat_line2);
            if (first_report ? g_frames > 300 : (g_frames % 3600) < (unsigned long)g_fps + 2) {
                say(SYS_TAG " %s\n" SYS_TAG " %s\n", g_stat_line, g_stat_line2);
                first_report = 0;
            }
            t_stat = t1; f_stat = g_frames; shown_stat = g_shown; run_ns = worst_ns = 0; g_scale_ns = g_show_ns = 0;
        }

        /* pace to the console's own frame rate */
        if (g_turbo) continue;
        next += period;
        now = now_ns();
        if (now > next + 100000000ull) next = now;       /* fell far behind (a stall): do not sprint to catch up */
        else if (next > now) {
            struct timespec ts;
            ts.tv_sec = (time_t)(next / 1000000000ull); ts.tv_nsec = (long)(next % 1000000000ull);
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        }
    }

    fe_audio_pause();
    sram_tick(1);
    say(SYS_TAG " leaving after %lu frames (%lu drawn): %.2f ms per frame on average, worst %.1f ms, %lu over one frame's time\n",
        g_frames, g_shown, g_frames ? (double)all_ns / 1e6 / g_frames : 0.0, (double)all_worst / 1e6, slow);
    g_loaded = 0;
    retro_unload_game();
    retro_deinit();
    free(g_rom); g_rom = NULL;
    g_ext_valid = 0;
    g_stat_line[0] = g_stat_line2[0] = 0;
    return ret;
}

#if defined(EVO_RETRO_GENESIS)
/* A Sega CD image needs the console's BIOS, which the player has to supply. */
static int cd_without_bios(const char *path)
{
    if (!has_ext(path, "cue") && !has_ext(path, "iso")) return 0;
    return access(SYS_DIR "/system/bios_CD_U.bin", R_OK) != 0 && access(SYS_DIR "/system/bios_CD_E.bin", R_OK) != 0 &&
           access(SYS_DIR "/system/bios_CD_J.bin", R_OK) != 0;
}
#endif

/* ======================================================================= main */
int main(int argc, char **argv)
{
    char rom[512] = "";
    const char *e;
    uint32_t *fb;
    int from_arg = 0;

    signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGPIPE, SIG_IGN);
    if (argc > 1) { strncpy(rom, argv[1], sizeof rom - 1); from_arg = 1; }
    if ((e = getenv("EVO_RETRO_STATS")) != NULL && atoi(e) > 0) g_stats = 1;
    if ((e = getenv("EVO_RETRO_TURBO")) != NULL && atoi(e) > 0) g_turbo = 1;
    if ((e = getenv("EVO_RETRO_SHADOW")) != NULL && atoi(e) > 0) g_direct = 0;
    if ((e = getenv("EVO_RETRO_FRAMES")) != NULL && atol(e) > 0) g_frame_limit = (unsigned long)atol(e);
    if ((e = getenv("EVO_RETRO_AUTOSTART")) != NULL && atol(e) > 0) g_autostart = (unsigned long)atol(e);
    if ((e = getenv("EVO_RETRO_SNAP")) != NULL && atoi(e) > 0) g_snap = 1;

    mkdir(SYS_DIR, 0755);
    mkdir(SYS_DIR "/saves", 0755);
    mkdir(SYS_DIR "/system", 0755);                     /* BIOS files, for the systems that need one */
    fe_opts_load();

    evo_graphics_init();
    g_ctx = evo_context_create(FE_W, FE_H, 0, 0);
    if (!g_ctx) { printf(SYS_TAG " context create failed\n"); return 1; }
    if (evo_context_show(g_ctx) != 0) { printf(SYS_TAG " show failed\n"); evo_context_destroy(g_ctx); return 1; }
    { evo_display_t *d = evo_context_get_display(g_ctx); if (d) evo_display_enable_presentation_mode(d); }
    fb = fe_fb();
    if (fb) { fe_clear(fb, C_BG); fe_present(); }

    fe_input_start();

    while (fe_running) {
        int r;
        if (!rom[0] && !fe_ui_picker(rom, sizeof rom)) break;
#if defined(EVO_RETRO_GENESIS)
        if (cd_without_bios(rom)) {
            fe_ui_message("Sega CD games need the console's BIOS file",
                          "copy bios_CD_U.bin (or _E / _J) to " SYS_DIR "/system", 8000);
            if (from_arg) break;
            rom[0] = 0;
            continue;
        }
#endif
        if (!g_turbo && !fe_audio_active()) fe_audio_init();    /* after the list: it holds the sound route */
        r = run_game(rom);
        if (r == RUN_QUIT) break;                       /* back to the launcher */
        if (r == RUN_RELOAD) continue;                  /* the same game, loaded again */
        if (r == RUN_FAILED) {
            char why[160];
            snprintf(why, sizeof why, "%s%s%s", base_name(rom), g_core_err[0] ? ": " : "", g_core_err);
            fe_ui_message("this game would not load", why, 6000);
            if (from_arg) break;
        }
        rom[0] = 0;                                     /* failed, or "Change game": the list */
        from_arg = 0;
    }

    fe_audio_shutdown();
    fe_input_stop();
    evo_context_destroy(g_ctx);
    g_ctx = NULL;
    printf(SYS_TAG " exit\n");
    return 0;
}
