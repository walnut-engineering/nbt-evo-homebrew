/* evo_padfake.c - a pretend gamepad, for testing apps without a controller in hand.
 *
 * Writes pad 1 of the shared pad-state block (/dev/shmem/evo_pad, evo_pad.h) the way
 * evo_padd does when a real controller reports, and can also put pad-marked events
 * into the knob ring. Apps cannot tell the difference, so their gamepad handling can
 * be driven from a script (and checked with evo_screenshot) from the host.
 *
 * Works with evo_padd running (no real controller connected) or without it - the tool
 * keeps the block's heartbeat fresh while it runs.
 *
 *   evo_padfake <step>...
 *     a+rb:300          hold those buttons 300 ms, then release
 *                       (a b x y up down left right lb rb ls rs menu view xbox)
 *     lx=20000          set an axis and leave it (lx ly rx ry: -32768..32767; lt rt: 0..1023)
 *     wait:500          pause
 *     ring:0x02:150     marked knob-ring BUTTON event, down then up after 150 ms
 *     ringdir:fwd:150   marked knob-ring tilt (fwd back left right)
 *     ringcenter:150    marked knob-ring press
 *     keep              leave the pad "connected" when the tool exits (default: disconnect)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "evo_feed.h"
#include "evo_pad.h"

static struct evo_pad  *g_pad;
static struct evo_feed *g_feed;

static void beat(void) { g_pad->alive = evo_now_ms(); }

/* sleep in slices so the heartbeat never goes stale while we hold something */
static void pause_ms(int ms)
{
    while (ms > 0) { int s = ms > 100 ? 100 : ms; usleep(s * 1000); ms -= s; beat(); }
}

static unsigned button_bit(const char *n)
{
    static const struct { const char *name; unsigned bit; } t[] = {
        { "a", EVO_PAD_A }, { "b", EVO_PAD_B }, { "x", EVO_PAD_X }, { "y", EVO_PAD_Y },
        { "up", EVO_PAD_UP }, { "down", EVO_PAD_DOWN }, { "left", EVO_PAD_LEFT }, { "right", EVO_PAD_RIGHT },
        { "lb", EVO_PAD_LB }, { "rb", EVO_PAD_RB }, { "ls", EVO_PAD_LS }, { "rs", EVO_PAD_RS },
        { "menu", EVO_PAD_MENU }, { "view", EVO_PAD_VIEW }, { "xbox", EVO_PAD_GUIDE },
    };
    unsigned i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcasecmp(n, t[i].name)) return t[i].bit;
    return 0;
}

static void ring_push(unsigned char kind, unsigned char b0, unsigned char b1, unsigned char b2)
{
    struct evo_ev *e;
    unsigned h;
    if (!g_feed || g_feed->magic != EVO_FEED_MAGIC) { fprintf(stderr, "padfake: no knob ring\n"); return; }
    h = g_feed->head;
    e = &g_feed->ev[h % EVO_FEED_RING];
    e->kind = kind; e->ctr = (unsigned char)h;
    e->b[0] = b0; e->b[1] = b1; e->b[2] = b2; e->b[3] = EVO_PAD_FEED_MARK; e->b[4] = e->b[5] = 0;
    __asm__ __volatile__(".word 0xf57ff05b" ::: "memory");      /* dmb */
    g_feed->head = h + 1;
}

