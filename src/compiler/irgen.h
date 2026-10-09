/* irgen.h -- LLVM IR generation for the Zan language. */

#ifndef ZAN_IRGEN_H
#define ZAN_IRGEN_H

#include "zan.h"
#include "ast.h"
#include "binder.h"
#include <stdlib.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Support.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

/* A frame-resident slot of an async $resume body */
typedef struct {
    LLVMValueRef slot_alloca;
    LLVMTypeRef  llvm;
    int          frame_index;
} zan_async_slot_t;

/* 编译器生成的保护文本驻留表项 */
typedef struct zan_str_intern {
    char *text;
    LLVMValueRef gv;
    struct zan_str_intern *next;
} zan_str_intern_t;

/* 代码生成器动态堆表扩容辅助原语 */
static inline bool zan_tab_grow(void **items, int *cap, size_t elem,
                                int initial) {
    int ncap = *cap ? *cap * 2 : initial;
    void *n = realloc(*items, (size_t)ncap * elem);
    if (!n) return false;
    *items = n;
    *cap = ncap;
    return true;
}

/* 确保表容量覆盖指定索引，新扩展槽位清零 */
static inline bool zan_tab_reserve(void **items, int *cap, size_t elem,
                                  int index, int initial) {
    if (index < *cap) return true;
    int ncap = *cap ? *cap : initial;
    while (ncap <= index) ncap *= 2;
    void *n = realloc(*items, (size_t)ncap * elem);
    if (!n) return false;
    memset((char *)n + (size_t)*cap * elem, 0,
           (size_t)(ncap - *cap) * elem);
    *items = n;
    *cap = ncap;
    return true;
}

#define ZAN_TAB_ENSURE(tab, cnt, cap, initial) \
    ((cnt) < (cap) || zan_tab_grow((void **)&(tab), &(cap), sizeof(*(tab)), (initial)))

/* 表达式推导递归深度上限（防止类型循环推导死循环） */
#define ZAN_MAX_INFER_DEPTH 16384

/* Nesting depth of try/finally regions a single function body may be inside. */
#define ZAN_MAX_FINALLY_DEPTH 256

/* Armed try handlers tracked at once */
#define ZAN_MAX_ARMED_TRY 1024

typedef struct zan_irgen_pending_scope {
    struct zan_irgen_pending_scope *parent;
    int handler_id; /* HPENDING entry preceding this region's pending exit */
    zan_ast_node_t *body;
} zan_irgen_pending_scope_t;

typedef struct zan_irgen_pending_context {
    zan_irgen_pending_scope_t *scope;
    zan_irgen_pending_scope_t *break_scope;
    zan_irgen_pending_scope_t *continue_scope;
} zan_irgen_pending_context_t;

typedef struct zan_goto_label_rec {
    zan_istr_t        name;
    LLVMValueRef      fn;
    LLVMBasicBlockRef bb;
    int               defined;       /* a label statement emitted here */
    int               fin_depth;     /* finally/lock stack depth */
    int               eh_armed_base; /* armed-handler stack depth */
    int               catch_base;    /* active-catch depth */
    int               locals_base;   /* locals scope depth */
    int               locals_owned;  /* owning locals in scope */
    zan_irgen_pending_scope_t *pending_scope;
    zan_irgen_pending_scope_t *label_owner;
} zan_goto_label_rec_t;

/* catch 块异常清理上下文项：记录异常变量局部索引以供作用域展开 */
typedef struct zan_irgen_catch_cleanup {
    LLVMValueRef exc_slot;   /* i8* slot holding the caught exception */
    LLVMValueRef owned_slot; /* i32 slot: non-zero when the handler owns it */
    LLVMValueRef tid_slot;   /* 当前捕获异常类型描述符，供无参 throw; 原样重抛 */
} zan_irgen_catch_cleanup_t;

/* One `finally` (or `lock`) region being emitted. */
typedef struct zan_irgen_finally_entry {
    zan_ast_node_t *body;   /* the finally block's AST */
    LLVMValueRef monitor_obj; /* lock (obj) 锁对象槽位，退出时释放监视器 */
    LLVMValueRef continuation_slot; /* frame-resident pending-exit selector */
    struct zan_irgen_finally_shared *shared; /* compatible pending-exit bodies */
    int outer_armed_depth;
    int outer_throw_locals_base;
    int outer_throw_catch_base;
    zan_irgen_pending_scope_t *pending_parent;
    zan_irgen_pending_scope_t *pending_scope;
    bool in_try_body;       /* 正在发射 try 保护块体 */
} zan_irgen_finally_entry_t;

