/* dongle.c - receive path of the Xbox wireless adapter.
 *
 * A C port of xow's dongle/dongle.cpp:
 *   Copyright (C) 2019 Medusalix
 *   This program is free software; you can redistribute it and/or modify it under the
 *   terms of the GNU General Public License as published by the Free Software
 *   Foundation; either version 2 of the License, or (at your option) any later version.
 *
 * A bulk IN transfer is: RxInfo word, payload, the same word again as an end marker.
 * Command-port payloads are MCU events; WLAN payloads are RxWi (32 bytes) + an 802.11
 * frame. Controllers associate like Wi-Fi stations and then talk GIP in QoS data frames.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>
#include <pthread.h>

#include "dongle.h"
#include "mt76.h"
#include "gip.h"
#include "padd.h"

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static gip_dev_t       g_ctl[MT_WCID_COUNT];
static int             g_used[MT_WCID_COUNT];
static int             g_slot_wcid[PAD_MAX];        /* pad slot -> wcid, 0 = free */

int dongle_connected(void)
{
    int i, n = 0;
    for (i = 0; i < MT_WCID_COUNT; i++) n += g_used[i];
    return n;
}

static int find_by_addr(const uint8_t *addr)
{
    int i;
    for (i = 0; i < MT_WCID_COUNT; i++)
        if (g_used[i] && memcmp(g_ctl[i].addr, addr, 6) == 0) return i + 1;
    return 0;
}

static void drop_locked(int wcid, int power_off)
{
    gip_dev_t *g = &g_ctl[wcid - 1];
    if (power_off) gip_power_off(g);
    if (g->slot >= 0) { padout_connected(g->slot, 0, g->vid, g->pid); g_slot_wcid[g->slot] = 0; }
    g_used[wcid - 1] = 0;
    if (!mt76_remove_client(wcid)) LOGE("dongle: failed to remove controller %d", wcid);
}

static void handle_controller_connect(const uint8_t *addr)
{
    char mac[18];
    uint64_t t0 = now_ms();
    int wcid, stale, slot, s;

    pthread_mutex_lock(&g_mtx);
    stale = find_by_addr(addr);
    /* answer first: the controller is waiting for the association response */
    wcid = mt76_associate_client(addr);
    if (wcid == 0) {
        pthread_mutex_unlock(&g_mtx);
        LOGE("dongle: failed to associate controller %s", mac_str(addr, mac));
        return;
    }
    /* A controller that re-associates before we saw it leave (quick power cycle) would
     * otherwise hold two entries and two pad slots until the firmware reports the old
     * one lost. (Not in xow, which just waits for that report.) */
    if (stale) {
        LOGD("dongle: %s re-associated, releasing its old entry %d", mac_str(addr, mac), stale);
        drop_locked(stale, 0);
    }
    slot = -1;
    for (s = 0; s < PAD_MAX; s++) if (!g_slot_wcid[s]) { slot = s; g_slot_wcid[s] = wcid; break; }
    gip_open(&g_ctl[wcid - 1], wcid, addr, slot);
    g_used[wcid - 1] = 1;
    pthread_mutex_unlock(&g_mtx);
    LOGI("dongle: controller %d connected (%s, pad slot %d, association answered in %u ms)", wcid, mac_str(addr, mac), slot,
         (unsigned)(now_ms() - t0));
}

static void handle_controller_disconnect(int wcid)
{
    if (wcid < 1 || wcid > MT_WCID_COUNT) return;       /* ignore invalid WCIDs */
    pthread_mutex_lock(&g_mtx);
    if (!g_used[wcid - 1]) { pthread_mutex_unlock(&g_mtx); return; }
    drop_locked(wcid, 0);
    pthread_mutex_unlock(&g_mtx);
    LOGI("dongle: controller %d disconnected", wcid);
}

static void handle_controller_pair(const uint8_t *addr, const uint8_t *pkt, int n)
{
    char mac[18];
    /* ReservedFrame: unknown, type - type 0x01 is a pairing request */
    if (n < 2 || pkt[1] != 0x01) return;
    if (!mt76_pair_client(addr))  { LOGE("dongle: failed to pair controller"); return; }
    if (!mt76_set_pairing(0))     { LOGE("dongle: failed to disable pairing"); return; }
    LOGI("dongle: controller paired: %s", mac_str(addr, mac));
}

static void handle_controller_packet(int wcid, const uint8_t *pkt, int n)
{
    if (wcid < 1 || wcid > MT_WCID_COUNT) return;
    if (n <= MT_QOS_LEN + 2) return;                    /* ignore invalid or empty packets */
    pkt += MT_QOS_LEN + 2; n -= MT_QOS_LEN + 2;         /* QoS control + 2 bytes of padding */

    pthread_mutex_lock(&g_mtx);
    if (g_used[wcid - 1] && !gip_handle_packet(&g_ctl[wcid - 1], pkt, n))
        LOGE("dongle: error handling packet for controller %d", wcid);
    pthread_mutex_unlock(&g_mtx);
}

