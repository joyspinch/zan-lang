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

void zan_dep_to_range(const zan_dependency_t *dep, zan_version_range_t *out) {
    memset(out, 0, sizeof(*out));
    if (!dep) {
        out->min_included = true;
        return;
    }
    switch (dep->kind) {
    case ZAN_DEP_ANY:
        out->min.major = 0;
        out->min.minor = 0;
        out->min.patch = 0;
        out->min_included = true;
        out->has_max = false;
        break;
    case ZAN_DEP_EXACT:
        out->min = dep->min_ver;
        out->min_included = true;
        out->max = dep->min_ver;
        out->max_included = true;
        out->has_max = true;
        break;
    case ZAN_DEP_MINIMUM:
        out->min = dep->min_ver;
        out->min_included = true;
        out->has_max = false;
        break;
    case ZAN_DEP_MAXIMUM:
        out->min.major = 0;
        out->min.minor = 0;
        out->min.patch = 0;
        out->min_included = true;
        out->max = dep->min_ver;
        out->max_included = true;
        out->has_max = true;
        break;
    case ZAN_DEP_COMPAT:
        out->min = dep->min_ver;
        out->min_included = true;
        out->max_included = false;
        out->has_max = true;
        if (dep->min_ver.major > 0) {
            out->max.major = dep->min_ver.major + 1;
            out->max.minor = 0;
            out->max.patch = 0;
        } else if (dep->min_ver.minor > 0) {
            out->max.major = 0;
            out->max.minor = dep->min_ver.minor + 1;
            out->max.patch = 0;
        } else {
            out->max.major = 0;
            out->max.minor = 0;
            out->max.patch = dep->min_ver.patch + 1;
        }
        break;
    case ZAN_DEP_TILDE:
        out->min = dep->min_ver;
        out->min_included = true;
        out->max_included = false;
        out->has_max = true;
        out->max.major = dep->min_ver.major;
        out->max.minor = dep->min_ver.minor + 1;
        out->max.patch = 0;
        break;
    case ZAN_DEP_RANGE:
        out->min = dep->min_ver;
        out->min_included = true;
        out->max = dep->max_ver;
        out->max_included = false;
        out->has_max = true;
        break;
    }
}

char *zan_range_format(const zan_version_range_t *r, char *buf, int buf_size) {
    if (!r || buf_size <= 0) return buf;
    char min_buf[64], max_buf[64];
    zan_version_format(&r->min, min_buf, sizeof(min_buf));
    if (!r->has_max) {
        snprintf(buf, (size_t)buf_size, ">=%s", min_buf);
    } else {
        zan_version_format(&r->max, max_buf, sizeof(max_buf));
        if (r->min_included && r->max_included && zan_version_compare(&r->min, &r->max) == 0) {
            snprintf(buf, (size_t)buf_size, "=%s", min_buf);
        } else {
            snprintf(buf, (size_t)buf_size, "%s%s, %s%s",
                     r->min_included ? ">=" : ">", min_buf,
                     r->max_included ? "<=" : "<", max_buf);
        }
    }
    return buf;
}

bool zan_range_intersect(const zan_version_range_t *a, const zan_version_range_t *b,
                         zan_version_range_t *out) {
    if (!a || !b || !out) return false;
    memset(out, 0, sizeof(*out));

    /* 下界取较大者 */
    int cmp_min = zan_version_compare(&a->min, &b->min);
    if (cmp_min > 0) {
        out->min = a->min;
        out->min_included = a->min_included;
    } else if (cmp_min < 0) {
        out->min = b->min;
        out->min_included = b->min_included;
    } else {
        out->min = a->min;
        out->min_included = a->min_included && b->min_included;
    }

    /* 上界取较小者 */
    if (!a->has_max && !b->has_max) {
        out->has_max = false;
    } else if (a->has_max && !b->has_max) {
        out->max = a->max;
        out->max_included = a->max_included;
        out->has_max = true;
    } else if (!a->has_max && b->has_max) {
        out->max = b->max;
        out->max_included = b->max_included;
        out->has_max = true;
    } else {
        int cmp_max = zan_version_compare(&a->max, &b->max);
        if (cmp_max < 0) {
            out->max = a->max;
            out->max_included = a->max_included;
        } else if (cmp_max > 0) {
            out->max = b->max;
            out->max_included = b->max_included;
        } else {
            out->max = a->max;
            out->max_included = a->max_included && b->max_included;
        }
        out->has_max = true;
    }

    /* 验证非空性 */
    if (out->has_max) {
        int cmp = zan_version_compare(&out->min, &out->max);
        if (cmp > 0) return false;
        if (cmp == 0 && (!out->min_included || !out->max_included)) return false;
    }
    return true;
}

bool zan_version_satisfies_range(const zan_version_t *v, const zan_version_range_t *r) {
    if (!v || !r) return false;
    int cmp_min = zan_version_compare(v, &r->min);
    if (cmp_min < 0) return false;
    if (cmp_min == 0 && !r->min_included) return false;
    if (r->has_max) {
        int cmp_max = zan_version_compare(v, &r->max);
        if (cmp_max > 0) return false;
        if (cmp_max == 0 && !r->max_included) return false;
    }
    return true;
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

bool zan_pkg_compute_tree_checksum(const char *dir, char out_hex[65]);
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

