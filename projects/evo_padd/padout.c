/* padout.c - where pad state leaves the daemon.
 *
 *  1. /dev/shmem/evo_pad (shared/include/evo_pad.h): the full state of up to four
 *     pads, for apps that want a real controller (the NES tile: diagonals, 2 players).
 *  2. The iDrive feed ring (shared/include/evo_feed.h): pad 1 as if it were the knob,
 *     so the launcher and every knob-driven app work with a pad unchanged:
 *         D-pad / left stick  tilt            A      knob press
 *         LB / RB             rotate (repeats) B      OPTION     X     MEDIA
 *         Menu                NAV             View   TEL        Y     COM
 *         Xbox                BACK
 *     The ring was designed for one writer (the tee inside `most`). We are a second
 *     one, like evo_feedpush: fine while the knob is idle, and a knob event that
 *     lands in the same instant as a pad event can be lost.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>

#include "padd.h"
#include "evo_feed.h"
#include "evo_pad.h"

#define STICK_ON    16000       /* left stick counts as a direction beyond this ... */
#define STICK_OFF   11000       /* ... and stops below this (hysteresis)            */
#define REPEAT_DELAY_MS 300     /* LB/RB held: first repeat */
#define REPEAT_EVERY_MS 60

/* raw tilt codes the tee passes in ev.b[2]; consumers that decode those get the same */
#define TILT_FWD 0x01
#define TILT_RIGHT 0x02
#define TILT_BACK 0x04
#define TILT_LEFT 0x08
/* hard-button ids (EVO_K_BUTTON b[0]) */
#define BTN_BACK 0x02
#define BTN_OPTION 0x04
#define BTN_MEDIA 0x08
#define BTN_NAV 0x10
#define BTN_TEL 0x20
#define BTN_COM 0x40

static pthread_mutex_t  g_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct evo_pad  *g_pad;
static struct evo_feed *g_feed;
static unsigned         g_feed_retry;
static int              g_use_feed = 1;

static pad_state_t g_last[PAD_MAX];
static unsigned    g_dirs;              /* pad 1: PAD_UP|DOWN|LEFT|RIGHT incl. the left stick */
static int         g_dir = -1;          /* pad 1: EVO_DIR_* currently held in the ring, -1 none */
static unsigned    g_lb_t, g_rb_t;      /* pad 1: next rotary repeat time, 0 = not held */

void padout_use_feed(int on) { g_use_feed = on; }

