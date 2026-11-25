/*
 * DOOM EVO Input Implementation
 * 
 * This implementation solves the blocking USB input issue by using a background
 * thread to handle device polling, while the main game loop runs uninterrupted.
 * 
 * Key features:
 * - Dedicated input thread to prevent blocking main game loop
 * - Lock-free queue for thread-safe communication
 * - Automatic retry on device disconnection
 * - Proper resource cleanup on shutdown
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <stdint.h>
#include <sys/neutrino.h>

// devu-kbd / devu-mouse ignore O_NONBLOCK, don't support select()/readcond(), and a thread
// REPLY-blocked in their read() keeps the whole process from exiting (QNX waits for it;
// even SIGKILL only lands once a key/mouse event arrives). A kernel-side TimerTimeout
// bounds the block (bench-tested: returns EINTR after the timeout), so the threads can
// poll shutdown_requested and the game can always quit.
#define EVO_INPUT_READ_TIMEOUT_MS 200
static ssize_t bounded_read(int fd, void *buf, size_t n) {
    uint64_t to = (uint64_t)EVO_INPUT_READ_TIMEOUT_MS * 1000000ULL;
    TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_REPLY | _NTO_TIMEOUT_SEND, NULL, &to, NULL);
    return read(fd, buf, n);
}
#include <sys/time.h>
#include <stdarg.h>

// DOOM includes
#include "doomdef.h"
#include "doomstat.h"      // players[] / consoleplayer (weapon cycling on the iDrive controller)
#include "d_event.h"
#include "doomkeys.h"
#include "i_system.h"
#include "m_argv.h"

// EVO device paths
#define EVO_KEYBOARD_DEVICE "/dev/usbkbd0"
#define EVO_MOUSE_DEVICE "/dev/usbmouse0"

// Input event queue
#define INPUT_EVENT_QUEUE_SIZE 64

typedef struct {
    event_t events[INPUT_EVENT_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
} input_event_queue_t;

// Input context
typedef struct {
    int keyboard_fd;
    int mouse_fd;
    int initialized;
    int debug_enabled;
    int shutdown_requested;
    
    // Threading: one blocking reader thread per device (devu-kbd and
    // devu-mouse ignore O_NONBLOCK, so a single thread would stall on
    // whichever device is idle)
    pthread_t input_thread;
    pthread_t mouse_thread;
    int mouse_thread_running;
    pthread_t controller_thread;      // iDrive controller via io-ipc (ipc25)
    int controller_thread_running;
    int mouse_packet_len;
    int mouse_y_invert;      // -mouseinvert: flip forward/back
    input_event_queue_t event_queue;
    
    // Device retry logic
    int keyboard_retry_count;
    int mouse_retry_count;
    time_t last_keyboard_retry_time;
    time_t last_mouse_retry_time;
    
    // Input filtering to ignore phantom keypresses
    time_t init_time;
    int input_ignore_period_ms;
} qnx_input_context_t;

static qnx_input_context_t g_input_ctx = {
    .keyboard_fd = -1,
    .mouse_fd = -1,
    .initialized = 0,
    .debug_enabled = 1,  // Enable debug to see raw key codes
    .shutdown_requested = 0,
    .keyboard_retry_count = 0,
    .mouse_retry_count = 0,
    .last_keyboard_retry_time = 0,
    .last_mouse_retry_time = 0,
    .init_time = 0,
    .input_ignore_period_ms = 100  // Reduce ignore period to 100ms for testing
};

// Key mapping from QNX to DOOM
static int qnx_to_doom_key(unsigned char qnx_key) {
    switch (qnx_key) {
        case 0x1C: return KEY_ENTER;       // Enter
        case 0x01: return KEY_ESCAPE;      // Escape
        case 0x2A: return KEY_RSHIFT;      // Left Shift
        case 0x36: return KEY_RSHIFT;      // Right Shift (map to same as left)
        case 0x1D: return KEY_RCTRL;       // Ctrl
        case 0x9D: return KEY_RCTRL;       // Ctrl (key up)
        case 0x38: return KEY_RALT;        // Alt
        case 0x0E: return KEY_BACKSPACE;   // Backspace
        case 0x0F: return KEY_TAB;         // Tab
        case 0x39: return ' ';             // Space
        case 0x11: return 'w';             // W key - return actual character
        case 0x1F: return 's';             // S key - return actual character
        case 0x1E: return 'a';             // A key - return actual character
        case 0x20: return 'd';             // D key - return actual character
        case 0x48: return KEY_UPARROW;     // Up Arrow
        case 0x50: return KEY_DOWNARROW;   // Down Arrow
        case 0x4B: return KEY_LEFTARROW;   // Left Arrow
        case 0x4D: return KEY_RIGHTARROW;  // Right Arrow
        
        // Q-P row (0x10-0x19) - CORRECTED MAPPING
        case 0x10: return 'q';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';
        // W (0x11), A (0x1E), S (0x1F), D (0x20) handled above as movement
        
        // A-L row (0x1E-0x26) - CORRECTED MAPPING
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        // A (0x1E), S (0x1F), D (0x20) handled above as movement
        
        // Z-M row (0x2C-0x32) - CORRECTED MAPPING
        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
        
        default:
            // Numbers 1-9, 0
            if (qnx_key >= 0x02 && qnx_key <= 0x0A) {
                return '1' + (qnx_key - 0x02);  // Numbers 1-9
            }
            if (qnx_key == 0x0B) return '0';    // 0 key
            
            return 0;  // Unknown key
    }
}

// Initialize event queue
static void init_event_queue(input_event_queue_t *queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    pthread_mutex_init(&queue->mutex, NULL);
}

// Add event to queue (thread-safe)
static void queue_event(input_event_queue_t *queue, event_t *event) {
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->count < INPUT_EVENT_QUEUE_SIZE) {
        queue->events[queue->tail] = *event;
        queue->tail = (queue->tail + 1) % INPUT_EVENT_QUEUE_SIZE;
        queue->count++;
        
        if (g_input_ctx.debug_enabled) {
            printf("DEBUG: Queued event type=%d, data1=%d (queue count=%d)\n", 
                   event->type, event->data1, queue->count);
        }
    } else {
        if (g_input_ctx.debug_enabled) {
            printf("DEBUG: Event queue full, dropping event\n");
        }
    }
    
    pthread_mutex_unlock(&queue->mutex);
}

// Get event from queue (thread-safe)
static int dequeue_event(input_event_queue_t *queue, event_t *event) {
    int result = 0;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->count > 0) {
        *event = queue->events[queue->head];
        queue->head = (queue->head + 1) % INPUT_EVENT_QUEUE_SIZE;
        queue->count--;
        result = 1;
        
        if (g_input_ctx.debug_enabled) {
            printf("DEBUG: Dequeued event type=%d, data1=%d (queue count=%d)\n", 
                   event->type, event->data1, queue->count);
        }
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    return result;
}

// Try to open device with retry logic
static int try_open_device(const char *device_path, int *retry_count) {
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    
    if (fd >= 0) {
        // Success - reset retry count
        *retry_count = 0;
        
        // Ensure non-blocking mode is set
        int flags = fcntl(fd, F_GETFL);
        if (flags >= 0) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
        
        printf("INPUT: Opened %s successfully (fd=%d)\n", device_path, fd);
        return fd;
    } else {
        (*retry_count)++;
        if (*retry_count <= 3) {  // Only log first few attempts
            printf("INPUT: Failed to open %s (attempt %d): %s\n", 
                   device_path, *retry_count, strerror(errno));
        }
        return -1;
    }
}

// Check if we should ignore input during initial period (to avoid phantom keypresses)
static int should_ignore_input(void) {
    if (g_input_ctx.init_time == 0) {
        return 0;  // Not initialized yet
    }
    
    time_t current_time = time(NULL);
    long elapsed_ms = (current_time - g_input_ctx.init_time) * 1000;
    
    if (elapsed_ms < g_input_ctx.input_ignore_period_ms) {
        if (g_input_ctx.debug_enabled) {
            printf("INPUT: Ignoring input during initial %dms period (elapsed: %ldms)\n", 
                   g_input_ctx.input_ignore_period_ms, elapsed_ms);
        }
        return 1;  // Ignore input
    }
    
    return 0;  // Accept input
}
// Process keyboard input in background thread
static void process_keyboard_input(void) {
    unsigned char buffer[32];
    ssize_t bytes_read;
    
    // Try to open keyboard if not available (only if not already open)
    if (g_input_ctx.keyboard_fd < 0) {
        time_t current_time = time(NULL);
        if (current_time - g_input_ctx.last_keyboard_retry_time > 5) {  // Retry every 5 seconds
            g_input_ctx.keyboard_fd = try_open_device(EVO_KEYBOARD_DEVICE, &g_input_ctx.keyboard_retry_count);
            g_input_ctx.last_keyboard_retry_time = current_time;
        }
        return;  // If device not available, nothing to process
    }
    
    // Read from keyboard device
    errno = 0;
    bytes_read = bounded_read(g_input_ctx.keyboard_fd, buffer, sizeof(buffer));
    
    if (bytes_read > 0) {
        // Check if we should ignore input during initial period
        if (should_ignore_input()) {
            if (g_input_ctx.debug_enabled) {
                printf("INPUT: Ignoring %zd bytes of keyboard input during initial period\n", bytes_read);
            }
            return;
        }
        
        for (ssize_t i = 0; i < bytes_read; i++) {
            unsigned char scancode = buffer[i];
            int doom_key = qnx_to_doom_key(scancode & 0x7F);
            
            if (doom_key > 0) {
                event_t event;
                event.type = (scancode & 0x80) ? ev_keyup : ev_keydown;
                event.data1 = doom_key;
                event.data2 = 0;
                event.data3 = 0;
                
                queue_event(&g_input_ctx.event_queue, &event);
                
                // Only log important movement keys when debug enabled
                if (g_input_ctx.debug_enabled && 
                    (doom_key == KEY_UPARROW || doom_key == KEY_DOWNARROW || 
                     doom_key == KEY_LEFTARROW || doom_key == KEY_RIGHTARROW)) {
                    printf("INPUT: Keyboard scancode=0x%02X, doom_key=%d, %s\n", 
                           scancode, doom_key, (scancode & 0x80) ? "UP" : "DOWN");
                }
            }
        }
    } else if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {   // EINTR = bounded_read timeout
            // Device error - close and retry
            printf("INPUT: Keyboard read error: %s (errno=%d) - closing device\n", strerror(errno), errno);
            close(g_input_ctx.keyboard_fd);
            g_input_ctx.keyboard_fd = -1;
        }
        // For EAGAIN/EWOULDBLOCK, just continue - this is normal for non-blocking reads
    }
}

// Process mouse input in background thread (blocking read on /dev/usbmouse0).
// devu-mouse delivers HID boot-protocol packets: buttons, dx, dy[, wheel].
// Events use vanilla DOOM semantics: one ev_mouse per packet with the button
// mask in data1 and the motion in data2/data3 (right and up positive, scaled
// by 4 like the original X11 driver), so G_Responder's mousebuttons[] and
// mousex/mousey handling work unchanged.
static void process_mouse_input(void) {
    unsigned char buffer[64];
    ssize_t bytes_read;
    static int dumps = 0;

    if (g_input_ctx.mouse_fd < 0) {
        time_t current_time = time(NULL);
        if (current_time - g_input_ctx.last_mouse_retry_time > 5) {
            g_input_ctx.mouse_fd = try_open_device(EVO_MOUSE_DEVICE, &g_input_ctx.mouse_retry_count);
            g_input_ctx.last_mouse_retry_time = current_time;
        }
        sleep(1);
        return;
    }

    errno = 0;
    bytes_read = bounded_read(g_input_ctx.mouse_fd, buffer, sizeof(buffer));
    if (bytes_read <= 0) {
        if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            printf("INPUT: Mouse read error: %s (errno=%d) - closing device\n", strerror(errno), errno);
            close(g_input_ctx.mouse_fd);
            g_input_ctx.mouse_fd = -1;
        }
        return;
    }

    if (dumps < 3) {
        int i;
        dumps++;
        printf("INPUT: mouse raw (%d bytes):", (int)bytes_read);
        for (i = 0; i < bytes_read && i < 16; i++) printf(" %02x", buffer[i]);
        printf("\n");
    }

    // Packet length: prefer 3 (buttons, dx, dy); use 4 when the read size only
    // fits 4-byte packets (extra wheel byte)
    if (g_input_ctx.mouse_packet_len == 0) {
        if (bytes_read % 3 == 0) g_input_ctx.mouse_packet_len = 3;
        else if (bytes_read % 4 == 0) g_input_ctx.mouse_packet_len = 4;
        else g_input_ctx.mouse_packet_len = 3;
        printf("INPUT: mouse packet length %d\n", g_input_ctx.mouse_packet_len);
    }

    if (should_ignore_input()) {
        return;
    }

    {
        // devu-mouse packet (observed on the head unit): byte0 = 0x40 | flags
        // with bit0 = left button, bit1 = right button, bit2 = X negative,
        // bit3 = Y negative; byte1/byte2 = |dx| and |dy| magnitudes (0..63).
        int len = g_input_ctx.mouse_packet_len;
        ssize_t off;
        static int decoded_dumps = 0;
        for (off = 0; off + len <= bytes_read; off += len) {
            unsigned char flags = buffer[off];
            int dx = buffer[off + 1];
            int dy = buffer[off + 2];
            event_t event;
            if (flags & 0x04) dx = -dx;
            if (flags & 0x08) dy = -dy;
            if (g_input_ctx.mouse_y_invert) dy = -dy;
            event.type = ev_mouse;
            event.data1 = (flags & 1) | (flags & 2);
            event.data2 = dx << 2;
            event.data3 = dy << 2;
            if (decoded_dumps < 20) {
                decoded_dumps++;
                printf("INPUT: mouse flags=0x%02x dx=%d dy=%d buttons=%d\n", flags, dx, dy, event.data1);
            }
            queue_event(&g_input_ctx.event_queue, &event);
        }
    }
}

// Mouse reader thread: blocks in read(), so it lives on its own thread
static void *mouse_thread_func(void *arg) {
    (void)arg;
    printf("INPUT: Mouse thread started\n");
    while (!g_input_ctx.shutdown_requested) {
        process_mouse_input();
    }
    printf("INPUT: Mouse thread exiting\n");
    return NULL;
}

// ---- iDrive controller via io-ipc broadcast on /dev/ipc/ipc25 ----
// The V850 (IOC/CAN gateway) streams controller events to the OMAP over io-ipc;
// a passive second reader gets a copy. The data only flows on ipc25 when all
// four channels are held open, so we open ipc25..28. Each frame is a 40-byte
// io-ipc transport header followed by a BMW "MOST-logical" payload:
//   ROTARY: e5 01 20 .. 1d <ctr>   (ctr = detent counter; delta = rotation)
//   center press burst: 05 00 a0 .. ff ff ff
// Rotary -> ev_mouse horizontal (analog turn). Other frames are logged to
// /dev/shmem/evo_ctrl.log so the remaining buttons/tilts can be mapped live.
// ---- iDrive controller -> DOOM --------------------------------------------
// The controller is NOT readable on OMAP /dev/ipc (that passive tap is dead).
// It arrives as DSI MoCCA-over-TCP notifications from the Jacinto; the helper
// daemon `evo_ctrlfeed` subscribes to the controller service and translates each
// notification into a compact event in the SPSC ring /dev/shmem/evo_ctrl_feed.
// This thread is the CONSUMER: it mmaps that ring and maps events to DOOM:
//   rotary       -> ev_mouse horizontal  (rotate the player / turn)
//   5-way fwd/back -> UP/DOWN arrow       (move forward / backward)
//   5-way left/right -> strafe keys , / . (strafe left / right)
//   centre press -> RCTRL                 (fire; held = continuous fire)
// The ring schema is the canonical contract in controller/evo_feed.h; it is
// duplicated here (not in the source/ include path) and MUST stay in sync.
#define EVO_FEED_PATH  "/dev/shmem/evo_ctrl_feed"
#define EVO_FEED_RING  512u
#define EVO_FEED_MAGIC 0x0e5feed3u
#define EVO_K_ROTARY 1
#define EVO_K_DIR    2
#define EVO_K_CENTER 3
#define EVO_K_BUTTON 4
#define EVO_DIR_FWD   0
#define EVO_DIR_BACK  1
#define EVO_DIR_LEFT  2
#define EVO_DIR_RIGHT 3
struct evo_ev   { unsigned char kind; unsigned char ctr; unsigned char b[6]; };
struct evo_feed { volatile unsigned int magic; volatile unsigned int head;
                  struct evo_ev ev[EVO_FEED_RING];
                  volatile unsigned int grab_until;   /* see controller/evo_feed.h: HMI muted while in the future */
                  volatile unsigned int tee_alive; };
