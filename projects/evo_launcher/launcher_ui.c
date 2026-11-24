/* launcher_ui.c - look-and-feel layer of the homebrew launcher (see launcher_ui.h).
 *
 * Portable C: no QNX, Screen or ILM calls, so tools/ui_preview.c can render the
 * exact same frames on the host.
 *
 * Where the look comes from (all read from the head unit at run time):
 *   HMI resource root   /opt/hmi/data/ro            (env EVO_HMI_RO overrides)
 *   ID version + brand  the HU coding persisted on the Jacinto, group 3000:
 *                       byte 29 mask 0x1C = HMI_ID_VERSION, byte 133 mask 0x3C = HMI_BRAND
 *                       (env EVO_CODING_FILE overrides the path)
 *   fonts               the .ttf files in <ro>/<brand>/font, then <ro>/common/font
 *   tile artwork        <ro>/common/assetDB/main_menu_plusplus_heroes/resources/hauptmenu_{a,i}_media{,_rahmen}.png
 *                       (the HMI's own blank "media" main-menu pad and its selection frame)
 *   accent colour       mean of <ro>/common/widgetasset/homebutton/verlauf_home_orange.png
 * Anything that is missing falls back to procedural drawing and built-in colours.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <ctype.h>

#define STBI_ONLY_PNG
#define STBI_NO_SIMD
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "font8x8_basic.h"   /* last-resort bitmap font when the unit has no TTF */
#include "launcher_ui.h"

#define W LUI_W
#define H LUI_H

/* ---------- layout (pixels on the 1440x540 panel) ---------- */
#define VIS       6                    /* entries side by side, like the OEM main menu */
#define TILE      160                  /* tile square (= the HMI media pad) */
#define PITCH     212                  /* tile-to-tile distance */
#define CARD_X    56
#define CARD_Y    46
#define CARD_W    (W - 2 * CARD_X)
#define CARD_H    448
#define CARD_R    16                   /* corner radius */
#define TITLE_Y   (CARD_Y + 50)        /* baselines / tops */
#define RULE_Y    (CARD_Y + 70)
#define LABEL_Y   (CARD_Y + 122)
#define TILE_Y    (CARD_Y + 142)
#define FOOT_Y    (CARD_Y + CARD_H - 24)
#define REFL_ROWS 56                   /* height of the floor reflection under a tile */
#define REFL_GAP  6
/* the HMI pad artwork is a 270x450 canvas with the 160x160 square at (55,176) */
#define ART_OX    55
#define ART_OY    176

enum { STYLE_ID5 = 0, STYLE_FLAT = 1 };

struct img { int w, h; uint32_t *px; };

struct glyph { unsigned char *bm; short w, h, xoff, yoff; float adv; char have; };
#define GC_MAX 8
struct gcache { int px; float scale; struct glyph g[256]; };
struct face {
    unsigned char *data; stbtt_fontinfo info; int ok;
    struct gcache *gc[GC_MAX]; int ngc;
    char file[64];
};

struct lui {
    /* theme */
    char ro[200], gen[16], brand_dir[16], desc[400];
    int  idver, brand, style;
    uint32_t c_text, c_dim, c_sel, c_accent, c_card_top, c_card_bot, c_edge, c_backdrop;
    unsigned backdrop_a;
    struct face f_reg, f_light;
    struct img tile_i, tile_a, frame_i, frame_a;
    int have_tiles;
    /* content */
    const struct lui_item *items; int n;
    /* state */
    uint32_t *base; int base_ok, base_first;
    int first, last_sel; unsigned sel_t0;
};

/* ================= pixel helpers ================= */
static inline unsigned div255(unsigned x) { x += 128; return (x + (x >> 8)) >> 8; }
static inline unsigned clampu(int v, int lo, int hi) { return (unsigned)(v < lo ? lo : (v > hi ? hi : v)); }

/* src colour (rgb, straight alpha sa) OVER the framebuffer pixel, which may itself
 * be translucent (the dimmed backdrop) - the ILM compositor blends it onto the HMI. */
static inline void px_over(uint32_t *d, uint32_t rgb, unsigned sa) {
    uint32_t D; unsigned da, ia, r, g, b;
    if (!sa) return;
    if (sa >= 255) { *d = 0xFF000000u | (rgb & 0x00FFFFFFu); return; }
    D = *d; da = D >> 24; ia = 255 - sa;
    if (da == 255) {
        r = div255(((rgb >> 16) & 255) * sa + ((D >> 16) & 255) * ia);
        g = div255(((rgb >> 8) & 255) * sa + ((D >> 8) & 255) * ia);
        b = div255((rgb & 255) * sa + (D & 255) * ia);
        *d = 0xFF000000u | (r << 16) | (g << 8) | b;
    } else if (da == 0) {
        *d = (sa << 24) | (rgb & 0x00FFFFFFu);
    } else {
        unsigned dw = div255(da * ia), oa = sa + dw;
        r = (((rgb >> 16) & 255) * sa + ((D >> 16) & 255) * dw) / oa;
        g = (((rgb >> 8) & 255) * sa + ((D >> 8) & 255) * dw) / oa;
        b = ((rgb & 255) * sa + (D & 255) * dw) / oa;
        *d = (oa << 24) | (r << 16) | (g << 8) | b;
    }
}
static inline uint32_t mix(uint32_t a, uint32_t b, unsigned t /*0..255*/) {
    unsigned it = 255 - t;
    unsigned r = div255(((a >> 16) & 255) * it + ((b >> 16) & 255) * t);
    unsigned g = div255(((a >> 8) & 255) * it + ((b >> 8) & 255) * t);
    unsigned bl = div255((a & 255) * it + (b & 255) * t);
    return (r << 16) | (g << 8) | bl;
}
static void fill_rect(uint32_t *fb, int x, int y, int w, int h, uint32_t rgb, unsigned a) {
    int i, j;
    for (j = y; j < y + h; j++) {
        if ((unsigned)j >= H) continue;
        for (i = x; i < x + w; i++) if ((unsigned)i < W) px_over(&fb[j * W + i], rgb, a);
    }
}

