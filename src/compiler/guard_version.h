/* guard_version.h -- LLVM pipeline runner with soft runtime-check region
 * versioning (see guard_version.cpp). */
#ifndef ZAN_GUARD_VERSION_H
#define ZAN_GUARD_VERSION_H

#include <llvm-c/Error.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int tune_loops; /* apply the four loop/SLP flags below */
    int loop_interleaving, loop_vectorization, slp_vectorization, loop_unrolling;
    int guard_version; /* add the versioning pass at the optimizer-early EP */
} zan_pipeline_opts_t;

/* Drop-in for LLVMRunPasses(mod, passes, tm, opts) with VerifyEach and
 * DebugLogging off. */
LLVMErrorRef zan_run_passes_guarded(LLVMModuleRef mod, const char *passes, LLVMTargetMachineRef tm,
                                    const zan_pipeline_opts_t *opts);

/* The same pass on its own, for the split pipeline: run it between the
 * simplification half (zan_opt_simplify_module) and the finish half, which
 * is where the optimizer-early extension point sits in default<ON>. */
void zan_guard_version_module(LLVMModuleRef mod, LLVMTargetMachineRef tm);

#ifdef __cplusplus
}
#endif

#endif
