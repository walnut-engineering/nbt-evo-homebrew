/* retro_ui.c - the frontend's own screens: the ROM list and the in-game menu.
 *
 * Both are driven the same way from every input: D-pad / stick / knob tilt or knob
 * rotation to move, the bottom button (Xbox A, knob press, Z) or Start to choose, the
 * right button (Xbox B, OPTION, X) or the menu button (Xbox button, BACK, Esc) to go
 * back. LB / RB page through a long list.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>

#include "font8x8_basic.h"
#include "retro_fe.h"

/* ======================================================= framebuffer helpers */
void fe_fill(uint32_t *fb, int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FE_W) w = FE_W - x;
    if (y + h > FE_H) h = FE_H - y;
    for (j = 0; j < h; j++) { uint32_t *d = fb + (y + j) * FE_W + x; for (i = 0; i < w; i++) d[i] = c; }
}

static void glyph(uint32_t *fb, int x, int y, unsigned char ch, int s, uint32_t c)
{
    const char *g = font8x8_basic[ch < 0x80 ? ch : '?'];
    int r, b;
    for (r = 0; r < 8; r++) {
        unsigned char bits = (unsigned char)g[r];
        for (b = 0; b < 8; b++) if (bits & (1u << b)) fe_fill(fb, x + b * s, y + r * s, s, s, c);
    }
}

void fe_text(uint32_t *fb, int x, int y, const char *str, int s, uint32_t c)
{
    for (; *str; str++) { glyph(fb, x, y, (unsigned char)*str, s, c); x += 8 * s; }
}

int fe_textw(const char *str, int s) { return (int)strlen(str) * 8 * s; }

void fe_clear(uint32_t *fb, uint32_t c) { int i; for (i = 0; i < FE_W * FE_H; i++) fb[i] = c; }

/* ============================================================ navigation input */
#define NAV_UP 1
#define NAV_DOWN 2
#define NAV_OK 4
#define NAV_BACK 8
#define NAV_PGUP 16
#define NAV_PGDN 32
#define NAV_LEFT 64
#define NAV_RIGHT 128

static unsigned g_nav_prev;
static unsigned g_rep_at;            /* when the held direction repeats next */

static void nav_reset(void)
{
    fe_input_poll();
    g_nav_prev = fe_input_pad(0);    /* whatever is held on entry must be released first */
    g_rep_at = 0;
    (void)fe_input_rotary(); (void)fe_input_menu();
}

/* one UI tick: edges (with auto-repeat on up/down) plus the knob's rotation */
static unsigned nav_read(int *steps)
{
    unsigned b, pressed, ev = 0, t = fe_now_ms();

    fe_input_poll();
    b = fe_input_pad(0);
    pressed = b & ~g_nav_prev;
    g_nav_prev = b;
    *steps = fe_input_rotary();

    if (pressed & FE_UP) ev |= NAV_UP;
    if (pressed & FE_DOWN) ev |= NAV_DOWN;
    if (pressed & (FE_UP | FE_DOWN)) g_rep_at = t + 350;
    else if ((b & (FE_UP | FE_DOWN)) && g_rep_at && (int)(t - g_rep_at) >= 0) {
        ev |= (b & FE_UP) ? NAV_UP : NAV_DOWN;
        g_rep_at = t + 70;
    }
    if (!(b & (FE_UP | FE_DOWN))) g_rep_at = 0;
    if (pressed & (FE_B | FE_START)) ev |= NAV_OK;
    if (pressed & FE_A) ev |= NAV_BACK;
    if (pressed & FE_LEFT) ev |= NAV_LEFT;
    if (pressed & FE_RIGHT) ev |= NAV_RIGHT;
    if (pressed & FE_L) ev |= NAV_PGUP;
    if (pressed & FE_R) ev |= NAV_PGDN;
    if (fe_input_menu() > 0) ev |= NAV_BACK;
    return ev;
}

/* ================================================================== ROM list */
static char **g_roms = NULL;
static int g_nroms = 0, g_cap = 0;

