/* evo_feedtap - read the controller feed ring published by evo_tee.so and print
 * events live.  Doubles as the reference consumer for wiring DOOM: open the shm,
 * mmap it, track a private tail, and drain ev[tail % RING] while tail < head.
 *
 *   evo_feedtap [secs]      (default 10)  - show NEW events for <secs> seconds
 *   evo_feedtap 0           - just print magic/head and exit (baseline probe)
 *   evo_feedtap <secs> grab - same, while HOLDING THE GRAB (HMI muted) - tests the
 *                             masking without DOOM; released on exit / lapses in 2 s
 */
#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "evo_feed.h"

int main(int argc, char **argv) {
    int secs = (argc > 1) ? atoi(argv[1]) : 10;
    int grab = (argc > 2) && argv[2][0] == 'g';
    int fd = open(EVO_FEED_PATH, O_RDWR);
    if (fd < 0) { perror("open feed"); return 1; }
    struct evo_feed *f = mmap(0, sizeof(*f), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (f == MAP_FAILED) { perror("mmap"); return 1; }

    printf("magic=%08x head=%u ring=%u (evo_ev=%u bytes, feed=%u bytes)\n",
           f->magic, f->head, (unsigned)EVO_FEED_RING,
           (unsigned)sizeof(struct evo_ev), (unsigned)sizeof(struct evo_feed));
    fflush(stdout);
    printf("grab_until=%u now=%u (%s) tee_alive %d ms ago\n", f->grab_until, evo_now_ms(),
           evo_grab_active(f) ? "ACTIVE" : "released", (int)(evo_now_ms() - f->tee_alive));
    if (secs <= 0) return (f->magic == EVO_FEED_MAGIC) ? 0 : 2;
    if (grab) { f->grab_until = evo_now_ms() + EVO_GRAB_TTL_MS; printf("GRAB held: the HMI should ignore the controller now\n"); fflush(stdout); }

    unsigned int tail = f->head;              /* only show events from now on */
    unsigned long rot = 0, btn = 0, t30 = 0, n = 0;
    int prevctr = -1;
    time_t t0 = time(0);
    while (time(0) - t0 < secs) {
        unsigned int h = f->head;
        while (tail != h) {                    /* unsigned monotonic head */
            struct evo_ev *e = &f->ev[tail % EVO_FEED_RING];
            const char *k = e->kind == 1 ? "ROTARY" :
                            e->kind == 2 ? "DIR" :
                            e->kind == 3 ? "CENTER" :
                            e->kind == 4 ? "BUTTON" : "????";
            int d = (int)(signed char)e->b[0];          /* rotary: signed detent delta */
            static const char *dn[4] = {"FWD", "BACK", "LEFT", "RIGHT"};
            (void)prevctr;
            if (e->kind == 1)      printf("[%lu] ROTARY %+d (pos %02x%02x) seq=%u\n", (unsigned long)tail, d, e->b[1], e->b[2], e->ctr);
            else if (e->kind == 2) printf("[%lu] DIR %s %s\n", (unsigned long)tail, e->b[0] < 4 ? dn[e->b[0]] : "?", e->b[1] ? "down" : "up");
            else if (e->kind == 3) printf("[%lu] CENTER %s\n", (unsigned long)tail, e->b[1] ? "down" : "up");
            else printf("[%lu] %s id=%02x %s  %02x %02x %02x %02x %02x %02x\n", (unsigned long)tail, k, e->b[0], e->b[1] ? "down" : "up",
                        e->b[0], e->b[1], e->b[2], e->b[3], e->b[4], e->b[5]);
            fflush(stdout);
            if (e->kind == 1) rot++; else if (e->kind == 4) btn++; else t30++;
            tail++; n++;
        }
        usleep(15000);
        if (grab) f->grab_until = evo_now_ms() + EVO_GRAB_TTL_MS;
    }
    if (grab) { f->grab_until = 0; printf("GRAB released\n"); }
    printf("TOTAL new=%lu  rotary=%lu button=%lu dir+center=%lu  final head=%u\n",
           n, rot, btn, t30, f->head);
    return 0;
}
