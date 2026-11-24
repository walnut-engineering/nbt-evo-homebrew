/* evo_dsiqnx - probe the QNX-native DSI transport. SBAttachInterface gives the
 * server's {nid,pid,chid} (a QNX channel). The unit's CHB framework uses QNX
 * message passing (ConnectAttach + MsgSend), NOT UNIX sockets. This probe:
 *   ConnectAttach(nid,pid,chid) -> coid; ChannelCreate -> our chid;
 *   MsgSend a DSI ConnReq (40B hdr + ConnReqInfo{our pid, our chid}) and dump the reply;
 *   then MsgReceive for any server-initiated notifications for a few seconds.
 *
 *   evo_dsiqnx <iface> [major]
 * LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/neutrino.h>
#include "dsi/private/servicebroker.h"

extern int  SBOpen(const char*);
extern void SBClose(int);
extern int  SBAttachInterface(int, const char*, int, int, struct SConnectionInfo*);
extern int  SBDetachInterface(int, SPartyID);

#define DSI_MAGIC 0x200
enum { CMD_DATAREQ=7, CMD_DATARESP=8, CMD_CONNREQ=9, CMD_CONNRESP=11 };

struct MsgHeader {
   int32_t  type; uint16_t protoMajor; uint16_t protoMinor;
   SPartyID serverID; SPartyID clientID;
   uint32_t cmd; uint32_t flags; int32_t packetLength; uint32_t reserved;
};
struct ConnReqInfo { uint32_t pid; uint32_t channel; };
struct EventInfo { uint32_t ifVersion; uint32_t rtype; uint32_t requestID; int32_t seq; };
enum { RT_REQUEST_NOTIFY=0x0101 };

/* send a native DSI message (40B header type=0 + body) via MsgSend on coid */
static int dsi_msgsend(int coid, uint32_t cmd, SPartyID srv, SPartyID cli,
                       const void* body, int blen, void* reply, int rlen) {
   unsigned char m[128];
   struct MsgHeader* h = (struct MsgHeader*)m;
   memset(m,0,sizeof m);
   h->type=0; h->protoMajor=4; h->protoMinor=0;
   h->serverID=srv; h->clientID=cli; h->cmd=cmd; h->packetLength=blen;
   if (blen && body) memcpy(m+sizeof(*h), body, blen);
   return MsgSend(coid, m, sizeof(*h)+blen, reply, rlen);
}

