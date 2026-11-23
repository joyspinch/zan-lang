#ifndef ZAN_REFLECT_API_H
#define ZAN_REFLECT_API_H

#include <string.h>
#include "binder.h"

#define ZAN_REFL_M_NAME 1     /* TypeInfo.Name              -> string */
#define ZAN_REFL_M_KIND 2     /* TypeInfo.Kind              -> string */
#define ZAN_REFL_M_FCOUNT 3   /* TypeInfo.FieldCount        -> int */
#define ZAN_REFL_M_FNAME 4    /* TypeInfo.GetFieldName(i)   -> string */
#define ZAN_REFL_M_FTYPE 5    /* TypeInfo.GetFieldType(i)   -> string */
#define ZAN_REFL_M_HAS 6      /* TypeInfo.HasField(name)    -> bool */
#define ZAN_REFL_M_MVALUE 7   /* TypeInfo.GetMemberValue(i) -> long */
#define ZAN_REFL_M_GETTYPE 10 /* obj.GetType()              -> TypeInfo */
#define ZAN_REFL_M_OHAS 11    /* obj.HasField(name)         -> bool */
#define ZAN_REFL_M_OINT 12    /* obj.GetFieldInt(name)      -> int */
#define ZAN_REFL_M_OLONG 13   /* obj.GetFieldLong(name)     -> long */
#define ZAN_REFL_M_ODOUBLE 14 /* obj.GetFieldDouble(name)   -> double */
#define ZAN_REFL_M_OSTRING 15 /* obj.GetFieldString(name)   -> string */
#define ZAN_REFL_M_OBOOL 16   /* obj.GetFieldBool(name)     -> bool */
#define ZAN_REFL_M_OOBJECT 17 /* obj.GetFieldObject(name)   -> object */

#define ZAN_REFL_M_MCOUNT 20  /* TypeInfo.MethodCount            -> int */
#define ZAN_REFL_M_MNAME 21   /* TypeInfo.GetMethodName(i)       -> string */
#define ZAN_REFL_M_MRET 22    /* TypeInfo.GetMethodReturnType(i) -> string */
#define ZAN_REFL_M_MPCOUNT 23 /* TypeInfo.GetMethodParamCount(i) -> int */
#define ZAN_REFL_M_MPTYPE 24  /* TypeInfo.GetMethodParamType(i,k)-> string */
#define ZAN_REFL_M_MHAS 25    /* TypeInfo.HasMethod(name)        -> bool */
#define ZAN_REFL_M_MSTATIC 26 /* TypeInfo.IsMethodStatic(i)      -> bool */
#define ZAN_REFL_M_CCOUNT 27  /* TypeInfo.ConstructorCount       -> int */
#define ZAN_REFL_M_CPCOUNT 28 /* TypeInfo.GetCtorParamCount(i)   -> int */
#define ZAN_REFL_M_CPTYPE 29  /* TypeInfo.GetCtorParamType(i,k)  -> string */
#define ZAN_REFL_M_CNEW 30    /* TypeInfo.CreateInstance(...)    -> object */

#define ZAN_REFL_M_ISARRAY 31    /* TypeInfo.IsArray        -> bool */
#define ZAN_REFL_M_ISNULLABLE 32 /* TypeInfo.IsNullable     -> bool */
#define ZAN_REFL_M_ISIFACE 33    /* TypeInfo.IsInterface    -> bool */
#define ZAN_REFL_M_ISGENERIC 34  /* TypeInfo.IsGeneric      -> bool */
#define ZAN_REFL_M_ELEMTYPE 35   /* TypeInfo.ElementType    -> TypeInfo */
#define ZAN_REFL_M_TACOUNT 36    /* TypeInfo.TypeArgCount   -> int */
#define ZAN_REFL_M_TARG 37       /* TypeInfo.GetTypeArg(i)  -> TypeInfo */
#define ZAN_REFL_M_DECLATTRS                                                   \
  38 /* TypeInfo.GetDeclaredAttributeData() -> string */
