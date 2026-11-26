#ifndef ZAN_CODEGEN_SPLIT_H
#define ZAN_CODEGEN_SPLIT_H

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The default<O1..O3> pipeline in two halves (what buildPerModuleDefaultPipeline
 * strings together):
 *
 *   simplify  -- inliner + function simplification (CGSCC walk), interprocedural
 *                cleanups. Needs the whole module to see every callee body.
 *   finish    -- the "module optimization" pipeline: loop rotate/vectorize/
 *                unroll, SLP, late cleanups, GlobalDCE. Function-local apart
 *                from dead-global removal, so it runs per partition.
 *
 * zan_opt_simplify_module + zan_opt_finish_module on the same module produce
 * exactly what default<ON> produces. The point of the split is that the
 * finish half can run per partition inside zan_codegen_split_emit, in parallel
 * with codegen, without losing any inlining (all inlining is in the first half).
 *
 * `level` is 1..3 (O1..O3); `vectorize` sets the loop/SLP vectorizer,
 * interleaving and unrolling tuning flags exactly as LLVMRunPasses options do.
 * The caller verifies the module afterwards, as it does for LLVMRunPasses. */
void zan_opt_simplify_module(LLVMModuleRef mod, LLVMTargetMachineRef tm, int level, int vectorize);
void zan_opt_finish_module(LLVMModuleRef mod, LLVMTargetMachineRef tm, int level, int vectorize);

typedef struct zan_split_opts {
    /* 0: the module is fully optimized. 1..3: run the finish half of
     * default<ON> on every partition before emitting it. */
    int finish_level;
    int vectorize;
    /* Called on each partition (from a worker thread, each with its own
     * module/context) after optimization and right before codegen. May be NULL. */
    void (*prepare)(void *ctx, LLVMModuleRef part);
    void *prepare_ctx;
} zan_split_opts_t;

/* Parallel object emission for one module.
 *
 * LLVM's code generator keeps per-module state until the whole module is
 * emitted, so its memory grows with module size (the self-hosted compiler:
 * 0.47 GB of IR -> 1.65 GB peak in codegen) and it runs on one thread. This
 * splits the module into `parts` linkable partitions (llvm::SplitModule, as
 * LTO's parallel codegen does), serializes each to bitcode, disposes the
 * input module, then optimizes (opts->finish_level) and emits the partitions
 * on up to `threads` threads, each in its own LLVMContext and TargetMachine
 * cloned from `tm`.
 *
 * Takes ownership of `mod` (always disposed). Partition i is written to
 * paths[i]. Returns 0 on success; on failure returns -1 and sets *error to a
 * malloc'd message. */
int zan_codegen_split_emit(LLVMModuleRef mod, LLVMTargetMachineRef tm, int parts, int threads,
                           const zan_split_opts_t *opts, const char *const *paths, char **error);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_CODEGEN_SPLIT_H */
