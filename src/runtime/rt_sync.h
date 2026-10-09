#ifndef ZAN_RT_SYNC_H
#define ZAN_RT_SYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 模块核心语义抽象与接口调用契约 */
int32_t zan_thread_start(void *body);

/* 获取当前线程的进程内唯一数值 ID（跨平台底层线程标识） */
int64_t zan_thread_current_id(void);

/* 释放当前线程的线程局部运行时状态（支持幂等调用） */
void zan_thread_detach(void);

/* 释放当前线程的异常处理状态 */
void __zan_eh_release(void);

/* 模块核心语义抽象与接口调用契约 */
void zan_monitor_enter(void *obj);
void zan_monitor_exit(void *obj);

/* UI 线程派发队列：支持多线程投递闭包并在 UI 主线程消费执行 */
void zan_dispatch_init(void);
int32_t zan_dispatch_post(void *fn);
void *zan_dispatch_take(void);

/* 底层系统交互与数据协议契约 */
void zan_ui_thread_set(void);
int32_t zan_ui_thread_check(void);
void zan_ui_thread_assert(const char *msg);

/* 进程内原子 i64 句柄封装：多线程并发安全 */
int64_t zan_atomic_int_create(int64_t initial_value);
void zan_atomic_int_destroy(int64_t handle);
int64_t zan_atomic_int_load(int64_t handle);
void zan_atomic_int_store(int64_t handle, int64_t value);
int64_t zan_atomic_int_exchange(int64_t handle, int64_t value);
int64_t zan_atomic_int_compare_exchange(
    int64_t handle, int64_t expected, int64_t desired);
int64_t zan_atomic_int_add(int64_t handle, int64_t delta);

/* 模块核心语义抽象与接口调用契约 */
int64_t zan_monotonic_us(void);

/* 底层系统交互与数据协议契约 */
int64_t zan_monotonic_ns(void);

/* 模块核心语义抽象与接口调用契约 */
int64_t zan_stopwatch_ticks(void);
int64_t zan_stopwatch_frequency(void);
int64_t zan_monotonic_ticks(void);
int64_t zan_monotonic_frequency(void);

int64_t zan_shared_table_create(
    const char *name, int32_t capacity, int32_t key_size, const char *schema);
int64_t zan_shared_table_open(const char *name);
/* 匿名共享内存表：仅通过继承句柄在父子进程间映射共享 */
int64_t zan_shared_table_create_anon(
    int32_t capacity, int32_t key_size, const char *schema);
int64_t zan_shared_table_handle(int64_t handle);
int64_t zan_shared_table_attach(int64_t os_handle);
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
int32_t zan_shared_table_expire(
    int64_t handle, const char *key, int64_t ttl_ms);
int32_t zan_shared_table_expire_at(
    int64_t handle, const char *key, int64_t expires_at);
int64_t zan_shared_table_expires_at(int64_t handle, const char *key);
int64_t zan_shared_table_purge_expired(int64_t handle, int64_t now_ms);
int32_t zan_shared_table_rate_allow(
    int64_t handle, const char *key, int64_t now_ms,
    int64_t window_ms, int64_t limit);
int32_t zan_shared_table_lock_acquire(
    int64_t handle, const char *key, int64_t owner,
    int64_t now_ms, int64_t lease_ms);
int32_t zan_shared_table_lock_release(
    int64_t handle, const char *key, int64_t owner);
int32_t zan_shared_table_delete(int64_t handle, const char *key);
int32_t zan_shared_table_exists(int64_t handle, const char *key);
int64_t zan_shared_table_count(int64_t handle);
void zan_shared_table_clear(int64_t handle);

/* 查询共享内存表元数据（0:保留字节 1:常驻字节 2:容量 3:已用行 4:行跨度 5:键大小 6:列数） */
#define ZAN_TABLE_STAT_RESERVED 0
#define ZAN_TABLE_STAT_RESIDENT 1
#define ZAN_TABLE_STAT_CAPACITY 2
#define ZAN_TABLE_STAT_COUNT 3
#define ZAN_TABLE_STAT_ROW_STRIDE 4
#define ZAN_TABLE_STAT_KEY_SIZE 5
#define ZAN_TABLE_STAT_COLUMNS 6
int64_t zan_shared_table_stat(int64_t handle, int32_t what);

/* 共享内存表哈希键变体接口：加速热路径查询 */
int64_t zan_shared_table_hash(const char *value);
int32_t zan_shared_table_set_int_at(
    int64_t handle, int64_t key_hash, const char *column_name, int64_t value);
int64_t zan_shared_table_get_int_at(
    int64_t handle, int64_t key_hash, const char *column_name);
int64_t zan_shared_table_increment_at(
    int64_t handle, int64_t key_hash, const char *column_name, int64_t delta);
int64_t zan_shared_table_extreme_at(
    int64_t handle, int64_t key_hash, const char *column_name, int64_t value,
    int64_t keep_larger);
int32_t zan_shared_table_set_string_at(
    int64_t handle, int64_t key_hash, const char *column_name,
    const char *value);
const char *zan_shared_table_get_string_at(
    int64_t handle, int64_t key_hash, const char *column_name);
int32_t zan_shared_table_match_at(
    int64_t handle, int64_t key_hash, const char *column_name,
    const char *text);
int32_t zan_shared_table_exists_at(int64_t handle, int64_t key_hash);
int32_t zan_shared_table_delete_at(int64_t handle, int64_t key_hash);

/* 底层系统交互与数据协议契约 */
long long zan_exe_dir_into(char *out, long long cap);
long long zan_dir_list_into(const char *pattern, char *out, long long cap);

/* 模块核心语义抽象与接口调用契约 */
int32_t zan_proc_run_safe(const char *exe, const char **args, int32_t argc);
int32_t zan_proc_start_detached_safe(const char *exe, const char **args, int32_t argc);
int32_t zan_proc_start_program_safe(const char *exe, const char *log_path);
int32_t zan_proc_capture_safe(const char *exe, const char **args, int32_t argc,
                              char **out_buf, int32_t *out_len, int32_t *exit_code);
void zan_proc_free_buf(char *buf);

/* 核心系统底层抽象与内存语义契约 */
long long zan_file_time(const char *path, int which);
long long zan_file_length(const char *path);
long long zan_file_attributes(const char *path);
/* 模块核心语义抽象与接口调用契约 */
const char *zan_file_app_dir(void);
const char *zan_file_read_path(const char *path);
long long zan_file_set_readonly(const char *path, int on);
long long zan_file_set_time(const char *path, int which, long long unix_sec);
long long zan_file_open(const char *path, const char *mode);
long long zan_file_read(long long handle, long long buf, long long count);
long long zan_file_write(long long handle, long long buf, long long count);
long long zan_file_seek(long long handle, long long offset, int origin);
long long zan_file_tell(long long handle);
long long zan_file_flush(long long handle);
long long zan_file_close(long long handle);
long long zan_file_eof(long long handle);

/* 全文件排他锁：进程退出时由操作系统自动回收 */
long long zan_file_try_lock(const char *path);
long long zan_file_unlock(long long handle);

/* 核心系统底层抽象与内存语义契约 */
long long zan_mmap_create(const char *name, long long size);
long long zan_mmap_open(const char *name, long long size);
long long zan_mmap_from_file(const char *path, long long size);
long long zan_mmap_map(long long handle, long long size);
long long zan_mmap_unmap(long long ptr, long long size);
long long zan_mmap_flush(long long ptr, long long size);
long long zan_mmap_close(long long handle);
long long zan_mmap_unlink(const char *name);

#ifdef __cplusplus
}
#endif

#endif
