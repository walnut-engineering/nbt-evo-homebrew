/* ui_preview.c - render the launcher UI on the HOST, exactly as the head unit will.
 *
 * Compiles launcher_ui.c natively and writes PPM frames, each composited over a
 * background the way the ILM compositor blends the launcher layer over the HMI.
 * Lets the look be iterated without a head unit on the bench.
 *
 *   build:  make -C projects/evo_launcher preview
 *   run:    EVO_HMI_RO=<copy of the unit's /opt/hmi/data/ro> \
 *           EVO_CODING_FILE=<copy of the unit's coding persistence file> \
 *           bin/host/ui_preview <outdir> <icondir> [background.rgb] [override] [Name=icon.rgba ...]
 *
 * EVO_HMI_RO needs only the files launcher_ui.c reads (see its header comment);
 * without it the fallback look is rendered. background.rgb = 1440x540 raw RGB
 * (e.g. a panel screenshot); omitted = black.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../launcher_ui.h"

struct icon { int w, h; uint32_t *px; };

static int load_rgba(const char *path, struct icon *ic) {
    uint32_t hd[2]; FILE *f = fopen(path, "rb");
    ic->px = NULL; ic->w = ic->h = 0;
    if (!f) return -1;
    if (fread(hd, 4, 2, f) != 2 || hd[0] == 0 || hd[1] == 0 || hd[0] > 512 || hd[1] > 512) { fclose(f); return -1; }
    ic->px = malloc((size_t)hd[0] * hd[1] * 4);
    if (fread(ic->px, 4, (size_t)hd[0] * hd[1], f) != (size_t)hd[0] * hd[1]) { fclose(f); free(ic->px); ic->px = NULL; return -1; }
    fclose(f); ic->w = (int)hd[0]; ic->h = (int)hd[1];
    return 0;
}
static void write_ppm(const char *path, const uint32_t *fb, const unsigned char *bg) {
    FILE *f = fopen(path, "wb"); int i;
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", LUI_W, LUI_H);
    for (i = 0; i < LUI_W * LUI_H; i++) {
        uint32_t p = fb[i]; unsigned a = p >> 24, ia = 255 - a, c[3], k;
        c[0] = (p >> 16) & 255; c[1] = (p >> 8) & 255; c[2] = p & 255;
        for (k = 0; k < 3; k++) { unsigned b = bg ? bg[i * 3 + k] : 0; fputc((int)((c[k] * a + b * ia + 127) / 255), f); }
    }
    fclose(f);
    printf("wrote %s\n", path);
}

int main(int argc, char **argv) {
    static const char *def[][2] = { {"DOOM","doom.rgba"}, {"Quake 3","quake.rgba"}, {"Terminal","terminal.rgba"},
                                    {"System","cpu.rgba"}, {"GPS","gps.rgba"} };
    struct lui_item items[32]; struct icon icons[32]; char names[32][64];
    uint32_t *fb = malloc((size_t)LUI_W * LUI_H * 4); unsigned char *bg = NULL;
    char path[512]; const char *out, *icondir, *override = NULL; int n = 0, i; lui_t *u;

    if (argc < 3) { fprintf(stderr, "usage: %s <outdir> <icondir> [background.rgb|-] [auto|id5|flat] [Name=icon.rgba ...]\n", argv[0]); return 2; }
    out = argv[1]; icondir = argv[2];
    if (argc > 3 && strcmp(argv[3], "-") != 0) {
        FILE *f = fopen(argv[3], "rb");
        if (f) { bg = malloc((size_t)LUI_W * LUI_H * 3); if (fread(bg, 3, (size_t)LUI_W * LUI_H, f) != (size_t)LUI_W * LUI_H) { free(bg); bg = NULL; } fclose(f); }
    }
    if (argc > 4) override = argv[4];
    if (argc > 5) {
        for (i = 5; i < argc && n < 32; i++, n++) {
            char *eq = strchr(argv[i], '=');
            snprintf(names[n], sizeof(names[n]), "%.*s", eq ? (int)(eq - argv[i]) : 63, argv[i]);
            snprintf(path, sizeof(path), "%s/%s", icondir, eq ? eq + 1 : "");
            load_rgba(path, &icons[n]);
        }
    } else {
        for (n = 0; n < 6; n++) {
            snprintf(names[n], sizeof(names[n]), "%s", def[n][0]);
            snprintf(path, sizeof(path), "%s/%s", icondir, def[n][1]);
            load_rgba(path, &icons[n]);
        }
    }
    for (i = 0; i < n; i++) { items[i].name = names[i]; items[i].icon_w = icons[i].w; items[i].icon_h = icons[i].h; items[i].icon_px = icons[i].px; }

    u = lui_create(override);
    if (!u) { fprintf(stderr, "lui_create failed\n"); return 1; }
    printf("theme: %s\n", lui_theme_desc(u));
    lui_set_items(u, items, n);

    lui_render_page(u, fb, 0, 600, 255);
    lui_render_page(u, fb, 0, 1000, 255);            /* selection settled on the first tile */
    snprintf(path, sizeof(path), "%s/page_sel0.ppm", out); write_ppm(path, fb, bg);
    lui_render_page(u, fb, n > 4 ? 4 : n - 1, 2000, 255);
    lui_render_page(u, fb, n > 4 ? 4 : n - 1, 2060, 255);   /* 60 ms into the glow ease */
    snprintf(path, sizeof(path), "%s/page_sel_moving.ppm", out); write_ppm(path, fb, bg);
    lui_render_page(u, fb, n - 1, 8600, 255);
    lui_render_page(u, fb, n - 1, 9000, 255);        /* last tile: shows scrolling when n > 6 */
    snprintf(path, sizeof(path), "%s/page_last.ppm", out); write_ppm(path, fb, bg);
    lui_render_page(u, fb, 0, 19600, 255);
    lui_render_page(u, fb, 0, 20000, 110);           /* part-way through the fade in */
    snprintf(path, sizeof(path), "%s/page_fade.ppm", out); write_ppm(path, fb, bg);
    lui_render_splash(u, fb, &items[0], 450);
    snprintf(path, sizeof(path), "%s/splash.ppm", out); write_ppm(path, fb, bg);
    lui_destroy(u);
    return 0;
}
