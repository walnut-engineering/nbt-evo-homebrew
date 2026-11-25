/* mt76.c - MT76 radio driver for the Xbox One Wireless Adapter (045e:02e6).
 *
 * A C port of xow's dongle/mt76.cpp:
 *   Copyright (C) 2019 Medusalix
 *   This program is free software; you can redistribute it and/or modify it under the
 *   terms of the GNU General Public License as published by the Free Software
 *   Foundation; either version 2 of the License, or (at your option) any later version.
 *
 * The sequence of register writes, MCU commands and frames is xow's, unchanged. What
 * changed is the form: xow overlays packed bitfield structs on packet bytes; this port
 * builds every header with explicit little-endian packing (the head unit's ARM faults
 * on unaligned access, and bitfield layout is a compiler detail we will not lean on).
 * Bit positions are documented where each word is built.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "mt76.h"
#include "mt76_regs.h"
#include "usbq.h"
#include "padd.h"

/* ---- enums from xow's mt76.h ---------------------------------------------------- */
enum { MT_QSEL_MGMT, MT_QSEL_HCCA, MT_QSEL_EDCA, MT_QSEL_EDCA_2 };
enum { MT_PHY_TYPE_CCK, MT_PHY_TYPE_OFDM, MT_PHY_TYPE_HT, MT_PHY_TYPE_HT_GF, MT_PHY_TYPE_VHT };
enum { MT_LED_BLINK, MT_LED_ON, MT_LED_OFF };
enum { FW_MAC_ADDRESS_SET = 0, FW_CLIENT_ADD = 1, FW_CLIENT_REMOVE = 2, FW_CHANNEL_CANDIDATES_SET = 7 };
enum { MT_CH_BW_20, MT_CH_BW_40, MT_CH_BW_80 };
enum { MT_CH_5G_JAPAN, MT_CH_5G_UNII_1, MT_CH_5G_UNII_2, MT_CH_5G_UNII_2E_1, MT_CH_5G_UNII_2E_2, MT_CH_5G_UNII_3 };
enum { MCU_CAL_R = 1, MCU_CAL_TEMP_SENSOR, MCU_CAL_RXDCOC, MCU_CAL_RC };
enum { MT_RF_CR, MT_BBP_CR, MT_RF_BBP_CR, MT_HL_TEMP_CR_UPDATE };
enum { RADIO_OFF = 0x30, RADIO_ON = 0x31 };
enum { Q_SELECT = 1 };
enum {
    CMD_PACKET_TX = 0, CMD_FUN_SET_OP = 1, CMD_LOAD_CR = 2, CMD_INTERNAL_FW_OP = 3, CMD_BURST_WRITE = 8,
    CMD_LED_MODE_OP = 16, CMD_POWER_SAVING_OP = 20, CMD_SWITCH_CHANNEL_OP = 30, CMD_CALIBRATION_OP = 31,
};
enum { NORMAL_PACKET, CMD_PACKET };
enum {
    MT_VEND_DEV_MODE = 0x1, MT_VEND_MULTI_WRITE = 0x6, MT_VEND_MULTI_READ = 0x7,
    MT_VEND_WRITE_CFG = 0x46, MT_VEND_READ_CFG = 0x47,
};

#define POLL_TIMEOUT_MS 1000
#define FW_HEADER_LEN   32          /* ilmLength, dlmLength, build, version, pad, buildTime[16] */

static uint8_t  g_mac[6];
static uint16_t g_clients;          /* bit n = WCID n+1 in use */
static int      g_io_errors;        /* control-transfer failures (xow terminates on the first) */
static int      g_radio_up;
static volatile int g_pairing;

const uint8_t *mt76_mac(void) { return g_mac; }

/* ---- small packet builder --------------------------------------------------------- */
typedef struct { uint8_t b[640]; int n; int overflow; } pkt_t;

static void pk_init(pkt_t *p) { p->n = 0; p->overflow = 0; }
static void pk_bytes(pkt_t *p, const void *d, int n)
{
    if (p->n + n > (int)sizeof p->b) { p->overflow = 1; return; }
    memcpy(p->b + p->n, d, n); p->n += n;
}
static void pk_zero(pkt_t *p, int n)
{
    if (p->n + n > (int)sizeof p->b) { p->overflow = 1; return; }
    memset(p->b + p->n, 0, n); p->n += n;
}
static void pk_u8(pkt_t *p, uint8_t v)   { pk_bytes(p, &v, 1); }
static void pk_u16(pkt_t *p, uint16_t v) { uint8_t b[2]; put16(b, v); pk_bytes(p, b, 2); }
static void pk_u32(pkt_t *p, uint32_t v) { uint8_t b[4]; put32(b, v); pk_bytes(p, b, 4); }
static int  pad4(int n) { return (4 - n % 4) % 4; }

/* ---- register access over EP0 ---------------------------------------------------- */
static uint32_t reg_read_req(uint16_t address, int request)
{
    uint8_t b[4] = { 0, 0, 0, 0 };
    if (usbq_ctrl_in(request, 0, address, b, 4) != 0) g_io_errors++;
    return le32(b);
}

static void reg_write_req(uint16_t address, uint32_t value, int request)
{
    int rc;
    if (request == MT_VEND_DEV_MODE) {
        rc = usbq_ctrl_out(request, address, 0, NULL, 0);       /* the "address" travels as wValue */
    } else {
        uint8_t b[4];
        put32(b, value);
        rc = usbq_ctrl_out(request, 0, address, b, 4);
    }
    if (rc != 0) g_io_errors++;
}

