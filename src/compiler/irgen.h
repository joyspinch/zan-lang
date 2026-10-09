/* 底层系统交互与数据协议契约 */

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

/* 底层系统交互与数据协议契约 */
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

/* 编译器代码生成与运行时系统底层调用契约 */
#define ZAN_MAX_FINALLY_DEPTH 256

/* 核心系统底层抽象与内存语义契约 */
#define ZAN_MAX_ARMED_TRY 1024

typedef struct zan_irgen_pending_scope {
    struct zan_irgen_pending_scope *parent;
    int handler_id; /* 底层系统交互与数据协议契约 */
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
    int               defined;       /* 核心系统底层抽象与内存语义契约 */
    int               fin_depth;     /* 核心系统底层抽象与内存语义契约 */
    int               eh_armed_base; /* 核心系统底层抽象与内存语义契约 */
    int               catch_base;    /* 核心系统底层抽象与内存语义契约 */
    int               locals_base;   /* 核心系统底层抽象与内存语义契约 */
    int               locals_owned;  /* 核心系统底层抽象与内存语义契约 */
    zan_irgen_pending_scope_t *pending_scope;
    zan_irgen_pending_scope_t *label_owner;
} zan_goto_label_rec_t;

/* catch 块异常清理上下文项：记录异常变量局部索引以供作用域展开 */
typedef struct zan_irgen_catch_cleanup {
    LLVMValueRef exc_slot;   /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef owned_slot; /* 底层系统交互与数据协议契约 */
    LLVMValueRef tid_slot;   /* 当前捕获异常类型描述符，供无参 throw; 原样重抛 */
} zan_irgen_catch_cleanup_t;

