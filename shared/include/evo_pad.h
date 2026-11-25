/* evo_pad.h - gamepad state shared by the pad daemon (evo_padd, the one writer) with
 * any app that wants a real controller: /dev/shmem/evo_pad.
 *
 * The daemon also feeds knob-equivalent events into the iDrive ring (evo_feed.h), so
 * apps written for the knob work with a pad unchanged. This block is for the rest:
 * every button at once (diagonals), both sticks, triggers, up to four pads.
 *
 * Reading it: map the file read-only; a pad is usable while the daemon is alive
 * (evo_pad_alive) and pad[i].connected is set. Fields are plain words written one at
 * a time, so read `buttons` once per frame into a local and work from that.
 */
#ifndef EVO_PAD_H
#define EVO_PAD_H

#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define EVO_PAD_PATH    "/dev/shmem/evo_pad"
#define EVO_PAD_MAGIC   0x0e5fad01u
#define EVO_PAD_MAX     4
#define EVO_PAD_ALIVE_MS 2000u      /* the daemon refreshes `alive` far more often than this */

/* buttons */
#define EVO_PAD_GUIDE   0x0001      /* the Xbox button */
#define EVO_PAD_MENU    0x0004      /* "start" */
#define EVO_PAD_VIEW    0x0008      /* "select" / back */
#define EVO_PAD_A       0x0010
#define EVO_PAD_B       0x0020
#define EVO_PAD_X       0x0040
#define EVO_PAD_Y       0x0080
#define EVO_PAD_UP      0x0100
#define EVO_PAD_DOWN    0x0200
#define EVO_PAD_LEFT    0x0400
#define EVO_PAD_RIGHT   0x0800
#define EVO_PAD_LB      0x1000
#define EVO_PAD_RB      0x2000
#define EVO_PAD_LS      0x4000      /* left stick click  */
#define EVO_PAD_RS      0x8000      /* right stick click */

struct evo_pad_slot {
    volatile unsigned int connected;    /* 1 while a controller holds this slot */
    volatile unsigned int seq;          /* bumps on every state change */
    volatile unsigned int buttons;      /* EVO_PAD_* */
    volatile int          lx, ly;       /* left stick,  -32768..32767, right / up positive */
    volatile int          rx, ry;       /* right stick, -32768..32767, right / up positive */
    volatile unsigned int lt, rt;       /* triggers, 0..1023 */
    volatile unsigned int vid, pid;     /* USB ids the controller announced */
    volatile unsigned int battery;      /* 0 empty .. 3 full, 0xff unknown */
    unsigned int          reserved[4];
};

struct evo_pad {
    volatile unsigned int magic;        /* EVO_PAD_MAGIC once initialised */
    volatile unsigned int alive;        /* evo_pad_now_ms() of the daemon's last heartbeat */
    unsigned int          reserved[6];
    struct evo_pad_slot   pad[EVO_PAD_MAX];
};

/* Events the pad daemon puts in the iDrive ring carry this in ev.b[3], so an app that
 * reads the pad block directly can skip them instead of seeing every press twice. */
#define EVO_PAD_FEED_MARK 0x58

/* the clock `alive` is written in: CLOCK_MONOTONIC milliseconds (same as evo_now_ms()) */
static inline unsigned int evo_pad_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)((unsigned long)t.tv_sec * 1000u + (unsigned long)t.tv_nsec / 1000000u);
}

/* Map the block read-only; NULL if the daemon has never run. */
static inline const struct evo_pad *evo_pad_open(void)
{
    int fd = open(EVO_PAD_PATH, O_RDONLY);
    void *p;
    if (fd < 0) return 0;
    p = mmap(0, sizeof(struct evo_pad), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    return p == MAP_FAILED ? 0 : (const struct evo_pad *)p;
}

static inline int evo_pad_alive(const struct evo_pad *p)
{
    return p && p->magic == EVO_PAD_MAGIC && (unsigned int)(evo_pad_now_ms() - p->alive) < EVO_PAD_ALIVE_MS;
}

/* buttons of pad `i`, or 0 when the daemon is gone or the pad is not connected */
static inline unsigned int evo_pad_buttons(const struct evo_pad *p, int i)
{
    if (!evo_pad_alive(p) || i < 0 || i >= EVO_PAD_MAX || !p->pad[i].connected) return 0;
    return p->pad[i].buttons;
}

#endif /* EVO_PAD_H */
