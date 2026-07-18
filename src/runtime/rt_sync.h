#ifndef ZAN_RT_SYNC_H
#define ZAN_RT_SYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn a new detached OS thread that runs the given Zan delegate (a no-arg
 * function pointer). Returns 1 on success, 0 on failure. */
int64_t zan_thread_start(void *body);

/* UI-thread dispatch queue: post a delegate from any thread, drain on the UI
 * thread. */
void zan_dispatch_init(void);
int64_t zan_dispatch_post(void *fn);
void *zan_dispatch_take(void);

int64_t zan_atomic_int_create(int64_t initial_value);
void zan_atomic_int_destroy(int64_t handle);
int64_t zan_atomic_int_load(int64_t handle);
void zan_atomic_int_store(int64_t handle, int64_t value);
int64_t zan_atomic_int_exchange(int64_t handle, int64_t value);
int64_t zan_atomic_int_compare_exchange(
    int64_t handle, int64_t expected, int64_t desired);
int64_t zan_atomic_int_add(int64_t handle, int64_t delta);

int64_t zan_shared_table_create(
    const char *name, int64_t capacity, int64_t key_size, const char *schema);
int64_t zan_shared_table_open(const char *name);
void zan_shared_table_close(int64_t handle);
int64_t zan_shared_table_destroy(int64_t handle);
int64_t zan_shared_table_set_int(
    int64_t handle, const char *key, const char *column, int64_t value);
int64_t zan_shared_table_get_int(
    int64_t handle, const char *key, const char *column);
int64_t zan_shared_table_set_float(
    int64_t handle, const char *key, const char *column, double value);
double zan_shared_table_get_float(
    int64_t handle, const char *key, const char *column);
int64_t zan_shared_table_set_string(
    int64_t handle, const char *key, const char *column, const char *value);
const char *zan_shared_table_get_string(
    int64_t handle, const char *key, const char *column);
int64_t zan_shared_table_increment(
    int64_t handle, const char *key, const char *column, int64_t delta);
int64_t zan_shared_table_delete(int64_t handle, const char *key);
int64_t zan_shared_table_exists(int64_t handle, const char *key);
int64_t zan_shared_table_count(int64_t handle);
void zan_shared_table_clear(int64_t handle);

/* filesystem helpers for the compiler driver */
long long zan_exe_dir_into(char *out, long long cap);
long long zan_dir_list_into(const char *pattern, char *out, long long cap);

#ifdef __cplusplus
}
#endif

#endif
