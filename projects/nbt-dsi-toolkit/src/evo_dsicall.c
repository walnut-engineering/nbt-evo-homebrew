/* evo_dsicall <iface> <major> <updateId> <cmd> [secs] - call a DSI METHOD over the
 * native QNX transport and dump the response. Method request encoding (decoded from
 * the WavePlayer proxy in Connectivity): rtype=REQUEST(0x0100), requestID =
 * 0x80000000 | updateId, body = marshaled args. Here body = one LE int32 <cmd>
 * (e.g. executeCommand: STOP=0, PLAY=1, ABORT=2). Reuses the evo_dsiqnx connect.
 *
 * LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib
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

#define DSI_MAGIC 0x200
enum { CMD_DATAREQ=7, CMD_DATARESP=8, CMD_CONNREQ=9 };
enum { RT_REQUEST=0x0100, RT_REQUEST_NOTIFY=0x0101 };

struct MsgHeader {
   int32_t type; uint16_t protoMajor; uint16_t protoMinor;
   SPartyID serverID; SPartyID clientID;
   uint32_t cmd; uint32_t flags; int32_t packetLength; uint32_t reserved;
};
struct EventInfo { uint32_t ifVersion; uint32_t rtype; uint32_t requestID; int32_t seq; };

static SPartyID g_server, g_client;

/* build [40B header type=0 + body] and MsgSend on coid */
static int dsi_msgsend(int coid, uint32_t cmd, const void* body, int blen, void* rep, int rlen) {
   unsigned char m[256];
   struct MsgHeader* h = (struct MsgHeader*)m; memset(m,0,sizeof m);
   h->type=0; h->protoMajor=4; h->serverID=g_server; h->clientID=g_client;
   h->cmd=cmd; h->packetLength=blen;
   if (blen && body) memcpy(m+sizeof(*h), body, blen);
   return MsgSend(coid, m, sizeof(*h)+blen, rep, rlen);
}

