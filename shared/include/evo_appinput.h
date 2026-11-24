/* evo_appinput.h - minimal iDrive-knob input for full-screen homebrew apps.
 *
 * A header-only helper that any homebrew app (terminal, sysmon, future tiles)
 * can include to get ONE behaviour the launcher vision needs everywhere:
 *
 *     the iDrive BACK button quits the app and returns to the OEM HMI,
 *     with NO keyboard required (important in the car).
 *
 * It spawns a background thread that:
 *   - holds the controller GRAB while the app is up (grab_until refreshed every
 *     ~400 ms) so the hidden OEM HMI ignores the knob - exactly what DOOM does;
 *   - watches the shared feed ring and clears the caller's run-flag (sets *run=0)
 *     when the user presses BACK (iDrive button id 0x02) - the single app-exit gesture.
 *     MENU (0x01) is intentionally untouched (reserved by the Android unit video switch);
 *   - releases the grab on exit so the HMI gets the controller straight back.
 *
 * The feed ring + event schema mirror doom_evo/controller/evo_feed.h (v3). This
 * header redefines them locally (self-contained) so an app needs only:
 *     #include "evo_appinput.h"   ... link -lpthread ... evo_ai_start(&g_running);
 * where g_running is the app's own `volatile int` main-loop flag (1 = run).
 *
 * The producer of the ring is the `most` tee (real iDrive knob) or evo_feedpush
 * (synthetic events, for host-driven testing). If the ring is absent the thread
 * simply idles - the app still runs, it just has no knob exit (keyboard only).
 */
#ifndef EVO_APPINPUT_H
#define EVO_APPINPUT_H

#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>

#define EVO_AI_FEED_PATH "/dev/shmem/evo_ctrl_feed"
#define EVO_AI_RING      512u
#define EVO_AI_MAGIC     0x0e5feed3u
#define EVO_AI_K_BUTTON  4          /* evo_feed.h EVO_K_BUTTON */
#define EVO_AI_BTN_BACK  0x02       /* iDrive BACK button id - the ONLY app-exit button */
#define EVO_AI_GRAB_TTL  2000u      /* ms; must refresh faster than this */
/* NB: MENU (0x01) is deliberately NOT handled here.  MENU-longpress is reserved by the
 * dual-screen Android head unit to switch video between the BMW HMI and the Android unit;
 * if our apps also quit on a MENU hold the two actions collide.  BACK is the only exit. */

struct evo_ai_ev   { unsigned char kind, ctr, b[6]; };
struct evo_ai_feed { volatile unsigned int magic, head;
                     struct evo_ai_ev ev[EVO_AI_RING];
                     volatile unsigned int grab_until, tee_alive; };

static pthread_t         evo_ai_tid;
static volatile int      evo_ai_started = 0;

static unsigned int evo_ai_now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)((unsigned long)t.tv_sec * 1000u + (unsigned long)t.tv_nsec / 1000000u);
}

/* thread arg = volatile int *run  (1 = keep running; thread sets it to 0 to quit) */
static void *evo_ai_thread(void *arg) {
    volatile int *run = (volatile int *)arg;
    struct evo_ai_feed *f = NULL;
    unsigned int tail = 0, last_grab = 0;
    int fd = open(EVO_AI_FEED_PATH, O_RDWR);
    if (fd >= 0) {
        f = (struct evo_ai_feed *)mmap(0, sizeof(*f), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (f == MAP_FAILED) f = NULL;
    }
    if (f) tail = f->head;                 /* only react to events from now on */

    while (*run) {
        unsigned int now = evo_ai_now_ms();
        if (f) {
            if ((int)(now - last_grab) > 400) { f->grab_until = now + EVO_AI_GRAB_TTL; last_grab = now; }
            unsigned int h = f->head;
            while (tail != h) {
                struct evo_ai_ev *e = &f->ev[tail % EVO_AI_RING];
                if (e->kind == EVO_AI_K_BUTTON && e->b[0] == EVO_AI_BTN_BACK && e->b[1])
                    *run = 0;                 /* BACK press = quit (the ONLY exit; MENU is reserved) */
                tail++;
            }
        }
        usleep(30000);                     /* ~33 Hz */
    }
    if (f) { f->grab_until = 0; munmap((void *)f, sizeof(*f)); }  /* controller back to the HMI */
    return NULL;
}

/* Start the knob thread. run_flag is the app's main-loop flag (1 = run); BACK/MENU
 * set it to 0. Returns 0 on success (pthread_create rc otherwise). Safe to call once. */
static int evo_ai_start(volatile int *run_flag) {
    if (evo_ai_started) return 0;
    int rc = pthread_create(&evo_ai_tid, NULL, evo_ai_thread, (void *)run_flag);
    if (rc == 0) evo_ai_started = 1;
    return rc;
}

#endif /* EVO_APPINPUT_H */