static uint32_t reg_read(uint16_t address)                { return reg_read_req(address, MT_VEND_MULTI_READ); }
static void     reg_write(uint16_t address, uint32_t v)   { reg_write_req(address, v, MT_VEND_MULTI_WRITE); }

/* ---- MCU commands ------------------------------------------------------------------
 * TxInfoCommand word: length[0:16] sequence[16:20] command[20:27] port[27:30] infoType[30:32].
 * Responses are not requested (sequence 0), as in xow. */
static int send_command(int command, const uint8_t *data, int len)
{
    pkt_t out;
    int padding = pad4(len);

    pk_init(&out);
    pk_u32(&out, (uint32_t)(len + padding) | ((uint32_t)command << 20) |
                 ((uint32_t)MT_PORT_CPU_TX << 27) | ((uint32_t)CMD_PACKET << 30));
    pk_bytes(&out, data, len);
    pk_zero(&out, padding);
    pk_zero(&out, 4);                                   /* 32 zero bits mark the end */
    if (out.overflow) { LOGE("mt76: command %d too long (%d)", command, len); return 0; }
    if (usbq_bulk_out(out.b, out.n) != 0) { LOGE("mt76: failed to write command %d", command); return 0; }
    return 1;
}

static int cmd_u32x2(int command, uint32_t a, uint32_t b)
{
    uint8_t d[8];
    put32(d, a); put32(d + 4, b);
    return send_command(command, d, 8);
}

static int cmd_u32(int command, uint32_t a)
{
    uint8_t d[4];
    put32(d, a);
    return send_command(command, d, 4);
}

static int select_function(uint32_t function, uint32_t value) { return cmd_u32x2(CMD_FUN_SET_OP, function, value); }
static int power_mode(uint32_t mode)                          { return cmd_u32(CMD_POWER_SAVING_OP, mode); }
static int load_cr(uint32_t mode)                             { return cmd_u32(CMD_LOAD_CR, mode); }
static int calibrate(uint32_t calibration, uint32_t value)    { return cmd_u32x2(CMD_CALIBRATION_OP, calibration, value); }
static int set_led_mode(uint32_t index)                       { return cmd_u32(CMD_LED_MODE_OP, index); }

static int burst_write(uint32_t index, const uint8_t *values, int len)
{
    pkt_t out;
    pk_init(&out);
    pk_u32(&out, index + MT_REGISTER_OFFSET);
    pk_bytes(&out, values, len);
    if (out.overflow) return 0;
    return send_command(CMD_BURST_WRITE, out.b, out.n);
}

static int send_firmware_command(uint32_t command, const uint8_t *data, int len)
{
    pkt_t out;
    pk_init(&out);
    pk_u32(&out, command);
    pk_bytes(&out, data, len);
    if (out.overflow) return 0;
    return send_command(CMD_INTERNAL_FW_OP, out.b, out.n);
}

/* ---- frames -----------------------------------------------------------------------
 * TxWi (20 bytes), word 0: timestamp = bit 3, phyType = bits 29..31
 *                  word 1: ack = bit 0, nseq = bit 1, wcid = bits 8..15, mpduByteCount = bits 16..29
 *                  words 2..4 (iv, eiv, eapId...) zero */
static void pk_txwi(pkt_t *p, int timestamp, int ack, int nseq, int wcid, int mpdu_len)
{
    pk_u32(p, ((uint32_t)MT_PHY_TYPE_OFDM << 29) | (timestamp ? 1u << 3 : 0));
    pk_u32(p, (ack ? 1u : 0) | (nseq ? 1u << 1 : 0) | ((uint32_t)(wcid & 0xff) << 8) | ((uint32_t)(mpdu_len & 0x3fff) << 16));
    pk_zero(p, 12);
}

/* 802.11 header (24 bytes). Frame control: type = bits 2..3, subtype = bits 4..7, fromDs = bit 9 */
static void pk_wlan_hdr(pkt_t *p, int type, int subtype, int from_ds, int duration, const uint8_t *dest)
{
    pk_u16(p, (uint16_t)((type << 2) | (subtype << 4) | (from_ds ? 1 << 9 : 0)));
    pk_u16(p, (uint16_t)duration);
    pk_bytes(p, dest, 6);
    pk_bytes(p, g_mac, 6);          /* source */
    pk_bytes(p, g_mac, 6);          /* BSSID  */
    pk_u16(p, 0);                   /* sequence control */
}

/* TxInfoPacket word: length[0:16] is80211 = bit 19, wiv = bit 24, qsel[25:27], port[27:30], infoType[30:32] */
static int send_wlan_packet(const uint8_t *data, int len)
{
    pkt_t out;
    int padding = pad4(len);

    pk_init(&out);
    pk_u32(&out, (uint32_t)(len + padding) | (1u << 19) | (1u << 24) | ((uint32_t)MT_QSEL_EDCA << 25) |
                 ((uint32_t)MT_PORT_WLAN << 27) | ((uint32_t)NORMAL_PACKET << 30));
    pk_bytes(&out, data, len);
    pk_zero(&out, padding);
    pk_zero(&out, 4);
    if (out.overflow) return 0;
    if (usbq_bulk_out(out.b, out.n) != 0) { LOGE("mt76: failed to write WLAN packet"); return 0; }
    return 1;
}

