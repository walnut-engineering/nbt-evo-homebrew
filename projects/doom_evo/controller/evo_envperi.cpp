/* evo_envperi - native QNX DSI-over-qnet subscriber (hand-written, read-only).
 *
 * Purpose: subscribe to a DSI data service (default EnvPeriphery.DSYSEnvPeriphery,
 * provided by HUJacintoStage1 on the Jacinto) the same way the HMI does, and dump
 * every notification so we can identify which attribute carries the iDrive knob.
 *
 * It is a pure ADDITIONAL subscriber: DSI multicasts notifications per-client, so
 * this does not disturb the HMI's own subscription (proven: two subscribers already
 * coexist on the wire). We only SBAttachInterface + REQUEST_NOTIFY + receive; we
 * never register a service or send a mutating request. MsgSend uses a timeout so a
 * non-responding server can never hang us. Clean detach on exit.
 *
 * Transport (from the open-source DSI source + decoded wire format):
 *   SBAttachInterface -> server {nid,pid,chid}, serverID, clientID
 *   ChannelCreate(our chid) ; ConnectAttach(server) -> coid
 *   send ConnectRequest  = MessageHeader{cmd=9} + ConnectRequestInfo{pid, chid}
 *   send REQUEST_NOTIFY  = MessageHeader{cmd=7} + EventInfo{REQUEST_NOTIFY, id}  (per id)
 *   the server ConnectAttaches back to our chid and MsgSends DataResponse
 *   (cmd=8) notifications; we MsgReceive + MsgReply(EOK) + decode.
 *
 * Usage: evo_envperi [iface] [major] [idlo] [idhi] [secs]
 *   iface default "EnvPeriphery.DSYSEnvPeriphery"; major 0=scan 1..20; ids 1..40; secs 0=forever
 *   LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/neutrino.h>
#include <sys/iomsg.h>
#include "dsi/private/servicebroker.h"     // SConnectionInfo, SPartyID, SFNDChannelInfo

extern "C" {
   int  SBOpen(const char*);
   void SBClose(int);
   int  SBAttachInterface(int, const char*, int, int, struct SConnectionInfo*);
   int  SBDetachInterface(int, SPartyID);
}

#define DSI_MAGIC 0x200
enum { CMD_DATAREQ=7, CMD_DATARESP=8, CMD_CONNREQ=9, CMD_DISCREQ=10, CMD_CONNRESP=11 };
enum { RT_REQUEST=0x0100, RT_REQUEST_NOTIFY=0x0101, RT_STOP_NOTIFY=0x0102 };

struct MsgHeader {
   int32_t  type;
   uint16_t protoMajor;
   uint16_t protoMinor;
   SPartyID serverID;
   SPartyID clientID;
   uint32_t cmd;
   uint32_t flags;
   uint32_t packetLength;
   int32_t  reserved;
};                                          // 40 bytes
struct EventInfo { uint32_t ifVersion; uint32_t rtype; uint32_t requestID; int32_t seq; }; // 16
struct ConnReqInfo { uint32_t pid; uint32_t channel; };                                    // 8

static int g_chid = -1, g_coid = -1, g_sb = -1;
static SConnectionInfo g_ci;

static void dump_response(const unsigned char* b, int n);   /* fwd */

/* MsgSend with a 2s send/reply timeout so we can never block on the master.
 * Captures the reply (the DSI ConnectResponse/data may arrive as the reply). */
static int send_msg(const void* buf, int len, const char* tag) {
   uint64_t to = 2ULL * 1000 * 1000 * 1000;
   unsigned char rep[2048];
   TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, NULL, &to, NULL);
   int r = MsgSend(g_coid, buf, len, rep, sizeof(rep));
   int e = errno;
   printf("MsgSend(%s) rc=%d errno=%d(%s) reply=%d\n", tag, r, (r==-1)?e:0, (r==-1)?strerror(e):"ok", r>0?r:0);
   if (r > 0) { printf("  reply: "); dump_response(rep, r); }
   return r;
}

static void send_header_only(uint32_t cmd, uint32_t plen, const void* body, const char* tag) {
   unsigned char m[128];
   MsgHeader* h = (MsgHeader*)m;
   memset(h, 0, sizeof(*h));
   h->type = DSI_MAGIC; h->protoMajor = 4; h->protoMinor = 0;
   h->serverID = g_ci.serverID; h->clientID = g_ci.clientID;
   h->cmd = cmd; h->flags = 0; h->packetLength = plen; h->reserved = 0;
   if (plen && body) memcpy(m + sizeof(MsgHeader), body, plen);
   send_msg(m, sizeof(MsgHeader) + plen, tag);
}

