/* evo_launcher.c - homebrew app launcher overlay for the NBT EVO ID5 HMI.
 *
 * A resident process that floats a card of app tiles over the dimmed, still-live
 * OEM HMI and launches the selected app. Add an app with a line in the manifest.
 *
 *   input   : the iDrive knob via the shared feed ring (evo_feed.h, produced by
 *             the most tee or by evo_feedpush for testing) - tilt up/down = open
 *             or close, rotate or tilt left/right = move, press = start, any ring
 *             button = close. While the card is up it holds the GRAB so the OEM
 *             HMI ignores the controller.
 *   draw    : launcher_ui.c renders the page and the splash into the framebuffer
 *             of our own ILM layer (evo_graphics_lib). The look is taken from the
 *             head unit itself - HMI fonts, main-menu tile artwork, accent colour,
 *             chosen by the coded ID version and brand - see launcher_ui.c.
 *   launch  : a normal process, so it posix_spawn()s the app directly (HmiMain3D
 *             cannot - ENOSYS - but we are not HmiMain3D). The overlay stays alive:
 *             splash, then transparent while the app owns the display, then idle.
 *
 * Manifest (/fs/sda1/evo/launcher.manifest), one tile per line:  Name|command|icon
 *   icon is optional (a placeholder is drawn when absent).  '#'=comment.
 * Theme override (/fs/sda1/evo/launcher_theme): "auto" (default), "id5" or "flat".
 *
 *   evo_launcher -daemon    resident: hidden until the knob is tilted up/down
 *   evo_launcher            show the page at once (test)
 *   evo_launcher -once      launch the selected app once and exit (no loop)
 */
#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <spawn.h>
#include <stdint.h>

#include "evo_graphics_lib.h"
#include "evo_context.h"   /* evo_context_get_display / framebuffer / present */
#include "evo_display.h"   /* evo_display_enable/disable_presentation_mode */
#include "evo_feed.h"      /* iDrive controller feed ring */
#include "evo_hmistate.h"  /* gate summon to the main Startmenu (evo_hmistate daemon) */
#include "launcher_ui.h"   /* page + splash rendering, theme detection */
#include <sys/stat.h>      /* stat() - hot-reload the manifest when it changes */

#define DISPLAY_W LUI_W
#define DISPLAY_H LUI_H
#define MAX_TILES 32    /* the row scrolls, so this is just the hard cap */
#define MANIFEST  "/fs/sda1/evo/launcher.manifest"
#define THEME_FILE "/fs/sda1/evo/launcher_theme"
#define FADE_IN_MS 170  /* card fade when summoned */

struct icon { int w, h; uint32_t *px; };
struct tile { char name[64]; char cmd[256]; char icon[256]; struct icon ic; };
static struct tile g_tiles[MAX_TILES];
static int g_ntiles = 0, g_sel = 0;
static struct lui_item g_items[MAX_TILES];   /* the tiles as launcher_ui sees them */
static lui_t *g_ui = NULL;
static unsigned g_show_t0 = 0;        /* when the card was summoned (fade in) */
static time_t g_manifest_mtime = 0;   /* for hot-reload on redeploy */
static volatile int g_running = 1;
static evo_context_t *g_ctx = NULL;
static evo_display_t *g_disp = NULL;
static struct evo_feed *g_feed = NULL;
static unsigned int g_tail = 0;
static int g_state = 0;           /* 0 = hidden (watching for the combo), 1 = page shown */
static unsigned g_cdown = 0;      /* center-down timestamp (0 = not pressed) */
static int    g_daemon = 0;       /* resident daemon mode (clean-exit after a launch) */
static struct evo_hmistate *g_hmi = NULL;   /* summon gate: only open from the main Startmenu */
#define LONGPRESS_MS 900          /* hold center this long = summon / dismiss the page */

