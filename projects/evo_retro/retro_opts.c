/* retro_opts.c - the settings a player can change, and where they are kept.
 *
 * The "Settings" page of the in-game menu shows one row per entry of the table below:
 * how the picture is fitted to the panel, the sound level, and a handful of the
 * emulator core's own options under plain names. Each has a short list of values; the
 * first is the default.
 *
 *   picture, pad        ours; SYS_DIR/settings.txt
 *   core options        the core asks for them by key (libretro "variables");
 *                       SYS_DIR/settings.txt, under the core's own key
 *   sound level         the gain file the launch script already reads
 *                       (/fs/sda1/evo/<system>_gain), so it stays the one place
 *
 * settings.txt is plain "key = value" lines. A line whose key is not one of the menu's
 * is handed to the core untouched and kept when the file is rewritten, so any of the
 * core's many other options can be set by hand without a rebuild.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "retro_fe.h"

#define SETTINGS  SYS_DIR "/settings.txt"
#define MAX_VALS  6
#define MAX_EXTRA 24

enum { K_PICTURE, K_SOUND, K_PAD, K_CORE };
enum { AT_ONCE = 0, AFTER_RESET, AFTER_RELOAD };

struct opt {
    const char *label, *key;
    int kind, when;
    int n;
    const char *vals[MAX_VALS];         /* as stored, and as handed to the core */
    const char *names[MAX_VALS];        /* as shown */
    const char *hint;
    int cur, at_load;
};

static struct opt g_opts[] = {
    { "Picture", "picture", K_PICTURE, AT_ONCE, 4,
      { "sharp", "full", "tv", "wide" }, { "Sharp 2x", "Full height", "TV 4:3", "Wide 16:9" }, NULL, 0, 0 },
    { "Sound level", "", K_SOUND, AT_ONCE, 0, { NULL }, { NULL },
      "300% is the default; lower if harsh", 0, 0 },
#if defined(EVO_RETRO_GENESIS)
    { "Controller", "pad", K_PAD, AT_ONCE, 3,
      { "auto", "3", "6" }, { "Auto", "3 buttons", "6 buttons" },
      "Auto: the pad each game was made for", 0, 0 },
    { "Region", "genesis_plus_gx_region_detect", K_CORE, AFTER_RESET, 4,
      { "auto", "ntsc-u", "pal", "ntsc-j" }, { "Auto", "USA", "Europe", "Japan" },
      "takes effect after Reset the console", 0, 0 },
    { "Sprite limit", "genesis_plus_gx_no_sprite_limit", K_CORE, AT_ONCE, 2,
      { "disabled", "enabled" }, { "On", "Off" },
      "Off = less flicker than the console", 0, 0 },
#else
    { "Region", "snes9x_2010_region", K_CORE, AFTER_RELOAD, 3,
      { "auto", "ntsc", "pal" }, { "Auto", "NTSC 60 Hz", "PAL 50 Hz" },
      "takes effect after Reset the console", 0, 0 },
    { "Sprite limit", "snes9x_2010_reduce_sprite_flicker", K_CORE, AT_ONCE, 2,
      { "disabled", "enabled" }, { "On", "Off" },
      "Off = less flicker than the console", 0, 0 },
    { "Less slowdown", "snes9x_2010_overclock_cycles", K_CORE, AT_ONCE, 4,
      { "disabled", "light", "compatible", "max" }, { "Off", "Light", "Medium", "Most" },
      "faster processor; may upset games", 0, 0 },
    { "Super FX speed", "snes9x_2010_overclock", K_CORE, AT_ONCE, 4,
      { "10 MHz (Default)", "15 MHz", "20 MHz", "40 MHz" }, { "Normal", "1.5x", "2x", "4x" },
      "smoother Super FX games (not exact)", 0, 0 },
#endif
};
#define N_OPTS ((int)(sizeof g_opts / sizeof g_opts[0]))

static struct { char key[64], val[64]; } g_extra[MAX_EXTRA];
static int g_nextra = 0;
static int g_core_changed = 0;

static const int g_levels[] = { 50, 100, 150, 200, 250, 300, 350, 400, 500, 600 };
#define N_LEVELS ((int)(sizeof g_levels / sizeof g_levels[0]))

/* ================================================================== the file */
static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

void fe_opts_load(void)
{
    FILE *f = fopen(SETTINGS, "r");
    char line[200];
    int i, k;

    if (f) {
        while (fgets(line, sizeof line, f)) {
            char *key = trim(line), *val, *eq;
            if (!*key || *key == '#' || !(eq = strchr(key, '='))) continue;
            *eq = 0;
            val = trim(eq + 1); key = trim(key);
            if (!*key || !*val) continue;
            for (i = 0; i < N_OPTS; i++) if (g_opts[i].key[0] && !strcmp(g_opts[i].key, key)) break;
            if (i < N_OPTS) {
                for (k = 0; k < g_opts[i].n; k++) if (!strcmp(g_opts[i].vals[k], val)) { g_opts[i].cur = k; break; }
            } else if (g_nextra < MAX_EXTRA && strlen(key) < sizeof g_extra[0].key && strlen(val) < sizeof g_extra[0].val) {
                strcpy(g_extra[g_nextra].key, key); strcpy(g_extra[g_nextra].val, val);
                g_nextra++;
            }
        }
        fclose(f);
    }
    for (i = 0; i < N_OPTS; i++) {
        g_opts[i].at_load = g_opts[i].cur;
        if (g_opts[i].kind == K_PICTURE) fe_video_set_mode(g_opts[i].cur);
    }
}

