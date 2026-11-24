/* evo_tee.so - LD_PRELOAD tee for an io-ipc client process (touchComDevice,
 * most, ...): pass-through + tee, with a re-armable RAW diagnostic logger.
 *
 * Hooks open()/close()/read()/readcond() (never MsgSend - that recursed and
 * crashed in earlier work).  It tracks every /dev/ipc/ channel fd the host process
 * opens, and on each read that carries bytes it (a) logs reads RAW (bounded,
 * re-armable) so we can learn the true channel framing, and (b) copies matching
 * controller frames into a shared ring (/dev/shmem/evo_ctrl_feed) for DOOM.
 *
 * Pass-through by default. While a consumer holds the GRAB (evo_feed.h) the
 * controller telegrams are neutralised in place for the host (HMI muted); see
 * e5_mask in evo_e5.h.  readcond() is hooked because the QNX io-ipc client may use it
 * instead of plain read(); if the symbol is absent the hook is simply unused.
 *
 * Files (TAG = $EVO_TEE_TAG, default "tee"; the shim sets it per host):
 *   /dev/shmem/evo_tee_<TAG>.log    log: ipc opens, raw reads, counters
 *   /dev/shmem/evo_tee_<TAG>.rearm  touch it to raw-log the next RAWLOG_CAP reads
 *                                   (checked every 16 reads once the budget is spent)
 * Raw lines: "rd <via> t=<ms> fd=<n> len=<n>: <bytes>" with t = ms since load.
 */
#define _QNX_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <time.h>
#include "evo_feed.h"
#include "evo_e5.h"

#define EVO_IPC_HDR 40
#define MAXFD 256
#define RAWLOG_CAP 400          /* raw-log budget per arm (boot traffic + one spin) */
#define RAWDUMP    256          /* bytes dumped per raw line (several telegrams per read) */
#define STATS_EVERY 256         /* counters line every N ipc reads */

typedef int (*readcond_t)(int, void*, int, int, int, int);

static int   (*r_open)(const char*,int,...);
static int   (*r_close)(int);
static ssize_t (*r_read)(int,void*,size_t);
static readcond_t r_readcond;

static char  g_isipc[MAXFD];                 /* 1 if fd is a /dev/ipc/ channel */
static int   g_fdchan[MAXFD];                /* ipc channel number for that fd (e.g. 25,27) */
static struct evo_feed *g_feed = NULL;
static FILE *g_log = NULL;
static unsigned long g_rot = 0, g_btn = 0, g_dir = 0, g_cen = 0;
static struct e5_state g_e5;
static struct e5_mask g_mask;
static int g_grabbed = 0;
static unsigned long g_masked = 0;
/* ---- knob injection (ipc25 only) ---- */
static struct evo_inject *g_inj = NULL;
static unsigned int g_inj_seq = 0;           /* last consumed host seq */
static unsigned char g_inj_q[24][20];        /* queued synthetic payloads (20 B each) */
static int g_inj_n = 0, g_inj_i = 0;         /* queue count / next index */
static unsigned long g_injected = 0;
/* 40-byte io-ipc header + 20-byte payload scaffold for a controller frame on ipc25 */
static const unsigned char INJ_HDR[40] = {
  0x00,0x00,0xf1,0x04,0x00,0x00,0x04,0x00,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x01,0x81,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x50 };
