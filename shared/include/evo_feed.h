/* evo_feed.h - shared iDrive-controller event ring between the DSI subscriber
 * (producer = evo_ctrlfeed) and DOOM (consumer = i_input_evo.c), backed by
 * /dev/shmem/evo_ctrl_feed. Lock-free SPSC ring: the producer fills ev[head%RING]
 * then bumps head; the consumer tracks its own tail and reads while tail < head.
 *
 * Transport note: the controller is NOT on OMAP /dev/ipc (that passive tap is
 * dead). It arrives as DSI MoCCA-over-TCP notifications from the Jacinto; the
 * producer subscribes like the HMI does (see evo_ctrlfeed.c / evo_dsitcp.c) and
 * translates each notification into one of the events below.
 */
#ifndef EVO_FEED_H
#define EVO_FEED_H
#include <time.h>

#define EVO_FEED_PATH  "/dev/shmem/evo_ctrl_feed"
#define EVO_FEED_RING  512u
#define EVO_FEED_MAGIC 0x0e5feed3u      /* v3: grab_until / tee_alive appended (v2 = DSI-era schema) */

/* event kinds (producer -> DOOM) */
#define EVO_K_ROTARY 1   /* (signed char)b[0] = detent delta; + = clockwise.      -> turn   */
#define EVO_K_DIR    2   /* b[0] = direction (EVO_DIR_*); b[1] = 1 down / 0 up.    -> move/strafe */
#define EVO_K_CENTER 3   /* b[1] = 1 down / 0 up (knob pressed straight down).     -> fire   */
#define EVO_K_BUTTON 4   /* b[0] = button id; b[1] = 1 down / 0 up. (spare: MENU/BACK/...)    */

/* 5-way directions for EVO_K_DIR.b[0] */
#define EVO_DIR_FWD   0  /* push forward (north) -> move forward   */
#define EVO_DIR_BACK  1  /* pull back   (south)  -> move backward  */
#define EVO_DIR_LEFT  2  /* push left   (west)   -> strafe left    */
#define EVO_DIR_RIGHT 3  /* push right  (east)   -> strafe right   */

/* kind=1 rotary: b[0]=signed delta, ctr=absolute detent counter (debug/robustness).
 * kind=2 dir:    b[0]=EVO_DIR_*,   b[1]=state (1 down, 0 up).
 * kind=3 center: b[1]=state.
 * kind=4 button: b[0]=id,          b[1]=state. */
struct evo_ev { unsigned char kind; unsigned char ctr; unsigned char b[6]; };

struct evo_feed {
    volatile unsigned int magic;   /* EVO_FEED_MAGIC once initialised */
    volatile unsigned int head;    /* producer write index (monotonic)  */
    struct evo_ev ev[EVO_FEED_RING];
    /* GRAB: while grab_until is in the future (evo_now_ms() clock) the producer (evo_tee
     * inside `most`) neutralises the controller telegrams for most/HMI in place, so the
     * BMW HMI sees no input while a consumer (DOOM) owns the controller. The consumer
     * must keep refreshing it (every <= 500 ms, TTL below): if it dies, the HMI gets the
     * controller back within EVO_GRAB_TTL_MS. 0 = released. */
    volatile unsigned int grab_until;
    volatile unsigned int tee_alive;   /* evo_now_ms() of the tee's last controller-channel read */
};
#define EVO_GRAB_TTL_MS 2000u

/* HMI INPUT INJECTION (knob) - the most-side tee (ipc25) synthesizes a controller
 * frame into a benign poll read so the HMI navigates. Host bumps seq to queue one. */
#define EVO_INJECT_PATH  "/dev/shmem/evo_inject"
#define EVO_INJECT_MAGIC 0x0e51c701u
/* kind: 1=rotary(arg=signed detents) 2=center press 3=tilt(arg=dir 0fwd1back2left3right) 4=button(arg=id bitmask) */
struct evo_inject { volatile unsigned int magic; volatile unsigned int seq; unsigned int kind; int arg; };
static inline unsigned int evo_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)((unsigned long)t.tv_sec * 1000u + (unsigned long)t.tv_nsec / 1000000u);
}
static inline int evo_grab_active(const struct evo_feed *f) {
    unsigned int g = f->grab_until;
    return g != 0 && (int)(g - evo_now_ms()) > 0;
}

#endif /* EVO_FEED_H */
