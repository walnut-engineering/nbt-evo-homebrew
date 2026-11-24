/*
 * evo_cursor - a mouse-cursor / pointer overlay for the NBT EVO head unit.
 *
 * Unlike DOOM and the terminal (which call evo_display_enable_presentation_mode
 * to HIDE the car HMI), this program deliberately leaves the HMI visible and
 * draws into a transparent framebuffer on top of it, so a cursor floats over
 * the live BMW UI. Whether that actually composites depends on the ILM surface
 * honouring per-pixel alpha - the -probe mode is here to answer that.
 *
 * Modes:
 *   -probe [secs]   draw a compositing test pattern (opaque | transparent |
 *                   opaque, plus a 50% alpha band) so a webcam frame shows
 *                   whether alpha blends over the HMI. Default 15s.
 *   -anim  [secs]   self-animate the cursor along a Lissajous path (no mouse
 *                   needed) to verify the cursor draws over the HMI. Default 20s.
 *   (default)       live: read /dev/usbmouse0 and move the cursor with the mouse.
 *   -scale N        pixel scale of the cursor sprite (default 3).
 *   -sens N         divide mouse deltas by N (default 1; 2-3 = slower).
 *
 * Teardown is trivial: we never hid the HMI, so exiting just destroys our layer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include "evo_graphics_lib.h"
#include "evo_context.h"
#include "evo_display.h"

#define DISPLAY_W 1440
#define DISPLAY_H 540
#define MOUSE_DEV "/dev/usbmouse0"

/* Value 0xAARRGGBB in a uint32_t; on little-endian ARM this lands in memory as
 * B,G,R,A which is what the EVO framebuffer expects (same packing DOOM uses). */
#define ARGB(a,r,g,b) ( ((uint32_t)(a)<<24) | ((uint32_t)(r)<<16) | ((uint32_t)(g)<<8) | (uint32_t)(b) )
#define TRANSPARENT   0x00000000u

static volatile int g_run = 1;
static void on_sig(int s){ (void)s; g_run = 0; }

static evo_context_t *g_ctx = NULL;

/* shared cursor position (updated by mouse thread, read by render loop) */
static volatile int g_cx = DISPLAY_W/2;
static volatile int g_cy = DISPLAY_H/2;
static volatile int g_btn = 0;

/* Cursor sprite: 'O' outline (black), 'X' fill (white), ' ' transparent. */
static const char *CURSOR[] = {
"O           ",
"OO          ",
"OXO         ",
"OXXO        ",
"OXXXO       ",
"OXXXXO      ",
"OXXXXXO     ",
"OXXXXXXO    ",
"OXXXXXXXO   ",
"OXXXXXXXXO  ",
"OXXXXXXXXXO ",
"OXXXXXXXXXXO",
"OXXXXXXOOOOO",
"OXXXOXXO    ",
"OXOO OXXO   ",
"OO    OXXO  ",
"O     OXXO  ",
"       OXXO ",
"       OXXO ",
"        OO  ",
};
#define CUR_ROWS (int)(sizeof(CURSOR)/sizeof(CURSOR[0]))

static void draw_cursor(uint32_t *fb, int cx, int cy, int scale){
    int ry, rx, sx, sy;
    for(ry=0; ry<CUR_ROWS; ry++){
        const char *row = CURSOR[ry];
        int rw = (int)strlen(row);
        for(rx=0; rx<rw; rx++){
            uint32_t col;
            char c = row[rx];
            /* button feedback: left held = orange fill, right held = blue fill, both = inverted */
            if(c=='O')      col = (g_btn==3) ? ARGB(0xFF,255,255,255) : ARGB(0xFF, 0, 0, 0);
            else if(c=='X') col = (g_btn==1) ? ARGB(0xFF,255,140,0) : (g_btn==2) ? ARGB(0xFF,60,140,255)
                                 : (g_btn==3) ? ARGB(0xFF,0,0,0) : ARGB(0xFF,255,255,255);
            else            continue;                         /* transparent */
            for(sy=0; sy<scale; sy++){
                for(sx=0; sx<scale; sx++){
                    int px = cx + rx*scale + sx;
                    int py = cy + ry*scale + sy;
                    if(px<0||px>=DISPLAY_W||py<0||py>=DISPLAY_H) continue;
                    fb[py*DISPLAY_W + px] = col;
                }
            }
        }
    }
}

