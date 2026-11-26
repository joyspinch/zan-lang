/* codegen_split.cpp -- the default<ON> pipeline in two halves, and parallel
 * optimization + emission of module partitions. See codegen_split.h. */

#include "codegen_split.h"
#include "procmem.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/IPO/Annotation2Metadata.h>
#include <llvm/Transforms/IPO/ForceFunctionAttrs.h>
#include <llvm/Transforms/Utils/SplitModule.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

void trace_mem(const char *what) {
    static const bool on = getenv("ZAN_SPLIT_TRACE") != nullptr;
    if (!on) return;
    zan_procmem_t m = zan_procmem_read();
    fprintf(stderr, "split: %-28s commit %5zu MB  peak %5zu MB\n", what, m.commit >> 20,
            m.peak >> 20);
}

/* One half of buildPerModuleDefaultPipeline, run the way LLVMRunPasses runs
 * a text pipeline (same tuning options, analyses and instrumentation, so
 * optnone and opt-bisect behave the same). */
void run_half(llvm::Module &m, llvm::TargetMachine *tm, int level, int vectorize, bool simplify) {
    llvm::OptimizationLevel ol = level <= 1   ? llvm::OptimizationLevel::O1
                                 : level == 2 ? llvm::OptimizationLevel::O2
                                              : llvm::OptimizationLevel::O3;
    llvm::PipelineTuningOptions pto;
    if (vectorize) {
        pto.LoopInterleaving = true;
        pto.LoopVectorization = true;
        pto.SLPVectorization = true;
        pto.LoopUnrolling = true;
    }
    llvm::PassInstrumentationCallbacks pic;
    llvm::PassBuilder pb(tm, pto, std::nullopt, &pic);
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    llvm::StandardInstrumentations si(m.getContext(), false, false);
    si.registerCallbacks(pic, &mam);
    llvm::ModulePassManager mpm;
    if (simplify) {
        mpm.addPass(llvm::Annotation2MetadataPass());
        mpm.addPass(llvm::ForceFunctionAttrsPass());
        pb.invokePipelineStartEPCallbacks(mpm, ol);
        mpm.addPass(pb.buildModuleSimplificationPipeline(ol, llvm::ThinOrFullLTOPhase::None));
    } else {
        mpm.addPass(pb.buildModuleOptimizationPipeline(ol, llvm::ThinOrFullLTOPhase::None));
    }
    mpm.run(m, mam);
}

struct split_job_t {
    const llvm::Target *target;
    llvm::Triple triple;
    std::string cpu, features;
    llvm::TargetOptions options;
    llvm::Reloc::Model reloc;
    llvm::CodeModel::Model code_model;
    llvm::CodeGenOptLevel opt_level;
    std::vector<llvm::SmallVector<char, 0>> bitcode;
    const char *const *paths;
    zan_split_opts_t opts;
    std::atomic<int> next{0};
    std::mutex err_mu;
    std::string error;

    void fail(const std::string &msg) {
        std::lock_guard<std::mutex> lock(err_mu);
        if (error.empty()) error = msg;
    }

    void emit_one(int i) {
        llvm::LLVMContext ctx;
        llvm::MemoryBufferRef buf(llvm::StringRef(bitcode[i].data(), bitcode[i].size()),
                                  "zan-codegen-part");
        auto mod = llvm::parseBitcodeFile(buf, ctx);
        if (!mod) {
            fail("cannot reload codegen partition: " + llvm::toString(mod.takeError()));
            return;
        }
        /* The serialized copy is all this thread needs; drop it early. */
        llvm::SmallVector<char, 0>().swap(bitcode[i]);
#if LLVM_VERSION_MAJOR >= 21
        std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
            triple, cpu, features, options, reloc, code_model, opt_level));
#else
        std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
            triple.str(), cpu, features, options, reloc, code_model, opt_level));
