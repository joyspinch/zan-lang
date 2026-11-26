/* build_cache.cpp -- content-addressed memo of the LLVM backend.
 * See build_cache.h and docs/BUILD_CACHE.md. */

#include "build_cache.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/TypeFinder.h>
#include <llvm/Support/BLAKE3.h>
#include <llvm/Support/CachePruning.h>
#include <llvm/Support/Chrono.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#ifdef _WIN32
#include <stdlib.h>
#define ZAN_BCACHE_ENVIRON _environ
#else
extern char **environ;
#define ZAN_BCACHE_ENVIRON environ
#endif

/* Bump when the entry format or key recipe changes. */
#define ZAN_BCACHE_FORMAT "zan-bcache-v1"

namespace {

using Digest = std::array<uint8_t, 32>;

struct state_t {
    bool enabled = false;
    Digest context{};
    llvm::SmallString<256> dir;
    bool dir_ready = false;
    bool warned_untrusted = false;
    bool capturing = false;
    std::string capture;
    int opt_hit = 0, opt_miss = 0, obj_hit = 0, obj_miss = 0, unit_hit = 0, unit_miss = 0;
    double ms_key = 0, ms_load = 0, ms_store = 0;
};

state_t g_bc;

double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/* Streams the bitcode writer straight into the hasher: no module-sized
 * buffer is materialized just to compute a key. */
class hash_ostream final : public llvm::raw_ostream {
    llvm::BLAKE3 &h_;
    uint64_t pos_ = 0;
    void write_impl(const char *p, size_t n) override {
        h_.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(p), n));
        pos_ += n;
    }
    uint64_t current_pos() const override { return pos_; }

  public:
    explicit hash_ostream(llvm::BLAKE3 &h) : h_(h) { SetBufferSize(1 << 16); }
    ~hash_ostream() override { flush(); }
};

void hash_str(llvm::BLAKE3 &h, llvm::StringRef s) {
    uint64_t n = s.size();
    h.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(&n), sizeof(n)));
    h.update(s);
}

void hash_u64(llvm::BLAKE3 &h, uint64_t v) {
    h.update(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(&v), sizeof(v)));
}

bool env_off(const char *name) {
    const char *v = getenv(name);
    return v && (!strcmp(v, "0") || !strcmp(v, "off") || !strcmp(v, "false"));
}

bool env_set(const char *name) {
    const char *v = getenv(name);
    return v && v[0];
}

/* Cache entries are linked into the program unverified (they are keyed by
 * the input, so a digest beside them proves nothing to someone who can write
 * both). What makes them trustworthy is that only this user can write them:
 * the directory is created owner-only, an existing one must be owned by us
 * and not group/other-writable (we tighten our own), and every entry read
 * must be a regular file with the same property. Windows: the default
 * location is per-user under %LOCALAPPDATA% and ACL-protected; checks below
 * are POSIX-only. */
#if !defined(_WIN32)
bool owned_private(const llvm::Twine &path, bool want_dir, bool fix) {
    llvm::sys::fs::file_status st;
    if (llvm::sys::fs::status(path, st, /*follow=*/false)) return false;
    if (want_dir ? st.type() != llvm::sys::fs::file_type::directory_file
                 : st.type() != llvm::sys::fs::file_type::regular_file)
        return false;
    if (st.getUser() != (uint32_t)geteuid()) return false;
    unsigned shared = llvm::sys::fs::group_write | llvm::sys::fs::others_write;
    if (st.permissions() & shared) {
        if (!fix) return false;
        if (llvm::sys::fs::setPermissions(path, llvm::sys::fs::owner_all)) return false;
    }
    return true;
}
#endif

/* The parent must not let others rename or replace `backend`: owned by us
 * or root, and not group/other-writable unless sticky (like /tmp). */
#if !defined(_WIN32)
bool parent_safe(const llvm::SmallString<256> &dir) {
    llvm::StringRef parent = llvm::sys::path::parent_path(dir);
    if (parent.empty()) return true;
    llvm::sys::fs::file_status st;
    if (llvm::sys::fs::status(parent, st)) return false;
    uint32_t uid = st.getUser();
    if (uid != (uint32_t)geteuid() && uid != 0) return false;
    unsigned shared = llvm::sys::fs::group_write | llvm::sys::fs::others_write;
    return !(st.permissions() & shared) || (st.permissions() & llvm::sys::fs::sticky_bit);
}
#endif

