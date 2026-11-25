/* evo_usbprobe.c - raw USB probe for the NBT EVO head unit (QNX libusbdi).
 *
 * The unit has no USB HID class driver (no devh-usb.so), so gamepads have to be
 * read raw through libusbdi. This tool is the plumbing that evo_padd is built on,
 * run standalone to see what a device actually sends:
 *
 *   1. connects to io-usb and lists EVERY device: VID:PID, class, interfaces,
 *      endpoints (so we know which interface/endpoint carries the pad reports);
 *   2. attaches to one device (default: the first Microsoft 045e non-hub, i.e. an
 *      Xbox pad or the Xbox 360 wireless receiver), opens the interrupt IN pipe of
 *      one interface and hex-dumps the reports it receives;
 *   3. optionally first sends the Xbox 360 wireless receiver's "inquire presence"
 *      packet (-x) and an LED pattern (-l N) on the interrupt OUT pipe;
 *   4. decodes Xbox 360 wireless frames into a human-readable line.
 *
 *   evo_usbprobe [-v vid] [-p pid] [-i iface] [-n reports] [-t ms] [-x] [-l led] [-P io-usb-path]
 *
 * Xbox 360 wireless receiver (045e:0719 / clones 045e:0291): 4 controller slots,
 * slot k = interfaces 2k (data) + 2k+1 (headset); data IN ep 0x81/0x83/0x85/0x87,
 * OUT 0x01/0x03/0x05/0x07, 32-byte frames. Frame[0]&0x08 = presence (frame[1]&0x80
 * connected); frame[1]==0x01 = input, the wired-360 report starts at frame+4.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/usbdi.h>
#include <sys/usb100.h>

#ifndef USB_VERSION
#define USB_VERSION 0x0100
#endif

static int g_vid = 0x045e, g_pid = -1, g_iface = 0, g_n = 40, g_tmo = 4000, g_inq = 0, g_led = -1, g_chip = 0;
static const char *g_path = NULL;
static struct usbd_connection *g_conn;
static struct usbd_device *g_dev;      /* the chosen device, kept attached */
static int g_devs = 0;

static const char *cls(int c)
{
    switch (c) {
    case 0x00: return "per-interface"; case 0x03: return "HID"; case 0x08: return "mass-storage";
    case 0x09: return "hub"; case 0x0a: return "cdc-data"; case 0xff: return "vendor"; default: return "";
    }
}

static void dump(struct usbd_device *dev, usbd_device_descriptor_t *dd)
{
    struct usbd_desc_node *n;
    int c, i, e;
    for (c = 1; c <= dd->bNumConfigurations; c++) {
        usbd_configuration_descriptor_t *cd = usbd_configuration_descriptor(dev, c, &n);
        if (!cd) continue;
        printf("  config %d: %d interface(s), max %d mA\n", c, cd->bNumInterfaces, cd->MaxPower * 2);
        for (i = 0; i < cd->bNumInterfaces; i++) {
            usbd_interface_descriptor_t *id = usbd_interface_descriptor(dev, c, i, 0, &n);
            if (!id) continue;
            printf("    iface %d: class %02x/%02x/%02x %s, %d endpoint(s)\n", i, id->bInterfaceClass,
                   id->bInterfaceSubClass, id->bInterfaceProtocol, cls(id->bInterfaceClass), id->bNumEndpoints);
            /* usbd_endpoint_descriptor() looks an endpoint up by its ADDRESS (0x01..0x0f OUT,
             * 0x81..0x8f IN), not by index - so walk both address ranges */
            for (e = 1; e < 32; e++) {
                int addr = e < 16 ? e : (0x80 | (e - 16));
                usbd_endpoint_descriptor_t *ed = (addr & 0x0f) ? usbd_endpoint_descriptor(dev, c, i, 0, addr, &n) : NULL;
                int t;
                if (!ed || ed->bEndpointAddress != addr) continue;
                t = ed->bmAttributes & 3;
                printf("      ep 0x%02x %s %-9s maxpkt %u interval %u\n", ed->bEndpointAddress,
                       (ed->bEndpointAddress & 0x80) ? "IN " : "OUT",
                       t == 3 ? "interrupt" : t == 2 ? "bulk" : t == 1 ? "isoch" : "control",
                       (unsigned)ed->wMaxPacketSize, ed->bInterval);
            }
        }
    }
}