int main(int argc, char** argv) {
   if (argc < 5) { printf("usage: %s <iface> <major> <updateId> <cmd> [secs]\n", argv[0]); return 1; }
   const char* iface = argv[1];
   int major = atoi(argv[2]);
   uint32_t uid = (uint32_t)strtol(argv[3],0,0);
   int32_t cmd = (int32_t)strtol(argv[4],0,0);
   int secs = (argc>5)?atoi(argv[5]):3;

   int sb = SBOpen("/srv/servicebroker");
   if (sb < 0) { printf("SBOpen failed\n"); return 1; }
   struct SConnectionInfo ci; memset(&ci,0,sizeof ci);
   int rc = SBAttachInterface(sb, iface, major, 0, &ci);
   if (rc != 0) { printf("SBAttachInterface(%s v%d) rc=%d\n", iface, major, rc); SBClose(sb); return 2; }
   g_server = ci.serverID; g_client = ci.clientID;
   printf("attached %s v%d: pid=%d chid=%d serverID=0x%08x\n", iface, major,
          (int)ci.channel.pid, (int)ci.channel.chid, (unsigned)g_server.s.localID);

   int mychid = ChannelCreate(0);
   int coid = ConnectAttach(ci.channel.nid, ci.channel.pid, ci.channel.chid, _NTO_SIDE_CHANNEL, 0);
   if (coid == -1) { printf("ConnectAttach failed: %s\n", strerror(errno)); return 3; }

   /* ConnReq (type=0, cmd=9, 16B body [chid1,chid2,pid,nid]) */
   unsigned char cmsg[64]; memset(cmsg,0,sizeof cmsg);
   struct MsgHeader* ch = (struct MsgHeader*)cmsg;
   ch->type=0; ch->protoMajor=4; ch->serverID=ci.serverID; ch->clientID=ci.clientID;
   ch->cmd=CMD_CONNREQ; ch->packetLength=16;
   uint32_t* cb = (uint32_t*)(cmsg+sizeof(*ch));
   cb[0]=(uint32_t)mychid; cb[1]=(uint32_t)mychid; cb[2]=(uint32_t)getpid(); cb[3]=0;
   unsigned char crep[64]; memset(crep,0,sizeof crep);
   int r = MsgSend(coid, cmsg, sizeof(*ch)+16, crep, 16);
   if (r == -1) { printf("ConnReq MsgSend failed: %s\n", strerror(errno)); return 4; }
   uint32_t* rb = (uint32_t*)crep;
   int dcoid = ConnectAttach(0, ci.channel.pid, (int)rb[0], _NTO_SIDE_CHANNEL, 0);
   printf("connected; data chid=%u (dcoid=%d)\n", rb[0], dcoid);

   /* METHOD request: rtype=REQUEST, requestID=plain uid, body=EventInfo + args.
    * args = [int32 cmd] by default, OR a raw hex blob if argv[4] starts with "hex:" */
   unsigned char reqbuf[256]; memset(reqbuf,0,sizeof reqbuf);
   struct EventInfo* ei = (struct EventInfo*)reqbuf;
   ei->rtype = RT_REQUEST; ei->requestID = uid; ei->seq = 0;
   int blen = sizeof(struct EventInfo);
   if (strncmp(argv[4],"hex:",4)==0) {
      const char* hx = argv[4]+4; int nib=0; unsigned char cur=0;
      for (const char* p=hx; *p; ++p) {
         char c=*p; int v;
         if (c>='0'&&c<='9') v=c-'0'; else if (c>='a'&&c<='f') v=c-'a'+10;
         else if (c>='A'&&c<='F') v=c-'A'+10; else continue;
         cur=(cur<<4)|v; if (++nib==2){ reqbuf[blen++]=cur; cur=0; nib=0; }
      }
   } else {
      *(int32_t*)(reqbuf+sizeof(struct EventInfo)) = cmd; blen += 4;
   }
   unsigned char rep[128]; memset(rep,0,sizeof rep);
   int sr = dsi_msgsend(dcoid>=0?dcoid:coid, CMD_DATAREQ, reqbuf, blen, rep, sizeof rep);
   printf("CALL uid=0x%x (reqID=0x%08x) bodylen=%d -> MsgSend rc=%d%s\n",
          uid, ei->requestID, blen-(int)sizeof(struct EventInfo), sr, sr==-1?strerror(errno):"");
   if (sr >= 0) { printf("  sync reply: "); for (int i=0;i<16;i++) printf("%02x", rep[i]); printf("\n"); }

   /* listen for async response/notification on our channel */
   uint64_t to = 1ULL*1000*1000*1000;
   time_t t0 = time(NULL);
   while ((time(NULL)-t0) < secs) {
      struct _msg_info info; memset(&info,0,sizeof info);
      unsigned char buf[1024];
      TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, NULL, &to, NULL);
      int rcvid = MsgReceive(mychid, buf, sizeof buf, &info);
      if (rcvid == -1) { if (errno==ETIMEDOUT) continue; break; }
      if (rcvid == 0) { struct _pulse* p=(struct _pulse*)buf; printf("  PULSE code=%d val=%d\n",p->code,p->value.sival_int); continue; }
      struct MsgHeader* h2 = (struct MsgHeader*)buf;
      struct EventInfo* ei = (struct EventInfo*)(buf+sizeof(*h2));
      printf("  RESP cmd=%u rt=0x%04x rid=0x%08x len=%d data=", (unsigned)h2->cmd,
             (unsigned)ei->rtype, (unsigned)ei->requestID, info.msglen);
      int off=sizeof(*h2)+sizeof(*ei), n=info.msglen; if(n>off+256)n=off+256;
      for (int i=off;i<n;i++) printf("%02x", buf[i]);
      printf("\n");
      MsgReply(rcvid, 0, NULL, 0);
   }
   if (dcoid>=0) ConnectDetach(dcoid);
   ConnectDetach(coid); ChannelDestroy(mychid);
   SBDetachInterface(sb, ci.clientID); SBClose(sb);
   return 0;
}
