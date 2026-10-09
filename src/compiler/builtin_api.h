/* builtin_api */

#ifndef ZAN_BUILTIN_API_H
#define ZAN_BUILTIN_API_H

typedef struct {
    const char *name;
    char kind;        /* 'M' method, 'P' property, 'F' field */
    const char *sig;  /* display signature for completion / signature help */
} zan_builtin_member_t;

typedef struct {
    const char *type;         /* receiver type name as irgen sees it */
    const char *name_public;  /* name the language spells it with ("Dictionary") */
    const char *display;      /* name shown in diagnostics ("Dictionary<K,V>") */
    int is_static;            /* 1 = static class (Console.X), 0 = instance */
    const zan_builtin_member_t *members;
    int member_count;
} zan_builtin_type_t;

/* All built-in types, in declaration order. */
const zan_builtin_type_t *zan_builtin_types(int *count);

/* The entry for `type` (irgen's internal name, e */
const zan_builtin_type_t *zan_builtin_find(const char *type);

/* 检查是否`type` has a member named `name` (length-delimited, not NUL-terminated) */
int zan_builtin_has_member(const char *type, const char *name, int name_len);

/* 内部辅助逻辑 */
char zan_builtin_member_kind(const char *type, const char *name, int name_len);

/* 内部辅助逻辑 */
const char *zan_builtin_member_result(const char *type, const char *name,
                                      int name_len);

#endif /* ZAN_BUILTIN_API_H */
