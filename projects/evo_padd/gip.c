/* gip.c - GIP (Game Input Protocol) for Xbox One controllers.
 *
 * A C port of xow's controller/gip.cpp and the protocol half of controller.cpp:
 *   Copyright (C) 2019 Medusalix
 *   This program is free software; you can redistribute it and/or modify it under the
 *   terms of the GNU General Public License as published by the Free Software
 *   Foundation; either version 2 of the License, or (at your option) any later version.
 *
 * Frame header, 4 bytes: command, (type << 4) | deviceId, sequence, length.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>

#include "gip.h"
#include "mt76.h"
#include "padd.h"

enum {
    CMD_ACKNOWLEDGE = 0x01, CMD_ANNOUNCE = 0x02, CMD_STATUS = 0x03, CMD_IDENTIFY = 0x04,
    CMD_POWER_MODE = 0x05, CMD_AUTHENTICATE = 0x06, CMD_GUIDE_BTN = 0x07, CMD_AUDIO_CONFIG = 0x08,
    CMD_RUMBLE = 0x09, CMD_LED_MODE = 0x0a, CMD_SERIAL_NUM = 0x1e, CMD_INPUT = 0x20,
};
/* command: no response; request: responds with data; request|ack: responds with ack + data */
enum { TYPE_COMMAND = 0x00, TYPE_ACK = 0x01, TYPE_REQUEST = 0x02 };
enum { POWER_ON = 0x00, POWER_SLEEP = 0x01, POWER_OFF = 0x04 };
enum { LED_OFF = 0x00, LED_ON = 0x01 };
enum { BATT_TYPE_CHARGING = 0x00 };

#define FRAME_LEN       4
#define ANNOUNCE_LEN    28      /* mac[6], u16, vendor, product, fw version 4 x u16, hw version 4 x u16 */
#define STATUS_LEN      4
#define GUIDE_LEN       2
#define SERIAL_LEN      16      /* u16, serial[14] */
#define INPUT_LEN       14      /* buttons u16, triggers 2 x u16, sticks 4 x i16 */

#define DEVICE_ID_CONTROLLER 0  /* accessories use IDs greater than zero */

void gip_open(gip_dev_t *g, int wcid, const uint8_t addr[6], int slot)
{
    memset(g, 0, sizeof *g);
    g->wcid = wcid;
    memcpy(g->addr, addr, 6);
    g->sequence = 0x01;
    g->battery = -1;
    g->slot = slot;
}

static uint8_t next_sequence(gip_dev_t *g)
{
    if (g->sequence == 0x00) g->sequence = 0x01;        /* zero is an invalid sequence number */
    return g->sequence++;
}

static int send(gip_dev_t *g, const uint8_t *d, int n)
{
    return mt76_send_client_packet(g->wcid, g->addr, d, n);
}

static int set_power_mode(gip_dev_t *g, uint8_t id, uint8_t mode)
{
    uint8_t o[FRAME_LEN + 1] = { CMD_POWER_MODE, (uint8_t)((TYPE_REQUEST << 4) | (id & 0x0f)), 0, 1, mode };
    o[2] = next_sequence(g);
    return send(g, o, sizeof o);
}

static int set_led_mode(gip_dev_t *g, uint8_t mode, uint8_t brightness)
{
    /* LedModeData: unknown, mode, brightness (0x00..0x20) */
    uint8_t o[FRAME_LEN + 3] = { CMD_LED_MODE, TYPE_REQUEST << 4, 0, 3, 0x00, mode, brightness };
    o[2] = next_sequence(g);
    return send(g, o, sizeof o);
}

static int request_serial_number(gip_dev_t *g)
{
    uint8_t o[FRAME_LEN + 1] = { CMD_SERIAL_NUM, (TYPE_REQUEST | TYPE_ACK) << 4, 0, 1, 0x04 };
    o[2] = next_sequence(g);
    return send(g, o, sizeof o);
}

/* ACK: header (ACKNOWLEDGE, request, same sequence, length 9), a zero byte, the frame
 * being acknowledged (type = request, sequence = its length, length = 0), 4 zero bytes */
static int acknowledge(gip_dev_t *g, const uint8_t *frame)
{
    uint8_t o[13];
    memset(o, 0, sizeof o);
    o[0] = CMD_ACKNOWLEDGE;
    o[1] = (uint8_t)((TYPE_REQUEST << 4) | (frame[1] & 0x0f));
    o[2] = frame[2];
    o[3] = FRAME_LEN + 5;
    o[5] = frame[0];
    o[6] = (uint8_t)((TYPE_REQUEST << 4) | (frame[1] & 0x0f));
    o[7] = frame[3];
    o[8] = 0;
    return send(g, o, sizeof o);
}