bool dir_trusted(const llvm::SmallString<256> &dir) {
#if defined(_WIN32)
    (void)dir;
    return true;
#else
    return parent_safe(dir) && owned_private(dir, /*want_dir=*/true, /*fix=*/true);
#endif
}

bool entry_trusted(const std::string &path) {
#if defined(_WIN32)
    (void)path;
    return true;
#else
    return owned_private(path, /*want_dir=*/false, /*fix=*/false);
#endif
}

bool ensure_dir() {
    if (g_bc.dir_ready) return true;
    llvm::SmallString<256> dir;
    const char *override_dir = getenv("ZAN_CACHE_DIR");
    if (override_dir && override_dir[0]) {
        dir = override_dir;
    } else {
        if (!llvm::sys::path::cache_directory(dir)) return false;
        llvm::sys::path::append(dir, "zan");
    }
    llvm::sys::path::append(dir, "backend");
    if (llvm::sys::fs::create_directories(dir, /*IgnoreExisting=*/true, llvm::sys::fs::owner_all))
        return false;
    if (!dir_trusted(dir)) {
        if (!g_bc.warned_untrusted) {
            fprintf(stderr,
                    "warning: build cache '%s' is not owned by you or is writable by others; "
                    "not using it (set ZAN_CACHE_DIR to a private directory)\n",
                    dir.c_str());
            g_bc.warned_untrusted = true;
        }
        g_bc.enabled = false;
        return false;
    }
    g_bc.dir = dir;
    g_bc.dir_ready = true;
    return true;
}

std::string entry_path(const zan_bcache_key_t *key, llvm::StringRef suffix) {
    llvm::SmallString<256> p(g_bc.dir);
    std::string name = "llvmcache-zan-" +
                       llvm::toHex(llvm::ArrayRef<uint8_t>(key->bytes, sizeof(key->bytes)),
                                   /*LowerCase=*/true) +
                       suffix.str();
    llvm::sys::path::append(p, name);
    return std::string(p.str());
}

/* Write through a unique temp file in the cache dir and rename into place, so
 * concurrent builds never observe a torn entry. Temp names carry the
 * llvmcache- prefix so pruning eventually removes leftovers of a crash. */
template <typename Fill> bool write_atomic(const std::string &dst, Fill fill) {
    llvm::SmallString<256> model(g_bc.dir);
    llvm::sys::path::append(model, "llvmcache-tmp-%%%%%%%%%%%%%%%%");
    int fd = -1;
    llvm::SmallString<256> tmp;
    if (llvm::sys::fs::createUniqueFile(model, fd, tmp, llvm::sys::fs::OF_None,
                                        llvm::sys::fs::owner_read | llvm::sys::fs::owner_write))
        return false;
    bool ok;
    {
        llvm::raw_fd_ostream os(fd, /*shouldClose=*/true);
        ok = fill(os);
        os.close();
        if (os.has_error()) {
            os.clear_error();
            ok = false;
        }
    }
    if (ok && llvm::sys::fs::rename(tmp, dst)) ok = false;
    if (!ok) llvm::sys::fs::remove(tmp);
    return ok;
}

/* Keep recently used entries alive under pruning (atime is unreliable). */
void touch(const std::string &path) {
    int fd = -1;
    if (llvm::sys::fs::openFileForRead(path, fd)) return;
    llvm::sys::fs::setLastAccessAndModificationTime(fd, std::chrono::system_clock::now());
    llvm::sys::Process::SafelyCloseFileDescriptor(fd);
}

