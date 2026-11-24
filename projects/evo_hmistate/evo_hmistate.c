/* evo_hmistate.c - detect whether the OEM HMI is on the main 6-tile Startmenu.
 *
 * WHY: the homebrew launcher ([[evo-homebrew-launcher]]) should only summon from the main menu,
 * not from a notification or a submenu. There is no clean readable HMI-state DSI interface, but
 * evo_screenshot proves screen_read_display reliably captures the OEM HMI framebuffer. So we
 * capture it and check a SIGNATURE of the Startmenu: the 6 tile LABELS ("Media/Radio",
 * "Communication", ... "Notifications") sit at 6 fixed, evenly-spaced x-centres on a near-black
 * lower background. Requiring bright text in ALL SIX label windows is highly specific - a submenu
 * or notification popup does not put text at all six positions - and is selection-invariant (the
 * labels are present regardless of which tile is highlighted).
 *
 * Modes:
 *   evo_hmistate            one-shot: capture once, print STARTMENU|OTHER + per-window counts, exit 0/1
 *   evo_hmistate -daemon    resident: mmap /dev/shmem/evo_hmistate, on each req_seq bump capture+check
 *
 * Tuning (env, no rebuild): EVO_HMI_LUMA (bright threshold, default 150),
 *   EVO_HMI_MINCOUNT (min bright px per window, default 110), EVO_HMI_DARKMAX (max lower-band mean
 *   luma, default 60; 0 disables the dark check).
 */
#include <screen/screen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "evo_hmistate.h"

/* --- capture context (created once, reused) --- */
typedef struct { screen_context_t ctx; screen_display_t disp; screen_pixmap_t pix;
                 screen_buffer_t buf; int w, h; } cap_t;

static int die(const char* m){ fprintf(stderr,"ERR %s: %s (errno=%d)\n", m, strerror(errno), errno); return -1; }

static int cap_init(cap_t* c){
    memset(c,0,sizeof(*c));
    if (screen_create_context(&c->ctx, SCREEN_DISPLAY_MANAGER_CONTEXT) != 0) return die("ctx");
    int nd=0;
    if (screen_get_context_property_iv(c->ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &nd)!=0 || nd<1) return die("ndisp");
    screen_display_t* ds=(screen_display_t*)calloc(nd,sizeof(*ds));
    if (screen_get_context_property_pv(c->ctx, SCREEN_PROPERTY_DISPLAYS,(void**)ds)!=0){ free(ds); return die("disps"); }
    c->disp=ds[0]; free(ds);
    int size[2]={0,0};
    if (screen_get_display_property_iv(c->disp, SCREEN_PROPERTY_SIZE, size)!=0) return die("size");
    c->w=size[0]; c->h=size[1];
    if (c->w<=0||c->h<=0) return die("badsize");
    if (screen_create_pixmap(&c->pix,c->ctx)!=0) return die("pix");
    int fmt=SCREEN_FORMAT_RGBX8888;   if (screen_set_pixmap_property_iv(c->pix,SCREEN_PROPERTY_FORMAT,&fmt)!=0) return die("fmt");
    int usage=SCREEN_USAGE_NATIVE;    if (screen_set_pixmap_property_iv(c->pix,SCREEN_PROPERTY_USAGE,&usage)!=0) return die("usage");
    int bs[2]={c->w,c->h};            if (screen_set_pixmap_property_iv(c->pix,SCREEN_PROPERTY_BUFFER_SIZE,bs)!=0) return die("bufsz");
    if (screen_create_pixmap_buffer(c->pix)!=0) return die("pixbuf");
    screen_buffer_t bufs[1]={0};
    if (screen_get_pixmap_property_pv(c->pix,SCREEN_PROPERTY_RENDER_BUFFERS,(void**)bufs)!=0) return die("renderbufs");
    c->buf=bufs[0];
    return 0;
}

/* capture into the pixmap; return the pointer+stride */
static int cap_grab(cap_t* c, unsigned char** ptr, int* stride){
    if (screen_read_display(c->disp, c->buf, 0, NULL, 0)!=0) return die("read_display");
    if (screen_get_buffer_property_pv(c->buf,SCREEN_PROPERTY_POINTER,(void**)ptr)!=0) return die("ptr");
    if (screen_get_buffer_property_iv(c->buf,SCREEN_PROPERTY_STRIDE,stride)!=0) return die("stride");
    return (*ptr)?0:-1;
}

static int env_int(const char* k,int d){ const char* v=getenv(k); if(v&&*v){ int x=atoi(v); return x; } return d; }

