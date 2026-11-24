/* ipc_sniff.so v3 - LD_PRELOAD interposer. Logs every /dev/* open touchComDevice
 * makes, and every read/readv/readcond/devctl on ANY /dev/ipc/* channel, to find
 * where the controller stream actually arrives. Pass-through only. */
#include <time.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/uio.h>

static ssize_t (*r_write)(int,const void*,size_t);
static ssize_t (*r_read)(int,void*,size_t);
static int (*r_open)(const char*,int,...);
static int (*r_devctl)(int,int,void*,size_t,int*);
static int (*r_readcond)(int,void*,int,int,int,int);
static ssize_t (*r_readv)(int,const struct iovec*,int);
static char ipcfd[2048];          /* 1 if fd is a /dev/ipc/* channel */
static char names[2048][24];      /* short name per ipc fd */
static FILE *lg;

static void init(void){
    if(!r_write)   r_write   = dlsym(RTLD_NEXT,"write");
    if(!r_read)    r_read    = dlsym(RTLD_NEXT,"read");
    if(!r_open)    r_open    = dlsym(RTLD_NEXT,"open");
    if(!r_devctl)  r_devctl  = dlsym(RTLD_NEXT,"devctl");
    if(!r_readcond)r_readcond= dlsym(RTLD_NEXT,"readcond");
    if(!r_readv)   r_readv   = dlsym(RTLD_NEXT,"readv");
    if(!lg) lg = fopen("/dev/shmem/ipc_sniff.log","a");
}
static void dump(const char*t,int fd,const unsigned char*b,int n){
    if(!lg) return;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    const char*nm=(fd>=0&&fd<2048)?names[fd]:"";
    fprintf(lg,"%ld.%03ld %s fd=%d(%s) n=%d:",(long)ts.tv_sec,ts.tv_nsec/1000000L,t,fd,nm,n);
    int i; for(i=0;i<n&&i<96;i++) fprintf(lg," %02x",b[i]);
    fprintf(lg,"\n"); fflush(lg);
}
int open(const char*path,int flags,...){
    init();
    va_list ap; va_start(ap,flags); int mode=va_arg(ap,int); va_end(ap);
    int fd=r_open(path,flags,mode);
    if(path && strstr(path,"/dev/")){
        if(lg){fprintf(lg,"=== OPEN %s -> fd=%d\n",path,fd);fflush(lg);}
        if(fd>=0 && fd<2048 && strstr(path,"/dev/ipc/")){ ipcfd[fd]=1; const char*s=strrchr(path,'/'); strncpy(names[fd], s?s+1:path, 23); }
    }
    return fd;
}
ssize_t write(int fd,const void*b,size_t n){ init(); ssize_t r=r_write(fd,b,n); if(fd>=0&&fd<2048&&ipcfd[fd]) dump("WR",fd,b,(int)n); return r; }
ssize_t read(int fd,void*b,size_t n){ init(); ssize_t r=r_read(fd,b,n); if(fd>=0&&fd<2048&&ipcfd[fd]&&r>0) dump("RD",fd,b,(int)r); return r; }
int readcond(int fd,void*b,int n,int mn,int tm,int to){ init(); int r=r_readcond?r_readcond(fd,b,n,mn,tm,to):-1; if(fd>=0&&fd<2048&&ipcfd[fd]&&r>0) dump("RC",fd,b,r); return r; }
ssize_t readv(int fd,const struct iovec*iov,int c){ init(); ssize_t r=r_readv?r_readv(fd,iov,c):-1; if(fd>=0&&fd<2048&&ipcfd[fd]&&r>0&&c>0){int m=(int)iov[0].iov_len; if(r<m)m=(int)r; dump("RV",fd,iov[0].iov_base,m);} return r; }
int devctl(int fd,int d,void*data,size_t nb,int*info){ init(); int r=r_devctl(fd,d,data,nb,info); if(fd>=0&&fd<2048&&ipcfd[fd]){ if(lg){fprintf(lg,"DC fd=%d dcmd=0x%x nb=%d rc=%d\n",fd,d,(int)nb,r);fflush(lg);} if(data)dump("DCdata",fd,data,(int)nb);} return r; }
