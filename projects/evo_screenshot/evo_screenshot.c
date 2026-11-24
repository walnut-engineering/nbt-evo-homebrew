/* evo_screenshot.c - grab the live HMI display via QNX Screen and dump raw RGBX.
 * Uses a SCREEN_DISPLAY_MANAGER_CONTEXT (privileged; run as root) + screen_read_display,
 * the documented screenshot path. Output file = 16B header {"EVOS", u32 w, u32 h, u32 bpp=4}
 * then w*h*4 de-strided RGBX bytes. Host converts to PNG (resolves channel order there).
 *   usage: evo_screenshot [outfile=/dev/shmem/hmi.raw] [display_index=0]
 */
#include <screen/screen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

static int die(const char* m){ fprintf(stderr,"ERR %s: %s (errno=%d)\n", m, strerror(errno), errno); return 1; }

int main(int argc, char** argv){
    const char* out = (argc>1)? argv[1] : "/dev/shmem/hmi.raw";
    int want = (argc>2)? atoi(argv[2]) : 0;

    screen_context_t ctx;
    if (screen_create_context(&ctx, SCREEN_DISPLAY_MANAGER_CONTEXT) != 0)
        return die("create_context(DISPLAY_MANAGER)");

    int nd = 0;
    if (screen_get_context_property_iv(ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &nd) != 0 || nd < 1)
        return die("display_count");
    screen_display_t* disps = (screen_display_t*)calloc(nd, sizeof(*disps));
    if (screen_get_context_property_pv(ctx, SCREEN_PROPERTY_DISPLAYS, (void**)disps) != 0)
        return die("get_displays");
    if (want < 0 || want >= nd) want = 0;
    screen_display_t disp = disps[want];

    int size[2] = {0,0};
    if (screen_get_display_property_iv(disp, SCREEN_PROPERTY_SIZE, size) != 0)
        return die("display_size");
    int w = size[0], h = size[1];
    fprintf(stderr, "display[%d]/%d  %dx%d\n", want, nd, w, h);
    if (w <= 0 || h <= 0) return die("bad_size");

    screen_pixmap_t pix;
    if (screen_create_pixmap(&pix, ctx) != 0) return die("create_pixmap");
    int fmt = SCREEN_FORMAT_RGBX8888;
    if (screen_set_pixmap_property_iv(pix, SCREEN_PROPERTY_FORMAT, &fmt) != 0) return die("set_format");
    int usage = SCREEN_USAGE_NATIVE;
    if (screen_set_pixmap_property_iv(pix, SCREEN_PROPERTY_USAGE, &usage) != 0) return die("set_usage");
    int bs[2] = { w, h };
    if (screen_set_pixmap_property_iv(pix, SCREEN_PROPERTY_BUFFER_SIZE, bs) != 0) return die("set_bufsize");
    if (screen_create_pixmap_buffer(pix) != 0) return die("create_pixmap_buffer");

    screen_buffer_t bufs[1] = {0};
    if (screen_get_pixmap_property_pv(pix, SCREEN_PROPERTY_RENDER_BUFFERS, (void**)bufs) != 0)
        return die("get_render_buffers");
    screen_buffer_t buf = bufs[0];

    if (screen_read_display(disp, buf, 0, NULL, 0) != 0) return die("read_display");

    unsigned char* ptr = NULL; int stride = 0;
    if (screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, (void**)&ptr) != 0) return die("get_pointer");
    if (screen_get_buffer_property_iv(buf, SCREEN_PROPERTY_STRIDE, &stride) != 0) return die("get_stride");
    fprintf(stderr, "ptr=%p stride=%d\n", (void*)ptr, stride);
    if (!ptr) return die("null_ptr");

    FILE* f = fopen(out, "wb");
    if (!f) return die("fopen_out");
    uint32_t hdr[4] = { 0x534F5645u /* "EVOS" */, (uint32_t)w, (uint32_t)h, 4u };
    fwrite(hdr, sizeof(hdr), 1, f);
    for (int y = 0; y < h; y++)
        fwrite(ptr + (size_t)y * stride, 4, w, f);
    fclose(f);
    fprintf(stderr, "wrote %s (%d bytes payload)\n", out, w*h*4);
    return 0;
}
