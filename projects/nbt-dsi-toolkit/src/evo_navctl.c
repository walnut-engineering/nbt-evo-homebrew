/* evo_navctl - drive the NBT EVO navigation programmatically over DSI.
 *
 * Talks to the nav engine's PosDemo service (the firmware's own GPS/position SIMULATOR)
 * and can be extended to RouteGuidance. Same DSI transport as evo_dsiqnx (QNX service
 * broker: SBOpen/SBAttachInterface + ConnReq + DATAREQ with EventInfo+payload).
 *
 * PosDemo interface (RE'd from NavNBTEvoController, the CDNAVIPosDemoProxy):
 *   service attach name = "PosDemo.DNAVIPosDemo"  (interface "DNAVIPosDemo", instance "PosDemo")
 *   request = EventInfo{ifVersion, rtype=0x0100(REQUEST), requestID=<updateId>, seq} + payload
 *   methods (updateId : payload):
 *     Enable       1 : [u8 flag][pad*3]            (flag=1 turns demo positioning ON)
 *     SetSpeed     4 : [f32 speed]                 (m/s; the demo advance speed)
 *     Deviate      5 : (none)
 *     SetPosition  7 : [i32 lat][i32 lon][f32 heading]   (teleport)
 *     SetRouteId   8 : [u32 route_id]              (which active route to demo-drive)
 *     Stop         9 : (none)
 *     DemoDriveTo 10 : [i32 lat][i32 lon][f32 heading]
 *   lat/lon are the nav's integer coordinate units (encoding TBD/calibrate; not needed to
 *   drive an existing route, which only needs Enable+SetRouteId+SetSpeed).
 *
 * The nav must accept demo positions: navigation.ini has `ln-use-demo-positions`; Enable(1)
 * is expected to switch the live source to demo at runtime. If positions are still ignored,
 * set ln-use-demo-positions=true and restart the nav.
 *
 * usage (run on the unit):
 *   LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib ./evo_navctl <cmd> [args] [ifver]
 *     enable [0|1]           - Enable demo positioning (default 1)
 *     speed <mps>            - SetSpeed (float m/s)
 *     routeid <n>            - SetRouteId (uint; 0 = active route)
 *     setpos <lat> <lon> [hdg] - SetPosition (ints + heading float)
 *     drive <lat> <lon> [hdg]  - DemoDriveTo
 *     stop | deviate
 *     drive-route [routeid] [mps] - convenience: enable 1 + setrouteid + setspeed
 *   ifver : DSI interface version for the attach+EventInfo (default 1; sweepable)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/neutrino.h>
#include "dsi/private/servicebroker.h"

extern int  SBOpen(const char*);
extern void SBClose(int);
extern int  SBAttachInterface(int, const char*, int, int, struct SConnectionInfo*);
extern int  SBDetachInterface(int, SPartyID);

enum { CMD_DATAREQ=7, CMD_DATARESP=8, CMD_CONNREQ=9 };
enum { RT_REQUEST=0x0100 };

struct MsgHeader {
   int32_t type; uint16_t protoMajor; uint16_t protoMinor;
   SPartyID serverID; SPartyID clientID;
   uint32_t cmd; uint32_t flags; int32_t packetLength; uint32_t reserved;
};
struct EventInfo { uint32_t ifVersion; uint32_t rtype; uint32_t requestID; int32_t seq; };

static SPartyID g_server, g_client;
static uint32_t g_ifver = 1;

static int send_dreq(int coid, const void* body, int blen) {
   unsigned char m[2048]; struct MsgHeader* h = (struct MsgHeader*)m; memset(m,0,sizeof m);
   h->type=0; h->protoMajor=4; h->serverID=g_server; h->clientID=g_client;
   h->cmd=CMD_DATAREQ; h->packetLength=blen;
   if (blen && body) memcpy(m+sizeof(*h), body, blen);
   unsigned char rep[64];
   return MsgSend(coid, m, sizeof(*h)+blen, rep, sizeof rep);
}

static void do_request(int dcoid, int mychid, const char* label,
                       uint32_t updateId, const unsigned char* args, int alen, int secs) {
   unsigned char req[2048]; memset(req,0,sizeof req);
   struct EventInfo* ei = (struct EventInfo*)req;
   ei->ifVersion=g_ifver; ei->rtype=RT_REQUEST; ei->requestID=updateId; ei->seq=0;
   int blen = sizeof(struct EventInfo);
   if (alen) { memcpy(req+blen, args, alen); blen += alen; }
   while (blen & 3) req[blen++]=0;
   int sr = send_dreq(dcoid, req, blen);
   printf("[%s] send updateId=%u args=%dB -> rc=%d%s\n", label, updateId, alen, sr,
          sr==-1?strerror(errno):"");
   uint64_t to=1ULL*1000*1000*1000; time_t t0=time(NULL);
   while ((time(NULL)-t0) < secs) {
      struct _msg_info info; memset(&info,0,sizeof info);
      unsigned char buf[2048];
      TimerTimeout(CLOCK_MONOTONIC,_NTO_TIMEOUT_RECEIVE,NULL,&to,NULL);
      int rcvid=MsgReceive(mychid, buf, sizeof buf, &info);
      if (rcvid==-1){ if(errno==ETIMEDOUT) continue; break; }
      if (rcvid==0){ continue; }
      struct MsgHeader* h2=(struct MsgHeader*)buf; struct EventInfo* e2=(struct EventInfo*)(buf+sizeof(*h2));
      int o=sizeof(*h2)+sizeof(*e2), n=info.msglen;
      printf("  [%s] RESP cmd=%u rt=0x%04x rid=0x%08x len=%d data=", label,(unsigned)h2->cmd,
             (unsigned)e2->rtype,(unsigned)e2->requestID,n);
      int m=n; if(m>o+64)m=o+64; for(int i=o;i<m;i++) printf("%02x",buf[i]); printf("\n");
      MsgReply(rcvid,0,NULL,0);
      return;
   }
   printf("  [%s] (no response within %ds)\n", label, secs);
}

/* method payload builders -------------------------------------------------- */
struct Op { const char* name; uint32_t updateId; unsigned char payload[64]; int plen; };