/* payload prefix common to rotary/button: e5 01 <fkt> 0c 0c 02 00 e5 01 07 00 00 00 01 1d */
static void inj_payload(unsigned char *p, unsigned char fkt, unsigned char seq,
                        unsigned char b16, unsigned char b17, unsigned char b18, unsigned char b19) {
    static const unsigned char pre[15] = {0xe5,0x01,0x10,0x0c,0x0c,0x02,0x00,0xe5,0x01,0x07,0x00,0x00,0x00,0x01,0x1d};
    memcpy(p, pre, 15); p[2] = fkt; p[15] = seq; p[16]=b16; p[17]=b17; p[18]=b18; p[19]=b19;
}
static void inj_enqueue(unsigned char *p) { if (g_inj_n < 24) memcpy(g_inj_q[g_inj_n++], p, 20); }
/* Build the synthetic frames for one host command into the queue. */
static void inj_build(unsigned int kind, int arg) {
    unsigned char pl[20];
    unsigned char seq = (unsigned char)(g_e5.have_pos ? (g_e5.pos & 0xff) : 0);  /* seq just needs to advance */
    if (kind == 1) {                                   /* rotary: |arg| detents, sign = direction */
        int n = arg < 0 ? -arg : arg, dir = arg < 0 ? -1 : 1, k;
        if (n > 20) n = 20;
        for (k = 0; k < n; k++) {
            int pos; g_e5.pos += dir; g_e5.have_pos = 1; pos = (g_e5.pos + 0x8000) & 0xffff;
            inj_payload(pl, 0x10, (unsigned char)(seq + k + 1), (unsigned char)(pos >> 8), (unsigned char)pos, 0x1e, 0x00);
            inj_enqueue(pl);
        }
    } else if (kind == 2) {                            /* centre press: down then up (id 1e01) */
        inj_payload(pl, 0x20, seq+1, 0x01, 0x00, 0x1e, 0x01); inj_enqueue(pl);
        inj_payload(pl, 0x20, seq+2, 0x00, 0x00, 0x1e, 0x01); inj_enqueue(pl);
    } else if (kind == 3) {                            /* tilt: dir down then up (id 1d01) */
        unsigned char tilt = arg==0?0x01: arg==1?0x04: arg==2?0x08: 0x02;   /* fwd/back/left/right */
        inj_payload(pl, 0x20, seq+1, 0x01, tilt, 0x1d, 0x01); inj_enqueue(pl);
        inj_payload(pl, 0x20, seq+2, 0x00, 0x00, 0x1d, 0x01); inj_enqueue(pl);
    } else if (kind == 4) {                            /* ring button: id 00<arg> down then up */
        inj_payload(pl, 0x20, seq+1, 0x01, 0x00, 0x00, (unsigned char)arg); inj_enqueue(pl);
        inj_payload(pl, 0x20, seq+2, 0x00, 0x00, 0x00, (unsigned char)arg); inj_enqueue(pl);
    }
}
static unsigned long g_rawn = 0;             /* raw reads logged in this arm */
static unsigned long g_reads = 0, g_bytes = 0; /* all ipc reads / bytes seen */
static char g_logpath[96], g_rearm[96];
static struct timespec g_t0;

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)((t.tv_sec - g_t0.tv_sec) * 1000 + (t.tv_nsec - g_t0.tv_nsec) / 1000000);
}

