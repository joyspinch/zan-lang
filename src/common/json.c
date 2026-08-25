/* json.c -- Minimal JSON parser and writer (see json.h). */
#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>

#include "host_oom.h"
/* ============================ construction ============================ */

/* small local strdup to avoid depending on the non-standard strdup */
static char *strdup_key(const char *s) {
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static json_value *json_alloc(json_type_t t) {
    json_value *v = (json_value *)calloc(1, sizeof(json_value));
    if (v) v->type = t;
    return v;
}

json_value *json_new_null(void) { return json_alloc(JSON_NULL); }

json_value *json_new_bool(bool b) {
    json_value *v = json_alloc(JSON_BOOL);
    if (v) v->as.b = b;
    return v;
}

json_value *json_new_num(double n) {
    json_value *v = json_alloc(JSON_NUM);
    if (v) v->as.num = n;
    return v;
}

json_value *json_new_str(const char *s) {
    json_value *v = json_alloc(JSON_STR);
    if (!v) return NULL;
    size_t n = s ? strlen(s) : 0;
    v->as.str = (char *)malloc(n + 1);
    if (!v->as.str) { free(v); return NULL; }
    if (s) memcpy(v->as.str, s, n);
    v->as.str[n] = '\0';
    return v;
}

json_value *json_new_obj(void) { return json_alloc(JSON_OBJ); }
json_value *json_new_arr(void) { return json_alloc(JSON_ARR); }

void json_arr_add(json_value *arr, json_value *val) {
    if (!arr || arr->type != JSON_ARR || !val) return;
    if (arr->as.arr.count >= arr->as.arr.cap) {
        if (arr->as.arr.cap > INT_MAX / 2) zan_host_oom();
        int nc = arr->as.arr.cap ? arr->as.arr.cap * 2 : 8;
        json_value **grown = (json_value **)realloc(arr->as.arr.items,
                                                    sizeof(json_value *) * (size_t)nc);
        if (!grown) return;
        arr->as.arr.items = grown;
        arr->as.arr.cap = nc;
    }
    arr->as.arr.items[arr->as.arr.count++] = val;
}

/* ---- object key index ----------------------------------------------------
 * json_obj_set runs once per member while parsing and used to linear-scan
 * every existing key for the replace case, making object construction
 * O(n^2) strcmps -- a 20k-key message cost ~2e8 comparisons, a CPU
 * amplifier any peer gets for free. A small open-addressing index (key hash
 * -> ordinal+1, 0 = empty slot) sits next to the ordered arrays;
 * serialization still walks the arrays, so insertion order is preserved.
 * There is no per-key removal API, so the index never needs tombstones.
 * The index is optional at runtime: if its allocation fails, callers fall
 * back to the linear scan. */
static uint64_t json_key_hash(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/* index_cap is always a power of two and at least twice the member count,
 * so the load factor stays <= 0.5 and probes terminate. */
static int json_obj_index_find(const json_value *obj, const char *key,
                               uint64_t h) {
    int cap = obj->as.obj.index_cap;
    if (!obj->as.obj.index || cap == 0) return -1;
    uint32_t mask = (uint32_t)(cap - 1);
    uint32_t i = (uint32_t)h & mask;
    while (obj->as.obj.index[i]) {
        int ordinal = obj->as.obj.index[i] - 1;
        if (ordinal >= 0 && ordinal < obj->as.obj.count &&
            strcmp(obj->as.obj.keys[ordinal], key) == 0)
            return ordinal;
        i = (i + 1) & mask;
    }
    return -1;
}

static void json_obj_index_insert(json_value *obj, const char *key,
                                  int ordinal) {
    uint64_t h = json_key_hash(key);
    uint32_t mask = (uint32_t)(obj->as.obj.index_cap - 1);
    uint32_t i = (uint32_t)h & mask;
    while (obj->as.obj.index[i]) i = (i + 1) & mask;
    obj->as.obj.index[i] = ordinal + 1;
}

/* (Re)build from the ordered arrays; ordinals are stable across growth, so
 * this is also correct after keys[] was realloc'd. A failed calloc leaves
 * the object without an index -- correctness never depends on it. */
static void json_obj_index_rebuild(json_value *obj) {
    int want = obj->as.obj.cap > 8 ? obj->as.obj.cap : 8;
    int cap = 16;
    while (cap < want * 2) {
        if (cap > (INT_MAX >> 1)) { free(obj->as.obj.index); obj->as.obj.index = NULL; obj->as.obj.index_cap = 0; return; }
        cap <<= 1;
    }
    free(obj->as.obj.index);
    obj->as.obj.index = (int *)calloc((size_t)cap, sizeof(int));
    obj->as.obj.index_cap = obj->as.obj.index ? cap : 0;
    if (!obj->as.obj.index) return;
    for (int k = 0; k < obj->as.obj.count; k++)
        json_obj_index_insert(obj, obj->as.obj.keys[k], k);
}

void json_obj_set(json_value *obj, const char *key, json_value *val) {
    if (!obj || obj->type != JSON_OBJ || !key || !val) return;

    /* replace existing: indexed when live, linear otherwise (tiny objects
     * never pay for an index; a failed index allocation keeps working). */
    int found = -1;
    if (obj->as.obj.index) {
        found = json_obj_index_find(obj, key, json_key_hash(key));
    } else {
        for (int i = 0; i < obj->as.obj.count; i++) {
            if (strcmp(obj->as.obj.keys[i], key) == 0) { found = i; break; }
        }
    }
    if (found >= 0) {
        json_free(obj->as.obj.vals[found]);
        obj->as.obj.vals[found] = val;
        return;
    }

    if (obj->as.obj.count >= obj->as.obj.cap) {
        /* Grow keys and vals independently. If one realloc fails we must not
         * touch the still-valid original of the other: realloc returns NULL
         * and leaves the old block intact, so only commit a pointer once its
         * realloc succeeded. This avoids the dangling-pointer + leaked-val
         * bug where freeing the failed-side buffer corrupted the surviving
         * side and left the object half-grown. */
        if (obj->as.obj.cap > INT_MAX / 2) { json_free(val); return; }
        int nc = obj->as.obj.cap ? obj->as.obj.cap * 2 : 8;
        char **nk = (char **)realloc(obj->as.obj.keys, sizeof(char *) * (size_t)nc);
        if (nk) obj->as.obj.keys = nk;
        json_value **nv = NULL;
        if (nk) {
            nv = (json_value **)realloc(obj->as.obj.vals, sizeof(json_value *) * (size_t)nc);
            if (nv) obj->as.obj.vals = nv;
        }
        if (!nk || !nv) {
            /* val was never stored, so we own it and must free it to avoid a
             * leak. The object itself stays consistent at its old capacity. */
            json_free(val);
            return;
        }
        obj->as.obj.cap = nc;
    }
    char *copy = strdup_key(key);
    if (!copy) { json_free(val); return; }
    obj->as.obj.keys[obj->as.obj.count] = copy;
    obj->as.obj.vals[obj->as.obj.count] = val;
    obj->as.obj.count++;

    /* Keep the index live once the object is big enough for O(n^2) to hurt:
     * build on first entry past 16 members, grow before load passes 0.5. */
    if (!obj->as.obj.index) {
        if (obj->as.obj.count >= 16) json_obj_index_rebuild(obj);
    } else if ((obj->as.obj.count + 1) * 2 > obj->as.obj.index_cap &&
               obj->as.obj.index_cap <= (INT_MAX >> 1)) {
        json_obj_index_rebuild(obj);
    } else {
        json_obj_index_insert(obj, copy, obj->as.obj.count - 1);
    }
}

/* ============================== free ================================= */

void json_free(json_value *v) {
    if (!v) return;
    switch (v->type) {
    case JSON_STR:
        free(v->as.str);
        break;
    case JSON_ARR:
        for (int i = 0; i < v->as.arr.count; i++) json_free(v->as.arr.items[i]);
        free(v->as.arr.items);
        break;
    case JSON_OBJ:
        for (int i = 0; i < v->as.obj.count; i++) {
            free(v->as.obj.keys[i]);
            json_free(v->as.obj.vals[i]);
        }
        free(v->as.obj.keys);
        free(v->as.obj.vals);
        free(v->as.obj.index);
        break;
    default:
        break;
    }
    free(v);
}

/* ============================ accessors ============================== */

bool json_is(const json_value *v, json_type_t type) {
    return v && v->type == type;
}

json_value *json_obj_get(const json_value *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJ || !key) return NULL;
    if (obj->as.obj.index) {
        int ordinal = json_obj_index_find(obj, key, json_key_hash(key));
        return ordinal >= 0 ? obj->as.obj.vals[ordinal] : NULL;
    }
    for (int i = 0; i < obj->as.obj.count; i++) {
        if (strcmp(obj->as.obj.keys[i], key) == 0) return obj->as.obj.vals[i];
    }
    return NULL;
}

const char *json_get_str(const json_value *v) {
    return (v && v->type == JSON_STR) ? v->as.str : NULL;
}

double json_get_num(const json_value *v, double def) {
    return (v && v->type == JSON_NUM) ? v->as.num : def;
}

bool json_get_bool(const json_value *v, bool def) {
    return (v && v->type == JSON_BOOL) ? v->as.b : def;
}

int json_arr_count(const json_value *v) {
    return (v && v->type == JSON_ARR) ? v->as.arr.count : 0;
}

json_value *json_arr_at(const json_value *v, int index) {
    if (!v || v->type != JSON_ARR || index < 0 || index >= v->as.arr.count)
        return NULL;
    return v->as.arr.items[index];
}

json_value *json_path(const json_value *root, const char *dotted_path) {
    if (!root || !dotted_path) return NULL;
    const json_value *cur = root;
    char key[256];
    const char *p = dotted_path;
    while (*p) {
        int k = 0;
        while (*p && *p != '.' && k < (int)sizeof(key) - 1) key[k++] = *p++;
        key[k] = '\0';
        if (*p == '.') p++;
        cur = json_obj_get(cur, key);
        if (!cur) return NULL;
    }
    return (json_value *)cur;
}

/* ============================== parser =============================== */

typedef struct {
    const char *p;
    const char *end;
    bool ok;
    int depth;
} jparser;

/* jp_value recurses once per nesting level and is fed directly by zan-lsp /
 * zan-dap with untrusted peer messages: without a cap, a few hundred KB of
 * `[[[[...` overflows the stack and kills the server. The cap has to clear
 * genmeta's compilation metadata too, whose nesting follows the expression
 * nesting of the compiled sources (ZanIDE's own unit reaches 144), so it is
 * set well above any hand-written code rather than at Json.NET's 128; a
 * thousand jp_value frames still cost well under a megabyte of stack. */
#define JSON_MAX_DEPTH 1024

static void jp_skip_ws(jparser *j) {
    while (j->p < j->end &&
           (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r'))
        j->p++;
}

static json_value *jp_value(jparser *j);

static json_value *jp_string(jparser *j) {
    if (j->p >= j->end || *j->p != '"') { j->ok = false; return NULL; }
    j->p++; /* opening quote */
    size_t cap = 16, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { j->ok = false; return NULL; }
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) {
            char e = *j->p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case '/': c = '/';  break;
            case '\\': c = '\\'; break;
            case '"': c = '"';  break;
            case 'u': {
                /* decode \uXXXX to UTF-8 (BMP only; surrogate pairs handled) */
                if (j->end - j->p < 4) { j->ok = false; free(buf); return NULL; }
                unsigned code = 0;
                for (int i = 0; i < 4; i++) {
                    char h = *j->p++;
                    code <<= 4;
                    if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
                    else { j->ok = false; free(buf); return NULL; }
                }
                if (code >= 0xD800 && code <= 0xDBFF &&
                    j->end - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
                    j->p += 2;
                    unsigned lo = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *j->p++;
                        lo <<= 4;
                        if (h >= '0' && h <= '9') lo |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') lo |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') lo |= (unsigned)(h - 'A' + 10);
                        else { j->ok = false; free(buf); return NULL; }
                    }
                    /* A high surrogate must pair with a low surrogate. The old
                     * code accepted ANY tail value: "\uD800\u0041" wrapped
                     * around into a wrong-but-valid code point, and a bad hex
                     * digit in the tail silently passed. */
                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        code = 0xFFFD;      /* U+FFFD, keep parsing */
                    } else {
                        code = 0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
                    }
                } else if (code >= 0xD800 && code <= 0xDFFF) {
                    /* Lone surrogate (bare low, or high with no \u tail):
                     * encode it as U+FFFD instead of emitting CESU-8 bytes no
                     * strict decoder accepts. */
                    code = 0xFFFD;
                }
                char utf8[4];
                int n = 0;
                if (code < 0x80) { utf8[n++] = (char)code; }
                else if (code < 0x800) {
                    utf8[n++] = (char)(0xC0 | (code >> 6));
                    utf8[n++] = (char)(0x80 | (code & 0x3F));
                } else if (code < 0x10000) {
                    utf8[n++] = (char)(0xE0 | (code >> 12));
                    utf8[n++] = (char)(0x80 | ((code >> 6) & 0x3F));
                    utf8[n++] = (char)(0x80 | (code & 0x3F));
                } else {
                    utf8[n++] = (char)(0xF0 | (code >> 18));
                    utf8[n++] = (char)(0x80 | ((code >> 12) & 0x3F));
                    utf8[n++] = (char)(0x80 | ((code >> 6) & 0x3F));
                    utf8[n++] = (char)(0x80 | (code & 0x3F));
                }
                for (int i = 0; i < n; i++) {
                    if (len + 1 >= cap) {
                        if (cap > SIZE_MAX / 2) { free(buf); j->ok = false; return NULL; }
                        cap *= 2;
                        char *g = (char *)realloc(buf, cap);
                        if (!g) { free(buf); j->ok = false; return NULL; }
                        buf = g;
                    }
                    buf[len++] = utf8[i];
                }
                continue;
            }
            default:
                /* RFC 8259 section 7: unknown escapes are a syntax error.
                 * Laundering "\q" into "q" accepts invalid input AND makes a
                 * round-trip through json_serialize change the bytes, which
                 * masks desync between peers. */
                free(buf); j->ok = false; return NULL;
            }
        }
        if (len + 1 >= cap) {
            if (cap > SIZE_MAX / 2) { free(buf); j->ok = false; return NULL; }
            cap *= 2;
            char *g = (char *)realloc(buf, cap);
            if (!g) { free(buf); j->ok = false; return NULL; }
            buf = g;
        }
        buf[len++] = c;
    }
    if (j->p >= j->end) { free(buf); j->ok = false; return NULL; }
    j->p++; /* closing quote */
    buf[len] = '\0';
    json_value *v = json_alloc(JSON_STR);
    if (!v) { free(buf); j->ok = false; return NULL; }
    v->as.str = buf;
    return v;
}

