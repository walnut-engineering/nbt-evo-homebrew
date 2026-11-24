/* evo_hmistate.h - shared contract between evo_hmistate (the capture+signature daemon) and
 * any client (e.g. evo_launcher) that wants to know whether the OEM HMI is currently on the
 * main 6-tile Startmenu. The daemon owns the privileged screen_read_display path; the client
 * just mmaps this tiny struct, bumps req_seq, and reads is_startmenu once resp_seq catches up.
 * Decoupling the capture into a separate process keeps the heavy DISPLAY_MANAGER screen context
 * out of the launcher's ILM graphics process (avoids the posix_spawn/graphics wedge class of bug). */
#ifndef EVO_HMISTATE_H
#define EVO_HMISTATE_H

#define EVO_HMISTATE_PATH   "/dev/shmem/evo_hmistate"
#define EVO_HMISTATE_MAGIC  0x484d5331u   /* "HMS1" */

struct evo_hmistate {
    volatile unsigned int magic;        /* EVO_HMISTATE_MAGIC once the daemon has initialised */
    volatile unsigned int req_seq;      /* a client bumps this to request a fresh capture+check */
    volatile unsigned int resp_seq;     /* the daemon sets this = req_seq when the result is ready */
    volatile int          is_startmenu; /* 1 = on the main 6-tile Startmenu, 0 = anywhere else */
    volatile unsigned int last_ms;      /* ms timestamp of the last check (daemon clock) */
    volatile int          window[8];    /* per-label-window bright-pixel counts (debug/tuning) */
};

#endif /* EVO_HMISTATE_H */