/* ---- efuse -------------------------------------------------------------------------
 * MT_EFUSE_CTRL: mode = bits 6..7, addressIn = bits 16..25, kick = bit 30.
 * Reads the 16-byte block holding `address`, returns `length` bytes starting at the
 * 4-byte word that holds it (xow's exact semantics, callers index accordingly). */
static int efuse_read(uint8_t address, uint8_t length, uint8_t *out)
{
    uint32_t control = reg_read(MT_EFUSE_CTRL);
    uint64_t t0;
    uint8_t i;
    int n = 0;

    control &= ~(uint32_t)MT_EFUSE_CTRL_MODE;                   /* MT_EE_READ */
    control &= ~(uint32_t)MT_EFUSE_CTRL_AIN;
    control |= (uint32_t)(address & ~0x0f) << 16;
    control |= (uint32_t)MT_EFUSE_CTRL_KICK;
    reg_write(MT_EFUSE_CTRL, control);

    t0 = now_ms();
    while (reg_read(MT_EFUSE_CTRL) & MT_EFUSE_CTRL_KICK) {
        if (now_ms() - t0 > POLL_TIMEOUT_MS || g_io_errors) { LOGE("mt76: read from EFUSE timed out"); return 0; }
    }
    for (i = 0; i < length; i += 4) {
        uint8_t offset = (uint8_t)((address & 0x0c) + i);
        uint32_t value = reg_read((uint16_t)(MT_EFUSE_DATA_BASE + offset));
        uint8_t remaining = (uint8_t)(length - i);
        uint8_t size = remaining < 4 ? remaining : 4, k;
        for (k = 0; k < size; k++) out[n++] = (uint8_t)(value >> (8 * k));
    }
    return n;
}

/* ---- channels ------------------------------------------------------------------------ */
static uint8_t channel_group(uint8_t channel)
{
    if (channel >= 184 && channel <= 196) return MT_CH_5G_JAPAN;
    if (channel <= 48)  return MT_CH_5G_UNII_1;
    if (channel <= 64)  return MT_CH_5G_UNII_2;
    if (channel <= 114) return MT_CH_5G_UNII_2E_1;
    if (channel <= 144) return MT_CH_5G_UNII_2E_2;
    return MT_CH_5G_UNII_3;
}

static uint8_t channel_subgroup(uint8_t channel)
{
    if (channel >= 192) return MT_CH_5G_HIGH;
    else if (channel >= 184) return MT_CH_5G_LOW;
    else if (channel < 6)   return MT_CH_2G_LOW;
    else if (channel < 11)  return MT_CH_2G_MID;
    else if (channel < 15)  return MT_CH_2G_HIGH;
    else if (channel < 44)  return MT_CH_5G_LOW;
    else if (channel < 52)  return MT_CH_5G_HIGH;
    else if (channel < 58)  return MT_CH_5G_LOW;
    else if (channel < 98)  return MT_CH_5G_HIGH;
    else if (channel < 106) return MT_CH_5G_LOW;
    else if (channel < 116) return MT_CH_5G_HIGH;
    else if (channel < 130) return MT_CH_5G_LOW;
    else if (channel < 149) return MT_CH_5G_HIGH;
    else if (channel < 157) return MT_CH_5G_LOW;
    return MT_CH_5G_HIGH;
}

static uint8_t channel_power(uint8_t channel)
{
    /* channel group -> entry in the power table, subgroup -> power offset value */
    int is24 = channel <= 14;
    uint8_t table_index = is24 ? MT_EE_TX_POWER_0_START_2G : MT_EE_TX_POWER_0_START_5G;
    uint8_t group = channel_group(channel), subgroup = channel_subgroup(channel);
    uint8_t entry[8], index, target, off;
    int8_t offset, power;

    if (!is24) table_index = (uint8_t)(table_index + group * MT_EE_TX_POWER_GROUP_SIZE_5G);
    if (efuse_read(table_index, 8, entry) < 8) { LOGE("mt76: failed to read power table entry"); return MT_CH_POWER_MIN; }

    index = is24 ? 4 : 5;
    target = entry[index];
    off = entry[index + subgroup];
    if ((off & 0x80) == 0) return target;               /* offset disabled */
    offset = (int8_t)(off & 0x3f);                      /* 0.5 dB steps    */
    power = (int8_t)((off & 0x40) ? target + offset : target - offset);
    if (power < MT_CH_POWER_MIN) return MT_CH_POWER_MIN;
    if (power > MT_CH_POWER_MAX) return MT_CH_POWER_MAX;
    return (uint8_t)power;
}

/* ChannelConfigData, 20 bytes: channel, 3 pad, txRxSetting u16, 2 pad, 8 pad,
 * bandwidth, txPower, scan, unknown */
static int configure_channel(uint8_t channel, uint8_t bandwidth, int scan)
{
    uint8_t c[20];
    memset(c, 0, sizeof c);
    c[0] = channel;
    put16(c + 4, 0x0101);                               /* TX and RX stream 1 */
    c[16] = bandwidth;
    c[17] = channel_power(channel);
    c[18] = scan ? 1 : 0;
    if (!send_command(CMD_SWITCH_CHANNEL_OP, c, sizeof c)) { LOGE("mt76: failed to switch channel"); return 0; }
    LOGD("mt76: channel %d, power %d", channel, c[17]);
    return 1;
}

