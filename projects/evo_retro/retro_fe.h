/* retro_fe.h - shared declarations for the evo_retro frontend.
 *
 * evo_retro is a small libretro frontend for the NBT EVO head unit: one binary per
 * system, the frontend statically linked with one emulator core. Each system is its
 * own project folder (Makefile, core.sh, tile descriptor, launch script) that builds
 * these sources through retro.mk; the system is chosen at compile time:
 *
 *   -DEVO_RETRO_GENESIS   ../evo_genesis   evo_genesis  + Genesis Plus GX
 *   -DEVO_RETRO_SNES      ../evo_snes      evo_snes     + Snes9x 2010
 *
 * Every non-static symbol of the frontend starts with fe_ so it cannot collide with a
 * core's globals (the cores use plain names like `bitmap`, `config`, `input`).
 */
#ifndef RETRO_FE_H
#define RETRO_FE_H

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- the system */
#if defined(EVO_RETRO_SNES)
#  define SYS_TITLE   "SNES"
#  define SYS_TAG     "[SNES]"
#  define SYS_DIR     "/fs/sda1/evo/snes"
#  define SYS_EXTS    "sfc", "smc", "swc", "fig", "zip"
#  define SYS_ENV     "EVO_SNES"
#  define SYS_GAIN    "/fs/sda1/evo/snes_gain"
#elif defined(EVO_RETRO_GENESIS)
#  define SYS_TITLE   "GENESIS"
#  define SYS_TAG     "[GENESIS]"
#  define SYS_DIR     "/fs/sda1/evo/genesis"
#  define SYS_EXTS    "md", "smd", "gen", "bin", "sms", "gg", "sg", "68k", "cue", "iso", "zip"
#  define SYS_ENV     "EVO_GENESIS"
#  define SYS_GAIN    "/fs/sda1/evo/genesis_gain"
#else
#  error "define EVO_RETRO_GENESIS or EVO_RETRO_SNES (retro.mk does, from the tile's SYSTEM)"
#endif

#define FE_W 1440           /* the panel */
#define FE_H 540

/* colours are 0xAARRGGBB */
#define C_BG     0xFF000000u
#define C_PANEL  0xFF0A0A0Cu
#define C_ACCENT 0xFFE08020u
#define C_TXT    0xFFFFFFFFu
#define C_DIM    0xFF9A9AA0u
#define C_SEL    0xFF202830u

/* ------------------------------------------------------------------- common */
extern volatile int fe_running;         /* cleared by SIGTERM/SIGINT: leave everything */
unsigned  fe_now_ms(void);              /* CLOCK_MONOTONIC milliseconds, never 0 */
uint32_t *fe_fb(void);                  /* the 1440x540 0xAARRGGBB framebuffer (or NULL) */
void      fe_present(void);
int       fe_write_file(const char *path, const void *data, size_t n);  /* through a .tmp file; 1 = written */

/* -------------------------------------------------------------------- input */
/* Buttons are libretro "RetroPad" bits: 1 << RETRO_DEVICE_ID_JOYPAD_*. */
#define FE_B      (1u << 0)     /* bottom face button */
#define FE_Y      (1u << 1)     /* left   */
#define FE_SELECT (1u << 2)
#define FE_START  (1u << 3)
#define FE_UP     (1u << 4)
#define FE_DOWN   (1u << 5)
#define FE_LEFT   (1u << 6)
#define FE_RIGHT  (1u << 7)
#define FE_A      (1u << 8)     /* right  */
#define FE_X      (1u << 9)     /* top    */
#define FE_L      (1u << 10)
#define FE_R      (1u << 11)
#define FE_L2     (1u << 12)
#define FE_R2     (1u << 13)
#define FE_L3     (1u << 14)
#define FE_R3     (1u << 15)
#define FE_PORTS  4

void     fe_input_start(void);          /* knob + keyboard threads, takes the controller grab */
void     fe_input_stop(void);
void     fe_input_poll(void);           /* sample the gamepads; call once per frame / UI tick */
unsigned fe_input_pad(int port);        /* RetroPad mask; port 0 = pad 1 + knob + keyboard */
void     fe_input_block_held(void);     /* what is held now does not count until it is released */
int      fe_input_rotary(void);         /* knob detents since the last call (+ = clockwise) */
int      fe_input_menu(void);           /* menu presses since the last call: Xbox button, knob BACK, Esc */

