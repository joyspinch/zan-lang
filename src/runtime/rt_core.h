/* rt_core.h -- Zan minimal runtime library.
 *
 * Provides:
 *   - ARC retain/release for reference types
 *   - Basic string management
 *   - Memory allocation helpers
 *   - Console output
 */

#ifndef ZAN_RT_CORE_H
#define ZAN_RT_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- ARC object header ---- */

typedef struct {
    int32_t refcount;   /* atomic reference count */
    int32_t type_id;    /* runtime type identifier */
} zan_rt_header_t;

/* ---- ARC operations ---- */

void *zan_rt_alloc(size_t size, int32_t type_id);
void zan_rt_retain(void *obj);
void zan_rt_release(void *obj);
int32_t zan_rt_refcount(void *obj);

/* ---- string type ---- */

typedef struct {
    zan_rt_header_t header;
    int32_t length;
    int32_t capacity;
    char data[];      /* flexible array member */
} zan_rt_string_t;

zan_rt_string_t *zan_rt_string_new(const char *cstr, int32_t len);
zan_rt_string_t *zan_rt_string_concat(zan_rt_string_t *a, zan_rt_string_t *b);
const char *zan_rt_string_cstr(zan_rt_string_t *s);
int32_t zan_rt_string_length(zan_rt_string_t *s);
void zan_rt_string_free(zan_rt_string_t *s);

/* ---- console I/O ---- */

void zan_rt_println(const char *str);
void zan_rt_print_int(int64_t val);
void zan_rt_print_double(double val);

/* ---- runtime init/shutdown ---- */

void zan_rt_init(void);
void zan_rt_shutdown(void);

#endif /* ZAN_RT_CORE_H */
