/* irgen.h -- LLVM IR generation for the Zan language. */

#ifndef ZAN_IRGEN_H
#define ZAN_IRGEN_H

#include "zan.h"
#include "ast.h"
#include "binder.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

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

    /* current 'this' context for method bodies */
    LLVMValueRef current_this;       /* alloca for 'this' pointer */
    zan_symbol_t *current_type_sym;  /* type symbol for 'this' */

    /* runtime function declarations */
    LLVMValueRef rt_println;   /* zan_rt_println(const char*) */
    LLVMValueRef rt_print_int; /* zan_rt_print_int(int64) */
    LLVMValueRef rt_print_double; /* zan_rt_print_double(double) */

    /* C library functions for string interpolation */
    LLVMValueRef fn_snprintf;
    LLVMValueRef fn_malloc;
    LLVMValueRef fn_free;
    LLVMValueRef fn_strlen;
    LLVMValueRef fn_strcpy;
    LLVMValueRef fn_strcat;

    /* struct type registry */
    struct {
        zan_symbol_t *sym;
        LLVMTypeRef llvm_type;
    } struct_types[256];
    int struct_type_count;

    /* user-defined functions */
    struct {
        zan_symbol_t *sym;
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
    } functions[1024];
    int function_count;

    /* break/continue targets */
    LLVMBasicBlockRef break_target;
    LLVMBasicBlockRef continue_target;

    /* constructors */
    struct {
        zan_symbol_t *type_sym;
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
        int param_count;
    } ctors[256];
    int ctor_count;

    /* ARC runtime functions */
    LLVMValueRef rt_retain;      /* zan_rt_retain(void*) */
    LLVMValueRef rt_release;     /* zan_rt_release(void*) */
    LLVMValueRef rt_alloc;       /* zan_rt_alloc(int64_t size) -> void* */

    /* runtime diagnostics & leak detection */
    LLVMValueRef fn_printf;       /* int printf(const char*, ...) */
    LLVMTypeRef  printf_type;
    LLVMValueRef fn_exit;         /* void exit(int) */
    LLVMTypeRef  exit_type;
    LLVMValueRef fn_atexit;       /* int atexit(void(*)(void)) */
    LLVMTypeRef  atexit_type;
    LLVMValueRef g_live;          /* i64 global: net live ARC allocations */
    LLVMValueRef g_site_live;     /* [N x i64] global: live count per alloc site */
    LLVMValueRef g_site_names;    /* [N x i8*] global: "file:line:col" per site */
    LLVMTypeRef  site_live_type;  /* [N x i64] array type */
    LLVMTypeRef  site_names_type; /* [N x i8*] array type */
    int          leak_site_count; /* number of distinct `new` sites assigned */
    LLVMValueRef fn_report_leaks; /* void __zan_report_leaks(void) */
    const char  *src_file;        /* source path, for runtime diagnostics */
    bool         runtime_checks;  /* insert div-by-zero (etc.) guards; default true */
    bool         check_leaks;     /* emit a leak report at program exit */

    /* vtable registry (for virtual dispatch) */
    struct {
        zan_symbol_t *type_sym;
        LLVMValueRef vtable_global;  /* global constant array */
        int method_count;
    } vtables[256];
    int vtable_count;

    /* built-in List<T> runtime support */
    LLVMValueRef fn_realloc;     /* realloc(void*, size_t) -> void* */
    LLVMTypeRef list_struct_type; /* { i64 count, i64 capacity, i64* data } */
    LLVMTypeRef dict_struct_type; /* { i64 count, i64 capacity, i8** keys, i64* values } */
    LLVMValueRef fn_strcmp;       /* strcmp(s1, s2) -> int */

    /* DllImport: tracked extern libraries for linker */
    zan_istr_t extern_libs[64];
    int extern_lib_count;
};

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name);
void zan_irgen_destroy(zan_irgen_t *g);

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit);
zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path);
zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path);

#endif /* ZAN_IRGEN_H */
