
#include "package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP "\\"
#define popen _popen
#define pclose _pclose
#define strdup _strdup
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#define PATH_SEP "/"
#endif

#include "../common/host_oom.h"
#include "../common/json.h"

bool zan_version_parse(const char *str, zan_version_t *out) {
    memset(out, 0, sizeof(*out));
    if (!str || !*str) return false;
    int consumed = 0;
    int n = sscanf(str, "%d.%d.%d%n", &out->major, &out->minor, &out->patch, &consumed);
    if (n < 3) {
        n = sscanf(str, "%d.%d%n", &out->major, &out->minor, &consumed);
        if (n < 2) return false;
        out->patch = 0;
    }
    if (out->major < 0 || out->minor < 0 || out->patch < 0) return false;
    if (str[consumed] == '-') {
        const char *pre = str + consumed + 1;
        size_t plen = strlen(pre);
        if (plen >= sizeof(out->prerelease)) plen = sizeof(out->prerelease) - 1;
        memcpy(out->prerelease, pre, plen);
        out->prerelease[plen] = 0;
    }
    return true;
}

static int version_cmp_int(int x, int y) {
    return (x > y) - (x < y);
}

int zan_version_compare(const zan_version_t *a, const zan_version_t *b) {
    int c = version_cmp_int(a->major, b->major);
    if (c) return c;
    c = version_cmp_int(a->minor, b->minor);
    if (c) return c;
    c = version_cmp_int(a->patch, b->patch);
    if (c) return c;
    bool a_pre = (a->prerelease[0] != 0);
    bool b_pre = (b->prerelease[0] != 0);
    if (!a_pre && b_pre) return 1;
    if (a_pre && !b_pre) return -1;
    if (a_pre && b_pre) return strcmp(a->prerelease, b->prerelease);
    return 0;
}

char *zan_version_format(const zan_version_t *v, char *buf, int buf_size) {
    if (v->prerelease[0]) {
        snprintf(buf, (size_t)buf_size, "%d.%d.%d-%s", v->major, v->minor, v->patch, v->prerelease);
    } else {
        snprintf(buf, (size_t)buf_size, "%d.%d.%d", v->major, v->minor, v->patch);
    }
    return buf;
}

/* --- SHA-256 算法实现 --- */
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ZAN_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ZAN_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define ZAN_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define ZAN_SIGMA0(x) (ZAN_ROTR(x, 2) ^ ZAN_ROTR(x, 13) ^ ZAN_ROTR(x, 22))
#define ZAN_SIGMA1(x) (ZAN_ROTR(x, 6) ^ ZAN_ROTR(x, 11) ^ ZAN_ROTR(x, 25))
#define ZAN_SIG0(x) (ZAN_ROTR(x, 7) ^ ZAN_ROTR(x, 18) ^ ((x) >> 3))
#define ZAN_SIG1(x) (ZAN_ROTR(x, 17) ^ ZAN_ROTR(x, 19) ^ ((x) >> 10))

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} zan_sha256_ctx_t;

static void zan_sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = ZAN_SIG1(w[i - 2]) + w[i - 7] + ZAN_SIG0(w[i - 15]) + w[i - 16];
    }
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + ZAN_SIGMA1(e) + ZAN_CH(e, f, g) + K256[i] + w[i];
        uint32_t t2 = ZAN_SIGMA0(a) + ZAN_MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void zan_sha256_init(zan_sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void zan_sha256_update(zan_sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    size_t idx = (size_t)(ctx->count & 63);
    ctx->count += len;
    while (len > 0) {
        size_t take = 64 - idx;
        if (take > len) take = len;
        memcpy(ctx->buffer + idx, data, take);
        idx += take;
        data += take;
        len -= take;
        if (idx == 64) {
            zan_sha256_transform(ctx->state, ctx->buffer);
            idx = 0;
        }
    }
}

static void zan_sha256_final(zan_sha256_ctx_t *ctx, uint8_t digest[32]) {
    size_t idx = (size_t)(ctx->count & 63);
    ctx->buffer[idx++] = 0x80;
    if (idx > 56) {
        memset(ctx->buffer + idx, 0, 64 - idx);
        zan_sha256_transform(ctx->state, ctx->buffer);
        idx = 0;
    }
    memset(ctx->buffer + idx, 0, 56 - idx);
    uint64_t bits = ctx->count * 8;
    for (int i = 7; i >= 0; i--) {
        ctx->buffer[56 + (7 - i)] = (uint8_t)((bits >> (i * 8)) & 0xff);
    }
    zan_sha256_transform(ctx->state, ctx->buffer);
    for (int i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)((ctx->state[i] >> 24) & 0xff);
        digest[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xff);
        digest[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xff);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xff);
    }
}

bool zan_pkg_compute_checksum(const char *file_path, char out_hex[65]) {
    if (!file_path || !out_hex) return false;
    FILE *f = fopen(file_path, "rb");
    if (!f) return false;
    zan_sha256_ctx_t ctx;
    zan_sha256_init(&ctx);
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        zan_sha256_update(&ctx, buf, n);
    }
    fclose(f);
    uint8_t digest[32];
    zan_sha256_final(&ctx, digest);
    for (int i = 0; i < 32; i++) {
        sprintf(out_hex + (i * 2), "%02x", digest[i]);
    }
    out_hex[64] = 0;
    return true;
}

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static void zan_dep_parse_constraint(const char *str, zan_dependency_t *dep) {
    if (!dep) return;
    if (!str || !*str) {
        dep->kind = ZAN_DEP_ANY;
        return;
    }
    const char *p = str;
    skip_ws(&p);
    if (!*p || *p == '*' || *p == 'x' || *p == 'X') {
        dep->kind = ZAN_DEP_ANY;
        return;
    }
    if (*p == '^') {
        dep->kind = ZAN_DEP_COMPAT;
        p++;
        skip_ws(&p);
        zan_version_parse(p, &dep->min_ver);
    } else if (*p == '~') {
        dep->kind = ZAN_DEP_TILDE;
        p++;
        skip_ws(&p);
        zan_version_parse(p, &dep->min_ver);
    } else if (*p == '>' && *(p + 1) == '=') {
        dep->kind = ZAN_DEP_MINIMUM;
        p += 2;
        skip_ws(&p);
        char ver1[64] = {0};
        int vi = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ',' && vi < 63) {
            ver1[vi++] = *p++;
        }
        ver1[vi] = 0;
        zan_version_parse(ver1, &dep->min_ver);
        skip_ws(&p);
        if (*p == ',') { p++; skip_ws(&p); }
        if (*p == '<') {
            dep->kind = ZAN_DEP_RANGE;
            if (*(p + 1) == '=') p += 2;
            else p++;
            skip_ws(&p);
            zan_version_parse(p, &dep->max_ver);
        }
    } else if (*p == '<' && *(p + 1) == '=') {
        dep->kind = ZAN_DEP_MAXIMUM;
        p += 2;
        skip_ws(&p);
        zan_version_parse(p, &dep->min_ver);
    } else if (*p == '=') {
        dep->kind = ZAN_DEP_EXACT;
        p++;
        skip_ws(&p);
        zan_version_parse(p, &dep->min_ver);
    } else {
        dep->kind = ZAN_DEP_COMPAT;
        zan_version_parse(p, &dep->min_ver);
    }
}

static bool read_qstr(const char **p, char *out, int max_len) {
    skip_ws(p);
    if (**p != '"') return false;
    (*p)++;
    int i = 0;
    while (**p && **p != '"' && i < max_len - 1) { out[i++] = **p; (*p)++; }
    out[i] = 0;
    if (**p == '"') (*p)++;
    return true;
}

static bool path_ends_with_json(const char *path) {
    if (!path) return false;
    size_t len = strlen(path);
    if (len >= 5 && (strcmp(path + len - 5, ".json") == 0 || strcmp(path + len - 5, ".JSON") == 0)) {
        return true;
    }
    return false;
}

