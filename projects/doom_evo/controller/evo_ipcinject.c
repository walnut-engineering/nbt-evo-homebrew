/* evo_ipcinject - write (inject) a frame onto a /dev/ipc/* channel, to test
 * whether controller events can be replayed to drive the HMI.
 *
 *   evo_ipcinject <dev> <hexbytes> [count] [delay_ms]
 *   e.g. evo_ipcinject /dev/ipc/ipc25 "00 00 f1 04 ..." 5 150
 *
 * Opens O_RDWR, parses the space/comma-separated hex into a buffer, and writes
 * it `count` times with `delay_ms` between writes. Prints each write's result.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

int main(int argc, char **argv){
    unsigned char buf[512];
    int len = 0, count, delay, i;
    const char *dev, *hex;
    char *p;
    int fd;
    if (argc < 3){ printf("usage: %s <dev> <hexbytes> [count] [delay_ms]\n", argv[0]); return 2; }
    dev = argv[1]; hex = argv[2];
    count = (argc > 3) ? atoi(argv[3]) : 1;
    delay = (argc > 4) ? atoi(argv[4]) : 100;
    /* parse hex */
    p = (char*)hex;
    while (*p && len < (int)sizeof(buf)){
        while (*p && !isxdigit((unsigned char)*p)) p++;
        if (!*p) break;
        buf[len++] = (unsigned char)strtol(p, &p, 16);
    }
    if (len == 0){ printf("no bytes parsed\n"); return 2; }
    printf("injecting %d bytes x%d (delay %dms) to %s:\n ", len, count, delay, dev);
    for (i = 0; i < len; i++) printf(" %02x", buf[i]);
    printf("\n");
    fd = open(dev, O_RDWR);
    if (fd < 0){ printf("open %s failed errno=%d (%s)\n", dev, errno, strerror(errno)); return 1; }
    for (i = 0; i < count; i++){
        int n = write(fd, buf, len);
        printf("  write[%d] = %d%s\n", i, n, n < 0 ? strerror(errno) : "");
        fflush(stdout);
        if (delay > 0) usleep(delay * 1000);
    }
    close(fd);
    printf("done\n");
    return 0;
}
