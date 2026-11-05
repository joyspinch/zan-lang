#ifndef ZAN_IRGEN_INTERNAL_H
#define ZAN_IRGEN_INTERNAL_H

#include "irgen.h"
#include "irgen_compact.h"
#include "optimizer.h"
#include "builtin_api.h"
#include "reflect_api.h"
#include "arena.h"
#include "diag.h"
#include "zan_abi.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#if defined(__has_include)
#if __has_include(<llvm/Config/llvm-config.h>)
#include <llvm/Config/llvm-config.h>
#endif
#endif

#ifdef _WIN32
#include <direct.h>
#define zan_getcwd _getcwd
#else
#include <unistd.h>
#define zan_getcwd getcwd
#endif

/* ========================================================================= */
/* Shared Inline Math & Comparison Primitives                                */
/* ========================================================================= */

static inline LLVMValueRef zan_iwiden(LLVMBuilderRef b, LLVMValueRef v, LLVMTypeRef to) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind) return v;
    return LLVMGetIntTypeWidth(vt) <= 8 ? LLVMBuildZExt(b, v, to, "zx")
                                        : LLVMBuildSExt(b, v, to, "wx");
}

static inline void zan_ipair(LLVMBuilderRef b, LLVMValueRef *l, LLVMValueRef *r) {
    LLVMTypeRef tl = LLVMTypeOf(*l), tr = LLVMTypeOf(*r);
    if (LLVMGetTypeKind(tl) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(tr) != LLVMIntegerTypeKind) return;
    unsigned wl = LLVMGetIntTypeWidth(tl), wr = LLVMGetIntTypeWidth(tr);
    if (wl == wr) {
        if (wl == 8) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(LLVMGetTypeContext(tl));
            *l = LLVMBuildZExt(b, *l, i64, "zx");
            *r = LLVMBuildZExt(b, *r, i64, "zx");
        }
        return;
    }
    if (wl < wr) *l = zan_iwiden(b, *l, tr);
    else         *r = zan_iwiden(b, *r, tl);
}

#define ZAN_IBIN(name, builder)                                              static inline LLVMValueRef name(LLVMBuilderRef b, LLVMValueRef l, LLVMValueRef r,                                    const char *n) {                                 zan_ipair(b, &l, &r);                                                        return builder(b, l, r, n);                                              }
ZAN_IBIN(zan_add,  LLVMBuildAdd)
ZAN_IBIN(zan_sub,  LLVMBuildSub)
ZAN_IBIN(zan_mul,  LLVMBuildMul)
ZAN_IBIN(zan_sdiv, LLVMBuildSDiv)
ZAN_IBIN(zan_udiv, LLVMBuildUDiv)
ZAN_IBIN(zan_srem, LLVMBuildSRem)
ZAN_IBIN(zan_urem, LLVMBuildURem)
ZAN_IBIN(zan_and,  LLVMBuildAnd)
ZAN_IBIN(zan_or,   LLVMBuildOr)
ZAN_IBIN(zan_xor,  LLVMBuildXor)
ZAN_IBIN(zan_shl,  LLVMBuildShl)
ZAN_IBIN(zan_lshr, LLVMBuildLShr)
ZAN_IBIN(zan_ashr, LLVMBuildAShr)
#undef ZAN_IBIN

static inline LLVMValueRef zan_icmp(LLVMBuilderRef b, LLVMIntPredicate p,
                                    LLVMValueRef l, LLVMValueRef r, const char *n) {
    zan_ipair(b, &l, &r);
    return LLVMBuildICmp(b, p, l, r, n);
}

static inline LLVMValueRef zan_tobool(LLVMBuilderRef b, LLVMValueRef v, const char *n) {
    LLVMTypeRef ty = LLVMTypeOf(v);
    LLVMTypeKind k = LLVMGetTypeKind(ty);
    if (k == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(ty) == 1) { return v; }
    if (k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind) {
        return LLVMBuildFCmp(b, LLVMRealUNE, v, LLVMConstNull(ty), n);
    }
    return LLVMBuildICmp(b, LLVMIntNE, v, LLVMConstNull(ty), n);
}


static inline bool target_is_wasm32(zan_irgen_t *g) {
    return g->target_triple[0] &&
           strncmp(g->target_triple, "wasm32", 6) == 0;
}

/* ========================================================================= */
/* Shared Types & Scope Structures                                           */
/* ========================================================================= */

#define INITIAL_LOCALS 16
#define ZAN_CLOSURE_HDR_FIELDS 3

typedef struct {
    zan_istr_t name;
    LLVMValueRef alloca;
    zan_type_t *type;
    int arc_owned;
    LLVMValueRef arr_len_slot;
    int eh_slot;
    LLVMValueRef box_cell;
    LLVMValueRef box_owner_slot;
    int          box_owned;
    int          opaque_string;
    LLVMValueRef obj_rc_flag;
    int          byref_slot;
    zan_ast_node_t *binding_decl;
    zan_ast_node_t *async_decl;
    int async_role;
    int frame_owner;
    int struct_rc;
    int per_iteration;
} local_var_t;

typedef struct {
    zan_ast_node_t *decl;
    LLVMValueRef cell;
    LLVMValueRef owner_slot;
    zan_type_t *type;
    LLVMTypeRef payload;
    int eh_slot;
} pattern_binding_t;

typedef struct {
    local_var_t *vars;
    int count;
    int cap;
    zan_arena_t *arena;
    pattern_binding_t *patterns;
    int pattern_count;
    int pattern_cap;
} local_scope_t;

enum {
    ASYNC_LOCAL_VALUE = 0,
    ASYNC_FOREACH_INDEX,
    ASYNC_FOREACH_COLLECTION,
    ASYNC_FOREACH_ENUMERATOR,
    ASYNC_FOREACH_SNAP
};

/* 异步方法需在堆帧中跨挂起点保持的局部变量记录 */
typedef struct {
    zan_istr_t   name;
    LLVMTypeRef  llvm;
    zan_type_t  *ztype;
    int          frame_index;
    bool         no_arc;
    bool         boxed;
    zan_ast_node_t *decl;
    int role;
} async_local_t;

typedef struct {
    zan_irgen_t   *g;
    int            await_count;
    async_local_t *locals;
    int            local_count;
    int            local_cap;
    local_scope_t *scope;
    int            foreach_next;
    int            try_count;
    int            fin_depth;
    int            fin_depth_max;
    zan_ast_node_t *body;
} async_scan_t;

enum {
    ASYNC_FRAME_SCHED = 0,
    ASYNC_FRAME_SCHED_STEP = 1,
    ASYNC_FRAME_STATE = 2,
    ASYNC_FRAME_DONE = 3,
    ASYNC_FRAME_AWAITER = 4,
    ASYNC_FRAME_AWAITER_STEP = 5,
    ASYNC_FRAME_RESULT = 6,
    ASYNC_FRAME_CLEANUP = 7,
    ASYNC_FRAME_HCOUNT = 8,
    ASYNC_FRAME_SELF_STEP = 9,
    ASYNC_FRAME_EXC = 10,
    ASYNC_FRAME_EXC_TID = 11,
    ASYNC_FRAME_EXC_OWNED = 12,
    ASYNC_FRAME_CANCEL = 13,
    ASYNC_FRAME_CHILD = 14,
    ASYNC_FRAME_LNEXT = 15,
    ASYNC_FRAME_PENDING_COUNT = 16,
    ASYNC_FRAME_HSTACK = 17,
    ASYNC_FRAME_CEXC = 18,
    ASYNC_FRAME_CEXC_OWNED = 19,
    ASYNC_FRAME_CEXC_TID = 20,
    ASYNC_FRAME_PENDING = 21,
    ASYNC_FRAME_HPENDING = 22,
    ASYNC_FRAME_FIRST_PARAM = 23
};