static json_value *jp_number(jparser *j) {
    const char *start = j->p;
    if (j->p < j->end && (*j->p == '-' || *j->p == '+')) j->p++;
    while (j->p < j->end &&
           (isdigit((unsigned char)*j->p) || *j->p == '.' ||
            *j->p == 'e' || *j->p == 'E' || *j->p == '+' || *j->p == '-'))
        j->p++;
    char tmp[64];
    size_t n = (size_t)(j->p - start);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    return json_new_num(strtod(tmp, NULL));
}

static json_value *jp_array(jparser *j) {
    j->p++; /* [ */
    json_value *arr = json_new_arr();
    jp_skip_ws(j);
    if (j->p < j->end && *j->p == ']') { j->p++; return arr; }
    for (;;) {
        jp_skip_ws(j);
        json_value *v = jp_value(j);
        if (!j->ok) { json_free(arr); return NULL; }
        json_arr_add(arr, v);
        jp_skip_ws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
        if (j->p < j->end && *j->p == ']') { j->p++; break; }
        j->ok = false; json_free(arr); return NULL;
    }
    return arr;
}

static json_value *jp_object(jparser *j) {
    j->p++; /* { */
    json_value *obj = json_new_obj();
    jp_skip_ws(j);
    if (j->p < j->end && *j->p == '}') { j->p++; return obj; }
    for (;;) {
        jp_skip_ws(j);
        json_value *key = jp_string(j);
        if (!j->ok || !key) { json_free(obj); return NULL; }
        jp_skip_ws(j);
        if (j->p >= j->end || *j->p != ':') {
            json_free(key); json_free(obj); j->ok = false; return NULL;
        }
        j->p++;
        jp_skip_ws(j);
        json_value *val = jp_value(j);
        if (!j->ok) { json_free(key); json_free(obj); return NULL; }
        json_obj_set(obj, key->as.str, val);
        json_free(key);
        jp_skip_ws(j);
        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
        if (j->p < j->end && *j->p == '}') { j->p++; break; }
        j->ok = false; json_free(obj); return NULL;
    }
    return obj;
}

