/*
 * qnx_ref_stub.c - a minimal stub renderer for quake3e, used for milestone M3.
 *
 * It implements GetRefAPI + the refexport_t interface the client drives, but
 * does no scene rendering: BeginRegistration brings up the GL context via
 * ri.GLimp_Init (this is where the EGL/GLES2 window opens on the head unit),
 * BeginFrame clears the framebuffer to a colour, EndFrame swaps. Everything
 * else is a safe no-op / dummy handle so the full client links and runs.
 *
 * This isolates M3 (client links + window opens) from M4 (porting the real
 * renderer2 to GLES2). When renderer2 is ported, this file is dropped and the
 * real GetRefAPI (tr_init.c) is linked instead - the QNX glimp/input/sound
 * layer carries over unchanged.
 */
#include "q_shared.h"
#include "qcommon.h"
#include "tr_public.h"
#include <GLES2/gl2.h>

refimport_t ri;

static glconfig_t  s_config;
static qboolean    s_glInit = qfalse;
static float       s_clear[4] = { 0.10f, 0.12f, 0.18f, 1.0f };

// ---- lifecycle ----------------------------------------------------------
static void RE_Shutdown( refShutdownCode_t code )
{
	if ( s_glInit && code != REF_KEEP_CONTEXT ) {
		ri.GLimp_Shutdown( code == REF_UNLOAD_DLL );
		s_glInit = qfalse;
	}
}

static void RE_BeginRegistration( glconfig_t *config )
{
	if ( !s_glInit ) {
		ri.GLimp_Init( &s_config );   // opens the EGL/GLES2 window
		s_glInit = qtrue;
		glViewport( 0, 0, s_config.vidWidth, s_config.vidHeight );
		ri.Printf( PRINT_ALL, "stub renderer: GL up %dx%d, GL_RENDERER=%s\n",
			s_config.vidWidth, s_config.vidHeight, (const char *)glGetString( GL_RENDERER ) );
	}
	*config = s_config;             // hand the config back to the client
}

static const glconfig_t *RE_GetConfig( void ) { return &s_config; }

// ---- frame --------------------------------------------------------------
static void RE_BeginFrame( stereoFrame_t stereoFrame )
{
	if ( s_glInit ) {
		glClearColor( s_clear[0], s_clear[1], s_clear[2], s_clear[3] );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}
}

static void RE_EndFrame( int *frontEndMsec, int *backEndMsec )
{
	if ( s_glInit )
		ri.GLimp_EndFrame();
	if ( frontEndMsec ) *frontEndMsec = 0;
	if ( backEndMsec )  *backEndMsec = 0;
}

// ---- registration (dummy handles) ---------------------------------------
static qhandle_t RE_RegisterModel( const char *name )       { return 1; }
static qhandle_t RE_RegisterSkin( const char *name )        { return 1; }
static qhandle_t RE_RegisterShader( const char *name )      { return 1; }
static qhandle_t RE_RegisterShaderNoMip( const char *name ) { return 1; }
static void RE_LoadWorld( const char *name )                {}
static void RE_SetWorldVisData( const byte *vis )           {}
static void RE_EndRegistration( void )                      {}
static void RE_RegisterFont( const char *fontName, int pointSize, fontInfo_t *font ) { if (font) memset(font,0,sizeof(*font)); }

// ---- scene (no-ops) ------------------------------------------------------
static void RE_ClearScene( void ) {}
static void RE_AddRefEntityToScene( const refEntity_t *re, qboolean intShaderTime ) {}
static void RE_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int num ) {}
static int  RE_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir ) { return 0; }
static void RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {}
static void RE_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {}
static void RE_AddLinearLightToScene( const vec3_t start, const vec3_t end, float intensity, float r, float g, float b ) {}
static void RE_RenderScene( const refdef_t *fd ) {}

// ---- 2D (no-ops) ---------------------------------------------------------
static void RE_SetColor( const float *rgba ) {}
static void RE_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader ) {}
static void RE_DrawStretchRaw( int x, int y, int w, int h, int cols, int rows, byte *data, int client, qboolean dirty ) {}
static void RE_UploadCinematic( int w, int h, int cols, int rows, byte *data, int client, qboolean dirty ) {}

