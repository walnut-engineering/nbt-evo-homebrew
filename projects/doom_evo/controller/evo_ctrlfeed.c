/* evo_ctrlfeed <service> [major] [attrlo] [attrhi] - iDrive controller -> DOOM feed.
 *
 * Subscribes to the iDrive-controller DSI service (MoCCA-over-TCP, exactly like
 * evo_dsitcp / the HMI) and translates each notification into a compact event in
 * the SPSC ring /dev/shmem/evo_ctrl_feed, which DOOM's i_input_evo.c consumes:
 *     rotary  -> turn,  5-way fwd/back -> move,  5-way left/right -> strafe,
 *     centre press -> fire.
 * Read-only on the bus (just another subscriber); the HMI is unaffected.
 *
 *   LD_LIBRARY_PATH=/opt/sys/lib:/opt/car/lib:/opt/core/lib:/usr/lib:/lib \
 *     evo_ctrlfeed <Service.Interface> [major] [attrlo] [attrhi]
 *
 * ========================================================================
 *  BEFORE THIS WORKS: run the spin test to learn (a) which service carries the
 *  controller and (b) how its attributes encode rotary / 5-way / centre.
 *     tools/spin_test.sh      (user rotates knob + presses during capture)
 *     -> find_knob.py prints the (serverID, requestID) whose values change.
 *     -> grep <serverID> captures/service_map.txt  => the service name for argv[1]
 *  Then fill the DECODE MAP constants below and rebuild. Until then the decode is
 *  a no-op passthrough that just logs every notification (so you can SEE the codes
 *  with this running and the knob being turned).
 * ========================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "dsi/private/servicebroker.h"
#include "evo_feed.h"

extern int  SBOpen(const char*);
extern void SBClose(int);
extern int  SBAttachInterfaceTCP(int, const char*, int, int, struct STCPConnectionInfo*);
extern int  SBDetachInterface(int, SPartyID);

/* ===================== DECODE MAP (fill from the spin test) ===================== */
/* Each DSI notification carries requestID = 0xC0000000|attr and an int32 array.
 * Set which attr is which, and how each value encodes. 0xFFFF = "unknown / log only". */
#define ATTR_ROTARY   0xFFFF   /* attr id of the rotary encoder                      */
#define ROTARY_FIELD  0        /* which int in the array holds the rotary value      */
#define ROTARY_ABS    1        /* 1 = value is an ABSOLUTE detent counter (take delta);
                                  0 = value is already a signed per-event delta       */
#define ROTARY_WRAP   256      /* counter wrap modulus if ABS (e.g. 256 for a u8)     */
#define ROTARY_SIGN   (+1)     /* flip to -1 if clockwise should turn left            */

#define ATTR_DIR      0xFFFF   /* attr id of the 5-way joystick direction            */
#define DIR_FIELD     0        /* which int holds the direction/state code           */
/* map the controller's direction code -> EVO_DIR_* ; edit dir_decode() for the real
 * encoding (single code+state, a bitmask, or 4 separate attrs). */

#define ATTR_CENTER   0xFFFF   /* attr id of the centre press (knob pushed down)     */
#define CENTER_FIELD  0        /* which int holds the pressed state (nonzero = down) */
/* ================================================================================ */

#define DSI_MAGIC 0x200
enum { CMD_DATAREQ=7, CMD_DATARESP=8, CMD_CONNREQ=9, CMD_DISCREQ=10, CMD_CONNRESP=11 };
enum { RT_REQUEST=0x0100, RT_REQUEST_NOTIFY=0x0101 };

struct MsgHeader { int32_t type; uint16_t protoMajor; uint16_t protoMinor;
   SPartyID serverID; SPartyID clientID; uint32_t cmd; uint32_t flags;
   int32_t packetLength; uint32_t reserved; };
struct EventInfo { uint32_t ifVersion; uint32_t rtype; uint32_t requestID; int32_t seq; };
struct Endpoint  { uint32_t ip; uint32_t port; };

