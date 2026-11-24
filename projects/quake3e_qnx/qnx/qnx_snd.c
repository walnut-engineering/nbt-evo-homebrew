/*
 * qnx_snd.c - QNX (NBT EVO) sound driver for quake3e, replacing
 * code/sdl/sdl_snd.c.
 *
 * quake3e's mixer owns a DMA ring (dma.buffer, dma.samples mono-samples). It
 * paints ahead of a playback cursor and learns how far the hardware has
 * consumed via SNDDMA_GetDMAPos(). SDL's backend fakes that cursor from an
 * audio callback (code/sdl/sdl_snd.c); we do the same with a consumer thread
 * over the head unit's QNX Sound Architecture (QSA):
 *
 *   * The device is opened with DOOM's proven recipe (i_sound_evo.c, verified
 *     on hu-omap 2026-11-03): snd_pcm_open_preferred(), S16_LE stereo, BLOCK
 *     mode, START_FULL, 4096-byte fragments (A2I rejects non-power-of-two),
 *     frags_max 4. The device's own rate (44100 here) becomes dma.speed so the
 *     engine mixes at the output rate with no resampling on our side.
 *   * One consumer thread copies a fragment out of dma.buffer at the playback
 *     cursor (same wrap logic as SDL's callback), advances the cursor, then
 *     does a BLOCKING snd_pcm_plugin_write(). The write itself paces the thread,
 *     so the cursor advances at the true device rate - exactly what the engine's
 *     timing needs.
 *   * SNDDMA_GetDMAPos() returns the cursor (mono-samples); SNDDMA_Begin/Submit
 *     are the lock that keeps the engine's paint from racing the copy (mirrors
 *     SDL_LockAudioDevice/UnlockAudioDevice).
 *   * audioconn -b 0xb8 builds the "browser" audio source -> eSNK_SPEAKERS while
 *     the game runs; -r 0xb8 on shutdown. The HU attenuates that source, so a
 *     post-mix master gain (default 3x, as DOOM) keeps us audible; tune at launch
 *     with EVO_Q3_AUDIO_GAIN (percent, e.g. 200 = 2x), clamped 0.1x..8x.
 */
#include "snd_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/asoundlib.h>

static qboolean snd_inited = qfalse;

// playback cursor, in mono-samples (frames * channels), like SDL's dmapos
static volatile int dmapos = 0;
static int          dmasize = 0;        // dma.buffer size in bytes

// QSA device (DOOM's qsa_open recipe)
static snd_pcm_t*   g_pcm = NULL;
static int          g_out_rate = 44100;
static int          g_frag_bytes = 4096;

// consumer thread + ring lock
static pthread_t       g_tid;
static volatile int    g_run = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

// browser audio source routed?
static int          g_routed = 0;

// post-mix master gain (256 = 1.0x). The browser source is attenuated by the
// HU, so we boost like DOOM does. Default 3x; EVO_Q3_AUDIO_GAIN (percent).
static int          g_gain_x256 = 768;

// ---------------------------------------------------------------------------
// Route (browser audio source, connection 0xb8) - verbatim from i_sound_evo.c
// ---------------------------------------------------------------------------
static void route_cmd( const char *verb )
{
	char cmd[256];
	snprintf( cmd, sizeof( cmd ),
		"LD_LIBRARY_PATH=/opt/mm/lib:/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib "
		"/opt/mm/bin/audioconn -%s 0xb8 >/dev/null 2>&1", verb );
	system( cmd );
}