typedef struct zan_goto_fixup {
    zan_istr_t   name;
    LLVMValueRef fn;
    zan_loc_t    loc;
    int          fin_depth;
    int          eh_armed_base;
    int          catch_base;
    int          locals_base;
    int          locals_owned;
    int          resolved;
    LLVMBasicBlockRef from_bb;  /* block holding this goto's forward branch */
    /* Cleanup stacks at the goto site */
    int finally_snap_n;
    struct zan_irgen_finally_entry *finally_snap;
    int catch_snap_n;
    struct zan_irgen_catch_cleanup *catch_snap;
    LLVMValueRef *armed_snap;
    zan_irgen_pending_scope_t *label_owner;
    zan_irgen_pending_context_t pending;
    LLVMBasicBlockRef break_target, continue_target;
    int throw_locals_base, throw_catch_base;
    int loop_locals_base, loop_catch_base;
    int finally_loop_base, eh_armed_loop_base;
    int checked_depth;
} zan_goto_fixup_t;

struct zan_irgen {
    zan_arena_t *arena;
    zan_diag_t *diag;
    zan_binder_t *binder;

    LLVMContextRef ctx;
    LLVMModuleRef mod;
    LLVMBuilderRef builder;

    /* current function being compiled */
    LLVMValueRef current_fn;
    LLVMTypeRef current_fn_ret_type;
    zan_type_t *current_fn_zan_ret_type; /* declared source-language return type */

    /* unique-name counter for null-conditional (`?.`) receiver temps */
    int qdot_counter;

    /* current 'this' context for method bodies */
    LLVMValueRef current_this;       /* alloca for 'this' pointer */
    zan_symbol_t *current_type_sym;  /* type symbol for 'this' */
    zan_ast_node_t *current_fn_body; /* root AST body of the fn being compiled */
    bool current_fn_is_ctor;         /* the fn being compiled is a constructor */
    bool current_fn_is_main;         /* 程序主入口函数：退出前释放静态对象 */
    bool current_fn_no_runtime;      /* [NoRuntime]: emit no ARC in this body */
    /* 处于 Lambda 函数体内部：此时 current_this 为 NULL */
    int lambda_depth;

    /* runtime function declarations */
    LLVMValueRef rt_println;   /* zan_rt_println(const char*) */
    LLVMValueRef rt_print_int; /* zan_rt_print_int(int64) */
    LLVMValueRef rt_print_uint; /* zan_rt_print_uint(uint64) */
    LLVMValueRef rt_print_double; /* zan_rt_print_double(double) */

    /* C library functions for string interpolation */
    LLVMValueRef fn_snprintf;
    LLVMValueRef fn_malloc;
    LLVMValueRef fn_free;
    LLVMValueRef fn_strlen;
    LLVMValueRef fn_strcpy;
    /* 内部 64 位十进制格式化实现 */
    LLVMValueRef fn_itoa64;
    LLVMValueRef fn_strcat;
    /* 模块共享空字符串常量字面量 */
    LLVMValueRef str_empty;

    /* 结构体类型注册表 */
    struct zan_struct_type_entry {
        zan_symbol_t *sym;
        LLVMTypeRef llvm_type;
        /* [StructLayout(LayoutKind */
        bool explicit_layout;
        unsigned long *field_offsets;
        LLVMTypeRef *field_llvm;
        int field_count;
    } *struct_types;
    int struct_type_count;
    int struct_type_cap;

    /* 类级 ARC 字段级联释放函数表 */
    struct zan_class_release_entry {
        zan_symbol_t *sym;
        zan_type_t   *inst;  /* instantiation walked (Acc<Node>), NULL if none */
        LLVMValueRef  fn;
    } *class_release;
    int class_release_count;
    int class_release_cap;

    /* user-defined functions (dynamically grown) */
    struct zan_fn_entry {
        zan_symbol_t *sym;
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
        /* 方法修饰符快照，供虚表与代码清单生成 */
        uint32_t modifiers;
    } *functions;
    int function_count;
    int function_cap;
    /* 符号名至函数索引的 O(1) 查找映射表 */
    struct zan_fn_index_slot {
        zan_symbol_t *sym;
        int idx;
    } *fn_index;
    int fn_index_cap;

    /* break/continue targets */
    LLVMBasicBlockRef break_target;
    LLVMBasicBlockRef continue_target;
    /* 最内层循环首个局部变量索引：break/continue 据此释放局部变量 */
    int loop_locals_base;
    /* 内部辅助实现 */
    int throw_locals_base;
    /* catch bodies currently being emitted, innermost last */
    zan_irgen_catch_cleanup_t *catch_cleanups;
    int catch_cleanup_count;
    int catch_cleanup_cap;
    /* 最内层循环内的 catch 清理项数量 */
    int loop_catch_base;
    /* 当前 try 块内的 catch 清理项数量 */
    int throw_catch_base;

    /* 当前激活的 finally 代码块栈（由内至外排布） */
    zan_irgen_finally_entry_t finallys[ZAN_MAX_FINALLY_DEPTH];
    int finally_count;
    /* finallys entered inside the innermost loop: break/continue run only those */
    int finally_loop_base;
    /* Executing cleanup ancestry survives truncation of the finallys stack. */
    zan_irgen_pending_context_t pending;