/* 底层系统交互与数据协议契约 */
typedef struct zan_irgen_finally_entry {
    zan_ast_node_t *body;   /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef monitor_obj; /* lock (obj) 锁对象槽位，退出时释放监视器 */
    LLVMValueRef continuation_slot; /* 核心系统底层抽象与内存语义契约 */
    struct zan_irgen_finally_shared *shared; /* 核心系统底层抽象与内存语义契约 */
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
    LLVMBasicBlockRef from_bb;  /* 底层系统交互与数据协议契约 */
    /* 核心系统底层抽象与内存语义契约 */
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

    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef current_fn;
    LLVMTypeRef current_fn_ret_type;
    zan_type_t *current_fn_zan_ret_type; /* 核心系统底层抽象与内存语义契约 */

    /* 模块核心语义抽象与接口调用契约 */
    int qdot_counter;

    /* 底层系统交互与数据协议契约 */
    LLVMValueRef current_this;       /* 核心系统底层抽象与内存语义契约 */
    zan_symbol_t *current_type_sym;  /* 核心系统底层抽象与内存语义契约 */
    zan_ast_node_t *current_fn_body; /* 底层系统交互与数据协议契约 */
    bool current_fn_is_ctor;         /* 核心系统底层抽象与内存语义契约 */
    bool current_fn_is_main;         /* 程序主入口函数：退出前释放静态对象 */
    bool current_fn_no_runtime;      /* 核心系统底层抽象与内存语义契约 */
    /* 处于 Lambda 函数体内部：此时 current_this 为 NULL */
    int lambda_depth;

    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef rt_println;   /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef rt_print_int; /* zan_rt_print_int(int64) */
    LLVMValueRef rt_print_uint; /* zan_rt_print_uint(uint64) */
    LLVMValueRef rt_print_double; /* zan_rt_print_double(double) */

    /* 核心系统底层抽象与内存语义契约 */
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
        zan_type_t   *inst;  /* 底层系统交互与数据协议契约 */
        LLVMValueRef  fn;
    } *class_release;
    int class_release_count;
    int class_release_cap;

    /* 核心系统底层抽象与内存语义契约 */
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

    /* 核心系统底层抽象与内存语义契约 */
    LLVMBasicBlockRef break_target;
    LLVMBasicBlockRef continue_target;
    /* 最内层循环首个局部变量索引：break/continue 据此释放局部变量 */
    int loop_locals_base;
    /* 内部辅助实现 */
    int throw_locals_base;
    /* 底层系统交互与数据协议契约 */
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
    /* 编译器代码生成与运行时系统底层调用契约 */
    int finally_loop_base;
    /* 模块核心语义抽象与接口调用契约 */
    zan_irgen_pending_context_t pending;

    /* 算术溢出检测嵌套深度：大于 0 时在 checked 块内发射溢出检测 */
    int irgen_checked_depth;

    /* 模块核心语义抽象与接口调用契约 */
    struct {
        LLVMValueRef old_top_slot; /* 核心系统底层抽象与内存语义契约 */
    } eh_armed[ZAN_MAX_ARMED_TRY];
    int eh_armed_count;
    /* 当前函数体拥有的作用域清理项 */
    int eh_armed_base;
    /* 编译器代码生成与运行时系统底层调用契约 */
    int eh_armed_loop_base;

    /* 内部辅助实现 */
    LLVMBasicBlockRef wasm_lpad_stack[ZAN_MAX_ARMED_TRY];
    int wasm_try_depth;
    bool in_wasm_throw_op; /* WebAssembly 异常抛出中：禁止 funclet 递归展开 */
    /* 底层系统交互与数据协议契约 */
    LLVMValueRef wasm_eh_throw_fn;      /* 底层系统交互与数据协议契约 */
    LLVMValueRef wasm_eh_throw_intrinsic_fn; /* @llvm.wasm.throw(i32, i8*) */
    LLVMValueRef wasm_eh_personality_fn;/* i32 @__gxx_wasm_personality_v0(...) */
    bool wasm_eh_used;                  /* 核心系统底层抽象与内存语义契约 */
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
        zan_symbol_t *msym;      /* 核心系统底层抽象与内存语义契约 */
        zan_type_t  **args;      /* 核心系统底层抽象与内存语义契约 */
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
    /* 模块核心语义抽象与接口调用契约 */
    struct zan_generic_inst {
        zan_symbol_t *type_sym;  /* 核心系统底层抽象与内存语义契约 */
        zan_type_t   *inst;      /* 底层系统交互与数据协议契约 */
    } *generic_insts;
    int generic_inst_count;
    int generic_inst_cap;

    /* 泛型方法单态化特化实例表 */
    struct zan_method_spec {
        zan_symbol_t   *msym;      /* 核心系统底层抽象与内存语义契约 */
        zan_symbol_t   *type_sym;  /* 核心系统底层抽象与内存语义契约 */
        zan_type_t     *owner_inst; /* 核心系统底层抽象与内存语义契约 */
        zan_ast_node_t *member;    /* AST_METHOD_DECL */
        zan_type_t    **bind;      /* 底层系统交互与数据协议契约 */
        int             bindc;
        LLVMValueRef    fn;
        LLVMTypeRef     fn_type;
        /* 异步方法特化元组：ramp、resume 与协程帧类型 */
        bool            is_async;
        void           *async_ir;
    } *method_specs;
    int method_spec_count;
    int method_spec_cap;
    int method_spec_emitted;   /* 底层系统交互与数据协议契约 */
    /* 正在发射的方法特化形参和实参映射 */
    zan_ast_list_t *cur_mtps;
    zan_type_t    **cur_mbind;

    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef rt_retain;      /* zan_rt_retain(void*) */
    LLVMValueRef rt_release;     /* zan_rt_release(void*) */
    LLVMValueRef rt_release_dyn; /* 底层系统交互与数据协议契约 */
    LLVMValueRef rt_alloc;       /* zan_rt_alloc(int64_t size) -> void* */
    LLVMValueRef rt_str_retain;  /* zan_rt_str_retain(void*) */
    LLVMValueRef rt_str_release; /* zan_rt_str_release(void*) */
    LLVMValueRef rt_str_alloc;   /* 底层系统交互与数据协议契约 */
    LLVMValueRef rt_arr_retain;  /* zan_rt_arr_retain(void*) */
    LLVMValueRef rt_arr_release; /* zan_rt_arr_release(void*) */
    LLVMTypeRef weak_node_type;  /* { next, target, slot } */
    LLVMValueRef weak_buckets;   /* 模块核心语义抽象与接口调用契约 */
    LLVMValueRef weak_lock;      /* zan_weak_lock */
    LLVMValueRef weak_count;     /* zan_weak_count */
    LLVMValueRef rt_weak_store;  /* zan_rt_weak_store(void**, void*) */
    LLVMValueRef rt_weak_nil_all; /* zan_rt_weak_nil_all(void*) */
    LLVMValueRef rt_weak_load_retain; /* 底层系统交互与数据协议契约 */
    LLVMValueRef rt_weak_destroy_begin; /* 底层系统交互与数据协议契约 */

    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef fn_printf;       /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef  printf_type;
    LLVMValueRef fn_exit;         /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef  exit_type;
    LLVMValueRef fn_atexit;       /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef  atexit_type;
    /* 内部辅助实现 */
    struct zan_static_field_ref {
        zan_type_t   *type;  /* 核心系统底层抽象与内存语义契约 */
        LLVMValueRef  gv;    /* 核心系统底层抽象与内存语义契约 */
    } *static_fields;
    int static_field_count;
    int static_field_cap;
    LLVMValueRef g_live;          /* 核心系统底层抽象与内存语义契约 */
    /* 内部辅助实现 */
    LLVMValueRef g_site_live;     /* 底层系统交互与数据协议契约 */
    LLVMValueRef g_site_names;    /* 底层系统交互与数据协议契约 */
    LLVMValueRef g_site_dtors;    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef g_site_tynames;  /* 祖先类型名称数组指针，供运行时 is/as 检查 */
    LLVMValueRef g_site_meta;     /* 分配点反射类型记录，供 GetType() 获取具体运行期类型 */
    LLVMValueRef g_site_count;    /* 核心系统底层抽象与内存语义契约 */
    zan_symbol_t **site_syms;    /* 底层系统交互与数据协议契约 */
    zan_type_t   **site_inst;    /* 分配点特化类类型：确保泛型析构释放对应字段 */
    int          *site_coll;     /* 核心系统底层抽象与内存语义契约 */
    zan_type_t   **site_coll_elem; /* 底层系统交互与数据协议契约 */
    /* 内部辅助实现 */
    uint32_t     *site_loc_file;
    uint32_t     *site_loc_line;
    int          leak_site_count; /* 核心系统底层抽象与内存语义契约 */
    int          leak_site_cap;   /* 底层系统交互与数据协议契约 */
    /* 内部辅助实现 */
    LLVMValueRef *desc_gv;       /* 核心系统底层抽象与内存语义契约 */
    int          desc_gv_cap;    /* capacity of desc_gv */
    bool         desc_hdr;       /* 核心系统底层抽象与内存语义契约 */
    /* 内部辅助实现 */
    zan_str_intern_t **str_intern; /* 底层系统交互与数据协议契约 */
                                   /* 核心系统底层抽象与内存语义契约 */
    int          str_intern_cap; /* 核心系统底层抽象与内存语义契约 */
    bool         rt_guard_split;   /* 内部辅助实现 */
    LLVMValueRef fn_report_leaks; /* void __zan_report_leaks(void) */
    const char  *src_file;        /* 核心系统底层抽象与内存语义契约 */
    bool         runtime_checks;  /* 底层系统交互与数据协议契约 */
    LLVMValueRef expect_false_fn; /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef soft_scratch_slot; /* 函数入口轻量刮擦页指针 */
    LLVMValueRef soft_scratch_fn;   /* 底层系统交互与数据协议契约 */
    bool         publish_mode;    /* 底层系统交互与数据协议契约 */
    bool         strict_runtime;  /* 内部辅助实现 */
    bool         check_leaks;     /* 核心系统底层抽象与内存语义契约 */
    bool         arc_guard;       /* 释放对象隔离区 (UAF 检测) */
    bool         arc_net;         /* 内部辅助实现 */
    bool         fast_codegen;    /* 核心系统底层抽象与内存语义契约 */
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

    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef fn_realloc;     /* realloc(void*, size_t) -> void* */
    LLVMTypeRef list_struct_type; /* { i64 count, i64 capacity, i64* data } */
    LLVMTypeRef span_struct_type; /* Span<T> value: { i8* base, i64 len } */
    LLVMTypeRef dict_struct_type; /* { i64 count, i64 capacity, i8** keys, i64* values } */
    LLVMTypeRef sb_struct_type;   /* StringBuilder { i64 count, i64 capacity, i8* data } */
    LLVMTypeRef task_struct_type; /* Task { i64 completed, i64 result, i64 thread_handle } */
    LLVMValueRef fn_strcmp;       /* strcmp(s1, s2) -> int */

    /* 底层系统交互与数据协议契约 */
    struct {
        zan_istr_t text;
        LLVMValueRef value;
    } *string_literals;
    int string_literal_count;
    int string_literal_cap;

    /* 分配点反射类型记录，供 GetType() 获取具体运行期类型 */
    struct {
        zan_symbol_t *sym;      /* 底层系统交互与数据协议契约 */
        const char   *name;     /* 核心系统底层抽象与内存语义契约 */
        LLVMValueRef  rec;      /* 核心系统底层抽象与内存语义契约 */
    } *refl_metas;
    int refl_meta_count;
    int refl_meta_cap;
    int refl_str_count;           /* 底层系统交互与数据协议契约 */
    bool refl_used;               /* 标记已使用 typeof/GetType：需生成分配点记录表 */
    LLVMTypeRef  refl_field_type;   /* { i8* name, i8* typeName, i64 kind, i64 off } */
    LLVMValueRef refl_empty_str;    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef fn_refl_find;      /* i64 (i8* ti, i8* name) */
    LLVMValueRef fn_refl_get_i64;   /* i64 (i8* ti, i8* obj, i8* name) */
    LLVMValueRef fn_refl_get_f64;   /* double (i8* ti, i8* obj, i8* name) */
    LLVMValueRef fn_refl_get_str;   /* i8* (i8* ti, i8* obj, i8* name) */
    LLVMValueRef fn_refl_fname;     /* i8* (i8* ti, i64 idx, i64 which) */
    LLVMValueRef fn_refl_obj_type;  /* i8* (i8* obj, i8* fallback) */
    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef  refl_method_type;
    /* 反射元数据静态记录表 */
    struct {
        LLVMValueRef  gv;        /* 核心系统底层抽象与内存语义契约 */
        LLVMTypeRef   arr_ty;
        zan_symbol_t *sym;       /* 核心系统底层抽象与内存语义契约 */
        int           n;
        bool          ctors;     /* 核心系统底层抽象与内存语义契约 */
    } *refl_mtabs;
    int refl_mtab_count;
    int refl_mtab_cap;
    int refl_thunk_count;         /* 核心系统底层抽象与内存语义契约 */
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

    /* 核心系统底层抽象与内存语义契约 */
    bool obfuscate_strings;
    unsigned char obf_key[16];
    /* 字符串混淆动态增长表 */
    struct { LLVMValueRef global; uint32_t len; } *obf_literals;
    int obf_literal_count;
    int obf_literal_cap;

    /* 模块核心语义抽象与接口调用契约 */
    LLVMTypeRef  co_step_type;    /* void(i8*) — a frame's resume/step fn */
    LLVMTypeRef  co_step_ptr;     /* void(i8*)* — pointer to a step fn */
    LLVMTypeRef  co_header_type;  /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef rt_co_ready;     /* 底层系统交互与数据协议契约 */
    LLVMTypeRef  rt_co_ready_type;
    /* 内部辅助实现 */
    LLVMValueRef rt_co_poll;
    LLVMTypeRef  rt_co_poll_type;
    LLVMValueRef rt_co_frame_free;/* 底层系统交互与数据协议契约 */
    LLVMTypeRef  rt_co_frame_free_type;
    LLVMValueRef rt_co_sched_init;/* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef  rt_co_sched_init_type;
    LLVMValueRef rt_co_sched_run; /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef  rt_co_sched_run_type;
    /* 内部辅助实现 */
    LLVMValueRef rt_co_sched_run_until;
    LLVMTypeRef  rt_co_sched_run_until_type;
    LLVMValueRef rt_co_delay;     /* 底层系统交互与数据协议契约 */
    LLVMTypeRef  rt_co_delay_type;
    /* 套接字就绪反应堆外部驱动绑定 */
    LLVMValueRef rt_io_wait_co;   /* 底层系统交互与数据协议契约 */
    LLVMTypeRef  rt_io_wait_co_type;
    LLVMValueRef rt_io_recv_co;   /* 模块核心语义抽象与接口调用契约 */
    LLVMTypeRef  rt_io_recv_co_type;
    LLVMValueRef rt_io_recv_to_co; /* zan_io_recv_to_co 外部符号：超时重叠套接字读取挂起 */
    LLVMTypeRef  rt_io_recv_to_co_type;
    LLVMValueRef rt_io_accept_co; /* 模块核心语义抽象与接口调用契约 */
    LLVMTypeRef  rt_io_accept_co_type;
    LLVMValueRef rt_io_resolve_co; /* 底层系统交互与数据协议契约 */
    LLVMTypeRef  rt_io_resolve_co_type;
    LLVMValueRef rt_io_resolve_sa_co; /* zan_io_resolve_sa_co 外部符号：异步套接字地址解析挂起 */
    LLVMTypeRef  rt_io_resolve_sa_co_type;
    LLVMValueRef rt_blocking_co;       /* 阻塞调用协程化转派原语 */
    LLVMTypeRef  rt_blocking_co_type;
    LLVMValueRef rt_io_pump_timeout;      /* i32 zan_io_pump_timeout(i64 timeout_ms) */
    LLVMTypeRef  rt_io_pump_timeout_type;
    LLVMValueRef rt_io_has_pending;       /* i32 zan_io_has_pending(void) */
    LLVMTypeRef  rt_io_has_pending_type;
    /* 模块核心语义抽象与接口调用契约 */
    bool         has_async_work;    /* 底层系统交互与数据协议契约 */
    bool         uses_socket_async; /* 核心系统底层抽象与内存语义契约 */
    bool         uses_timer_runtime; /* 核心系统底层抽象与内存语义契约 */
    bool         uses_sync_runtime; /* 核心系统底层抽象与内存语义契约 */
    bool         uses_file_runtime; /* set by zan_file_* (file IO) externs */
    bool         uses_embed_api;    /* 核心系统底层抽象与内存语义契约 */
    bool         uses_inflate;      /* 底层系统交互与数据协议契约 */
    /* 内部辅助实现 */
    zan_goto_label_rec_t *goto_labels;
    int goto_label_count;
    int goto_label_cap;
    /* 底层系统交互与数据协议契约 */
    zan_goto_fixup_t *goto_fixups;
    int goto_fixup_count;
    int goto_fixup_cap;
    /* 异常类类型名注册表 */
    struct {
        LLVMValueRef tid;   /* 底层系统交互与数据协议契约 */
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
    LLVMValueRef current_async_resume_fn; /* 核心系统底层抽象与内存语义契约 */
    /* 正在发射的异步函数体 AST */
    zan_ast_node_t *current_async_body;
    /* 异步方法声明返回类型，用于协程结果槽编解码 */
    zan_type_t  *current_async_ret_type;
    /* await 协程状态机上下文：状态分发 switch、下一状态编号及恢复块 */
    LLVMValueRef current_async_switch;
    int          current_async_next_state;
    int          current_async_sub_base; /* 底层系统交互与数据协议契约 */
    int          current_async_sub_next;
    int          current_async_ret_agg_slot; /* 底层系统交互与数据协议契约 */
    zan_async_slot_t *current_async_slots;
    int          current_async_slot_count;
    /* 异步协程完成汇聚块：共享析构局部变量并发布完成状态 */
    int          current_async_frame_local_count;
    LLVMBasicBlockRef current_async_complete_bb;
    LLVMValueRef current_async_result_phi;
    LLVMBasicBlockRef current_async_suspend_ret_bb; /* 核心系统底层抽象与内存语义契约 */
    LLVMBasicBlockRef current_async_requeue_bb; /* 模块核心语义抽象与接口调用契约 */
    LLVMBasicBlockRef current_async_cancel_bb;  /* 底层系统交互与数据协议契约 */
    LLVMBasicBlockRef current_async_rethrow_bb; /* 底层系统交互与数据协议契约 */
    LLVMBasicBlockRef current_async_sub_rethrow_bb; /* 底层系统交互与数据协议契约 */
    LLVMValueRef      current_async_sub_rethrow_phi_sub; /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef      current_async_sub_rethrow_phi_ev;  /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef current_async_state_ptr;       /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef current_async_cancel_ptr;      /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef current_async_self_i8;         /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef current_async_self_int;        /* cached (uintptr_t)(i8*)frame */
    LLVMValueRef current_async_child_ptr;       /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef current_async_sub_slot_ptr;    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef current_async_result_ptr;      /* 核心系统底层抽象与内存语义契约 */
    /* 底层系统交互与数据协议契约 */
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
    int          current_async_try_count;    /* 底层系统交互与数据协议契约 */
    /* 协程内 foreach 编号，索引常驻帧槽 */
    int          current_async_foreach_next;
    /* 协程帧持有的接收者强引用（析构时释放） */
    int          current_async_this_owned;
    /* 核心系统底层抽象与内存语义契约 */
    zan_type_t  *current_async_this_type;

    /* 底层系统交互与数据协议契约 */
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
    /* 模块核心语义抽象与接口调用契约 */
    char **abi_pending;
    int abi_pending_count;
    int abi_pending_cap;

    /* 模块核心语义抽象与接口调用契约 */
    LLVMTypeRef  eh_state_ty;
    LLVMValueRef eh_state_owner;   /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef eh_state_cached;
    LLVMValueRef eh_state_fields[8];

    /* 核心系统底层抽象与内存语义契约 */
    char target_triple[128];
    bool target_is_windows;   /* 底层系统交互与数据协议契约 */
    bool target_is_macos;     /* Darwin 平台 stdio 流导出符号适配 */
    bool target_is_wasm;      /* WebAssembly 原生异常处理模式标记 */
    bool external_async_executor; /* 编译器代码生成与运行时系统底层调用契约 */
    bool mf_native;           /* 本机宿主平台构建标记 (x64/arm64) */

    /* 模块核心语义抽象与接口调用契约 */
    char prefix_cache[32][64];
    bool prefix_cache_val[32];
    int  prefix_cache_count;

    /* DWARF 调试信息构建器状态 (-g 选项激活) */
    bool             emit_debug;
    LLVMDIBuilderRef di_builder;
    LLVMMetadataRef  di_cu;
    LLVMMetadataRef *di_files;      /* 核心系统底层抽象与内存语义契约 */
    int              di_file_cap;
    uint32_t         di_cur_line;   /* 核心系统底层抽象与内存语义契约 */
    uint32_t         di_cur_file;   /* 底层系统交互与数据协议契约 */

    /* ARC 语句控制流作用域嵌套深度 */
    int arc_stmt_depth;

    /* 内部辅助实现 */
    struct zan_body_write_entry {
        zan_ast_node_t *body;
        zan_istr_t      name;
        unsigned char   written;
        unsigned char   lam_written;
        unsigned char   lam_captured; /* 底层系统交互与数据协议契约 */
        unsigned char   known;
    } *body_write_memo;
    unsigned body_write_memo_cap;   /* 核心系统底层抽象与内存语义契约 */
    unsigned body_write_memo_count;
    zan_ast_node_t *body_write_scan_done; /* 底层系统交互与数据协议契约 */

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
    const char *name;      /* 底层系统交互与数据协议契约 */
    unsigned    blocks, insns;
    unsigned char defined, internal_linkage, varargs;
    unsigned char indirect_call, addr_taken;
    /* 核心系统底层抽象与内存语义契约 */
    unsigned char is_async, is_spec, virtual_dispatch, simple_abi;
    unsigned char eligible, clean_root;
    unsigned char kind;    /* 核心系统底层抽象与内存语义契约 */
    int reg_idx;           /* 核心系统底层抽象与内存语义契约 */
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

/* 模块核心语义抽象与接口调用契约 */
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
