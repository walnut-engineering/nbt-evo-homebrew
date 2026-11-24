/* mouse_dump - print timestamped raw packets from /dev/usbmouse0 (QNX devu-mouse) to find the encoding. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
int main(int argc, char **argv){
    const char *dev = argc > 1 ? argv[1] : "/dev/usbmouse0";
    int secs = argc > 2 ? atoi(argv[2]) : 20;
    unsigned char buf[64]; struct timespec t0, t; int fd = open(dev, O_RDONLY);
    if (fd < 0) { printf("open %s failed errno=%d\n", dev, errno); return 1; }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        int n = read(fd, buf, sizeof buf), i;
        clock_gettime(CLOCK_MONOTONIC, &t);
        long ms = (t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000;
        if (ms > secs * 1000L) break;
        if (n <= 0) { usleep(5000); continue; }
        printf("%6ld n=%d", ms, n);
        for (i = 0; i < n; i++) printf(" %02x", buf[i]);
        printf("\n"); fflush(stdout);
    }
    close(fd); return 0;
}
