/* Windows resource embedding without an external toolchain.
 *
 * An .ico file is turned into a COFF object holding a .rsrc section with the
 * RT_ICON / RT_GROUP_ICON resources laid out exactly the way the PE loader
 * (and Explorer) expects, so the bundled ld can link it into the .exe. This
 * is what windres would otherwise be needed for; zan ships no windres, and
 * requiring MinGW or the Windows SDK just to give a program an icon would
 * break the "zanc alone produces an .exe" promise.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/host_oom.h"

#include "winres.h"
#include "win_utf8.h"

#ifdef _WIN32
#define fopen zan_utf8_fopen
#undef RT_ICON
#undef RT_GROUP_ICON
#endif

#define RES_LANG 1033           /* en-US: what every toolchain defaults to */
#define RT_ICON 3
#define RT_GROUP_ICON 14

typedef struct {
    unsigned char width, height, colors, reserved;
    unsigned short planes, bits;
    unsigned int bytes;
    unsigned int offset;
} ico_entry_t;

typedef struct {
    unsigned char *buf;
    size_t len, cap;
} buf_t;

static int buf_grow(buf_t *b, size_t need) {
    if (b->len + need <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 1024;
    while (cap < b->len + need) cap *= 2;
    unsigned char *p = (unsigned char *)realloc(b->buf, cap);
    if (!p) return 0;
    b->buf = p;
    b->cap = cap;
    return 1;
}

static int put(buf_t *b, const void *data, size_t n) {
    if (!buf_grow(b, n)) return 0;
    memcpy(b->buf + b->len, data, n);
    b->len += n;
    return 1;
}

static int put16(buf_t *b, unsigned short v) {
    unsigned char t[2] = { (unsigned char)(v & 0xff), (unsigned char)(v >> 8) };
    return put(b, t, 2);
}

static int put32(buf_t *b, unsigned int v) {
    unsigned char t[4] = { (unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff),
                           (unsigned char)((v >> 16) & 0xff), (unsigned char)(v >> 24) };
    return put(b, t, 4);
}

static void set32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)(v >> 24);
}

static unsigned int rd16(const unsigned char *p) { return p[0] | ((unsigned int)p[1] << 8); }
static unsigned int rd32(const unsigned char *p) {
    return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

static unsigned char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    unsigned char *p = (unsigned char *)malloc((size_t)n);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)n;
    return p;
}

/* One directory level: 16-byte header plus `count` id entries. */
static int dir_header(buf_t *b, unsigned short count) {
    return put32(b, 0) && put32(b, 0) && put16(b, 0) && put16(b, 0) &&
           put16(b, 0) && put16(b, count);
}