static int init_channels(void)
{
    static const uint8_t candidates[] = { 0x01, 0xa5, 0x0b, 0x01, 0x06, 0x0b, 0x24, 0x28, 0x2c, 0x30, 0x95, 0x99, 0x9d, 0xa1 };
    uint8_t values[sizeof candidates * 4];
    unsigned i;

    configure_channel(0x01, MT_CH_BW_20, 1);
    configure_channel(0x06, MT_CH_BW_20, 1);
    configure_channel(0x0b, MT_CH_BW_20, 1);
    configure_channel(0x24, MT_CH_BW_40, 1);
    configure_channel(0x28, MT_CH_BW_40, 0);
    configure_channel(0x2c, MT_CH_BW_40, 1);
    configure_channel(0x30, MT_CH_BW_40, 0);
    configure_channel(0x95, MT_CH_BW_80, 1);
    configure_channel(0x99, MT_CH_BW_80, 0);
    configure_channel(0x9d, MT_CH_BW_80, 1);
    configure_channel(0xa1, MT_CH_BW_80, 0);
    configure_channel(0xa5, MT_CH_BW_80, 0);

    for (i = 0; i < sizeof candidates; i++) put32(values + 4 * i, candidates[i]);
    if (!send_firmware_command(FW_CHANNEL_CANDIDATES_SET, values, sizeof values)) {
        LOGE("mt76: failed to set channel candidates");
        return 0;
    }
    return 1;
}

/* ---- crystal trim ------------------------------------------------------------------ */
static int calibrate_crystal(void)
{
    uint8_t trim[4];
    uint16_t value;
    int8_t offset;
    uint32_t ctrl;

    if (efuse_read(MT_EE_XTAL_TRIM_2, 4, trim) < 4) { LOGE("mt76: failed to read second trim value"); return 0; }
    value = (uint16_t)((trim[3] << 8) | trim[2]);
    offset = (int8_t)(value & 0x7f);
    if ((value & 0xff) == 0xff) offset = 0;
    else if (value & 0x80) offset = (int8_t)-offset;
    value >>= 8;

    if (value == 0x00 || value == 0xff) {
        if (efuse_read(MT_EE_XTAL_TRIM_1, 4, trim) < 4) { LOGE("mt76: failed to read first trim value"); return 0; }
        value = (uint16_t)((trim[3] << 8) | trim[2]);
        value &= 0xff;
        if (value == 0x00 || value == 0xff) value = 0x14;
    }
    value = (uint16_t)((value & 0x7f) + offset);

    ctrl = reg_read(MT_XO_CTRL5) & ~(uint32_t)MT_XO_CTRL5_C2_VAL;
    reg_write_req(MT_XO_CTRL5, ctrl | ((uint32_t)value << 8), MT_VEND_WRITE_CFG);
    reg_write_req(MT_XO_CTRL6, MT_XO_CTRL6_C2_CTRL, MT_VEND_WRITE_CFG);
    reg_write(MT_CMB_CTRL, 0x0091a7ff);
    LOGD("mt76: crystal trim 0x%02x", value);
    return 1;
}

