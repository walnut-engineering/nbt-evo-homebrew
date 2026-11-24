/*
 * qnx_ilm.c - see qnx_ilm.h. Minimal GENIVI ILM client (dlopen'd) that makes
 * our GLES2 window's surface visible on top of the CID via the LayerManager.
 *
 * The sequence mirrors the proven one in shared/evo_graphics_lib
 * (evo_display_unified.c), trimmed to the "new layer + render-order-first"
 * path that is known to work on this unit. All ILM entry points are resolved
 * with dlsym so the game still links/runs if the LayerManager libs are absent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include "qnx_ilm.h"

/* ---- minimal ILM ABI (matches libilmClient on the unit) ---- */
typedef unsigned int t_ilm_uint;
typedef unsigned int t_ilm_bool;   /* 4-byte on purpose: getBool() writes 4 aligned bytes */
typedef int          t_ilm_errorTypes;
#define ILM_OK 0

static void *g_ilm;
static int   g_layer_added;

static t_ilm_errorTypes (*p_init)(void);
static t_ilm_errorTypes (*p_destroy)(void);
static t_ilm_errorTypes (*p_commit)(void);
static t_ilm_errorTypes (*p_getSurfaceIDs)(int*, t_ilm_uint**);
static t_ilm_errorTypes (*p_layerCreateDim)(t_ilm_uint*, t_ilm_uint, t_ilm_uint);
static t_ilm_errorTypes (*p_layerRemove)(t_ilm_uint);
static t_ilm_errorTypes (*p_layerAddSurface)(t_ilm_uint, t_ilm_uint);
static t_ilm_errorTypes (*p_surfSetVis)(t_ilm_uint, t_ilm_bool);
static t_ilm_errorTypes (*p_layerSetVis)(t_ilm_uint, t_ilm_bool);
static t_ilm_errorTypes (*p_surfSetDst)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint);
static t_ilm_errorTypes (*p_surfSetSrc)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint);
static t_ilm_errorTypes (*p_layerSetDst)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint);
static t_ilm_errorTypes (*p_layerSetSrc)(t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint, t_ilm_uint);
static t_ilm_errorTypes (*p_layerSetRenderOrder)(t_ilm_uint, t_ilm_uint*, t_ilm_uint);
static t_ilm_errorTypes (*p_displaySetRenderOrder)(t_ilm_uint, t_ilm_uint*, t_ilm_uint);
static void             (*p_getLayerIDsOnScreen)(t_ilm_uint, t_ilm_uint*, t_ilm_uint**);

#define SYM(v,n) do{ *(void**)&(v) = dlsym(g_ilm, n); if(!(v)){ printf("QNX ILM: missing %s\n", n); } }while(0)

static int ilm_load( void )
{
	const char *names[] = {
		"libilmClient.cpp-ne.so.1",
		"/fs/sda0/opt/sys/lib/libilmClient.cpp-ne.so.1",
		"libilmClient.gpp.so.1",
		"libilmClient.so.1",
		NULL
	};
	int i;
	for ( i = 0; names[i]; i++ ) {
		g_ilm = dlopen( names[i], RTLD_LAZY | RTLD_GLOBAL );
		if ( g_ilm ) { printf( "QNX ILM: loaded %s\n", names[i] ); break; }
	}
	if ( !g_ilm ) { printf( "QNX ILM: libilmClient not found - window stays off-screen\n" ); return -1; }

	SYM( p_init,                 "ilm_init" );
	SYM( p_destroy,              "ilm_destroy" );
	SYM( p_commit,               "ilm_commitChanges" );
	SYM( p_getSurfaceIDs,        "ilm_getSurfaceIDs" );
	SYM( p_layerCreateDim,       "ilm_layerCreateWithDimension" );
	SYM( p_layerRemove,          "ilm_layerRemove" );
	SYM( p_layerAddSurface,      "ilm_layerAddSurface" );
	SYM( p_surfSetVis,           "ilm_surfaceSetVisibility" );
	SYM( p_layerSetVis,          "ilm_layerSetVisibility" );
	SYM( p_surfSetDst,           "ilm_surfaceSetDestinationRectangle" );
	SYM( p_surfSetSrc,           "ilm_surfaceSetSourceRectangle" );
	SYM( p_layerSetDst,          "ilm_layerSetDestinationRectangle" );
	SYM( p_layerSetSrc,          "ilm_layerSetSourceRectangle" );
	SYM( p_layerSetRenderOrder,  "ilm_layerSetRenderOrder" );
	SYM( p_displaySetRenderOrder,"ilm_displaySetRenderOrder" );
	SYM( p_getLayerIDsOnScreen,  "ilm_getLayerIDsOnScreen" );

	if ( !p_init || !p_commit || !p_getSurfaceIDs || !p_layerCreateDim ||
	     !p_layerAddSurface || !p_displaySetRenderOrder || !p_getLayerIDsOnScreen ) {
		printf( "QNX ILM: core entry points missing - cannot composite\n" );
		return -1;
	}
	return 0;
}