/* ---------- icons (.rgba: [u32 w][u32 h][w*h u32 0xAARRGGBB]) ---------- */
static int load_icon(const char *path, struct icon *ic) {
    ic->px = NULL; ic->w = ic->h = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t hd[2];
    if (fread(hd, 4, 2, f) != 2) { fclose(f); return -1; }
    int w = (int)hd[0], h = (int)hd[1];
    if (w <= 0 || h <= 0 || w > 512 || h > 512) { fclose(f); return -1; }
    uint32_t *px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!px) { fclose(f); return -1; }
    if (fread(px, 4, (size_t)w * h, f) != (size_t)(w * h)) { free(px); fclose(f); return -1; }
    fclose(f);
    ic->w = w; ic->h = h; ic->px = px;
    return 0;
}
/* ---------- manifest ---------- */
static void add_tile(const char *name, const char *cmd, const char *icon) {
    if (g_ntiles >= MAX_TILES) return;
    struct tile *t = &g_tiles[g_ntiles++];
    snprintf(t->name, sizeof(t->name), "%s", name);
    snprintf(t->cmd, sizeof(t->cmd), "%s", cmd);
    snprintf(t->icon, sizeof(t->icon), "%s", icon ? icon : "");
    t->ic.px = NULL;
    if (icon && *icon) load_icon(icon, &t->ic);
}
static void load_manifest(void) {
    FILE *f = fopen(MANIFEST, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *p = line; while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
            char *nl = strpbrk(p, "\r\n"); if (nl) *nl = 0;
            char *b1 = strchr(p, '|'); if (!b1) continue; *b1 = 0;
            char *cmd = b1 + 1, *icon = NULL;
            char *b2 = strchr(cmd, '|'); if (b2) { *b2 = 0; icon = b2 + 1; }
            add_tile(p, cmd, icon);
        }
        fclose(f);
    }
    if (g_ntiles == 0) {   /* built-in defaults so it is always useful */
        add_tile("DOOM",     "/fs/sda1/evo/doom_launch.sh", "/fs/sda1/evo/icons/doom.rgba");
        add_tile("Terminal", "/fs/sda1/evo/evo_terminal",   "/fs/sda1/evo/icons/terminal.rgba");
        add_tile("System",   "/fs/sda1/evo/evo_sysmon",     "/fs/sda1/evo/icons/cpu.rgba");
    }
}
/* hand the current tile list to the UI (call after every manifest load) */
static void sync_items(void) {
    int i;
    for (i = 0; i < g_ntiles; i++) {
        g_items[i].name = g_tiles[i].name;
        g_items[i].icon_w = g_tiles[i].ic.w; g_items[i].icon_h = g_tiles[i].ic.h;
        g_items[i].icon_px = g_tiles[i].ic.px;
    }
    if (g_ui) lui_set_items(g_ui, g_items, g_ntiles);
}

/* Re-read the manifest if it changed on disk (e.g. a redeploy), so added/removed
 * tiles appear on the next summon with no launcher restart. Frees the old tiles'
 * icon pixmaps first. No-op when the mtime is unchanged. */
static void reload_manifest_if_changed(void) {
    struct stat st;
    if (stat(MANIFEST, &st) != 0) return;                 /* no manifest: keep current tiles */
    if (g_ntiles > 0 && st.st_mtime == g_manifest_mtime) return;
    int i;
    for (i = 0; i < g_ntiles; i++)
        if (g_tiles[i].ic.px) { free(g_tiles[i].ic.px); g_tiles[i].ic.px = NULL; }
    g_ntiles = 0;
    load_manifest();
    g_manifest_mtime = st.st_mtime;
    if (g_sel >= g_ntiles) g_sel = (g_ntiles > 0) ? g_ntiles - 1 : 0;
    sync_items();
    printf("[LAUNCH] manifest reloaded: %d tiles\n", g_ntiles); fflush(stdout);
}

/* ---------- render the page: a floating card over the dimmed live HMI ---------- */
static unsigned g_fr_n = 0, g_fr_sum = 0, g_fr_max = 0;   /* frame-time stats while the card is up */
static void render_page(unsigned alpha) {
    uint32_t *fb = evo_context_get_framebuffer(g_ctx);
    unsigned t0, dt;
    if (!fb) return;
    t0 = evo_now_ms();
    lui_render_page(g_ui, fb, g_sel, t0, alpha);
    evo_context_present(g_ctx);
    dt = evo_now_ms() - t0;
    g_fr_n++; g_fr_sum += dt; if (dt > g_fr_max) g_fr_max = dt;
}
static void log_frame_stats(void) {
    if (g_fr_n) printf("[LAUNCH] %u frames, avg %u ms, max %u ms\n", g_fr_n, g_fr_sum / g_fr_n, g_fr_max);
    fflush(stdout);
    g_fr_n = g_fr_sum = g_fr_max = 0;
}

