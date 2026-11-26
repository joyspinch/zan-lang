#ifndef ZAN_BUILD_PARTITION_H
#define ZAN_BUILD_PARTITION_H

/* Partitioned backend for development builds (docs/BUILD_CACHE.md stage 2).
 *
 * After the whole-module Zan passes, the method bodies of each source unit
 * (irgen_origin.c: a stdlib/package directory, or "user") move into a module
 * of their own. Each unit module is optimized with the full default<ON>
 * pipeline and compiled to an object on a worker thread, and the build cache
 * keys that object by the unit module's canonical bitcode -- so after a user
 * edit every library unit whose code did not change is a cache hit. What
 * stays behind (the residual: runtime helpers, type tables, main, anything
 * without a unit) goes through the ordinary optimize/emit path.
 *
 * Cross-module references: library unit functions are hidden external;
 * small ones are imported into callers as available_externally so inlining
 * across units still happens; small call-only residual helpers are copied
 * in as internal functions; unnamed_addr constants are copied privately
 * under content-derived names; everything else a unit uses from the
 * residual is promoted there to a hidden external symbol (".zp" suffix).
 * Unit modules list their members sorted by name, carry no value or type
 * names, and are built only from the module's content, so a no-cache build
 * produces the same objects as a cached one. */

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zan_part_opts {
    /* Function names and the index into `units` of their unit. */
    const char *const *fn_names;
    const int *fn_units;
    int fn_count;
    const char *const *units;
    int unit_count;
    /* Optimizes one unit module (full pipeline, no whole-module-only
     * passes); returns 0 on success. Runs on worker threads. */
    int (*optimize)(LLVMModuleRef mod, LLVMTargetMachineRef tm, int level);
    /* Whole-module steps `optimize` omits; run on the module once it is
     * certain to be split, before planning. */
    void (*prelude)(LLVMModuleRef mod);
    int level;
    /* Called on each optimized unit module right before codegen. */
    void (*prepare)(void *ctx, LLVMModuleRef mod);
    void *prepare_ctx;
    int threads;
    /* Object paths are "<obj_base>.u<i>.o". */
    const char *obj_base;
} zan_part_opts_t;

typedef struct zan_part_result {
    char **objs; /* malloc'd paths, objs[0..count) */
    int count;
    int units_hit, units_miss;
    long moved_insns, residual_insns;
    double ms_split, ms_compile;
} zan_part_result_t;

/* Splits `mod` (in place it becomes the residual module) and compiles the
 * unit modules. Returns 1 when partitioned, 0 when the module is not worth
 * splitting (left untouched), -1 on error (*error malloc'd). On success the
 * residual lists every symbol the optimized units still reference in
 * llvm.compiler.used: post-optimization checks (extern library pruning, the
 * scheduler-init strip) then see the whole program's references, and
 * GlobalDCE keeps discardable definitions the units need. The list has no
 * effect on the emitted object. */
int zan_part_run(LLVMModuleRef mod, LLVMTargetMachineRef tm, const zan_part_opts_t *opts,
                 zan_part_result_t *out, char **error);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_BUILD_PARTITION_H */
