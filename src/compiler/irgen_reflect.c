/* Reflection metadata — first layer.
 *
 * `typeof(T)` and `obj.GetType()` yield a TypeInfo: a pointer into a
 * compiler-emitted, immortal record that describes one type. The record is laid
 * out so that the pointer lands exactly on the type's display name, with the
 * reflection slots in FRONT of it:
 *
 *      offset from the TypeInfo value
 *      -40   i8*  fields        pointer to the field-record array (or null)
 *      -32   i64  field_count
 *      -24   i64  type kind     ZAN_REFL_TK_*
 *      -16   i64  refcount      ZAN_STRING_SENTINEL_RC  \  the string RC header
 *       -8   i64  magic         ZAN_STRING_MAGIC        /  (see zan_abi.h)
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
#define ZAN_REFL_FIELDS_OFF (-40)
#define ZAN_REFL_COUNT_OFF  (-32)
#define ZAN_REFL_KIND_OFF   (-24)
/* bytes in front of the name payload: the four slots above plus the magic */
#define ZAN_REFL_PREFIX     40
/* { i8*, i8*, i64, i64 } */
#define ZAN_REFL_FIELD_SIZE 32

/* type kinds */
#define ZAN_REFL_TK_CLASS     1
#define ZAN_REFL_TK_STRUCT    2
#define ZAN_REFL_TK_ENUM      3
#define ZAN_REFL_TK_INTERFACE 4
#define ZAN_REFL_TK_PRIMITIVE 5
#define ZAN_REFL_TK_OTHER     6

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
        LLVMConstInt(i64, ZAN_STRING_MAGIC, 0),
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
    case TYPE_ARRAY:
    case TYPE_NULLABLE:
    case TYPE_DELEGATE:
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

    int field_count = 0;
    LLVMValueRef fields = NULL;
    if (t && sym) {
        if (t->kind == TYPE_ENUM)
            fields = refl_emit_fields_enum(g, sym, &field_count);
        else if (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT)
            fields = refl_emit_fields_class(g, sym, &field_count);
    }
    if (!fields) fields = LLVMConstNull(i8ptr);

    LLVMTypeRef rec_ty = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){
        i8ptr, i64, i64, i64, i64,
        LLVMArrayType(i8, (unsigned)(disp_len + 1)) }, 6, 0);
    LLVMValueRef *chars = (LLVMValueRef *)calloc((size_t)disp_len + 1,
                                                sizeof(LLVMValueRef));
    if (!chars) return LLVMConstNull(i8ptr);
    for (int i = 0; i < disp_len; i++)
        chars[i] = LLVMConstInt(i8, (unsigned char)disp[i], 0);
    chars[disp_len] = LLVMConstInt(i8, 0, 0);
    LLVMValueRef init = LLVMConstNamedStruct(rec_ty, (LLVMValueRef[]){
        fields,
        LLVMConstInt(i64, (unsigned long long)field_count, 0),
        LLVMConstInt(i64, (unsigned long long)refl_type_kind(t), 0),
        LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
        LLVMConstInt(i64, ZAN_STRING_MAGIC, 0),
        LLVMConstArray(i8, chars, (unsigned)(disp_len + 1)) }, 6);
    free(chars);
    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.type.%d", g->refl_meta_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, rec_ty, nm);
    LLVMSetInitializer(gv, init);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetAlignment(gv, 8);
    LLVMValueRef idx[3] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 5, 0),
                            LLVMConstInt(i64, 0, 0) };
    LLVMValueRef rec = LLVMConstGEP2(rec_ty, gv, idx, 3);

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
    return rec;
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

    LLVMPositionBuilderAtEnd(g->builder, entry);
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

    LLVMPositionBuilderAtEnd(g->builder, entry);
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
 * anything else (never null, so the result is printable/concatenable). */
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

    LLVMPositionBuilderAtEnd(g->builder, entry);
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
    LLVMBuildRet(g->builder, LLVMBuildSelect(g->builder,
        LLVMBuildIsNull(g->builder, sv, "refl.snull"), empty, sv, "refl.str"));

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

/* `ti.<member>` / `ti.<method>(args)`. Returns false when `name` is not part of
 * the surface, so the caller keeps its normal path. */
static bool refl_emit_typeinfo_member(zan_irgen_t *g, LLVMValueRef ti,
                                      zan_istr_t name,
                                      zan_ast_node_t *arg0,
                                      local_scope_t *locals,
                                      LLVMValueRef *out) {
    int code = 0;
    if (!zan_refl_typeinfo_member(name, &code)) return false;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    ti = LLVMBuildBitCast(g->builder, ti, i8ptr, "refl.ti");
    switch (code) {
    case ZAN_REFL_M_NAME: /* Name: the record's payload already IS that string */
        *out = ti;
        return true;
    case ZAN_REFL_M_KIND: {   /* Kind */
        LLVMValueRef k = refl_hdr_i64(g, ti, ZAN_REFL_KIND_OFF, "refl.tk");
        const char *names[] = { "other", "class", "struct", "enum",
                                "interface", "primitive", "other" };
        LLVMValueRef v = refl_const_string(g, names[0], 5);
        for (int i = 1; i <= 6; i++) {
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

/* `obj.GetType()` / `obj.GetFieldInt("x")` / ... on a class or struct value. */
static bool refl_emit_instance_call(zan_irgen_t *g, zan_type_t *rt,
                                    LLVMValueRef recv, zan_istr_t name,
                                    zan_ast_node_t *arg0,
                                    local_scope_t *locals, LLVMValueRef *out) {
    int code = 0;
    if (!zan_refl_instance_method(name, &code)) return false;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    if (code == ZAN_REFL_M_GETTYPE) {
        *out = refl_emit_get_type(g, rt, recv);
        return true;
    }
    LLVMValueRef ti = refl_emit_get_type(g, rt, recv);
    LLVMValueRef obj = refl_receiver_ptr(g, rt, recv);
    LLVMValueRef nm = arg0 ? emit_expr(g, arg0, locals) : refl_empty_string(g);
    nm = LLVMBuildBitCast(g->builder, nm, i8ptr, "refl.nm");
    LLVMTypeRef three[3] = { i8ptr, i8ptr, i8ptr };
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
