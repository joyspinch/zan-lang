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
#define ZAN_REFL_M_OBOOL      16   /* obj.GetFieldBool(name)     -> bool */

/* second layer: the method / constructor tables */
#define ZAN_REFL_M_MCOUNT     20   /* TypeInfo.MethodCount            -> int */
#define ZAN_REFL_M_MNAME      21   /* TypeInfo.GetMethodName(i)       -> string */
#define ZAN_REFL_M_MRET       22   /* TypeInfo.GetMethodReturnType(i) -> string */
#define ZAN_REFL_M_MPCOUNT    23   /* TypeInfo.GetMethodParamCount(i) -> int */
#define ZAN_REFL_M_MPTYPE     24   /* TypeInfo.GetMethodParamType(i,k)-> string */
#define ZAN_REFL_M_MHAS       25   /* TypeInfo.HasMethod(name)        -> bool */
#define ZAN_REFL_M_MSTATIC    26   /* TypeInfo.IsMethodStatic(i)      -> bool */
#define ZAN_REFL_M_CCOUNT     27   /* TypeInfo.ConstructorCount       -> int */
#define ZAN_REFL_M_CPCOUNT    28   /* TypeInfo.GetCtorParamCount(i)   -> int */
#define ZAN_REFL_M_CPTYPE     29   /* TypeInfo.GetCtorParamType(i,k)  -> string */
#define ZAN_REFL_M_CNEW       30   /* TypeInfo.CreateInstance(...)    -> object */

/* second layer: the shape of a type (array / nullable / interface / generic) */
#define ZAN_REFL_M_ISARRAY    31   /* TypeInfo.IsArray        -> bool */
#define ZAN_REFL_M_ISNULLABLE 32   /* TypeInfo.IsNullable     -> bool */
#define ZAN_REFL_M_ISIFACE    33   /* TypeInfo.IsInterface    -> bool */
#define ZAN_REFL_M_ISGENERIC  34   /* TypeInfo.IsGeneric      -> bool */
#define ZAN_REFL_M_ELEMTYPE   35   /* TypeInfo.ElementType    -> TypeInfo */
#define ZAN_REFL_M_TACOUNT    36   /* TypeInfo.TypeArgCount   -> int */
#define ZAN_REFL_M_TARG       37   /* TypeInfo.GetTypeArg(i)  -> TypeInfo */

/* second layer: writing a member, and calling one by name */
#define ZAN_REFL_M_SETINT     40   /* obj.SetFieldInt(name, v)    -> bool */
#define ZAN_REFL_M_SETLONG    41   /* obj.SetFieldLong(name, v)   -> bool */
#define ZAN_REFL_M_SETDOUBLE  42   /* obj.SetFieldDouble(name, v) -> bool */
#define ZAN_REFL_M_SETSTRING  43   /* obj.SetFieldString(name, v) -> bool */
#define ZAN_REFL_M_SETBOOL    44   /* obj.SetFieldBool(name, v)   -> bool */
#define ZAN_REFL_M_IVOID      45   /* obj.Invoke(name[, arg])       -> bool */
#define ZAN_REFL_M_IINT       46   /* obj.InvokeInt(name[, arg])    -> int */
#define ZAN_REFL_M_ILONG      47   /* obj.InvokeLong(name[, arg])   -> long */
#define ZAN_REFL_M_IDOUBLE    48   /* obj.InvokeDouble(name[, arg]) -> double */
#define ZAN_REFL_M_ISTRING    49   /* obj.InvokeString(name[, arg]) -> string */
#define ZAN_REFL_M_OMHAS      50   /* obj.HasMethod(name)           -> bool */

static inline bool zan_refl_is_typeinfo(const zan_type_t *t) {
    return t && t->kind == TYPE_STRUCT && t->name.len == 8 &&
           memcmp(t->name.str, "TypeInfo", 8) == 0;
}