/* wait (up to ~3s) for the LayerManager to create our surface from the posted window */
static int wait_for_surface( t_ilm_uint sid )
{
	int attempt;
	for ( attempt = 0; attempt < 30; attempt++ ) {
		int count = 0, i; t_ilm_uint *ids = NULL;
		if ( p_getSurfaceIDs( &count, &ids ) == ILM_OK && ids ) {
			for ( i = 0; i < count; i++ ) if ( ids[i] == sid ) { free( ids ); return 0; }
			free( ids );
		}
		usleep( 100000 );
	}
	return -1;
}

int qnx_ilm_make_visible( int width, int height )
{
	t_ilm_uint surf = QNX_ILM_SURFACE_ID, layer = QNX_ILM_LAYER_ID;
	t_ilm_uint *cur = NULL, *order = NULL, ncur = 0, n = 0, i;

	if ( ilm_load() != 0 ) return -1;
	if ( p_init() != ILM_OK ) { printf( "QNX ILM: ilm_init failed\n" ); return -1; }

	if ( wait_for_surface( surf ) != 0 )
		printf( "QNX ILM: surface %u not registered yet - continuing anyway\n", surf );
	else
		printf( "QNX ILM: surface %u registered by LayerManager\n", surf );

	/* our layer, full CID size */
	p_layerCreateDim( &layer, (t_ilm_uint)width, (t_ilm_uint)height );
	g_layer_added = 1;
	if ( p_layerSetDst ) p_layerSetDst( layer, 0, 0, width, height );
	if ( p_layerSetSrc ) p_layerSetSrc( layer, 0, 0, width, height );
	if ( p_surfSetDst )  p_surfSetDst( surf, 0, 0, width, height );
	if ( p_surfSetSrc )  p_surfSetSrc( surf, 0, 0, width, height );
	p_surfSetVis( surf, 1 );
	p_layerSetVis( layer, 1 );
	p_layerAddSurface( layer, surf );
	if ( p_layerSetRenderOrder ) p_layerSetRenderOrder( layer, &surf, 1 );

	/* put our layer FIRST (= on top on this unit) in screen 0's render order */
	p_getLayerIDsOnScreen( QNX_ILM_SCREEN_ID, &ncur, &cur );
	order = (t_ilm_uint*)malloc( ( ncur + 1 ) * sizeof( t_ilm_uint ) );
	if ( !order ) { if ( cur ) free( cur ); return -1; }
	order[n++] = layer;
	for ( i = 0; i < ncur; i++ ) if ( cur[i] != layer ) order[n++] = cur[i];
	p_displaySetRenderOrder( QNX_ILM_SCREEN_ID, order, n );
	p_commit();
	printf( "QNX ILM: layer %u placed on top of screen %d (%u layers total)\n",
		layer, QNX_ILM_SCREEN_ID, n );

	free( order );
	if ( cur ) free( cur );
	return 0;
}

void qnx_ilm_hide( void )
{
	if ( !g_ilm ) return;
	/* drop our layer from the render order so the HMI is whole again */
	if ( g_layer_added && p_getLayerIDsOnScreen && p_displaySetRenderOrder ) {
		t_ilm_uint *cur = NULL, *order = NULL, ncur = 0, n = 0, i;
		p_getLayerIDsOnScreen( QNX_ILM_SCREEN_ID, &ncur, &cur );
		if ( cur ) {
			order = (t_ilm_uint*)malloc( ncur * sizeof( t_ilm_uint ) );
			if ( order ) {
				for ( i = 0; i < ncur; i++ ) if ( cur[i] != (t_ilm_uint)QNX_ILM_LAYER_ID ) order[n++] = cur[i];
				p_displaySetRenderOrder( QNX_ILM_SCREEN_ID, order, n );
				if ( p_commit ) p_commit();
				free( order );
			}
			free( cur );
		}
		if ( p_layerRemove ) p_layerRemove( QNX_ILM_LAYER_ID );
		if ( p_commit ) p_commit();
	}
	if ( p_destroy ) p_destroy();
	dlclose( g_ilm );
	g_ilm = NULL;
}
