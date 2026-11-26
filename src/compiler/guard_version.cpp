/* guard_version.cpp -- soft runtime-check region versioning.
 *
 * Zan's runtime checks are soft: outside a try a failing check reports once
 * per site and execution continues on a scratch object, so the failing edge
 * merges back into the fast path (generic_guard.c emit_runtime_check /
 * emit_soft_base_select).  After the merge LLVM no longer knows the checked
 * value is good, so every later access re-tests the same condition, loads go
 * through phi(x, scratch) and nothing is CSE'd or hoisted.  On nbody that is
 * 2x the unchecked time.
 *
 * Fix: duplicate code so the hot path never sees a merge, keeping every
 * check, every report and their order (pure duplication plus folding of
 * values that are provably equal on the path taken):
 *  1. Whole loop (version_loop): run a clone of a loop that contains guards;
 *     each guard in the clone keeps its own report block, whose successors
 *     become the original loop's.  The first failing check therefore reports
 *     at its own site and hands over to the original loop, which finishes
 *     that iteration and the rest fully checked.  The clone has no calls and
 *     no merges, so LICM, unswitching and late ARC treat it like unchecked
 *     code.
 *  2. Invariant guard (step_invariant): unswitch the loop on it in the
 *     preheader; the original loop, still checking in place, runs otherwise.
 *  3. Region (step): for loops too big to clone whole, clone the blocks a
 *     guard dominates within the iteration and enter them from its ok edge.
 * Strict mode and try still throw from the same sites.
 *
 * Runs once at default<ON>'s optimizer-early point (after inlining and the
 * function simplification pipeline, before vectorisation); each round is
 * followed by simplifycfg/instcombine/GVN/LICM/trivial unswitch on the
 * function.  Original (slow-world) branches are marked and never versioned
 * again, so k checked values give k+1 copies, not 2^k; region and
 * per-function budgets bound code size.  ZAN_NO_GUARD_VERSION=1 disables it;
 * ZAN_GUARD_VERSION_STATS=1 prints counts, =2 each region. */

#include "guard_version.h"

#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/ProfDataUtils.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Support/Error.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/LICM.h>
#include <llvm/Transforms/Scalar/LoopPassManager.h>
#include <llvm/Transforms/Scalar/SimpleLoopUnswitch.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/SSAUpdater.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace llvm;