#define ZAN_REFL_M_FDECLATTRS                                                  \
  39 /* TypeInfo.GetFieldAttributeData(i) -> string */

#define ZAN_REFL_M_SETINT 40    /* obj.SetFieldInt(name, v)    -> bool */
#define ZAN_REFL_M_SETLONG 41   /* obj.SetFieldLong(name, v)   -> bool */
#define ZAN_REFL_M_SETDOUBLE 42 /* obj.SetFieldDouble(name, v) -> bool */
#define ZAN_REFL_M_SETSTRING 43 /* obj.SetFieldString(name, v) -> bool */
#define ZAN_REFL_M_SETBOOL 44   /* obj.SetFieldBool(name, v)   -> bool */
#define ZAN_REFL_M_IVOID 45     /* obj.Invoke(name[, arg])       -> bool */
#define ZAN_REFL_M_IINT 46      /* obj.InvokeInt(name[, arg])    -> int */
#define ZAN_REFL_M_ILONG 47     /* obj.InvokeLong(name[, arg])   -> long */
#define ZAN_REFL_M_IDOUBLE 48   /* obj.InvokeDouble(name[, arg]) -> double */
#define ZAN_REFL_M_ISTRING 49   /* obj.InvokeString(name[, arg]) -> string */
#define ZAN_REFL_M_OMHAS 50     /* obj.HasMethod(name)           -> bool */
#define ZAN_REFL_M_FATTRS                                                      \
  51 /* TypeInfo.GetFieldCustomAttributes(i) -> string */
#define ZAN_REFL_M_TATTRS                                                      \
  52 /* TypeInfo.GetCustomAttributes()       -> string */
#define ZAN_REFL_M_MDECLATTRS                                                  \
  66 /* TypeInfo.GetMethodAttributeData(i) -> string */
#define ZAN_REFL_M_MATTRS                                                      \
  60 /* TypeInfo.GetMethodCustomAttributes(i) -> string */
#define ZAN_REFL_M_FULLNAME                                                    \
  61 /* TypeInfo.FullName       -> string（B-ID107） */
#define ZAN_REFL_M_SETOBJECT 57 /* obj.SetFieldObject(name, v)    -> bool */
#define ZAN_REFL_M_FTYPEINFO                                                   \
  58 /* TypeInfo.GetFieldTypeInfo(i)   -> TypeInfo                             \
      */
#define ZAN_REFL_M_ITASK                                                       \
  63 /* obj.InvokeTask(name[, arg])    -> long (task token) */