/* blit a straight-alpha image, scaled by a global alpha (0..255) */
static void blit(uint32_t *fb, int dx, int dy, const struct img *im, unsigned ga) {
    int x, y;
    if (!im->px || !ga) return;
    for (y = 0; y < im->h; y++) {
        int py = dy + y; const uint32_t *s; uint32_t *d;
        if ((unsigned)py >= H) continue;
        s = im->px + y * im->w; d = fb + py * W;
        for (x = 0; x < im->w; x++) {
            int pxx = dx + x; unsigned a = s[x] >> 24;
            if (!a || (unsigned)pxx >= W) continue;
            px_over(&d[pxx], s[x], ga >= 255 ? a : div255(a * ga));
        }
    }
}
/* an icon is the caller's pixel block, not a struct img */
static void blit_px(uint32_t *fb, int dx, int dy, const uint32_t *px, int w, int h, unsigned ga) {
    struct img im; im.w = w; im.h = h; im.px = (uint32_t *)px;
    blit(fb, dx, dy, &im, ga);
}
/* Floor reflection: mirror whatever is already drawn in the tile square (pad, icon,
 * selection frame) into the rows below it, fading out. Source pixels are opaque. */
static void reflect_tile(uint32_t *fb, int x, int y) {
    int k, i;
    for (k = 0; k < REFL_ROWS; k++) {
        int sy = y + TILE - 1 - k, dy = y + TILE + REFL_GAP + k;
        unsigned f = (unsigned)(REFL_ROWS - k) * 255 / REFL_ROWS, a = div255(84 * div255(f * f));
        if ((unsigned)sy >= H || (unsigned)dy >= H || !a) continue;
        for (i = x - 1; i <= x + TILE; i++)
            if ((unsigned)i < W) px_over(&fb[dy * W + i], fb[sy * W + i], a);
    }
}

/* signed distance from a pixel centre to a rounded rectangle (negative inside) */
static float rr_dist(float px, float py, float x0, float y0, float x1, float y1, float r) {
    float cx = px < x0 + r ? x0 + r : (px > x1 - r ? x1 - r : px);
    float cy = py < y0 + r ? y0 + r : (py > y1 - r ? y1 - r : py);
    float dx = px - cx, dy = py - cy;
    return (float)sqrt(dx * dx + dy * dy) - r;
}
/* rounded rectangle, vertical gradient top->bot, anti-aliased edge, alpha a */
static void fill_rrect(uint32_t *fb, int x, int y, int w, int h, int r, uint32_t top, uint32_t bot, unsigned a) {
    int i, j;
    for (j = y; j < y + h; j++) {
        uint32_t c; int edge_row;
        if ((unsigned)j >= H) continue;
        c = mix(top, bot, (unsigned)(j - y) * 255 / (h > 1 ? h - 1 : 1));
        edge_row = (j < y + r || j >= y + h - r);
        for (i = x; i < x + w; i++) {
            unsigned cov = 255;
            if ((unsigned)i >= W) continue;
            if (edge_row && (i < x + r || i >= x + w - r)) {
                float d = rr_dist(i + 0.5f, j + 0.5f, (float)x, (float)y, (float)(x + w), (float)(y + h), (float)r);
                if (d >= 0.5f) continue;
                if (d > -0.5f) cov = (unsigned)((0.5f - d) * 255.0f);
            }
            px_over(&fb[j * W + i], c, cov >= 255 ? a : div255(cov * a));
        }
    }
}
/* a line of the given thickness following the rounded-rectangle outline; inner > 0
 * extends a soft glow outwards over `glow` pixels */