/* Members read off a TypeInfo value. */
static inline bool zan_refl_typeinfo_member(zan_istr_t n, int *out) {
    static const struct { const char *name; int len; int code; } tbl[] = {
        { "Name",              4,  ZAN_REFL_M_NAME },
        { "Kind",              4,  ZAN_REFL_M_KIND },
        { "FieldCount",       10,  ZAN_REFL_M_FCOUNT },
        { "GetFieldName",     12,  ZAN_REFL_M_FNAME },
        { "GetFieldType",     12,  ZAN_REFL_M_FTYPE },
        { "HasField",          8,  ZAN_REFL_M_HAS },
        { "GetMemberValue",   14,  ZAN_REFL_M_MVALUE },
        { "MethodCount",      11,  ZAN_REFL_M_MCOUNT },
        { "GetMethodName",    13,  ZAN_REFL_M_MNAME },
        { "GetMethodReturnType", 19, ZAN_REFL_M_MRET },
        { "GetMethodParamCount", 19, ZAN_REFL_M_MPCOUNT },
        { "GetMethodParamType",  18, ZAN_REFL_M_MPTYPE },
        { "HasMethod",         9,  ZAN_REFL_M_MHAS },
        { "IsMethodStatic",   14,  ZAN_REFL_M_MSTATIC },
        { "ConstructorCount", 16,  ZAN_REFL_M_CCOUNT },
        { "GetCtorParamCount",17,  ZAN_REFL_M_CPCOUNT },
        { "GetCtorParamType", 16,  ZAN_REFL_M_CPTYPE },
        { "CreateInstance",   14,  ZAN_REFL_M_CNEW },
        { "IsArray",           7,  ZAN_REFL_M_ISARRAY },
        { "IsNullable",       10,  ZAN_REFL_M_ISNULLABLE },
        { "IsInterface",      11,  ZAN_REFL_M_ISIFACE },
        { "IsGeneric",         9,  ZAN_REFL_M_ISGENERIC },
        { "ElementType",      11,  ZAN_REFL_M_ELEMTYPE },
        { "TypeArgCount",     12,  ZAN_REFL_M_TACOUNT },
        { "GetTypeArg",       10,  ZAN_REFL_M_TARG },
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
        { "GetFieldBool",   12, ZAN_REFL_M_OBOOL },
        { "SetFieldInt",    11, ZAN_REFL_M_SETINT },
        { "SetFieldLong",   12, ZAN_REFL_M_SETLONG },
        { "SetFieldDouble", 14, ZAN_REFL_M_SETDOUBLE },
        { "SetFieldString", 14, ZAN_REFL_M_SETSTRING },
        { "SetFieldBool",   12, ZAN_REFL_M_SETBOOL },
        { "Invoke",          6, ZAN_REFL_M_IVOID },
        { "InvokeInt",       9, ZAN_REFL_M_IINT },
        { "InvokeLong",     10, ZAN_REFL_M_ILONG },
        { "InvokeDouble",   12, ZAN_REFL_M_IDOUBLE },
        { "InvokeString",   12, ZAN_REFL_M_ISTRING },
        { "HasMethod",       9, ZAN_REFL_M_OMHAS },
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
    case ZAN_REFL_M_OBOOL:
    case ZAN_REFL_M_MNAME:
    case ZAN_REFL_M_MRET:
    case ZAN_REFL_M_MPCOUNT:
    case ZAN_REFL_M_MPTYPE:
    case ZAN_REFL_M_MHAS:
    case ZAN_REFL_M_MSTATIC:
    case ZAN_REFL_M_CPCOUNT:
    case ZAN_REFL_M_CPTYPE:
    case ZAN_REFL_M_TARG:
    case ZAN_REFL_M_SETINT:
    case ZAN_REFL_M_SETLONG:
    case ZAN_REFL_M_SETDOUBLE:
    case ZAN_REFL_M_SETSTRING:
    case ZAN_REFL_M_SETBOOL:
    case ZAN_REFL_M_IVOID:
    case ZAN_REFL_M_IINT:
    case ZAN_REFL_M_ILONG:
    case ZAN_REFL_M_IDOUBLE:
    case ZAN_REFL_M_ISTRING:
    case ZAN_REFL_M_OMHAS:
        return true;
    default:
        return false;
    }
}

