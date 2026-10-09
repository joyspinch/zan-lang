/* 底层系统交互与数据协议契约 */
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
                 /* 底层系统交互与数据协议契约 */
                 int *index; int index_cap; } obj;
    } as;
};

/* ---- parsing ---- */

/* 底层系统交互与数据协议契约 */
json_value *json_parse(const char *text);

/* 核心系统底层抽象与内存语义契约 */
void json_free(json_value *v);

/* 核心系统底层抽象与内存语义契约 */

json_value *json_obj_get(const json_value *obj, const char *key);
const char *json_get_str(const json_value *v);         /* 核心系统底层抽象与内存语义契约 */
double      json_get_num(const json_value *v, double def);
bool        json_get_bool(const json_value *v, bool def);
int         json_arr_count(const json_value *v);
json_value *json_arr_at(const json_value *v, int index);
bool        json_is(const json_value *v, json_type_t type);

/* 底层系统交互与数据协议契约 */
json_value *json_path(const json_value *root, const char *dotted_path);

/* ---- construction ---- */

json_value *json_new_null(void);
json_value *json_new_bool(bool b);
json_value *json_new_num(double n);
json_value *json_new_str(const char *s);   /* copies s */
json_value *json_new_obj(void);
json_value *json_new_arr(void);

/* 核心系统底层抽象与内存语义契约 */
void json_obj_set(json_value *obj, const char *key, json_value *val);
void json_arr_add(json_value *arr, json_value *val);

/* 底层系统交互与数据协议契约 */
char *json_serialize(const json_value *v);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_JSON_H */
