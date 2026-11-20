#ifndef ZAN_SEMANTIC_REFS_H
#define ZAN_SEMANTIC_REFS_H

#include "checker.h"
#include "host_oom.h"
#include <stdlib.h>

/* The checker interns names in the arena. Consume the results before freeing it. */
typedef struct {
    zan_istr_t name;
    bool is_parameter;
    zan_loc_t declaration;
    zan_loc_t occurrence;
} semantic_ref_t;

typedef struct {
    uint32_t file_id;
    semantic_ref_t *items;
    size_t count;
    size_t capacity;
} semantic_refs_t;

static void semantic_refs_record(void *context, zan_istr_t name,
                                 bool is_parameter, zan_loc_t declaration,
                                 zan_loc_t occurrence) {
    semantic_refs_t *refs = context;
    if (occurrence.file_id != refs->file_id || declaration.file_id != refs->file_id) return;
    if (refs->count == refs->capacity) {
        if (refs->capacity > SIZE_MAX / (2 * sizeof(*refs->items))) zan_host_oom();
        refs->capacity = refs->capacity ? 2 * refs->capacity : 128;
        refs->items = realloc(refs->items, refs->capacity * sizeof(*refs->items));
        if (!refs->items) zan_host_oom();
    }
    refs->items[refs->count++] = (semantic_ref_t){name, is_parameter, declaration, occurrence};
}

static int semantic_ref_source_order(const void *left, const void *right) {
    uint32_t a = ((const semantic_ref_t *)left)->occurrence.offset;
    uint32_t b = ((const semantic_ref_t *)right)->occurrence.offset;
    return (a > b) - (a < b);
}

static bool semantic_ref_same_symbol(const semantic_ref_t *a, const semantic_ref_t *b) {
    return a->declaration.file_id == b->declaration.file_id &&
           a->declaration.offset == b->declaration.offset &&
           a->is_parameter == b->is_parameter;
}

static void semantic_refs_sort(semantic_refs_t *refs) {
    if (refs->count > 1)
        qsort(refs->items, refs->count, sizeof(*refs->items), semantic_ref_source_order);
}

static const semantic_ref_t *semantic_refs_select(const semantic_refs_t *refs,
                                                 uint32_t line, uint32_t col,
                                                 bool *ambiguous) {
    const semantic_ref_t *symbol = NULL;
    for (size_t i = 0; i < refs->count; i++) {
        const semantic_ref_t *ref = &refs->items[i];
        if (ref->occurrence.line != line || col < ref->occurrence.col ||
            col - ref->occurrence.col >= ref->name.len) continue;
        if (symbol && !semantic_ref_same_symbol(symbol, ref)) {
            if (ambiguous) *ambiguous = true;
            return NULL;
        }
        symbol = ref;
    }
    return symbol;
}

/* Advance over one source token. Specialization and repeated checking can
 * report it more than once. */
static const semantic_ref_t *semantic_refs_next(const semantic_refs_t *refs,
                                                const semantic_ref_t *symbol,
                                                size_t *index, bool *ambiguous) {
    const semantic_ref_t *ref = &refs->items[(*index)++];
    bool match = semantic_ref_same_symbol(symbol, ref);
    bool other = !match;
    while (*index < refs->count && refs->items[*index].occurrence.offset == ref->occurrence.offset) {
        bool same = semantic_ref_same_symbol(symbol, &refs->items[(*index)++]);
        match |= same;
        other |= !same;
    }
    if (match && other) *ambiguous = true;
    return match && !other ? ref : NULL;
}

#endif
