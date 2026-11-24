/* msg_sniff.so - LD_PRELOAD: log QNX native messaging (ConnectAttach + MsgSend*)
 * so we can see where a process sends events and the payload format. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/uio.h>
#include <sys/types.h>
static FILE *lg;
static void L(void){ if(!lg) lg = fopen("/dev/shmem/msg_sniff.log","a"); }
static void T(char*b){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); sprintf(b,"%ld.%03ld",(long)t.tv_sec,t.tv_nsec/1000000L); }
static void hex(const unsigned char*p,int n){ int i; for(i=0;i<n&&i<40;i++) fprintf(lg," %02x",p[i]); }

int ConnectAttach(uint32_t nd,pid_t pid,int chid,unsigned idx,int fl){
    int(*r)(uint32_t,pid_t,int,unsigned,int)=dlsym(RTLD_NEXT,"ConnectAttach");
    int c=r(nd,pid,chid,idx,fl);
    L(); if(lg){char t[32];T(t);fprintf(lg,"%s CONNECT coid=%d -> nd=%u pid=%d chid=%d\n",t,c,(unsigned)nd,(int)pid,chid);fflush(lg);}
    return c;
}
static void dv(const char*tag,int coid,const struct iovec*iov,int parts){
    if(!lg)return; char t[32];T(t); int tot=0,i; for(i=0;i<parts;i++)tot+=iov[i].iov_len;
    fprintf(lg,"%s %s coid=%d len=%d:",t,tag,coid,tot);
    int n=0; for(i=0;i<parts&&n<40;i++){const unsigned char*p=iov[i].iov_base;int j;for(j=0;j<(int)iov[i].iov_len&&n<40;j++,n++)fprintf(lg," %02x",p[j]);}
    fprintf(lg,"\n");fflush(lg);
}
int MsgSendv(int c,const struct iovec*s,int sp,const struct iovec*r_,int rp){ int(*r)(int,const struct iovec*,int,const struct iovec*,int)=dlsym(RTLD_NEXT,"MsgSendv"); L();dv("SENDV",c,s,sp); return r(c,s,sp,r_,rp); }
int MsgSendvnc(int c,const struct iovec*s,int sp,const struct iovec*r_,int rp){ int(*r)(int,const struct iovec*,int,const struct iovec*,int)=dlsym(RTLD_NEXT,"MsgSendvnc"); L();dv("SENDVNC",c,s,sp); return r(c,s,sp,r_,rp); }
int MsgSend(int c,const void*s,int sb,void*r_,int rb){ int(*r)(int,const void*,int,void*,int)=dlsym(RTLD_NEXT,"MsgSend"); L(); if(lg){char t[32];T(t);fprintf(lg,"%s SEND coid=%d len=%d:",t,c,sb);hex((const unsigned char*)s,sb);fprintf(lg,"\n");fflush(lg);} return r(c,s,sb,r_,rb); }
int MsgSendPulse(int c,int pr,int code,int val){ int(*r)(int,int,int,int)=dlsym(RTLD_NEXT,"MsgSendPulse"); L(); if(lg){char t[32];T(t);fprintf(lg,"%s PULSE coid=%d code=%d val=%d\n",t,c,code,val);fflush(lg);} return r(c,pr,code,val); }
