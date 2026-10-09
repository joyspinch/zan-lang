/* stdlib_ext.h: Zan 扩展标准库（HTTP 客户端、线程同步、JSON 解析、StringBuilder） */

#ifndef ZAN_STDLIB_EXT_H
#define ZAN_STDLIB_EXT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int status_code;
    char *body;
    size_t body_len;
    char *headers;
    size_t headers_len;
} zan_http_response_t;

/* 执行 HTTP GET 请求（返回堆分配的响应，调用方负责释放） */
zan_http_response_t *zan_http_get(const char *url);

/* 执行 HTTP POST 请求 */
zan_http_response_t *zan_http_post(const char *url, const char *body, const char *content_type);

/* 释放 HTTP 响应结构体 */
void zan_http_response_free(zan_http_response_t *resp);

typedef void *zan_thread_t;
typedef void *zan_mutex_t;
typedef void *zan_event_t;

typedef void (*zan_thread_fn)(void *arg);

/* 创建并启动新线程 */
zan_thread_t zan_thread_create(zan_thread_fn fn, void *arg);

/* 等待线程执行完毕 */
void zan_thread_join(zan_thread_t thread);

/* 当前线程休眠指定毫秒 */
void zan_thread_sleep(int ms);

/* 获取当前线程 ID */
int64_t zan_thread_id(void);

/* 互斥锁操作 */
zan_mutex_t zan_mutex_create(void);
void zan_mutex_lock(zan_mutex_t mtx);
void zan_mutex_unlock(zan_mutex_t mtx);
void zan_mutex_destroy(zan_mutex_t mtx);

/* Event/signal operations */
zan_event_t zan_event_create(void);
void zan_event_signal(zan_event_t evt);
void zan_event_wait(zan_event_t evt);
void zan_event_destroy(zan_event_t evt);

/* Atomic operations */
int64_t zan_atomic_add(volatile int64_t *ptr, int64_t val);
int64_t zan_atomic_load(volatile int64_t *ptr);
void zan_atomic_store(volatile int64_t *ptr, int64_t val);

typedef enum {
    ZAN_JSON_NULL,
    ZAN_JSON_BOOL,
    ZAN_JSON_NUMBER,
    ZAN_JSON_STRING,
    ZAN_JSON_ARRAY,
    ZAN_JSON_OBJECT,
} zan_json_type_t;

typedef struct zan_json_value zan_json_value_t;

struct zan_json_value {
    zan_json_type_t type;
    union {
        bool bool_val;
        double number_val;
        struct { char *str; size_t len; } string_val;
        struct { zan_json_value_t **items; int count; int cap; } array_val;
        struct {
            char **keys;
            zan_json_value_t **values;
            int count;
            int cap;
        } object_val;
    };
};

/* Parse JSON string into value tree */
zan_json_value_t *zan_json_parse(const char *json, size_t len);

/* Serialize value tree to JSON string */
char *zan_json_serialize(const zan_json_value_t *val, bool pretty);

/* Access helpers */
zan_json_value_t *zan_json_get(const zan_json_value_t *obj, const char *key);
zan_json_value_t *zan_json_index(const zan_json_value_t *arr, int idx);
const char *zan_json_as_string(const zan_json_value_t *val);
double zan_json_as_number(const zan_json_value_t *val);
bool zan_json_as_bool(const zan_json_value_t *val);

/* Builder helpers */
zan_json_value_t *zan_json_new_object(void);
zan_json_value_t *zan_json_new_array(void);
zan_json_value_t *zan_json_new_string(const char *str);
zan_json_value_t *zan_json_new_number(double num);
zan_json_value_t *zan_json_new_bool(bool val);
zan_json_value_t *zan_json_new_null(void);
void zan_json_object_set(zan_json_value_t *obj, const char *key, zan_json_value_t *val);
void zan_json_array_push(zan_json_value_t *arr, zan_json_value_t *val);

/* Free a JSON value tree */
void zan_json_free(zan_json_value_t *val);

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} zan_string_builder_t;

void zan_sb_init(zan_string_builder_t *sb);
void zan_sb_append(zan_string_builder_t *sb, const char *str);
void zan_sb_append_char(zan_string_builder_t *sb, char c);
void zan_sb_append_int(zan_string_builder_t *sb, int64_t val);
void zan_sb_append_double(zan_string_builder_t *sb, double val);
char *zan_sb_to_string(zan_string_builder_t *sb);
void zan_sb_clear(zan_string_builder_t *sb);
void zan_sb_destroy(zan_string_builder_t *sb);

#endif /* ZAN_STDLIB_EXT_H */
