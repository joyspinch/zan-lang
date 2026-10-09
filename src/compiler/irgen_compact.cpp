/* irgen_compact.cpp -- Persistent, bounded function-local LLVM compaction. */

#include "irgen_compact.h"

#include <llvm/ADT/DenseSet.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Scalar/EarlyCSE.h>
#include <llvm/Transforms/Scalar/SROA.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Utils/SimplifyCFGOptions.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <string>

#if LLVM_VERSION_MAJOR < 15
#error "The function compactor requires LLVM 15 or newer"
#endif

namespace {

/* LLVM 15/16 keep DT/AC and instruction worklists inside SROAPass itself.
 * Clearing FAM alone would leave dangling pointers in a persistent FPM. Keep
 * the FPM pass stateless and destroy the native pass before clearing analyses.
 * Newer LLVM also works with this lifetime, without any pipeline parsing. */
struct CompactSROAPass : llvm::PassInfoMixin<CompactSROAPass> {
    llvm::PreservedAnalyses run(llvm::Function &fn,
                                llvm::FunctionAnalysisManager &fam) {
#if LLVM_VERSION_MAJOR >= 16
        llvm::SROAPass pass(llvm::SROAOptions::PreserveCFG);
#else
        llvm::SROAPass pass;
#endif
        return pass.run(fn, fam);
    }
};

/* SimplifyCFG stores the current AssumptionCache in its options during run.
 * A local native pass prevents that pointer surviving FAM.clear(). Restrict
 * this early cleanup to compacting CFGs: no lookup-table globals, speculative
 * block duplication, common-instruction motion or loop expansion pipeline. */
struct CompactSimplifyCFGPass : llvm::PassInfoMixin<CompactSimplifyCFGPass> {
    llvm::PreservedAnalyses run(llvm::Function &fn,
                                llvm::FunctionAnalysisManager &fam) {
        llvm::SimplifyCFGOptions options;
        options.bonusInstThreshold(0)
            .forwardSwitchCondToPhi(false)
            .convertSwitchRangeToICmp(false)
            .convertSwitchToLookupTable(false)
            .needCanonicalLoops(false)
            .hoistCommonInsts(false)
            .sinkCommonInsts(false);
#if LLVM_VERSION_MAJOR >= 17
        options.speculateBlocks(false);
#else
        options.setFoldTwoEntryPHINode(false);
#endif
        llvm::SimplifyCFGPass pass(options);
        return pass.run(fn, fam);
    }
};

static void compact_clear_error(char *errbuf, size_t errbuf_size) {
    if (errbuf && errbuf_size) errbuf[0] = '\0';
}

static int compact_error(char *errbuf, size_t errbuf_size,
                          const char *message) {
    if (errbuf && errbuf_size) {
        std::snprintf(errbuf, errbuf_size, "%s", message);
    } else {
        std::fprintf(stderr, "error: LLVM function compactor: %s\n", message);
    }
    return 0;
}

static int compact_verify(llvm::Function &fn, const char *stage,
                           char *errbuf, size_t errbuf_size) {
    std::string detail;
    llvm::raw_string_ostream out(detail);
    if (!llvm::verifyFunction(fn, &out)) return 1;
    out.flush();
    std::string message = std::string(stage) + " function '" +
                          fn.getName().str() + "': " + detail;
    return compact_error(errbuf, errbuf_size, message.c_str());
}

} /* namespace */

struct zan_irgen_compactor {
    llvm::Module *module;
    /* Declaration order follows LLVM's proxy lifetime requirement: destruction
     * runs outer managers before the inner managers they reference. */
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    llvm::PassBuilder pass_builder;
    llvm::FunctionPassManager fpm;
    /* Stable function identities only: this O(functions) bookkeeping lets a
     * final support sweep skip immediate completion points. It owns no IR,
     * blocks, instructions or analyses and dies before the borrowed module. */
    llvm::DenseSet<llvm::Function *> completed;

    explicit zan_irgen_compactor(llvm::Module *mod) : module(mod) {}

    void clear_analyses() {
        /* Clear outer proxies while every inner manager is still alive. This
         * removes result-map keys as well as instruction-bearing results, even
         * if the emitter has added functions/globals or moved bodies to shards.
         * Registrations and the three-pass pipeline remain reusable. */
        mam.clear();
        cgam.clear();
        fam.clear();
        lam.clear();
    }

    ~zan_irgen_compactor() { clear_analyses(); }
};

