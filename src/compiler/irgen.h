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

    /* runtime function declarations */
    LLVMValueRef rt_println;   /* zan_rt_println(const char*) */
    LLVMValueRef rt_print_int; /* zan_rt_print_int(int64) */
    LLVMValueRef rt_print_double; /* zan_rt_print_double(double) */
};

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name);
void zan_irgen_destroy(zan_irgen_t *g);

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit);
zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path);
zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path);

#endif /* ZAN_IRGEN_H */
