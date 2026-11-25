/* evo_nes_osd.c - Nofrendo "osd" (O/S-dependent) port for the NBT EVO head unit.
 *
 * Nofrendo is made portable through a small layer declared in osd.h plus a
 * viddriver_t. This file is that layer for QNX 6.5 / the homebrew launcher. It
 * includes ONLY nofrendo headers and libc/pthread; the QNX Screen/ILM display,
 * the iDrive feed ring, the USB keyboard and QSA audio all live in evo_nes.c, and
 * the two halves talk through evo_nes_port.h.
 *
 *   video : nofrendo renders 8-bit indexed 256x224 into its own primary buffer and
 *           hands it to custom_blit() on every drawn frame -> nesfe_blit() palette-
 *           converts, 2x-scales and presents; then one frame of APU audio is pulled
 *           and written to QSA (the blocking write is the pacing).
 *   clock : osd_installtimer() -> a 60 Hz thread that bumps nofrendo_ticks through
 *           the core's own timer_isr (its autoframeskip design); the audio write
 *           keeps the emulation locked to the DAC clock.
 *   input : osd_getinput() per frame edge-detects the joypad mask the frontend
 *           keeps (iDrive knob + keyboard) into event_joypad1_* MAKE/BREAK events;
 *           a confirmed quit calls main_quit(), which also saves battery RAM.
 *   files : plain fopen() paths; osd_newextension() puts .sav / .ss<n> beside the ROM.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include <noftypes.h>
#include <nofconfig.h>
#include <log.h>
#include <osd.h>
#include <nofrendo.h>
#include <event.h>
#include <bitmap.h>
#include <vid_drv.h>
#include <nes.h>
#include <nesinput.h>

#include "evo_nes_port.h"

#define HW_W 256
#define HW_H 240

static const char *g_rom = "";
static char g_cfgfile[] = "/fs/sda1/evo/nes/nofrendo.cfg";

/* The cartridge image. This core (ESP32 lineage) does not fopen() the ROM: rom_load()
 * asks osd_getromdata() for the whole iNES image in memory and keeps rominfo->rom /
 * ->vrom pointing INTO it (rom_free no longer free()s them - QNX_PATCHES.md #3), so
 * we own this buffer for the life of the game and free it in nesosd_shutdown(). */
static unsigned char *g_romimage = NULL;
static unsigned char g_badrom[32];       /* handed over when the file can't be read: no iNES magic -> clean failure */

/* ---------------------------------------------------------------- frame clock */
static void (*g_tick_func)(void) = NULL;
static pthread_t g_timer_tid;
static volatile int g_timer_run = 0;
static int g_timer_started = 0;

static void *timer_thread(void *arg)
{
   int hz = (int)(long)arg;
   long period_ns;
   struct timespec next;

   if (hz <= 0) hz = 60;
   period_ns = 1000000000L / hz;
   clock_gettime(CLOCK_MONOTONIC, &next);
   while (g_timer_run)
   {
      next.tv_nsec += period_ns;
      while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
      if (g_tick_func) g_tick_func();
   }
   return NULL;
}

/* nofrendo asks for `func` to be called `frequency` times a second (its timer ISR). */
int osd_installtimer(int frequency, void *func, int funcsize, void *counter, int countersize)
{
   (void)funcsize; (void)counter; (void)countersize;
   g_tick_func = (void (*)(void))func;
   if (!g_timer_started)
   {
      g_timer_run = 1;
      if (pthread_create(&g_timer_tid, NULL, timer_thread, (void *)(long)frequency) != 0)
      {
         g_timer_run = 0;
         return -1;
      }
      g_timer_started = 1;
   }
   return 0;
}

void nesosd_shutdown(void)
{
   if (g_timer_started)
   {
      g_timer_run = 0;
      pthread_join(g_timer_tid, NULL);
      g_timer_started = 0;
   }
   if (g_romimage) { free(g_romimage); g_romimage = NULL; }   /* rom_free() ran already: safe */
}

/* ---------------------------------------------------------------------- sound */
static void (*g_playfunc)(void *buffer, int size) = NULL;

void osd_setsound(void (*playfunc)(void *buffer, int size)) { g_playfunc = playfunc; }

void osd_getsoundinfo(sndinfo_t *info)
{
   int r = nesfe_audio_rate();
   info->sample_rate = (r > 0) ? r : 44100;
   info->bps = 16;
}

/* ---------------------------------------------------------------------- video */
static unsigned int g_pal[256];
static uint8 *g_hwbuf = NULL;
static bitmap_t *g_hwbmp = NULL;

static int vd_init(int width, int height) { (void)width; (void)height; return 0; }

static void vd_shutdown(void)
{
   if (g_hwbmp) bmp_destroy(&g_hwbmp);
   if (g_hwbuf) { free(g_hwbuf); g_hwbuf = NULL; }
}

static int vd_set_mode(int width, int height)
{
   (void)width; (void)height;
   if (!g_hwbuf) { g_hwbuf = (uint8 *)calloc(HW_W * HW_H, 1); if (!g_hwbuf) return -1; }
   if (!g_hwbmp) { g_hwbmp = bmp_createhw(g_hwbuf, HW_W, HW_H, HW_W); if (!g_hwbmp) return -1; }
   return 0;
}

static void vd_set_palette(rgb_t *pal)
{
   int i;
   for (i = 0; i < 256; i++)
      g_pal[i] = 0xFF000000u | ((unsigned)pal[i].r << 16) | ((unsigned)pal[i].g << 8) | (unsigned)pal[i].b;
}

static void vd_clear(uint8 color) { if (g_hwbuf) memset(g_hwbuf, color, HW_W * HW_H); }

