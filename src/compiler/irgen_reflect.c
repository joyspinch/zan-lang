/* Reflection metadata.
 *
 * `typeof(T)` and `obj.GetType()` yield a TypeInfo: a pointer into a
 * compiler-emitted, immortal record that describes one type. The record is laid
 * out so that the pointer lands exactly on the type's display name, with the
 * reflection slots in FRONT of it:
 *
 *      offset from the TypeInfo value
 *      -88   i8*  methods       pointer to the method-record array (or null)
 *      -80   i64  method_count
 *      -72   i8*  ctors         pointer to the constructor-record array
 *      -64   i64  ctor_count
 *      -56   i8*  elem          TypeInfo of the array element / nullable
 *                               payload (null when the type has neither)
 *      -48   i8*  targs         null-terminated array of TypeInfo, one per
 *                               generic argument (null when not generic)
 *      -40   i8*  fields        pointer to the field-record array (or null)
 *      -32   i64  field_count
 *      -24   i64  type kind     ZAN_REFL_TK_*
 *      -16   i64  refcount      ZAN_STRING_SENTINEL_RC  \  the string RC header
 *       -8   i64  tag|length    ZAN_STR_HDR_WORD(len)   /  (see zan_abi.h)
 *        0   i8[] name          NUL-terminated display name ("Widget", "int")
 *
 * The two header words a Zan string carries are therefore present at exactly
 * the offsets the string ABI puts them, which makes a TypeInfo *also* a valid
 * managed string holding the type name: printing, concatenating or comparing
 * one needs no conversion, and retain/release see the sentinel refcount and
 * leave it alone. `TypeInfo.Name` is the identity.
 *
 * Each field record is { i8* name, i8* typeName, i64 kind, i64 offset }: kind
 * is a ZAN_REFL_FK_* code that says how to load the slot, offset is the byte
 * offset inside the instance (a relocatable constant expression — the layout is
 * only known once the data layout is attached at object-emission time), or, for
 * an enum member, the member's constant value.
 *
 * Records are emitted on demand and cached per (symbol, display name). Field
 * lookup and loading are done by a handful of small internal functions built
 * into the module on first use, so no runtime-library support is needed.
 */

/* record header offsets, from the TypeInfo value */
#define ZAN_REFL_METHODS_OFF (-88)
#define ZAN_REFL_MCOUNT_OFF  (-80)
#define ZAN_REFL_CTORS_OFF   (-72)
#define ZAN_REFL_CCOUNT_OFF  (-64)
#define ZAN_REFL_ELEM_OFF    (-56)
#define ZAN_REFL_TARGS_OFF   (-48)
#define ZAN_REFL_FIELDS_OFF (-40)
#define ZAN_REFL_COUNT_OFF  (-32)
#define ZAN_REFL_KIND_OFF   (-24)
/* bytes in front of the name payload */
#define ZAN_REFL_PREFIX     88
/* { i8*, i8*, i64, i64 } */
#define ZAN_REFL_FIELD_SIZE 32
/* { i8* name, i8* retType, i64 retKind, i64 paramCount, i8* paramTypes,
 *   i8* thunk, i64 flags } */
#define ZAN_REFL_METHOD_SIZE 56
#define ZAN_REFL_MO_NAME     0
#define ZAN_REFL_MO_RET      8
#define ZAN_REFL_MO_RETKIND  16
#define ZAN_REFL_MO_PCOUNT   24
#define ZAN_REFL_MO_PTYPES   32
#define ZAN_REFL_MO_THUNK    40
#define ZAN_REFL_MO_FLAGS    48

/* method flags */
#define ZAN_REFL_MF_STATIC   1
#define ZAN_REFL_MF_VIRTUAL  2
#define ZAN_REFL_MF_PROPGET  4
#define ZAN_REFL_MF_PROPSET  8

/* which table a reader walks */
#define ZAN_REFL_TBL_METHOD  0
#define ZAN_REFL_TBL_CTOR    1

/* `which` selector of __zan_refl_mstr */
#define ZAN_REFL_MW_NAME     0
#define ZAN_REFL_MW_RET      1
#define ZAN_REFL_MW_PARAM    2

/* `which` selector of __zan_refl_mi64 */
#define ZAN_REFL_MI_COUNT    0
#define ZAN_REFL_MI_PCOUNT   1
#define ZAN_REFL_MI_FLAGS    2
#define ZAN_REFL_MI_RETKIND  3

/* how a value handed to __zan_refl_set was packed into its i64 slot */
#define ZAN_REFL_VK_INT      0   /* an integer, sign-extended */
#define ZAN_REFL_VK_DOUBLE   1   /* a double, bit-cast */
#define ZAN_REFL_VK_PTR      2   /* a string / reference, as an integer */

/* the maximum number of arguments a reflected call can pack */
#define ZAN_REFL_ARG_SLOTS   4

/* type kinds */
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

/* field kinds: how to load the slot the record points at */
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
/* an enum member: no storage, the `offset` slot holds its constant value */
#define ZAN_REFL_FK_ENUM_MEMBER 20

/* `which` selector of __zan_refl_fname */
#define ZAN_REFL_WHICH_NAME 0
#define ZAN_REFL_WHICH_TYPE 1



static LLVMTypeRef refl_field_type(zan_irgen_t *g) {
    if (!g->refl_field_type) {
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef elems[4] = { i8ptr, i8ptr, i64, i64 };
        g->refl_field_type = LLVMStructTypeInContext(g->ctx, elems, 4, 0);
    }
    return g->refl_field_type;
}

/* A NUL-terminated immortal Zan string as a *constant*: usable inside a global
 * initializer, unlike emit_string_literal_rc, which needs a builder. */
static LLVMValueRef refl_const_string(zan_irgen_t *g, const char *s, int len) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (len < 0) len = 0;
    /* the RC header words are their own i64 slots, so the struct is 8-aligned
     * and the payload starts exactly ZAN_STRING_HDR bytes in */
    LLVMTypeRef rec_ty = LLVMStructTypeInContext(g->ctx,
        (LLVMTypeRef[]){ i64, i64, LLVMArrayType(i8, (unsigned)(len + 1)) }, 3, 0);
    LLVMValueRef *chars = (LLVMValueRef *)calloc((size_t)len + 1, sizeof(LLVMValueRef));
    if (!chars) return LLVMConstNull(LLVMPointerType(i8, 0));
    for (int i = 0; i < len; i++)
        chars[i] = LLVMConstInt(i8, (unsigned char)s[i], 0);
    chars[len] = LLVMConstInt(i8, 0, 0);
    LLVMValueRef init = LLVMConstNamedStruct(rec_ty, (LLVMValueRef[]){
        LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
        LLVMConstInt(i64, ZAN_STR_HDR_WORD(len), 0),
        LLVMConstArray(i8, chars, (unsigned)(len + 1)) }, 3);
    free(chars);
    char nm[64];
    snprintf(nm, sizeof(nm), "__zan.refl.str.%d", g->refl_str_count++);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, rec_ty, nm);
    LLVMSetInitializer(gv, init);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetAlignment(gv, 8);
    LLVMValueRef idx[3] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0),
                            LLVMConstInt(i64, 0, 0) };
    return LLVMConstGEP2(rec_ty, gv, idx, 3);
}

/* The load code for a field of `t`. */
static int refl_field_kind(zan_type_t *t) {
    if (!t) return ZAN_REFL_FK_OTHER;
    switch (t->kind) {
    case TYPE_BOOL:   return ZAN_REFL_FK_BOOL;
    case TYPE_SBYTE:  return ZAN_REFL_FK_SBYTE;
    case TYPE_BYTE:   return ZAN_REFL_FK_BYTE;
    case TYPE_SHORT:  return ZAN_REFL_FK_SHORT;
    case TYPE_USHORT: return ZAN_REFL_FK_USHORT;
    case TYPE_INT:    return ZAN_REFL_FK_INT;
    case TYPE_UINT:   return ZAN_REFL_FK_UINT;
    case TYPE_LONG:
    case TYPE_NINT:   return ZAN_REFL_FK_LONG;
    case TYPE_ULONG:  return ZAN_REFL_FK_ULONG;
    case TYPE_CHAR:   return ZAN_REFL_FK_CHAR;
    case TYPE_ENUM:   return ZAN_REFL_FK_ENUM;
    case TYPE_FLOAT:  return ZAN_REFL_FK_FLOAT;
    case TYPE_DOUBLE: return ZAN_REFL_FK_DOUBLE;
    case TYPE_STRING: return ZAN_REFL_FK_STRING;
    case TYPE_OBJECT:
    case TYPE_CLASS:
    case TYPE_INTERFACE:
    case TYPE_ARRAY:
    case TYPE_DELEGATE: return ZAN_REFL_FK_REF;
    default: return ZAN_REFL_FK_OTHER;
    }
}

static int refl_type_kind(zan_type_t *t) {
    if (!t) return ZAN_REFL_TK_OTHER;
    switch (t->kind) {
    case TYPE_CLASS:     return ZAN_REFL_TK_CLASS;
    case TYPE_STRUCT:    return ZAN_REFL_TK_STRUCT;
    case TYPE_ENUM:      return ZAN_REFL_TK_ENUM;
    case TYPE_INTERFACE: return ZAN_REFL_TK_INTERFACE;
    case TYPE_ARRAY:     return ZAN_REFL_TK_ARRAY;
    case TYPE_NULLABLE:  return ZAN_REFL_TK_NULLABLE;
    case TYPE_DELEGATE:  return ZAN_REFL_TK_DELEGATE;
    case TYPE_TASK:      return ZAN_REFL_TK_OTHER;
    default:             return ZAN_REFL_TK_PRIMITIVE;
    }
}

/* A field's type name for the metadata: the declared type's simple name, with
 * `[]` for an array so `int[]` is not reported as `int`. */
static LLVMValueRef refl_field_type_name(zan_irgen_t *g, zan_type_t *t) {
    char buf[160];
    int n = 0;
    if (t && t->name.len > 0) {
        n = (int)t->name.len;
        if (n > (int)sizeof(buf) - 3) n = (int)sizeof(buf) - 3;
        memcpy(buf, t->name.str, (size_t)n);
    } else {
        memcpy(buf, "?", 1);
        n = 1;
    }
    if (t && t->kind == TYPE_ARRAY) { buf[n++] = '['; buf[n++] = ']'; }
    buf[n] = 0;
    return refl_const_string(g, buf, n);
}

static struct zan_struct_type_entry *refl_struct_entry(zan_irgen_t *g,
                                                      zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++)
        if (g->struct_types[i].sym == sym) return &g->struct_types[i];
    return NULL;
}

/* The byte offset of layout slot `slot` in `st`, as a relocatable constant. */
static LLVMValueRef refl_slot_offset(zan_irgen_t *g, LLVMTypeRef st, int slot) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef idx[2] = { LLVMConstInt(i64, 0, 0),
                            LLVMConstInt(i32, (unsigned long long)slot, 0) };
    LLVMValueRef p = LLVMConstGEP2(st, LLVMConstNull(LLVMPointerType(st, 0)),
                                   idx, 2);
    return LLVMConstPtrToInt(p, i64);
}

/* Emit the field-record array for a class/struct, or NULL when it has none. */
static LLVMValueRef refl_emit_fields_class(zan_irgen_t *g, zan_symbol_t *sym,
                                           int *out_count) {
    *out_count = 0;
    if (!sym) return NULL;
    LLVMTypeRef st = get_struct_llvm_type(g, sym);
    if (!st) return NULL;
    struct zan_struct_type_entry *entry = refl_struct_entry(g, sym);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef frec = refl_field_type(g);

    int cap = sym->member_count > 0 ? sym->member_count : 1;
    LLVMValueRef *recs = (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
    if (!recs) return NULL;
    int n = 0;
    int slot = class_vptr_offset(sym);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m))
            continue;
        LLVMValueRef off;
        if (entry && entry->explicit_layout && entry->field_offsets &&
            slot < entry->field_count)
            off = LLVMConstInt(i64, entry->field_offsets[slot], 0);
        else
            off = refl_slot_offset(g, st, slot);
        recs[n++] = LLVMConstNamedStruct(frec, (LLVMValueRef[]){
            refl_const_string(g, m->name.str, (int)m->name.len),
            refl_field_type_name(g, m->type),
            LLVMConstInt(i64, (unsigned long long)refl_field_kind(m->type), 0),
            off }, 4);
        slot++;
    }
    LLVMValueRef arr = NULL;
    if (n > 0) {
        LLVMTypeRef arr_ty = LLVMArrayType(frec, (unsigned)n);
        char nm[96];
        snprintf(nm, sizeof(nm), "__zan.refl.fields.%.*s.%d",
                 (int)sym->name.len, sym->name.str, g->refl_meta_count);
        LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
        LLVMSetInitializer(gv, LLVMConstArray(frec, recs, (unsigned)n));
        LLVMSetLinkage(gv, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(gv, 1);
        arr = LLVMConstBitCast(gv,
            LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
        *out_count = n;
    }
    free(recs);
    return arr;
}

/* Emit the member-record array for an enum: one record per member, carrying
 * the member's constant value in the offset slot (see the C# rule the emit
 * side uses: an explicit `= n` resets the running value, otherwise +1). */
static LLVMValueRef refl_emit_fields_enum(zan_irgen_t *g, zan_symbol_t *sym,
                                          int *out_count) {
    *out_count = 0;
    if (!sym) return NULL;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef frec = refl_field_type(g);
    int cap = sym->member_count > 0 ? sym->member_count : 1;
    LLVMValueRef *recs = (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
    if (!recs) return NULL;
    int n = 0;
    long long val = 0;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind != SYM_ENUM_MEMBER) continue;
        if (m->decl && m->decl->kind == AST_ENUM_MEMBER &&
            m->decl->enum_member.value &&
            m->decl->enum_member.value->kind == AST_INT_LITERAL)
            val = m->decl->enum_member.value->int_val;
        recs[n++] = LLVMConstNamedStruct(frec, (LLVMValueRef[]){
            refl_const_string(g, m->name.str, (int)m->name.len),
            refl_const_string(g, sym->name.str, (int)sym->name.len),
            LLVMConstInt(i64, ZAN_REFL_FK_ENUM_MEMBER, 0),
            LLVMConstInt(i64, (unsigned long long)val, 0) }, 4);
        val++;
    }
    LLVMValueRef arr = NULL;
    if (n > 0) {
        LLVMTypeRef arr_ty = LLVMArrayType(frec, (unsigned)n);
        char nm[96];
        snprintf(nm, sizeof(nm), "__zan.refl.enum.%.*s.%d",
                 (int)sym->name.len, sym->name.str, g->refl_meta_count);
        LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
        LLVMSetInitializer(gv, LLVMConstArray(frec, recs, (unsigned)n));
        LLVMSetLinkage(gv, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(gv, 1);
        arr = LLVMConstBitCast(gv,
            LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
        *out_count = n;
    }
    free(recs);
    return arr;
}

/* ---- method / constructor tables ------------------------------------- */

static LLVMTypeRef refl_method_type(zan_irgen_t *g) {
    if (!g->refl_method_type) {
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef elems[7] = { i8ptr, i8ptr, i64, i64, i8ptr, i8ptr, i64 };
        g->refl_method_type = LLVMStructTypeInContext(g->ctx, elems, 7, 0);
    }
    return g->refl_method_type;
}

/* PROPGET/PROPSET when `msym` is the synthesized accessor of a property of
 * `sym` (`get_Count` for `Count`), 0 for an ordinary method. The reflected
 * read/write of a property routes through these instead of the backing slot,
 * which is the only correct answer for a property with a custom accessor: its
 * slot is never written. */
static int refl_accessor_flag(zan_symbol_t *sym, zan_symbol_t *msym) {
    if (!sym || !msym || msym->name.len <= 4) return 0;
    bool is_get = memcmp(msym->name.str, "get_", 4) == 0;
    bool is_set = memcmp(msym->name.str, "set_", 4) == 0;
    if (!is_get && !is_set) return 0;
    const char *pn = msym->name.str + 4;
    int plen = (int)msym->name.len - 4;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *p = sym->members[i];
        if (p && p->kind == SYM_PROPERTY && (int)p->name.len == plen &&
            memcmp(p->name.str, pn, (size_t)plen) == 0)
            return is_get ? ZAN_REFL_MF_PROPGET : ZAN_REFL_MF_PROPSET;
    }
    return 0;
}

/* The reflected methods of `sym`, in declaration order. */
static int refl_collect_methods(zan_symbol_t *sym, zan_symbol_t **out, int cap) {
    int n = 0;
    if (!sym) return 0;
    for (int i = 0; i < sym->member_count && n < cap; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL)
            continue;
        out[n++] = m;
    }
    return n;
}

