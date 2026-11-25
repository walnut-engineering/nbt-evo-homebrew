/* retro_audio.c - the core's sound out of the car's speakers.
 *
 * Two clocks have to be kept together: the emulated console makes a fixed number of
 * samples per frame (SNES 32,040 Hz, Genesis 44,100 Hz), and the sound device eats
 * 44,100 a second by its own crystal. Neither is allowed to wait for the other - the
 * picture is paced by a timer so it stays even - so:
 *
 *   emulation thread   each batch of core samples is resampled (4-point Hermite) to
 *                      the device rate and put in a ring buffer.
 *   output thread      takes one fragment at a time from the ring and writes it to
 *                      QSA; the blocking write is its only clock. An empty ring gives
 *                      silence, so a paused or stalled emulator never leaves the
 *                      device starved.
 *   rate control       the resampling ratio is nudged by at most half a percent
 *                      (inaudible) so the ring hovers around its target: fuller ->
 *                      make slightly fewer samples, emptier -> slightly more. That
 *                      absorbs the difference between the two clocks for good.
 *
 * Device recipe as in the DOOM and NES tiles: snd_pcm_open_preferred() with the HARMAN
 * libasound first on LD_LIBRARY_PATH, S16_LE stereo, block mode, and `audioconn -b
 * 0xb8` to route the browser source to the speakers. That source is attenuated by the
 * unit, hence the gain (percent, <SYS>_AUDIO_GAIN, default 300) with a soft limiter so
 * loud passages round off instead of clipping hard.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <sys/asoundlib.h>

#include "retro_fe.h"

#define RING_FRAMES 16384u                  /* power of two; 370 ms at 44.1 kHz */
#define RING_MASK   (RING_FRAMES - 1u)
#define DRC_MAX     0.005                   /* the most the pitch is ever bent */

static snd_pcm_t *g_pcm = NULL;
static int g_rate = 0, g_frag = 2048;       /* device rate, fragment size in bytes */
static pthread_t g_thread;
static volatile int g_run = 0;
static volatile int g_paused = 1;           /* 1 = the emulator is not producing: silence is expected */

static short *g_ring = NULL;                /* interleaved stereo */
static unsigned g_head = 0, g_tail = 0;     /* frames; head = write, tail = read (free-running) */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_target = 1470;            /* frames the ring should hold on average */

static double g_in_rate = 44100.0;          /* what the core produces */
static double g_phase = 0.0;                /* position between g_hist[1] and g_hist[2] */
static int g_hist[4][2];                    /* the last four input frames */
static int g_gain = 300;
static short g_lim[32768];                  /* soft limiter: |sample * gain| >> 2 -> output */

/* statistics (read by the emulation thread once a second) */
static unsigned g_underruns = 0, g_rearmed = 0, g_dropped = 0;
static unsigned long long g_fill_sum = 0;
static unsigned g_fill_n = 0, g_peak = 0, g_peak_max = 0;
static unsigned long g_loud = 0, g_made = 0;   /* output samples past the limiter's knee / all of them */
static double g_adjust = 1.0;

/* audioconn needs the mm libs on its path; set them inline so we don't depend on ours */
static void audioconn(const char *verb)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd,
             "LD_LIBRARY_PATH=/opt/mm/lib:/opt/sys/lib:/lib:/usr/lib /opt/mm/bin/audioconn -%s 0xb8 >/dev/null 2>&1", verb);
    system(cmd);
}

static unsigned ring_fill(void) { return g_head - g_tail; }

/* gain, then a soft knee: straight up to 70 % of full scale, the rest squeezed towards
 * (never past) full scale. Indexed by the boosted magnitude / 4, up to 4x full scale. */
static void build_limiter(void)
{
    int i;
    for (i = 0; i < 32768; i++) {
        double x = (double)i * 4.0 / 32767.0, y;
        const double knee = 0.7;
        y = x <= knee ? x : knee + (1.0 - knee) * tanh((x - knee) / (1.0 - knee));
        g_lim[i] = (short)(y * 32767.0 + 0.5);
    }
}

