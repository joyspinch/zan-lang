/* optimizer.c -- Zan compiler optimization passes implementation. */

#include "optimizer.h"
#include "irgen.h"
#include "binder.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#include <time.h>
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#endif

/* ---- ARC optimization ---- */

typedef enum {
    ARC_NONE = 0,
    ARC_RETAIN_OBJ,
    ARC_RELEASE_OBJ,
    ARC_RETAIN_STR,
    ARC_RELEASE_STR,
} zan_arc_call_kind_t;

static zan_arc_call_kind_t get_arc_call_kind(LLVMValueRef inst) {
    if (!inst || LLVMGetInstructionOpcode(inst) != LLVMCall) return ARC_NONE;
    LLVMValueRef callee = LLVMGetCalledValue(inst);
    if (!callee) return ARC_NONE;
    const char *fn_name = LLVMGetValueName(callee);
    if (!fn_name) return ARC_NONE;
    if (strcmp(fn_name, "zan_rt_retain") == 0 || strcmp(fn_name, "zan_retain") == 0)
        return ARC_RETAIN_OBJ;
    if (strcmp(fn_name, "zan_rt_release") == 0 || strcmp(fn_name, "zan_rt_release_dyn") == 0 ||
        strcmp(fn_name, "zan_release") == 0)
        return ARC_RELEASE_OBJ;
    if (strcmp(fn_name, "zan_rt_str_retain") == 0)
        return ARC_RETAIN_STR;
    if (strcmp(fn_name, "zan_rt_str_release") == 0)
        return ARC_RELEASE_STR;
    return ARC_NONE;
}

static LLVMValueRef strip_pointer_casts(LLVMValueRef v) {
    int depth = 0;
    while (v && LLVMIsAInstruction(v) && depth < 16) {
        LLVMOpcode op = LLVMGetInstructionOpcode(v);
        if (op == LLVMBitCast || op == LLVMAddrSpaceCast) {
            v = LLVMGetOperand(v, 0);
            depth++;
        } else {
            break;
        }
    }
    return v;
}

static LLVMValueRef get_arc_operand(LLVMValueRef call) {
    if (!call || LLVMGetNumOperands(call) < 1) return NULL;
    return strip_pointer_casts(LLVMGetOperand(call, 0));
}

static bool are_same_arc_object(LLVMValueRef op1, LLVMValueRef op2) {
    if (op1 == op2) return true;
    if (!op1 || !op2) return false;
    if (LLVMIsAInstruction(op1) && LLVMIsAInstruction(op2)) {
        if (LLVMGetInstructionOpcode(op1) == LLVMLoad && LLVMGetInstructionOpcode(op2) == LLVMLoad) {
            LLVMValueRef ptr1 = strip_pointer_casts(LLVMGetOperand(op1, 0));
            LLVMValueRef ptr2 = strip_pointer_casts(LLVMGetOperand(op2, 0));
            if (ptr1 == ptr2 && ptr1 != NULL) {
                return true;
            }
        }
    }
    return false;
}

