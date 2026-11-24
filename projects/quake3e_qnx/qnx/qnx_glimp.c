/*
 * qnx_glimp.c - QNX (NBT EVO / OMAP5 / PowerVR SGX544MP) GL context layer for
 * quake3e, replacing code/sdl/sdl_glimp.c. Implements the glimp interface the
 * renderer imports via ri.* : GLimp_Init / GLimp_EndFrame / GLimp_Shutdown /
 * GL_GetProcAddress / GLimp_Minimize / GLimp_LogComment, plus the gamma and
 * clipboard stubs the client expects.
 *
 * The GL context is real hardware OpenGL ES 2.0 on the PowerVR SGX544MP, brought
 * up with the proven evo_gltest recipe:
 *   - a QNX Screen window with SCREEN_USAGE_OPENGL_ES2
 *   - eglGetDisplay((EGLNativeDisplayType)1)   (graphics.conf: 1 = PowerVR 3D)
 *   - direct EGL (libEGL.so.1 / libGLESv2.so.1), ES2 config, window surface
 * Runtime env must set GRAPHICS_ROOT=/lib/graphics/omap5430 and an LD_LIBRARY_PATH
 * that includes it (see projects/quake3e_qnx/README.md / docs/gpu-gles2-breakthrough.md).
 *
 * GL_GetProcAddress resolves the renderer's qgl* table: core GLES2 entry points
 * are linked in (dlsym RTLD_DEFAULT finds them); extensions go through
 * eglGetProcAddress. Desktop-GL-only names resolve to NULL - that is the
 * renderer2->GLES2 porting surface (milestone M4), not a glimp failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "client.h"
#include "tr_public.h"
#include "qnx_glw.h"
#include "qnx_ilm.h"

#include <GLES2/gl2.h>

typedef enum {
	RSERR_OK,
	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,
	RSERR_FATAL_ERROR,
	RSERR_UNKNOWN
} rserr_t;

glwstate_t glw_state;

cvar_t *r_stereoEnabled;
cvar_t *in_nograb;

/*
===============
GLimp_LogComment
===============
*/
void GLimp_LogComment( const char *comment )
{
	if ( glw_state.log_fp )
		fprintf( glw_state.log_fp, "%s", comment );
}


/*
===============
GLimp_Minimize
===============
*/
void GLimp_Minimize( void )
{
	// no window manager on the head unit - nothing to minimize
}