/* ---------- knob feed ---------- */
static void move_sel(int d) {
    int n = g_sel + d;
    if (n < 0) n = 0;
    if (n >= g_ntiles) n = g_ntiles - 1;
    g_sel = n;
}
static void feed_open(void) {
    int fd = open(EVO_FEED_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) return;
    if (ftruncate(fd, sizeof(struct evo_feed)) != 0) { /* may already exist */ }
    g_feed = mmap(0, sizeof(struct evo_feed), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g_feed == MAP_FAILED) { g_feed = NULL; return; }
    if (g_feed->magic != EVO_FEED_MAGIC) { memset(g_feed, 0, sizeof(*g_feed)); g_feed->magic = EVO_FEED_MAGIC; }
    g_tail = g_feed->head;   /* only react to events from now on */
}

/* ---------- summon gate: only open the page from the main 6-tile Startmenu ----------
 * Map the evo_hmistate daemon's shared struct (it owns the privileged screen capture + the
 * Startmenu signature, so the heavy DISPLAY_MANAGER screen context stays out of this ILM
 * graphics process). We never block on it: FAIL-OPEN everywhere (no daemon, bad magic, or a
 * timeout -> allow the summon), so the launcher is never worse than its old always-summon
 * behavior. A tri-state file /fs/sda1/evo/launcher_gate ("0" disables the gate) lets the user
 * flip it without a rebuild. */
static void hmistate_open(void) {
    int fd = open(EVO_HMISTATE_PATH, O_RDWR, 0666);     /* do NOT create: only the daemon owns it */
    if (fd < 0) { g_hmi = NULL; return; }
    g_hmi = mmap(0, sizeof(struct evo_hmistate), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g_hmi == MAP_FAILED) g_hmi = NULL;
}
static int gate_enabled(void) {
    FILE *f = fopen("/fs/sda1/evo/launcher_gate", "r");
    if (!f) return 1;                         /* default: gate ON */
    int c = fgetc(f); fclose(f);
    return c != '0';                          /* only a literal "0" disables it */
}
/* 1 = allow the summon (on the Startmenu, or fail-open); 0 = suppress it (confirmed NOT Startmenu) */
static int gate_allows_summon(void) {
    if (!gate_enabled()) return 1;
    if (!g_hmi || g_hmi->magic != EVO_HMISTATE_MAGIC) return 1;   /* daemon absent -> fail-open */
    unsigned req = g_hmi->req_seq + 1;
    g_hmi->req_seq = req;                     /* ask the daemon for a fresh capture+check */
    int i;
    for (i = 0; i < 60; i++) {                /* wait up to ~300ms for the answer */
        if (g_hmi->resp_seq == req) return g_hmi->is_startmenu ? 1 : 0;
        usleep(5000);
    }
    return 1;                                 /* timeout -> fail-open */
}
/* returns 0 none, 1 summon, 2 launch, 3 dismiss.  State-aware: while hidden it
 * only watches for the entry combo (long center-press); while shown it navigates. */
static int feed_poll(void) {
    if (!g_feed) return 0;
    int ret = 0;
    unsigned int h = g_feed->head;
    while (g_tail != h) {
        struct evo_ev *e = &g_feed->ev[g_tail % EVO_FEED_RING];
        if (e->kind == EVO_K_DIR && e->b[1]) {              /* 5-way press */
            if (e->b[0] == EVO_DIR_FWD || e->b[0] == EVO_DIR_BACK)
                ret = g_state ? 3 : 1;                       /* UP/DOWN toggles the homebrew page */
            else if (g_state && e->b[0] == EVO_DIR_RIGHT) move_sel(1);
            else if (g_state && e->b[0] == EVO_DIR_LEFT)  move_sel(-1);
        } else if (g_state && e->kind == EVO_K_ROTARY) {
            int d = (int)(signed char)e->b[0];
            move_sel(d > 0 ? 1 : (d < 0 ? -1 : 0));         /* one tile per detent */
        } else if (g_state && e->kind == EVO_K_CENTER && e->b[1]) ret = 2;   /* press = launch */
        else if (g_state && e->kind == EVO_K_BUTTON && e->b[1]) ret = 3;     /* back = close */
        g_tail++;
    }
    if (g_state) g_feed->grab_until = evo_now_ms() + EVO_GRAB_TTL_MS;  /* own the knob only while shown */
    return ret;
}