#endif
        if (!tm) {
            fail("cannot create target machine for codegen partition");
            return;
        }
        if (opts.finish_level > 0) {
            run_half(**mod, tm.get(), opts.finish_level, opts.vectorize, false);
            std::string msg;
            llvm::raw_string_ostream os(msg);
            if (llvm::verifyModule(**mod, &os)) {
                fail("module verification failed after optimizing a partition: " + msg);
                return;
            }
        }
        if (opts.prepare) opts.prepare(opts.prepare_ctx, llvm::wrap(mod->get()));
        std::error_code ec;
        llvm::raw_fd_ostream out(paths[i], ec, llvm::sys::fs::OF_None);
        if (ec) {
            fail(std::string("cannot open ") + paths[i] + ": " + ec.message());
            return;
        }
        llvm::legacy::PassManager pm;
        if (tm->addPassesToEmitFile(pm, out, nullptr, llvm::CodeGenFileType::ObjectFile)) {
            fail("target cannot emit object files");
            return;
        }
        pm.run(**mod);
        out.flush();
        if (out.has_error()) fail(std::string("write failed: ") + paths[i]);
    }

    void worker() {
        for (;;) {
            int i = next.fetch_add(1);
            if (i >= (int)bitcode.size()) return;
            emit_one(i);
        }
    }
};

} // namespace

extern "C" void zan_opt_simplify_module(LLVMModuleRef mod, LLVMTargetMachineRef tm, int level,
                                        int vectorize) {
    run_half(*llvm::unwrap(mod), reinterpret_cast<llvm::TargetMachine *>(tm), level, vectorize,
             true);
}

extern "C" void zan_opt_finish_module(LLVMModuleRef mod, LLVMTargetMachineRef tm, int level,
                                      int vectorize) {
    run_half(*llvm::unwrap(mod), reinterpret_cast<llvm::TargetMachine *>(tm), level, vectorize,
             false);
}

extern "C" int zan_codegen_split_emit(LLVMModuleRef mod_ref, LLVMTargetMachineRef tm_ref, int parts,
                                      int threads, const zan_split_opts_t *opts,
                                      const char *const *paths, char **error) {
    *error = nullptr;
    std::unique_ptr<llvm::Module> mod(llvm::unwrap(mod_ref));
    auto *tm = reinterpret_cast<llvm::TargetMachine *>(tm_ref);
    split_job_t job;
    job.target = &tm->getTarget();
    job.triple = tm->getTargetTriple();
    job.cpu = tm->getTargetCPU().str();
    job.features = tm->getTargetFeatureString().str();
    job.options = tm->Options;
    job.reloc = tm->getRelocationModel();
    job.code_model = tm->getCodeModel();
    job.opt_level = tm->getOptLevel();
    job.paths = paths;
    if (opts) job.opts = *opts;
    else memset(&job.opts, 0, sizeof(job.opts));

    if (job.opts.finish_level > 0) {
        /* The finish pipeline ends in GlobalDCE, which would delete a
         * linkonce definition its own partition no longer uses while other
         * partitions still reference it (they only see a declaration).
         * weak keeps the same merge semantics without being discardable. */
        for (llvm::GlobalValue &gv : mod->global_values()) {
            if (gv.isDeclaration()) continue;
            if (gv.hasLinkOnceODRLinkage()) gv.setLinkage(llvm::GlobalValue::WeakODRLinkage);
            else if (gv.hasLinkOnceAnyLinkage()) gv.setLinkage(llvm::GlobalValue::WeakAnyLinkage);
        }
    }
    trace_mem("before split");
    int produced = 0;
    llvm::SplitModule(
        *mod, (unsigned)parts,
        [&](std::unique_ptr<llvm::Module> part) {
            if (produced >= parts) return;
            job.bitcode.emplace_back();
            llvm::raw_svector_ostream os(job.bitcode.back());
            llvm::WriteBitcodeToFile(*part, os);
            produced++;
        },
        /*PreserveLocals=*/false);
    trace_mem("partitions serialized");
    mod.reset();
    trace_mem("input module freed");

    /* SplitModule may produce fewer non-empty partitions than requested; it
     * still calls back once per partition, so the counts agree. */
    if (produced != parts) {
        *error = strdup("codegen split produced an unexpected partition count");
        return -1;
    }
    if (threads < 1) threads = 1;
    if (threads > parts) threads = parts;
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; t++) pool.emplace_back([&job] { job.worker(); });
    job.worker();
    for (auto &th : pool) th.join();
    trace_mem("partitions emitted");
    if (!job.error.empty()) {
        *error = strdup(job.error.c_str());
        return -1;
    }
    return 0;
}