namespace {

/* Budgets in instructions.  Hot loops are a few hundred instructions (the
 * bench suite's largest is 305); bigger loops are rarely where the time goes
 * and doubling them shows in the binary (+14% text on zandoc at 1500). */
constexpr unsigned kMaxRegionInsts = 400;
constexpr unsigned kMaxVersionsPerFn = 48;
constexpr unsigned kMaxFnInsts = 40000;

struct GVStats {
    bool verbose = false;
    unsigned versions = 0;
    unsigned folded = 0;
    unsigned functions = 0;
    unsigned loops = 0;
};

#if LLVM_VERSION_MAJOR >= 23
using CondBr = CondBrInst;
CondBr *as_cond_br(Instruction *I) { return dyn_cast_or_null<CondBrInst>(I); }
CondBr *make_cond_br(Value *C, BasicBlock *T, BasicBlock *F, Instruction *before) {
    return CondBrInst::Create(C, T, F, before->getIterator());
}
#else
using CondBr = BranchInst;
CondBr *as_cond_br(Instruction *I) {
    auto *B = dyn_cast_or_null<BranchInst>(I);
    return (B && B->isConditional()) ? B : nullptr;
}
CondBr *make_cond_br(Value *C, BasicBlock *T, BasicBlock *F, Instruction *before) {
    return BranchInst::Create(T, F, C, before);
}
#endif

bool is_guard_report(const Function *F) {
    if (!F) return false;
    StringRef n = F->getName();
    return n == "__zan_guard_raise" || n == "zan_rt_guard_fail3" || n == "zan_rt_soft_is_hard" ||
           n == "zan_rt_soft_note";
}

bool block_reports(const BasicBlock *BB) {
    for (const Instruction &I : *BB)
        if (auto *CB = dyn_cast<CallBase>(&I))
            if (is_guard_report(CB->getCalledFunction())) return true;
    return false;
}

/* The report sits at most two blocks below the failing edge (non-split mode
 * tests "inside a try" first). */
bool reports_soon(const BasicBlock *BB) {
    if (block_reports(BB)) return true;
    for (const BasicBlock *S : successors(BB))
        if (block_reports(S)) return true;
    return false;
}

/* Index of the ok successor of a guard branch, or -1 if BI is no guard.  The
 * failing side is the one that reports; when neither side reports directly
 * the expect-derived branch weights pick the cold side. */
int ok_successor(CondBr *BI) {
    BasicBlock *T = BI->getSuccessor(0), *F = BI->getSuccessor(1);
    if (T == F) return -1;
    bool tr = block_reports(T), fr = block_reports(F);
    if (tr != fr) return tr ? 1 : 0;
    if (tr) return -1;
    SmallVector<uint32_t, 2> w;
    if (!extractBranchWeights(BI->getMetadata(LLVMContext::MD_prof), w) || w.size() != 2) return -1;
    bool t_cold = (uint64_t)w[0] * 64 < w[1], f_cold = (uint64_t)w[1] * 64 < w[0];
    if (t_cold == f_cold) return -1;
    if (!reports_soon(t_cold ? T : F)) return -1;
    return t_cold ? 1 : 0;
}

/* `icmp eq/ne V, null` -> V (pointer casts stripped), else null. */
Value *null_tested(Value *C, bool *is_eq) {
    auto *IC = dyn_cast<ICmpInst>(C);
    if (!IC || !IC->isEquality()) return nullptr;
    Value *a = IC->getOperand(0), *b = IC->getOperand(1);
    if (isa<ConstantPointerNull>(a)) std::swap(a, b);
    if (!isa<ConstantPointerNull>(b) || !a->getType()->isPointerTy()) return nullptr;
    *is_eq = IC->getPredicate() == ICmpInst::ICMP_EQ;
    return a->stripPointerCasts();
}

/* Same value modulo reloads: a load, or a phi of loads, of one pointer
 * (a bounds check re-reads the array length after each failing report). */
const Value *load_root(const Value *v, unsigned depth = 0) {
    if (auto *L = dyn_cast<LoadInst>(v)) return L->getPointerOperand();
    if (auto *P = dyn_cast<PHINode>(v)) {
        if (depth >= 4 || P->getNumIncomingValues() == 0) return nullptr;
        const Value *r = nullptr;
        for (const Value *in : P->incoming_values()) {
            const Value *x = in == P ? r : load_root(in, depth + 1);
            if (!x || (r && x != r)) return nullptr;
            r = x;
        }
        return r;
    }
    return nullptr;
}

bool same_operand(const Value *a, const Value *b) {
    if (a == b) return true;
    const Value *ra = load_root(a), *rb = ra ? load_root(b) : nullptr;
    return ra && ra == rb;
}

/* Would GVN fold `b` once `a` is known on the clone path? */
bool similar_cond(Value *a, Value *b) {
    auto *x = dyn_cast<ICmpInst>(a), *y = dyn_cast<ICmpInst>(b);
    if (!x || !y) return false;
    if (x->getPredicate() == y->getPredicate())
        return same_operand(x->getOperand(0), y->getOperand(0)) &&
               same_operand(x->getOperand(1), y->getOperand(1));
    if (x->getPredicate() == y->getSwappedPredicate())
        return same_operand(x->getOperand(0), y->getOperand(1)) &&
               same_operand(x->getOperand(1), y->getOperand(0));
    return false;
}

bool clonable(const BasicBlock *B) {
    if (B->hasAddressTaken()) return false;
    for (const Instruction &I : *B) {
        if (I.getType()->isTokenTy()) return false;
        if (I.isEHPad() && !isa<LandingPadInst>(I)) return false;
        if (isa<CallBrInst>(I) || isa<IndirectBrInst>(I)) return false;
        if (auto *CB = dyn_cast<CallBase>(&I))
            if (CB->cannotDuplicate() || CB->isConvergent()) return false;
    }
    return true;
}

struct Versioner {
    Function &F;
    GVStats &stats;
    unsigned budget;
    unsigned done_kind;