/* ---------- launch a tile WITHOUT tearing down our overlay ----------
 * The OLD approach destroyed our overlay, _exit(0)'d, and let the supervisor respawn a fresh
 * launcher.  Two bugs fell out of that: (a) re-initializing graphics right after the app
 * churned ILM segfaulted (rc=139); (b) the respawned launcher came up WATCHING THE KNOB while
 * the app still ran, so up/down summoned the card on top of a running app.  Instead we keep
 * our single overlay layer alive for the whole session: show a brief "Launching" splash over
 * the app's init, then render the overlay fully TRANSPARENT so the app (which takes
 * presentation mode on its own layer) shows through, and waitpid IN THIS PROCESS.  Blocking in
 * waitpid means we are NOT in the knob-watch loop while the app runs (no summon-over-app), and
 * never destroying/recreating our graphics means we never hit the ILM re-init fault. */
static void present_both_transparent(void);   /* defined below */

/* card/app gone -> render our overlay transparent so the live HMI shows through, go hidden.
 * We never hid the HMI (we float our layer ABOVE it), so nothing to restore here; a launched app
 * that used presentation mode restores the HMI itself on exit.  Re-assert our layer on top so the
 * idle transparent overlay stays above the HMI for the next summon. */
static void restore_hmi_idle(void) {
    if (g_feed) g_feed->grab_until = 0;   /* release the knob to the HMI (clear any stale grab) */
    present_both_transparent();           /* render a fully-transparent frame -> live HMI shows through */
    g_state = 0;
}

#define LAUNCH_SPLASH_MS 2200   /* hold the splash this long so the app's surface is up first */
static void render_launching(unsigned t_ms) {
    uint32_t *fb = evo_context_get_framebuffer(g_ctx);
    if (!fb) return;
    lui_render_splash(g_ui, fb, (g_sel >= 0 && g_sel < g_ntiles) ? &g_items[g_sel] : NULL, t_ms);
    evo_context_present(g_ctx);
}

static void launch(const char *cmd, const char *name) {
    log_frame_stats();
    if (g_feed) g_feed->grab_until = 0;      /* release the knob to the app */
    g_state = 0;

    /* 1) splash on our (still-alive) overlay - covers the OEM HMI during the app's init */
    { int k; for (k = 0; k < 2; k++) render_launching(0); }

    /* 2) spawn the app */
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawnattr_t at; posix_spawnattr_init(&at);
    char *av[] = { "sh", "-c", (char *)cmd, (char *)NULL };
    char *envp[] = {
        "PATH=/bin:/usr/bin:/proc/boot:/sbin:/usr/sbin:/var/run/screen_tests:/fs/sda1/evo",
        "HOME=/fs/sda1/evo",
        "LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib",
        (char *)NULL
    };
    pid_t pid = -1;
    int rc = posix_spawn(&pid, "/bin/sh", &fa, &at, av, envp);
    posix_spawn_file_actions_destroy(&fa); posix_spawnattr_destroy(&at);
    if (rc != 0 || pid <= 0) {
        printf("[LAUNCH] posix_spawn('%s') failed: %s\n", cmd, strerror(rc)); fflush(stdout);
        restore_hmi_idle(); return;
    }
    printf("[LAUNCH] '%s' pid=%d (splash, then app takes over)\n", name, (int)pid); fflush(stdout);

    /* 3) hold the splash while the app initializes behind our overlay (bail if it dies early) */
    unsigned t0 = evo_now_ms();
    while ((int)(evo_now_ms() - t0) < LAUNCH_SPLASH_MS) {
        render_launching(evo_now_ms() - t0);     /* animated: the progress line sweeps */
        usleep(25000);
        int st2, w = waitpid(pid, &st2, WNOHANG);
        if (w == pid) { restore_hmi_idle(); return; }                        /* app exited early */
    }

    /* 4) the app now renders to our layer (HMI still hidden by our presentation mode), overwriting
     *    the splash.  We just wait for it to exit - and while blocked here we do NOT watch the knob,
     *    so nothing can summon the card over a running app. */
    { int st; while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {} }

    /* 5) app gone: it hid nothing (we already had), so WE bring the OEM HMI back and go idle. */
    restore_hmi_idle();
    printf("[LAUNCH] '%s' exited; HMI restored, back to hidden watch\n", name); fflush(stdout);
}

