/* evo_padd.c - Xbox wireless controller daemon for the NBT EVO head unit.
 *
 * Drives an Xbox One Wireless Adapter (045e:02e6) plugged into the unit: uploads the
 * adapter's firmware, runs the small 802.11 access point the controllers associate
 * with, speaks GIP to them and hands their input to the homebrew launcher and apps.
 * The unit has no USB HID or Wi-Fi driver for any of this, so it all lives here, on
 * raw libusbdi. A port of xow (https://github.com/medusalix/xow) to QNX and C.
 *
 *   evo_padd [-v|-vv|-vvv] [-f firmware] [-p] [-n] [-w] [-t seconds] [-S stage] [-P io-usb-path]
 *     -v   trace controller input; -vv protocol detail; -vvv every received transfer
 *     -f   firmware file (default /fs/sda1/evo/xbox/FW_ACC_00U.bin - not shipped)
 *     -p   start in pairing mode (same as pressing the adapter's button)
 *     -n   no knob events: only publish the pad state (/dev/shmem/evo_pad)
 *     -w   wait for the adapter to be plugged in instead of giving up after 5 s
 *          (boot-time use; the daemon exits when the adapter is unplugged, so run
 *          it from a restart loop)
 *     -t   exit after this many seconds (testing)
 *     -S   stop after bring-up stage: 1 = firmware upload only
 *   SIGUSR1 = enter pairing mode, SIGTERM/SIGINT = controllers off, radio off, exit.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#include "padd.h"
#include "usbq.h"
#include "mt76.h"
#include "dongle.h"

#define DONGLE_VID          0x045e
#define DONGLE_PID_OLD      0x02e6
#define DONGLE_PID_NEW      0x02fe
#define DONGLE_PID_SURFACE  0x091e
#define DEFAULT_FIRMWARE    "/fs/sda1/evo/xbox/FW_ACC_00U.bin"
#define PAIRING_TIMEOUT_MS   30000u

int g_verbose = 0;

static volatile sig_atomic_t g_stop, g_pair_req;
static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_t0;

uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

void plog(int level, const char *fmt, ...)
{
    va_list ap;
    uint64_t t;
    if (level >= 2 && g_verbose < level - 1) return;
    t = now_ms() - g_t0;
    pthread_mutex_lock(&g_log_mtx);
    printf("[%5u.%03u] %s", (unsigned)(t / 1000), (unsigned)(t % 1000), level == 0 ? "ERROR " : "");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    pthread_mutex_unlock(&g_log_mtx);
}

const char *mac_str(const uint8_t *a, char buf[18])
{
    snprintf(buf, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
    return buf;
}

void hex_str(const uint8_t *d, int n, char *out, int outsz)
{
    int i, o = 0;
    out[0] = 0;
    for (i = 0; i < n && o + 4 < outsz; i++) o += snprintf(out + o, outsz - o, "%02x%s", d[i], i + 1 < n ? " " : "");
}

static void on_signal(int sig)
{
    if (sig == SIGUSR1) g_pair_req = 1;
    else g_stop = 1;
}

typedef struct { usbq_reader_t *r; int ep; } rx_arg_t;

static void *rx_thread(void *arg)
{
    rx_arg_t *a = arg;
    int errors = 0;
    uint8_t *data;

    while (!g_stop) {
        int n = usbq_read(a->r, &data, 500);
        if (n > 0) { errors = 0; dongle_handle_bulk(data, n, a->ep); }
        else if (n < 0) {
            if (g_stop) break;
            if (usbq_gone() || ++errors >= 5) { g_stop = 1; break; }
            usleep(100000);
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    static const int pids[] = { DONGLE_PID_OLD, DONGLE_PID_NEW, DONGLE_PID_SURFACE };
    const char *firmware = DEFAULT_FIRMWARE, *iousb = NULL;
    int pair = 0, seconds = 0, stage = 0, wait_adapter = 0, o, rc = 1;
    usbq_reader_t *r_evt = NULL, *r_pkt = NULL;
    rx_arg_t a_evt, a_pkt;
    pthread_t t_evt, t_pkt;
    int have_evt = 0, have_pkt = 0;
    uint64_t t_end, t_pair = 0;

    while ((o = getopt(argc, argv, "vf:pnwt:S:P:")) != -1) {
        switch (o) {
        case 'v': g_verbose++; break;
        case 'f': firmware = optarg; break;
        case 'p': pair = 1; break;
        case 'n': padout_use_feed(0); break;
        case 'w': wait_adapter = 1; break;
        case 't': seconds = atoi(optarg); break;
        case 'S': stage = atoi(optarg); break;
        case 'P': iousb = optarg; break;
        default:
            fprintf(stderr, "usage: %s [-v|-vv|-vvv] [-f firmware] [-p] [-n] [-w] [-t seconds] [-S stage] [-P io-usb-path]\n", argv[0]);
            return 2;
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    g_t0 = now_ms();
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGUSR1, on_signal);
    signal(SIGHUP, SIG_IGN);

    LOGI("evo_padd: Xbox wireless adapter daemon (a QNX port of xow)");
    if (usbq_open(DONGLE_VID, pids, 3, iousb, wait_adapter ? -1 : 5000, MT_EP_WRITE, &g_stop) != 0) goto out;
    if (mt76_init(firmware, stage) != 0) goto out;
    if (stage == 1) { rc = 0; goto out; }

    padout_init();
    r_evt = usbq_reader_open(MT_EP_READ);
    r_pkt = usbq_reader_open(MT_EP_READ_PACKET);
    if (!r_evt || !r_pkt) goto down;
    a_evt.r = r_evt; a_evt.ep = MT_EP_READ;
    a_pkt.r = r_pkt; a_pkt.ep = MT_EP_READ_PACKET;
    if (pthread_create(&t_evt, NULL, rx_thread, &a_evt) != 0) { LOGE("cannot start the event thread"); goto down; }
    have_evt = 1;
    if (pthread_create(&t_pkt, NULL, rx_thread, &a_pkt) != 0) { LOGE("cannot start the packet thread"); goto down; }
    have_pkt = 1;
    LOGI("evo_padd: ready - turn a paired controller on, or press the adapter's button to pair");

    if (pair) g_pair_req = 1;
    t_end = seconds > 0 ? now_ms() + (uint64_t)seconds * 1000u : 0;
    while (!g_stop) {
        if (g_pair_req) { g_pair_req = 0; mt76_set_pairing(1); }
        /* pairing mode ends by itself (as in xone); the button restarts the clock */
        if (mt76_pairing()) {
            if (!t_pair) t_pair = now_ms() + PAIRING_TIMEOUT_MS;
            else if (now_ms() >= t_pair) { t_pair = 0; mt76_set_pairing(0); }
        } else t_pair = 0;
        if (usbq_gone()) break;
        if (t_end && now_ms() >= t_end) break;
        padout_tick();
        usleep(20000);
    }
    rc = 0;

down:
    LOGI("evo_padd: shutting down");
    g_stop = 1;
    if (!usbq_gone()) {
        dongle_shutdown();          /* controllers off */
        mt76_shutdown();            /* LED off, radio off */
    }
    padout_shutdown();
    /* The receive threads are still blocked in usbd_io(), and on this unit's io-usb
     * there is no safe way to get them out: a read with a timeout leaves its URB
     * pending (the late completion then crashes libusbdi's event thread once the URB
     * is freed), and usbd_abort_pipe() can block forever. So leave: process exit
     * closes the io-usb connection and the server reclaims the pipes and transfers. */
    fflush(stdout);
    _exit(rc);
out:
    usbq_close();
    return rc;
}
