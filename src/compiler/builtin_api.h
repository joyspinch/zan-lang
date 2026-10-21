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

/* 检查「编译器原生实现的成员」：这些成员不在任何成员表里，实现完全在
 * 编译器里（按「类型名 + 成员名」分派）。判定「某类型上有没有这个方法」
 * 时必须把它们算进去——成员表里查不到 ≠ 方法不存在。
 * 长度受限、不是 NUL 结尾（类型名可能是用户类型，不能按 C 字符串比）。 */
int zan_builtin_has_native_member(const char *type, int type_len,
                                   const char *name, int name_len);

char zan_builtin_member_kind(const char *type, const char *name, int name_len);

const char *zan_builtin_member_result(const char *type, const char *name,
                                      int name_len);

/* 取内建方法的形参类型名列表（签名的括号里，按逗号分隔）。
 * 返回形参个数；out[] 里是各形参的类型名（"T" / "int" / "List<T>" …）。
 * 非方法成员或无括号签名返回 0。 */
int zan_builtin_member_params(const char *type, const char *name, int name_len,
                              const char **out, int max_out);

#endif /* ZAN_BUILTIN_API_H */