static void insertion(struct usbd_connection *conn, usbd_device_instance_t *inst)
{
    struct usbd_device *dev;
    struct usbd_desc_node *n;
    usbd_device_descriptor_t *dd;
    int rc = usbd_attach(conn, inst, 0, &dev);
    if (rc != EOK) { printf("device (path %d devno %d): usbd_attach failed %d\n", inst->path, inst->devno, rc); return; }
    dd = usbd_device_descriptor(dev, &n);
    g_devs++;
    printf("device path %d devno %d: %04x:%04x class %02x/%02x/%02x %s, %d config(s)\n", inst->path, inst->devno,
           dd->idVendor, dd->idProduct, dd->bDeviceClass, dd->bDeviceSubClass, dd->bDeviceProtocol,
           cls(dd->bDeviceClass), dd->bNumConfigurations);
    dump(dev, dd);
    if (!g_dev && dd->idVendor == g_vid && (g_pid < 0 || dd->idProduct == g_pid) && dd->bDeviceClass != 9) {
        g_dev = dev;
        printf("  -> selected for probing\n");
    } else {
        usbd_detach(dev);
    }
}

static void removal(struct usbd_connection *conn, usbd_device_instance_t *inst)
{
    (void)conn;
    printf("device removed (path %d devno %d)\n", inst->path, inst->devno);
}

/* ---- MT76 chip check (Xbox One wireless adapter 045e:02e6 / 02fe) ------------------
 * Proves the three things a driver port needs from libusbdi on this unit: vendor
 * control transfers on EP0, and that the bulk IN/OUT pipes open. Register reads only;
 * nothing is written to the device. */
static int vend_read(struct usbd_pipe *ctrl, struct usbd_urb *urb, unsigned char *buf,
                     int req, int index, const char *name)
{
    _Uint32t st = 0, len = 0;
    int rc;
    memset(buf, 0, 4);
    usbd_setup_vendor(urb, URB_DIR_IN, req, USB_TYPE_VENDOR | USB_RECIPIENT_DEVICE, 0, index, buf, 4);
    rc = usbd_io(urb, ctrl, NULL, NULL, 1000);
    usbd_urb_status(urb, &st, &len);
    if (rc != EOK) { printf("  %-28s req 0x%02x idx 0x%04x -> rc %d (%s) status 0x%08x\n", name, req, index, rc, strerror(rc), (unsigned)st); return rc; }
    printf("  %-28s req 0x%02x idx 0x%04x -> 0x%02x%02x%02x%02x  (len %u)\n", name, req, index, buf[3], buf[2], buf[1], buf[0], (unsigned)len);
    return EOK;
}

static void chip_check(void)
{
    struct usbd_desc_node *n;
    struct usbd_pipe *ctrl = NULL, *p;
    struct usbd_urb *urb = usbd_alloc_urb(NULL);
    unsigned char *buf = usbd_alloc(64);
    static const int eps[] = { 0x84, 0x85, 0x04, 0x05 };
    int rc, i;

    rc = usbd_open_pipe(g_dev, (usbd_descriptors_t *)usbd_device_descriptor(g_dev, &n), &ctrl);
    printf("open control pipe (device descriptor) -> %d\n", rc);
    if (rc != EOK) {
        usbd_endpoint_descriptor_t *e0 = usbd_endpoint_descriptor(g_dev, 1, 0, 0, 0, &n);
        rc = e0 ? usbd_open_pipe(g_dev, (usbd_descriptors_t *)e0, &ctrl) : ENOENT;
        printf("open control pipe (ep0 descriptor) -> %d\n", rc);
    }
    if (rc == EOK) {
        printf("MT76 register reads (vendor IN control transfers):\n");
        vend_read(ctrl, urb, buf, 0x07, 0x0000, "MT_ASIC_VERSION");          /* MT_VEND_MULTI_READ */
        vend_read(ctrl, urb, buf, 0x07, 0x1000, "MT_MAC_CSR0 (MAC version)");
        vend_read(ctrl, urb, buf, 0x47, 0x0230, "MT_FCE_DMA_ADDR (fw state)"); /* MT_VEND_READ_CFG; 0 = no firmware */
        vend_read(ctrl, urb, buf, 0x47, 0x9018, "MT_USB_U3DMA_CFG");
    }
    printf("bulk pipes:\n");
    for (i = 0; i < 4; i++) {
        usbd_endpoint_descriptor_t *ed = usbd_endpoint_descriptor(g_dev, 1, 0, 0, eps[i], &n);
        if (!ed) { printf("  ep 0x%02x: no descriptor\n", eps[i]); continue; }
        p = NULL;
        rc = usbd_open_pipe(g_dev, (usbd_descriptors_t *)ed, &p);
        printf("  ep 0x%02x (%s, maxpkt %u): open -> %d\n", eps[i], (ed->bmAttributes & 3) == 2 ? "bulk" : "other", (unsigned)ed->wMaxPacketSize, rc);
        if (rc == EOK && p) usbd_close_pipe(p);
    }
    if (ctrl) usbd_close_pipe(ctrl);
    usbd_free(buf);
    usbd_free_urb(urb);
}