#define ZAN_REFL_M_IOBJECT 62 /* obj.InvokeObject(name[, arg])  -> object */
#define ZAN_REFL_M_FFIND 67      /* TypeInfo.FindField(name)       -> int */
#define ZAN_REFL_M_MFIND 68      /* TypeInfo.FindMethod(name)      -> int */
#define ZAN_REFL_M_OINT_AT 69    /* obj.GetFieldIntAt(idx)         -> int */
#define ZAN_REFL_M_OLONG_AT 70   /* obj.GetFieldLongAt(idx)        -> long */
#define ZAN_REFL_M_ODOUBLE_AT 71 /* obj.GetFieldDoubleAt(idx)      -> double */
#define ZAN_REFL_M_OSTRING_AT 72 /* obj.GetFieldStringAt(idx)      -> string */
#define ZAN_REFL_M_OBOOL_AT 73   /* obj.GetFieldBoolAt(idx)        -> bool */
#define ZAN_REFL_M_OOBJECT_AT 74 /* obj.GetFieldObjectAt(idx)      -> object */
#define ZAN_REFL_M_SETINT_AT 75  /* obj.SetFieldIntAt(idx, v)      -> bool */
#define ZAN_REFL_M_SETLONG_AT 76 /* obj.SetFieldLongAt(idx, v)     -> bool */
#define ZAN_REFL_M_SETDOUBLE_AT 77 /* obj.SetFieldDoubleAt(idx, v) -> bool */
#define ZAN_REFL_M_SETSTRING_AT 78 /* obj.SetFieldStringAt(idx, v) -> bool */
#define ZAN_REFL_M_SETBOOL_AT 79 /* obj.SetFieldBoolAt(idx, v)     -> bool */
#define ZAN_REFL_M_SETOBJECT_AT 80 /* obj.SetFieldObjectAt(idx, v) -> bool */
#define ZAN_REFL_M_IVOID_AT 81   /* obj.InvokeAt(idx[, arg])       -> bool */
#define ZAN_REFL_M_IINT_AT 82    /* obj.InvokeIntAt(idx[, arg])    -> int */
#define ZAN_REFL_M_ILONG_AT 83   /* obj.InvokeLongAt(idx[, arg])   -> long */
#define ZAN_REFL_M_IDOUBLE_AT 84 /* obj.InvokeDoubleAt(idx[, arg]) -> double */
#define ZAN_REFL_M_ISTRING_AT 85 /* obj.InvokeStringAt(idx[, arg]) -> string */
#define ZAN_REFL_M_IOBJECT_AT 86 /* obj.InvokeObjectAt(idx[, arg]) -> object */
#define ZAN_REFL_M_ITASK_AT 87   /* obj.InvokeTaskAt(idx[, arg])   -> long */
#define ZAN_REFL_M_SIVOID 88     /* TypeInfo.InvokeStatic(name[, arg]) -> bool */
#define ZAN_REFL_M_SIINT 89      /* TypeInfo.InvokeStaticInt(...)      -> int */
#define ZAN_REFL_M_SILONG 90     /* TypeInfo.InvokeStaticLong(...)     -> long */
#define ZAN_REFL_M_SIDOUBLE 91   /* TypeInfo.InvokeStaticDouble(...)   -> double */
#define ZAN_REFL_M_SISTRING 92   /* TypeInfo.InvokeStaticString(...)   -> string */
#define ZAN_REFL_M_SIOBJECT 93   /* TypeInfo.InvokeStaticObject(...)   -> object */
#define ZAN_REFL_M_SITASK 94     /* TypeInfo.InvokeStaticTask(...)     -> long */
#define ZAN_REFL_M_SGINT 95      /* TypeInfo.GetStaticFieldInt(name)   -> int */
#define ZAN_REFL_M_SGLONG 96     /* TypeInfo.GetStaticFieldLong(name)  -> long */
#define ZAN_REFL_M_SGDOUBLE 97   /* TypeInfo.GetStaticFieldDouble(name)-> double */
#define ZAN_REFL_M_SGSTRING 98   /* TypeInfo.GetStaticFieldString(name)-> string */
#define ZAN_REFL_M_SGBOOL 99     /* TypeInfo.GetStaticFieldBool(name)  -> bool */
#define ZAN_REFL_M_SGOBJECT 100  /* TypeInfo.GetStaticFieldObject(name)-> object */
#define ZAN_REFL_M_SSINT 101     /* TypeInfo.SetStaticFieldInt(n, v)   -> bool */
#define ZAN_REFL_M_SSLONG 102    /* TypeInfo.SetStaticFieldLong(n, v)  -> bool */
#define ZAN_REFL_M_SSDOUBLE 103  /* TypeInfo.SetStaticFieldDouble(n,v) -> bool */
#define ZAN_REFL_M_SSSTRING 104  /* TypeInfo.SetStaticFieldString(n,v) -> bool */
#define ZAN_REFL_M_SSBOOL 105    /* TypeInfo.SetStaticFieldBool(n, v)  -> bool */
#define ZAN_REFL_M_SSOBJECT 106  /* TypeInfo.SetStaticFieldObject(n,v) -> bool */

static inline bool zan_refl_is_typeinfo(const zan_type_t *t) {
  return t && t->kind == TYPE_STRUCT && t->name.len == 8 &&
         memcmp(t->name.str, "TypeInfo", 8) == 0;
}

