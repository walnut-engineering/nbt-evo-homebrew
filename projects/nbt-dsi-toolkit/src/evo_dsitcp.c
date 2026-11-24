/* evo_dsitcp [iface] [major] [attrlo] [attrhi] [secs] - native DSI MoCCA-over-TCP
 * subscriber. Read-only: attaches a service via the servicebroker's TCP variant,
 * opens the bidirectional MoCCA transport exactly like the HMI does, subscribes
 * (REQUEST_NOTIFY) to a range of attribute ids, and dumps every notification.
 *
 * Transport (decoded from re/captures/boot_handshake.pcap):
 *   SBAttachInterfaceTCP -> server socket (ip:port), serverID, clientID.
 *   FORWARD  : we TCP-connect to the server socket; send ConnReq (body = our own
 *              (ip,port) endpoint) then REQUEST_NOTIFY per attribute.
 *   REVERSE  : the server connects BACK to the (ip,port) we advertised; it pushes
 *              the notifications (cmd=8 DataResp) over that reverse connection.
 *   So we must listen on a local port and advertise it in the ConnReq body.
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "dsi/private/servicebroker.h"

extern int  SBOpen(const char*);
extern void SBClose(int);
extern int  SBAttachInterfaceTCP(int, const char*, int, int, struct STCPConnectionInfo*);
extern int  SBDetachInterface(int, SPartyID);

#define DSI_MAGIC 0x200
enum { CMD_DATAREQ=7, CMD_DATARESP=8, CMD_CONNREQ=9, CMD_DISCREQ=10, CMD_CONNRESP=11 };
enum { RT_REQUEST=0x0100, RT_REQUEST_NOTIFY=0x0101 };

/* 40-byte DSI MessageHeader (little-endian, matches the wire) */
struct MsgHeader {
   int32_t  type;
   uint16_t protoMajor;
   uint16_t protoMinor;
   SPartyID serverID;      /* 8B */
   SPartyID clientID;      /* 8B */
   uint32_t cmd;
   uint32_t flags;
   int32_t  packetLength;
   uint32_t reserved;
};
struct EventInfo { uint32_t ifVersion; uint32_t rtype; uint32_t requestID; int32_t seq; }; /* 16B */
struct Endpoint  { uint32_t ip; uint32_t port; };   /* 8B ConnReq body: (ip,port), host order */

static SPartyID g_server, g_client;
static uint32_t g_myip = 0xa030c7fe;   /* 160.48.199.254 (OMAP veom0) host order */

static int send_all(int fd, const void* b, int n) {
   const char* p = b; int left = n;
   while (left > 0) { int w = send(fd, p, left, 0); if (w <= 0) return -1; p += w; left -= w; }
   return n;
}

/* build + send a DSI message (header + optional body) on fd */
static int dsi_send(int fd, uint32_t cmd, const void* body, int blen) {
   unsigned char m[128];
   struct MsgHeader* h = (struct MsgHeader*)m;
   memset(h, 0, sizeof(*h));
   h->type = DSI_MAGIC; h->protoMajor = 4; h->protoMinor = 0;
   h->serverID = g_server; h->clientID = g_client;
   h->cmd = cmd; h->flags = 0; h->packetLength = blen; h->reserved = 0;
   if (blen && body) memcpy(m + sizeof(*h), body, blen);
   return send_all(fd, m, sizeof(*h) + blen);
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
      for (int i=0;i<plen && i<16;i++) printf("%02x", body[i]);
      printf("\n");
   }
   fflush(stdout);
}

/* Per-connection reassembly: a DSI stream is a sequence of 40B-header+body
 * messages (header starts with type==DSI_MAGIC), optionally preceded by the
 * 8-byte transport reply {ip,port} the server sends after ConnReq. recv() can
 * split or coalesce these, so we buffer and extract only complete messages. */
struct sbuf { unsigned char d[65536]; int len; };

static void process_stream(const char* tag, struct sbuf* s) {
   int off = 0;
   while (s->len - off >= 8) {
      const unsigned char* p = s->d + off;
      int32_t type; memcpy(&type, p, 4);
      if (type != DSI_MAGIC) {            /* 8-byte transport reply {ip,port} */
         printf("[%s] transport ", tag);
         for (int i=0;i<8;i++) printf("%02x", p[i]);
         printf("\n"); fflush(stdout);
         off += 8; continue;
      }
      if (s->len - off < (int)sizeof(struct MsgHeader)) break;   /* need full header */
      const struct MsgHeader* h = (const struct MsgHeader*)p;
      int plen = h->packetLength;
      if (plen < 0 || plen > 60000) { off += 1; continue; }      /* desync: resync */
      int total = (int)sizeof(struct MsgHeader) + plen;
      if (s->len - off < total) break;                           /* body incomplete */
      dump_one(tag, p, plen);
      off += total;
   }
   if (off > 0) { memmove(s->d, s->d + off, s->len - off); s->len -= off; }
}