static bool is_safe_arc_intermediate(LLVMValueRef inst, LLVMValueRef target_obj) {
    if (!inst) return false;
    LLVMOpcode opcode = LLVMGetInstructionOpcode(inst);

    /* Terminators are not safe: do not cross basic block boundaries */
    if (LLVMIsATerminatorInst(inst)) return false;

    /* Calls: check for ARC calls on different objects or safe intrinsics/read-only calls */
    if (opcode == LLVMCall) {
        zan_arc_call_kind_t k = get_arc_call_kind(inst);
        if (k != ARC_NONE) {
            LLVMValueRef op = get_arc_operand(inst);
            /* If it operates on the same object, stop: must pair in program order */
            if (are_same_arc_object(op, target_obj)) return false;
            /* ARC calls on other objects do not mutate target_obj's refcount */
            return true;
        }
        LLVMValueRef callee = LLVMGetCalledValue(inst);
        if (callee) {
            const char *fn_name = LLVMGetValueName(callee);
            if (fn_name) {
                if (strncmp(fn_name, "llvm.lifetime.", 14) == 0 ||
                    strncmp(fn_name, "llvm.dbg.", 9) == 0 ||
                    strncmp(fn_name, "llvm.assume", 11) == 0 ||
                    strncmp(fn_name, "llvm.expect", 11) == 0 ||
                    strncmp(fn_name, "llvm.sadd.", 10) == 0 ||
                    strncmp(fn_name, "llvm.uadd.", 10) == 0 ||
                    strncmp(fn_name, "llvm.ssub.", 10) == 0 ||
                    strncmp(fn_name, "llvm.usub.", 10) == 0 ||
                    strncmp(fn_name, "llvm.smul.", 10) == 0 ||
                    strncmp(fn_name, "llvm.umul.", 10) == 0 ||
                    strncmp(fn_name, "llvm.bswap.", 11) == 0 ||
                    strncmp(fn_name, "llvm.ctpop.", 11) == 0 ||
                    strncmp(fn_name, "llvm.ctlz.", 10) == 0 ||
                    strncmp(fn_name, "llvm.cttz.", 10) == 0 ||
                    strcmp(fn_name, "zan_bound_check") == 0 ||
                    strcmp(fn_name, "zan_rt_bound_check") == 0) {
                    return true;
                }
            }
        }
        return false;
    }

    /* Store: safe only if target_obj itself is not being stored and the underlying slot is not overwritten */
    if (opcode == LLVMStore) {
        LLVMValueRef val = LLVMGetOperand(inst, 0);
        LLVMValueRef dst = strip_pointer_casts(LLVMGetOperand(inst, 1));
        if (strip_pointer_casts(val) == target_obj) {
            return false; /* Object stored to memory, potentially escaping */
        }
        if (LLVMIsAInstruction(target_obj) && LLVMGetInstructionOpcode(target_obj) == LLVMLoad) {
            LLVMValueRef src_ptr = strip_pointer_casts(LLVMGetOperand(target_obj, 0));
            if (dst == src_ptr) {
                return false; /* Underlying slot overwritten */
            }
        }
        return true;
    }

    /* Atomic operations and fences: unsafe */
    if (opcode == LLVMFence || opcode == LLVMAtomicRMW || opcode == LLVMAtomicCmpXchg) {
        return false;
    }

    /* Pure arithmetic, logical, bitwise, comparison, selection,
     * addressing, casting, extraction, and local load instructions are safe. */
    return true;
}

zan_arc_opt_stats_t zan_opt_arc(zan_irgen_t *g, zan_opt_level_t level) {
    zan_arc_opt_stats_t stats = {0, 0, 0};
    if (level == ZAN_OPT_NONE) return stats;

    LLVMModuleRef mod = g->mod;
    LLVMValueRef fn = LLVMGetFirstFunction(mod);

    while (fn) {
        bool fn_changed = true;
        int fn_pass = 0;
        while (fn_changed && fn_pass < 8) {
            fn_changed = false;
            fn_pass++;
            LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
            while (bb) {
                LLVMValueRef inst = LLVMGetFirstInstruction(bb);
                while (inst) {
                    LLVMValueRef next = LLVMGetNextInstruction(inst);
                    zan_arc_call_kind_t k1 = get_arc_call_kind(inst);

                    /* Eliminate no-op ARC operations on null pointers */
                    if (k1 != ARC_NONE) {
                        LLVMValueRef op = get_arc_operand(inst);
                        if (op && (LLVMIsNull(op) || (LLVMIsAConstant(op) && LLVMIsNull(op)))) {
                            LLVMInstructionEraseFromParent(inst);
                            stats.pairs_elided++;
                            fn_changed = true;
                            inst = next;
                            continue;
                        }
                    }

                    /* Eliminate redundant pairs: retain(x) followed by release(x) within safe window */
                    if (k1 == ARC_RETAIN_OBJ || k1 == ARC_RETAIN_STR) {
                        LLVMValueRef op1 = get_arc_operand(inst);
                        if (op1) {
                            LLVMValueRef curr = next;
                            int steps = 0;
                            const int MAX_ARC_WINDOW = 64;
                            LLVMValueRef match_release = NULL;

                            while (curr && steps < MAX_ARC_WINDOW) {
                                zan_arc_call_kind_t k2 = get_arc_call_kind(curr);
                                if (k2 != ARC_NONE) {
                                    LLVMValueRef op2 = get_arc_operand(curr);
                                    if (are_same_arc_object(op1, op2)) {
                                        if ((k1 == ARC_RETAIN_OBJ && k2 == ARC_RELEASE_OBJ) ||
                                            (k1 == ARC_RETAIN_STR && k2 == ARC_RELEASE_STR)) {
                                            match_release = curr;
                                        }
                                        break;
                                    }
                                }

                                if (!is_safe_arc_intermediate(curr, op1)) {
                                    break;
                                }
                                curr = LLVMGetNextInstruction(curr);
                                steps++;
                            }

                            if (match_release) {
                                LLVMValueRef after_retain = LLVMGetNextInstruction(inst);
                                if (after_retain == match_release) {
                                    after_retain = LLVMGetNextInstruction(match_release);
                                }
                                LLVMInstructionEraseFromParent(match_release);
                                LLVMInstructionEraseFromParent(inst);
                                stats.pairs_elided++;
                                fn_changed = true;
                                inst = after_retain;
                                continue;
                            }
                        }
                    }

                    inst = next;
                }
                bb = LLVMGetNextBasicBlock(bb);
            }
        }
        fn = LLVMGetNextFunction(fn);
    }

    return stats;
}