enum { FAM_UNKNOWN = 0, FAM_BOOL, FAM_INT, FAM_FLOAT, FAM_STRING, FAM_REF, FAM_DECIMAL };

enum { ASYNC_PENDING_EXCEPTION = 1, ASYNC_PENDING_RETURN = 2,
       ASYNC_PENDING_BORROWED_EXCEPTION = 3 };

/* ========================================================================= */
/* Cross-File Global Variables                                               */
/* ========================================================================= */

extern unsigned g_local_gen;
extern zan_irgen_t *g_di_emit_ctx;

struct zan_ctor_entry *find_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                                 zan_ast_list_t *args, local_scope_t *locals,
                                 zan_ast_node_t *exclude);
struct zan_struct_type_entry *get_struct_entry(zan_irgen_t *g, zan_symbol_t *type_sym);

/* ========================================================================= */
/* Cross-File Function Prototypes                                            */
/* ========================================================================= */

/* --- From irgen.c (82 functions) --- */
LLVMTypeRef arc_desc_type(zan_irgen_t *g);
LLVMValueRef arc_site_arg(zan_irgen_t *g, int site_idx);
bool class_has_virtual_methods(zan_symbol_t *sym);
int class_vptr_offset(zan_symbol_t *sym);
int count_virtual_methods(zan_symbol_t *sym);
bool decl_is_explicit_layout(zan_symbol_t *sym);
unsigned long di_align_up(unsigned long v, unsigned long a);
void di_clear(zan_irgen_t *g);
void di_debug_types_reset(void);
void di_set_loc(zan_irgen_t *g, zan_loc_t loc);
int eh_slot_kind_of(zan_type_t *t);
unsigned elem_slot_words(zan_irgen_t *g, zan_type_t *elem);
LLVMValueRef emit_alloc_rc_collection(zan_irgen_t *g, zan_ast_node_t *expr, long size, int coll_kind, zan_type_t *elem_type);
void emit_arc_quarantine(zan_irgen_t *g, LLVMValueRef obj, LLVMValueRef rc_iptr);
void emit_dbl_str(zan_irgen_t *g, LLVMValueRef buf, LLVMValueRef cap, LLVMValueRef v);
LLVMValueRef emit_entry_alloca(zan_irgen_t *g, LLVMTypeRef ty, const char *name);
LLVMValueRef emit_field_ptr(zan_irgen_t *g, zan_symbol_t *type_sym, LLVMTypeRef st, LLVMValueRef base, int fi, const char *name);
void emit_header_read_guard(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef ptr8, LLVMBasicBlockRef ret_bb);
LLVMValueRef emit_itoa_into(zan_irgen_t *g, LLVMValueRef buf, LLVMValueRef v, int is_unsigned);
void emit_leak_counter_add(zan_irgen_t *g, LLVMValueRef ptr, long long delta);
void emit_rt_alloc_add(zan_irgen_t *g, LLVMValueRef bytes);
LLVMValueRef extend_int_for_slot(zan_irgen_t *g, LLVMValueRef v, zan_type_t *elem_type, LLVMTypeRef slot_ty);
bool field_member_is_static(zan_symbol_t *m);
zan_type_t * field_type_at(zan_irgen_t *g, zan_symbol_t *fsym, zan_type_t *recv);
zan_type_t * field_type_here(zan_irgen_t *g, zan_type_t *t);
LLVMValueRef get_calloc_fn(zan_irgen_t *g);
LLVMTypeRef get_decimal_type(zan_irgen_t *g);
int get_field_index(zan_symbol_t *type_sym, zan_istr_t field_name);
zan_symbol_t * get_field_sym(zan_symbol_t *type_sym, zan_istr_t field_name);
zan_symbol_t * get_method_sym(zan_symbol_t *type_sym, zan_istr_t method_name);
LLVMTypeRef get_struct_llvm_type(zan_irgen_t *g, zan_symbol_t *sym);
int get_virtual_method_index(zan_symbol_t *type_sym, zan_symbol_t *method_sym);
int irgen_find_function(zan_irgen_t *g, zan_symbol_t *sym);
void * irgen_grow(void *base, int *cap, int need, size_t elem);
void irgen_register_function(zan_irgen_t *g, zan_symbol_t *sym, LLVMValueRef fn, LLVMTypeRef fn_type);
int is_arc_managed_type(zan_type_t *t);
int is_builtin_collection_type(zan_type_t *t);
int is_rc_managed_type(zan_type_t *t);
bool llvm_is_nullable(LLVMTypeRef t);
LLVMValueRef load_struct_from_slot(zan_irgen_t *g, LLVMValueRef slot_ptr, LLVMTypeRef st);
const char * loc_site_file(zan_irgen_t *g, zan_loc_t loc);
void local_add(local_scope_t *scope, zan_istr_t name, LLVMValueRef alloca, zan_type_t *type);
pattern_binding_t * local_add_pattern_binding(local_scope_t *scope, zan_ast_node_t *decl);
local_var_t * local_find(local_scope_t *scope, zan_istr_t name);
local_var_t * local_find_async_decl(local_scope_t *scope, zan_ast_node_t *decl);
local_var_t * local_find_async_role(local_scope_t *scope, zan_ast_node_t *decl, int role);
local_var_t * local_find_binding_decl(local_scope_t *scope, zan_ast_node_t *decl);
pattern_binding_t * local_find_pattern_binding(local_scope_t *scope, zan_ast_node_t *decl);
void local_scope_init(local_scope_t *s, zan_arena_t *arena);
local_scope_t * local_scope_new(zan_arena_t *arena);
LLVMTypeRef local_slot_type(zan_irgen_t *g, local_var_t *v);
LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type);
bool member_name_is(zan_symbol_t *m, zan_istr_t name);
int method_accepts_arity(zan_symbol_t *m, int argc);
int method_declared_param_count(zan_symbol_t *m);
bool method_introduces_virtual_slot(zan_symbol_t *type_sym, zan_symbol_t *m);
int method_is_params_variadic(zan_symbol_t *m);
zan_symbol_t * method_sym_for_decl(zan_symbol_t *type_sym, zan_ast_node_t *decl);
LLVMValueRef nullable_get_payload(zan_irgen_t *g, LLVMValueRef v);
LLVMValueRef nullable_has_value(zan_irgen_t *g, LLVMValueRef v);
LLVMValueRef nullable_none(LLVMTypeRef nty);
LLVMTypeRef nullable_payload_type(LLVMTypeRef t);
LLVMValueRef nullable_some(zan_irgen_t *g, LLVMTypeRef nty, LLVMValueRef v);
LLVMTypeRef nullable_type_of(zan_irgen_t *g, LLVMTypeRef payload);
LLVMValueRef promote_loaded(zan_irgen_t *g, LLVMValueRef v, zan_type_t *type);
void register_struct_type(zan_irgen_t *g, zan_symbol_t *sym);
void release_all_arc_locals(zan_irgen_t *g, local_scope_t *locals);
int reserve_arc_site(zan_irgen_t *g, zan_symbol_t *sym, zan_type_t *inst, int coll_kind, zan_type_t *coll_elem);
int reserve_closure_site(zan_irgen_t *g);
zan_symbol_t * resolve_iface_overload(zan_symbol_t *iface, zan_istr_t name, int argc, int type_arg_count);
zan_symbol_t * resolve_overload(zan_symbol_t *type_sym, zan_istr_t name, int argc, int type_arg_count);
zan_type_t * resolve_type_ctx(zan_irgen_t *g, zan_ast_node_t *tref);
LLVMValueRef slot_word_index(zan_irgen_t *g, LLVMValueRef idx, unsigned words);
void store_struct_in_slot(zan_irgen_t *g, LLVMValueRef v, LLVMValueRef slot_ptr, zan_ast_node_t *at);
int type_named(zan_type_t *t, const char *n, int len);
LLVMValueRef zan_call2(LLVMBuilderRef b, LLVMTypeRef ty, LLVMValueRef fn, LLVMValueRef *args, unsigned n, const char *nm);
void zan_emit_frame_free(zan_irgen_t *g, LLVMValueRef frame_i8);
LLVMValueRef zan_hdr_is_string(zan_irgen_t *g, LLVMValueRef word, const char *nm);
LLVMValueRef zan_hdr_read_ok(zan_irgen_t *g, LLVMValueRef obj);
void zan_irgen_compact_completed(zan_irgen_t *g, LLVMValueRef fn);
void zan_irgen_emit_oom_check(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef raw);
LLVMValueRef zan_irgen_intern_string(zan_irgen_t *g, const char *text);

