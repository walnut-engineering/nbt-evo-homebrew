/* retro_bench.c - headless libretro frontend: load one ROM, run N frames, time them.
 *
 * The smallest thing that can host a libretro core, used to answer two questions on
 * the head unit itself before any real frontend exists: does the core run here, and
 * how many milliseconds does a frame cost on this CPU?
 *
 *   retro_bench <rom> [frames=600] [dump.raw] [press-start-every=0]
 *
 * It links statically against one core. Video frames are counted and one can be
 * dumped (RGB565, for looking at on the host); audio is counted and dropped; input
 * is "nothing held", optionally tapping START every N frames to get past menus.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "libretro.h"

static enum retro_pixel_format g_fmt = RETRO_PIXEL_FORMAT_0RGB1555;
static unsigned g_w, g_h, g_frames, g_start_every;
static size_t g_pitch;
static unsigned long g_audio_frames, g_sum;
static const char *g_dump;
static unsigned g_dump_at;
static int g_verbose_env;

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
    va_list ap;
    if (level < RETRO_LOG_WARN) return;
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
}

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: g_fmt = *(enum retro_pixel_format *)data; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "/dev/shmem"; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: ((struct retro_variable *)data)->value = NULL; return false;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int *)data = 3; return true;
    default:
        if (g_verbose_env) printf("env %u (0x%x) -> unsupported\n", cmd & 0xffff, cmd);
        return false;
    }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data) return;
    g_w = w; g_h = h; g_pitch = pitch;
    {   /* touch the frame so a core cannot get away with not rendering it */
        const unsigned char *p = data;
        unsigned y;
        for (y = 0; y < h; y += 8) g_sum += p[y * pitch] + p[y * pitch + (w / 2) * 2];
    }
    if (g_dump && g_frames == g_dump_at) {
        FILE *f = fopen(g_dump, "wb");
        if (f) {
            unsigned y, hdr[4];
            hdr[0] = w; hdr[1] = h; hdr[2] = (unsigned)g_fmt; hdr[3] = 0;
            fwrite(hdr, 4, 4, f);
            for (y = 0; y < h; y++) fwrite((const char *)data + y * pitch, 1, w * (g_fmt == RETRO_PIXEL_FORMAT_XRGB8888 ? 4 : 2), f);
            fclose(f);
        }
    }
}

static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; g_audio_frames++; }
static size_t audio_batch_cb(const int16_t *d, size_t n) { (void)d; g_audio_frames += n; return n; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port == 0 && device == RETRO_DEVICE_JOYPAD && id == RETRO_DEVICE_ID_JOYPAD_START && g_start_every)
        return (g_frames % g_start_every) < 4;
    return 0;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    struct retro_system_info si;
    struct retro_system_av_info av;
    struct retro_game_info gi;
    unsigned n = 600, i;
    double t0, t1, worst = 0, tf;
    void *rom = NULL;
    long size = 0;
    FILE *f;

    if (argc < 2) { printf("usage: %s <rom> [frames] [dump.raw] [press-start-every]\n", argv[0]); return 2; }
    if (argc > 2) n = (unsigned)atoi(argv[2]);
    if (argc > 3 && argv[3][0] != '-') g_dump = argv[3];
    if (argc > 4) g_start_every = (unsigned)atoi(argv[4]);
    g_dump_at = n - 1;
    g_verbose_env = getenv("BENCH_ENV") != NULL;

    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    memset(&si, 0, sizeof si);
    retro_get_system_info(&si);
    printf("core: %s %s (need_fullpath %d)\n", si.library_name, si.library_version, (int)si.need_fullpath);

    memset(&gi, 0, sizeof gi);
    gi.path = argv[1];
    if (!si.need_fullpath) {
        f = fopen(argv[1], "rb");
        if (!f) { perror(argv[1]); return 1; }
        fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
        rom = malloc((size_t)size);
        if (!rom || fread(rom, 1, (size_t)size, f) != (size_t)size) { printf("cannot read rom\n"); return 1; }
        fclose(f);
        gi.data = rom; gi.size = (size_t)size;
    }
    t0 = now_ms();
    if (!retro_load_game(&gi)) { printf("retro_load_game failed\n"); return 1; }
    memset(&av, 0, sizeof av);
    retro_get_system_av_info(&av);
    printf("loaded in %.0f ms: %ux%u (max %ux%u) %.2f fps, audio %.0f Hz\n", now_ms() - t0,
           av.geometry.base_width, av.geometry.base_height, av.geometry.max_width, av.geometry.max_height,
           av.timing.fps, av.timing.sample_rate);

    for (i = 0; i < 60; i++) { g_frames = i; retro_run(); }         /* warm-up, not timed */
    g_audio_frames = 0;
    t0 = now_ms();
    for (i = 0; i < n; i++) {
        g_frames = i;
        tf = now_ms();
        retro_run();
        tf = now_ms() - tf;
        if (tf > worst) worst = tf;
    }
    t1 = now_ms();
    printf("%u frames in %.0f ms: %.2f ms/frame avg, %.2f ms worst -> %.0f fps possible (%.0f%% of one core at %.2f fps)\n",
           n, t1 - t0, (t1 - t0) / n, worst, 1000.0 * n / (t1 - t0), (t1 - t0) / n * av.timing.fps / 10.0, av.timing.fps);
    printf("video %ux%u pitch %u fmt %d, audio %lu frames (%.0f/s), checksum %lu\n", g_w, g_h, (unsigned)g_pitch, (int)g_fmt,
           g_audio_frames, g_audio_frames / (n / av.timing.fps), g_sum);
    retro_unload_game();
    retro_deinit();
    return 0;
}