/* append recv'd bytes to a reassembly buffer, then extract complete messages */
static int feed_stream(const char* tag, int fd, struct sbuf* s) {
   if (s->len >= (int)sizeof(s->d)) s->len = 0;                  /* overflow guard */
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
   struct STCPConnectionInfo ci; int rc=-1, m;
   if (major>0) { memset(&ci,0,sizeof ci); rc=SBAttachInterfaceTCP(sb,iface,major,0,&ci); }
   else for (m=1;m<=20;m++){ memset(&ci,0,sizeof ci); rc=SBAttachInterfaceTCP(sb,iface,m,0,&ci); if(rc==0){major=m;break;} }
   if (rc != 0) { printf("SBAttachInterfaceTCP(%s) failed rc=%d\n", iface, rc); SBClose(sb); return 2; }
   g_server = ci.serverID; g_client = ci.clientID;
   printf("attached %s v%d: server %u.%u.%u.%u:%u  serverID=0x%08x clientID=0x%08x\n",
          iface, major, (ci.socket.ipaddress>>24)&0xff,(ci.socket.ipaddress>>16)&0xff,
          (ci.socket.ipaddress>>8)&0xff,ci.socket.ipaddress&0xff,(unsigned)ci.socket.port,
          (unsigned)g_server.s.localID,(unsigned)g_client.s.localID);

   /* 1) local listener for the reverse (notification) connection */
   int lfd = socket(AF_INET, SOCK_STREAM, 0);
   int one=1; setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
   struct sockaddr_in la; memset(&la,0,sizeof la);
   la.sin_family=AF_INET; la.sin_addr.s_addr=htonl(INADDR_ANY); la.sin_port=0;
   if (bind(lfd,(struct sockaddr*)&la,sizeof la)<0){ printf("bind: %s\n",strerror(errno)); return 3; }
   listen(lfd,4);
   struct sockaddr_in sn; socklen_t snl=sizeof sn; getsockname(lfd,(struct sockaddr*)&sn,&snl);
   int myport = ntohs(sn.sin_port);
   printf("listening for reverse conn on 0.0.0.0:%d (advertising %u.%u.%u.%u:%d)\n", myport,
          (g_myip>>24)&0xff,(g_myip>>16)&0xff,(g_myip>>8)&0xff,g_myip&0xff, myport);

   /* 2) forward connection to the server master port */
   int ffd = socket(AF_INET, SOCK_STREAM, 0);
   struct sockaddr_in sa; memset(&sa,0,sizeof sa);
   sa.sin_family=AF_INET; sa.sin_port=htons((uint16_t)ci.socket.port);
   sa.sin_addr.s_addr=htonl(ci.socket.ipaddress);
   if (connect(ffd,(struct sockaddr*)&sa,sizeof sa)<0){ printf("connect: %s\n",strerror(errno)); return 4; }
   setsockopt(ffd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
   printf("forward connected.\n");

   /* 3) ConnReq: body = our advertised (ip,port) */
   struct Endpoint ep; ep.ip = g_myip; ep.port = (uint32_t)myport;
   if (dsi_send(ffd, CMD_CONNREQ, &ep, sizeof ep) < 0){ printf("send ConnReq: %s\n",strerror(errno)); return 5; }
   printf("sent ConnReq.\n");

   /* 4) REQUEST_NOTIFY for attribute ids [alo,ahi] (requestID = 0xC0000000|attr) */
   for (int a=alo; a<=ahi; a++) {
      struct EventInfo ei; memset(&ei,0,sizeof ei);
      ei.ifVersion=0; ei.rtype=RT_REQUEST_NOTIFY; ei.requestID=0xC0000000u|(uint32_t)a; ei.seq=0;
      dsi_send(ffd, CMD_DATAREQ, &ei, sizeof ei);
   }
   printf("sent REQUEST_NOTIFY attrs 0x%x..0x%x; waiting for notifications...\n", alo, ahi);

   /* 5) event loop: accept the reverse conn, read notifications from fwd+reverse.
    *    Each stream is reassembled so split/coalesced TCP segments decode cleanly. */
   int rfd = -1;
   time_t t0 = time(NULL);
   static struct sbuf fbuf, rbuf;    /* forward + reverse reassembly buffers */
   for (;;) {
      fd_set rs; FD_ZERO(&rs);
      FD_SET(lfd,&rs); FD_SET(ffd,&rs); int mx=lfd>ffd?lfd:ffd;
      if (rfd>=0){ FD_SET(rfd,&rs); if(rfd>mx)mx=rfd; }
      struct timeval tv; tv.tv_sec=1; tv.tv_usec=0;
      int r = select(mx+1,&rs,NULL,NULL,&tv);
      if (r>0) {
         if (FD_ISSET(lfd,&rs)) {
            struct sockaddr_in ra; socklen_t ral=sizeof ra;
            int nf = accept(lfd,(struct sockaddr*)&ra,&ral);
            if (nf>=0){ rfd=nf; setsockopt(rfd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
               printf(">>> reverse connection accepted from %s:%d\n", inet_ntoa(ra.sin_addr), ntohs(ra.sin_port)); fflush(stdout); }
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
