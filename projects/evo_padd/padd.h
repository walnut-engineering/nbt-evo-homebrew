/* padd.h - shared bits of evo_padd: logging, clock, the pad state every layer passes on.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EVO_PADD_H
#define EVO_PADD_H

#include <stdint.h>

/* ---- logging (evo_padd.c) ---------------------------------------------------- */
extern int g_verbose;
void plog(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define LOGE(...) plog(0, __VA_ARGS__)
#define LOGI(...) plog(1, __VA_ARGS__)
#define LOGD(...) plog(2, __VA_ARGS__)      /* only with -v  */
#define LOGT(...) plog(3, __VA_ARGS__)      /* only with -vv */

uint64_t    now_ms(void);
const char *mac_str(const uint8_t *addr, char buf[18]);
void        hex_str(const uint8_t *d, int n, char *out, int outsz);

/* ---- little-endian packing: no struct casts onto packet bytes (strict-alignment ARM) */
static inline uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* ---- pad state ----------------------------------------------------------------
 * Button bits 2..15 are the controller's own input-report bits; bit 0 is ours
 * (the Xbox button arrives in a separate message). */
#define PAD_GUIDE   0x0001
#define PAD_MENU    0x0004      /* "start" */
#define PAD_VIEW    0x0008      /* "select"/back */
#define PAD_A       0x0010
#define PAD_B       0x0020
#define PAD_X       0x0040
#define PAD_Y       0x0080
#define PAD_UP      0x0100
#define PAD_DOWN    0x0200
#define PAD_LEFT    0x0400
#define PAD_RIGHT   0x0800
#define PAD_LB      0x1000
#define PAD_RB      0x2000
#define PAD_LS      0x4000
#define PAD_RS      0x8000

#define PAD_MAX     4           /* pads we expose (the radio itself allows 16) */

typedef struct {
    uint16_t buttons;           /* PAD_* */
    uint16_t lt, rt;            /* triggers, 0..1023 */
    int16_t  lx, ly, rx, ry;    /* sticks, -32768..32767, up/right positive */
} pad_state_t;

/* ---- output layer (padout.c) -------------------------------------------------- */
void padout_use_feed(int on);       /* 0 = pad-state block only, no knob events */
void padout_init(void);
void padout_tick(void);             /* heartbeat + key repeat; call every ~20 ms */
void padout_connected(int slot, int connected, uint16_t vid, uint16_t pid);
void padout_battery(int slot, int level);
void padout_state(int slot, const pad_state_t *st);
void padout_shutdown(void);

#endif
