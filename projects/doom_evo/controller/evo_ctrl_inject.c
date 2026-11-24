/* evo_ctrl_inject.so - LD_PRELOAD for touchComDevice.
 * DIAGNOSTIC+INJECT: always logs every /dev/ipc/* open and the first reads on
 * each (channel + bytes) to /dev/shmem/evo_inject.log, so we can see which
 * channel touchComDevice reads the controller from. If EVO_INJECT=1 it also
 * fabricates a synthetic ROTARY frame on the target channel (EVO_INJECT_CHAN,
 * default ipc25) every EVO_INJECT_MS (default 1500) ms. Inert/pass-through for
 * injection unless EVO_INJECT=1. Hooks open/close/select/read only (no MsgSend).
 */
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>

static int (*r_open)(const char*,int,...);
static int (*r_close)(int);
static int (*r_select)(int,fd_set*,fd_set*,fd_set*,struct timeval*);
static ssize_t (*r_read)(int,void*,size_t);

static char g_isipc[4096];        /* 1 if fd is a /dev/ipc/* channel */
static char g_name[4096][16];     /* short channel name per fd */
static int  g_rdcount[4096];      /* reads logged per fd */
static int  g_targetfd = -1;
static int  g_enabled = 0;
static long g_period_ms = 1500;
static char g_chan[16] = "ipc25";
static long g_last_ms = 0;
static int  g_pending = 0;
static unsigned char g_ctr = 0x40;
static FILE *g_lg = NULL;

static unsigned char g_frame[60] = {
 0x00,0x00,0xf1,0x04,0x00,0x00,0x04,0x00,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0x01,0x81,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x50,0xe5,0x01,0x20,0x0c,0x0c,0x02,0x00,0xe5,
 0x01,0x07,0x00,0x00,0x00,0x01,0x1d,0x40,0x00,0x00,0x00,0x02};

static long now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (long)ts.tv_sec*1000+ts.tv_nsec/1000000L; }
static void init_once(void){
    const char*e;
    if(r_open) return;
    r_open=dlsym(RTLD_NEXT,"open"); r_close=dlsym(RTLD_NEXT,"close");
    r_select=dlsym(RTLD_NEXT,"select"); r_read=dlsym(RTLD_NEXT,"read");
    e=getenv("EVO_INJECT"); g_enabled=(e&&e[0]=='1');
    e=getenv("EVO_INJECT_MS"); if(e) g_period_ms=atol(e);
    e=getenv("EVO_INJECT_CHAN"); if(e) snprintf(g_chan,sizeof g_chan,"%s",e);
    g_lg=fopen("/dev/shmem/evo_inject.log","a");
    if(g_lg){ fprintf(g_lg,"[init] enabled=%d chan=%s period=%ld\n",g_enabled,g_chan,g_period_ms); fflush(g_lg);}
    g_last_ms=now_ms();
}
int open(const char*path,int flags,...){
    int fd; va_list ap; int mode; init_once();
    va_start(ap,flags); mode=va_arg(ap,int); va_end(ap);
    fd=r_open(path,flags,mode);
    if(path && strstr(path,"/dev/ipc/") && fd>=0 && fd<4096){
        const char*s=strrchr(path,'/'); s=s?s+1:path;
        g_isipc[fd]=1; strncpy(g_name[fd],s,15); g_rdcount[fd]=0;
        if(g_lg){ fprintf(g_lg,"[open] %s -> fd=%d\n",path,fd); fflush(g_lg);}
        if(!strcmp(s,g_chan)) g_targetfd=fd;
    }
    return fd;
}
int close(int fd){ init_once(); if(fd>=0&&fd<4096){ g_isipc[fd]=0; if(fd==g_targetfd)g_targetfd=-1;} return r_close(fd); }
int select(int n,fd_set*rd,fd_set*wr,fd_set*ex,struct timeval*tv){
    int rc; init_once(); rc=r_select(n,rd,wr,ex,tv);
    if(g_enabled && g_targetfd>=0 && rc>=0 && rd){
        long t=now_ms();
        if(t-g_last_ms>=g_period_ms){ if(!FD_ISSET(g_targetfd,rd)){FD_SET(g_targetfd,rd); rc=(rc<0?1:rc+1);} g_pending=1; g_last_ms=t; }
    }
    return rc;
}
ssize_t read(int fd,void*buf,size_t n){
    init_once();
    if(g_enabled && g_pending && fd==g_targetfd){
        g_pending=0; g_ctr++; g_frame[55]=g_ctr;
        size_t len=sizeof g_frame; if(len>n)len=len>n?n:len;
        memcpy(buf,g_frame,len);
        if(g_lg){ fprintf(g_lg,"[inject] ROTARY ctr=0x%02x on %s\n",g_ctr,g_name[fd]); fflush(g_lg);}
        return (ssize_t)len;
    }
    ssize_t r=r_read(fd,buf,n);
    if(fd>=0&&fd<4096&&g_isipc[fd]&&r>0&&g_rdcount[fd]<3){
        g_rdcount[fd]++;
        if(g_lg){ int i; unsigned char*b=buf; fprintf(g_lg,"[read] %s n=%d:",g_name[fd],(int)r); for(i=0;i<r&&i<24;i++)fprintf(g_lg," %02x",b[i]); fprintf(g_lg,"\n"); fflush(g_lg);}
    }
    return r;
}
