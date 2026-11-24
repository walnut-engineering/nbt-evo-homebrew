/* evo_gps - a full-screen GPS/position status app for the NBT EVO ID5 head unit.
 *
 * A homebrew launcher tile.  Shows live GPS/position status read from the nav
 * engine's positioning service over the QNX-native DSI transport (same path as
 * the nbt-dsi-toolkit evo_dsiqnx / evo_navctl):
 *
 *   SBAttachInterface("PosReal.DNAVIPos", v33) -> server channel (NavigationPositioning)
 *   ConnReq -> data channels -> subscribe REQUEST_NOTIFY to attr 0..0x20
 *   NOTIFY (cmd=DataResp) carries the current position; attr 0x0 holds
 *     field[3] = longitude, field[4] = latitude  (NDS units = deg * 2^32/360)
 *
 * (PosReal = real GPS; PosDemo can spoof it via evo_navctl for a bench demo with
 * no antenna.)  Rendered with the shared evo_graphics_lib + font8x8 straight to
 * the framebuffer (HMI hidden via presentation mode), iDrive BACK = exit (no
 * keyboard), exactly like evo_sysmon/evo_terminal.
 *
 * The raw NOTIFY arrays for attr 0/1 are also shown at the bottom so the
 * remaining fields (fix/sats/speed/heading/HDOP) can be identified against a real
 * fix once an antenna is connected.
 */
#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/neutrino.h>

#include "evo_graphics_lib.h"
#include "evo_context.h"
#include "evo_display.h"
#include "font8x8_basic.h"   /* -I../evo_terminal */
#include "evo_appinput.h"    /* -I../../shared/include : iDrive BACK = exit */
#include "dsi/private/servicebroker.h"   /* -I<nbt-dsi-toolkit>/dsi_sdk/include */

extern int  SBOpen(const char*);
extern void SBClose(int);
extern int  SBAttachInterface(int, const char*, int, int, struct SConnectionInfo*);
extern int  SBDetachInterface(int, SPartyID);

#define DISPLAY_W 1440
#define DISPLAY_H 540

/* colours 0xAARRGGBB (framebuffer BGRX; greys swap-safe) */
#define C_BG     0xFF0A0A0CU
#define C_PANEL  0xFF14161CU
#define C_EDGE   0xFF2C2C32U
#define C_ACCENT 0xFF40A0E0U   /* GPS blue */
#define C_TXT    0xFFFFFFFFU
#define C_DIM    0xFFA0A0A8U
#define C_GREEN  0xFF46C86EU
#define C_AMBER  0xFFE0A020U
#define C_RED    0xFFE05050U

#define NDS_K (4294967296.0 / 360.0)   /* NDS integer units per degree = 2^32/360 */

/* ---------- framebuffer primitives (same as evo_sysmon/launcher) ---------- */
static inline void put(uint32_t *fb, int x, int y, uint32_t c) {
    if ((unsigned)x < DISPLAY_W && (unsigned)y < DISPLAY_H) fb[y * DISPLAY_W + x] = c;
}
static void fill(uint32_t *fb, int x, int y, int w, int h, uint32_t c) {
    int i, j; for (j = 0; j < h; j++) for (i = 0; i < w; i++) put(fb, x + i, y + j, c);
}
static void frame(uint32_t *fb, int x, int y, int w, int h, int t, uint32_t c) {
    fill(fb, x, y, w, t, c); fill(fb, x, y + h - t, w, t, c);
    fill(fb, x, y, t, h, c); fill(fb, x + w - t, y, t, h, c);
}
static void glyph(uint32_t *fb, int x, int y, char ch, int s, uint32_t c) {
    const char *g = font8x8_basic[(unsigned char)ch & 0x7f];
    int r, b, sy, sx;
    for (r = 0; r < 8; r++) { unsigned char bits = (unsigned char)g[r];
        for (b = 0; b < 8; b++) if (bits & (1u << b))
            for (sy = 0; sy < s; sy++) for (sx = 0; sx < s; sx++)
                put(fb, x + b * s + sx, y + r * s + sy, c);
    }
}
static void text(uint32_t *fb, int x, int y, const char *str, int s, uint32_t c) {
    for (; *str; str++) { glyph(fb, x, y, *str, s, c); x += 8 * s; }
}
static int textw(const char *s, int sc) { return (int)strlen(s) * 8 * sc; }

