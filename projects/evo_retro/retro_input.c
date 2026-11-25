/* retro_input.c - everything a player can press, as libretro RetroPad masks.
 *
 *   gamepads : evo_padd's shared block (shared/include/evo_pad.h), up to four. Read
 *              directly, so diagonals and every button work. The mapping is by
 *              POSITION, which is what the RetroPad is: Xbox A (bottom) = RetroPad B,
 *              Xbox B (right) = RetroPad A, Xbox X (left) = RetroPad Y, Xbox Y (top) =
 *              RetroPad X. On an SNES that is the SNES pad as printed; on a Genesis
 *              the core turns it into X/A/B = A/B/C and LB/Y/RB = X/Y/Z.
 *              The Xbox button is the menu button.
 *   knob     : the iDrive controller through the feed ring (evo_feed.h): tilt = D-pad
 *              (one direction), press = the bottom button, OPTION = the right one,
 *              MEDIA = the left one, NAV = Start, TEL = Select, BACK = menu. Enough
 *              for menus and simple games. We hold the controller grab while running
 *              so the hidden BMW HMI does not see the knob.
 *   keyboard : /dev/usbkbd0 (PS/2 set-1 scancodes): arrows, Z/X/A/S = bottom/right/
 *              left/top, Q/W = L/R, Enter = Start, Tab or right Shift = Select,
 *              Esc = menu.
 *
 * The pad daemon mirrors pad 1 into the knob ring for apps that only know the knob;
 * those events are marked (EVO_PAD_FEED_MARK) and skipped here, since we read the pad
 * itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/neutrino.h>

#include "evo_feed.h"
#include "evo_pad.h"
#include "retro_fe.h"

#define STICK_ON   16000        /* left stick as a D-pad */
#define TRIGGER_ON 300          /* of 1023 */
#define DPAD (FE_UP | FE_DOWN | FE_LEFT | FE_RIGHT)

static volatile int g_threads = 0;
static pthread_t g_kt, g_bt;
static volatile unsigned g_knob = 0;            /* held via the knob */
static volatile unsigned g_kbd = 0;             /* held via the keyboard */
/* counters written by one thread each and consumed by the main thread */
static volatile int g_rot_made = 0;             /* knob thread */
static volatile unsigned g_menu_knob = 0;       /* knob thread */
static volatile unsigned g_menu_kbd = 0;        /* keyboard thread */
static int g_rot_taken = 0;
static unsigned g_menu_taken = 0, g_menu_pad = 0;

static const struct evo_pad *g_blk = NULL;
static unsigned g_blk_retry = 0;
static unsigned g_pad[FE_PORTS];
static unsigned g_guide_prev = 0;               /* bit i = pad i's Xbox button was down */

