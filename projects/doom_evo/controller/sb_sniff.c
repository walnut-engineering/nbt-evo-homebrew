/* sb_sniff.so - LD_PRELOAD: log the exact interface name(s) a process registers
 * with the DSI servicebroker. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "dsi/private/servicebroker.h"
static FILE *lg;
static void L(void){ if(!lg) lg = fopen("/dev/shmem/sb_sniff.log","a"); }
int SBRegisterInterface(int h,const char*n,int a,int b,int c,SPartyID*s){
    L(); if(lg){fprintf(lg,"REG    '%s' v%d.%d chid=%d\n",n?n:"(null)",a,b,c);fflush(lg);}
    int(*r)(int,const char*,int,int,int,SPartyID*)=dlsym(RTLD_NEXT,"SBRegisterInterface");
    return r?r(h,n,a,b,c,s):-1;
}
int SBRegisterInterfaceEx(int h,const struct SFNDInterfaceDescription*d,int cnt,int c,SPartyID*s){
    L(); if(lg){int i;for(i=0;i<cnt;i++)fprintf(lg,"REGEX  '%s' v%u.%u chid=%d\n",d[i].name,(unsigned)d[i].version.majorVersion,(unsigned)d[i].version.minorVersion,c);fflush(lg);}
    int(*r)(int,const struct SFNDInterfaceDescription*,int,int,SPartyID*)=dlsym(RTLD_NEXT,"SBRegisterInterfaceEx");
    return r?r(h,d,cnt,c,s):-1;
}
int SBRegisterInterfaceIPC(int h,const char*n,int a,int b,int c,SPartyID*s){
    L(); if(lg){fprintf(lg,"REGIPC '%s' v%d.%d chid=%d\n",n?n:"(null)",a,b,c);fflush(lg);}
    int(*r)(int,const char*,int,int,int,SPartyID*)=dlsym(RTLD_NEXT,"SBRegisterInterfaceIPC");
    return r?r(h,n,a,b,c,s):-1;
}
int SBRegisterGroupInterface(int h,const char*n,int a,int b,int c,const char*g,SPartyID*s){
    L(); if(lg){fprintf(lg,"REGG   '%s' v%d.%d chid=%d grp=%s\n",n?n:"(null)",a,b,c,g?g:"");fflush(lg);}
    int(*r)(int,const char*,int,int,int,const char*,SPartyID*)=dlsym(RTLD_NEXT,"SBRegisterGroupInterface");
    return r?r(h,n,a,b,c,g,s):-1;
}
int SBAttachInterface(int h,const char*n,int a,int b,void*ci){
    L(); if(lg){fprintf(lg,"ATTACH   '%s' v%d.%d\n",n?n:"(null)",a,b);fflush(lg);}
    int(*r)(int,const char*,int,int,void*)=dlsym(RTLD_NEXT,"SBAttachInterface");
    return r?r(h,n,a,b,ci):-1;
}
int SBAttachInterfaceIPC(int h,const char*n,int a,int b,void*ci){
    L(); if(lg){fprintf(lg,"ATTACHIPC '%s' v%d.%d\n",n?n:"(null)",a,b);fflush(lg);}
    int(*r)(int,const char*,int,int,void*)=dlsym(RTLD_NEXT,"SBAttachInterfaceIPC");
    return r?r(h,n,a,b,ci):-1;
}
int SBAttachInterfaceExtended(int h,const char*n,int a,int b,void*ci,int c,int cd,int v,void*nid){
    L(); if(lg){fprintf(lg,"ATTACHEXT '%s' v%d.%d\n",n?n:"(null)",a,b);fflush(lg);}
    int(*r)(int,const char*,int,int,void*,int,int,int,void*)=dlsym(RTLD_NEXT,"SBAttachInterfaceExtended");
    return r?r(h,n,a,b,ci,c,cd,v,nid):-1;
}
