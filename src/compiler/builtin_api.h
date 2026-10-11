/* builtin_api */

#ifndef ZAN_BUILTIN_API_H
#define ZAN_BUILTIN_API_H

typedef struct {
    const char *name;
    char kind;        /* 'M' method, 'P' property, 'F' field */
    const char *sig;
} zan_builtin_member_t;

typedef struct {
    const char *type;
    const char *name_public;
    const char *display;
    int is_static;
    const zan_builtin_member_t *members;
    int member_count;
} zan_builtin_type_t;

const zan_builtin_type_t *zan_builtin_types(int *count);

const zan_builtin_type_t *zan_builtin_find(const char *type);

/* 检查是否`type` has a member named `name` (length-delimited, not NUL-terminated) */
int zan_builtin_has_member(const char *type, const char *name, int name_len);

char zan_builtin_member_kind(const char *type, const char *name, int name_len);

const char *zan_builtin_member_result(const char *type, const char *name,
                                      int name_len);

#endif /* ZAN_BUILTIN_API_H */
