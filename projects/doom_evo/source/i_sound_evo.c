//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2025 EVO Audio Integration
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	EVO Audio System - real SFX mixer over the head unit's QNX Sound
//	Architecture (QSA) library, routed to the speakers through the
//	"browser" audio source (connection 0xb8).
//
// How it works (proven on hu-omap 2026-11-03):
//   * snd_pcm_open_preferred() opens the default libasound playback device
//     (/dev/libasound/dev1-1 -> A2I input_stereo_3), S16_LE stereo, the
//     device's own rate (22050..44100 -> 44100 here), BLOCK mode, fragments
//     of 4096 bytes (a power of two: A2I's check_parameter rejects others).
//   * a dedicated mixer thread mixes the active channels (vanilla DOOM
//     volume-lookup mixing of the 8-bit sfx lumps) into that device, one
//     fragment per blocking write (the write itself paces the thread).
//   * audioconn -b 0xb8 builds the browser source -> eSNK_SPEAKERS while the
//     game runs; -r 0xb8 on shutdown. The source is "interrupting
//     entertainment": it takes the speakers from radio/BT while we play.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/asoundlib.h>

#include "z_zone.h"
#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "doomdef.h"
#include "sounds.h"
#include "i_oplmusic_evo.h"

// S_sfx[] lives in sounds.c
extern sfxinfo_t S_sfx[];

#define NUM_MIX_CHANNELS 16
#define SFX_RATE_DEFAULT 11025

// One mixing voice.
typedef struct {
    volatile int   active;
    const byte*    data;     // 8-bit unsigned PCM (points into the cached lump)
    unsigned int   len;      // number of samples
    unsigned int   pos;      // 16.16 fixed-point read cursor
    unsigned int   step;     // 16.16 fixed-point advance per output frame
    int            leftvol;  // 0..127
    int            rightvol; // 0..127
    int            handle;   // unique id handed back to the game
} mixchan_t;