/* ---------- shared GPS state (data thread -> render thread) ---------- */
#define RAW_N 24
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_running = 1;
struct gps_state {
    int      have_fix;       /* got at least one position NOTIFY */
    double   lat, lon;       /* degrees */
    int32_t  raw0[RAW_N];    /* attr 0x0 payload (first RAW_N ints) */
    int      raw0_n;
    int32_t  raw1[RAW_N];    /* attr 0x1 payload */
    int      raw1_n;
    unsigned updates;        /* NOTIFY count */
    time_t   last;           /* wall time of last update */
};
static struct gps_state g_gps;

/* ---------- DSI positioning client (QNX-native, from evo_dsiqnx) ---------- */
enum { CMD_DATAREQ = 7, CMD_DATARESP = 8, CMD_CONNREQ = 9 };
enum { RT_REQUEST_NOTIFY = 0x0101 };
struct MsgHeader {
    int32_t type; uint16_t protoMajor; uint16_t protoMinor;
    SPartyID serverID; SPartyID clientID;
    uint32_t cmd; uint32_t flags; int32_t packetLength; uint32_t reserved;
};
struct EventInfo { uint32_t ifVersion; uint32_t rtype; uint32_t requestID; int32_t seq; };

static int dsi_send(int coid, uint32_t cmd, SPartyID srv, SPartyID cli,
                    const void *body, int blen, void *reply, int rlen) {
    unsigned char m[128]; struct MsgHeader *h = (struct MsgHeader *)m;
    memset(m, 0, sizeof m);
    h->type = 0; h->protoMajor = 4; h->protoMinor = 0;
    h->serverID = srv; h->clientID = cli; h->cmd = cmd; h->packetLength = blen;
    if (blen && body) memcpy(m + sizeof(*h), body, blen);
    return MsgSend(coid, m, sizeof(*h) + blen, reply, rlen);
}

