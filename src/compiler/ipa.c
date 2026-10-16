
#include "ipa.h"
#include "miniz.h"
#include "zan_default_icon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} ipa_buf_t;

static void buf_init(ipa_buf_t *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_free(ipa_buf_t *b) {
    if (b->data) free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static void buf_write(ipa_buf_t *b, const void *p, size_t n) {
    if (n == 0) return;
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n) nc *= 2;
        unsigned char *nd = (unsigned char *)realloc(b->data, nc);
        if (!nd) {
            fprintf(stderr, "error: out of memory in IPA packager\n");
            exit(1);
        }
        b->data = nd;
        b->cap = nc;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void buf_u16(ipa_buf_t *b, uint16_t v) { buf_write(b, &v, 2); }
static void buf_u32(ipa_buf_t *b, uint32_t v) { buf_write(b, &v, 4); }

static uint32_t ipa_crc32(const unsigned char *d, size_t n) {
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

typedef struct {
    char name[256];
    size_t offset;
    uint32_t crc;
    uint32_t uncomp_size;
    uint32_t comp_size;
    uint16_t method;
    uint32_t mode;
} ipa_zip_ent_t;

typedef struct {
    ipa_buf_t out;
    ipa_zip_ent_t *ents;
    int nent;
    int cap;
} ipa_zip_t;

static void zip_start(ipa_zip_t *z) {
    buf_init(&z->out);
    z->cap = 32;
    z->nent = 0;
    z->ents = (ipa_zip_ent_t *)calloc(z->cap, sizeof(ipa_zip_ent_t));
    if (!z->ents) {
        fprintf(stderr, "error: out of memory in zip_start\n");
        exit(1);
    }
}

static void zip_destroy(ipa_zip_t *z) {
    buf_free(&z->out);
    if (z->ents) free(z->ents);
    z->ents = NULL;
}

static int zip_add(ipa_zip_t *z, const char *name, const void *data, size_t sz, uint32_t mode) {
    if (z->nent >= z->cap) {
        int ncap = z->cap * 2;
        ipa_zip_ent_t *ne = (ipa_zip_ent_t *)realloc(z->ents, sizeof(ipa_zip_ent_t) * ncap);
        if (!ne) return -1;
        z->ents = ne;
        z->cap = ncap;
    }

    ipa_zip_ent_t *e = &z->ents[z->nent];
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    e->uncomp_size = (uint32_t)sz;
    e->crc = ipa_crc32((const unsigned char *)data, sz);
    e->offset = z->out.len;
    e->mode = mode;

    size_t comp_len = 0;
    void *comp = NULL;
    if (sz > 64) {
        comp = tdefl_compress_mem_to_heap(data, sz, &comp_len, TDEFL_DEFAULT_MAX_PROBES);
    }

    const void *payload = data;
    if (comp && comp_len < sz) {
        e->method = 8; /* Deflate */
        e->comp_size = (uint32_t)comp_len;
        payload = comp;
    } else {
        e->method = 0; /* Store */
        e->comp_size = (uint32_t)sz;
    }

    size_t name_len = strlen(name);

    buf_u32(&z->out, 0x04034b50u);
    buf_u16(&z->out, 20);
    buf_u16(&z->out, 0);            /* flags */
    buf_u16(&z->out, e->method);    /* method: 8 (deflate) or 0 (store) */
    buf_u16(&z->out, 0);
    buf_u16(&z->out, 0x21);
    buf_u32(&z->out, e->crc);
    buf_u32(&z->out, e->comp_size);
    buf_u32(&z->out, e->uncomp_size);
    buf_u16(&z->out, (uint16_t)name_len);
    buf_u16(&z->out, 0);
    buf_write(&z->out, name, name_len);
    buf_write(&z->out, payload, e->comp_size);

    if (comp) free(comp);

    z->nent++;
    return 0;
}

static int zip_finish(ipa_zip_t *z) {
    size_t cen = z->out.len;
    for (int i = 0; i < z->nent; i++) {
        ipa_zip_ent_t *e = &z->ents[i];
        size_t name_len = strlen(e->name);

        buf_u32(&z->out, 0x02014b50u);
        buf_u16(&z->out, 0x0314);
        buf_u16(&z->out, 20);
        buf_u16(&z->out, 0);        /* flags */
        buf_u16(&z->out, e->method);/* compression: deflate / store */
        buf_u16(&z->out, 0);
        buf_u16(&z->out, 0x21);
        buf_u32(&z->out, e->crc);
        buf_u32(&z->out, e->comp_size);
        buf_u32(&z->out, e->uncomp_size);
        buf_u16(&z->out, (uint16_t)name_len);
        buf_u16(&z->out, 0);        /* extra */
        buf_u16(&z->out, 0);        /* comment */
        buf_u16(&z->out, 0);        /* disk # */
        buf_u16(&z->out, 0);
        uint32_t ext_attr = (e->mode ? e->mode : 0100644) << 16;
        buf_u32(&z->out, ext_attr);
        buf_u32(&z->out, (uint32_t)e->offset);
        buf_write(&z->out, e->name, name_len);
    }

    size_t cen_size = z->out.len - cen;

    buf_u32(&z->out, 0x06054b50u);
    buf_u16(&z->out, 0);
    buf_u16(&z->out, 0);
    buf_u16(&z->out, (uint16_t)z->nent);
    buf_u16(&z->out, (uint16_t)z->nent);
    buf_u32(&z->out, (uint32_t)cen_size);
    buf_u32(&z->out, (uint32_t)cen);
    buf_u16(&z->out, 0);
    return 0;
}

static unsigned char *read_file_bytes(const char *path, size_t *out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    unsigned char *b = (unsigned char *)malloc((size_t)sz ? (size_t)sz : 1);
    if (!b) { fclose(f); return NULL; }
    if (sz > 0 && fread(b, 1, (size_t)sz, f) != (size_t)sz) {
        free(b);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_sz = (size_t)sz;
    return b;
}

static unsigned char *extract_png_from_ico_or_png(const unsigned char *data, size_t sz, size_t *out_sz) {
    if (!data || sz < 8) return NULL;
    /* 1. Check if data is already a PNG: \x89PNG\r\n\x1a\n */
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4e && data[3] == 0x47 &&
        data[4] == 0x0d && data[5] == 0x0a && data[6] == 0x1a && data[7] == 0x0a) {
        unsigned char *copy = (unsigned char *)malloc(sz);
        if (copy) {
            memcpy(copy, data, sz);
            *out_sz = sz;
        }
        return copy;
    }
    /* 2. Check if data is Windows ICO (0x0000 0x0001) */
    if (sz >= 22 && data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] == 0) {
        uint16_t count = (uint16_t)(data[4] | (data[5] << 8));
        int best_width = -1;
        uint32_t best_offset = 0;
        uint32_t best_size = 0;
        for (uint16_t i = 0; i < count; i++) {
            size_t entry_off = 6 + (size_t)i * 16;
            if (entry_off + 16 > sz) break;
            int w = data[entry_off];
            if (w == 0) w = 256;
            uint32_t img_sz = (uint32_t)(data[entry_off + 8] | (data[entry_off + 9] << 8) |
                                        (data[entry_off + 10] << 16) | (data[entry_off + 11] << 24));
            uint32_t img_off = (uint32_t)(data[entry_off + 12] | (data[entry_off + 13] << 8) |
                                         (data[entry_off + 14] << 16) | (data[entry_off + 15] << 24));
            if (img_off + img_sz <= sz && img_sz >= 8) {
                const unsigned char *p = data + img_off;
                if (p[0] == 0x89 && p[1] == 0x50 && p[2] == 0x4e && p[3] == 0x47) {
                    if (w > best_width) {
                        best_width = w;
                        best_offset = img_off;
                        best_size = img_sz;
                    }
                }
            }
        }
        if (best_width > 0 && best_size > 0) {
            unsigned char *copy = (unsigned char *)malloc(best_size);
            if (copy) {
                memcpy(copy, data + best_offset, best_size);
                *out_sz = best_size;
            }
            return copy;
        }
    }
    return NULL;
}

int zan_ipa_build(const char *ipa_path, const char *binary_path,
                  const char *app_name, const char *bundle_id,
                  const char *display_name, const char *version,
                  const char *icon_path) {
    if (!ipa_path || !binary_path || !app_name) {
        fprintf(stderr, "error: invalid arguments to zan_ipa_build\n");
        return -1;
    }

    size_t bin_sz = 0;
    unsigned char *bin_data = read_file_bytes(binary_path, &bin_sz);
    if (!bin_data) {
        fprintf(stderr, "error: failed to read binary '%s' for IPA packaging\n", binary_path);
        return -1;
    }

    size_t icon_png_sz = 0;
    unsigned char *icon_png = NULL;
    if (icon_path && icon_path[0]) {
        size_t raw_icon_sz = 0;
        unsigned char *raw_icon = read_file_bytes(icon_path, &raw_icon_sz);
        if (raw_icon) {
            icon_png = extract_png_from_ico_or_png(raw_icon, raw_icon_sz, &icon_png_sz);
            free(raw_icon);
        }
    }
    if (!icon_png) {
        icon_png = extract_png_from_ico_or_png(zan_default_icon, ZAN_DEFAULT_ICON_LEN, &icon_png_sz);
    }

    char default_bundle[256];
    if (!bundle_id || !bundle_id[0]) {
        snprintf(default_bundle, sizeof(default_bundle), "dev.zan.%s", app_name);
        bundle_id = default_bundle;
    }
    if (!display_name || !display_name[0]) {
        display_name = app_name;
    }
    if (!version || !version[0]) {
        version = "1.0.0";
    }

    char plist[4096];
    int plist_len = snprintf(plist, sizeof(plist),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>CFBundleDevelopmentRegion</key>\n"
        "    <string>en</string>\n"
        "    <key>CFBundleExecutable</key>\n"
        "    <string>%s</string>\n"
        "    <key>CFBundleIdentifier</key>\n"
        "    <string>%s</string>\n"
        "    <key>CFBundleInfoDictionaryVersion</key>\n"
        "    <string>6.0</string>\n"
        "    <key>CFBundleName</key>\n"
        "    <string>%s</string>\n"
        "    <key>CFBundleDisplayName</key>\n"
        "    <string>%s</string>\n"
        "    <key>CFBundlePackageType</key>\n"
        "    <string>APPL</string>\n"
        "    <key>CFBundleShortVersionString</key>\n"
        "    <string>%s</string>\n"
        "    <key>CFBundleVersion</key>\n"
        "    <string>1</string>\n"
        "    <key>LSRequiresIPhoneOS</key>\n"
        "    <true/>\n"
        "    <key>MinimumOSVersion</key>\n"
        "    <string>14.0</string>\n"
        "    <key>UIDeviceFamily</key>\n"
        "    <array>\n"
        "        <integer>1</integer>\n"
        "        <integer>2</integer>\n"
        "    </array>\n"
        "    <key>UIRequiredDeviceCapabilities</key>\n"
        "    <array>\n"
        "        <string>arm64</string>\n"
        "    </array>\n"
        "    <key>UISupportedInterfaceOrientations</key>\n"
        "    <array>\n"
        "        <string>UIInterfaceOrientationPortrait</string>\n"
        "        <string>UIInterfaceOrientationLandscapeLeft</string>\n"
        "        <string>UIInterfaceOrientationLandscapeRight</string>\n"
        "    </array>\n"
        "    <key>CFBundleIcons</key>\n"
        "    <dict>\n"
        "        <key>CFBundlePrimaryIcon</key>\n"
        "        <dict>\n"
        "            <key>CFBundleIconFiles</key>\n"
        "            <array>\n"
        "                <string>AppIcon20x20</string>\n"
        "                <string>AppIcon29x29</string>\n"
        "                <string>AppIcon40x40</string>\n"
        "                <string>AppIcon60x60</string>\n"
        "            </array>\n"
        "            <key>CFBundleIconName</key>\n"
        "            <string>AppIcon</string>\n"
        "        </dict>\n"
        "    </dict>\n"
        "    <key>CFBundleIcons~ipad</key>\n"
        "    <dict>\n"
        "        <key>CFBundlePrimaryIcon</key>\n"
        "        <dict>\n"
        "            <key>CFBundleIconFiles</key>\n"
        "            <array>\n"
        "                <string>AppIcon20x20</string>\n"
        "                <string>AppIcon29x29</string>\n"
        "                <string>AppIcon40x40</string>\n"
        "                <string>AppIcon60x60</string>\n"
        "                <string>AppIcon76x76</string>\n"
        "                <string>AppIcon83.5x83.5</string>\n"
        "            </array>\n"
        "            <key>CFBundleIconName</key>\n"
        "            <string>AppIcon</string>\n"
        "        </dict>\n"
        "    </dict>\n"
        "    <key>CFBundleIconFiles</key>\n"
        "    <array>\n"
        "        <string>AppIcon20x20</string>\n"
        "        <string>AppIcon29x29</string>\n"
        "        <string>AppIcon40x40</string>\n"
        "        <string>AppIcon60x60</string>\n"
        "        <string>AppIcon76x76</string>\n"
        "        <string>AppIcon83.5x83.5</string>\n"
        "    </array>\n"
        "    <key>CFBundleIconFile</key>\n"
        "    <string>AppIcon60x60</string>\n"
        "</dict>\n"
        "</plist>\n",
        app_name, bundle_id, app_name, display_name, version);

    if (plist_len < 0 || (size_t)plist_len >= sizeof(plist)) {
        if (icon_png) free(icon_png);
        free(bin_data);
        fprintf(stderr, "error: generated Info.plist exceeded buffer\n");
        return -1;
    }

    const char *pkg_info = "APPL????";

    ipa_zip_t zip;
    zip_start(&zip);

    char bin_entry[512];
    snprintf(bin_entry, sizeof(bin_entry), "Payload/%s.app/%s", app_name, app_name);
    if (zip_add(&zip, bin_entry, bin_data, bin_sz, 0100755) != 0) {
        if (icon_png) free(icon_png);
        free(bin_data);
        zip_destroy(&zip);
        return -1;
    }
    free(bin_data);

    char plist_entry[512];
    snprintf(plist_entry, sizeof(plist_entry), "Payload/%s.app/Info.plist", app_name);
    if (zip_add(&zip, plist_entry, plist, (size_t)plist_len, 0100644) != 0) {
        if (icon_png) free(icon_png);
        zip_destroy(&zip);
        return -1;
    }

    char pkg_entry[512];
    snprintf(pkg_entry, sizeof(pkg_entry), "Payload/%s.app/PkgInfo", app_name);
    if (zip_add(&zip, pkg_entry, pkg_info, 8, 0100644) != 0) {
        if (icon_png) free(icon_png);
        zip_destroy(&zip);
        return -1;
    }

    if (icon_png && icon_png_sz > 0) {
        const char *icon_names[] = {
            "AppIcon.png",
            "AppIcon60x60@2x.png",
            "AppIcon60x60@3x.png",
            "AppIcon76x76@2x~ipad.png",
            "AppIcon20x20@2x.png",
            "AppIcon20x20@3x.png",
            "AppIcon29x29@2x.png",
            "AppIcon29x29@3x.png",
            "AppIcon40x40@2x.png",
            "AppIcon40x40@3x.png",
            "AppIcon83.5x83.5@2x~ipad.png",
            "iTunesArtwork",
            NULL
        };
        char icon_entry[512];
        for (int k = 0; icon_names[k]; k++) {
            snprintf(icon_entry, sizeof(icon_entry), "Payload/%s.app/%s", app_name, icon_names[k]);
            zip_add(&zip, icon_entry, icon_png, icon_png_sz, 0100644);
        }
        zip_add(&zip, "iTunesArtwork", icon_png, icon_png_sz, 0100644);
    }
    if (icon_png) free(icon_png);

    zip_finish(&zip);

    FILE *out = fopen(ipa_path, "wb");
    if (!out) {
        fprintf(stderr, "error: failed to open '%s' for writing\n", ipa_path);
        zip_destroy(&zip);
        return -1;
    }
    size_t written = fwrite(zip.out.data, 1, zip.out.len, out);
    fclose(out);

    if (written != zip.out.len) {
        fprintf(stderr, "error: failed to write complete IPA file to '%s'\n", ipa_path);
        remove(ipa_path);
        zip_destroy(&zip);
        return -1;
    }

    zip_destroy(&zip);
    return 0;
}
