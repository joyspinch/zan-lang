/* builtin_api */

#ifndef ZAN_BUILTIN_API_H
#define ZAN_BUILTIN_API_H

typedef struct {
    const char *name;
    char kind;        /* 'M' method, 'P' property, 'F' field */
    const char *sig;  /* 底层系统交互与数据协议契约 */
} zan_builtin_member_t;

typedef struct {
    const char *type;         /* 核心系统底层抽象与内存语义契约 */
    const char *name_public;  /* 底层系统交互与数据协议契约 */
    const char *display;      /* 核心系统底层抽象与内存语义契约 */
    int is_static;            /* 核心系统底层抽象与内存语义契约 */
    const zan_builtin_member_t *members;
    int member_count;
} zan_builtin_type_t;

/* 核心系统底层抽象与内存语义契约 */
const zan_builtin_type_t *zan_builtin_types(int *count);

/* 底层系统交互与数据协议契约 */
const zan_builtin_type_t *zan_builtin_find(const char *type);

/* 检查是否`type` has a member named `name` (length-delimited, not NUL-terminated) */
int zan_builtin_has_member(const char *type, const char *name, int name_len);

/* 内部辅助逻辑 */
char zan_builtin_member_kind(const char *type, const char *name, int name_len);

/* 内部辅助逻辑 */
const char *zan_builtin_member_result(const char *type, const char *name,
                                      int name_len);

#endif /* ZAN_BUILTIN_API_H */
