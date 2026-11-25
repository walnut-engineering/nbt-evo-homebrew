/* usbq.h - USB transport for evo_padd on QNX libusbdi (the unit's io-usb stack).
 * One adapter, opened once: vendor control transfers on EP0, one bulk OUT pipe,
 * and a bulk IN reader per receive thread.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EVO_PADD_USBQ_H
#define EVO_PADD_USBQ_H

#include <stdint.h>
#include <signal.h>

#define USBQ_RX_MAX   512       /* one bulk IN transfer (as the reference driver) */
#define USBQ_TX_MAX   16384     /* largest bulk OUT: a firmware chunk + framing   */

/* Connect to io-usb and attach the first device matching vid + one of pids. Only
 * that device is ever attached. Waits up to wait_ms for it (< 0 = for ever, until
 * *stop becomes non-zero). 0 or an errno. */
int  usbq_open(int vid, const int *pids, int npids, const char *iousb_path, int wait_ms, int ep_out,
               volatile sig_atomic_t *stop);
void usbq_close(void);
int  usbq_gone(void);           /* the adapter was unplugged */
int  usbq_pid(void);

/* Vendor control transfers, device recipient. 0 or an errno. */
int  usbq_ctrl_in(int request, int value, int index, void *data, int len);
int  usbq_ctrl_out(int request, int value, int index, const void *data, int len);

/* Bulk OUT on the write endpoint (serialised). 0 or an errno. */
int  usbq_bulk_out(const void *data, int len);

/* Bulk IN: one reader per thread, each with its own pipe, URB and buffer. */
typedef struct usbq_reader usbq_reader_t;
usbq_reader_t *usbq_reader_open(int ep_num);
/* > 0 = bytes (data valid until the next call), 0 = nothing within timeout_ms (the
 * read stays armed for the next call) or an empty transfer, < 0 = -errno */
int  usbq_read(usbq_reader_t *r, uint8_t **data, int timeout_ms);
void usbq_reader_close(usbq_reader_t *r);

#endif