void prune() {
    const char *spec = getenv("ZAN_CACHE_PRUNE");
    auto policy = llvm::parseCachePruningPolicy(
        spec && spec[0] ? spec : "prune_interval=20m:prune_after=168h:cache_size_bytes=4g");
    if (!policy) {
        llvm::consumeError(policy.takeError());
        return;
    }
#if LLVM_VERSION_MAJOR >= 21
    auto pruned = llvm::pruneCache(g_bc.dir, *policy);
    if (!pruned) llvm::consumeError(pruned.takeError());
#else
    (void)llvm::pruneCache(g_bc.dir, *policy);
#endif
}

/* Everything about the target machine that changes generated code. */
void hash_tm(llvm::BLAKE3 &h, const llvm::TargetMachine *tm) {
    hash_str(h, tm->getTargetTriple().str());
    hash_str(h, tm->getTargetCPU());
    hash_str(h, tm->getTargetFeatureString());
    hash_u64(h, (uint64_t)tm->getOptLevel());
    hash_u64(h, (uint64_t)tm->getRelocationModel());
    hash_u64(h, (uint64_t)tm->getCodeModel());
    hash_u64(h, (uint64_t)tm->Options.FunctionSections);
    hash_u64(h, (uint64_t)tm->Options.DataSections);
    hash_u64(h, (uint64_t)tm->Options.EmulatedTLS);
    hash_u64(h, (uint64_t)tm->Options.ExceptionModel);
}

void finish_key(llvm::BLAKE3 &h, zan_bcache_key_t *key) {
    auto r = h.final();
    memcpy(key->bytes, r.data(), sizeof(key->bytes));
    key->valid = true;
}

} // namespace

extern "C" void zan_bcache_init(int argc, char **argv) {
    g_bc.enabled = false;
    for (int i = 1; i < argc; i++)
        if (argv[i] && !strcmp(argv[i], "--no-build-cache")) return;
    if (env_off("ZAN_BUILD_CACHE")) return;
    /* Debugging knobs whose effect is a side channel (an IR dump, LLVM's own
     * statistics) would silently vanish on a hit. */
    if (env_set("ZAN_DUMP_OPT_IR") || env_set("ZAN_LLVM_ARGS")) return;

    llvm::BLAKE3 h;
    hash_str(h, ZAN_BCACHE_FORMAT);
    hash_str(h, LLVM_VERSION_STRING);
    /* Compiler identity: path, size and mtime of the running binary (the
     * check ccache defaults to). A rebuilt zanc never reuses old entries. */
    std::string exe = llvm::sys::fs::getMainExecutable(argc > 0 ? argv[0] : "zanc",
                                                       reinterpret_cast<void *>(&zan_bcache_init));
    llvm::sys::fs::file_status st;
    if (exe.empty() || llvm::sys::fs::status(exe, st)) return;
    hash_str(h, exe);
    hash_u64(h, st.getSize());
    hash_u64(h, (uint64_t)st.getLastModificationTime().time_since_epoch().count());
    /* Every option, so any flag that steers the target machine or the
     * pipeline is part of the key. -o / --emit-deps name outputs only. */
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--emit-deps")) && i + 1 < argc) {
            i++;
            continue;
        }
        if (!strcmp(argv[i], "--time")) continue;
        hash_str(h, argv[i]);
    }
    /* Every ZAN_* variable, sorted: the compiler reads dozens of them. */
    std::vector<std::string> env;
    for (char **e = ZAN_BCACHE_ENVIRON; e && *e; e++) {
        if (strncmp(*e, "ZAN_", 4) != 0) continue;
        if (!strncmp(*e, "ZAN_CACHE_DIR=", 14) || !strncmp(*e, "ZAN_BUILD_CACHE=", 16) ||
            !strncmp(*e, "ZAN_CACHE_PRUNE=", 16) || !strncmp(*e, "ZAN_PART_DUMP=", 14))
            continue;
        env.emplace_back(*e);
    }
    std::sort(env.begin(), env.end());
    hash_u64(h, env.size());
    for (const std::string &e : env) hash_str(h, e);
    auto r = h.final();
    std::copy(r.begin(), r.end(), g_bc.context.begin());
    g_bc.enabled = true;
}

extern "C" bool zan_bcache_enabled(void) { return g_bc.enabled; }