static int build_op(int argc, char** argv, int i, struct Op* op, int* consumed) {
   const char* c = argv[i];
   memset(op,0,sizeof *op);
   if (!strcmp(c,"enable")) {
      op->name="Enable"; op->updateId=1;
      int flag = (i+1<argc && argv[i+1][0]!='-' ) ? atoi(argv[i+1]) : 1;
      if (i+1<argc && (argv[i+1][0]=='0'||argv[i+1][0]=='1')) *consumed=2; else *consumed=1;
      op->payload[0]=(unsigned char)flag; op->plen=4;
   } else if (!strcmp(c,"speed")) {
      op->name="SetSpeed"; op->updateId=4; float s=(i+1<argc)?(float)atof(argv[i+1]):0;
      memcpy(op->payload,&s,4); op->plen=4; *consumed=2;
   } else if (!strcmp(c,"routeid")) {
      op->name="SetRouteId"; op->updateId=8; uint32_t r=(i+1<argc)?(uint32_t)strtoul(argv[i+1],0,0):0;
      memcpy(op->payload,&r,4); op->plen=4; *consumed=2;
   } else if (!strcmp(c,"setpos") || !strcmp(c,"drive")) {
      op->name = c[0]=='s'?"SetPosition":"DemoDriveTo"; op->updateId = c[0]=='s'?7:10;
      int32_t lat=(i+1<argc)?(int32_t)strtol(argv[i+1],0,0):0;
      int32_t lon=(i+2<argc)?(int32_t)strtol(argv[i+2],0,0):0;
      float hdg=(i+3<argc)?(float)atof(argv[i+3]):0;
      memcpy(op->payload,&lat,4); memcpy(op->payload+4,&lon,4); memcpy(op->payload+8,&hdg,4);
      op->plen=12; *consumed=4;
   } else if (!strcmp(c,"stop")) { op->name="Stop"; op->updateId=9; op->plen=0; *consumed=1; }
   else if (!strcmp(c,"deviate")) { op->name="Deviate"; op->updateId=5; op->plen=0; *consumed=1; }
   else return -1;
   return 0;
}