    /* 算术溢出检测嵌套深度：大于 0 时在 checked 块内发射溢出检测 */
    int irgen_checked_depth;

    /* try statements whose handler is currently armed, innermost last */
    struct {
        LLVMValueRef old_top_slot; /* i32 alloca: __zan_eh_top at try entry */
    } eh_armed[ZAN_MAX_ARMED_TRY];
    int eh_armed_count;
    /* 当前函数体拥有的作用域清理项 */
    int eh_armed_base;
    /* entries armed inside the innermost loop: break/continue leave only those */
    int eh_armed_loop_base;

    /* 内部辅助实现 */
    LLVMBasicBlockRef wasm_lpad_stack[ZAN_MAX_ARMED_TRY];
    int wasm_try_depth;
    bool in_wasm_throw_op; /* WebAssembly 异常抛出中：禁止 funclet 递归展开 */
    /* cached per-module declarations (irgen_builtins.c) */
    LLVMValueRef wasm_eh_throw_fn;      /* void @__cxa_throw(ptr,ptr,ptr) */
    LLVMValueRef wasm_eh_throw_intrinsic_fn; /* @llvm.wasm.throw(i32, i8*) */
    LLVMValueRef wasm_eh_personality_fn;/* i32 @__gxx_wasm_personality_v0(...) */
    bool wasm_eh_used;                  /* program uses try or throw at all */
    LLVMValueRef wasm_eh_state_fn;      /* 异常状态查询原语：无抛出保证 */

    /* constructors */
    struct zan_ctor_entry {
        zan_symbol_t *type_sym;
        zan_ast_node_t *decl;
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
        int param_count;
    } *ctors;
    int ctor_count;
    int ctor_cap;

    /* 内部辅助实现 */
    zan_type_t *collect_inst_ctx; /* 内部辅助实现 */
    zan_type_t *cur_inst;   /* 当前泛型类单态化实例化上下文 */
    struct zan_generic_fn {
        zan_symbol_t *msym;      /* the (erased) generic method symbol */
        zan_type_t  **args;      /* concrete type args of the instantiation */
        int           argc;
        LLVMValueRef  fn;
        LLVMTypeRef   fn_type;
    } *generic_fns;
    int generic_fn_count;
    int generic_fn_cap;
    struct zan_generic_ctor {
        zan_symbol_t *type_sym;
        zan_ast_node_t *decl;
        zan_type_t  **args;
        int           argc;
        int           param_count;
        LLVMValueRef  fn;
        LLVMTypeRef   fn_type;
    } *generic_ctors;
    int generic_ctor_count;
    int generic_ctor_cap;
    /* distinct concrete instantiations discovered in the unit (worklist seed) */
    struct zan_generic_inst {
        zan_symbol_t *type_sym;  /* the generic class/struct symbol */
        zan_type_t   *inst;      /* instantiation type (sym + concrete type_args) */
    } *generic_insts;
    int generic_inst_count;
    int generic_inst_cap;

    /* 泛型方法单态化特化实例表 */
    struct zan_method_spec {
        zan_symbol_t   *msym;      /* the generic method symbol */
        zan_symbol_t   *type_sym;  /* declaring class */
        zan_type_t     *owner_inst; /* declaring class instantiation, or NULL */
        zan_ast_node_t *member;    /* AST_METHOD_DECL */
        zan_type_t    **bind;      /* concrete type per declared type param */
        int             bindc;
        LLVMValueRef    fn;
        LLVMTypeRef     fn_type;
        /* 异步方法特化元组：ramp、resume 与协程帧类型 */
        bool            is_async;
        void           *async_ir;
    } *method_specs;
    int method_spec_count;
    int method_spec_cap;
    int method_spec_emitted;   /* queue cursor: bodies [0..emitted) are done */
    /* 正在发射的方法特化形参和实参映射 */
    zan_ast_list_t *cur_mtps;
    zan_type_t    **cur_mbind;