/* The reflected constructors of `sym`: the declarations, so the table can be
 * shaped before any of them has been emitted. A `static T()` type initializer
 * is not a constructor (it is never selected by `new T(...)`). */
static int refl_collect_ctors(zan_symbol_t *sym, zan_ast_node_t **out, int cap) {
    int n = 0;
    if (!sym || !sym->decl) return 0;
    zan_ast_list_t *members = &sym->decl->type_decl.members;
    for (int i = 0; i < members->count && n < cap; i++) {
        zan_ast_node_t *m = members->items[i];
        if (m->kind != AST_CONSTRUCTOR_DECL) continue;
        if (m->method_decl.modifiers & MOD_STATIC) continue;
        out[n++] = m;
    }
    return n;
}

/* Widen an i64 argument slot to what the callee's parameter really is, or
 * report that the parameter has no slot representation (a struct by value, a
 * by-ref parameter) so the method gets no thunk at all. */
static LLVMValueRef refl_slot_to(zan_irgen_t *g, LLVMValueRef slot,
                                 LLVMTypeRef want, bool *ok) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    switch (LLVMGetTypeKind(want)) {
    case LLVMIntegerTypeKind:
        if (want == i64) return slot;
        return LLVMBuildTrunc(g->builder, slot, want, "refl.a.tr");
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, slot, dbl, "refl.a.d");
    case LLVMFloatTypeKind:
        return LLVMBuildFPTrunc(g->builder,
            LLVMBuildBitCast(g->builder, slot, dbl, "refl.a.d"), want,
            "refl.a.f");
    case LLVMPointerTypeKind:
        return LLVMBuildIntToPtr(g->builder, slot, want, "refl.a.p");
    default:
        *ok = false;
        return LLVMConstNull(want);
    }
}

/* The inverse: a returned value packed back into its i64 slot. */
static LLVMValueRef refl_to_slot(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef t = LLVMTypeOf(v);
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind:
        if (t == i64) return v;
        if (LLVMGetIntTypeWidth(t) == 1)
            return LLVMBuildZExt(g->builder, v, i64, "refl.r.z");
        return LLVMBuildSExt(g->builder, v, i64, "refl.r.s");
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, v, i64, "refl.r.d");
    case LLVMFloatTypeKind:
        return LLVMBuildBitCast(g->builder,
            LLVMBuildFPExt(g->builder, v, dbl, "refl.r.fe"), i64, "refl.r.f");
    case LLVMPointerTypeKind:
        return LLVMBuildPtrToInt(g->builder, v, i64, "refl.r.p");
    default:
        return LLVMConstInt(i64, 0, 0);
    }
}

/* void thunk(i8 *self, i64 *args, i64 *ret): unpack the argument slots into
 * the real signature, call `fn`, and pack the result back. One is emitted per
 * reflected method, which is what lets a call by name work without the call
 * site knowing the signature. NULL when the signature has a parameter or a
 * result no slot can carry. */
static LLVMValueRef refl_make_thunk(zan_irgen_t *g, LLVMValueRef fn,
                                    LLVMTypeRef fn_ty, bool is_static,
                                    const char *tag) {
    if (!fn || !fn_ty) return NULL;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);

    unsigned total = LLVMCountParamTypes(fn_ty);
    if (total > ZAN_REFL_ARG_SLOTS + 1) return NULL;
    LLVMTypeRef ptypes[ZAN_REFL_ARG_SLOTS + 1];
    LLVMGetParamTypes(fn_ty, ptypes);
    unsigned first = is_static ? 0 : 1;
    if (!is_static && (total < 1 ||
                       LLVMGetTypeKind(ptypes[0]) != LLVMPointerTypeKind))
        return NULL;

    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.thunk.%s.%d", tag ? tag : "m",
             g->refl_thunk_count++);
    LLVMTypeRef th_ty = LLVMFunctionType(vd,
        (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0);
    LLVMValueRef th = LLVMAddFunction(g->mod, nm, th_ty);
    LLVMSetLinkage(th, LLVMInternalLinkage);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, th, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    bool ok = true;
    LLVMValueRef call_args[ZAN_REFL_ARG_SLOTS + 1];
    if (!is_static)
        call_args[0] = LLVMBuildBitCast(g->builder, LLVMGetParam(th, 0),
                                        ptypes[0], "refl.self");
    for (unsigned i = first; i < total && ok; i++) {
        LLVMValueRef k = LLVMConstInt(i64, (unsigned long long)(i - first), 0);
        LLVMValueRef sp = LLVMBuildGEP2(g->builder, i64, LLVMGetParam(th, 1),
                                        &k, 1, "refl.as");
        LLVMValueRef slot = LLVMBuildLoad2(g->builder, i64, sp, "refl.av");
        call_args[i] = refl_slot_to(g, slot, ptypes[i], &ok);
    }
    LLVMTypeRef rty = LLVMGetReturnType(fn_ty);
    if (ok && LLVMGetTypeKind(rty) != LLVMVoidTypeKind &&
        LLVMGetTypeKind(rty) != LLVMIntegerTypeKind &&
        LLVMGetTypeKind(rty) != LLVMFloatTypeKind &&
        LLVMGetTypeKind(rty) != LLVMDoubleTypeKind &&
        LLVMGetTypeKind(rty) != LLVMPointerTypeKind)
        ok = false;
    if (!ok) {
        LLVMDeleteFunction(th);
        if (save) LLVMPositionBuilderAtEnd(g->builder, save);
        return NULL;
    }

    LLVMValueRef res = zan_call2(g->builder, fn_ty, fn, call_args, total, "");
    LLVMValueRef slot = LLVMGetTypeKind(rty) == LLVMVoidTypeKind
        ? LLVMConstInt(i64, 0, 0) : refl_to_slot(g, res);
    LLVMBuildStore(g->builder, slot, LLVMGetParam(th, 2));
    LLVMBuildRetVoid(g->builder);
    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return LLVMConstBitCast(th, i8ptr);
}

/* The `[n x i8*]` type-name array of a parameter list, as an i8*. */
static LLVMValueRef refl_param_names(zan_irgen_t *g, zan_ast_list_t *params) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!params || params->count <= 0) return LLVMConstNull(i8ptr);
    LLVMValueRef *names = (LLVMValueRef *)calloc((size_t)params->count,
                                                 sizeof(LLVMValueRef));
    if (!names) return LLVMConstNull(i8ptr);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *p = params->items[i];
        zan_type_t *pt = zan_binder_resolve_type(g->binder, p->param.type);
        names[i] = refl_field_type_name(g, pt);
    }
    LLVMTypeRef arr_ty = LLVMArrayType(i8ptr, (unsigned)params->count);
    char nm[64];
    snprintf(nm, sizeof(nm), "__zan.refl.ptypes.%d", g->refl_str_count++);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstArray(i8ptr, names,
                                          (unsigned)params->count));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    free(names);
    return LLVMConstBitCast(gv, i8ptr);
}

/* One method record. The thunk is only looked up when `late` (every function
 * is declared by then); an early record carries a null thunk, which reads as
 * "described but not callable". */
static LLVMValueRef refl_method_record(zan_irgen_t *g, zan_symbol_t *sym,
                                       zan_symbol_t *msym, bool late) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef mrec = refl_method_type(g);
    zan_ast_node_t *decl = msym->decl;
    zan_type_t *rt = decl->method_decl.return_type
        ? zan_binder_resolve_type(g->binder, decl->method_decl.return_type)
        : g->binder->type_void;
    bool is_static = (msym->modifiers & MOD_STATIC) != 0 ||
                     (decl->method_decl.modifiers & MOD_STATIC) != 0;
    int flags = is_static ? ZAN_REFL_MF_STATIC : 0;
    if (decl->method_decl.modifiers & (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT))
        flags |= ZAN_REFL_MF_VIRTUAL;
    flags |= refl_accessor_flag(sym, msym);
    LLVMValueRef thunk = LLVMConstNull(i8ptr);
    if (late) {
        int fi = irgen_find_function(g, msym);
        if (fi >= 0) {
            LLVMValueRef t = refl_make_thunk(g, g->functions[fi].fn,
                                             g->functions[fi].fn_type,
                                             is_static, "m");
            if (t) thunk = t;
        }
    }
    return LLVMConstNamedStruct(mrec, (LLVMValueRef[]){
        refl_const_string(g, msym->name.str, (int)msym->name.len),
        refl_field_type_name(g, rt),
        LLVMConstInt(i64, (unsigned long long)(
            rt && rt->kind == TYPE_VOID ? 0 : refl_field_kind(rt)), 0),
        LLVMConstInt(i64, (unsigned long long)decl->method_decl.params.count, 0),
        refl_param_names(g, &decl->method_decl.params),
        thunk,
        LLVMConstInt(i64, (unsigned long long)flags, 0) }, 7);
}

/* void thunk(i8 *unused, i64 *args, i64 *ret): allocate an instance of `sym`,
 * install its vtable and run `ctor` over the argument slots, exactly as
 * `new T(...)` does, then hand the object back in the result slot. */
static LLVMValueRef refl_make_ctor_thunk(zan_irgen_t *g, zan_symbol_t *sym,
                                         LLVMValueRef ctor_fn,
                                         LLVMTypeRef ctor_ty) {
    if (!sym || !sym->type || sym->type->kind != TYPE_CLASS) return NULL;
    LLVMTypeRef st = get_struct_llvm_type(g, sym);
    if (!st || !ctor_fn || !ctor_ty) return NULL;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);

    unsigned total = LLVMCountParamTypes(ctor_ty);
    if (total < 1 || total > ZAN_REFL_ARG_SLOTS + 1) return NULL;
    LLVMTypeRef ptypes[ZAN_REFL_ARG_SLOTS + 1];
    LLVMGetParamTypes(ctor_ty, ptypes);

    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.thunk.new.%d", g->refl_thunk_count++);
    LLVMValueRef th = LLVMAddFunction(g->mod, nm,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                         (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0));
    LLVMSetLinkage(th, LLVMInternalLinkage);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMPositionBuilderAtEnd(g->builder,
        LLVMAppendBasicBlockInContext(g->ctx, th, "entry"));

    int site = reserve_arc_site(g, sym, sym->type, 0, NULL);
    LLVMValueRef raw = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0),
        g->rt_alloc, (LLVMValueRef[]){ LLVMSizeOf(st),
            LLVMConstInt(i64, (unsigned long long)site, 0),
            LLVMConstNull(i8ptr) }, 3, "refl.newobj");
    LLVMValueRef obj = LLVMBuildBitCast(g->builder, raw,
                                        LLVMPointerType(st, 0), "refl.objp");
    LLVMBuildStore(g->builder, LLVMConstNull(st), obj);
    if (class_has_virtual_methods(sym)) {
        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, st, obj, 0, "refl.vpf");
        LLVMBuildStore(g->builder,
            LLVMBuildBitCast(g->builder, get_vtable_global(g, sym), i8ptr,
                             "refl.vt"), vp);
    }

    bool ok = true;
    LLVMValueRef args[ZAN_REFL_ARG_SLOTS + 1];
    args[0] = LLVMBuildBitCast(g->builder, obj, ptypes[0], "refl.this");
    for (unsigned i = 1; i < total && ok; i++) {
        LLVMValueRef k = LLVMConstInt(i64, (unsigned long long)(i - 1), 0);
        LLVMValueRef sp = LLVMBuildGEP2(g->builder, i64, LLVMGetParam(th, 1),
                                        &k, 1, "refl.as");
        args[i] = refl_slot_to(g,
            LLVMBuildLoad2(g->builder, i64, sp, "refl.av"), ptypes[i], &ok);
    }
    if (!ok) {
        LLVMDeleteFunction(th);
        if (save) LLVMPositionBuilderAtEnd(g->builder, save);
        return NULL;
    }
    zan_call2(g->builder, ctor_ty, ctor_fn, args, total, "");
    LLVMBuildStore(g->builder,
        LLVMBuildPtrToInt(g->builder, raw, i64, "refl.newi"),
        LLVMGetParam(th, 2));
    LLVMBuildRetVoid(g->builder);
    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return LLVMConstBitCast(th, i8ptr);
}