#define EVO_GRAB_TTL_MS 2000u
static unsigned int evo_now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)((unsigned long)t.tv_sec * 1000u + (unsigned long)t.tv_nsec / 1000000u);
}
void I_Quit(void);

static int g_ctrl_turn_scale = 170; // detent delta * this -> mouse dx per flush (env DOOM_CTRL_TURN); vanilla keeps
                                    // ONE mouse event per tic, so detents are accumulated and flushed every 25 ms
#define EVO_TILT_MIN_HOLD_MS 350     // a tilt tap holds its move key at least this long (a tap is ~150 ms)
static volatile int g_ctrl_quit = 0; // set by the controller thread (BACK pressed); I_Quit runs on the MAIN thread
extern int key_up, key_down, key_strafeleft, key_straferight, key_fire, key_use, key_speed;   // this port's bindings
extern boolean menuactive;      // m_menu.c: DOOM menu open  -> controller drives the menu
extern int     messageToPrint;  // m_menu.c: a prompt (e.g. "quit? y/n") is showing
#define EVO_TAP_MS 60           // a "tap" holds its key this long: G_BuildTiccmd samples keys once per tic (~29 ms)
#define EVO_TAPS   16
static int g_tap_key[EVO_TAPS]; static unsigned int g_tap_at[EVO_TAPS];