    /* ARC runtime functions */
    LLVMValueRef rt_retain;      /* zan_rt_retain(void*) */
    LLVMValueRef rt_release;     /* zan_rt_release(void*) */
    LLVMValueRef rt_release_dyn; /* zan_rt_release_dyn(void*): RTTI dispatch */
    LLVMValueRef rt_alloc;       /* zan_rt_alloc(int64_t size) -> void* */
    LLVMValueRef rt_str_retain;  /* zan_rt_str_retain(void*) */
    LLVMValueRef rt_str_release; /* zan_rt_str_release(void*) */
    LLVMValueRef rt_str_alloc;   /* zan_rt_str_alloc(int64_t size) -> void* */
    LLVMValueRef rt_arr_retain;  /* zan_rt_arr_retain(void*) */
    LLVMValueRef rt_arr_release; /* zan_rt_arr_release(void*) */
    LLVMTypeRef weak_node_type;  /* { next, target, slot } */
    LLVMValueRef weak_buckets;   /* zan_weak_buckets: bucket-array base, calloc on first use */
    LLVMValueRef weak_lock;      /* zan_weak_lock */
    LLVMValueRef weak_count;     /* zan_weak_count */
    LLVMValueRef rt_weak_store;  /* zan_rt_weak_store(void**, void*) */
    LLVMValueRef rt_weak_nil_all; /* zan_rt_weak_nil_all(void*) */
    LLVMValueRef rt_weak_load_retain; /* void* zan_rt_weak_load_retain(void**) */
    LLVMValueRef rt_weak_destroy_begin; /* int1 zan_rt_weak_destroy_begin(void*) */

    /* runtime diagnostics & leak detection */
    LLVMValueRef fn_printf;       /* int printf(const char*, ...) */
    LLVMTypeRef  printf_type;
    LLVMValueRef fn_exit;         /* void exit(int) */
    LLVMTypeRef  exit_type;
    LLVMValueRef fn_atexit;       /* int atexit(void(*)(void)) */
    LLVMTypeRef  atexit_type;
    /* 内部辅助实现 */
    struct zan_static_field_ref {
        zan_type_t   *type;  /* the field's declared (rc-managed) type */
        LLVMValueRef  gv;    /* backing global */
    } *static_fields;
    int static_field_count;
    int static_field_cap;
    LLVMValueRef g_live;          /* i64 global: net live ARC allocations */
    /* 内部辅助实现 */
    LLVMValueRef g_site_live;     /* ptr to [N x i64]: live count per alloc site */
    LLVMValueRef g_site_names;    /* ptr to [N x i8*]: "file:line:col" per site */
    LLVMValueRef g_site_dtors;    /* ptr to [N x i8*]: release fn per alloc site */
    LLVMValueRef g_site_tynames;  /* 祖先类型名称数组指针，供运行时 is/as 检查 */
    LLVMValueRef g_site_meta;     /* 分配点反射类型记录，供 GetType() 获取具体运行期类型 */
    LLVMValueRef g_site_count;    /* i64: number of slots in the site tables */
    zan_symbol_t **site_syms;    /* concrete class symbol per alloc site */
    zan_type_t   **site_inst;    /* 分配点特化类类型：确保泛型析构释放对应字段 */
    int          *site_coll;     /* per site: 0=class, 1=List, 2=StringBuilder */
    zan_type_t   **site_coll_elem; /* per site: List element type (for release) */
    /* 内部辅助实现 */
    uint32_t     *site_loc_file;
    uint32_t     *site_loc_line;
    int          leak_site_count; /* number of distinct `new` sites assigned */
    int          leak_site_cap;   /* capacity of the site_* host-side arrays */
    /* 内部辅助实现 */
    LLVMValueRef *desc_gv;       /* per shape: @__zan_desc_<i> global */
    int          desc_gv_cap;    /* capacity of desc_gv */
    bool         desc_hdr;       /* header word = descriptor pointer mode */
    /* 内部辅助实现 */
    zan_str_intern_t **str_intern; /* chained hash, ZAN_STR_INTERN_BUCKETS */
                                   /* (bucket array calloc'd on first intern) */
    int          str_intern_cap; /* allocated bucket count */
    bool         rt_guard_split;   /* 内部辅助实现 */
    LLVMValueRef fn_report_leaks; /* void __zan_report_leaks(void) */
    const char  *src_file;        /* source path, for runtime diagnostics */
    bool         runtime_checks;  /* insert div-by-zero (etc.) guards; default true */
    LLVMValueRef expect_false_fn; /* cached llvm */
    LLVMValueRef soft_scratch_slot; /* 函数入口轻量刮擦页指针 */
    LLVMValueRef soft_scratch_fn;   /* the function soft_scratch_slot lives in */
    bool         publish_mode;    /* --publish: release build without unused bodies */
    bool         strict_runtime;  /* 内部辅助实现 */
    bool         check_leaks;     /* emit a leak report at program exit */
    bool         arc_guard;       /* 释放对象隔离区 (UAF 检测) */
    bool         arc_net;         /* 内部辅助实现 */
    bool         fast_codegen;    /* machine codegen at -O0 (fast turnaround) */
    bool         emit_lib;        /* 库构建模式：保持 public 成员导出链接 */
    bool         emit_shared;     /* 动态共享库模式：发射平台标准入口 (DllMain) */

    /* Binding<T> 属性访问器函数降解缓存 */
    struct {
        zan_symbol_t *cls;
        zan_symbol_t *field;
        LLVMValueRef get_fn;
        LLVMValueRef set_fn;
    } *bind_accs;
    int bind_acc_count;
    int bind_acc_cap;

