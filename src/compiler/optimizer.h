/* optimizer.h -- Zan compiler optimization passes. */

#ifndef ZAN_OPTIMIZER_H
#define ZAN_OPTIMIZER_H

#include "zan.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- Optimization level ---- */

typedef enum {
    ZAN_OPT_NONE = 0,
    ZAN_OPT_BASIC = 1,
    ZAN_OPT_FULL = 2,
    ZAN_OPT_SIZE = 3,
    ZAN_OPT_AGGRESSIVE = 4
} zan_opt_level_t;

/* ---- ARC optimization ---- */
typedef struct {
    int pairs_elided;
    int moves_optimized;
    int stack_promotions;
} zan_arc_opt_stats_t;

zan_arc_opt_stats_t zan_opt_arc(zan_irgen_t *g, zan_opt_level_t level);

/* ---- Devirtualization ---- */
typedef struct {
    int calls_devirtualized;
    int interfaces_resolved;
} zan_devirt_stats_t;

zan_devirt_stats_t zan_opt_devirtualize(zan_irgen_t *g, zan_binder_t *binder);

/* ---- Escape analysis ---- */
typedef struct {
    int objects_stack_allocated;
    int allocations_eliminated;
} zan_escape_stats_t;

zan_escape_stats_t zan_opt_escape_analysis(zan_irgen_t *g);

/* ---- Constant folding ---- */
typedef struct {
    int constants_folded;
    int branches_eliminated;
    int variables_propagated;
} zan_constfold_stats_t;

zan_constfold_stats_t zan_opt_const_fold(zan_irgen_t *g);

/* ---- Dead code elimination ---- */
typedef struct {
    int dead_stores;
    int dead_calls;
    int unreachable_blocks;
} zan_dce_stats_t;

zan_dce_stats_t zan_opt_dce(zan_irgen_t *g);

/* ---- Inlining ---- */
typedef struct {
    int functions_inlined;
    int recursive_skipped;
} zan_inline_stats_t;

zan_inline_stats_t zan_opt_inline(zan_irgen_t *g, zan_opt_level_t level);

/* ---- Combined pipeline ---- */
typedef struct {
    zan_arc_opt_stats_t arc;
    zan_devirt_stats_t devirt;
    zan_escape_stats_t escape;
    zan_constfold_stats_t constfold;
    zan_dce_stats_t dce;
    zan_inline_stats_t inlining;
    double time_ms;
} zan_opt_report_t;

zan_opt_report_t zan_optimize(zan_irgen_t *g, zan_binder_t *binder, zan_opt_level_t level);
void zan_opt_report_print(const zan_opt_report_t *report);
void zan_opt_configure_llvm_passes(zan_irgen_t *g, zan_opt_level_t level);

#endif /* ZAN_OPTIMIZER_H */
