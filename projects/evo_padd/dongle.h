/* dongle.h - receive path: MCU events and 802.11 frames -> controllers.
 * A C port of xow's dongle/dongle.{h,cpp} (Copyright (C) 2019 Medusalix).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EVO_PADD_DONGLE_H
#define EVO_PADD_DONGLE_H

#include <stdint.h>

void dongle_handle_bulk(const uint8_t *data, int n, int ep);    /* one bulk IN transfer */
void dongle_shutdown(void);                                     /* power off + remove every controller */
int  dongle_connected(void);                                    /* controllers currently associated */

#endif
