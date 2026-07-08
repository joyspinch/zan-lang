/* json.h -- Minimal dependency-free JSON parser and writer.
 *
 * Used by the Zan language server (LSP) and debug adapter (DAP) to speak
 * their JSON-RPC based protocols. Supports the full JSON grammar with a
 * simple owned-tree representation.
 */
#ifndef ZAN_JSON_H
#define ZAN_JSON_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUM,
    JSON_STR,
    JSON_ARR,
    JSON_OBJ
} json_type_t;

typedef struct json_value json_value;

struct json_value {
    json_type_t type;
    union {
        bool   b;
        double num;
        char  *str;                 /* owned, NUL-terminated */
        struct { json_value **items; int count; int cap; } arr;
        struct { char **keys; json_value **vals; int count; int cap; } obj;
    } as;
};

/* ---- parsing ---- */

/* Parse a NUL-terminated JSON document. Returns NULL on syntax error.
 * The returned tree must be released with json_free(). */
json_value *json_parse(const char *text);

/* Free a value tree (safe on NULL). */
void json_free(json_value *v);

/* ---- accessors (all NULL/def tolerant) ---- */

json_value *json_obj_get(const json_value *obj, const char *key);
const char *json_get_str(const json_value *v);         /* NULL if not string */
double      json_get_num(const json_value *v, double def);
bool        json_get_bool(const json_value *v, bool def);
int         json_arr_count(const json_value *v);
json_value *json_arr_at(const json_value *v, int index);
bool        json_is(const json_value *v, json_type_t type);

/* Convenience: obj.key traversal returning the leaf value or NULL. */
json_value *json_path(const json_value *root, const char *dotted_path);

/* ---- construction ---- */

json_value *json_new_null(void);
json_value *json_new_bool(bool b);
json_value *json_new_num(double n);
json_value *json_new_str(const char *s);   /* copies s */
json_value *json_new_obj(void);
json_value *json_new_arr(void);

/* Takes ownership of `val`. */
void json_obj_set(json_value *obj, const char *key, json_value *val);
void json_arr_add(json_value *arr, json_value *val);

/* Serialize to a freshly malloc'd NUL-terminated string (caller frees). */
char *json_serialize(const json_value *v);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_JSON_H */
