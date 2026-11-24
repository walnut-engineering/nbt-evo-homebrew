/*
 * i_video_host.c - headless video driver for the HOST (x86) build of doom_evo.
 *
 * This file is only used by host/Makefile. It lets the DOOM game loop run on
 * the development machine so game-logic, timing and input changes can be
 * exercised without the EVO head unit. It mirrors the responsibilities of
 * source/i_video_evo_v2.c (screen buffer ownership, I_StartTic -> input
 * polling) but renders nothing; optionally it dumps frames as PPM files and
 * prints player telemetry so a run can be checked from its log.
 *
 * Extra command line options understood here:
 *   -hosttics N        quit cleanly once gametic reaches N
 *   -hostdump N        write screens[0] as a PPM every N frames
 *   -hostdumpdir DIR   directory for the PPM files (default ".")
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

static byte host_palette[256 * 3];
static int host_tics_limit = 0;
static int host_dump_every = 0;
static const char *host_dump_dir = ".";
static int host_frames = 0;
static int host_initialized = 0;

void I_InitDirectInput(void);
void I_GetEvents(void);

static void host_write_ppm(void)
{
    char path[512];
    FILE *f;
    int i;

    snprintf(path, sizeof(path), "%s/frame_%05d.ppm", host_dump_dir, host_frames);
    f = fopen(path, "wb");
    if (!f) {
        printf("[HOST] cannot write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", SCREENWIDTH, SCREENHEIGHT);
    for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
        fwrite(&host_palette[screens[0][i] * 3], 1, 3, f);
    fclose(f);
    printf("[HOST] wrote %s\n", path);
}

static void host_telemetry(void)
{
    unsigned char seen[256];
    int i, colours = 0;
    player_t *pl = &players[consoleplayer];

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
        seen[screens[0][i]] = 1;
    for (i = 0; i < 256; i++)
        colours += seen[i];

    if (gamestate == GS_LEVEL && pl->mo) {
        unsigned deg = (unsigned)(((unsigned long long)pl->mo->angle * 360u) >> 32);
        printf("[HOST] frame %d tic %d state %d colours %d player x=%d y=%d z=%d angle=%u health=%d ammo=%d\n",
               host_frames, gametic, gamestate, colours,
               pl->mo->x >> FRACBITS, pl->mo->y >> FRACBITS, pl->mo->z >> FRACBITS,
               deg, pl->health, pl->ammo[am_clip]);
    } else {
        printf("[HOST] frame %d tic %d state %d colours %d\n",
               host_frames, gametic, gamestate, colours);
    }
    fflush(stdout);
}

void I_InitGraphics(void)
{
    int p;

    if (host_initialized)
        return;

    /* Same ownership model as i_video_evo_v2.c: the driver owns screens[0]. */
    screens[0] = (byte *)malloc(SCREENWIDTH * SCREENHEIGHT);
    if (!screens[0])
        I_Error("I_InitGraphics: cannot allocate screen buffer");
    memset(screens[0], 0, SCREENWIDTH * SCREENHEIGHT);

    p = M_CheckParm("-hosttics");
    if (p && p < myargc - 1)
        host_tics_limit = atoi(myargv[p + 1]);
    p = M_CheckParm("-hostdump");
    if (p && p < myargc - 1)
        host_dump_every = atoi(myargv[p + 1]);
    p = M_CheckParm("-hostdumpdir");
    if (p && p < myargc - 1)
        host_dump_dir = myargv[p + 1];

    host_initialized = 1;
    printf("[HOST] headless video ready (%dx%d), tic limit %d, dump every %d frames to %s\n",
           SCREENWIDTH, SCREENHEIGHT, host_tics_limit, host_dump_every, host_dump_dir);
    fflush(stdout);

    /* The EVO driver also brings up input from here; keep the same order. */
    I_InitDirectInput();
}

void I_ShutdownGraphics(void)
{
    if (!host_initialized)
        return;
    host_initialized = 0;
    printf("[HOST] graphics shutdown after %d frames, gametic %d\n", host_frames, gametic);
    fflush(stdout);
}

void I_SetPalette(byte *palette)
{
    memcpy(host_palette, palette, sizeof(host_palette));
}

void I_UpdateNoBlit(void)
{
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
    I_GetEvents();
}

void I_FinishUpdate(void)
{
    if (!host_initialized)
        return;

    host_frames++;

    if (host_dump_every > 0 && (host_frames % host_dump_every) == 0)
        host_write_ppm();

    if (host_frames <= 3 || (host_frames % 35) == 0)
        host_telemetry();

    if (host_tics_limit > 0 && gametic >= host_tics_limit) {
        printf("[HOST] tic limit %d reached, quitting\n", host_tics_limit);
        host_telemetry();
        I_Quit();
    }
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

int I_GetFPS(void)
{
    return 0;
}