// post a key event to DOOM's queue
static void ctrl_post_key(int down, int key) {
    event_t e; memset(&e, 0, sizeof e);
    e.type = down ? ev_keydown : ev_keyup; e.data1 = key;
    queue_event(&g_input_ctx.event_queue, &e);
}
// tap = keydown now, keyup after EVO_TAP_MS (serviced in the controller loop)
static void ctrl_tap_key(int key) {
    int i;
    if (!key) return;
    for (i = 0; i < EVO_TAPS; i++) if (g_tap_key[i] == key) return;          // already held by a tap
    for (i = 0; i < EVO_TAPS; i++) if (!g_tap_key[i]) { g_tap_key[i] = key; g_tap_at[i] = evo_now_ms() + EVO_TAP_MS; ctrl_post_key(1, key); return; }
}
static void ctrl_service_taps(unsigned int now) {
    int i;
    for (i = 0; i < EVO_TAPS; i++) if (g_tap_key[i] && (int)(now - g_tap_at[i]) >= 0) { ctrl_post_key(0, g_tap_key[i]); g_tap_key[i] = 0; }
}
// NAV / TEL: next / previous OWNED weapon via the digit keys (fist+chainsaw share '1')
static int weapon_digit(int w) {
    switch (w) { case wp_fist: case wp_chainsaw: return '1'; case wp_pistol: return '2'; case wp_shotgun: return '3';
                 case wp_chaingun: return '4'; case wp_missile: return '5'; case wp_plasma: return '6'; case wp_bfg: return '7'; }
    return 0;
}
static void ctrl_weapon_cycle(int step) {
    player_t *pl = &players[consoleplayer]; int w = pl->readyweapon, i;
    for (i = 1; i < NUMWEAPONS; i++) {
        int c = (w + step * i + 2 * NUMWEAPONS) % NUMWEAPONS;
        if (pl->weaponowned[c] && weapon_digit(c) != weapon_digit(w)) { ctrl_tap_key(weapon_digit(c)); return; }
    }
}
// map a 5-way direction to its DOOM key (vanilla defaults: strafe = ',' '.')
static int ctrl_dir_key(int dir) {
    switch (dir) {
        case EVO_DIR_FWD:   return key_up;            // 'w' in this port (not the arrow keys)
        case EVO_DIR_BACK:  return key_down;          // 's'
        case EVO_DIR_LEFT:  return key_strafeleft;    // 'a'
        case EVO_DIR_RIGHT: return key_straferight;   // 'd'
    }
    return 0;
}

