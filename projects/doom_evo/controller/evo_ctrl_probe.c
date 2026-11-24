/* evo_ctrl_probe - safe, read-only, non-blocking tap of the NBT controller IPC
 * channel (/dev/ipc/ipc27) to learn the iDrive rotary/button message format.
 * Self-terminates after N seconds; never blocks the HMI. Seed for DOOM's
 * controller input module. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>

int main(int argc, char **argv) {
    const char *dev = (argc > 1) ? argv[1] : "/dev/ipc/ipc27";
    int secs = (argc > 2) ? atoi(argv[2]) : 20;
    const char *mode = (argc > 3) ? argv[3] : "rw";
    int flags = (strcmp(mode,"ro")==0 ? O_RDONLY : O_RDWR) | O_NONBLOCK;
    int fd = open(dev, flags);
    if (fd < 0) {
        printf("OPEN %s FAILED: %s\n", dev, strerror(errno));
        return 1;
    }
    printf("OPEN %s ok fd=%d listening %ds (non-blocking, read-only)\n", dev, fd, secs);
    fflush(stdout);

    time_t start = time(NULL);
    unsigned char buf[1024];
    long msgs = 0, total = 0;
    while (time(NULL) - start < secs) {
        fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 150000;
        int r = select(fd + 1, &rf, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(fd, &rf)) {
            int n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
                msgs++; total += n;
                printf("[%ld.%03ld] n=%d:", (long)(ts.tv_sec), ts.tv_nsec/1000000L, n);
                int i; for (i = 0; i < n && i < 80; i++) printf(" %02x", buf[i]);
                printf("\n"); fflush(stdout);
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                printf("READ ERR: %s\n", strerror(errno));
                if (errno != EINTR) break;
            }
        } else if (r < 0 && errno != EINTR) {
            printf("SELECT ERR: %s\n", strerror(errno));
            break;
        }
    }
    printf("DONE msgs=%ld bytes=%ld\n", msgs, total);
    close(fd);
    return 0;
}