    Versioner(Function &f, GVStats &s, unsigned size)
        : F(f), stats(s), budget(std::max(1000u, size / 2)),
          done_kind(f.getContext().getMDKindID("zan.guard.done")) {}

    void note(const char *kind, unsigned size) {
        if (stats.verbose)
            fprintf(stderr, "guard-version: %s %s, %u insts\n", F.getName().str().c_str(), kind,
                    size);
    }

    /* Versioned guards and every branch of a slow world carry this mark; it
     * survives the cleanup passes that run between rounds. */
    bool is_done(Instruction *I) { return I->getMetadata(done_kind) != nullptr; }
    void mark_done(Instruction *I) { I->setMetadata(done_kind, MDNode::get(F.getContext(), {})); }

    /* One region per round, then GVN & co. so later guards see the clone's
     * collapsed phis (bounds checks re-test a reloaded length). */
    bool run(FunctionAnalysisManager &FAM) {
        bool changed = false;
        for (unsigned n = 0; n < kMaxVersionsPerFn; ++n) {
            bool did;
            {
                DominatorTree DT(F);
                LoopInfo LI(DT);
                did = step(DT, LI);
            }
            if (!did) break;
            changed = true;
            FAM.invalidate(F, PreservedAnalyses::none());
            /* The fast loop's guards now leave the loop: GVN folds repeats the
             * first test dominates, trivial unswitching hoists the invariant
             * ones, and with no calls left LICM can hoist and promote. */
            FunctionPassManager cleanup;
            cleanup.addPass(SimplifyCFGPass());
            cleanup.addPass(InstCombinePass());
            cleanup.addPass(GVNPass());
            LoopPassManager lpm;
            lpm.addPass(LICMPass(LICMOptions()));
            lpm.addPass(SimpleLoopUnswitchPass(false, true));
            cleanup.addPass(createFunctionToLoopPassAdaptor(std::move(lpm), true));
            cleanup.addPass(SimplifyCFGPass());
            cleanup.addPass(InstCombinePass());
            cleanup.run(F, FAM);
        }
        if (changed)
            for (BasicBlock &B : F)
                if (Instruction *T = B.getTerminator()) T->setMetadata(done_kind, nullptr);
        return changed;
    }

    /* All blocks of `L` if they can be cloned within budget. */
    bool loop_region(Loop *L, SmallVectorImpl<BasicBlock *> &region,
                     DenseSet<BasicBlock *> &in_region, unsigned *size) {
        *size = 0;
        for (BasicBlock *B : L->blocks()) {
            if (!clonable(B)) return false;
            *size += (unsigned)B->size();
            region.push_back(B);
            in_region.insert(B);
        }
        return *size <= kMaxRegionInsts && *size <= budget;
    }

    /* A guard whose condition does not change inside its loop: unswitch the
     * outermost such loop on it.  The preheader tests C once; the ok side
     * runs a clone of the loop with C folded, the other side the original
     * loop, which still checks (and reports) at the original site. */
    bool step_invariant(DominatorTree &DT, LoopInfo &LI,
                        ReversePostOrderTraversal<Function *> &rpot) {
        for (BasicBlock *H : rpot) {
            CondBr *BI = as_cond_br(H->getTerminator());
            if (!BI || is_done(BI)) continue;
            Value *C = BI->getCondition();
            Loop *L = LI.getLoopFor(H);
            if (!L || isa<Constant>(C) || !L->isLoopInvariant(C)) continue;
            int ok = ok_successor(BI);
            if (ok < 0) continue;
            Loop *best = nullptr;
            for (Loop *X = L; X && X->isLoopInvariant(C); X = X->getParentLoop()) {
                BasicBlock *P = X->getLoopPreheader();
                if (!P || !isa<Instruction>(P->getTerminator()) ||
                    P->getTerminator()->getNumSuccessors() != 1 || as_cond_br(P->getTerminator()))
                    continue;
                if (auto *CI = dyn_cast<Instruction>(C))
                    if (!DT.dominates(CI, P->getTerminator())) continue;
                SmallVector<BasicBlock *, 32> r;
                DenseSet<BasicBlock *> in;
                unsigned size;
                if (loop_region(X, r, in, &size)) best = X;
            }
            if (!best) continue;
            SmallVector<BasicBlock *, 32> region;
            DenseSet<BasicBlock *> in_region;
            unsigned size;
            loop_region(best, region, in_region, &size);
            bool is_eq = false;
            Value *V = null_tested(C, &is_eq);
            bool nonnull = V && (is_eq != (ok == 0));
            BasicBlock *P = best->getLoopPreheader();
            version(P, nullptr, C, ok, best->getHeader(), region, in_region, nonnull ? V : nullptr);
            note("unswitch", size);
            budget -= size;
            return true;
        }
        return false;
    }

