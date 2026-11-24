/* evo_sbtcp [iface] [major] - resolve a DSI service's TCP endpoint via the
 * servicebroker's SBAttachInterfaceTCP. Prints the server socket (ip:port) plus
 * the TCP-transport serverID/clientID. This is how a MoCCA-over-TCP client learns
 * where to connect (vs the qnet-native SBAttachInterface which returns a channel).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include "dsi/private/servicebroker.h"   /* STCPConnectionInfo, SPartyID */

extern int  SBOpen(const char*);
extern void SBClose(int);
extern int  SBAttachInterfaceTCP(int, const char*, int, int, struct STCPConnectionInfo*);
extern int  SBDetachInterface(int, SPartyID);

int main(int argc, char **argv) {
    const char *name = (argc > 1) ? argv[1] : "EnvPeriphery.DSYSEnvPeriphery";
    int maj = (argc > 2) ? atoi(argv[2]) : 0;   /* 0 = scan v1..20 */
    int h = SBOpen("/srv/servicebroker");
    printf("SBOpen=%d\n", h);
    if (h < 0) return 1;

    struct STCPConnectionInfo ci;
    int rc = -1, m;
    if (maj > 0) { memset(&ci, 0, sizeof ci); errno = 0; rc = SBAttachInterfaceTCP(h, name, maj, 0, &ci); }
    else for (m = 1; m <= 20; m++) {
        memset(&ci, 0, sizeof ci); errno = 0;
        rc = SBAttachInterfaceTCP(h, name, m, 0, &ci);
        if (rc == 0) { maj = m; break; }
    }
    printf("SBAttachInterfaceTCP(%s, v%d) rc=%d errno=%d(%s)\n",
           name, maj, rc, errno, strerror(errno));
    if (rc == 0) {
        unsigned ip = ci.socket.ipaddress;    /* host byte order per header */
        printf("  server socket: %u.%u.%u.%u : %u   (ip=0x%08x)\n",
               (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff,
               (unsigned)ci.socket.port, ip);
        printf("  serverID = 0x%08x / 0x%08x\n",
               (unsigned)ci.serverID.s.localID, (unsigned)ci.serverID.s.extendedID);
        printf("  clientID = 0x%08x / 0x%08x\n",
               (unsigned)ci.clientID.s.localID, (unsigned)ci.clientID.s.extendedID);
        printf("  ifVersion = %u.%u\n",
               (unsigned)ci.ifVersion.majorVersion, (unsigned)ci.ifVersion.minorVersion);
        SBDetachInterface(h, ci.clientID);
    }
    SBClose(h);
    return 0;
}
