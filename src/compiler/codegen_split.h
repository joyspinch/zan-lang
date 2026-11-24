#ifndef ZAN_CODEGEN_SPLIT_H
#define ZAN_CODEGEN_SPLIT_H

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parallel object emission for one already-optimized module.
 *
 * LLVM's code generator keeps per-module state until the whole module is
 * emitted, so its memory grows with module size (the self-hosted compiler:
 * 0.47 GB of IR -> 1.65 GB peak in codegen) and it runs on one thread. This
 * splits the module into `parts` linkable partitions (llvm::SplitModule, as
 * LTO's parallel codegen does), serializes each to bitcode, disposes the
 * input module, then emits the partitions on up to `threads` threads, each in
 * its own LLVMContext and TargetMachine cloned from `tm`.
 *
 * Takes ownership of `mod` (always disposed). Partition i is written to
 * paths[i]. Returns 0 on success; on failure returns -1 and sets *error to a
 * malloc'd message. */
int zan_codegen_split_emit(LLVMModuleRef mod, LLVMTargetMachineRef tm, int parts, int threads,
                           const char *const *paths, char **error);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_CODEGEN_SPLIT_H */