static void device_announced(gip_dev_t *g, const uint8_t *p)
{
    g->vid = le16(p + 8);
    g->pid = le16(p + 10);
    LOGI("pad %d: announced, product %04x:%04x, firmware %d.%d.%d.%d, hardware %d.%d.%d.%d", g->wcid, g->vid, g->pid,
         le16(p + 12), le16(p + 14), le16(p + 16), le16(p + 18), le16(p + 20), le16(p + 22), le16(p + 24), le16(p + 26));

    /* initial power mode, LED dimmed a little like the original driver, serial number */
    if (!set_power_mode(g, DEVICE_ID_CONTROLLER, POWER_ON)) { LOGE("pad %d: failed to set initial power mode", g->wcid); return; }
    if (!set_led_mode(g, LED_ON, 0x14))                     { LOGE("pad %d: failed to set initial LED mode", g->wcid); return; }
    if (!request_serial_number(g))                          { LOGE("pad %d: failed to request serial number", g->wcid); return; }
    g->announced = 1;
    if (g->slot >= 0) padout_connected(g->slot, 1, g->vid, g->pid);
}

int gip_handle_packet(gip_dev_t *g, const uint8_t *d, int n)
{
    static const char *levels[] = { "empty", "low", "medium", "full" };
    uint8_t command, device_id, type, length;
    const uint8_t *p;
    int plen;

    if (n < FRAME_LEN) return 1;                        /* ignore invalid packets */
    command = d[0]; device_id = d[1] & 0x0f; type = d[1] >> 4; length = d[3];

    if ((type & TYPE_ACK) && !acknowledge(g, d)) { LOGE("pad %d: failed to acknowledge packet", g->wcid); return 0; }
    if (device_id > 0) return 1;                        /* ignore packets from accessories */

    p = d + FRAME_LEN; plen = n - FRAME_LEN;            /* data is 32-bit aligned: check minimum sizes */

    if (command == CMD_ANNOUNCE && length == ANNOUNCE_LEN && plen >= ANNOUNCE_LEN) {
        device_announced(g, p);
    } else if (command == CMD_STATUS && length == STATUS_LEN && plen >= STATUS_LEN) {
        int level = p[0] & 0x03, btype = (p[0] >> 2) & 0x03;
        if (btype != BATT_TYPE_CHARGING && level != g->battery) {
            LOGI("pad %d: battery %s", g->wcid, levels[level]);
            g->battery = level;
            if (g->slot >= 0) padout_battery(g->slot, level);
        }
    } else if (command == CMD_GUIDE_BTN && length == GUIDE_LEN && plen >= GUIDE_LEN) {
        if (p[0]) g->st.buttons |= PAD_GUIDE; else g->st.buttons &= (uint16_t)~PAD_GUIDE;
        if (g->slot >= 0) padout_state(g->slot, &g->st);
    } else if (command == CMD_SERIAL_NUM && length == SERIAL_LEN && plen >= SERIAL_LEN) {
        char serial[15];
        memcpy(serial, p + 2, 14); serial[14] = 0;
        LOGI("pad %d: serial number %s", g->wcid, serial);
    } else if (command == CMD_INPUT && length >= INPUT_LEN && plen >= INPUT_LEN) {
        /* Elite controllers send a larger report; the standard part comes first */
        g->st.buttons = (uint16_t)((le16(p) & 0xfffc) | (g->st.buttons & PAD_GUIDE));
        g->st.lt = le16(p + 2);
        g->st.rt = le16(p + 4);
        g->st.lx = (int16_t)le16(p + 6);
        g->st.ly = (int16_t)le16(p + 8);
        g->st.rx = (int16_t)le16(p + 10);
        g->st.ry = (int16_t)le16(p + 12);
        g->inputs++;
        if (g->slot >= 0) padout_state(g->slot, &g->st);
    } else if (g_verbose >= 2) {
        char hex[3 * 48 + 4];
        hex_str(d, n > 48 ? 48 : n, hex, sizeof hex);
        LOGD("pad %d: unhandled GIP cmd 0x%02x type %x seq %d len %d: %s", g->wcid, command, type, d[2], length, hex);
    }
    return 1;
}

void gip_power_off(gip_dev_t *g)
{
    if (!set_power_mode(g, DEVICE_ID_CONTROLLER, POWER_OFF)) LOGE("pad %d: failed to turn off controller", g->wcid);
}
