/* evo_nes.c - NES emulator tile for the NBT EVO homebrew launcher (the frontend).
 *
 * The Nofrendo core (./nofrendo, LGPL) does the emulation and evo_nes_osd.c is its
 * O/S layer; this file is everything the launcher side needs:
 *
 *   display : an evo_graphics_lib full-screen context (OEM HMI hidden). The NES
 *             frame (256x224, 8-bit indexed) is palette-converted and 2x integer-
 *             scaled to 512x448, centred on the 1440x540 panel, black borders.
 *   input   : the iDrive knob via the shared feed ring (/dev/shmem/evo_ctrl_feed,
 *             produced by the `most` tee): tilt = D-pad (held while tilted),
 *             press = A, OPTION or MEDIA = B, NAV = Start, TEL = Select,
 *             BACK twice within 3 s = quit (one BACK leaves the ROM picker),
 *             rotate = move in the picker. MENU is left alone (reserved).
 *             A USB keyboard (/dev/usbkbd0) works too: arrows, X/Space = A, Z = B,
 *             Enter = Start, RShift/Tab = Select, Esc Esc = quit.
 *   audio   : QSA via DOOM's proven recipe (preferred PLAYBACK device, S16_LE
 *             stereo, 4096-byte fragments, `audioconn -b 0xb8` -> speakers).
 *             One emulated frame of APU output per drawn frame; the blocking write
 *             paces emulation to the DAC. Gain: EVO_NES_AUDIO_GAIN percent
 *             (default 300 - the unit attenuates the 0xb8 browser source).
 *   roms    : bring your own .nes files in /fs/sda1/evo/nes/ (the USB stick). A
 *             knob-driven picker lists them. Battery saves (.sav) and save states
 *             land beside the ROM.
 *             `evo_nes /path/game.nes` skips the picker.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/asoundlib.h>

#include "evo_graphics_lib.h"
#include "evo_context.h"
#include "evo_display.h"
#include "font8x8_basic.h"
#include "evo_feed.h"
#include "evo_pad.h"
#include "evo_nes_port.h"

#define DISPLAY_W 1440
#define DISPLAY_H 540
#define ROM_DIR   "/fs/sda1/evo/nes"
#define MAX_ROMS  128
#define BACK_CONFIRM_MS 3000u

/* colours are 0xAARRGGBB */
#define C_BG     0xFF000000u
#define C_PANEL  0xFF0A0A0Cu
#define C_ACCENT 0xFFE08020u
#define C_TXT    0xFFFFFFFFu
#define C_DIM    0xFF9A9AA0u
#define C_SEL    0xFF202830u

static volatile int g_running = 1;          /* process-level run flag (signals) */
static volatile int g_quit = 0;             /* the user asked to leave the emulator */
static volatile int g_in_picker = 1;
static volatile unsigned g_knob = 0;        /* NESBTN_* held via the iDrive knob */
static volatile unsigned g_kbd = 0;         /* NESBTN_* held via the USB keyboard */
static volatile int g_rotary = 0;           /* accumulated detents (picker) */
static volatile unsigned g_back_armed_at = 0;   /* ms clock; 0 = not armed */
static evo_context_t *g_ctx = NULL;

static void on_sig(int s) { (void)s; g_running = 0; }
static unsigned now_ms(void) { unsigned t = evo_now_ms(); return t ? t : 1; }

/* ---- quit gesture. In the picker one BACK leaves. In a game BACK must be pressed
 * twice within BACK_CONFIRM_MS so an accidental press doesn't dump you to the HMI. */
static void back_pressed(void)
{
    unsigned t = now_ms();
    if (g_in_picker) { g_quit = 1; return; }
    if (g_back_armed_at && (unsigned)(t - g_back_armed_at) < BACK_CONFIRM_MS) { g_quit = 1; g_back_armed_at = 0; }
    else g_back_armed_at = t;
}
static int back_prompt_active(void)
{
    return g_back_armed_at && (unsigned)(now_ms() - g_back_armed_at) < BACK_CONFIRM_MS;
}