// ============================================================================
// GAMEPAD (evo_padd): twin-stick controls, read straight from the pad-state block
// (shared/include/evo_pad.h) instead of the knob events the daemon mirrors into
// the ring - those can only say "one direction" and "one detent".
//
//   game   left stick / D-pad  move + strafe (diagonals)     right stick  turn (analogue)
//          RT or X  fire        A or B  use / open            LT  walk (runs by default)
//          LB / RB  previous / next weapon                    Y or View  automap
//          Menu  DOOM menu      Xbox  tap = menu, hold = quit to the launcher
//   menu   stick / D-pad  move (repeats)   A  select ('y' on a prompt)
//          B  back one level ('n' on a prompt)   Menu / Xbox tap  close
// ============================================================================
#include "evo_pad.h"
#define PAD_DEAD_MOVE     12000   // stick travel before it counts as a direction
#define PAD_DEAD_TURN     6000
#define PAD_TURN_MAX      190     // ev_mouse dx per tic at full deflection (about the keyboard's fast turn)
#define PAD_TRIGGER_ON    300     // of 1023
#define PAD_QUIT_HOLD_MS  800
#define PAD_REPEAT_FIRST_MS 350
#define PAD_REPEAT_MS     140

static const struct evo_pad *g_pad_blk = NULL;
static unsigned int g_pad_retry = 0;
static int g_pad_on = 0;                 // a pad is driving DOOM: its mirrored ring events are skipped
static struct {
    unsigned int btn;                    // buttons at the previous poll
    int key[4];                          // move keys held: EVO_DIR_FWD, BACK, LEFT, RIGHT
    int fire, use, run;
    unsigned int guide_ms;               // Xbox button down since (0 = not down)
    unsigned int turn_ms;                // last turn event
    int nav_key; unsigned int nav_ms;    // menu direction being repeated
} g_pd;

static void pad_hold(int *held, int want, int key) {
    if (want && !*held)      { ctrl_post_key(1, key); *held = 1; }
    else if (!want && *held) { ctrl_post_key(0, key); *held = 0; }
}
static void pad_release_game(void) {
    int d;
    for (d = 0; d < 4; d++) pad_hold(&g_pd.key[d], 0, ctrl_dir_key(d));
    pad_hold(&g_pd.fire, 0, key_fire);
    pad_hold(&g_pd.use, 0, key_use);
    pad_hold(&g_pd.run, 0, key_speed);
}