/* How many arguments the member accepts at most: two for `(index, index)` and
 * `(name, value)` shaped members, one for the `(name)` / `(index)` ones. A
 * constructor call takes as many as the constructor has parameters, capped by
 * what the reflection call path can pack. */
#define ZAN_REFL_MAX_ARGS 2

static inline int zan_refl_member_max_args(int code) {
    switch (code) {
    case ZAN_REFL_M_MPTYPE:
    case ZAN_REFL_M_CPTYPE:
    case ZAN_REFL_M_SETINT:
    case ZAN_REFL_M_SETLONG:
    case ZAN_REFL_M_SETDOUBLE:
    case ZAN_REFL_M_SETSTRING:
    case ZAN_REFL_M_SETBOOL:
    case ZAN_REFL_M_IVOID:
    case ZAN_REFL_M_IINT:
    case ZAN_REFL_M_ILONG:
    case ZAN_REFL_M_IDOUBLE:
    case ZAN_REFL_M_ISTRING:
    case ZAN_REFL_M_CNEW:
        return 2;
    default:
        return zan_refl_member_takes_arg(code) ? 1 : 0;
    }
}

static inline zan_type_t *zan_refl_result_type(zan_binder_t *b, int code) {
    switch (code) {
    case ZAN_REFL_M_NAME:
    case ZAN_REFL_M_KIND:
    case ZAN_REFL_M_FNAME:
    case ZAN_REFL_M_FTYPE:
    case ZAN_REFL_M_OSTRING:
    case ZAN_REFL_M_MNAME:
    case ZAN_REFL_M_MRET:
    case ZAN_REFL_M_MPTYPE:
    case ZAN_REFL_M_CPTYPE:
    case ZAN_REFL_M_ISTRING: return b->type_string;
    case ZAN_REFL_M_FCOUNT:
    case ZAN_REFL_M_OINT:
    case ZAN_REFL_M_MCOUNT:
    case ZAN_REFL_M_MPCOUNT:
    case ZAN_REFL_M_CCOUNT:
    case ZAN_REFL_M_CPCOUNT:
    case ZAN_REFL_M_TACOUNT:
    case ZAN_REFL_M_IINT:    return b->type_int;
    case ZAN_REFL_M_HAS:
    case ZAN_REFL_M_OHAS:
    case ZAN_REFL_M_OBOOL:
    case ZAN_REFL_M_MHAS:
    case ZAN_REFL_M_MSTATIC:
    case ZAN_REFL_M_ISARRAY:
    case ZAN_REFL_M_ISNULLABLE:
    case ZAN_REFL_M_ISIFACE:
    case ZAN_REFL_M_ISGENERIC:
    case ZAN_REFL_M_SETINT:
    case ZAN_REFL_M_SETLONG:
    case ZAN_REFL_M_SETDOUBLE:
    case ZAN_REFL_M_SETSTRING:
    case ZAN_REFL_M_SETBOOL:
    case ZAN_REFL_M_IVOID:
    case ZAN_REFL_M_OMHAS:   return b->type_bool;
    case ZAN_REFL_M_ODOUBLE:
    case ZAN_REFL_M_IDOUBLE: return b->type_double;
    case ZAN_REFL_M_GETTYPE:
    case ZAN_REFL_M_ELEMTYPE:
    case ZAN_REFL_M_TARG:    return b->type_typeinfo;
    case ZAN_REFL_M_CNEW:    return b->type_object;
    default:                 return b->type_long;
    }
}

/* Builtin collections are lowered by their own builtin tables, not through the
 * reflection records, so they are not reflectable receivers. */
static inline bool zan_refl_is_reflectable(const zan_type_t *t) {
    static const char *builtins[] = { "List", "Dict", "StringBuilder", "Span",
                                      "Task", "Grouping" };
    if (!t) return false;
    /* `object` is reflectable too: it is what CreateInstance() hands back, and
     * the readers resolve the concrete type off the instance anyway. */
    if (t->kind == TYPE_OBJECT) return true;
    if (t->kind != TYPE_CLASS && t->kind != TYPE_STRUCT) return false;
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