/* ================================================================ iDrive knob */
/* Consumer of the shared feed ring (schema: shared/include/evo_feed.h v3). Holds the
 * controller GRAB while we run so the hidden OEM HMI ignores the knob, exactly like
 * DOOM / the launcher; releases it on exit. Tilt direction comes from the raw code
 * in b[2] (the tee leaves the mapping to the consumer), b[0] as a fallback. */
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

    while (g_running) {
        unsigned t = now_ms();
        if (f) {
            if ((int)(t - last_grab) > 400) { f->grab_until = t + EVO_GRAB_TTL_MS; last_grab = t; }
            {
                unsigned h = f->head;
                while (tail != h) {
                    struct evo_ev e = f->ev[tail % EVO_FEED_RING];
                    tail++;
                    /* Events the pad daemon mirrors into the ring: we read the pad itself
                     * (pad_mask), so only take what the pad block does not give us - the
                     * rotary steps for the picker and BACK. */
                    if (e.b[3] == EVO_PAD_FEED_MARK &&
                        (e.kind == EVO_K_DIR || e.kind == EVO_K_CENTER || (e.kind == EVO_K_BUTTON && e.b[0] != 0x02)))
                        continue;
                    switch (e.kind) {
                    case EVO_K_ROTARY:
                        g_rotary += (signed char)e.b[0];
                        break;
                    case EVO_K_DIR: {
                        unsigned m = g_knob & ~(NESBTN_UP | NESBTN_DOWN | NESBTN_LEFT | NESBTN_RIGHT);
                        if (e.b[1]) {                                        /* tilt down */
                            int code = e.b[2];
                            int dir = code == 0x01 ? EVO_DIR_FWD  : code == 0x04 ? EVO_DIR_BACK :
                                      code == 0x08 ? EVO_DIR_LEFT : code == 0x02 ? EVO_DIR_RIGHT : e.b[0];
                            m |= dir == EVO_DIR_FWD  ? NESBTN_UP   : dir == EVO_DIR_BACK ? NESBTN_DOWN :
                                 dir == EVO_DIR_LEFT ? NESBTN_LEFT : NESBTN_RIGHT;
                        }                                                    /* release: the one lever is at rest */
                        g_knob = m;
                        break; }
                    case EVO_K_CENTER:
                        g_knob = e.b[1] ? (g_knob | NESBTN_A) : (g_knob & ~NESBTN_A);
                        break;
                    case EVO_K_BUTTON: {
                        int down = e.b[1];
                        unsigned bit = 0;
                        switch (e.b[0]) {
                        case 0x04: case 0x08: bit = NESBTN_B;      break;   /* OPTION, MEDIA */
                        case 0x10:            bit = NESBTN_START;  break;   /* NAV */
                        case 0x20:            bit = NESBTN_SELECT; break;   /* TEL */
                        case 0x02: if (down) back_pressed();       break;   /* BACK */
                        default: break;                                     /* MENU (0x01) reserved, COM unused */
                        }
                        if (bit) g_knob = down ? (g_knob | bit) : (g_knob & ~bit);
                        break; }
                    default:
                        break;
                    }
                }
            }
        }
        usleep(4000);                              /* ~250 Hz: knob latency well under a frame */
    }
    if (f) { f->grab_until = 0; munmap((void *)f, sizeof *f); }   /* controller back to the HMI */
    return NULL;
}

/* ============================================================== USB keyboard */
/* /dev/usbkbd0 delivers PS/2 set-1 scancodes (0xE0 = extended prefix, bit 0x80 =
 * release). devu-kbd never unblocks a read by itself, so each read is bounded by a
 * kernel timeout - that is what lets this thread exit with the process. */
static void kbd_set(unsigned bit, int down) { g_kbd = down ? (g_kbd | bit) : (g_kbd & ~bit); }

static void *kbd_thread(void *arg)
{
    int fd = -1, ext = 0;
    unsigned char buf[32];
    (void)arg;

    while (g_running) {
        int n, i;
        if (fd < 0) { fd = open("/dev/usbkbd0", O_RDONLY); if (fd < 0) { sleep(1); continue; } }
        {
            uint64_t to = 200ull * 1000000ull;      /* 200 ms */
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
            if (ext) {
                ext = 0;
                if      (code == 0x48) kbd_set(NESBTN_UP, down);
                else if (code == 0x50) kbd_set(NESBTN_DOWN, down);
                else if (code == 0x4B) kbd_set(NESBTN_LEFT, down);
                else if (code == 0x4D) kbd_set(NESBTN_RIGHT, down);
                continue;
            }
            switch (code) {
            case 0x48: kbd_set(NESBTN_UP, down);    break;       /* keypad arrows (no 0xE0) */
            case 0x50: kbd_set(NESBTN_DOWN, down);  break;
            case 0x4B: kbd_set(NESBTN_LEFT, down);  break;
            case 0x4D: kbd_set(NESBTN_RIGHT, down); break;
            case 0x2D: case 0x39: kbd_set(NESBTN_A, down); break;        /* X, Space */
            case 0x2C:            kbd_set(NESBTN_B, down); break;        /* Z */
            case 0x1C:            kbd_set(NESBTN_START, down); break;    /* Enter */
            case 0x36: case 0x0F: kbd_set(NESBTN_SELECT, down); break;   /* Right Shift, Tab */
            case 0x01: if (down) back_pressed(); break;                  /* Esc = BACK */
            default: break;
            }
        }
    }
    if (fd >= 0) close(fd);
    return NULL;
}

