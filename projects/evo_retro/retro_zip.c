/* retro_zip.c - read one game out of a .zip archive.
 *
 * ROM collections are usually kept zipped, one game per archive. This reads the first
 * entry whose extension is one of the system's own (SYS_EXTS) into memory: the
 * archive's directory at the end of the file, then that one entry - "stored" or
 * "deflated", the two methods zip tools actually use - checked against its CRC.
 *
 * The inflate below is a small plain decoder for RFC 1951: canonical Huffman codes
 * read a bit at a time. It is several times slower than zlib and still takes only a
 * fraction of a second for a cartridge, once per game start; in exchange the frontend
 * needs no compression library on the unit.
 *
 * Everything is read byte-wise (this CPU faults on unaligned 16/32-bit loads) and
 * every length that comes from the file is checked before it is used.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "retro_fe.h"

#define ZIP_MAX_OUT (64u * 1024u * 1024u)   /* the largest game we will unpack */
#define ZIP_MAX_DIR (4u * 1024u * 1024u)

static const char *g_err = "";
const char *fe_zip_error(void) { return g_err; }

/* ================================================================== inflate */
struct inf {
    const unsigned char *in;  size_t inlen, inpos;
    unsigned char *out;       size_t outlen, outpos;
    unsigned bitbuf;          int bitcnt;
    int err;
};

struct huff { unsigned short count[16], symbol[288]; };

static unsigned bits(struct inf *s, int need)           /* need: 0..16 */
{
    unsigned v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) { s->err = 1; return 0; }
        v |= (unsigned)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return v & ((1u << need) - 1u);
}

/* code lengths -> a canonical code. < 0: more codes than the lengths allow */
static int build(struct huff *h, const unsigned char *len, int n)
{
    unsigned short offs[16];
    int i, left = 1;

    for (i = 0; i < 16; i++) h->count[i] = 0;
    for (i = 0; i < n; i++) h->count[len[i]]++;
    for (i = 1; i < 16; i++) {
        left <<= 1;
        left -= h->count[i];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (i = 1; i < 15; i++) offs[i + 1] = (unsigned short)(offs[i] + h->count[i]);
    for (i = 0; i < n; i++) if (len[i]) h->symbol[offs[len[i]]++] = (unsigned short)i;
    return left;
}

/* one symbol: walk the code a bit at a time, shortest codes first */
static int decode(struct inf *s, const struct huff *h)
{
    int code = 0, first = 0, index = 0, len;

    for (len = 1; len <= 15; len++) {
        int count = h->count[len];
        code |= (int)bits(s, 1);
        if (s->err) return -1;
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = 1;                                         /* a code the table does not have */
    return -1;
}

static const unsigned short LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                          67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char  LEXT[29]  = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                          1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char  DEXT[30]  = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
                                          11, 11, 12, 12, 13, 13 };

/* the body of a compressed block: literals and (length, distance) copies */
static void block(struct inf *s, const struct huff *lc, const struct huff *dc)
{
    for (;;) {
        int sym = decode(s, lc);
        if (s->err) return;
        if (sym < 256) {
            if (s->outpos >= s->outlen) { s->err = 1; return; }
            s->out[s->outpos++] = (unsigned char)sym;
        } else if (sym == 256) {
            return;
        } else {
            unsigned len, dist;
            int d;
            sym -= 257;
            if (sym >= 29) { s->err = 1; return; }
            len = LBASE[sym] + bits(s, LEXT[sym]);
            d = decode(s, dc);
            if (s->err || d >= 30) { s->err = 1; return; }
            dist = DBASE[d] + bits(s, DEXT[d]);
            if (s->err || dist > s->outpos || len > s->outlen - s->outpos) { s->err = 1; return; }
            while (len--) { s->out[s->outpos] = s->out[s->outpos - dist]; s->outpos++; }
        }
    }
}

static void stored(struct inf *s)
{
    unsigned len, nlen;
    s->bitbuf = 0; s->bitcnt = 0;                       /* the rest of this byte is padding */
    if (s->inlen - s->inpos < 4) { s->err = 1; return; }
    len  = s->in[s->inpos] | ((unsigned)s->in[s->inpos + 1] << 8);
    nlen = s->in[s->inpos + 2] | ((unsigned)s->in[s->inpos + 3] << 8);
    s->inpos += 4;
    if ((len ^ 0xffffu) != nlen || len > s->inlen - s->inpos || len > s->outlen - s->outpos) { s->err = 1; return; }
    memcpy(s->out + s->outpos, s->in + s->inpos, len);
    s->inpos += len; s->outpos += len;
}

static void fixed(struct inf *s)
{
    static struct huff lc, dc;
    static int made = 0;
    if (!made) {
        unsigned char len[288];
        int i;
        for (i = 0; i < 144; i++) len[i] = 8;
        for (; i < 256; i++) len[i] = 9;
        for (; i < 280; i++) len[i] = 7;
        for (; i < 288; i++) len[i] = 8;
        build(&lc, len, 288);
        for (i = 0; i < 30; i++) len[i] = 5;
        build(&dc, len, 30);
        made = 1;
    }
    block(s, &lc, &dc);
}

static void dynamic(struct inf *s)
{
    static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    unsigned char len[288 + 32];
    struct huff lc, dc;
    int nlen, ndist, ncode, i;

    nlen  = (int)bits(s, 5) + 257;
    ndist = (int)bits(s, 5) + 1;
    ncode = (int)bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > 30) { s->err = 1; return; }

    for (i = 0; i < ncode; i++) len[order[i]] = (unsigned char)bits(s, 3);
    for (; i < 19; i++) len[order[i]] = 0;
    if (s->err || build(&lc, len, 19) < 0) { s->err = 1; return; }

    for (i = 0; i < nlen + ndist; ) {
        int sym = decode(s, &lc), rep, prev = 0;
        if (s->err) return;
        if (sym < 16) { len[i++] = (unsigned char)sym; continue; }
        if (sym == 16) {
            if (i == 0) { s->err = 1; return; }
            prev = len[i - 1];
            rep = 3 + (int)bits(s, 2);
        } else if (sym == 17) rep = 3 + (int)bits(s, 3);
        else rep = 11 + (int)bits(s, 7);
        if (s->err || i + rep > nlen + ndist) { s->err = 1; return; }
        while (rep--) len[i++] = (unsigned char)prev;
    }
    if (len[256] == 0) { s->err = 1; return; }          /* no way to end the block */
    if (build(&lc, len, nlen) < 0 || build(&dc, len + nlen, ndist) < 0) { s->err = 1; return; }
    block(s, &lc, &dc);
}

