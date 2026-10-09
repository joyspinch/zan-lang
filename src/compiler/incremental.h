/* 编译器代码生成与运行时系统底层调用契约 */

#ifndef ZAN_INCREMENTAL_H
#define ZAN_INCREMENTAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint64_t hash;
    uint64_t mtime;     /* 核心系统底层抽象与内存语义契约 */
    uint64_t size;      /* 核心系统底层抽象与内存语义契约 */
} zan_file_stamp_t;

typedef struct {
    char *source_path;         /* 核心系统底层抽象与内存语义契约 */
    char *object_path;         /* cached .o file */
    zan_file_stamp_t stamp;    /* 核心系统底层抽象与内存语义契约 */
    char **deps;               /* 核心系统底层抽象与内存语义契约 */
    int dep_count;
    bool needs_rebuild;        /* 核心系统底层抽象与内存语义契约 */
} zan_compile_unit_t;

typedef struct {
    char *cache_dir;                /* 核心系统底层抽象与内存语义契约 */
    zan_compile_unit_t *units;      /* 核心系统底层抽象与内存语义契约 */
    int unit_count;
    int unit_cap;
    bool cache_valid;               /* 核心系统底层抽象与内存语义契约 */
    void *lock;                     /* 编译器代码生成与运行时系统底层调用契约 */
} zan_incr_cache_t;

/* 底层系统交互与数据协议契约 */
void zan_incr_init(zan_incr_cache_t *cache, const char *project_dir);

/* 底层系统交互与数据协议契约 */
bool zan_incr_load(zan_incr_cache_t *cache);

/* 底层系统交互与数据协议契约 */
bool zan_incr_save(zan_incr_cache_t *cache);

/* 核心系统底层抽象与内存语义契约 */
bool zan_incr_needs_rebuild(zan_incr_cache_t *cache, const char *source_path);

/* 核心系统底层抽象与内存语义契约 */
void zan_incr_register(zan_incr_cache_t *cache, const char *source_path,
                       const char *object_path, const char **deps, int dep_count);

/* 模块核心语义抽象与接口调用契约 */
const char *zan_incr_get_object(zan_incr_cache_t *cache, const char *source_path);

/* 底层系统交互与数据协议契约 */
void zan_incr_invalidate(zan_incr_cache_t *cache, const char *changed_file);

/* 底层系统交互与数据协议契约 */
void zan_incr_clean(zan_incr_cache_t *cache);

/* 核心系统底层抽象与内存语义契约 */
void zan_incr_destroy(zan_incr_cache_t *cache);

/* 底层系统交互与数据协议契约 */
uint64_t zan_hash_file(const char *path);

/* 核心系统底层抽象与内存语义契约 */
uint64_t zan_hash_buffer(const void *data, size_t len);

/* 核心系统底层抽象与内存语义契约 */
uint64_t zan_file_mtime(const char *path);

/* 核心系统底层抽象与内存语义契约 */
uint64_t zan_file_size(const char *path);

typedef struct {
    const char **source_files;  /* 核心系统底层抽象与内存语义契约 */
    int file_count;
    int thread_count;           /* 核心系统底层抽象与内存语义契约 */
    const char *output_dir;     /* where to place .o files */
    bool incremental;           /* 核心系统底层抽象与内存语义契约 */
    zan_incr_cache_t *cache;    /* 核心系统底层抽象与内存语义契约 */
} zan_parallel_opts_t;

typedef struct {
    char *source_path;
    char *object_path;
    int exit_code;
    char *error_msg;            /* NULL on success */
} zan_compile_result_t;

/* 底层系统交互与数据协议契约 */
zan_compile_result_t *zan_parallel_compile(zan_parallel_opts_t *opts);

/* 核心系统底层抽象与内存语义契约 */
void zan_parallel_results_free(zan_compile_result_t *results, int count);

/* 核心系统底层抽象与内存语义契约 */
int zan_cpu_count(void);

#endif /* ZAN_INCREMENTAL_H */
