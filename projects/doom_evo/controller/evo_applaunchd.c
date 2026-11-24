/* evo_applaunchd - launch requests from HMI plugins (which can't spawn themselves).
 * Polls /dev/shmem/evo_applaunch.req; when present, reads the shell command line, deletes
 * the file, and runs it via spawnl(P_NOWAIT, /bin/sh -c ...). Runs as a normal process
 * (has process-creation ability, unlike HmiMain3D). Background it:
 *   (/fs/sda1/evo/evo_applaunchd >/dev/null 2>&1 &)
 */
#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <process.h>
#include <sys/wait.h>

#define REQ "/dev/shmem/evo_applaunch.req"

int main(void) {
    for (;;) {
        FILE* f = fopen(REQ, "r");
        if (f) {
            char cmd[1024]; cmd[0] = 0;
            if (!fgets(cmd, sizeof cmd, f)) cmd[0] = 0;
            fclose(f);
            unlink(REQ);                              /* consume the request */
            size_t n = strlen(cmd); if (n && cmd[n-1]=='\n') cmd[n-1]=0;
            if (cmd[0]) {
                char* argv[] = { (char*)"/bin/sh", (char*)"-c", cmd, (char*)0 };
                int pid = spawnv(P_NOWAIT, "/bin/sh", argv);   /* QNX native spawn, non-blocking */
                fprintf(stderr, "[applaunchd] ran: %s (pid=%d)\n", cmd, pid);
            }
        }
        usleep(500000);                               /* 0.5 s poll */
    }
    return 0;
}