/* ================================================================== QSA audio */
static snd_pcm_t *g_pcm = NULL;
static int g_rate = 0, g_frag = 4096, g_gain = 300;
static unsigned char *g_stage = NULL;   /* bytes staged towards one fragment */
static int g_stage_fill = 0;
static short *g_mono = NULL;
static int g_mono_cap = 0;
static int g_rem = 0;                   /* sample-rate remainder (rate % 60) accumulator */
static int g_underruns = 0;             /* short/failed fragment writes that had to be re-armed */

/* audioconn needs the mm libs on its path; set them inline so we don't depend on ours */
static void audioconn(const char *verb)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd,
             "LD_LIBRARY_PATH=/opt/mm/lib:/opt/sys/lib:/lib:/usr/lib /opt/mm/bin/audioconn -%s 0xb8 >/dev/null 2>&1", verb);
    system(cmd);
}

static int audio_init(void)
{
    int card = -1, dev = -1, rc, rate = 44100, frag = 4096;
    snd_pcm_channel_info_t info;
    snd_pcm_channel_params_t p;
    snd_pcm_channel_setup_t su;
    const char *g = getenv("EVO_NES_AUDIO_GAIN");

    if (g) { int v = atoi(g); if (v >= 10 && v <= 800) g_gain = v; }

    rc = snd_pcm_open_preferred(&g_pcm, &card, &dev, SND_PCM_OPEN_PLAYBACK);
    if (rc < 0 || !g_pcm) {
        printf("[NES] audio: snd_pcm_open_preferred failed %d (%s) - running silent\n", rc, snd_strerror(rc));
        g_pcm = NULL; return 0;
    }
    memset(&info, 0, sizeof info);
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    if (snd_pcm_plugin_info(g_pcm, &info) == 0) {
        if (info.max_rate > 0 && info.max_rate < rate) rate = info.max_rate;
        if (info.min_rate > 0 && rate < info.min_rate) rate = info.min_rate;
    }
    memset(&p, 0, sizeof p);
    p.channel = SND_PCM_CHANNEL_PLAYBACK;
    p.mode = SND_PCM_MODE_BLOCK;
    p.start_mode = SND_PCM_START_FULL;
    p.stop_mode = SND_PCM_STOP_STOP;
    p.buf.block.frag_size = frag;
    p.buf.block.frags_min = 1;
    p.buf.block.frags_max = 4;
    p.format.interleave = 1;
    p.format.format = SND_PCM_SFMT_S16_LE;
    p.format.rate = rate;
    p.format.voices = 2;
    if ((rc = snd_pcm_plugin_params(g_pcm, &p)) < 0) {
        printf("[NES] audio: plugin_params failed %d (%s)\n", rc, snd_strerror(rc));
        snd_pcm_close(g_pcm); g_pcm = NULL; return 0;
    }
    memset(&su, 0, sizeof su);
    su.channel = SND_PCM_CHANNEL_PLAYBACK;
    if (snd_pcm_plugin_setup(g_pcm, &su) == 0) {
        if (su.format.rate > 0) rate = su.format.rate;
        if (su.buf.block.frag_size > 0) frag = su.buf.block.frag_size;
    }
    if ((rc = snd_pcm_plugin_prepare(g_pcm, SND_PCM_CHANNEL_PLAYBACK)) < 0) {
        printf("[NES] audio: plugin_prepare failed %d (%s)\n", rc, snd_strerror(rc));
        snd_pcm_close(g_pcm); g_pcm = NULL; return 0;
    }
    g_rate = rate; g_frag = frag;
    g_stage = (unsigned char *)malloc(g_frag + 16384);
    g_stage_fill = 0;
    audioconn("b");                                  /* browser source -> speakers */
    printf("[NES] audio: QSA %d Hz stereo S16_LE, fragment %d bytes, gain %d%%\n", g_rate, g_frag, g_gain);
    return 1;
}

