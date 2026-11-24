/* evo_dsimp [iface] [major] [attrlo] [attrhi] [secs] - native (local) DSI client.
 *
 * The NBT's (no-tcp) DSI services talk over ABSTRACT UNIX-domain sockets named
 * "\0dsi/<pid>/<chid>" (decoded from the open DSI SDK: CCommEngine::makeLocalPath).
 * Same 40-byte DSI PDU as the TCP transport (evo_dsitcp) - only the envelope
 * differs: AF_UNIX abstract sockets, and the ConnReq body is ConnectRequestInfo
 * {pid, channel} (our own reverse endpoint) instead of {ip,port}.
 *
 *   SBAttachInterface(name,major,0,&ci) -> ci.channel{nid,pid,chid}, serverID, clientID.
 *   FORWARD : connect AF_UNIX to "\0dsi/<ci.pid>/<ci.chid>"; send ConnReq{our pid, our lfd}.
 *   REVERSE : server connects back to our "\0dsi/<getpid()>/<lfd>"; pushes notifications.
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
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include "dsi/private/servicebroker.h"

extern int  SBOpen(const char*);
extern void SBClose(int);
extern int  SBAttachInterface(int, const char*, int, int, struct SConnectionInfo*);
extern int  SBDetachInterface(int, SPartyID);

#define DSI_MAGIC 0x200
enum { CMD_DATAREQ=7, CMD_DATARESP=8, CMD_CONNREQ=9, CMD_DISCREQ=10, CMD_CONNRESP=11 };
enum { RT_REQUEST=0x0100, RT_REQUEST_NOTIFY=0x0101 };

struct MsgHeader {
   int32_t  type; uint16_t protoMajor; uint16_t protoMinor;
   SPartyID serverID; SPartyID clientID;
   uint32_t cmd; uint32_t flags; int32_t packetLength; uint32_t reserved;
};
struct EventInfo { uint32_t ifVersion; uint32_t rtype; uint32_t requestID; int32_t seq; };
struct ConnReqInfo { uint32_t pid; uint32_t channel; };   /* local ConnReq body */

static SPartyID g_server, g_client;

static int send_all(int fd, const void* b, int n) {
   const char* p = b; int left = n;
   while (left > 0) { int w = send(fd, p, left, 0); if (w <= 0) return -1; p += w; left -= w; }
   return n;
}
static int dsi_send(int fd, uint32_t cmd, const void* body, int blen) {
   unsigned char m[128];
   struct MsgHeader* h = (struct MsgHeader*)m;
   memset(h, 0, sizeof(*h));
   h->type = DSI_MAGIC; h->protoMajor = 4; h->protoMinor = 0;
   h->serverID = g_server; h->clientID = g_client;
   h->cmd = cmd; h->packetLength = blen;
   if (blen && body) memcpy(m + sizeof(*h), body, blen);
   return send_all(fd, m, sizeof(*h) + blen);
}

/* fill an abstract-namespace sockaddr_un "\0dsi/<pid>/<chid>"; return addrlen */
static socklen_t abs_path(struct sockaddr_un* sa, int pid, int chid) {
   memset(sa, 0, sizeof(*sa));
   sa->sun_family = AF_UNIX;
   sa->sun_path[0] = 0;                                  /* abstract namespace */
   int n = sprintf(sa->sun_path + 1, "dsi/%d/%d", pid, chid);
   return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + n);
}

static void dump_one(const char* tag, const unsigned char* b, int plen) {
   const struct MsgHeader* h = (const struct MsgHeader*)b;
   const unsigned char* body = b + sizeof(*h);
   const char* cn = h->cmd==CMD_DATARESP?"DataResp":h->cmd==CMD_CONNRESP?"ConnResp":
                    h->cmd==CMD_DATAREQ?"DataReq":h->cmd==CMD_CONNREQ?"ConnReq":"?";
   if (h->cmd == CMD_DATARESP && plen >= (int)sizeof(struct EventInfo)) {
      const struct EventInfo* ei = (const struct EventInfo*)body;
      const int32_t* d = (const int32_t*)(body + sizeof(*ei));
      int nd = (plen - sizeof(*ei)) / 4;
      printf("[%s] %s sid=0x%08x rid=0x%08x rt=0x%04x n=%d [", tag, cn,
             (unsigned)h->serverID.s.localID, (unsigned)ei->requestID, (unsigned)ei->rtype, nd);
      for (int i=0;i<nd && i<24;i++) printf("%s%d", i?",":"", d[i]);
      printf("]\n");
   } else {
      printf("[%s] %s sid=0x%08x cmd=%u plen=%d body=", tag, cn,
             (unsigned)h->serverID.s.localID, (unsigned)h->cmd, plen);
      for (int i=0;i<plen && i<24;i++) printf("%02x", body[i]);
      printf("\n");
   }
   fflush(stdout);
}

struct sbuf { unsigned char d[65536]; int len; };
static void process_stream(const char* tag, struct sbuf* s) {
   int off = 0;
   while (s->len - off >= 8) {
      const unsigned char* p = s->d + off;
      int32_t type; memcpy(&type, p, 4);
      if (type != DSI_MAGIC) {
         printf("[%s] transport ", tag);
         for (int i=0;i<8;i++) printf("%02x", p[i]);
         printf("\n"); fflush(stdout);
         off += 8; continue;
      }
      if (s->len - off < (int)sizeof(struct MsgHeader)) break;
      const struct MsgHeader* h = (const struct MsgHeader*)p;
      int plen = h->packetLength;
      if (plen < 0 || plen > 60000) { off += 1; continue; }
      int total = (int)sizeof(struct MsgHeader) + plen;
      if (s->len - off < total) break;
      dump_one(tag, p, plen);
      off += total;
   }
   if (off > 0) { memmove(s->d, s->d + off, s->len - off); s->len -= off; }
}
static int feed_stream(const char* tag, int fd, struct sbuf* s) {
   if (s->len >= (int)sizeof(s->d)) s->len = 0;
   int n = recv(fd, s->d + s->len, sizeof(s->d) - s->len, 0);
   if (n <= 0) return -1;
   s->len += n;
   process_stream(tag, s);
   return n;
}

