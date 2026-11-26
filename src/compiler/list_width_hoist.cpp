/* list_width_hoist.cpp -- hoist List<T> slot-width tag checks out of loops.
 *
 * Every specialised List<T> element access first checks the header's width
 * tag (irgen_list_slots.c, list_data_for):
 *
 *     X:    %esz  = load i64, ptr (lp + 24)
 *           %same = icmp eq i64 %esz, C
 *           br i1 %same, label %ok, label %fix
 *     fix:  call @__zan_list_repack(lp[, C])     ; rare: erased/specialised seam
 *           br label %ok
 *
 * Inside a loop the cold repack call clobbers the header, so LLVM must reload
 * the tag, the data pointer and the count on every iteration and the loop can
 * neither be vectorised nor keep its pointers in registers: List<int> index
 * and Add loops ran 1.6x slower than std::vector, almost all of it this check.
 *
 * Only two things ever change a live list's tag: a repack call, and the tag
 * store in a list's own construction (a fresh object, never a list that is
 * already live across the loop). So when every call in the loop is known not
 * to repack lp to another width -- runtime helpers that run no user code,
 * read-only callees, repacks to the same C -- the tag is C after the first
 * check executes and stays C. This pass then performs the check once in the
 * preheader and folds the in-loop branch to %ok; a cleanup pipeline removes
 * the dead repack block and lets LICM/the vectoriser see a call-free loop.
 * Repacking earlier than the original program would is semantically
 * invisible (contents are unchanged, only the slot width), so the only
 * requirement on placement is that lp is valid there: the check block must
 * dominate every latch and exit, i.e. the first iteration executes it.
 */

#include "list_width_hoist.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Transforms/Utils/LoopUtils.h>


namespace {

constexpr int64_t kEszOffset = 24; /* List header: {count, cap, data, esz} */

/* LLVM 23 split BranchInst into CondBrInst/UncondBrInst and deprecated the
 * old accessors; keep one spelling per version. */
bool is_br(const llvm::Instruction *t, unsigned succs) {
#if LLVM_VERSION_MAJOR >= 23
    return succs == 2 ? llvm::isa<llvm::CondBrInst>(t) : llvm::isa<llvm::UncondBrInst>(t);
#else
    auto *b = llvm::dyn_cast<llvm::BranchInst>(t);
    return b && b->getNumSuccessors() == succs;
#endif
}

bool is_repack(const llvm::Function *fn) {
    return fn && fn->getName().starts_with("__zan_list_repack");
}

/* Runtime helpers that never run user code and never touch a list header. */
bool is_inert_runtime(llvm::StringRef n) {
    static const char *const exact[] = {"__zan_guard_raise",
                                        "__zan_eh_state_tsd",
                                        "zan_rt_guard_fail2",
                                        "zan_rt_guard_fail3",
                                        "zan_rt_soft_scratch",
                                        "zan_rt_alloc",
                                        "zan_rt_retain",
                                        "zan_rt_arr_retain",
                                        "zan_rt_live_register",
                                        "zan_rt_live_unregister",
                                        "malloc",
                                        "calloc",
                                        "realloc",
                                        "free",
                                        "puts",
                                        "printf",
                                        "exit",
                                        "abort",
                                        "__cxa_allocate_exception",
                                        "__cxa_throw",
                                        "zan_rt_println",
                                        nullptr};
    for (int i = 0; exact[i]; i++)
        if (n == exact[i]) return true;
    /* String helpers: strings have no finaliser, so even their release runs
     * no user code; prints and soft notes only write the runtime's buffers. */
    return n.starts_with("zan_rt_str_") || n.starts_with("zan_rt_print_") ||
           n.starts_with("zan_rt_soft_note") || n.starts_with("zan_mem_");
}

struct Check {
    llvm::BasicBlock *x = nullptr; /* block ending in the tag branch */
    llvm::Instruction *br = nullptr; /* conditional branch ending x */
    llvm::LoadInst *load = nullptr; /* %esz */
    llvm::CallBase *repack = nullptr; /* call in the fix block */
    llvm::BasicBlock *ok = nullptr;
    uint64_t want = 0;
};

const llvm::Value *strip(const llvm::Value *v, const llvm::DataLayout &dl, int64_t &off) {
    llvm::APInt ap(dl.getIndexTypeSizeInBits(v->getType()), 0);
    const llvm::Value *base = v->stripAndAccumulateConstantOffsets(dl, ap, true);
    off = ap.getSExtValue();
    return base;
}

/* Match the tag-check shape around a repack call. The repack must open the
 * block taken when the tag differs; what follows it there does not matter.
 * Jump threading often merges that block with the code after the check
 * (foreach's collection-modified test and the element load), and once the
 * check is hoisted the block is simply unreachable: with the tag equal to C
 * the in-loop branch always took %ok anyway. */
bool match_check(llvm::CallBase *call, const llvm::DataLayout &dl, Check &out) {
    if (call->arg_size() < 1) return false;
    llvm::BasicBlock *fix = call->getParent();
    if (&*fix->getFirstNonPHIOrDbg() != call) return false;
    llvm::BasicBlock *x = fix->getSinglePredecessor();
    if (!x) return false;
    llvm::Instruction *br = x->getTerminator();
    if (!br || !is_br(br, 2)) return false;
    auto *cmp = llvm::dyn_cast<llvm::ICmpInst>(br->getOperand(0));
    if (!cmp || !cmp->isEquality()) return false;
    auto *load = llvm::dyn_cast<llvm::LoadInst>(cmp->getOperand(0));
    auto *cst = llvm::dyn_cast<llvm::ConstantInt>(cmp->getOperand(1));
    if (!load || !cst || load->isVolatile() || !load->getType()->isIntegerTy(64)) return false;
    /* eq: true → ok, false → fix; ne: the other way round */
    bool eq = cmp->getPredicate() == llvm::ICmpInst::ICMP_EQ;
    llvm::BasicBlock *ok = br->getSuccessor(eq ? 0 : 1);
    if (br->getSuccessor(eq ? 1 : 0) != fix || ok == fix) return false;
    if (call->arg_size() >= 2) {
        auto *w = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(1));
        if (!w || w->getZExtValue() != cst->getZExtValue()) return false;
    }
    int64_t off_load = 0, off_list = 0;
    const llvm::Value *b1 = strip(load->getPointerOperand(), dl, off_load);
    const llvm::Value *b2 = strip(call->getArgOperand(0), dl, off_list);
    if (b1 != b2 || off_load != off_list + kEszOffset) return false;
    out.x = x;
    out.br = br;
    out.load = load;
    out.repack = call;
    out.ok = ok;
    out.want = cst->getZExtValue();
    return true;
}

