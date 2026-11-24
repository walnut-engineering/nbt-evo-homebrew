/* host unit test: replay an evo_tee raw log through e5_scan and print the feed events */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "evo_e5.h"
static long g_t;
static void emit(void *c, unsigned char k, unsigned char ctr, const unsigned char *b) {
    (void)c;
    static const char *dn[4] = {"FWD", "BACK", "LEFT", "RIGHT"};
    if (k == EVO_K_ROTARY) printf("t=%7ld ROTARY %+d (pos %02x%02x) seq=%02x\n", g_t, (signed char)b[0], b[1], b[2], ctr);
    else if (k == EVO_K_DIR) printf("t=%7ld DIR %s %s\n", g_t, dn[b[0]], b[1] ? "down" : "up");
    else if (k == EVO_K_CENTER) printf("t=%7ld CENTER %s\n", g_t, b[1] ? "down" : "up");
    else printf("t=%7ld BUTTON id=%02x %s\n", g_t, b[0], b[1] ? "down" : "up");
}
static void emit_hmi(void *c, unsigned char k, unsigned char ctr, const unsigned char *b) {
    printf("   HMI-side: "); emit(c, k, ctr, b);
}
/* usage: e5_test <log> [t_from]                      decode (DOOM side)
 *        e5_test <log> t_from mask <grab_from> <grab_to>  also run e5_mask with the grab held
 *        in [grab_from,grab_to) and decode what the HMI would see */
int main(int argc, char **argv) {
    char line[4096]; unsigned char buf[512], hm[512]; struct e5_state st = {0, 0, -1}, sth = {0, 0, -1};
    struct e5_mask mk; memset(&mk, 0, sizeof mk);
    FILE *f = fopen(argv[1], "r"); long from = argc > 2 ? atol(argv[2]) : 0; int tg = 0;
    int domask = argc > 5 && !strcmp(argv[3], "mask"); long g0 = domask ? atol(argv[4]) : 0, g1 = domask ? atol(argv[5]) : 0;
    long masked = 0;
    while (fgets(line, sizeof line, f)) {
        char *c = strchr(line, ':'); int n = 0; long t;
        if (strncmp(line, "rd ", 3) || !c || sscanf(line, "rd %*s t=%ld", &t) != 1) continue;
        for (c++; *c && n < 512; c += 3) { unsigned v; if (sscanf(c, " %2x", &v) != 1) break; buf[n++] = v; }
        g_t = t;
        if (t >= from) tg += e5_scan(&st, buf, n, emit, NULL); else e5_scan(&st, buf, n, emit, NULL);
        if (domask) {
            memcpy(hm, buf, n);
            masked += e5_mask(&mk, hm, n, t >= g0 && t < g1);
            if (t >= from) e5_scan(&sth, hm, n, emit_hmi, NULL);
        }
    }
    printf("telegrams decoded after t_from: %d%s\n", tg, domask ? "" : "");
    if (domask) printf("masked telegrams: %ld (grab window %ld..%ld), final HMI offset=%d\n", masked, g0, g1, mk.offset);
    return 0;
}