static void feed_init(void) {
    int fd;
    void *m;
    if (g_feed) return;
    fd = r_open(EVO_FEED_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) return;
    if (ftruncate(fd, sizeof(struct evo_feed)) != 0) { r_close(fd); return; }
    m = mmap(0, sizeof(struct evo_feed), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    r_close(fd);
    if (m == MAP_FAILED) return;
    g_feed = (struct evo_feed *)m;
    if (g_feed->magic != EVO_FEED_MAGIC) {       /* fresh (or stale-schema) ring: (re)initialise */
        g_feed->head = 0;
        g_feed->grab_until = 0;
        g_feed->magic = EVO_FEED_MAGIC;
    }
    {   /* inject control shm (knob -> HMI) */
        int ifd = r_open(EVO_INJECT_PATH, O_RDWR | O_CREAT, 0666);
        if (ifd >= 0) {
            void *im;
            if (ftruncate(ifd, sizeof(struct evo_inject)) == 0 &&
                (im = mmap(0, sizeof(struct evo_inject), PROT_READ | PROT_WRITE, MAP_SHARED, ifd, 0)) != MAP_FAILED) {
                g_inj = (struct evo_inject *)im;
                g_inj->magic = EVO_INJECT_MAGIC;
                g_inj_seq = g_inj->seq;
            }
            r_close(ifd);
        }
    }
}

static void feed_push(unsigned char kind, unsigned char ctr, const unsigned char *b) {
    unsigned int h;
    struct evo_ev *e;
    if (!g_feed) return;
    if (g_feed->magic != EVO_FEED_MAGIC) g_feed->magic = EVO_FEED_MAGIC;   /* re-own after an old producer */
    h = g_feed->head;
    e = &g_feed->ev[h % EVO_FEED_RING];
    e->kind = kind; e->ctr = ctr;
    memcpy(e->b, b, 6);
    /* Publish the slot before advancing head.  NOT __sync_synchronize(): this
     * Linux-targeted libgcc implements it by calling the Linux kernel helper at
     * 0xffff0fa0, which does not exist on QNX -> SIGSEGV (killed `most` once).
     * Raw "dmb ish" (ARM encoding), fine on the Cortex-A15. */
    __asm__ __volatile__(".word 0xf57ff05b" ::: "memory");
    g_feed->head = h + 1;
}

static void emit_feed(void *ctx, unsigned char kind, unsigned char ctr, const unsigned char *b) {
    (void)ctx;
    feed_push(kind, ctr, b);
    if (kind == EVO_K_ROTARY) g_rot++; else if (kind == EVO_K_DIR) g_dir++;
    else if (kind == EVO_K_CENTER) g_cen++; else g_btn++;
}

/* GRAB handling: runs AFTER tee_bytes (DOOM gets the real events), rewrites the
 * caller's buffer so most/HMI see neutral input while a consumer holds the grab. */
static ssize_t mask_bytes(int fd, unsigned char *buf, ssize_t r) {
    int grabbed;
    if (r <= 0 || !g_feed) return r;
    g_feed->tee_alive = evo_now_ms();
    grabbed = evo_grab_active(g_feed);
    if (grabbed != g_grabbed) {
        g_grabbed = grabbed;
        if (g_log) { fprintf(g_log, "[evo_tee] grab %s t=%ld masked=%lu\n", grabbed ? "ON (HMI muted)" : "off", now_ms(), g_masked); fflush(g_log); }
    }
    g_masked += (unsigned long)e5_mask(&g_mask, buf, (int)r, grabbed);

    /* INJECTION (ipc25/knob channel only): turn a benign poll/heartbeat read into a
     * synthetic controller frame so the HMI navigates. Only overwrite reads that carry
     * NO real controller event (poll 15 02 01 04 or heartbeat e5 0f), so real input is
     * never clobbered. One synthetic frame per qualifying read. */
    if (g_inj && fd >= 0 && fd < MAXFD && g_fdchan[fd] == 25) {
        if (g_inj->magic == EVO_INJECT_MAGIC && g_inj->seq != g_inj_seq) {
            g_inj_seq = g_inj->seq;
            if (g_inj_n == g_inj_i) { g_inj_n = g_inj_i = 0; }   /* reuse drained queue */
            inj_build(g_inj->kind, g_inj->arg);
            if (g_log) { fprintf(g_log, "[evo_tee] inject queued kind=%u arg=%d n=%d t=%ld\n", g_inj->kind, g_inj->arg, g_inj_n - g_inj_i, now_ms()); fflush(g_log); }
        }
        if (g_inj_i < g_inj_n && r >= 40) {
            unsigned char *pl = buf + 40;
            int is_poll = (r >= 44 && pl[0]==0x15 && pl[1]==0x02 && pl[2]==0x01 && pl[3]==0x04);
            int is_hb   = (r >= 42 && pl[0]==0xe5 && pl[1]==0x0f);
            if (is_poll || is_hb) {
                memcpy(buf, INJ_HDR, 40);
                memcpy(buf + 40, g_inj_q[g_inj_i++], 20);
                g_injected++;
                if (g_log && (g_injected % 4)==0) { fprintf(g_log, "[evo_tee] injected=%lu left=%d t=%ld\n", g_injected, g_inj_n - g_inj_i, now_ms()); fflush(g_log); }
                return 60;
            }
        }
    }
    return r;
}

/* Single place that sees every ipc read's bytes, from whatever read primitive. */
static void tee_bytes(int fd, const unsigned char *buf, ssize_t r, const char *via) {
    if (r <= 0) return;
    g_reads++; g_bytes += (unsigned long)r;
    /* RAW diagnostic: dump up to RAWLOG_CAP reads per arm.  Once the budget is
     * spent, every 16th read checks for the re-arm file (one access() call). */
    if (g_log && g_rawn >= RAWLOG_CAP && (g_reads & 15) == 0 && access(g_rearm, F_OK) == 0) {
        unlink(g_rearm);
        g_rawn = 0;
        fprintf(g_log, "[evo_tee] re-armed t=%ld reads=%lu\n", now_ms(), g_reads);
        fflush(g_log);
    }
    if (g_log && g_rawn < RAWLOG_CAP && !(r >= 44 && buf[40] == 0x15 && buf[41] == 0x02 && buf[42] == 0x01)
                                     && !(r >= 44 && buf[40] == 0xe5 && buf[41] == 0x0f)) {
        int i, lim = (int)(r < RAWDUMP ? r : RAWDUMP);
        g_rawn++;
        fprintf(g_log, "rd %s t=%ld fd=%d len=%d:", via, now_ms(), fd, (int)r);
        for (i = 0; i < lim; i++) fprintf(g_log, " %02x", buf[i]);
        fprintf(g_log, "\n");
        fflush(g_log);
    }
    if (g_feed) e5_scan(&g_e5, buf, (int)r, emit_feed, NULL);
    if (g_log && (g_reads % STATS_EVERY) == 0) {
        fprintf(g_log, "[evo_tee] t=%ld reads=%lu bytes=%lu rot=%lu dir=%lu center=%lu btn=%lu head=%u\n",
                now_ms(), g_reads, g_bytes, g_rot, g_dir, g_cen, g_btn, g_feed ? g_feed->head : 0);
        fflush(g_log);
    }
}

static void init_once(void) {
    if (r_read) return;
    r_open  = dlsym(RTLD_NEXT, "open");
    r_close = dlsym(RTLD_NEXT, "close");
    r_read  = dlsym(RTLD_NEXT, "read");
    r_readcond = (readcond_t)dlsym(RTLD_NEXT, "readcond");
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    {
        const char *tag = getenv("EVO_TEE_TAG");
        if (!tag || !*tag || strlen(tag) > 16) tag = "tee";
        snprintf(g_logpath, sizeof g_logpath, "/dev/shmem/evo_tee_%s.log", tag);
        snprintf(g_rearm, sizeof g_rearm, "/dev/shmem/evo_tee_%s.rearm", tag);
    }
    g_log = fopen(g_logpath, "w");
    if (g_log) {
        fprintf(g_log, "[evo_tee] loaded pid=%d readcond=%s rawcap=%d\n",
                (int)getpid(), r_readcond ? "yes" : "no", RAWLOG_CAP);
        fflush(g_log);
    }
    feed_init();
}

int open(const char *path, int flags, ...) {
    va_list ap; int mode = 0, fd;
    init_once();
    va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
    fd = r_open(path, flags, mode);
    if (path && fd >= 0 && fd < MAXFD && strncmp(path, "/dev/ipc/", 9) == 0) {
        g_isipc[fd] = 1;
        {   /* parse the trailing ipc channel number, e.g. /dev/ipc/ipc25 -> 25 */
            const char *q = path; int n = 0, seen = 0;
            while (*q) { if (*q >= '0' && *q <= '9') { n = n*10 + (*q - '0'); seen = 1; } else if (seen) break; q++; }
            g_fdchan[fd] = seen ? n : 0;
        }
        if (g_log) { fprintf(g_log, "[evo_tee] ipc open %s -> fd=%d chan=%d\n", path, fd, g_fdchan[fd]); fflush(g_log); }
    }
    return fd;
}

int close(int fd) {
    init_once();
    if (fd >= 0 && fd < MAXFD) g_isipc[fd] = 0;
    return r_close(fd);
}

ssize_t read(int fd, void *buf, size_t n) {
    ssize_t r;
    init_once();
    r = r_read(fd, buf, n);                    /* real read */
    if (fd >= 0 && fd < MAXFD && g_isipc[fd]) {
        tee_bytes(fd, (const unsigned char *)buf, r, "read");
        r = mask_bytes(fd, (unsigned char *)buf, r);   /* mask while grabbed; inject on ipc25 polls */
    }
    return r;
}

/* QNX conditional read: readcond(fd, buf, n, min, time, timeout). */
int readcond(int fd, void *buf, int n, int min, int time, int timeout) {
    int r;
    init_once();
    if (!r_readcond) { errno = ENOSYS; return -1; }
    r = r_readcond(fd, buf, n, min, time, timeout);
    if (fd >= 0 && fd < MAXFD && g_isipc[fd]) {
        tee_bytes(fd, (const unsigned char *)buf, r, "rcnd");
        r = (int)mask_bytes(fd, (unsigned char *)buf, r);
    }
    return r;
}
