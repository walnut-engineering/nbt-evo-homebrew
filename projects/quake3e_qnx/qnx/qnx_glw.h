/*
 * qnx_glw.h - QNX (NBT EVO) replacement for sdl_glw.h. SDL-free: just the glimp
 * window state shared between qnx_glimp.c and qnx_input.c, plus the input decls.
 */
#ifndef __GLW_QNX_H__
#define __GLW_QNX_H__

#include <screen/screen.h>
#include <EGL/egl.h>

typedef struct
{
	FILE *log_fp;

	qboolean isFullscreen;

	glconfig_t *config; // feedback to renderer module

	int desktop_width;
	int desktop_height;

	int window_width;
	int window_height;

	int monitorCount;

	// QNX Screen + EGL state (the evo_gltest recipe)
	screen_context_t	screen_ctx;
	screen_window_t		screen_win;
	EGLDisplay			dpy;
	EGLSurface			surf;
	EGLContext			ctx;

} glwstate_t;

extern glwstate_t glw_state;

extern cvar_t *in_nograb;

void IN_Init( void );
void IN_Shutdown( void );

#endif