extern "C" bool zan_bcache_opt_key(LLVMModuleRef mod, int opt_level, zan_bcache_key_t *key) {
    key->valid = false;
    if (!g_bc.enabled || !mod) return false;
    double t0 = now_ms();
    llvm::BLAKE3 h;
    h.update(llvm::ArrayRef<uint8_t>(g_bc.context.data(), g_bc.context.size()));
    hash_str(h, "optimize");
    hash_u64(h, (uint64_t)opt_level);
    {
        hash_ostream os(h);
        llvm::WriteBitcodeToFile(*llvm::unwrap(mod), os);
    }
    finish_key(h, key);
    g_bc.ms_key += now_ms() - t0;
    return true;
}

/* Struct type names live in the LLVMContext, and irgen's module keeps its
 * named types there, so a module parsed into that context gets uniqued names
 * (String.1) that depend on what else the context holds. Optimized modules
 * are stored with anonymous struct types; nothing after optimization looks
 * types up by name, and codegen ignores them. */
static void anonymize_struct_types(llvm::Module &m) {
    llvm::TypeFinder tf;
    tf.run(m, /*onlyNamed=*/true);
    for (llvm::StructType *st : tf) st->setName("");
}

extern "C" LLVMModuleRef zan_bcache_opt_load(const zan_bcache_key_t *key, LLVMModuleRef old_mod,
                                             char **log) {
    *log = nullptr;
    if (!g_bc.enabled || !key->valid || !ensure_dir()) return nullptr;
    double t0 = now_ms();
    std::string bc_path = entry_path(key, ".opt.bc");
    auto buf = entry_trusted(bc_path)
                   ? llvm::MemoryBuffer::getFile(bc_path, /*IsText=*/false,
                                                 /*RequiresNullTerminator=*/false)
                   : llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>(
                         std::make_error_code(std::errc::permission_denied));
    if (!buf) {
        g_bc.opt_miss++;
        g_bc.ms_load += now_ms() - t0;
        return nullptr;
    }
    llvm::Module *old = llvm::unwrap(old_mod);
    auto loaded = llvm::parseBitcodeFile((*buf)->getMemBufferRef(), old->getContext());
    if (!loaded) {
        llvm::consumeError(loaded.takeError());
        llvm::sys::fs::remove(bc_path); /* corrupt: recompute and rewrite */
        g_bc.opt_miss++;
        g_bc.ms_load += now_ms() - t0;
        return nullptr;
    }
    std::string log_path = entry_path(key, ".opt.log");
    if (!entry_trusted(log_path)) {
        /* fall through without the replayed log */
    } else if (auto lb = llvm::MemoryBuffer::getFile(log_path, /*IsText=*/true)) {
        llvm::StringRef text = (*lb)->getBuffer();
        char *copy = static_cast<char *>(malloc(text.size() + 1));
        if (copy) {
            memcpy(copy, text.data(), text.size());
            copy[text.size()] = '\0';
            *log = copy;
        }
    }
    touch(bc_path);
    delete old;
    g_bc.opt_hit++;
    g_bc.ms_load += now_ms() - t0;
    return llvm::wrap(loaded->release());
}

extern "C" LLVMModuleRef zan_bcache_opt_store(const zan_bcache_key_t *key, LLVMModuleRef mod,
                                              const char *log) {
    if (!g_bc.enabled || !key->valid || !mod) return mod;
    llvm::Module *m = llvm::unwrap(mod);
    double t0 = now_ms();
    anonymize_struct_types(*m);
    llvm::SmallVector<char, 0> bc;
    {
        llvm::raw_svector_ostream os(bc);
        llvm::WriteBitcodeToFile(*m, os, /*ShouldPreserveUseListOrder=*/true);
    }
    /* Continue with the module a hit would load, not the in-memory one: the
     * writer's output also depends on in-memory history (function-local
     * symbol table order, attributes the reader drops), so only a reparse
     * makes this build hand codegen the same bytes as the next build. */
    llvm::MemoryBufferRef ref(llvm::StringRef(bc.data(), bc.size()), "zan-bcache");
    auto reparsed = llvm::parseBitcodeFile(ref, m->getContext());
    if (!reparsed) {
        llvm::consumeError(reparsed.takeError());
        g_bc.ms_store += now_ms() - t0;
        return mod;
    }
    delete m;
    if (ensure_dir()) {
        /* The log is optional on load; write it first so a published .opt.bc
         * always finds its report. */
        write_atomic(entry_path(key, ".opt.log"), [&](llvm::raw_fd_ostream &os) {
            if (log) os << log;
            return true;
        });
        write_atomic(entry_path(key, ".opt.bc"), [&](llvm::raw_fd_ostream &os) {
            os.write(bc.data(), bc.size());
            return true;
        });
        prune();
    }
    g_bc.ms_store += now_ms() - t0;
    return llvm::wrap(reparsed->release());
}