/* Startmenu signature. Fills window[0..5] with bright-pixel counts; returns 1 if Startmenu. */
static int sig_startmenu(unsigned char* p,int stride,int w,int h,int window[8]){
    int LUMA = env_int("EVO_HMI_LUMA",150);
    int MINC = env_int("EVO_HMI_MINCOUNT",110);
    int DARKMAX = env_int("EVO_HMI_DARKMAX",60);
    /* 6 tile-label x-centres + label-row band, defined on the reference 1440x540 panel, scaled to w,h */
    static const int CX1440[6]={175,395,610,827,1043,1262};
    int half = 95*w/1440; int y0=122*h/540, y1=168*h/540;
    int all6=1;
    for(int i=0;i<6;i++){
        int cx=CX1440[i]*w/1440, n=0;
        for(int y=y0;y<y1;y++){
            unsigned char* row=p+(size_t)y*stride;
            for(int x=cx-half;x<cx+half;x++){
                if(x<0||x>=w) continue;
                unsigned char* q=row+(size_t)x*4;    /* bytes = B,G,R,X */
                int luma=(q[2]*77 + q[1]*150 + q[0]*29)>>8;
                if(luma>LUMA) n++;
            }
        }
        window[i]=n;
        if(n<MINC) all6=0;
    }
    window[6]=0; window[7]=0;
    /* lower band must be near-black (Startmenu has only the faint reflection down there) */
    if(DARKMAX>0){
        long s=0,c=0; int yb0=470*h/540, yb1=500*h/540;
        for(int y=yb0;y<yb1;y++){ unsigned char* row=p+(size_t)y*stride;
            for(int x=0;x<w;x+=4){ unsigned char* q=row+(size_t)x*4; s+=(q[2]*77+q[1]*150+q[0]*29)>>8; c++; } }
        int mean = c? (int)(s/c):255; window[7]=mean;
        if(mean>DARKMAX) all6=0;
    }
    return all6;
}

static unsigned now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (unsigned)(t.tv_sec*1000u + t.tv_nsec/1000000u); }

int main(int argc,char**argv){
    int daemon_mode = (argc>1 && strcmp(argv[1],"-daemon")==0);
    cap_t cap;
    if (cap_init(&cap)!=0){ fprintf(stderr,"[hmistate] capture init failed\n"); return 2; }

    if(!daemon_mode){
        unsigned char* p=NULL; int st=0; int win[8]={0};
        if(cap_grab(&cap,&p,&st)!=0){ fprintf(stderr,"[hmistate] grab failed\n"); return 2; }
        int sm=sig_startmenu(p,st,cap.w,cap.h,win);
        printf("%s  windows=[%d %d %d %d %d %d] darkmean=%d (%dx%d)\n",
               sm?"STARTMENU":"OTHER", win[0],win[1],win[2],win[3],win[4],win[5],win[7],cap.w,cap.h);
        return sm?0:1;
    }

    /* daemon: publish via shared memory, serve on-demand checks */
    int fd=open(EVO_HMISTATE_PATH,O_RDWR|O_CREAT,0666);
    if(fd<0){ die("shm open"); return 2; }
    if(ftruncate(fd,sizeof(struct evo_hmistate))!=0){ /* may already exist */ }
    struct evo_hmistate* s=mmap(0,sizeof(*s),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    close(fd);
    if(s==MAP_FAILED){ die("mmap"); return 2; }
    s->magic=EVO_HMISTATE_MAGIC; s->req_seq=0; s->resp_seq=0; s->is_startmenu=1; /* fail-open default */
    fprintf(stderr,"[hmistate] daemon up (%dx%d), serving %s\n",cap.w,cap.h,EVO_HMISTATE_PATH);
    unsigned last_req=s->req_seq;
    for(;;){
        unsigned req=s->req_seq;
        /* ON-DEMAND ONLY: capture only when a client bumps req_seq (idle cost = zero captures). */
        if(req!=last_req){
            unsigned char* p=NULL; int st=0; int win[8]={0};
            if(cap_grab(&cap,&p,&st)==0){
                int sm=sig_startmenu(p,st,cap.w,cap.h,win);
                for(int i=0;i<8;i++) s->window[i]=win[i];
                s->is_startmenu=sm; s->last_ms=now_ms();
            } /* on grab failure keep the previous value (fail-open) */
            last_req=req;
            s->resp_seq=req;    /* ack the request */
        }
        usleep(25000);          /* 25ms poll -> on-demand latency <=25ms, no idle capture load */
    }
    return 0;
}
