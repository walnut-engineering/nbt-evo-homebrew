/* retro_video.c - one core frame onto the 1440x540 panel.
 *
 * The cores hand over a small picture (256x224, 320x224, sometimes twice that in each
 * direction) in RGB565. How it is fitted is the player's choice (Settings > Picture):
 *
 *   Sharp 2x      a whole-number scale, centred, black around - every pixel the same
 *                 size. SNES 256x224 -> 512x448; Genesis 320x224 -> 640x448 (its
 *                 256-wide mode is the same width on a real TV, so it is stretched to
 *                 640 too rather than shown narrower); Master System 512x384, Game
 *                 Gear 480x432. The 512-wide and interlaced modes land on the same
 *                 rectangle, so the picture does not jump when a game switches mode.
 *   Full height   all 540 rows, pixels kept square (SNES 617 wide, Genesis 771).
 *   TV 4:3        all 540 rows, 720 wide - the shape of the TV these were made for.
 *   Wide 16:9     all 540 rows, 960 wide - stretched.
 *
 * Both directions use a "sharp bilinear" map: at a whole-number scale it is exact
 * pixel repetition; in between it blends only the one-pixel seams, so scrolling stays
 * even without blurring the picture.
 *
 * The destination may be graphics memory (the off-screen scan-out buffer), which is
 * slow to read: each picture row is built in a small line buffer in ordinary memory
 * and then only written out, once per panel row it covers.
 *
 * What this costs is mostly the writing: about 2 ms per megabyte into the scan-out
 * buffer on this unit, so 1.5 ms for the 2x fit and 4 to 5 ms for the full-height ones
 * (measured 2026-11-24). A second thread on the other CPU core was tried and gave only
 * 15 % - the memory is the limit, not the arithmetic - so there is none. The way to
 * make the big fits cheap would be to hand the compositor a small buffer and let it do
 * the stretching.
 */
#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "retro_fe.h"

#define SRC_MAX_W 1024

static int g_fmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint32_t *g_lut = NULL;              /* 16-bit pixel -> 0xFFRRGGBB */
static int g_lut_fmt = -1;
static unsigned g_sw = 0, g_sh = 0;         /* source size the layout below is for */
static int g_dx, g_dy, g_dw, g_dh;
static unsigned short g_xi[FE_W];           /* per destination column: source column ... */
static unsigned short g_xf[FE_W];           /* ... and how much of the next one, 0..256 */
static unsigned short g_yi[FE_H];           /* the same per destination row */
static unsigned short g_yf[FE_H];
static int g_nearest = 1;
static int g_mode = FE_PIC_SHARP;
static uint32_t g_row[SRC_MAX_W + 1];       /* one source row as 32-bit pixels */
static uint32_t g_lbuf[2][FE_W];            /* the two scaled source rows a panel row can need */

void fe_video_format(int fmt) { g_fmt = fmt; }
void fe_video_rect(int *x, int *y, int *w, int *h) { *x = g_dx; *y = g_dy; *w = g_dw; *h = g_dh; }

void fe_video_set_mode(int mode)
{
    if (mode < 0 || mode >= FE_PIC_COUNT || mode == g_mode) return;
    g_mode = mode;
    g_sw = g_sh = 0;                        /* lay out again at the next frame */
}