/* --- From irgen_abi.c (5 functions) --- */
void abi_add_int_ext_attrs(zan_irgen_t *g, LLVMValueRef fn, zan_type_t *ret, zan_type_t **ptypes, int pc);
unsigned long abi_align_of(LLVMTypeRef t);
LLVMValueRef abi_extern_thunk(zan_irgen_t *g, const char *name, LLVMTypeRef zan_ft);
void abi_pending_report(zan_irgen_t *g);
unsigned long abi_size_of(LLVMTypeRef t);

/* --- From irgen_arc.c (62 functions) --- */
void add_generic_ctor(zan_irgen_t *g, zan_symbol_t *type_sym, zan_ast_node_t *decl, zan_type_t **args, int argc, int param_count, LLVMValueRef fn, LLVMTypeRef fn_type);
void add_generic_fn(zan_irgen_t *g, zan_symbol_t *msym, zan_type_t **args, int argc, LLVMValueRef fn, LLVMTypeRef fn_type);
void add_generic_inst(zan_irgen_t *g, zan_type_t *inst);
void build_class_release_body(zan_irgen_t *g, zan_symbol_t *sym, zan_type_t *inst, LLVMValueRef fn);
LLVMValueRef build_site_field_graph(zan_irgen_t *g, int site_idx);
LLVMTypeRef closure_header_type(zan_irgen_t *g);
void coerce_args_to_params(zan_irgen_t *g, LLVMTypeRef fn_type, LLVMValueRef *call_args, int argc);
LLVMValueRef coerce_struct_value(zan_irgen_t *g, LLVMValueRef v, zan_type_t *t);
LLVMValueRef coerce_struct_value_arg(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef param_ty);
zan_type_t * concretize(zan_irgen_t *g, zan_type_t *t);
void emit_all_class_releases(zan_irgen_t *g);
void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
void emit_arc_retain(zan_irgen_t *g, LLVMValueRef v);
void emit_array_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef arr, LLVMValueRef len);
LLVMValueRef emit_boundary_coerce(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target);
void emit_closure_record_release(zan_irgen_t *g, LLVMValueRef rec);
void emit_closure_release(zan_irgen_t *g, LLVMValueRef v);
void emit_closure_retain(zan_irgen_t *g, LLVMValueRef v);
LLVMValueRef emit_closure_untag(zan_irgen_t *g, LLVMValueRef v);
void emit_collection_release_raw_slot(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef raw, LLVMTypeRef slot_ty);
void emit_collection_slot_store(zan_irgen_t *g, zan_type_t *elem_type, LLVMTypeRef slot_ty, LLVMValueRef slot_ptr, LLVMValueRef value, zan_ast_node_t *rhs, local_scope_t *locals, int overwrite_old);
void emit_collection_value_release(zan_irgen_t *g, zan_type_t *type, LLVMValueRef value, unsigned depth);
void emit_collection_value_retain(zan_irgen_t *g, zan_type_t *type, LLVMValueRef value, unsigned depth);
LLVMValueRef emit_delegate_equals(zan_irgen_t *g, LLVMValueRef a, LLVMValueRef b);
LLVMValueRef emit_delegate_invoke(zan_irgen_t *g, LLVMValueRef dv, LLVMTypeRef fn_type, LLVMTypeRef ret, LLVMValueRef *args, int argc, const char *name);
void emit_dict_release_elems(zan_irgen_t *g, zan_type_t *dict_type, LLVMValueRef col);
void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef col);
void emit_rc_release_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
void emit_rc_retain_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
void emit_site_dtor_table(zan_irgen_t *g);
void emit_site_field_graph_table(zan_irgen_t *g);
void emit_site_live_tables(zan_irgen_t *g);
void emit_site_tyname_table(zan_irgen_t *g);
void emit_string_release(zan_irgen_t *g, LLVMValueRef v);
void emit_string_retain(zan_irgen_t *g, LLVMValueRef v);
void emit_struct_field_capture(zan_irgen_t *g, zan_type_t *ftype, LLVMValueRef field_ptr, LLVMValueRef v, zan_ast_node_t *rhs, local_scope_t *locals);
void emit_struct_local_capture(zan_irgen_t *g, zan_type_t *type, LLVMValueRef slot_alloca, LLVMValueRef v, zan_ast_node_t *rhs, local_scope_t *locals);
void emit_struct_local_release(zan_irgen_t *g, zan_type_t *type, LLVMValueRef slot);
void emit_struct_local_retain(zan_irgen_t *g, zan_type_t *type, LLVMValueRef slot);
void emit_typed_out_store(zan_irgen_t *g, zan_type_t *value_type, LLVMValueRef out_ptr, LLVMValueRef value);
void emit_vtables(zan_irgen_t *g);
LLVMValueRef find_generic_ctor(zan_irgen_t *g, zan_symbol_t *type_sym, zan_ast_node_t *decl, zan_type_t **args, int argc, LLVMTypeRef *out_fn_type);
LLVMValueRef find_generic_fn(zan_irgen_t *g, zan_symbol_t *msym, zan_type_t **args, int argc, LLVMTypeRef *out_fn_type);
zan_type_t * generic_method_ret(zan_irgen_t *g, zan_symbol_t *msym, zan_ast_node_t *call, local_scope_t *locals);
LLVMValueRef get_arc_free_decl(zan_irgen_t *g);
LLVMValueRef get_array_desc(zan_irgen_t *g, zan_type_t *elem_type);
LLVMValueRef get_array_release_decl(zan_irgen_t *g, zan_type_t *elem_type, int rect);
LLVMValueRef get_class_release_decl(zan_irgen_t *g, zan_symbol_t *sym, zan_type_t *inst);
LLVMValueRef get_collection_release_decl(zan_irgen_t *g, int site);
LLVMValueRef get_vtable_global(zan_irgen_t *g, zan_symbol_t *sym);
LLVMValueRef get_vtable_global_inst(zan_irgen_t *g, zan_symbol_t *sym, zan_type_t *inst);
bool is_user_generic_sym(zan_symbol_t *sym);
LLVMValueRef load_dict_value_words(zan_irgen_t *g, LLVMValueRef raw);
void mangle_inst_suffix(char *buf, size_t n, zan_type_t *inst);
void mangle_type_token(char *buf, size_t n, size_t *off, zan_type_t *t);
zan_type_t * subst_type_param(zan_type_t *t, zan_type_t *recv);
zan_type_t * subst_type_param_deep(zan_irgen_t *g, zan_type_t *t, zan_type_t *recv);
bool type_arglists_equal(zan_type_t **a, int an, zan_type_t **b, int bn);
int type_contains_collection_rc(zan_irgen_t *g, zan_type_t *type, unsigned depth);
bool type_is_concrete(zan_type_t *t);
bool types_equal(zan_type_t *a, zan_type_t *b);
zan_type_t * vtable_owner_inst(zan_irgen_t *g, zan_type_t *inst, zan_symbol_t *want);