bool zan_pkg_load(zan_package_t *pkg, const char *manifest_path) {
    FILE *f = fopen(manifest_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 10 * 1024 * 1024) { fclose(f); return false; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t nr = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[nr] = 0;

    memset(pkg, 0, sizeof(*pkg));
    pkg->dep_cap = 16;
    pkg->deps = (zan_dependency_t *)calloc((size_t)pkg->dep_cap, sizeof(zan_dependency_t));

    const char *scan = buf;
    skip_ws(&scan);
    if (*scan == '{') {
        json_value *root = json_parse(buf);
        free(buf);
        if (!root || root->type != JSON_OBJ) {
            if (root) json_free(root);
            return false;
        }

        const char *name_str = json_get_str(json_obj_get(root, "name"));
        if (name_str) strncpy(pkg->name, name_str, sizeof(pkg->name) - 1);

        const char *ver_str = json_get_str(json_obj_get(root, "version"));
        if (ver_str) {
            pkg->has_version = zan_version_parse(ver_str, &pkg->version);
        }

        const char *desc_str = json_get_str(json_obj_get(root, "description"));
        if (desc_str) strncpy(pkg->description, desc_str, sizeof(pkg->description) - 1);

        const char *author_str = json_get_str(json_obj_get(root, "author"));
        if (author_str) strncpy(pkg->author, author_str, sizeof(pkg->author) - 1);

        const char *lic_str = json_get_str(json_obj_get(root, "license"));
        if (lic_str) strncpy(pkg->license, lic_str, sizeof(pkg->license) - 1);

        const char *entry_str = json_get_str(json_obj_get(root, "entry"));
        if (!entry_str) entry_str = json_get_str(json_obj_get(root, "entry_point"));
        if (entry_str) strncpy(pkg->entry_point, entry_str, sizeof(pkg->entry_point) - 1);

        const char *plugin_str = json_get_str(json_obj_get(root, "plugin_id"));
        if (plugin_str) strncpy(pkg->plugin_id, plugin_str, sizeof(pkg->plugin_id) - 1);

        json_value *deps = json_obj_get(root, "dependencies");
        if (!deps) deps = json_obj_get(root, "deps");
        if (deps && deps->type == JSON_OBJ) {
            for (int i = 0; i < deps->as.obj.count; i++) {
                const char *dep_name = deps->as.obj.keys[i];
                json_value *dep_val = deps->as.obj.vals[i];
                const char *dep_source = "";
                const char *dep_constraint = "*";

                if (dep_val && dep_val->type == JSON_STR) {
                    dep_constraint = dep_val->as.str;
                } else if (dep_val && dep_val->type == JSON_OBJ) {
                    const char *src = json_get_str(json_obj_get(dep_val, "source"));
                    if (!src) src = json_get_str(json_obj_get(dep_val, "git"));
                    if (src) dep_source = src;

                    const char *c = json_get_str(json_obj_get(dep_val, "version"));
                    if (c) dep_constraint = c;
                }
                zan_pkg_add_dep(pkg, dep_name, dep_source, dep_constraint);
            }
        }

        json_free(root);
        return true;
    }

    /* INI / zan.pkg 格式解析 */
    const char *p = buf;
    bool in_deps = false;
    while (*p) {
        char line[1024];
        int li = 0;
        while (*p && *p != '\n' && *p != '\r' && li < (int)sizeof(line) - 1) {
            line[li++] = *p++;
        }
        line[li] = 0;
        while (*p == '\n' || *p == '\r') p++;

        const char *lp = line;
        skip_ws(&lp);
        if (*lp == '#' || *lp == 0) continue;
        if (*lp == '[') {
            in_deps = (strncmp(lp, "[deps]", 6) == 0 || strncmp(lp, "[dependencies]", 14) == 0);
            continue;
        }
        char key[128] = {0};
        int ki = 0;
        while (*lp && *lp != '=' && !isspace((unsigned char)*lp) && ki < 127) { key[ki++] = *lp; lp++; }
        key[ki] = 0;
        skip_ws(&lp);
        if (*lp == '=') lp++;
        skip_ws(&lp);

        if (!in_deps) {
            char val[256] = {0};
            if (*lp == '"') { read_qstr(&lp, val, sizeof(val)); }
            else { int vi = 0; while (*lp && *lp != '\n' && *lp != '\r' && vi < 255) { val[vi++] = *lp; lp++; } val[vi] = 0; }
            if (strcmp(key, "name") == 0) strncpy(pkg->name, val, sizeof(pkg->name) - 1);
            else if (strcmp(key, "version") == 0) {
                pkg->has_version = zan_version_parse(val, &pkg->version);
            }
            else if (strcmp(key, "description") == 0) strncpy(pkg->description, val, sizeof(pkg->description) - 1);
            else if (strcmp(key, "author") == 0) strncpy(pkg->author, val, sizeof(pkg->author) - 1);
            else if (strcmp(key, "license") == 0) strncpy(pkg->license, val, sizeof(pkg->license) - 1);
            else if (strcmp(key, "entry") == 0) strncpy(pkg->entry_point, val, sizeof(pkg->entry_point) - 1);
            else if (strcmp(key, "plugin_id") == 0) strncpy(pkg->plugin_id, val, sizeof(pkg->plugin_id) - 1);
        } else {
            if (pkg->dep_count >= pkg->dep_cap) {
                int ncap = pkg->dep_cap * 2;
                zan_dependency_t *grown = (zan_dependency_t *)realloc(pkg->deps, sizeof(zan_dependency_t) * (size_t)ncap);
                if (!grown) {
                    zan_host_oom();
                }
                pkg->deps = grown;
                pkg->dep_cap = ncap;
            }
            zan_dependency_t *dep = &pkg->deps[pkg->dep_count];
            memset(dep, 0, sizeof(*dep));
            strncpy(dep->name, key, sizeof(dep->name) - 1);
            if (*lp == '{') {
                lp++;
                while (*lp && *lp != '}') {
                    skip_ws(&lp);
                    char dkey[64] = {0}; int di = 0;
                    while (*lp && *lp != '=' && !isspace((unsigned char)*lp) && *lp != '}' && di < 63) { dkey[di++] = *lp; lp++; }
                    dkey[di] = 0; skip_ws(&lp);
                    if (*lp == '=') lp++; skip_ws(&lp);
                    char dval[512] = {0};
                    if (*lp == '"') { read_qstr(&lp, dval, sizeof(dval)); }
                    skip_ws(&lp); if (*lp == ',') lp++;
                    if (strcmp(dkey, "source") == 0 || strcmp(dkey, "git") == 0) strncpy(dep->source, dval, sizeof(dep->source) - 1);
                    else if (strcmp(dkey, "version") == 0) {
                        zan_dep_parse_constraint(dval, dep);
                    }
                }
                if (*lp == '}') lp++;
            }
            pkg->dep_count++;
        }
    }
    free(buf);
    return true;
}

bool zan_pkg_save(const zan_package_t *pkg, const char *manifest_path) {
    FILE *f = fopen(manifest_path, "w");
    if (!f) return false;
    char ver_buf[64];
    zan_version_format(&pkg->version, ver_buf, sizeof(ver_buf));

    if (path_ends_with_json(manifest_path)) {
        fprintf(f, "{\n");
        fprintf(f, "  \"name\": \"%s\",\n", pkg->name);
        fprintf(f, "  \"version\": \"%s\"", ver_buf);
        if (pkg->description[0]) fprintf(f, ",\n  \"description\": \"%s\"", pkg->description);
        if (pkg->author[0]) fprintf(f, ",\n  \"author\": \"%s\"", pkg->author);
        if (pkg->license[0]) fprintf(f, ",\n  \"license\": \"%s\"", pkg->license);
        if (pkg->entry_point[0]) fprintf(f, ",\n  \"entry\": \"%s\"", pkg->entry_point);
        if (pkg->plugin_id[0]) fprintf(f, ",\n  \"plugin_id\": \"%s\"", pkg->plugin_id);
        if (pkg->dep_count > 0) {
            fprintf(f, ",\n  \"dependencies\": {\n");
            for (int i = 0; i < pkg->dep_count; i++) {
                const zan_dependency_t *d = &pkg->deps[i];
                char dep_ver[64]; zan_version_format(&d->min_ver, dep_ver, sizeof(dep_ver));
                const char *prefix = "";
                switch (d->kind) {
                case ZAN_DEP_COMPAT: prefix = "^"; break;
                case ZAN_DEP_TILDE: prefix = "~"; break;
                case ZAN_DEP_MINIMUM: prefix = ">="; break;
                case ZAN_DEP_MAXIMUM: prefix = "<="; break;
                case ZAN_DEP_EXACT: prefix = "="; break;
                case ZAN_DEP_RANGE: prefix = ">="; break;
                case ZAN_DEP_ANY: prefix = "*"; break;
                }
                if (d->source[0]) {
                    fprintf(f, "    \"%s\": { \"source\": \"%s\", \"version\": \"%s%s\" }%s\n",
                            d->name, d->source, prefix, dep_ver, (i + 1 < pkg->dep_count) ? "," : "");
                } else {
                    fprintf(f, "    \"%s\": \"%s%s\"%s\n",
                            d->name, prefix, dep_ver, (i + 1 < pkg->dep_count) ? "," : "");
                }
            }
            fprintf(f, "  }\n");
        } else {
            fprintf(f, "\n");
        }
        fprintf(f, "}\n");
    } else {
        fprintf(f, "# Zan Package Manifest\n");
        fprintf(f, "name = \"%s\"\n", pkg->name);
        fprintf(f, "version = \"%s\"\n", ver_buf);
        if (pkg->description[0]) fprintf(f, "description = \"%s\"\n", pkg->description);
        if (pkg->author[0]) fprintf(f, "author = \"%s\"\n", pkg->author);
        if (pkg->license[0]) fprintf(f, "license = \"%s\"\n", pkg->license);
        if (pkg->entry_point[0]) fprintf(f, "entry = \"%s\"\n", pkg->entry_point);
        if (pkg->plugin_id[0]) fprintf(f, "plugin_id = \"%s\"\n", pkg->plugin_id);
        if (pkg->dep_count > 0) {
            fprintf(f, "\n[deps]\n");
            for (int i = 0; i < pkg->dep_count; i++) {
                const zan_dependency_t *d = &pkg->deps[i];
                char dep_ver[64]; zan_version_format(&d->min_ver, dep_ver, sizeof(dep_ver));
                const char *prefix = "";
                switch (d->kind) {
                case ZAN_DEP_COMPAT: prefix = "^"; break;
                case ZAN_DEP_TILDE: prefix = "~"; break;
                case ZAN_DEP_MINIMUM: prefix = ">="; break;
                case ZAN_DEP_MAXIMUM: prefix = "<="; break;
                case ZAN_DEP_EXACT: prefix = "="; break;
                case ZAN_DEP_RANGE: prefix = ">="; break;
                case ZAN_DEP_ANY: prefix = "*"; break;
                }
                fprintf(f, "%s = { source = \"%s\", version = \"%s%s\" }\n", d->name, d->source, prefix, dep_ver);
            }
        }
    }
    bool ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    return ok;
}

void zan_pkg_new(zan_package_t *pkg, const char *name, const char *version) {
    memset(pkg, 0, sizeof(*pkg));
    strncpy(pkg->name, name, sizeof(pkg->name) - 1);
    zan_version_parse(version, &pkg->version);
    pkg->dep_cap = 16;
    pkg->deps = (zan_dependency_t *)calloc((size_t)pkg->dep_cap, sizeof(zan_dependency_t));
}

void zan_pkg_add_dep(zan_package_t *pkg, const char *name, const char *source, const char *version_constraint) {
    if (pkg->dep_count >= pkg->dep_cap) {
        int ncap = pkg->dep_cap * 2;
        zan_dependency_t *grown = (zan_dependency_t *)realloc(pkg->deps, sizeof(zan_dependency_t) * (size_t)ncap);
        if (!grown) {
            zan_host_oom();
        }
        pkg->deps = grown;
        pkg->dep_cap = ncap;
    }
    zan_dependency_t *dep = &pkg->deps[pkg->dep_count++];
    memset(dep, 0, sizeof(*dep));
    strncpy(dep->name, name, sizeof(dep->name) - 1);
    strncpy(dep->source, source, sizeof(dep->source) - 1);
    zan_dep_parse_constraint(version_constraint, dep);
}

bool zan_pkg_remove_dep(zan_package_t *pkg, const char *name) {
    for (int i = 0; i < pkg->dep_count; i++) {
        if (strcmp(pkg->deps[i].name, name) == 0) {
            memmove(&pkg->deps[i], &pkg->deps[i + 1], sizeof(zan_dependency_t) * (size_t)(pkg->dep_count - i - 1));
            pkg->dep_count--;
            return true;
        }
    }
    return false;
}

static bool pkg_is_dir(const char *path);

static void ensure_dir_pkg(const char *path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

void zan_pkg_init(zan_pkg_registry_t *reg, const char *project_dir) {
    memset(reg, 0, sizeof(*reg));
    size_t dlen = strlen(project_dir);
    reg->cache_dir = (char *)malloc(dlen + 32);
    snprintf(reg->cache_dir, dlen + 32, "%s" PATH_SEP ".zan-packages", project_dir);
    ensure_dir_pkg(reg->cache_dir);
    reg->lock_file = (char *)malloc(dlen + 16);
    snprintf(reg->lock_file, dlen + 16, "%s" PATH_SEP "zan.lock", project_dir);
    reg->resolved = (zan_package_t **)calloc(256, sizeof(zan_package_t *));
    reg->resolved_count = 0;
}

bool zan_pkg_version_satisfies(const zan_dependency_t *dep, const zan_version_t *ver) {
    if (!dep || !ver) return false;
    switch (dep->kind) {
    case ZAN_DEP_ANY:
        return true;
    case ZAN_DEP_EXACT:
        return zan_version_compare(ver, &dep->min_ver) == 0;
    case ZAN_DEP_MINIMUM:
        return zan_version_compare(ver, &dep->min_ver) >= 0;
    case ZAN_DEP_MAXIMUM:
        return zan_version_compare(ver, &dep->min_ver) <= 0;
    case ZAN_DEP_COMPAT: {
        if (zan_version_compare(ver, &dep->min_ver) < 0) return false;
        if (dep->min_ver.major > 0) return ver->major == dep->min_ver.major;
        else if (dep->min_ver.minor > 0) return ver->major == 0 && ver->minor == dep->min_ver.minor;
        return zan_version_compare(ver, &dep->min_ver) == 0;
    }
    case ZAN_DEP_TILDE: {
        if (zan_version_compare(ver, &dep->min_ver) < 0) return false;
        return ver->major == dep->min_ver.major && ver->minor == dep->min_ver.minor;
    }
    case ZAN_DEP_RANGE:
        return zan_version_compare(ver, &dep->min_ver) >= 0 && zan_version_compare(ver, &dep->max_ver) < 0;
    }
    return false;
}

static bool pkg_token_is_shell_safe(const char *s) {
    if (!s) return true;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) continue;
        if (c == '%') {
            if (isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                p += 2;
                continue;
            }
            return false;
        }
        if (strchr(":/@._~+-", c) != NULL) continue;
        return false;
    }
    return true;
}