static int icon_object(const unsigned char *ico, size_t ico_len,
                      const char *out_obj_path, int arm64) {
    if (!ico) return 1;
    if (ico_len < 6 || rd16(ico) != 0 || rd16(ico + 2) != 1) { return 2; }
    int n = (int)rd16(ico + 4);
    if (n <= 0 || n > 64 || ico_len < 6 + (size_t)n * 16) { return 2; }

    ico_entry_t *ents = (ico_entry_t *)calloc((size_t)n, sizeof(ico_entry_t));
    if (!ents) { return 3; }
    for (int i = 0; i < n; i++) {
        const unsigned char *e = ico + 6 + (size_t)i * 16;
        ents[i].width = e[0]; ents[i].height = e[1];
        ents[i].colors = e[2]; ents[i].reserved = e[3];
        ents[i].planes = (unsigned short)rd16(e + 4);
        ents[i].bits = (unsigned short)rd16(e + 6);
        ents[i].bytes = rd32(e + 8);
        ents[i].offset = rd32(e + 12);
        if ((unsigned long long)ents[i].offset + ents[i].bytes > ico_len) {
            free(ents); return 2;
        }
    }

    /* The group directory names the images by resource id; it is the resource
     * Explorer and LoadIcon actually look up. */
    buf_t grp = {0};
    if (!(put16(&grp, 0) && put16(&grp, 1) && put16(&grp, (unsigned short)n))) {
        free(ents); return 3;
    }
    for (int i = 0; i < n; i++) {
        unsigned char h[12];
        h[0] = ents[i].width; h[1] = ents[i].height;
        h[2] = ents[i].colors; h[3] = ents[i].reserved;
        h[4] = (unsigned char)(ents[i].planes & 0xff);
        h[5] = (unsigned char)(ents[i].planes >> 8);
        h[6] = (unsigned char)(ents[i].bits & 0xff);
        h[7] = (unsigned char)(ents[i].bits >> 8);
        set32(h + 8, ents[i].bytes);
        if (!put(&grp, h, 12) || !put16(&grp, (unsigned short)(i + 1))) {
            free(grp.buf); free(ents); return 3;
        }
    }

    /* Resource tree: type -> name -> language -> data entry. Sizes are known
     * up front, so the whole thing is emitted in one pass with the data-entry
     * offsets computed rather than patched. */
    int leaves = n + 1;                        /* n icons + one group */
    unsigned int off_root = 0;
    unsigned int sz_root = 16 + 2 * 8;
    unsigned int off_ticon = off_root + sz_root;
    unsigned int sz_ticon = 16 + (unsigned int)n * 8;
    unsigned int off_tgrp = off_ticon + sz_ticon;
    unsigned int sz_tgrp = 16 + 8;
    unsigned int off_names = off_tgrp + sz_tgrp;    /* one lang dir per leaf */
    unsigned int sz_name = 16 + 8;
    unsigned int off_data_entries = off_names + (unsigned int)leaves * sz_name;
    unsigned int off_data = off_data_entries + (unsigned int)leaves * 16;

    buf_t sec = {0};
    /* root */
    if (!dir_header(&sec, 2)) goto oom;
    if (!put32(&sec, RT_ICON) || !put32(&sec, off_ticon | 0x80000000u)) goto oom;
    if (!put32(&sec, RT_GROUP_ICON) || !put32(&sec, off_tgrp | 0x80000000u)) goto oom;
    /* RT_ICON: one name entry per image */
    if (!dir_header(&sec, (unsigned short)n)) goto oom;
    for (int i = 0; i < n; i++) {
        unsigned int nd = off_names + (unsigned int)i * sz_name;
        if (!put32(&sec, (unsigned int)(i + 1)) || !put32(&sec, nd | 0x80000000u)) goto oom;
    }
    /* RT_GROUP_ICON: a single group, id 1 */
    if (!dir_header(&sec, 1)) goto oom;
    {
        unsigned int nd = off_names + (unsigned int)n * sz_name;
        if (!put32(&sec, 1) || !put32(&sec, nd | 0x80000000u)) goto oom;
    }
    /* language level, one per leaf, pointing at its data entry */
    for (int i = 0; i < leaves; i++) {
        if (!dir_header(&sec, 1)) goto oom;
        if (!put32(&sec, RES_LANG) ||
            !put32(&sec, off_data_entries + (unsigned int)i * 16)) goto oom;
    }
    /* data entries: OffsetToData holds the in-section offset and is turned
     * into an RVA by a relocation against the section symbol. */
    unsigned int cursor = off_data;
    unsigned int *data_off = (unsigned int *)calloc((size_t)leaves, sizeof(unsigned int));
    unsigned int *reloc_at = (unsigned int *)calloc((size_t)leaves, sizeof(unsigned int));
    if (!data_off || !reloc_at) { free(data_off); free(reloc_at); goto oom; }
    for (int i = 0; i < leaves; i++) {
        unsigned int size = (i < n) ? ents[i].bytes : (unsigned int)grp.len;
        data_off[i] = cursor;
        reloc_at[i] = (unsigned int)sec.len;
        if (!put32(&sec, cursor) || !put32(&sec, size) ||
            !put32(&sec, 0) || !put32(&sec, 0)) {
            free(data_off); free(reloc_at); goto oom;
        }
        cursor += (size + 7u) & ~7u;
    }
    /* payloads, 8-byte aligned like every other resource compiler emits */
    for (int i = 0; i < leaves; i++) {
        const unsigned char *src = (i < n) ? ico + ents[i].offset : grp.buf;
        unsigned int size = (i < n) ? ents[i].bytes : (unsigned int)grp.len;
        while (sec.len < data_off[i]) { unsigned char z = 0; if (!put(&sec, &z, 1)) goto oom2; }
        if (!put(&sec, src, size)) goto oom2;
        unsigned int pad = ((size + 7u) & ~7u) - size;
        while (pad--) { unsigned char z = 0; if (!put(&sec, &z, 1)) goto oom2; }
    }

    FILE *out = fopen(out_obj_path, "wb");
    if (!out) { free(data_off); free(reloc_at); free(sec.buf); free(grp.buf);
                free(ents); return 4; }

    unsigned int sec_size = (unsigned int)sec.len;
    unsigned int ptr_raw = 20 + 40;
    unsigned int ptr_reloc = ptr_raw + sec_size;
    unsigned int nreloc = (unsigned int)leaves;
    unsigned int ptr_syms = ptr_reloc + nreloc * 10;

    buf_t o = {0};
    /* The resource object must carry the target machine, or the linker
     * rejects it when cross-linking (e.g. --target win-arm64). */
    put16(&o, arm64 ? 0xAA64 : 0x8664);
    put16(&o, 1);                      /* one section */
    put32(&o, 0);                      /* timestamp: keep output reproducible */
    put32(&o, ptr_syms);
    put32(&o, 2);                      /* section symbol + its aux record */
    put16(&o, 0);
    put16(&o, 0);
    put(&o, ".rsrc\0\0\0", 8);
    put32(&o, 0);                      /* VirtualSize */
    put32(&o, 0);                      /* VirtualAddress */
    put32(&o, sec_size);
    put32(&o, ptr_raw);
    put32(&o, ptr_reloc);
    put32(&o, 0);
    put16(&o, (unsigned short)nreloc);
    put16(&o, 0);
    put32(&o, 0x40300040u);            /* init data | 8-byte align | read */
    put(&o, sec.buf, sec.len);
    for (unsigned int i = 0; i < nreloc; i++) {
        put32(&o, reloc_at[i]);
        put32(&o, 0);                  /* symbol 0: the .rsrc section */
        /* ADDR32NB: 0x0003 on AMD64, 0x0002 on ARM64 */
        put16(&o, arm64 ? 0x0002 : 0x0003);
    }
    put(&o, ".rsrc\0\0\0", 8);         /* section symbol */
    put32(&o, 0);
    put16(&o, 1);                      /* section number */
    put16(&o, 0);
    { unsigned char t[2] = { 3 /* IMAGE_SYM_CLASS_STATIC */, 1 /* aux count */ };
      put(&o, t, 2); }
    /* Section aux record: 18 bytes like every other symbol record, so the
     * string table that follows starts where the header says it does (lld
     * trusts the offset and crashes on a short record). */
    put32(&o, sec_size);               /* aux: section length */
    put16(&o, (unsigned short)nreloc);
    put16(&o, 0);                      /* line numbers */
    put32(&o, 0);                      /* checksum */
    put16(&o, 0);                      /* associated section */
    { unsigned char t[4] = {0, 0, 0, 0}; put(&o, t, 4); }
    put32(&o, 4);                      /* empty string table */

    size_t wrote = fwrite(o.buf, 1, o.len, out);
    fclose(out);
    free(o.buf);
    free(data_off);
    free(reloc_at);
    free(sec.buf);
    free(grp.buf);
    free(ents);
    return wrote == o.len ? 0 : 4;

oom2:
    free(data_off);
    free(reloc_at);
oom:
    free(sec.buf);
    free(grp.buf);
    free(ents);
    return 3;
}

int zan_winres_icon_object_mem(const unsigned char *ico, size_t ico_len,
                              const char *out_obj_path, int arm64) {
    return icon_object(ico, ico_len, out_obj_path, arm64);
}

int zan_winres_icon_object(const char *ico_path, const char *out_obj_path,
                          int arm64) {
    size_t ico_len = 0;
    unsigned char *ico = read_file(ico_path, &ico_len);
    if (!ico) return 1;
    int r = icon_object(ico, ico_len, out_obj_path, arm64);
    free(ico);
    return r;
}