/* ---- register init ------------------------------------------------------------------- */
static int init_registers(void)
{
    static const struct { uint16_t reg; uint32_t val; } tab[] = {
        { MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_RESET_CSR | MT_MAC_SYS_CTRL_RESET_BBP },
        { MT_USB_DMA_CFG, 0 },
        { MT_MAC_SYS_CTRL, 0 },
        { MT_PWR_PIN_CFG, 0 },
        { MT_LDO_CTRL_1, 0x6b006464 },
        { MT_WPDMA_GLO_CFG, 0x70 },
        { MT_WMM_AIFSN, 0x2273 },
        { MT_WMM_CWMIN, 0x2344 },
        { MT_WMM_CWMAX, 0x34aa },
        { MT_FCE_DMA_ADDR, 0x041200 },
        { MT_TSO_CTRL, 0 },
        { MT_PBF_SYS_CTRL, 0x080c00 },
        { MT_PBF_TX_MAX_PCNT, 0x1fbf1f1f },
        { MT_FCE_PSE_CTRL, 0x01 },
        { MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX | MT_MAC_SYS_CTRL_ENABLE_RX },
        { MT_AUTO_RSP_CFG, 0x13 },
        { MT_MAX_LEN_CFG, 0x3e3fff },
        { MT_AMPDU_MAX_LEN_20M1S, 0xfffc9855 },
        { MT_AMPDU_MAX_LEN_20M2S, 0xff },
        { MT_BKOFF_SLOT_CFG, 0x0109 },
        { MT_PWR_PIN_CFG, 0 },
        { MT_EDCA_CFG_AC(0), 0x064320 },
        { MT_EDCA_CFG_AC(1), 0x0a4700 },
        { MT_EDCA_CFG_AC(2), 0x043238 },
        { MT_EDCA_CFG_AC(3), 0x03212f },
        { MT_TX_PIN_CFG, 0x150f0f },
        { MT_TX_SW_CFG0, 0x101001 },
        { MT_TX_SW_CFG1, 0x010000 },
        { MT_TXOP_CTRL_CFG, 0x10583f },
        { MT_TX_TIMEOUT_CFG, 0x0a0f90 },
        { MT_TX_RETRY_CFG, 0x47d01f0f },
        { MT_CCK_PROT_CFG, 0x03f40003 },
        { MT_OFDM_PROT_CFG, 0x03f40003 },
        { MT_MM20_PROT_CFG, 0x01742004 },
        { MT_GF20_PROT_CFG, 0x01742004 },
        { MT_GF40_PROT_CFG, 0x03f42084 },
        { MT_EXP_ACK_TIME, 0x2c00dc },
        { MT_TX_ALC_CFG_2, 0x22160a00 },
        { MT_TX_ALC_CFG_3, 0x22160a76 },
        { MT_TX_ALC_CFG_0, 0x3f3f1818 },
        { MT_TX_ALC_CFG_4, 0x0606 },
        { MT_PIFS_TX_CFG, 0x060fff },
        { MT_RX_FILTR_CFG, 0x017f17 },
        { MT_LEGACY_BASIC_RATE, 0x017f },
        { MT_HT_BASIC_RATE, 0x8003 },
        { MT_PN_PAD_MODE, 0x02 },
        { MT_TXOP_HLDR_ET, 0x02 },
        { MT_TX_PROT_CFG6, 0xe3f42004 },
        { MT_TX_PROT_CFG7, 0xe3f42084 },
        { MT_TX_PROT_CFG8, 0xe3f42104 },
        { MT_DACCLK_EN_DLY_CFG, 0 },
        { MT_RF_PA_MODE_ADJ0, 0xee000000 },
        { MT_RF_PA_MODE_ADJ1, 0xee000000 },
        { MT_TX0_RF_GAIN_CORR, 0x0f3c3c3c },
        { MT_TX1_RF_GAIN_CORR, 0x0f3c3c3c },
        { MT_PBF_CFG, 0x1efebcf5 },
        { MT_PAUSE_ENABLE_CONTROL1, 0x0a },
        { MT_RF_BYPASS_0, 0x7f000000 },
        { MT_RF_SETTING_0, 0x1a800000 },
        { MT_XIFS_TIME_CFG, 0x33a40e0a },
        { MT_FCE_L2_STUFF, 0x03ff0223 },
        { MT_TX_RTS_CFG, 0 },
        { MT_BEACON_TIME_CFG, 0x0640 },
        { MT_EXT_CCA_CFG, 0xf0e4 },
        { MT_CH_TIME_CFG, 0x015f },
    };
    uint8_t chip[4];
    unsigned i;
    char mac[18];

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) reg_write(tab[i].reg, tab[i].val);
    if (g_io_errors) return 0;

    if (!calibrate_crystal()) { LOGE("mt76: failed to calibrate crystal"); return 0; }

    /* automatic gain control */
    reg_write(MT_BBP(AGC, 8), 0x18365efa);
    reg_write(MT_BBP(AGC, 9), 0x18365efa);

    if (efuse_read(MT_EE_MAC_ADDR, 6, g_mac) < 6) { LOGE("mt76: failed to read MAC address"); return 0; }
    /* some adapters' addresses start with 6c:5d:3a; controllers only connect to 62:45:bx:xx:xx:xx */
    if (g_mac[0] != 0x62) {
        LOGD("mt76: MAC address %s is not a 62:45:bx one, correcting", mac_str(g_mac, mac));
        g_mac[0] = 0x62; g_mac[1] = 0x45; g_mac[2] = 0xbd;
    }
    if (!burst_write(MT_MAC_ADDR_DW0, g_mac, 6))  { LOGE("mt76: failed to write MAC address"); return 0; }
    if (!burst_write(MT_MAC_BSSID_DW0, g_mac, 6)) { LOGE("mt76: failed to write BSSID"); return 0; }

    {
        uint32_t asic = reg_read(MT_ASIC_VERSION) >> 16, macv = reg_read(MT_MAC_CSR0) >> 16;
        if (efuse_read(MT_EE_CHIP_ID, 4, chip) < 4) { LOGE("mt76: failed to read chip id"); return 0; }
        LOGD("mt76: ASIC version %x, MAC version %x, chip id %x", (unsigned)asic, (unsigned)macv, (chip[1] << 8) | chip[2]);
    }
    LOGI("mt76: wireless address %s", mac_str(g_mac, mac));
    return g_io_errors == 0;
}

/* ---- firmware ------------------------------------------------------------------------
 * Firmware chunk framing: TxInfoCommand word with port = CPU_TX, infoType = NORMAL,
 * length = chunk length; then the chunk; then 4 zero bytes. */
static int load_firmware_part(uint32_t offset, const uint8_t *start, uint32_t total)
{
    uint8_t *out = malloc(MT_FW_CHUNK_SIZE + 8);
    uint32_t done;

    if (!out) return 0;
    for (done = 0; done < total; done += MT_FW_CHUNK_SIZE) {
        uint32_t remaining = total - done;
        uint32_t length = remaining > MT_FW_CHUNK_SIZE ? MT_FW_CHUNK_SIZE : remaining;
        uint32_t complete = (length << 16) | MT_DMA_COMPLETE;
        uint64_t t0;

        put32(out, length | ((uint32_t)MT_PORT_CPU_TX << 27) | ((uint32_t)NORMAL_PACKET << 30));
        memcpy(out + 4, start + done, length);
        memset(out + 4 + length, 0, 4);

        reg_write_req(MT_FCE_DMA_ADDR, offset + done, MT_VEND_WRITE_CFG);
        reg_write_req(MT_FCE_DMA_LEN, length << 16, MT_VEND_WRITE_CFG);
        if (g_io_errors || usbq_bulk_out(out, (int)length + 8) != 0) {
            LOGE("mt76: failed to write firmware chunk at 0x%x", (unsigned)(offset + done));
            free(out);
            return 0;
        }
        t0 = now_ms();
        while (reg_read_req(MT_FCE_DMA_LEN, MT_VEND_READ_CFG) != complete) {
            if (now_ms() - t0 > POLL_TIMEOUT_MS || g_io_errors) {
                LOGE("mt76: firmware part loading timed out at 0x%x (DMA_LEN 0x%08x, want 0x%08x)", (unsigned)(offset + done),
                     (unsigned)reg_read_req(MT_FCE_DMA_LEN, MT_VEND_READ_CFG), (unsigned)complete);
                free(out);
                return 0;
            }
        }
        LOGT("mt76: fw chunk 0x%06x + 0x%04x ok (%u ms)", (unsigned)(offset + done), (unsigned)length, (unsigned)(now_ms() - t0));
    }
    free(out);
    return 1;
}

