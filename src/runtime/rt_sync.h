#ifndef ZAN_RT_SYNC_H
#define ZAN_RT_SYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn a new detached OS thread that runs the given Zan delegate (a no-arg
 * function pointer). Returns 1 on success, 0 on failure. */
int32_t zan_thread_start(void *body);

/* Drop the calling thread's per-thread runtime state (currently its
 * exception-handling block). Threads started by zan_thread_start do this
 * themselves; a foreign callback thread that ran Zan code must call it, or it
 * keeps a slot in a table with a hard limit. Idempotent. */
void zan_thread_detach(void);

/* `lock (obj)` statement monitor: process-wide recursive mutex (coarser than
 * C#'s per-object monitor; the object argument is currently unused). */
void zan_monitor_enter(void *obj);
void zan_monitor_exit(void *obj);

/* UI-thread dispatch queue: post a delegate from any thread, drain on the UI
 * thread. */
void zan_dispatch_init(void);
int32_t zan_dispatch_post(void *fn);
void *zan_dispatch_take(void);

/* A process-local atomic i64 behind an opaque handle. Every operation on a
 * live handle is safe from any thread; destroying one is not an operation on
 * it but the end of its life, so the owner must have joined or otherwise
 * excluded every user first -- as with C# Dispose, a concurrent destroy and
 * load is a use-after-free in the caller, not something the handle can defend
 * against. */
int64_t zan_atomic_int_create(int64_t initial_value);
void zan_atomic_int_destroy(int64_t handle);
int64_t zan_atomic_int_load(int64_t handle);
void zan_atomic_int_store(int64_t handle, int64_t value);
int64_t zan_atomic_int_exchange(int64_t handle, int64_t value);
int64_t zan_atomic_int_compare_exchange(
    int64_t handle, int64_t expected, int64_t desired);
int64_t zan_atomic_int_add(int64_t handle, int64_t delta);

int64_t zan_shared_table_create(
    const char *name, int32_t capacity, int32_t key_size, const char *schema);
int64_t zan_shared_table_open(const char *name);
void zan_shared_table_close(int64_t handle);
int32_t zan_shared_table_destroy(int64_t handle);
int32_t zan_shared_table_set_int(
    int64_t handle, const char *key, const char *column, int64_t value);
int64_t zan_shared_table_get_int(
    int64_t handle, const char *key, const char *column);
int32_t zan_shared_table_set_float(
    int64_t handle, const char *key, const char *column, double value);
double zan_shared_table_get_float(
    int64_t handle, const char *key, const char *column);
int32_t zan_shared_table_set_string(
    int64_t handle, const char *key, const char *column, const char *value);
const char *zan_shared_table_get_string(
    int64_t handle, const char *key, const char *column);
int64_t zan_shared_table_increment(
    int64_t handle, const char *key, const char *column, int64_t delta);
int32_t zan_shared_table_delete(int64_t handle, const char *key);
int32_t zan_shared_table_exists(int64_t handle, const char *key);
int64_t zan_shared_table_count(int64_t handle);
void zan_shared_table_clear(int64_t handle);

/* filesystem helpers for the compiler driver */
long long zan_exe_dir_into(char *out, long long cap);
long long zan_dir_list_into(const char *pattern, char *out, long long cap);

/* file metadata (System.IO.FileInfo) */
long long zan_file_time(const char *path, int which);
long long zan_file_length(const char *path);
long long zan_file_attributes(const char *path);
long long zan_file_set_readonly(const char *path, int on);
long long zan_file_open(const char *path, const char *mode);
long long zan_file_read(long long handle, long long buf, long long count);
long long zan_file_write(long long handle, long long buf, long long count);
long long zan_file_seek(long long handle, long long offset, int origin);
long long zan_file_tell(long long handle);
long long zan_file_flush(long long handle);
long long zan_file_close(long long handle);
long long zan_file_eof(long long handle);

#ifdef __cplusplus
}
#endif

#endif
