/* evo_e5.h - decoder for the iDrive controller telegrams (MOST FBlock 0xE5) as
 * they arrive on OMAP /dev/ipc/ipc25 inside `most` (the V850/IOC MOST-subnet
 * stream; CAN on the car side, MOST logical addressing on this side).
 * Shared by evo_tee.c (LD_PRELOAD in `most`) and the host unit test e5_test.c.
 *
 * Telegram (payload after the 40-byte io-ipc header), decoded on the bench
 * 2026-11-06 (see re_knowledge / memory evo-idrive-controller):
 *   e5 01 <FktHi> 0c  0c 02 00  e5 01 <len> 00 00 00  01 1d <seq> <d0> <d1> <d2> <d3>
 *   FktHi 0x10 (Fkt 0x100) ROTARY : d0 d1 = 16-bit position, centre 0x8000, +1/detent clockwise
 *   FktHi 0x20 (Fkt 0x200) BUTTON : d0 = state (01 down / 00 up), d1 = tilt direction,
 *                                   d2 d3 = id: 1e01 centre push, 1d01 joystick tilt
 *                                   (d1: 01 forward 02 right 04 back 08 left, bench-confirmed 2026-11-06;
 *                                   00 on release; d0=02 = long hold after ~2 s),
 *                                   00xx ring buttons: 01 MENU 02 BACK 04 OPTION 08 MEDIA
 *                                   10 NAV 20 TEL 40 COM.
 *   e5 0f ... = controller heartbeat (every 2 s), FktHi 0x30 = misc counter: ignored.
 * <seq> is a per-message sequence number (NOT a detent counter). Several telegrams
 * may share one read(), so the scanner walks the whole buffer.
 */
#ifndef EVO_E5_H
#define EVO_E5_H
#include "evo_feed.h"

struct e5_state { int have_pos, pos, tilt_dir; };
typedef void (*e5_emit_fn)(void *ctx, unsigned char kind, unsigned char ctr, const unsigned char *b6);

/* Scan one read buffer; emit() is called per decoded event. Returns telegrams seen. */
static int e5_scan(struct e5_state *st, const unsigned char *p, int n, e5_emit_fn emit, void *ctx) {
    int i, found = 0;
    for (i = 0; i + 20 <= n; i++) {
        unsigned char b[6], seq, fkt;
        if (!(p[i] == 0xe5 && p[i+1] == 0x01 && p[i+3] == 0x0c && p[i+9] == 0x07 &&
              p[i+13] == 0x01 && p[i+14] == 0x1d)) continue;
        fkt = p[i+2]; seq = p[i+15];
        b[0] = b[1] = b[2] = b[3] = b[4] = b[5] = 0;
        if (fkt == 0x10) {                                   /* rotary: absolute position */
            int pos = ((p[i+16] << 8) | p[i+17]) - 0x8000, d = st->have_pos ? pos - st->pos : 0;
            st->pos = pos; st->have_pos = 1;
            if (d > 127) d = 127;
            if (d < -127) d = -127;
            b[0] = (unsigned char)(signed char)d; b[1] = p[i+16]; b[2] = p[i+17];
            if (d) emit(ctx, EVO_K_ROTARY, seq, b);
        } else if (fkt == 0x20) {                            /* button / tilt / push */
            int state = p[i+16] ? 1 : 0, x = p[i+17], id = (p[i+18] << 8) | p[i+19];
            if (id == 0x1e01) {
                b[1] = (unsigned char)state;
                emit(ctx, EVO_K_CENTER, seq, b);
            } else if (id == 0x1d01) {
                int dir = -1;
                if (state) {
                    dir = x == 0x01 ? EVO_DIR_FWD : x == 0x04 ? EVO_DIR_BACK :
                          x == 0x08 ? EVO_DIR_LEFT : x == 0x02 ? EVO_DIR_RIGHT : -1;
                    if (dir >= 0) st->tilt_dir = dir;
                } else { dir = st->tilt_dir; st->tilt_dir = -1; }   /* release = last tilt */
                if (dir >= 0) { b[0] = (unsigned char)dir; b[1] = (unsigned char)state; b[2] = (unsigned char)x;
                                emit(ctx, EVO_K_DIR, seq, b); }
            } else {
                b[0] = (unsigned char)id; b[1] = (unsigned char)state; b[2] = p[i+18];
                emit(ctx, EVO_K_BUTTON, seq, b);
            }
        } else continue;                                     /* 0x30 etc: not an input */
        found++; i += 19;
    }
    return found;
}

/* ---- HMI-side masking: rewrite telegrams IN PLACE in the buffer most/HMI will parse.
 * While `grabbed`: rotary movement is absorbed into an offset (HMI position frozen,
 * and continuous afterwards: no jump when the grab ends); button/tilt/push downs become
 * a neutral "id 0000 up"; an up whose down the HMI never saw is neutralised too, and an
 * up whose down the HMI did see is always passed (nothing can stick). Lengths and
 * sequence numbers are untouched. */
struct e5_mask { int have_pos, last_real, offset; unsigned short seen[8], hidden[8]; int nseen, nhidden; };

static int e5_list_find(const unsigned short *l, int n, unsigned short id) {
    int k;
    for (k = 0; k < n; k++) if (l[k] == id) return k;
    return -1;
}
static void e5_list_add(unsigned short *l, int *n, unsigned short id) {
    if (*n < 8 && e5_list_find(l, *n, id) < 0) l[(*n)++] = id;
}
static void e5_list_del(unsigned short *l, int *n, int k) { l[k] = l[--(*n)]; }

static int e5_mask(struct e5_mask *m, unsigned char *p, int n, int grabbed) {
    int i, masked = 0;
    for (i = 0; i + 20 <= n; i++) {
        if (!(p[i] == 0xe5 && p[i+1] == 0x01 && p[i+3] == 0x0c && p[i+9] == 0x07 &&
              p[i+13] == 0x01 && p[i+14] == 0x1d)) continue;
        if (p[i+2] == 0x10) {
            int real = ((p[i+16] << 8) | p[i+17]) - 0x8000, v;
            if (!m->have_pos) { m->have_pos = 1; m->last_real = real; }
            if (grabbed && real != m->last_real) { m->offset += real - m->last_real; masked++; }
            m->last_real = real;
            v = (real - m->offset + 0x8000) & 0xffff;
            p[i+16] = (unsigned char)(v >> 8); p[i+17] = (unsigned char)v;
        } else if (p[i+2] == 0x20) {
            unsigned short id = (unsigned short)((p[i+18] << 8) | p[i+19]);
            int ks = e5_list_find(m->seen, m->nseen, id), kh = e5_list_find(m->hidden, m->nhidden, id);
            int neutral = 0;
            if (p[i+16]) {                                   /* down */
                if (grabbed) { neutral = 1; e5_list_add(m->hidden, &m->nhidden, id); }
                else e5_list_add(m->seen, &m->nseen, id);
            } else {                                         /* up */
                if (ks >= 0) e5_list_del(m->seen, &m->nseen, ks);          /* HMI saw the down: pass */
                else if (kh >= 0) { e5_list_del(m->hidden, &m->nhidden, kh); neutral = 1; }
                else if (grabbed) neutral = 1;
            }
            if (neutral) { p[i+16] = 0; p[i+17] = 0; p[i+18] = 0; p[i+19] = 0; masked++; }
        } else continue;
        i += 19;
    }
    return masked;
}
#endif