/* 1 = exactly `outlen` bytes were produced */
static int inflate_raw(const unsigned char *in, size_t inlen, unsigned char *out, size_t outlen)
{
    struct inf s;
    int last;

    memset(&s, 0, sizeof s);
    s.in = in; s.inlen = inlen; s.out = out; s.outlen = outlen;
    do {
        unsigned type;
        last = (int)bits(&s, 1);
        type = bits(&s, 2);
        if (s.err) break;
        if (type == 0) stored(&s);
        else if (type == 1) fixed(&s);
        else if (type == 2) dynamic(&s);
        else s.err = 1;
    } while (!last && !s.err);
    return !s.err && s.outpos == outlen;
}

/* ====================================================================== CRC */
static unsigned long crc32_of(const unsigned char *p, size_t n)
{
    static unsigned long table[256];
    static int made = 0;
    unsigned long c = 0xffffffffUL;
    size_t i;

    if (!made) {
        unsigned k, j;
        for (k = 0; k < 256; k++) {
            unsigned long v = k;
            for (j = 0; j < 8; j++) v = (v & 1) ? 0xedb88320UL ^ (v >> 1) : v >> 1;
            table[k] = v;
        }
        made = 1;
    }
    for (i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return (c ^ 0xffffffffUL) & 0xffffffffUL;
}

/* ================================================================== the zip */
static unsigned rd16(const unsigned char *p) { return p[0] | ((unsigned)p[1] << 8); }
static unsigned long rd32(const unsigned char *p)
{
    return p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int ext_wanted(const char *name, size_t n, char *ext, size_t extlen)
{
    static const char *const exts[] = { SYS_EXTS };
    size_t dot = n, i;
    unsigned k;

    for (i = n; i > 0; i--) {
        if (name[i - 1] == '.') { dot = i; break; }
        if (name[i - 1] == '/') break;
    }
    if (dot >= n) return 0;
    for (k = 0; k < sizeof exts / sizeof exts[0]; k++) {
        size_t el = strlen(exts[k]);
        if (!strcmp(exts[k], "zip") || !strcmp(exts[k], "cue") || !strcmp(exts[k], "iso")) continue;   /* not cartridges */
        if (el != n - dot || strncasecmp(name + dot, exts[k], el)) continue;
        if (ext && extlen) { strncpy(ext, exts[k], extlen - 1); ext[extlen - 1] = 0; }
        return 1;
    }
    return 0;
}

static void *read_at(FILE *f, long off, size_t n)
{
    void *p;
    if (fseek(f, off, SEEK_SET) != 0 || !(p = malloc(n ? n : 1))) return NULL;
    if (fread(p, 1, n, f) != n) { free(p); return NULL; }
    return p;
}

/* Reads the first game in `zip`. 1 = *data (malloc'd) and *size are set, and `ext` holds
 * the game's extension in lower case; 0 = no luck, fe_zip_error() says why. */
int fe_zip_read(const char *zip, void **data, size_t *size, char *ext, size_t extlen)
{
    FILE *f = fopen(zip, "rb");
    unsigned char *tail = NULL, *dir = NULL, *comp = NULL, *out = NULL, lh[30];
    long fsize, tail_len, i, eocd = -1;
    unsigned long dir_size, dir_off, pos, csize = 0, usize = 0, crc = 0, lho = 0;
    unsigned entries, e, method = 0;
    int found = 0, ok = 0;

    *data = NULL; *size = 0;
    g_err = "cannot open the archive";
    if (!f) return 0;
    g_err = "not a zip archive";
    if (fseek(f, 0, SEEK_END) != 0 || (fsize = ftell(f)) < 22) goto done;

    /* the directory's footer is within the last 64 KB (it may be followed by a comment) */
    tail_len = fsize < 65557L ? fsize : 65557L;
    if (!(tail = (unsigned char *)read_at(f, fsize - tail_len, (size_t)tail_len))) goto done;
    for (i = tail_len - 22; i >= 0; i--)
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) { eocd = i; break; }
    if (eocd < 0) goto done;
    entries  = rd16(tail + eocd + 10);
    dir_size = rd32(tail + eocd + 12);
    dir_off  = rd32(tail + eocd + 16);
    if (dir_size > ZIP_MAX_DIR || dir_off > (unsigned long)fsize || dir_size > (unsigned long)fsize - dir_off) goto done;
    if (!(dir = (unsigned char *)read_at(f, (long)dir_off, (size_t)dir_size))) goto done;

    g_err = "no game for this system in the archive";
    for (pos = 0, e = 0; e < entries && pos + 46 <= dir_size; e++) {
        const unsigned char *h = dir + pos;
        unsigned nlen, xlen, clen;
        if (rd32(h) != 0x02014b50UL) break;
        nlen = rd16(h + 28); xlen = rd16(h + 30); clen = rd16(h + 32);
        if (pos + 46 + nlen > dir_size) break;
        if (!found && nlen > 0 && h[46 + nlen - 1] != '/' && ext_wanted((const char *)h + 46, nlen, ext, extlen)) {
            if (rd16(h + 8) & 1) { g_err = "the archive is password-protected"; goto done; }
            method = rd16(h + 10); crc = rd32(h + 16); csize = rd32(h + 20); usize = rd32(h + 24); lho = rd32(h + 42);
            found = 1;
        }
        pos += 46ul + nlen + xlen + clen;
    }
    if (!found) goto done;

    g_err = "the archive is damaged";
    if (usize == 0 || usize > ZIP_MAX_OUT || csize > (unsigned long)fsize || lho > (unsigned long)fsize - 30) goto done;
    if (fseek(f, (long)lho, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50UL) goto done;
    pos = lho + 30ul + rd16(lh + 26) + rd16(lh + 28);
    if (pos > (unsigned long)fsize || csize > (unsigned long)fsize - pos) goto done;
    if (method != 0 && method != 8) { g_err = "the archive uses a compression this cannot read"; goto done; }
    if (method == 0 && csize != usize) goto done;

    g_err = "not enough memory for this game";
    if (!(out = (unsigned char *)malloc(usize))) goto done;
    if (method == 0) {
        g_err = "the archive is damaged";
        if (fseek(f, (long)pos, SEEK_SET) != 0 || fread(out, 1, usize, f) != usize) goto done;
    } else {
        if (!(comp = (unsigned char *)read_at(f, (long)pos, csize))) goto done;
        g_err = "the archive is damaged";
        if (!inflate_raw(comp, csize, out, usize)) goto done;
    }
    if (crc32_of(out, usize) != crc) goto done;
    *data = out; *size = usize; out = NULL;
    g_err = "";
    ok = 1;
done:
    free(tail); free(dir); free(comp); free(out);
    fclose(f);
    return ok;
}
