/* evo_ipctap - gently tap a /dev/ipc/* channel to observe the V850(IOC)->OMAP
 * stream (where the iDrive controller surfaces on the HU side).
 *
 * Opens the channel READ-ONLY + non-blocking, reads via select() with a timeout,
 * hexdumps each chunk with a timestamp, and exits after N seconds. If the open
 * happens to steal messages from touchComDevice the HMI controller may pause;
 * exiting (which this does automatically) restores it.
 *
 *   evo_ipctap [/dev/ipc/ipc27] [seconds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>

int main(int argc, char **argv){
    const char *dev = (argc > 1) ? argv[1] : "/dev/ipc/ipc27";
    int secs = (argc > 2) ? atoi(argv[2]) : 15;
    unsigned char buf[1024];
    int total = 0, reads = 0;
    time_t t0;
    int fd = open(dev, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("open %s failed: errno=%d (%s)\n", dev, errno, strerror(errno));
        return 1;
    }
    printf("opened %s fd=%d, reading %ds (operate the controller now)\n", dev, fd, secs);
    fflush(stdout);
    t0 = time(NULL);
    while (time(NULL) - t0 < secs) {
        fd_set rf; struct timeval tv;
        int s;
        FD_ZERO(&rf); FD_SET(fd, &rf);
        tv.tv_sec = 0; tv.tv_usec = 200000; /* 200 ms */
        s = select(fd + 1, &rf, NULL, NULL, &tv);
        if (s > 0 && FD_ISSET(fd, &rf)) {
            int n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                struct timespec ts; int i;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                reads++; total += n;
                printf("%ld.%03ld n=%3d:", (long)ts.tv_sec, ts.tv_nsec / 1000000L, n);
                for (i = 0; i < n && i < 72; i++) printf(" %02x", buf[i]);
                printf("\n"); fflush(stdout);
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                printf("read error: errno=%d (%s)\n", errno, strerror(errno));
                break;
            }
        } else if (s < 0 && errno != EINTR) {
            printf("select error: errno=%d (%s)\n", errno, strerror(errno));
            break;
        }
    }
    printf("done: %d reads, %d bytes total from %s\n", reads, total, dev);
    close(fd);
    return 0;
}