// ---------------------------------------------------------------------------
// QSA device open (the proven recipe from DOOM's i_sound_evo.c / qsa_tone_test)
// ---------------------------------------------------------------------------
static int qsa_open( void )
{
	int card = -1, dev = -1, rc;
	snd_pcm_channel_info_t info;
	snd_pcm_channel_params_t p;
	snd_pcm_channel_setup_t su;
	int rate = 44100, frag = 4096, frags = 4;

	rc = snd_pcm_open_preferred( &g_pcm, &card, &dev, SND_PCM_OPEN_PLAYBACK );
	if ( rc < 0 || !g_pcm ) {
		Com_Printf( "QNX SND: snd_pcm_open_preferred failed: %d (%s)\n", rc, snd_strerror( rc ) );
		g_pcm = NULL;
		return 0;
	}
	Com_Printf( "QNX SND: QSA device open (card %d dev %d)\n", card, dev );

	memset( &info, 0, sizeof( info ) );
	info.channel = SND_PCM_CHANNEL_PLAYBACK;
	if ( snd_pcm_plugin_info( g_pcm, &info ) == 0 ) {
		if ( info.max_rate > 0 && info.max_rate < rate ) rate = info.max_rate;
		if ( info.min_rate > 0 && rate < info.min_rate ) rate = info.min_rate;
	}

	memset( &p, 0, sizeof( p ) );
	p.channel = SND_PCM_CHANNEL_PLAYBACK;
	p.mode = SND_PCM_MODE_BLOCK;
	p.start_mode = SND_PCM_START_FULL;
	p.stop_mode = SND_PCM_STOP_STOP;
	p.buf.block.frag_size = frag;
	p.buf.block.frags_min = 1;
	p.buf.block.frags_max = frags;    // a real count: -1 is rejected (-22)
	p.format.interleave = 1;
	p.format.format = SND_PCM_SFMT_S16_LE;
	p.format.rate = rate;
	p.format.voices = 2;
	rc = snd_pcm_plugin_params( g_pcm, &p );
	if ( rc < 0 ) {
		Com_Printf( "QNX SND: plugin_params failed: %d (%s)\n", rc, snd_strerror( rc ) );
		snd_pcm_close( g_pcm ); g_pcm = NULL;
		return 0;
	}

	memset( &su, 0, sizeof( su ) );
	su.channel = SND_PCM_CHANNEL_PLAYBACK;
	if ( snd_pcm_plugin_setup( g_pcm, &su ) == 0 ) {
		if ( su.format.rate > 0 ) rate = su.format.rate;
		if ( su.buf.block.frag_size > 0 ) frag = su.buf.block.frag_size;
	}
	rc = snd_pcm_plugin_prepare( g_pcm, SND_PCM_CHANNEL_PLAYBACK );
	if ( rc < 0 ) {
		Com_Printf( "QNX SND: plugin_prepare failed: %d (%s)\n", rc, snd_strerror( rc ) );
		snd_pcm_close( g_pcm ); g_pcm = NULL;
		return 0;
	}
	g_out_rate = rate;
	g_frag_bytes = frag;
	Com_Printf( "QNX SND: QSA ready: %d Hz stereo S16_LE, fragment %d bytes\n", g_out_rate, g_frag_bytes );
	return 1;
}

// ---------------------------------------------------------------------------
// Consumer thread: copy one fragment out of the engine's DMA ring at the
// playback cursor (SDL callback wrap logic), advance the cursor, then do one
// BLOCKING write. The blocking write paces the cursor at the device rate.
// ---------------------------------------------------------------------------
static void *snd_thread( void *arg )
{
	const int ss = 2;                 // S16 -> 2 bytes per sample
	char *scratch = (char *)malloc( g_frag_bytes );
	(void)arg;
	if ( !scratch )
		return NULL;

	while ( g_run ) {
		int len = g_frag_bytes;
		int n;

		// --- copy a fragment out of the ring (guarded against the painter) ---
		pthread_mutex_lock( &g_lock );
		{
			int pos = dmapos * ss;            // byte offset into dma.buffer
			if ( pos >= dmasize )
				dmapos = pos = 0;
			{
				int tobufend = dmasize - pos; // bytes to ring end
				int len1 = len;
				int len2 = 0;
				if ( len1 > tobufend ) {
					len1 = tobufend;
					len2 = len - len1;
				}
				memcpy( scratch, dma.buffer + pos, len1 );
				if ( len2 <= 0 ) {
					dmapos += ( len1 / ss );
				} else {               // wraparound
					memcpy( scratch + len1, dma.buffer, len2 );
					dmapos = ( len2 / ss );
				}
			}
			if ( dmapos >= dmasize )
				dmapos = 0;
		}
		pthread_mutex_unlock( &g_lock );

		// --- post-mix master gain (HU attenuates the browser source) ---
		if ( g_gain_x256 != 256 ) {
			short *s = (short *)scratch;
			int i, ns = g_frag_bytes / ss;
			for ( i = 0; i < ns; i++ ) {
				int v = ( (int)s[i] * g_gain_x256 ) >> 8;
				if ( v > 32767 ) v = 32767; else if ( v < -32768 ) v = -32768;
				s[i] = (short)v;
			}
		}

		// --- one blocking write (paces this thread) ---
		n = snd_pcm_plugin_write( g_pcm, scratch, g_frag_bytes );
		if ( n != g_frag_bytes ) {
			snd_pcm_channel_status_t st;
			memset( &st, 0, sizeof( st ) );
			st.channel = SND_PCM_CHANNEL_PLAYBACK;
			if ( snd_pcm_plugin_status( g_pcm, &st ) == 0 &&
			     ( st.status == SND_PCM_STATUS_UNDERRUN || st.status == SND_PCM_STATUS_READY ) )
				snd_pcm_plugin_prepare( g_pcm, SND_PCM_CHANNEL_PLAYBACK );
		}
	}
	free( scratch );
	return NULL;
}