/* ---------- the resident floating overlay: created ONCE, toggled transparent<->card ----------
 * NO presentation mode (the live HMI stays visible behind the card) AND crucially NO
 * create/destroy on summon/dismiss: that churns ILM (layer add/remove + ilm_destroy) on
 * every toggle and races any other ILM client (e.g. evo_screenshot) -> SIGSEGV.  Instead
 * the overlay layer is permanent; "hide" = render a fully-transparent frame (the HMI shows
 * through at full brightness), "show" = render the dimmed card.  Rock-solid + screenshot-safe. */
static void fill_transparent(uint32_t *fb) { int i; for (i = 0; i < DISPLAY_W * DISPLAY_H; i++) fb[i] = 0u; }
static void present_both_transparent(void) {
    int k;                                      /* clear BOTH double-buffers to transparent */
    if (!g_ctx) return;
    for (k = 0; k < 2; k++) {
        uint32_t *fb = evo_context_get_framebuffer(g_ctx);
        if (fb) { fill_transparent(fb); evo_context_present(g_ctx); }
    }
}
static int overlay_init(void) {                 /* build the resident, initially-transparent overlay */
    /* EXACTLY the evo_cursor recipe (the PROVEN over-the-live-HMI overlay, user-verified 2026-11-06):
     * create + show, and NOTHING else.  evo_context_show inserts our surface-2000/layer-9000 at the
     * TOP of the ILM render order, which is what makes it composite ABOVE the live HMI with per-pixel
     * alpha.  Do NOT call raise_to_top: it rewrites ilm_displaySetRenderOrder, which does NOT control
     * the physical z-order here (confirmed: card stayed behind the HMI) and was undoing the lib's own
     * correct top-insertion.  No presentation mode -> the HMI stays visible behind the card. */
    /* Claim our OWN surface+layer (2001/9001) so we never share with the apps we spawn (2000/9000).
     * An app's teardown runs ilm_layerRemoveSurface(layer,surface); on the shared IDs that yanked
     * OUR surface off OUR layer, leaving the card live-but-invisible after any app launch+exit.
     * The apps get a fixed envp (no EVO_* inherited), so they stay on the default 2000/9000. */
    setenv("EVO_SURFACE_ID", "2001", 1);
    setenv("EVO_LAYER_ID",   "9001", 1);
    g_ctx = evo_context_create(DISPLAY_W, DISPLAY_H, 0, 0);
    if (!g_ctx) { printf("[LAUNCH] overlay_init: create failed\n"); fflush(stdout); return -1; }
    evo_context_show(g_ctx);
    g_disp = evo_context_get_display(g_ctx);
    present_both_transparent();                 /* start transparent -> live HMI shows through */
    return 0;
}
/* Our overlay layer is composited BELOW the OEM HMI, so the card is only visible when the HMI is
 * hidden.  Presentation mode hides every other layer but keeps ours -> the card shows (exactly how
 * the apps show).  The launcher OWNS the hide/restore across summon -> launch -> app-exit: a
 * launched app's own enable_presentation_mode then finds everything already hidden and is a no-op,
 * so only our disable (on dismiss or after the app exits) brings the OEM HMI back. */