static bool pkg_safe_component(const char *s);

bool zan_pkg_fetch(zan_pkg_registry_t *reg, const zan_dependency_t *dep) {
    char pkg_dir[1024];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s" PATH_SEP "%s", reg->cache_dir, dep->name);
    if (pkg_is_dir(pkg_dir)) return true;
    if (!pkg_safe_component(dep->name)) {
        fprintf(stderr,
                "error: refusing to fetch '%s': package name must be a single "
                "path component (letters, digits, '.', '_', '-'; no '..', "
                "'/', '\\\\')\n",
                dep->name);
        return false;
    }
    if (dep->source[0] && (strncmp(dep->source, "http", 4) == 0 ||
                           strncmp(dep->source, "git@", 4) == 0)) {
        char cmd[2048]; char ver_buf[64];
        zan_version_format(&dep->min_ver, ver_buf, sizeof(ver_buf));
        if (!pkg_token_is_shell_safe(dep->name) ||
            !pkg_token_is_shell_safe(dep->source) ||
            !pkg_token_is_shell_safe(ver_buf)) {
            fprintf(stderr, "error: refusing to fetch '%s': unsafe characters in package, source or version\n", dep->name);
            return false;
        }
        snprintf(cmd, sizeof(cmd), "git clone --depth 1 --branch v%s \"%s\" \"%s\" 2>&1", ver_buf, dep->source, pkg_dir);
        int ret = system(cmd);
        if (ret != 0) {
            snprintf(cmd, sizeof(cmd), "git clone --depth 1 \"%s\" \"%s\" 2>&1", dep->source, pkg_dir);
            ret = system(cmd);
        }
        return ret == 0;
    }
    return false;
}

