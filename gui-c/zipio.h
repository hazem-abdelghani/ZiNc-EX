/* Reading, checking and writing zip files in memory (stored and deflate entries). No Windows code: testable on any host. */
#ifndef ZIPIO_H
#define ZIPIO_H
#include <stddef.h>

typedef struct {
    int entries;                  /* files in the zip */
    unsigned long unpacked;       /* their size when unpacked */
    int nproblems;
    char problems[12][200];       /* what is wrong, one line each (at most 12 are kept) */
} ZipReport;

/* checks the directory of the zip and the CRC of every file; 1 when everything is fine */
int zip_verify(const unsigned char *buf, size_t len, ZipReport *rep);

typedef struct { char name[260]; unsigned long csize, usize, crc; int method; unsigned long offset; } ZipEntry;
/* lists the entries (max of them); returns the count or -1 when the zip is damaged */
int zip_entries(const unsigned char *buf, size_t len, ZipEntry *out, int max);
/* the unpacked bytes of an entry into out (usize bytes); 1 on success */
int zip_extract(const unsigned char *buf, size_t len, const ZipEntry *e, unsigned char *out);

unsigned long zip_crc32(unsigned long crc, const unsigned char *p, size_t n);

/* writing: files are stored without compression */
typedef struct { unsigned char *data; size_t n, cap; int count; unsigned char *dir; size_t dn, dcap; } ZipWriter;
void zw_init(ZipWriter *w);
int zw_add(ZipWriter *w, const char *name, const void *data, size_t n);   /* name with "/" separators */
int zw_finish(ZipWriter *w);                                              /* data / n then hold the zip */
void zw_free(ZipWriter *w);

#endif
