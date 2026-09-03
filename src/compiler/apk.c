/* apk.c -- one-shot Android APK assembly for zanc (--emit-apk). See apk.h.
 *
 * Everything here runs on the build host with no Android SDK: the shell
 * manifest/dex/arsc are prebuilt, the zip writer is local, and the only
 * external process is Java running the staged apksigner.jar. */

#include "apk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "../common/host_oom.h"

/* ========================= small dynamic buffer ========================= */

typedef struct {
    unsigned char *p;
    size_t len, cap;
} buf_t;

static void buf_init(buf_t *b) { b->p = NULL; b->len = 0; b->cap = 0; }

static void buf_free(buf_t *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

static void buf_reserve(buf_t *b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 4096;
    while (nc < b->len + extra) nc *= 2;
    unsigned char *np = (unsigned char *)realloc(b->p, nc);
    if (!np) { zan_host_oom(); }
    b->p = np; b->cap = nc;
}

static void buf_write(buf_t *b, const void *d, size_t n) {
    buf_reserve(b, n);
    memcpy(b->p + b->len, d, n);
    b->len += n;
}

static void buf_u16(buf_t *b, uint16_t v) { buf_write(b, &v, 2); }
static void buf_u32(buf_t *b, uint32_t v) { buf_write(b, &v, 4); }
static void buf_u8(buf_t *b, uint8_t v) { buf_write(b, &v, 1); }

/* ========================= CRC-32 (IEEE) ========================= */

static uint32_t crc32_buf(const unsigned char *d, size_t n) {
    static uint32_t table[256];
    static int have = 0;
    if (!have) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        have = 1;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ========================= raw DEFLATE (stored-ish) =========================
 * The manifest and dex are tiny; native libs must be STORED anyway. Using
 * real deflate for the two compressed entries keeps APKs honest without a
 * zlib dependency: a fixed-Huffman "stored block" deflate wrapper emits
 * valid deflate streams for any input, just without shrinkage. apksigner
 * and the Android package parser accept it (compress ratio 1.0). */

static size_t deflate_store(const unsigned char *in, size_t n,
                            unsigned char **out) {
    /* raw deflate stream of non-final stored blocks (65535 bytes each) */
    size_t nb = (n + 65534) / 65535;
    size_t cap = n + nb * 5 + 16;
    unsigned char *o = (unsigned char *)malloc(cap ? cap : 1);
    if (!o) zan_host_oom();
    size_t w = 0;
    size_t off = 0;
    if (n == 0) {
        /* empty final stored block: header + LEN=0 + NLEN=0xFFFF */
        o[w++] = 0x01; o[w++] = 0x00; o[w++] = 0x00; o[w++] = 0xFF; o[w++] = 0xFF;
    }
    while (off < n) {
        size_t chunk = n - off;
        if (chunk > 65535) chunk = 65535;
        int final = (off + chunk == n) ? 1 : 0;
        o[w++] = (unsigned char)(final ? 1 : 0);
        o[w++] = (unsigned char)(chunk & 0xFF);
        o[w++] = (unsigned char)((chunk >> 8) & 0xFF);
        o[w++] = (unsigned char)(~chunk & 0xFF);
        o[w++] = (unsigned char)((~chunk >> 8) & 0xFF);
        memcpy(o + w, in + off, chunk);
        w += chunk;
        off += chunk;
    }
    *out = o;
    return w;
}

/* ========================= zip writer ========================= */

typedef struct {
    char name[128];
    size_t offset;      /* local header offset in the output */
    uint32_t crc, csize, usize;
    uint16_t method;    /* 0 stored, 8 deflate */
    int align;          /* pad before data so (header_end) % align == 0 */
} zip_ent_t;

typedef struct {
    buf_t out;
    zip_ent_t ents[64];
    int nent;
} zip_t;

static void zip_start(zip_t *z) { buf_init(&z->out); z->nent = 0; }

static void zip_end_align(zip_t *z, int boundary) {
    while (z->out.len % (size_t)boundary) buf_u8(&z->out, 0);
}

static int zip_add(zip_t *z, const char *name, const unsigned char *data,
                   size_t n, int compress, int align) {
    if (z->nent >= (int)(sizeof(z->ents) / sizeof(z->ents[0]))) return -1;
    zip_ent_t *e = &z->ents[z->nent];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->method = compress ? 8 : 0;
    e->align = align;
    e->usize = (uint32_t)n;
    e->crc = crc32_buf(data, n);

    unsigned char *comp = NULL;
    size_t csize = n;
    if (compress) csize = deflate_store(data, n, &comp);
    e->csize = (uint32_t)csize;

    e->offset = z->out.len;
    size_t name_len = strlen(name);
    /* Align a STORED entry's data start by padding before its header. The
     * very first entry must not be preceded by padding: Android's package
     * parser requires the file to begin with the local-header magic. */
    if (align > 1 && e->method == 0 && z->nent > 0) {
        size_t need = 30 + name_len;
        while ((z->out.len + need) % (size_t)align) buf_u8(&z->out, 0);
        e->offset = z->out.len;
    }
    buf_u32(&z->out, 0x04034b50u);
    buf_u16(&z->out, 20);       /* version needed */
    buf_u16(&z->out, 0);        /* flags */
    buf_u16(&z->out, e->method);
    buf_u16(&z->out, 0);        /* mod time */
    buf_u16(&z->out, 0x21);     /* mod date (1980-01-01) */
    buf_u32(&z->out, e->crc);
    buf_u32(&z->out, (uint32_t)csize);
    buf_u32(&z->out, e->usize);
    buf_u16(&z->out, (uint16_t)name_len);
    buf_u16(&z->out, 0);        /* extra len */
    buf_write(&z->out, name, name_len);
    buf_write(&z->out, compress ? comp : data, csize);
    if (compress) free(comp);
    z->nent++;
    return 0;
}

static int zip_finish(zip_t *z) {
    size_t cen = z->out.len;
    for (int i = 0; i < z->nent; i++) {
        zip_ent_t *e = &z->ents[i];
        size_t name_len = strlen(e->name);
        buf_u32(&z->out, 0x02014b50u);
        buf_u16(&z->out, 20);   /* version made by */
        buf_u16(&z->out, 20);   /* version needed */
        buf_u16(&z->out, 0);    /* flags */
        buf_u16(&z->out, e->method);
        buf_u16(&z->out, 0);    /* time */
        buf_u16(&z->out, 0x21); /* date */
        buf_u32(&z->out, e->crc);
        buf_u32(&z->out, e->csize);
        buf_u32(&z->out, e->usize);
        buf_u16(&z->out, (uint16_t)name_len);
        buf_u16(&z->out, 0);    /* extra */
        buf_u16(&z->out, 0);    /* comment */
        buf_u16(&z->out, 0);    /* disk */
        buf_u16(&z->out, 0);    /* internal attrs */
        buf_u32(&z->out, 0);    /* external attrs */
        buf_u32(&z->out, (uint32_t)e->offset);
        buf_write(&z->out, e->name, name_len);
    }
    size_t cen_size = z->out.len - cen;
    buf_u32(&z->out, 0x06054b50u);
    buf_u16(&z->out, 0); buf_u16(&z->out, 0);
    buf_u16(&z->out, (uint16_t)z->nent);
    buf_u16(&z->out, (uint16_t)z->nent);
    buf_u32(&z->out, (uint32_t)cen_size);
    buf_u32(&z->out, (uint32_t)cen);
    buf_u16(&z->out, 0);
    return 0;
}

/* ========================= AXML string-pool patching =========================
 * The template AndroidManifest.xml is a binary AXML file. Its string pool
 * (chunk type 0x0001, UTF-16LE) holds every attribute value; the package
 * name and application label are ordinary strings inside it. Patching =
 * rebuild the pool with the two strings replaced and fix the offsets, plus
 * every chunk size that depends on the pool length (just the root's). */

static int axml_patch(const unsigned char *xml, size_t xml_len,
                      const char *package, const char *label,
                      unsigned char **out, size_t *out_len) {
    if (xml_len < 40) return -1;
    uint32_t root_size;
    uint16_t root_hdr;
    memcpy(&root_hdr, xml + 2, 2);
    memcpy(&root_size, xml + 4, 4);
    if (root_hdr != 8 || root_size != xml_len) return -1; /* flat file */
    uint16_t first_type, first_hdr;
    uint32_t first_size;
    memcpy(&first_type, xml + 8, 2);
    memcpy(&first_hdr, xml + 10, 2);
    memcpy(&first_size, xml + 12, 4);
    if (first_type != 0x0001 || first_hdr != 28) return -1;
    /* ResStringPool header at xml+8: type(2) hdrSize(2) size(4) stringCount(4)
     * styleCount(4) flags(4) stringsStart(4) stylesStart(4) = 28 bytes. */
    uint32_t pool_size, str_start;
    uint32_t flags32;
    memcpy(&pool_size, xml + 12, 4);
    memcpy(&flags32, xml + 24, 4);
    memcpy(&str_start, xml + 28, 4);
    int utf8 = (flags32 & 0x100) != 0;
    (void)pool_size;
    if (8 + first_size > xml_len || str_start < 28) return -1;

    size_t pool_off = 8;
    const unsigned char *pool = xml + pool_off;
    uint32_t str_count;
    memcpy(&str_count, pool + 8, 4);
    if (str_count > 4096) return -1;
    const uint32_t *offs = (const uint32_t *)(pool + 28);
    const unsigned char *base = pool + str_start;

    /* extract strings as byte blobs (prefix + data + terminator, raw);
     * heap tables: 4096 entries x 256B of stack would overflow 1MB stacks */
    unsigned char **raw = (unsigned char **)calloc(str_count, sizeof(*raw));
    size_t *raw_len = (size_t *)calloc(str_count, sizeof(*raw_len));
    char (*text)[256] = (char (*)[256])calloc(str_count, 256);
    if (!raw || !raw_len || !text) zan_host_oom();
    for (uint32_t i = 0; i < str_count; i++) {
        size_t p = (size_t)(base - pool) + offs[i];
        if (p >= first_size) return -1;
        if (utf8) {
            uint16_t u16n, u8n;
            memcpy(&u8n, pool + p, 1); u8n = pool[p];       /* chars (8-bit) */
            memcpy(&u16n, pool + p + 1, 1);
            /* handle the two forms: 8-bit or 16-bit length prefix */
            if (pool[p] & 0x80) return -1; /* long strings unsupported */
            size_t n = pool[p];
            size_t hl = 1;
            /* aapt2 writes len8 then data; but some encoders use len16 */
            memcpy(text[i], pool + p + hl, n < 255 ? n : 255);
            text[i][n < 255 ? n : 255] = '\0';
            raw_len[i] = hl + n + 1; /* len + data + nul */
            raw[i] = (unsigned char *)malloc(raw_len[i] ? raw_len[i] : 1);
            if (!raw[i]) zan_host_oom();
            memcpy(raw[i], pool + p, raw_len[i]);
        } else {
            uint16_t n;
            memcpy(&n, pool + p, 2);
            size_t chars = n < 255 ? n : 255;
            for (size_t c = 0; c < chars; c++) {
                uint16_t ch;
                memcpy(&ch, pool + p + 2 + c * 2, 2);
                text[i][c] = (char)(ch < 128 ? ch : '?');
            }
            text[i][chars] = '\0';
            raw_len[i] = 2 + (size_t)n * 2 + 2;
            raw[i] = (unsigned char *)malloc(raw_len[i] ? raw_len[i] : 1);
            if (!raw[i]) zan_host_oom();
            memcpy(raw[i], pool + p, raw_len[i]);
        }
    }

    /* locate package + label by value; the template holds exactly one of each */
    int pkg_i = -1, lbl_i = -1;
    for (uint32_t i = 0; i < str_count; i++) {
        if (strcmp(text[i], "dev.zan.app") == 0 && pkg_i < 0) pkg_i = (int)i;
        if (strcmp(text[i], "Zan App") == 0 && lbl_i < 0) lbl_i = (int)i;
    }
    if (pkg_i < 0) { fprintf(stderr, "error: template manifest lacks the "
                             "package placeholder\n"); return -1; }

    /* replace helper: build "len + utf16 + nul" (or utf8 form) */
    unsigned char repl[2][1200];
    size_t repl_len[2];
    const char *vals[2] = { package, label };
    int slots[2] = { pkg_i, lbl_i };
    for (int v = 0; v < 2; v++) {
        if (slots[v] < 0) { repl_len[v] = 0; continue; }
        size_t n = strlen(vals[v]);
        if (utf8) {
            repl[v][0] = (unsigned char)n;
            memcpy(repl[v] + 1, vals[v], n);
            repl[v][1 + n] = 0;
            repl_len[v] = 1 + n + 1;
        } else {
            for (size_t c = 0; c < n; c++) {
                uint16_t ch = (unsigned char)vals[v][c];
                memcpy(repl[v] + 2 + c * 2, &ch, 2);
            }
            uint16_t u16n = (uint16_t)n, zero = 0;
            memcpy(repl[v], &u16n, 2);
            memcpy(repl[v] + 2 + n * 2, &zero, 2);
            repl_len[v] = 2 + n * 2 + 2;
        }
    }

    /* rebuild the pool */
    buf_t nb; buf_init(&nb);
    uint32_t *new_offs = (uint32_t *)malloc(sizeof(uint32_t) * str_count);
    if (!new_offs) zan_host_oom();
    uint32_t acc = 0;
    for (uint32_t i = 0; i < str_count; i++) {
        int slot = (i == (uint32_t)pkg_i) ? 0
                 : (lbl_i >= 0 && i == (uint32_t)lbl_i) ? 1 : -1;
        const unsigned char *d = slot >= 0 ? repl[slot] : raw[i];
        size_t dl = slot >= 0 ? repl_len[slot] : raw_len[i];
        new_offs[i] = acc;
        buf_write(&nb, d, dl);
        acc += (uint32_t)dl;
    }
    uint32_t new_str_start = 28 + str_count * 4;
    uint32_t new_pool_size = new_str_start + (uint32_t)nb.len;
    uint32_t pad = (4 - (new_pool_size % 4)) % 4;
    new_pool_size += pad;

    buf_t ob; buf_init(&ob);
    /* root chunk header (8 bytes) then pool then the rest */
    buf_write(&ob, xml, 8);
    uint16_t pt = 0x0001, ph = 28;
    buf_write(&ob, &pt, 2);
    buf_write(&ob, &ph, 2);
    buf_write(&ob, &new_pool_size, 4);
    buf_write(&ob, &str_count, 4);
    uint32_t zero32 = 0;
    buf_write(&ob, &zero32, 4);            /* style count */
    buf_write(&ob, &flags32, 4);           /* flags */
    buf_write(&ob, &new_str_start, 4);
    buf_write(&ob, &zero32, 4);            /* styles start */
    buf_write(&ob, new_offs, 4 * str_count);
    buf_write(&ob, nb.p, nb.len);
    { unsigned char z4[4] = {0,0,0,0}; buf_write(&ob, z4, pad); }
    /* the remaining chunks, sizes untouched (they carry no pool offsets) */
    buf_write(&ob, xml + 8 + first_size, xml_len - (8 + first_size));
    /* fix the root file-size field */
    { uint32_t total = (uint32_t)ob.len; memcpy(ob.p + 4, &total, 4); }

    *out = ob.p; *out_len = ob.len;
    for (uint32_t i = 0; i < str_count; i++) free(raw[i]);
    free(raw); free(raw_len); free(text);
    free(new_offs);
    buf_free(&nb);
    return 0;
}

/* ========================= file helpers ========================= */

static unsigned char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    unsigned char *d = (unsigned char *)malloc((size_t)n + 1);
    if (!d) { fclose(f); zan_host_oom(); }
    if (n > 0 && fread(d, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(d); return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return d;
}

static int write_all(const char *path, const unsigned char *d, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (n > 0 && fwrite(d, 1, n, f) != n) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

/* ========================= Java discovery + auto-download ========================= */

static int file_exists(const char *p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static int dir_exists(const char *p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* Find a java executable: JAVA_HOME/bin/java(.exe), then PATH. */
static int find_java(char *out, size_t outsz) {
    const char *home = getenv("JAVA_HOME");
    if (home && home[0]) {
#ifdef _WIN32
        snprintf(out, outsz, "%s\\bin\\java.exe", home);
#else
        snprintf(out, outsz, "%s/bin/java", home);
#endif
        if (file_exists(out)) return 0;
    }
#ifdef _WIN32
    /* best-effort PATH probe via SearchPath */
    char found[MAX_PATH];
    if (SearchPathA(NULL, "java.exe", NULL, MAX_PATH, found, NULL) > 0) {
        snprintf(out, outsz, "%s", found);
        return 0;
    }
#else
    if (file_exists("/usr/bin/java") || file_exists("/usr/local/bin/java")) {
        snprintf(out, outsz, "java");
        return 0;
    }
    FILE *w = popen("command -v java 2>/dev/null", "r");
    if (w) {
        char line[1024];
        if (fgets(line, sizeof(line), w)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
            if (l > 0 && file_exists(line)) { snprintf(out, outsz, "%s", line); pclose(w); return 0; }
        }
        pclose(w);
    }
#endif
    return -1;
}

/* Download url -> path via zan_http_get (winhttp on Windows; the POSIX
 * backend in stdlib_ext.c is a stub that fails fast so the user gets a
 * clear message instead of a hang). Small and synchronous: the jar is
 * ~1 MB. */
#include "stdlib_ext.h"

static int download_file(const char *url, const char *path) {
    zan_http_response_t *resp = zan_http_get(url);
    if (!resp || resp->status_code != 200 || !resp->body || resp->body_len == 0) {
        zan_http_response_free(resp);
        return -1;
    }
    int rc = write_all(path, (const unsigned char *)resp->body, resp->body_len);
    zan_http_response_free(resp);
    return rc;
}

/* Ensure a usable Java exists; downloads a portable JRE into ~/.zan/java on
 * first use. Returns 0 and fills the java path, or -1 after printing why. */
static int ensure_java(char *out, size_t outsz) {
    if (find_java(out, outsz) == 0) return 0;

#ifdef _WIN32
    const char *url =
        "https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jre/hotspot/normal/eclipse"
        "?project=jdk";
    const char *zdir_name = "jre-win";
#else
    fprintf(stderr,
        "error: apksigner needs Java, and no java was found on PATH.\n"
        "       Install a JRE (e.g. your package manager's default-jre) and retry.\n");
    return -1;
#endif

    const char *home = getenv("USERPROFILE");
    if (!home || !home[0]) home = getenv("HOME");
    if (!home || !home[0]) {
        fprintf(stderr, "error: Java is required for APK signing but no java "
                "was found and no home directory is set\n");
        return -1;
    }
    char zdir[1200], zip_path[1300], done[1300];
#ifdef _WIN32
    snprintf(zdir, sizeof(zdir), "%s\\.zan", home);
    snprintf(zip_path, sizeof(zip_path), "%s\\jre.zip", zdir);
    snprintf(done, sizeof(done), "%s\\jre-win\\.done", zdir);
#else
    snprintf(zdir, sizeof(zdir), "%s/.zan", home);
    snprintf(zip_path, sizeof(zip_path), "%s/jre.zip", zdir);
    snprintf(done, sizeof(done), "%s/jre-win/.done", zdir);
#endif
    if (!dir_exists(zdir)) {
#ifdef _WIN32
        CreateDirectoryA(zdir, NULL);
#else
        mkdir(zdir, 0755);
#endif
    }
    if (file_exists(done)) {
        /* previously downloaded */
#ifdef _WIN32
        snprintf(out, outsz, "%s\\jre-win\\bin\\java.exe", zdir);
#else
        snprintf(out, outsz, "%s/jre-win/bin/java", zdir);
#endif
        if (file_exists(out)) return 0;
    }

    printf("  note: no Java found -- downloading a portable JRE (~50 MB, once)\n");
    if (download_file(url, zip_path) != 0) {
        fprintf(stderr,
            "error: could not download the signing runtime (Java).\n"
            "       Install a JRE and make sure 'java' is on PATH, then retry.\n");
        return -1;
    }

    /* unzip with PowerShell (ships with Windows) */
    char cmd[3000];
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -Command \"$ErrorActionPreference='Stop';"
        "Expand-Archive -Force -LiteralPath '%s' -DestinationPath '%s'\"",
        zip_path, zdir);
    int rc = system(cmd);
    remove(zip_path);
    if (rc != 0) {
        fprintf(stderr, "error: could not unpack the downloaded JRE\n");
        return -1;
    }
    /* adoptium archives as jdk-21.x.y+z-hotspot/ -> rename to a fixed dir */
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -Command \"$d=Get-ChildItem '%s' -Directory |"
        "Where-Object Name -like 'jdk-*'; if($d){Move-Item -Force $d.FullName '%s\\jre-win'}\"",
        zdir, zdir);
    system(cmd);
    FILE *df = fopen(done, "wb");
    if (df) { fputs("ok\n", df); fclose(df); }
#ifdef _WIN32
    snprintf(out, outsz, "%s\\jre-win\\bin\\java.exe", zdir);
#else
    snprintf(out, outsz, "%s/jre-win/bin/java", zdir);
#endif
    if (!file_exists(out)) {
        fprintf(stderr, "error: JRE download did not produce %s\n", out);
        return -1;
    }
    return 0;
}

/* ========================= keystore + signing ========================= */

static int find_java(char *out, size_t outsz);

static int ensure_keystore(char *out, size_t outsz) {
    const char *home = getenv("USERPROFILE");
    if (!home || !home[0]) home = getenv("HOME");
    if (!home || !home[0]) return -1;
#ifdef _WIN32
    snprintf(out, outsz, "%s\\.zan\\debug.keystore", home);
#else
    snprintf(out, outsz, "%s/.zan/debug.keystore", home);
#endif
    if (file_exists(out)) return 0;
#ifdef _WIN32
    { char dir[1200]; snprintf(dir, sizeof(dir), "%s\\.zan", home);
      if (!dir_exists(dir)) CreateDirectoryA(dir, NULL); }
#endif
    return 0; /* path resolved; caller generates the file if absent */
}

static int run_quiet(const char *cmd) {
    int rc = system(cmd);
    return rc == 0 ? 0 : -1;
}

/* ========================= main entry ========================= */

int zan_apk_build(const char *apk_path, const char *lib_main,
                  const char *abi, const char *package, const char *label,
                  const char *shell_dir, char **extra_libs, int extra_count) {
    char tmp_apk[1400];
    snprintf(tmp_apk, sizeof(tmp_apk), "%s.tmp", apk_path);

    /* ---- load shell assets ---- */
    char path[1400];
    size_t man_len = 0, arsc_len = 0, dex_len = 0, lib_len = 0;
    snprintf(path, sizeof(path), "%s/AndroidManifest.xml.bin", shell_dir);
    unsigned char *manifest = read_all(path, &man_len);
    snprintf(path, sizeof(path), "%s/resources.arsc", shell_dir);
    unsigned char *arsc = read_all(path, &arsc_len);
    snprintf(path, sizeof(path), "%s/classes.dex", shell_dir);
    unsigned char *dex = read_all(path, &dex_len);
    unsigned char *lib = read_all(lib_main, &lib_len);
    if (!manifest || !arsc || !dex || !lib) {
        fprintf(stderr, "error: APK shell assets missing in '%s' "
                "(manifest/arsc/dex) or library '%s' unreadable\n",
                shell_dir, lib_main);
        free(manifest); free(arsc); free(dex); free(lib);
        return 1;
    }

    /* ---- patch the manifest string pool ---- */
    unsigned char *man2 = NULL; size_t man2_len = 0;
    int prc = axml_patch(manifest, man_len, package, label, &man2, &man2_len);
    free(manifest);
    if (prc != 0) { fprintf(stderr, "error: manifest patch failed\n");
        free(arsc); free(dex); free(lib); return 1; }

    /* ---- assemble the zip ---- */
    zip_t z; zip_start(&z);
    zip_add(&z, "AndroidManifest.xml", man2, man2_len, 1, 4);
    zip_add(&z, "resources.arsc", arsc, arsc_len, 0, 4);
    zip_add(&z, "classes.dex", dex, dex_len, 1, 4);
    char lname[160];
    snprintf(lname, sizeof(lname), "lib/%s/libmain.so", abi);
    zip_add(&z, lname, lib, lib_len, 0, 4);
    for (int i = 0; i < extra_count; i++) {
        size_t elen = 0;
        unsigned char *edata = read_all(extra_libs[i], &elen);
        if (!edata) {
            fprintf(stderr, "error: cannot read bundled library '%s'\n",
                    extra_libs[i]);
            free(edata); buf_free(&z.out); free(man2); free(arsc);
            free(dex); free(lib); return 1;
        }
        const char *base = strrchr(extra_libs[i], '/');
        const char *base2 = strrchr(extra_libs[i], '\\');
        if (base2 > base) base = base2;
        base = base ? base + 1 : extra_libs[i];
        char ename[200];
        snprintf(ename, sizeof(ename), "lib/%s/%s", abi, base);
        zip_add(&z, ename, edata, elen, 0, 4);
        free(edata);
    }
    zip_finish(&z);
    int rc = write_all(tmp_apk, z.out.p, z.out.len);
    buf_free(&z.out);
    free(man2); free(arsc); free(dex); free(lib);
    if (rc != 0) {
        fprintf(stderr, "error: cannot write '%s'\n", tmp_apk);
        return 1;
    }

    /* ---- sign ---- */
    char exe_dir[1200] = {0};
#ifdef _WIN32
    { char mod[1200]; DWORD n = GetModuleFileNameA(NULL, mod, sizeof(mod));
      if (n > 0 && n < sizeof(mod)) { char *s = strrchr(mod, '\\'); if (s) *s = 0; snprintf(exe_dir, sizeof(exe_dir), "%s", mod); } }
#elif defined(__APPLE__)
    { uint32_t sz = sizeof(exe_dir); if (_NSGetExecutablePath(exe_dir, &sz) != 0) exe_dir[0] = 0; else { char *s = strrchr(exe_dir, '/'); if (s) *s = 0; } }
#else
    { ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
      if (n > 0) { exe_dir[n] = 0; char *s = strrchr(exe_dir, '/'); if (s) *s = 0; } }
#endif
    char signer[1300];
    snprintf(signer, sizeof(signer), "%s/apksigner.jar", exe_dir);
    if (!file_exists(signer)) {
        /* also staged inside the apk-shell asset directory */
        snprintf(signer, sizeof(signer), "%s/apk-shell/apksigner.jar", exe_dir);
    }
    if (!file_exists(signer)) {
        fprintf(stderr, "error: apksigner.jar not found next to zan "
                "(expected apk-shell/apksigner.jar; reinstall zan)\n");
        remove(tmp_apk);
        return 1;
    }

    char java[1200];
    if (ensure_java(java, sizeof(java)) != 0) { remove(tmp_apk); return 1; }

    char ks[1200];
    if (ensure_keystore(ks, sizeof(ks)) != 0) {
        fprintf(stderr, "error: cannot place the debug keystore\n");
        remove(tmp_apk);
        return 1;
    }
    if (!file_exists(ks)) {
        /* keytool ships with every JRE/JDK, next to java */
        char keytool[1200], cmd[3000];
        { char *s = strrchr(java, '/'); char *s2 = strrchr(java, '\\');
          char *cut = (s2 > s) ? s2 : s;
          if (cut) { size_t keep = (size_t)(cut - java); snprintf(keytool, sizeof(keytool), "%.*s", (int)keep, java); }
          else snprintf(keytool, sizeof(keytool), "%s", java); }
#ifdef _WIN32
        /* system() runs `cmd /c <string>`; a string that both starts and
         * ends with a quote gets its outer pair stripped, so wrap the whole
         * command in one extra pair (the classic cmd /c quirk). */
        snprintf(cmd, sizeof(cmd), "\"\"%s\\keytool.exe\" -genkeypair -keystore \"%s\""
                 " -storepass android -keypass android -alias zan"
                 " -dname CN=Zan_Debug -keyalg RSA -keysize 2048"
                 " -validity 10000 -storetype PKCS12\"", keytool, ks);
#else
        snprintf(cmd, sizeof(cmd), "\"%s/keytool\" -genkeypair -keystore \"%s\""
                 " -storepass android -keypass android -alias zan"
                 " -dname \"CN=Zan Debug\" -keyalg RSA -keysize 2048"
                 " -validity 10000 -storetype PKCS12 >/dev/null 2>&1", keytool, ks);
#endif
        if (run_quiet(cmd) != 0 || !file_exists(ks)) {
            fprintf(stderr, "error: could not generate the debug keystore "
                    "(keytool missing from the Java runtime?)\n");
            remove(tmp_apk);
            return 1;
        }
        printf("  generated debug keystore ? %s\n", ks);
    }

    char scmd[4000];
#ifdef _WIN32
    snprintf(scmd, sizeof(scmd),
             "\"\"%s\" -jar \"%s\" sign --ks \"%s\" --ks-pass pass:android"
             " --out \"%s\" \"%s\"\"", java, signer, ks, apk_path, tmp_apk);
#else
    snprintf(scmd, sizeof(scmd),
             "\"%s\" -jar \"%s\" sign --ks \"%s\" --ks-pass pass:android"
             " --out \"%s\" \"%s\" >/dev/null 2>&1", java, signer, ks, apk_path, tmp_apk);
#endif
    if (run_quiet(scmd) != 0) {
        fprintf(stderr, "error: APK signing failed (apksigner). Run with the"
                " same command manually for details:\n  java -jar %s sign "
                "--ks %s --out %s %s\n", signer, ks, apk_path, tmp_apk);
        remove(tmp_apk);
        return 1;
    }
    remove(tmp_apk);
    return 0;
}
