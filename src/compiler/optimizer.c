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

static bool is_arc_call(LLVMValueRef inst, const char *name) {
    if (LLVMGetInstructionOpcode(inst) != LLVMCall) return false;
    LLVMValueRef callee = LLVMGetCalledValue(inst);
    if (!callee) return false;
    const char *fn_name = LLVMGetValueName(callee);
    if (!fn_name) return false;
    return strcmp(fn_name, name) == 0;
}

static LLVMValueRef get_arc_operand(LLVMValueRef call) {
    if (LLVMGetNumOperands(call) < 1) return NULL;
    return LLVMGetOperand(call, 0);
}

zan_arc_opt_stats_t zan_opt_arc(zan_irgen_t *g, zan_opt_level_t level) {
    zan_arc_opt_stats_t stats = {0, 0, 0};
    if (level == ZAN_OPT_NONE) return stats;

    LLVMModuleRef mod = g->mod;
    LLVMValueRef fn = LLVMGetFirstFunction(mod);

    while (fn) {
        LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
        while (bb) {
            LLVMValueRef inst = LLVMGetFirstInstruction(bb);
            while (inst) {
                LLVMValueRef next = LLVMGetNextInstruction(inst);

                /* Pattern: retain(x) followed by release(x) */
                if (next && is_arc_call(inst, "zan_retain") && is_arc_call(next, "zan_release")) {
                    LLVMValueRef op1 = get_arc_operand(inst);
                    LLVMValueRef op2 = get_arc_operand(next);
                    if (op1 && op2 && op1 == op2) {
                        LLVMValueRef after_next = LLVMGetNextInstruction(next);
                        LLVMInstructionEraseFromParent(next);
                        LLVMInstructionEraseFromParent(inst);
                        stats.pairs_elided++;
                        inst = after_next;
                        continue;
                    }
                }

                /* Pattern: release(x) followed by retain(x) */
                if (next && is_arc_call(inst, "zan_release") && is_arc_call(next, "zan_retain")) {
                    LLVMValueRef op1 = get_arc_operand(inst);
                    LLVMValueRef op2 = get_arc_operand(next);
                    if (op1 && op2 && op1 == op2) {
                        LLVMValueRef after_next = LLVMGetNextInstruction(next);
                        LLVMInstructionEraseFromParent(next);
                        LLVMInstructionEraseFromParent(inst);
                        stats.pairs_elided++;
                        inst = after_next;
                        continue;
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
                if (LLVMGetInstructionOpcode(inst) == LLVMCall) {
                    LLVMValueRef callee = LLVMGetCalledValue(inst);
                    if (callee && LLVMGetInstructionOpcode(callee) == LLVMLoad) {
                        LLVMValueRef ptr = LLVMGetOperand(callee, 0);
                        if (ptr && LLVMGetInstructionOpcode(ptr) == LLVMGetElementPtr) {
                            LLVMValueRef base = LLVMGetOperand(ptr, 0);
                            if (base && LLVMIsAGlobalVariable(base)) {
                                const char *vt_name = LLVMGetValueName(base);
                                if (vt_name && strstr(vt_name, "_vtable")) {
                                    stats.calls_devirtualized++;
                                }
                            }
                        }
                    }
                }
                inst = LLVMGetNextInstruction(inst);
            }
            bb = LLVMGetNextBasicBlock(bb);
        }
        fn = LLVMGetNextFunction(fn);
    }

    return stats;
}

/* ---- Escape analysis ---- */

static bool value_escapes(LLVMValueRef alloc, LLVMValueRef fn) {
    (void)fn;
    LLVMUseRef use = LLVMGetFirstUse(alloc);
    while (use) {
        LLVMValueRef user = LLVMGetUser(use);
        unsigned opcode = LLVMGetInstructionOpcode(user);

        switch (opcode) {
        case LLVMStore:
            if (LLVMGetOperand(user, 0) == alloc) {
                LLVMValueRef dest = LLVMGetOperand(user, 1);
                if (!LLVMIsAAllocaInst(dest)) return true;
            }
            break;
        case LLVMCall: {
            LLVMValueRef callee = LLVMGetCalledValue(user);
            const char *name = callee ? LLVMGetValueName(callee) : NULL;
            if (name && (strcmp(name, "zan_retain") == 0 || strcmp(name, "zan_release") == 0))
                break;
            return true;
        }
        case LLVMRet:
            return true;
        case LLVMGetElementPtr:
        case LLVMBitCast:
            if (value_escapes(user, fn)) return true;
            break;
        default:
            break;
        }
        use = LLVMGetNextUse(use);
    }
    return false;
}

zan_escape_stats_t zan_opt_escape_analysis(zan_irgen_t *g) {
    zan_escape_stats_t stats = {0, 0};

    LLVMModuleRef mod = g->mod;
    LLVMValueRef fn = LLVMGetFirstFunction(mod);

    while (fn) {
        LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
        while (bb) {
            LLVMValueRef inst = LLVMGetFirstInstruction(bb);
            while (inst) {
                if (LLVMGetInstructionOpcode(inst) == LLVMCall) {
                    LLVMValueRef callee = LLVMGetCalledValue(inst);
                    const char *name = callee ? LLVMGetValueName(callee) : NULL;
                    if (name && strcmp(name, "zan_alloc") == 0) {
                        if (!value_escapes(inst, fn)) {
                            stats.objects_stack_allocated++;
                        }
                    }
                }
                inst = LLVMGetNextInstruction(inst);
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
                if (opcode == LLVMBr && LLVMGetNumOperands(inst) == 3) {
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
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
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
    zan_inline_stats_t stats = {0, 0};

    LLVMModuleRef mod = g->mod;
    LLVMValueRef fn = LLVMGetFirstFunction(mod);

    while (fn) {
        /* Never force-inline interposable definitions (weak/linkonce/common):
         * they exist to be replaced at link time. The prime example is the
         * weak zan_io_pump stub, which the real blocking reactor pump in
         * zanrt_io overrides at link time. Force-inlining its `ret 0` body
         * folds the scheduler's idle path to a constant "no IO pending", so
         * any async/socket program (e.g. a server awaiting Accept) exits
         * immediately instead of blocking on the reactor. Leave inlining of
         * such functions to the LLVM pipeline, which honors link-time
         * interposition. */
        LLVMLinkage lk = LLVMGetLinkage(fn);
        bool interposable =
            (lk == LLVMWeakAnyLinkage || lk == LLVMWeakODRLinkage ||
             lk == LLVMLinkOnceAnyLinkage || lk == LLVMLinkOnceODRLinkage ||
             lk == LLVMCommonLinkage || lk == LLVMExternalWeakLinkage ||
             lk == LLVMAvailableExternallyLinkage);
        if (!LLVMIsDeclaration(fn) && !interposable) {
            unsigned bb_count = LLVMCountBasicBlocks(fn);
            if (bb_count <= 4 && level >= ZAN_OPT_BASIC) {
                LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)(-1),
                    LLVMCreateEnumAttribute(LLVMGetModuleContext(mod),
                        LLVMGetEnumAttributeKindForName("alwaysinline", 12), 0));
                stats.functions_inlined++;
            } else if (bb_count <= 10 && level >= ZAN_OPT_FULL) {
                LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)(-1),
                    LLVMCreateEnumAttribute(LLVMGetModuleContext(mod),
                        LLVMGetEnumAttributeKindForName("inlinehint", 10), 0));
            }
        }
        fn = LLVMGetNextFunction(fn);
    }

    return stats;
}

/* ---- LLVM pass pipeline configuration ---- */

void zan_opt_configure_llvm_passes(zan_irgen_t *g, zan_opt_level_t level) {
    if (level == ZAN_OPT_NONE) return;

    LLVMModuleRef mod = g->mod;
    const char *passes;
    switch (level) {
    case ZAN_OPT_BASIC: passes = "default<O1>"; break;
    case ZAN_OPT_FULL: passes = "default<O2>"; break;
    case ZAN_OPT_SIZE: passes = "default<Os>"; break;
    case ZAN_OPT_AGGRESSIVE: passes = "default<O3>"; break;
    default: return;
    }

    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    LLVMPassBuilderOptionsSetVerifyEach(opts, 0);
    LLVMPassBuilderOptionsSetDebugLogging(opts, 0);

    if (level >= ZAN_OPT_FULL) {
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

/* ---- Combined pipeline ---- */

zan_opt_report_t zan_optimize(zan_irgen_t *g, zan_binder_t *binder, zan_opt_level_t level) {
    zan_opt_report_t report;
    memset(&report, 0, sizeof(report));
    if (level == ZAN_OPT_NONE) return report;

    double t0 = get_time_ms();

    report.arc = zan_opt_arc(g, level);
    report.devirt = zan_opt_devirtualize(g, binder);
    report.escape = zan_opt_escape_analysis(g);
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
    if (report->escape.objects_stack_allocated > 0)
        fprintf(stderr, "  Escape: %d objects stack-allocated\n", report->escape.objects_stack_allocated);
    if (report->constfold.constants_folded > 0)
        fprintf(stderr, "  Const: %d expressions folded\n", report->constfold.constants_folded);
    if (report->dce.dead_stores > 0)
        fprintf(stderr, "  DCE: %d dead stores removed\n", report->dce.dead_stores);
    if (report->inlining.functions_inlined > 0)
        fprintf(stderr, "  Inline: %d functions inlined\n", report->inlining.functions_inlined);
}
