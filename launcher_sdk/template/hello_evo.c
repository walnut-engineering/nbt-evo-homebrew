/* hello_evo.c - the smallest complete NBT EVO launcher app.
 *
 * A full-screen page that draws over the (hidden) OEM HMI and exits when you
 * press the iDrive BACK button. This is the starting point for a new app:
 * copy this whole folder to ../../projects/<yourapp>/, rename the files, set
 * APP/SRCS in the Makefile and the fields in app.desc, then edit draw().
 *
 * THE APP CONTRACT (all the launcher expects of a tile):
 *   1. render full screen (DISPLAY_W x DISPLAY_H) through evo_graphics_lib, and
 *   2. exit cleanly when the user presses iDrive BACK.
 * The launcher spawns this binary, waits, and repaints its own page when the
 * app exits - so a plain `return 0` from main() hands control back to the menu.
 *
 * The standard skeleton below (graphics init -> context -> presentation mode ->
 * evo_ai_start for BACK -> draw loop -> destroy) is identical to evo_sysmon /
 * evo_gps; only draw() differs between apps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>

#include "evo_graphics_lib.h"
#include "evo_context.h"
#include "evo_display.h"
#include "font8x8_basic.h"   /* shared 8x8 bitmap font (shared/include) */
#include "evo_appinput.h"    /* evo_ai_start(): starts the iDrive knob thread; BACK -> exit */

#define DISPLAY_W 1440
#define DISPLAY_H 540

/* colours are 0xAARRGGBB */
#define C_BG     0xFF0A0A0CU
#define C_ACCENT 0xFFE08020U   /* BMW orange */
#define C_TXT    0xFFFFFFFFU
#define C_DIM    0xFFB0B0B0U
#define C_TRACK  0xFF303036U

static volatile int g_running = 1;
static void on_sig(int s) { (void)s; g_running = 0; }

/* ---- tiny framebuffer drawing helpers (font8x8) ------------------------- */
static void put(uint32_t *fb, int x, int y, uint32_t c) {
    if ((unsigned)x < DISPLAY_W && (unsigned)y < DISPLAY_H) fb[y * DISPLAY_W + x] = c;
}
static void fill(uint32_t *fb, int x, int y, int w, int h, uint32_t c) {
    int i, j; for (j = 0; j < h; j++) for (i = 0; i < w; i++) put(fb, x + i, y + j, c);
}
static void glyph(uint32_t *fb, int x, int y, char ch, int s, uint32_t c) {
    const char *g = font8x8_basic[(unsigned char)ch & 0x7f]; int r, b, sy, sx;
    for (r = 0; r < 8; r++) { unsigned char bits = (unsigned char)g[r];
        for (b = 0; b < 8; b++) if (bits & (1u << b))
            for (sy = 0; sy < s; sy++) for (sx = 0; sx < s; sx++)
                put(fb, x + b * s + sx, y + r * s + sy, c); }
}
static void text(uint32_t *fb, int x, int y, const char *str, int s, uint32_t c) {
    for (; *str; str++) { glyph(fb, x, y, *str, s, c); x += 8 * s; }
}
static int textw(const char *str, int s) { return (int)strlen(str) * 8 * s; }

/* ---- draw one frame. `tick` increments every frame, for simple animation. */
static void draw(uint32_t *fb, unsigned tick) {
    int i; for (i = 0; i < DISPLAY_W * DISPLAY_H; i++) fb[i] = C_BG;   /* clear */

    const char *msg = "HELLO EVO";
    int s = 8, w = textw(msg, s);
    text(fb, (DISPLAY_W - w) / 2, DISPLAY_H / 2 - 8 * s / 2 - 36, msg, s, C_TXT);

    const char *hint = "press iDrive BACK to exit";
    text(fb, (DISPLAY_W - textw(hint, 2)) / 2, DISPLAY_H - 70, hint, 2, C_DIM);

    /* a little moving accent marker so you can see the frame is live */
    int bw = 240, bx0 = (DISPLAY_W - bw) / 2, by = DISPLAY_H / 2 + 44;
    fill(fb, bx0, by, bw, 6, C_TRACK);
    fill(fb, bx0 + (int)((tick * 4) % (unsigned)(bw - 40)), by, 40, 6, C_ACCENT);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGPIPE, SIG_IGN);

    evo_graphics_init();
    evo_context_t *ctx = evo_context_create(DISPLAY_W, DISPLAY_H, 0, 0);
    if (!ctx) { printf("[HELLO] context create failed\n"); return 1; }
    if (evo_context_show(ctx) != 0) { printf("[HELLO] show failed\n"); evo_context_destroy(ctx); return 1; }
    { evo_display_t *d = evo_context_get_display(ctx);
      if (d) evo_display_enable_presentation_mode(d); }     /* hide the OEM HMI behind us */

    evo_ai_start(&g_running);                               /* iDrive BACK sets g_running = 0 */
    printf("[HELLO] running - press BACK to exit\n"); fflush(stdout);

    unsigned tick = 0;
    while (g_running) {
        uint32_t *fb = evo_context_get_framebuffer(ctx);
        if (fb) { draw(fb, tick++); evo_context_present(ctx); }
        usleep(30000);                                      /* ~30 Hz */
    }

    evo_context_destroy(ctx);
    printf("[HELLO] exit\n");
    return 0;
}