static json_value *jp_value(jparser *j) {
    jp_skip_ws(j);
    if (j->p >= j->end) { j->ok = false; return NULL; }
    char c = *j->p;
    if (c == '"') return jp_string(j);
    if (c == '{' || c == '[') {
        if (j->depth >= JSON_MAX_DEPTH) { j->ok = false; return NULL; }
        j->depth++;
        json_value *v = (c == '{') ? jp_object(j) : jp_array(j);
        j->depth--;
        return v;
    }
    if (c == '-' || isdigit((unsigned char)c)) return jp_number(j);
    if ((size_t)(j->end - j->p) >= 4 && strncmp(j->p, "true", 4) == 0) {
        j->p += 4; return json_new_bool(true);
    }
    if ((size_t)(j->end - j->p) >= 5 && strncmp(j->p, "false", 5) == 0) {
        j->p += 5; return json_new_bool(false);
    }
    if ((size_t)(j->end - j->p) >= 4 && strncmp(j->p, "null", 4) == 0) {
        j->p += 4; return json_new_null();
    }
    j->ok = false;
    return NULL;
}

json_value *json_parse(const char *text) {
    if (!text) return NULL;
    jparser j;
    j.p = text;
    j.end = text + strlen(text);
    j.ok = true;
    j.depth = 0;
    json_value *v = jp_value(&j);
    if (!j.ok) { json_free(v); return NULL; }
    /* A well-formed document is exactly one value; reject anything trailing
     * other than whitespace, so a malformed `{"a":1} garbage` from an LSP/DAP
     * peer isn't silently accepted (and the garbage dropped). */
    jp_skip_ws(&j);
    if (j.p != j.end) { json_free(v); return NULL; }
    return v;
}