/* connect to PosReal.DNAVIPos, subscribe, and pump NOTIFYs into g_gps until g_running=0. */
static void *gps_thread(void *arg) {
    (void)arg;
    while (g_running) {
        int sb = SBOpen("/srv/servicebroker");
        if (sb < 0) { sleep(2); continue; }
        struct SConnectionInfo ci; memset(&ci, 0, sizeof ci);
        if (SBAttachInterface(sb, "PosReal.DNAVIPos", 33, 0, &ci) != 0) { SBClose(sb); sleep(2); continue; }

        int mychid = ChannelCreate(0);
        int coid = ConnectAttach(ci.channel.nid, ci.channel.pid, ci.channel.chid, _NTO_SIDE_CHANNEL, 0);
        if (coid == -1) coid = ConnectAttach(ci.channel.nid, ci.channel.pid, ci.channel.chid, 0, 0);
        if (coid == -1) { ChannelDestroy(mychid); SBClose(sb); sleep(2); continue; }

        unsigned char msg[64]; memset(msg, 0, sizeof msg);
        struct MsgHeader *h = (struct MsgHeader *)msg;
        h->type = 0; h->protoMajor = 4; h->protoMinor = 0;
        h->serverID = ci.serverID; h->clientID = ci.clientID; h->cmd = CMD_CONNREQ; h->packetLength = 16;
        uint32_t *bd = (uint32_t *)(msg + sizeof(*h));
        bd[0] = (uint32_t)mychid; bd[1] = (uint32_t)mychid; bd[2] = (uint32_t)getpid(); bd[3] = 0;
        unsigned char reply[64]; memset(reply, 0, sizeof reply);
        if (MsgSend(coid, msg, sizeof(*h) + 16, reply, 16) == -1) {
            ConnectDetach(coid); ChannelDestroy(mychid); SBClose(sb); sleep(2); continue;
        }
        uint32_t *rb = (uint32_t *)reply;
        int dcoid = ConnectAttach(0, ci.channel.pid, (int)rb[0], _NTO_SIDE_CHANNEL, 0);

        for (int a = 0; a <= 0x20; a++) {
            struct EventInfo ei; memset(&ei, 0, sizeof ei);
            ei.rtype = RT_REQUEST_NOTIFY; ei.requestID = 0xC0000000u | (uint32_t)a;
            unsigned char rep[64];
            dsi_send(dcoid >= 0 ? dcoid : coid, CMD_DATAREQ, ci.serverID, ci.clientID, &ei, sizeof ei, rep, sizeof rep);
        }

        uint64_t to = 1ULL * 1000 * 1000 * 1000;   /* 1s receive timeout */
        while (g_running) {
            struct _msg_info info; memset(&info, 0, sizeof info);
            unsigned char rbuf[1024];
            TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, NULL, &to, NULL);
            int rcvid = MsgReceive(mychid, rbuf, sizeof rbuf, &info);
            if (rcvid == -1) { if (errno == ETIMEDOUT) continue; break; }
            if (rcvid == 0) continue;   /* pulse */
            struct MsgHeader *h2 = (struct MsgHeader *)rbuf;
            if (info.msglen >= (int)sizeof(struct MsgHeader) && h2->cmd == CMD_DATARESP) {
                struct EventInfo *ei = (struct EventInfo *)(rbuf + sizeof(*h2));
                int32_t *d = (int32_t *)(rbuf + sizeof(*h2) + sizeof(*ei));
                int nd = (h2->packetLength - (int)sizeof(*ei)) / 4;
                int attr = (int)(ei->requestID & 0xFF);
                pthread_mutex_lock(&g_lock);
                if (attr == 0x0 && nd >= 5) {
                    g_gps.lon = (double)d[3] / NDS_K;
                    g_gps.lat = (double)d[4] / NDS_K;
                    g_gps.have_fix = 1;
                    g_gps.raw0_n = nd < RAW_N ? nd : RAW_N;
                    for (int i = 0; i < g_gps.raw0_n; i++) g_gps.raw0[i] = d[i];
                } else if (attr == 0x1) {
                    g_gps.raw1_n = nd < RAW_N ? nd : RAW_N;
                    for (int i = 0; i < g_gps.raw1_n; i++) g_gps.raw1[i] = d[i];
                }
                g_gps.updates++; g_gps.last = time(NULL);
                pthread_mutex_unlock(&g_lock);
            }
            MsgReply(rcvid, 0, NULL, 0);
        }
        if (dcoid >= 0) ConnectDetach(dcoid);
        ConnectDetach(coid); ChannelDestroy(mychid);
        SBDetachInterface(sb, ci.clientID); SBClose(sb);
    }
    return NULL;
}