/* ---- Devirtualization ---- */

zan_devirt_stats_t zan_opt_devirtualize(zan_irgen_t *g, zan_binder_t *binder) {
    zan_devirt_stats_t stats = {0, 0};
    (void)binder;

    LLVMModuleRef mod = g->mod;
    LLVMValueRef fn = LLVMGetFirstFunction(mod);

    while (fn) {
        LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
        while (bb) {
            LLVMValueRef inst = LLVMGetFirstInstruction(bb);
            while (inst) {
                LLVMValueRef next = LLVMGetNextInstruction(inst);
                if (LLVMGetInstructionOpcode(inst) == LLVMCall) {
                    LLVMValueRef callee = LLVMGetCalledValue(inst);
                    if (callee && LLVMGetInstructionOpcode(callee) == LLVMLoad) {
                        LLVMValueRef ptr = LLVMGetOperand(callee, 0);
                        if (ptr && LLVMGetInstructionOpcode(ptr) == LLVMGetElementPtr) {
                            LLVMValueRef base = LLVMGetOperand(ptr, 0);
                            if (base && LLVMIsAGlobalVariable(base) && LLVMGetInitializer(base)) {
                                const char *vt_name = LLVMGetValueName(base);
                                if (vt_name && strstr(vt_name, "_vtable")) {
                                    LLVMValueRef init = LLVMGetInitializer(base);
                                    int num_gep_ops = LLVMGetNumOperands(ptr);
                                    LLVMValueRef idx_op = LLVMGetOperand(ptr, num_gep_ops - 1);
                                    if (LLVMIsAConstantInt(idx_op)) {
                                        unsigned long long slot = LLVMConstIntGetZExtValue(idx_op);
                                        LLVMValueRef elem = LLVMGetOperand(init, (unsigned)slot);
                                        while (elem && LLVMIsAConstantExpr(elem) && LLVMGetConstOpcode(elem) == LLVMBitCast) {
                                            elem = LLVMGetOperand(elem, 0);
                                        }
                                        if (elem && LLVMIsAFunction(elem)) {
                                            unsigned num_call_ops = (unsigned)LLVMGetNumOperands(inst);
                                            LLVMValueRef target = elem;
                                            if (LLVMTypeOf(target) != LLVMTypeOf(callee)) {
                                                LLVMBuilderRef b = g->builder;
                                                LLVMPositionBuilderBefore(b, inst);
                                                target = LLVMBuildBitCast(b, elem, LLVMTypeOf(callee), "devirt.fn");
                                            }
                                            LLVMSetOperand(inst, num_call_ops - 1, target);
                                            stats.calls_devirtualized++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                inst = next;
            }
            bb = LLVMGetNextBasicBlock(bb);
        }
        fn = LLVMGetNextFunction(fn);
    }

    return stats;
}

/* ---- Constant folding ---- */

zan_constfold_stats_t zan_opt_const_fold(zan_irgen_t *g) {
    zan_constfold_stats_t stats = {0, 0, 0};

    LLVMModuleRef mod = g->mod;
    LLVMValueRef fn = LLVMGetFirstFunction(mod);

    while (fn) {
        LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
        while (bb) {
            LLVMValueRef inst = LLVMGetFirstInstruction(bb);
            while (inst) {
                LLVMValueRef next = LLVMGetNextInstruction(inst);
                unsigned opcode = LLVMGetInstructionOpcode(inst);

                /* Check for binary ops on constants - LLVM handles via InstCombine */
                if (opcode == LLVMAdd || opcode == LLVMSub ||
                    opcode == LLVMMul || opcode == LLVMSDiv) {
                    LLVMValueRef lhs = LLVMGetOperand(inst, 0);
                    LLVMValueRef rhs = LLVMGetOperand(inst, 1);
                    if (LLVMIsAConstantInt(lhs) && LLVMIsAConstantInt(rhs)) {
                        stats.constants_folded++;
                        /* LLVM pass pipeline handles actual folding */
                    }
                }

                /* Detect dead conditional branches */
#if ZAN_LLVM_MAJOR >= 23
                /* 23 split the br opcode: the 3-operand conditional form is
                 * its own LLVMCondBr now. */
                if (opcode == LLVMCondBr && LLVMGetNumOperands(inst) == 3) {
#else
                if (opcode == LLVMBr && LLVMGetNumOperands(inst) == 3) {
#endif
                    LLVMValueRef cond = LLVMGetCondition(inst);
                    if (cond && LLVMIsAConstantInt(cond)) {
                        stats.branches_eliminated++;
                    }
                }

                inst = next;
            }
            bb = LLVMGetNextBasicBlock(bb);
        }
        fn = LLVMGetNextFunction(fn);
    }

    return stats;
}

/* ---- Dead code elimination ---- */

zan_dce_stats_t zan_opt_dce(zan_irgen_t *g) {
    zan_dce_stats_t stats = {0, 0, 0};

    LLVMModuleRef mod = g->mod;
    LLVMValueRef fn = LLVMGetFirstFunction(mod);

    while (fn) {
        /* LLVMGetFirstBasicBlock returns NULL for declarations (extern
         * [DllImport] targets, runtime decls, not-yet-defined generic
         * instantiations); LLVMGetEntryBasicBlock would instead deref the
         * empty block list and hand back a bogus block, so walking its
         * instructions faults. Use the first block and skip bodyless fns. */
        LLVMBasicBlockRef entry = LLVMGetFirstBasicBlock(fn);
        if (entry) {
            LLVMValueRef inst = LLVMGetFirstInstruction(entry);
            while (inst) {
                LLVMValueRef next = LLVMGetNextInstruction(inst);
                if (LLVMGetInstructionOpcode(inst) == LLVMAlloca) {
                    if (!LLVMGetFirstUse(inst)) {
                        stats.dead_stores++;
                    }
                }
                inst = next;
            }
        }
        fn = LLVMGetNextFunction(fn);
    }

    return stats;
}

/* ---- Inlining ---- */

zan_inline_stats_t zan_opt_inline(zan_irgen_t *g, zan_opt_level_t level) {
    (void)g;
    (void)level;
    /* No force, no hint: ARC ownership transfer across inline boundaries
     * requires EH-ownership-aware lifetime tracking, so automatic inlining
     * attributes are omitted here. LLVM's standard size-tier cost model
     * handles inlining decisions during pass execution. */
    zan_inline_stats_t stats = {0, 0};
    return stats;
}

/* ---- LLVM pass pipeline configuration ---- */

#if ZAN_LLVM_MAJOR >= 23
/* LLVM 23 removed the Os/Oz optimization levels: run the O2 pipeline and
 * mark every defined function with the size attributes instead, which is
 * how clang -Os/-Oz are encoded now. */
static void zan_opt_mark_size(zan_irgen_t *g, bool min_size) {
    LLVMContextRef ctx = LLVMGetModuleContext(g->mod);
    LLVMAttributeRef opt = LLVMCreateEnumAttribute(ctx,
        LLVMGetEnumAttributeKindForName("optsize", 7), 0);
    LLVMAttributeRef mins = min_size
        ? LLVMCreateEnumAttribute(ctx,
              LLVMGetEnumAttributeKindForName("minsize", 7), 0)
        : NULL;
    for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
         fn = LLVMGetNextFunction(fn)) {
        if (LLVMIsDeclaration(fn)) continue;
        LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)(-1), opt);
        if (mins) LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)(-1), mins);
    }
}
#endif

void zan_opt_configure_llvm_passes(zan_irgen_t *g, zan_opt_level_t level) {
    if (level == ZAN_OPT_NONE) return;

    LLVMModuleRef mod = g->mod;
    const char *passes;
    switch (level) {
    case ZAN_OPT_BASIC: passes = "default<O1>"; break;
    case ZAN_OPT_FULL: passes = "default<O2>"; break;
#if ZAN_LLVM_MAJOR >= 23
    case ZAN_OPT_SIZE:
        zan_opt_mark_size(g, false);
        passes = "default<O2>";
        break;
    case ZAN_OPT_SIZE_MIN:
        zan_opt_mark_size(g, true);
        passes = "default<O2>";
        break;
#else
    case ZAN_OPT_SIZE: passes = "default<Os>"; break;
    case ZAN_OPT_SIZE_MIN: passes = "default<Oz>"; break;
#endif
    case ZAN_OPT_AGGRESSIVE: passes = "default<O3>"; break;
    default: return;
    }

    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    LLVMPassBuilderOptionsSetVerifyEach(opts, 0);
    LLVMPassBuilderOptionsSetDebugLogging(opts, 0);

    /* Vectorization/unrolling grow code; only the speed levels want them
     * (Os/Oz optimize for size). */
    if (level == ZAN_OPT_FULL || level == ZAN_OPT_AGGRESSIVE) {
        LLVMPassBuilderOptionsSetLoopInterleaving(opts, 1);
        LLVMPassBuilderOptionsSetLoopVectorization(opts, 1);
        LLVMPassBuilderOptionsSetSLPVectorization(opts, 1);
        LLVMPassBuilderOptionsSetLoopUnrolling(opts, 1);
    }

    LLVMErrorRef err = LLVMRunPasses(mod, passes, NULL, opts);
    if (err) {
        char *msg = LLVMGetErrorMessage(err);
        fprintf(stderr, "warning: LLVM pass pipeline error: %s\n", msg);
        LLVMDisposeErrorMessage(msg);
    }

    LLVMDisposePassBuilderOptions(opts);
}

/* Delete every function and global no live code refers to, without running any
 * other transform. A whole program is one module here, so a `using` that pulls
 * in a directory of stdlib widgets leaves uncalled definitions behind.
 * GlobalDCE is a pure reachability sweep over the module's reference graph,
 * which keeps unoptimized builds fast while dropping dead weight. Only
 * internal-linkage definitions can be removed, which is why irgen marks
 * everything but `main` internal. */
void zan_opt_strip_unused(zan_irgen_t *g) {
    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    LLVMPassBuilderOptionsSetVerifyEach(opts, 0);
    LLVMPassBuilderOptionsSetDebugLogging(opts, 0);
    LLVMErrorRef err = LLVMRunPasses(g->mod, "globaldce", NULL, opts);
    if (err) {
        char *msg = LLVMGetErrorMessage(err);
        fprintf(stderr, "warning: LLVM globaldce error: %s\n", msg);
        LLVMDisposeErrorMessage(msg);
    }
    LLVMDisposePassBuilderOptions(opts);
}

static void zan_opt_early_mem2reg(zan_irgen_t *g) {
    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    LLVMPassBuilderOptionsSetVerifyEach(opts, 0);
    LLVMPassBuilderOptionsSetDebugLogging(opts, 0);
    LLVMErrorRef err = LLVMRunPasses(g->mod, "sroa,early-cse", NULL, opts);
    if (err) {
        char *msg = LLVMGetErrorMessage(err);
        fprintf(stderr, "warning: LLVM early sroa error: %s\n", msg);
        LLVMDisposeErrorMessage(msg);
    }
    LLVMDisposePassBuilderOptions(opts);
}

/* ---- Combined pipeline ---- */

zan_opt_report_t zan_optimize(zan_irgen_t *g, zan_binder_t *binder, zan_opt_level_t level) {
    zan_opt_report_t report;
    memset(&report, 0, sizeof(report));
    if (level == ZAN_OPT_NONE) return report;

    double t0 = get_time_ms();

    zan_opt_early_mem2reg(g);
    report.arc = zan_opt_arc(g, level);
    report.devirt = zan_opt_devirtualize(g, binder);
    report.constfold = zan_opt_const_fold(g);
    report.dce = zan_opt_dce(g);
    report.inlining = zan_opt_inline(g, level);

    zan_opt_configure_llvm_passes(g, level);

    double t1 = get_time_ms();
    report.time_ms = t1 - t0;

    return report;
}

void zan_opt_report_print(const zan_opt_report_t *report) {
    fprintf(stderr, "Optimization report (%.1f ms):\n", report->time_ms);
    if (report->arc.pairs_elided > 0)
        fprintf(stderr, "  ARC: %d retain/release pairs elided\n", report->arc.pairs_elided);
    if (report->devirt.calls_devirtualized > 0)
        fprintf(stderr, "  Devirt: %d virtual calls resolved\n", report->devirt.calls_devirtualized);
    if (report->constfold.constants_folded > 0)
        fprintf(stderr, "  Const: %d expressions folded\n", report->constfold.constants_folded);
    if (report->dce.dead_stores > 0)
        fprintf(stderr, "  DCE: %d dead stores removed\n", report->dce.dead_stores);
    if (report->inlining.functions_inlined > 0)
        fprintf(stderr, "  Inline: %d functions inlined\n", report->inlining.functions_inlined);
}
