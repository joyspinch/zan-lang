/* codegen_split.cpp -- split an optimized module and emit the parts in
 * parallel. See codegen_split.h. */

#include "codegen_split.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
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

extern "C" int zan_codegen_split_emit(LLVMModuleRef mod_ref, LLVMTargetMachineRef tm_ref, int parts,
                                      int threads, const char *const *paths, char **error) {
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
    mod.reset();

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
    if (!job.error.empty()) {
        *error = strdup(job.error.c_str());
        return -1;
    }
    return 0;
}