extern "C" bool zan_bcache_obj_key(LLVMModuleRef mod, LLVMTargetMachineRef tm_ref, int parts,
                                   zan_bcache_key_t *key) {
    key->valid = false;
    if (!g_bc.enabled || !mod || !tm_ref) return false;
    double t0 = now_ms();
    auto *tm = reinterpret_cast<llvm::TargetMachine *>(tm_ref);
    llvm::BLAKE3 h;
    h.update(llvm::ArrayRef<uint8_t>(g_bc.context.data(), g_bc.context.size()));
    hash_str(h, "object");
    hash_tm(h, tm);
    hash_u64(h, (uint64_t)parts);
    {
        hash_ostream os(h);
        llvm::WriteBitcodeToFile(*llvm::unwrap(mod), os);
    }
    finish_key(h, key);
    g_bc.ms_key += now_ms() - t0;
    return true;
}

static std::string obj_entry(const zan_bcache_key_t *key, int i) {
    return entry_path(key, i == 0 ? std::string(".o") : ".p" + std::to_string(i) + ".o");
}

extern "C" bool zan_bcache_obj_load(const zan_bcache_key_t *key, int parts,
                                    const char *const *paths) {
    if (!g_bc.enabled || !key->valid || parts < 1 || !ensure_dir()) return false;
    double t0 = now_ms();
    bool ok = true;
    for (int i = 0; i < parts && ok; i++) ok = entry_trusted(obj_entry(key, i));
    for (int i = 0; i < parts && ok; i++)
        ok = !llvm::sys::fs::copy_file(obj_entry(key, i), paths[i]);
    if (ok) {
        touch(obj_entry(key, 0));
        g_bc.obj_hit++;
    } else {
        g_bc.obj_miss++;
    }
    g_bc.ms_load += now_ms() - t0;
    return ok;
}

extern "C" void zan_bcache_obj_store(const zan_bcache_key_t *key, int parts,
                                     const char *const *paths) {
    if (!g_bc.enabled || !key->valid || parts < 1 || !ensure_dir()) return;
    double t0 = now_ms();
    /* Part 0 last: it is the entry's commit marker for readers. */
    for (int i = parts - 1; i >= 0; i--) {
        auto buf = llvm::MemoryBuffer::getFile(paths[i], /*IsText=*/false,
                                               /*RequiresNullTerminator=*/false);
        if (!buf) return;
        llvm::StringRef bytes = (*buf)->getBuffer();
        if (!write_atomic(obj_entry(key, i), [&](llvm::raw_fd_ostream &os) {
                os << bytes;
                return true;
            }))
            return;
    }
    prune();
    g_bc.ms_store += now_ms() - t0;
}

extern "C" bool zan_bcache_unit_key(const void *bitcode, size_t size, LLVMTargetMachineRef tm_ref,
                                    int opt_level, zan_bcache_key_t *key) {
    key->valid = false;
    if (!g_bc.enabled || !bitcode || !tm_ref) return false;
    double t0 = now_ms();
    llvm::BLAKE3 h;
    h.update(llvm::ArrayRef<uint8_t>(g_bc.context.data(), g_bc.context.size()));
    hash_str(h, "unit");
    hash_tm(h, reinterpret_cast<llvm::TargetMachine *>(tm_ref));
    hash_u64(h, (uint64_t)opt_level);
    h.update(llvm::ArrayRef<uint8_t>(static_cast<const uint8_t *>(bitcode), size));
    finish_key(h, key);
    g_bc.ms_key += now_ms() - t0;
    return true;
}