    bool has_guard(Loop *L) {
        for (BasicBlock *B : L->blocks()) {
            CondBr *b = as_cond_br(B->getTerminator());
            if (!b || is_done(b)) continue;
            int ok = ok_successor(b);
            if (ok >= 0 && L->contains(b->getSuccessor(1 - ok))) return true;
        }
        return false;
    }

    /* Outermost loop that has live guards and fits the budget. */
    bool step_whole_loop(LoopInfo &LI) {
        SmallVector<Loop *, 8> work(LI.begin(), LI.end());
        while (!work.empty()) {
            Loop *L = work.pop_back_val();
            if (!has_guard(L)) continue;
            BasicBlock *P = L->getLoopPreheader();
            SmallVector<BasicBlock *, 32> region;
            DenseSet<BasicBlock *> in_region;
            unsigned size;
            if (P && P->getTerminator()->getNumSuccessors() == 1 &&
                !as_cond_br(P->getTerminator()) && loop_region(L, region, in_region, &size)) {
                version_loop(L, region, in_region);
                note("loop", size);
                budget -= size;
                return true;
            }
            work.append(L->begin(), L->end());
        }
        return false;
    }

    /* Whole-loop versioning: run a clone of the loop first; every guard in
     * the clone sends its failing edge to the same guard's report block in
     * the original loop, which then finishes that iteration and the rest of
     * the loop fully checked.  Execution is unchanged -- the original takes
     * over exactly where the first check fails -- but the clone has no
     * calls and no merges, so LLVM treats it like unchecked code. */
    void version_loop(Loop *L, SmallVectorImpl<BasicBlock *> &region,
                      DenseSet<BasicBlock *> &in_region) {
        BasicBlock *P = L->getLoopPreheader(), *Hd = L->getHeader();
        SmallVector<std::pair<CondBr *, unsigned>, 16> guards;
        for (BasicBlock *B : region) {
            CondBr *b = as_cond_br(B->getTerminator());
            if (!b || is_done(b)) continue;
            int ok = ok_successor(b);
            if (ok >= 0 && in_region.count(b->getSuccessor(1 - ok)))
                guards.push_back({b, (unsigned)(1 - ok)});
        }
        ValueToValueMapTy vmap;
        SmallVector<BasicBlock *, 32> clones;
        DenseSet<BasicBlock *> in_clone;
        clone_region(region, vmap, clones, in_clone);
        BasicBlock *NHd = cast<BasicBlock>(vmap[Hd]);
        fix_clone_phis(clones, in_clone, NHd, P);

        Hd->removePredecessor(P, true);
        P->getTerminator()->replaceSuccessorWith(Hd, NHd);
        fix_exits(region, clones, in_clone, vmap);

        /* Cross over right after each report: the clone keeps its own copy
         * of the report block (single predecessor, so a retain the late ARC
         * pass sinks off the hot edge has somewhere to go), and that block's
         * successors become the original loop's. */
        DenseMap<BasicBlock *, BasicBlock *> orig_of;
        for (unsigned k = 0; k < region.size(); ++k) orig_of[clones[k]] = region[k];
        SmallPtrSet<BasicBlock *, 16> crossed;
        for (auto &g : guards) {
            BasicBlock *obad = g.first->getSuccessor(g.second);
            BasicBlock *nbad = cast<BasicBlock>(vmap[obad]);
            if (!crossed.insert(nbad).second) continue;
            Instruction *t = nbad->getTerminator();
            SmallPtrSet<BasicBlock *, 4> seen;
            bool dup = false;
            for (BasicBlock *T : successors(nbad)) dup |= !seen.insert(T).second;
            if (dup) continue;
            for (unsigned i = 0, n = t->getNumSuccessors(); i < n; ++i) {
                BasicBlock *NT = t->getSuccessor(i);
                if (!in_clone.count(NT)) continue;
                BasicBlock *T = orig_of[NT];
                NT->removePredecessor(nbad, true);
                t->setSuccessor(i, T);
                for (PHINode &Ph : T->phis()) {
                    Value *v = Ph.getIncomingValueForBlock(obad);
                    auto it = vmap.find(v);
                    Ph.addIncoming(it != vmap.end() ? (Value *)it->second : v, nbad);
                }
            }
        }

        /* Original values no longer dominate the uses the cross edges reach. */
        DominatorTree DT2(F);
        for (BasicBlock *B : region) {
            for (Instruction &I : *B) {
                if (I.getType()->isVoidTy()) continue;
                SmallVector<Use *, 8> fix;
                for (Use &U : I.uses()) {
                    auto *UI = cast<Instruction>(U.getUser());
                    BasicBlock *UB = UI->getParent();
                    if (auto *Ph = dyn_cast<PHINode>(UI)) UB = Ph->getIncomingBlock(U);
                    if (!DT2.isReachableFromEntry(UB) || DT2.dominates(&I, U)) continue;
                    fix.push_back(&U);
                }
                kill_stray_dbg(I, [&](BasicBlock *RB) { return DT2.dominates(B, RB); });
                if (fix.empty()) continue;
                Value *NI = vmap[&I];
                SSAUpdater ssa;
                ssa.Initialize(I.getType(), I.getName());
                ssa.AddAvailableValue(B, &I);
                ssa.AddAvailableValue(cast<Instruction>(NI)->getParent(), NI);
                for (Use *U : fix) ssa.RewriteUse(*U);
            }
        }
        for (BasicBlock *B : region)
            if (CondBr *b = as_cond_br(B->getTerminator())) mark_done(b);
        stats.versions++;
        stats.loops++;
    }

