/* rt_core.c -- Zan minimal runtime library implementation.
 *
 * Provides ARC memory management, basic strings, and console output.
 * This is linked into every Zan executable.
 */

#include "rt_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- ARC operations ---- */

void *zan_rt_alloc(size_t size, int32_t type_id) {
    size_t total = sizeof(zan_rt_header_t) + size;
    zan_rt_header_t *header = (zan_rt_header_t *)calloc(1, total);
    if (!header) {
        fprintf(stderr, "zan: out of memory (alloc %zu bytes)\n", total);
        abort();
    }
    header->refcount = 1;
    header->type_id = type_id;
    return (void *)(header + 1);
}

static zan_rt_header_t *get_header(void *obj) {
    return ((zan_rt_header_t *)obj) - 1;
}

void zan_rt_retain(void *obj) {
    if (!obj) return;
    zan_rt_header_t *h = get_header(obj);
    __atomic_add_fetch(&h->refcount, 1, __ATOMIC_RELAXED);
}

void zan_rt_release(void *obj) {
    if (!obj) return;
    zan_rt_header_t *h = get_header(obj);
    if (__atomic_sub_fetch(&h->refcount, 1, __ATOMIC_ACQ_REL) == 0) {
        free(h);
    }
}

int32_t zan_rt_refcount(void *obj) {
    if (!obj) return 0;
    zan_rt_header_t *h = get_header(obj);
    return __atomic_load_n(&h->refcount, __ATOMIC_RELAXED);
}

/* ---- string ---- */

zan_rt_string_t *zan_rt_string_new(const char *cstr, int32_t len) {
    if (len < 0) len = (int32_t)strlen(cstr);
    size_t total = sizeof(zan_rt_string_t) + (size_t)len + 1;
    zan_rt_string_t *s = (zan_rt_string_t *)calloc(1, total);
    if (!s) {
        fprintf(stderr, "zan: out of memory (string %d bytes)\n", len);
        abort();
    }
    s->header.refcount = 1;
    s->header.type_id = 1; /* TYPE_STRING */
    s->length = len;
    s->capacity = len;
    if (cstr) memcpy(s->data, cstr, (size_t)len);
    s->data[len] = '\0';
    return s;
}

zan_rt_string_t *zan_rt_string_concat(zan_rt_string_t *a, zan_rt_string_t *b) {
    if (!a) return b;
    if (!b) return a;
    int32_t new_len = a->length + b->length;
    zan_rt_string_t *result = zan_rt_string_new(NULL, new_len);
    memcpy(result->data, a->data, (size_t)a->length);
    memcpy(result->data + a->length, b->data, (size_t)b->length);
    result->data[new_len] = '\0';
    result->length = new_len;
    return result;
}

const char *zan_rt_string_cstr(zan_rt_string_t *s) {
    if (!s) return "";
    return s->data;
}

int32_t zan_rt_string_length(zan_rt_string_t *s) {
    if (!s) return 0;
    return s->length;
}

void zan_rt_string_free(zan_rt_string_t *s) {
    if (!s) return;
    free(s);
}

/* ---- console I/O ---- */

void zan_rt_println(const char *str) {
    printf("%s\n", str ? str : "");
}

void zan_rt_print_int(int64_t val) {
    printf("%lld\n", (long long)val);
}

void zan_rt_print_double(double val) {
    printf("%g\n", val);
}

/* ---- runtime init/shutdown ---- */

void zan_rt_init(void) {
    /* reserved for future use (GC init, thread pool, etc.) */
}

void zan_rt_shutdown(void) {
    /* reserved for future use (GC finalize, cleanup) */
}