namespace {

/* Also clear on verifier failure or a C++ exception; no run leaves a cached
 * Function, BasicBlock, instruction, loop or call-graph result behind. */
struct CompactAnalysisScope {
    zan_irgen_compactor_t *compactor;
    explicit CompactAnalysisScope(zan_irgen_compactor_t *c) : compactor(c) {}
    ~CompactAnalysisScope() { compactor->clear_analyses(); }
};

} /* namespace */

extern "C" zan_irgen_compactor_t *zan_irgen_compactor_create(
    LLVMModuleRef module, char *errbuf, size_t errbuf_size) {
    compact_clear_error(errbuf, errbuf_size);
    if (!module) {
        compact_error(errbuf, errbuf_size, "cannot create with a NULL module");
        return nullptr;
    }
    llvm::Module *mod = llvm::unwrap(module);
    if (mod->getDataLayoutStr().empty()) {
        compact_error(errbuf, errbuf_size,
                      "set the target module DataLayout before creating the compactor");
        return nullptr;
    }
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
#endif
        std::unique_ptr<zan_irgen_compactor_t> compactor(
            new (std::nothrow) zan_irgen_compactor(mod));
        if (!compactor) {
            compact_error(errbuf, errbuf_size, "cannot allocate the compactor");
            return nullptr;
        }
        llvm::PassBuilder &pb = compactor->pass_builder;
        pb.registerModuleAnalyses(compactor->mam);
        pb.registerCGSCCAnalyses(compactor->cgam);
        pb.registerFunctionAnalyses(compactor->fam);
        pb.registerLoopAnalyses(compactor->lam);
        pb.crossRegisterProxies(compactor->lam, compactor->fam,
                                 compactor->cgam, compactor->mam);

        /* Build once, then run directly on one Function. SROA supplies SSA
         * promotion using LLVM's legality checks (including volatile/EH slots);
         * no custom mem2reg, inlining, module adaptor or GlobalDCE is involved.
         * EarlyCSE's MemorySSA mode is intentionally disabled for bounded cost. */
        compactor->fpm.addPass(CompactSROAPass());
        compactor->fpm.addPass(llvm::EarlyCSEPass(false));
        compactor->fpm.addPass(CompactSimplifyCFGPass());
        return compactor.release();
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    } catch (const std::exception &err) {
        compact_error(errbuf, errbuf_size, err.what());
    } catch (...) {
        compact_error(errbuf, errbuf_size,
                      "unknown C++ exception while creating the compactor");
    }
    return nullptr;
#endif
}

extern "C" int zan_irgen_compactor_run(zan_irgen_compactor_t *compactor,
                                        LLVMValueRef function,
                                        char *errbuf, size_t errbuf_size) {
    compact_clear_error(errbuf, errbuf_size);
    if (!compactor)
        return compact_error(errbuf, errbuf_size, "cannot run a NULL compactor");
    if (!function)
        return compact_error(errbuf, errbuf_size, "cannot compact a NULL function");
    llvm::Function *fn = llvm::dyn_cast<llvm::Function>(llvm::unwrap(function));
    if (!fn)
        return compact_error(errbuf, errbuf_size, "value is not an LLVM function");
    if (fn->getParent() != compactor->module)
        return compact_error(errbuf, errbuf_size,
                              "function belongs to a different module");
    if (compactor->module->getDataLayoutStr().empty())
        return compact_error(errbuf, errbuf_size, "module DataLayout was cleared");
    if (fn->isDeclaration() || fn->hasOptNone()) return 1;
    if (compactor->completed.count(fn)) return 1;

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
#endif
        CompactAnalysisScope scope(compactor);
        if (!compact_verify(*fn, "invalid input IR in", errbuf, errbuf_size))
            return 0;
        /* Materialize only the module->function proxy, not a module analysis
         * pipeline. The registered inverse proxy is available to function
         * analyses such as AAManager without retaining stale module results. */
        compactor->mam.getResult<llvm::FunctionAnalysisManagerModuleProxy>(
            *compactor->module);
        compactor->fpm.run(*fn, compactor->fam);
        if (!compact_verify(*fn, "invalid compacted IR in", errbuf, errbuf_size))
            return 0;
        compactor->completed.insert(fn);
        return 1;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    } catch (const std::exception &err) {
        return compact_error(errbuf, errbuf_size, err.what());
    } catch (...) {
        return compact_error(errbuf, errbuf_size,
                              "unknown C++ exception while compacting a function");
    }
#endif
}

extern "C" void zan_irgen_compactor_destroy(zan_irgen_compactor_t *compactor) {
    delete compactor;
}