static inline bool zan_refl_typeinfo_member(zan_istr_t n, int *out) {
  static const struct {
    const char *name;
    int len;
    int code;
  } tbl[] = {
      {"Name", 4, ZAN_REFL_M_NAME},
      {"FullName", 8, ZAN_REFL_M_FULLNAME},
      {"Kind", 4, ZAN_REFL_M_KIND},
      {"FieldCount", 10, ZAN_REFL_M_FCOUNT},
      {"GetFieldName", 12, ZAN_REFL_M_FNAME},
      {"GetFieldType", 12, ZAN_REFL_M_FTYPE},
      {"HasField", 8, ZAN_REFL_M_HAS},
      {"GetMemberValue", 14, ZAN_REFL_M_MVALUE},
      {"MethodCount", 11, ZAN_REFL_M_MCOUNT},
      {"GetMethodName", 13, ZAN_REFL_M_MNAME},
      {"GetMethodReturnType", 19, ZAN_REFL_M_MRET},
      {"GetMethodParamCount", 19, ZAN_REFL_M_MPCOUNT},
      {"GetMethodParamType", 18, ZAN_REFL_M_MPTYPE},
      {"HasMethod", 9, ZAN_REFL_M_MHAS},
      {"IsMethodStatic", 14, ZAN_REFL_M_MSTATIC},
      {"ConstructorCount", 16, ZAN_REFL_M_CCOUNT},
      {"GetCtorParamCount", 17, ZAN_REFL_M_CPCOUNT},
      {"GetCtorParamType", 16, ZAN_REFL_M_CPTYPE},
      {"CreateInstance", 14, ZAN_REFL_M_CNEW},
      {"IsArray", 7, ZAN_REFL_M_ISARRAY},
      {"IsNullable", 10, ZAN_REFL_M_ISNULLABLE},
      {"IsInterface", 11, ZAN_REFL_M_ISIFACE},
      {"IsGeneric", 9, ZAN_REFL_M_ISGENERIC},
      {"ElementType", 11, ZAN_REFL_M_ELEMTYPE},
      {"TypeArgCount", 12, ZAN_REFL_M_TACOUNT},
      {"GetTypeArg", 10, ZAN_REFL_M_TARG},
      {"GetDeclaredAttributeData", 24, ZAN_REFL_M_DECLATTRS},
      {"GetFieldAttributeData", 21, ZAN_REFL_M_FDECLATTRS},
      {"GetFieldTypeInfo", 16, ZAN_REFL_M_FTYPEINFO},
      {"GetCustomAttributes", 19, ZAN_REFL_M_TATTRS},
      {"GetAttrs", 8, ZAN_REFL_M_TATTRS},
      {"GetFieldCustomAttributes", 24, ZAN_REFL_M_FATTRS},
      {"GetFieldAttrs", 13, ZAN_REFL_M_FATTRS},
      {"GetMethodCustomAttributes", 25, ZAN_REFL_M_MATTRS},
      {"GetMethodAttributeData", 22, ZAN_REFL_M_MDECLATTRS},
      {"GetMethodAttrs", 14, ZAN_REFL_M_MATTRS},
      {"FindField", 9, ZAN_REFL_M_FFIND},
      {"FindMethod", 10, ZAN_REFL_M_MFIND},
      {"InvokeStatic", 12, ZAN_REFL_M_SIVOID},
      {"InvokeStaticInt", 15, ZAN_REFL_M_SIINT},
      {"InvokeStaticLong", 16, ZAN_REFL_M_SILONG},
      {"InvokeStaticDouble", 18, ZAN_REFL_M_SIDOUBLE},
      {"InvokeStaticString", 18, ZAN_REFL_M_SISTRING},
      {"InvokeStaticObject", 18, ZAN_REFL_M_SIOBJECT},
      {"InvokeStaticTask", 16, ZAN_REFL_M_SITASK},
      {"GetStaticFieldInt", 17, ZAN_REFL_M_SGINT},
      {"GetStaticFieldLong", 18, ZAN_REFL_M_SGLONG},
      {"GetStaticFieldDouble", 20, ZAN_REFL_M_SGDOUBLE},
      {"GetStaticFieldString", 20, ZAN_REFL_M_SGSTRING},
      {"GetStaticFieldBool", 18, ZAN_REFL_M_SGBOOL},
      {"GetStaticFieldObject", 20, ZAN_REFL_M_SGOBJECT},
      {"SetStaticFieldInt", 17, ZAN_REFL_M_SSINT},
      {"SetStaticFieldLong", 18, ZAN_REFL_M_SSLONG},
      {"SetStaticFieldDouble", 20, ZAN_REFL_M_SSDOUBLE},
      {"SetStaticFieldString", 20, ZAN_REFL_M_SSSTRING},
      {"SetStaticFieldBool", 18, ZAN_REFL_M_SSBOOL},
      {"SetStaticFieldObject", 20, ZAN_REFL_M_SSOBJECT},
  };
  for (int i = 0; i < (int)(sizeof(tbl) / sizeof(tbl[0])); i++)
    if ((int)n.len == tbl[i].len &&
        memcmp(n.str, tbl[i].name, (size_t)tbl[i].len) == 0) {
      if (out)
        *out = tbl[i].code;
      return true;
    }
  return false;
}