static bool zan_pkg_resolve_seen(zan_pkg_registry_t *reg, zan_package_t *root,
                                 char (*seen)[128], int *seen_count) {
    bool all_ok = true;
    for (int i = 0; i < root->dep_count; i++) {
        zan_dependency_t *dep = &root->deps[i];
        fprintf(stderr, "Resolving: %s\n", dep->name);
        if (!zan_pkg_fetch(reg, dep)) {
            fprintf(stderr, "error: failed to fetch package '%s' from %s\n", dep->name, dep->source);
            all_ok = false;
            continue;
        }
        char manifest_path[1024];
        snprintf(manifest_path, sizeof(manifest_path), "%s" PATH_SEP "%s" PATH_SEP "zan.pkg", reg->cache_dir, dep->name);
        zan_package_t fetched_pkg;
        if (zan_pkg_load(&fetched_pkg, manifest_path)) {
            char computed_hash[65] = {0};
            bool has_hash = zan_pkg_compute_checksum(manifest_path, computed_hash);
            if (!zan_pkg_version_satisfies(dep, &fetched_pkg.version)) {
                char ver_buf[64]; zan_version_format(&fetched_pkg.version, ver_buf, sizeof(ver_buf));
                fprintf(stderr, "warning: package '%s' version %s may not satisfy constraint\n", dep->name, ver_buf);
            }
            if (reg->resolved && reg->resolved_count < 256) {
                bool already_resolved = false;
                for (int r = 0; r < reg->resolved_count; r++) {
                    if (reg->resolved[r] && strcmp(reg->resolved[r]->name, fetched_pkg.name) == 0) {
                        already_resolved = true;
                        if (reg->resolved[r]->checksum[0] && has_hash) {
                            if (strcmp(reg->resolved[r]->checksum, computed_hash) != 0) {
                                fprintf(stderr, "error: checksum mismatch for package '%s': expected %s, got %s\n",
                                        fetched_pkg.name, reg->resolved[r]->checksum, computed_hash);
                                zan_pkg_destroy(&fetched_pkg);
                                return false;
                            }
                        } else if (has_hash) {
                            strncpy(reg->resolved[r]->checksum, computed_hash, sizeof(reg->resolved[r]->checksum) - 1);
                        }
                        break;
                    }
                }
                if (!already_resolved) {
                    zan_package_t *rp = (zan_package_t *)calloc(1, sizeof(zan_package_t));
                    strncpy(rp->name, fetched_pkg.name, sizeof(rp->name) - 1);
                    if (dep->source[0]) strncpy(rp->source, dep->source, sizeof(rp->source) - 1);
                    rp->version = fetched_pkg.version;
                    rp->has_version = true;
                    if (has_hash) {
                        strncpy(rp->checksum, computed_hash, sizeof(rp->checksum) - 1);
                    }
                    reg->resolved[reg->resolved_count++] = rp;
                }
            }
            if (fetched_pkg.dep_count > 0) {
                bool seen_before = false;
                for (int s = 0; s < *seen_count; s++) {
                    if (strcmp(seen[s], fetched_pkg.name) == 0) { seen_before = true; break; }
                }
                if (seen_before) {
                    fprintf(stderr, "note: '%s' is already resolved (shared dependency "
                                    "or dependency cycle); skipping its subtree\n",
                            fetched_pkg.name);
                } else if (*seen_count < 256) {
                    snprintf(seen[*seen_count], 128, "%s", fetched_pkg.name);
                    (*seen_count)++;
                    if (!zan_pkg_resolve_seen(reg, &fetched_pkg, seen, seen_count))
                        all_ok = false;
                } else {
                    fprintf(stderr, "error: dependency chain deeper than 256 packages\n");
                    all_ok = false;
                }
            }
            zan_pkg_destroy(&fetched_pkg);
        } else {
            fprintf(stderr, "error: failed to read manifest for package '%s'\n", dep->name);
            all_ok = false;
        }
    }
    return all_ok;
}

bool zan_pkg_resolve(zan_pkg_registry_t *reg, zan_package_t *root) {
    char seen[256][128];
    int seen_count = 0;
    if (root->name[0]) {
        snprintf(seen[seen_count++], 128, "%s", root->name);
    }
    return zan_pkg_resolve_seen(reg, root, seen, &seen_count);
}

char **zan_pkg_get_sources(zan_pkg_registry_t *reg, int *out_count) {
    (void)reg;
    *out_count = 0;
    return NULL;
}

bool zan_pkg_write_lock(zan_pkg_registry_t *reg) {
    if (!reg || !reg->lock_file) return false;
    FILE *f = fopen(reg->lock_file, "w");
    if (!f) return false;
    fprintf(f, "# Zan lock file - auto-generated\n\n");
    for (int i = 0; i < reg->resolved_count; i++) {
        zan_package_t *pkg = reg->resolved[i];
        if (!pkg) continue;
        char ver_buf[64]; zan_version_format(&pkg->version, ver_buf, sizeof(ver_buf));
        fprintf(f, "[[package]]\nname = \"%s\"\nversion = \"%s\"\n", pkg->name, ver_buf);
        if (pkg->source[0]) {
            fprintf(f, "source = \"%s\"\n", pkg->source);
        }
        if (pkg->checksum[0]) {
            fprintf(f, "checksum = \"%s\"\n", pkg->checksum);
        }
        fprintf(f, "\n");
    }
    bool ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    return ok;
}

static bool add_locked_pkg(zan_pkg_registry_t *reg, const char *name, const char *ver_str,
                           const char *source, const char *checksum) {
    if (!name || !name[0] || !ver_str || !ver_str[0]) return false;
    if (!pkg_safe_component(name)) {
        fprintf(stderr, "error: unsafe package name '%s' in lock file\n", name);
        return false;
    }
    zan_version_t v;
    if (!zan_version_parse(ver_str, &v)) {
        fprintf(stderr, "error: invalid version '%s' for package '%s' in lock file\n", ver_str, name);
        return false;
    }
    if (!reg->resolved) {
        reg->resolved = (zan_package_t **)calloc(256, sizeof(zan_package_t *));
        reg->resolved_count = 0;
    }
    for (int i = 0; i < reg->resolved_count; i++) {
        if (reg->resolved[i] && strcmp(reg->resolved[i]->name, name) == 0) {
            if (source && source[0]) strncpy(reg->resolved[i]->source, source, sizeof(reg->resolved[i]->source) - 1);
            if (checksum && checksum[0]) strncpy(reg->resolved[i]->checksum, checksum, sizeof(reg->resolved[i]->checksum) - 1);
            return zan_version_compare(&reg->resolved[i]->version, &v) == 0;
        }
    }
    if (reg->resolved_count >= 256) {
        fprintf(stderr, "error: lock file contains too many packages (>256)\n");
        return false;
    }
    zan_package_t *pkg = (zan_package_t *)calloc(1, sizeof(zan_package_t));
    strncpy(pkg->name, name, sizeof(pkg->name) - 1);
    pkg->version = v;
    pkg->has_version = true;
    if (source && source[0]) strncpy(pkg->source, source, sizeof(pkg->source) - 1);
    if (checksum && checksum[0]) strncpy(pkg->checksum, checksum, sizeof(pkg->checksum) - 1);
    reg->resolved[reg->resolved_count++] = pkg;
    return true;
}

