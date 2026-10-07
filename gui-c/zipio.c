#include "zipio.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------- crc32 */
static unsigned long crc_table[256];
static int crc_ready;
unsigned long zip_crc32(unsigned long crc, const unsigned char *p, size_t n) {
    size_t i;
    if (!crc_ready) {
        unsigned long c;
        int k, j;
        for (k = 0; k < 256; k++) { c = (unsigned long)k; for (j = 0; j < 8; j++) c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1; crc_table[k] = c; }
        crc_ready = 1;
    }
    crc ^= 0xffffffffUL;
    for (i = 0; i < n; i++) crc = crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffUL;
}

/* ---------------------------------------------------------------- inflate (after the plain description in RFC 1951) */
typedef struct {
    const unsigned char *in; size_t inlen, inpos;
    unsigned long bitbuf; int bitcnt;
    unsigned char win[32768]; unsigned long wpos;   /* the last 32 KB of output (also all of it for the back references) */
    unsigned char *out; size_t outcap; unsigned long total;
    unsigned long crcreg;   /* running crc (inverted) */
    int err;
} Inf;

#define MAXBITS 15
typedef struct { unsigned short count[MAXBITS + 1]; unsigned short symbol[288]; } Huff;

static int getbits(Inf *s, int need) {
    unsigned long v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) { s->err = 1; return 0; }
        v |= (unsigned long)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1UL << need) - 1));
}

static void put(Inf *s, unsigned char b) {
    s->win[s->wpos & 32767] = b;
    s->wpos++;
    if (s->out && s->total < s->outcap) s->out[s->total] = b;
    s->total++;
    s->crcreg = crc_table[(s->crcreg ^ b) & 0xff] ^ (s->crcreg >> 8);
}

static int build(Huff *h, const short *length, int n) {
    int sym, len, left;
    unsigned short offs[MAXBITS + 1];
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) { left <<= 1; left -= h->count[len]; if (left < 0) return left; }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = (unsigned short)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++) if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (unsigned short)sym;
    return left;
}

