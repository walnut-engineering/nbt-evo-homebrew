/* gip.h - GIP (Game Input Protocol), the controller side of the link.
 * A C port of xow's controller/gip.{h,cpp} + the parts of controller.cpp that are
 * protocol, not Linux input (Copyright (C) 2019 Medusalix).
 *
 * Handshake:  <- Announce   -> Power mode: on   -> LED mode: dim   -> Serial number request
 * then the controller streams Input reports (and Status / Guide-button messages).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EVO_PADD_GIP_H
#define EVO_PADD_GIP_H

#include <stdint.h>
#include "padd.h"

typedef struct gip_dev {
    int         wcid;           /* 1..16 */
    uint8_t     addr[6];
    uint8_t     sequence;
    int         announced;
    uint16_t    vid, pid;
    int         battery;        /* 0..3, -1 = unknown */
    int         slot;           /* pad slot 0..PAD_MAX-1, -1 = not exposed */
    pad_state_t st;
    unsigned long inputs;       /* input reports received */
} gip_dev_t;

void gip_open(gip_dev_t *g, int wcid, const uint8_t addr[6], int slot);
int  gip_handle_packet(gip_dev_t *g, const uint8_t *d, int n);      /* 0 = failed to answer */
void gip_power_off(gip_dev_t *g);

#endif