static inline bool zan_refl_instance_method(zan_istr_t n, int *out) {
  static const struct {
    const char *name;
    int len;
    int code;
  } tbl[] = {
      {"GetType", 7, ZAN_REFL_M_GETTYPE},
      {"HasField", 8, ZAN_REFL_M_OHAS},
      {"GetFieldInt", 11, ZAN_REFL_M_OINT},
      {"GetFieldLong", 12, ZAN_REFL_M_OLONG},
      {"GetFieldDouble", 14, ZAN_REFL_M_ODOUBLE},
      {"GetFieldString", 14, ZAN_REFL_M_OSTRING},
      {"GetFieldBool", 12, ZAN_REFL_M_OBOOL},
      {"GetFieldObject", 14, ZAN_REFL_M_OOBJECT},
      {"SetFieldInt", 11, ZAN_REFL_M_SETINT},
      {"SetFieldLong", 12, ZAN_REFL_M_SETLONG},
      {"SetFieldDouble", 14, ZAN_REFL_M_SETDOUBLE},
      {"SetFieldString", 14, ZAN_REFL_M_SETSTRING},
      {"SetFieldBool", 12, ZAN_REFL_M_SETBOOL},
      {"SetFieldObject", 14, ZAN_REFL_M_SETOBJECT},
      {"Invoke", 6, ZAN_REFL_M_IVOID},
      {"InvokeInt", 9, ZAN_REFL_M_IINT},
      {"InvokeLong", 10, ZAN_REFL_M_ILONG},
      {"InvokeDouble", 12, ZAN_REFL_M_IDOUBLE},
      {"InvokeString", 12, ZAN_REFL_M_ISTRING},
      {"InvokeObject", 12, ZAN_REFL_M_IOBJECT},
      {"InvokeTask", 10, ZAN_REFL_M_ITASK},
      {"HasMethod", 9, ZAN_REFL_M_OMHAS},
      {"GetFieldIntAt", 13, ZAN_REFL_M_OINT_AT},
      {"GetFieldLongAt", 14, ZAN_REFL_M_OLONG_AT},
      {"GetFieldDoubleAt", 16, ZAN_REFL_M_ODOUBLE_AT},
      {"GetFieldStringAt", 16, ZAN_REFL_M_OSTRING_AT},
      {"GetFieldBoolAt", 14, ZAN_REFL_M_OBOOL_AT},
      {"GetFieldObjectAt", 16, ZAN_REFL_M_OOBJECT_AT},
      {"SetFieldIntAt", 13, ZAN_REFL_M_SETINT_AT},
      {"SetFieldLongAt", 14, ZAN_REFL_M_SETLONG_AT},
      {"SetFieldDoubleAt", 16, ZAN_REFL_M_SETDOUBLE_AT},
      {"SetFieldStringAt", 16, ZAN_REFL_M_SETSTRING_AT},
      {"SetFieldBoolAt", 14, ZAN_REFL_M_SETBOOL_AT},
      {"SetFieldObjectAt", 16, ZAN_REFL_M_SETOBJECT_AT},
      {"InvokeAt", 8, ZAN_REFL_M_IVOID_AT},
      {"InvokeIntAt", 11, ZAN_REFL_M_IINT_AT},
      {"InvokeLongAt", 12, ZAN_REFL_M_ILONG_AT},
      {"InvokeDoubleAt", 14, ZAN_REFL_M_IDOUBLE_AT},
      {"InvokeStringAt", 14, ZAN_REFL_M_ISTRING_AT},
      {"InvokeObjectAt", 14, ZAN_REFL_M_IOBJECT_AT},
      {"InvokeTaskAt", 12, ZAN_REFL_M_ITASK_AT},
  };
  for (int i = 0; i < (int)(sizeof(tbl) / sizeof(tbl[0])); i++)
    if ((int)n.len == tbl[i].len &&
        memcmp(n.str, tbl[i].name, (size_t)tbl[i].len) == 0) {
      if (out)
        *out = tbl[i].code;
      return true;
    }
  return false;
}

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
  case ZAN_REFL_M_OOBJECT:
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
  case ZAN_REFL_M_IOBJECT:
  case ZAN_REFL_M_ITASK:
  case ZAN_REFL_M_OMHAS:
  case ZAN_REFL_M_FATTRS:
  case ZAN_REFL_M_MDECLATTRS:
  case ZAN_REFL_M_MATTRS:
  case ZAN_REFL_M_FDECLATTRS:
  case ZAN_REFL_M_FTYPEINFO:
  case ZAN_REFL_M_SETOBJECT:
  case ZAN_REFL_M_FFIND:
  case ZAN_REFL_M_MFIND:
  case ZAN_REFL_M_OINT_AT:
  case ZAN_REFL_M_OLONG_AT:
  case ZAN_REFL_M_ODOUBLE_AT:
  case ZAN_REFL_M_OSTRING_AT:
  case ZAN_REFL_M_OBOOL_AT:
  case ZAN_REFL_M_OOBJECT_AT:
  case ZAN_REFL_M_SETINT_AT:
  case ZAN_REFL_M_SETLONG_AT:
  case ZAN_REFL_M_SETDOUBLE_AT:
  case ZAN_REFL_M_SETSTRING_AT:
  case ZAN_REFL_M_SETBOOL_AT:
  case ZAN_REFL_M_SETOBJECT_AT:
  case ZAN_REFL_M_IVOID_AT:
  case ZAN_REFL_M_IINT_AT:
  case ZAN_REFL_M_ILONG_AT:
  case ZAN_REFL_M_IDOUBLE_AT:
  case ZAN_REFL_M_ISTRING_AT:
  case ZAN_REFL_M_IOBJECT_AT:
  case ZAN_REFL_M_ITASK_AT:
  case ZAN_REFL_M_SIVOID:
  case ZAN_REFL_M_SIINT:
  case ZAN_REFL_M_SILONG:
  case ZAN_REFL_M_SIDOUBLE:
  case ZAN_REFL_M_SISTRING:
  case ZAN_REFL_M_SIOBJECT:
  case ZAN_REFL_M_SITASK:
  case ZAN_REFL_M_SGINT:
  case ZAN_REFL_M_SGLONG:
  case ZAN_REFL_M_SGDOUBLE:
  case ZAN_REFL_M_SGSTRING:
  case ZAN_REFL_M_SGBOOL:
  case ZAN_REFL_M_SGOBJECT:
  case ZAN_REFL_M_SSINT:
  case ZAN_REFL_M_SSLONG:
  case ZAN_REFL_M_SSDOUBLE:
  case ZAN_REFL_M_SSSTRING:
  case ZAN_REFL_M_SSBOOL:
  case ZAN_REFL_M_SSOBJECT:
    return true;
  default:
    return false;
  }
}