/* One constructor record: same shape as a method record, named ".ctor". */
static LLVMValueRef refl_ctor_record(zan_irgen_t *g, zan_symbol_t *sym,
                                     zan_ast_node_t *decl, bool late) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef thunk = LLVMConstNull(i8ptr);
    if (late) {
        for (int i = 0; i < g->ctor_count; i++) {
            if (g->ctors[i].type_sym != sym || g->ctors[i].decl != decl)
                continue;
            LLVMValueRef t = refl_make_ctor_thunk(g, sym, g->ctors[i].fn,
                                                  g->ctors[i].fn_type);
            if (t) thunk = t;
            break;
        }
    }
    return LLVMConstNamedStruct(refl_method_type(g), (LLVMValueRef[]){
        refl_const_string(g, ".ctor", 5),
        refl_const_string(g, sym->name.str, (int)sym->name.len),
        LLVMConstInt(i64, ZAN_REFL_FK_REF, 0),
        LLVMConstInt(i64, (unsigned long long)decl->method_decl.params.count, 0),
        refl_param_names(g, &decl->method_decl.params),
        thunk,
        LLVMConstInt(i64, 0, 0) }, 7);
}

/* The initializer of one shaped table, built from the symbol table (early) or
 * with the thunks resolved as well (late). */
static LLVMValueRef refl_mtab_init(zan_irgen_t *g, zan_symbol_t *sym,
                                   bool ctors, int n, bool late) {
    LLVMTypeRef mrec = refl_method_type(g);
    LLVMValueRef *recs = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    if (!recs) return NULL;
    if (ctors) {
        zan_ast_node_t **decls = (zan_ast_node_t **)calloc((size_t)n,
                                                    sizeof(zan_ast_node_t *));
        int got = decls ? refl_collect_ctors(sym, decls, n) : 0;
        for (int i = 0; i < n; i++)
            recs[i] = i < got ? refl_ctor_record(g, sym, decls[i], late)
                              : LLVMConstNull(mrec);
        free(decls);
    } else {
        zan_symbol_t **msyms = (zan_symbol_t **)calloc((size_t)n,
                                                    sizeof(zan_symbol_t *));
        int got = msyms ? refl_collect_methods(sym, msyms, n) : 0;
        for (int i = 0; i < n; i++)
            recs[i] = i < got ? refl_method_record(g, sym, msyms[i], late)
                              : LLVMConstNull(mrec);
        free(msyms);
    }
    LLVMValueRef init = LLVMConstArray(mrec, recs, (unsigned)n);
    free(recs);
    return init;
}

/* Shape the method (or constructor) table of `sym`: the record count comes out
 * of the symbol table, so the array's type is final here, but the thunks are
 * only bound in refl_finalize_mtabs -- a typeof(T) in a top-level function is
 * lowered before the class's methods are declared. */
static LLVMValueRef refl_shape_mtab(zan_irgen_t *g, zan_symbol_t *sym,
                                    bool ctors, int *out_count) {
    *out_count = 0;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!sym) return LLVMConstNull(i8ptr);
    int n = 0;
    if (ctors) {
        if (!sym->decl) return LLVMConstNull(i8ptr);
        zan_ast_list_t *ms = &sym->decl->type_decl.members;
        for (int i = 0; i < ms->count; i++)
            if (ms->items[i]->kind == AST_CONSTRUCTOR_DECL &&
                !(ms->items[i]->method_decl.modifiers & MOD_STATIC))
                n++;
    } else {
        for (int i = 0; i < sym->member_count; i++) {
            zan_symbol_t *m = sym->members[i];
            if (m && m->kind == SYM_METHOD && m->decl &&
                m->decl->kind == AST_METHOD_DECL)
                n++;
        }
    }
    if (n <= 0) return LLVMConstNull(i8ptr);

    LLVMTypeRef arr_ty = LLVMArrayType(refl_method_type(g), (unsigned)n);
    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.%s.%.*s.%d", ctors ? "ctors" : "mtab",
             (int)sym->name.len, sym->name.str, g->refl_mtab_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstNull(arr_ty));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetAlignment(gv, 8);

    if (g->refl_mtab_count == g->refl_mtab_cap) {
        int ncap = g->refl_mtab_cap ? g->refl_mtab_cap * 2 : 16;
        g->refl_mtabs = (typeof(g->refl_mtabs))realloc(g->refl_mtabs,
            (size_t)ncap * sizeof(*g->refl_mtabs));
        g->refl_mtab_cap = ncap;
    }
    g->refl_mtabs[g->refl_mtab_count].gv = gv;
    g->refl_mtabs[g->refl_mtab_count].arr_ty = arr_ty;
    g->refl_mtabs[g->refl_mtab_count].sym = sym;
    g->refl_mtabs[g->refl_mtab_count].n = n;
    g->refl_mtabs[g->refl_mtab_count].ctors = ctors;
    g->refl_mtab_count++;
    *out_count = n;
    return LLVMConstBitCast(gv, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
}

/* The type record for `t` under the display name `disp`, emitted once. */
static LLVMValueRef refl_meta_for(zan_irgen_t *g, zan_type_t *t,
                                  const char *disp, int disp_len) {
    char namebuf[256];
    if (!disp || disp_len <= 0) {
        int n = 0;
        if (t && t->name.len > 0) {
            n = (int)t->name.len;
            if (n > (int)sizeof(namebuf) - 3) n = (int)sizeof(namebuf) - 3;
            memcpy(namebuf, t->name.str, (size_t)n);
        } else {
            memcpy(namebuf, "?", 1);
            n = 1;
        }
        if (t && t->kind == TYPE_ARRAY) { namebuf[n++] = '['; namebuf[n++] = ']'; }
        namebuf[n] = 0;
        disp = namebuf;
        disp_len = n;
    }
    zan_symbol_t *sym = t ? t->sym : NULL;
    for (int i = 0; i < g->refl_meta_count; i++) {
        if (g->refl_metas[i].sym == sym &&
            strncmp(g->refl_metas[i].name, disp, (size_t)disp_len) == 0 &&
            g->refl_metas[i].name[disp_len] == 0)
            return g->refl_metas[i].rec;
    }

    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

    LLVMTypeRef rec_ty = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){
        i8ptr, i64, i8ptr, i64, i8ptr, i8ptr, i8ptr, i64, i64, i64, i64,
        LLVMArrayType(i8, (unsigned)(disp_len + 1)) }, 12, 0);
    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.type.%d", g->refl_meta_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, rec_ty, nm);
    LLVMSetInitializer(gv, LLVMConstNull(rec_ty));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetAlignment(gv, 8);
    LLVMValueRef idx[3] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 11, 0),
                            LLVMConstInt(i64, 0, 0) };
    LLVMValueRef rec = LLVMConstGEP2(rec_ty, gv, idx, 3);

    /* Registered before the initializer is built: the element and generic
     * argument records are records themselves, and a type can name itself
     * (`class Node { Node[] kids; }`), which would otherwise recurse. */
    if (g->refl_meta_count == g->refl_meta_cap) {
        int ncap = g->refl_meta_cap ? g->refl_meta_cap * 2 : 32;
        g->refl_metas = (typeof(g->refl_metas))realloc(g->refl_metas,
            (size_t)ncap * sizeof(*g->refl_metas));
        g->refl_meta_cap = ncap;
    }
    g->refl_metas[g->refl_meta_count].sym = sym;
    g->refl_metas[g->refl_meta_count].name =
        zan_arena_strdup(g->arena, disp, (size_t)disp_len);
    g->refl_metas[g->refl_meta_count].rec = rec;
    g->refl_meta_count++;
    g->refl_used = true;

    int field_count = 0;
    LLVMValueRef fields = NULL;
    if (t && sym) {
        if (t->kind == TYPE_ENUM)
            fields = refl_emit_fields_enum(g, sym, &field_count);
        else if (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT)
            fields = refl_emit_fields_class(g, sym, &field_count);
    }
    if (!fields) fields = LLVMConstNull(i8ptr);

    int method_count = 0, ctor_count = 0;
    LLVMValueRef methods = LLVMConstNull(i8ptr), ctors = LLVMConstNull(i8ptr);
    if (t && sym && (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT ||
                     t->kind == TYPE_INTERFACE)) {
        methods = refl_shape_mtab(g, sym, false, &method_count);
        ctors = refl_shape_mtab(g, sym, true, &ctor_count);
    }

    /* An array's element type and a nullable's payload type are records of
     * their own, so `typeof(int[]).ElementType.Name` is answerable. */
    LLVMValueRef elem = LLVMConstNull(i8ptr);
    if (t && (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) &&
        t->element_type)
        elem = refl_meta_for(g, t->element_type, NULL, 0);

    /* The generic arguments, as a null-terminated array of records. */
    LLVMValueRef targs = LLVMConstNull(i8ptr);
    if (t && t->type_arg_count > 0 && t->type_args) {
        int n = t->type_arg_count;
        LLVMValueRef *recs = (LLVMValueRef *)calloc((size_t)n + 1,
                                                    sizeof(LLVMValueRef));
        if (recs) {
            for (int i = 0; i < n; i++)
                recs[i] = t->type_args[i]
                    ? refl_meta_for(g, t->type_args[i], NULL, 0)
                    : LLVMConstNull(i8ptr);
            recs[n] = LLVMConstNull(i8ptr);
            LLVMTypeRef arr_ty = LLVMArrayType(i8ptr, (unsigned)n + 1);
            char tn[64];
            snprintf(tn, sizeof(tn), "__zan.refl.targs.%d", g->refl_str_count++);
            LLVMValueRef tgv = LLVMAddGlobal(g->mod, arr_ty, tn);
            LLVMSetInitializer(tgv, LLVMConstArray(i8ptr, recs,
                                                   (unsigned)n + 1));
            LLVMSetLinkage(tgv, LLVMPrivateLinkage);
            LLVMSetGlobalConstant(tgv, 1);
            free(recs);
            targs = LLVMConstBitCast(tgv, i8ptr);
        }
    }

    LLVMValueRef *chars = (LLVMValueRef *)calloc((size_t)disp_len + 1,
                                                sizeof(LLVMValueRef));
    if (!chars) return rec;
    for (int i = 0; i < disp_len; i++)
        chars[i] = LLVMConstInt(i8, (unsigned char)disp[i], 0);
    chars[disp_len] = LLVMConstInt(i8, 0, 0);
    LLVMSetInitializer(gv, LLVMConstNamedStruct(rec_ty, (LLVMValueRef[]){
        methods,
        LLVMConstInt(i64, (unsigned long long)method_count, 0),
        ctors,
        LLVMConstInt(i64, (unsigned long long)ctor_count, 0),
        elem,
        targs,
        fields,
        LLVMConstInt(i64, (unsigned long long)field_count, 0),
        LLVMConstInt(i64, (unsigned long long)refl_type_kind(t), 0),
        LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
        LLVMConstInt(i64, ZAN_STR_HDR_WORD(disp_len), 0),
        LLVMConstArray(i8, chars, (unsigned)(disp_len + 1)) }, 12));
    free(chars);
    return rec;
}

/* Bind the thunks of every shaped method/constructor table. Called once the
 * module's functions all exist (irgen_emit.c), which is the earliest point at
 * which a reflected call can be wired to the real function. */
static void refl_finalize_mtabs(zan_irgen_t *g) {
    for (int i = 0; i < g->refl_mtab_count; i++) {
        LLVMValueRef init = refl_mtab_init(g, g->refl_mtabs[i].sym,
                                           g->refl_mtabs[i].ctors,
                                           g->refl_mtabs[i].n, true);
        if (init) LLVMSetInitializer(g->refl_mtabs[i].gv, init);
    }
}

/* ---- record readers -------------------------------------------------- */

/* Load one i64 header slot of the record `ti` points into. */
static LLVMValueRef refl_hdr_i64(zan_irgen_t *g, LLVMValueRef ti, int off,
                                 const char *name) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef d = LLVMConstInt(i64, (unsigned long long)(long long)off, 1);
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, ti, &d, 1, "refl.hp");
    p = LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), "refl.hpc");
    return LLVMBuildLoad2(g->builder, i64, p, name);
}

static LLVMValueRef refl_hdr_ptr(zan_irgen_t *g, LLVMValueRef ti, int off,
                                 const char *name) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef d = LLVMConstInt(i64, (unsigned long long)(long long)off, 1);
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, ti, &d, 1, "refl.pp");
    p = LLVMBuildBitCast(g->builder, p, LLVMPointerType(i8ptr, 0), "refl.ppc");
    return LLVMBuildLoad2(g->builder, i8ptr, p, name);
}

static LLVMValueRef refl_empty_string(zan_irgen_t *g) {
    if (!g->refl_empty_str) g->refl_empty_str = refl_const_string(g, "", 0);
    return g->refl_empty_str;
}

/* i64 __zan_refl_find(i8 *ti, i8 *name): the index of the field record named
 * `name`, or -1. Built once per module. */
static LLVMValueRef refl_find_fn(zan_irgen_t *g) {
    if (g->fn_refl_find) return g->fn_refl_find;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_find", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_find = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head  = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body  = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef want = LLVMGetParam(fn, 1);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
    LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.count");
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.i");
    LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntSLT, i, count, "refl.more");
    LLVMBuildCondBr(g->builder, more, body, miss);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef byte_off = LLVMBuildMul(g->builder, i,
        LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo");
    LLVMValueRef fe = LLVMBuildGEP2(g->builder, i8, fields, &byte_off, 1, "refl.fe");
    LLVMValueRef fname = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildBitCast(g->builder, fe, LLVMPointerType(i8ptr, 0), "refl.fnp"),
        "refl.fn");
    LLVMValueRef c = zan_call2(g->builder,
        LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        g->fn_strcmp, (LLVMValueRef[]){ fname, want }, 2, "refl.cmp");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, c, LLVMConstInt(i32, 0, 0), "refl.hit"),
        found, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef inext = LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0),
                                      "refl.i.n");
    LLVMBuildBr(g->builder, head);
    LLVMAddIncoming(i, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), inext },
                    (LLVMBasicBlockRef[]){ entry, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMBuildRet(g->builder, i);
    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, (unsigned long long)-1LL, 1));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* Defined with the method-table readers below: a reflected member read tries
 * the property's real getter first, and only then the backing slot. */