/* ================================================================ iDrive knob */
static void *knob_thread(void *arg)
{
    struct evo_feed *f = NULL;
    unsigned tail = 0, last_grab = 0;
    int fd;
    (void)arg;

    fd = open(EVO_FEED_PATH, O_RDWR);
    if (fd >= 0) {
        f = (struct evo_feed *)mmap(0, sizeof *f, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (f == MAP_FAILED) f = NULL;
    }
    if (f) tail = f->head;                        /* only react to events from now on */

    while (g_threads) {
        unsigned t = fe_now_ms();
        if (f) {
            unsigned h;
            if ((int)(t - last_grab) > 400) { f->grab_until = t + EVO_GRAB_TTL_MS; last_grab = t; }
            h = f->head;
            while (tail != h) {
                struct evo_ev e = f->ev[tail % EVO_FEED_RING];
                tail++;
                if (e.b[3] == EVO_PAD_FEED_MARK) continue;      /* the gamepad: read directly */
                switch (e.kind) {
                case EVO_K_ROTARY:
                    g_rot_made += (signed char)e.b[0];
                    break;
                case EVO_K_DIR: {
                    unsigned m = g_knob & ~DPAD;
                    if (e.b[1]) {                               /* tilted: raw code in b[2] */
                        int code = e.b[2];
                        int dir = code == 0x01 ? EVO_DIR_FWD  : code == 0x04 ? EVO_DIR_BACK :
                                  code == 0x08 ? EVO_DIR_LEFT : code == 0x02 ? EVO_DIR_RIGHT : e.b[0];
                        m |= dir == EVO_DIR_FWD  ? FE_UP   : dir == EVO_DIR_BACK ? FE_DOWN :
                             dir == EVO_DIR_LEFT ? FE_LEFT : FE_RIGHT;
                    }                                           /* released: the lever is at rest */
                    g_knob = m;
                    break; }
                case EVO_K_CENTER:
                    g_knob = e.b[1] ? (g_knob | FE_B) : (g_knob & ~FE_B);
                    break;
                case EVO_K_BUTTON: {
                    int down = e.b[1];
                    unsigned bit = 0;
                    switch (e.b[0]) {
                    case 0x04: bit = FE_A;      break;          /* OPTION */
                    case 0x08: bit = FE_Y;      break;          /* MEDIA */
                    case 0x10: bit = FE_START;  break;          /* NAV */
                    case 0x20: bit = FE_SELECT; break;          /* TEL */
                    case 0x02: if (down) g_menu_knob++; break;  /* BACK */
                    default: break;                             /* MENU reserved, COM unused */
                    }
                    if (bit) g_knob = down ? (g_knob | bit) : (g_knob & ~bit);
                    break; }
                default:
                    break;
                }
            }
        }
        usleep(4000);
    }
    if (f) { f->grab_until = 0; munmap((void *)f, sizeof *f); }   /* controller back to the HMI */
    return NULL;
}

/* ============================================================== USB keyboard */
static void kbd_set(unsigned bit, int down) { g_kbd = down ? (g_kbd | bit) : (g_kbd & ~bit); }

static void *kbd_thread(void *arg)
{
    int fd = -1, ext = 0;
    unsigned char buf[32];
    (void)arg;

    while (g_threads) {
        int n, i;
        if (fd < 0) { fd = open("/dev/usbkbd0", O_RDONLY); if (fd < 0) { sleep(1); continue; } }
        {   /* devu-kbd never unblocks a read by itself: bound it so the thread can exit */
            uint64_t to = 200ull * 1000000ull;
            TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_REPLY | _NTO_TIMEOUT_SEND, NULL, &to, NULL);
        }
        n = read(fd, buf, sizeof buf);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == ETIMEDOUT)) continue;
            close(fd); fd = -1; sleep(1); continue;
        }
        for (i = 0; i < n; i++) {
            unsigned char sc = buf[i];
            int down; unsigned char code;
            if (sc == 0xE0) { ext = 1; continue; }
            down = !(sc & 0x80); code = sc & 0x7f;
            if (ext) ext = 0;                   /* extended arrows share the keypad codes */
            switch (code) {
            case 0x48: kbd_set(FE_UP, down);     break;
            case 0x50: kbd_set(FE_DOWN, down);   break;
            case 0x4B: kbd_set(FE_LEFT, down);   break;
            case 0x4D: kbd_set(FE_RIGHT, down);  break;
            case 0x2C: kbd_set(FE_B, down);      break;     /* Z */
            case 0x2D: kbd_set(FE_A, down);      break;     /* X */
            case 0x1E: kbd_set(FE_Y, down);      break;     /* A */
            case 0x1F: kbd_set(FE_X, down);      break;     /* S */
            case 0x10: kbd_set(FE_L, down);      break;     /* Q */
            case 0x11: kbd_set(FE_R, down);      break;     /* W */
            case 0x1C: kbd_set(FE_START, down);  break;     /* Enter */
            case 0x36: case 0x0F: kbd_set(FE_SELECT, down); break;   /* right Shift, Tab */
            case 0x01: if (down) g_menu_kbd++;   break;     /* Esc */
            default: break;
            }
        }
    }
    if (fd >= 0) close(fd);
    return NULL;
}

/* ================================================================= gamepads */
static unsigned cancel_opposites(unsigned m)
{
    /* up+down or left+right cannot happen on a real pad and confuse games */
    if ((m & (FE_UP | FE_DOWN)) == (FE_UP | FE_DOWN))       m &= ~(FE_UP | FE_DOWN);
    if ((m & (FE_LEFT | FE_RIGHT)) == (FE_LEFT | FE_RIGHT)) m &= ~(FE_LEFT | FE_RIGHT);
    return m;
}

