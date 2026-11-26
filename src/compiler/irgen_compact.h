
#ifndef ZAN_IRGEN_COMPACT_H
#define ZAN_IRGEN_COMPACT_H

#include <stddef.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zan_irgen_compactor zan_irgen_compactor_t;

zan_irgen_compactor_t *zan_irgen_compactor_create(LLVMModuleRef module,
                                                  char *errbuf,
                                                  size_t errbuf_size);

int zan_irgen_compactor_run(zan_irgen_compactor_t *compactor,
                            LLVMValueRef function, char *errbuf,
                            size_t errbuf_size);

void zan_irgen_compactor_destroy(zan_irgen_compactor_t *compactor);

/* LLVM's C target-machine API lacks these target options. */
LLVMTargetMachineRef zan_create_target_machine_with_options(
    LLVMTargetRef target, const char *triple, const char *cpu,
    const char *features, LLVMCodeGenOptLevel level, LLVMBool emulated_tls,
    LLVMBool wasm_exceptions);

/* Drop a function's body, keeping its linkage (Function::deleteBody resets it
 * to external). The function becomes a declaration. */
void zan_llvm_delete_body(LLVMValueRef function);

/* Split basic block `bb` immediately before `before_inst`, updating all
 * successor PHI incoming blocks automatically. Returns the new tail block. */
LLVMBasicBlockRef zan_llvm_split_bb(LLVMBasicBlockRef bb,
                                    LLVMValueRef before_inst,
                                    const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_IRGEN_COMPACT_H */