/* --- From irgen_async.c (46 functions) --- */
void anf_normalize_block(zan_irgen_t *g, zan_ast_node_t *block, int *counter);
bool anf_stmt_contains_await(zan_ast_node_t *st);
int async_count_transfers(zan_ast_node_t *st);
LLVMValueRef async_pending_count_ptr(zan_irgen_t *g, LLVMValueRef frame, LLVMTypeRef ft);
LLVMValueRef async_pending_discard_fn(zan_irgen_t *g, LLVMTypeRef ft);
LLVMValueRef async_pending_field(zan_irgen_t *g, LLVMTypeRef ft, LLVMValueRef record, unsigned field);
void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st);
LLVMValueRef coerce_async_ret(zan_irgen_t *g, LLVMValueRef val);
LLVMValueRef coerce_from_frame_result(zan_irgen_t *g, LLVMValueRef res, zan_type_t *ty);
LLVMValueRef coerce_to_frame_result(zan_irgen_t *g, LLVMValueRef v, zan_type_t *ty);
LLVMValueRef coerce_to_i64(zan_irgen_t *g, LLVMValueRef v);
void emit_async_cancel_check(zan_irgen_t *g, local_scope_t *locals);
void emit_async_check_sub_exc(zan_irgen_t *g, LLVMValueRef sub, LLVMValueRef tmp_mark);
void emit_async_complete(zan_irgen_t *g, local_scope_t *locals, LLVMValueRef result_i64);
void emit_async_complete_epilogue(zan_irgen_t *g, local_scope_t *locals);
void emit_async_eh_prologue(zan_irgen_t *g);
void emit_async_eh_unarm(zan_irgen_t *g);
void emit_async_exc_epilogue(zan_irgen_t *g, local_scope_t *locals);
void emit_async_finalize_slots(zan_irgen_t *g);
void emit_async_pending_discard(zan_irgen_t *g, LLVMValueRef frame, LLVMTypeRef ft, LLVMValueRef depth);
void emit_async_pending_discard_body(zan_irgen_t *g, LLVMTypeRef ft, zan_type_t *ret_type);
void emit_async_pending_discard_preserving(zan_irgen_t *g, LLVMValueRef depth);
void emit_async_pending_pop(zan_irgen_t *g, LLVMValueRef record);
LLVMValueRef emit_async_pending_push(zan_irgen_t *g, LLVMValueRef value, LLVMValueRef tid, LLVMValueRef kind);
void emit_async_pending_return(zan_irgen_t *g, LLVMValueRef value);
LLVMValueRef emit_async_pending_take_return(zan_irgen_t *g);
LLVMValueRef emit_async_pending_top(zan_irgen_t *g);
bool emit_async_preempt_site(zan_irgen_t *g, LLVMBasicBlockRef resume_target);
void emit_co_cancel_delay(zan_irgen_t *g, LLVMValueRef frame);
void emit_unhandled_class_exc_print(zan_irgen_t *g, LLVMValueRef fn, LLVMTypeRef printf_ty, LLVMValueRef printf_fn, LLVMValueRef cname, LLVMValueRef exc, LLVMValueRef tid, LLVMBasicBlockRef cont_bb);
zan_type_t * foreach_proto_current_type(zan_irgen_t *g, zan_type_t *ct);
zan_type_t * foreach_proto_enum_type(zan_irgen_t *g, zan_type_t *ct);
LLVMBasicBlockRef get_async_cancel_bb(zan_irgen_t *g, local_scope_t *locals);
LLVMValueRef get_async_cancel_ptr(zan_irgen_t *g);
LLVMValueRef get_async_child_ptr(zan_irgen_t *g);
LLVMBasicBlockRef get_async_requeue_bb(zan_irgen_t *g);
LLVMValueRef get_async_result_ptr(zan_irgen_t *g);
LLVMValueRef get_async_self_i8(zan_irgen_t *g);
LLVMValueRef get_async_self_int(zan_irgen_t *g, LLVMTypeRef ptr_int_ty);
LLVMValueRef get_async_state_ptr(zan_irgen_t *g);
LLVMValueRef get_async_sub_slot_ptr(zan_irgen_t *g);
LLVMBasicBlockRef get_async_suspend_ret_bb(zan_irgen_t *g);
LLVMValueRef get_co_cancel_fn(zan_irgen_t *g);
LLVMValueRef get_co_isdone_fn(zan_irgen_t *g);
LLVMValueRef get_co_track_fn(zan_irgen_t *g);
LLVMValueRef get_co_untrack_fn(zan_irgen_t *g);

/* --- From irgen_builtins.c (36 functions) --- */
LLVMValueRef coerce_dict_key(zan_irgen_t *g, LLVMValueRef key, zan_type_t *kt);
LLVMValueRef emit_dict_find(zan_irgen_t *g, zan_type_t *dict_type, LLVMValueRef draw, LLVMValueRef key);
LLVMValueRef emit_eh_buf_ptr(zan_irgen_t *g, LLVMValueRef idx);
void emit_eh_longjmp(zan_irgen_t *g, LLVMValueRef bufp);
LLVMValueRef emit_eh_mark_ptr(zan_irgen_t *g, LLVMValueRef idx);
LLVMValueRef emit_eh_setjmp(zan_irgen_t *g, LLVMValueRef bufp);
void emit_eh_tmp_drop(zan_irgen_t *g, LLVMValueRef obj);
void emit_eh_tmp_pop(zan_irgen_t *g);
void emit_eh_tmp_push(zan_irgen_t *g, LLVMValueRef obj);
void emit_eh_tmp_push_slot(zan_irgen_t *g, LLVMValueRef slot, int kind);
void emit_eh_unwind_to_handler(zan_irgen_t *g, LLVMValueRef top);
void emit_invalidate_freed_string(zan_irgen_t *g, zan_ast_node_t *arg, local_scope_t *locals);
LLVMValueRef emit_null_cond(zan_irgen_t *g, zan_ast_node_t *expr, zan_ast_node_t *qmem, local_scope_t *locals);
void fill_default_args(zan_irgen_t *g, zan_ast_node_t *call, zan_symbol_t *method_sym);
LLVMValueRef get_char_case_fn(zan_irgen_t *g);
LLVMValueRef get_class_tid_global(zan_irgen_t *g, zan_symbol_t *sym);
LLVMValueRef get_dict_remove_fn(zan_irgen_t *g);
LLVMValueRef get_dict_set_fn(zan_irgen_t *g);
LLVMValueRef get_eh_exc_owned_global(zan_irgen_t *g);
LLVMValueRef get_eh_exc_tid_global(zan_irgen_t *g);
void get_eh_globals(zan_irgen_t *g, LLVMValueRef *top, LLVMValueRef *bufs, LLVMValueRef *exc);
LLVMValueRef get_eh_tid_match_fn(zan_irgen_t *g);
LLVMValueRef get_eh_tid_name_fn(zan_irgen_t *g);
LLVMValueRef get_eh_tmp_top_global(zan_irgen_t *g);
LLVMValueRef get_eh_tmp_unwind_fn(zan_irgen_t *g);
LLVMValueRef get_libc_fn(zan_irgen_t *g, const char *name, LLVMTypeRef ty);
LLVMValueRef get_static_field_global(zan_irgen_t *g, zan_symbol_t *class_sym, zan_symbol_t *fsym, zan_type_t *inst);
LLVMValueRef get_str_case_fn(zan_irgen_t *g, int upper);
LLVMValueRef get_str_join_fn(zan_irgen_t *g);
LLVMValueRef get_str_last_index_of_fn(zan_irgen_t *g);
LLVMValueRef get_str_replace_fn(zan_irgen_t *g);
LLVMValueRef get_str_split_fn(zan_irgen_t *g);
LLVMValueRef get_str_trim_fn(zan_irgen_t *g);
void pack_params_args(zan_irgen_t *g, zan_ast_node_t *call, zan_symbol_t *method_sym, local_scope_t *locals);
void reorder_named_args_impl(zan_irgen_t *g, zan_ast_list_t *args, zan_loc_t loc, zan_ast_node_t *decl);
zan_type_t * static_access_inst(zan_irgen_t *g, zan_ast_node_t *obj_expr);

