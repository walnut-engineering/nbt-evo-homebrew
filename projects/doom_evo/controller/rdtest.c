/* rdtest - which way can a blocked read on /dev/usbmouse0 or /dev/usbkbd0 be bounded?
 * 1) select() with timeout  2) TimerTimeout(_NTO_TIMEOUT_REPLY) before read()  3) readcond() timeout */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/neutrino.h>
#include <time.h>
#include <stdint.h>
static long ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000+t.tv_nsec/1000000; }
int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "/dev/usbmouse0";
    unsigned char buf[64]; int fd = open(dev, O_RDONLY); long t0;
    if (fd < 0) { perror("open"); return 1; }
    { fd_set r; struct timeval tv = {1, 0}; FD_ZERO(&r); FD_SET(fd, &r); t0 = ms();
      int rc = select(fd + 1, &r, 0, 0, &tv); printf("select: rc=%d errno=%d after %ld ms\n", rc, errno, ms() - t0); }
    { uint64_t to = 1000000000ULL; t0 = ms();
      TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_REPLY | _NTO_TIMEOUT_SEND, NULL, &to, NULL);
      int rc = read(fd, buf, sizeof buf); printf("TimerTimeout+read: rc=%d errno=%d (%s) after %ld ms\n", rc, errno, strerror(errno), ms() - t0); }
    { t0 = ms(); int rc = readcond(fd, buf, sizeof buf, 1, 0, 10);
      printf("readcond(timeout 1s): rc=%d errno=%d (%s) after %ld ms\n", rc, errno, strerror(errno), ms() - t0); }
    return 0;
}