static LLVMValueRef refl_pget_fn(zan_irgen_t *g);

/* The value a getter returned, converted the way the caller wants it. `kind` is
 * the getter's return kind, `slot` its raw result. */
static LLVMValueRef refl_pget_as_i64(zan_irgen_t *g, LLVMValueRef slot,
                                     LLVMValueRef kind) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMValueRef is_fp = LLVMBuildOr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                      LLVMConstInt(i64, ZAN_REFL_FK_FLOAT, 0), "refl.pf"),
        LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                      LLVMConstInt(i64, ZAN_REFL_FK_DOUBLE, 0), "refl.pd"),
        "refl.pfp");
    return LLVMBuildSelect(g->builder, is_fp,
        LLVMBuildFPToSI(g->builder,
            LLVMBuildBitCast(g->builder, slot, dbl, "refl.pbc"), i64,
            "refl.pf2i"), slot, "refl.pi");
}

static LLVMValueRef refl_pget_as_f64(zan_irgen_t *g, LLVMValueRef slot,
                                     LLVMValueRef kind) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMValueRef is_fp = LLVMBuildOr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                      LLVMConstInt(i64, ZAN_REFL_FK_FLOAT, 0), "refl.pf"),
        LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                      LLVMConstInt(i64, ZAN_REFL_FK_DOUBLE, 0), "refl.pd"),
        "refl.pfp");
    return LLVMBuildSelect(g->builder, is_fp,
        LLVMBuildBitCast(g->builder, slot, dbl, "refl.pbc"),
        LLVMBuildSIToFP(g->builder, slot, dbl, "refl.pi2f"), "refl.pdv");
}

/* The reflected getter call shared by the three field readers: `*out_kind` is
 * the getter's return kind (0 when the type has no such property getter). */
static LLVMValueRef refl_call_pget(zan_irgen_t *g, LLVMValueRef ti,
                                   LLVMValueRef obj, LLVMValueRef name,
                                   LLVMValueRef *out_kind) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMValueRef kslot = LLVMBuildAlloca(g->builder, i64, "refl.pk");
    LLVMValueRef v = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64ptr },
                         4, 0),
        refl_pget_fn(g), (LLVMValueRef[]){ ti, obj, name, kslot }, 4, "refl.pv");
    *out_kind = LLVMBuildLoad2(g->builder, i64, kslot, "refl.pkv");
    return v;
}

/* i64 __zan_refl_get_i64(i8 *ti, i8 *obj, i8 *name): the named field of `obj`
 * read as an integer (0 when there is no such field, or when it holds
 * something no integer can represent). An enum member's constant needs no
 * object. */
static LLVMValueRef refl_get_i64_fn(zan_irgen_t *g) {
    if (g->fn_refl_get_i64) return g->fn_refl_get_i64;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i16 = LLVMInt16TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_i64", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_get_i64 = fn;
    LLVMValueRef find = refl_find_fn(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef have  = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMBasicBlockRef konst = LLVMAppendBasicBlockInContext(g->ctx, fn, "const");
    LLVMBasicBlockRef load  = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
    LLVMBasicBlockRef zero  = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef obj = LLVMGetParam(fn, 1);
    LLVMValueRef name = LLVMGetParam(fn, 2);

    LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
    LLVMBasicBlockRef lookup = LLVMAppendBasicBlockInContext(g->ctx, fn, "lookup");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    /* a property with a real getter is read THROUGH it, not off its slot */
    LLVMValueRef pk = NULL;
    LLVMValueRef pv = refl_call_pget(g, ti, obj, name, &pk);
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntNE, pk, LLVMConstInt(i64, 0, 0),
                      "refl.hasprop"), prop, lookup);

    LLVMPositionBuilderAtEnd(g->builder, prop);
    LLVMBuildRet(g->builder, refl_pget_as_i64(g, pv, pk));

    LLVMPositionBuilderAtEnd(g->builder, lookup);
    LLVMValueRef idx = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        find, (LLVMValueRef[]){ ti, name }, 2, "refl.idx");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, LLVMConstInt(i64, 0, 0),
                      "refl.nf"),
        zero, have);

    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
    LLVMValueRef byte_off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo");
    LLVMValueRef fe = LLVMBuildGEP2(g->builder, i8, fields, &byte_off, 1, "refl.fe");
    LLVMValueRef kind = refl_hdr_i64(g, fe, 16, "refl.kind");
    LLVMValueRef foff = refl_hdr_i64(g, fe, 24, "refl.off");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                      LLVMConstInt(i64, ZAN_REFL_FK_ENUM_MEMBER, 0), "refl.isem"),
        konst, load);

    LLVMPositionBuilderAtEnd(g->builder, konst);
    LLVMBuildRet(g->builder, foff);

    LLVMPositionBuilderAtEnd(g->builder, zero);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    LLVMPositionBuilderAtEnd(g->builder, load);
    LLVMBasicBlockRef slot = LLVMAppendBasicBlockInContext(g->ctx, fn, "slot");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, obj, "refl.objnull"), zero, slot);

    LLVMPositionBuilderAtEnd(g->builder, slot);
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.slot");

    /* one block per load width; the kind decides the extension */
    struct { int kind; LLVMTypeRef ty; int sext; const char *nm; } cases[] = {
        { ZAN_REFL_FK_BOOL,   i8,  0, "b" },
        { ZAN_REFL_FK_BYTE,   i8,  0, "u8" },
        { ZAN_REFL_FK_SBYTE,  i8,  1, "i8" },
        { ZAN_REFL_FK_SHORT,  i16, 1, "i16" },
        { ZAN_REFL_FK_USHORT, i16, 0, "u16" },
        { ZAN_REFL_FK_INT,    i32, 1, "i32" },
        { ZAN_REFL_FK_UINT,   i32, 0, "u32" },
        { ZAN_REFL_FK_LONG,   i64, 1, "i64" },
        { ZAN_REFL_FK_ULONG,  i64, 0, "u64" },
        { ZAN_REFL_FK_CHAR,   i64, 1, "ch" },
        { ZAN_REFL_FK_ENUM,   i64, 1, "en" },
        { ZAN_REFL_FK_FLOAT,  flt, 0, "f32" },
        { ZAN_REFL_FK_DOUBLE, dbl, 0, "f64" },
    };
    int ncases = (int)(sizeof(cases) / sizeof(cases[0]));
    LLVMValueRef sw = LLVMBuildSwitch(g->builder, kind, zero, (unsigned)ncases);
    for (int ci = 0; ci < ncases; ci++) {
        char bn[24];
        snprintf(bn, sizeof(bn), "ld.%s", cases[ci].nm);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, bn);
        LLVMAddCase(sw, LLVMConstInt(i64, (unsigned long long)cases[ci].kind, 0), bb);
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef v = LLVMBuildLoad2(g->builder, cases[ci].ty,
            LLVMBuildBitCast(g->builder, p,
                LLVMPointerType(cases[ci].ty, 0), "refl.sp"), "refl.v");
        if (cases[ci].ty == flt || cases[ci].ty == dbl)
            v = LLVMBuildFPToSI(g->builder, v, i64, "refl.f2i");
        else if (cases[ci].ty != i64)
            v = cases[ci].sext ? LLVMBuildSExt(g->builder, v, i64, "refl.sx")
                               : LLVMBuildZExt(g->builder, v, i64, "refl.zx");
        LLVMBuildRet(g->builder, v);
    }

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* double __zan_refl_get_f64(i8 *ti, i8 *obj, i8 *name): a float/double field
 * unconverted; anything else goes through the integer reader. */
static LLVMValueRef refl_get_f64_fn(zan_irgen_t *g) {
    if (g->fn_refl_get_f64) return g->fn_refl_get_f64;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(dbl,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_f64", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_get_f64 = fn;
    LLVMValueRef find = refl_find_fn(g);
    LLVMValueRef geti = refl_get_i64_fn(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef have  = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMBasicBlockRef ld32  = LLVMAppendBasicBlockInContext(g->ctx, fn, "f32");
    LLVMBasicBlockRef ld64  = LLVMAppendBasicBlockInContext(g->ctx, fn, "f64");
    LLVMBasicBlockRef via_i = LLVMAppendBasicBlockInContext(g->ctx, fn, "via_int");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef obj = LLVMGetParam(fn, 1);
    LLVMValueRef name = LLVMGetParam(fn, 2);

    LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
    LLVMBasicBlockRef notprop = LLVMAppendBasicBlockInContext(g->ctx, fn, "notprop");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef pk = NULL;
    LLVMValueRef pv = refl_call_pget(g, ti, obj, name, &pk);
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntNE, pk, LLVMConstInt(i64, 0, 0),
                      "refl.hasprop"), prop, notprop);

    LLVMPositionBuilderAtEnd(g->builder, prop);
    LLVMBuildRet(g->builder, refl_pget_as_f64(g, pv, pk));

    LLVMPositionBuilderAtEnd(g->builder, notprop);
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, obj, "refl.objnull"), via_i, have);

    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef idx = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        find, (LLVMValueRef[]){ ti, name }, 2, "refl.idx");
    LLVMBasicBlockRef kinds = LLVMAppendBasicBlockInContext(g->ctx, fn, "kinds");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, LLVMConstInt(i64, 0, 0),
                      "refl.nf"),
        via_i, kinds);

    LLVMPositionBuilderAtEnd(g->builder, kinds);
    LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
    LLVMValueRef byte_off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo");
    LLVMValueRef fe = LLVMBuildGEP2(g->builder, i8, fields, &byte_off, 1, "refl.fe");
    LLVMValueRef kind = refl_hdr_i64(g, fe, 16, "refl.kind");
    LLVMValueRef foff = refl_hdr_i64(g, fe, 24, "refl.off");
    LLVMValueRef sw = LLVMBuildSwitch(g->builder, kind, via_i, 2);
    LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_FLOAT, 0), ld32);
    LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_DOUBLE, 0), ld64);

    LLVMPositionBuilderAtEnd(g->builder, ld32);
    LLVMValueRef p32 = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.s32");
    LLVMValueRef v32 = LLVMBuildLoad2(g->builder, flt,
        LLVMBuildBitCast(g->builder, p32, LLVMPointerType(flt, 0), "refl.p32"),
        "refl.v32");
    LLVMBuildRet(g->builder, LLVMBuildFPExt(g->builder, v32, dbl, "refl.ext"));

    LLVMPositionBuilderAtEnd(g->builder, ld64);
    LLVMValueRef p64 = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.s64");
    LLVMBuildRet(g->builder, LLVMBuildLoad2(g->builder, dbl,
        LLVMBuildBitCast(g->builder, p64, LLVMPointerType(dbl, 0), "refl.p64"),
        "refl.v64"));

    LLVMPositionBuilderAtEnd(g->builder, via_i);
    LLVMValueRef iv = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0),
        geti, (LLVMValueRef[]){ ti, obj, name }, 3, "refl.iv");
    LLVMBuildRet(g->builder, LLVMBuildSIToFP(g->builder, iv, dbl, "refl.i2f"));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i8 *__zan_refl_get_str(i8 *ti, i8 *obj, i8 *name): a string field, or "" for
 * anything else (never null, so the result is printable/concatenable).
 * The result is returned owned (+1), like every other call: the field keeps its
 * own reference, so the caller's release cannot free the object's string. */
static LLVMValueRef refl_get_str_fn(zan_irgen_t *g) {
    if (g->fn_refl_get_str) return g->fn_refl_get_str;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_str", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_get_str = fn;
    LLVMValueRef find = refl_find_fn(g);
    LLVMValueRef empty = refl_empty_string(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef have  = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMBasicBlockRef load  = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
    LLVMBasicBlockRef none  = LLVMAppendBasicBlockInContext(g->ctx, fn, "none");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef obj = LLVMGetParam(fn, 1);
    LLVMValueRef name = LLVMGetParam(fn, 2);

    LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
    LLVMBasicBlockRef notprop = LLVMAppendBasicBlockInContext(g->ctx, fn, "notprop");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef pk = NULL;
    LLVMValueRef pv = refl_call_pget(g, ti, obj, name, &pk);
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                      LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), "refl.propstr"),
        prop, notprop);

    LLVMTypeRef arc_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                          (LLVMTypeRef[]){ i8ptr }, 1, 0);

    LLVMPositionBuilderAtEnd(g->builder, prop);
    LLVMValueRef psv = LLVMBuildIntToPtr(g->builder, pv, i8ptr, "refl.psv");
    LLVMValueRef psr = LLVMBuildSelect(g->builder,
        LLVMBuildIsNull(g->builder, psv, "refl.psnull"), empty, psv, "refl.ps");
    zan_call2(g->builder, arc_ty, g->rt_str_retain, &psr, 1, "");
    LLVMBuildRet(g->builder, psr);

    LLVMPositionBuilderAtEnd(g->builder, notprop);
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, obj, "refl.objnull"), none, have);

    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef idx = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        find, (LLVMValueRef[]){ ti, name }, 2, "refl.idx");
    LLVMBasicBlockRef chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "chk");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, LLVMConstInt(i64, 0, 0),
                      "refl.nf"),
        none, chk);

    LLVMPositionBuilderAtEnd(g->builder, chk);
    LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
    LLVMValueRef byte_off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo");
    LLVMValueRef fe = LLVMBuildGEP2(g->builder, i8, fields, &byte_off, 1, "refl.fe");
    LLVMValueRef kind = refl_hdr_i64(g, fe, 16, "refl.kind");
    LLVMValueRef foff = refl_hdr_i64(g, fe, 24, "refl.off");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                      LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), "refl.isstr"),
        load, none);

    LLVMPositionBuilderAtEnd(g->builder, load);
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.slot");
    LLVMValueRef sv = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildBitCast(g->builder, p, LLVMPointerType(i8ptr, 0), "refl.sp"),
        "refl.sv");
    /* an unassigned string field is null; answer "" instead */
    LLVMValueRef svr = LLVMBuildSelect(g->builder,
        LLVMBuildIsNull(g->builder, sv, "refl.snull"), empty, sv, "refl.str");
    zan_call2(g->builder, arc_ty, g->rt_str_retain, &svr, 1, "");
    LLVMBuildRet(g->builder, svr);

    LLVMPositionBuilderAtEnd(g->builder, none);
    LLVMBuildRet(g->builder, empty);

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i8 *__zan_refl_fname(i8 *ti, i64 idx, i64 which): the name (which=0) or the
 * declared type name (which=1) of field `idx`, or "" when out of range. */