static void stroke_rrect(uint32_t *fb, int x, int y, int w, int h, int r, float th, float glow, uint32_t rgb, unsigned a) {
    int i, j, m = (int)(glow + th + 2);
    for (j = y - m; j < y + h + m; j++) {
        if ((unsigned)j >= H) continue;
        for (i = x - m; i < x + w + m; i++) {
            float d, cov;
            if ((unsigned)i >= W) continue;
            /* skip the interior quickly */
            if (i > x + m && i < x + w - m && j > y + m && j < y + h - m) { i = x + w - m - 1; continue; }
            d = rr_dist(i + 0.5f, j + 0.5f, (float)x, (float)y, (float)(x + w), (float)(y + h), (float)r);
            if (d <= 0.0f) cov = d > -th ? 1.0f : (d > -th - 1.0f ? d + th + 1.0f : 0.0f);
            else if (glow > 0.0f && d < glow) { float t = 1.0f - d / glow; cov = t * t * 0.55f; }
            else cov = d < 1.0f ? 1.0f - d : 0.0f;
            if (cov <= 0.0f) continue;
            px_over(&fb[j * W + i], rgb, (unsigned)(cov * a));
        }
    }
}
/* soft drop shadow around a rounded rectangle (drawn outside it only) */
static void shadow_rrect(uint32_t *fb, int x, int y, int w, int h, int r, int spread, unsigned amax) {
    int i, j;
    for (j = y - spread; j < y + h + spread; j++) {
        if ((unsigned)j >= H) continue;
        for (i = x - spread; i < x + w + spread; i++) {
            float d, t;
            if ((unsigned)i >= W) continue;
            if (i >= x + r && i < x + w - r && j >= y && j < y + h) { i = x + w - r - 1; continue; }
            d = rr_dist(i + 0.5f, j + 0.5f, (float)x, (float)y, (float)(x + w), (float)(y + h), (float)r);
            if (d <= 0.0f || d >= (float)spread) continue;
            t = 1.0f - d / (float)spread;
            px_over(&fb[j * W + i], 0x000000, (unsigned)(t * t * (float)amax));
        }
    }
}
/* horizontal hairline that fades out towards both ends */
static void hline_fade(uint32_t *fb, int x, int y, int w, int fade, uint32_t rgb, unsigned a) {
    int i;
    if ((unsigned)y >= H) return;
    for (i = 0; i < w; i++) {
        int e = i < w - 1 - i ? i : w - 1 - i; unsigned k = e >= fade ? 255 : (unsigned)e * 255 / (unsigned)fade;
        if ((unsigned)(x + i) < W) px_over(&fb[y * W + x + i], rgb, div255(a * k));
    }
}

/* ================= images ================= */
static int img_load(struct img *im, const char *path) {
    int w = 0, h = 0, n = 0, i; unsigned char *d;
    im->px = NULL; im->w = im->h = 0;
    d = stbi_load(path, &w, &h, &n, 4);
    if (!d) return -1;
    im->px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!im->px) { stbi_image_free(d); return -1; }
    for (i = 0; i < w * h; i++)
        im->px[i] = ((uint32_t)d[i * 4 + 3] << 24) | ((uint32_t)d[i * 4] << 16) | ((uint32_t)d[i * 4 + 1] << 8) | d[i * 4 + 2];
    stbi_image_free(d);
    im->w = w; im->h = h;
    return 0;
}
static void img_free(struct img *im) { if (im->px) free(im->px); im->px = NULL; im->w = im->h = 0; }
static int img_mean(const struct img *im, uint32_t *rgb) {
    unsigned long long r = 0, g = 0, b = 0, n = 0; int i;
    for (i = 0; i < im->w * im->h; i++) {
        uint32_t p = im->px[i]; if ((p >> 24) < 128) continue;
        r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
    }
    if (!n) return -1;
    *rgb = ((uint32_t)(r / n) << 16) | ((uint32_t)(g / n) << 8) | (uint32_t)(b / n);
    return 0;
}

/* ================= text ================= */
static int face_load(struct face *f, const char *path) {
    FILE *fp; long sz; const char *bn;
    memset(f, 0, sizeof(*f));
    fp = fopen(path, "rb"); if (!fp) return -1;
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < 1000 || sz > 16 * 1024 * 1024) { fclose(fp); return -1; }
    f->data = (unsigned char *)malloc((size_t)sz);
    if (!f->data || fread(f->data, 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); free(f->data); f->data = NULL; return -1; }
    fclose(fp);
    if (!stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) { free(f->data); f->data = NULL; return -1; }
    bn = strrchr(path, '/'); snprintf(f->file, sizeof(f->file), "%s", bn ? bn + 1 : path);
    f->ok = 1;
    return 0;
}
static void face_free(struct face *f) {
    int i, k;
    for (i = 0; i < f->ngc; i++) {
        for (k = 0; k < 256; k++) if (f->gc[i]->g[k].bm) stbtt_FreeBitmap(f->gc[i]->g[k].bm, NULL);
        free(f->gc[i]);
    }
    if (f->data) free(f->data);
    memset(f, 0, sizeof(*f));
}
static struct gcache *face_cache(struct face *f, int px) {
    int i; struct gcache *c;
    for (i = 0; i < f->ngc; i++) if (f->gc[i]->px == px) return f->gc[i];
    if (f->ngc >= GC_MAX) return f->gc[0];
    c = (struct gcache *)calloc(1, sizeof(*c)); if (!c) return NULL;
    c->px = px; c->scale = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
    f->gc[f->ngc++] = c;
    return c;
}
static struct glyph *face_glyph(struct face *f, struct gcache *c, int cp) {
    struct glyph *g = &c->g[cp & 255];
    if (!g->have) {
        int w = 0, h = 0, xo = 0, yo = 0, adv = 0, lsb = 0;
        g->bm = stbtt_GetCodepointBitmap(&f->info, c->scale, c->scale, cp, &w, &h, &xo, &yo);
        stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
        g->w = (short)w; g->h = (short)h; g->xoff = (short)xo; g->yoff = (short)yo;
        g->adv = (float)adv * c->scale; g->have = 1;
    }
    return g;
}
/* minimal UTF-8: 1- and 2-byte sequences (Latin-1 range); anything else -> '?' */
static int next_cp(const char **s) {
    const unsigned char *p = (const unsigned char *)*s; int c = *p++;
    if (c >= 0xC0 && c < 0xE0 && (*p & 0xC0) == 0x80) { c = ((c & 0x1F) << 6) | (*p++ & 0x3F); }
    else if (c >= 0x80) { while ((*p & 0xC0) == 0x80) p++; c = '?'; }
    *s = (const char *)p;
    return c > 255 ? '?' : c;
}
/* draw (fb != NULL) or just measure; returns the advance width in pixels.
 * y is the BASELINE. alpha 0..255. */