static int load_firmware(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t *fw;
    long size;
    uint32_t ilm, dlm, state;
    uint64_t t0;
    char build[17];

    if (!f) {
        LOGE("mt76: cannot open firmware %s: %s", path, strerror(errno));
        LOGE("mt76: the adapter's firmware (FW_ACC_00U.bin) is Microsoft's and is not shipped - see the README");
        return 0;
    }
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size < FW_HEADER_LEN || size > 4 * 1024 * 1024 || !(fw = malloc((size_t)size))) { fclose(f); LOGE("mt76: bad firmware size %ld", size); return 0; }
    if (fread(fw, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(fw); LOGE("mt76: failed to read firmware"); return 0; }
    fclose(f);

    ilm = le32(fw); dlm = le32(fw + 4);
    if ((uint64_t)FW_HEADER_LEN + ilm + dlm > (uint64_t)size) {
        LOGE("mt76: firmware header does not match the file (ilm %u dlm %u size %ld)", (unsigned)ilm, (unsigned)dlm, size);
        free(fw);
        return 0;
    }
    memcpy(build, fw + 16, 16); build[16] = 0;
    LOGD("mt76: firmware %ld bytes: ILM %u, DLM %u, build %u, time %.16s", size, (unsigned)ilm, (unsigned)dlm, le16(fw + 8), build);

    state = reg_read_req(MT_FCE_DMA_ADDR, MT_VEND_READ_CFG);
    if (state) {
        uint32_t patch;
        LOGD("mt76: firmware already loaded (state 0x%08x), resetting...", (unsigned)state);
        patch = reg_read_req(MT_RF_PATCH, MT_VEND_READ_CFG);
        patch &= ~(uint32_t)BIT(19);
        /* mandatory for already initialized radios */
        reg_write_req(MT_RF_PATCH, patch, MT_VEND_WRITE_CFG);
        reg_write_req(MT_FW_RESET_IVB, 0, MT_VEND_DEV_MODE);
        t0 = now_ms();
        /* xow waits for 0x80000000 (it also USB-resets the adapter first, which we do
         * not); without that port reset the MCU reset leaves the register at 0. */
        while ((state = reg_read_req(MT_FCE_DMA_ADDR, MT_VEND_READ_CFG)) != 0x80000000 && state != 0) {
            if (g_io_errors) { free(fw); return 0; }
            if (now_ms() - t0 > POLL_TIMEOUT_MS) {
                /* Not in xow: the register is also the upload's address latch, so an
                 * upload that died half way leaves it non-zero with no firmware running
                 * to answer the reset. Carry on - a chip that is really stuck fails the
                 * first chunk below. */
                LOGD("mt76: no reset acknowledge (state 0x%08x) - treating as an interrupted upload", (unsigned)state);
                break;
            }
        }
    }
    if (g_io_errors) { free(fw); return 0; }

    /* DMA config: rxBulkEnabled = bit 22, txBulkEnabled = bit 23; then enable FCE and packet DMA */
    reg_write_req(MT_USB_U3DMA_CFG, MT_USB_DMA_CFG_RX_BULK_EN | MT_USB_DMA_CFG_TX_BULK_EN, MT_VEND_WRITE_CFG);
    reg_write(MT_FCE_PSE_CTRL, 0x01);
    reg_write(MT_TX_CPU_FROM_FCE_BASE_PTR, 0x400230);
    reg_write(MT_TX_CPU_FROM_FCE_MAX_COUNT, 0x01);
    reg_write(MT_TX_CPU_FROM_FCE_CPU_DESC_IDX, 0x01);
    reg_write(MT_FCE_PDMA_GLOBAL_CONF, 0x44);
    reg_write(MT_FCE_SKIP_FS, 0x03);
    if (g_io_errors) { free(fw); return 0; }

    t0 = now_ms();
    if (!load_firmware_part(MT_MCU_ILM_OFFSET, fw + FW_HEADER_LEN, ilm))       { LOGE("mt76: failed to write ILM"); free(fw); return 0; }
    if (!load_firmware_part(MT_MCU_DLM_OFFSET, fw + FW_HEADER_LEN + ilm, dlm)) { LOGE("mt76: failed to write DLM"); free(fw); return 0; }
    free(fw);
    LOGD("mt76: firmware image written in %u ms", (unsigned)(now_ms() - t0));

    /* load the initial vector block and start */
    reg_write_req(MT_FCE_DMA_ADDR, 0, MT_VEND_WRITE_CFG);
    reg_write_req(MT_FW_LOAD_IVB, 0, MT_VEND_DEV_MODE);
    t0 = now_ms();
    while (reg_read_req(MT_FCE_DMA_ADDR, MT_VEND_READ_CFG) != 0x01) {
        if (now_ms() - t0 > POLL_TIMEOUT_MS || g_io_errors) {
            LOGE("mt76: firmware loading timed out (FCE_DMA_ADDR 0x%08x)", (unsigned)reg_read_req(MT_FCE_DMA_ADDR, MT_VEND_READ_CFG));
            return 0;
        }
    }
    LOGI("mt76: firmware loaded and running");
    return 1;
}

