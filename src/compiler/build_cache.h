#ifndef ZAN_BUILD_CACHE_H
#define ZAN_BUILD_CACHE_H

/* Content-addressed build cache for the LLVM backend (docs/BUILD_CACHE.md).
 *
 * Two memo tables, both keyed by a BLAKE3 digest of the module's bitcode plus
 * a context digest (compiler binary identity, LLVM version, argv without -o,
 * every ZAN_* environment variable):
 *   - optimize: pre-optimization module      -> optimized bitcode + the
 *                                               optimizer's stderr report
 *   - object:   module as handed to codegen  -> object file(s)
 * The keys hash what closed-world pruning, generic instantiation and
 * reflection demand already produced, so the cache never re-decides any of
 * them: a hit returns exactly what the same input computed before.
 *
 * Disabled by --no-build-cache, ZAN_BUILD_CACHE=0, and whenever a debugging
 * knob with side effects is set (ZAN_DUMP_OPT_IR, ZAN_LLVM_ARGS). Location:
 * $ZAN_CACHE_DIR, else the platform user cache dir + /zan/backend. */

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char bytes[32];
    bool valid;
} zan_bcache_key_t;

/* Call once after argument expansion. Computes the context digest. */
void zan_bcache_init(int argc, char **argv);
bool zan_bcache_enabled(void);

/* optimize memo. zan_bcache_opt_load consumes `old_mod` on a hit and returns
 * the cached optimized module parsed into the same context; on a miss it
 * returns NULL and leaves `old_mod` untouched. *log receives the replayed
 * optimizer report (malloc'd, may be NULL). */
bool zan_bcache_opt_key(LLVMModuleRef mod, int opt_level, zan_bcache_key_t *key);
LLVMModuleRef zan_bcache_opt_load(const zan_bcache_key_t *key, LLVMModuleRef old_mod, char **log);
/* Stores the optimized module and returns the module to continue with: the
 * stored bitcode parsed back into the same context (consuming `mod`), so a
 * miss feeds codegen exactly what a later hit will. Returns `mod` unchanged
 * when the cache is off or the round trip fails. */
LLVMModuleRef zan_bcache_opt_store(const zan_bcache_key_t *key, LLVMModuleRef mod, const char *log);

/* object memo: `parts` objects, paths[0..parts-1]. */
bool zan_bcache_obj_key(LLVMModuleRef mod, LLVMTargetMachineRef tm, int parts,
                        zan_bcache_key_t *key);
bool zan_bcache_obj_load(const zan_bcache_key_t *key, int parts, const char *const *paths);
void zan_bcache_obj_store(const zan_bcache_key_t *key, int parts, const char *const *paths);

/* Optimizer report text: printed to stderr and, between capture_begin and
 * capture_end, also recorded so a cache hit can replay it. */
void zan_bcache_logf(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void zan_bcache_capture_begin(void);
char *zan_bcache_capture_end(void); /* malloc'd; NULL only when out of memory */

/* --time: one summary line of hits, misses and time spent in the cache. */
void zan_bcache_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_BUILD_CACHE_H */