static inline short shape(int s)            /* one resampled sample -> what the device gets */
{
    int v = s * g_gain / 100, a = v < 0 ? -v : v;
    if ((unsigned)a > g_peak) g_peak = (unsigned)a;
    if (a > 22937) g_loud++;                /* 70 % of full scale: where the limiter starts to act */
    g_made++;
    a >>= 2;
    if (a > 32767) a = 32767;
    return (short)(v < 0 ? -g_lim[a] : g_lim[a]);
}

/* ============================================================ output thread */
static void *out_thread(void *arg)
{
    short *frag = (short *)malloc((size_t)g_frag);
    unsigned need = (unsigned)g_frag / 4u;      /* frames per fragment */
    (void)arg;

    if (!frag) return NULL;
    while (g_run) {
        unsigned have, i, waited = 0;
        int w;

        /* give the emulator up to one fragment's time to deliver before falling back on silence */
        while (g_run && ring_fill() < need && !g_paused && waited < need * 1000u / (unsigned)g_rate) { usleep(1000); waited++; }

        pthread_mutex_lock(&g_lock);
        have = ring_fill();
        if (have > need) have = need;
        for (i = 0; i < have; i++) {
            unsigned k = (g_tail + i) & RING_MASK;
            frag[2 * i] = g_ring[2 * k]; frag[2 * i + 1] = g_ring[2 * k + 1];
        }
        g_tail += have;
        pthread_mutex_unlock(&g_lock);
        if (have < need) {
            memset(frag + 2 * have, 0, (size_t)(need - have) * 4);
            if (!g_paused) g_underruns++;
        }

        w = snd_pcm_plugin_write(g_pcm, frag, (size_t)g_frag);      /* blocks: the device's clock */
        if (w != g_frag) { g_rearmed++; snd_pcm_plugin_prepare(g_pcm, SND_PCM_CHANNEL_PLAYBACK); }
    }
    free(frag);
    return NULL;
}

/* ===================================================== emulation-thread side */
/* The level comes from the launch script (which reads the gain file into the
 * environment), or straight from the gain file when started by hand. */
static void load_gain(void)
{
    static int done = 0;
    const char *g = getenv(SYS_ENV "_AUDIO_GAIN");
    char txt[16] = "";
    FILE *f;
    int v = 0;

    if (done) return;
    done = 1;
    if (g) v = atoi(g);
    else if ((f = fopen(SYS_GAIN, "r")) != NULL) { if (fgets(txt, sizeof txt, f)) v = atoi(txt); fclose(f); }
    if (v >= 10 && v <= 800) g_gain = v;
}

int fe_audio_gain(void) { load_gain(); return g_gain; }

void fe_audio_set_gain(int percent)
{
    load_gain();
    if (percent >= 10 && percent <= 800) g_gain = percent;
}