// ---------------------------------------------------------------------------
// SNDDMA_* interface (what snd_dma.c calls)
// ---------------------------------------------------------------------------
qboolean SNDDMA_Init( void )
{
	int tmp;

	if ( snd_inited )
		return qtrue;

	{
		const char *g = getenv( "EVO_Q3_AUDIO_GAIN" );   // percent, e.g. 200 = 2x
		if ( g && *g ) {
			int pct = atoi( g );
			if ( pct < 10 )   pct = 10;     // 0.1x floor
			if ( pct > 800 )  pct = 800;    // 8x cap (peaks hard-clip)
			g_gain_x256 = pct * 256 / 100;
		}
	}

	if ( !qsa_open() ) {
		Com_Printf( "QNX SNDDMA_Init: no QSA device - running silent\n" );
		return qfalse;
	}

	// Ring of ~16 device fragments, rounded to a power of two (SDL keeps the
	// mix buffer well above one fragment and copies a little each callback).
	tmp = ( g_frag_bytes / 2 ) * 16;        // mono-samples (S16 -> /2)
	tmp -= tmp % 2;                         // divisible by channel count (2)
	{ int p = 1; while ( p < tmp ) p <<= 1; tmp = p; }   // round up to power of 2

	dmapos = 0;
	dma.samplebits = 16;
	dma.isfloat = 0;
	dma.channels = 2;
	dma.samples = tmp;
	dma.fullsamples = dma.samples / dma.channels;
	dma.submission_chunk = 1;
	dma.speed = g_out_rate;
	dmasize = dma.samples * ( dma.samplebits / 8 );
	dma.buffer = calloc( 1, dmasize );
	if ( !dma.buffer ) {
		Com_Printf( "QNX SNDDMA_Init: out of memory for DMA buffer\n" );
		snd_pcm_close( g_pcm ); g_pcm = NULL;
		return qfalse;
	}

	route_cmd( "b" );          // audioconn -b 0xb8
	g_routed = 1;

	g_run = 1;
	if ( pthread_create( &g_tid, NULL, snd_thread, NULL ) != 0 ) {
		Com_Printf( "QNX SNDDMA_Init: could not start sound thread - running silent\n" );
		g_run = 0;
		if ( g_routed ) { route_cmd( "r" ); g_routed = 0; }
		free( dma.buffer ); dma.buffer = NULL;
		snd_pcm_close( g_pcm ); g_pcm = NULL;
		return qfalse;
	}

	Com_Printf( "QNX SND: online (%d mono-samples ring, master gain %d%%)\n",
		dma.samples, g_gain_x256 * 100 / 256 );
	snd_inited = qtrue;
	return qtrue;
}

int SNDDMA_GetDMAPos( void )
{
	return dmapos;
}

void SNDDMA_Shutdown( void )
{
	if ( !snd_inited )
		return;

	if ( g_run ) {
		g_run = 0;
		pthread_join( g_tid, NULL );
	}
	if ( g_routed ) { route_cmd( "r" ); g_routed = 0; }   // audioconn -r 0xb8
	if ( g_pcm ) {
		snd_pcm_plugin_flush( g_pcm, SND_PCM_CHANNEL_PLAYBACK );
		snd_pcm_close( g_pcm );
		g_pcm = NULL;
	}
	if ( dma.buffer ) {
		free( dma.buffer );
		dma.buffer = NULL;
	}
	dmapos = dmasize = 0;
	snd_inited = qfalse;
	Com_Printf( "QNX SND: shut down\n" );
}

// The engine paints into dma.buffer between these; the consumer thread reads it
// under the same lock (mirrors SDL_LockAudioDevice / SDL_UnlockAudioDevice).
void SNDDMA_BeginPainting( void )
{
	if ( snd_inited )
		pthread_mutex_lock( &g_lock );
}

void SNDDMA_Submit( void )
{
	if ( snd_inited )
		pthread_mutex_unlock( &g_lock );
}

void SNDDMA_Activate( void )
{
}