static int text_run(uint32_t *fb, struct face *f, int px, int x, int y, const char *str, uint32_t rgb, unsigned alpha) {
    float pen = 0.0f; int prev = 0;
    if (!f->ok) {                                   /* bitmap fallback */
        int s = px / 11, n = 0; if (s < 1) s = 1;
        for (; *str; str++, n++) {
            if (fb) {
                const char *g = font8x8_basic[(unsigned char)*str & 0x7f]; int r, b;
                for (r = 0; r < 8; r++) for (b = 0; b < 8; b++) if ((unsigned char)g[r] & (1u << b))
                    fill_rect(fb, x + n * 8 * s + b * s, y - 7 * s + r * s, s, s, rgb, alpha);
            }
        }
        return n * 8 * s;
    } else {
        struct gcache *c = face_cache(f, px);
        if (!c) return 0;
        while (*str) {
            int cp = next_cp(&str); struct glyph *g = face_glyph(f, c, cp);
            if (prev) pen += (float)stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * c->scale;
            if (fb && g->bm) {
                int gx = x + (int)floor(pen + 0.5f) + g->xoff, gy = y + g->yoff, i, j;
                for (j = 0; j < g->h; j++) {
                    int py = gy + j; if ((unsigned)py >= H) continue;
                    for (i = 0; i < g->w; i++) {
                        unsigned cov = g->bm[j * g->w + i]; int pxx = gx + i;
                        if (!cov || (unsigned)pxx >= W) continue;
                        px_over(&fb[py * W + pxx], rgb, alpha >= 255 ? cov : div255(cov * alpha));
                    }
                }
            }
            pen += g->adv; prev = cp;
        }
        return (int)(pen + 0.5f);
    }
}
static int text_w(struct face *f, int px, const char *s) { return text_run(NULL, f, px, 0, 0, s, 0, 0); }
static void text_at(uint32_t *fb, struct face *f, int px, int x, int y, const char *s, uint32_t rgb, unsigned a) {
    text_run(fb, f, px, x, y, s, rgb, a);
}
/* centred on cx; shrinks, then truncates with "..", to fit maxw */
static void text_fit_center(uint32_t *fb, struct face *f, int px, int cx, int y, int maxw, const char *s, uint32_t rgb, unsigned a) {
    char buf[72]; int w, n;
    snprintf(buf, sizeof(buf), "%s", s);
    w = text_w(f, px, buf);
    while (w > maxw && px > 19) { px -= 2; w = text_w(f, px, buf); }
    n = (int)strlen(buf);
    while (w > maxw && n > 3) { n--; buf[n] = 0; buf[n - 1] = '.'; buf[n - 2] = '.'; w = text_w(f, px, buf); }
    text_at(fb, f, px, cx - w / 2, y, buf, rgb, a);
}

/* ================= theme detection ================= */
#define CODING_DEFAULT "/net/hu-jacinto/var/opt/sys/persistence/early/pers_NbtEvoJacintoDiagHUHighAppCodingController"

/* The HU coding as persisted by the coding controller: somewhere after a header,
 * [u32 count] then count x ([u32 len][len bytes]) coding groups 3000, 3001, ...
 * running to (almost) the end of the file. Find that chain instead of trusting a
 * fixed offset. Returns 0 and fills idver/brand on success. */
static int coding_parse(const unsigned char *d, int n, int *idver, int *brand) {
    int p;
    for (p = 8; p + 8 < n; p++) {
        unsigned cnt = d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((unsigned)d[p + 3] << 24), i;
        int o = p + 4, first = -1, ok = 1;
        if (cnt < 4 || cnt > 64) continue;
        for (i = 0; i < cnt; i++) {
            unsigned l;
            if (o + 4 > n) { ok = 0; break; }
            l = d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((unsigned)d[o + 3] << 24);
            if (l < 1 || l > 1024 || o + 4 + (int)l > n) { ok = 0; break; }
            if (i == 0) { if (l < 134) { ok = 0; break; } first = o + 4; }
            o += 4 + (int)l;
        }
        if (!ok || n - o > 16) continue;
        *idver = (d[first + 29] & 0x1C) >> 2;
        *brand = (d[first + 133] & 0x3C) >> 2;
        return 0;
    }
    return -1;
}
struct coding_job { char path[256]; volatile int done; int ok, idver, brand; };
static void *coding_thread(void *arg) {
    struct coding_job *j = (struct coding_job *)arg;
    unsigned char buf[8192]; int n = 0; FILE *f = fopen(j->path, "rb");
    if (f) { n = (int)fread(buf, 1, sizeof(buf), f); fclose(f); }
    if (n > 200 && coding_parse(buf, n, &j->idver, &j->brand) == 0) j->ok = 1;
    j->done = 1;
    return NULL;
}
/* The file sits on the other processor (qnet). Read it on a helper thread and give
 * up after 1.5 s so a missing Jacinto can never hang the launcher. */
