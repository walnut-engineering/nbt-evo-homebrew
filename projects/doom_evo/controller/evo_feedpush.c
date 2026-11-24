/* evo_feedpush - push synthetic controller events straight into the DOOM feed ring
 * (/dev/shmem/evo_ctrl_feed, evo_feed.h) so DOOM can be driven programmatically for a
 * demo. DOOM's input thread consumes this ring; we are a second producer, safe only when
 * nobody is turning the real knob (the tee is the normal producer).
 *   evo_feedpush turn <n>    rotary detents (+ clockwise = turn right)
 *   evo_feedpush fwd|back|left|right [ms]   5-way hold for <ms> (default 400), then release
 *   evo_feedpush fire [ms]   centre press hold
 *   evo_feedpush btn <id> [ms]   hard-button press/release (id 0x01=MENU 0x02=BACK 0x04=OPTION
 *                                0x08=MEDIA 0x10=NAV 0x20=TEL 0x40=COM); hex (0x..) or decimal
 */
#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "evo_feed.h"

static struct evo_feed *g_feed;
static void push(unsigned char kind, const unsigned char b[6]) {
    unsigned h = g_feed->head;
    struct evo_ev *e = &g_feed->ev[h % EVO_FEED_RING];
    e->kind = kind; e->ctr = (unsigned char)h;
    memcpy(e->b, b, 6);
    __asm__ __volatile__(".word 0xf57ff05b" ::: "memory");   /* dmb (not __sync_* - Linux helper is fatal on QNX) */
    g_feed->head = h + 1;
}
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: evo_feedpush turn <n>|fwd|back|left|right|fire [ms]\n"); return 2; }
    int fd = open(EVO_FEED_PATH, O_RDWR);
    if (fd < 0) { perror("open feed"); return 1; }
    g_feed = mmap(0, sizeof(*g_feed), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g_feed == MAP_FAILED) { perror("mmap"); return 1; }
    if (g_feed->magic != EVO_FEED_MAGIC) { fprintf(stderr, "feed not ready (magic %08x)\n", g_feed->magic); return 1; }
    unsigned char b[6];
    if (!strcmp(argv[1], "turn")) {
        int n = argc > 2 ? atoi(argv[2]) : 1, step = n < 0 ? -1 : 1, k, an = n < 0 ? -n : n;
        for (k = 0; k < an; k++) { memset(b, 0, 6); b[0] = (unsigned char)(signed char)step; push(EVO_K_ROTARY, b); usleep(25000); }
    } else if (!strcmp(argv[1], "fire")) {
        int ms = argc > 2 ? atoi(argv[2]) : 250;
        memset(b, 0, 6); b[1] = 1; push(EVO_K_CENTER, b); usleep(ms * 1000);
        memset(b, 0, 6); b[1] = 0; push(EVO_K_CENTER, b);
    } else if (!strcmp(argv[1], "btn")) {
        if (argc < 3) { fprintf(stderr, "usage: evo_feedpush btn <id> [ms]\n"); return 2; }
        int id = (int)strtol(argv[2], NULL, 0);          /* 0x.. hex or decimal */
        int ms = argc > 3 ? atoi(argv[3]) : 150;
        memset(b, 0, 6); b[0] = (unsigned char)id; b[1] = 1; push(EVO_K_BUTTON, b); usleep(ms * 1000);
        memset(b, 0, 6); b[0] = (unsigned char)id; b[1] = 0; push(EVO_K_BUTTON, b);
    } else {
        int dir = !strcmp(argv[1],"fwd")?EVO_DIR_FWD: !strcmp(argv[1],"back")?EVO_DIR_BACK:
                  !strcmp(argv[1],"left")?EVO_DIR_LEFT: !strcmp(argv[1],"right")?EVO_DIR_RIGHT: -1;
        if (dir < 0) { fprintf(stderr, "bad dir %s\n", argv[1]); return 2; }
        int ms = argc > 2 ? atoi(argv[2]) : 400;
        memset(b, 0, 6); b[0] = (unsigned char)dir; b[1] = 1; push(EVO_K_DIR, b); usleep(ms * 1000);
        memset(b, 0, 6); b[0] = (unsigned char)dir; b[1] = 0; push(EVO_K_DIR, b);
    }
    return 0;
}