/* Can any call in the loop move a live list to a width other than `want`? */
bool loop_keeps_width(llvm::Loop *loop, uint64_t want, const llvm::DataLayout &dl) {
    for (llvm::BasicBlock *bb : loop->blocks()) {
        for (llvm::Instruction &inst : *bb) {
            auto *call = llvm::dyn_cast<llvm::CallBase>(&inst);
            if (!call) continue;
            if (call->onlyReadsMemory()) continue;
            if (auto *ii = llvm::dyn_cast<llvm::IntrinsicInst>(call)) {
                if (llvm::isa<llvm::MemIntrinsic>(ii)) return false;
                continue; /* lifetime/assume/expect/arith: no header writes */
            }
            llvm::Function *callee = call->getCalledFunction();
            if (!callee) return false; /* indirect: may run anything */
            if (is_repack(callee)) {
                Check other;
                if (call->arg_size() >= 2) {
                    auto *w = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(1));
                    if (!w || w->getZExtValue() != want) return false;
                } else if (!match_check(call, dl, other) || other.want != want) {
                    return false;
                }
                continue;
            }
            if (!is_inert_runtime(callee->getName())) return false;
        }
    }
    return true;
}

/* Make v available in the preheader (it must be loop-invariant). */
bool invariant_operand(llvm::Loop *loop, llvm::Value *v) {
    bool changed = false;
    return loop->makeLoopInvariant(v, changed);
}

/* Does `ph` (or a block on its single-predecessor chain) load from the
 * list that check `c` repacks? */
bool list_read_before(llvm::BasicBlock *ph, const Check &c, const llvm::DataLayout &dl) {
    int64_t off = 0;
    const llvm::Value *list = strip(c.repack->getArgOperand(0), dl, off);
    llvm::BasicBlock *b = ph;
    for (int depth = 0; b && depth < 4; depth++, b = b->getSinglePredecessor()) {
        for (llvm::Instruction &inst : *b) {
            auto *ld = llvm::dyn_cast<llvm::LoadInst>(&inst);
            if (!ld || ld->isVolatile()) continue;
            int64_t o = 0;
            if (strip(ld->getPointerOperand(), dl, o) == list) return true;
        }
    }
    return false;
}