static void handle_wlan_packet(const uint8_t *pkt, int n)
{
    const uint8_t *hdr = NULL, *dest, *source, *inner;
    int wcid, type, subtype, inner_len, off;
    uint16_t fc;

    /* The frame is RxWi (32 bytes) + 802.11 header. Frames the firmware reports while
     * it scans the channel candidates carry one more word (the channel) in front, so
     * accept either position and take the one whose destination is our address. */
    for (off = 0; off <= 4; off += 4) {
        if (n <= off + MT_RXWI_LEN + MT_WLAN_HDR_LEN) break;
        if (memcmp(pkt + off + MT_RXWI_LEN + 4, mt76_mac(), 6) == 0) { hdr = pkt + off + MT_RXWI_LEN; break; }
    }
    if (!hdr) return;                                   /* not for us (or too short) */
    wcid = pkt[off + 4];                                /* RxWi word 1, bits 0..7 */
    fc = le16(hdr);
    type = (fc >> 2) & 0x3; subtype = (fc >> 4) & 0xf;
    dest = hdr + 4; source = hdr + 10;
    inner = hdr + MT_WLAN_HDR_LEN; inner_len = n - off - MT_RXWI_LEN - MT_WLAN_HDR_LEN;
    (void)dest;

    if (g_verbose >= 2 && !(type == MT_WLAN_DATA && subtype == MT_WLAN_QOS_DATA)) {
        char a[18], hex[3 * 64 + 4];
        hex_str(inner, inner_len > 64 ? 64 : inner_len, hex, sizeof hex);
        LOGT("wlan: type %d subtype 0x%x wcid %d from %s (rxwi +%d), %d bytes: %s", type, subtype, wcid, mac_str(source, a), off, inner_len, hex);
    }

    if (type == MT_WLAN_MANAGEMENT) {
        switch (subtype) {
        case MT_WLAN_ASSOCIATION_REQ:
            handle_controller_connect(source);
            break;
        /* 1537 controllers associate, disassociate and associate again during pairing;
         * these disassociations happen without an EVT_CLIENT_LOST */
        case MT_WLAN_DISASSOCIATION:
            handle_controller_disconnect(wcid);
            break;
        /* reserved frames carry several things; pairing requests are the known one */
        case MT_WLAN_RESERVED:
            handle_controller_pair(source, inner, inner_len);
            break;
        }
    } else if (type == MT_WLAN_DATA && subtype == MT_WLAN_QOS_DATA) {
        handle_controller_packet(wcid, inner, inner_len);
    }
}

void dongle_handle_bulk(const uint8_t *data, int n, int ep)
{
    uint32_t info;
    const uint8_t *pkt;
    int plen;

    if (g_verbose >= 3) {
        char hex[3 * 64 + 4];
        hex_str(data, n > 64 ? 64 : n, hex, sizeof hex);
        LOGT("rx ep%d %d bytes: %s%s", ep, n, hex, n > 64 ? " ..." : "");
    }
    if (n <= MT_RXINFO_LEN + 4) return;                 /* ignore invalid or empty data */
    info = le32(data);
    pkt = data + MT_RXINFO_LEN;
    plen = n - MT_RXINFO_LEN - 4;                       /* minus the end marker */

    if (MT_RXINFO_PORT(info) == MT_PORT_CPU_RX) {
        switch (MT_RXINFO_EVENT(info)) {
        case MT_EVT_BUTTON_PRESS:
            LOGI("dongle: adapter button pressed");
            mt76_set_pairing(1);
            break;
        case MT_EVT_PACKET_RX:
            handle_wlan_packet(pkt, plen);
            break;
        case MT_EVT_CLIENT_LOST:
            LOGD("dongle: client %d lost", pkt[0]);
            handle_controller_disconnect(pkt[0]);
            break;
        default:
            if (g_verbose >= 2) {
                char hex[3 * 32 + 4];
                hex_str(pkt, plen > 32 ? 32 : plen, hex, sizeof hex);
                LOGT("dongle: MCU event 0x%x (%d bytes): %s", (unsigned)MT_RXINFO_EVENT(info), plen, hex);
            }
            break;
        }
    } else if (MT_RXINFO_PORT(info) == MT_PORT_WLAN) {
        if (MT_RXINFO_IS80211(info)) handle_wlan_packet(pkt, plen);
    }
}

void dongle_shutdown(void)
{
    int w;
    pthread_mutex_lock(&g_mtx);
    for (w = 1; w <= MT_WCID_COUNT; w++)
        if (g_used[w - 1]) drop_locked(w, 1);
    pthread_mutex_unlock(&g_mtx);
}