static int decode(Inf *s, const Huff *h) {
    int len, code = 0, first = 0, count, index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        code |= getbits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(Inf *s, const Huff *lc, const Huff *dc) {
    for (;;) {
        int sym = decode(s, lc), len, dist;
        if (sym < 0) return -1;
        if (sym < 256) put(s, (unsigned char)sym);
        else if (sym == 256) return 0;
        else {
            sym -= 257;
            if (sym >= 29) return -1;
            len = lbase[sym] + getbits(s, lext[sym]);
            sym = decode(s, dc);
            if (sym < 0 || sym >= 30) return -1;
            dist = dbase[sym] + getbits(s, dext[sym]);
            if (s->err) return -1;
            if ((unsigned long)dist > s->wpos || dist > 32768) return -1;
            while (len--) put(s, s->win[(s->wpos - (unsigned long)dist) & 32767]);
        }
    }
}

static int fixed_block(Inf *s) {
    static Huff lc, dc;
    static int ready;
    if (!ready) {
        short l[288];
        int i;
        for (i = 0; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lc, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&dc, l, 30);
        ready = 1;
    }
    return codes(s, &lc, &dc);
}

static int dynamic_block(Inf *s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    Huff lc, dc, cl;
    int nlen = getbits(s, 5) + 257, ndist = getbits(s, 5) + 1, ncode = getbits(s, 4) + 4, i, err;
    if (s->err || nlen > 286 || ndist > 30) return -1;
    for (i = 0; i < ncode; i++) lengths[order[i]] = (short)getbits(s, 3);
    for (; i < 19; i++) lengths[order[i]] = 0;
    if (s->err || build(&cl, lengths, 19) != 0) return -1;
    i = 0;
    while (i < nlen + ndist) {
        int sym = decode(s, &cl), len = 0, rep;
        if (sym < 0) return -1;
        if (sym < 16) lengths[i++] = (short)sym;
        else {
            if (sym == 16) { if (i == 0) return -1; len = lengths[i - 1]; rep = 3 + getbits(s, 2); }
            else if (sym == 17) rep = 3 + getbits(s, 3);
            else rep = 11 + getbits(s, 7);
            if (s->err || i + rep > nlen + ndist) return -1;
            while (rep--) lengths[i++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -1;
    err = build(&lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return -1;
    err = build(&dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return -1;
    return codes(s, &lc, &dc);
}

/* inflates in[0..inlen) (a raw deflate stream); the bytes go to out (when not NULL, up to outcap) and into the crc */
static int inflate_raw(const unsigned char *in, size_t inlen, unsigned char *out, size_t outcap, unsigned long *crc, unsigned long *outlen) {
    Inf *s = (Inf *)calloc(1, sizeof(Inf));
    int last, type, ret = 0;
    if (!s) return -1;
    zip_crc32(0, NULL, 0);   /* makes the table */
    s->crcreg = 0xffffffffUL;
    s->in = in; s->inlen = inlen; s->out = out; s->outcap = outcap;
    do {
        last = getbits(s, 1);
        type = getbits(s, 2);
        if (s->err) { ret = -1; break; }
        if (type == 0) {
            unsigned len, nlen;
            s->bitbuf = 0; s->bitcnt = 0;
            if (s->inpos + 4 > s->inlen) { ret = -1; break; }
            len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
            nlen = s->in[s->inpos + 2] | (s->in[s->inpos + 3] << 8);
            s->inpos += 4;
            if (len != (~nlen & 0xffff) || s->inpos + len > s->inlen) { ret = -1; break; }
            while (len--) put(s, s->in[s->inpos++]);
        } else if (type == 1) ret = fixed_block(s);
        else if (type == 2) ret = dynamic_block(s);
        else ret = -1;
        if (ret) break;
    } while (!last);
    *outlen = s->total;
    if (crc) *crc = s->crcreg ^ 0xffffffffUL;
    ret = ret ? -1 : 0;
    free(s);
    return ret;
}

/* ---------------------------------------------------------------- zip directory */
static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned long rd32(const unsigned char *p) { return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }

/* the central directory: offset and number of entries; 0 when there is none */
static int find_dir(const unsigned char *buf, size_t len, size_t *dirOff, int *count) {
    size_t i, lo;
    if (len < 22) return 0;
    lo = len > 65557 ? len - 65557 : 0;
    for (i = len - 22 + 1; i-- > lo; ) {
        if (buf[i] == 0x50 && buf[i + 1] == 0x4b && buf[i + 2] == 5 && buf[i + 3] == 6) {
            size_t off = rd32(buf + i + 16);
            if (off > len) return 0;
            *dirOff = off;
            *count = (int)rd16(buf + i + 10);
            return 1;
        }
    }
    return 0;
}

int zip_entries(const unsigned char *buf, size_t len, ZipEntry *out, int max) {
    size_t off;
    int count, i, n = 0;
    if (!find_dir(buf, len, &off, &count)) return -1;
    for (i = 0; i < count; i++) {
        unsigned nl, el, cl;
        ZipEntry *e;
        if (off + 46 > len || rd32(buf + off) != 0x02014b50UL) return -1;
        nl = rd16(buf + off + 28); el = rd16(buf + off + 30); cl = rd16(buf + off + 32);
        if (off + 46 + nl + el + cl > len) return -1;
        if (n < max) {
            e = &out[n];
            memset(e, 0, sizeof *e);
            e->method = (int)rd16(buf + off + 10);
            e->crc = rd32(buf + off + 16);
            e->csize = rd32(buf + off + 20);
            e->usize = rd32(buf + off + 24);
            e->offset = rd32(buf + off + 42);
            memcpy(e->name, buf + off + 46, nl < 259 ? nl : 259);
            e->name[nl < 259 ? nl : 259] = 0;
        }
        n++;
        off += 46 + nl + el + cl;
    }
    return n;
}

/* where the data of an entry starts (after its local header); 0 when the header is wrong */
static size_t data_start(const unsigned char *buf, size_t len, const ZipEntry *e) {
    size_t o = e->offset;
    if (o + 30 > len || rd32(buf + o) != 0x04034b50UL) return 0;
    o += 30 + rd16(buf + o + 26) + rd16(buf + o + 28);
    if (o > len || o + e->csize > len) return 0;
    return o;
}

int zip_extract(const unsigned char *buf, size_t len, const ZipEntry *e, unsigned char *out) {
    size_t o = data_start(buf, len, e);
    unsigned long crc, got;
    if (!o) return 0;
    if (e->method == 0) {
        if (e->csize != e->usize) return 0;
        memcpy(out, buf + o, e->usize);
        crc = zip_crc32(0, out, e->usize);
    } else if (e->method == 8) {
        if (inflate_raw(buf + o, e->csize, out, e->usize, &crc, &got) != 0 || got != e->usize) return 0;
    } else return 0;
    return crc == e->crc;
}

static void problem(ZipReport *r, const char *fmt, const char *name) {
    if (r->nproblems < 12) snprintf(r->problems[r->nproblems], sizeof r->problems[0], fmt, name);
    r->nproblems++;
}

int zip_verify(const unsigned char *buf, size_t len, ZipReport *rep) {
    ZipEntry *ents;
    int n, i, cap = 4096;
    memset(rep, 0, sizeof *rep);
    if (len < 22) { problem(rep, "%s", "the file is too small to be a zip"); return 0; }
    ents = (ZipEntry *)malloc(sizeof(ZipEntry) * (size_t)cap);
    if (!ents) { problem(rep, "%s", "not enough memory to check the file"); return 0; }
    n = zip_entries(buf, len, ents, cap);
    if (n < 0) { problem(rep, "%s", "the zip directory is damaged or the file is cut off (not a complete download?)"); free(ents); return 0; }
    if (n > cap) { problem(rep, "%s", "too many files in the zip to check them all"); n = cap; }
    rep->entries = n;
    for (i = 0; i < n; i++) {
        const ZipEntry *e = &ents[i];
        size_t o;
        unsigned long crc, got;
        size_t nl = strlen(e->name);
        rep->unpacked += e->usize;
        if (nl && e->name[nl - 1] == '/') { rep->entries--; continue; }   /* a folder */
        o = data_start(buf, len, e);
        if (!o) { problem(rep, "%s: its data is missing (the zip is cut off or damaged)", e->name); continue; }
        if (e->method == 0) {
            if (zip_crc32(0, buf + o, e->usize) != e->crc) problem(rep, "%s: wrong checksum (the file is corrupt)", e->name);
        } else if (e->method == 8) {
            if (inflate_raw(buf + o, e->csize, NULL, 0, &crc, &got) != 0) problem(rep, "%s: cannot be unpacked (the file is corrupt)", e->name);
            else if (got != e->usize) problem(rep, "%s: has the wrong size (the file is corrupt)", e->name);
            else if (crc != e->crc) problem(rep, "%s: wrong checksum (the file is corrupt)", e->name);
        } else problem(rep, "%s: uses a compression method this check does not know", e->name);
    }
    free(ents);
    if (rep->entries == 0 && rep->nproblems == 0) problem(rep, "%s", "the zip contains no files");
    return rep->nproblems == 0;
}

/* ---------------------------------------------------------------- writing */
static int grow(unsigned char **p, size_t *cap, size_t need) {
    if (need <= *cap) return 1;
    {
        size_t nc = *cap ? *cap : 4096;
        unsigned char *np;
        while (nc < need) nc *= 2;
        np = (unsigned char *)realloc(*p, nc);
        if (!np) return 0;
        *p = np; *cap = nc;
    }
    return 1;
}
static void w16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void w32(unsigned char *p, unsigned long v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }

void zw_init(ZipWriter *w) { memset(w, 0, sizeof *w); }
void zw_free(ZipWriter *w) { free(w->data); free(w->dir); memset(w, 0, sizeof *w); }

int zw_add(ZipWriter *w, const char *name, const void *data, size_t n) {
    size_t nl = strlen(name), at = w->n;
    unsigned long crc = zip_crc32(0, (const unsigned char *)data, n);
    unsigned char *h, *d;
    if (!grow(&w->data, &w->cap, at + 30 + nl + n) || !grow(&w->dir, &w->dcap, w->dn + 46 + nl)) return 0;
    h = w->data + at;
    memset(h, 0, 30);
    w32(h, 0x04034b50UL); w16(h + 4, 10); w16(h + 10, 0); w16(h + 12, 0x21);   /* stored, 1980-01-01 */
    w32(h + 14, crc); w32(h + 18, n); w32(h + 22, n); w16(h + 26, (unsigned)nl);
    memcpy(h + 30, name, nl);
    if (n) memcpy(h + 30 + nl, data, n);
    w->n = at + 30 + nl + n;
    d = w->dir + w->dn;
    memset(d, 0, 46);
    w32(d, 0x02014b50UL); w16(d + 4, 20); w16(d + 6, 10); w16(d + 14, 0x21);
    w32(d + 16, crc); w32(d + 20, n); w32(d + 24, n); w16(d + 28, (unsigned)nl); w32(d + 42, at);
    memcpy(d + 46, name, nl);
    w->dn += 46 + nl;
    w->count++;
    return 1;
}

int zw_finish(ZipWriter *w) {
    unsigned char *e;
    size_t dirAt = w->n;
    if (!grow(&w->data, &w->cap, w->n + w->dn + 22)) return 0;
    memcpy(w->data + w->n, w->dir, w->dn);
    w->n += w->dn;
    e = w->data + w->n;
    memset(e, 0, 22);
    w32(e, 0x06054b50UL); w16(e + 8, (unsigned)w->count); w16(e + 10, (unsigned)w->count); w32(e + 12, w->dn); w32(e + 16, dirAt);
    w->n += 22;
    return 1;
}