static void ctrl_service_pad(unsigned int now, int in_menu) {
    unsigned int b, pressed, released;
    int lx, ly, rx, lt, rt, want[4], d;

    if (!g_pad_blk) {                               // the daemon may start after DOOM
        if (g_pad_retry && (int)(now - g_pad_retry) < 1000) return;
        g_pad_retry = now | 1u;
        g_pad_blk = evo_pad_open();
        if (!g_pad_blk) return;
    }
    if (!evo_pad_alive(g_pad_blk) || !g_pad_blk->pad[0].connected) {
        if (g_pad_on) { pad_release_game(); g_pad_on = 0; }
        return;
    }
    b = g_pad_blk->pad[0].buttons;
    lx = g_pad_blk->pad[0].lx; ly = g_pad_blk->pad[0].ly; rx = g_pad_blk->pad[0].rx;
    lt = (int)g_pad_blk->pad[0].lt; rt = (int)g_pad_blk->pad[0].rt;
    if (!g_pad_on) {                                // just appeared: what is held now is not a new press
        memset(&g_pd, 0, sizeof g_pd);
        g_pd.btn = b;
        g_pad_on = 1;
    }
    pressed = b & ~g_pd.btn; released = g_pd.btn & ~b; g_pd.btn = b;

    // Xbox button: hold = leave DOOM, tap = menu
    if (pressed & EVO_PAD_GUIDE) g_pd.guide_ms = now | 1u;
    if (g_pd.guide_ms && (b & EVO_PAD_GUIDE) && (int)(now - g_pd.guide_ms) >= PAD_QUIT_HOLD_MS) {
        g_pd.guide_ms = 0;
        pad_release_game();
        g_ctrl_quit = 1;                            // I_Quit runs on the main thread
        return;
    }
    if (released & EVO_PAD_GUIDE) { if (g_pd.guide_ms) ctrl_tap_key(KEY_ESCAPE); g_pd.guide_ms = 0; }
    if (pressed & EVO_PAD_MENU) ctrl_tap_key(KEY_ESCAPE);

    want[EVO_DIR_FWD]   = (b & EVO_PAD_UP)    || ly >  PAD_DEAD_MOVE;
    want[EVO_DIR_BACK]  = (b & EVO_PAD_DOWN)  || ly < -PAD_DEAD_MOVE;
    want[EVO_DIR_LEFT]  = (b & EVO_PAD_LEFT)  || lx < -PAD_DEAD_MOVE;
    want[EVO_DIR_RIGHT] = (b & EVO_PAD_RIGHT) || lx >  PAD_DEAD_MOVE;

    if (in_menu) {
        int nav = want[EVO_DIR_FWD] ? KEY_UPARROW : want[EVO_DIR_BACK] ? KEY_DOWNARROW :
                  want[EVO_DIR_LEFT] ? KEY_LEFTARROW : want[EVO_DIR_RIGHT] ? KEY_RIGHTARROW : 0;
        pad_release_game();
        if (nav != g_pd.nav_key) { g_pd.nav_key = nav; if (nav) { ctrl_tap_key(nav); g_pd.nav_ms = now + PAD_REPEAT_FIRST_MS; } }
        else if (nav && (int)(now - g_pd.nav_ms) >= 0) { ctrl_tap_key(nav); g_pd.nav_ms = now + PAD_REPEAT_MS; }
        if (pressed & EVO_PAD_A) ctrl_tap_key(messageToPrint ? 'y' : KEY_ENTER);
        if (pressed & EVO_PAD_B) ctrl_tap_key(messageToPrint ? 'n' : KEY_BACKSPACE);
        return;
    }
    g_pd.nav_key = 0;

    for (d = 0; d < 4; d++) pad_hold(&g_pd.key[d], want[d], ctrl_dir_key(d));
    pad_hold(&g_pd.fire, rt > PAD_TRIGGER_ON || (b & EVO_PAD_X), key_fire);
    pad_hold(&g_pd.use, (b & (EVO_PAD_A | EVO_PAD_B)) != 0, key_use);
    pad_hold(&g_pd.run, lt <= PAD_TRIGGER_ON, key_speed);          // run unless LT is held
    if (pressed & EVO_PAD_RB) ctrl_weapon_cycle(+1);
    if (pressed & EVO_PAD_LB) ctrl_weapon_cycle(-1);
    if (pressed & (EVO_PAD_Y | EVO_PAD_VIEW)) ctrl_tap_key(KEY_TAB);

    // right stick: one mouse event per tic, squared response (fine aim near the centre)
    if ((int)(now - g_pd.turn_ms) >= 25) {
        int a = rx < 0 ? -rx : rx;
        if (a > PAD_DEAD_TURN) {
            int v = (a - PAD_DEAD_TURN) * 1000 / (32767 - PAD_DEAD_TURN);     // 0..1000
            int dx = v * v / 1000 * PAD_TURN_MAX / 1000;
            event_t e2; memset(&e2, 0, sizeof e2);
            if (dx < 1) dx = 1;
            e2.type = ev_mouse; e2.data2 = rx < 0 ? -dx : dx;
            queue_event(&g_input_ctx.event_queue, &e2);
        }
        g_pd.turn_ms = now;
    }
}