/* ============================ serialize ============================== */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    int    oom;     /* sticky: a realloc failed (test builds only -- the
                     * production allocation policy aborts); writers stop
                     * touching buf so a forced failure cannot corrupt it */
} sbuf;

static void sb_ensure(sbuf *s, size_t extra) {
    if (s->len + extra + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 256;
        while (nc < s->len + extra + 1) nc *= 2;
        /* Keep the old block on failure: committing `nc` with buf == NULL (or
         * a stale cap) turned every later write into a NULL deref or an
         * overflow. */
        char *g = (char *)realloc(s->buf, nc);
        if (!g) { s->oom = 1; return; }
        s->buf = g;
        s->cap = nc;
    }
}

static void sb_putc(sbuf *s, char c) {
    if (s->oom) return;
    sb_ensure(s, 1);
    if (s->oom) return;
    s->buf[s->len++] = c;
}

static void sb_puts(sbuf *s, const char *str) {
    if (s->oom) return;
    size_t n = strlen(str);
    sb_ensure(s, n);
    if (s->oom) return;
    memcpy(s->buf + s->len, str, n);
    s->len += n;
}

static void sb_put_escaped(sbuf *s, const char *str) {
    sb_putc(s, '"');
    for (const char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':  sb_puts(s, "\\\""); break;
        case '\\': sb_puts(s, "\\\\"); break;
        case '\n': sb_puts(s, "\\n"); break;
        case '\t': sb_puts(s, "\\t"); break;
        case '\r': sb_puts(s, "\\r"); break;
        case '\b': sb_puts(s, "\\b"); break;
        case '\f': sb_puts(s, "\\f"); break;
        default:
            if (c < 0x20) {
                char tmp[8];
                snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                sb_puts(s, tmp);
            } else {
                sb_putc(s, (char)c);
            }
        }
    }
    sb_putc(s, '"');
}

