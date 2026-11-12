#ifndef ZAN_EMBED_STRING_H
#define ZAN_EMBED_STRING_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "../common/zan_abi.h"

/* Text views of embedded bytes are borrowed for the lifetime of the image.
 * Raw decode/bytes pointers keep their original allocation and byte contract. */
static unsigned char *zan_embed_string_alloc(size_t len) {
    if (len > UINT32_MAX || len > SIZE_MAX - ZAN_OBJ_HDR_SIZE - 1) return NULL;
    uint64_t *raw = (uint64_t *)malloc(ZAN_OBJ_HDR_SIZE + len + 1);
    if (!raw) return NULL;
    raw[0] = ZAN_STRING_SENTINEL_RC;
    raw[1] = ZAN_STR_HDR_WORD(len);
    unsigned char *data = (unsigned char *)raw + ZAN_OBJ_HDR_SIZE;
    data[len] = 0;
    return data;
}

static const char *zan_embed_string_empty(void) {
    static const struct {
        uint64_t rc, word;
        char data[1];
    } empty = { ZAN_STRING_SENTINEL_RC, ZAN_STR_HDR_WORD(0), { 0 } };
    return empty.data;
}

/* The table owns immutable source buffers for the image lifetime. Cache views
 * by source/length so repeated reads do not allocate or invalidate old views. */
static const char *zan_embed_string_view(const unsigned char *source, uint64_t len) {
    typedef struct view {
        const unsigned char *source;
        uint64_t len;
        const char *text;
        struct view *next;
    } view;
    static view *views;
    static atomic_flag gate = ATOMIC_FLAG_INIT;
    if (len > UINT32_MAX || (!source && len)) return zan_embed_string_empty();
    while (atomic_flag_test_and_set_explicit(&gate, memory_order_acquire)) { }
    for (view *p = views; p; p = p->next) {
        if (p->source == source && p->len == len) {
            atomic_flag_clear_explicit(&gate, memory_order_release);
            return p->text;
        }
    }
    view *entry = (view *)malloc(sizeof(*entry));
    if (!entry) {
        atomic_flag_clear_explicit(&gate, memory_order_release);
        return zan_embed_string_empty();
    }
    unsigned char *text = zan_embed_string_alloc((size_t)len);
    if (!text) {
        free(entry);
        atomic_flag_clear_explicit(&gate, memory_order_release);
        return zan_embed_string_empty();
    }
    if (len) {
        memcpy(text, source, (size_t)len);
        const unsigned char *nul = (const unsigned char *)memchr(source, 0, (size_t)len);
        ((uint64_t *)text)[-1] = ZAN_STR_HDR_WORD(nul ? (size_t)(nul - source) : len);
    }
    entry->source = source;
    entry->len = len;
    entry->text = (const char *)text;
    entry->next = views;
    views = entry;
    atomic_flag_clear_explicit(&gate, memory_order_release);
    return entry->text;
}

#endif