int fe_audio_init(void)
{
    int card = -1, dev = -1, rc, rate = 44100, frag = 2048;
    snd_pcm_channel_info_t info;
    snd_pcm_channel_params_t p;
    snd_pcm_channel_setup_t su;

    load_gain();
    build_limiter();

    rc = snd_pcm_open_preferred(&g_pcm, &card, &dev, SND_PCM_OPEN_PLAYBACK);
    if (rc < 0 || !g_pcm) {
        printf(SYS_TAG " audio: snd_pcm_open_preferred failed %d (%s) - running silent\n", rc, snd_strerror(rc));
        g_pcm = NULL; return 0;
    }
    memset(&info, 0, sizeof info);
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    if (snd_pcm_plugin_info(g_pcm, &info) == 0) {
        if (info.max_rate > 0 && info.max_rate < rate) rate = info.max_rate;
        if (info.min_rate > 0 && rate < info.min_rate) rate = info.min_rate;
        if (info.min_fragment_size > 0 && frag < info.min_fragment_size) frag = info.min_fragment_size;
    }
    memset(&p, 0, sizeof p);
    p.channel = SND_PCM_CHANNEL_PLAYBACK;
    p.mode = SND_PCM_MODE_BLOCK;
    p.start_mode = SND_PCM_START_FULL;
    p.stop_mode = SND_PCM_STOP_STOP;
    p.buf.block.frag_size = frag;
    p.buf.block.frags_min = 1;
    p.buf.block.frags_max = 3;              /* a real count: -1 is rejected */
    p.format.interleave = 1;
    p.format.format = SND_PCM_SFMT_S16_LE;
    p.format.rate = rate;
    p.format.voices = 2;
    if ((rc = snd_pcm_plugin_params(g_pcm, &p)) < 0) {
        printf(SYS_TAG " audio: plugin_params failed %d (%s) - running silent\n", rc, snd_strerror(rc));
        snd_pcm_close(g_pcm); g_pcm = NULL; return 0;
    }
    memset(&su, 0, sizeof su);
    su.channel = SND_PCM_CHANNEL_PLAYBACK;
    if (snd_pcm_plugin_setup(g_pcm, &su) == 0) {
        if (su.format.rate > 0) rate = su.format.rate;
        if (su.buf.block.frag_size > 0) frag = su.buf.block.frag_size;
    }
    if ((rc = snd_pcm_plugin_prepare(g_pcm, SND_PCM_CHANNEL_PLAYBACK)) < 0) {
        printf(SYS_TAG " audio: plugin_prepare failed %d (%s) - running silent\n", rc, snd_strerror(rc));
        snd_pcm_close(g_pcm); g_pcm = NULL; return 0;
    }
    g_rate = rate; g_frag = frag & ~3;
    g_ring = (short *)calloc(RING_FRAMES, 2 * sizeof(short));
    if (!g_ring || g_frag < 64) { snd_pcm_close(g_pcm); g_pcm = NULL; return 0; }

    audioconn("b");                                  /* browser source -> speakers */
    g_run = 1; g_paused = 1;
    if (pthread_create(&g_thread, NULL, out_thread, NULL) != 0) {
        g_run = 0; snd_pcm_close(g_pcm); g_pcm = NULL; audioconn("r"); return 0;
    }
    printf(SYS_TAG " audio: QSA %d Hz stereo, fragment %d bytes (%.1f ms), gain %d%%\n",
           g_rate, g_frag, (double)(g_frag / 4) * 1000.0 / g_rate, g_gain);
    return 1;
}

void fe_audio_shutdown(void)
{
    if (!g_pcm) return;
    g_run = 0;
    pthread_join(g_thread, NULL);
    if (g_peak > g_peak_max) g_peak_max = g_peak;
    printf(SYS_TAG " audio: closing - %u gap%s, %u device restart%s, %u frames dropped; loudest %u%% of full scale, %.2f%% of samples limited\n",
           g_underruns, g_underruns == 1 ? "" : "s", g_rearmed, g_rearmed == 1 ? "" : "s", g_dropped,
           g_peak_max * 100u / 32767u, g_made ? 100.0 * (double)g_loud / (double)g_made : 0.0);
    snd_pcm_plugin_flush(g_pcm, SND_PCM_CHANNEL_PLAYBACK);
    snd_pcm_close(g_pcm); g_pcm = NULL;
    audioconn("r");
    free(g_ring); g_ring = NULL;
}

int fe_audio_active(void) { return g_pcm != NULL; }

/* The core's sample rate and frame rate (again whenever the core changes them). The
 * ring is aimed at one frame of sound plus one fragment, measured just before a
 * frame's worth is added (so about half a frame more on average): enough that an
 * ordinary late frame is not heard, short enough that sound stays with the picture. */
void fe_audio_set_rate(double core_rate, double fps)
{
    if (!g_pcm) return;
    if (!(core_rate > 8000.0 && core_rate < 200000.0)) core_rate = 44100.0;
    if (!(fps > 20.0 && fps < 130.0)) fps = 60.0;
    g_in_rate = core_rate;
    g_target = (unsigned)((double)g_rate / fps) + (unsigned)g_frag / 4u;
    if (g_target > RING_FRAMES / 3) g_target = RING_FRAMES / 3;
}

/* Start (or restart, after a pause) from a clean state: no stale sound, the ring
 * pre-filled with silence to its target so the first frames do not underrun. */