bool hoist_one(llvm::Function &fn, const llvm::DataLayout &dl) {
    llvm::SmallVector<llvm::CallBase *, 8> calls;
    for (llvm::BasicBlock &bb : fn)
        for (llvm::Instruction &inst : bb)
            if (auto *call = llvm::dyn_cast<llvm::CallBase>(&inst))
                if (is_repack(call->getCalledFunction())) calls.push_back(call);
    if (calls.empty()) return false;
    llvm::DominatorTree dt(fn);
    llvm::LoopInfo li(dt);
    for (llvm::CallBase *call : calls) {
        Check c;
        if (!llvm::isa<llvm::CallInst>(call) || !match_check(call, dl, c)) continue;
        llvm::Loop *loop = li.getLoopFor(c.x);
        if (!loop || li.getLoopFor(call->getParent()) != loop) continue;
        llvm::BasicBlock *ph = loop->getLoopPreheader();
        /* The first iteration must run the check: then lp is valid in the
         * preheader exactly as it is at the check. */
        llvm::SmallVector<llvm::BasicBlock *, 4> exits, latches;
        loop->getExitingBlocks(exits);
        loop->getLoopLatches(latches);
        bool dominated = true;
        for (llvm::BasicBlock *b : exits) {
            /* An exit into a dead end (OOM puts+exit, ...) never comes back. */
            bool live_exit = false;
            for (llvm::BasicBlock *succ : llvm::successors(b))
                if (!loop->contains(succ) &&
                    !llvm::isa<llvm::UnreachableInst>(succ->getTerminator()))
                    live_exit = true;
            if (live_exit) dominated &= dt.dominates(c.x, b);
        }
        for (llvm::BasicBlock *b : latches) dominated &= dt.dominates(c.x, b);
        /* An unrotated loop (foreach: the header tests "any left?" before the
         * body) need not run the check on a zero-trip entry. Hoisting is still
         * sound when the list is already dereferenced on the way into the
         * loop (foreach loads Count from it in the preheader): lp is valid
         * there, and repacking early only changes the slot width. */
        if (!dominated && !(ph && list_read_before(ph, c, dl))) continue;
        if (!loop_keeps_width(loop, c.want, dl)) continue;
        if (!ph) ph = llvm::InsertPreheaderForLoop(loop, &dt, &li, nullptr, false);
        if (!ph) continue;
        if (!invariant_operand(loop, c.load->getPointerOperand())) continue;
        bool args_ok = true;
        for (llvm::Value *arg : call->args()) args_ok &= invariant_operand(loop, arg);
        if (!args_ok) continue;

        /* preheader: if (esz != want) repack(lp, want) */
        llvm::Instruction *term = ph->getTerminator();
        llvm::IRBuilder<> b(term);
        llvm::LoadInst *esz =
            b.CreateLoad(c.load->getType(), c.load->getPointerOperand(), "lw.esz.hoist");
        esz->setAlignment(c.load->getAlign());
        llvm::Value *differs =
            b.CreateICmpNE(esz, llvm::ConstantInt::get(esz->getType(), c.want), "lw.differs");
        llvm::Instruction *then = llvm::SplitBlockAndInsertIfThen(differs, term, false);
        llvm::Instruction *clone = call->clone();
        clone->insertBefore(then->getIterator());
        /* in the loop the tag is already right: take the %ok edge */
        llvm::BasicBlock *fix_bb = call->getParent();
        llvm::IRBuilder<>(c.br).CreateBr(c.ok);
        fix_bb->removePredecessor(c.x);
        c.br->eraseFromParent();
        return true;
    }
    return false;
}

} // namespace

extern "C" unsigned zan_opt_hoist_list_width_checks(LLVMModuleRef mod_ref) {
    llvm::Module *mod = llvm::unwrap(mod_ref);
    bool any_repack = false;
    for (llvm::Function &fn : *mod) any_repack |= is_repack(&fn);
    if (!any_repack) return 0;
    const llvm::DataLayout &dl = mod->getDataLayout();
    unsigned hoisted = 0;
    for (llvm::Function &fn : *mod) {
        if (fn.isDeclaration() || is_repack(&fn)) continue;
        /* One hoist per round (analyses are rebuilt); a hoisted check that
         * now sits in an outer loop body can be lifted again next round. */
        for (int round = 0; round < 64 && hoist_one(fn, dl); round++) hoisted++;
    }
    return hoisted;
}
