/* Included by main.c after source path handling. */
#include "json.h"
#include "semantic_refs.h"

typedef struct {
    const char *file;
    semantic_refs_t refs;
} semantic_query_t;

static bool semantic_refs_find_file(semantic_query_t *query, const zan_diag_t *diag,
                                    const char *file) {
    char key[4096];
    canon_key(file, key, sizeof(key));
    for (int i = 0; i < diag->file_count; i++) {
        if (!diag->file_sources[i]) continue;
        char candidate[4096];
        canon_key(diag->file_names[i], candidate, sizeof(candidate));
        if (strcmp(key, candidate) != 0) continue;
        if (query->file) return false;
        query->file = diag->file_names[i];
        query->refs.file_id = (uint32_t)i;
    }
    return query->file != NULL;
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

static const char *semantic_ref_kind_name(zan_sym_kind_t kind) {
    switch (kind) {
    case SYM_CLASS: return "class";
    case SYM_STRUCT: return "struct";
    case SYM_INTERFACE: return "interface";
    case SYM_ENUM: return "enum";
    case SYM_METHOD: return "method";
    case SYM_CONSTRUCTOR: return "constructor";
    case SYM_FIELD: return "field";
    case SYM_PROPERTY: return "property";
    case SYM_PARAM: return "parameter";
    case SYM_LOCAL: return "local";
    case SYM_ENUM_MEMBER: return "enum-member";
    case SYM_TYPE_PARAM: return "type-parameter";
    case SYM_DELEGATE: return "delegate";
    case SYM_NAMESPACE: return "namespace";
    }
    return "unknown";
}

static int semantic_refs_print(semantic_query_t *query, const zan_diag_t *diag,
                               uint32_t line, uint32_t col) {
    semantic_refs_t *refs = &query->refs;
    semantic_refs_sort(refs);
    const semantic_ref_t *symbol = semantic_refs_select(refs, line, col, NULL);
    json_value *result = json_new_obj();
    json_value *locations = json_new_arr();
    json_obj_set(result, "format", json_new_str("zan.semantic-refs.v1"));
    if (symbol) {
        json_value *value = json_new_obj();
        json_obj_set(value, "kind", json_new_str(semantic_ref_kind_name(symbol->kind)));
        json_obj_set(value, "name", json_new_str(symbol->name.str));
        json_obj_set(value, "declaration", semantic_ref_json_location(
            diag->file_names[symbol->declaration.file_id], symbol->declaration,
            symbol->declaration_length));
        json_obj_set(result, "symbol", value);

        bool ambiguous = false;
        json_value *parts = NULL;
        for (size_t i = 0; i < refs->count;) {
            const semantic_ref_t *ref = semantic_refs_next(refs, symbol, &i, &ambiguous);
            if (!ref || semantic_ref_same_location(ref->occurrence, ref->declaration)) continue;
            json_value *location = semantic_ref_json_location(diag->file_names[ref->occurrence.file_id],
                                                              ref->occurrence, ref->occurrence_length);
            if (ref->is_declaration) {
                if (!parts) parts = json_new_arr();
                json_arr_add(parts, location);
            } else {
                json_arr_add(locations, location);
            }
        }
        if (parts) json_obj_set(value, "additionalDeclarations", parts);
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