/*
===============
QNX_DestroyContext - tear down EGL + Screen
===============
*/
static void QNX_DestroyContext( void )
{
	if ( glw_state.dpy != EGL_NO_DISPLAY ) {
		eglMakeCurrent( glw_state.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
		if ( glw_state.ctx != EGL_NO_CONTEXT )  eglDestroyContext( glw_state.dpy, glw_state.ctx );
		if ( glw_state.surf != EGL_NO_SURFACE ) eglDestroySurface( glw_state.dpy, glw_state.surf );
		eglTerminate( glw_state.dpy );
	}
	if ( glw_state.screen_win ) screen_destroy_window( glw_state.screen_win );
	if ( glw_state.screen_ctx ) screen_destroy_context( glw_state.screen_ctx );
	glw_state.dpy = EGL_NO_DISPLAY;
	glw_state.surf = EGL_NO_SURFACE;
	glw_state.ctx = EGL_NO_CONTEXT;
	glw_state.screen_win = NULL;
	glw_state.screen_ctx = NULL;
}


/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown( qboolean unloadDLL )
{
	qnx_ilm_hide();           // drop our layer so the HMI is whole again
	IN_Shutdown();
	QNX_DestroyContext();
	memset( &glw_state, 0, sizeof( glw_state ) );
}


/*
===============
QNX_GetDisplaySize - physical resolution of the first attached display
===============
*/
static qboolean QNX_GetDisplaySize( int *w, int *h )
{
	int ndisp = 0;
	screen_display_t *disps;
	int dims[2] = { 0, 0 };

	if ( screen_get_context_property_iv( glw_state.screen_ctx,
			SCREEN_PROPERTY_DISPLAY_COUNT, &ndisp ) != 0 || ndisp < 1 )
		return qfalse;

	disps = Z_Malloc( ndisp * sizeof( *disps ) );
	if ( screen_get_context_property_pv( glw_state.screen_ctx,
			SCREEN_PROPERTY_DISPLAYS, (void **)disps ) != 0 ) {
		Z_Free( disps );
		return qfalse;
	}
	if ( screen_get_display_property_iv( disps[0], SCREEN_PROPERTY_SIZE, dims ) != 0
			|| dims[0] <= 0 || dims[1] <= 0 ) {
		Z_Free( disps );
		return qfalse;
	}
	Z_Free( disps );
	*w = dims[0];
	*h = dims[1];
	return qtrue;
}


/*
===============
QNX_SetMode - create the Screen window + EGL/GLES2 context, fill glConfig
===============
*/
static rserr_t QNX_SetMode( glconfig_t *config )
{
	int W, H;
	int fmt, usage, zorder, interval;
	int size[2];
	EGLint vmaj = 0, vmin = 0, ncfg = 0;
	EGLConfig cfg;
	const EGLint cfgattr[] = {
		EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
		EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	const EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	intptr_t dpycand[] = { 1, 2, (intptr_t)EGL_DEFAULT_DISPLAY };
	unsigned i;

	// ---- QNX Screen window ----
	if ( screen_create_context( &glw_state.screen_ctx, SCREEN_APPLICATION_CONTEXT ) != 0 ) {
		Com_Printf( S_COLOR_RED "QNX_SetMode: screen_create_context failed\n" );
		return RSERR_FATAL_ERROR;
	}

	if ( !QNX_GetDisplaySize( &W, &H ) ) {
		W = 1440; H = 540; // NBT CID full panel fallback
		Com_Printf( "QNX_SetMode: display size query failed, using %dx%d\n", W, H );
	}

	if ( screen_create_window( &glw_state.screen_win, glw_state.screen_ctx ) != 0 ) {
		Com_Printf( S_COLOR_RED "QNX_SetMode: screen_create_window failed\n" );
		return RSERR_FATAL_ERROR;
	}
	fmt = SCREEN_FORMAT_RGBX8888;
	screen_set_window_property_iv( glw_state.screen_win, SCREEN_PROPERTY_FORMAT, &fmt );
	usage = SCREEN_USAGE_OPENGL_ES2;
	screen_set_window_property_iv( glw_state.screen_win, SCREEN_PROPERTY_USAGE, &usage );
	size[0] = W; size[1] = H;
	screen_set_window_property_iv( glw_state.screen_win, SCREEN_PROPERTY_SIZE, size );
	screen_set_window_property_iv( glw_state.screen_win, SCREEN_PROPERTY_SOURCE_SIZE, size );
	zorder = 10; // sit above the live HMI while we bring the game up
	screen_set_window_property_iv( glw_state.screen_win, SCREEN_PROPERTY_ZORDER, &zorder );
	{
		// Advertise an ILM surface id so the GENIVI LayerManager picks this
		// window up as a surface (see qnx_ilm.c); it maps the window's ID
		// string to the ILM surface number.
		char idstr[16];
		Com_sprintf( idstr, sizeof( idstr ), "%d", QNX_ILM_SURFACE_ID );
		screen_set_window_property_cv( glw_state.screen_win, SCREEN_PROPERTY_ID_STRING,
			strlen( idstr ), idstr );
	}
	if ( screen_create_window_buffers( glw_state.screen_win, 2 ) != 0 ) {
		Com_Printf( S_COLOR_RED "QNX_SetMode: screen_create_window_buffers failed\n" );
		return RSERR_FATAL_ERROR;
	}
	Com_Printf( "QNX Screen window %dx%d (OPENGL_ES2)\n", W, H );

	// ---- EGL bring-up (direct PowerVR, display handle 1) ----
	glw_state.dpy = EGL_NO_DISPLAY;
	for ( i = 0; i < ARRAY_LEN( dpycand ); i++ ) {
		glw_state.dpy = eglGetDisplay( (EGLNativeDisplayType)dpycand[i] );
		if ( glw_state.dpy != EGL_NO_DISPLAY )
			break;
	}
	if ( glw_state.dpy == EGL_NO_DISPLAY ) {
		Com_Printf( S_COLOR_RED "QNX_SetMode: eglGetDisplay found no display\n" );
		return RSERR_FATAL_ERROR;
	}
	if ( !eglInitialize( glw_state.dpy, &vmaj, &vmin ) ) {
		Com_Printf( S_COLOR_RED "QNX_SetMode: eglInitialize failed (0x%x)\n", eglGetError() );
		return RSERR_FATAL_ERROR;
	}
	Com_Printf( "EGL %d.%d  vendor=%s\n", vmaj, vmin, eglQueryString( glw_state.dpy, EGL_VENDOR ) );

	eglBindAPI( EGL_OPENGL_ES_API );
	if ( !eglChooseConfig( glw_state.dpy, cfgattr, &cfg, 1, &ncfg ) || ncfg < 1 ) {
		Com_Printf( S_COLOR_RED "QNX_SetMode: eglChooseConfig failed (0x%x)\n", eglGetError() );
		return RSERR_FATAL_ERROR;
	}
	glw_state.surf = eglCreateWindowSurface( glw_state.dpy, cfg,
		(EGLNativeWindowType)glw_state.screen_win, NULL );
	if ( glw_state.surf == EGL_NO_SURFACE ) {
		Com_Printf( S_COLOR_RED "QNX_SetMode: eglCreateWindowSurface failed (0x%x)\n", eglGetError() );
		return RSERR_FATAL_ERROR;
	}
	glw_state.ctx = eglCreateContext( glw_state.dpy, cfg, EGL_NO_CONTEXT, ctxattr );
	if ( glw_state.ctx == EGL_NO_CONTEXT ) {
		Com_Printf( S_COLOR_RED "QNX_SetMode: eglCreateContext failed (0x%x)\n", eglGetError() );
		return RSERR_FATAL_ERROR;
	}
	if ( !eglMakeCurrent( glw_state.dpy, glw_state.surf, glw_state.surf, glw_state.ctx ) ) {
		Com_Printf( S_COLOR_RED "QNX_SetMode: eglMakeCurrent failed (0x%x)\n", eglGetError() );
		return RSERR_FATAL_ERROR;
	}

	// vsync: r_swapInterval (0 = off/unlocked); renderer-owned cvar, read locally
	interval = Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE | CVAR_LATCH )->integer;
	eglSwapInterval( glw_state.dpy, interval );

	// Post one black frame so the LayerManager creates our ILM surface from the
	// now-posted window, then raise our layer to the top of the CID so the game
	// is actually visible over the live HMI.
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	eglSwapBuffers( glw_state.dpy, glw_state.surf );
	qnx_ilm_make_visible( W, H );

	// ---- fill glConfig ----
	config->vidWidth = W;
	config->vidHeight = H;
	config->windowAspect = (float)W / (float)H;
	config->colorBits = 24;
	config->depthBits = 24;
	config->stencilBits = 8;
	config->displayFrequency = 60;
	config->isFullscreen = qtrue;
	config->deviceSupportsGamma = qfalse; // no gamma ramp on this EGL stack

	glw_state.isFullscreen = qtrue;
	glw_state.window_width = W;
	glw_state.window_height = H;
	glw_state.desktop_width = W;
	glw_state.desktop_height = H;

	Com_Printf( "GL_RENDERER: %s\n", (const char *)glGetString( GL_RENDERER ) );

	return RSERR_OK;
}


/*
===============
GLimp_Init
===============
*/
void GLimp_Init( glconfig_t *config )
{
	rserr_t err;

	Com_DPrintf( "GLimp_Init()\n" );

	glw_state.config = config;
	glw_state.dpy = EGL_NO_DISPLAY;
	glw_state.surf = EGL_NO_SURFACE;
	glw_state.ctx = EGL_NO_CONTEXT;

	in_nograb = Cvar_Get( "in_nograb", "1", 0 );
	r_stereoEnabled = Cvar_Get( "r_stereoEnabled", "0", CVAR_ARCHIVE | CVAR_LATCH );

	err = QNX_SetMode( config );
	if ( err != RSERR_OK ) {
		Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL ES 2.0 subsystem" );
		return;
	}

	// force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	IN_Init();
}


/*
===============
GLimp_EndFrame - swap buffers
===============
*/
void GLimp_EndFrame( void )
{
	if ( glw_state.dpy != EGL_NO_DISPLAY )
		eglSwapBuffers( glw_state.dpy, glw_state.surf );
}


/*
===============
GL_GetProcAddress - resolve qgl* for the renderer

Core GLES2 symbols are linked in (dlsym RTLD_DEFAULT); extensions come from
eglGetProcAddress. Desktop-GL-only names return NULL (the M4 porting surface).
===============
*/
void *GL_GetProcAddress( const char *symbol )
{
	void *p = dlsym( RTLD_DEFAULT, symbol );
	if ( p == NULL )
		p = (void *)eglGetProcAddress( symbol );
	return p;
}


/*
===============
Gamma - not supported on this EGL stack (deviceSupportsGamma = qfalse)
===============
*/
void GLimp_InitGamma( glconfig_t *config )
{
	config->deviceSupportsGamma = qfalse;
}

void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
	// no-op
}


/*
===============
Clipboard - headless unit, nothing to share
===============
*/
char *Sys_GetClipboardData( void )
{
	return NULL;
}

void Sys_SetClipboardBitmap( const byte *bitmap, int length )
{
	// no-op
}