static void *controller_thread_func(void *arg) {
    (void)arg;
    const char *e = getenv("DOOM_CTRL_TURN");
    if (e && atoi(e) > 0) g_ctrl_turn_scale = atoi(e);
    FILE *lg = fopen("/dev/shmem/evo_ctrl.log", "w");
    struct evo_feed *feed = NULL;
    int ffd = -1;
    unsigned int tail = 0;
    int dir_down[4] = {0,0,0,0};   // track held 5-way directions for clean keyup
    unsigned int last_grab_ms = 0, menu_down_ms = 0;
    unsigned int dir_down_ms[4] = {0,0,0,0}, dir_release_at[4] = {0,0,0,0};
    int dir_pending[4] = {0,0,0,0};   // release deferred until the minimum hold has elapsed
    int turn_acc = 0; unsigned int last_turn_ms = 0;
    int fire_down = 0, use_down = 0, run_on = 0, in_menu = 0;
    printf("INPUT: Controller thread started (DSI feed %s, turn_scale=%d)\n",
           EVO_FEED_PATH, g_ctrl_turn_scale);
    while (!g_input_ctx.shutdown_requested) {
        // (re)attach to the feed; evo_ctrlfeed may start after DOOM, or restart.
        if (!feed) {
            if (ffd < 0) ffd = open(EVO_FEED_PATH, O_RDWR);
            if (ffd >= 0) {
                void *m = mmap(NULL, sizeof(struct evo_feed), PROT_READ | PROT_WRITE, MAP_SHARED, ffd, 0);
                if (m != MAP_FAILED) {
                    feed = (struct evo_feed *)m;
                    if (feed->magic != EVO_FEED_MAGIC) { munmap(m, sizeof(struct evo_feed)); feed = NULL; }
                    else { tail = feed->head; if (lg) { fprintf(lg, "feed attached head=%u\n", tail); fflush(lg); } }
                }
            }
            if (!feed) { usleep(300000); continue; }   // no feed yet: poll politely
        }
        // GRAB: own the controller while DOOM runs (HMI muted by evo_tee); refreshed so it lapses if we die
        {
            unsigned int now = evo_now_ms();
            if ((int)(now - last_grab_ms) > 400) { feed->grab_until = now + EVO_GRAB_TTL_MS; last_grab_ms = now; }
            ctrl_service_taps(now);
            {   // menu context: DOOM menu or a prompt. Entering it releases every held game key.
                int m = (menuactive || messageToPrint) ? 1 : 0;
                if (m && !in_menu) {
                    int d; for (d = 0; d < 4; d++) if (dir_down[d]) { ctrl_post_key(0, ctrl_dir_key(d)); dir_down[d] = 0; dir_pending[d] = 0; }
                    if (fire_down) { ctrl_post_key(0, key_fire); fire_down = 0; }
                    if (use_down)  { ctrl_post_key(0, key_use);  use_down = 0; }
                    turn_acc = 0;
                }
                in_menu = m;
            }
            ctrl_service_pad(now, in_menu);          // gamepad, if one is connected
            { int d; for (d = 0; d < 4; d++)           // deferred tilt releases (minimum hold)
                if (dir_pending[d] && (int)(now - dir_release_at[d]) >= 0) { ctrl_post_key(0, ctrl_dir_key(d)); dir_down[d] = 0; dir_pending[d] = 0; } }
            if (turn_acc && (int)(now - last_turn_ms) >= 25) {   // one mouse event per tic with the summed detents
                // Rate scaling: a lone detent is a precise nudge, a fast spin turns you around.
                // The in-game "mouse sensitivity" slider scales the result (x0.5 .. x1.4) in G_Responder.
                int gap = (int)(now - last_turn_ms), mult10 = gap < 70 ? 22 : gap < 140 ? 15 : 10;
                event_t e2; memset(&e2, 0, sizeof e2);
                e2.type = ev_mouse; e2.data2 = turn_acc * g_ctrl_turn_scale * mult10 / 10;   // + = clockwise = turn right
                queue_event(&g_input_ctx.event_queue, &e2);
                if (lg) { fprintf(lg, "TURN %+d -> dx %d\n", turn_acc, e2.data2); fflush(lg); }
                turn_acc = 0; last_turn_ms = now;
            }
        }
        // drain any new events
        unsigned int head = feed->head;
        if ((int)(head - tail) < 0) tail = head;          // producer re-initialised the ring: resync
        if (head == tail) { usleep(5000); continue; }   // ~5ms idle; keeps input snappy
        while (tail != head) {
            struct evo_ev ev = feed->ev[tail % EVO_FEED_RING];
            tail++;
            if (g_pad_on && ev.b[3] == EVO_PAD_FEED_MARK) continue;   // the pad's mirrored knob events: handled directly above
            switch (ev.kind) {
                case EVO_K_ROTARY: {
                    int d = (signed char)ev.b[0];
                    if (in_menu) {                      // menu: each detent steps the list
                        int n = d < 0 ? -d : d; if (n > 4) n = 4;
                        while (n--) ctrl_tap_key(d > 0 ? KEY_DOWNARROW : KEY_UPARROW);
                    } else turn_acc += d;               // game: flushed above, once per tic
                    break;
                }
                case EVO_K_DIR: {
                    // Map from the RAW tilt code (b[2]) so the mapping lives here, not in the tee inside `most`:
                    // 01 forward, 02 right, 04 back, 08 left (bench-confirmed 2026-11-06, two independent runs).
                    // A release carries code 00: release whatever raw code is currently down.
                    static int raw_dir_down = -1;
                    int down = ev.b[1], code = ev.b[2], dir;
                    if (down) { dir = code == 0x01 ? EVO_DIR_FWD : code == 0x04 ? EVO_DIR_BACK :
                                      code == 0x08 ? EVO_DIR_LEFT : code == 0x02 ? EVO_DIR_RIGHT : ev.b[0];
                                raw_dir_down = dir; }
                    else { dir = raw_dir_down >= 0 ? raw_dir_down : ev.b[0]; raw_dir_down = -1; }
                    if (in_menu) {                      // menu: tilt = arrow keys (tap on press)
                        if (down) ctrl_tap_key(dir == EVO_DIR_FWD ? KEY_UPARROW : dir == EVO_DIR_BACK ? KEY_DOWNARROW :
                                               dir == EVO_DIR_LEFT ? KEY_LEFTARROW : KEY_RIGHTARROW);
                    } else if (dir >= 0 && dir < 4) {
                        int key = ctrl_dir_key(dir);
                        if (key) {
                            unsigned int now2 = evo_now_ms();
                            if (down) {
                                if (dir_pending[dir]) dir_pending[dir] = 0;              // re-tilt before the deferred release: stay held
                                else if (!dir_down[dir]) { ctrl_post_key(1, key); dir_down[dir] = 1; }
                                dir_down_ms[dir] = now2;
                            } else if (dir_down[dir] && !dir_pending[dir]) {
                                if ((int)(now2 - dir_down_ms[dir]) < EVO_TILT_MIN_HOLD_MS) {   // tap: keep moving a bit
                                    dir_pending[dir] = 1; dir_release_at[dir] = dir_down_ms[dir] + EVO_TILT_MIN_HOLD_MS;
                                } else { ctrl_post_key(0, key); dir_down[dir] = 0; }
                            }
                        }
                        if (lg) { fprintf(lg, "DIR %d %s\n", dir, down?"down":"up"); fflush(lg); }
                    }
                    break;
                }
                case EVO_K_CENTER: {
                    if (in_menu) {                      // menu: push = select ('y' on a prompt)
                        if (ev.b[1]) ctrl_tap_key(messageToPrint ? 'y' : KEY_ENTER);
                    } else {
                        ctrl_post_key(ev.b[1] ? 1 : 0, key_fire);    // fire (hold = auto-fire)
                        fire_down = ev.b[1] ? 1 : 0;
                    }
                    if (lg) { fprintf(lg, "CENTER %s%s\n", ev.b[1]?"down":"up", in_menu ? " (menu)" : " -> FIRE"); fflush(lg); }
                    break;
                }
                case EVO_K_BUTTON:
                default:
                    {
                        int down = ev.b[1];
                        switch (ev.b[0]) {
                            case 0x02:                  // BACK = quit DOOM -> back to the HMI / launcher (the app-exit gesture)
                                if (down) { feed->grab_until = 0; g_ctrl_quit = 1; }  // main thread runs I_Quit
                                break;
                            case 0x01:                  // MENU: SHORT tap = DOOM menu.  A LONG hold does nothing here -
                                                        //   MENU-longpress is the Android unit's video-switch gesture.
                                if (down) menu_down_ms = evo_now_ms() | 1u;
                                else if (menu_down_ms) {
                                    if ((int)(evo_now_ms() - menu_down_ms) < 600) ctrl_tap_key(KEY_ESCAPE);
                                    menu_down_ms = 0;
                                }
                                break;
                            case 0x04: if (down) ctrl_tap_key(KEY_TAB); break;         // OPTION: automap / minimap toggle
                            case 0x08: if (!in_menu) { ctrl_post_key(down, key_use); use_down = down; } break;   // MEDIA: use (doors, switches)
                            case 0x10: if (down && !in_menu) ctrl_weapon_cycle(+1); break;             // NAV: next weapon
                            case 0x20: if (down && !in_menu) ctrl_weapon_cycle(-1); break;             // TEL: previous weapon
                            case 0x40: if (down && !in_menu) { run_on = !run_on; ctrl_post_key(run_on, key_speed); } break;   // COM: run toggle
                        }
                    }
                    if (lg) { fprintf(lg, "BTN id=%02x %s\n", ev.b[0], ev.b[1]?"down":"up"); fflush(lg); }
                    break;
            }
        }
    }
    // release any still-held directions on exit so DOOM doesn't keep moving
    { int d; for (d = 0; d < 4; d++) if (dir_down[d]) ctrl_post_key(0, ctrl_dir_key(d)); }
    if (fire_down) ctrl_post_key(0, key_fire);
    if (use_down) ctrl_post_key(0, key_use);
    if (run_on) ctrl_post_key(0, key_speed);
    pad_release_game();
    if (feed) { feed->grab_until = 0; munmap((void *)feed, sizeof(struct evo_feed)); }   // controller back to the HMI
    if (ffd >= 0) close(ffd);
    if (lg) fclose(lg);
    printf("INPUT: Controller thread exiting\n");
    return NULL;
}