    /* built-in List<T> runtime support */
    LLVMValueRef fn_realloc;     /* realloc(void*, size_t) -> void* */
    LLVMTypeRef list_struct_type; /* { i64 count, i64 capacity, i64* data } */
    LLVMTypeRef span_struct_type; /* Span<T> value: { i8* base, i64 len } */
    LLVMTypeRef dict_struct_type; /* { i64 count, i64 capacity, i8** keys, i64* values } */
    LLVMTypeRef sb_struct_type;   /* StringBuilder { i64 count, i64 capacity, i8* data } */
    LLVMTypeRef task_struct_type; /* Task { i64 completed, i64 result, i64 thread_handle } */
    LLVMValueRef fn_strcmp;       /* strcmp(s1, s2) -> int */

    /* string literal cache (dedup same-content globals) */
    struct {
        zan_istr_t text;
        LLVMValueRef value;
    } *string_literals;
    int string_literal_count;
    int string_literal_cap;

    /* 分配点反射类型记录，供 GetType() 获取具体运行期类型 */
    struct {
        zan_symbol_t *sym;      /* declaring symbol; NULL for builtin types */
        const char   *name;     /* display name the record carries */
        LLVMValueRef  rec;      /* i8* to the record's name payload */
    } *refl_metas;
    int refl_meta_count;
    int refl_meta_cap;
    int refl_str_count;           /* names emitted, for unique global names */
    bool refl_used;               /* 标记已使用 typeof/GetType：需生成分配点记录表 */
    LLVMTypeRef  refl_field_type;   /* { i8* name, i8* typeName, i64 kind, i64 off } */
    LLVMValueRef refl_empty_str;    /* "" as an immortal Zan string */
    LLVMValueRef fn_refl_find;      /* i64 (i8* ti, i8* name) */
    LLVMValueRef fn_refl_get_i64;   /* i64 (i8* ti, i8* obj, i8* name) */
    LLVMValueRef fn_refl_get_f64;   /* double (i8* ti, i8* obj, i8* name) */
    LLVMValueRef fn_refl_get_str;   /* i8* (i8* ti, i8* obj, i8* name) */
    LLVMValueRef fn_refl_fname;     /* i8* (i8* ti, i64 idx, i64 which) */
    LLVMValueRef fn_refl_obj_type;  /* i8* (i8* obj, i8* fallback) */
    /* second layer: the method / constructor tables */
    LLVMTypeRef  refl_method_type;
    /* 反射元数据静态记录表 */
    struct {
        LLVMValueRef  gv;        /* [n x method record] global */
        LLVMTypeRef   arr_ty;
        zan_symbol_t *sym;       /* the declaring type */
        int           n;
        bool          ctors;     /* constructor table, not method table */
    } *refl_mtabs;
    int refl_mtab_count;
    int refl_mtab_cap;
    int refl_thunk_count;         /* thunks emitted, for unique names */
    LLVMValueRef fn_refl_mfind;     /* i64 (i8* ti, i8* name, i64 flags) */
    LLVMValueRef fn_refl_mstr;      /* i8* (i8* ti, i64 tbl, i64 i, i64 which, i64 k) */
    LLVMValueRef fn_refl_mi64;      /* i64 (i8* ti, i64 tbl, i64 i, i64 which) */
    LLVMValueRef fn_refl_invoke;    /* i64 (i8* ti, i64 tbl, i64 i, i8* obj,
                                     *      i64* args, i64* ok) */
    LLVMValueRef fn_refl_set;       /* i64 (i8* ti, i8* obj, i8* name, i64 v,
                                     *      i64 vkind) */
    LLVMValueRef fn_refl_pget;      /* i64 (i8* ti, i8* obj, i8* name,
                                     *      i64* kindout) */
    LLVMValueRef fn_refl_cfind;     /* i64 (i8* ti, i64 nargs) */
    LLVMValueRef fn_refl_tainfo;    /* i64 (i8* ti, i64 idx, i64 which) */

    /* --publish string obfuscation */
    bool obfuscate_strings;
    unsigned char obf_key[16];
    /* 字符串混淆动态增长表 */
    struct { LLVMValueRef global; uint32_t len; } *obf_literals;
    int obf_literal_count;
    int obf_literal_cap;