void fe_audio_resume(void)
{
    if (!g_pcm) return;
    pthread_mutex_lock(&g_lock);
    g_tail = g_head;
    memset(g_ring, 0, RING_FRAMES * 2 * sizeof(short));
    g_head += g_target;
    pthread_mutex_unlock(&g_lock);
    memset(g_hist, 0, sizeof g_hist);
    g_phase = 0.0;
    g_paused = 0;
}

void fe_audio_pause(void) { g_paused = 1; }

static inline int hermite(int xm1, int x0, int x1, int x2, double t)
{
    double c1 = 0.5 * (x1 - xm1);
    double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    double v = ((c3 * t + c2) * t + c1) * t + x0;
    return (int)(v < 0 ? v - 0.5 : v + 0.5);
}

/* `frames` stereo frames from the core -> the ring, at the device rate */
void fe_audio_push(const int16_t *in, size_t frames)
{
    static short out[4096 * 2];
    unsigned fill, n = 0, space, i;
    double level, step;
    size_t k = 0;

    if (!g_pcm || g_paused || !frames) return;

    fill = ring_fill();
    g_fill_sum += fill; g_fill_n++;
    level = (double)fill / (2.0 * (double)g_target);        /* 0.5 = on target */
    if (level > 1.0) level = 1.0;
    g_adjust = 1.0 + DRC_MAX * (1.0 - 2.0 * level);         /* > 1: make more samples */
    step = g_in_rate / ((double)g_rate * g_adjust);         /* input frames per output frame */

    while (k < frames) {
        /* the window needs one new input frame whenever the phase passes 1 */
        while (g_phase >= 1.0 && k < frames) {
            memmove(g_hist[0], g_hist[1], 3 * sizeof g_hist[0]);
            g_hist[3][0] = in[2 * k]; g_hist[3][1] = in[2 * k + 1];
            k++; g_phase -= 1.0;
        }
        if (g_phase >= 1.0) break;                          /* input used up */
        out[2 * n]     = shape(hermite(g_hist[0][0], g_hist[1][0], g_hist[2][0], g_hist[3][0], g_phase));
        out[2 * n + 1] = shape(hermite(g_hist[0][1], g_hist[1][1], g_hist[2][1], g_hist[3][1], g_phase));
        n++; g_phase += step;
        if (n == 4096) {                                    /* flush a full block and carry on */
            pthread_mutex_lock(&g_lock);
            space = RING_FRAMES - ring_fill();
            if (n > space) { g_dropped += n - space; n = space; }
            for (i = 0; i < n; i++) { unsigned q = (g_head + i) & RING_MASK; g_ring[2 * q] = out[2 * i]; g_ring[2 * q + 1] = out[2 * i + 1]; }
            g_head += n;
            pthread_mutex_unlock(&g_lock);
            n = 0;
        }
    }
    if (n) {
        pthread_mutex_lock(&g_lock);
        space = RING_FRAMES - ring_fill();
        if (n > space) { g_dropped += n - space; n = space; }
        for (i = 0; i < n; i++) { unsigned q = (g_head + i) & RING_MASK; g_ring[2 * q] = out[2 * i]; g_ring[2 * q + 1] = out[2 * i + 1]; }
        g_head += n;
        pthread_mutex_unlock(&g_lock);
    }
}

/* one line for the statistics display; resets the per-period figures */
void fe_audio_stats(char *buf, size_t n)
{
    double fill_ms = g_fill_n ? (double)g_fill_sum / g_fill_n * 1000.0 / (g_rate ? g_rate : 1) : 0.0;
    if (!g_pcm) { snprintf(buf, n, "no sound device"); return; }
    snprintf(buf, n, "sound: buffer %.0f ms (aim %.0f)  pitch %+.2f%%  peak %u%%  gaps %u  restarts %u",
             fill_ms, (double)g_target * 1000.0 / g_rate, (g_adjust - 1.0) * 100.0, g_peak * 100u / 32767u, g_underruns, g_rearmed);
    if (g_peak > g_peak_max) g_peak_max = g_peak;
    g_fill_sum = 0; g_fill_n = 0; g_peak = 0;
}