/* clear to transparent, draw the cursor, present */
/* Dirty-rectangle renderer: the layer is cleared once, then only the sprite's previous
 * and new rectangles are touched, and the window is posted only when something changed.
 * (Clearing the whole 1440x540 layer 30x/s through a single buffer made the screen flash.) */
#define SPRITE_W 12
static int g_last_x = -1, g_last_y = -1, g_last_btn = -1, g_cleared = 0;
static void clear_rect(uint32_t *fb, int x, int y, int w, int h){
    int yy, xx;
    for(yy = y; yy < y + h; yy++){
        if(yy < 0 || yy >= DISPLAY_H) continue;
        for(xx = x; xx < x + w; xx++){
            if(xx < 0 || xx >= DISPLAY_W) continue;
            fb[yy*DISPLAY_W + xx] = TRANSPARENT;
        }
    }
}
static void render_cursor_frame(int cx, int cy, int scale){
    uint32_t *fb = evo_context_get_framebuffer(g_ctx);
    int btn = g_btn;
    if(!fb) return;
    if(!g_cleared){ memset(fb, 0, (size_t)DISPLAY_W*DISPLAY_H*4); g_cleared = 1; }
    else if(cx == g_last_x && cy == g_last_y && btn == g_last_btn) return;   /* nothing changed: no post */
    if(g_last_x >= 0) clear_rect(fb, g_last_x, g_last_y, SPRITE_W*scale, CUR_ROWS*scale);
    draw_cursor(fb, cx, cy, scale);
    g_last_x = cx; g_last_y = cy; g_last_btn = btn;
    evo_context_present(g_ctx);
}

/* compositing test pattern: left opaque red | middle transparent | right opaque
 * blue, with a 50%-alpha green band across the centre and a big cursor. */
static void render_probe_frame(int scale){
    int x, y;
    uint32_t *fb = evo_context_get_framebuffer(g_ctx);
    if(!fb) return;
    for(y=0; y<DISPLAY_H; y++){
        for(x=0; x<DISPLAY_W; x++){
            uint32_t c;
            if(x < DISPLAY_W/3)        c = ARGB(0xFF, 230, 30, 30);   /* opaque red  */
            else if(x < 2*DISPLAY_W/3) c = TRANSPARENT;               /* see HMI     */
            else                       c = ARGB(0xFF, 30, 120, 255);  /* opaque blue */
            fb[y*DISPLAY_W + x] = c;
        }
    }
    for(y=DISPLAY_H/2-28; y<DISPLAY_H/2+28; y++){
        if(y<0||y>=DISPLAY_H) continue;
        for(x=0; x<DISPLAY_W; x++) fb[y*DISPLAY_W + x] = ARGB(0x80, 40, 255, 40); /* 50% green */
    }
    draw_cursor(fb, DISPLAY_W/2 - 20, DISPLAY_H/2 - 60, scale*2);
    evo_context_present(g_ctx);
}

/* mouse reader. devu-mouse delivers 3-byte packets in the classic Microsoft
 * serial-mouse format (confirmed from a timestamped capture on the unit,
 * 2026-11-06):
 *   byte0 = 0 1 L R Y7 Y6 X7 X6      (0x40 always set; L=bit5, R=bit4)
 *   byte1 = 0 0 X5..X0                (low 6 bits of dx)
 *   byte2 = 0 0 Y5..Y0                (low 6 bits of dy)
 * dx/dy are 8-bit two's complement once the high bits are put back; +Y = down. */
