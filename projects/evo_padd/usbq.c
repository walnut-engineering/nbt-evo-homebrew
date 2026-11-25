/* usbq.c - USB transport for evo_padd on QNX libusbdi.
 *
 * What the reference driver (xow) gets from libusb, on the head unit's io-usb:
 *   - attach by VID/PID          usbd_connect(ident) -> insertion callback -> usbd_attach
 *   - control pipe               usbd_open_pipe(device descriptor)
 *   - endpoints                  usbd_endpoint_descriptor() takes the endpoint ADDRESS
 *   - transfer buffers           must come from usbd_alloc()
 * Control transfers are synchronous usbd_io() calls. Bulk transfers use usbd_io()'s
 * completion callback and wait on a semaphore: with a NULL callback, usbd_urb_status()
 * on this io-usb can still show the PREVIOUS transfer's status and length when
 * usbd_io() returns (measured: reads cut short or seen twice, writes "in progress").
 * The callback is where the stack hands over the final values. Each pipe has its own
 * URB + buffer, and a mutex where more than one thread can reach it.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sys/usbdi.h>
#include <sys/usb100.h>

#include "usbq.h"
#include "padd.h"

#ifndef USB_VERSION
#define USB_VERSION 0x0100
#endif

#define CTRL_TIMEOUT_MS 1000
#define OUT_TIMEOUT_MS  1000
#define CTRL_BUF        64

/* one bulk transfer in flight: filled in by the completion callback */
typedef struct {
    sem_t             done;
    volatile _Uint32t status, len;
    volatile int      pending;
} xfer_t;

struct usbq_reader {
    struct usbd_pipe *pipe;
    struct usbd_urb  *urb;
    uint8_t          *buf;
    int               ep;
    xfer_t            x;
};

/* runs on libusbdi's event thread when a bulk transfer completes */
static void on_complete(struct usbd_urb *urb, struct usbd_pipe *pipe, void *handle)
{
    xfer_t *x = handle;
    _Uint32t st = 0, len = 0;
    (void)pipe;
    usbd_urb_status(urb, &st, &len);
    x->status = st; x->len = len;
    x->pending = 0;
    sem_post(&x->done);
}