static void audio_shutdown(void)
{
    if (g_pcm) {
        printf("[NES] audio: closing (%d underrun%s re-armed)\n", g_underruns, g_underruns == 1 ? "" : "s");
        snd_pcm_plugin_flush(g_pcm, SND_PCM_CHANNEL_PLAYBACK); snd_pcm_close(g_pcm); g_pcm = NULL;
    }
    audioconn("r");
    free(g_stage); g_stage = NULL;
    free(g_mono); g_mono = NULL;
}

int nesfe_audio_rate(void) { return g_pcm ? g_rate : 0; }

/* one emulated frame of 16-bit mono APU output -> stereo S16 -> QSA, fragment by fragment */
int nesfe_audio_frame(void (*pull)(void *buf, int nsamples))
{
    int n, i;
    if (!g_pcm || !pull) return 0;
    n = g_rate / 60;
    g_rem += g_rate % 60;
    if (g_rem >= 60) { g_rem -= 60; n++; }
    if (n > g_mono_cap) { free(g_mono); g_mono = (short *)malloc(n * sizeof(short)); g_mono_cap = n; }
    pull(g_mono, n);
    for (i = 0; i < n; i++) {
        int s = (int)g_mono[i] * g_gain / 100;
        short *o = (short *)(g_stage + g_stage_fill);
        if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
        o[0] = (short)s; o[1] = (short)s;
        g_stage_fill += 4;
        if (g_stage_fill >= g_frag) {
            int w = snd_pcm_plugin_write(g_pcm, g_stage, g_frag);     /* blocks: this is the pacing */
            if (w != g_frag) { g_underruns++; snd_pcm_plugin_prepare(g_pcm, SND_PCM_CHANNEL_PLAYBACK); }   /* underrun: re-arm */
            g_stage_fill -= g_frag;
            if (g_stage_fill > 0) memmove(g_stage, g_stage + g_frag, g_stage_fill);
        }
    }
    return 1;
}

/* ======================================================= framebuffer helpers */
static void put(uint32_t *fb, int x, int y, uint32_t c)
{
    if ((unsigned)x < DISPLAY_W && (unsigned)y < DISPLAY_H) fb[y * DISPLAY_W + x] = c;
}
static void fill(uint32_t *fb, int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) put(fb, x + i, y + j, c);
}
static void glyph(uint32_t *fb, int x, int y, char ch, int s, uint32_t c)
{
    const char *g = font8x8_basic[(unsigned char)ch & 0x7f];
    int r, b, sy, sx;
    for (r = 0; r < 8; r++) {
        unsigned char bits = (unsigned char)g[r];
        for (b = 0; b < 8; b++) if (bits & (1u << b))
            for (sy = 0; sy < s; sy++) for (sx = 0; sx < s; sx++) put(fb, x + b * s + sx, y + r * s + sy, c);
    }
}
static void text(uint32_t *fb, int x, int y, const char *str, int s, uint32_t c)
{
    for (; *str; str++) { glyph(fb, x, y, *str, s, c); x += 8 * s; }
}
static int textw(const char *str, int s) { return (int)strlen(str) * 8 * s; }
static void clear_fb(uint32_t *fb, uint32_t c) { int i; for (i = 0; i < DISPLAY_W * DISPLAY_H; i++) fb[i] = c; }

/* ===================================================== the emulated frame */
/* 8-bit indexed rows -> 2x integer scale (256x224 -> 512x448), centred; the quit
 * prompt lives in the bottom border band, which is cleared every frame. */
void nesfe_blit(const unsigned char *const *lines, int w, int h, const unsigned int *pal)
{
    uint32_t *fb = evo_context_get_framebuffer(g_ctx);
    /* the PPU renders 240 lines; the top/bottom 8 are overscan (never shown on a TV,
     * often garbage), so present the visible 224 */
    int skip = (h > 224) ? (h - 224) / 2 : 0, vis = h - 2 * skip;
    int sw = w * 2, sh = vis * 2, ox = (DISPLAY_W - sw) / 2, oy = (DISPLAY_H - sh) / 2, x, y, by;
    if (!fb) return;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    for (y = 0; y < vis && oy + 2 * y + 1 < DISPLAY_H; y++) {
        const unsigned char *src = lines[y + skip];
        uint32_t *d0 = fb + (oy + 2 * y) * DISPLAY_W + ox, *d1 = d0 + DISPLAY_W;
        for (x = 0; x < w && ox + 2 * x + 1 < DISPLAY_W; x++) {
            uint32_t c = pal[src[x]];
            d0[2 * x] = c; d0[2 * x + 1] = c; d1[2 * x] = c; d1[2 * x + 1] = c;
        }
    }
    by = oy + sh;
    if (by < DISPLAY_H) {
        fill(fb, 0, by, DISPLAY_W, DISPLAY_H - by, C_BG);
        if (back_prompt_active()) {
            const char *m = "press BACK again to quit";
            text(fb, (DISPLAY_W - textw(m, 2)) / 2, by + ((DISPLAY_H - by) - 16) / 2, m, 2, C_ACCENT);
        }
    }
    evo_context_present(g_ctx);
}