static int coding_read(int *idver, int *brand) {
    struct coding_job *j = (struct coding_job *)calloc(1, sizeof(*j)); pthread_t t; int i;
    const char *p = getenv("EVO_CODING_FILE");
    if (!j) return -1;
    snprintf(j->path, sizeof(j->path), "%s", (p && *p) ? p : CODING_DEFAULT);
    if (pthread_create(&t, NULL, coding_thread, j) != 0) { free(j); return -1; }
    pthread_detach(t);
    for (i = 0; i < 150 && !j->done; i++) usleep(10000);
    if (!j->done) return -1;                        /* leak j on purpose: the thread still owns it */
    if (j->ok) { *idver = j->idver; *brand = j->brand; free(j); return 0; }
    free(j);
    return -1;
}

static int ends_ci(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf), i;
    if (b > a) return 0;
    for (i = 0; i < b; i++) if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)suf[i])) return 0;
    return 1;
}
/* first *.ttf in dir whose name ends with `suffix` (e.g. "RG.ttf"); suffix ".ttf" = any */
static int find_ttf(const char *dir, const char *suffix, char *out, size_t outsz) {
    DIR *d = opendir(dir); struct dirent *e; int found = 0;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL)
        if (ends_ci(e->d_name, suffix)) { snprintf(out, outsz, "%s/%s", dir, e->d_name); found = 1; break; }
    closedir(d);
    return found ? 0 : -1;
}
static void load_fonts(lui_t *u) {
    char dirs[2][256], path[400]; int i;
    const char *ef = getenv("EVO_FONT");
    snprintf(dirs[0], sizeof(dirs[0]), "%s/%s/font", u->ro, u->brand_dir);
    snprintf(dirs[1], sizeof(dirs[1]), "%s/common/font", u->ro);
    if (ef && *ef) face_load(&u->f_reg, ef);
    for (i = 0; i < 2 && !u->f_reg.ok; i++)   if (find_ttf(dirs[i], "RG.ttf", path, sizeof(path)) == 0) face_load(&u->f_reg, path);
    for (i = 0; i < 2 && !u->f_light.ok; i++) if (find_ttf(dirs[i], "LG.ttf", path, sizeof(path)) == 0) face_load(&u->f_light, path);
    for (i = 0; i < 2 && !u->f_reg.ok; i++)   if (find_ttf(dirs[i], ".ttf", path, sizeof(path)) == 0) face_load(&u->f_reg, path);
    if (!u->f_light.ok && u->f_reg.ok) {      /* one weight only: use it for both */
        snprintf(path, sizeof(path), "%s/%s", dirs[0], u->f_reg.file);
        if (face_load(&u->f_light, path) != 0) { snprintf(path, sizeof(path), "%s/%s", dirs[1], u->f_reg.file); face_load(&u->f_light, path); }
    }
    if (!u->f_reg.ok && u->f_light.ok) {
        snprintf(path, sizeof(path), "%s/%s", dirs[0], u->f_light.file);
        if (face_load(&u->f_reg, path) != 0) { snprintf(path, sizeof(path), "%s/%s", dirs[1], u->f_light.file); face_load(&u->f_reg, path); }
    }
}
static void load_tiles(lui_t *u) {
    static const char *dir = "common/assetDB/main_menu_plusplus_heroes/resources";
    char p[400]; int ok = 1;
    snprintf(p, sizeof(p), "%s/%s/hauptmenu_i_media.png", u->ro, dir);        ok &= img_load(&u->tile_i, p) == 0;
    snprintf(p, sizeof(p), "%s/%s/hauptmenu_a_media.png", u->ro, dir);        ok &= img_load(&u->tile_a, p) == 0;
    snprintf(p, sizeof(p), "%s/%s/hauptmenu_i_media_rahmen.png", u->ro, dir); ok &= img_load(&u->frame_i, p) == 0;
    snprintf(p, sizeof(p), "%s/%s/hauptmenu_a_media_rahmen.png", u->ro, dir); ok &= img_load(&u->frame_a, p) == 0;
    /* the layout assumes the 270x450 pad canvas */
    if (ok && (u->tile_i.w != 270 || u->tile_i.h != 450 || u->frame_a.w != 270 || u->frame_a.h != 450)) ok = 0;
    if (!ok) { img_free(&u->tile_i); img_free(&u->tile_a); img_free(&u->frame_i); img_free(&u->frame_a); }
    u->have_tiles = ok;
}