int main(int argc, char** argv) {
   const char* iface = (argc>1)?argv[1]:"EnvPeriphery.DSYSEnvPeriphery";
   int major = (argc>2)?atoi(argv[2]):0;
   int alo   = (argc>3)?atoi(argv[3]):0;
   int ahi   = (argc>4)?atoi(argv[4]):0x30;
   int secs  = (argc>5)?atoi(argv[5]):0;

   int sb = SBOpen("/srv/servicebroker");
   if (sb < 0) { printf("SBOpen failed\n"); return 1; }
   struct SConnectionInfo ci; int rc=-1, m;
   if (major>0) { memset(&ci,0,sizeof ci); rc=SBAttachInterface(sb,iface,major,0,&ci); }
   else for (m=1;m<=20;m++){ memset(&ci,0,sizeof ci); rc=SBAttachInterface(sb,iface,m,0,&ci); if(rc==0){major=m;break;} }
   if (rc != 0) { printf("SBAttachInterface(%s) failed rc=%d\n", iface, rc); SBClose(sb); return 2; }
   g_server = ci.serverID; g_client = ci.clientID;
   printf("attached %s v%d: server nid=%u pid=%d chid=%d  serverID=0x%08x clientID=0x%08x\n",
          iface, major, (unsigned)ci.channel.nid, (int)ci.channel.pid, (int)ci.channel.chid,
          (unsigned)g_server.s.localID, (unsigned)g_client.s.localID);

   /* 1) local acceptor for the reverse (notification) connection, bound to
    *    "\0dsi/<getpid()>/<lfd>" exactly like the SDK (chid advertised = the fd). */
   int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (lfd < 0) { printf("socket(lfd): %s\n", strerror(errno)); return 3; }
   struct sockaddr_un la; socklen_t lal = abs_path(&la, getpid(), lfd);
   if (bind(lfd,(struct sockaddr*)&la,lal)<0){ printf("bind \\0dsi/%d/%d: %s\n",getpid(),lfd,strerror(errno)); return 3; }
   listen(lfd,4);
   printf("listening (reverse) on abstract dsi/%d/%d\n", getpid(), lfd);

   /* 2) forward connection to the server's abstract socket */
   int ffd = socket(AF_UNIX, SOCK_STREAM, 0);
   struct sockaddr_un sa; socklen_t sal = abs_path(&sa, ci.channel.pid, ci.channel.chid);
   if (connect(ffd,(struct sockaddr*)&sa,sal)<0){ printf("connect dsi/%d/%d: %s\n",(int)ci.channel.pid,(int)ci.channel.chid,strerror(errno)); return 4; }
   printf("forward connected to dsi/%d/%d\n", (int)ci.channel.pid, (int)ci.channel.chid);

   /* 3) ConnReq: body = our advertised (pid, channel=lfd) */
   struct ConnReqInfo cri; cri.pid = (uint32_t)getpid(); cri.channel = (uint32_t)lfd;
   if (dsi_send(ffd, CMD_CONNREQ, &cri, sizeof cri) < 0){ printf("send ConnReq: %s\n",strerror(errno)); return 5; }
   printf("sent ConnReq{pid=%u,chan=%u}\n", cri.pid, cri.channel);

   /* 4) REQUEST_NOTIFY for attribute ids [alo,ahi] */
   for (int a=alo; a<=ahi; a++) {
      struct EventInfo ei; memset(&ei,0,sizeof ei);
      ei.rtype=RT_REQUEST_NOTIFY; ei.requestID=0xC0000000u|(uint32_t)a;
      dsi_send(ffd, CMD_DATAREQ, &ei, sizeof ei);
   }
   printf("sent REQUEST_NOTIFY attrs 0x%x..0x%x; waiting...\n", alo, ahi);

   /* 5) event loop */
   int rfd = -1; time_t t0 = time(NULL);
   static struct sbuf fbuf, rbuf;
   for (;;) {
      fd_set rs; FD_ZERO(&rs);
      FD_SET(lfd,&rs); FD_SET(ffd,&rs); int mx=lfd>ffd?lfd:ffd;
      if (rfd>=0){ FD_SET(rfd,&rs); if(rfd>mx)mx=rfd; }
      struct timeval tv; tv.tv_sec=1; tv.tv_usec=0;
      int r = select(mx+1,&rs,NULL,NULL,&tv);
      if (r>0) {
         if (FD_ISSET(lfd,&rs)) {
            int nf = accept(lfd,NULL,NULL);
            if (nf>=0){ rfd=nf; printf(">>> reverse connection accepted\n"); fflush(stdout); }
         }
         if (FD_ISSET(ffd,&rs)) { if (feed_stream("fwd",ffd,&fbuf)<0){ printf("forward closed\n"); break; } }
         if (rfd>=0 && FD_ISSET(rfd,&rs)) { if (feed_stream("rev",rfd,&rbuf)<0){ printf("reverse closed\n"); rfd=-1; } }
      }
      if (secs && (time(NULL)-t0)>=secs) break;
   }
   SBDetachInterface(sb, g_client);
   if (rfd>=0) close(rfd); close(ffd); close(lfd); SBClose(sb);
   return 0;
}