static void hexdump(const unsigned char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) printf("%02x%s", b[i], (i % 16 == 15 || i == n - 1) ? "\n" : " ");
}

/* Xbox 360 wireless receiver frame -> readable line */
static void decode360w(const unsigned char *d, int n)
{
    if (n >= 2 && (d[0] & 0x08)) {
        printf("    [360W] presence: %s\n", (d[1] & 0x80) ? "CONTROLLER CONNECTED" : (d[1] & 0x40) ? "headset only" : "controller gone");
        return;
    }
    if (n >= 20 && d[0] == 0x00 && d[1] == 0x01 && d[2] == 0x00 && d[3] == 0xf0) {
        const unsigned char *p = d + 4;     /* wired-360 layout */
        short lx = (short)(p[6] | (p[7] << 8)), ly = (short)(p[8] | (p[9] << 8));
        short rx = (short)(p[10] | (p[11] << 8)), ry = (short)(p[12] | (p[13] << 8));
        printf("    [360W] dpad%s%s%s%s start%d back%d LS%d RS%d | LB%d RB%d guide%d A%d B%d X%d Y%d | LT%3d RT%3d | L(%6d,%6d) R(%6d,%6d)\n",
               (p[2] & 0x01) ? " up" : "", (p[2] & 0x02) ? " down" : "", (p[2] & 0x04) ? " left" : "", (p[2] & 0x08) ? " right" : "",
               !!(p[2] & 0x10), !!(p[2] & 0x20), !!(p[2] & 0x40), !!(p[2] & 0x80),
               !!(p[3] & 0x01), !!(p[3] & 0x02), !!(p[3] & 0x04), !!(p[3] & 0x10), !!(p[3] & 0x20), !!(p[3] & 0x40), !!(p[3] & 0x80),
               p[4], p[5], lx, ly, rx, ry);
    }
}