static LLVMValueRef refl_fname_fn(zan_irgen_t *g) {
    if (g->fn_refl_fname) return g->fn_refl_fname;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i64, i64 }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_fname", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_fname = fn;
    LLVMValueRef empty = refl_empty_string(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef load  = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
    LLVMBasicBlockRef bad   = LLVMAppendBasicBlockInContext(g->ctx, fn, "bad");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef idx = LLVMGetParam(fn, 1);
    LLVMValueRef which = LLVMGetParam(fn, 2);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.count");
    LLVMValueRef ok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.ge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
    LLVMBuildCondBr(g->builder, ok, load, bad);

    LLVMPositionBuilderAtEnd(g->builder, load);
    LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
    LLVMValueRef byte_off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo");
    LLVMValueRef sel = LLVMBuildSelect(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                      LLVMConstInt(i64, ZAN_REFL_WHICH_NAME, 0), "refl.isname"),
        LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 8, 0), "refl.sel");
    byte_off = LLVMBuildAdd(g->builder, byte_off, sel, "refl.fo2");
    LLVMValueRef fe = LLVMBuildGEP2(g->builder, i8, fields, &byte_off, 1, "refl.fe");
    LLVMValueRef sv = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildBitCast(g->builder, fe, LLVMPointerType(i8ptr, 0), "refl.fp"),
        "refl.fv");
    LLVMBuildRet(g->builder, LLVMBuildSelect(g->builder,
        LLVMBuildIsNull(g->builder, sv, "refl.fnull"), empty, sv, "refl.name"));

    LLVMPositionBuilderAtEnd(g->builder, bad);
    LLVMBuildRet(g->builder, empty);

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i8 *__zan_refl_obj_type(i8 *obj, i8 *fallback): the record recorded for the
 * object's allocation site — its CONCRETE type, so a base-typed variable
 * holding a derived instance reports the derived type. Falls back to the static
 * type's record for anything with no usable site word (a string, a borrowed
 * buffer, an object allocated in another module). */
static LLVMValueRef refl_obj_type_fn(zan_irgen_t *g) {
    if (g->fn_refl_obj_type) return g->fn_refl_obj_type;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_obj_type", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_obj_type = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef tbl   = LLVMAppendBasicBlockInContext(g->ctx, fn, "tbl");
    LLVMBasicBlockRef hit   = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef fb    = LLVMAppendBasicBlockInContext(g->ctx, fn, "fallback");

    LLVMValueRef obj = LLVMGetParam(fn, 0);
    LLVMValueRef fallback = LLVMGetParam(fn, 1);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, obj, "refl.objnull"), fb, tbl);

    LLVMPositionBuilderAtEnd(g->builder, tbl);
    LLVMValueRef site = refl_hdr_i64(g, obj, ZAN_OBJ_SITE_OFF, "refl.site");
    /* site 0 means "not recorded"; the string magic and any garbage are out of
     * range for the unsigned compare */
    LLVMValueRef in_range = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntUGT, site, LLVMConstInt(i64, 0, 0), "refl.s0"),
        LLVMBuildICmp(g->builder, LLVMIntULT, site,
                      LLVMConstInt(i64, ZAN_MAX_LEAK_SITES, 0), "refl.sn"),
        "refl.sok");
    LLVMBasicBlockRef ld = LLVMAppendBasicBlockInContext(g->ctx, fn, "ld");
    LLVMBuildCondBr(g->builder, in_range, ld, fb);

    LLVMPositionBuilderAtEnd(g->builder, ld);
    LLVMValueRef idxs[2] = { LLVMConstInt(i64, 0, 0), site };
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, g->site_meta_type,
                                      g->g_site_meta, idxs, 2, "refl.mslot");
    LLVMValueRef m = LLVMBuildLoad2(g->builder, i8ptr, slot, "refl.m");
    LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, m, "refl.mnull"), fb, hit);

    LLVMPositionBuilderAtEnd(g->builder, hit);
    LLVMBuildRet(g->builder, m);
    LLVMPositionBuilderAtEnd(g->builder, fb);
    LLVMBuildRet(g->builder, fallback);

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* ---- method-table readers -------------------------------------------- */

/* The method (tbl=0) or constructor (tbl=1) array of the record, and how many
 * records it holds. */
static void refl_table_of(zan_irgen_t *g, LLVMValueRef ti, LLVMValueRef tbl,
                          LLVMValueRef *out_arr, LLVMValueRef *out_count) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef is_m = LLVMBuildICmp(g->builder, LLVMIntEQ, tbl,
        LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), "refl.ism");
    *out_arr = LLVMBuildSelect(g->builder, is_m,
        refl_hdr_ptr(g, ti, ZAN_REFL_METHODS_OFF, "refl.mt"),
        refl_hdr_ptr(g, ti, ZAN_REFL_CTORS_OFF, "refl.ct"), "refl.tbl");
    *out_count = LLVMBuildSelect(g->builder, is_m,
        refl_hdr_i64(g, ti, ZAN_REFL_MCOUNT_OFF, "refl.mc"),
        refl_hdr_i64(g, ti, ZAN_REFL_CCOUNT_OFF, "refl.cc"), "refl.tc");
}

/* i64 __zan_refl_mfind(i8 *ti, i8 *name, i64 flags): the index of a method
 * record in the method table. With flags 0 the record name must match `name`
 * exactly; otherwise the record must carry one of the flags and its name match
 * `name` from its fifth character on, which is how the `get_`/`set_` accessor
 * of the property `name` is found. -1 when there is none. */
static LLVMValueRef refl_mfind_fn(zan_irgen_t *g) {
    if (g->fn_refl_mfind) return g->fn_refl_mfind;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_mfind", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_mfind = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head  = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body  = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef cmp   = LLVMAppendBasicBlockInContext(g->ctx, fn, "cmp");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef want = LLVMGetParam(fn, 1);
    LLVMValueRef flags = LLVMGetParam(fn, 2);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef methods = refl_hdr_ptr(g, ti, ZAN_REFL_METHODS_OFF, "refl.mt");
    LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_MCOUNT_OFF, "refl.mc");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, methods, "refl.nomt"), miss, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.mi");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, i, count, "refl.more"), body, miss);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef off = LLVMBuildMul(g->builder, i,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.mo");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, methods, &off, 1, "refl.mr");
    LLVMValueRef mname = refl_hdr_ptr(g, rec, ZAN_REFL_MO_NAME, "refl.mn");
    LLVMValueRef mflags = refl_hdr_i64(g, rec, ZAN_REFL_MO_FLAGS, "refl.mf");
    /* an accessor lookup skips the `get_`/`set_` prefix of the record name */
    LLVMValueRef acc = LLVMBuildICmp(g->builder, LLVMIntNE, flags,
                                     LLVMConstInt(i64, 0, 0), "refl.isacc");
    LLVMValueRef four = LLVMBuildSelect(g->builder, acc,
        LLVMConstInt(i64, 4, 0), LLVMConstInt(i64, 0, 0), "refl.skip");
    LLVMValueRef cand = LLVMBuildGEP2(g->builder, i8, mname, &four, 1, "refl.mn2");
    LLVMValueRef flag_ok = LLVMBuildOr(g->builder,
        LLVMBuildNot(g->builder, acc, "refl.noacc"),
        LLVMBuildICmp(g->builder, LLVMIntNE,
            LLVMBuildAnd(g->builder, mflags, flags, "refl.fand"),
            LLVMConstInt(i64, 0, 0), "refl.fhit"), "refl.fok");
    LLVMBuildCondBr(g->builder, flag_ok, cmp, next);

    LLVMPositionBuilderAtEnd(g->builder, cmp);
    LLVMValueRef c = zan_call2(g->builder,
        LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        g->fn_strcmp, (LLVMValueRef[]){ cand, want }, 2, "refl.mcmp");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, c, LLVMConstInt(i32, 0, 0),
                      "refl.mhit"), found, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef inext = LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0),
                                      "refl.mi.n");
    LLVMBuildBr(g->builder, head);
    LLVMAddIncoming(i, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), inext },
                    (LLVMBasicBlockRef[]){ entry, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMBuildRet(g->builder, i);
    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, (unsigned long long)-1LL, 1));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i8 *__zan_refl_mstr(i8 *ti, i64 tbl, i64 idx, i64 which, i64 k): the name,
 * the return type name, or the k-th parameter type name of a method or
 * constructor record; "" when out of range. */
static LLVMValueRef refl_mstr_fn(zan_irgen_t *g) {
    if (g->fn_refl_mstr) return g->fn_refl_mstr;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i64, i64, i64, i64 }, 5, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_mstr", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_mstr = fn;
    LLVMValueRef empty = refl_empty_string(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef have  = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMBasicBlockRef par   = LLVMAppendBasicBlockInContext(g->ctx, fn, "param");
    LLVMBasicBlockRef own   = LLVMAppendBasicBlockInContext(g->ctx, fn, "own");
    LLVMBasicBlockRef bad   = LLVMAppendBasicBlockInContext(g->ctx, fn, "bad");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef tbl = LLVMGetParam(fn, 1);
    LLVMValueRef idx = LLVMGetParam(fn, 2);
    LLVMValueRef which = LLVMGetParam(fn, 3);
    LLVMValueRef k = LLVMGetParam(fn, 4);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = NULL, count = NULL;
    refl_table_of(g, ti, tbl, &arr, &count);
    LLVMValueRef ok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.ge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
    ok = LLVMBuildAnd(g->builder, ok,
        LLVMBuildIsNotNull(g->builder, arr, "refl.hastbl"), "refl.ok2");
    LLVMBuildCondBr(g->builder, ok, have, bad);

    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.mo");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "refl.mr");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                      LLVMConstInt(i64, ZAN_REFL_MW_PARAM, 0), "refl.isp"),
        par, own);

    LLVMPositionBuilderAtEnd(g->builder, own);
    LLVMValueRef sel = LLVMBuildSelect(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                      LLVMConstInt(i64, ZAN_REFL_MW_NAME, 0), "refl.isn"),
        LLVMConstInt(i64, ZAN_REFL_MO_NAME, 0),
        LLVMConstInt(i64, ZAN_REFL_MO_RET, 0), "refl.sel");
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, i8, rec, &sel, 1, "refl.sp");
    LLVMValueRef sv = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildBitCast(g->builder, sp, LLVMPointerType(i8ptr, 0), "refl.spc"),
        "refl.sv");
    LLVMBuildRet(g->builder, LLVMBuildSelect(g->builder,
        LLVMBuildIsNull(g->builder, sv, "refl.snull"), empty, sv, "refl.s"));

    LLVMPositionBuilderAtEnd(g->builder, par);
    LLVMValueRef pc = refl_hdr_i64(g, rec, ZAN_REFL_MO_PCOUNT, "refl.pc");
    LLVMValueRef ptypes = refl_hdr_ptr(g, rec, ZAN_REFL_MO_PTYPES, "refl.pt");
    LLVMValueRef pok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, k, LLVMConstInt(i64, 0, 0), "refl.kge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, k, pc, "refl.klt"), "refl.pok");
    pok = LLVMBuildAnd(g->builder, pok,
        LLVMBuildIsNotNull(g->builder, ptypes, "refl.haspt"), "refl.pok2");
    LLVMBasicBlockRef pld = LLVMAppendBasicBlockInContext(g->ctx, fn, "pload");
    LLVMBuildCondBr(g->builder, pok, pld, bad);

    LLVMPositionBuilderAtEnd(g->builder, pld);
    LLVMValueRef pp = LLVMBuildGEP2(g->builder, i8ptr,
        LLVMBuildBitCast(g->builder, ptypes, LLVMPointerType(i8ptr, 0), "refl.ptc"),
        &k, 1, "refl.pe");
    LLVMValueRef pv = LLVMBuildLoad2(g->builder, i8ptr, pp, "refl.pv");
    LLVMBuildRet(g->builder, LLVMBuildSelect(g->builder,
        LLVMBuildIsNull(g->builder, pv, "refl.pnull"), empty, pv, "refl.p"));

    LLVMPositionBuilderAtEnd(g->builder, bad);
    LLVMBuildRet(g->builder, empty);

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i64 __zan_refl_mi64(i8 *ti, i64 tbl, i64 idx, i64 which): the record count of
 * a table, or the parameter count / flags / return kind of one record. */