/* A real gamepad through evo_padd (shared/include/evo_pad.h): every button at once, so
 * diagonals work, and a second pad is player 2. D-pad or left stick; the face buttons
 * as two pairs - A/B = NES A, X/Y = NES B - so the thumb rolls X -> A the way it rolls
 * B -> A on the original pad; Menu = Start, View = Select. */
#define PAD_STICK 16000
static const struct evo_pad *g_padblk = NULL;
static unsigned g_pad_retry = 0;

static unsigned pad_mask(int i)
{
    unsigned b, m = 0;
    int lx, ly;

    if (!g_padblk) {                                /* the daemon may start after us */
        unsigned t = now_ms();
        if (g_pad_retry && (int)(t - g_pad_retry) < 1000) return 0;
        g_pad_retry = t ? t : 1;
        g_padblk = evo_pad_open();
        if (!g_padblk) return 0;
    }
    if (!evo_pad_alive(g_padblk) || !g_padblk->pad[i].connected) return 0;
    b = g_padblk->pad[i].buttons; lx = g_padblk->pad[i].lx; ly = g_padblk->pad[i].ly;
    if ((b & EVO_PAD_UP)    || ly >  PAD_STICK) m |= NESBTN_UP;
    if ((b & EVO_PAD_DOWN)  || ly < -PAD_STICK) m |= NESBTN_DOWN;
    if ((b & EVO_PAD_LEFT)  || lx < -PAD_STICK) m |= NESBTN_LEFT;
    if ((b & EVO_PAD_RIGHT) || lx >  PAD_STICK) m |= NESBTN_RIGHT;
    /* opposite directions together cannot happen on a real NES pad and confuse games */
    if ((m & (NESBTN_UP | NESBTN_DOWN)) == (NESBTN_UP | NESBTN_DOWN))       m &= ~(NESBTN_UP | NESBTN_DOWN);
    if ((m & (NESBTN_LEFT | NESBTN_RIGHT)) == (NESBTN_LEFT | NESBTN_RIGHT)) m &= ~(NESBTN_LEFT | NESBTN_RIGHT);
    if (b & (EVO_PAD_A | EVO_PAD_B)) m |= NESBTN_A;
    if (b & (EVO_PAD_X | EVO_PAD_Y)) m |= NESBTN_B;
    if (b & EVO_PAD_MENU) m |= NESBTN_START;
    if (b & EVO_PAD_VIEW) m |= NESBTN_SELECT;
    return m;
}

unsigned nesfe_buttons(void)  { return g_knob | g_kbd | pad_mask(0); }
unsigned nesfe_buttons2(void) { return pad_mask(1); }
int nesfe_quit_requested(void) { return g_quit || !g_running; }
void nesfe_log(const char *s) { fputs(s, stdout); fflush(stdout); }

/* ================================================================ ROM picker */
static char g_roms[MAX_ROMS][256];
static int g_nroms = 0;