static void save(void)
{
    char buf[4096];
    size_t n = 0;
    int i;

    n += (size_t)snprintf(buf + n, sizeof buf - n,
        "# " SYS_TITLE " settings - written by the in-game menu.\n"
        "# Any other \"key = value\" line is handed to the emulator core as one of its options.\n");
    for (i = 0; i < N_OPTS && n < sizeof buf - 160; i++)
        if (g_opts[i].key[0]) n += (size_t)snprintf(buf + n, sizeof buf - n, "%s = %s\n", g_opts[i].key, g_opts[i].vals[g_opts[i].cur]);
    for (i = 0; i < g_nextra && n < sizeof buf - 160; i++)
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s = %s\n", g_extra[i].key, g_extra[i].val);
    if (!fe_write_file(SETTINGS, buf, n)) printf(SYS_TAG " could not write " SETTINGS "\n");
}

/* ============================================================= for the menu */
int fe_opt_count(void) { return N_OPTS; }
const char *fe_opt_label(int i) { return (i >= 0 && i < N_OPTS) ? g_opts[i].label : ""; }

const char *fe_opt_value(int i)
{
    static char num[16];
    if (i < 0 || i >= N_OPTS) return "";
    if (g_opts[i].kind == K_SOUND) { snprintf(num, sizeof num, "%d%%", fe_audio_gain()); return num; }
    return g_opts[i].names[g_opts[i].cur];
}

const char *fe_opt_note(int i)
{
    static char note[64];
    if (i < 0 || i >= N_OPTS) return NULL;
    if (g_opts[i].kind == K_PICTURE) {
        int x, y, w, h;
        fe_video_rect(&x, &y, &w, &h);
        snprintf(note, sizeof note, "%d x %d of the panel's %d x %d", w, h, FE_W, FE_H);
        return note;
    }
    return g_opts[i].hint;
}

void fe_opt_step(int i, int dir)
{
    struct opt *o;
    if (i < 0 || i >= N_OPTS || !dir) return;
    o = &g_opts[i];

    if (o->kind == K_SOUND) {                           /* to the next level above / below where it is */
        int g = fe_audio_gain(), k, v = g;
        char txt[16];
        if (dir > 0) { for (k = 0; k < N_LEVELS; k++) if (g_levels[k] > g) { v = g_levels[k]; break; } }
        else { for (k = N_LEVELS - 1; k >= 0; k--) if (g_levels[k] < g) { v = g_levels[k]; break; } }
        if (v == g) return;
        fe_audio_set_gain(v);
        snprintf(txt, sizeof txt, "%d\n", v);
        if (!fe_write_file(SYS_GAIN, txt, strlen(txt))) printf(SYS_TAG " could not write " SYS_GAIN "\n");
        return;
    }

    o->cur = (o->cur + (dir > 0 ? 1 : o->n - 1)) % o->n;
    if (o->kind == K_PICTURE) fe_video_set_mode(o->cur);
    else if (o->kind == K_PAD) fe_core_pad_changed();
    else g_core_changed = 1;
    save();
}

/* ====================================================== for the libretro side */
const char *fe_opts_core_value(const char *key)
{
    int i;
    for (i = 0; i < N_OPTS; i++) if (g_opts[i].kind == K_CORE && !strcmp(g_opts[i].key, key)) return g_opts[i].vals[g_opts[i].cur];
    for (i = 0; i < g_nextra; i++) if (!strcmp(g_extra[i].key, key)) return g_extra[i].val;
    return NULL;
}

int fe_opts_core_changed(void)
{
    int c = g_core_changed;
    g_core_changed = 0;
    return c;
}

int fe_opts_pad(void)
{
    int i;
    for (i = 0; i < N_OPTS; i++) if (g_opts[i].kind == K_PAD) return g_opts[i].cur == 1 ? 3 : g_opts[i].cur == 2 ? 6 : 0;
    return 0;
}

int fe_opts_reload_wanted(void)
{
    int i;
    for (i = 0; i < N_OPTS; i++) if (g_opts[i].when == AFTER_RELOAD && g_opts[i].cur != g_opts[i].at_load) return 1;
    return 0;
}

void fe_opts_game_loaded(void)
{
    int i;
    for (i = 0; i < N_OPTS; i++) g_opts[i].at_load = g_opts[i].cur;
    g_core_changed = 0;                                 /* the core has just read them all */
}
