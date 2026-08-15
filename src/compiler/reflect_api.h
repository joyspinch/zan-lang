/* reflect_api.h -- the reflection member surface, shared by the checker and
 * the IR generator so both agree on which names exist and what they yield.
 * The lowering lives in src/compiler/irgen_reflect.c, which also documents the
 * type-record layout. */
#ifndef ZAN_REFLECT_API_H
#define ZAN_REFLECT_API_H

#include <string.h>
#include "binder.h"

/* member codes; the lowering switches on these */
#define ZAN_REFL_M_NAME        1   /* TypeInfo.Name              -> string */
#define ZAN_REFL_M_KIND        2   /* TypeInfo.Kind              -> string */
#define ZAN_REFL_M_FCOUNT      3   /* TypeInfo.FieldCount        -> int */
#define ZAN_REFL_M_FNAME       4   /* TypeInfo.GetFieldName(i)   -> string */
#define ZAN_REFL_M_FTYPE       5   /* TypeInfo.GetFieldType(i)   -> string */
#define ZAN_REFL_M_HAS         6   /* TypeInfo.HasField(name)    -> bool */
#define ZAN_REFL_M_MVALUE      7   /* TypeInfo.GetMemberValue(i) -> long */
#define ZAN_REFL_M_GETTYPE    10   /* obj.GetType()              -> TypeInfo */
#define ZAN_REFL_M_OHAS       11   /* obj.HasField(name)         -> bool */
#define ZAN_REFL_M_OINT       12   /* obj.GetFieldInt(name)      -> int */
#define ZAN_REFL_M_OLONG      13   /* obj.GetFieldLong(name)     -> long */
#define ZAN_REFL_M_ODOUBLE    14   /* obj.GetFieldDouble(name)   -> double */
#define ZAN_REFL_M_OSTRING    15   /* obj.GetFieldString(name)   -> string */

static inline bool zan_refl_is_typeinfo(const zan_type_t *t) {
    return t && t->kind == TYPE_STRUCT && t->name.len == 8 &&
           memcmp(t->name.str, "TypeInfo", 8) == 0;
}

/* Members read off a TypeInfo value. */
static inline bool zan_refl_typeinfo_member(zan_istr_t n, int *out) {
    static const struct { const char *name; int len; int code; } tbl[] = {
        { "Name",           4,  ZAN_REFL_M_NAME },
        { "Kind",           4,  ZAN_REFL_M_KIND },
        { "FieldCount",    10,  ZAN_REFL_M_FCOUNT },
        { "GetFieldName",  12,  ZAN_REFL_M_FNAME },
        { "GetFieldType",  12,  ZAN_REFL_M_FTYPE },
        { "HasField",       8,  ZAN_REFL_M_HAS },
        { "GetMemberValue",14,  ZAN_REFL_M_MVALUE },
    };
    for (int i = 0; i < (int)(sizeof(tbl) / sizeof(tbl[0])); i++)
        if ((int)n.len == tbl[i].len &&
            memcmp(n.str, tbl[i].name, (size_t)tbl[i].len) == 0) {
            if (out) *out = tbl[i].code;
            return true;
        }
    return false;
}

/* Methods every class/struct value carries, unless it declares its own member
 * of that name (a user `GetType` wins). */
static inline bool zan_refl_instance_method(zan_istr_t n, int *out) {
    static const struct { const char *name; int len; int code; } tbl[] = {
        { "GetType",         7, ZAN_REFL_M_GETTYPE },
        { "HasField",        8, ZAN_REFL_M_OHAS },
        { "GetFieldInt",    11, ZAN_REFL_M_OINT },
        { "GetFieldLong",   12, ZAN_REFL_M_OLONG },
        { "GetFieldDouble", 14, ZAN_REFL_M_ODOUBLE },
        { "GetFieldString", 14, ZAN_REFL_M_OSTRING },
    };
    for (int i = 0; i < (int)(sizeof(tbl) / sizeof(tbl[0])); i++)
        if ((int)n.len == tbl[i].len &&
            memcmp(n.str, tbl[i].name, (size_t)tbl[i].len) == 0) {
            if (out) *out = tbl[i].code;
            return true;
        }
    return false;
}

/* True when the member takes an argument (and so is only valid as a call). */
static inline bool zan_refl_member_takes_arg(int code) {
    switch (code) {
    case ZAN_REFL_M_FNAME:
    case ZAN_REFL_M_FTYPE:
    case ZAN_REFL_M_HAS:
    case ZAN_REFL_M_MVALUE:
    case ZAN_REFL_M_OHAS:
    case ZAN_REFL_M_OINT:
    case ZAN_REFL_M_OLONG:
    case ZAN_REFL_M_ODOUBLE:
    case ZAN_REFL_M_OSTRING:
        return true;
    default:
        return false;
    }
}

static inline zan_type_t *zan_refl_result_type(zan_binder_t *b, int code) {
    switch (code) {
    case ZAN_REFL_M_NAME:
    case ZAN_REFL_M_KIND:
    case ZAN_REFL_M_FNAME:
    case ZAN_REFL_M_FTYPE:
    case ZAN_REFL_M_OSTRING: return b->type_string;
    case ZAN_REFL_M_FCOUNT:
    case ZAN_REFL_M_OINT:    return b->type_int;
    case ZAN_REFL_M_HAS:
    case ZAN_REFL_M_OHAS:    return b->type_bool;
    case ZAN_REFL_M_ODOUBLE: return b->type_double;
    case ZAN_REFL_M_GETTYPE: return b->type_typeinfo;
    default:                 return b->type_long;
    }
}

/* Builtin collections are lowered by their own builtin tables, not through the
 * reflection records, so they are not reflectable receivers. */
static inline bool zan_refl_is_reflectable(const zan_type_t *t) {
    static const char *builtins[] = { "List", "Dict", "StringBuilder", "Span",
                                      "Task", "Grouping" };
    if (!t || (t->kind != TYPE_CLASS && t->kind != TYPE_STRUCT)) return false;
    if (zan_refl_is_typeinfo(t)) return false;
    for (int i = 0; i < (int)(sizeof(builtins) / sizeof(builtins[0])); i++) {
        int len = (int)strlen(builtins[i]);
        if ((int)t->name.len == len &&
            memcmp(t->name.str, builtins[i], (size_t)len) == 0)
            return false;
    }
    return true;
}

/* The result type of `recv.<n>`, or NULL when it is not a reflection member. */
static inline zan_type_t *zan_refl_member_type(zan_binder_t *b,
                                              const zan_type_t *recv,
                                              zan_istr_t n) {
    int code = 0;
    if (zan_refl_is_typeinfo(recv)) {
        if (!zan_refl_typeinfo_member(n, &code)) return NULL;
        return zan_refl_result_type(b, code);
    }
    if (!zan_refl_is_reflectable(recv)) return NULL;
    if (!zan_refl_instance_method(n, &code)) return NULL;
    return zan_refl_result_type(b, code);
}

#endif /* ZAN_REFLECT_API_H */