/* --- From irgen_call.c (1 functions) --- */
LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);

/* --- From irgen_call_async.c (2 functions) --- */
LLVMValueRef emit_detach_async_call(zan_irgen_t *g, LLVMValueRef sub, bool keep_result, bool is_explicit_spawn);
LLVMValueRef emit_task_action_run(zan_irgen_t *g, LLVMValueRef lv, zan_ast_node_t *sub_arg, local_scope_t *locals);

/* --- From irgen_call_builtins.c (1 functions) --- */
LLVMValueRef emit_builtin_call_intercept(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, bool *matched);

/* --- From irgen_call_spec.c (26 functions) --- */
bool call_receiver_is_open_generic(zan_irgen_t *g, zan_ast_node_t *call, local_scope_t *locals);
bool class_inst_implements_iface(zan_irgen_t *g, zan_symbol_t *cls, zan_type_t *inst, zan_type_t *obj_ty);
void diagnose_unresolved_static_chain(zan_irgen_t *g, zan_ast_node_t *recv);
int emit_call_arg_eh_push(zan_irgen_t *g, zan_ast_node_t *arg, LLVMValueRef val, local_scope_t *locals);
void emit_intrinsic_drop_recv(zan_irgen_t *g, zan_ast_node_t *lobj, LLVMValueRef recv, local_scope_t *locals, int pushed);
int emit_intrinsic_own_recv(zan_irgen_t *g, zan_ast_node_t *lobj, LLVMValueRef recv, local_scope_t *locals);
LLVMValueRef emit_method_spec_call(zan_irgen_t *g, int idx, zan_ast_node_t *call, zan_ast_node_t *recv_expr, local_scope_t *locals);
LLVMValueRef emit_object_family_call(zan_irgen_t *g, zan_ast_node_t *expr, int fam, local_scope_t *locals);
LLVMValueRef emit_parse_bad_pred(zan_irgen_t *g, LLVMValueRef s, LLVMValueRef ep);
LLVMValueRef emit_parse_str_guard(zan_irgen_t *g, LLVMValueRef s, zan_loc_t loc);
LLVMValueRef emit_string_copy_range(zan_irgen_t *g, LLVMValueRef start, LLVMValueRef end);
LLVMValueRef emit_string_like_arg(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals, int *owned);
LLVMValueRef emit_unresolved_call_result(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
int format_name_path(zan_ast_node_t *n, char *buf, int cap);
bool ident_names_own_field(zan_irgen_t *g, zan_ast_node_t *e);
int irgen_enum_members(zan_symbol_t *sym, zan_symbol_t **out_syms, long long *out_vals, int cap);
int is_string_like_expr(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool lambda_body_has_value_return(zan_ast_node_t *n);
bool math_arg_is_numeric(LLVMValueRef v);
LLVMValueRef math_arg_to_double(zan_irgen_t *g, LLVMValueRef v);
bool math_require_numeric(zan_irgen_t *g, zan_ast_node_t *expr, zan_ast_node_t *arg_ast, LLVMValueRef v, local_scope_t *locals, const char *name);
void release_string_like_arg(zan_irgen_t *g, zan_ast_node_t *e, LLVMValueRef v, int owned, local_scope_t *locals);
bool src_method_takes_over(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
int try_method_spec(zan_irgen_t *g, zan_symbol_t *msym, zan_ast_node_t *call, zan_ast_node_t *recv_expr, local_scope_t *locals);
bool type_is_text_arg(zan_type_t *t);
bool type_is_textual_or_numeric(zan_type_t *t);

/* --- From irgen_emit.c (4 functions) --- */
void emit_pending_method_specs(zan_irgen_t *g);
int get_or_create_method_spec(zan_irgen_t *g, zan_symbol_t *msym, zan_type_t **bind, int bindc, zan_type_t *owner_inst);
bool method_is_tp_template(zan_irgen_t *g, zan_ast_node_t *member);
zan_status_t zan_bind_target_layout(zan_irgen_t *g, LLVMTargetMachineRef *out_tm);

/* --- From irgen_expr.c (23 functions) --- */
LLVMValueRef cpu_feature_fn(zan_irgen_t *g);
bool emit_aes_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
bool emit_bit_operations_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
LLVMValueRef emit_byte_eq_mask_i64(zan_irgen_t *g, LLVMValueRef a, LLVMValueRef b, unsigned lanes);
bool emit_bytes_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
bool emit_cpu_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
bool emit_native_memory_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
bool emit_pixel_ops_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
LLVMValueRef emit_pmovmskb_portable(zan_irgen_t *g, LLVMValueRef v, unsigned lanes);
LLVMValueRef emit_property_getter_call(zan_irgen_t *g, zan_symbol_t *getter, zan_type_t *recv_type, LLVMValueRef recv, zan_ast_node_t *obj_ast, local_scope_t *locals);
void emit_property_setter_call(zan_irgen_t *g, zan_symbol_t *setter, zan_type_t *recv_type, LLVMValueRef recv, LLVMValueRef value, zan_ast_node_t *obj_ast, zan_ast_node_t *rhs_ast, local_scope_t *locals);
bool emit_span_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
bool emit_sse2_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
bool emit_sse42_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
bool emit_vector128_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
bool emit_vector256_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, LLVMValueRef *out);
int expr_has_reliable_string_bounds(zan_ast_node_t *expr, local_scope_t *locals);
bool is_target_class_obj(zan_ast_node_t *obj, const char *name, size_t len, local_scope_t *locals);
zan_symbol_t * property_getter_sym(zan_irgen_t *g, zan_symbol_t *prop);
zan_symbol_t * property_setter_sym(zan_irgen_t *g, zan_symbol_t *prop);
LLVMTypeRef span_elem_storage_type(zan_irgen_t *g, zan_type_t *elem_type);
int type_declares_member(zan_symbol_t *cls, zan_istr_t name);
LLVMValueRef v16i8_to_vec128(zan_irgen_t *g, LLVMValueRef v16);

/* --- From irgen_expr_access.c (4 functions) --- */
LLVMValueRef emit_expr_index(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_member_access(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_string_interp(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
int interp_format_to_printf(const zan_istr_t *spec, bool is_float, bool is_ulong, bool *want_float, char *out, size_t outcap);

/* --- From irgen_expr_construct.c (16 functions) --- */
LLVMValueRef emit_box_struct(zan_irgen_t *g, zan_type_t *st, zan_ast_node_t *src_expr, LLVMValueRef val, local_scope_t *locals);
LLVMValueRef emit_expr_await_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_cast_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_conditional(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_new_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_query_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_switch_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_tuple(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_with_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_runtime_is_check(zan_irgen_t *g, LLVMValueRef x, const char *tname);
LLVMValueRef emit_runtime_is_check_name(zan_irgen_t *g, LLVMValueRef x, zan_type_t *tt);
LLVMValueRef emit_user_conversion(zan_irgen_t *g, zan_type_t *from_type, zan_type_t *to_type, const char *op_name, LLVMValueRef val, zan_ast_node_t *val_expr, local_scope_t *locals);
zan_symbol_t * find_user_conversion(zan_irgen_t *g, zan_type_t *from_type, zan_type_t *to_type, const char *op_name);
bool irgen_boxed_eligible_struct(zan_irgen_t *g, zan_type_t *t);
zan_ast_node_t * owned_rhs_marker(zan_irgen_t *g, zan_loc_t loc);
int type_is_or_derives(zan_type_t *s, zan_type_t *t);

/* --- From irgen_expr_core.c (51 functions) --- */
void check_generic_invariance(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src, zan_ast_node_t *at, const char *what);
void check_implicit_narrowing(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src, zan_ast_node_t *at, const char *what);
void check_no_boxing_conversion(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src, zan_ast_node_t *at, const char *what);
void check_value_type_mismatch(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src, zan_ast_node_t *at, const char *what);
bool class_derives_from(zan_symbol_t *derived, zan_symbol_t *base);
bool class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface);
zan_type_t * container_elem_type(zan_type_t *t);
zan_type_t * dict_key_type(zan_irgen_t *g, zan_type_t *t);
zan_type_t * dict_value_type(zan_type_t *t);
LLVMValueRef emit_guarded_member_object(zan_irgen_t *g, zan_ast_node_t *member, local_scope_t *locals);
LLVMValueRef emit_guarded_member_object_ex(zan_irgen_t *g, zan_ast_node_t *member, local_scope_t *locals, bool async_gate);
LLVMValueRef emit_task_member(zan_irgen_t *g, LLVMValueRef hp, zan_type_t *rt, int mode);
void emit_weak_read_guard(zan_irgen_t *g, zan_ast_node_t *read_expr, LLVMValueRef value, zan_loc_t loc, local_scope_t *locals);
zan_symbol_t * expr_class_sym(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
int expr_family(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool expr_is_bool(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool expr_is_char(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool expr_is_int32(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool expr_is_longish(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool expr_is_uint(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool expr_is_ulong(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool expr_is_unsigned_int(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool fill_ctor_default_args(zan_irgen_t *g, zan_symbol_t *type_sym, const zan_ast_list_t *args, zan_ast_list_t *out);
zan_symbol_t * find_extension_method(zan_irgen_t *g, zan_type_t *recv_ty, zan_istr_t name, int argc, zan_ast_node_t *call, zan_ast_node_t *recv_expr, local_scope_t *locals);
bool has_declared_ctor(zan_irgen_t *g, zan_symbol_t *sym);
bool implicit_ctor_for_arg(zan_irgen_t *g, zan_type_t *target, zan_type_t *source, zan_ast_node_t *arg, local_scope_t *locals);
void infer_cache_invalidate(void);
zan_type_t * infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool irgen_type_is_value_kind(zan_type_t *t);
bool is_call_to(zan_ast_node_t *expr, const char *obj, const char *method);
bool is_implicit_ctor_source(zan_type_t *t);
bool is_name_path(zan_ast_node_t *node);
int is_span_type(zan_type_t *t);
bool is_string_expr(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
bool istr_matches_class(zan_istr_t name, const char *cls);
zan_type_t * member_access_field_type(zan_irgen_t *g, local_scope_t *locals, zan_ast_node_t *member);
zan_ast_node_t * name_path_head(zan_ast_node_t *node);
int op_index_param_offset(zan_symbol_t *m);
bool qpath_ns_ok(zan_irgen_t *g, zan_ast_node_t *path, zan_symbol_t *sym);
int render_type_full(zan_type_t *t, char *buf, int cap);
int render_type_ref_name(const zan_ast_node_t *t, char *buf, int cap);
zan_symbol_t * resolve_op_overload(zan_irgen_t *g, zan_symbol_t *type_sym, zan_istr_t name, zan_ast_node_t *call, local_scope_t *locals);
zan_symbol_t * resolve_overload_typed(zan_irgen_t *g, zan_symbol_t *type_sym, zan_istr_t name, zan_ast_node_t *call, local_scope_t *locals);
bool target_is_wasm32(zan_irgen_t *g);
bool types_concrete_equal(zan_type_t *a, zan_type_t *b);
LLVMValueRef zan_array_alloc(zan_irgen_t *g, LLVMValueRef total, LLVMValueRef count);
LLVMValueRef zan_array_alloc_typed(zan_irgen_t *g, LLVMValueRef total, LLVMValueRef count, zan_type_t *elem_type);
LLVMValueRef zan_array_len(zan_irgen_t *g, LLVMValueRef arr);
bool zan_convert_tostring_exact_decl(zan_irgen_t *g, zan_ast_node_t *arg, local_scope_t *locals);
LLVMValueRef zan_mdarray_alloc(zan_irgen_t *g, LLVMValueRef *dims, int rank, LLVMTypeRef elem_llvm);
bool zan_type_defines(zan_irgen_t *g, const char *type_name, const char *method);

/* --- From irgen_expr_dispatch.c (1 functions) --- */
LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);

/* --- From irgen_expr_lambda.c (23 functions) --- */
int body_writes_ident(zan_irgen_t *g, zan_ast_node_t *body, zan_istr_t name);
void box_captured_parameter(zan_irgen_t *g, local_scope_t *locals, zan_ast_node_t *param, zan_type_t *type, LLVMTypeRef payload, LLVMValueRef value, zan_ast_node_t *body);
LLVMValueRef box_value_ptr(zan_irgen_t *g, LLVMValueRef cell, LLVMTypeRef payload);
LLVMValueRef build_static_mg_record(zan_irgen_t *g, zan_loc_t loc, const char *lname, LLVMTypeRef rec_ty, LLVMValueRef thunk);
void check_delegate_async_match(zan_irgen_t *g, zan_ast_node_t *e, zan_type_t *dt, local_scope_t *locals);
LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg, zan_type_t *ptype, local_scope_t *locals);
LLVMValueRef emit_binding_acc_delegate(zan_irgen_t *g, LLVMValueRef acc, LLVMTypeRef acc_ty);
LLVMValueRef emit_box_cell(zan_irgen_t *g, zan_loc_t loc, LLVMTypeRef payload, zan_type_t *vtype, LLVMValueRef init);
LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr, zan_type_t *expected, local_scope_t *locals);
LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym, LLVMValueRef recv, zan_loc_t loc);
LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg, local_scope_t *locals);
LLVMValueRef emit_ref_lvalue_ptr(zan_irgen_t *g, zan_ast_node_t *tgt, local_scope_t *locals);
void infer_method_tp_bindings(zan_irgen_t *g, zan_symbol_t *msym, zan_ast_node_t *call, zan_ast_node_t *recv_expr, local_scope_t *locals, zan_type_t **bind);
bool irgen_is_tree_lambda(zan_irgen_t *g, zan_ast_node_t *expr);
bool irgen_type_is_tree_target(zan_type_t *t);
int local_is_lambda_captured(zan_irgen_t *g, local_scope_t *locals, zan_ast_node_t *body, zan_ast_node_t *decl);
zan_type_t * method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx);
zan_type_t * method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym, int idx, zan_ast_node_t *call, zan_ast_node_t *recv_expr, local_scope_t *locals);
bool method_ret_is_bare_tp(zan_symbol_t *msym);
zan_type_t * method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym, zan_ast_node_t *call, zan_ast_node_t *recv_expr, local_scope_t *locals);
void own_box_cell(zan_irgen_t *g, local_var_t *v, LLVMValueRef cell);
zan_type_t * subst_method_tp(zan_irgen_t *g, zan_type_t *t, zan_ast_list_t *tps, zan_type_t **bind);
bool type_mentions_tp(zan_type_t *t);

/* --- From irgen_expr_ops.c (23 functions) --- */
LLVMValueRef coerce_to_decimal(zan_irgen_t *g, LLVMValueRef val);
LLVMValueRef emit_binding_value(zan_irgen_t *g, zan_type_t *bind_t, zan_ast_node_t *rhs, local_scope_t *locals);
LLVMValueRef emit_console_color_get(zan_irgen_t *g, int is_bg);
void emit_console_color_reset(zan_irgen_t *g);
void emit_decl_field_initializers(zan_irgen_t *g, zan_symbol_t *type_sym, zan_type_t *recv_type, LLVMValueRef object_ptr, local_scope_t *locals);
void emit_dict_value_set(zan_irgen_t *g, zan_type_t *dict_type, LLVMValueRef raw, LLVMValueRef key, LLVMValueRef value, zan_ast_node_t *key_expr, zan_ast_node_t *value_expr, local_scope_t *locals);
LLVMValueRef emit_expr_assignment(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_binary(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_identifier(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_unary(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_implicit_box(zan_irgen_t *g, zan_type_t *lt, LLVMValueRef val, zan_ast_node_t *rhs, local_scope_t *locals);
void emit_implicit_field_initializers(zan_irgen_t *g, zan_symbol_t *type_sym, zan_type_t *recv_type, LLVMValueRef object_ptr, local_scope_t *locals);
LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, int is_prefix);
LLVMValueRef emit_mdarray_elem_ptr(zan_irgen_t *g, LLVMValueRef arr_ptr, zan_ast_node_t *expr, LLVMTypeRef elem_llvm, local_scope_t *locals, LLVMValueRef *out_oob);
LLVMValueRef emit_struct_elem_ptr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_typed_equality(zan_irgen_t *g, zan_type_t *type, LLVMValueRef left, LLVMValueRef right);
zan_type_t * field_store_type(zan_irgen_t *g, zan_symbol_t *fsym, zan_type_t *recv);
LLVMValueRef finish_member_of_temp(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, zan_type_t *obj_type, LLVMValueRef obj_val, LLVMValueRef fv);
LLVMValueRef load_collection_slot_value(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef slot_ptr);
int receiver_is_owned_temp(zan_irgen_t *g, zan_ast_node_t *object, local_scope_t *locals, zan_type_t *obj_type, LLVMValueRef obj_val);
bool recv_is_stringlike(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
zan_type_t * subst_delegate_sig(zan_irgen_t *g, zan_type_t *t, zan_type_t *recv);
int type_is_binding(zan_type_t *t);

/* --- From irgen_generics.c (82 functions) --- */
void arc_own_local(zan_irgen_t *g, local_scope_t *locals);
int call_arg_is_owned_temp(zan_irgen_t *g, zan_ast_node_t *arg, zan_type_t *at, local_scope_t *locals);
int call_consumes_free_arg(LLVMValueRef callee);
LLVMValueRef coerce_generic_result(zan_irgen_t *g, LLVMValueRef result, zan_symbol_t *method_sym, zan_type_t *recv);
void coerce_int_pair(zan_irgen_t *g, LLVMValueRef *a, LLVMValueRef *b);
LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target);
void collect_inst_expr(zan_irgen_t *g, zan_ast_node_t *e);
void discover_generic_insts(zan_irgen_t *g, zan_ast_node_t *unit);
LLVMValueRef emit_bool_cond(zan_irgen_t *g, LLVMValueRef val, zan_ast_node_t *ast, local_scope_t *locals);
LLVMValueRef emit_bool_to_cstr(zan_irgen_t *g, LLVMValueRef v);
LLVMValueRef emit_char_to_cstr(zan_irgen_t *g, LLVMValueRef val);
void emit_clear_owned_locals_range(zan_irgen_t *g, local_scope_t *locals, int start);
LLVMValueRef emit_cstr_len_of(zan_irgen_t *g, LLVMValueRef s, zan_ast_node_t *ast);
LLVMValueRef emit_delegate_call(zan_irgen_t *g, zan_type_t *delegate_type, LLVMValueRef fn_ptr, zan_ast_node_t *call, local_scope_t *locals);
LLVMValueRef emit_dispatch_call(zan_irgen_t *g, zan_symbol_t *static_sym, zan_symbol_t *method_sym, LLVMValueRef static_fn, LLVMTypeRef fn_type, LLVMValueRef *call_args, int argc, const char *cn);
LLVMValueRef emit_entry_scratch(zan_irgen_t *g, unsigned size, const char *name);
void emit_extern_call_len_invalidate(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef *args, int argc, zan_ast_node_t *call, local_scope_t *locals);
void emit_fatal_report(zan_irgen_t *g, LLVMValueRef text, int exit_code);
void emit_fopen_check(zan_irgen_t *g, LLVMValueRef fp, const char *msg);
void emit_guard_report3(zan_irgen_t *g, LLVMValueRef file_gv, unsigned line, unsigned col, LLVMValueRef msg_gv);
void emit_guard_report_merged(zan_irgen_t *g, LLVMValueRef text);
void emit_index_bounds_check(zan_irgen_t *g, LLVMValueRef index, LLVMValueRef length, zan_loc_t loc, const char *kind);
LLVMValueRef emit_index_i64(zan_irgen_t *g, LLVMValueRef value, const char *name);
LLVMValueRef emit_index_oob_pred(zan_irgen_t *g, LLVMValueRef index, LLVMValueRef length);
void emit_index_range_check(zan_irgen_t *g, LLVMValueRef index, LLVMValueRef length, bool allow_end, zan_loc_t loc, const char *kind);
LLVMValueRef emit_index_safe_bounds(zan_irgen_t *g, LLVMValueRef index, LLVMValueRef length, zan_loc_t loc, const char *kind);
LLVMValueRef emit_index_safe_check(zan_irgen_t *g, LLVMValueRef index, LLVMValueRef length, bool allow_end, zan_loc_t loc, const char *kind);
void emit_io_abort_if(zan_irgen_t *g, LLVMValueRef cond, const char *msg);
void emit_leak_report_support(zan_irgen_t *g);
void emit_obj_local_store(zan_irgen_t *g, local_var_t *v, LLVMValueRef val, zan_type_t *vtype, zan_ast_node_t *rhs, local_scope_t *locals);
void emit_oob_store_finish(zan_irgen_t *g, LLVMBasicBlockRef do_bb, LLVMBasicBlockRef end_bb);
void emit_oob_store_split(zan_irgen_t *g, LLVMValueRef oob, LLVMBasicBlockRef *do_bb, LLVMBasicBlockRef *end_bb);
void emit_rc_capture_local(zan_irgen_t *g, zan_type_t *type, LLVMValueRef slot_alloca, LLVMValueRef v, zan_ast_node_t *rhs, local_scope_t *locals);
void emit_rc_store_field(zan_irgen_t *g, zan_type_t *type, LLVMValueRef field_ptr, LLVMValueRef v, zan_ast_node_t *rhs, local_scope_t *locals, int is_weak);
void emit_release_active_catch_excs(zan_irgen_t *g, int base);
void emit_release_obj_local(zan_irgen_t *g, local_var_t *v);
void emit_release_obj_value(zan_irgen_t *g, LLVMValueRef cur);
void emit_release_owned_call_temp(zan_irgen_t *g, zan_ast_node_t *arg, LLVMValueRef val, local_scope_t *locals);
void emit_release_owned_locals(zan_irgen_t *g, local_scope_t *locals);
void emit_release_owned_locals_from(zan_irgen_t *g, local_scope_t *locals, int start);
void emit_release_owned_locals_range(zan_irgen_t *g, local_scope_t *locals, int start);
void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error, zan_loc_t loc, const char *msg);
void emit_sb_append_bytes(zan_irgen_t *g, LLVMValueRef sbp, LLVMValueRef s, LLVMValueRef slen);
LLVMValueRef emit_soft_base_select(zan_irgen_t *g, LLVMValueRef base, LLVMValueRef isnull, zan_loc_t loc);
void emit_span_safe_window(zan_irgen_t *g, LLVMValueRef *start_out, LLVMValueRef *window_out, LLVMValueRef length, zan_loc_t loc, const char *kind);
void emit_span_window_check(zan_irgen_t *g, LLVMValueRef start, LLVMValueRef window, LLVMValueRef length, zan_loc_t loc, const char *kind);
LLVMValueRef emit_str_concat(zan_irgen_t *g, LLVMValueRef a, LLVMValueRef b);
LLVMValueRef emit_str_concat_n(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_str_nonnull(zan_irgen_t *g, LLVMValueRef v);
LLVMValueRef emit_string_alloc_rc(zan_irgen_t *g, LLVMValueRef payload_size);
LLVMValueRef emit_string_base_guard(zan_irgen_t *g, LLVMValueRef payload, zan_loc_t loc);
LLVMValueRef emit_string_buffer_len(zan_irgen_t *g, LLVMValueRef payload, zan_loc_t loc);
LLVMValueRef emit_string_elem_guard(zan_irgen_t *g, LLVMValueRef payload, LLVMValueRef index, zan_loc_t loc, LLVMValueRef *arr_ptr_out);
void emit_string_len_invalidate(zan_irgen_t *g, LLVMValueRef payload);
void emit_string_len_set(zan_irgen_t *g, LLVMValueRef payload, LLVMValueRef len);
LLVMValueRef emit_string_length(zan_irgen_t *g, LLVMValueRef payload, zan_loc_t loc);
LLVMValueRef emit_string_literal_rc(zan_irgen_t *g, zan_istr_t text);
LLVMValueRef emit_to_cstr(zan_irgen_t *g, LLVMValueRef val);
LLVMValueRef emit_to_cstr_of(zan_irgen_t *g, LLVMValueRef val, zan_ast_node_t *ast, local_scope_t *locals);
LLVMValueRef emit_to_cstr_u(zan_irgen_t *g, LLVMValueRef val, bool is_unsigned);
LLVMValueRef emit_value_as_cstr(zan_irgen_t *g, LLVMValueRef v);
LLVMValueRef emit_widen_i64_for_print(zan_irgen_t *g, LLVMValueRef v);
int expr_is_local_ident(zan_ast_node_t *e, local_scope_t *locals);
int expr_yields_delegate_value(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
int expr_yields_owned_rc_value(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
LLVMValueRef get_co_reap_fn(zan_irgen_t *g);
LLVMValueRef get_str_ordinal_cmp_fn(zan_irgen_t *g, zan_loc_t loc);
zan_type_t * ident_inst_type(zan_irgen_t *g, zan_ast_node_t *e);
int irgen_expr_was_boxed(zan_irgen_t *g, zan_ast_node_t *e);
void irgen_note_boxed_expr(zan_irgen_t *g, zan_ast_node_t *e);
bool is_str_concat_node(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
int local_is_dyn_obj(zan_irgen_t *g, local_var_t *v);
int local_owns_arc(local_var_t *v);
int local_slot_owns_rc(local_var_t *v);
int member_field_is_weak(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
zan_type_t * member_owned_field_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
void release_boxed_local(zan_irgen_t *g, local_var_t *v);
LLVMValueRef route_generic_method(zan_irgen_t *g, zan_type_t *recv_ty, zan_symbol_t *method_sym, LLVMValueRef erased_fn, LLVMTypeRef erased_ty, LLVMTypeRef *out_ty);
LLVMValueRef struct_base_ptr(zan_irgen_t *g, local_var_t *local, LLVMTypeRef st);
bool sym_declares_extern(zan_symbol_t *sym);
void zan_irgen_emit_string_deobf(zan_irgen_t *g);
LLVMValueRef zan_store_fit(zan_irgen_t *g, LLVMValueRef val, LLVMValueRef ptr);

/* --- From irgen_reflect.c (9 functions) --- */
void emit_site_meta_table(zan_irgen_t *g);
bool refl_emit_instance_call(zan_irgen_t *g, zan_type_t *rt, LLVMValueRef recv, zan_istr_t name, zan_ast_node_t **args, int argc, local_scope_t *locals, LLVMValueRef *out);
bool refl_emit_typeinfo_member(zan_irgen_t *g, LLVMValueRef ti, zan_istr_t name, zan_ast_node_t **args, int argc, local_scope_t *locals, LLVMValueRef *out);
void refl_finalize_mtabs(zan_irgen_t *g);
LLVMValueRef refl_meta_for(zan_irgen_t *g, zan_type_t *t, const char *disp, int disp_len);
LLVMValueRef refl_obj_type_fn(zan_irgen_t *g);
void refl_reserve_thunk_sites(zan_irgen_t *g);
bool refl_type_needs_site_meta(zan_irgen_t *g, zan_symbol_t *sym);
void zan_irgen_emit_arc_desc_init(zan_irgen_t *g);


/* --- From irgen_stmt.c (7 functions) --- */
bool call_is_async_callee(zan_irgen_t *g, zan_ast_node_t *call, local_scope_t *locals);
void emit_eh_disarm_from(zan_irgen_t *g, int base);
void emit_eh_hook_call(zan_irgen_t *g, const char *name);
void emit_finally_body(zan_irgen_t *g, local_scope_t *locals, const zan_irgen_finally_entry_t *entry);
void emit_finallys_left_by_throw(zan_irgen_t *g, local_scope_t *locals);
void emit_release_static_rc_fields(zan_irgen_t *g, zan_ast_node_t *unit);
void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);

/* --- From irgen_weak.c (4 functions) --- */
void emit_defer_fallback(zan_irgen_t *g);
LLVMValueRef emit_weak_field_load(zan_irgen_t *g, LLVMValueRef field_ptr, LLVMTypeRef val_ty);
void emit_weak_runtime(zan_irgen_t *g);
void emit_weak_store(zan_irgen_t *g, LLVMValueRef field_ptr, LLVMValueRef value);

#endif /* ZAN_IRGEN_INTERNAL_H */