static int show_page(void) {                    /* grab the knob + mark shown; the loop renders the card */
    if (g_feed) g_feed->grab_until = evo_now_ms() + EVO_GRAB_TTL_MS;
    /* The overlay layer is ALREADY on top (from overlay_init's context_show, evo_cursor-style) and stays
     * there; "summon" just grabs the knob and flips state so the render loop draws the dimmed card over
     * the still-visible HMI.  No raise_to_top, no presentation mode - that alpha/dimmed-HMI look is the
     * whole point ("felt more integrated"). */
    g_state = 1;
    g_show_t0 = evo_now_ms();                   /* the loop fades the card in from here */
    return 0;
}
static void hide_page(void) {                   /* release knob + restore the OEM HMI (NO destroy) */
    if (g_feed) g_feed->grab_until = 0;
    if (g_state && g_ctx && g_ui) {             /* quick fade out instead of a hard cut */
        int k; for (k = 3; k >= 1; k--) { render_page((unsigned)k * 64); usleep(12000); }
    }
    restore_hmi_idle();                         /* transparent frame -> the live HMI at full brightness */
    log_frame_stats();
}

static void on_sig(int s) { (void)s; g_running = 0; }

int main(int argc, char **argv) {
    int daemon_mode = (argc > 1 && strcmp(argv[1], "-daemon") == 0);
    g_daemon = daemon_mode;
    int once        = (argc > 1 && strcmp(argv[1], "-once") == 0);
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);   /* NB: do NOT ignore SIGCHLD - waitpid() must block on the launched app */

    load_manifest();
    { struct stat st0; if (stat(MANIFEST, &st0) == 0) g_manifest_mtime = st0.st_mtime; }
    {   /* look: detected from the unit unless the theme file says otherwise */
        char ov[32] = ""; FILE *tf = fopen(THEME_FILE, "r");
        if (tf) { if (fscanf(tf, "%31s", ov) != 1) ov[0] = 0; fclose(tf); }
        g_ui = lui_create(ov[0] ? ov : NULL);
        if (!g_ui) { printf("[LAUNCH] ui init failed\n"); return 1; }
        printf("[LAUNCH] theme: %s\n", lui_theme_desc(g_ui)); fflush(stdout);
        sync_items();
    }
    evo_graphics_init();
    feed_open();
    hmistate_open();
    printf("[LAUNCH] %d tiles, mode=%s, feed %s, gate %s\n", g_ntiles,
           daemon_mode ? "daemon(resident)" : "page", g_feed ? "mapped" : "unavailable",
           g_hmi ? "mapped" : "fail-open(no daemon)"); fflush(stdout);

    if (overlay_init() != 0) return 1;           /* resident overlay layer (transparent while hidden) */

    if (!daemon_mode) {                          /* standalone page (test): show immediately */
        show_page();
        if (once) { render_page(255);
                    launch(g_tiles[g_sel].cmd, g_tiles[g_sel].name); return 0; }
    } else {
        printf("[LAUNCH] daemon: up/down to summon the homebrew page\n"); fflush(stdout);
    }

    /* Resident single process: hidden = transparent overlay (watch the knob); summon =
     * grab + render the dimmed card; dismiss = render transparent + release.  No ILM churn. */
    while (g_running) {
        int act = feed_poll();
        if (!g_state) {                          /* hidden: idle until up/down */
            if (act == 1) {
                if (gate_allows_summon()) { printf("[LAUNCH] summon\n"); fflush(stdout); reload_manifest_if_changed(); show_page(); }
                else { printf("[LAUNCH] summon suppressed (not on Startmenu)\n"); fflush(stdout); }
            }
        } else {                                 /* shown: navigate / launch / dismiss */
            if (act == 2) launch(g_tiles[g_sel].cmd, g_tiles[g_sel].name);  /* splash, run, resume */
            else if (act == 3) { if (daemon_mode) { printf("[LAUNCH] dismiss\n"); fflush(stdout); hide_page(); }
                                 else g_running = 0; }
        }
        if (g_state && g_ctx) {
            unsigned el = evo_now_ms() - g_show_t0;
            unsigned alpha = el >= FADE_IN_MS ? 255 : 40 + el * 215 / FADE_IN_MS;
            render_page(alpha);
            usleep(alpha < 255 ? 8000 : 30000);  /* fade as fast as it renders, then ~30 Hz */
        } else {
            usleep(50000);                       /* slow idle while hidden */
        }
    }

    hide_page();
    if (g_ctx) evo_context_destroy(g_ctx);
    lui_destroy(g_ui);
    printf("[LAUNCH] exit\n");
    return 0;
}