int main(int argc, char **argv)
{
    usbd_device_ident_t ident = { USBD_CONNECT_WILDCARD, USBD_CONNECT_WILDCARD, USBD_CONNECT_WILDCARD,
                                  USBD_CONNECT_WILDCARD, USBD_CONNECT_WILDCARD };
    usbd_funcs_t funcs = { _USBDI_NFUNCS, insertion, removal, NULL };
    usbd_connect_parm_t parm;
    int rc, o, i;

    while ((o = getopt(argc, argv, "v:p:i:n:t:xl:P:c")) != -1) {
        switch (o) {
        case 'v': g_vid = (int)strtol(optarg, 0, 0); break;
        case 'p': g_pid = (int)strtol(optarg, 0, 0); break;
        case 'i': g_iface = atoi(optarg); break;
        case 'n': g_n = atoi(optarg); break;
        case 't': g_tmo = atoi(optarg); break;
        case 'x': g_inq = 1; break;
        case 'c': g_chip = 1; break;
        case 'l': g_led = atoi(optarg); break;
        case 'P': g_path = optarg; break;
        default:
            fprintf(stderr, "usage: %s [-v vid] [-p pid] [-i iface] [-n reports] [-t ms] [-x inquire] [-l led] [-c mt76-chip-check] [-P io-usb-path]\n", argv[0]);
            return 2;
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);

    memset(&parm, 0, sizeof parm);
    parm.path = g_path; parm.vusb = USB_VERSION; parm.vusbd = USBD_VERSION; parm.flags = 0;
    parm.argc = 0; parm.argv = NULL; parm.evtbufsz = 0; parm.ident = &ident; parm.funcs = &funcs;
    parm.connect_wait = USBD_CONNECT_WAIT;
    rc = usbd_connect(&parm, &g_conn);
    if (rc != EOK) { printf("usbd_connect(%s) failed: %d (%s)\n", g_path ? g_path : "default", rc, strerror(rc)); return 1; }
    printf("connected to io-usb (%s); enumerating...\n", g_path ? g_path : "default");
    for (i = 0; i < 30 && !g_dev; i++) usleep(100000);      /* insertion callbacks arrive asynchronously */
    usleep(300000);
    printf("%d device(s) seen\n", g_devs);
    if (!g_dev) {
        printf("no device matching %04x:%s to probe\n", g_vid, g_pid < 0 ? "any" : "pid");
        usbd_disconnect(g_conn);
        return 0;
    }

    {
        struct usbd_desc_node *n;
        usbd_endpoint_descriptor_t *ein = NULL, *eout = NULL, *ed;
        usbd_interface_descriptor_t *id;
        struct usbd_pipe *pin = NULL, *pout = NULL;
        struct usbd_urb *urb;
        unsigned char *buf;
        _Uint32t st, len;
        int e, k, got = 0, maxpkt;

        rc = usbd_select_config(g_dev, 1);
        if (rc != EOK) printf("select_config(1) -> %d (%s) - continuing\n", rc, strerror(rc));
        if (g_chip) { chip_check(); goto out; }
        id = usbd_interface_descriptor(g_dev, 1, g_iface, 0, &n);
        if (!id) { printf("no interface %d\n", g_iface); goto out; }
        for (e = 0; e < 32; e++) {
            ed = usbd_endpoint_descriptor(g_dev, 1, g_iface, 0, e, &n);
            if (!ed || ed->bEndpointAddress == 0 || (ed->bmAttributes & 3) != 3) continue;
            if (ed->bEndpointAddress & 0x80) { if (!ein) ein = ed; } else { if (!eout) eout = ed; }
        }
        if (!ein) { printf("interface %d has no interrupt IN endpoint\n", g_iface); goto out; }
        rc = usbd_select_interface(g_dev, g_iface, 0);
        printf("select_interface(%d) -> %d\n", g_iface, rc);

        urb = usbd_alloc_urb(NULL);
        buf = usbd_alloc(64);
        maxpkt = ein->wMaxPacketSize > 64 ? 64 : ein->wMaxPacketSize;
        rc = usbd_open_pipe(g_dev, (usbd_descriptors_t *)ein, &pin);
        printf("open IN pipe 0x%02x -> %d\n", ein->bEndpointAddress, rc);
        if (rc != EOK) goto out;
        if (eout && (g_inq || g_led >= 0)) {
            rc = usbd_open_pipe(g_dev, (usbd_descriptors_t *)eout, &pout);
            printf("open OUT pipe 0x%02x -> %d\n", eout->bEndpointAddress, rc);
        }
        if (pout && g_inq) {      /* 360W: ask the receiver to announce controller presence */
            static const unsigned char inq[12] = { 0x08, 0x00, 0x0F, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0 };
            memcpy(buf, inq, 12);
            usbd_setup_interrupt(urb, URB_DIR_OUT, buf, 12);
            rc = usbd_io(urb, pout, NULL, NULL, 1000);
            usbd_urb_status(urb, &st, &len);
            printf("360W inquire-presence -> rc %d status 0x%08x len %u\n", rc, (unsigned)st, (unsigned)len);
        }
        if (pout && g_led >= 0) {  /* 360W LED ring: 0x40 | pattern (6 = player 1 solid) */
            unsigned char led[12] = { 0x00, 0x00, 0x08, 0x40, 0, 0, 0, 0, 0, 0, 0, 0 };
            led[3] = (unsigned char)(0x40 | (g_led & 0x0f));
            memcpy(buf, led, 12);
            usbd_setup_interrupt(urb, URB_DIR_OUT, buf, 12);
            rc = usbd_io(urb, pout, NULL, NULL, 1000);
            usbd_urb_status(urb, &st, &len);
            printf("360W LED %d -> rc %d status 0x%08x len %u\n", g_led, rc, (unsigned)st, (unsigned)len);
        }
        printf("reading up to %d reports (%d ms timeout each) - press buttons on the pad...\n", g_n, g_tmo);
        for (k = 0; k < g_n; k++) {
            usbd_setup_interrupt(urb, URB_DIR_IN | URB_SHORT_XFER_OK, buf, maxpkt);
            rc = usbd_io(urb, pin, NULL, NULL, g_tmo);
            usbd_urb_status(urb, &st, &len);
            if (rc != EOK) { printf("  (io rc %d status 0x%08x: %s)\n", rc, (unsigned)st, rc == ETIMEDOUT ? "timeout" : strerror(rc)); continue; }
            got++;
            printf("  report %d (%u B): ", got, (unsigned)len);
            hexdump(buf, (int)len);
            decode360w(buf, (int)len);
        }
        printf("%d report(s) received\n", got);
        if (pout) usbd_close_pipe(pout);
        usbd_close_pipe(pin);
        usbd_free(buf);
        usbd_free_urb(urb);
    }
out:
    usbd_detach(g_dev);
    usbd_disconnect(g_conn);
    return 0;
}