bool zan_pkg_read_lock(zan_pkg_registry_t *reg) {
    if (!reg || !reg->lock_file) return false;
    FILE *f = fopen(reg->lock_file, "r");
    if (!f) return false;

    char line[1024];
    char cur_name[128] = {0};
    char cur_ver[64] = {0};
    char cur_source[512] = {0};
    char cur_checksum[65] = {0};
    bool in_pkg = false;
    bool ok = true;

    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        skip_ws(&p);
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        if (*p == '[') {
            if (strncmp(p, "[[package]]", 11) == 0) {
                if (in_pkg) {
                    if (!cur_name[0] || !cur_ver[0] || !add_locked_pkg(reg, cur_name, cur_ver, cur_source, cur_checksum)) {
                        ok = false;
                        break;
                    }
                }
                in_pkg = true;
                cur_name[0] = 0;
                cur_ver[0] = 0;
                cur_source[0] = 0;
                cur_checksum[0] = 0;
            }
            continue;
        }
        if (!in_pkg) continue;
        char key[128] = {0};
        int ki = 0;
        while (*p && *p != '=' && !isspace((unsigned char)*p) && ki < 127) { key[ki++] = *p; p++; }
        key[ki] = 0;
        skip_ws(&p);
        if (*p == '=') p++;
        skip_ws(&p);
        char val[512] = {0};
        if (*p == '"') { read_qstr(&p, val, sizeof(val)); }
        else { int vi = 0; while (*p && *p != '\n' && *p != '\r' && vi < 511) { val[vi++] = *p; p++; } val[vi] = 0; }

        if (strcmp(key, "name") == 0) {
            strncpy(cur_name, val, sizeof(cur_name) - 1);
        } else if (strcmp(key, "version") == 0) {
            strncpy(cur_ver, val, sizeof(cur_ver) - 1);
        } else if (strcmp(key, "source") == 0) {
            strncpy(cur_source, val, sizeof(cur_source) - 1);
        } else if (strcmp(key, "checksum") == 0) {
            strncpy(cur_checksum, val, sizeof(cur_checksum) - 1);
        }
    }
    if (ok && in_pkg) {
        if (!cur_name[0] || !cur_ver[0] || !add_locked_pkg(reg, cur_name, cur_ver, cur_source, cur_checksum)) {
            ok = false;
        }
    }
    fclose(f);
    return ok;
}

static bool pkg_safe_component(const char *s) {
    if (!s || !*s || strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!isalnum(c) && c != '_' && c != '-' && c != '.') return false;
    }
    return true;
}

static bool pkg_safe_namespace_path(const char *s) {
    if (!s || !*s || s[0] == '/' || s[0] == '\\') return false;
    char part[128]; size_t n = 0;
    for (;;) {
        char c = *s++;
        if (c == '/' || c == '\\' || c == 0) {
            if (n == 0 || n >= sizeof(part)) return false;
            part[n] = 0;
            if (!pkg_safe_component(part)) return false;
            n = 0;
            if (!c) return true;
        } else if (n + 1 < sizeof(part)) {
            part[n++] = c;
        } else return false;
    }
}

static bool pkg_is_dir(const char *path) {
#ifdef _WIN32
    char norm[1024];
    snprintf(norm, sizeof(norm), "%s", path);
    for (char *p = norm; *p; p++)
        if (*p == '/') *p = '\\';
    DWORD a = GetFileAttributesA(norm);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool zan_pkg_global_store(char *out, size_t out_size) {
    const char *base;
#ifdef _WIN32
    base = getenv("LOCALAPPDATA");
    if (!base || !*base) return false;
    return snprintf(out, out_size, "%s\\Zan\\packages", base) > 0 &&
           strlen(out) < out_size;
#else
    base = getenv("XDG_DATA_HOME");
    if (base && *base)
        return snprintf(out, out_size, "%s/zan/packages", base) > 0 &&
               strlen(out) < out_size;
    base = getenv("HOME");
    if (!base || !*base) return false;
    return snprintf(out, out_size, "%s/.local/share/zan/packages", base) > 0 &&
           strlen(out) < out_size;
#endif
}

static bool pkg_dir_in_list(const char (*out_dirs)[1024], int count, const char *dir) {
    for (int i = 0; i < count; i++) {
        if (strcmp(out_dirs[i], dir) == 0) return true;
    }
    return false;
}


static int pkg_names_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char **pkg_dir_names_sorted(const char *dir, int *out_count) {
    *out_count = 0;
    int cap = 64, count = 0;
    char **names = (char **)malloc((size_t)cap * sizeof(char *));
    if (!names) return NULL;
#ifdef _WIN32
    char pattern[1024]; WIN32_FIND_DATAA fd;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", dir) < (int)sizeof(pattern)) {
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 ||
                    strcmp(fd.cFileName, "..") == 0) continue;
                if (count == cap) {
                    int ncap = cap * 2;
                    char **grown = (char **)realloc(names,
                        (size_t)ncap * sizeof(char *));
                    if (!grown) break;
                    names = grown;
                    cap = ncap;
                }
                size_t len = strlen(fd.cFileName);
                names[count] = (char *)malloc(len + 1);
                if (!names[count]) break;
                memcpy(names[count], fd.cFileName, len + 1);
                count++;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 ||
                strcmp(e->d_name, "..") == 0) continue;
            if (count == cap) {
                int ncap = cap * 2;
                char **grown = (char **)realloc(names,
                    (size_t)ncap * sizeof(char *));
                if (!grown) break;
                names = grown;
                cap = ncap;
            }
            size_t len = strlen(e->d_name);
            names[count] = (char *)malloc(len + 1);
            if (!names[count]) break;
            memcpy(names[count], e->d_name, len + 1);
            count++;
        }
        closedir(d);
    }
#endif
    if (count > 1) qsort(names, (size_t)count, sizeof(char *), pkg_names_cmp);
    *out_count = count;
    return names;
}

static void pkg_dir_names_free(char **names, int count) {
    if (!names) return;
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
}

static int pkg_scan_store(const char *store, const char *namespace_path,
                          char (*out_dirs)[1024], int count, int max_dirs) {
    if (!pkg_is_dir(store)) return count;
    int n = 0;
    char **names = pkg_dir_names_sorted(store, &n);
    if (!names) return count;
    for (int i = 0; i < n; i++) {
        const char *name = names[i];
        if (!pkg_safe_component(name)) continue;
        char root[1024], cand[1024];
        if (snprintf(root, sizeof(root), "%s" PATH_SEP "%s", store, name)
            >= (int)sizeof(root)) continue;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(root);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
#else
        struct stat st;
        if (lstat(root, &st) != 0 || !S_ISDIR(st.st_mode) ||
            S_ISLNK(st.st_mode)) continue;
#endif
        /* 1 */
        snprintf(cand, sizeof(cand), "%s" PATH_SEP "src" PATH_SEP "%s",
                 root, namespace_path);
        if (!pkg_is_dir(cand)) {
            snprintf(cand, sizeof(cand), "%s" PATH_SEP "stdlib" PATH_SEP "%s",
                     root, namespace_path);
        }
        if (!pkg_is_dir(cand)) {
            snprintf(cand, sizeof(cand), "%s" PATH_SEP "%s",
                     root, namespace_path);
        }
        if (pkg_is_dir(cand) && count < max_dirs &&
            !pkg_dir_in_list(out_dirs, count, cand)) {
            snprintf(out_dirs[count++], 1024, "%s", cand);
            zan_pkg_note_usage(store, name);
        }
    }
    pkg_dir_names_free(names, n);
    return count;
}


#define PKG_NAMESPACE_BUCKETS 1024

typedef struct pkg_index_package {
    char *store;
    char *name;
    struct pkg_index_package *next;
} pkg_index_package_t;

typedef struct pkg_index_source {
    char *path;
    pkg_index_package_t *package;
    struct pkg_index_source *next;
} pkg_index_source_t;

typedef struct {
    pkg_index_source_t *source;
    int descendant;
} pkg_namespace_source_t;

typedef struct pkg_index_namespace {
    char name[256];
    pkg_namespace_source_t *sources;
    int count, capacity;
    struct pkg_index_namespace *next;
} pkg_index_namespace_t;

struct zan_pkg_source_index {
    pkg_index_namespace_t *namespaces[PKG_NAMESPACE_BUCKETS];
    pkg_index_package_t *packages;
    pkg_index_source_t *sources;
};