static void build_lut(void)
{
    unsigned v;
    if (!g_lut) g_lut = (uint32_t *)malloc(65536 * sizeof(uint32_t));
    if (!g_lut) return;
    for (v = 0; v < 65536; v++) {
        unsigned r, g, b;
        if (g_fmt == RETRO_PIXEL_FORMAT_RGB565) {
            r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31;
            r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
        } else {                                        /* 0RGB1555 */
            r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31;
            r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
        }
        g_lut[v] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    g_lut_fmt = g_fmt;
}

/* Map n_dst destination positions onto n_src source positions: for each, the source
 * index and how much of the next one to blend in (0..256). Returns 1 when nothing is
 * blended anywhere (a whole-number scale). */
static int make_map(unsigned short *idx, unsigned short *frac, int n_dst, int n_src)
{
    int x, nearest = 1;
    for (x = 0; x < n_dst; x++) {
        /* centre of destination position x, in source positions (16.16 fixed point) */
        long long p = (((long long)(2 * x + 1) * (long long)n_src) << 15) / n_dst - 32768;
        long long i = p >> 16, t = p & 0xffff;
        long long f = 32768 + ((t - 32768) * n_dst) / (long long)n_src;    /* sharpen by the scale */
        if (f < 0) f = 0;
        if (f > 65536) f = 65536;
        f = (f + 128) >> 8;                                                /* 0..256 */
        if (i < 0) { i = 0; f = 0; }
        if (i >= (long long)n_src - 1) { i = (long long)n_src - 1; f = 0; }
        if (f == 256) { i++; f = 0; }
        idx[x] = (unsigned short)i; frac[x] = (unsigned short)f;
        if (f) nearest = 0;
    }
    return nearest;
}

static void layout(unsigned w, unsigned h)
{
    /* the doubled modes (512 wide, 448 tall) are the same picture at twice the detail */
    int bw = (w > 400) ? (int)w / 2 : (int)w, bh = (h > 300) ? (int)h / 2 : (int)h;
    int k = FE_H / bh, kx;

#if defined(EVO_RETRO_GENESIS)
    if (bh >= 224) bw = 320;                /* Mega Drive modes: 256 wide fills the same width as 320 */
#endif
    switch (g_mode) {
    case FE_PIC_FULL: g_dh = FE_H; g_dw = (bw * FE_H + bh / 2) / bh; break;
    case FE_PIC_TV:   g_dh = FE_H; g_dw = FE_H * 4 / 3;  break;
    case FE_PIC_WIDE: g_dh = FE_H; g_dw = FE_H * 16 / 9; break;
    default:
        kx = FE_W / bw;
        if (kx < k) k = kx;
        if (k < 1) k = 1;
        if (k > 3) k = 3;
        g_dw = bw * k; g_dh = bh * k;
        break;
    }
    if (g_dw > FE_W) g_dw = FE_W;
    if (g_dh > FE_H) g_dh = FE_H;
    g_dx = (FE_W - g_dw) / 2; g_dy = (FE_H - g_dh) / 2;

    g_nearest = make_map(g_xi, g_xf, g_dw, (int)w);
    (void)make_map(g_yi, g_yf, g_dh, (int)h);
    g_sw = w; g_sh = h;
}

static inline uint32_t mix(uint32_t a, uint32_t b, unsigned f)         /* f = share of b, 1..255 */
{
    uint32_t rb = ((((a & 0x00FF00FFu) * (256u - f)) + ((b & 0x00FF00FFu) * f)) >> 8) & 0x00FF00FFu;
    uint32_t g  = ((((a & 0x0000FF00u) * (256u - f)) + ((b & 0x0000FF00u) * f)) >> 8) & 0x0000FF00u;
    return 0xFF000000u | rb | g;
}

/* Lay the picture out for this source size. Returns 1 when the layout changed (the
 * caller then has to clear what is around the picture). */
int fe_video_prepare(unsigned w, unsigned h)
{
    if (!w || !h || w > SRC_MAX_W) return 0;
    if (g_fmt != RETRO_PIXEL_FORMAT_XRGB8888 && g_lut_fmt != g_fmt) build_lut();
    if (w == g_sw && h == g_sh) return 0;
    layout(w, h);
    return 1;
}

/* source row `sy`, scaled to the picture's width, into `d` */
static void scale_row(uint32_t *d, const void *data, unsigned w, size_t pitch, int sy)
{
    int x;
    if (g_fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
        const uint32_t *s = (const uint32_t *)((const char *)data + (size_t)sy * pitch);
        if (g_nearest) for (x = 0; x < g_dw; x++) d[x] = s[g_xi[x]] | 0xFF000000u;
        else for (x = 0; x < g_dw; x++) {
            unsigned f = g_xf[x], i = g_xi[x];
            d[x] = f ? mix(s[i], s[i + 1], f) : (s[i] | 0xFF000000u);
        }
    } else {
        const uint16_t *s = (const uint16_t *)((const char *)data + (size_t)sy * pitch);
        if (g_nearest) {
            for (x = 0; x < g_dw; x++) d[x] = g_lut[s[g_xi[x]]];
        } else {
            unsigned i;
            for (i = 0; i < w; i++) g_row[i] = g_lut[s[i]];
            for (x = 0; x < g_dw; x++) {
                unsigned f = g_xf[x];
                i = g_xi[x];
                d[x] = f ? mix(g_row[i], g_row[i + 1], f) : g_row[i];
            }
        }
    }
}

/* Scale one core frame into `dst`, a 1440-pixel-wide frame. Writes the picture
 * rectangle only and never reads `dst`. Call fe_video_prepare() first. */
void fe_video_blit(uint32_t *dst, const void *data, unsigned w, unsigned h, size_t pitch)
{
    int r, x, have[2] = { -1, -1 };         /* which source row each line buffer holds */

    if (!dst || !data || w != g_sw || h != g_sh) return;
    if (g_fmt != RETRO_PIXEL_FORMAT_XRGB8888 && !g_lut) return;
    for (r = 0; r < g_dh; r++) {
        int sy = g_yi[r], a, b;
        unsigned f = g_yf[r];
        uint32_t *out = dst + (g_dy + r) * FE_W + g_dx;

        a = have[0] == sy ? 0 : have[1] == sy ? 1 : -1;
        if (a < 0) {                        /* rows only ever move down: reuse the older buffer */
            a = have[0] < have[1] ? 0 : 1;
            scale_row(g_lbuf[a], data, w, pitch, sy);
            have[a] = sy;
        }
        if (!f) { memcpy(out, g_lbuf[a], (size_t)g_dw * 4); continue; }
        b = 1 - a;                          /* a seam between two source rows */
        if (have[b] != sy + 1) { scale_row(g_lbuf[b], data, w, pitch, sy + 1); have[b] = sy + 1; }
        {
            const uint32_t *pa = g_lbuf[a], *pb = g_lbuf[b];
            for (x = 0; x < g_dw; x++) out[x] = mix(pa[x], pb[x], f);
        }
    }
}