#define ZAN_REFL_MAX_ARGS 65

static inline int zan_refl_member_max_args(int code) {
  switch (code) {
  case ZAN_REFL_M_IVOID:
  case ZAN_REFL_M_IINT:
  case ZAN_REFL_M_ILONG:
  case ZAN_REFL_M_IDOUBLE:
  case ZAN_REFL_M_ISTRING:
  case ZAN_REFL_M_IOBJECT:
  case ZAN_REFL_M_ITASK:
  case ZAN_REFL_M_IVOID_AT:
  case ZAN_REFL_M_IINT_AT:
  case ZAN_REFL_M_ILONG_AT:
  case ZAN_REFL_M_IDOUBLE_AT:
  case ZAN_REFL_M_ISTRING_AT:
  case ZAN_REFL_M_IOBJECT_AT:
  case ZAN_REFL_M_ITASK_AT:
  case ZAN_REFL_M_SIVOID:
  case ZAN_REFL_M_SIINT:
  case ZAN_REFL_M_SILONG:
  case ZAN_REFL_M_SIDOUBLE:
  case ZAN_REFL_M_SISTRING:
  case ZAN_REFL_M_SIOBJECT:
  case ZAN_REFL_M_SITASK:
    return 65;
  case ZAN_REFL_M_CNEW:
    return 64;
  case ZAN_REFL_M_MPTYPE:
  case ZAN_REFL_M_CPTYPE:
  case ZAN_REFL_M_SETINT:
  case ZAN_REFL_M_SETLONG:
  case ZAN_REFL_M_SETDOUBLE:
  case ZAN_REFL_M_SETSTRING:
  case ZAN_REFL_M_SETBOOL:
  case ZAN_REFL_M_SETOBJECT:
  case ZAN_REFL_M_SETINT_AT:
  case ZAN_REFL_M_SETLONG_AT:
  case ZAN_REFL_M_SETDOUBLE_AT:
  case ZAN_REFL_M_SETSTRING_AT:
  case ZAN_REFL_M_SETBOOL_AT:
  case ZAN_REFL_M_SETOBJECT_AT:
  case ZAN_REFL_M_SSINT:
  case ZAN_REFL_M_SSLONG:
  case ZAN_REFL_M_SSDOUBLE:
  case ZAN_REFL_M_SSSTRING:
  case ZAN_REFL_M_SSBOOL:
  case ZAN_REFL_M_SSOBJECT:
    return 2;
  default:
    return zan_refl_member_takes_arg(code) ? 1 : 0;
  }
}