static int cmp_name(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

static void scan_roms(void)
{
    DIR *d = opendir(ROM_DIR);
    struct dirent *e;
    g_nroms = 0;
    if (!d) return;
    while ((e = readdir(d)) != NULL && g_nroms < MAX_ROMS) {
        size_t L = strlen(e->d_name);
        if (L > 4 && strcasecmp(e->d_name + L - 4, ".nes") == 0) {
            strncpy(g_roms[g_nroms], e->d_name, 255);
            g_roms[g_nroms][255] = 0;
            g_nroms++;
        }
    }
    closedir(d);
    qsort(g_roms, g_nroms, sizeof g_roms[0], cmp_name);
}

/* returns 1 with `path` filled in, 0 if the user left */
static int picker(char *path, size_t pathlen)
{
    int sel = 0, first = 0, nitems, prev_rot = g_rotary;
    unsigned prev = nesfe_buttons();        /* knob + keyboard + gamepad, as in the game */

    scan_roms();
    nitems = g_nroms;
    g_in_picker = 1;

    while (g_running && !g_quit) {
        int rot = g_rotary, d = rot - prev_rot;
        unsigned b = nesfe_buttons(), pressed = b & ~prev;
        uint32_t *fb;
        prev_rot = rot; prev = b;

        if (d) sel += d;
        if (pressed & NESBTN_UP) sel--;
        if (pressed & NESBTN_DOWN) sel++;
        if (sel < 0) sel = 0;
        if (sel >= nitems) sel = nitems > 0 ? nitems - 1 : 0;
        if (nitems > 0 && (pressed & (NESBTN_A | NESBTN_START))) {
            snprintf(path, pathlen, "%s/%s", ROM_DIR, g_roms[sel]);
            g_in_picker = 0;
            return 1;
        }

        fb = evo_context_get_framebuffer(g_ctx);
        if (fb) {
            const int rows = 11, rowh = 30, y0 = 130;
            int i;
            char n[96];
            clear_fb(fb, C_PANEL);
            text(fb, 60, 36, "NES", 7, C_TXT);
            snprintf(n, sizeof n, "%d ROM%s in %s", g_nroms, g_nroms == 1 ? "" : "s", ROM_DIR);
            text(fb, 60 + textw("NES", 7) + 40, 36 + 56 - 16, n, 2, C_DIM);
            if (sel < first) first = sel;
            if (sel >= first + rows) first = sel - rows + 1;
            for (i = 0; i < rows && first + i < nitems; i++) {
                int idx = first + i, y = y0 + i * rowh;
                char label[80];
                size_t L;
                strncpy(label, g_roms[idx], sizeof label - 1); label[sizeof label - 1] = 0;
                L = strlen(label); if (L > 4) label[L - 4] = 0;        /* drop ".nes" */
                if (strlen(label) > 52) label[52] = 0;
                if (idx == sel) { fill(fb, 48, y - 3, DISPLAY_W - 96, rowh, C_SEL); fill(fb, 48, y - 3, 6, rowh, C_ACCENT); }
                text(fb, 72, y, label, 3, idx == sel ? C_TXT : C_DIM);
            }
            if (g_nroms == 0)
                text(fb, 72, y0 + rowh + 10, "put .nes files in /fs/sda1/evo/nes (the USB stick)", 2, C_ACCENT);
            {
                const char *hint = "rotate / tilt: choose    press: play    BACK: exit";
                text(fb, (DISPLAY_W - textw(hint, 2)) / 2, DISPLAY_H - 40, hint, 2, C_DIM);
            }
            evo_context_present(g_ctx);
        }
        usleep(33000);
    }
    return 0;
}

/* ======================================================================= main */
int main(int argc, char **argv)
{
    char rom[512] = "";
    pthread_t kt, bt;
    uint32_t *fb;

    signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGPIPE, SIG_IGN);
    if (argc > 1) { strncpy(rom, argv[1], sizeof rom - 1); g_in_picker = 0; }

    evo_graphics_init();
    g_ctx = evo_context_create(DISPLAY_W, DISPLAY_H, 0, 0);
    if (!g_ctx) { printf("[NES] context create failed\n"); return 1; }
    if (evo_context_show(g_ctx) != 0) { printf("[NES] show failed\n"); evo_context_destroy(g_ctx); return 1; }
    { evo_display_t *d = evo_context_get_display(g_ctx); if (d) evo_display_enable_presentation_mode(d); }
    fb = evo_context_get_framebuffer(g_ctx);
    if (fb) { clear_fb(fb, C_BG); evo_context_present(g_ctx); }

    pthread_create(&kt, NULL, knob_thread, NULL);
    pthread_create(&bt, NULL, kbd_thread, NULL);

    if (!rom[0] && !picker(rom, sizeof rom)) {
        printf("[NES] picker: exit\n");
    } else {
        audio_init();
        fb = evo_context_get_framebuffer(g_ctx);
        if (fb) { clear_fb(fb, C_BG); evo_context_present(g_ctx); }
        g_in_picker = 0; g_back_armed_at = 0; g_quit = 0;
        printf("[NES] run %s\n", rom); fflush(stdout);
        nesosd_run(rom);                              /* returns when the user quits */
        printf("[NES] emulator returned\n");
        nesosd_shutdown();
        audio_shutdown();
    }

    g_running = 0;
    pthread_join(kt, NULL);
    pthread_join(bt, NULL);
    evo_context_destroy(g_ctx);
    printf("[NES] exit\n");
    return 0;
}