    void clone_region(SmallVectorImpl<BasicBlock *> &region, ValueToValueMapTy &vmap,
                      SmallVectorImpl<BasicBlock *> &clones, DenseSet<BasicBlock *> &in_clone) {
        for (BasicBlock *B : region) {
            BasicBlock *NB = CloneBasicBlock(B, vmap, ".gv", &F);
            vmap[B] = NB;
            clones.push_back(NB);
            in_clone.insert(NB);
        }
        const RemapFlags rf = RF_IgnoreMissingLocals | RF_NoModuleLevelChanges;
        for (BasicBlock *NB : clones) {
            for (Instruction &I : *NB) {
                RemapInstruction(&I, vmap, rf);
#if LLVM_VERSION_MAJOR >= 19
                RemapDbgRecordRange(F.getParent(), I.getDbgRecordRange(), vmap, rf);
#endif
            }
        }
    }

    /* Clone phis keep only edges that exist in the clone (plus H -> NS). */
    void fix_clone_phis(SmallVectorImpl<BasicBlock *> &clones, DenseSet<BasicBlock *> &in_clone,
                        BasicBlock *NS, BasicBlock *H) {
        for (BasicBlock *NB : clones) {
            for (PHINode &Ph : NB->phis()) {
                for (int i = (int)Ph.getNumIncomingValues() - 1; i >= 0; --i) {
                    BasicBlock *IB = Ph.getIncomingBlock((unsigned)i);
                    if (in_clone.count(IB) || (NB == NS && IB == H)) continue;
                    Ph.removeIncomingValue((unsigned)i, false);
                }
            }
        }
    }