static bitmap_t *vd_lock_write(void)
{
   if (!g_hwbmp) vd_set_mode(HW_W, HW_H);
   return g_hwbmp;
}

static void vd_free_write(int num_dirties, rect_t *dirty_rects) { (void)num_dirties; (void)dirty_rects; }

/* called by vid_flush() once per drawn frame with nofrendo's primary buffer */
static void vd_custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects)
{
   (void)num_dirties; (void)dirty_rects;
   if (bmp)
      nesfe_blit((const unsigned char *const *)bmp->line, bmp->width, bmp->height, g_pal);
   if (g_playfunc)
      nesfe_audio_frame(g_playfunc);
}

static viddriver_t g_viddrv =
{
   "evo_graphics_lib (QNX Screen / GENIVI ILM)",
   vd_init,
   vd_shutdown,
   vd_set_mode,
   vd_set_palette,
   vd_clear,
   vd_lock_write,
   vd_free_write,
   vd_custom_blit,
   false
};

void osd_getvideoinfo(vidinfo_t *info)
{
   info->default_width = NES_SCREEN_WIDTH;
   info->default_height = NES_SCREEN_HEIGHT;   /* all 240 PPU lines; the frontend crops the overscan */
   info->driver = &g_viddrv;
}

/* ---------------------------------------------------------------------- input */
static unsigned g_last_buttons = 0, g_last_buttons2 = 0;

/* NESBTN_* bit order: A, B, Select, Start, Up, Down, Left, Right */
static void joypad_events(unsigned b, unsigned *last, const int *ev)
{
   unsigned chg = b ^ *last;
   int i;

   *last = b;
   for (i = 0; i < 8; i++)
   {
      unsigned bit = 1u << i;
      if (chg & bit)
      {
         event_t h = event_get(ev[i]);
         if (h) h((b & bit) ? INP_STATE_MAKE : INP_STATE_BREAK);
      }
   }
}

void osd_getinput(void)
{
   static const int ev1[8] =
   {
      event_joypad1_a, event_joypad1_b, event_joypad1_select, event_joypad1_start,
      event_joypad1_up, event_joypad1_down, event_joypad1_left, event_joypad1_right
   };
   static const int ev2[8] =
   {
      event_joypad2_a, event_joypad2_b, event_joypad2_select, event_joypad2_start,
      event_joypad2_up, event_joypad2_down, event_joypad2_left, event_joypad2_right
   };

   joypad_events(nesfe_buttons(), &g_last_buttons, ev1);
   joypad_events(nesfe_buttons2(), &g_last_buttons2, ev2);
   if (nesfe_quit_requested())
      main_quit();
}

void osd_getmouse(int *x, int *y, int *button) { *x = 0; *y = 0; *button = 0; }

/* ---------------------------------------------------------------------- files */
void osd_fullname(char *fullname, const char *shortname)
{
   strncpy(fullname, shortname, PATH_MAX);
   fullname[PATH_MAX - 1] = 0;
}

/* "<dir>/game.nes" + ".sav" -> "<dir>/game.sav": saves live beside the ROM */
char *osd_newextension(char *string, char *ext)
{
   char *slash = strrchr(string, '/');
   char *dot = strrchr(string, '.');
   if (dot && (!slash || dot > slash))
      *dot = 0;
   strcat(string, ext);
   return string;
}

int osd_makesnapname(char *filename, int len) { (void)filename; (void)len; return -1; }

/* rom_load() calls this (declared `extern char *osd_getromdata();` in nes_rom.c) for
 * the whole iNES image. Read the selected ROM file into an owned buffer; on any
 * failure hand back a zeroed stub, whose missing "NES\x1a" magic makes rom_load()
 * fail cleanly instead of dereferencing NULL. */
char *osd_getromdata(void)
{
   FILE *fp;
   long len;
   size_t got;

   if (g_romimage) { free(g_romimage); g_romimage = NULL; }
   memset(g_badrom, 0, sizeof g_badrom);

   fp = fopen(g_rom, "rb");
   if (!fp) { nesfe_log("[NES] cannot open the ROM file\n"); return (char *)g_badrom; }
   fseek(fp, 0, SEEK_END);
   len = ftell(fp);
   fseek(fp, 0, SEEK_SET);
   if (len < 16 || len > 8L * 1024 * 1024)
   {
      fclose(fp);
      nesfe_log("[NES] ROM file has an implausible size\n");
      return (char *)g_badrom;
   }
   g_romimage = (unsigned char *)malloc((size_t)len + 16);     /* +16: slack past the end */
   if (!g_romimage) { fclose(fp); return (char *)g_badrom; }
   got = fread(g_romimage, 1, (size_t)len, fp);
   fclose(fp);
   if (got != (size_t)len)
   {
      free(g_romimage); g_romimage = NULL;
      nesfe_log("[NES] short read on the ROM file\n");
      return (char *)g_badrom;
   }
   memset(g_romimage + len, 0, 16);
   return (char *)g_romimage;
}

/* ------------------------------------------------------- init / shutdown / main */
static int logprint(const char *string) { nesfe_log(string); return 0; }

int osd_init(void)
{
   log_chain_logfunc(logprint);
   return 0;
}

void osd_shutdown(void)
{
   nesosd_shutdown();
   vd_shutdown();
}

/* the O/S-specific part of main(): nofrendo_main() -> osd_main() -> main_loop() */
int osd_main(int argc, char *argv[])
{
   (void)argc; (void)argv;
   config.filename = g_cfgfile;
   return main_loop(g_rom, system_autodetect);
}

int nesosd_run(const char *rompath)
{
   g_rom = rompath ? rompath : "";
   return nofrendo_main(0, NULL);
}
