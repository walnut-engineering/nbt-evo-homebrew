/* evo_dsi <ifname> [ifname...] - resolve DSI services via the servicebroker,
 * scanning versions 0..40. rc: 0=FOUND, 1=name-ok/version-mismatch, 2=unknown. */
#include <stdio.h>
#include <string.h>
#include <dsi/clientlib.h>
extern int SBOpen(const char*); extern void SBClose(int);
extern int SBAttachInterface(int,const char*,int,int,struct SConnectionInfo*);
int main(int argc,char**argv){
    int h=SBOpen("/srv/servicebroker");
    printf("SBOpen=%d\n",h); if(h<0) return 1;
    int a; for(a=1;a<argc;a++){
        const char*name=argv[a]; int maj, hit=0;
        for(maj=0;maj<=40;maj++){
            struct SConnectionInfo ci; memset(&ci,0,sizeof ci);
            int rc=SBAttachInterface(h,name,maj,0,&ci);
            if(rc==0||rc==1){ hit=1;
                printf("  %-44s v%d.0 rc=%d %-12s chid=%d pid=%d nid=%u\n",name,maj,rc,
                       rc==0?"FOUND":"name-ok",(int)ci.channel.chid,(int)ci.channel.pid,(unsigned)ci.channel.nid);
            }
        }
        if(!hit) printf("  %-44s  (not registered, all versions rc=2)\n",name);
    }
    SBClose(h); return 0;
}
