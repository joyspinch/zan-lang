#include "irgen_internal.h"

#define ZAN_REFL_FULLNAME_OFF (-112) /* B-ID107：头部 i8ptr，尾部倒数 OFF 不变 */
#define ZAN_REFL_ATTRS_OFF   (-104)
#define ZAN_REFL_TABLE_OFF   (-96)
#define ZAN_REFL_METHODS_OFF (-88)
#define ZAN_REFL_MCOUNT_OFF  (-80)
#define ZAN_REFL_CTORS_OFF   (-72)
#define ZAN_REFL_CCOUNT_OFF  (-64)
#define ZAN_REFL_ELEM_OFF    (-56)
#define ZAN_REFL_TARGS_OFF   (-48)
#define ZAN_REFL_FIELDS_OFF (-40)
#define ZAN_REFL_COUNT_OFF  (-32)
#define ZAN_REFL_KIND_OFF   (-24)
#define ZAN_REFL_PREFIX     88
/* { i8*, i8*, i64, i64, i8*, i64, i8*, i8*, i8* } */
#define ZAN_REFL_FIELD_SIZE 72
#define ZAN_REFL_METHOD_SIZE 64
#define ZAN_REFL_MO_NAME     0
#define ZAN_REFL_MO_RET      8
#define ZAN_REFL_MO_RETKIND  16
#define ZAN_REFL_MO_PCOUNT   24
#define ZAN_REFL_MO_PTYPES   32
#define ZAN_REFL_MO_THUNK    40
#define ZAN_REFL_MO_FLAGS    48
#define ZAN_REFL_MO_ATTRS    56

#define ZAN_REFL_MF_STATIC   1
#define ZAN_REFL_MF_VIRTUAL  2
#define ZAN_REFL_MF_PROPGET  4
#define ZAN_REFL_MF_PROPSET  8

#define ZAN_REFL_TBL_METHOD  0
#define ZAN_REFL_TBL_CTOR    1

/* `which` selector of __zan_refl_mstr */
#define ZAN_REFL_MW_NAME     0
#define ZAN_REFL_MW_RET      1
#define ZAN_REFL_MW_PARAM    2
#define ZAN_REFL_MW_ATTRS    3

/* `which` selector of __zan_refl_mi64 */
#define ZAN_REFL_MI_COUNT    0
#define ZAN_REFL_MI_PCOUNT   1
#define ZAN_REFL_MI_FLAGS    2
#define ZAN_REFL_MI_RETKIND  3

#define ZAN_REFL_VK_INT      0   /* an integer, sign-extended */
#define ZAN_REFL_VK_DOUBLE   1   /* a double, bit-cast */
#define ZAN_REFL_VK_PTR      2   /* a string / reference, as an integer */

#define ZAN_REFL_ARG_SLOTS   64

#define ZAN_REFL_TK_CLASS     1
#define ZAN_REFL_TK_STRUCT    2
#define ZAN_REFL_TK_ENUM      3
#define ZAN_REFL_TK_INTERFACE 4
#define ZAN_REFL_TK_PRIMITIVE 5
#define ZAN_REFL_TK_OTHER     6
#define ZAN_REFL_TK_ARRAY     7
#define ZAN_REFL_TK_NULLABLE  8
#define ZAN_REFL_TK_DELEGATE  9
#define ZAN_REFL_TK_LAST      9

#define ZAN_REFL_FK_BOOL   1
#define ZAN_REFL_FK_SBYTE  2
#define ZAN_REFL_FK_BYTE   3
#define ZAN_REFL_FK_SHORT  4
#define ZAN_REFL_FK_USHORT 5
#define ZAN_REFL_FK_INT    6
#define ZAN_REFL_FK_UINT   7
#define ZAN_REFL_FK_LONG   8
#define ZAN_REFL_FK_ULONG  9
#define ZAN_REFL_FK_CHAR   10
#define ZAN_REFL_FK_ENUM   11
#define ZAN_REFL_FK_FLOAT  12
#define ZAN_REFL_FK_DOUBLE 13
#define ZAN_REFL_FK_STRING 14
#define ZAN_REFL_FK_REF    15
#define ZAN_REFL_FK_OTHER  16
#define ZAN_REFL_FK_ENUM_MEMBER 20

/* `which` selector of __zan_refl_fname */
#define ZAN_REFL_WHICH_NAME 0
#define ZAN_REFL_WHICH_TYPE 1
#define ZAN_REFL_WHICH_CUSTOM 2
#define ZAN_REFL_WHICH_DEFAULT 3
#define ZAN_REFL_WHICH_ATTRS 4

/* Composite Dispatcher Submodules */
#include "reflect/reflect_fields.c"
#include "reflect/reflect_thunks.c"
#include "reflect/reflect_attrs.c"
#include "reflect/reflect_accessors.c"
#include "reflect/reflect_invoke.c"
#include "reflect/reflect_typeinfo.c"