static char *pkg_index_copy(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);
    memcpy(copy, s, n);
    return copy;
}

static unsigned pkg_namespace_hash(const char *name, size_t len) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < len; i++)
        h = (h ^ (unsigned char)name[i]) * 16777619u;
    return h & (PKG_NAMESPACE_BUCKETS - 1);
}

static pkg_index_namespace_t *pkg_index_find_namespace(
    const zan_pkg_source_index_t *index, const char *name, size_t len) {
    unsigned bucket = pkg_namespace_hash(name, len);
    for (pkg_index_namespace_t *ns = index->namespaces[bucket]; ns; ns = ns->next)
        if (strlen(ns->name) == len && memcmp(ns->name, name, len) == 0)
            return ns;
    return NULL;
}

static void pkg_index_add_namespace(zan_pkg_source_index_t *index,
                                    const char *name, size_t len,
                                    pkg_index_source_t *source, int descendant) {
    if (!len) return;
    pkg_index_namespace_t *ns = pkg_index_find_namespace(index, name, len);
    if (!ns) {
        unsigned bucket = pkg_namespace_hash(name, len);
        ns = (pkg_index_namespace_t *)calloc(1, sizeof(*ns));
        memcpy(ns->name, name, len);
        ns->name[len] = 0;
        ns->next = index->namespaces[bucket];
        index->namespaces[bucket] = ns;
    }
    if (ns->count == ns->capacity) {
        int cap = ns->capacity ? ns->capacity * 2 : 8;
        ns->sources = (pkg_namespace_source_t *)realloc(
            ns->sources, (size_t)cap * sizeof(*ns->sources));
        ns->capacity = cap;
    }
    ns->sources[ns->count++] = (pkg_namespace_source_t){source, descendant};
}

static void pkg_rel_dir_ns(const char *dir, const char *src_root,
                           char *out, size_t cap) {
    size_t rl = strlen(src_root);
    const char *rel = strlen(dir) >= rl ? dir + rl : dir;
    if (*rel == '/' || *rel == '\\') rel++;
    size_t w = 0;
    for (; *rel && w + 1 < cap; rel++) {
        char c = *rel;
        if (c == '/' || c == '\\') c = '.';
        out[w++] = c;
    }
    out[w] = '\0';
}