// ---- misc (safe defaults) ------------------------------------------------
static int  RE_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection, int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer ) { return 0; }
static int  RE_LerpTag( orientation_t *tag, qhandle_t model, int startFrame, int endFrame, float frac, const char *tagName ) { if (tag){ VectorClear(tag->origin); VectorClear(tag->axis[0]); VectorClear(tag->axis[1]); VectorClear(tag->axis[2]); } return 0; }
static void RE_ModelBounds( qhandle_t model, vec3_t mins, vec3_t maxs ) { if(mins) VectorClear(mins); if(maxs) VectorClear(maxs); }
static void RE_RemapShader( const char *oldShader, const char *newShader, const char *offsetTime ) {}
static qboolean RE_GetEntityToken( char *buffer, int size ) { if (buffer && size) buffer[0] = '\0'; return qfalse; }
static qboolean RE_inPVS( const vec3_t p1, const vec3_t p2 ) { return qtrue; }
static void RE_TakeVideoFrame( int h, int w, byte *captureBuffer, byte *encodeBuffer, qboolean motionJpeg ) {}
static void RE_ThrottleBackend( void ) {}
static void RE_FinishBloom( void ) {}
static void RE_SetColorMappings( void ) {}
static qboolean RE_CanMinimize( void ) { return qfalse; }
static void RE_VertexLighting( qboolean allowed ) {}
static void RE_SyncRender( void ) {}

// ---- the only exported entry point --------------------------------------
refexport_t *GetRefAPI( int apiVersion, refimport_t *rimp )
{
	static refexport_t re;

	ri = *rimp;
	Com_Memset( &re, 0, sizeof( re ) );

	if ( apiVersion != REF_API_VERSION ) {
		ri.Printf( PRINT_ALL, "Mismatched REF_API_VERSION: expected %i, got %i\n",
			REF_API_VERSION, apiVersion );
		return NULL;
	}

	re.Shutdown              = RE_Shutdown;
	re.BeginRegistration     = RE_BeginRegistration;
	re.RegisterModel         = RE_RegisterModel;
	re.RegisterSkin          = RE_RegisterSkin;
	re.RegisterShader        = RE_RegisterShader;
	re.RegisterShaderNoMip   = RE_RegisterShaderNoMip;
	re.LoadWorld             = RE_LoadWorld;
	re.SetWorldVisData       = RE_SetWorldVisData;
	re.EndRegistration       = RE_EndRegistration;
	re.ClearScene            = RE_ClearScene;
	re.AddRefEntityToScene   = RE_AddRefEntityToScene;
	re.AddPolyToScene        = RE_AddPolyToScene;
	re.LightForPoint         = RE_LightForPoint;
	re.AddLightToScene       = RE_AddLightToScene;
	re.AddAdditiveLightToScene = RE_AddAdditiveLightToScene;
	re.AddLinearLightToScene = RE_AddLinearLightToScene;
	re.RenderScene           = RE_RenderScene;
	re.SetColor              = RE_SetColor;
	re.DrawStretchPic        = RE_DrawStretchPic;
	re.DrawStretchRaw        = RE_DrawStretchRaw;
	re.UploadCinematic       = RE_UploadCinematic;
	re.BeginFrame            = RE_BeginFrame;
	re.EndFrame              = RE_EndFrame;
	re.MarkFragments         = RE_MarkFragments;
	re.LerpTag               = RE_LerpTag;
	re.ModelBounds           = RE_ModelBounds;
	re.RegisterFont          = RE_RegisterFont;
	re.RemapShader           = RE_RemapShader;
	re.GetEntityToken        = RE_GetEntityToken;
	re.inPVS                 = RE_inPVS;
	re.TakeVideoFrame        = RE_TakeVideoFrame;
	re.ThrottleBackend       = RE_ThrottleBackend;
	re.FinishBloom           = RE_FinishBloom;
	re.SetColorMappings      = RE_SetColorMappings;
	re.CanMinimize           = RE_CanMinimize;
	re.GetConfig             = RE_GetConfig;
	re.VertexLighting        = RE_VertexLighting;
	re.SyncRender            = RE_SyncRender;

	return &re;
}