    /* async/await CPS lowering (see docs/ASYNC_CPS_DESIGN.md) */
    LLVMTypeRef  co_step_type;    /* void(i8*) — a frame's resume/step fn */
    LLVMTypeRef  co_step_ptr;     /* void(i8*)* — pointer to a step fn */
    LLVMTypeRef  co_header_type;  /* shared frame header {i64,step*,i32,i32,i8*,step*,i64} */
    LLVMValueRef rt_co_ready;     /* void zan_co_ready(void* frame, step) */
    LLVMTypeRef  rt_co_ready_type;
    /* 内部辅助实现 */
    LLVMValueRef rt_co_poll;
    LLVMTypeRef  rt_co_poll_type;
    LLVMValueRef rt_co_frame_free;/* void __zan_co_frame_free(void* frame) */
    LLVMTypeRef  rt_co_frame_free_type;
    LLVMValueRef rt_co_sched_init;/* void zan_co_sched_init(void) */
    LLVMTypeRef  rt_co_sched_init_type;
    LLVMValueRef rt_co_sched_run; /* void zan_co_sched_run(void) */
    LLVMTypeRef  rt_co_sched_run_type;
    /* 内部辅助实现 */
    LLVMValueRef rt_co_sched_run_until;
    LLVMTypeRef  rt_co_sched_run_until_type;
    LLVMValueRef rt_co_delay;     /* void zan_co_delay(i64 ms, void* frame, step) */
    LLVMTypeRef  rt_co_delay_type;
    /* 套接字就绪反应堆外部驱动绑定 */
    LLVMValueRef rt_io_wait_co;   /* void zan_io_wait_co(iptr fd,i32 interest,i8* frame,step) */
    LLVMTypeRef  rt_io_wait_co_type;
    LLVMValueRef rt_io_recv_co;   /* void zan_io_recv_co(iptr fd,i8* buf,i32 len,i8* frame,step,i64* out_n) */
    LLVMTypeRef  rt_io_recv_co_type;
    LLVMValueRef rt_io_recv_to_co; /* zan_io_recv_to_co 外部符号：超时重叠套接字读取挂起 */
    LLVMTypeRef  rt_io_recv_to_co_type;
    LLVMValueRef rt_io_accept_co; /* void zan_io_accept_co(iptr fd,i8* frame,step,iptr* out_fd) */
    LLVMTypeRef  rt_io_accept_co_type;
    LLVMValueRef rt_io_resolve_co; /* void zan_io_resolve_co(i8* host,i8* frame,step,i32* out) */
    LLVMTypeRef  rt_io_resolve_co_type;
    LLVMValueRef rt_io_resolve_sa_co; /* zan_io_resolve_sa_co 外部符号：异步套接字地址解析挂起 */
    LLVMTypeRef  rt_io_resolve_sa_co_type;
    LLVMValueRef rt_blocking_co;       /* 阻塞调用协程化转派原语 */
    LLVMTypeRef  rt_blocking_co_type;
    LLVMValueRef rt_io_pump_timeout;      /* i32 zan_io_pump_timeout(i64 timeout_ms) */
    LLVMTypeRef  rt_io_pump_timeout_type;
    LLVMValueRef rt_io_has_pending;       /* i32 zan_io_has_pending(void) */
    LLVMTypeRef  rt_io_has_pending_type;
    /* rt_io.o provides socket readiness and generic blocking-await jobs. */
    bool         has_async_work;    /* set when any async method or await is emitted */
    bool         uses_socket_async; /* set when either IO await is lowered */
    bool         uses_timer_runtime; /* set by Timer API externs */
    bool         uses_sync_runtime; /* set by AtomicInt/SharedTable externs */
    bool         uses_file_runtime; /* set by zan_file_* (file IO) externs */
    bool         uses_embed_api;    /* set by zan_embed_* extern references */
    bool         uses_inflate;      /* set by zan_embed_decode/rawlen (compressed payloads) */
    /* 内部辅助实现 */
    zan_goto_label_rec_t *goto_labels;
    int goto_label_count;
    int goto_label_cap;
    /* forward gotos waiting for their label's definition */
    zan_goto_fixup_t *goto_fixups;
    int goto_fixup_count;
    int goto_fixup_cap;
    /* 异常类类型名注册表 */
    struct {
        LLVMValueRef tid;   /* address of the __zan_tid_<Class> global */
        const char     *name;
    } *tid_names;
    int tid_name_count;
    int tid_name_cap;
    /* 异常类描述符全局表及其元素类型 */
    LLVMValueRef tid_name_reg_global;
    LLVMTypeRef  tid_name_reg_ent_ty;
    /* 内部辅助实现 */
    LLVMValueRef current_async_frame;
    LLVMTypeRef  current_async_frame_type;
    LLVMValueRef current_async_resume_fn; /* the $resume fn being emitted */
    /* 正在发射的异步函数体 AST */
    zan_ast_node_t *current_async_body;
    /* 异步方法声明返回类型，用于协程结果槽编解码 */
    zan_type_t  *current_async_ret_type;
    /* await 协程状态机上下文：状态分发 switch、下一状态编号及恢复块 */
    LLVMValueRef current_async_switch;
    int          current_async_next_state;
    int          current_async_sub_base; /* frame index of first sub-task slot */
    int          current_async_sub_next;
    int          current_async_ret_agg_slot; /* frame index of aggregate return slot (-1 if none) */
    zan_async_slot_t *current_async_slots;
    int          current_async_slot_count;
    /* 异步协程完成汇聚块：共享析构局部变量并发布完成状态 */
    int          current_async_frame_local_count;
    LLVMBasicBlockRef current_async_complete_bb;
    LLVMValueRef current_async_result_phi;
    LLVMBasicBlockRef current_async_suspend_ret_bb; /* shared async suspension exit block */
    LLVMBasicBlockRef current_async_requeue_bb; /* shared Task.Yield/preempt ready-and-ret block */
    LLVMBasicBlockRef current_async_cancel_bb;  /* shared top-level cancel exit block */
    LLVMBasicBlockRef current_async_rethrow_bb; /* shared sub-task exception rethrow block */
    LLVMBasicBlockRef current_async_sub_rethrow_bb; /* shared sub-task exception transfer block */
    LLVMValueRef      current_async_sub_rethrow_phi_sub; /* PHI collecting threw sub-frame */
    LLVMValueRef      current_async_sub_rethrow_phi_ev;  /* PHI collecting threw exception ptr */
    LLVMValueRef current_async_state_ptr;       /* cached &frame->state GEP */
    LLVMValueRef current_async_cancel_ptr;      /* cached &frame->cancel GEP */
    LLVMValueRef current_async_self_i8;         /* cached (i8*)frame bitcast */
    LLVMValueRef current_async_self_int;        /* cached (uintptr_t)(i8*)frame */
    LLVMValueRef current_async_child_ptr;       /* cached &frame->child GEP */
    LLVMValueRef current_async_sub_slot_ptr;    /* cached &frame->sub_slot GEP */
    LLVMValueRef current_async_result_ptr;      /* cached &frame->result GEP */
    /* Persistent per-function IR compaction state (owned by irgen.c). */
    void        *function_compactor;
    /* 内部辅助实现 */
    LLVMValueRef current_async_eh_entry;
    LLVMBasicBlockRef current_async_exc_bb;
    LLVMValueRef current_async_rearm_switch;
    /* 异常处理器重新布防块：恢复调用帧异常簿记 */
    LLVMValueRef current_async_rearm_init_switch;
    LLVMBasicBlockRef current_async_rearm_next_bb;
    int          current_async_handler_next;
    /* 协程帧内异常处理器槽位数 */
    int          current_async_handler_cap;
    int          current_async_try_count;    /* 0 if the async body has no lexical try statements */
    /* 协程内 foreach 编号，索引常驻帧槽 */
    int          current_async_foreach_next;
    /* 协程帧持有的接收者强引用（析构时释放） */
    int          current_async_this_owned;
    /* the receiver type that +1 belongs to */
    zan_type_t  *current_async_this_type;