static void sb_put_value(sbuf *s, const json_value *v) {
    if (!v) { sb_puts(s, "null"); return; }
    switch (v->type) {
    case JSON_NULL: sb_puts(s, "null"); break;
    case JSON_BOOL: sb_puts(s, v->as.b ? "true" : "false"); break;
    case JSON_NUM: {
        double n = v->as.num;
        /* inf and nan serialize to invalid JSON ("inf"/"nan" via %g), which
         * makes strict peers abort the whole response. The parser accepts
         * overflowing literals like 1e999, so any arithmetic on them can
         * reach this point. Emit null, JSON's canonical "no value". */
        if (!isfinite(n)) { sb_puts(s, "null"); break; }
        char tmp[64];
        if (n == floor(n) && fabs(n) < 1e15) {
            snprintf(tmp, sizeof(tmp), "%lld", (long long)n);
        } else {
            snprintf(tmp, sizeof(tmp), "%.17g", n);
        }
        sb_puts(s, tmp);
        break;
    }
    case JSON_STR: sb_put_escaped(s, v->as.str ? v->as.str : ""); break;
    case JSON_ARR:
        sb_putc(s, '[');
        for (int i = 0; i < v->as.arr.count; i++) {
            if (i) sb_putc(s, ',');
            sb_put_value(s, v->as.arr.items[i]);
        }
        sb_putc(s, ']');
        break;
    case JSON_OBJ:
        sb_putc(s, '{');
        for (int i = 0; i < v->as.obj.count; i++) {
            if (i) sb_putc(s, ',');
            sb_put_escaped(s, v->as.obj.keys[i]);
            sb_putc(s, ':');
            sb_put_value(s, v->as.obj.vals[i]);
        }
        sb_putc(s, '}');
        break;
    }
}

char *json_serialize(const json_value *v) {
    sbuf s = {0};
    sb_put_value(&s, v);
    if (!s.buf) { s.buf = (char *)malloc(1); s.cap = 1; s.len = 0; }
    if (!s.oom) s.buf[s.len] = '\0';
    else if (s.cap > 0) s.buf[0] = '\0';   /* best-effort prefix, terminated */
    return s.buf;
}