static int cmp_name(const void *a, const void *b)
{
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

static int wanted(const char *name)
{
    static const char *const exts[] = { SYS_EXTS };
    const char *dot = strrchr(name, '.');
    unsigned i;
    if (!dot || name[0] == '.') return 0;
    for (i = 0; i < sizeof exts / sizeof exts[0]; i++) if (strcasecmp(dot + 1, exts[i]) == 0) return 1;
    return 0;
}

#if defined(EVO_RETRO_GENESIS)
/* A Sega CD game is a .cue file naming its track files, and those are often .bin - the
 * extension cartridges use too. Whatever a .cue in the folder names is taken off the
 * list, so each disc shows once. */
static void hide_cd_tracks(void)
{
    int i, k, n = g_nroms;

    for (i = 0; i < n; i++) {
        const char *dot = strrchr(g_roms[i], '.');
        char path[600], line[400];
        FILE *f;
        if (!dot || strcasecmp(dot, ".cue") != 0) continue;
        snprintf(path, sizeof path, "%s/%s", SYS_DIR, g_roms[i]);
        if (!(f = fopen(path, "r"))) continue;
        while (fgets(line, sizeof line, f)) {
            char *q1, *q2, *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (strncasecmp(p, "FILE", 4) != 0 || !(q1 = strchr(p, '"')) || !(q2 = strchr(q1 + 1, '"'))) continue;
            *q2 = 0;
            for (k = 0; k < n; k++)
                if (g_roms[k][0] && strcasecmp(g_roms[k], q1 + 1) == 0 && k != i) g_roms[k][0] = 0;    /* marked */
        }
        fclose(f);
    }
    for (i = k = 0; i < n; i++) {
        if (g_roms[i][0]) g_roms[k++] = g_roms[i];
        else free(g_roms[i]);
    }
    g_nroms = k;
}
#endif

static void scan_roms(void)
{
    DIR *d = opendir(SYS_DIR);
    struct dirent *e;
    int i;

    for (i = 0; i < g_nroms; i++) free(g_roms[i]);
    g_nroms = 0;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        if (!wanted(e->d_name)) continue;
        if (g_nroms == g_cap) {
            int nc = g_cap ? g_cap * 2 : 64;
            char **n = (char **)realloc(g_roms, (size_t)nc * sizeof *n);
            if (!n) break;
            g_roms = n; g_cap = nc;
        }
        if ((g_roms[g_nroms] = strdup(e->d_name)) != NULL) g_nroms++;
    }
    closedir(d);
#if defined(EVO_RETRO_GENESIS)
    hide_cd_tracks();
#endif
    if (g_nroms > 1) qsort(g_roms, (size_t)g_nroms, sizeof *g_roms, cmp_name);
}

/* file name -> what is shown: no extension, cut to fit */
static void label_of(const char *name, char *out, size_t n, size_t maxchars)
{
    char *dot;
    strncpy(out, name, n - 1); out[n - 1] = 0;
    dot = strrchr(out, '.');
    if (dot) *dot = 0;
    if (strlen(out) > maxchars && maxchars > 2) { out[maxchars - 2] = '.'; out[maxchars - 1] = '.'; out[maxchars] = 0; }
}

int fe_ui_picker(char *path, size_t pathlen)
{
    const int rows = 11, rowh = 32, y0 = 126;
    static int sel = 0, first = 0;          /* "Change game" comes back to where the list was */

    scan_roms();
    nav_reset();

    while (fe_running) {
        int steps;
        unsigned ev = nav_read(&steps);
        uint32_t *fb;

        sel += steps;
        if (ev & NAV_UP) sel--;
        if (ev & NAV_DOWN) sel++;
        if (ev & NAV_PGUP) sel -= rows;
        if (ev & NAV_PGDN) sel += rows;
        if (sel >= g_nroms) sel = g_nroms - 1;
        if (sel < 0) sel = 0;
        if (ev & NAV_BACK) return 0;
        if ((ev & NAV_OK) && g_nroms > 0) {
            snprintf(path, pathlen, "%s/%s", SYS_DIR, g_roms[sel]);
            return 1;
        }

        fb = fe_fb();
        if (fb) {
            char n[96];
            int i;
            fe_clear(fb, C_PANEL);
            fe_text(fb, 60, 36, SYS_TITLE, 7, C_TXT);
            snprintf(n, sizeof n, "%d game%s in %s", g_nroms, g_nroms == 1 ? "" : "s", SYS_DIR);
            fe_text(fb, 60 + fe_textw(SYS_TITLE, 7) + 40, 36 + 56 - 16, n, 2, C_DIM);
            if (sel < first) first = sel;
            if (sel >= first + rows) first = sel - rows + 1;
            for (i = 0; i < rows && first + i < g_nroms; i++) {
                int idx = first + i, y = y0 + i * rowh;
                char label[96];
                label_of(g_roms[idx], label, sizeof label, 52);
                if (idx == sel) { fe_fill(fb, 48, y - 4, FE_W - 96, rowh, C_SEL); fe_fill(fb, 48, y - 4, 6, rowh, C_ACCENT); }
                fe_text(fb, 72, y, label, 3, idx == sel ? C_TXT : C_DIM);
            }
            if (g_nroms > rows) {                       /* where we are in a long list */
                int track = rows * rowh, bar = track * rows / g_nroms, top;
                if (bar < 12) bar = 12;
                top = (g_nroms - rows) > 0 ? (track - bar) * first / (g_nroms - rows) : 0;
                fe_fill(fb, FE_W - 40, y0 - 4, 4, track, C_SEL);
                fe_fill(fb, FE_W - 40, y0 - 4 + top, 4, bar, C_DIM);
            }
            if (g_nroms == 0) {
                fe_text(fb, 72, y0 + rowh, "no games yet - copy your own ROM files to", 2, C_ACCENT);
                fe_text(fb, 72, y0 + rowh + 28, SYS_DIR "  (on the USB stick)", 2, C_ACCENT);
            }
            {
                const char *hint = "up / down or rotate: choose    A or press: play    B or BACK: exit";
                fe_text(fb, (FE_W - fe_textw(hint, 2)) / 2, FE_H - 36, hint, 2, C_DIM);
            }
            fe_present();
        }
        usleep(25000);
    }
    return 0;
}