static LLVMValueRef refl_mi64_fn(zan_irgen_t *g) {
    if (g->fn_refl_mi64) return g->fn_refl_mi64;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i64, i64 }, 4, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_mi64", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_mi64 = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef cnt   = LLVMAppendBasicBlockInContext(g->ctx, fn, "count");
    LLVMBasicBlockRef one   = LLVMAppendBasicBlockInContext(g->ctx, fn, "one");
    LLVMBasicBlockRef zero  = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef tbl = LLVMGetParam(fn, 1);
    LLVMValueRef idx = LLVMGetParam(fn, 2);
    LLVMValueRef which = LLVMGetParam(fn, 3);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = NULL, count = NULL;
    refl_table_of(g, ti, tbl, &arr, &count);
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                      LLVMConstInt(i64, ZAN_REFL_MI_COUNT, 0), "refl.iscnt"),
        cnt, one);

    LLVMPositionBuilderAtEnd(g->builder, cnt);
    LLVMBuildRet(g->builder, count);

    LLVMPositionBuilderAtEnd(g->builder, one);
    LLVMValueRef ok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.ge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
    ok = LLVMBuildAnd(g->builder, ok,
        LLVMBuildIsNotNull(g->builder, arr, "refl.hastbl"), "refl.ok2");
    LLVMBasicBlockRef ld = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
    LLVMBuildCondBr(g->builder, ok, ld, zero);

    LLVMPositionBuilderAtEnd(g->builder, ld);
    LLVMValueRef off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.mo");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "refl.mr");
    LLVMValueRef pc = refl_hdr_i64(g, rec, ZAN_REFL_MO_PCOUNT, "refl.pc");
    LLVMValueRef fl = refl_hdr_i64(g, rec, ZAN_REFL_MO_FLAGS, "refl.fl");
    LLVMValueRef rk = refl_hdr_i64(g, rec, ZAN_REFL_MO_RETKIND, "refl.rk");
    LLVMValueRef v = LLVMBuildSelect(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                      LLVMConstInt(i64, ZAN_REFL_MI_PCOUNT, 0), "refl.ispc"),
        pc, LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                          LLVMConstInt(i64, ZAN_REFL_MI_FLAGS, 0), "refl.isfl"),
            fl, rk, "refl.v1"), "refl.v");
    LLVMBuildRet(g->builder, v);

    LLVMPositionBuilderAtEnd(g->builder, zero);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i64 __zan_refl_invoke(i8 *ti, i64 tbl, i64 idx, i8 *obj, i64 *args,
 *                       i64 *kindout): call the thunk of one record over the
 * argument slots. `kindout` receives 1 + the return kind (so 0 means "not
 * called": no such record, or one the thunk generator could not express), and
 * the result comes back in its slot form. */
static LLVMValueRef refl_invoke_fn(zan_irgen_t *g) {
    if (g->fn_refl_invoke) return g->fn_refl_invoke;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr, i64ptr, i64ptr }, 6, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_invoke", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_invoke = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef have  = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMBasicBlockRef call  = LLVMAppendBasicBlockInContext(g->ctx, fn, "call");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef tbl = LLVMGetParam(fn, 1);
    LLVMValueRef idx = LLVMGetParam(fn, 2);
    LLVMValueRef obj = LLVMGetParam(fn, 3);
    LLVMValueRef args = LLVMGetParam(fn, 4);
    LLVMValueRef kindout = LLVMGetParam(fn, 5);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = NULL, count = NULL;
    refl_table_of(g, ti, tbl, &arr, &count);
    LLVMValueRef ok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.ge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
    ok = LLVMBuildAnd(g->builder, ok,
        LLVMBuildIsNotNull(g->builder, arr, "refl.hastbl"), "refl.ok2");
    LLVMBuildCondBr(g->builder, ok, have, miss);

    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.mo");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "refl.mr");
    LLVMValueRef thunk = refl_hdr_ptr(g, rec, ZAN_REFL_MO_THUNK, "refl.th");
    LLVMValueRef rk = refl_hdr_i64(g, rec, ZAN_REFL_MO_RETKIND, "refl.rk");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, thunk, "refl.nothunk"), miss, call);

    LLVMPositionBuilderAtEnd(g->builder, call);
    LLVMValueRef ret = LLVMBuildAlloca(g->builder, i64, "refl.ret");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ret);
    LLVMTypeRef th_ty = LLVMFunctionType(vd,
        (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0);
    zan_call2(g->builder, th_ty,
        LLVMBuildBitCast(g->builder, thunk, LLVMPointerType(th_ty, 0), "refl.thf"),
        (LLVMValueRef[]){ obj, args, ret }, 3, "");
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, rk, LLVMConstInt(i64, 1, 0), "refl.k1"), kindout);
    LLVMBuildRet(g->builder, LLVMBuildLoad2(g->builder, i64, ret, "refl.rv"));

    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), kindout);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i64 __zan_refl_pget(i8 *ti, i8 *obj, i8 *name, i64 *kindout): the value of
 * the property `name` through its real getter. `kindout` receives the getter's
 * return kind, or 0 when the type has no such property getter -- the reflected
 * field readers then fall back to the backing slot. */
static LLVMValueRef refl_pget_fn(zan_irgen_t *g) {
    if (g->fn_refl_pget) return g->fn_refl_pget;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64ptr }, 4, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_pget", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_pget = fn;
    LLVMValueRef mfind = refl_mfind_fn(g);
    LLVMValueRef invoke = refl_invoke_fn(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef call  = LLVMAppendBasicBlockInContext(g->ctx, fn, "call");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef obj = LLVMGetParam(fn, 1);
    LLVMValueRef name = LLVMGetParam(fn, 2);
    LLVMValueRef kindout = LLVMGetParam(fn, 3);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef idx = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
        mfind, (LLVMValueRef[]){ ti, name,
            LLVMConstInt(i64, ZAN_REFL_MF_PROPGET, 0) }, 3, "refl.gi");
    LLVMValueRef ok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.gok"),
        LLVMBuildIsNotNull(g->builder, obj, "refl.gobj"), "refl.gok2");
    LLVMBuildCondBr(g->builder, ok, call, miss);

    LLVMPositionBuilderAtEnd(g->builder, call);
    LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64,
        LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0), "refl.gargs");
    LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.gk");
    LLVMValueRef v = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr,
                                               i64ptr, i64ptr }, 6, 0),
        invoke, (LLVMValueRef[]){ ti,
            LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), idx, obj, slots, kout },
        6, "refl.gv");
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64, kout, "refl.gkv");
    /* the invoke reports 1 + kind; a not-called getter reads as "no property" */
    LLVMBuildStore(g->builder,
        LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntEQ, k, LLVMConstInt(i64, 0, 0),
                          "refl.gnk"),
            LLVMConstInt(i64, 0, 0),
            LLVMBuildSub(g->builder, k, LLVMConstInt(i64, 1, 0), "refl.gk1"),
            "refl.gks"), kindout);
    LLVMBuildRet(g->builder, v);

    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), kindout);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i64 __zan_refl_set(i8 *ti, i8 *obj, i8 *name, i64 v, i64 vkind): write the
 * named member of `obj`, 1 when it was written. A property with a real setter
 * is written THROUGH that setter (its backing slot is not the property); an
 * ordinary field is converted to the field's own kind and stored. */
static LLVMValueRef refl_set_fn(zan_irgen_t *g) {
    if (g->fn_refl_set) return g->fn_refl_set;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i16 = LLVMInt16TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64, i64 }, 5, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_set", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_set = fn;
    LLVMValueRef mfind = refl_mfind_fn(g);
    LLVMValueRef invoke = refl_invoke_fn(g);
    LLVMValueRef find = refl_find_fn(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef setr  = LLVMAppendBasicBlockInContext(g->ctx, fn, "setter");
    LLVMBasicBlockRef fld   = LLVMAppendBasicBlockInContext(g->ctx, fn, "field");
    LLVMBasicBlockRef slot  = LLVMAppendBasicBlockInContext(g->ctx, fn, "slot");
    LLVMBasicBlockRef no    = LLVMAppendBasicBlockInContext(g->ctx, fn, "no");
    LLVMBasicBlockRef yes   = LLVMAppendBasicBlockInContext(g->ctx, fn, "yes");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef obj = LLVMGetParam(fn, 1);
    LLVMValueRef name = LLVMGetParam(fn, 2);
    LLVMValueRef val = LLVMGetParam(fn, 3);
    LLVMValueRef vkind = LLVMGetParam(fn, 4);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef sidx = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
        mfind, (LLVMValueRef[]){ ti, name,
            LLVMConstInt(i64, ZAN_REFL_MF_PROPSET, 0) }, 3, "refl.si");
    LLVMValueRef has_setter = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, sidx, LLVMConstInt(i64, 0, 0), "refl.sge"),
        LLVMBuildIsNotNull(g->builder, obj, "refl.sobj"), "refl.hasset");
    LLVMBuildCondBr(g->builder, has_setter, setr, fld);

    /* through the setter: the value is handed over in its slot form, which is
     * exactly what the thunk unpacks into the setter's parameter type. */
    LLVMPositionBuilderAtEnd(g->builder, setr);
    LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64,
        LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0), "refl.sargs");
    LLVMBuildStore(g->builder, val, slots);
    LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.sk");
    zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr,
                                               i64ptr, i64ptr }, 6, 0),
        invoke, (LLVMValueRef[]){ ti,
            LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), sidx, obj, slots, kout },
        6, "refl.sv");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntNE,
            LLVMBuildLoad2(g->builder, i64, kout, "refl.skv"),
            LLVMConstInt(i64, 0, 0), "refl.sdone"), yes, no);

    LLVMPositionBuilderAtEnd(g->builder, fld);
    LLVMValueRef idx = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        find, (LLVMValueRef[]){ ti, name }, 2, "refl.fi");
    LLVMValueRef fok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.fge"),
        LLVMBuildIsNotNull(g->builder, obj, "refl.fobj"), "refl.fok");
    LLVMBuildCondBr(g->builder, fok, slot, no);

    LLVMPositionBuilderAtEnd(g->builder, slot);
    LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
    LLVMValueRef foffr = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo");
    LLVMValueRef fe = LLVMBuildGEP2(g->builder, i8, fields, &foffr, 1, "refl.fe");
    LLVMValueRef fkind = refl_hdr_i64(g, fe, 16, "refl.fkind");
    LLVMValueRef foff = refl_hdr_i64(g, fe, 24, "refl.foff");
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.fslot");
    /* the value in both shapes, so each field kind picks the one it needs */
    LLVMValueRef is_fp = LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
        LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0), "refl.visfp");
    LLVMValueRef as_d = LLVMBuildSelect(g->builder, is_fp,
        LLVMBuildBitCast(g->builder, val, dbl, "refl.vbc"),
        LLVMBuildSIToFP(g->builder, val, dbl, "refl.vi2f"), "refl.vd");
    LLVMValueRef as_i = LLVMBuildSelect(g->builder, is_fp,
        LLVMBuildFPToSI(g->builder,
            LLVMBuildBitCast(g->builder, val, dbl, "refl.vbc2"), i64, "refl.vf2i"),
        val, "refl.vi");

    struct { int kind; LLVMTypeRef ty; int fp; const char *nm; } cases[] = {
        { ZAN_REFL_FK_BOOL,   i8,  0, "b" },
        { ZAN_REFL_FK_BYTE,   i8,  0, "u8" },
        { ZAN_REFL_FK_SBYTE,  i8,  0, "i8" },
        { ZAN_REFL_FK_SHORT,  i16, 0, "i16" },
        { ZAN_REFL_FK_USHORT, i16, 0, "u16" },
        { ZAN_REFL_FK_INT,    i32, 0, "i32" },
        { ZAN_REFL_FK_UINT,   i32, 0, "u32" },
        { ZAN_REFL_FK_LONG,   i64, 0, "i64" },
        { ZAN_REFL_FK_ULONG,  i64, 0, "u64" },
        { ZAN_REFL_FK_CHAR,   i64, 0, "ch" },
        { ZAN_REFL_FK_ENUM,   i64, 0, "en" },
        { ZAN_REFL_FK_FLOAT,  flt, 1, "f32" },
        { ZAN_REFL_FK_DOUBLE, dbl, 1, "f64" },
    };
    int ncases = (int)(sizeof(cases) / sizeof(cases[0]));
    LLVMValueRef sw = LLVMBuildSwitch(g->builder, fkind, no,
                                      (unsigned)ncases + 1);
    for (int ci = 0; ci < ncases; ci++) {
        char bn[24];
        snprintf(bn, sizeof(bn), "st.%s", cases[ci].nm);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, bn);
        LLVMAddCase(sw, LLVMConstInt(i64, (unsigned long long)cases[ci].kind, 0), bb);
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef sv;
        if (cases[ci].fp)
            sv = cases[ci].ty == flt
               ? LLVMBuildFPTrunc(g->builder, as_d, flt, "refl.stf")
               : as_d;
        else if (cases[ci].kind == ZAN_REFL_FK_BOOL)
            sv = LLVMBuildZExt(g->builder,
                    LLVMBuildICmp(g->builder, LLVMIntNE, as_i,
                                  LLVMConstInt(i64, 0, 0), "refl.stb"), i8,
                    "refl.stbz");
        else
            sv = cases[ci].ty == i64 ? as_i
               : LLVMBuildTrunc(g->builder, as_i, cases[ci].ty, "refl.sttr");
        LLVMBuildStore(g->builder, sv,
            LLVMBuildBitCast(g->builder, p,
                LLVMPointerType(cases[ci].ty, 0), "refl.stp"));
        LLVMBuildBr(g->builder, yes);
    }
    /* a string field: retain the new value and release the old one, exactly as
     * an ordinary assignment to that field does */
    LLVMBasicBlockRef sstr = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.str");
    LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), sstr);
    LLVMPositionBuilderAtEnd(g->builder, sstr);
    LLVMBasicBlockRef sstr2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.str2");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                      LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0), "refl.visptr"),
        sstr2, no);
    LLVMPositionBuilderAtEnd(g->builder, sstr2);
    LLVMValueRef sp = LLVMBuildBitCast(g->builder, p,
                                       LLVMPointerType(i8ptr, 0), "refl.stsp");
    LLVMValueRef nv = LLVMBuildIntToPtr(g->builder, val, i8ptr, "refl.stsv");
    LLVMValueRef ov = LLVMBuildLoad2(g->builder, i8ptr, sp, "refl.stold");
    LLVMTypeRef arc_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                          (LLVMTypeRef[]){ i8ptr }, 1, 0);
    zan_call2(g->builder, arc_ty, g->rt_str_retain, &nv, 1, "");
    LLVMBuildStore(g->builder, nv, sp);
    zan_call2(g->builder, arc_ty, g->rt_str_release, &ov, 1, "");
    LLVMBuildBr(g->builder, yes);

    LLVMPositionBuilderAtEnd(g->builder, yes);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
    LLVMPositionBuilderAtEnd(g->builder, no);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i64 __zan_refl_cfind(i8 *ti, i64 nargs): the index of the constructor taking
 * `nargs` arguments and having a thunk, or -1. Reflected construction selects
 * on the argument count alone -- the slots carry no type of their own. */
