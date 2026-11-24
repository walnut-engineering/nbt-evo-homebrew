/* evo_ipctap2 - watch ALL /dev/ipc/ipc25..28 at once (passive read), to find
 * which channel (if any) delivers controller data to a second reader.
 *   evo_ipctap2 [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>

#define NCH 4
int main(int argc, char **argv){
    int secs = (argc > 1) ? atoi(argv[1]) : 18;
    const char *names[NCH] = {"ipc25","ipc26","ipc27","ipc28"};
    int fds[NCH];
    int i, maxfd = -1, total = 0;
    time_t t0;
    for (i = 0; i < NCH; i++){
        char path[64];
        snprintf(path, sizeof path, "/dev/ipc/%s", names[i]);
        fds[i] = open(path, O_RDONLY | O_NONBLOCK);
        printf("open %s -> fd=%d%s\n", names[i], fds[i],
               fds[i] < 0 ? strerror(errno) : "");
        if (fds[i] > maxfd) maxfd = fds[i];
    }
    fflush(stdout);
    t0 = time(NULL);
    while (time(NULL) - t0 < secs){
        fd_set rf; struct timeval tv; int s;
        FD_ZERO(&rf);
        for (i = 0; i < NCH; i++) if (fds[i] >= 0) FD_SET(fds[i], &rf);
        tv.tv_sec = 0; tv.tv_usec = 200000;
        s = select(maxfd + 1, &rf, NULL, NULL, &tv);
        if (s <= 0) continue;
        for (i = 0; i < NCH; i++){
            if (fds[i] >= 0 && FD_ISSET(fds[i], &rf)){
                unsigned char buf[512];
                int n = read(fds[i], buf, sizeof buf);
                if (n > 0){
                    struct timespec ts; int j;
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    total += n;
                    printf("%ld.%03ld %s n=%d:", (long)ts.tv_sec, ts.tv_nsec/1000000L, names[i], n);
                    for (j = 0; j < n && j < 72; j++) printf(" %02x", buf[j]);
                    printf("\n"); fflush(stdout);
                }
            }
        }
    }
    printf("done: %d bytes total across all channels\n", total);
    for (i = 0; i < NCH; i++) if (fds[i] >= 0) close(fds[i]);
    return 0;
}