/* ============================================================== in-game menu */
#define ROWH 44

static void panel(uint32_t *fb, int px, int py, int pw, int ph, const char *title, const char *sub)
{
    fe_fill(fb, px - 2, py - 2, pw + 4, ph + 4, C_ACCENT);
    fe_fill(fb, px, py, pw, ph, C_PANEL);
    fe_text(fb, px + 28, py + 22, title, 4, C_TXT);
    if (sub) fe_text(fb, px + 28, py + 64, sub, 2, C_DIM);
}

static void row_mark(uint32_t *fb, int px, int pw, int y)
{
    fe_fill(fb, px + 14, y - 6, pw - 28, ROWH - 4, C_SEL);
    fe_fill(fb, px + 14, y - 6, 6, ROWH - 4, C_ACCENT);
}

/* The Settings page. It sits at the left edge so that the picture behind it stays in
 * view while its fit is being changed. Left / right (or A) change the value at once;
 * B or the menu button goes back. */
static void settings_page(void)
{
    const int n = fe_opt_count(), pw = 660, ph = 96 + n * ROWH + 46;
    const int px = 24, py = (FE_H - ph) / 2;
    int sel = 0, i, dirty = 1;

    nav_reset();
    while (fe_running) {
        int steps;
        unsigned ev = nav_read(&steps);
        uint32_t *fb;

        sel += steps;
        if (ev & NAV_UP) sel--;
        if (ev & NAV_DOWN) sel++;
        if (sel < 0) sel = 0;
        if (sel > n - 1) sel = n - 1;
        if (ev & (NAV_LEFT | NAV_PGUP)) { fe_opt_step(sel, -1); dirty = 1; }
        if (ev & (NAV_RIGHT | NAV_PGDN | NAV_OK)) { fe_opt_step(sel, +1); dirty = 1; }
        if (ev & NAV_BACK) return;

        fb = fe_fb();
        if (fb) {
            const char *note;
            if (dirty) { fe_backdrop(fb); dirty = 0; }      /* the picture, as it is fitted now */
            panel(fb, px, py, pw, ph, "SETTINGS", "left / right: change    B: back");
            for (i = 0; i < n; i++) {
                int y = py + 96 + i * ROWH;
                char v[40];
                if (i == sel) row_mark(fb, px, pw, y);
                fe_text(fb, px + 36, y + 2, fe_opt_label(i), 3, i == sel ? C_TXT : C_DIM);
                snprintf(v, sizeof v, "< %s >", fe_opt_value(i));
                fe_text(fb, px + 36 + 15 * 24, y + 6, v, 2, i == sel ? C_ACCENT : C_DIM);
            }
            note = fe_opt_note(sel);
            if (note) fe_text(fb, px + 36, py + ph - 34, note, 2, C_DIM);
            fe_present();
        }
        usleep(25000);
    }
}

enum { IT_RESUME, IT_SAVE, IT_LOAD, IT_SETTINGS, IT_RESET, IT_LIST, IT_QUIT, IT_COUNT };