    /* DllImport: tracked extern libraries for linker */
    zan_istr_t *extern_libs;
    int extern_lib_count;
    int extern_lib_cap;
    /* DllImport 外部库符号列表（用于跨平台链接检测） */
    struct {
        zan_istr_t lib;
        zan_istr_t name; /* 外部符号名称 */
    } *extern_fns;
    int extern_fn_count;
    int extern_fn_cap;
    /* FFI on targets with no aggregate C ABI classification (wasm32 etc */
    char **abi_pending;
    int abi_pending_count;
    int abi_pending_cap;

    /* Per-thread exception-handling state (see irgen_builtins */
    LLVMTypeRef  eh_state_ty;
    LLVMValueRef eh_state_owner;   /* function the cache below belongs to */
    LLVMValueRef eh_state_cached;
    LLVMValueRef eh_state_fields[8];

    /* cross-compilation target */
    char target_triple[128];
    bool target_is_windows;   /* true when emitting for Windows (Sleep vs poll) */
    bool target_is_macos;     /* Darwin 平台 stdio 流导出符号适配 */
    bool target_is_wasm;      /* WebAssembly 原生异常处理模式标记 */
    bool external_async_executor; /* target/runtime capability: omit the inline
                                   * coroutine driver and link the external
                                   * executor object instead. */
    bool mf_native;           /* 本机宿主平台构建标记 (x64/arm64) */

    /* Cached results of prefix queries (e.g. WebView, CEF, icons, etc.) */
    char prefix_cache[32][64];
    bool prefix_cache_val[32];
    int  prefix_cache_count;

    /* DWARF 调试信息构建器状态 (-g 选项激活) */
    bool             emit_debug;
    LLVMDIBuilderRef di_builder;
    LLVMMetadataRef  di_cu;
    LLVMMetadataRef *di_files;      /* DIFile per source file_id */
    int              di_file_cap;
    uint32_t         di_cur_line;   /* source line of the statement in progress */
    uint32_t         di_cur_file;   /* its file_id (for local-variable declares) */