static inline zan_type_t *zan_refl_result_type(zan_binder_t *b, int code) {
  switch (code) {
  case ZAN_REFL_M_NAME:
  case ZAN_REFL_M_FULLNAME:
  case ZAN_REFL_M_KIND:
  case ZAN_REFL_M_FNAME:
  case ZAN_REFL_M_FTYPE:
  case ZAN_REFL_M_OSTRING:
  case ZAN_REFL_M_MNAME:
  case ZAN_REFL_M_MRET:
  case ZAN_REFL_M_MPTYPE:
  case ZAN_REFL_M_CPTYPE:
  case ZAN_REFL_M_DECLATTRS:
  case ZAN_REFL_M_FDECLATTRS:
  case ZAN_REFL_M_FATTRS:
  case ZAN_REFL_M_MDECLATTRS:
  case ZAN_REFL_M_MATTRS:
  case ZAN_REFL_M_TATTRS:
  case ZAN_REFL_M_ISTRING:
  case ZAN_REFL_M_OSTRING_AT:
  case ZAN_REFL_M_ISTRING_AT:
  case ZAN_REFL_M_SISTRING:
  case ZAN_REFL_M_SGSTRING:
    return b->type_string;
  case ZAN_REFL_M_FCOUNT:
  case ZAN_REFL_M_OINT:
  case ZAN_REFL_M_MCOUNT:
  case ZAN_REFL_M_MPCOUNT:
  case ZAN_REFL_M_CCOUNT:
  case ZAN_REFL_M_CPCOUNT:
  case ZAN_REFL_M_TACOUNT:
  case ZAN_REFL_M_IINT:
  case ZAN_REFL_M_FFIND:
  case ZAN_REFL_M_MFIND:
  case ZAN_REFL_M_OINT_AT:
  case ZAN_REFL_M_IINT_AT:
  case ZAN_REFL_M_SIINT:
  case ZAN_REFL_M_SGINT:
    return b->type_int;
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
  case ZAN_REFL_M_SETOBJECT:
  case ZAN_REFL_M_IVOID:
  case ZAN_REFL_M_OMHAS:
  case ZAN_REFL_M_OBOOL_AT:
  case ZAN_REFL_M_SETINT_AT:
  case ZAN_REFL_M_SETLONG_AT:
  case ZAN_REFL_M_SETDOUBLE_AT:
  case ZAN_REFL_M_SETSTRING_AT:
  case ZAN_REFL_M_SETBOOL_AT:
  case ZAN_REFL_M_SETOBJECT_AT:
  case ZAN_REFL_M_IVOID_AT:
  case ZAN_REFL_M_SIVOID:
  case ZAN_REFL_M_SGBOOL:
  case ZAN_REFL_M_SSINT:
  case ZAN_REFL_M_SSLONG:
  case ZAN_REFL_M_SSDOUBLE:
  case ZAN_REFL_M_SSSTRING:
  case ZAN_REFL_M_SSBOOL:
  case ZAN_REFL_M_SSOBJECT:
    return b->type_bool;
  case ZAN_REFL_M_ODOUBLE:
  case ZAN_REFL_M_IDOUBLE:
  case ZAN_REFL_M_ODOUBLE_AT:
  case ZAN_REFL_M_IDOUBLE_AT:
  case ZAN_REFL_M_SIDOUBLE:
  case ZAN_REFL_M_SGDOUBLE:
    return b->type_double;
  case ZAN_REFL_M_GETTYPE:
  case ZAN_REFL_M_ELEMTYPE:
  case ZAN_REFL_M_FTYPEINFO:
  case ZAN_REFL_M_TARG:
    return b->type_typeinfo;
  case ZAN_REFL_M_OOBJECT:
  case ZAN_REFL_M_IOBJECT:
  case ZAN_REFL_M_CNEW:
  case ZAN_REFL_M_OOBJECT_AT:
  case ZAN_REFL_M_IOBJECT_AT:
  case ZAN_REFL_M_SIOBJECT:
  case ZAN_REFL_M_SGOBJECT:
    return b->type_object;
  default:
    return b->type_long;
  }
}