static int g_sens = 1;   /* divide deltas by this (-sens N); 1 = raw counts */
static int sext8(int v){ return v & 0x80 ? v - 256 : v; }
static void *mouse_thread(void *arg){
    unsigned char buf[64];
    int fd = open(MOUSE_DEV, O_RDONLY);
    (void)arg;
    if(fd < 0){
        printf("[cursor] cannot open %s (errno %d) - live mouse disabled\n", MOUSE_DEV, errno);
        fflush(stdout);
        return NULL;
    }
    printf("[cursor] mouse open on %s (MS 3-byte format, sens 1/%d)\n", MOUSE_DEV, g_sens); fflush(stdout);
    long npk = 0, nrd = 0, nerr = 0; time_t last = time(NULL);
    while(g_run){
        int off;
        int n = read(fd, buf, sizeof(buf));
        if(time(NULL) - last >= 5){
            printf("[cursor] stats: reads=%ld pkts=%ld errs=%ld pos=%d,%d btn=%d\n", nrd, npk, nerr, g_cx, g_cy, g_btn);
            fflush(stdout); last = time(NULL);
        }
        if(n <= 0){
            nerr++;
            if(n < 0 && errno == EINTR) continue;
            if(n < 0 && errno != EAGAIN){            /* device went away (replug?): reopen */
                printf("[cursor] read error errno=%d - reopening %s\n", errno, MOUSE_DEV); fflush(stdout);
                close(fd); sleep(1); fd = open(MOUSE_DEV, O_RDONLY);
                if(fd < 0){ sleep(1); fd = open(MOUSE_DEV, O_RDONLY); }
                continue;
            }
            usleep(10000);
            continue;
        }
        nrd++;
        for(off=0; off+3<=n; off+=3){
            npk++;
            unsigned char b0 = buf[off];
            int dx = sext8(((b0 & 0x03) << 6) | (buf[off+1] & 0x3f));
            int dy = sext8(((b0 & 0x0c) << 4) | (buf[off+2] & 0x3f));
            int nx, ny;
            if(!(b0 & 0x40)) continue;             /* not a packet header: resync */
            nx = g_cx + dx / g_sens; ny = g_cy + dy / g_sens;
            if(nx < 0) nx = 0; if(nx >= DISPLAY_W) nx = DISPLAY_W-1;
            if(ny < 0) ny = 0; if(ny >= DISPLAY_H) ny = DISPLAY_H-1;
            g_cx = nx; g_cy = ny;
            g_btn = ((b0 >> 5) & 1) | (((b0 >> 4) & 1) << 1);   /* bit0 = left, bit1 = right */
        }
    }
    close(fd);
    return NULL;
}

int main(int argc, char **argv){
    int scale = 3, anim_secs = 0, probe = 0, i;
    time_t t0;
    double tt = 0;

    for(i=1; i<argc; i++){
        if(!strcmp(argv[i], "-probe")){
            probe = 1;
            if(i+1<argc && argv[i+1][0]>='0' && argv[i+1][0]<='9') anim_secs = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "-anim")){
            if(i+1<argc && argv[i+1][0]>='0' && argv[i+1][0]<='9') anim_secs = atoi(argv[++i]);
            else anim_secs = 20;
        } else if(!strcmp(argv[i], "-scale") && i+1<argc){
            scale = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "-sens") && i+1<argc){
            g_sens = atoi(argv[++i]); if(g_sens < 1) g_sens = 1;
        }
    }
    signal(SIGTERM, on_sig);
    signal(SIGINT,  on_sig);

    evo_graphics_init();
    g_ctx = evo_context_create(DISPLAY_W, DISPLAY_H, 0, 0);
    if(!g_ctx){ printf("[cursor] evo_context_create failed\n"); return 1; }
    if(evo_context_show(g_ctx) != 0){ printf("[cursor] evo_context_show failed\n"); }

    /* deliberately NO presentation mode: keep the HMI visible underneath. */
    { evo_display_t *disp = evo_context_get_display(g_ctx);
      if(disp){ evo_display_set_opacity(disp, 1.0f); }
      printf("[cursor] overlay layer up, HMI left visible (disp=%p)\n", (void*)disp);
    }
    fflush(stdout);

    if(probe){
        int secs = anim_secs ? anim_secs : 15;
        printf("[cursor] PROBE pattern for %ds\n", secs); fflush(stdout);
        t0 = time(NULL);
        while(g_run && time(NULL)-t0 < secs){ render_probe_frame(scale); usleep(33000); }
    } else if(anim_secs > 0){
        printf("[cursor] ANIM for %ds\n", anim_secs); fflush(stdout);
        t0 = time(NULL);
        while(g_run && time(NULL)-t0 < anim_secs){
            int cx = (int)(DISPLAY_W/2 + (DISPLAY_W/2 - 80) * sin(tt*1.3));
            int cy = (int)(DISPLAY_H/2 + (DISPLAY_H/2 - 80) * sin(tt*2.1));
            render_cursor_frame(cx, cy, scale);
            tt += 0.033;
            usleep(33000);
        }
    } else {
        pthread_t th;
        printf("[cursor] LIVE mouse mode\n"); fflush(stdout);
        pthread_create(&th, NULL, mouse_thread, NULL);
        pthread_detach(th);
        while(g_run){ render_cursor_frame(g_cx, g_cy, scale); usleep(33000); }
    }

    evo_context_destroy(g_ctx);
    evo_graphics_shutdown();
    printf("[cursor] exited cleanly\n"); fflush(stdout);
    return 0;
}