static void pkg_index_source_tree(zan_pkg_source_index_t *index,
                                   pkg_index_package_t *package, const char *dir,
                                   zan_pkg_namespace_probe_t probe,
                                   int depth, const char *src_root,
                                   int project_owned) {
    if (depth > 64) return;
    int n = 0;
    char **names = pkg_dir_names_sorted(dir, &n);
    if (!names) return;
    for (int i = 0; i < n; i++) {
        const char *name = names[i];
        if (!pkg_safe_component(name)) continue;
        char path[1024];
        if (snprintf(path, sizeof(path), "%s" PATH_SEP "%s", dir, name)
            >= (int)sizeof(path)) continue;
        int is_dir = 0, is_reg = 0;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(path);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
        is_reg = !is_dir;
#else
        struct stat st;
        if (lstat(path, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        is_dir = S_ISDIR(st.st_mode) != 0;
        is_reg = S_ISREG(st.st_mode) != 0;
#endif
        if (is_dir) {
            pkg_index_source_tree(index, package, path, probe, depth + 1,
                                   src_root, project_owned);
        } else if (is_reg) {
            size_t len = strlen(name);
            if (len < 5 || strcmp(name + len - 4, ".zan") != 0) continue;
            char declared[256] = {0};
            int has_namespace = probe(path, declared, sizeof(declared)) && declared[0];
            if (!has_namespace)
                pkg_rel_dir_ns(dir, src_root, declared, sizeof(declared));
            if (!declared[0]) continue;
            pkg_index_source_t *source = (pkg_index_source_t *)malloc(sizeof(*source));
            source->path = pkg_index_copy(path);
            source->package = package;
            source->next = index->sources;
            index->sources = source;
            pkg_index_add_namespace(index, declared, strlen(declared), source, 0);
            if (has_namespace && project_owned) {
                for (size_t k = 0; declared[k]; k++)
                    if (declared[k] == '.')
                        pkg_index_add_namespace(index, declared, k, source, 1);
            }
        }
    }
    pkg_dir_names_free(names, n);
}

typedef struct {
    char **names;
    int count;
    int capacity;
} pkg_seen_names_t;

static bool pkg_mark_seen(pkg_seen_names_t *seen, const char *name) {
    for (int i = 0; i < seen->count; i++)
        if (strcmp(seen->names[i], name) == 0) return false;
    if (seen->count == seen->capacity) {
        int cap = seen->capacity ? seen->capacity * 2 : 16;
        char **grown = (char **)realloc(seen->names, (size_t)cap * sizeof(*seen->names));
        if (!grown) {
            zan_host_oom();
        }
        seen->names = grown;
        seen->capacity = cap;
    }
    seen->names[seen->count++] = strdup(name);
    return true;
}

static void pkg_index_store(zan_pkg_source_index_t *index, const char *store,
                             zan_pkg_namespace_probe_t probe,
                             pkg_seen_names_t *seen, int project_owned) {
    if (!pkg_is_dir(store)) return;
    int n = 0;
    char **names = pkg_dir_names_sorted(store, &n);
    if (!names) return;
    for (int i = 0; i < n; i++) {
        const char *name = names[i];
        if (!pkg_safe_component(name)) continue;
        char root[1024], source[1024];
        if (snprintf(root, sizeof(root), "%s" PATH_SEP "%s", store, name)
            >= (int)sizeof(root)) continue;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(root);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
#else
        struct stat st;
        if (lstat(root, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
#endif
        if (snprintf(source, sizeof(source), "%s" PATH_SEP "src", root)
            >= (int)sizeof(source)) continue;
        if (!pkg_is_dir(source)) {
            if (snprintf(source, sizeof(source), "%s" PATH_SEP "stdlib", root)
                >= (int)sizeof(source)) continue;
            if (!pkg_is_dir(source)) snprintf(source, sizeof(source), "%s", root);
        }
        if (!pkg_mark_seen(seen, name)) continue;
        pkg_index_package_t *package = (pkg_index_package_t *)malloc(sizeof(*package));
        package->store = pkg_index_copy(store);
        package->name = pkg_index_copy(name);
        package->next = index->packages;
        index->packages = package;
        pkg_index_source_tree(index, package, source, probe, 0, source, project_owned);
    }
    pkg_dir_names_free(names, n);
}

zan_pkg_source_index_t *zan_pkg_source_index_create(
    const char *project_dir, zan_pkg_namespace_probe_t probe) {
    if (!project_dir || !project_dir[0] || !probe) return NULL;
    zan_pkg_source_index_t *index = (zan_pkg_source_index_t *)calloc(1, sizeof(*index));
    char store[1024];
    pkg_seen_names_t seen = {0};
    snprintf(store, sizeof(store), "%s" PATH_SEP "packages", project_dir);
    pkg_index_store(index, store, probe, &seen, 1);
    snprintf(store, sizeof(store), "%s" PATH_SEP ".zan-packages", project_dir);
    pkg_index_store(index, store, probe, &seen, 1);
    char exe_dir[1024] = {0};
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir))) {
        char *sep = strrchr(exe_dir, '\\'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s\\..\\packages", exe_dir);
        pkg_index_store(index, store, probe, &seen, 0);
        snprintf(store, sizeof(store), "%s\\packages", exe_dir);
        pkg_index_store(index, store, probe, &seen, 0);
    }
#elif defined(__APPLE__)
    uint32_t size = sizeof(exe_dir);
    if (_NSGetExecutablePath(exe_dir, &size) == 0) {
        char *sep = strrchr(exe_dir, '/'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        pkg_index_store(index, store, probe, &seen, 0);
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len > 0) {
        exe_dir[len] = 0;
        char *sep = strrchr(exe_dir, '/'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        pkg_index_store(index, store, probe, &seen, 0);
    }
#endif
    if (zan_pkg_global_store(store, sizeof(store)))
        pkg_index_store(index, store, probe, &seen, 0);
    for (int i = 0; i < seen.count; i++) free(seen.names[i]);
    free(seen.names);
    return index;
}

int zan_pkg_source_index_visit(const zan_pkg_source_index_t *index,
                                const char *namespace_path,
                                zan_pkg_source_visitor_t visitor, void *context,
                                int hierarchical) {
    if (!index || !pkg_safe_namespace_path(namespace_path) || !visitor) return 0;
    char target_ns[256];
    size_t n = strlen(namespace_path);
    if (n >= sizeof(target_ns)) return 0;
    for (size_t i = 0; i <= n; i++)
        target_ns[i] = (namespace_path[i] == '/' || namespace_path[i] == '\\') ? '.' : namespace_path[i];
    pkg_index_namespace_t *ns = pkg_index_find_namespace(index, target_ns, n);
    if (!ns) return 0;
    int found = 0;
    pkg_index_package_t *used = NULL;
    for (int i = 0; i < ns->count; i++) {
        const pkg_namespace_source_t *ref = &ns->sources[i];
        if (ref->descendant && !hierarchical) continue;
        pkg_index_package_t *package = ref->source->package;
        if (used && used != package)
            zan_pkg_note_usage(used->store, used->name);
        used = package;
        visitor(ref->source->path, context);
        found++;
    }
    if (used) zan_pkg_note_usage(used->store, used->name);
    return found;
}

void zan_pkg_source_index_destroy(zan_pkg_source_index_t *index) {
    if (!index) return;
    for (int i = 0; i < PKG_NAMESPACE_BUCKETS; i++) {
        pkg_index_namespace_t *ns = index->namespaces[i];
        while (ns) {
            pkg_index_namespace_t *next = ns->next;
            free(ns->sources);
            free(ns);
            ns = next;
        }
    }
    while (index->sources) {
        pkg_index_source_t *source = index->sources;
        index->sources = source->next;
        free(source->path);
        free(source);
    }
    while (index->packages) {
        pkg_index_package_t *package = index->packages;
        index->packages = package->next;
        free(package->store);
        free(package->name);
        free(package);
    }
    free(index);
}

int zan_pkg_visit_namespace(const char *project_dir, const char *namespace_path,
                            zan_pkg_namespace_probe_t probe,
                            zan_pkg_source_visitor_t visitor, void *context,
                            int hierarchical) {
    if (!pkg_safe_namespace_path(namespace_path) || !visitor ||
        strlen(namespace_path) >= 256) return 0;
    zan_pkg_source_index_t *index = zan_pkg_source_index_create(project_dir, probe);
    int found = zan_pkg_source_index_visit(index, namespace_path, visitor, context,
                                           hierarchical);
    zan_pkg_source_index_destroy(index);
    return found;
}

static int pkg_add_package_root(const char *store, const char *name,
                                char (*out_roots)[1024], int count,
                                int max_roots) {
    char root[1024], cand[1024];
    snprintf(root, sizeof(root), "%s/%s", store, name);
    snprintf(cand, sizeof(cand), "%s/src", root);
    if (!pkg_is_dir(cand)) {
        snprintf(cand, sizeof(cand), "%s/stdlib", root);
    }
    if (!pkg_is_dir(cand)) {
        snprintf(cand, sizeof(cand), "%s", root);
    }
    if (!pkg_is_dir(cand) || count >= max_roots ||
        pkg_dir_in_list(out_roots, count, cand))
        return count;
    snprintf(out_roots[count++], 1024, "%s", cand);
    return count;
}

static int pkg_store_source_roots(const char *store,
                                  char (*out_roots)[1024], int count,
                                  int max_roots) {
    if (!pkg_is_dir(store)) return count;
    int n = 0;
    char **names = pkg_dir_names_sorted(store, &n);
    if (!names) return count;
    for (int i = 0; i < n; i++) {
        const char *name = names[i];
        if (!pkg_safe_component(name)) continue;
        char pkg[1024];
        if (snprintf(pkg, sizeof(pkg), "%s" PATH_SEP "%s", store, name)
            >= (int)sizeof(pkg)) continue;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(pkg);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
#else
        struct stat st;
        if (lstat(pkg, &st) != 0 || !S_ISDIR(st.st_mode) ||
            S_ISLNK(st.st_mode)) continue;
#endif
        count = pkg_add_package_root(store, name, out_roots,
                                     count, max_roots);
    }
    pkg_dir_names_free(names, n);
    return count;
}

int zan_pkg_all_source_roots(const char *project_dir,
                             char (*out_roots)[1024], int max_roots) {
    if (!project_dir || !project_dir[0] || !out_roots || max_roots <= 0)
        return 0;
    char store[1024];
    int count = 0;
    if (project_dir && project_dir[0]) {
        snprintf(store, sizeof(store), "%s/packages", project_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
        snprintf(store, sizeof(store), "%s/.zan-packages", project_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    }
    char exe_dir[1024] = {0};
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir))) {
        char *sep = strrchr(exe_dir, '\\'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
        snprintf(store, sizeof(store), "%s/packages", exe_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    }
#elif defined(__APPLE__)
    uint32_t size = sizeof(exe_dir);
    if (_NSGetExecutablePath(exe_dir, &size) == 0) {
        char *sep = strrchr(exe_dir, '/'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len > 0) {
        exe_dir[len] = 0;
        char *sep = strrchr(exe_dir, '/'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    }
#endif
    if (zan_pkg_global_store(store, sizeof(store)))
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    return count;
}

int zan_pkg_find_namespace(const char *project_dir, const char *namespace_path,
                           char (*out_dirs)[1024], int max_dirs) {
    if (!project_dir || !project_dir[0] ||
        !pkg_safe_namespace_path(namespace_path) ||
        !out_dirs || max_dirs <= 0) return 0;
    char store[1024]; int count = 0;
    snprintf(store, sizeof(store), "%s" PATH_SEP "packages", project_dir);
    count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
    snprintf(store, sizeof(store), "%s" PATH_SEP ".zan-packages", project_dir);
    count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
    {
        char exe_dir[1024] = {0};
#ifdef _WIN32
        if (GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir))) {
            char *last_sep = strrchr(exe_dir, '\\');
            if (last_sep) *last_sep = '\0';
            snprintf(store, sizeof(store), "%s\\..\\packages", exe_dir);
            if (pkg_is_dir(store))
                count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
            snprintf(store, sizeof(store), "%s\\packages", exe_dir);
            if (pkg_is_dir(store))
                count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
        }
#elif defined(__APPLE__)
        uint32_t exe_sz = sizeof(exe_dir);
        if (_NSGetExecutablePath(exe_dir, &exe_sz) == 0) {
            char *last_sep = strrchr(exe_dir, '/');
            if (last_sep) *last_sep = '\0';
            snprintf(store, sizeof(store), "%s/../packages", exe_dir);
            if (pkg_is_dir(store))
                count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
        }
#else
        ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
        if (len > 0) {
            exe_dir[len] = '\0';
            char *last_sep = strrchr(exe_dir, '/');
            if (last_sep) *last_sep = '\0';
            snprintf(store, sizeof(store), "%s/../packages", exe_dir);
            if (pkg_is_dir(store))
                count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
        }
#endif
    }
    if (zan_pkg_global_store(store, sizeof(store)))
        count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
    return count;
}

static bool pkg_mkdirs(const char *path) {
    char tmp[1024]; size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return false;
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] != '/' && tmp[i] != '\\' && tmp[i] != 0) continue;
#ifdef _WIN32
        if (i == 2 && tmp[1] == ':') continue;
#endif
        char save = tmp[i]; tmp[i] = 0;
        if (*tmp && !pkg_is_dir(tmp)) {
#ifdef _WIN32
            if (!CreateDirectoryA(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
#else
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
#endif
        }
        tmp[i] = save;
    }
    return true;
}

static bool pkg_copy_tree(const char *src, const char *dst, int depth) {
    if (depth > 64 || !pkg_mkdirs(dst)) return false;
#ifdef _WIN32
    char pattern[1024]; WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof(pattern), "%s\\*", src);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (!pkg_safe_component(fd.cFileName) ||
            (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { ok = false; break; }
        char a[1024], b[1024];
        snprintf(a, sizeof(a), "%s\\%s", src, fd.cFileName);
        snprintf(b, sizeof(b), "%s\\%s", dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = pkg_copy_tree(a, b, depth + 1);
        else ok = CopyFileA(a, b, TRUE) != 0;
    } while (ok && FindNextFileA(h, &fd));
    FindClose(h); return ok;
#else
    DIR *d = opendir(src); if (!d) return false;
    bool ok = true; struct dirent *e;
    while (ok && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (!pkg_safe_component(e->d_name)) { ok = false; break; }
        char a[1024], b[1024]; struct stat st;
        snprintf(a, sizeof(a), "%s/%s", src, e->d_name);
        snprintf(b, sizeof(b), "%s/%s", dst, e->d_name);
        if (lstat(a, &st) != 0 || S_ISLNK(st.st_mode)) { ok = false; break; }
        if (S_ISDIR(st.st_mode)) ok = pkg_copy_tree(a, b, depth + 1);
        else if (S_ISREG(st.st_mode)) {
            FILE *in = fopen(a, "rb"), *out = in ? fopen(b, "wb") : NULL;
            if (!out) { if (in) fclose(in); ok = false; break; }
            char buf[16384]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) != 0)
                if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
            if (ferror(in) || fclose(out) != 0) ok = false;
            fclose(in);
        } else ok = false;
    }
    closedir(d); return ok;
#endif
}

static void pkg_remove_tree(const char *path) {
#ifdef _WIN32
    char pattern[1024]; WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char p[1024]; snprintf(p, sizeof(p), "%s\\%s", path, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) pkg_remove_tree(p);
            else DeleteFileA(p);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(path);
#else
    DIR *d = opendir(path); struct dirent *e;
    if (d) { while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char p[1024]; struct stat st; snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) pkg_remove_tree(p);
        else unlink(p);
    } closedir(d); }
    rmdir(path);
#endif
}

bool zan_pkg_install_local(const char *source_dir, const char *package_name,
                           zan_pkg_scope_t scope, const char *project_dir,
                           char *status, size_t status_size) {
    if (!source_dir || !pkg_safe_component(package_name) || !pkg_is_dir(source_dir)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=invalid_source"); return false;
    }
    char manifest[1024];
    snprintf(manifest, sizeof(manifest), "%s" PATH_SEP "zan.pkg", source_dir);
    zan_package_t pkg;
    if (!zan_pkg_load(&pkg, manifest) || !pkg.name[0] || strcmp(pkg.name, package_name) != 0) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=invalid_manifest package=%s", package_name);
        return false;
    }
    if (!pkg.has_version) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=missing_version package=%s", package_name);
        zan_pkg_destroy(&pkg);
        return false;
    }
    char pkg_src[1024], pkg_stdlib[1024];
    snprintf(pkg_src, sizeof(pkg_src), "%s" PATH_SEP "src", source_dir);
    snprintf(pkg_stdlib, sizeof(pkg_stdlib), "%s" PATH_SEP "stdlib", source_dir);
    if (!pkg_is_dir(pkg_src) && !pkg_is_dir(pkg_stdlib)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=no_stdlib_layout package=%s", package_name);
        zan_pkg_destroy(&pkg);
        return false;
    }
    zan_pkg_destroy(&pkg);
    char store[1024];
    if (scope == ZAN_PKG_SCOPE_GLOBAL) {
        if (!zan_pkg_global_store(store, sizeof(store))) {
            snprintf(status, status_size, "ZANPKG_STATUS action=install status=no_global_store package=%s", package_name); return false;
        }
    } else {
        if (!project_dir || !*project_dir) {
            snprintf(status, status_size, "ZANPKG_STATUS action=install status=no_project package=%s", package_name); return false;
        }
        snprintf(store, sizeof(store), "%s" PATH_SEP ".zan-packages", project_dir);
    }
    if (!pkg_mkdirs(store)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=io_error package=%s", package_name); return false;
    }
    char dst[1024], stage[1024];
    snprintf(dst, sizeof(dst), "%s" PATH_SEP "%s", store, package_name);