/* -------------------------------------------------------------------- video */
void fe_video_format(int retro_pixel_format);
/* Lay out for this source size; 1 = the layout changed (clear around the picture). */
int  fe_video_prepare(unsigned w, unsigned h);
/* Scale one core frame into a 1440-wide frame; writes the picture rectangle only. */
void fe_video_blit(uint32_t *dst, const void *data, unsigned w, unsigned h, size_t pitch);
/* where the picture sits on the panel (valid after fe_video_prepare) */
void fe_video_rect(int *x, int *y, int *w, int *h);
/* How the picture is fitted to the panel. Changing it re-lays-out the next frame. */
enum { FE_PIC_SHARP = 0,                /* whole-number scale (2x), every pixel the same size */
       FE_PIC_FULL,                     /* the full panel height, square pixels */
       FE_PIC_TV,                       /* the full panel height, 4:3 like the TV these were made for */
       FE_PIC_WIDE,                     /* the full panel height, stretched to 16:9 */
       FE_PIC_COUNT };
void fe_video_set_mode(int mode);
/* everything outside the picture has to be drawn again (after one of our own screens) */
void fe_redraw_all(void);

/* -------------------------------------------------------------------- audio */
int  fe_audio_init(void);               /* 1 = a sound device is open */
void fe_audio_shutdown(void);
int  fe_audio_active(void);
void fe_audio_set_rate(double core_rate, double fps);
void fe_audio_resume(void);             /* start, or restart after a pause, from silence */
void fe_audio_pause(void);
void fe_audio_push(const int16_t *stereo, size_t frames);
void fe_audio_stats(char *buf, size_t n);
int  fe_audio_gain(void);               /* percent */
void fe_audio_set_gain(int percent);    /* takes effect at once */

/* ----------------------------------------------------------------------- UI */
void fe_fill(uint32_t *fb, int x, int y, int w, int h, uint32_t c);
void fe_text(uint32_t *fb, int x, int y, const char *s, int scale, uint32_t c);
int  fe_textw(const char *s, int scale);
void fe_clear(uint32_t *fb, uint32_t c);

/* ROM list. Returns 1 with `path` filled in, 0 if the user left. */
int  fe_ui_picker(char *path, size_t pathlen);
/* In-game menu over the frozen picture. Save / load state are done inside it (through
 * the fe_state_* calls below); a loaded state comes back as FE_MENU_RESUME. */
enum { FE_MENU_RESUME = 0, FE_MENU_RESET, FE_MENU_LIST, FE_MENU_QUIT };
int  fe_ui_pause(const char *game);
/* the frozen game picture, dimmed, as the background of the menu (evo_retro.c) */
void fe_backdrop(uint32_t *fb);

/* ----------------------------------------------------------------- settings */
/* What the "Settings" page of the menu shows (retro_opts.c): the picture mode, the
 * sound level and a few options of the emulator core. Kept in SYS_DIR/settings.txt. */
void        fe_opts_load(void);
int         fe_opt_count(void);
const char *fe_opt_label(int i);
const char *fe_opt_value(int i);        /* the current value, as shown */
const char *fe_opt_note(int i);         /* a hint for the bottom line, or NULL */
void        fe_opt_step(int i, int dir);    /* -1 / +1: previous / next value; applied and saved */
/* for the libretro side */
const char *fe_opts_core_value(const char *key);    /* NULL = not set: the core's default */
int         fe_opts_core_changed(void);     /* 1 once after a core option changed */
int         fe_opts_pad(void);              /* 0 = let the core decide, else 3 or 6 buttons (Genesis) */
int         fe_opts_reload_wanted(void);    /* a changed option needs the game loaded again */
void        fe_opts_game_loaded(void);      /* ... which has just happened */
void        fe_core_pad_changed(void);      /* evo_retro.c: hand fe_opts_pad() to the core */

/* --------------------------------------------------------------------- zip */
/* The first game inside a .zip, into memory (malloc'd). 0 = failed, fe_zip_error() says why. */
int         fe_zip_read(const char *zip, void **data, size_t *size, char *ext, size_t extlen);
const char *fe_zip_error(void);

/* ---------------------------------------------------------------- save states */
#define FE_STATE_SLOTS 4
int  fe_state_exists(int slot);         /* slots are 1..FE_STATE_SLOTS */
int  fe_state_save(int slot);           /* 1 = written */
int  fe_state_load(int slot);           /* 1 = the game is now at that point */
void fe_ui_message(const char *line1, const char *line2, unsigned hold_ms);

#endif /* RETRO_FE_H */
