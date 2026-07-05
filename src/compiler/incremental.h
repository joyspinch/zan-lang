/* incremental.h -- Incremental compilation support for Zan compiler.
 *
 * Tracks file modification times and content hashes to skip recompilation
 * of unchanged source files. Uses a .zan-cache directory to store:
 *   - File content hashes (SHA-256 simplified as CRC-based)
 *   - Compiled object files (.o)
 *   - Symbol dependency graph
 */

#ifndef ZAN_INCREMENTAL_H
#define ZAN_INCREMENTAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- file hash (fast 64-bit hash for change detection) ---- */

typedef struct {
    uint64_t hash;
    uint64_t mtime;     /* last modification time */
    uint64_t size;      /* file size */
} zan_file_stamp_t;

/* ---- dependency tracking ---- */

typedef struct {
    char *source_path;         /* source .zan file */
    char *object_path;         /* cached .o file */
    zan_file_stamp_t stamp;    /* file stamp when last compiled */
    char **deps;               /* files this depends on (imports) */
    int dep_count;
    bool needs_rebuild;        /* set during analysis */
} zan_compile_unit_t;

/* ---- incremental cache ---- */

typedef struct {
    char *cache_dir;                /* .zan-cache directory path */
    zan_compile_unit_t *units;      /* array of compilation units */
    int unit_count;
    int unit_cap;
    bool cache_valid;               /* whether cache was loaded successfully */
} zan_incr_cache_t;

/* Initialize incremental cache for a project directory */
void zan_incr_init(zan_incr_cache_t *cache, const char *project_dir);

/* Load cache from .zan-cache/manifest.json */
bool zan_incr_load(zan_incr_cache_t *cache);

/* Save cache to .zan-cache/manifest.json */
bool zan_incr_save(zan_incr_cache_t *cache);

/* Check if a source file needs recompilation */
bool zan_incr_needs_rebuild(zan_incr_cache_t *cache, const char *source_path);

/* Register a compiled unit in the cache */
void zan_incr_register(zan_incr_cache_t *cache, const char *source_path,
                       const char *object_path, const char **deps, int dep_count);

/* Get cached object file path (NULL if needs rebuild) */
const char *zan_incr_get_object(zan_incr_cache_t *cache, const char *source_path);

/* Invalidate all entries depending on a given file */
void zan_incr_invalidate(zan_incr_cache_t *cache, const char *changed_file);

/* Clean cache (remove all cached objects) */
void zan_incr_clean(zan_incr_cache_t *cache);

/* Destroy cache and free memory */
void zan_incr_destroy(zan_incr_cache_t *cache);

/* ---- file hashing utility ---- */

/* Compute a fast 64-bit hash of file contents */
uint64_t zan_hash_file(const char *path);

/* Compute hash of a memory buffer */
uint64_t zan_hash_buffer(const void *data, size_t len);

/* Get file modification time (0 on error) */
uint64_t zan_file_mtime(const char *path);

/* Get file size (0 on error) */
uint64_t zan_file_size(const char *path);

/* ---- parallel compilation ---- */

typedef struct {
    const char **source_files;  /* array of source file paths */
    int file_count;
    int thread_count;           /* 0 = auto-detect CPU count */
    const char *output_dir;     /* where to place .o files */
    bool incremental;           /* use incremental cache */
    zan_incr_cache_t *cache;    /* incremental cache (may be NULL) */
} zan_parallel_opts_t;

typedef struct {
    char *source_path;
    char *object_path;
    int exit_code;
    char *error_msg;            /* NULL on success */
} zan_compile_result_t;

/* Compile multiple files in parallel using thread pool */
zan_compile_result_t *zan_parallel_compile(zan_parallel_opts_t *opts);

/* Free results array */
void zan_parallel_results_free(zan_compile_result_t *results, int count);

/* Get number of available CPU cores */
int zan_cpu_count(void);

#endif /* ZAN_INCREMENTAL_H */