int main(int argc, char** argv) {
   if (argc < 2) { printf("usage: %s <enable|speed|routeid|setpos|drive|stop|deviate|drive-route> [args] [ifver]\n", argv[0]); return 1; }

   /* trailing numeric arg = ifver (only if it doesn't belong to a command that needs it) */
   /* Simpler: accept ifver via env NAVCTL_IFVER, else default 1. */
   const char* ev = getenv("NAVCTL_IFVER"); if (ev) g_ifver=(uint32_t)strtoul(ev,0,0);
   int secs = 3;

   int sb=SBOpen("/srv/servicebroker"); if(sb<0){printf("SBOpen failed\n");return 1;}
   struct SConnectionInfo ci; memset(&ci,0,sizeof ci);
   int rc=SBAttachInterface(sb,"PosDemo.DNAVIPosDemo",(int)g_ifver,0,&ci);
   if(rc!=0){printf("SBAttachInterface(PosDemo.DNAVIPosDemo,v%u) rc=%d  (try a different ifver via NAVCTL_IFVER)\n",g_ifver,rc);SBClose(sb);return 2;}
   g_server=ci.serverID; g_client=ci.clientID;
   printf("attached PosDemo.DNAVIPosDemo v%u: pid=%d chid=%d serverID=0x%08x\n",
          g_ifver,(int)ci.channel.pid,(int)ci.channel.chid,(unsigned)g_server.s.localID);
   int mychid=ChannelCreate(0);
   unsigned char cmsg[64]; memset(cmsg,0,sizeof cmsg);
   struct MsgHeader* ch=(struct MsgHeader*)cmsg;
   int coid=ConnectAttach(ci.channel.nid,ci.channel.pid,ci.channel.chid,_NTO_SIDE_CHANNEL,0);
   if(coid==-1){printf("ConnectAttach: %s\n",strerror(errno));return 3;}
   ch->type=0; ch->protoMajor=4; ch->serverID=ci.serverID; ch->clientID=ci.clientID; ch->cmd=CMD_CONNREQ; ch->packetLength=16;
   uint32_t* cb=(uint32_t*)(cmsg+sizeof(*ch)); cb[0]=mychid; cb[1]=mychid; cb[2]=getpid(); cb[3]=0;
   unsigned char crep[64]; memset(crep,0,sizeof crep);
   if(MsgSend(coid,cmsg,sizeof(*ch)+16,crep,16)==-1){printf("ConnReq: %s\n",strerror(errno));return 4;}
   int dcoid=ConnectAttach(0,ci.channel.pid,(int)((uint32_t*)crep)[0],_NTO_SIDE_CHANNEL,0);
   printf("connected; data dcoid=%d\n", dcoid);

   if (!strcmp(argv[1],"sim")) {
      /* sim <delayms> <lon1> <lat1> <hdg1> <lon2> <lat2> <hdg2> ...
       * Visit each (lon,lat,heading) point in order via a SYNCHRONOUS SetPosition that
       * drains the response each step (the earlier fire-and-forget flooded NavigationBasic's
       * reply queue and crashed it). The host supplies the dense path + headings. Gentle pacing.*/
      int delayms = (argc>2)?atoi(argv[2]):1200;
      int np = (argc-3)/3;
      unsigned char p[16];
      p[0]=1; p[1]=p[2]=p[3]=0; do_request(dcoid,mychid,"Enable",1,p,4,1);
      printf("sim: %d points, %dms/step (synchronous, response-drained)\n", np, delayms);
      for (int i=0; i<np; i++) {
         int32_t lon=(int32_t)strtol(argv[3+i*3],0,0);
         int32_t lat=(int32_t)strtol(argv[4+i*3],0,0);
         float   hdg=(float)atof(argv[5+i*3]);
         memcpy(p,&lon,4); memcpy(p+4,&lat,4); memcpy(p+8,&hdg,4);
         do_request(dcoid,mychid,"SetPosition",7,p,12,2); /* waits -> drains the reply */
         if (i+1<np) usleep(delayms*1000);
      }
      printf("sim: done (%d points)\n", np);
   } else if (!strcmp(argv[1],"drive-route")) {
      uint32_t route=(argc>2)?(uint32_t)strtoul(argv[2],0,0):0;
      float sp=(argc>3)?(float)atof(argv[3]):13.9f; /* ~50 km/h */
      unsigned char p[8];
      p[0]=1; p[1]=p[2]=p[3]=0; do_request(dcoid,mychid,"Enable",1,p,4,secs);
      memcpy(p,&route,4); do_request(dcoid,mychid,"SetRouteId",8,p,4,secs);
      memcpy(p,&sp,4);    do_request(dcoid,mychid,"SetSpeed",4,p,4,secs);
   } else {
      int i=1; while(i<argc){ struct Op op; int consumed=1;
         if (build_op(argc,argv,i,&op,&consumed)!=0){ printf("(unknown cmd '%s')\n",argv[i]); break; }
         do_request(dcoid,mychid,op.name,op.updateId,op.payload,op.plen,secs);
         i+=consumed;
      }
   }

   if(dcoid>=0) ConnectDetach(dcoid);
   ConnectDetach(coid); ChannelDestroy(mychid);
   SBDetachInterface(sb,ci.clientID); SBClose(sb);
   return 0;
}
