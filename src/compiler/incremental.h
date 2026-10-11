
#ifndef ZAN_INCREMENTAL_H
#define ZAN_INCREMENTAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint64_t hash;
    uint64_t mtime;
    uint64_t size;
} zan_file_stamp_t;

typedef struct {
    char *source_path;
    char *object_path;         /* cached .o file */
    zan_file_stamp_t stamp;
    char **deps;
    int dep_count;
    bool needs_rebuild;
} zan_compile_unit_t;

typedef struct {
    char *cache_dir;
    zan_compile_unit_t *units;
    int unit_count;
    int unit_cap;
    bool cache_valid;
    void *lock;
} zan_incr_cache_t;

void zan_incr_init(zan_incr_cache_t *cache, const char *project_dir);

bool zan_incr_load(zan_incr_cache_t *cache);

bool zan_incr_save(zan_incr_cache_t *cache);

bool zan_incr_needs_rebuild(zan_incr_cache_t *cache, const char *source_path);

void zan_incr_register(zan_incr_cache_t *cache, const char *source_path,
                       const char *object_path, const char **deps, int dep_count);

const char *zan_incr_get_object(zan_incr_cache_t *cache, const char *source_path);

void zan_incr_invalidate(zan_incr_cache_t *cache, const char *changed_file);

void zan_incr_clean(zan_incr_cache_t *cache);

void zan_incr_destroy(zan_incr_cache_t *cache);

uint64_t zan_hash_file(const char *path);

uint64_t zan_hash_buffer(const void *data, size_t len);

uint64_t zan_file_mtime(const char *path);

uint64_t zan_file_size(const char *path);

typedef struct {
    const char **source_files;
    int file_count;
    int thread_count;
    const char *output_dir;     /* where to place .o files */
    bool incremental;
    zan_incr_cache_t *cache;
} zan_parallel_opts_t;

typedef struct {
    char *source_path;
    char *object_path;
    int exit_code;
    char *error_msg;            /* NULL on success */
} zan_compile_result_t;

zan_compile_result_t *zan_parallel_compile(zan_parallel_opts_t *opts);

void zan_parallel_results_free(zan_compile_result_t *results, int count);

int zan_cpu_count(void);

#endif /* ZAN_INCREMENTAL_H */