/* wait for the callback; ms < 0 = for ever. 0 = completed, ETIMEDOUT otherwise */
static int xfer_wait(xfer_t *x, int ms)
{
    struct timespec ts;
    if (ms < 0) {
        while (sem_wait(&x->done) != 0) { if (errno != EINTR) return errno; }
        return 0;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    while (sem_timedwait(&x->done, &ts) != 0) { if (errno != EINTR) return errno; }
    return 0;
}

static int status_ok(_Uint32t st)
{
    return (st & USBD_URB_STATUS_MASK) == USBD_STATUS_CMP;
}

static struct usbd_connection *g_conn;
static struct usbd_device     *g_dev;
static volatile int            g_gone;
static int                     g_vid, g_pid_found = -1, g_npids;
static int                     g_pids[8];
static int                     g_path = -1, g_devno = -1;
static volatile int            g_attach_err;

static struct usbd_pipe *g_ctrl, *g_out;
static struct usbd_urb  *g_ctrl_urb, *g_out_urb;
static uint8_t          *g_ctrl_buf, *g_out_buf;
static pthread_mutex_t   g_ctrl_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t   g_out_mtx  = PTHREAD_MUTEX_INITIALIZER;
static xfer_t            g_out_x;
static int               g_out_dead;        /* a write never completed: the pipe is unusable */

static void insertion(struct usbd_connection *conn, usbd_device_instance_t *inst)
{
    struct usbd_device *dev;
    int i, rc, match = 0;

    /* Decide from the ids io-usb hands us, BEFORE attaching: an attach is exclusive,
     * so a resident daemon that attached to every new device "just to look" could
     * steal a USB stick or a phone from its real driver. */
    if (g_dev || (int)inst->ident.vendor != g_vid) return;
    for (i = 0; i < g_npids; i++) if ((int)inst->ident.device == g_pids[i]) match = 1;
    if (!match) return;

    rc = usbd_attach(conn, inst, 0, &dev);
    if (rc != EOK) {
        LOGE("usb: adapter %04x:%04x found but attach failed: %d (%s)%s", g_vid, (unsigned)inst->ident.device, rc, strerror(rc),
             rc == EBUSY ? " - is another evo_padd running?" : "");
        g_attach_err = rc;
        return;
    }
    g_pid_found = (int)inst->ident.device;
    g_path = inst->path; g_devno = inst->devno;
    g_dev = dev;
}

static void removal(struct usbd_connection *conn, usbd_device_instance_t *inst)
{
    (void)conn;
    if (g_dev && inst->path == g_path && inst->devno == g_devno) {
        g_gone = 1;
        LOGI("usb: adapter unplugged");
    }
}

int usbq_gone(void) { return g_gone; }
int usbq_pid(void)  { return g_pid_found; }

int usbq_open(int vid, const int *pids, int npids, const char *iousb_path, int wait_ms, int ep_out,
              volatile sig_atomic_t *stop)
{
    /* only our vendor's devices are announced to us; the product id is matched in the callback */
    static usbd_device_ident_t ident = { USBD_CONNECT_WILDCARD, USBD_CONNECT_WILDCARD, USBD_CONNECT_WILDCARD,
                                         USBD_CONNECT_WILDCARD, USBD_CONNECT_WILDCARD };
    static usbd_funcs_t funcs = { _USBDI_NFUNCS, insertion, removal, NULL };
    usbd_connect_parm_t parm;
    struct usbd_desc_node *n;
    usbd_endpoint_descriptor_t *ed;
    int rc, waited = 0;

    g_vid = vid;
    g_npids = npids > 8 ? 8 : npids;
    memcpy(g_pids, pids, sizeof(int) * g_npids);
    ident.vendor = (_Uint32t)vid;

    memset(&parm, 0, sizeof parm);
    parm.path = iousb_path; parm.vusb = USB_VERSION; parm.vusbd = USBD_VERSION; parm.flags = 0;
    parm.argc = 0; parm.argv = NULL; parm.evtbufsz = 0; parm.ident = &ident; parm.funcs = &funcs;
    parm.connect_wait = USBD_CONNECT_WAIT;
    rc = usbd_connect(&parm, &g_conn);
    if (rc != EOK) { LOGE("usb: usbd_connect failed: %d (%s)", rc, strerror(rc)); return rc; }

    /* insertion callbacks are asynchronous; wait_ms < 0 = wait for the adapter for ever */
    if (wait_ms < 0) LOGI("usb: waiting for an Xbox wireless adapter...");
    while (!g_dev && !g_attach_err && (wait_ms < 0 || waited < wait_ms) && !(stop && *stop)) { usleep(50000); waited += 50; }
    if (!g_dev) {
        if (!g_attach_err && !(stop && *stop)) LOGE("usb: no Xbox wireless adapter (%04x:02e6) found", vid);
        usbd_disconnect(g_conn); g_conn = NULL;
        return g_attach_err ? g_attach_err : ENODEV;
    }
    LOGI("usb: adapter %04x:%04x attached (path %d devno %d)", vid, g_pid_found, g_path, g_devno);

    rc = usbd_select_config(g_dev, 1);
    if (rc != EOK) LOGD("usb: select_config(1) -> %d (%s), continuing", rc, strerror(rc));

    rc = usbd_open_pipe(g_dev, (usbd_descriptors_t *)usbd_device_descriptor(g_dev, &n), &g_ctrl);
    if (rc != EOK) { LOGE("usb: open control pipe failed: %d (%s)", rc, strerror(rc)); return rc; }

    ed = usbd_endpoint_descriptor(g_dev, 1, 0, 0, ep_out & 0x0f, &n);
    if (!ed) { LOGE("usb: no bulk OUT endpoint 0x%02x", ep_out & 0x0f); return ENOENT; }
    rc = usbd_open_pipe(g_dev, (usbd_descriptors_t *)ed, &g_out);
    if (rc != EOK) { LOGE("usb: open bulk OUT 0x%02x failed: %d (%s)", ep_out & 0x0f, rc, strerror(rc)); return rc; }

    g_ctrl_urb = usbd_alloc_urb(NULL);
    g_out_urb  = usbd_alloc_urb(NULL);
    g_ctrl_buf = usbd_alloc(CTRL_BUF);
    g_out_buf  = usbd_alloc(USBQ_TX_MAX);
    if (!g_ctrl_urb || !g_out_urb || !g_ctrl_buf || !g_out_buf) { LOGE("usb: out of transfer memory"); return ENOMEM; }
    sem_init(&g_out_x.done, 0, 0);
    return EOK;
}

void usbq_close(void)
{
    if (g_out)  usbd_close_pipe(g_out);
    if (g_ctrl) usbd_close_pipe(g_ctrl);
    if (g_out_buf)  usbd_free(g_out_buf);
    if (g_ctrl_buf) usbd_free(g_ctrl_buf);
    if (g_out_urb)  usbd_free_urb(g_out_urb);
    if (g_ctrl_urb) usbd_free_urb(g_ctrl_urb);
    if (g_dev)  usbd_detach(g_dev);
    if (g_conn) usbd_disconnect(g_conn);
    g_out = g_ctrl = NULL; g_out_buf = g_ctrl_buf = NULL; g_out_urb = g_ctrl_urb = NULL;
    g_dev = NULL; g_conn = NULL;
}

static int ctrl_xfer(int in, int request, int value, int index, void *data, int len)
{
    _Uint32t st = 0, got = 0;
    int rc;

    if (len > CTRL_BUF) return EINVAL;
    if (g_gone) return ENODEV;
    pthread_mutex_lock(&g_ctrl_mtx);
    if (!in && len) memcpy(g_ctrl_buf, data, len);
    usbd_setup_vendor(g_ctrl_urb, len ? (in ? URB_DIR_IN : URB_DIR_OUT) : URB_DIR_NONE, request,
                      USB_TYPE_VENDOR | USB_RECIPIENT_DEVICE, value, index, len ? g_ctrl_buf : NULL, len);
    rc = usbd_io(g_ctrl_urb, g_ctrl, NULL, NULL, CTRL_TIMEOUT_MS);
    usbd_urb_status(g_ctrl_urb, &st, &got);
    if (rc == EOK && in) {
        if ((int)got != len) rc = EIO;
        else memcpy(data, g_ctrl_buf, len);
    }
    pthread_mutex_unlock(&g_ctrl_mtx);
    if (rc != EOK)
        LOGE("usb: control %s req 0x%02x val 0x%04x idx 0x%04x len %d failed: %d (%s) status 0x%08x",
             in ? "IN" : "OUT", request, value, index, len, rc, strerror(rc), (unsigned)st);
    return rc;
}

int usbq_ctrl_in(int request, int value, int index, void *data, int len)
{
    return ctrl_xfer(1, request, value, index, data, len);
}

int usbq_ctrl_out(int request, int value, int index, const void *data, int len)
{
    return ctrl_xfer(0, request, value, index, (void *)data, len);
}

int usbq_bulk_out(const void *data, int len)
{
    _Uint32t st = 0, sent = 0;
    int rc;

    if (len <= 0 || len > USBQ_TX_MAX) return EINVAL;
    if (g_gone) return ENODEV;
    pthread_mutex_lock(&g_out_mtx);
    if (g_out_dead) { pthread_mutex_unlock(&g_out_mtx); return EIO; }
    memcpy(g_out_buf, data, len);
    usbd_setup_bulk(g_out_urb, URB_DIR_OUT, g_out_buf, len);
    g_out_x.pending = 1;
    rc = usbd_io(g_out_urb, g_out, on_complete, &g_out_x, USBD_TIME_INFINITY);
    if (rc == EOK) {
        rc = xfer_wait(&g_out_x, OUT_TIMEOUT_MS);
        if (rc != 0) g_out_dead = 1;            /* still in flight: URB and buffer are not ours to reuse */
        else { st = g_out_x.status; sent = g_out_x.len; if (!status_ok(st) || (int)sent != len) rc = EIO; }
    }
    pthread_mutex_unlock(&g_out_mtx);
    LOGT("usb: bulk OUT %d bytes -> rc %d status 0x%08x len %u", len, rc, (unsigned)st, (unsigned)sent);
    if (rc != EOK)
        LOGE("usb: bulk OUT %d bytes failed: %d (%s) status 0x%08x sent %u", len, rc, strerror(rc), (unsigned)st, (unsigned)sent);
    return rc;
}

usbq_reader_t *usbq_reader_open(int ep_num)
{
    struct usbd_desc_node *n;
    usbd_endpoint_descriptor_t *ed;
    usbq_reader_t *r = calloc(1, sizeof *r);
    int rc;

    if (!r) return NULL;
    r->ep = 0x80 | (ep_num & 0x0f);
    ed = usbd_endpoint_descriptor(g_dev, 1, 0, 0, r->ep, &n);
    if (!ed) { LOGE("usb: no bulk IN endpoint 0x%02x", r->ep); free(r); return NULL; }
    rc = usbd_open_pipe(g_dev, (usbd_descriptors_t *)ed, &r->pipe);
    if (rc != EOK) { LOGE("usb: open bulk IN 0x%02x failed: %d (%s)", r->ep, rc, strerror(rc)); free(r); return NULL; }
    r->urb = usbd_alloc_urb(NULL);
    r->buf = usbd_alloc(USBQ_RX_MAX);
    if (!r->urb || !r->buf) { LOGE("usb: out of transfer memory (reader 0x%02x)", r->ep); usbq_reader_close(r); return NULL; }
    sem_init(&r->x.done, 0, 0);
    return r;
}

int usbq_read(usbq_reader_t *r, uint8_t **data, int timeout_ms)
{
    int rc;

    if (g_gone) return -ENODEV;
    /* A transfer that is still in flight from an earlier call stays in flight: we only
     * submit when the previous one has completed, and a timeout here just means "nothing
     * yet". The URB is never aborted or resubmitted while pending - on this io-usb both
     * usbd_io() timeouts and usbd_abort_pipe() misbehave (late completions on a reused
     * URB; an abort that never returns). */
    if (!r->x.pending) {
        usbd_setup_bulk(r->urb, URB_DIR_IN | URB_SHORT_XFER_OK, r->buf, USBQ_RX_MAX);
        r->x.pending = 1;
        rc = usbd_io(r->urb, r->pipe, on_complete, &r->x, USBD_TIME_INFINITY);
        if (rc != EOK) {
            r->x.pending = 0;
            if (!g_gone) LOGE("usb: bulk IN 0x%02x submit failed: %d (%s)", r->ep, rc, strerror(rc));
            return -rc;
        }
    }
    if (xfer_wait(&r->x, timeout_ms) != 0) return 0;
    if (!status_ok(r->x.status)) {
        if (!g_gone) LOGE("usb: bulk IN 0x%02x failed: status 0x%08x", r->ep, (unsigned)r->x.status);
        return -EIO;
    }
    *data = r->buf;
    return (int)r->x.len;
}

void usbq_reader_close(usbq_reader_t *r)
{
    if (!r) return;
    if (r->pipe) usbd_close_pipe(r->pipe);
    if (r->buf)  usbd_free(r->buf);
    if (r->urb)  usbd_free_urb(r->urb);
    free(r);
}