static void feed_map(void)
{
    int fd;
    void *p;

    if (g_feed || !g_use_feed) return;
    /* same open as the launcher: create it if the tee is not installed on this unit */
    fd = open(EVO_FEED_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) return;
    if (ftruncate(fd, sizeof(struct evo_feed)) != 0) { /* already sized */ }
    p = mmap(0, sizeof(struct evo_feed), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return;
    g_feed = p;
    if (g_feed->magic != EVO_FEED_MAGIC) {              /* nobody initialised it yet: we do */
        g_feed->head = 0;
        g_feed->grab_until = 0;
        g_feed->magic = EVO_FEED_MAGIC;
    }
    LOGD("padout: feed ring mapped");
}

static void feed_push(unsigned char kind, unsigned char b0, unsigned char b1, unsigned char b2)
{
    struct evo_ev *e;
    unsigned h;

    if (!g_feed || g_feed->magic != EVO_FEED_MAGIC) return;
    h = g_feed->head;
    e = &g_feed->ev[h % EVO_FEED_RING];
    e->kind = kind; e->ctr = (unsigned char)h;
    e->b[0] = b0; e->b[1] = b1; e->b[2] = b2; e->b[3] = EVO_PAD_FEED_MARK; e->b[4] = 0; e->b[5] = 0;
    __asm__ __volatile__(".word 0xf57ff05b" ::: "memory");      /* dmb: publish the event before head */
    g_feed->head = h + 1;
}

static int dir_code(int dir)
{
    return dir == EVO_DIR_FWD ? TILT_FWD : dir == EVO_DIR_BACK ? TILT_BACK : dir == EVO_DIR_LEFT ? TILT_LEFT : TILT_RIGHT;
}

static unsigned dir_bit(int dir)
{
    return dir == EVO_DIR_FWD ? PAD_UP : dir == EVO_DIR_BACK ? PAD_DOWN : dir == EVO_DIR_LEFT ? PAD_LEFT : PAD_RIGHT;
}

static void feed_button(unsigned changed, unsigned now, unsigned bit, unsigned char id)
{
    if (changed & bit) feed_push(EVO_K_BUTTON, id, (now & bit) ? 1 : 0, 0);
}

/* D-pad bits with the left stick folded in (hysteresis against the previous result) */
static unsigned directions(const pad_state_t *st, unsigned prev)
{
    unsigned d = st->buttons & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
    if (st->ly >  ((prev & PAD_UP)    ? STICK_OFF : STICK_ON)) d |= PAD_UP;
    if (st->ly < -((prev & PAD_DOWN)  ? STICK_OFF : STICK_ON)) d |= PAD_DOWN;
    if (st->lx < -((prev & PAD_LEFT)  ? STICK_OFF : STICK_ON)) d |= PAD_LEFT;
    if (st->lx >  ((prev & PAD_RIGHT) ? STICK_OFF : STICK_ON)) d |= PAD_RIGHT;
    return d;
}

/* pad 1 -> knob events */
static void feed_from_pad(const pad_state_t *st, const pad_state_t *old)
{
    unsigned now = st->buttons, changed = now ^ old->buttons;
    unsigned dirs = directions(st, g_dirs), pressed = dirs & ~g_dirs;
    int want = g_dir, d;

    /* The knob tilts one way at a time: keep the held direction while it is still
     * held, and let a newly pressed one take over. */
    if (want >= 0 && !(dirs & dir_bit(want))) want = -1;
    for (d = 0; d < 4; d++) if (pressed & dir_bit(d)) want = d;
    if (want < 0) for (d = 0; d < 4; d++) if (dirs & dir_bit(d)) { want = d; break; }
    if (want != g_dir) {
        if (g_dir >= 0) feed_push(EVO_K_DIR, (unsigned char)g_dir, 0, (unsigned char)dir_code(g_dir));
        if (want >= 0)  feed_push(EVO_K_DIR, (unsigned char)want, 1, (unsigned char)dir_code(want));
        g_dir = want;
    }
    g_dirs = dirs;

    if (changed & PAD_A) feed_push(EVO_K_CENTER, 0, (now & PAD_A) ? 1 : 0, 0);
    feed_button(changed, now, PAD_B, BTN_OPTION);
    feed_button(changed, now, PAD_X, BTN_MEDIA);
    feed_button(changed, now, PAD_MENU, BTN_NAV);
    feed_button(changed, now, PAD_VIEW, BTN_TEL);
    feed_button(changed, now, PAD_Y, BTN_COM);
    feed_button(changed, now, PAD_GUIDE, BTN_BACK);

    if (changed & PAD_LB) {
        if (now & PAD_LB) { feed_push(EVO_K_ROTARY, (unsigned char)(signed char)-1, 0, 0); g_lb_t = evo_now_ms() + REPEAT_DELAY_MS; }
        else g_lb_t = 0;
    }
    if (changed & PAD_RB) {
        if (now & PAD_RB) { feed_push(EVO_K_ROTARY, 1, 0, 0); g_rb_t = evo_now_ms() + REPEAT_DELAY_MS; }
        else g_rb_t = 0;
    }
}

/* everything pad 1 holds in the ring is released (controller gone / daemon exit) */
static void feed_release_all(void)
{
    pad_state_t none;
    memset(&none, 0, sizeof none);
    feed_from_pad(&none, &g_last[0]);
    g_lb_t = g_rb_t = 0;
}

void padout_init(void)
{
    int fd;
    void *p;

    memset(g_last, 0, sizeof g_last);
    fd = open(EVO_PAD_PATH, O_RDWR | O_CREAT, 0666);
    if (fd >= 0) {
        if (ftruncate(fd, sizeof(struct evo_pad)) != 0) { /* already sized */ }
        p = mmap(0, sizeof(struct evo_pad), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p != MAP_FAILED) {
            g_pad = p;
            memset(g_pad, 0, sizeof *g_pad);
            g_pad->alive = evo_now_ms();
            g_pad->magic = EVO_PAD_MAGIC;
        }
    }
    if (!g_pad) LOGE("padout: cannot create %s - apps will not see the pad state", EVO_PAD_PATH);
    feed_map();
}

/* called from the main loop every few tens of ms */
void padout_tick(void)
{
    unsigned t = evo_now_ms();

    pthread_mutex_lock(&g_mtx);
    if (g_pad) g_pad->alive = t;
    if (!g_feed && (int)(t - g_feed_retry) > 2000) { g_feed_retry = t; feed_map(); }
    if (g_lb_t && (int)(t - g_lb_t) >= 0) { feed_push(EVO_K_ROTARY, (unsigned char)(signed char)-1, 0, 0); g_lb_t = t + REPEAT_EVERY_MS; }
    if (g_rb_t && (int)(t - g_rb_t) >= 0) { feed_push(EVO_K_ROTARY, 1, 0, 0); g_rb_t = t + REPEAT_EVERY_MS; }
    pthread_mutex_unlock(&g_mtx);
}

void padout_connected(int slot, int connected, uint16_t vid, uint16_t pid)
{
    if (slot < 0 || slot >= PAD_MAX) return;
    pthread_mutex_lock(&g_mtx);
    if (!connected && slot == 0) feed_release_all();
    memset(&g_last[slot], 0, sizeof g_last[slot]);
    if (g_pad) {
        struct evo_pad_slot *s = &g_pad->pad[slot];
        s->connected = 0;
        s->buttons = 0; s->lx = s->ly = s->rx = s->ry = 0; s->lt = s->rt = 0;
        s->vid = vid; s->pid = pid; s->battery = 0xff;
        s->seq++;
        s->connected = connected ? 1 : 0;
    }
    pthread_mutex_unlock(&g_mtx);
    LOGI("pad slot %d: %s (%04x:%04x)", slot, connected ? "ready" : "gone", vid, pid);
}

void padout_battery(int slot, int level)
{
    if (slot < 0 || slot >= PAD_MAX || !g_pad) return;
    g_pad->pad[slot].battery = (unsigned)level;
}

static void buttons_str(uint16_t b, char *out, int outsz)
{
    static const struct { uint16_t bit; const char *name; } names[] = {
        { PAD_UP, "up" }, { PAD_DOWN, "down" }, { PAD_LEFT, "left" }, { PAD_RIGHT, "right" },
        { PAD_A, "A" }, { PAD_B, "B" }, { PAD_X, "X" }, { PAD_Y, "Y" }, { PAD_LB, "LB" }, { PAD_RB, "RB" },
        { PAD_LS, "LS" }, { PAD_RS, "RS" }, { PAD_MENU, "menu" }, { PAD_VIEW, "view" }, { PAD_GUIDE, "xbox" },
    };
    unsigned i;
    int n = 0;
    out[0] = 0;
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (b & names[i].bit) n += snprintf(out + n, outsz - n, "%s%s", n ? " " : "", names[i].name);
    if (!n) snprintf(out, outsz, "-");
}

/* quantise the analogue values so the trace only moves when the stick really does */
static int coarse(const pad_state_t *a, const pad_state_t *b)
{
    return (a->lx >> 12) != (b->lx >> 12) || (a->ly >> 12) != (b->ly >> 12) ||
           (a->rx >> 12) != (b->rx >> 12) || (a->ry >> 12) != (b->ry >> 12) ||
           (a->lt >> 7) != (b->lt >> 7) || (a->rt >> 7) != (b->rt >> 7);
}

void padout_state(int slot, const pad_state_t *st)
{
    char btn[96];

    if (slot < 0 || slot >= PAD_MAX) return;
    pthread_mutex_lock(&g_mtx);
    if (g_pad) {
        struct evo_pad_slot *s = &g_pad->pad[slot];
        s->lx = st->lx; s->ly = st->ly; s->rx = st->rx; s->ry = st->ry;
        s->lt = st->lt; s->rt = st->rt;
        s->buttons = st->buttons;
        s->seq++;
    }
    if (slot == 0) feed_from_pad(st, &g_last[0]);
    if (g_verbose >= 1 && (st->buttons != g_last[slot].buttons || coarse(st, &g_last[slot]))) {
        buttons_str(st->buttons, btn, sizeof btn);
        LOGD("pad slot %d: [%s]  L(%6d,%6d) R(%6d,%6d)  LT %4u RT %4u", slot, btn, st->lx, st->ly, st->rx, st->ry, st->lt, st->rt);
    }
    g_last[slot] = *st;
    pthread_mutex_unlock(&g_mtx);
}

void padout_shutdown(void)
{
    int i;
    pthread_mutex_lock(&g_mtx);
    feed_release_all();
    if (g_pad) {
        for (i = 0; i < PAD_MAX; i++) g_pad->pad[i].connected = 0;
        g_pad->alive = 0;
    }
    pthread_mutex_unlock(&g_mtx);
}