#ifdef _WIN32
    snprintf(stage, sizeof(stage), "%s" PATH_SEP ".%s.tmp.%lu", store, package_name, (unsigned long)GetCurrentProcessId());
#else
    snprintf(stage, sizeof(stage), "%s/.%s.tmp.%ld", store, package_name, (long)getpid());
#endif
    if (pkg_is_dir(dst)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=exists package=%s", package_name); return false;
    }
    pkg_remove_tree(stage);
    if (!pkg_copy_tree(source_dir, stage, 0)) {
        pkg_remove_tree(stage);
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=unsafe_or_io package=%s", package_name); return false;
    }
#ifdef _WIN32
    bool renamed = MoveFileExA(stage, dst, MOVEFILE_WRITE_THROUGH) != 0;
#else
    bool renamed = rename(stage, dst) == 0;
#endif
    if (!renamed) { pkg_remove_tree(stage); snprintf(status, status_size, "ZANPKG_STATUS action=install status=io_error package=%s", package_name); return false; }
    snprintf(status, status_size, "ZANPKG_STATUS action=install status=installed package=%s scope=%s",
             package_name, scope == ZAN_PKG_SCOPE_GLOBAL ? "global" : "project");
    return true;
}

bool zan_pkg_api_validate(const char *api_url, char *status, size_t status_size) {
    bool secure = api_url && (strncmp(api_url, "https://", 8) == 0 ||
        strncmp(api_url, "http://localhost", 16) == 0 ||
        strncmp(api_url, "http://127.0.0.1", 16) == 0 ||
        strncmp(api_url, "http://[::1]", 12) == 0);
    if (!secure) {
        snprintf(status, status_size, "ZANPKG_STATUS action=api status=insecure_url");
        return false;
    }
    snprintf(status, status_size, "ZANPKG_STATUS action=remote status=unsupported capability=http_archive_signature");
    return true;
}

void zan_pkg_destroy(zan_package_t *pkg) {
    free(pkg->deps);
    free(pkg->source_dirs);
    memset(pkg, 0, sizeof(*pkg));
}

void zan_pkg_registry_destroy(zan_pkg_registry_t *reg) {
    free(reg->cache_dir);
    free(reg->lock_file);
    for (int i = 0; i < reg->resolved_count; i++) { zan_pkg_destroy(reg->resolved[i]); free(reg->resolved[i]); }
    free(reg->resolved);
    memset(reg, 0, sizeof(*reg));
}


typedef struct {
    char pkg[128];
    char plugin_id[64];
} zan_usage_seen_t;

static zan_usage_seen_t zan_usage_seen[1024];
static int zan_usage_seen_count;

void zan_pkg_note_usage(const char *store, const char *package_name) {
    if (!store || !package_name || !*package_name) return;
    for (int i = 0; i < zan_usage_seen_count; i++)
        if (strcmp(zan_usage_seen[i].pkg, package_name) == 0) return;
    if (zan_usage_seen_count >= (int)(sizeof(zan_usage_seen) / sizeof(zan_usage_seen[0])))
        return;
    char manifest_path[1200];
    snprintf(manifest_path, sizeof(manifest_path), "%s" PATH_SEP "%s" PATH_SEP "zan.pkg",
             store, package_name);
    zan_package_t pkg;
    memset(&pkg, 0, sizeof(pkg));
    if (!zan_pkg_load(&pkg, manifest_path)) return;
    if (pkg.plugin_id[0]) {
        strncpy(zan_usage_seen[zan_usage_seen_count].pkg, package_name,
                sizeof(zan_usage_seen[0].pkg) - 1);
        strncpy(zan_usage_seen[zan_usage_seen_count].plugin_id, pkg.plugin_id,
                sizeof(zan_usage_seen[0].plugin_id) - 1);
        zan_usage_seen_count++;
        fprintf(stderr, "ZANPKG_USAGE plugin_id=%s package=%s action=build\n",
                pkg.plugin_id, package_name);
    }
    zan_pkg_destroy(&pkg);
}
