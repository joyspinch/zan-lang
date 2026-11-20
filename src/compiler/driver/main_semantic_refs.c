/* Included by main.c after source path handling. */
#include "json.h"

typedef struct {
    zan_istr_t name;
    bool is_parameter;
    zan_loc_t declaration;
    zan_loc_t occurrence;
} semantic_ref_t;

typedef struct {
    const char *file;
    uint32_t file_id;
    semantic_ref_t *items;
    size_t count;
    size_t capacity;
} semantic_refs_t;

static bool semantic_refs_find_file(semantic_refs_t *refs, const zan_diag_t *diag,
                                    const char *file) {
    char key[4096];
    canon_key(file, key, sizeof(key));
    for (int i = 0; i < diag->file_count; i++) {
        if (!diag->file_sources[i]) continue;
        char candidate[4096];
        canon_key(diag->file_names[i], candidate, sizeof(candidate));
        if (strcmp(key, candidate) != 0) continue;
        if (refs->file) return false;
        refs->file = diag->file_names[i];
        refs->file_id = (uint32_t)i;
    }
    return refs->file != NULL;
}

static void semantic_refs_record(void *context, zan_istr_t name,
                                 bool is_parameter, zan_loc_t declaration,
                                 zan_loc_t occurrence) {
    semantic_refs_t *refs = context;
    if (occurrence.file_id != refs->file_id) return;
    if (refs->count == refs->capacity) {
        if (refs->capacity > SIZE_MAX / (2 * sizeof(*refs->items))) zan_host_oom();
        refs->capacity = refs->capacity ? 2 * refs->capacity : 128;
        refs->items = realloc(refs->items, refs->capacity * sizeof(*refs->items));
    }
    refs->items[refs->count++] = (semantic_ref_t){name, is_parameter, declaration, occurrence};
}

static int semantic_ref_source_order(const void *left, const void *right) {
    uint32_t a = ((const semantic_ref_t *)left)->occurrence.offset;
    uint32_t b = ((const semantic_ref_t *)right)->occurrence.offset;
    return (a > b) - (a < b);
}

static bool semantic_ref_same_symbol(const semantic_ref_t *a, const semantic_ref_t *b) {
    return a->declaration.offset == b->declaration.offset &&
           a->is_parameter == b->is_parameter;
}

static const semantic_ref_t *semantic_refs_select(const semantic_refs_t *refs,
                                                  uint32_t line, uint32_t col) {
    const semantic_ref_t *symbol = NULL;
    for (size_t i = 0; i < refs->count; i++) {
        const semantic_ref_t *ref = &refs->items[i];
        if (ref->occurrence.line != line || col < ref->occurrence.col ||
            col - ref->occurrence.col >= ref->name.len) continue;
        if (symbol && !semantic_ref_same_symbol(symbol, ref)) return NULL;
        symbol = ref;
    }
    return symbol;
}

static json_value *semantic_ref_json_location(const char *file, zan_loc_t loc,
                                              uint32_t length) {
    json_value *value = json_new_obj();
    json_obj_set(value, "file", json_new_str(file));
    json_obj_set(value, "line", json_new_num(loc.line));
    json_obj_set(value, "col", json_new_num(loc.col));
    json_obj_set(value, "length", json_new_num(length));
    return value;
}

static int semantic_refs_print(semantic_refs_t *refs, uint32_t line, uint32_t col) {
    if (refs->count > 1)
        qsort(refs->items, refs->count, sizeof(*refs->items), semantic_ref_source_order);

    const semantic_ref_t *symbol = semantic_refs_select(refs, line, col);
    json_value *result = json_new_obj();
    json_value *locations = json_new_arr();
    json_obj_set(result, "format", json_new_str("zan.semantic-refs.v1"));
    if (symbol) {
        json_value *value = json_new_obj();
        json_obj_set(value, "kind", json_new_str(symbol->is_parameter ? "parameter" : "local"));
        json_obj_set(value, "name", json_new_str(symbol->name.str));
        json_obj_set(value, "declaration", semantic_ref_json_location(
            refs->file, symbol->declaration, symbol->name.len));
        json_obj_set(result, "symbol", value);

        for (size_t i = 0; i < refs->count;) {
            const semantic_ref_t *ref = &refs->items[i++];
            bool matches = semantic_ref_same_symbol(symbol, ref);
            /* Rechecking an expression or specializing a generic base can
             * produce several observations of the same source token. */
            while (i < refs->count && refs->items[i].occurrence.offset == ref->occurrence.offset) {
                if (!semantic_ref_same_symbol(symbol, &refs->items[i])) matches = false;
                i++;
            }
            if (matches && ref->occurrence.offset != symbol->declaration.offset)
                json_arr_add(locations, semantic_ref_json_location(
                    refs->file, ref->occurrence, ref->name.len));
        }
    } else {
        json_obj_set(result, "symbol", json_new_null());
    }
    json_obj_set(result, "references", locations);
    char *wire = json_serialize(result);
    int rc = puts(wire) == EOF || fflush(stdout) == EOF;
    free(wire);
    json_free(result);
    return rc;
}