    /* ARC 语句控制流作用域嵌套深度 */
    int arc_stmt_depth;

    /* 内部辅助实现 */
    struct zan_body_write_entry {
        zan_ast_node_t *body;
        zan_istr_t      name;
        unsigned char   written;
        unsigned char   lam_written;
        unsigned char   lam_captured; /* name-only candidate; scope checked before boxing */
        unsigned char   known;
    } *body_write_memo;
    unsigned body_write_memo_cap;   /* power of two, or 0 = not built */
    unsigned body_write_memo_count;
    zan_ast_node_t *body_write_scan_done; /* body the full scan last covered */

    /* 流式分片状态：发射后即时回收函数体至文本缓冲区以控制内存峰值 */
    bool enable_streaming_shard;
    int  streaming_shard_count;
    int  streaming_shard_cap;
    struct zan_shard_buf {
        char         *text;
        size_t        len;
        size_t        cap;
        int           fn_count;
        LLVMValueRef *fns;
        int           fns_cap;
    } *streaming_shards;
    int streaming_shard_cur_fns;
};

void zan_irgen_shard_buf_append(zan_irgen_t *g, LLVMValueRef fn, const char *txt);
void zan_irgen_shard_buf_free(zan_irgen_t *g);
bool zan_irgen_shard_harvest_fn(zan_irgen_t *g, LLVMValueRef fn);
void zan_irgen_shard_harvest_stats(void);

/* 内部辅助实现 */
static inline void zan_set_module_local(LLVMValueRef fn) {
    if (fn) LLVMSetLinkage(fn, LLVMInternalLinkage);
}

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name,
                            const char *target_triple,
                            bool target_is_windows, bool external_async_executor,
                            bool check_leaks, bool runtime_checks,
                            bool arc_guard, bool arc_net);
void zan_irgen_destroy(zan_irgen_t *g);
void zan_irgen_release_llvm(zan_irgen_t *g);

/* 编译器生成的保护文本驻留符号 */
LLVMValueRef zan_irgen_intern_string(zan_irgen_t *g, const char *text);

/* 内部辅助实现 */
void zan_irgen_emit_oom_check(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef raw);

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit);
/* 模块初始化解密构造函数 (.ctors) 发射 */
void zan_irgen_emit_string_deobf(zan_irgen_t *g);
zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path);
zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path);

/* 内部辅助实现 */
typedef struct zan_mf_fn {
    const char *name;      /* module-owned LLVM name (alive while g lives) */
    unsigned    blocks, insns;
    unsigned char defined, internal_linkage, varargs;
    unsigned char indirect_call, addr_taken;
    /* registry facts (kind == user, defined) */
    unsigned char is_async, is_spec, virtual_dispatch, simple_abi;
    unsigned char eligible, clean_root;
    unsigned char kind;    /* ZAN_MF_* from irgen_manifest.c */
    int reg_idx;           /* index into zan_irgen.functions, or -1 */
    /* 直接调用边图：函数清单索引及引用的全局符号 */
    int        *calls;     int call_cnt, call_cap;
    const char **exts;     int ext_cnt,  ext_cap;
    const char **globs;    int glob_cnt, glob_cap;
} zan_mf_fn;

typedef struct zan_cg_manifest {
    zan_mf_fn *fns;
    int fn_count;
    unsigned long long total_insns;
    int defined_count, extern_count, global_count;
} zan_cg_manifest_t;

/* 目标是否为原生宿主架构 (x64/arm64) */
void zan_irgen_manifest_build(zan_irgen_t *g, zan_cg_manifest_t *m,
                              bool native);
void zan_irgen_manifest_report(zan_irgen_t *g, const zan_cg_manifest_t *m);
int  zan_irgen_manifest_write_json(zan_irgen_t *g, zan_cg_manifest_t *m,
                                   const char *path);
void zan_irgen_manifest_free(zan_cg_manifest_t *m);

/* 内部辅助实现 */
int zan_irgen_shard_run(zan_irgen_t *g, const zan_cg_manifest_t *m,
                        const char *obj_base, char ***out_objs);

/* Binds the target triple + data layout to the module early */
void zan_irgen_bind_target(zan_irgen_t *g);

/* DllImport 外部库符号列表（用于跨平台链接检测） */
int zan_irgen_stub_extern_lib(zan_irgen_t *g, const char *lib, int lib_len);

/* 从外部依赖库列表中移除指定库名，消除 -l 参数 */
int zan_irgen_drop_extern_lib(zan_irgen_t *g, const char *lib, int lib_len);

/* 内部辅助实现 */
int zan_irgen_prune_extern_libs(zan_irgen_t *g);

/* 检查模块中是否存在以指定前缀开头的方法实现 */
bool zan_irgen_defines_prefix(zan_irgen_t *g, const char *prefix);

#endif /* ZAN_IRGEN_H */
