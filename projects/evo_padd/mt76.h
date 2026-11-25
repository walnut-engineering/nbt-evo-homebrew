/* mt76.h - MT76 radio driver for the Xbox One Wireless Adapter.
 * A C port of xow's dongle/mt76.{h,cpp} (Copyright (C) 2019 Medusalix).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EVO_PADD_MT76_H
#define EVO_PADD_MT76_H

#include <stdint.h>

/* endpoint numbers: MCU events, WLAN packets, everything we send */
#define MT_EP_READ          5
#define MT_EP_READ_PACKET   4
#define MT_EP_WRITE         4

#define MT_WCID_COUNT       16

/* 802.11 frame types / subtypes */
#define MT_WLAN_MANAGEMENT        0x00
#define MT_WLAN_DATA              0x02
#define MT_WLAN_ASSOCIATION_REQ   0x00
#define MT_WLAN_ASSOCIATION_RESP  0x01
#define MT_WLAN_DISASSOCIATION    0x0a
#define MT_WLAN_RESERVED          0x07
#define MT_WLAN_BEACON            0x08
#define MT_WLAN_QOS_DATA          0x08

/* wire sizes of the headers the receive path walks over */
#define MT_RXINFO_LEN       4       /* RxInfo word (also repeated as end marker)  */
#define MT_RXWI_LEN         32      /* receive wireless information               */
#define MT_TXWI_LEN         20      /* transmit wireless information              */
#define MT_WLAN_HDR_LEN     24      /* frame control, duration, 3 addresses, seq  */
#define MT_QOS_LEN          2

/* RxInfo word fields */
#define MT_RXINFO_PORT(w)       (((w) >> 27) & 0x7)
#define MT_RXINFO_EVENT(w)      (((w) >> 20) & 0xf)     /* command port */
#define MT_RXINFO_IS80211(w)    (((w) >> 19) & 0x1)     /* WLAN port    */
#define MT_PORT_WLAN            0
#define MT_PORT_CPU_RX          1
#define MT_PORT_CPU_TX          2

/* MCU events */
#define MT_EVT_BUTTON_PRESS     0x04    /* the adapter's own button           */
#define MT_EVT_PACKET_RX        0x0c    /* packet from a client               */
#define MT_EVT_CLIENT_LOST      0x0e    /* lost connection to a client        */

/* Bring the chip up: firmware, radio, registers, channels, beacon.
 * stop_after: 0 = everything, 1 = firmware only (bring-up debugging). 0 = ok. */
int  mt76_init(const char *firmware_path, int stop_after);
void mt76_shutdown(void);

const uint8_t *mt76_mac(void);

/* client operations (callers hold the dongle's controller lock) */
int  mt76_associate_client(const uint8_t addr[6]);          /* wcid 1..16, 0 = failed */
int  mt76_remove_client(int wcid);
int  mt76_pair_client(const uint8_t addr[6]);
int  mt76_send_client_packet(int wcid, const uint8_t addr[6], const uint8_t *pkt, int len);
int  mt76_set_pairing(int enable);
int  mt76_pairing(void);

#endif