/* An entry is <key>.u.refs then <key>.u.o; the object is written last and
 * is the commit marker. */
extern "C" bool zan_bcache_unit_load(const zan_bcache_key_t *key, const char *obj_path,
                                     char **refs) {
    *refs = nullptr;
    if (!g_bc.enabled || !key->valid || !ensure_dir()) return false;
    double t0 = now_ms();
    std::string obj = entry_path(key, ".u.o"), list = entry_path(key, ".u.refs");
    bool ok = entry_trusted(obj) && entry_trusted(list);
    if (ok) {
        auto buf = llvm::MemoryBuffer::getFile(list, /*IsText=*/false,
                                               /*RequiresNullTerminator=*/false);
        ok = (bool)buf;
        if (ok) {
            llvm::StringRef text = (*buf)->getBuffer();
            *refs = static_cast<char *>(malloc(text.size() + 1));
            ok = *refs != nullptr;
            if (ok) {
                memcpy(*refs, text.data(), text.size());
                (*refs)[text.size()] = '\0';
            }
        }
    }
    if (ok) ok = !llvm::sys::fs::copy_file(obj, obj_path);
    if (ok) {
        touch(obj);
        touch(list);
        g_bc.unit_hit++;
    } else {
        free(*refs);
        *refs = nullptr;
        g_bc.unit_miss++;
    }
    g_bc.ms_load += now_ms() - t0;
    return ok;
}

extern "C" void zan_bcache_unit_store(const zan_bcache_key_t *key, const char *obj_path,
                                      const char *refs) {
    if (!g_bc.enabled || !key->valid || !ensure_dir()) return;
    double t0 = now_ms();
    auto buf = llvm::MemoryBuffer::getFile(obj_path, /*IsText=*/false,
                                           /*RequiresNullTerminator=*/false);
    if (!buf) return;
    llvm::StringRef bytes = (*buf)->getBuffer();
    llvm::StringRef list(refs ? refs : "");
    if (!write_atomic(entry_path(key, ".u.refs"), [&](llvm::raw_fd_ostream &os) {
            os << list;
            return true;
        }))
        return;
    if (!write_atomic(entry_path(key, ".u.o"), [&](llvm::raw_fd_ostream &os) {
            os << bytes;
            return true;
        }))
        return;
    g_bc.ms_store += now_ms() - t0;
}

extern "C" void zan_bcache_logf(const char *fmt, ...) {
    char small[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(small, sizeof(small), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    std::string text;
    if ((size_t)n < sizeof(small)) {
        text.assign(small, (size_t)n);
    } else {
        text.resize((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(&text[0], text.size(), fmt, ap);
        va_end(ap);
        text.resize((size_t)n);
    }
    fputs(text.c_str(), stderr);
    if (g_bc.capturing) g_bc.capture += text;
}

extern "C" void zan_bcache_capture_begin(void) {
    g_bc.capture.clear();
    g_bc.capturing = true;
}

extern "C" char *zan_bcache_capture_end(void) {
    g_bc.capturing = false;
    char *out = static_cast<char *>(malloc(g_bc.capture.size() + 1));
    if (!out) return nullptr;
    memcpy(out, g_bc.capture.data(), g_bc.capture.size());
    out[g_bc.capture.size()] = '\0';
    g_bc.capture.clear();
    return out;
}

extern "C" void zan_bcache_print_stats(void) {
    if (!g_bc.enabled) {
        fprintf(stderr, "build cache: off\n");
        return;
    }
    fprintf(stderr,
            "build cache: optimize %d hit/%d miss, object %d hit/%d miss, unit %d hit/%d miss; "
            "key %.1f ms, load %.1f ms, store %.1f ms (%s)\n",
            g_bc.opt_hit, g_bc.opt_miss, g_bc.obj_hit, g_bc.obj_miss, g_bc.unit_hit, g_bc.unit_miss,
            g_bc.ms_key, g_bc.ms_load, g_bc.ms_store, g_bc.dir_ready ? g_bc.dir.c_str() : "-");
}