int main(int argc, char** argv) {
   const char* iface = (argc>1)?argv[1]:"AuxInOEStream1.DMMNBTAuxiliaryInputStreaming";
   int major = (argc>2)?atoi(argv[2]):2;
   int alo   = (argc>3)?(int)strtol(argv[3],0,0):0;   /* strtol base 0 -> accepts 0x.. */
   int ahi   = (argc>4)?(int)strtol(argv[4],0,0):0x20;
   int secs  = (argc>5)?atoi(argv[5]):6;

   int sb = SBOpen("/srv/servicebroker");
   if (sb < 0) { printf("SBOpen failed\n"); return 1; }
   struct SConnectionInfo ci; memset(&ci,0,sizeof ci);
   int rc = SBAttachInterface(sb, iface, major, 0, &ci);
   if (rc != 0) { printf("SBAttachInterface(%s v%d) rc=%d\n", iface, major, rc); SBClose(sb); return 2; }
   printf("attached %s v%d: nid=%u pid=%d chid=%d serverID=0x%08x clientID=0x%08x\n",
          iface, major, (unsigned)ci.channel.nid, (int)ci.channel.pid, (int)ci.channel.chid,
          (unsigned)ci.serverID.s.localID, (unsigned)ci.clientID.s.localID);

   /* our receive channel for reverse notifications */
   int mychid = ChannelCreate(0);
   printf("ChannelCreate -> chid=%d (our pid=%d)\n", mychid, getpid());

   /* connect to the server's channel */
   int coid = ConnectAttach(ci.channel.nid, ci.channel.pid, ci.channel.chid, _NTO_SIDE_CHANNEL, 0);
   if (coid == -1) {
      printf("ConnectAttach(nid=%u,pid=%d,chid=%d) FAILED: %s\n",
             (unsigned)ci.channel.nid,(int)ci.channel.pid,(int)ci.channel.chid, strerror(errno));
      /* retry without _NTO_SIDE_CHANNEL flag */
      coid = ConnectAttach(ci.channel.nid, ci.channel.pid, ci.channel.chid, 0, 0);
      printf("retry plain ConnectAttach -> coid=%d (%s)\n", coid, coid==-1?strerror(errno):"ok");
      if (coid==-1) { SBClose(sb); return 3; }
   } else {
      printf("ConnectAttach -> coid=%d\n", coid);
   }

   /* build ConnReq: 40B header (type=0 for native!, cmd=9) + 16B body.
    * Body (decoded from DSIServerHandleConnectRequestEx/DSIChannelAttach) =
    * 4x uint32 [chid1, chid2, client pid, client nid] - the reverse channels
    * the server ConnectAttaches back to for notifications. */
   unsigned char msg[64]; memset(msg,0,sizeof msg);
   struct MsgHeader* h = (struct MsgHeader*)msg;
   h->type = 0; h->protoMajor = 4; h->protoMinor = 0;
   h->serverID = ci.serverID; h->clientID = ci.clientID;
   h->cmd = CMD_CONNREQ; h->packetLength = 16;
   uint32_t* body = (uint32_t*)(msg + sizeof(*h));
   body[0] = (uint32_t)mychid;      /* chid1 */
   body[1] = (uint32_t)mychid;      /* chid2 (reuse one channel) */
   body[2] = (uint32_t)getpid();    /* client pid */
   body[3] = 0;                     /* client nid (local) */
   int msglen = sizeof(*h) + 16;

   unsigned char reply[64]; memset(reply,0,sizeof reply);
   int r = MsgSend(coid, msg, msglen, reply, 16);   /* server MsgReply's 16B */
   printf("MsgSend(ConnReq type=0, %d bytes) -> rc=%d%s\n", msglen, r, r==-1?strerror(errno):"");
   {  /* reply16 = [server_dchid1, server_dchid2, server pid, nid] */
      uint32_t* rb = (uint32_t*)reply;
      printf("  reply: dchid1=%u dchid2=%u spid=%u nid=%u  raw=", rb[0],rb[1],rb[2],rb[3]);
      for (int i=0;i<16;i++) printf("%02x", reply[i]);
      printf("\n");
   }

   /* attach to the server's two DATA channels (from the reply) for requests */
   uint32_t* rb = (uint32_t*)reply;
   int dcoid1 = ConnectAttach(0, ci.channel.pid, (int)rb[0], _NTO_SIDE_CHANNEL, 0);
   int dcoid2 = ConnectAttach(0, ci.channel.pid, (int)rb[1], _NTO_SIDE_CHANNEL, 0);
   printf("data channels: dcoid1=%d (chid %u)  dcoid2=%d (chid %u)\n", dcoid1, rb[0], dcoid2, rb[1]);

   /* subscribe REQUEST_NOTIFY to attribute ids [alo,ahi] on the data channel */
   for (int a=alo; a<=ahi; a++) {
      struct EventInfo ei; memset(&ei,0,sizeof ei);
      ei.rtype=RT_REQUEST_NOTIFY; ei.requestID=0xC0000000u|(uint32_t)a;
      unsigned char rep[64];
      int sr = dsi_msgsend(dcoid1>=0?dcoid1:coid, CMD_DATAREQ, ci.serverID, ci.clientID,
                           &ei, sizeof ei, rep, sizeof rep);
      if (a==alo) printf("subscribe attr 0x%x -> MsgSend rc=%d%s\n", a, sr, sr==-1?strerror(errno):"");
   }
   printf("subscribed attrs 0x%x..0x%x; receiving for %ds...\n", alo, ahi, secs);

   /* receive notifications on our channel for `secs` */
   uint64_t timeout = 1ULL*1000*1000*1000;
   time_t t0 = time(NULL);
   while ((time(NULL)-t0) < secs) {
      struct _msg_info info; memset(&info,0,sizeof info);
      unsigned char rbuf[1024];
      TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, NULL, &timeout, NULL);
      int rcvid = MsgReceive(mychid, rbuf, sizeof rbuf, &info);
      if (rcvid == -1) { if (errno==ETIMEDOUT) continue; printf("  MsgReceive: %s\n", strerror(errno)); break; }
      if (rcvid == 0) { struct _pulse* p=(struct _pulse*)rbuf; printf("  PULSE code=%d val=%d\n",p->code,p->value.sival_int); continue; }
      struct MsgHeader* h2 = (struct MsgHeader*)rbuf;
      if (info.msglen>=(int)sizeof(struct MsgHeader) && h2->cmd==CMD_DATARESP) {
         struct EventInfo* ei=(struct EventInfo*)(rbuf+sizeof(*h2));
         int32_t* d=(int32_t*)(rbuf+sizeof(*h2)+sizeof(*ei));
         int nd=(h2->packetLength-sizeof(*ei))/4;
         printf("  NOTIFY rid=0x%08x rt=0x%04x n=%d [", ei->requestID, ei->rtype, nd);
         for (int i=0;i<nd&&i<16;i++) printf("%s%d", i?",":"", d[i]);
         printf("]\n");
      } else {
         printf("  MSG cmd=%u len=%d: ", (unsigned)h2->cmd, info.msglen);
         int n=info.msglen>48?48:info.msglen; for(int i=0;i<n;i++) printf("%02x", rbuf[i]);
         printf("\n");
      }
      MsgReply(rcvid, 0, NULL, 0);
   }
   if (dcoid1>=0) ConnectDetach(dcoid1);
   if (dcoid2>=0) ConnectDetach(dcoid2);

   ConnectDetach(coid);
   ChannelDestroy(mychid);
   SBDetachInterface(sb, ci.clientID);
   SBClose(sb);
   return 0;
}