int fe_ui_pause(const char *game)
{
    static const char *const items[IT_COUNT] = { "Resume", "Save state", "Load state", "Settings", "Reset the console",
                                                 "Change game", "Quit" };
    static const char *const hints[IT_COUNT] = { "", "left / right: choose the slot", "left / right: choose the slot",
                                                 "picture, sound level, console options", "like the console's reset button",
                                                 "back to the list of games", "back to the launcher" };
    static int slot = 1;                    /* remembered while the app runs */
    const int n = IT_COUNT, pw = 640, ph = 96 + n * ROWH + 46;
    const int px = (FE_W - pw) / 2, py = (FE_H - ph) / 2;
    int sel = 0, i, dirty = 1;
    uint32_t *fb;
    char label[64], note[64] = "";
    unsigned note_until = 0;

    label_of(game, label, sizeof label, 36);
    nav_reset();

    while (fe_running) {
        int steps;
        unsigned ev = nav_read(&steps);

        sel += steps;
        if (ev & NAV_UP) sel--;
        if (ev & NAV_DOWN) sel++;
        if (sel < 0) sel = 0;
        if (sel > n - 1) sel = n - 1;
        if (sel == IT_SAVE || sel == IT_LOAD) {
            if (ev & (NAV_LEFT | NAV_PGUP))  { if (--slot < 1) slot = FE_STATE_SLOTS; }
            if (ev & (NAV_RIGHT | NAV_PGDN)) { if (++slot > FE_STATE_SLOTS) slot = 1; }
        }
        if (ev & NAV_BACK) return FE_MENU_RESUME;
        if (ev & NAV_OK) {
            if (sel == IT_RESUME) return FE_MENU_RESUME;
            if (sel == IT_RESET)  return FE_MENU_RESET;
            if (sel == IT_LIST)   return FE_MENU_LIST;
            if (sel == IT_QUIT)   return FE_MENU_QUIT;
            if (sel == IT_SETTINGS) {
                settings_page();
                nav_reset();
                dirty = 1;
                continue;
            }
            if (sel == IT_SAVE) {
                snprintf(note, sizeof note, fe_state_save(slot) ? "saved to slot %d" : "could not save to slot %d", slot);
                note_until = fe_now_ms() + 2500;
            } else if (sel == IT_LOAD) {
                if (!fe_state_exists(slot)) snprintf(note, sizeof note, "slot %d is empty", slot);
                else if (fe_state_load(slot)) return FE_MENU_RESUME;
                else snprintf(note, sizeof note, "slot %d would not load", slot);
                note_until = fe_now_ms() + 2500;
            }
        }
        if (note[0] && (int)(fe_now_ms() - note_until) >= 0) note[0] = 0;

        fb = fe_fb();
        if (fb) {
            if (dirty) { fe_backdrop(fb); dirty = 0; }
            panel(fb, px, py, pw, ph, "PAUSED", label);
            for (i = 0; i < n; i++) {
                int y = py + 96 + i * ROWH;
                if (i == sel) row_mark(fb, px, pw, y);
                fe_text(fb, px + 36, y + 2, items[i], 3, i == sel ? C_TXT : C_DIM);
                if (i == IT_SAVE || i == IT_LOAD) {
                    char t[32];
                    snprintf(t, sizeof t, "< slot %d >%s", slot, (i == IT_LOAD && !fe_state_exists(slot)) ? " empty" : "");
                    fe_text(fb, px + 36 + 12 * 24, y + 6, t, 2, i == sel ? C_ACCENT : C_DIM);
                }
            }
            if (note[0]) fe_text(fb, px + 36, py + ph - 34, note, 2, C_ACCENT);
            else fe_text(fb, px + 36, py + ph - 34, hints[sel], 2, C_DIM);
            fe_present();
        }
        usleep(25000);
    }
    return FE_MENU_QUIT;
}

/* a full-screen note (a ROM that would not load, ...); any button dismisses it */
void fe_ui_message(const char *line1, const char *line2, unsigned hold_ms)
{
    unsigned t0 = fe_now_ms();
    int steps;

    nav_reset();
    while (fe_running && (unsigned)(fe_now_ms() - t0) < hold_ms) {
        uint32_t *fb = fe_fb();
        if (nav_read(&steps) & (NAV_OK | NAV_BACK)) break;
        if (fb) {
            fe_clear(fb, C_PANEL);
            fe_text(fb, (FE_W - fe_textw(line1, 3)) / 2, FE_H / 2 - 40, line1, 3, C_TXT);
            if (line2) fe_text(fb, (FE_W - fe_textw(line2, 2)) / 2, FE_H / 2 + 10, line2, 2, C_DIM);
            fe_present();
        }
        usleep(25000);
    }
}