static LLVMValueRef refl_cfind_fn(zan_irgen_t *g) {
    if (g->fn_refl_cfind) return g->fn_refl_cfind;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_cfind", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_cfind = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head  = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body  = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef want = LLVMGetParam(fn, 1);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = refl_hdr_ptr(g, ti, ZAN_REFL_CTORS_OFF, "refl.ct");
    LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_CCOUNT_OFF, "refl.cc");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, arr, "refl.noct"), miss, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.ci");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, i, count, "refl.more"), body, miss);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef off = LLVMBuildMul(g->builder, i,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.co");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "refl.cr");
    LLVMValueRef pc = refl_hdr_i64(g, rec, ZAN_REFL_MO_PCOUNT, "refl.cpc");
    LLVMValueRef th = refl_hdr_ptr(g, rec, ZAN_REFL_MO_THUNK, "refl.cth");
    LLVMBuildCondBr(g->builder, LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, pc, want, "refl.cpq"),
        LLVMBuildIsNotNull(g->builder, th, "refl.cthnn"), "refl.chit"),
        found, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef inext = LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0),
                                      "refl.ci.n");
    LLVMBuildBr(g->builder, head);
    LLVMAddIncoming(i, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), inext },
                    (LLVMBasicBlockRef[]){ entry, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMBuildRet(g->builder, i);
    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, (unsigned long long)-1LL, 1));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* i64 __zan_refl_tainfo(i8 *ti, i64 idx, i64 which): the number of generic
 * arguments (which=0) or the idx-th argument record as an integer (which=1, 0
 * when out of range). The array is null-terminated, so no count is stored. */
static LLVMValueRef refl_tainfo_fn(zan_irgen_t *g) {
    if (g->fn_refl_tainfo) return g->fn_refl_tainfo;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i64 }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_tainfo", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_tainfo = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head  = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMBasicBlockRef cnt   = LLVMAppendBasicBlockInContext(g->ctx, fn, "count");
    LLVMBasicBlockRef ld    = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
    LLVMBasicBlockRef zero  = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef idx = LLVMGetParam(fn, 1);
    LLVMValueRef which = LLVMGetParam(fn, 2);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = refl_hdr_ptr(g, ti, ZAN_REFL_TARGS_OFF, "refl.ta");
    LLVMValueRef arrp = LLVMBuildBitCast(g->builder, arr,
                                         LLVMPointerType(i8ptr, 0), "refl.tap");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, arr, "refl.nota"), zero, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.ti");
    LLVMValueRef e = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildGEP2(g->builder, i8ptr, arrp, &i, 1, "refl.tae"), "refl.tav");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, e, "refl.taend"), done, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef inext = LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0),
                                      "refl.ti.n");
    LLVMBuildBr(g->builder, head);
    LLVMAddIncoming(i, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), inext },
                    (LLVMBasicBlockRef[]){ entry, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which, LLVMConstInt(i64, 0, 0),
                      "refl.iscnt"), cnt, ld);

    LLVMPositionBuilderAtEnd(g->builder, cnt);
    LLVMBuildRet(g->builder, i);

    LLVMPositionBuilderAtEnd(g->builder, ld);
    LLVMBasicBlockRef ld2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "load2");
    LLVMBuildCondBr(g->builder, LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.tge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, i, "refl.tlt"), "refl.tok"),
        ld2, zero);

    LLVMPositionBuilderAtEnd(g->builder, ld2);
    LLVMValueRef v = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildGEP2(g->builder, i8ptr, arrp, &idx, 1, "refl.tap2"), "refl.tv");
    LLVMBuildRet(g->builder, LLVMBuildPtrToInt(g->builder, v, i64, "refl.tvi"));

    LLVMPositionBuilderAtEnd(g->builder, zero);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* ---- call-site helpers ---------------------------------------------- */

/* The TypeInfo of `obj`, whose static type is `st`. */
static LLVMValueRef refl_emit_get_type(zan_irgen_t *g, zan_type_t *st,
                                       LLVMValueRef obj) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fallback = refl_meta_for(g, st, NULL, 0);
    if (!st || (st->kind != TYPE_CLASS && st->kind != TYPE_INTERFACE &&
                st->kind != TYPE_OBJECT))
        return fallback;   /* value types have no runtime type to look up */
    if (!obj || LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind)
        return fallback;
    obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "refl.obj");
    LLVMValueRef fn = refl_obj_type_fn(g);
    return zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        fn, (LLVMValueRef[]){ obj, fallback }, 2, "refl.ty");
}

/* A receiver lowered to an i8* the readers can walk: a class instance is
 * already a pointer; a struct value has to be spilled so its fields have
 * addresses. */
static LLVMValueRef refl_receiver_ptr(zan_irgen_t *g, zan_type_t *rt,
                                      LLVMValueRef v) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!v) return LLVMConstNull(i8ptr);
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
        return LLVMBuildBitCast(g->builder, v, i8ptr, "refl.recv");
    if (rt && rt->kind == TYPE_STRUCT) {
        LLVMValueRef tmp = LLVMBuildAlloca(g->builder, vt, "refl.spill");
        LLVMBuildStore(g->builder, v, tmp);
        return LLVMBuildBitCast(g->builder, tmp, i8ptr, "refl.recv");
    }
    return LLVMConstNull(i8ptr);
}

/* ---- reader call wrappers -------------------------------------------- */

static LLVMValueRef refl_mi64(zan_irgen_t *g, LLVMValueRef ti, int tbl,
                              LLVMValueRef idx, int which) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    return zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i64 }, 4, 0),
        refl_mi64_fn(g), (LLVMValueRef[]){ ti,
            LLVMConstInt(i64, (unsigned long long)tbl, 0), idx,
            LLVMConstInt(i64, (unsigned long long)which, 0) }, 4, "refl.mi");
}

static LLVMValueRef refl_mstr(zan_irgen_t *g, LLVMValueRef ti, int tbl,
                              LLVMValueRef idx, int which, LLVMValueRef k) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    return zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64, i64, i64, i64 },
                         5, 0),
        refl_mstr_fn(g), (LLVMValueRef[]){ ti,
            LLVMConstInt(i64, (unsigned long long)tbl, 0), idx,
            LLVMConstInt(i64, (unsigned long long)which, 0), k }, 5, "refl.ms");
}

static LLVMValueRef refl_mfind(zan_irgen_t *g, LLVMValueRef ti,
                               LLVMValueRef name, int flags) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    return zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
        refl_mfind_fn(g), (LLVMValueRef[]){ ti, name,
            LLVMConstInt(i64, (unsigned long long)flags, 0) }, 3, "refl.mfi");
}

/* An index argument as an i64 (0 when the call has none). */
static LLVMValueRef refl_arg_index(zan_irgen_t *g, zan_ast_node_t *arg,
                                   local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (!arg) return LLVMConstInt(i64, 0, 0);
    return emit_index_i64(g, emit_expr(g, arg, locals), "refl.i");
}

/* A name argument as an i8* ("" when the call has none). */
static LLVMValueRef refl_arg_str(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef v = arg ? emit_expr(g, arg, locals) : refl_empty_string(g);
    return LLVMBuildBitCast(g->builder, v, i8ptr, "refl.nm");
}

/* The i64 argument slots of a reflected call: `args[from..]` packed the way the
 * thunks unpack them. Unused slots stay 0. */
static LLVMValueRef refl_pack_args(zan_irgen_t *g, zan_ast_node_t **args,
                                   int argc, int from,
                                   local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64,
        LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0), "refl.args");
    for (int i = 0; i < ZAN_REFL_ARG_SLOTS; i++) {
        LLVMValueRef off = LLVMConstInt(i64, (unsigned long long)i, 0);
        LLVMValueRef p = LLVMBuildGEP2(g->builder, i64, slots, &off, 1,
                                       "refl.as");
        int ai = from + i;
        LLVMValueRef v = (args && ai < argc)
            ? refl_to_slot(g, emit_expr(g, args[ai], locals))
            : LLVMConstInt(i64, 0, 0);
        LLVMBuildStore(g->builder, v, p);
    }
    return slots;
}

/* `ti.<member>` / `ti.<method>(args)`. Returns false when `name` is not part of
 * the surface, so the caller keeps its normal path. */
static bool refl_emit_typeinfo_member(zan_irgen_t *g, LLVMValueRef ti,
                                      zan_istr_t name,
                                      zan_ast_node_t **args, int argc,
                                      local_scope_t *locals,
                                      LLVMValueRef *out) {
    int code = 0;
    if (!zan_refl_typeinfo_member(name, &code)) return false;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    zan_ast_node_t *arg0 = argc > 0 ? args[0] : NULL;
    zan_ast_node_t *arg1 = argc > 1 ? args[1] : NULL;
    ti = LLVMBuildBitCast(g->builder, ti, i8ptr, "refl.ti");
    switch (code) {
    case ZAN_REFL_M_MCOUNT:      /* MethodCount / ConstructorCount */
    case ZAN_REFL_M_CCOUNT: {
        LLVMValueRef c = refl_mi64(g, ti,
            code == ZAN_REFL_M_MCOUNT ? ZAN_REFL_TBL_METHOD : ZAN_REFL_TBL_CTOR,
            LLVMConstInt(i64, 0, 0), ZAN_REFL_MI_COUNT);
        *out = LLVMBuildTrunc(g->builder, c, i32, "refl.mc32");
        return true;
    }
    case ZAN_REFL_M_MNAME:       /* GetMethodName / GetMethodReturnType */
    case ZAN_REFL_M_MRET: {
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        *out = refl_mstr(g, ti, ZAN_REFL_TBL_METHOD, idx,
            code == ZAN_REFL_M_MNAME ? ZAN_REFL_MW_NAME : ZAN_REFL_MW_RET,
            LLVMConstInt(i64, 0, 0));
        return true;
    }
    case ZAN_REFL_M_MPCOUNT:     /* GetMethodParamCount / GetCtorParamCount */
    case ZAN_REFL_M_CPCOUNT: {
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        LLVMValueRef c = refl_mi64(g, ti,
            code == ZAN_REFL_M_MPCOUNT ? ZAN_REFL_TBL_METHOD : ZAN_REFL_TBL_CTOR,
            idx, ZAN_REFL_MI_PCOUNT);
        *out = LLVMBuildTrunc(g->builder, c, i32, "refl.pc32");
        return true;
    }
    case ZAN_REFL_M_MPTYPE:      /* GetMethodParamType / GetCtorParamType */
    case ZAN_REFL_M_CPTYPE: {
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        LLVMValueRef k = refl_arg_index(g, arg1, locals);
        *out = refl_mstr(g, ti,
            code == ZAN_REFL_M_MPTYPE ? ZAN_REFL_TBL_METHOD : ZAN_REFL_TBL_CTOR,
            idx, ZAN_REFL_MW_PARAM, k);
        return true;
    }
    case ZAN_REFL_M_MHAS: {      /* HasMethod(name) */
        LLVMValueRef nm = refl_arg_str(g, arg0, locals);
        *out = LLVMBuildICmp(g->builder, LLVMIntSGE,
            refl_mfind(g, ti, nm, 0), LLVMConstInt(i64, 0, 0), "refl.mhas");
        return true;
    }
    case ZAN_REFL_M_MSTATIC: {   /* IsMethodStatic(i) */
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        LLVMValueRef fl = refl_mi64(g, ti, ZAN_REFL_TBL_METHOD, idx,
                                    ZAN_REFL_MI_FLAGS);
        *out = LLVMBuildICmp(g->builder, LLVMIntNE,
            LLVMBuildAnd(g->builder, fl,
                LLVMConstInt(i64, ZAN_REFL_MF_STATIC, 0), "refl.stand"),
            LLVMConstInt(i64, 0, 0), "refl.isst");
        return true;
    }
    case ZAN_REFL_M_CNEW: {      /* CreateInstance(...) */
        LLVMValueRef slots = refl_pack_args(g, args, argc, 0, locals);
        LLVMValueRef idx = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
            refl_cfind_fn(g), (LLVMValueRef[]){ ti,
                LLVMConstInt(i64, (unsigned long long)argc, 0) }, 2, "refl.ci");
        LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.nk");
        LLVMValueRef v = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr,
                                                   i64ptr, i64ptr }, 6, 0),
            refl_invoke_fn(g), (LLVMValueRef[]){ ti,
                LLVMConstInt(i64, ZAN_REFL_TBL_CTOR, 0), idx,
                LLVMConstNull(i8ptr), slots, kout }, 6, "refl.new");
        *out = LLVMBuildIntToPtr(g->builder,
            LLVMBuildSelect(g->builder,
                LLVMBuildICmp(g->builder, LLVMIntEQ,
                    LLVMBuildLoad2(g->builder, i64, kout, "refl.nkv"),
                    LLVMConstInt(i64, 0, 0), "refl.nofail"),
                LLVMConstInt(i64, 0, 0), v, "refl.nsel"), i8ptr, "refl.nobj");
        return true;
    }
    case ZAN_REFL_M_ISARRAY:     /* IsArray / IsNullable / IsInterface */
    case ZAN_REFL_M_ISNULLABLE:
    case ZAN_REFL_M_ISIFACE: {
        int want = code == ZAN_REFL_M_ISARRAY    ? ZAN_REFL_TK_ARRAY
                 : code == ZAN_REFL_M_ISNULLABLE ? ZAN_REFL_TK_NULLABLE
                                                 : ZAN_REFL_TK_INTERFACE;
        *out = LLVMBuildICmp(g->builder, LLVMIntEQ,
            refl_hdr_i64(g, ti, ZAN_REFL_KIND_OFF, "refl.tk"),
            LLVMConstInt(i64, (unsigned long long)want, 0), "refl.iskind");
        return true;
    }
    case ZAN_REFL_M_ISGENERIC:   /* IsGeneric */
        *out = LLVMBuildIsNotNull(g->builder,
            refl_hdr_ptr(g, ti, ZAN_REFL_TARGS_OFF, "refl.ta"), "refl.isgen");
        return true;
    case ZAN_REFL_M_ELEMTYPE: {  /* ElementType */
        LLVMValueRef e = refl_hdr_ptr(g, ti, ZAN_REFL_ELEM_OFF, "refl.el");
        *out = LLVMBuildSelect(g->builder,
            LLVMBuildIsNull(g->builder, e, "refl.elnull"),
            refl_meta_for(g, NULL, NULL, 0), e, "refl.elt");
        return true;
    }
    case ZAN_REFL_M_TACOUNT: {   /* TypeArgCount */
        LLVMValueRef c = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64 }, 3, 0),
            refl_tainfo_fn(g), (LLVMValueRef[]){ ti, LLVMConstInt(i64, 0, 0),
                LLVMConstInt(i64, 0, 0) }, 3, "refl.tac");
        *out = LLVMBuildTrunc(g->builder, c, i32, "refl.tac32");
        return true;
    }
    case ZAN_REFL_M_TARG: {      /* GetTypeArg(i) */
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        LLVMValueRef v = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64 }, 3, 0),
            refl_tainfo_fn(g), (LLVMValueRef[]){ ti, idx,
                LLVMConstInt(i64, 1, 0) }, 3, "refl.tav");
        *out = LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntEQ, v, LLVMConstInt(i64, 0, 0),
                          "refl.tanull"),
            refl_meta_for(g, NULL, NULL, 0),
            LLVMBuildIntToPtr(g->builder, v, i8ptr, "refl.tap"), "refl.targ");
        return true;
    }
    }
    switch (code) {
    case ZAN_REFL_M_NAME: /* Name: the record's payload already IS that string */
        *out = ti;
        return true;
    case ZAN_REFL_M_KIND: {   /* Kind */
        LLVMValueRef k = refl_hdr_i64(g, ti, ZAN_REFL_KIND_OFF, "refl.tk");
        const char *names[] = { "other", "class", "struct", "enum",
                                "interface", "primitive", "other",
                                "array", "nullable", "delegate" };
        LLVMValueRef v = refl_const_string(g, names[0], 5);
        for (int i = 1; i <= ZAN_REFL_TK_LAST; i++) {
            LLVMValueRef s = refl_const_string(g, names[i], (int)strlen(names[i]));
            v = LLVMBuildSelect(g->builder,
                LLVMBuildICmp(g->builder, LLVMIntEQ, k,
                              LLVMConstInt(i64, (unsigned long long)i, 0), "refl.kq"),
                s, v, "refl.kn");
        }
        *out = v;
        return true;
    }
    case ZAN_REFL_M_FCOUNT: { /* FieldCount */
        LLVMValueRef c = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.fc");
        *out = LLVMBuildTrunc(g->builder, c, i32, "refl.fc32");
        return true;
    }
    case ZAN_REFL_M_FNAME:
    case ZAN_REFL_M_FTYPE: {  /* GetFieldName / GetFieldType */
        LLVMValueRef idx = arg0 ? emit_expr(g, arg0, locals)
                                : LLVMConstInt(i64, 0, 0);
        idx = emit_index_i64(g, idx, "refl.i");
        *out = zan_call2(g->builder,
            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64, i64 }, 3, 0),
            refl_fname_fn(g), (LLVMValueRef[]){ ti, idx,
                LLVMConstInt(i64, code == ZAN_REFL_M_FNAME ? ZAN_REFL_WHICH_NAME
                                            : ZAN_REFL_WHICH_TYPE, 0) },
            3, "refl.fname");
        return true;
    }
    case ZAN_REFL_M_HAS: {    /* HasField(name) */
        LLVMValueRef nm = arg0 ? emit_expr(g, arg0, locals)
                               : refl_empty_string(g);
        LLVMValueRef found = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
            refl_find_fn(g), (LLVMValueRef[]){ ti,
                LLVMBuildBitCast(g->builder, nm, i8ptr, "refl.nm") }, 2, "refl.f");
        *out = LLVMBuildICmp(g->builder, LLVMIntSGE, found,
                             LLVMConstInt(i64, 0, 0), "refl.has");
        return true;
    }
    default: {         /* GetMemberValue(i) */
        LLVMValueRef idx = arg0 ? emit_expr(g, arg0, locals)
                                : LLVMConstInt(i64, 0, 0);
        idx = emit_index_i64(g, idx, "refl.i");
        LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fl");
        LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.fc");
        LLVMValueRef ok = LLVMBuildAnd(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntSGE, idx,
                          LLVMConstInt(i64, 0, 0), "refl.ge"),
            LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"),
            "refl.ok");
        /* a null-safe read: out-of-range answers 0 without branching */
        LLVMValueRef safe = LLVMBuildSelect(g->builder, ok, idx,
                                            LLVMConstInt(i64, 0, 0), "refl.si");
        LLVMValueRef off = LLVMBuildAdd(g->builder,
            LLVMBuildMul(g->builder, safe,
                         LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo"),
            LLVMConstInt(i64, 24, 0), "refl.vo");
        LLVMValueRef zero_slot = LLVMBuildAlloca(g->builder, i64, "refl.z");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), zero_slot);
        LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, fields, &off, 1, "refl.vp");
        p = LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), "refl.vpc");
        LLVMValueRef live = LLVMBuildAnd(g->builder, ok,
            LLVMBuildIsNotNull(g->builder, fields, "refl.fl.nn"), "refl.live");
        p = LLVMBuildSelect(g->builder, live, p, zero_slot, "refl.vsel");
        *out = LLVMBuildLoad2(g->builder, i64, p, "refl.mv");
        return true;
    }
    }
}

