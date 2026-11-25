
#ifndef ZAN_OPTIMIZER_H
#define ZAN_OPTIMIZER_H

#include "zan.h"
#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ZAN_OPT_NONE = 0,
    ZAN_OPT_BASIC = 1,
    ZAN_OPT_FULL = 2,
    ZAN_OPT_SIZE = 3,
    ZAN_OPT_AGGRESSIVE = 4,
    ZAN_OPT_SIZE_MIN = 5
} zan_opt_level_t;

typedef struct {
    int pairs_elided;
    int moves_optimized;
    int stack_promotions;
} zan_arc_opt_stats_t;

zan_arc_opt_stats_t zan_opt_arc(zan_irgen_t *g, zan_opt_level_t level);

typedef struct {
    int objects_analyzed;
    int stack_promoted;
} zan_escape_stats_t;

zan_escape_stats_t zan_opt_escape(zan_irgen_t *g);

typedef struct {
    int calls_devirtualized;
    int interfaces_resolved;
} zan_devirt_stats_t;

zan_devirt_stats_t zan_opt_devirtualize(zan_irgen_t *g, zan_binder_t *binder);

typedef struct {
    int constants_folded;
    int branches_eliminated;
    int variables_propagated;
} zan_constfold_stats_t;

zan_constfold_stats_t zan_opt_const_fold(zan_irgen_t *g);

typedef struct {
    int dead_stores;
    int dead_calls;
    int unreachable_blocks;
} zan_dce_stats_t;

zan_dce_stats_t zan_opt_dce(zan_irgen_t *g);

typedef struct {
    int functions_inlined;
    int recursive_skipped;
} zan_inline_stats_t;

zan_inline_stats_t zan_opt_inline(zan_irgen_t *g, zan_opt_level_t level);

typedef struct {
    zan_escape_stats_t escape;
    zan_arc_opt_stats_t arc;
    zan_devirt_stats_t devirt;
    zan_constfold_stats_t constfold;
    zan_dce_stats_t dce;
    zan_inline_stats_t inlining;
    double time_ms;
    bool success; /* false: stop before publishing IR or machine code */
} zan_opt_report_t;

zan_opt_report_t zan_optimize(zan_irgen_t *g, zan_binder_t *binder, zan_opt_level_t level);
/* Decides whether this build leaves the finish half of the LLVM pipeline to
 * object emission (codegen_split.h) and records it in g->opt_finish_level /
 * opt_finish_vectorize. zan_optimize calls it; a build-cache hit on the
 * optimized module calls it to restore the same pending state. */
bool zan_opt_plan_finish(zan_irgen_t *g, zan_opt_level_t level);
void zan_opt_report_print(const zan_opt_report_t *report);
zan_status_t zan_opt_configure_llvm_passes(zan_irgen_t *g, zan_opt_level_t level);
zan_status_t zan_opt_run_passes_on_module(LLVMModuleRef mod, LLVMTargetMachineRef tm, zan_opt_level_t level);
/* The same pipeline for one partitioned unit module (build_partition.h):
 * no whole-module-only steps. Thread-safe per module; returns 0 on success. */
int zan_opt_run_passes_on_unit(LLVMModuleRef mod, LLVMTargetMachineRef tm, int level);
/* Whole-module steps the unit pipeline leaves out; run once before splitting. */
void zan_opt_unit_prelude(LLVMModuleRef mod);

/* 收缩 pass（S-UNWIND）：nothrow 闭包后将可证
 * 不抛的 invoke 塌缩为 call（personality/语义不变）。返回塌缩数。 */
int zan_shrink_collapse_invokes(LLVMModuleRef mod);

/* 注记 Zan 编译器发射的函数 LLVM 名（irgen_register_function 调用）。
 * publish 跨片时该名在别的分片模块里只是 declaration，收缩 pass 用
 * 它区分"Zan 函数（可抛）"与"C 导入（按前缀白名单判 nothrow）"。 */
void zan_opt_note_zan_fn_name(const char *name);

zan_status_t zan_opt_strip_unused(zan_irgen_t *g);

/* Reject unsupported hardware calls, never mere intrinsic declarations. */
zan_status_t zan_opt_validate_cpu(zan_irgen_t *g, LLVMModuleRef mod);

/* Shared gate for optimizer stages and artifact writers. */
zan_status_t zan_opt_verify_module(LLVMModuleRef mod, const char *stage);

/* opt_tbaa.c: field-offset TBAA (see file comment). */
void zan_tbaa_mark_field(zan_irgen_t *g, LLVMValueRef gep, LLVMTypeRef st, int fi);
int zan_opt_tbaa(LLVMModuleRef mod);

#endif /* ZAN_OPTIMIZER_H */