    /* Exit edges of the clone: extend phis outside the region. */
    void fix_exits(SmallVectorImpl<BasicBlock *> &region, SmallVectorImpl<BasicBlock *> &clones,
                   DenseSet<BasicBlock *> &in_clone, ValueToValueMapTy &vmap) {
        for (unsigned k = 0; k < region.size(); ++k) {
            BasicBlock *B = region[k], *NB = clones[k];
            SmallPtrSet<BasicBlock *, 4> seen;
            for (BasicBlock *T : successors(NB)) {
                if (in_clone.count(T) || !seen.insert(T).second) continue;
                for (PHINode &Ph : T->phis()) {
                    unsigned n = Ph.getNumIncomingValues();
                    for (unsigned i = 0; i < n; ++i) {
                        if (Ph.getIncomingBlock(i) != B) continue;
                        Value *v = Ph.getIncomingValue(i);
                        auto it = vmap.find(v);
                        Ph.addIncoming(it != vmap.end() ? (Value *)it->second : v, NB);
                    }
                }
            }
        }
    }

    template <typename Keep> void kill_stray_dbg(Instruction &I, Keep keep) {
#if LLVM_VERSION_MAJOR >= 21
        SmallVector<DbgVariableRecord *, 4> dbg;
        findDbgUsers(&I, dbg);
        for (DbgVariableRecord *R : dbg) {
            BasicBlock *RB = R->getParent();
            if (RB && !keep(RB)) R->setKillLocation();
        }
#elif LLVM_VERSION_MAJOR >= 19
        SmallVector<DbgVariableIntrinsic *, 4> dbg_insts;
        SmallVector<DbgVariableRecord *, 4> dbg;
        findDbgUsers(dbg_insts, &I, &dbg);
        for (DbgVariableIntrinsic *D : dbg_insts) {
            BasicBlock *RB = D->getParent();
            if (RB && !keep(RB)) D->setKillLocation();
        }
        for (DbgVariableRecord *R : dbg) {
            BasicBlock *RB = R->getParent();
            if (RB && !keep(RB)) R->setKillLocation();
        }
#else
        (void)I;
        (void)keep;
#endif
    }

    bool step(DominatorTree &DT, LoopInfo &LI) {
        ReversePostOrderTraversal<Function *> rpot(&F);
        if (step_whole_loop(LI)) return true;
        if (step_invariant(DT, LI, rpot)) return true;
        for (BasicBlock *H : rpot) {
            CondBr *BI = as_cond_br(H->getTerminator());
            if (!BI || is_done(BI)) continue;
            Value *C = BI->getCondition();
            if (isa<Constant>(C)) continue;
            Loop *L = LI.getLoopFor(H);
            if (!L) continue;
            int ok = ok_successor(BI);
            if (ok < 0) continue;
            BasicBlock *S = BI->getSuccessor(ok);
            if (S == H || !L->contains(S) || !DT.dominates(H, S)) continue;

            SmallVector<BasicBlock *, 32> region;
            DenseSet<BasicBlock *> in_region;
            unsigned size = 0;
            bool ok_region = true;
            for (BasicBlock *B : L->blocks()) {
                if (B == H || !DT.dominates(H, B)) continue;
                if (!clonable(B)) {
                    ok_region = false;
                    break;
                }
                size += (unsigned)B->size();
                region.push_back(B);
                in_region.insert(B);
            }
            if (!ok_region || size > kMaxRegionInsts || size > budget) {
                mark_done(BI);
                continue;
            }

            /* Worth it only if the region re-tests C or the same pointer. */
            bool is_eq = false;
            Value *V = null_tested(C, &is_eq);
            bool ok_val = ok == 0;
            bool nonnull = V && (is_eq != ok_val);
            bool benefit = false;
            for (User *U : C->users())
                if (auto *UI = dyn_cast<Instruction>(U))
                    if (in_region.count(UI->getParent())) benefit = true;
            if (!benefit) {
                for (BasicBlock *B : region) {
                    CondBr *b2 = as_cond_br(B->getTerminator());
                    if (b2 && b2->getCondition() != C && similar_cond(b2->getCondition(), C) &&
                        ok_successor(b2) >= 0) {
                        benefit = true;
                        break;
                    }
                }
            }
            if (!benefit && nonnull) {
                for (BasicBlock *B : region) {
                    for (Instruction &I : *B) {
                        bool e2;
                        if (&I != C && null_tested(&I, &e2) == V) {
                            benefit = true;
                            break;
                        }
                    }
                    if (benefit) break;
                }
            }
            if (!benefit) {
                mark_done(BI);
                continue;
            }
            version(H, BI, C, ok, S, region, in_region, nonnull ? V : nullptr);
            note("region", size);
            budget -= size;
            return true;
        }
        return false;
    }