/* ---------- render ---------- */
static void kv(uint32_t *fb, int x, int y, const char *k, const char *v, uint32_t vc) {
    text(fb, x, y, k, 2, C_DIM);
    text(fb, x + 230, y, v, 3, vc);
}
static void render(uint32_t *fb, const struct gps_state *s) {
    int i;
    for (i = 0; i < DISPLAY_W * DISPLAY_H; i++) fb[i] = C_BG;

    /* header */
    fill(fb, 0, 0, DISPLAY_W, 6, C_ACCENT);
    text(fb, 40, 26, "GPS", 6, C_TXT);
    int live = s->have_fix && (time(NULL) - s->last) <= 3;
    const char *st = s->have_fix ? (live ? "POSITION LIVE" : "STALE") : "ACQUIRING...";
    text(fb, 250, 42, st, 3, s->have_fix ? (live ? C_GREEN : C_AMBER) : C_AMBER);
    char u[48]; snprintf(u, sizeof u, "updates: %u", s->updates);
    text(fb, DISPLAY_W - textw(u, 2) - 40, 48, u, 2, C_DIM);

    /* main panel: lat / lon */
    int px = 40, py = 120, pw = DISPLAY_W - 80, ph = 210;
    fill(fb, px, py, pw, ph, C_PANEL); frame(fb, px, py, pw, ph, 2, C_EDGE);
    char buf[64];
    if (s->have_fix) {
        snprintf(buf, sizeof buf, "%.6f %c", s->lat < 0 ? -s->lat : s->lat, s->lat < 0 ? 'S' : 'N');
        kv(fb, px + 30, py + 30,  "LATITUDE", buf, C_TXT);
        snprintf(buf, sizeof buf, "%.6f %c", s->lon < 0 ? -s->lon : s->lon, s->lon < 0 ? 'W' : 'E');
        kv(fb, px + 30, py + 90,  "LONGITUDE", buf, C_TXT);
    } else {
        kv(fb, px + 30, py + 30,  "LATITUDE", "--", C_DIM);
        kv(fb, px + 30, py + 90,  "LONGITUDE", "--", C_DIM);
    }
    /* raw NDS (so a real fix can be cross-checked) */
    snprintf(buf, sizeof buf, "NDS lat=%d lon=%d", s->raw0_n > 4 ? s->raw0[4] : 0, s->raw0_n > 3 ? s->raw0[3] : 0);
    text(fb, px + 30, py + 150, buf, 2, C_DIM);

    /* raw NOTIFY dumps (field-ID aid until we label fix/sats/speed/heading) */
    int ry = 360;
    text(fb, 40, ry, "RAW attr0:", 2, C_ACCENT);
    char line[256]; line[0] = 0;
    for (i = 0; i < s->raw0_n; i++) { char t[16]; snprintf(t, sizeof t, "%d ", s->raw0[i]); if (strlen(line) + strlen(t) < sizeof line - 1) strcat(line, t); }
    text(fb, 220, ry, line, 1, C_DIM);
    text(fb, 40, ry + 40, "RAW attr1:", 2, C_ACCENT);
    line[0] = 0;
    for (i = 0; i < s->raw1_n; i++) { char t[16]; snprintf(t, sizeof t, "%d ", s->raw1[i]); if (strlen(line) + strlen(t) < sizeof line - 1) strcat(line, t); }
    text(fb, 220, ry + 40, line, 1, C_DIM);

    text(fb, 40, DISPLAY_H - 26, "PosReal.DNAVIPos v33  -  iDrive BACK = exit", 2, C_DIM);
}

static void on_sig(int s) { (void)s; g_running = 0; }

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
    memset(&g_gps, 0, sizeof g_gps);

    printf("[GPS] starting\n"); fflush(stdout);
    evo_graphics_init();
    evo_context_t *ctx = evo_context_create(DISPLAY_W, DISPLAY_H, 0, 0);
    if (!ctx) { printf("[GPS] context create failed\n"); return 1; }
    if (evo_context_show(ctx) != 0) { printf("[GPS] show failed\n"); evo_context_destroy(ctx); return 1; }
    { evo_display_t *d = evo_context_get_display(ctx);
      if (d && evo_display_enable_presentation_mode(d) == 0) printf("[GPS] presentation mode (HMI hidden)\n"); }
    if (evo_ai_start(&g_running) == 0) printf("[GPS] iDrive BACK = exit\n");
    fflush(stdout);

    pthread_t tid; pthread_create(&tid, NULL, gps_thread, NULL);

    while (g_running) {
        struct gps_state cur;
        pthread_mutex_lock(&g_lock); cur = g_gps; pthread_mutex_unlock(&g_lock);
        uint32_t *fb = evo_context_get_framebuffer(ctx);
        if (fb) { render(fb, &cur); evo_context_present(ctx); }
        usleep(250000);   /* 4 Hz */
    }

    printf("[GPS] shutting down\n"); fflush(stdout);
    { evo_display_t *d = evo_context_get_display(ctx); if (d) evo_display_disable_presentation_mode(d); }
    evo_context_destroy(ctx);
    return 0;
}