static SPartyID g_server, g_client;
static uint32_t g_myip = 0xa030c7fe;   /* 160.48.199.254 (OMAP veom0) host order */
static struct evo_feed* g_feed = NULL;
static FILE* g_log = NULL;

/* -------- the feed (producer side) -------- */
static int feed_init(void) {
   int fd = open(EVO_FEED_PATH, O_CREAT|O_RDWR, 0666);
   if (fd < 0) { perror("open feed"); return -1; }
   if (ftruncate(fd, sizeof(struct evo_feed)) < 0) { perror("ftruncate"); close(fd); return -1; }
   void* m = mmap(NULL, sizeof(struct evo_feed), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
   close(fd);
   if (m == MAP_FAILED) { perror("mmap feed"); return -1; }
   g_feed = (struct evo_feed*)m;
   g_feed->head = 0;
   g_feed->magic = EVO_FEED_MAGIC;   /* set last: DOOM waits for this */
   return 0;
}
static void feed_push(unsigned char kind, unsigned char ctr, unsigned char b0, unsigned char b1) {
   if (!g_feed) return;
   unsigned h = g_feed->head;
   struct evo_ev* e = &g_feed->ev[h % EVO_FEED_RING];
   e->kind = kind; e->ctr = ctr; e->b[0] = b0; e->b[1] = b1;
   e->b[2]=e->b[3]=e->b[4]=e->b[5]=0;
   __asm__ __volatile__("" ::: "memory");   /* fill slot before publishing head */
   g_feed->head = h + 1;
}

/* -------- decode: notification -> feed event (THE part the spin test parameterises) -------- */
static void dir_decode(int code) {
   /* TODO per spin test. Common BMW 5-way encodings:
    *  (A) one attr, value = {0 idle,1 N,2 S,3 E/right,4 W/left} + a separate release(0).
    *  (B) one attr, value = a bitmask of held directions.
    *  (C) four attrs (one per direction), value = 1 down / 0 up.
    * The skeleton below assumes (A): a single current-direction code; 0 = all released.
    * Edit the case labels to the real codes, and the EVO_DIR_* targets to taste. */
   static int held = -1;   /* currently-held EVO_DIR_* , or -1 */
   int dir = -1;
   switch (code) {
      /* case 1: dir = EVO_DIR_FWD;   break;   <-- fill real codes */
      /* case 2: dir = EVO_DIR_BACK;  break; */
      /* case 3: dir = EVO_DIR_RIGHT; break; */
      /* case 4: dir = EVO_DIR_LEFT;  break; */
      case 0: default: dir = -1; break;        /* released / idle */
   }
   if (dir == held) return;
   if (held >= 0) feed_push(EVO_K_DIR, 0, (unsigned char)held, 0);   /* release the old */
   if (dir  >= 0) feed_push(EVO_K_DIR, 0, (unsigned char)dir, 1);    /* press the new */
   held = dir;
}

static void decode_notif(uint32_t requestID, const int32_t* d, int nd) {
   unsigned attr = requestID & 0xFFFF;
   if (g_log) {   /* always log so the spin-test codes are visible with this running */
      fprintf(g_log, "NOTIF attr=0x%x n=%d [", attr, nd);
      for (int i=0;i<nd && i<16;i++) fprintf(g_log, "%s%d", i?",":"", d[i]);
      fprintf(g_log, "]\n"); fflush(g_log);
   }
   if (attr == ATTR_ROTARY && nd > ROTARY_FIELD) {
      static int last = -1; int cur = d[ROTARY_FIELD], delta;
      if (ROTARY_ABS) { if (last < 0) { last = cur; return; }
         delta = cur - last; last = cur;
         if (delta >  ROTARY_WRAP/2) delta -= ROTARY_WRAP;
         if (delta < -ROTARY_WRAP/2) delta += ROTARY_WRAP;
      } else delta = cur;
      delta *= ROTARY_SIGN;
      while (delta >  127) { feed_push(EVO_K_ROTARY, 0, (unsigned char)127, 0); delta -= 127; }
      while (delta < -127) { feed_push(EVO_K_ROTARY, 0, (unsigned char)(signed char)-127, 0); delta += 127; }
      if (delta) feed_push(EVO_K_ROTARY, 0, (unsigned char)(signed char)delta, 0);
      return;
   }
   if (attr == ATTR_DIR && nd > DIR_FIELD) { dir_decode(d[DIR_FIELD]); return; }
   if (attr == ATTR_CENTER && nd > CENTER_FIELD) {
      feed_push(EVO_K_CENTER, 0, 0, d[CENTER_FIELD] ? 1 : 0);
      return;
   }
}

/* -------- DSI transport (identical to evo_dsitcp) -------- */
static int send_all(int fd, const void* b, int n){ const char* p=b; int l=n;
   while(l>0){ int w=send(fd,p,l,0); if(w<=0) return -1; p+=w; l-=w; } return n; }
static int dsi_send(int fd, uint32_t cmd, const void* body, int blen){
   unsigned char m[128]; struct MsgHeader* h=(struct MsgHeader*)m; memset(h,0,sizeof(*h));
   h->type=DSI_MAGIC; h->protoMajor=4; h->protoMinor=0; h->serverID=g_server; h->clientID=g_client;
   h->cmd=cmd; h->packetLength=blen; if(blen&&body) memcpy(m+sizeof(*h),body,blen);
   return send_all(fd,m,sizeof(*h)+blen); }

struct sbuf { unsigned char d[65536]; int len; };
static void process_stream(struct sbuf* s){
   int off=0;
   while (s->len-off >= 8){
      const unsigned char* p=s->d+off; int32_t type; memcpy(&type,p,4);
      if (type!=DSI_MAGIC){ off+=8; continue; }                 /* 8B transport reply */
      if (s->len-off < (int)sizeof(struct MsgHeader)) break;
      const struct MsgHeader* h=(const struct MsgHeader*)p; int plen=h->packetLength;
      if (plen<0||plen>60000){ off+=1; continue; }
      int total=(int)sizeof(struct MsgHeader)+plen;
      if (s->len-off < total) break;
      if (h->cmd==CMD_DATARESP && plen>=(int)sizeof(struct EventInfo)){
         const struct EventInfo* ei=(const struct EventInfo*)(p+sizeof(*h));
         const int32_t* dd=(const int32_t*)((const unsigned char*)ei+sizeof(*ei));
         int nd=(plen-sizeof(*ei))/4;
         if (ei->rtype==0x0202) decode_notif(ei->requestID, dd, nd);   /* RESULT_DATA_OK */
      }
      off+=total;
   }
   if (off>0){ memmove(s->d,s->d+off,s->len-off); s->len-=off; }
}
static int feed_stream(int fd, struct sbuf* s){
   if (s->len>=(int)sizeof(s->d)) s->len=0;
   int n=recv(fd,s->d+s->len,sizeof(s->d)-s->len,0);
   if (n<=0) return -1; s->len+=n; process_stream(s); return n;
}

int main(int argc, char** argv){
   if (argc < 2){ printf("usage: evo_ctrlfeed <Service.Interface> [major] [attrlo] [attrhi]\n"); return 1; }
   const char* iface = argv[1];
   int major = (argc>2)?atoi(argv[2]):0;
   int alo   = (argc>3)?atoi(argv[3]):0;
   int ahi   = (argc>4)?atoi(argv[4]):0x40;

   g_log = fopen("/dev/shmem/evo_ctrlfeed.log", "w");
   if (feed_init() < 0) return 2;
   printf("feed %s ready (magic set)\n", EVO_FEED_PATH);

   int sb = SBOpen("/srv/servicebroker");
   if (sb<0){ printf("SBOpen failed\n"); return 3; }
   struct STCPConnectionInfo ci; int rc=-1,m;
   if (major>0){ memset(&ci,0,sizeof ci); rc=SBAttachInterfaceTCP(sb,iface,major,0,&ci); }
   else for(m=1;m<=20;m++){ memset(&ci,0,sizeof ci); rc=SBAttachInterfaceTCP(sb,iface,m,0,&ci); if(rc==0){major=m;break;} }
   if (rc!=0){ printf("SBAttachInterfaceTCP(%s) failed rc=%d\n",iface,rc); SBClose(sb); return 4; }
   g_server=ci.serverID; g_client=ci.clientID;
   printf("attached %s v%d: %u.%u.%u.%u:%u sid=0x%08x cid=0x%08x\n", iface, major,
          (ci.socket.ipaddress>>24)&0xff,(ci.socket.ipaddress>>16)&0xff,(ci.socket.ipaddress>>8)&0xff,
          ci.socket.ipaddress&0xff,(unsigned)ci.socket.port,(unsigned)g_server.s.localID,(unsigned)g_client.s.localID);

   int lfd=socket(AF_INET,SOCK_STREAM,0); int one=1;
   setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
   struct sockaddr_in la; memset(&la,0,sizeof la);
   la.sin_family=AF_INET; la.sin_addr.s_addr=htonl(INADDR_ANY); la.sin_port=0;
   if (bind(lfd,(struct sockaddr*)&la,sizeof la)<0){ perror("bind"); return 5; }
   listen(lfd,4);
   struct sockaddr_in sn; socklen_t snl=sizeof sn; getsockname(lfd,(struct sockaddr*)&sn,&snl);
   int myport=ntohs(sn.sin_port);

   int ffd=socket(AF_INET,SOCK_STREAM,0);
   struct sockaddr_in sa; memset(&sa,0,sizeof sa);
   sa.sin_family=AF_INET; sa.sin_port=htons((uint16_t)ci.socket.port);
   sa.sin_addr.s_addr=htonl(ci.socket.ipaddress);
   if (connect(ffd,(struct sockaddr*)&sa,sizeof sa)<0){ perror("connect"); return 6; }
   setsockopt(ffd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);

   struct Endpoint ep; ep.ip=g_myip; ep.port=(uint32_t)myport;
   dsi_send(ffd,CMD_CONNREQ,&ep,sizeof ep);
   for (int a=alo;a<=ahi;a++){ struct EventInfo ei; memset(&ei,0,sizeof ei);
      ei.rtype=RT_REQUEST_NOTIFY; ei.requestID=0xC0000000u|(uint32_t)a; dsi_send(ffd,CMD_DATAREQ,&ei,sizeof ei); }
   printf("subscribed attrs 0x%x..0x%x; feeding DOOM. (knob events -> %s)\n", alo, ahi, EVO_FEED_PATH);

   int rfd=-1; static struct sbuf fbuf,rbuf;
   for(;;){
      fd_set rs; FD_ZERO(&rs); FD_SET(lfd,&rs); FD_SET(ffd,&rs); int mx=lfd>ffd?lfd:ffd;
      if(rfd>=0){ FD_SET(rfd,&rs); if(rfd>mx)mx=rfd; }
      struct timeval tv; tv.tv_sec=1; tv.tv_usec=0;
      if (select(mx+1,&rs,NULL,NULL,&tv)>0){
         if (FD_ISSET(lfd,&rs)){ struct sockaddr_in ra; socklen_t ral=sizeof ra;
            int nf=accept(lfd,(struct sockaddr*)&ra,&ral);
            if(nf>=0){ rfd=nf; setsockopt(rfd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
               printf(">>> reverse conn from %s\n", inet_ntoa(ra.sin_addr)); fflush(stdout); } }
         if (FD_ISSET(ffd,&rs)){ if(feed_stream(ffd,&fbuf)<0){ printf("forward closed\n"); break; } }
         if (rfd>=0 && FD_ISSET(rfd,&rs)){ if(feed_stream(rfd,&rbuf)<0){ printf("reverse closed\n"); rfd=-1; } }
      }
   }
   SBDetachInterface(sb,g_client);
   if(rfd>=0) close(rfd); close(ffd); close(lfd); SBClose(sb);
   if(g_log) fclose(g_log);
   return 0;
}