// Background input thread function
static void *input_thread_func(void *arg) {
    printf("INPUT: Background input thread started\n");
    
    while (!g_input_ctx.shutdown_requested) {
        // Keyboard only; the mouse has its own thread
        process_keyboard_input();
        
        // Optimized polling - faster for better responsiveness
        // 10ms gives ~100Hz which balances responsiveness with CPU usage
        usleep(10000);  // 10ms - gives ~100Hz polling rate for better responsiveness
    }
    
    printf("INPUT: Background input thread exiting\n");
    return NULL;
}

// Initialize QNX threaded input system
void I_InitDirectInput(void) {
    printf("I_InitDirectInput: Setting up input system.\n");
    
    // Check if already initialized
    if (g_input_ctx.initialized) {
        printf("I_InitDirectInput: Already initialized, skipping.\n");
        return;
    }
    
    printf("I_InitDirectInput: Initializing QNX threaded input system...\n");
    
    // Initialize event queue
    init_event_queue(&g_input_ctx.event_queue);
    
    // Try to open devices initially
    g_input_ctx.keyboard_fd = try_open_device(EVO_KEYBOARD_DEVICE, &g_input_ctx.keyboard_retry_count);
    g_input_ctx.mouse_fd = try_open_device(EVO_MOUSE_DEVICE, &g_input_ctx.mouse_retry_count);
    g_input_ctx.mouse_packet_len = 0;
    g_input_ctx.mouse_y_invert = M_CheckParm("-mouseinvert") != 0;
    
    // Set initialization time for input filtering
    g_input_ctx.init_time = time(NULL);
    printf("INPUT: Will ignore input for %dms to avoid phantom keypresses\n", g_input_ctx.input_ignore_period_ms);
    
    g_input_ctx.last_keyboard_retry_time = time(NULL);
    g_input_ctx.last_mouse_retry_time = time(NULL);
    g_input_ctx.shutdown_requested = 0;
    
    // Start background input thread
    int result = pthread_create(&g_input_ctx.input_thread, NULL, input_thread_func, NULL);
    if (result != 0) {
        printf("I_InitDirectInput: ERROR - Failed to create input thread: %s\n", strerror(result));
        return;
    }
    
    if (pthread_create(&g_input_ctx.mouse_thread, NULL, mouse_thread_func, NULL) == 0) {
        g_input_ctx.mouse_thread_running = 1;
    } else {
        printf("I_InitDirectInput: ERROR - Failed to create mouse thread: %s\n", strerror(errno));
    }

    if (pthread_create(&g_input_ctx.controller_thread, NULL, controller_thread_func, NULL) == 0) {
        g_input_ctx.controller_thread_running = 1;
    } else {
        printf("I_InitDirectInput: ERROR - Failed to create controller thread: %s\n", strerror(errno));
    }

    g_input_ctx.initialized = 1;
    printf("I_InitDirectInput: QNX threaded input system initialized\n");
}