int main(int argc, char **argv)
{
    struct evo_pad_slot *s;
    int fd, i, keep = 0;

    if (argc < 2) { fprintf(stderr, "usage: %s <step>...   (see the header of evo_padfake.c)\n", argv[0]); return 2; }

    fd = open(EVO_PAD_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) { perror("open " EVO_PAD_PATH); return 1; }
    if (ftruncate(fd, sizeof(struct evo_pad)) != 0) { /* already sized */ }
    g_pad = mmap(0, sizeof(struct evo_pad), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g_pad == MAP_FAILED) { perror("mmap"); return 1; }
    if (g_pad->magic != EVO_PAD_MAGIC) { memset(g_pad, 0, sizeof *g_pad); g_pad->magic = EVO_PAD_MAGIC; }
    beat();

    fd = open(EVO_FEED_PATH, O_RDWR);
    if (fd >= 0) {
        void *p = mmap(0, sizeof(struct evo_feed), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p != MAP_FAILED) g_feed = p;
    }

    s = &g_pad->pad[0];
    if (!s->connected) {
        s->buttons = 0; s->lx = s->ly = s->rx = s->ry = 0; s->lt = s->rt = 0;
        s->vid = 0x045e; s->pid = 0x02ea; s->battery = 3;
        s->seq++;
        s->connected = 1;
        pause_ms(150);                              /* let apps notice the pad before the first press */
    }

    for (i = 1; i < argc; i++) {
        char tok[96], *a, *b;
        strncpy(tok, argv[i], sizeof tok - 1); tok[sizeof tok - 1] = 0;

        if (!strcmp(tok, "keep")) { keep = 1; continue; }
        if ((a = strchr(tok, '=')) != NULL) {       /* axis */
            int v = atoi(a + 1);
            *a = 0;
            if      (!strcmp(tok, "lx")) s->lx = v; else if (!strcmp(tok, "ly")) s->ly = v;
            else if (!strcmp(tok, "rx")) s->rx = v; else if (!strcmp(tok, "ry")) s->ry = v;
            else if (!strcmp(tok, "lt")) s->lt = (unsigned)v; else if (!strcmp(tok, "rt")) s->rt = (unsigned)v;
            else { fprintf(stderr, "padfake: unknown axis '%s'\n", tok); return 2; }
            s->seq++; beat();
            continue;
        }
        a = strchr(tok, ':');
        if (!a) { fprintf(stderr, "padfake: bad step '%s'\n", argv[i]); return 2; }
        *a++ = 0;
        if (!strcmp(tok, "wait")) { pause_ms(atoi(a)); continue; }
        if (!strcmp(tok, "ringcenter")) {
            ring_push(EVO_K_CENTER, 0, 1, 0); pause_ms(atoi(a)); ring_push(EVO_K_CENTER, 0, 0, 0);
            continue;
        }
        if (!strcmp(tok, "ring") || !strcmp(tok, "ringdir")) {
            int ms = 150;
            b = strchr(a, ':');
            if (b) { *b++ = 0; ms = atoi(b); }
            if (!strcmp(tok, "ring")) {
                unsigned char id = (unsigned char)strtol(a, NULL, 0);
                ring_push(EVO_K_BUTTON, id, 1, 0); pause_ms(ms); ring_push(EVO_K_BUTTON, id, 0, 0);
            } else {
                int d = !strcmp(a, "fwd") ? EVO_DIR_FWD : !strcmp(a, "back") ? EVO_DIR_BACK :
                        !strcmp(a, "left") ? EVO_DIR_LEFT : !strcmp(a, "right") ? EVO_DIR_RIGHT : -1;
                unsigned char code;
                if (d < 0) { fprintf(stderr, "padfake: bad direction '%s'\n", a); return 2; }
                code = d == EVO_DIR_FWD ? 0x01 : d == EVO_DIR_BACK ? 0x04 : d == EVO_DIR_LEFT ? 0x08 : 0x02;
                ring_push(EVO_K_DIR, (unsigned char)d, 1, code); pause_ms(ms); ring_push(EVO_K_DIR, (unsigned char)d, 0, code);
            }
            continue;
        }
        {                                           /* buttons: name+name:ms */
            unsigned mask = 0;
            char *n = strtok(tok, "+");
            while (n) {
                unsigned bit = button_bit(n);
                if (!bit) { fprintf(stderr, "padfake: unknown button '%s'\n", n); return 2; }
                mask |= bit;
                n = strtok(NULL, "+");
            }
            s->buttons |= mask; s->seq++; beat();
            pause_ms(atoi(a));
            s->buttons &= ~mask; s->seq++; beat();
            pause_ms(60);                           /* a gap, so the next press is a new press */
        }
    }

    if (!keep) {
        s->buttons = 0; s->lx = s->ly = s->rx = s->ry = 0; s->lt = s->rt = 0;
        s->seq++;
        s->connected = 0;
    }
    beat();
    return 0;
}