    /* Clone `region` and enter the clone from H on C's ok edge.  BI is the
     * guard ending H whose ok successor S moves to the clone; with BI null, H
     * is a loop preheader ending in `br S` and gets a new `br C` instead. */
    void version(BasicBlock *H, CondBr *BI, Value *C, int ok, BasicBlock *S,
                 SmallVectorImpl<BasicBlock *> &region, DenseSet<BasicBlock *> &in_region,
                 Value *nonnull_v) {
        LLVMContext &ctx = F.getContext();
        ValueToValueMapTy vmap;
        SmallVector<BasicBlock *, 32> clones;
        DenseSet<BasicBlock *> in_clone;
        clone_region(region, vmap, clones, in_clone);
        BasicBlock *NS = cast<BasicBlock>(vmap[S]);
        fix_clone_phis(clones, in_clone, NS, H);

        /* Enter the clone from the ok edge. */
        if (BI) {
            S->removePredecessor(H);
            BI->setSuccessor((unsigned)ok, NS);
        } else {
            Instruction *old = H->getTerminator();
            BI = make_cond_br(C, ok == 0 ? NS : S, ok == 0 ? S : NS, old);
            BI->setDebugLoc(old->getDebugLoc());
            old->eraseFromParent();
        }

        fix_exits(region, clones, in_clone, vmap);

        /* Values defined in the region and used past it now have two defs. */
        for (BasicBlock *B : region) {
            for (Instruction &I : *B) {
                SmallVector<Use *, 8> outside;
                for (Use &U : I.uses()) {
                    auto *UI = cast<Instruction>(U.getUser());
                    BasicBlock *UB = UI->getParent();
                    if (auto *P = dyn_cast<PHINode>(UI)) UB = P->getIncomingBlock(U);
                    if (in_region.count(UB) || in_clone.count(UB)) continue;
                    outside.push_back(&U);
                }
                kill_stray_dbg(
                    I, [&](BasicBlock *RB) { return in_region.count(RB) || in_clone.count(RB); });
                if (outside.empty()) continue;
                Value *NI = vmap[&I];
                SSAUpdater ssa;
                ssa.Initialize(I.getType(), I.getName());
                ssa.AddAvailableValue(B, &I);
                ssa.AddAvailableValue(cast<Instruction>(NI)->getParent(), NI);
                for (Use *U : outside) ssa.RewriteUse(*U);
            }
        }

        /* Fold: inside the clone C has its ok value, and a pointer known
         * non-null tests non-null everywhere. */
        Constant *okc = ConstantInt::getBool(ctx, ok == 0);
        for (Use &U : make_early_inc_range(C->uses())) {
            auto *UI = dyn_cast<Instruction>(U.getUser());
            if (!UI) continue;
            BasicBlock *UB = UI->getParent();
            if (auto *P = dyn_cast<PHINode>(UI)) UB = P->getIncomingBlock(U);
            if (in_clone.count(UB) || (UB == H && UI->getParent() == NS)) {
                U.set(okc);
                stats.folded++;
            }
        }
        if (nonnull_v) {
            for (BasicBlock *NB : clones) {
                for (Instruction &I : make_early_inc_range(*NB)) {
                    bool e2;
                    if (null_tested(&I, &e2) != nonnull_v) continue;
                    I.replaceAllUsesWith(ConstantInt::getBool(ctx, !e2));
                    stats.folded++;
                }
            }
        }

        /* The original region is now the slow world: never version it. */
        mark_done(BI);
        for (BasicBlock *B : region)
            if (CondBr *b = as_cond_br(B->getTerminator())) mark_done(b);
        stats.versions++;
    }
};

bool module_has_guards(Module &M) {
    static const char *names[] = {"__zan_guard_raise", "zan_rt_guard_fail3", "zan_rt_soft_note"};
    for (const char *n : names)
        if (Function *F = M.getFunction(n))
            if (!F->use_empty()) return true;
    return false;
}

bool function_has_guards(Function &F, unsigned *size) {
    bool any = false;
    unsigned n = 0;
    for (BasicBlock &B : F) {
        n += (unsigned)B.size();
        if (!any && block_reports(&B)) any = true;
    }
    *size = n;
    return any;
}

struct GuardVersionPass : PassInfoMixin<GuardVersionPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
        if (!module_has_guards(M)) return PreservedAnalyses::all();
        FunctionAnalysisManager &FAM =
            MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
        GVStats stats;
        const char *st = getenv("ZAN_GUARD_VERSION_STATS");
        stats.verbose = st && st[0] == '2';
        bool any = false;
        for (Function &F : M) {
            if (F.isDeclaration() || F.hasOptNone() || F.hasOptSize()) continue;
            unsigned size = 0;
            if (!function_has_guards(F, &size) || size > kMaxFnInsts) continue;
            Versioner v(F, stats, size);
            if (!v.run(FAM)) continue;
            any = true;
            stats.functions++;
        }
        if (getenv("ZAN_GUARD_VERSION_STATS"))
            fprintf(
                stderr,
                "guard-version: %u regions versioned (%u whole loops) in %u functions, %u tests "
                "folded\n",
                stats.versions, stats.loops, stats.functions, stats.folded);
        return any ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

} // namespace

