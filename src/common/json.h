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
        struct { char **keys; json_value **vals; int count; int cap;
                 int *index; int index_cap; } obj;
    } as;
};

/* ---- parsing ---- */

json_value *json_parse(const char *text);

void json_free(json_value *v);


json_value *json_obj_get(const json_value *obj, const char *key);
const char *json_get_str(const json_value *v);
double      json_get_num(const json_value *v, double def);
bool        json_get_bool(const json_value *v, bool def);
int         json_arr_count(const json_value *v);
json_value *json_arr_at(const json_value *v, int index);
bool        json_is(const json_value *v, json_type_t type);

json_value *json_path(const json_value *root, const char *dotted_path);

/* ---- construction ---- */

json_value *json_new_null(void);
json_value *json_new_bool(bool b);
json_value *json_new_num(double n);
json_value *json_new_str(const char *s);   /* copies s */
json_value *json_new_obj(void);
json_value *json_new_arr(void);

void json_obj_set(json_value *obj, const char *key, json_value *val);
void json_arr_add(json_value *arr, json_value *val);

char *json_serialize(const json_value *v);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_JSON_H */