static const char *idver_name(int v) {
    static const char *n[] = { "ID4++", "ID5", "ID5++", "ID6 Light", "ID6" };
    return (v >= 0 && v <= 4) ? n[v] : "unknown";
}
static const char *brand_name(int b) {
    static const char *n[] = { "BMW", "BMW i", "MINI", "Rolls-Royce", "BMW M", "Supra" };
    return (b >= 0 && b <= 5) ? n[b] : "unknown";
}

lui_t *lui_create(const char *override) {
    lui_t *u = (lui_t *)calloc(1, sizeof(*u)); char rp[PATH_MAX]; const char *ro, *g; struct img sw; int coded;
    if (!u) return NULL;
    u->base = (uint32_t *)malloc((size_t)W * H * 4);
    if (!u->base) { free(u); return NULL; }
    u->last_sel = -1;

    /* 1) resource root + HMI generation (the /opt/hmi/data symlink names it: .../ID5/data) */
    ro = getenv("EVO_HMI_RO"); if (!ro || !*ro) ro = "/opt/hmi/data/ro";
    snprintf(u->ro, sizeof(u->ro), "%s", ro);
    snprintf(u->gen, sizeof(u->gen), "?");
    if (realpath(ro, rp) && (g = strstr(rp, "/ID")) != NULL) {
        int k = 0; g++;
        while (g[k] && g[k] != '/' && k < (int)sizeof(u->gen) - 1) { u->gen[k] = g[k]; k++; }
        u->gen[k] = 0;
    }

    /* 2) ID version + brand from the HU coding */
    u->idver = -1; u->brand = -1;
    coded = coding_read(&u->idver, &u->brand) == 0;
    snprintf(u->brand_dir, sizeof(u->brand_dir), "%s",
             u->brand == 3 ? "rr" : u->brand == 4 ? "bmwm" : u->brand == 2 ? "mini" : "bmw");

    /* 3) palette. Text colours are the ID5 list colours measured off the live HMI. */
    u->c_text = 0x97B6BF; u->c_dim = 0x74898F; u->c_sel = 0xFFFFFF;
    u->c_accent = 0xFF7937; u->c_card_top = 0x1B2126; u->c_card_bot = 0x090B0E; u->c_edge = 0x3B4850;
    u->c_backdrop = 0x04060A; u->backdrop_a = 0xCC;
    snprintf(rp, sizeof(rp), "%s/common/widgetasset/homebutton/verlauf_home_orange.png", u->ro);
    if (img_load(&sw, rp) == 0) { uint32_t c; if (img_mean(&sw, &c) == 0) u->c_accent = c; img_free(&sw); }

    /* 4) fonts + tile artwork from the unit */
    load_fonts(u);
    load_tiles(u);

    /* 5) style: the glossy pad look belongs to ID4++/ID5/ID5++; ID6 lines are flat */
    if (override && strcmp(override, "flat") == 0)      u->style = STYLE_FLAT;
    else if (override && strcmp(override, "id5") == 0)  u->style = u->have_tiles ? STYLE_ID5 : STYLE_FLAT;
    else u->style = (u->have_tiles && u->idver < 3) ? STYLE_ID5 : STYLE_FLAT;
    if (u->style == STYLE_FLAT && u->idver >= 3) { u->c_text = 0xC8D2D8; u->c_dim = 0x8996A0; u->c_card_top = 0x1C2228; }

    snprintf(u->desc, sizeof(u->desc),
             "style=%s hmi=%s coding=%s%s%s%s font=%s/%s tiles=%s accent=#%06X",
             u->style == STYLE_ID5 ? "id5-glossy" : "flat", u->gen,
             coded ? idver_name(u->idver) : "unreadable", coded ? " / " : "", coded ? brand_name(u->brand) : "",
             (override && *override && strcmp(override, "auto")) ? " (override)" : "",
             u->f_reg.ok ? u->f_reg.file : "bitmap", u->f_light.ok ? u->f_light.file : "bitmap",
             u->have_tiles ? "hmi-assets" : "procedural", (unsigned)u->c_accent);
    return u;
}
void lui_destroy(lui_t *u) {
    if (!u) return;
    face_free(&u->f_reg); face_free(&u->f_light);
    img_free(&u->tile_i); img_free(&u->tile_a); img_free(&u->frame_i); img_free(&u->frame_a);
    free(u->base); free(u);
}
const char *lui_theme_desc(const lui_t *u) { return u->desc; }
int lui_visible_count(const lui_t *u) { (void)u; return VIS; }
void lui_set_items(lui_t *u, const struct lui_item *items, int n) {
    u->items = items; u->n = n < 0 ? 0 : n; u->base_ok = 0; u->first = 0; u->last_sel = -1;
}

/* ================= page ================= */
/* accent light on the "floor" under the selected tile - the OEM main menu marks its
 * selected pad the same way */
