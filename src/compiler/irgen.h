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

    /* constructors */
    struct {
        zan_symbol_t *type_sym;
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
        int param_count;
    } ctors[256];
    int ctor_count;
};

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name);
void zan_irgen_destroy(zan_irgen_t *g);

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit);
zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path);
zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path);

#endif /* ZAN_IRGEN_H */
