/* evo_sblist [sbpath] [regexp] - enumerate DSI services registered with the
 * servicebroker (federated, so it includes Jacinto-provided services).
 *   evo_sblist                      list all services via /srv/servicebroker
 *   evo_sblist /srv/servicebroker  "(?i)control|rotar|zbe|input"   regex match
 * Uses the SDK convenience calls SBGetInterfaceList / SBMatchInterfaceList. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dsi/clientlib.h>

int main(int argc, char **argv) {
    const char *sb = (argc > 1) ? argv[1] : "/srv/servicebroker";
    const char *re = (argc > 2) ? argv[2] : NULL;
    const int N = 2000;
    int h = SBOpen(sb);
    printf("SBOpen(\"%s\") = %d\n", sb, h);
    if (h < 0) return 1;

    struct SFNDInterfaceDescription *ifs = calloc(N, sizeof *ifs);
    if (!ifs) { SBClose(h); return 2; }
    int out = 0, rc;
    errno = 0;
    if (re) rc = SBMatchInterfaceList(h, re, ifs, N, &out);
    else    rc = SBGetInterfaceList(h, ifs, N, &out);
    printf("%s rc=%d errno=%d(%s) count=%d\n",
           re ? "SBMatchInterfaceList" : "SBGetInterfaceList",
           rc, errno, strerror(errno), out);

    int i;
    for (i = 0; i < out && i < N; i++)
        if (ifs[i].name[0])
            printf("  %-52s v%u.%u\n", ifs[i].name,
                   (unsigned)ifs[i].version.majorVersion,
                   (unsigned)ifs[i].version.minorVersion);
    SBClose(h);
    return 0;
}
