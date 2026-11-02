#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>

#include "host_oom.h"
/* construction */

/* 平台兼容的轻量级字符串复制实现 */
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

static uint64_t json_key_hash(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

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

static void json_obj_index_rebuild(json_value *obj) {
    long long want = obj->as.obj.cap > 8 ? obj->as.obj.cap : 8;
    if (want > (INT_MAX >> 2)) {
        free(obj->as.obj.index);
        obj->as.obj.index = NULL;
        obj->as.obj.index_cap = 0;
        return;
    }
    int cap = 16;
    while ((long long)cap < want * 2) cap <<= 1;
    free(obj->as.obj.index);
    obj->as.obj.index = (int *)calloc((size_t)cap, sizeof(int));
    obj->as.obj.index_cap = obj->as.obj.index ? cap : 0;
    if (!obj->as.obj.index) return;
    for (int k = 0; k < obj->as.obj.count; k++)
        json_obj_index_insert(obj, obj->as.obj.keys[k], k);
}

void json_obj_set(json_value *obj, const char *key, json_value *val) {
    if (!obj || obj->type != JSON_OBJ || !key || !val) return;

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
            /* 未持久化挂载的瞬态节点由当前调用栈负责释放，防止内存泄漏 */
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

    if (!obj->as.obj.index) {
        if (obj->as.obj.count >= 16) json_obj_index_rebuild(obj);
    } else if ((long long)(obj->as.obj.count + 1) * 2 >
                   (long long)obj->as.obj.index_cap &&
               obj->as.obj.index_cap <= (INT_MAX >> 1)) {
        json_obj_index_rebuild(obj);
    } else {
        json_obj_index_insert(obj, copy, obj->as.obj.count - 1);
    }
}

/* free */

/* 待释放节点栈的推入：容量翻倍，OOM 走 host fail-fast（释放路径无回旋） */
static void jfree_push(json_value ***st, size_t *cap, size_t *n,
                       json_value *v) {
    if (*n == *cap) {
        if (*cap > SIZE_MAX / (2 * sizeof(json_value *))) zan_host_oom();
        *cap *= 2;
        json_value **g =
            (json_value **)realloc(*st, sizeof(json_value *) * *cap);
        if (!g) zan_host_oom();
        *st = g;
    }
    (*st)[(*n)++] = v;
}

void json_free(json_value *v) {
    if (!v) return;
    /* 显式栈迭代释放：解析器允许 1024 层、程序化构建不限层，曾递归下钻
     * 可耗尽线程栈（析构路径 Stack Overflow） */
    size_t cap = 64, n = 0;
    json_value **st = (json_value **)malloc(sizeof(json_value *) * cap);
    if (!st) zan_host_oom();
    st[n++] = v;
    while (n) {
        json_value *cur = st[--n];
        switch (cur->type) {
        case JSON_STR:
            free(cur->as.str);
            break;
        case JSON_ARR:
            for (int i = 0; i < cur->as.arr.count; i++)
                jfree_push(&st, &cap, &n, cur->as.arr.items[i]);
            free(cur->as.arr.items);
            break;
        case JSON_OBJ:
            for (int i = 0; i < cur->as.obj.count; i++)
                free(cur->as.obj.keys[i]);
            for (int i = 0; i < cur->as.obj.count; i++)
                jfree_push(&st, &cap, &n, cur->as.obj.vals[i]);
            free(cur->as.obj.keys);
            free(cur->as.obj.vals);
            free(cur->as.obj.index);
            break;
        default:
            break;
        }
        free(cur);
    }
    free(st);
}

/* accessors */

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

/* parser */

typedef struct {
    const char *p;
    const char *end;
    bool ok;
    int depth;
} jparser;

#define JSON_MAX_DEPTH 1024

static void jp_skip_ws(jparser *j) {
    while (j->p < j->end &&
           (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r'))
        j->p++;
}

static json_value *jp_value(jparser *j);

/* UTF-8 良构校验并把整段序列追加进解码缓冲（lead 已由调用方读走但尚未入
 * buf，续字节在 j->p 处）。拒绝坏续字节、超长编码（C0/C1 头、E0 后 A0
 * 以下、F0 后 90 以下）、UTF-16 代理区（ED 后 A0 以上）与 >U+10FFFF
 * （F4 后 90 以上、F5+）。追加途中扩容失败返回 false，buf 所有权仍在
 * 调用方。 */
static bool jp_utf8_append(jparser *j, unsigned char lead,
                           char **pbuf, size_t *plen, size_t *pcap) {
    int need;
    unsigned char lo_min = 0x80, lo_max = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        need = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        need = 3;
        if (lead == 0xE0) { lo_min = 0xA0; }
        else if (lead == 0xED) { lo_max = 0x9F; }
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        need = 4;
        if (lead == 0xF0) { lo_min = 0x90; }
        else if (lead == 0xF4) { lo_max = 0x8F; }
    } else {
        return false;
    }
    if (j->end - j->p < need - 1) return false;
    for (int k = 0; k < need - 1; k++) {
        unsigned char cont = (unsigned char)j->p[k];
        if (cont < lo_min || cont > lo_max) return false;
        lo_min = 0x80; lo_max = 0xBF;
    }
    const char *src = j->p - 1; /* lead 已被读走，回看一字节 */
    for (int k = 0; k < need; k++) {
        if (*plen + 1 >= *pcap) {
            if (*pcap > SIZE_MAX / 2) return false;
            *pcap *= 2;
            char *g = (char *)realloc(*pbuf, *pcap);
            if (!g) return false;
            *pbuf = g;
        }
        (*pbuf)[(*plen)++] = src[k];
    }
    j->p += need - 1;
    return true;
}

static json_value *jp_string(jparser *j) {
    if (j->p >= j->end || *j->p != '"') { j->ok = false; return NULL; }
    j->p++;
    size_t cap = 16, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { j->ok = false; return NULL; }
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if ((unsigned char)c < 0x20) {
            /* RFC 8259：字符串内不允许未转义控制字符（曾直通，可走私协议帧） */
            free(buf); j->ok = false; return NULL;
        }
        if ((unsigned char)c >= 0x80) {
            /* 非法 UTF-8 序列（坏续字节/超长/代理区/越界码点）曾直通 */
            if (!jp_utf8_append(j, (unsigned char)c, &buf, &len, &cap)) {
                free(buf); j->ok = false; return NULL;
            }
            continue;
        }
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
                if (code == 0) { j->ok = false; free(buf); return NULL; }
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
                    /* UTF-16 高低代理对严格配对校验 */
                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        code = 0xFFFD;
                    } else {
                        code = 0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
                    }
                } else if (code >= 0xD800 && code <= 0xDFFF) {
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
    j->p++;
    buf[len] = '\0';
    json_value *v = json_alloc(JSON_STR);
    if (!v) { free(buf); j->ok = false; return NULL; }
    v->as.str = buf;
    return v;
}

/* RFC 8259 number 语法整 token 复核：-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
 * 曾只扫字符集不验结构，strtod 前缀语义把 "1.2.3"→1.2、"-"→0.0、"1e"→1.0
 * 静默产错值（消费方 LSP/DAP 外部输入面）。 */
static bool jp_num_valid(const char *s, size_t n) {
    size_t i = 0;
    if (i < n && s[i] == '-') i++;
    if (i >= n || s[i] < '0' || s[i] > '9') return false;
    if (s[i] == '0') {
        i++;
    } else {
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (i < n && s[i] == '.') {
        i++;
        if (i >= n || s[i] < '0' || s[i] > '9') return false;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (i >= n || s[i] < '0' || s[i] > '9') return false;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    }
    return i == n;
}

static json_value *jp_number(jparser *j) {
    const char *start = j->p;
    /* RFC 8259 grammar: no leading '+' */
    if (j->p < j->end && *j->p == '-') j->p++;
    while (j->p < j->end &&
           (isdigit((unsigned char)*j->p) || *j->p == '.' ||
            *j->p == 'e' || *j->p == 'E' || *j->p == '+' || *j->p == '-'))
        j->p++;
    size_t n = (size_t)(j->p - start);
    if (!jp_num_valid(start, n)) {
        j->ok = false;
        return NULL;
    }
    char tmp[64];
    if (n >= sizeof(tmp)) {
        /* 语法已验合法的超长数值按浮点边界值落盘（溢出保护） */
        return json_new_num(*start == '-' ? -HUGE_VAL : HUGE_VAL);
    }
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
    jp_skip_ws(&j);
    if (j.p != j.end) { json_free(v); return NULL; }
    return v;
}

/* serialize */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    int    oom;
} sbuf;

static void sb_ensure(sbuf *s, size_t extra) {
    if (s->len + extra + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 256;
        while (nc < s->len + extra + 1) nc *= 2;
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

/* 序列化工作栈帧：容器携带下一子下标，idx=-1 表示尚未开括号 */
typedef struct {
    const json_value *v;
    int idx;
} putframe;

static void sb_put_value(sbuf *s, const json_value *v) {
    if (!v) { sb_puts(s, "null"); return; }
    /* 显式栈迭代序列化：与 json_free 同源的深嵌套栈溢出防护，
     * 输出序与旧递归版逐字节一致 */
    size_t cap = 64, n = 0;
    putframe *st = (putframe *)malloc(sizeof(putframe) * cap);
    if (!st) { s->oom = 1; return; }
    st[n].v = v; st[n].idx = -1; n++;
    while (n) {
        if (s->oom) break;
        putframe *f = &st[n - 1];
        const json_value *cur = f->v;
        if (cur->type == JSON_ARR) {
            if (f->idx < 0) { sb_putc(s, '['); f->idx = 0; continue; }
            if (f->idx >= cur->as.arr.count) { sb_putc(s, ']'); n--; continue; }
            if (f->idx > 0) sb_putc(s, ',');
            const json_value *child = cur->as.arr.items[f->idx++];
            if (n == cap) {
                size_t nc = cap * 2;
                putframe *g = (putframe *)realloc(st, sizeof(putframe) * nc);
                if (!g) { s->oom = 1; break; }
                st = g; cap = nc;
            }
            st[n].v = child; st[n].idx = -1; n++;
        } else if (cur->type == JSON_OBJ) {
            if (f->idx < 0) { sb_putc(s, '{'); f->idx = 0; continue; }
            if (f->idx >= cur->as.obj.count) { sb_putc(s, '}'); n--; continue; }
            if (f->idx > 0) sb_putc(s, ',');
            sb_put_escaped(s, cur->as.obj.keys[f->idx]);
            sb_putc(s, ':');
            const json_value *child = cur->as.obj.vals[f->idx++];
            if (n == cap) {
                size_t nc = cap * 2;
                putframe *g = (putframe *)realloc(st, sizeof(putframe) * nc);
                if (!g) { s->oom = 1; break; }
                st = g; cap = nc;
            }
            st[n].v = child; st[n].idx = -1; n++;
        } else {
            switch (cur->type) {
            case JSON_NULL: sb_puts(s, "null"); break;
            case JSON_BOOL: sb_puts(s, cur->as.b ? "true" : "false"); break;
            case JSON_NUM: {
                double num = cur->as.num;
                if (!isfinite(num)) { sb_puts(s, "null"); break; }
                char tmp[64];
                if (num == floor(num) && fabs(num) < 1e15) {
                    snprintf(tmp, sizeof(tmp), "%lld", (long long)num);
                } else {
                    snprintf(tmp, sizeof(tmp), "%.17g", num);
                }
                sb_puts(s, tmp);
                break;
            }
            case JSON_STR:
                sb_put_escaped(s, cur->as.str ? cur->as.str : "");
                break;
            default:
                break;
            }
            n--;
        }
    }
    free(st);
}

char *json_serialize(const json_value *v) {
    sbuf s = {0};
    sb_put_value(&s, v);
    if (!s.buf) { s.buf = (char *)malloc(1); s.cap = 1; s.len = 0; }
    if (!s.oom) s.buf[s.len] = '\0';
    else if (s.cap > 0) s.buf[0] = '\0';
    return s.buf;
}
