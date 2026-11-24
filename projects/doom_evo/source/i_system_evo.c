// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// QNX-specific system interface for DOOM
// Based on original i_system.c but adapted for QNX
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_system_qnx.c,v 1.0 2025/05/30 doom_qnx Exp $";

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>

// QNX-specific includes
#ifdef __EVO__
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <time.h>
#include <errno.h>
#endif

#include "doomdef.h"
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"
#include "d_net.h"
#include "g_game.h"

#ifdef __GNUG__
#pragma implementation "i_system.h"
#endif
#include "i_system.h"

int	mb_used = 6;

void
I_Tactile
( int	on,
  int	off,
  int	total )
{
  // UNUSED.
  on = off = total = 0;
}

ticcmd_t	emptycmd;
ticcmd_t*	I_BaseTiccmd(void)
{
    return &emptycmd;
}

int  I_GetHeapSize (void)
{
    return mb_used*1024*1024;
}

byte* I_ZoneBase (int*	size)
{
    *size = mb_used*1024*1024;
    return (byte *) malloc (*size);
}

//
// I_GetTime
// returns time in 1/70th second tics
//
#ifdef __QNXNTO__
#include <sys/neutrino.h>
#include <sys/syspage.h>
#endif

int  I_GetTime (void)
{
#ifdef __QNXNTO__
    /* gettimeofday() on the head unit only advances once per kernel tick, so
     * tic boundaries were seen late and the game loop settled at 29 fps
     * instead of 35. ClockCycles() is the free-running CPU cycle counter. */
    static uint64_t base = 0;
    static uint64_t cps = 0;
    uint64_t now = ClockCycles();
    if (!cps) {
        cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
        base = now;
    }
    return (int)(((now - base) * (uint64_t)TICRATE) / cps);
#else
    struct timeval	tp;
    struct timezone	tzp;
    int			newtics;
    static int		basetime=0;
  
    gettimeofday(&tp, &tzp);
    if (!basetime)
	basetime = tp.tv_sec;
    newtics = (tp.tv_sec-basetime)*TICRATE + tp.tv_usec*TICRATE/1000000;
    return newtics;
#endif
}

//
// I_Init
//
void I_Init (void)
{
    I_InitSound();
    //  I_InitGraphics();
}

//
// I_Quit
//
void I_Quit (void)
{
    printf("I_Quit: Starting shutdown sequence\n");
    
    printf("I_Quit: Calling D_QuitNetGame\n");
    D_QuitNetGame ();
    
    printf("I_Quit: Calling I_ShutdownSound\n");
    I_ShutdownSound();
    
    printf("I_Quit: Calling I_ShutdownMusic\n");
    I_ShutdownMusic();
    
    printf("I_Quit: Calling M_SaveDefaults\n");
    M_SaveDefaults ();
    
    printf("I_Quit: Calling I_ShutdownGraphics\n");
    I_ShutdownGraphics();
    
    printf("I_Quit: All shutdown complete, exiting\n");
    fflush(stdout);
    _exit(0);
}

void I_WaitVBL(int count)
{
#ifdef __EVO__
    // QNX-specific VBL wait - use nanosleep for timing
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = (count * 16666667); // ~60Hz VBL timing
    nanosleep(&req, NULL);
#else
    // Fallback for non-QNX systems
    usleep (count * (1000000/70) );
#endif
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

byte*	I_AllocLow(int length)
{
    byte*	mem;
        
    mem = (byte *)malloc (length);
    memset (mem,0,length);
    return mem;
}

//
// I_Error
//
extern boolean demorecording;

void I_Error (char *error, ...)
{
    va_list	argptr;

    // Message first.
    va_start (argptr,error);
    fprintf (stderr, "Error: ");
    vfprintf (stderr,error,argptr);
    fprintf (stderr, "\n");
    va_end (argptr);

    fflush( stderr );

    // Shutdown. Here might be other errors.
    if (demorecording)
	G_CheckDemoStatus();

    D_QuitNetGame ();
    I_ShutdownGraphics();
    
    exit(-1);
}

//
// QNX-specific system information functions
//
#ifdef __EVO__
void I_QNXSystemInfo(void)
{
    printf("QNX System Information:\n");
    printf("Page size: %d bytes\n", getpagesize());
    
    // Get CPU information from syspage
    if (_syspage_ptr) {
        printf("CPU frequency available in syspage\n");
        printf("Number of CPUs: %d\n", _syspage_ptr->num_cpu);
    }
}
#endif