static mixchan_t     g_chan[NUM_MIX_CHANNELS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int           g_handle_seq = 1;

// pre-scaled mix table: vol_lookup[v][s] = ((v*(s-128))*256)/127, clamped s16
static short         g_vol_lookup[128][256];

// master output gain (256 = 1.0x). DOOM's mix rarely nears full scale and the
// browser audio source is attenuated by the HU, so a boost keeps us audible
// without cranking the HU volume. Tunable at launch via DOOM_AUDIO_GAIN (percent,
// e.g. 200 = 2x); clamped to 8x. Peaks hard-clip, which is fine for DOOM.
static int           g_gain_x256 = 768;   // default 3x (verified good on the bench)

// QSA device
static snd_pcm_t*    g_pcm = NULL;
static int           g_out_rate = 44100;
static int           g_frag_bytes = 4096;
static int           g_audio_ok = 0;

// mixer thread
static pthread_t     g_mix_thread;
static volatile int  g_mix_run = 0;
static int           g_routed = 0;

// music: the OPL backend renders into this buffer each fragment and we mix it
// into the output alongside the SFX. Gain is separate from the SFX master gain
// (OPL output is already full-range). Tunable via DOOM_MUSIC_GAIN (percent).
static short*        g_music_buf = NULL;
// Default music gain 2x. Measured across all 13 shareware songs: the loudest
// (D_INTRO/D_E1M9/D_INTER) peak ~13.6k at unity, so 2x (~27k) stays below
// full-scale with no clipping, while sitting under the 3x SFX master gain
// (g_gain_x256=768) so gunfire still punches through - the balance we want.
// Override at launch with DOOM_MUSIC_GAIN (percent); final tuning is by ear.
static int           g_music_gain_x256 = 512;   // 2.0x
static int           g_music_ok = 0;

// ---------------------------------------------------------------------------
// Route (browser audio source, connection 0xb8)
// ---------------------------------------------------------------------------
static void route_cmd(const char* verb)
{
    char cmd[256];
    // audioconn needs the mm libs on its path; set them inline so we don't
    // depend on the launcher's environment for the child.
    snprintf(cmd, sizeof(cmd),
             "LD_LIBRARY_PATH=/opt/mm/lib:/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib "
             "/opt/mm/bin/audioconn -%s 0xb8 >/dev/null 2>&1", verb);
    system(cmd);
}

// ---------------------------------------------------------------------------
// SFX lump cache (vanilla getsfx: strip the 8-byte header, keep rate+length)
// ---------------------------------------------------------------------------
typedef struct { const byte* data; unsigned int len; int rate; } sfxcache_t;
static sfxcache_t g_sfxcache[NUMSFX];

static int getsfx(int id, sfxcache_t* out)
{
    int lump, size;
    const byte* lumpdata;

    if (id < 0 || id >= NUMSFX)
        return 0;
    if (g_sfxcache[id].data) { *out = g_sfxcache[id]; return 1; }

    lump = I_GetSfxLumpNum(&S_sfx[id]);
    if (lump < 0)
        return 0;
    size = W_LumpLength(lump);
    if (size <= 8)
        return 0;
    lumpdata = (const byte*)W_CacheLumpNum(lump, PU_STATIC); // kept for the session
    if (!lumpdata)
        return 0;

    // header: u16 format, u16 rate, u32 nsamples, then 8-bit unsigned PCM
    {
        int rate = lumpdata[2] | (lumpdata[3] << 8);
        unsigned int nsamp = (unsigned)lumpdata[4] | ((unsigned)lumpdata[5] << 8) |
                             ((unsigned)lumpdata[6] << 16) | ((unsigned)lumpdata[7] << 24);
        unsigned int avail = (unsigned)(size - 8);
        if (rate <= 0) rate = SFX_RATE_DEFAULT;
        if (nsamp == 0 || nsamp > avail) nsamp = avail;
        g_sfxcache[id].data = lumpdata + 8;
        g_sfxcache[id].len  = nsamp;
        g_sfxcache[id].rate = rate;
    }
    *out = g_sfxcache[id];
    return 1;
}

// ---------------------------------------------------------------------------
// QSA device (the proven recipe from qsa_tone_test)
// ---------------------------------------------------------------------------
static int qsa_open(void)
{
    int card = -1, dev = -1, rc;
    snd_pcm_channel_info_t info;
    snd_pcm_channel_params_t p;
    snd_pcm_channel_setup_t su;
    int rate = 44100, frag = 4096, frags = 4;

    rc = snd_pcm_open_preferred(&g_pcm, &card, &dev, SND_PCM_OPEN_PLAYBACK);
    if (rc < 0 || !g_pcm) {
        printf("AUDIO: snd_pcm_open_preferred failed: %d (%s)\n", rc, snd_strerror(rc));
        g_pcm = NULL;
        return 0;
    }
    printf("AUDIO: QSA device open (card %d dev %d)\n", card, dev);

    memset(&info, 0, sizeof(info));
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    if (snd_pcm_plugin_info(g_pcm, &info) == 0) {
        if (info.max_rate > 0 && info.max_rate < rate) rate = info.max_rate;
        if (info.min_rate > 0 && rate < info.min_rate) rate = info.min_rate;
        printf("AUDIO: device rates %d..%d, fragment %d..%d\n",
               info.min_rate, info.max_rate, info.min_fragment_size, info.max_fragment_size);
    }

    memset(&p, 0, sizeof(p));
    p.channel = SND_PCM_CHANNEL_PLAYBACK;
    p.mode = SND_PCM_MODE_BLOCK;
    p.start_mode = SND_PCM_START_FULL;
    p.stop_mode = SND_PCM_STOP_STOP;
    p.buf.block.frag_size = frag;
    p.buf.block.frags_min = 1;
    p.buf.block.frags_max = frags;   // a real count: -1 is rejected (-22)
    p.format.interleave = 1;
    p.format.format = SND_PCM_SFMT_S16_LE;
    p.format.rate = rate;
    p.format.voices = 2;
    rc = snd_pcm_plugin_params(g_pcm, &p);
    if (rc < 0) {
        printf("AUDIO: plugin_params failed: %d (%s)\n", rc, snd_strerror(rc));
        snd_pcm_close(g_pcm); g_pcm = NULL;
        return 0;
    }

    memset(&su, 0, sizeof(su));
    su.channel = SND_PCM_CHANNEL_PLAYBACK;
    if (snd_pcm_plugin_setup(g_pcm, &su) == 0) {
        if (su.format.rate > 0) rate = su.format.rate;
        if (su.buf.block.frag_size > 0) frag = su.buf.block.frag_size;
    }
    rc = snd_pcm_plugin_prepare(g_pcm, SND_PCM_CHANNEL_PLAYBACK);
    if (rc < 0) {
        printf("AUDIO: plugin_prepare failed: %d (%s)\n", rc, snd_strerror(rc));
        snd_pcm_close(g_pcm); g_pcm = NULL;
        return 0;
    }
    g_out_rate = rate;
    g_frag_bytes = frag;
    printf("AUDIO: QSA ready: %d Hz stereo S16_LE, fragment %d bytes\n", g_out_rate, g_frag_bytes);
    return 1;
}

// ---------------------------------------------------------------------------
// Mixer thread: one fragment per blocking write.
// ---------------------------------------------------------------------------
static void* mix_thread(void* arg)
{
    int frames = g_frag_bytes / 4;          // stereo S16 = 4 bytes/frame
    short* out = (short*)malloc(g_frag_bytes);
    (void)arg;
    if (!out) return NULL;

    while (g_mix_run) {
        int f, c;
        // render this fragment's music first (its own lock, not the sfx lock)
        if (g_music_ok && g_music_buf)
            EvoMusic_Render(g_music_buf, frames);
        pthread_mutex_lock(&g_lock);
        for (f = 0; f < frames; f++) {
            int dl = 0, dr = 0;
            for (c = 0; c < NUM_MIX_CHANNELS; c++) {
                mixchan_t* ch = &g_chan[c];
                unsigned int idx;
                if (!ch->active) continue;
                idx = ch->pos >> 16;
                if (idx >= ch->len) { ch->active = 0; continue; }
                {
                    byte s = ch->data[idx];
                    dl += g_vol_lookup[ch->leftvol][s];
                    dr += g_vol_lookup[ch->rightvol][s];
                }
                ch->pos += ch->step;
            }
            dl = (int)(((long long)dl * g_gain_x256) >> 8);   /* int64: safe at high gain */
            dr = (int)(((long long)dr * g_gain_x256) >> 8);
            if (g_music_ok && g_music_buf) {
                dl += ((int)g_music_buf[f * 2]     * g_music_gain_x256) >> 8;
                dr += ((int)g_music_buf[f * 2 + 1] * g_music_gain_x256) >> 8;
            }
            if (dl > 32767) dl = 32767; else if (dl < -32768) dl = -32768;
            if (dr > 32767) dr = 32767; else if (dr < -32768) dr = -32768;
            out[f * 2]     = (short)dl;
            out[f * 2 + 1] = (short)dr;
        }
        pthread_mutex_unlock(&g_lock);

        {
            int n = snd_pcm_plugin_write(g_pcm, out, g_frag_bytes);
            if (n != g_frag_bytes) {
                snd_pcm_channel_status_t st;
                memset(&st, 0, sizeof(st));
                st.channel = SND_PCM_CHANNEL_PLAYBACK;
                if (snd_pcm_plugin_status(g_pcm, &st) == 0 &&
                    (st.status == SND_PCM_STATUS_UNDERRUN || st.status == SND_PCM_STATUS_READY))
                    snd_pcm_plugin_prepare(g_pcm, SND_PCM_CHANNEL_PLAYBACK);
            }
        }
    }
    free(out);
    return NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void I_InitSound(void)
{
    int v, s;

    printf("AUDIO: initializing EVO QSA sound (browser source 0xb8)...\n");
    memset(g_chan, 0, sizeof(g_chan));
    memset(g_sfxcache, 0, sizeof(g_sfxcache));

    {
        const char* g = getenv("DOOM_AUDIO_GAIN");   // percent, e.g. 200 = 2x
        if (g && *g) {
            int pct = atoi(g);
            if (pct < 10)   pct = 10;     // 0.1x floor
            if (pct > 3200) pct = 3200;   // 32x cap (mix multiply is now int64-safe; peaks hard-clip)
            g_gain_x256 = pct * 256 / 100;
        }
    }
    printf("AUDIO: master gain %d%%\n", g_gain_x256 * 100 / 256);

    // build the pre-scaled mix table
    for (v = 0; v < 128; v++)
        for (s = 0; s < 256; s++)
            g_vol_lookup[v][s] = (short)((v * (s - 128) * 256) / 127);

    if (!qsa_open()) {
        printf("AUDIO: no QSA device - running silent\n");
        g_audio_ok = 0;
        return;
    }

    // music fragment buffer + gain (OPL backend inited below, after audio is up)
    g_music_buf = (short*)malloc(g_frag_bytes);
    {
        const char* mg = getenv("DOOM_MUSIC_GAIN");   // percent, e.g. 150 = 1.5x
        if (mg && *mg) {
            int p = atoi(mg);
            if (p < 0) p = 0; if (p > 800) p = 800;
            g_music_gain_x256 = p * 256 / 100;
        }
    }

    route_cmd("b");           // audioconn -b 0xb8
    g_routed = 1;

    g_mix_run = 1;
    if (pthread_create(&g_mix_thread, NULL, mix_thread, NULL) != 0) {
        printf("AUDIO: could not start mixer thread - running silent\n");
        g_mix_run = 0;
        if (g_routed) { route_cmd("r"); g_routed = 0; }
        snd_pcm_close(g_pcm); g_pcm = NULL;
        g_audio_ok = 0;
        return;
    }
    g_audio_ok = 1;
    printf("AUDIO: EVO QSA sound online\n");

    // Bring up the OPL (FM) music backend using the GENMIDI lump from the wad.
    {
        int gl = W_CheckNumForName("GENMIDI");
        if (gl >= 0 && g_music_buf != NULL) {
            void* genmidi = W_CacheLumpNum(gl, PU_STATIC);
            EvoMusic_Init(g_out_rate, genmidi);
            g_music_ok = 1;
        } else {
            printf("MUSIC: GENMIDI lump not found - music disabled\n");
        }
    }
}

void I_ShutdownSound(void)
{
    if (g_music_ok) { EvoMusic_Shutdown(); g_music_ok = 0; }
    if (g_mix_run) {
        g_mix_run = 0;
        pthread_join(g_mix_thread, NULL);
    }
    if (g_music_buf) { free(g_music_buf); g_music_buf = NULL; }
    if (g_routed) { route_cmd("r"); g_routed = 0; }   // audioconn -r 0xb8
    if (g_pcm) {
        snd_pcm_plugin_flush(g_pcm, SND_PCM_CHANNEL_PLAYBACK);
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
    }
    g_audio_ok = 0;
    printf("AUDIO: sound system shutdown\n");
}

int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

void I_SetChannels(void)
{
}

void I_SetSfxVolume(int volume)
{
    (void)volume;   // per-channel volume is applied at mix time
}

// vol 0..127, sep 0..254 (128 = centre) -> left/right 0..127
static void calc_pan(int vol, int sep, int* lv, int* rv)
{
    int l, r;
    if (vol < 0) vol = 0; if (vol > 127) vol = 127;
    if (sep < 0) sep = 0; if (sep > 254) sep = 254;
    l = (vol * (254 - sep)) / 254;
    r = (vol * sep) / 254;
    if (l < 0) l = 0; if (l > 127) l = 127;
    if (r < 0) r = 0; if (r > 127) r = 127;
    *lv = l; *rv = r;
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    sfxcache_t sc;
    int c, slot = -1, handle;
    (void)priority;

    if (!g_audio_ok)
        return -1;
    if (!getsfx(id, &sc))
        return -1;

    pthread_mutex_lock(&g_lock);
    // free slot, else steal slot 0
    for (c = 0; c < NUM_MIX_CHANNELS; c++)
        if (!g_chan[c].active) { slot = c; break; }
    if (slot < 0) slot = 0;

    handle = g_handle_seq++;
    g_chan[slot].data = sc.data;
    g_chan[slot].len  = sc.len;
    g_chan[slot].pos  = 0;
    {
        // step = sfx_rate / out_rate, scaled by pitch (128 = normal)
        unsigned int base = (unsigned int)(((unsigned long long)sc.rate << 16) / (unsigned)g_out_rate);
        if (pitch <= 0) pitch = 128;
        g_chan[slot].step = (unsigned int)(((unsigned long long)base * (unsigned)pitch) / 128u);
        if (g_chan[slot].step == 0) g_chan[slot].step = 1;
    }
    calc_pan(vol, sep, &g_chan[slot].leftvol, &g_chan[slot].rightvol);
    g_chan[slot].handle = handle;
    g_chan[slot].active = 1;
    pthread_mutex_unlock(&g_lock);

    return handle;
}

void I_StopSound(int handle)
{
    int c;
    if (!g_audio_ok) return;
    pthread_mutex_lock(&g_lock);
    for (c = 0; c < NUM_MIX_CHANNELS; c++)
        if (g_chan[c].active && g_chan[c].handle == handle) { g_chan[c].active = 0; break; }
    pthread_mutex_unlock(&g_lock);
}

int I_SoundIsPlaying(int handle)
{
    int c, playing = 0;
    if (!g_audio_ok) return 0;
    pthread_mutex_lock(&g_lock);
    for (c = 0; c < NUM_MIX_CHANNELS; c++)
        if (g_chan[c].active && g_chan[c].handle == handle) { playing = 1; break; }
    pthread_mutex_unlock(&g_lock);
    return playing;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    int c;
    (void)pitch;
    if (!g_audio_ok) return;
    pthread_mutex_lock(&g_lock);
    for (c = 0; c < NUM_MIX_CHANNELS; c++)
        if (g_chan[c].active && g_chan[c].handle == handle) {
            calc_pan(vol, sep, &g_chan[c].leftvol, &g_chan[c].rightvol);
            break;
        }
    pthread_mutex_unlock(&g_lock);
}

// The mixer thread owns the device; these are no-ops in this design.
void I_SubmitSound(void) {}
void I_UpdateSound(void) {}

//
// MUSIC API - OPL (FM) playback via the EVO OPL backend (i_oplmusic_evo.c).
// The backend is brought up in I_InitSound (needs the QSA rate + GENMIDI lump);
// these just forward the game's requests to it.
//
void I_InitMusic(void) { /* backend inited in I_InitSound */ }
void I_ShutdownMusic(void) { if (g_music_ok) { EvoMusic_Shutdown(); g_music_ok = 0; } }
void I_SetMusicVolume(int volume) {
    // DOOM passes the music volume as 0..15 here (the menu's "*8" scale-up is
    // commented out), but the OPL backend wants 0..127 like the original DMX.
    // Without this, every note is set ~8x too quiet. Scale the small range up.
    if (volume <= 15) volume = volume * 127 / 15;
    if (g_music_ok) EvoMusic_SetVolume(volume);
}
void I_PauseSong(int handle) { (void)handle; if (g_music_ok) EvoMusic_Pause(1); }
void I_ResumeSong(int handle) { (void)handle; if (g_music_ok) EvoMusic_Pause(0); }
void I_StopSong(int handle) { (void)handle; if (g_music_ok) EvoMusic_Stop(); }
void I_UnRegisterSong(int handle) { (void)handle; if (g_music_ok) EvoMusic_Stop(); }
int  I_RegisterSong(void* data) { return (g_music_ok && EvoMusic_Register(data)) ? 1 : 0; }
void I_PlaySong(int handle, int looping) { (void)handle; if (g_music_ok) EvoMusic_Play(looping); }