// Shutdown QNX threaded input system
void I_ShutdownDirectInput(void) {
    printf("I_ShutdownDirectInput: Shutting down QNX threaded input system...\n");
    
    if (g_input_ctx.initialized) {
        // Signal thread to shutdown
        g_input_ctx.shutdown_requested = 1;
        
        // devu-kbd ignores O_NONBLOCK: read() on /dev/usbkbd0 stays reply-blocked
        // until a key arrives, so a plain join would wait for a keypress. Cancel
        // the thread (read is a cancellation point and the resource manager
        // honours the unblock) and give up after two seconds either way; the
        // process is exiting.
        pthread_cancel(g_input_ctx.input_thread);
#ifdef __QNXNTO__
        {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += 2;
            if (pthread_timedjoin(g_input_ctx.input_thread, NULL, &deadline) != 0) {
                printf("I_ShutdownDirectInput: input thread did not stop in 2s, detaching\n");
                pthread_detach(g_input_ctx.input_thread);
            }
        }
#else
        pthread_join(g_input_ctx.input_thread, NULL);
#endif
        if (g_input_ctx.mouse_thread_running) {
            pthread_cancel(g_input_ctx.mouse_thread);
#ifdef __QNXNTO__
            {
                struct timespec deadline;
                clock_gettime(CLOCK_REALTIME, &deadline);
                deadline.tv_sec += 2;
                if (pthread_timedjoin(g_input_ctx.mouse_thread, NULL, &deadline) != 0) {
                    printf("I_ShutdownDirectInput: mouse thread did not stop in 2s, detaching\n");
                    pthread_detach(g_input_ctx.mouse_thread);
                }
            }
#else
            pthread_join(g_input_ctx.mouse_thread, NULL);
#endif
            g_input_ctx.mouse_thread_running = 0;
        }

        // Controller thread loops on shutdown_requested with a 50ms select
        // timeout, so it exits on its own; cancel+detach as a safety net.
        if (g_input_ctx.controller_thread_running) {
            pthread_cancel(g_input_ctx.controller_thread);
            pthread_detach(g_input_ctx.controller_thread);
            g_input_ctx.controller_thread_running = 0;
        }

        // Close devices
        if (g_input_ctx.keyboard_fd >= 0) {
            close(g_input_ctx.keyboard_fd);
            g_input_ctx.keyboard_fd = -1;
        }
        
        if (g_input_ctx.mouse_fd >= 0) {
            close(g_input_ctx.mouse_fd);
            g_input_ctx.mouse_fd = -1;
        }
        
        // Cleanup event queue
        pthread_mutex_destroy(&g_input_ctx.event_queue.mutex);
        
        g_input_ctx.initialized = 0;
    }
    
    printf("I_ShutdownDirectInput: QNX threaded input system shutdown complete\n");
}

// Main input polling function - called from DOOM main loop
// This is now non-blocking and just processes queued events
void I_GetEvents(void) {
    if (!g_input_ctx.initialized) {
        return;
    }
    if (g_ctrl_quit) {                      // BACK pressed on the iDrive controller: quit from the main thread
        g_ctrl_quit = 0;
        printf("INPUT: quitting (iDrive BACK pressed / gamepad Xbox button held)\n");
        I_Quit();
    }
    
    // Process queued events with improved rate limiting for better responsiveness
    event_t event;
    int events_processed = 0;
    const int max_events_per_frame = 12;  // Increased from 6 to 12 for better responsiveness
    
    // Prioritize keyboard events over mouse movement for gameplay
    int keyboard_events = 0;
    int mouse_events = 0;
    const int max_mouse_events = 3;  // Limit mouse events to prevent flooding
    
    while (events_processed < max_events_per_frame && dequeue_event(&g_input_ctx.event_queue, &event)) {
        // Skip excessive mouse movement events to prioritize keyboard input
        if (event.type == ev_mouse && mouse_events >= max_mouse_events) {
            continue;
        }
        
        D_PostEvent(&event);
        events_processed++;
        
        // Track event types
        if (event.type == ev_mouse) {
            mouse_events++;
        } else {
            keyboard_events++;
        }
        
        // Debug: Show key events only when debug enabled and only for important keys
        if (g_input_ctx.debug_enabled && event.type == ev_keydown) {
            // Only log important keys to reduce spam
            if (event.data1 == KEY_UPARROW || event.data1 == KEY_DOWNARROW || 
                event.data1 == KEY_LEFTARROW || event.data1 == KEY_RIGHTARROW ||
                event.data1 == KEY_ESCAPE || event.data1 == KEY_ENTER) {
                printf("DEBUG: Posted keydown event data1=%d to DOOM engine", event.data1);
                // Show what key this corresponds to
                if (event.data1 == KEY_UPARROW) printf(" (UP/FORWARD)");
                else if (event.data1 == KEY_DOWNARROW) printf(" (DOWN/BACKWARD)");
                else if (event.data1 == KEY_LEFTARROW) printf(" (LEFT/TURN_LEFT)");
                else if (event.data1 == KEY_RIGHTARROW) printf(" (RIGHT/TURN_RIGHT)");
                else if (event.data1 == KEY_ENTER) printf(" (ENTER)");
                else if (event.data1 == KEY_ESCAPE) printf(" (ESCAPE)");
                printf("\n");
            }
        }
    }
    
    // Only log rate limiting when debug is enabled and we actually hit the limit
    if (events_processed >= max_events_per_frame && g_input_ctx.debug_enabled) {
        printf("DEBUG: Rate limited - processed %d events this frame (kb:%d, mouse:%d)\n", 
               events_processed, keyboard_events, mouse_events);
    }
}