static void floor_glow(uint32_t *fb, int cx, int y, uint32_t rgb, unsigned strength) {
    int hw = 118, hh = 9, i, j;
    for (j = -hh; j <= hh; j++) {
        int py = y + j; unsigned ay = (unsigned)(hh - (j < 0 ? -j : j)) * 255 / (unsigned)hh;
        if ((unsigned)py >= H) continue;
        ay = div255(ay * ay);
        for (i = -hw; i <= hw; i++) {
            int px = cx + i; unsigned ax = (unsigned)(hw - (i < 0 ? -i : i)) * 255 / (unsigned)hw;
            if ((unsigned)px >= W) continue;
            ax = div255(ax * ax);
            if (j == 0) px_over(&fb[py * W + px], mix(rgb, 0xFFFFFF, 70), div255(ax * strength));
            else        px_over(&fb[py * W + px], rgb, div255(div255(ax * ay) * strength) / 2);
        }
    }
}
static int tile_x(const lui_t *u, int vis) {
    int nv = u->n < VIS ? u->n : VIS, span;
    if (nv < 1) nv = 1;
    span = (nv - 1) * PITCH + TILE;
    return CARD_X + (CARD_W - span) / 2 + vis * PITCH;
}
/* one tile: pad, frame, icon. active = selected look, glow = 0..255 strength of the frame */
static void draw_tile(lui_t *u, uint32_t *fb, int x, int y, const struct lui_item *it, int active, unsigned glow) {
    int iw = it ? it->icon_w : 0, ih = it ? it->icon_h : 0;
    if (u->style == STYLE_ID5) {
        blit(fb, x - ART_OX, y - ART_OY, active ? &u->tile_a : &u->tile_i, 255);
        if (it && it->icon_px && iw <= TILE && ih <= TILE) blit_px(fb, x + (TILE - iw) / 2, y + (TILE - ih) / 2, it->icon_px, iw, ih, 255);
        if (active) blit(fb, x - ART_OX, y - ART_OY, &u->frame_a, glow);
        else        blit(fb, x - ART_OX, y - ART_OY, &u->frame_i, 255);
    } else {
        fill_rrect(fb, x, y, TILE, TILE, 12, active ? 0x2A333A : 0x20272D, active ? 0x161B20 : 0x12161A, 255);
        if (it && it->icon_px && iw <= TILE && ih <= TILE) blit_px(fb, x + (TILE - iw) / 2, y + (TILE - ih) / 2, it->icon_px, iw, ih, 255);
        if (active) stroke_rrect(fb, x, y, TILE, TILE, 12, 2.0f, 12.0f, u->c_accent, glow);
        else        stroke_rrect(fb, x, y, TILE, TILE, 12, 1.0f, 0.0f, u->c_edge, 200);
    }
    if (it && !it->icon_px)                         /* placeholder when an app has no icon */
        fill_rrect(fb, x + 48, y + 48, 64, 64, 10, active ? u->c_accent : 0x3A444B, active ? u->c_accent : 0x2A3238, 255);
}
/* "<" (dir -1) or ">" (dir +1), 24 px tall, tip at x */
static void draw_chevron(uint32_t *fb, int x, int y, int dir, uint32_t rgb, unsigned a) {
    int i, t;
    for (i = -12; i <= 12; i++) {
        int off = i < 0 ? -i : i;
        for (t = 0; t < 3; t++) {
            int px = x - dir * (off + t), py = y + i;
            if ((unsigned)px < W && (unsigned)py < H) px_over(&fb[py * W + px], rgb, a);
        }
    }
}

/* everything that does not depend on the selection, drawn once per page of tiles */
static void build_base(lui_t *u) {
    uint32_t *fb = u->base; int i, v;
    uint32_t dimpx = ((uint32_t)u->backdrop_a << 24) | u->c_backdrop;
    for (i = 0; i < W * H; i++) fb[i] = dimpx;

    shadow_rrect(fb, CARD_X, CARD_Y + 6, CARD_W, CARD_H, CARD_R, 30, 150);
    fill_rrect(fb, CARD_X, CARD_Y, CARD_W, CARD_H, CARD_R, u->c_card_top, u->c_card_bot, 255);
    stroke_rrect(fb, CARD_X, CARD_Y, CARD_W, CARD_H, CARD_R, 1.0f, 0.0f, u->c_edge, 150);
    hline_fade(fb, CARD_X + CARD_R, CARD_Y + 1, CARD_W - 2 * CARD_R, 260, 0x8FA6B0, 90);   /* top light edge */

    /* header: accent tick + title, like an HMI screen title */
    fill_rrect(fb, CARD_X + 30, TITLE_Y - 27, 4, 30, 2, u->c_accent, u->c_accent, 255);
    text_at(fb, &u->f_light, 36, CARD_X + 46, TITLE_Y, "Homebrew", u->c_text, 255);
    hline_fade(fb, CARD_X + 24, RULE_Y, CARD_W - 48, 200, u->c_edge, 255);
    if (u->n > 0) {
        char cnt[32]; int w;
        snprintf(cnt, sizeof(cnt), u->n == 1 ? "%d app" : "%d apps", u->n);
        w = text_w(&u->f_reg, 22, cnt);
        text_at(fb, &u->f_reg, 22, CARD_X + CARD_W - 34 - w, TITLE_Y - 2, cnt, u->c_dim, 255);
    }

    for (v = 0; v < VIS && u->first + v < u->n; v++) {
        const struct lui_item *it = &u->items[u->first + v]; int x = tile_x(u, v);
        draw_tile(u, fb, x, TILE_Y, it, 0, 0);
    }
    if (u->n == 0)
        text_fit_center(fb, &u->f_reg, 26, W / 2, TILE_Y + 90, CARD_W - 80, "No apps installed", u->c_dim, 255);

    /* footer: how to drive it, in the HMI's dim header colour */
    {
        static const char *hint = "Turn to select      Press to start      Tilt up or down to close";
        int w = text_w(&u->f_reg, 20, hint);
        text_at(fb, &u->f_reg, 20, W / 2 - w / 2, FOOT_Y, hint, u->c_dim, 255);
    }
    if (u->first > 0)            draw_chevron(fb, CARD_X + 22, TILE_Y + TILE / 2, -1, u->c_text, 230);
    if (u->first + VIS < u->n)   draw_chevron(fb, CARD_X + CARD_W - 22, TILE_Y + TILE / 2, 1, u->c_text, 230);
    u->base_first = u->first; u->base_ok = 1;
}

