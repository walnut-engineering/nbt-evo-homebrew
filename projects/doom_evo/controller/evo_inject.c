/* evo_inject - queue one HMI knob-input injection for the most-side tee (evo_tee.so).
 * The tee (loaded in `most`) turns a benign ipc25 poll read into a synthetic controller
 * frame so the HMI navigates. Usage:
 *   evo_inject rotate <n>     rotate n detents (+ = clockwise/down-list, - = up)
 *   evo_inject select         centre press (OK)
 *   evo_inject up|down|left|right   5-way tilt
 *   evo_inject menu|back|option|media|nav|tel|com   ring button
 */
#define _QNX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "evo_feed.h"

int main(int argc, char **argv) {
    unsigned int kind = 0; int arg = 0;
    if (argc < 2) { fprintf(stderr, "usage: evo_inject rotate <n>|select|up|down|left|right|menu|back|option|media|nav|tel|com\n"); return 2; }
    if      (!strcmp(argv[1],"rotate")) { kind=1; arg = argc>2?atoi(argv[2]):1; }
    else if (!strcmp(argv[1],"select")) { kind=2; }
    else if (!strcmp(argv[1],"up"))     { kind=3; arg=0; }
    else if (!strcmp(argv[1],"down"))   { kind=3; arg=1; }
    else if (!strcmp(argv[1],"left"))   { kind=3; arg=2; }
    else if (!strcmp(argv[1],"right"))  { kind=3; arg=3; }
    else if (!strcmp(argv[1],"menu"))   { kind=4; arg=0x01; }
    else if (!strcmp(argv[1],"back"))   { kind=4; arg=0x02; }
    else if (!strcmp(argv[1],"option")) { kind=4; arg=0x04; }
    else if (!strcmp(argv[1],"media"))  { kind=4; arg=0x08; }
    else if (!strcmp(argv[1],"nav"))    { kind=4; arg=0x10; }
    else if (!strcmp(argv[1],"tel"))    { kind=4; arg=0x20; }
    else if (!strcmp(argv[1],"com"))    { kind=4; arg=0x40; }
    else { fprintf(stderr, "unknown action %s\n", argv[1]); return 2; }

    int fd = open(EVO_INJECT_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) { perror("open inject"); return 1; }
    if (ftruncate(fd, sizeof(struct evo_inject)) != 0) { perror("ftruncate"); return 1; }
    struct evo_inject *in = mmap(0, sizeof(*in), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (in == MAP_FAILED) { perror("mmap"); return 1; }
    if (in->magic != EVO_INJECT_MAGIC) in->magic = EVO_INJECT_MAGIC;
    in->kind = kind; in->arg = arg;
    __asm__ __volatile__(".word 0xf57ff05b" ::: "memory");
    in->seq = in->seq + 1;
    printf("inject kind=%u arg=%d seq=%u\n", kind, arg, in->seq);
    return 0;
}