namespace {

struct PipelineEnv {
    PassInstrumentationCallbacks pic;
    PassBuilder pb;
    LoopAnalysisManager lam;
    FunctionAnalysisManager fam;
    CGSCCAnalysisManager cgam;
    ModuleAnalysisManager mam;
    StandardInstrumentations si;

    PipelineEnv(Module &M, TargetMachine *tm, const PipelineTuningOptions &pto)
        : pb(tm, pto, {}, &pic), si(M.getContext(), false, false) {}

    /* Same analyses and instrumentation as LLVMRunPasses, so optnone and
     * opt-bisect behave as on the plain text pipeline. */
    void init() {
        pb.registerLoopAnalyses(lam);
        pb.registerFunctionAnalyses(fam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerModuleAnalyses(mam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);
        si.registerCallbacks(pic, &mam);
    }
};

bool guard_version_enabled() { return !getenv("ZAN_NO_GUARD_VERSION"); }

} // namespace

extern "C" LLVMErrorRef zan_run_passes_guarded(LLVMModuleRef mod, const char *passes,
                                               LLVMTargetMachineRef tm,
                                               const zan_pipeline_opts_t *o) {
    Module *M = unwrap(mod);
    PipelineTuningOptions pto;
    if (o && o->tune_loops) {
        pto.LoopInterleaving = o->loop_interleaving;
        pto.LoopVectorization = o->loop_vectorization;
        pto.SLPVectorization = o->slp_vectorization;
        pto.LoopUnrolling = o->loop_unrolling;
    }
    PipelineEnv env(*M, reinterpret_cast<TargetMachine *>(tm), pto);
    if (o && o->guard_version && guard_version_enabled()) {
#if LLVM_VERSION_MAJOR >= 20
        env.pb.registerOptimizerEarlyEPCallback(
            [](ModulePassManager &mpm, OptimizationLevel, ThinOrFullLTOPhase) {
                mpm.addPass(GuardVersionPass());
            });
#else
        env.pb.registerOptimizerEarlyEPCallback(
            [](ModulePassManager &mpm, OptimizationLevel) { mpm.addPass(GuardVersionPass()); });
#endif
    }
    env.init();
    ModulePassManager mpm;
    if (Error err = env.pb.parsePassPipeline(mpm, passes)) return wrap(std::move(err));
    mpm.run(*M, env.mam);
    return LLVMErrorSuccess;
}

extern "C" void zan_guard_version_module(LLVMModuleRef mod, LLVMTargetMachineRef tm) {
    if (!guard_version_enabled()) return;
    Module *M = unwrap(mod);
    PipelineEnv env(*M, reinterpret_cast<TargetMachine *>(tm), PipelineTuningOptions());
    env.init();
    ModulePassManager mpm;
    mpm.addPass(GuardVersionPass());
    mpm.run(*M, env.mam);
}