void fe_input_poll(void)
{
    int i, alive;

    if (!g_blk) {                                   /* the daemon may start after us */
        unsigned t = fe_now_ms();
        if (!g_blk_retry || (int)(t - g_blk_retry) >= 1000) { g_blk_retry = t; g_blk = evo_pad_open(); }
    }
    alive = g_blk && evo_pad_alive(g_blk);
    for (i = 0; i < FE_PORTS; i++) {
        unsigned b, m = 0, bit = 1u << i;
        int lx, ly;
        if (!alive || !g_blk->pad[i].connected) { g_pad[i] = 0; g_guide_prev &= ~bit; continue; }
        b = g_blk->pad[i].buttons; lx = g_blk->pad[i].lx; ly = g_blk->pad[i].ly;
        if ((b & EVO_PAD_UP)    || ly >  STICK_ON) m |= FE_UP;
        if ((b & EVO_PAD_DOWN)  || ly < -STICK_ON) m |= FE_DOWN;
        if ((b & EVO_PAD_LEFT)  || lx < -STICK_ON) m |= FE_LEFT;
        if ((b & EVO_PAD_RIGHT) || lx >  STICK_ON) m |= FE_RIGHT;
        if (b & EVO_PAD_A)    m |= FE_B;            /* by position, see the top of the file */
        if (b & EVO_PAD_B)    m |= FE_A;
        if (b & EVO_PAD_X)    m |= FE_Y;
        if (b & EVO_PAD_Y)    m |= FE_X;
        if (b & EVO_PAD_LB)   m |= FE_L;
        if (b & EVO_PAD_RB)   m |= FE_R;
        if (b & EVO_PAD_LS)   m |= FE_L3;
        if (b & EVO_PAD_RS)   m |= FE_R3;
        if (b & EVO_PAD_MENU) m |= FE_START;
        if (b & EVO_PAD_VIEW) m |= FE_SELECT;
        if (g_blk->pad[i].lt > TRIGGER_ON) m |= FE_L2;
        if (g_blk->pad[i].rt > TRIGGER_ON) m |= FE_R2;
        g_pad[i] = m;
        if (b & EVO_PAD_GUIDE) { if (!(g_guide_prev & bit)) g_menu_pad++; g_guide_prev |= bit; }
        else g_guide_prev &= ~bit;
    }
}

static unsigned raw_pad(int port)
{
    unsigned m = g_pad[port];
    if (port == 0) m |= g_knob | g_kbd;
    return m;
}

/* Buttons that were down when one of our own screens closed stay invisible to the
 * game until they are let go, so the press that chose "Resume" is not also a jump. */
static unsigned g_block[FE_PORTS];

void fe_input_block_held(void)
{
    int i;
    fe_input_poll();
    for (i = 0; i < FE_PORTS; i++) g_block[i] = raw_pad(i);
}

unsigned fe_input_pad(int port)
{
    unsigned m;
    if (port < 0 || port >= FE_PORTS) return 0;
    m = raw_pad(port);
    g_block[port] &= m;                             /* released: no longer blocked */
    return cancel_opposites(m & ~g_block[port]);
}

int fe_input_rotary(void)
{
    int made = g_rot_made, d = made - g_rot_taken;
    g_rot_taken = made;
    return d;
}

int fe_input_menu(void)
{
    unsigned made = g_menu_knob + g_menu_kbd + g_menu_pad, d = made - g_menu_taken;
    g_menu_taken = made;
    return (int)d;
}

void fe_input_start(void)
{
    g_threads = 1;
    pthread_create(&g_kt, NULL, knob_thread, NULL);
    pthread_create(&g_bt, NULL, kbd_thread, NULL);
}

void fe_input_stop(void)
{
    if (!g_threads) return;
    g_threads = 0;
    pthread_join(g_kt, NULL);
    pthread_join(g_bt, NULL);
}