/* ---- beacon -------------------------------------------------------------------------
 * BeaconFrame: timestamp u64, interval u16, capabilityInfo u16, ssid u16 (wildcard).
 * MT_BEACON_TIME_CFG: tsfTimerEnabled bit 16, tsfSyncMode bits 17..18, tbttTimerEnabled
 * bit 19, transmitBeacon bit 20. */
static int write_beacon(int pairing)
{
    static const uint8_t broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    /* information element (ID 0xdd, length 0x10); byte 8 is the pairing flag */
    uint8_t data[18] = { 0xdd, 0x10, 0x00, 0x50, 0xf2, 0x11, 0x01, 0x10, 0x00, 0xa5, 0x30, 0x99, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    pkt_t out;
    uint32_t config;

    data[8] = pairing ? 1 : 0;
    pk_init(&out);
    /* OFDM, generate the beacon timestamp, hardware sequence control */
    pk_txwi(&out, 1, 0, 1, 0, MT_WLAN_HDR_LEN + 14 + (int)sizeof data);
    pk_wlan_hdr(&out, MT_WLAN_MANAGEMENT, MT_WLAN_BEACON, 0, 0, broadcast);
    pk_zero(&out, 8);               /* timestamp */
    pk_u16(&out, 0x64);             /* 100 ms interval */
    pk_u16(&out, 0xc631);           /* original capability info */
    pk_u16(&out, 0);                /* wildcard SSID */
    pk_bytes(&out, data, sizeof data);

    config = reg_read(MT_BEACON_TIME_CFG);
    config |= MT_BEACON_TIME_CFG_TIMER_EN | MT_BEACON_TIME_CFG_TBTT_EN | MT_BEACON_TIME_CFG_SYNC_MODE | MT_BEACON_TIME_CFG_BEACON_TX;

    if (!burst_write(MT_BEACON_BASE, out.b, out.n)) { LOGE("mt76: failed to write beacon"); return 0; }
    reg_write(MT_BEACON_TIME_CFG, config);
    if (!calibrate(MCU_CAL_RXDCOC, 0)) { LOGE("mt76: failed to calibrate beacon"); return 0; }
    return g_io_errors == 0;
}

/* ---- client operations --------------------------------------------------------------- */
int mt76_associate_client(const uint8_t addr[6])
{
    uint16_t free_ids = (uint16_t)~g_clients;
    int wcid = __builtin_ffs(free_ids);
    uint8_t wcid_data[8] = { 0, 0, 0, 0, 0x40, 0x1f, 0, 0 };
    pkt_t out;

    if (wcid == 0) { LOGE("mt76: all WCIDs are taken"); return 0; }
    g_clients |= (uint16_t)(1u << (wcid - 1));

    pk_init(&out);
    /* OFDM, wait for acknowledgement, ignore WCID */
    pk_txwi(&out, 0, 1, 0, 0xff, MT_WLAN_HDR_LEN + 14);
    pk_wlan_hdr(&out, MT_WLAN_MANAGEMENT, MT_WLAN_ASSOCIATION_RESP, 0, 0, addr);
    /* AssociationResponseFrame: capabilityInfo, statusCode, associationId, u64 unknown */
    pk_u16(&out, 0);
    pk_u16(&out, 0x0110);           /* original status code    */
    pk_u16(&out, 0x0f00);           /* original association ID */
    pk_zero(&out, 8);

    wcid_data[0] = (uint8_t)(wcid - 1);
    /* WCID 0 is reserved for beacon frames */
    if (!burst_write(MT_WCID_ADDR(wcid), addr, 6))                         { LOGE("mt76: failed to write WCID"); goto fail; }
    if (!send_firmware_command(FW_CLIENT_ADD, wcid_data, sizeof wcid_data)) { LOGE("mt76: failed to add client"); goto fail; }
    if (!send_wlan_packet(out.b, out.n))                                   { LOGE("mt76: failed to send association packet"); goto fail; }
    if (!g_pairing && !set_led_mode(MT_LED_ON))                            { LOGE("mt76: failed to set LED mode"); goto fail; }
    return wcid;
fail:
    g_clients &= (uint16_t)~(1u << (wcid - 1));
    return 0;
}

int mt76_remove_client(int wcid)
{
    static const uint8_t empty[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t wcid_data[4] = { 0, 0, 0, 0 };

    if (wcid < 1 || wcid > MT_WCID_COUNT) return 0;
    wcid_data[0] = (uint8_t)(wcid - 1);
    g_clients &= (uint16_t)~(1u << (wcid - 1));
    if (!send_firmware_command(FW_CLIENT_REMOVE, wcid_data, sizeof wcid_data)) { LOGE("mt76: failed to remove client"); return 0; }
    if (!burst_write(MT_WCID_ADDR(wcid), empty, 6))                           { LOGE("mt76: failed to write WCID"); return 0; }
    if (g_clients == 0 && !g_pairing && !set_led_mode(MT_LED_OFF))            { LOGE("mt76: failed to set LED mode"); return 0; }
    return 1;
}

int mt76_pair_client(const uint8_t addr[6])
{
    static const uint8_t data[11] = { 0x70, 0x02, 0x00, 0x45, 0x55, 0x01, 0x0f, 0x8f, 0xff, 0x87, 0x1f };
    pkt_t out;

    pk_init(&out);
    pk_txwi(&out, 0, 1, 0, 0xff, MT_WLAN_HDR_LEN + (int)sizeof data);
    pk_wlan_hdr(&out, MT_WLAN_MANAGEMENT, MT_WLAN_RESERVED, 0, 0, addr);
    pk_bytes(&out, data, sizeof data);
    if (!send_wlan_packet(out.b, out.n)) { LOGE("mt76: failed to send pairing packet"); return 0; }
    return 1;
}

int mt76_send_client_packet(int wcid, const uint8_t addr[6], const uint8_t *pkt, int len)
{
    pkt_t out;
    int frame_len = MT_TXWI_LEN + MT_WLAN_HDR_LEN + MT_QOS_LEN;

    if (wcid < 1 || wcid > MT_WCID_COUNT) return 0;
    if ((g_clients & (1u << (wcid - 1))) == 0) return 1;        /* skip unconnected WCIDs */

    pk_init(&out);
    /* WCID as a big-endian word, then a zero word */
    pk_u8(&out, 0); pk_u8(&out, 0); pk_u8(&out, 0); pk_u8(&out, (uint8_t)(wcid - 1));
    pk_zero(&out, 4);
    /* OFDM, wait for acknowledgement */
    pk_txwi(&out, 0, 1, 0, 0, MT_WLAN_HDR_LEN + MT_QOS_LEN + len);
    /* sent from the AP (fromDs); duration = time to transmit in microseconds */
    pk_wlan_hdr(&out, MT_WLAN_DATA, MT_WLAN_QOS_DATA, 1, 144, addr);
    pk_u16(&out, 0);                                            /* QoS control */
    pk_zero(&out, pad4(frame_len));                             /* frames and data are 32-bit aligned */
    pk_bytes(&out, pkt, len);
    pk_zero(&out, pad4(len));
    if (out.overflow) { LOGE("mt76: client packet too long (%d)", len); return 0; }
    if (!send_command(CMD_PACKET_TX, out.b, out.n)) { LOGE("mt76: failed to send controller packet"); return 0; }
    return 1;
}

int mt76_set_pairing(int enable)
{
    if (!write_beacon(enable)) { LOGE("mt76: failed to write beacon"); return 0; }
    /* light: blinking while pairing, on while a controller is connected, else off
     * (xow leaves it on after pairing; xone does it this way) */
    if (!set_led_mode(enable ? MT_LED_BLINK : g_clients ? MT_LED_ON : MT_LED_OFF)) { LOGE("mt76: failed to set LED mode"); return 0; }
    if (enable != g_pairing) LOGI("mt76: pairing %s", enable ? "enabled" : "disabled");
    g_pairing = enable;
    return 1;
}

int mt76_pairing(void) { return g_pairing; }

/* ---- bring-up / shutdown ------------------------------------------------------------- */
int mt76_init(const char *firmware_path, int stop_after)
{
    g_io_errors = 0;
    g_clients = 0;
    g_radio_up = 0;
    g_pairing = 0;

    if (!load_firmware(firmware_path)) { LOGE("mt76: failed to load firmware"); return -1; }
    if (stop_after == 1) return 0;

    /* select RX ring buffer 1, turn the radio on, load the BBP command register */
    if (!select_function(Q_SELECT, 1) || !power_mode(RADIO_ON) || !load_cr(MT_RF_BBP_CR)) {
        LOGE("mt76: failed to init radio");
        return -1;
    }
    g_radio_up = 1;
    if (!init_registers()) { LOGE("mt76: failed to init registers"); return -1; }
    if (!send_firmware_command(FW_MAC_ADDRESS_SET, g_mac, 6)) { LOGE("mt76: failed to set MAC address"); return -1; }

    /* reset necessary for reliable WLAN associations */
    reg_write(MT_MAC_SYS_CTRL, 0);
    reg_write(MT_RF_BYPASS_0, 0);
    reg_write(MT_RF_SETTING_0, 0);

    if (!calibrate(MCU_CAL_TEMP_SENSOR, 0) || !calibrate(MCU_CAL_RXDCOC, 1) || !calibrate(MCU_CAL_RC, 0)) {
        LOGE("mt76: failed to calibrate chip");
        return -1;
    }
    reg_write(MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX | MT_MAC_SYS_CTRL_ENABLE_RX);

    if (!init_channels())  { LOGE("mt76: failed to init channels"); return -1; }
    if (!write_beacon(0))  { LOGE("mt76: failed to write beacon"); return -1; }
    if (g_io_errors)       { LOGE("mt76: %d control transfer error(s) during bring-up", g_io_errors); return -1; }
    LOGI("mt76: radio up");
    return 0;
}

void mt76_shutdown(void)
{
    if (!g_radio_up || usbq_gone()) return;
    if (!set_led_mode(MT_LED_OFF)) LOGE("mt76: failed to turn off LED");
    if (!power_mode(RADIO_OFF))    LOGE("mt76: failed to turn off radio");
    g_radio_up = 0;
}