void lui_render_page(lui_t *u, uint32_t *fb, int sel, unsigned now_ms, unsigned alpha) {
    int i;
    if (sel < 0) sel = 0;
    if (sel >= u->n) sel = u->n - 1;
    /* keep the selection inside the visible window (scrolls one tile at a time) */
    if (sel >= 0) {
        if (sel < u->first) u->first = sel;
        if (sel > u->first + VIS - 1) u->first = sel - VIS + 1;
    }
    if (u->first < 0) u->first = 0;
    if (!u->base_ok || u->base_first != u->first) build_base(u);
    if (sel != u->last_sel) { u->last_sel = sel; u->sel_t0 = now_ms; }
    memcpy(fb, u->base, (size_t)W * H * 4);

    if (sel >= 0 && sel < u->n) {
        const struct lui_item *it = &u->items[sel]; int x = tile_x(u, sel - u->first);
        unsigned dt = now_ms - u->sel_t0, glow = dt >= 160 ? 255 : 90 + dt * 165 / 160;   /* frame eases in */
        draw_tile(u, fb, x, TILE_Y, it, 1, glow);
        floor_glow(fb, x + TILE / 2, TILE_Y + TILE + REFL_GAP + REFL_ROWS + 4, u->c_accent, glow);
    }
    for (i = 0; i < VIS && u->first + i < u->n; i++) {
        int idx = u->first + i, x = tile_x(u, i), on = idx == sel;
        reflect_tile(fb, x, TILE_Y);
        text_fit_center(fb, &u->f_reg, on ? 29 : 26, x + TILE / 2, LABEL_Y, PITCH - 10, u->items[idx].name,
                        on ? u->c_sel : u->c_text, 255);
    }
    /* position dots, only when the row scrolls */
    if (u->n > VIS) {
        int span = (u->n - 1) * 14, x0 = W / 2 - span / 2, y = FOOT_Y - 36;
        for (i = 0; i < u->n; i++)
            fill_rrect(fb, x0 + i * 14 - 3, y - 3, 6, 6, 3, i == sel ? u->c_accent : u->c_edge, i == sel ? u->c_accent : u->c_edge, 255);
    }
    if (alpha < 255)                                  /* whole-page fade */
        for (i = 0; i < W * H; i++) { uint32_t p = fb[i]; fb[i] = (div255((p >> 24) * alpha) << 24) | (p & 0x00FFFFFFu); }
}

/* ================= splash ================= */
void lui_render_splash(lui_t *u, uint32_t *fb, const struct lui_item *it, unsigned t_ms) {
    int j, i, ty = 96, tx = W / 2 - TILE / 2, bw = 360, bx = W / 2 - 180, by = 452, seg = 110, pos;
    const char *name = (it && it->name) ? it->name : "";
    for (j = 0; j < H; j++) {                          /* opaque: it has to cover the HMI while the app starts */
        uint32_t c = 0xFF000000u | mix(0x141A1F, 0x040506, (unsigned)j * 255 / (H - 1));
        for (i = 0; i < W; i++) fb[j * W + i] = c;
    }
    draw_tile(u, fb, tx, ty, it, 1, 255);
    reflect_tile(fb, tx, ty);
    text_fit_center(fb, &u->f_light, 42, W / 2, ty + TILE + 116, W - 200, name, u->c_sel, 255);
    {
        static const char *s = "Starting";
        int w = text_w(&u->f_reg, 22, s);
        text_at(fb, &u->f_reg, 22, W / 2 - w / 2, ty + TILE + 152, s, u->c_dim, 255);
    }
    /* indeterminate progress: an accent segment sweeping a hairline track */
    fill_rect(fb, bx, by, bw, 2, u->c_edge, 255);
    pos = (int)((t_ms % 1300u) * (unsigned)(bw + seg) / 1300u) - seg;
    for (i = 0; i < seg; i++) {
        int x = bx + pos + i, e = i < seg - 1 - i ? i : seg - 1 - i; unsigned a = e >= 30 ? 255 : (unsigned)e * 255 / 30;
        if (x < bx || x >= bx + bw) continue;
        px_over(&fb[by * W + x], u->c_accent, a); px_over(&fb[(by + 1) * W + x], u->c_accent, a);
    }
}