static inline bool zan_refl_is_reflectable(const zan_type_t *t) {
  static const char *builtins[] = {"Dict", "StringBuilder", "Span", "Task",
                                   "Grouping"};
  if (!t)
    return false;
  if (t->kind == TYPE_OBJECT)
    return true;
  if (t->kind != TYPE_CLASS && t->kind != TYPE_STRUCT)
    return false;
  if (zan_refl_is_typeinfo(t))
    return false;
  for (int i = 0; i < (int)(sizeof(builtins) / sizeof(builtins[0])); i++) {
    int len = (int)strlen(builtins[i]);
    if ((int)t->name.len == len &&
        memcmp(t->name.str, builtins[i], (size_t)len) == 0)
      return false;
  }
  return true;
}

static inline zan_type_t *
zan_refl_member_type(zan_binder_t *b, const zan_type_t *recv, zan_istr_t n) {
  int code = 0;
  if (zan_refl_is_typeinfo(recv)) {
    if (!zan_refl_typeinfo_member(n, &code))
      return NULL;
    return zan_refl_result_type(b, code);
  }
  if (!zan_refl_is_reflectable(recv))
    return NULL;
  if (!zan_refl_instance_method(n, &code))
    return NULL;
  return zan_refl_result_type(b, code);
}

#endif /* ZAN_REFLECT_API_H */
