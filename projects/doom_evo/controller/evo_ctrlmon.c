/* evo_ctrlmon - live-decode the iDrive controller from /dev/ipc/ipc25.
 * Reads the V850->OMAP broadcast, strips the 40-byte io-ipc header, classifies
 * each payload and prints ROTARY (with signed delta) and BUTTON events as they
 * happen. Ignores the 100ms poll, the heartbeat, and status chatter.
 *   evo_ctrlmon [seconds]      (default 60)
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>

#define DEV "/dev/ipc/ipc25"
#define HDR 40   /* io-ipc transport header length */

static int eq(const unsigned char*p,int n,const char*hexpairs){
    /* compare n bytes of p against space-separated hex */
    int i; const char*s=hexpairs;
    for(i=0;i<n;i++){ long v=strtol(s,(char**)&s,16); if((unsigned char)v!=p[i]) return 0; }
    return 1;
}

int main(int argc,char**argv){
    int secs=(argc>1)?atoi(argv[1]):60;
    int fd=open(DEV,O_RDONLY|O_NONBLOCK);
    time_t t0; int lastctr=-1;
    if(fd<0){ printf("open %s failed errno=%d (%s)\n",DEV,errno,strerror(errno)); return 1; }
    printf("live controller monitor on %s for %ds - operate the wheel/buttons\n",DEV,secs);
    fflush(stdout);
    t0=time(NULL);
    while(time(NULL)-t0<secs){
        fd_set rf; struct timeval tv; int s;
        unsigned char buf[1024];
        FD_ZERO(&rf); FD_SET(fd,&rf); tv.tv_sec=0; tv.tv_usec=100000;
        s=select(fd+1,&rf,NULL,NULL,&tv);
        if(s<=0||!FD_ISSET(fd,&rf)) continue;
        int n=read(fd,buf,sizeof buf);
        if(n<=HDR) continue;
        unsigned char*p=buf+HDR; int pl=n-HDR;
        /* poll / heartbeat / status: ignore */
        if(pl>=4 && eq(p,4,"15 02 01 04")) continue;
        if(pl>=2 && p[0]==0xe5 && p[1]==0x0f) continue;          /* heartbeat */
        if(pl>=3 && p[0]==0x05 && p[1]==0x00 && p[2]==0x1f) continue; /* status */
        /* ROTARY: e5 01 20 ... 01 1d <ctr> */
        if(pl>=16 && p[0]==0xe5 && p[1]==0x01 && p[2]==0x20 && p[14]==0x1d){
            int ctr=p[15], d=0;
            if(lastctr>=0){ d=ctr-lastctr; if(d>127)d-=256; if(d<-127)d+=256; }
            lastctr=ctr;
            printf("ROTARY  delta=%+3d  dir=%-5s ctr=0x%02x tail=%02x %02x %02x %02x\n",
                   d, d>0?"RIGHT":(d<0?"LEFT":"-"), ctr, p[16],p[17],p[18],p[19]);
            fflush(stdout); continue;
        }
        /* BUTTON bursts */
        {
            int i,isbtn=0;
            for(i=0;i+4<=pl;i++){ if(p[i]==0x0f&&p[i+1]==0xd2&&p[i+2]==0x04&&p[i+3]==0x10){isbtn=1;break;} }
            if(!isbtn && pl>=3 && p[0]==0x05&&p[1]==0x00&&p[2]==0xa0) isbtn=2;
            if(!isbtn && pl>=6 && p[0]==0x0f&&p[1]==0xc0) isbtn=3;
            if(isbtn){
                int j; printf("BUTTON  id=%02x %02x %02x  [",p[0],p[1],p[2]);
                for(j=0;j<pl&&j<20;j++) printf("%02x ",p[j]);
                printf("]\n"); fflush(stdout); continue;
            }
        }
        /* anything else that isn't noise */
        { int j; printf("OTHER   ["); for(j=0;j<pl&&j<24;j++) printf("%02x ",p[j]); printf("]\n"); fflush(stdout); }
    }
    printf("monitor done\n"); close(fd); return 0;
}