static void dump_response(const unsigned char* b, int n) {
   if (n < (int)sizeof(MsgHeader)) return;
   const MsgHeader* h = (const MsgHeader*)b;
   const char* cn = h->cmd==CMD_DATARESP?"DataResp":h->cmd==CMD_CONNRESP?"ConnResp":
                    h->cmd==CMD_DATAREQ?"DataReq":"?";
   int plen = h->packetLength;
   if (plen > n - (int)sizeof(MsgHeader)) plen = n - (int)sizeof(MsgHeader);
   const unsigned char* body = b + sizeof(MsgHeader);
   if (h->cmd == CMD_DATARESP && plen >= (int)sizeof(EventInfo)) {
      const EventInfo* ei = (const EventInfo*)body;
      const int32_t* d = (const int32_t*)(body + sizeof(EventInfo));
      int nd = (plen - sizeof(EventInfo)) / 4;
      printf("%s sid=0x%08x rid=0x%08x rt=0x%04x n=%d [", cn,
             (unsigned)h->serverID.s.localID, (unsigned)ei->requestID, (unsigned)ei->rtype, nd);
      for (int i = 0; i < nd && i < 16; i++) printf("%s%d", i?",":"", d[i]);
      printf("]\n"); fflush(stdout);
   } else {
      printf("%s sid=0x%08x cmd=%u plen=%d\n", cn,
             (unsigned)h->serverID.s.localID, (unsigned)h->cmd, plen); fflush(stdout);
   }
}

int main(int argc, char** argv) {
   const char* iface = (argc > 1) ? argv[1] : "EnvPeriphery.DSYSEnvPeriphery";
   int major = (argc > 2) ? atoi(argv[2]) : 0;      // 0 = scan
   int idlo  = (argc > 3) ? atoi(argv[3]) : 1;
   int idhi  = (argc > 4) ? atoi(argv[4]) : 40;
   int secs  = (argc > 5) ? atoi(argv[5]) : 0;

   g_sb = SBOpen("/srv/servicebroker");
   printf("SBOpen=%d\n", g_sb);
   if (g_sb < 0) return 1;

   int rc = -1;
   memset(&g_ci, 0, sizeof(g_ci));
   if (major > 0) {
      rc = SBAttachInterface(g_sb, iface, major, 0, &g_ci);
   } else {
      for (int m = 1; m <= 20; m++) {
         memset(&g_ci, 0, sizeof(g_ci));
         rc = SBAttachInterface(g_sb, iface, m, 0, &g_ci);
         if (rc == 0) { major = m; break; }
      }
   }
   printf("SBAttachInterface(%s, v%d) rc=%d  server nid=%u pid=%d chid=%d  serverID=0x%08x clientID=0x%08x\n",
          iface, major, rc, (unsigned)g_ci.channel.nid, (int)g_ci.channel.pid, (int)g_ci.channel.chid,
          (unsigned)g_ci.serverID.s.localID, (unsigned)g_ci.clientID.s.localID);
   if (rc != 0) { SBClose(g_sb); return 2; }

   g_chid = ChannelCreate(_NTO_CHF_UNBLOCK);
   g_coid = ConnectAttach(g_ci.channel.nid, g_ci.channel.pid, g_ci.channel.chid, 0, 0);
   printf("chid=%d coid=%d (errno=%d)\n", g_chid, g_coid, errno);
   if (g_chid < 0 || g_coid < 0) { SBDetachInterface(g_sb, g_ci.clientID); SBClose(g_sb); return 3; }

   /* 1) ConnectRequest (QNX/IPC form, from colibry sendConnectRequestIPC disasm):
    *    body is just the 4-byte channel (chid); the server derives our nd/pid from
    *    MsgInfo and ConnectAttaches back to deliver notifications. */
   uint32_t ipc_chan = (uint32_t)g_chid;
   send_header_only(CMD_CONNREQ, sizeof(ipc_chan), &ipc_chan, "ConnReq");
   printf("sent ConnectRequest (IPC chid=%d)\n", (int)ipc_chan);

   /* 2) REQUEST_NOTIFY for each attribute id in [idlo,idhi] to discover the knob. */
   for (int id = idlo; id <= idhi; id++) {
      EventInfo ei; memset(&ei, 0, sizeof(ei));
      ei.ifVersion = 0; ei.rtype = RT_REQUEST_NOTIFY; ei.requestID = (uint32_t)id; ei.seq = -1;
      send_header_only(CMD_DATAREQ, sizeof(ei), &ei, "Notify");
   }
   printf("sent REQUEST_NOTIFY ids %d..%d; receiving...\n", idlo, idhi);

   /* 3) receive loop: notifications arrive as messages on our channel. */
   time_t t0 = time(NULL);
   for (;;) {
      struct _msg_info info;
      unsigned char rbuf[4096];
      uint64_t to = 1ULL * 1000 * 1000 * 1000;       // 1s so we can honor `secs`
      TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, NULL, &to, NULL);
      int rcvid = MsgReceive(g_chid, rbuf, sizeof(rbuf), &info);
      if (rcvid > 0) { MsgReply(rcvid, 0, NULL, 0); dump_response(rbuf, info.msglen); }
      else if (rcvid == 0) { /* pulse */ printf("(pulse code=%d)\n", ((_pulse*)rbuf)->code); fflush(stdout); }
      if (secs && (time(NULL) - t0) >= secs) break;
   }

   /* clean teardown */
   SBDetachInterface(g_sb, g_ci.clientID);
   ConnectDetach(g_coid);
   ChannelDestroy(g_chid);
   SBClose(g_sb);
   return 0;
}
