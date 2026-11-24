/* evo_sbmap [regex] - enumerate DSI services and resolve each one's TCP endpoint
 * + serverID via SBAttachInterfaceTCP, printing name -> serverID -> ip:port.
 * Lets us map a wire serverID (e.g. the knob's, found by a spin capture) back to
 * the interface NAME we must attach. Optional regex filters the interface list. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <dsi/clientlib.h>
#include "dsi/private/servicebroker.h"

extern int  SBOpen(const char*);
extern void SBClose(int);
extern int  SBAttachInterfaceTCP(int, const char*, int, int, struct STCPConnectionInfo*);
extern int  SBDetachInterface(int, SPartyID);

int main(int argc, char **argv) {
    const char *re = (argc > 1) ? argv[1] : NULL;
    const int N = 2000;
    int h = SBOpen("/srv/servicebroker");
    if (h < 0) { printf("SBOpen failed\n"); return 1; }
    struct SFNDInterfaceDescription *ifs = calloc(N, sizeof *ifs);
    int out = 0, rc;
    if (re) rc = SBMatchInterfaceList(h, re, ifs, N, &out);
    else    rc = SBGetInterfaceList(h, ifs, N, &out);
    printf("list rc=%d count=%d\n", rc, out);

    for (int i = 0; i < out && i < N; i++) {
        if (!ifs[i].name[0]) continue;
        struct STCPConnectionInfo ci; memset(&ci, 0, sizeof ci);
        int r = SBAttachInterfaceTCP(h, ifs[i].name,
                                     ifs[i].version.majorVersion,
                                     ifs[i].version.minorVersion, &ci);
        if (r == 0) {
            unsigned ip = ci.socket.ipaddress;
            printf("0x%08x  %u.%u.%u.%u:%-5u  %-50s v%u.%u\n",
                   (unsigned)ci.serverID.s.localID,
                   (ip>>24)&0xff,(ip>>16)&0xff,(ip>>8)&0xff,ip&0xff,(unsigned)ci.socket.port,
                   ifs[i].name,(unsigned)ifs[i].version.majorVersion,(unsigned)ifs[i].version.minorVersion);
            SBDetachInterface(h, ci.clientID);
        } else {
            printf("   (no-tcp)  %-50s v%u.%u\n", ifs[i].name,
                   (unsigned)ifs[i].version.majorVersion,(unsigned)ifs[i].version.minorVersion);
        }
    }
    SBClose(h);
    return 0;
}