/* `obj.GetType()` / `obj.GetFieldInt("x")` / ... on a class or struct value.
 * `nm_tmp`/`val_tmp` report the emitted name and value arguments, so the
 * caller can release the ones it owns: like every other call, an argument
 * written in place is the call site's temporary. */
static bool refl_emit_instance_call_1(zan_irgen_t *g, zan_type_t *rt,
                                      LLVMValueRef recv, zan_istr_t name,
                                      zan_ast_node_t **args, int argc,
                                      local_scope_t *locals, LLVMValueRef *out,
                                      LLVMValueRef *nm_tmp,
                                      LLVMValueRef *val_tmp) {
    int code = 0;
    if (!zan_refl_instance_method(name, &code)) return false;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    zan_ast_node_t *arg0 = argc > 0 ? args[0] : NULL;
    zan_ast_node_t *arg1 = argc > 1 ? args[1] : NULL;
    if (code == ZAN_REFL_M_GETTYPE) {
        *out = refl_emit_get_type(g, rt, recv);
        return true;
    }
    LLVMValueRef ti = refl_emit_get_type(g, rt, recv);
    LLVMValueRef obj = refl_receiver_ptr(g, rt, recv);
    LLVMValueRef nm = arg0 ? emit_expr(g, arg0, locals) : refl_empty_string(g);
    if (arg0) *nm_tmp = nm;
    nm = LLVMBuildBitCast(g->builder, nm, i8ptr, "refl.nm");
    LLVMTypeRef three[3] = { i8ptr, i8ptr, i8ptr };
    /* SetField<T>(name, value): the setter of a property, else the field slot */
    if (code == ZAN_REFL_M_SETINT || code == ZAN_REFL_M_SETLONG ||
        code == ZAN_REFL_M_SETBOOL || code == ZAN_REFL_M_SETDOUBLE ||
        code == ZAN_REFL_M_SETSTRING) {
        LLVMValueRef v = arg1 ? emit_expr(g, arg1, locals) : NULL;
        if (arg1) *val_tmp = v;
        int vkind = code == ZAN_REFL_M_SETDOUBLE ? ZAN_REFL_VK_DOUBLE
                  : code == ZAN_REFL_M_SETSTRING ? ZAN_REFL_VK_PTR
                                                 : ZAN_REFL_VK_INT;
        LLVMValueRef slot = v ? refl_to_slot(g, v) : LLVMConstInt(i64, 0, 0);
        LLVMValueRef ok = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64,
                                                   i64 }, 5, 0),
            refl_set_fn(g), (LLVMValueRef[]){ ti, obj, nm, slot,
                LLVMConstInt(i64, (unsigned long long)vkind, 0) }, 5, "refl.set");
        *out = LLVMBuildICmp(g->builder, LLVMIntNE, ok,
                             LLVMConstInt(i64, 0, 0), "refl.setok");
        return true;
    }
    if (code == ZAN_REFL_M_OMHAS) {   /* HasMethod(name) */
        *out = LLVMBuildICmp(g->builder, LLVMIntSGE, refl_mfind(g, ti, nm, 0),
                             LLVMConstInt(i64, 0, 0), "refl.omhas");
        return true;
    }
    /* Invoke*(name, args...): the method's own thunk, by name */
    if (code == ZAN_REFL_M_IVOID || code == ZAN_REFL_M_IINT ||
        code == ZAN_REFL_M_ILONG || code == ZAN_REFL_M_IDOUBLE ||
        code == ZAN_REFL_M_ISTRING) {
        LLVMValueRef idx = refl_mfind(g, ti, nm, 0);
        LLVMValueRef slots = refl_pack_args(g, args, argc, 1, locals);
        LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.ik");
        LLVMValueRef v = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr,
                                                   i64ptr, i64ptr }, 6, 0),
            refl_invoke_fn(g), (LLVMValueRef[]){ ti,
                LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), idx, obj, slots,
                kout }, 6, "refl.iv");
        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i64, kout, "refl.ikv");
        LLVMValueRef called = LLVMBuildICmp(g->builder, LLVMIntNE, kv,
            LLVMConstInt(i64, 0, 0), "refl.icalled");
        if (code == ZAN_REFL_M_IVOID) { *out = called; return true; }
        /* the invoke reports 1 + the return kind */
        LLVMValueRef k = LLVMBuildSub(g->builder, kv, LLVMConstInt(i64, 1, 0),
                                      "refl.ik1");
        if (code == ZAN_REFL_M_ISTRING) {
            LLVMValueRef s = LLVMBuildIntToPtr(g->builder, v, i8ptr, "refl.isv");
            LLVMValueRef isstr = LLVMBuildAnd(g->builder, called,
                LLVMBuildICmp(g->builder, LLVMIntEQ, k,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), "refl.isk"),
                "refl.isok");
            isstr = LLVMBuildAnd(g->builder, isstr,
                LLVMBuildIsNotNull(g->builder, s, "refl.isnn"), "refl.isok2");
            *out = LLVMBuildSelect(g->builder, isstr, s, refl_empty_string(g),
                                   "refl.istr");
            return true;
        }
        if (code == ZAN_REFL_M_IDOUBLE) {
            *out = LLVMBuildSelect(g->builder, called,
                refl_pget_as_f64(g, v, k), LLVMConstReal(dbl, 0.0), "refl.idv");
            return true;
        }
        LLVMValueRef iv = LLVMBuildSelect(g->builder, called,
            refl_pget_as_i64(g, v, k), LLVMConstInt(i64, 0, 0), "refl.iiv");
        *out = code == ZAN_REFL_M_IINT
             ? LLVMBuildTrunc(g->builder, iv, i32, "refl.ii32") : iv;
        return true;
    }
    if (code == ZAN_REFL_M_OBOOL) {   /* GetFieldBool(name) */
        LLVMValueRef v = zan_call2(g->builder,
            LLVMFunctionType(i64, three, 3, 0), refl_get_i64_fn(g),
            (LLVMValueRef[]){ ti, obj, nm }, 3, "refl.b64");
        *out = LLVMBuildICmp(g->builder, LLVMIntNE, v,
                             LLVMConstInt(i64, 0, 0), "refl.bool");
        return true;
    }
    if (code == ZAN_REFL_M_OHAS) {
        LLVMValueRef found = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
            refl_find_fn(g), (LLVMValueRef[]){ ti, nm }, 2, "refl.f");
        *out = LLVMBuildICmp(g->builder, LLVMIntSGE, found,
                             LLVMConstInt(i64, 0, 0), "refl.has");
        return true;
    }
    if (code == ZAN_REFL_M_ODOUBLE) {
        *out = zan_call2(g->builder, LLVMFunctionType(dbl, three, 3, 0),
            refl_get_f64_fn(g), (LLVMValueRef[]){ ti, obj, nm }, 3, "refl.f64");
        return true;
    }
    if (code == ZAN_REFL_M_OSTRING) {
        *out = zan_call2(g->builder, LLVMFunctionType(i8ptr, three, 3, 0),
            refl_get_str_fn(g), (LLVMValueRef[]){ ti, obj, nm }, 3, "refl.str");
        return true;
    }
    LLVMValueRef v = zan_call2(g->builder, LLVMFunctionType(i64, three, 3, 0),
        refl_get_i64_fn(g), (LLVMValueRef[]){ ti, obj, nm }, 3, "refl.i64");
    *out = code == ZAN_REFL_M_OINT ? LLVMBuildTrunc(g->builder, v, i32, "refl.i32") : v;
    return true;
}

static bool refl_emit_instance_call(zan_irgen_t *g, zan_type_t *rt,
                                    LLVMValueRef recv, zan_istr_t name,
                                    zan_ast_node_t **args, int argc,
                                    local_scope_t *locals, LLVMValueRef *out) {
    LLVMValueRef nm_tmp = NULL, val_tmp = NULL;
    if (!refl_emit_instance_call_1(g, rt, recv, name, args, argc, locals, out,
                                   &nm_tmp, &val_tmp))
        return false;
    if (nm_tmp)
        emit_release_owned_call_temp(g, args[0], nm_tmp, locals);
    if (val_tmp)
        emit_release_owned_call_temp(g, args[1], val_tmp, locals);
    return true;
}

/* Per-allocation-site record table, so GetType() can answer the concrete type.
 * Emitted only when the module actually reflects. */
static void emit_site_meta_table(zan_irgen_t *g) {
    if (!g->refl_used || !g->g_site_meta || !g->site_syms) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    int n = ZAN_MAX_LEAK_SITES;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    if (!elems) return;
    for (int i = 0; i < n; i++) {
        elems[i] = LLVMConstNull(i8ptr);
        if (i >= g->leak_site_count) continue;
        if (g->site_coll && g->site_coll[i]) continue;   /* List/Dict/... */
        zan_symbol_t *sym = g->site_syms[i];
        if (!sym || !sym->type) continue;
        elems[i] = refl_meta_for(g, sym->type, sym->name.str, (int)sym->name.len);
    }
    LLVMSetInitializer(g->g_site_meta, LLVMConstArray(i8ptr, elems, (unsigned)n));
    free(elems);
}
