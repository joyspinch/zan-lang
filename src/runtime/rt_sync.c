#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
/* 内部辅助逻辑 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
/* 内部辅助逻辑 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "rt_sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rt_timer.h"           /* 核心系统底层抽象与内存语义契约 */
#include "../common/zan_abi.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(__linux__) && !defined(__GLIBC_PREREQ)
#include <features.h>
#endif

#if defined(__ANDROID__) || (defined(__OHOS__) && !defined(ZAN_SYNC_NO_SHM_SHIM))
/* 内部辅助实现 */
static const char *zan_android_shm_dir(void) {
    const char *d = getenv("ZAN_SHM_DIR");
    return (d && d[0]) ? d : "/data/local/tmp";
}
/* 内部辅助实现 */
static int zan_android_shm_path(const char *name, char *path, size_t cap) {
    if (name[0] == '/') name++;
    int n = snprintf(path, cap, "%s/%s", zan_android_shm_dir(), name);
    if (n < 0 || (size_t)n >= cap) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}
static int zan_android_shm_open(const char *name, int flags, mode_t mode) {
    char path[1024];
    if (zan_android_shm_path(name, path, sizeof(path)) != 0) return -1;
    return open(path, flags | O_CLOEXEC, mode);
}
static int zan_android_shm_unlink(const char *name) {
    char path[1024];
    if (zan_android_shm_path(name, path, sizeof(path)) != 0) return -1;
    return unlink(path);
}
#define shm_open(n, f, m) zan_android_shm_open((n), (f), (m))
#define shm_unlink(n) zan_android_shm_unlink((n))
#endif

#if !defined(_WIN32) && !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif
#if !defined(_WIN32) && !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif

#define ZAN_TABLE_MAGIC UINT64_C(0x5a414e54424c3031)
/* 内部辅助逻辑 */
#define ZAN_TABLE_VERSION 5
#define ZAN_TABLE_MAX_COLUMNS 64
#define ZAN_TABLE_COLUMN_NAME 32
/* 核心系统底层抽象与内存语义契约 */
#define ZAN_TABLE_MAX_KEY 1024
#define ZAN_TABLE_MAX_STRING 1048576
#define ZAN_TABLE_MAX_CAPACITY (UINT64_C(1) << 30)
#define ZAN_TABLE_INT 1
#define ZAN_TABLE_STRING 2
#define ZAN_TABLE_FLOAT 3
#define ZAN_SLOT_EMPTY 0
#define ZAN_SLOT_USED 1
#define ZAN_SLOT_TOMBSTONE 2
#define ZAN_SLOT_PREFIX 32

typedef struct {
#ifdef _WIN32
    volatile LONG64 value;
#else
    int64_t value;
#endif
} zan_atomic_int;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t offset;
    uint32_t reserved;
    char name[ZAN_TABLE_COLUMN_NAME];
} zan_shared_column;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t ready;
    uint64_t total_size;
    uint64_t capacity;
    uint64_t count;
    uint64_t expiry_count;
    uint32_t key_size;
    uint32_t row_stride;
    uint32_t column_count;
    /* 内部辅助逻辑 */
    uint32_t struct_lock;
    zan_shared_column columns[ZAN_TABLE_MAX_COLUMNS];
} zan_shared_header;

typedef struct {
    zan_shared_header *header;
    size_t mapped_size;
    char map_name[256];
    /* 内部辅助逻辑 */
    int anonymous;
    int attached;   /* 模块核心语义抽象与接口调用契约 */
#ifdef _WIN32
    HANDLE mapping;
#else
    int fd;
    pthread_mutex_t local_mutex;
    int local_mutex_ready;
#endif
} zan_shared_table;

/* 内部辅助逻辑 */
#ifdef _WIN32
static INIT_ONCE zan_shared_string_once = INIT_ONCE_STATIC_INIT;
static DWORD zan_shared_string_slot = FLS_OUT_OF_INDEXES;

static void CALLBACK zan_shared_string_free(void *buffer) {
    free(buffer);
}

static BOOL CALLBACK zan_shared_string_init(
    PINIT_ONCE once, void *parameter, void **context) {
    (void)once;
    (void)parameter;
    (void)context;
    zan_shared_string_slot = FlsAlloc(zan_shared_string_free);
    return zan_shared_string_slot != FLS_OUT_OF_INDEXES;
}

static char *zan_get_shared_string(void) {
    char *buffer;
    if (!InitOnceExecuteOnce(
            &zan_shared_string_once, zan_shared_string_init, NULL, NULL)) {
        zan_rt_fatal("oom", "sync: shared-string TLS init failed");
    }
    buffer = (char *)FlsGetValue(zan_shared_string_slot);
    if (!buffer) {
        buffer = (char *)calloc(ZAN_TABLE_MAX_STRING + 1, 1);
        if (!FlsSetValue(zan_shared_string_slot, buffer)) {
            free(buffer);
            zan_rt_fatal("oom", "sync: shared-string TLS set failed");
        }
    }
    return buffer;
}
#else
static pthread_key_t zan_shared_string_key;

static void zan_shared_string_dtor(void *buffer) { free(buffer); }

static void zan_shared_string_key_create(void) {
    if (pthread_key_create(&zan_shared_string_key, zan_shared_string_dtor) != 0)
        zan_rt_fatal("oom", "sync: shared-string TLS init failed");
}

static char *zan_get_shared_string(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, zan_shared_string_key_create);
    char *buffer = (char *)pthread_getspecific(zan_shared_string_key);
    if (!buffer) {
        buffer = (char *)calloc(ZAN_TABLE_MAX_STRING + 1, 1);
        if (pthread_setspecific(zan_shared_string_key, buffer) != 0) {
            free(buffer);
            zan_rt_fatal("oom", "sync: shared-string TLS set failed");
        }
    }
    return buffer;
}
#endif

static size_t zan_align8(size_t value) {
    return (value + 7u) & ~(size_t)7u;
}

static size_t zan_strnlen(const char *value, size_t max_len) {
    size_t len = 0;
    if (!value) return 0;
    while (len < max_len && value[len]) len++;
    return len;
}

static uint64_t zan_hash_bytes(const char *value) {
    uint64_t hash = UINT64_C(1469598103934665603);
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    while (*p) {
        hash ^= *p++;
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}

static uint64_t zan_round_capacity(uint64_t capacity) {
    uint64_t rounded = 1;
    while (rounded < capacity && rounded < ZAN_TABLE_MAX_CAPACITY) rounded <<= 1;
    return rounded;
}

static int zan_column_name_valid(const char *name, size_t len) {
    if (len == 0 || len >= ZAN_TABLE_COLUMN_NAME) return 0;
    for (size_t i = 0; i < len; i++) {
        char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_')) {
            return 0;
        }
    }
    return 1;
}

static int zan_parse_schema(
    const char *schema,
    uint32_t key_size,
    zan_shared_column columns[ZAN_TABLE_MAX_COLUMNS],
    uint32_t *column_count,
    uint32_t *row_stride) {
    const char *p = schema;
    uint32_t count = 0;
    size_t offset = zan_align8(ZAN_SLOT_PREFIX + key_size);

    if (!schema || !*schema) return 0;
    while (*p) {
        if (count >= ZAN_TABLE_MAX_COLUMNS) return 0;
        uint32_t type;
        uint32_t size;
        if (*p == 'i') {
            type = ZAN_TABLE_INT;
            size = 8;
            p++;
            if (*p++ != ':') return 0;
        } else if (*p == 'f') {
            type = ZAN_TABLE_FLOAT;
            size = 8;
            p++;
            if (*p++ != ':') return 0;
        } else if (*p == 's') {
            char *end = NULL;
            unsigned long parsed;
            type = ZAN_TABLE_STRING;
            p++;
            if (*p++ != ':') return 0;
            parsed = strtoul(p, &end, 10);
            if (end == p || !end || *end != ':' ||
                parsed == 0 || parsed > ZAN_TABLE_MAX_STRING) {
                return 0;
            }
            size = (uint32_t)parsed + 1;
            p = end + 1;
        } else {
            return 0;
        }

        const char *name = p;
        while (*p && *p != ';') p++;
        size_t name_len = (size_t)(p - name);
        if (*p != ';' || !zan_column_name_valid(name, name_len)) return 0;
        for (uint32_t i = 0; i < count; i++) {
            if (strlen(columns[i].name) == name_len &&
                memcmp(columns[i].name, name, name_len) == 0) {
                return 0;
            }
        }

        if (type == ZAN_TABLE_INT || type == ZAN_TABLE_FLOAT) {
            offset = zan_align8(offset);
        }
        columns[count].type = type;
        columns[count].size = size;
        columns[count].offset = (uint32_t)offset;
        memcpy(columns[count].name, name, name_len);
        columns[count].name[name_len] = '\0';
        offset += size;
        count++;
        p++;
    }

    offset = zan_align8(offset);
    if (offset > UINT32_MAX) return 0;
    *column_count = count;
    *row_stride = (uint32_t)offset;
    return count > 0;
}

static void zan_make_names(const char *name, char map_name[256]) {
    unsigned long long hash = (unsigned long long)zan_hash_bytes(name);
#ifdef _WIN32
    snprintf(map_name, 256, "Local\\zan_table_%016llx", hash);
#else
    snprintf(
        map_name, 256, "/tmp/zan_table_%lu_%016llx.shm",
        (unsigned long)getuid(), hash);
#endif
}

/* CAS 自旋锁保护共享内存结构 */
/* 内部辅助实现 */
#define ZAN_LOCK_HELD_BIT 0x80000000u
#define ZAN_LOCK_PID_MASK 0x7FFFFFFFu

static int zan_pid_alive(uint32_t pid) {
    if (!pid) return 0;
#ifdef _WIN32
    /* 内部辅助实现 */
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                           FALSE, (DWORD)pid);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
    int alive = WaitForSingleObject(h, 0) != WAIT_OBJECT_0;
    CloseHandle(h);
    return alive;
#else
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

static void zan_lock_word_acquire(volatile uint32_t *word) {
#ifdef _WIN32
    LONG mine = (LONG)(ZAN_LOCK_HELD_BIT |
                       ((uint32_t)GetCurrentProcessId() & ZAN_LOCK_PID_MASK));
    for (unsigned spin = 0;; spin++) {
        if (InterlockedCompareExchange((volatile LONG *)word, mine, 0) == 0)
            return;
        if (spin < 64) {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        } else if (spin < 4096 || (spin & 1023u) != 0) {
            SwitchToThread();
        } else {
            LONG held = InterlockedCompareExchange((volatile LONG *)word,
                                                   0, 0);
            LONG hpid = held & (LONG)ZAN_LOCK_PID_MASK;
            if ((held & (LONG)ZAN_LOCK_HELD_BIT) && hpid &&
                !zan_pid_alive((uint32_t)hpid)) {
                /* 内部辅助实现 */
                LONG now = InterlockedCompareExchange((volatile LONG *)word,
                                                      0, 0);
                if (now == held && !zan_pid_alive((uint32_t)hpid)) {
                    InterlockedCompareExchange((volatile LONG *)word, 0, held);
                }
            }
            SwitchToThread();
        }
    }
#else
    uint32_t mine = ZAN_LOCK_HELD_BIT |
                    ((uint32_t)getpid() & ZAN_LOCK_PID_MASK);
    for (unsigned spin = 0;; spin++) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(word, &expected, mine, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return;
        }
        if (spin < 64) {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        } else if (spin < 4096 || (spin & 1023u) != 0) {
            sched_yield();
        } else {
            uint32_t held = __atomic_load_n(word, __ATOMIC_RELAXED);
            uint32_t hpid = held & ZAN_LOCK_PID_MASK;
            if ((held & ZAN_LOCK_HELD_BIT) && hpid &&
                !zan_pid_alive(hpid)) {
                /* 模块核心语义抽象与接口调用契约 */
                uint32_t now = __atomic_load_n(word, __ATOMIC_ACQUIRE);
                if (now == held && !zan_pid_alive(hpid)) {
                    uint32_t expected = held;
                    __atomic_compare_exchange_n(word, &expected, 0, 0,
                                                __ATOMIC_RELEASE,
                                                __ATOMIC_RELAXED);
                }
            }
            sched_yield();
        }
    }
#endif
}

static void zan_lock_word_release(volatile uint32_t *word) {
#ifdef _WIN32
    InterlockedExchange((volatile LONG *)word, 0);
#else
    __atomic_store_n(word, 0, __ATOMIC_RELEASE);
#endif
}

static void zan_struct_lock(zan_shared_header *header) {
    zan_lock_word_acquire(&header->struct_lock);
}

static void zan_struct_unlock(zan_shared_header *header) {
    zan_lock_word_release(&header->struct_lock);
}

/* 按键查找或分配共享表行槽位 */
static unsigned char *zan_row_for(
    zan_shared_header *header, const char *key, int create);

static zan_shared_column *zan_find_column(
    zan_shared_header *header, const char *name, uint32_t type) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < header->column_count; i++) {
        zan_shared_column *column = &header->columns[i];
        if (column->type == type && strcmp(column->name, name) == 0) {
            return column;
        }
    }
    return NULL;
}

static unsigned char *zan_row_at(zan_shared_header *header, uint64_t index) {
    size_t rows_offset = zan_align8(sizeof(*header));
    return (unsigned char *)header + rows_offset +
           (size_t)index * header->row_stride;
}

static uint64_t zan_row_slot(zan_shared_header *header, unsigned char *row) {
    size_t rows_offset = zan_align8(sizeof(*header));
    return (uint64_t)((row - ((unsigned char *)header + rows_offset)) /
        header->row_stride);
}

static uint64_t *zan_expiry_heap(zan_shared_header *header) {
    size_t rows_offset = zan_align8(sizeof(*header));
    return (uint64_t *)((unsigned char *)header + rows_offset +
        (size_t)header->capacity * header->row_stride);
}

static uint32_t *zan_row_state(unsigned char *row) {
    return (uint32_t *)row;
}

static uint32_t zan_load_state(unsigned char *row) {
#ifdef _WIN32
    return (uint32_t)InterlockedCompareExchange(
        (volatile LONG *)zan_row_state(row), 0, 0);
#else
    return __atomic_load_n(zan_row_state(row), __ATOMIC_ACQUIRE);
#endif
}

static void zan_publish_state(unsigned char *row, uint32_t state) {
#ifdef _WIN32
    InterlockedExchange((volatile LONG *)zan_row_state(row), (LONG)state);
#else
    __atomic_store_n(zan_row_state(row), state, __ATOMIC_RELEASE);
#endif
}

static uint32_t *zan_row_lock_word(unsigned char *row) {
    return (uint32_t *)(row + 4);
}

static uint64_t *zan_row_hash(unsigned char *row) {
    return (uint64_t *)(row + 8);
}

static int64_t *zan_row_expires_at(unsigned char *row) {
    return (int64_t *)(row + 16);
}

static uint64_t *zan_row_heap_index(unsigned char *row) {
    return (uint64_t *)(row + 24);
}

static char *zan_row_key(unsigned char *row) {
    return (char *)(row + ZAN_SLOT_PREFIX);
}

/* 获取行锁后重新校验有效性 */
static int zan_row_revalidate(
    unsigned char *row, uint64_t hash, const char *key, uint32_t key_size) {
    if (zan_load_state(row) != ZAN_SLOT_USED) return 0;
    if (*zan_row_hash(row) != hash) return 0;
    if (key &&
        strncmp((const char *)zan_row_key(row), key, key_size) != 0)
        return 0;
    return 1;
}

static void zan_row_lock(unsigned char *row) {
    zan_lock_word_acquire(zan_row_lock_word(row));
}

static void zan_row_unlock(unsigned char *row) {
    zan_lock_word_release(zan_row_lock_word(row));
}

static void zan_expiry_swap(zan_shared_header *header, uint64_t a, uint64_t b) {
    uint64_t *heap = zan_expiry_heap(header);
    uint64_t slot = heap[a];
    heap[a] = heap[b];
    heap[b] = slot;
    *zan_row_heap_index(zan_row_at(header, heap[a])) = a + 1;
    *zan_row_heap_index(zan_row_at(header, heap[b])) = b + 1;
}

static void zan_expiry_up(zan_shared_header *header, uint64_t index) {
    uint64_t *heap = zan_expiry_heap(header);
    while (index > 0) {
        uint64_t parent = (index - 1) / 2;
        int64_t expires = *zan_row_expires_at(zan_row_at(header, heap[index]));
        int64_t parent_expires = *zan_row_expires_at(zan_row_at(header, heap[parent]));
        if (expires >= parent_expires) break;
        zan_expiry_swap(header, index, parent);
        index = parent;
    }
}

static void zan_expiry_down(zan_shared_header *header, uint64_t index) {
    uint64_t *heap = zan_expiry_heap(header);
    for (;;) {
        uint64_t left = index * 2 + 1;
        if (left >= header->expiry_count) break;
        uint64_t right = left + 1;
        uint64_t smallest = left;
        if (right < header->expiry_count &&
            *zan_row_expires_at(zan_row_at(header, heap[right])) <
            *zan_row_expires_at(zan_row_at(header, heap[left]))) {
            smallest = right;
        }
        if (*zan_row_expires_at(zan_row_at(header, heap[index])) <=
            *zan_row_expires_at(zan_row_at(header, heap[smallest]))) break;
        zan_expiry_swap(header, index, smallest);
        index = smallest;
    }
}

static void zan_expiry_remove(zan_shared_header *header, unsigned char *row) {
    uint64_t encoded = *zan_row_heap_index(row);
    if (encoded == 0 || encoded > header->expiry_count) return;
    uint64_t index = encoded - 1;
    uint64_t *heap = zan_expiry_heap(header);
    uint64_t last = --header->expiry_count;
    *zan_row_heap_index(row) = 0;
    *zan_row_expires_at(row) = 0;
    if (index == last) return;
    heap[index] = heap[last];
    *zan_row_heap_index(zan_row_at(header, heap[index])) = index + 1;
    if (index > 0 &&
        *zan_row_expires_at(zan_row_at(header, heap[index])) <
        *zan_row_expires_at(zan_row_at(header, heap[(index - 1) / 2]))) {
        zan_expiry_up(header, index);
    } else {
        zan_expiry_down(header, index);
    }
}

static void zan_expiry_set(
    zan_shared_header *header, uint64_t slot, unsigned char *row, int64_t expires_at) {
    uint64_t encoded = *zan_row_heap_index(row);
    *zan_row_expires_at(row) = expires_at;
    if (expires_at <= 0) {
        if (encoded != 0) zan_expiry_remove(header, row);
        return;
    }
    if (encoded == 0) {
        uint64_t index = header->expiry_count++;
        zan_expiry_heap(header)[index] = slot;
        *zan_row_heap_index(row) = index + 1;
        zan_expiry_up(header, index);
        return;
    }
    uint64_t index = encoded - 1;
    zan_expiry_up(header, index);
    encoded = *zan_row_heap_index(row);
    zan_expiry_down(header, encoded - 1);
}

static int64_t zan_wall_now_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&ft);
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (int64_t)(value.QuadPart / UINT64_C(10000) - UINT64_C(11644473600000));
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* 共享表惰性过期淘汰机制 */
#define ZAN_PURGE_MIN_INTERVAL_MS 16
#define ZAN_PURGE_MAX_BATCH 64

static void zan_purge_expired_locked(
    zan_shared_header *header, int64_t now_ms, uint64_t max_batch) {
    uint64_t *heap = zan_expiry_heap(header);
    uint64_t batch = 0;
    while (header->expiry_count > 0 && batch < max_batch) {
        unsigned char *row = zan_row_at(header, heap[0]);
        if (*zan_row_expires_at(row) > now_ms) break;
        zan_row_lock(row);
        zan_expiry_remove(header, row);
        /* 内部辅助实现 */
        memset(row + 8, 0, header->row_stride - 8);
        zan_publish_state(row, ZAN_SLOT_TOMBSTONE);
        header->count--;
        zan_row_unlock(row);
        batch++;
    }
}

static void zan_purge_expired(zan_shared_header *header) {
    /* 模块核心语义抽象与接口调用契约 */
    if (__atomic_load_n(&header->expiry_count, __ATOMIC_RELAXED) == 0)
        return;
    /* 内部辅助逻辑 */
    static int64_t last_attempt_ms;
    int64_t now_ms = zan_wall_now_ms();
    int64_t last = __atomic_load_n(&last_attempt_ms, __ATOMIC_RELAXED);
    if (last > now_ms - ZAN_PURGE_MIN_INTERVAL_MS && last <= now_ms) return;
    __atomic_store_n(&last_attempt_ms, now_ms, __ATOMIC_RELAXED);
    uint64_t *heap = zan_expiry_heap(header);
    uint64_t top = __atomic_load_n(&heap[0], __ATOMIC_RELAXED);
    if (top >= header->capacity) return; /* 底层系统交互与数据协议契约 */
    unsigned char *row = zan_row_at(header, top);
    if (*zan_row_expires_at(row) > now_ms) return;
    zan_struct_lock(header);
    zan_purge_expired_locked(header, now_ms, ZAN_PURGE_MAX_BATCH);
    zan_struct_unlock(header);
}

static unsigned char *zan_find_row(
    zan_shared_header *header, const char *key, int create) {
    size_t key_len = zan_strnlen(key, header->key_size + 1u);
    if (!key || key_len == 0 || key_len > header->key_size) return NULL;

    /* 共享表负载因子达到 7/8 时拒绝插入并扩容 */
    if (create && header->count >= header->capacity - header->capacity / 8)
        return NULL;

    uint64_t hash = zan_hash_bytes(key);
    uint64_t first_tombstone = UINT64_MAX;
    for (uint64_t probe = 0; probe < header->capacity; probe++) {
        uint64_t index = (hash + probe) & (header->capacity - 1);
        unsigned char *row = zan_row_at(header, index);
        uint32_t state = zan_load_state(row);
        if (state == ZAN_SLOT_USED) {
            if (*zan_row_hash(row) == hash &&
                strncmp(zan_row_key(row), key, header->key_size) == 0) {
                return row;
            }
            continue;
        }
        if (state == ZAN_SLOT_TOMBSTONE) {
            if (first_tombstone == UINT64_MAX) first_tombstone = index;
            continue;
        }
        if (!create) return NULL;
        if (first_tombstone != UINT64_MAX) row = zan_row_at(header, first_tombstone);
        /* 内部辅助实现 */
        *zan_row_hash(row) = hash;
        memcpy(zan_row_key(row), key, key_len);
        if ((size_t)key_len < header->key_size)
            zan_row_key(row)[key_len] = '\0';
        /* 内部辅助逻辑 */
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }

    if (create && first_tombstone != UINT64_MAX) {
        unsigned char *row = zan_row_at(header, first_tombstone);
        /* 底层系统交互与数据协议契约 */
        *zan_row_hash(row) = hash;
        memcpy(zan_row_key(row), key, key_len);
        if ((size_t)key_len < header->key_size)
            zan_row_key(row)[key_len] = '\0';
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }
    return NULL;
}

static unsigned char *zan_row_for(
    zan_shared_header *header, const char *key, int create) {
    zan_purge_expired(header);
    unsigned char *row = zan_find_row(header, key, 0);
    if (row || !create) return row;
    /* 内部辅助逻辑 */
    zan_struct_lock(header);
    row = zan_find_row(header, key, 1);
    zan_struct_unlock(header);
    return row;
}

/* 内部辅助实现 */

static unsigned char *zan_find_row_hash(
    zan_shared_header *header, uint64_t hash, int create) {
    /* 底层系统交互与数据协议契约 */
    if (create && header->count >= header->capacity - header->capacity / 8)
        return NULL;
    if (!hash) hash = 1;
    uint64_t first_tombstone = UINT64_MAX;
    for (uint64_t probe = 0; probe < header->capacity; probe++) {
        uint64_t index = (hash + probe) & (header->capacity - 1);
        unsigned char *row = zan_row_at(header, index);
        uint32_t state = zan_load_state(row);
        if (state == ZAN_SLOT_USED) {
            if (*zan_row_hash(row) == hash) return row;
            continue;
        }
        if (state == ZAN_SLOT_TOMBSTONE) {
            if (first_tombstone == UINT64_MAX) first_tombstone = index;
            continue;
        }
        if (!create) return NULL;
        if (first_tombstone != UINT64_MAX) row = zan_row_at(header, first_tombstone);
        /* 内部辅助逻辑 */
        *zan_row_hash(row) = hash;
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }
    if (create && first_tombstone != UINT64_MAX) {
        unsigned char *row = zan_row_at(header, first_tombstone);
        /* 底层系统交互与数据协议契约 */
        *zan_row_hash(row) = hash;
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }
    return NULL;
}

static unsigned char *zan_row_for_hash(
    zan_shared_header *header, uint64_t hash, int create) {
    zan_purge_expired(header);
    unsigned char *row = zan_find_row_hash(header, hash, 0);
    if (row || !create) return row;
    zan_struct_lock(header);
    row = zan_find_row_hash(header, hash, 1);
    zan_struct_unlock(header);
    return row;
}

static void zan_shared_table_free(zan_shared_table *table) {
    if (!table) return;
#ifdef _WIN32
    if (table->header) UnmapViewOfFile(table->header);
    if (table->mapping) CloseHandle(table->mapping);
#else
    if (table->header && table->mapped_size) {
        munmap(table->header, table->mapped_size);
    }
    if (table->fd >= 0) close(table->fd);
    if (table->local_mutex_ready) pthread_mutex_destroy(&table->local_mutex);
#endif
    free(table);
}

/* 在独立操作系统线程上派生运行 Zan 委托 */

typedef void (*zan_thread_body_fn)(void);

/* 模块核心语义抽象与接口调用契约 */
static void *zan_delegate_record(void *d);
static void zan_delegate_retain(void *d);
static void zan_delegate_release(void *d);

static void zan_thread_invoke(void *body) {
    void *rec = zan_delegate_record(body);
    if (!rec) {
        ((zan_thread_body_fn)body)();
        return;
    }
    void (*fn)(void *) =
        *(void (**)(void *))((char *)rec + ZAN_CLOSURE_FN_OFF);
    if (fn) fn(rec);
}

/* 内部辅助逻辑 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void __zan_eh_release(void) { }
#else
void __zan_eh_release(void) { }
#endif

/* 模块核心语义抽象与接口调用契约 */
void zan_thread_detach(void) {
    __zan_eh_release();
}

#ifdef _WIN32
static DWORD WINAPI zan_thread_trampoline(LPVOID arg) {
    zan_thread_invoke(arg);
    zan_delegate_release(arg); /* 底层系统交互与数据协议契约 */
    zan_thread_detach();
    return 0;
}

int32_t zan_thread_start(void *body) {
    if (!body) return 0;
    /* 内部辅助逻辑 */
    zan_delegate_retain(body);
    HANDLE h = CreateThread(NULL, 0, zan_thread_trampoline, body, 0, NULL);
    if (!h) {
        zan_delegate_release(body); /* 底层系统交互与数据协议契约 */
        return 0;
    }
    CloseHandle(h); /* 底层系统交互与数据协议契约 */
    return 1;
}
#else
static void *zan_thread_trampoline(void *arg) {
    zan_thread_invoke(arg);
    zan_delegate_release(arg); /* 底层系统交互与数据协议契约 */
    zan_thread_detach();
    return NULL;
}

int32_t zan_thread_start(void *body) {
    if (!body) return 0;
    /* 内部辅助逻辑 */
    zan_delegate_retain(body);
    pthread_t t;
    if (pthread_create(&t, NULL, zan_thread_trampoline, body) != 0) {
        zan_delegate_release(body); /* 底层系统交互与数据协议契约 */
        return 0;
    }
    pthread_detach(t);
    return 1;
}
#endif

#if defined(__linux__)
/* 内部辅助实现 */
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 30)
#define zan_tid() gettid()
#else
#include <sys/syscall.h>
#define zan_tid() ((long)syscall(SYS_gettid))
#endif
#else
#include <sys/syscall.h>
#define zan_tid() ((long)syscall(SYS_gettid))
#endif
#elif defined(__APPLE__)
#include <pthread.h>
#endif

int64_t zan_thread_current_id(void) {
#ifdef _WIN32
    return (int64_t)GetCurrentThreadId();
#elif defined(__linux__)
    return (int64_t)zan_tid();
#elif defined(__APPLE__)
    /* 内部辅助逻辑 */
    uint64_t tid = 0;
    if (pthread_threadid_np(NULL, &tid) != 0) return 0;
    return (int64_t)tid;
#else
    /* 模块核心语义抽象与接口调用契约 */
    uintptr_t self = (uintptr_t)pthread_self();
    return (int64_t)((self >> 16) ^ (self & 0xffffffffu));
#endif
}

/* 内部辅助逻辑 */

#define ZAN_MONITOR_STRIPES 64

static unsigned zan_monitor_stripe(void *obj) {
    uintptr_t bits = (uintptr_t)obj;
    /* 内部辅助逻辑 */
    bits ^= bits >> 20;
    bits ^= bits >> 8;
    return (unsigned)((bits >> 4) & (ZAN_MONITOR_STRIPES - 1));
}

#ifdef _WIN32
static CRITICAL_SECTION g_monitor_cs[ZAN_MONITOR_STRIPES];
static INIT_ONCE g_monitor_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK zan_monitor_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    for (unsigned i = 0; i < ZAN_MONITOR_STRIPES; i++)
        InitializeCriticalSection(&g_monitor_cs[i]);
    return TRUE;
}
void zan_monitor_enter(void *obj) {
    InitOnceExecuteOnce(&g_monitor_once, zan_monitor_init, NULL, NULL);
    EnterCriticalSection(&g_monitor_cs[zan_monitor_stripe(obj)]);
}
void zan_monitor_exit(void *obj) {
    LeaveCriticalSection(&g_monitor_cs[zan_monitor_stripe(obj)]);
}
#else
static pthread_mutex_t g_monitor_mx[ZAN_MONITOR_STRIPES];
static pthread_once_t g_monitor_once = PTHREAD_ONCE_INIT;
static void zan_monitor_init(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    for (unsigned i = 0; i < ZAN_MONITOR_STRIPES; i++)
        pthread_mutex_init(&g_monitor_mx[i], &attr);
    pthread_mutexattr_destroy(&attr);
}
void zan_monitor_enter(void *obj) {
    pthread_once(&g_monitor_once, zan_monitor_init);
    pthread_mutex_lock(&g_monitor_mx[zan_monitor_stripe(obj)]);
}
void zan_monitor_exit(void *obj) {
    pthread_mutex_unlock(&g_monitor_mx[zan_monitor_stripe(obj)]);
}
#endif

/* UI 主线程安全分发任务队列 */

/* 内部辅助逻辑 */
/* 内部辅助逻辑 */
#define ZAN_DISPATCH_CAP0 64
#define ZAN_DISPATCH_CAP_MAX (1u << 20)
static void *g_dispatch_static[ZAN_DISPATCH_CAP0];
static void **g_dispatch_ring = g_dispatch_static;
static int g_dispatch_cap = ZAN_DISPATCH_CAP0;
static int g_dispatch_head = 0;
static int g_dispatch_tail = 0;

#ifdef _WIN32
/* 内部辅助实现 */
static CRITICAL_SECTION g_dispatch_cs;
static INIT_ONCE g_dispatch_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK zan_dispatch_cs_init(PINIT_ONCE once, PVOID param,
                                          PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&g_dispatch_cs);
    return TRUE;
}

static void zan_dispatch_lock(void) {
    InitOnceExecuteOnce(&g_dispatch_once, zan_dispatch_cs_init, NULL, NULL);
    EnterCriticalSection(&g_dispatch_cs);
}
static void zan_dispatch_unlock(void) { LeaveCriticalSection(&g_dispatch_cs); }
#else
static pthread_mutex_t g_dispatch_mx = PTHREAD_MUTEX_INITIALIZER;
static void zan_dispatch_lock(void) { pthread_mutex_lock(&g_dispatch_mx); }
static void zan_dispatch_unlock(void) { pthread_mutex_unlock(&g_dispatch_mx); }
#endif

static volatile uintptr_t g_ui_thread_id = 0;

void zan_ui_thread_set(void) {
#ifdef _WIN32
    g_ui_thread_id = (uintptr_t)GetCurrentThreadId();
#else
    g_ui_thread_id = (uintptr_t)pthread_self();
#endif
}

int32_t zan_ui_thread_check(void) {
    if (!g_ui_thread_id) return 1; /* 核心系统底层抽象与内存语义契约 */
#ifdef _WIN32
    return (uintptr_t)GetCurrentThreadId() == g_ui_thread_id ? 1 : 0;
#else
    return pthread_equal((pthread_t)g_ui_thread_id, pthread_self()) ? 1 : 0;
#endif
}

void zan_ui_thread_assert(const char *msg) {
    if (!zan_ui_thread_check()) {
        fprintf(stderr, "fatal error: UI thread assertion failed: %s (called from background thread)\n",
                msg ? msg : "must be called on UI thread");
        abort();
    }
}

/* 模块核心语义抽象与接口调用契约 */
void zan_dispatch_init(void) {
    zan_ui_thread_set();
    zan_dispatch_lock();
    g_dispatch_head = 0;
    g_dispatch_tail = 0;
    zan_dispatch_unlock();
}

/* 内部辅助逻辑 */
static void *zan_delegate_record(void *d) {
    uintptr_t v = (uintptr_t)d;
    if (!(v & (uintptr_t)ZAN_CLOSURE_TAG)) return NULL;
    return (void *)(v & ~(uintptr_t)ZAN_CLOSURE_TAG);
}

/* 核心系统底层抽象与内存语义契约 */
static void zan_delegate_retain(void *d) {
    void *rec = zan_delegate_record(d);
    if (!rec) return;
    volatile int64_t *rc = (volatile int64_t *)((char *)rec + ZAN_OBJ_RC_OFF);
#ifdef _WIN32
    InterlockedIncrement64((volatile LONG64 *)rc);
#else
    __atomic_add_fetch((int64_t *)rc, (int64_t)1, __ATOMIC_SEQ_CST);
#endif
}

/* 核心系统底层抽象与内存语义契约 */
static void zan_delegate_release(void *d) {
    void *rec = zan_delegate_record(d);
    if (!rec) return;
    void *dtor = *(void **)((char *)rec + ZAN_CLOSURE_DTOR_OFF);
    if (dtor) ((void (*)(void *))dtor)(rec);
}

/* 底层系统交互与数据协议契约 */
static int zan_dispatch_grow(void) {
    if ((unsigned)g_dispatch_cap >= ZAN_DISPATCH_CAP_MAX) return 0;
    int ncap = g_dispatch_cap * 2;
    void **nring = (void **)malloc((size_t)ncap * sizeof *nring);
    if (!nring) return 0;
    int count = 0;
    for (int i = g_dispatch_head; i != g_dispatch_tail;
         i = (i + 1) % g_dispatch_cap)
        nring[count++] = g_dispatch_ring[i];
    if (g_dispatch_ring != g_dispatch_static) free(g_dispatch_ring);
    g_dispatch_ring = nring;
    g_dispatch_cap = ncap;
    g_dispatch_head = 0;
    g_dispatch_tail = count;
    return 1;
}

/* Enqueue a delegate to run on the UI thread */
int32_t zan_dispatch_post(void *fn) {
    if (!fn) return 0;
    int32_t ok = 0;
    zan_dispatch_lock();
    int next = (g_dispatch_tail + 1) % g_dispatch_cap;
    if (next == g_dispatch_head && zan_dispatch_grow())
        next = (g_dispatch_tail + 1) % g_dispatch_cap;
    if (next != g_dispatch_head) {
        zan_delegate_retain(fn);
        g_dispatch_ring[g_dispatch_tail] = fn;
        g_dispatch_tail = next;
        ok = 1;
    }
    zan_dispatch_unlock();
    if (!ok) {
        /* 内部辅助逻辑 */
        static int reported;
        if (!reported) {
            reported = 1;
            fprintf(stderr, "zan: UI dispatch queue full (%d pending); "
                            "posted handler dropped\n", g_dispatch_cap - 1);
        }
    }
    return ok;
}

/* 模块核心语义抽象与接口调用契约 */
void *zan_dispatch_take(void) {
    void *fn = NULL;
    zan_dispatch_lock();
    if (g_dispatch_head != g_dispatch_tail) {
        fn = g_dispatch_ring[g_dispatch_head];
        g_dispatch_head = (g_dispatch_head + 1) % g_dispatch_cap;
    }
    zan_dispatch_unlock();
    return fn;
}

/* 核心系统底层抽象与内存语义契约 */
void zan_dispatch_clear(void) {
    /* 内部辅助实现 */
    for (;;) {
        void *drop[64];
        int n = 0;
        zan_dispatch_lock();
        while (n < (int)(sizeof drop / sizeof *drop) &&
               g_dispatch_head != g_dispatch_tail) {
            drop[n++] = g_dispatch_ring[g_dispatch_head];
            g_dispatch_head = (g_dispatch_head + 1) % g_dispatch_cap;
        }
        if (g_dispatch_head == g_dispatch_tail) {
            g_dispatch_head = 0;
            g_dispatch_tail = 0;
        }
        zan_dispatch_unlock();
        for (int i = 0; i < n; i++) zan_delegate_release(drop[i]);
        if (n < (int)(sizeof drop / sizeof *drop)) return;
    }
}

int64_t zan_atomic_int_create(int64_t initial_value) {
    zan_atomic_int *atomic = (zan_atomic_int *)malloc(sizeof(*atomic));
    if (!atomic) return 0;
#ifdef _WIN32
    atomic->value = (LONG64)initial_value;
#else
    __atomic_store_n(&atomic->value, initial_value, __ATOMIC_SEQ_CST);
#endif
    return (int64_t)(intptr_t)atomic;
}

void zan_atomic_int_destroy(int64_t handle) {
    free((void *)(intptr_t)handle);
}

int64_t zan_atomic_int_load(int64_t handle) {
    zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
    if (!atomic) return 0;
#ifdef _WIN32
    return (int64_t)InterlockedCompareExchange64(&atomic->value, 0, 0);
#else
    return __atomic_load_n(&atomic->value, __ATOMIC_SEQ_CST);
#endif
}

void zan_atomic_int_store(int64_t handle, int64_t value) {
    zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
    if (!atomic) return;
#ifdef _WIN32
    InterlockedExchange64(&atomic->value, (LONG64)value);
#else
    __atomic_store_n(&atomic->value, value, __ATOMIC_SEQ_CST);
#endif
}

int64_t zan_atomic_int_exchange(int64_t handle, int64_t value) {
    zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
    if (!atomic) return 0;
#ifdef _WIN32
    return (int64_t)InterlockedExchange64(&atomic->value, (LONG64)value);
#else
    return __atomic_exchange_n(&atomic->value, value, __ATOMIC_SEQ_CST);
#endif
}

int64_t zan_atomic_int_compare_exchange(
    int64_t handle, int64_t expected, int64_t desired) {
    zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
    if (!atomic) return 0;
#ifdef _WIN32
    return (int64_t)InterlockedCompareExchange64(
        &atomic->value, (LONG64)desired, (LONG64)expected);
#else
    __atomic_compare_exchange_n(
        &atomic->value, &expected, desired, 0,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
#endif
}

int64_t zan_atomic_int_add(int64_t handle, int64_t delta) {
    zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
    if (!atomic) return 0;
#ifdef _WIN32
    return (int64_t)InterlockedExchangeAdd64(&atomic->value, (LONG64)delta) + delta;
#else
    return __atomic_add_fetch(&atomic->value, delta, __ATOMIC_SEQ_CST);
#endif
}

/* 内部辅助逻辑 */
typedef struct {
    uint64_t capacity;
    uint32_t key_size;
    uint32_t column_count;
    uint32_t row_stride;
    size_t total_size;
    zan_shared_column columns[ZAN_TABLE_MAX_COLUMNS];
} zan_table_layout;

static int zan_table_layout_of(
    int32_t capacity_value, int32_t key_size_value, const char *schema,
    zan_table_layout *out) {
    if (capacity_value <= 0 ||
        capacity_value > (int32_t)ZAN_TABLE_MAX_CAPACITY ||
        key_size_value <= 0 || key_size_value > ZAN_TABLE_MAX_KEY) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->capacity = zan_round_capacity((uint64_t)capacity_value);
    out->key_size = (uint32_t)key_size_value;
    if (!zan_parse_schema(
            schema, out->key_size, out->columns, &out->column_count,
            &out->row_stride)) {
        return 0;
    }

    size_t rows_offset = zan_align8(sizeof(zan_shared_header));
    if ((size_t)out->capacity > (SIZE_MAX - rows_offset) / out->row_stride) {
        return 0;
    }
    size_t rows_size = (size_t)out->capacity * out->row_stride;
    if ((size_t)out->capacity >
        (SIZE_MAX - rows_offset - rows_size) / sizeof(uint64_t)) {
        return 0;
    }
    out->total_size =
        rows_offset + rows_size + (size_t)out->capacity * sizeof(uint64_t);
    return 1;
}

/* 内部辅助逻辑 */
static int zan_header_geometry_ok(const zan_shared_header *header) {
    if (header->capacity == 0 ||
        header->capacity > ZAN_TABLE_MAX_CAPACITY ||
        (header->capacity & (header->capacity - 1)) != 0)
        return 0;   /* 底层系统交互与数据协议契约 */
    if (header->key_size == 0 || header->key_size > ZAN_TABLE_MAX_KEY)
        return 0;
    if (header->column_count > ZAN_TABLE_MAX_COLUMNS)
        return 0;
    size_t rows_offset = zan_align8(sizeof(zan_shared_header));
    size_t min_stride = zan_align8(ZAN_SLOT_PREFIX + (size_t)header->key_size);
    if (header->row_stride < min_stride)
        return 0;
    for (uint32_t i = 0; i < header->column_count; i++) {
        const zan_shared_column *c = &header->columns[i];
        if (c->type != ZAN_TABLE_INT && c->type != ZAN_TABLE_STRING &&
            c->type != ZAN_TABLE_FLOAT)
            return 0;
        if (c->size == 0 ||
            (uint64_t)c->offset + c->size > header->row_stride)
            return 0;
        /* 底层系统交互与数据协议契约 */
        if (!memchr(c->name, '\0', sizeof(c->name)))
            return 0;
    }
    uint64_t expected =
        (uint64_t)rows_offset +
        header->capacity * (uint64_t)header->row_stride +
        header->capacity * sizeof(uint64_t);
    return expected == header->total_size;
}

/* 初始化并发布共享内存表头部结构 */
static void zan_table_init_header(
    zan_shared_table *table, const zan_table_layout *layout) {
    table->mapped_size = layout->total_size;
    /* 核心系统底层抽象与内存语义契约 */
    memset(table->header, 0, sizeof(zan_shared_header));
    table->header->magic = ZAN_TABLE_MAGIC;
    table->header->version = ZAN_TABLE_VERSION;
    table->header->total_size = layout->total_size;
    table->header->capacity = layout->capacity;
    table->header->key_size = layout->key_size;
    table->header->row_stride = layout->row_stride;
    table->header->column_count = layout->column_count;
    memcpy(table->header->columns, layout->columns,
           sizeof(table->header->columns));
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    table->header->ready = 1;
}

static zan_shared_table *zan_table_alloc(void) {
    zan_shared_table *table = (zan_shared_table *)calloc(1, sizeof(*table));
    if (!table) return NULL;
#ifndef _WIN32
    table->fd = -1;
    if (pthread_mutex_init(&table->local_mutex, NULL) != 0) {
        free(table);
        return NULL;
    }
    table->local_mutex_ready = 1;
#endif
    return table;
}

/* 内部辅助实现 */
int64_t zan_shared_table_create_anon(
    int32_t capacity_value, int32_t key_size_value, const char *schema) {
    zan_table_layout layout;
    if (!zan_table_layout_of(capacity_value, key_size_value, schema, &layout)) {
        return 0;
    }

    zan_shared_table *table = zan_table_alloc();
    if (!table) return 0;
    table->anonymous = 1;

#ifdef _WIN32
    /* 内部辅助逻辑 */
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    DWORD size_high = (DWORD)(((uint64_t)layout.total_size) >> 32);
    DWORD size_low = (DWORD)((uint64_t)layout.total_size & UINT32_MAX);
    table->mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
        size_high, size_low, NULL);
    if (!table->mapping) {
        zan_shared_table_free(table);
        return 0;
    }
    table->header = (zan_shared_header *)MapViewOfFile(
        table->mapping, FILE_MAP_ALL_ACCESS, 0, 0, layout.total_size);
    if (!table->header) {
        zan_shared_table_free(table);
        return 0;
    }
#else
    /* 内部辅助实现 */
    static uint32_t anon_seq = 0;
    char shm_name[64];
    /* 内部辅助逻辑 */
    uint32_t seq = __atomic_fetch_add(&anon_seq, 1, __ATOMIC_RELAXED);
    snprintf(shm_name, sizeof(shm_name), "/zan_table_%ld_%u_%u",
             (long)getpid(), (unsigned)time(NULL), seq);
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        zan_shared_table_free(table);
        return 0;
    }
    shm_unlink(shm_name);
    /* 内部辅助逻辑 */
    if (fcntl(fd, F_SETFD, 0) != 0) {
        close(fd);
        zan_shared_table_free(table);
        return 0;
    }
    table->fd = fd;
    if (ftruncate(fd, (off_t)layout.total_size) != 0) {
        zan_shared_table_free(table);
        return 0;
    }
    table->header = (zan_shared_header *)mmap(
        NULL, layout.total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (table->header == MAP_FAILED) {
        table->header = NULL;
        zan_shared_table_free(table);
        return 0;
    }
#endif

    zan_table_init_header(table, &layout);
    return (int64_t)(intptr_t)table;
}

/* 内部辅助逻辑 */
int64_t zan_shared_table_handle(int64_t handle) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !table->anonymous) return 0;
#ifdef _WIN32
    return (int64_t)(intptr_t)table->mapping;
#else
    return (int64_t)table->fd;
#endif
}

/* 内部实现与并发/内存约束规范 */
int64_t zan_shared_table_attach(int64_t os_handle) {
    if (os_handle <= 0) return 0;
    zan_shared_table *table = zan_table_alloc();
    if (!table) return 0;
    table->anonymous = 1;
    table->attached = 1;

#ifdef _WIN32
    table->mapping = (HANDLE)(intptr_t)os_handle;
    table->header = (zan_shared_header *)MapViewOfFile(
        table->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!table->header) {
        zan_shared_table_free(table);
        return 0;
    }
    /* 内部辅助实现 */
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(table->header, &mbi, sizeof(mbi)) ||
            mbi.RegionSize < sizeof(zan_shared_header)) {
            zan_shared_table_free(table);
            return 0;
        }
        table->mapped_size = mbi.RegionSize;
    }
#else
    table->fd = (int)os_handle;
    struct stat stat_buf;
    if (fstat(table->fd, &stat_buf) != 0 || stat_buf.st_size <= 0) {
        zan_shared_table_free(table);
        return 0;
    }
    table->mapped_size = (size_t)stat_buf.st_size;
    /* 内部辅助实现 */
    table->header = (zan_shared_header *)mmap(
        NULL, table->mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED,
        table->fd, 0);
    if (table->header == MAP_FAILED) {
        table->header = NULL;
        zan_shared_table_free(table);
        return 0;
    }
#endif

#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    if (table->header->ready != 1 ||
        table->header->magic != ZAN_TABLE_MAGIC ||
        table->header->version != ZAN_TABLE_VERSION ||
        table->header->total_size < sizeof(zan_shared_header) ||
        table->header->total_size > table->mapped_size ||
        !zan_header_geometry_ok(table->header)) {
        zan_shared_table_free(table);
        return 0;
    }
    return (int64_t)(intptr_t)table;
}

int64_t zan_shared_table_create(
    const char *name, int32_t capacity_value, int32_t key_size_value,
    const char *schema) {
    if (!name || !*name) return 0;
    zan_table_layout layout;
    if (!zan_table_layout_of(capacity_value, key_size_value, schema, &layout)) {
        return 0;
    }
    size_t total_size = layout.total_size;

    zan_shared_table *table = zan_table_alloc();
    if (!table) return 0;
    zan_make_names(name, table->map_name);

#ifdef _WIN32
    DWORD size_high = (DWORD)(((uint64_t)total_size) >> 32);
    DWORD size_low = (DWORD)((uint64_t)total_size & UINT32_MAX);
    table->mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        size_high, size_low, table->map_name);
    if (!table->mapping || GetLastError() == ERROR_ALREADY_EXISTS) {
        zan_shared_table_free(table);
        return 0;
    }
    table->header = (zan_shared_header *)MapViewOfFile(
        table->mapping, FILE_MAP_ALL_ACCESS, 0, 0, total_size);
    if (!table->header) {
        zan_shared_table_free(table);
        return 0;
    }
#else
    table->fd = open(
        table->map_name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (table->fd < 0) {
        zan_shared_table_free(table);
        return 0;
    }
    if (ftruncate(table->fd, (off_t)total_size) != 0) {
        unlink(table->map_name);
        zan_shared_table_free(table);
        return 0;
    }
    table->header = (zan_shared_header *)mmap(
        NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, table->fd, 0);
    if (table->header == MAP_FAILED) {
        table->header = NULL;
        unlink(table->map_name);
        zan_shared_table_free(table);
        return 0;
    }
#endif

    zan_table_init_header(table, &layout);
    return (int64_t)(intptr_t)table;
}

int64_t zan_shared_table_open(const char *name) {
    if (!name || !*name) return 0;
    zan_shared_table *table = (zan_shared_table *)calloc(1, sizeof(*table));
    if (!table) return 0;
#ifndef _WIN32
    table->fd = -1;
    if (pthread_mutex_init(&table->local_mutex, NULL) != 0) {
        free(table);
        return 0;
    }
    table->local_mutex_ready = 1;
#endif
    zan_make_names(name, table->map_name);

#ifdef _WIN32
    table->mapping = OpenFileMappingA(
        FILE_MAP_ALL_ACCESS, FALSE, table->map_name);
    if (!table->mapping) {
        zan_shared_table_free(table);
        return 0;
    }
    table->header = (zan_shared_header *)MapViewOfFile(
        table->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!table->header) {
        zan_shared_table_free(table);
        return 0;
    }
    /* 模块核心语义抽象与接口调用契约 */
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(table->header, &mbi, sizeof(mbi)) ||
            mbi.RegionSize < sizeof(zan_shared_header)) {
            zan_shared_table_free(table);
            return 0;
        }
        table->mapped_size = mbi.RegionSize;
    }
#else
    table->fd = open(table->map_name, O_RDWR | O_NOFOLLOW);
    if (table->fd < 0) {
        zan_shared_table_free(table);
        return 0;
    }
    struct stat stat_buf;
    if (fstat(table->fd, &stat_buf) != 0 || stat_buf.st_size <= 0) {
        zan_shared_table_free(table);
        return 0;
    }
    table->mapped_size = (size_t)stat_buf.st_size;
    table->header = (zan_shared_header *)mmap(
        NULL, table->mapped_size,
        PROT_READ | PROT_WRITE, MAP_SHARED, table->fd, 0);
    if (table->header == MAP_FAILED) {
        table->header = NULL;
        zan_shared_table_free(table);
        return 0;
    }
#endif

#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    if (table->header->ready != 1 ||
        table->header->magic != ZAN_TABLE_MAGIC ||
        table->header->version != ZAN_TABLE_VERSION ||
        table->header->total_size < sizeof(zan_shared_header) ||
        table->header->total_size > table->mapped_size ||
        !zan_header_geometry_ok(table->header)) {
        zan_shared_table_free(table);
        return 0;
    }
#ifndef _WIN32
    /* 内部辅助实现 */
    if (table->header->total_size != table->mapped_size) {
        zan_shared_table_free(table);
        return 0;
    }
#endif
    return (int64_t)(intptr_t)table;
}

void zan_shared_table_close(int64_t handle) {
    zan_shared_table_free((zan_shared_table *)(intptr_t)handle);
}

int32_t zan_shared_table_destroy(int64_t handle) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int32_t result = 1;
#ifndef _WIN32
    /* 内部辅助逻辑 */
    if (!table->anonymous) {
        if (unlink(table->map_name) != 0 && errno != ENOENT) result = 0;
    }
#endif
    zan_shared_table_free(table);
    return result;
}

#ifdef _WIN32
typedef struct {
    void *VirtualAddress;
    ULONG_PTR VirtualAttributes;
} zan_ws_ex_info;

typedef BOOL(WINAPI *zan_query_ws_ex_fn)(HANDLE, zan_ws_ex_info *, DWORD);
#endif

/* 模块核心语义抽象与接口调用契约 */
static int64_t zan_table_resident_bytes(const zan_shared_table *table) {
    if (!table->header || !table->mapped_size) return 0;
#ifdef _WIN32
    static zan_query_ws_ex_fn query_ws_ex = NULL;
    static int query_ws_ex_resolved = 0;
    if (!query_ws_ex_resolved) {
        HMODULE kernel = GetModuleHandleA("kernel32.dll");
        if (kernel) {
            query_ws_ex = (zan_query_ws_ex_fn)(void *)GetProcAddress(
                kernel, "K32QueryWorkingSetEx");
        }
        query_ws_ex_resolved = 1;
    }
    if (!query_ws_ex) return -1;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    size_t page_size = info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
    size_t pages = (table->mapped_size + page_size - 1) / page_size;
    enum { ZAN_WS_BATCH = 4096 };
    zan_ws_ex_info *batch = (zan_ws_ex_info *)calloc(
        ZAN_WS_BATCH, sizeof(zan_ws_ex_info));
    if (!batch) return -1;
    HANDLE self = GetCurrentProcess();
    unsigned char *base = (unsigned char *)table->header;
    int64_t resident = 0;
    for (size_t first = 0; first < pages; first += ZAN_WS_BATCH) {
        size_t n = pages - first;
        if (n > ZAN_WS_BATCH) n = ZAN_WS_BATCH;
        for (size_t i = 0; i < n; i++) {
            batch[i].VirtualAddress = base + (first + i) * page_size;
            batch[i].VirtualAttributes = 0;
        }
        if (!query_ws_ex(
                self, batch, (DWORD)(n * sizeof(zan_ws_ex_info)))) {
            free(batch);
            return -1;
        }
        for (size_t i = 0; i < n; i++) {
            if (batch[i].VirtualAttributes & 1u) resident += (int64_t)page_size;
        }
    }
    free(batch);
    if (resident > (int64_t)table->mapped_size) {
        resident = (int64_t)table->mapped_size;
    }
    return resident;
#else
    long page_conf = sysconf(_SC_PAGESIZE);
    size_t page_size = page_conf > 0 ? (size_t)page_conf : 4096u;
    enum { ZAN_MINCORE_BATCH = 16384 };  /* 核心系统底层抽象与内存语义契约 */
    unsigned char *vec = (unsigned char *)malloc(ZAN_MINCORE_BATCH);
    if (!vec) return -1;
    unsigned char *base = (unsigned char *)table->header;
    size_t pages = (table->mapped_size + page_size - 1) / page_size;
    int64_t resident = 0;
    for (size_t first = 0; first < pages; first += ZAN_MINCORE_BATCH) {
        size_t n = pages - first;
        if (n > ZAN_MINCORE_BATCH) n = ZAN_MINCORE_BATCH;
        size_t offset = first * page_size;
        size_t length = n * page_size;
        if (offset + length > table->mapped_size) {
            length = table->mapped_size - offset;
        }
#if defined(__APPLE__)
        if (mincore((void *)(base + offset), length, (char *)vec) != 0) {
#else
        if (mincore((void *)(base + offset), length, vec) != 0) {
#endif
            free(vec);
            return -1;
        }
        for (size_t i = 0; i < n; i++) {
            if (vec[i] & 1u) resident += (int64_t)page_size;
        }
    }
    free(vec);
    if (resident > (int64_t)table->mapped_size) {
        resident = (int64_t)table->mapped_size;
    }
    return resident;
#endif
}

int64_t zan_shared_table_stat(int64_t handle, int32_t what) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !table->header) return 0;
    switch (what) {
        case ZAN_TABLE_STAT_RESERVED:
            return (int64_t)table->mapped_size;
        case ZAN_TABLE_STAT_RESIDENT:
            return zan_table_resident_bytes(table);
        case ZAN_TABLE_STAT_CAPACITY:
            return (int64_t)table->header->capacity;
        case ZAN_TABLE_STAT_COUNT:
            return (int64_t)table->header->count;
        case ZAN_TABLE_STAT_ROW_STRIDE:
            return (int64_t)table->header->row_stride;
        case ZAN_TABLE_STAT_KEY_SIZE:
            return (int64_t)table->header->key_size;
        case ZAN_TABLE_STAT_COLUMNS:
            return (int64_t)table->header->column_count;
        default:
            return 0;
    }
}

int32_t zan_shared_table_set_int(
    int64_t handle, const char *key, const char *column_name, int64_t value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_row_for(table->header, key, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return 1;
}

int64_t zan_shared_table_get_int(
    int64_t handle, const char *key, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_row_for(table->header, key, 0) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    zan_row_unlock(row);
    return value;
}

int32_t zan_shared_table_set_float(
    int64_t handle, const char *key, const char *column_name, double value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_FLOAT);
    unsigned char *row = column ? zan_row_for(table->header, key, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return 1;
}

double zan_shared_table_get_float(
    int64_t handle, const char *key, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0.0;
    double value = 0.0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_FLOAT);
    unsigned char *row = column ? zan_row_for(table->header, key, 0) : NULL;
    if (!row) return 0.0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0.0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    zan_row_unlock(row);
    return value;
}

int32_t zan_shared_table_set_string(
    int64_t handle, const char *key, const char *column_name, const char *value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !value) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    size_t value_len = column ? strlen(value) : 0;
    unsigned char *row =
        column && value_len < column->size
            ? zan_row_for(table->header, key, 1)
            : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    char *destination = (char *)(row + column->offset);
    /* 内部辅助实现 */
    memcpy(destination, value, value_len);
    destination[value_len] = '\0';
    zan_row_unlock(row);
    return 1;
}

const char *zan_shared_table_get_string(
    int64_t handle, const char *key, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    char *result = zan_get_shared_string();
    result[0] = '\0';
    if (!table) return result;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    unsigned char *row = column ? zan_row_for(table->header, key, 0) : NULL;
    if (!row) return result;
    zan_row_lock(row);
    size_t max_len = 0;
    if (zan_row_revalidate(row, zan_hash_bytes(key), key,
                           table->header->key_size)) {
        max_len = column->size - 1u;
        size_t len = zan_strnlen((const char *)(row + column->offset), max_len);
        memcpy(result, row + column->offset, len);
        result[len] = '\0';
    }
    zan_row_unlock(row);
    return result;
}

int64_t zan_shared_table_increment(
    int64_t handle, const char *key, const char *column_name, int64_t delta) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_row_for(table->header, key, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    value += delta;
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return value;
}

/* 核心系统底层抽象与内存语义契约 */
int64_t zan_monotonic_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    /* 模块核心语义抽象与接口调用契约 */
    return (int64_t)((now.QuadPart / frequency.QuadPart) * 1000000
        + ((now.QuadPart % frequency.QuadPart) * 1000000) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
#endif
}

/* 核心系统底层抽象与内存语义契约 */
int64_t zan_monotonic_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    /* 模块核心语义抽象与接口调用契约 */
    return (int64_t)((now.QuadPart / frequency.QuadPart) * 1000000000
        + ((now.QuadPart % frequency.QuadPart) * 1000000000) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
#endif
}

int64_t zan_stopwatch_ticks(void) {
#ifdef _WIN32
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
#else
    return zan_monotonic_ns();
#endif
}

int64_t zan_stopwatch_frequency(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
#else
    return 1000000000;
#endif
}

int64_t zan_monotonic_ticks(void) {
    return zan_stopwatch_ticks();
}

int64_t zan_monotonic_frequency(void) {
    return zan_stopwatch_frequency();
}

/* 底层系统交互与数据协议契约 */

int64_t zan_shared_table_hash(const char *value) {
    return (int64_t)zan_hash_bytes(value);
}

int32_t zan_shared_table_set_int_at(
    int64_t handle, int64_t key_hash, const char *column_name, int64_t value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return 1;
}

int64_t zan_shared_table_get_int_at(
    int64_t handle, int64_t key_hash, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 0) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    zan_row_unlock(row);
    return value;
}

int64_t zan_shared_table_increment_at(
    int64_t handle, int64_t key_hash, const char *column_name, int64_t delta) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    value += delta;
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return value;
}

/* 内部辅助逻辑 */
int64_t zan_shared_table_extreme_at(
    int64_t handle, int64_t key_hash, const char *column_name, int64_t value,
    int64_t keep_larger) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t stored = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&stored, row + column->offset, sizeof(stored));
    int replace = keep_larger ? (value > stored) : (stored == 0 || value < stored);
    if (replace) {
        memcpy(row + column->offset, &value, sizeof(value));
        stored = value;
    }
    zan_row_unlock(row);
    return stored;
}

int32_t zan_shared_table_set_string_at(
    int64_t handle, int64_t key_hash, const char *column_name,
    const char *value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !value) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    size_t value_len = column ? strlen(value) : 0;
    unsigned char *row =
        column && value_len < column->size
            ? zan_row_for_hash(table->header, (uint64_t)key_hash, 1)
            : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    char *destination = (char *)(row + column->offset);
    /* 内部辅助实现 */
    memcpy(destination, value, value_len);
    destination[value_len] = '\0';
    zan_row_unlock(row);
    return 1;
}

const char *zan_shared_table_get_string_at(
    int64_t handle, int64_t key_hash, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    char *result = zan_get_shared_string();
    result[0] = '\0';
    if (!table) return result;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 0) : NULL;
    if (!row) return result;
    zan_row_lock(row);
    size_t max_len = 0;
    if (zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        max_len = column->size - 1u;
        size_t len = zan_strnlen((const char *)(row + column->offset), max_len);
        memcpy(result, row + column->offset, len);
        result[len] = '\0';
    }
    zan_row_unlock(row);
    return result;
}

/* 内部辅助逻辑 */
int32_t zan_shared_table_match_at(
    int64_t handle, int64_t key_hash, const char *column_name,
    const char *text) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !text) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    unsigned char *row =
        column ? zan_find_row_hash(table->header, (uint64_t)key_hash, 0) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    int equal = 0;
    if (zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0))
        equal = strncmp(
            (const char *)(row + column->offset), text, column->size) == 0;
    zan_row_unlock(row);
    return equal ? 1 : 0;
}

int32_t zan_shared_table_exists_at(int64_t handle, int64_t key_hash) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_purge_expired(table->header);
    return zan_find_row_hash(table->header, (uint64_t)key_hash, 0) != NULL;
}

int32_t zan_shared_table_delete_at(int64_t handle, int64_t key_hash) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_struct_lock(table->header);
    /* 内部辅助实现 */
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), ZAN_PURGE_MAX_BATCH);
    unsigned char *row = zan_find_row_hash(
        table->header, (uint64_t)key_hash, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_remove(table->header, row);
        /* 模块核心语义抽象与接口调用契约 */
        memset(row + 8, 0, table->header->row_stride - 8);
        zan_publish_state(row, ZAN_SLOT_TOMBSTONE);
        table->header->count--;
        zan_row_unlock(row);
    }
    zan_struct_unlock(table->header);
    return row != NULL;
}

int32_t zan_shared_table_expire(
    int64_t handle, const char *key, int64_t ttl_ms) {
    if (ttl_ms < 0) return 0;
    int64_t now_ms = zan_wall_now_ms();
    if (ttl_ms > INT64_MAX - now_ms) return 0;
    return zan_shared_table_expire_at(handle, key, now_ms + ttl_ms);
}

int32_t zan_shared_table_expire_at(
    int64_t handle, const char *key, int64_t expires_at_ms) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !key || expires_at_ms < 0) return 0;
    zan_struct_lock(table->header);
    /* 内部辅助实现 */
    zan_purge_expired_locked(table->header, zan_wall_now_ms(),
                             ZAN_PURGE_MAX_BATCH);
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_set(
            table->header, zan_row_slot(table->header, row), row, expires_at_ms);
        zan_row_unlock(row);
    }
    zan_struct_unlock(table->header);
    return row != NULL;
}

int64_t zan_shared_table_expires_at(int64_t handle, const char *key) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !key) return -1;
    zan_purge_expired(table->header);
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (!row) return -1;
    zan_row_lock(row);
    int64_t expires_at = -1;
    if (zan_row_revalidate(row, zan_hash_bytes(key), key,
                           table->header->key_size))
        expires_at = *zan_row_expires_at(row);
    zan_row_unlock(row);
    return expires_at;
}

int64_t zan_shared_table_purge_expired(int64_t handle, int64_t now_ms) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    if (now_ms < 0) now_ms = zan_wall_now_ms();
    zan_struct_lock(table->header);
    uint64_t before = table->header->count;
    zan_purge_expired_locked(table->header, now_ms, UINT64_MAX);
    uint64_t removed = before - table->header->count;
    zan_struct_unlock(table->header);
    return (int64_t)removed;
}

int32_t zan_shared_table_rate_allow(
    int64_t handle, const char *key, int64_t now_ms,
    int64_t window_ms, int64_t limit) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (limit <= 0) return 1;
    if (!table || !key || window_ms <= 0) return 0;
    if (now_ms < 0) now_ms = zan_wall_now_ms();
    if (window_ms > INT64_MAX - now_ms) return 0;
    zan_shared_column *count_column = zan_find_column(
        table->header, "count", ZAN_TABLE_INT);
    zan_shared_column *start_column = zan_find_column(
        table->header, "window_start", ZAN_TABLE_INT);
    if (!count_column || !start_column) return 0;

    int allowed = 0;
    zan_struct_lock(table->header);
    /* 内部辅助实现 */
    zan_purge_expired_locked(table->header, now_ms, ZAN_PURGE_MAX_BATCH);
    /* 内部辅助实现 */
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (!row) row = zan_find_row(table->header, key, 1);
    if (row) {
        zan_row_lock(row);
        int64_t count = 0;
        int64_t start = 0;
        memcpy(&count, row + count_column->offset, sizeof(count));
        memcpy(&start, row + start_column->offset, sizeof(start));
        if (start <= 0 || now_ms - start >= window_ms) {
            count = 1;
            start = now_ms;
            zan_expiry_set(
                table->header, zan_row_slot(table->header, row), row,
                now_ms + window_ms);
            allowed = 1;
        } else if (count < limit) {
            count++;
            allowed = 1;
        }
        memcpy(row + count_column->offset, &count, sizeof(count));
        memcpy(row + start_column->offset, &start, sizeof(start));
        zan_row_unlock(row);
    }
    zan_struct_unlock(table->header);
    return allowed;
}

int32_t zan_shared_table_lock_acquire(
    int64_t handle, const char *key, int64_t owner,
    int64_t now_ms, int64_t lease_ms) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !key || owner == 0 || lease_ms <= 0) return 0;
    if (now_ms < 0) now_ms = zan_wall_now_ms();
    if (lease_ms > INT64_MAX - now_ms) return 0;
    zan_shared_column *owner_column = zan_find_column(
        table->header, "owner", ZAN_TABLE_INT);
    if (!owner_column) return 0;

    int acquired = 0;
    zan_struct_lock(table->header);
    zan_purge_expired_locked(table->header, now_ms, UINT64_MAX);
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (!row) {
        row = zan_find_row(table->header, key, 1);
        if (row) {
            zan_row_lock(row);
            memcpy(row + owner_column->offset, &owner, sizeof(owner));
            zan_expiry_set(
                table->header, zan_row_slot(table->header, row), row,
                now_ms + lease_ms);
            zan_row_unlock(row);
            acquired = 1;
        }
    }
    zan_struct_unlock(table->header);
    return acquired;
}

int32_t zan_shared_table_lock_release(
    int64_t handle, const char *key, int64_t owner) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !key || owner == 0) return 0;
    zan_shared_column *owner_column = zan_find_column(
        table->header, "owner", ZAN_TABLE_INT);
    if (!owner_column) return 0;

    int released = 0;
    zan_struct_lock(table->header);
    /* 内部辅助逻辑 */
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), ZAN_PURGE_MAX_BATCH);
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        int64_t current_owner = 0;
        memcpy(&current_owner, row + owner_column->offset, sizeof(current_owner));
        if (current_owner == owner) {
            zan_expiry_remove(table->header, row);
            /* 模块核心语义抽象与接口调用契约 */
            memset(row + 8, 0, table->header->row_stride - 8);
            zan_publish_state(row, ZAN_SLOT_TOMBSTONE);
            table->header->count--;
            released = 1;
        }
        zan_row_unlock(row);
    }
    zan_struct_unlock(table->header);
    return released;
}

int32_t zan_shared_table_delete(int64_t handle, const char *key) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_struct_lock(table->header);
    /* 内部辅助逻辑 */
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), ZAN_PURGE_MAX_BATCH);
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_remove(table->header, row);
        /* 模块核心语义抽象与接口调用契约 */
        memset(row + 8, 0, table->header->row_stride - 8);
        zan_publish_state(row, ZAN_SLOT_TOMBSTONE);
        table->header->count--;
        zan_row_unlock(row);
    }
    zan_struct_unlock(table->header);
    return row != NULL;
}

int32_t zan_shared_table_exists(int64_t handle, const char *key) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_purge_expired(table->header);
    return zan_find_row(table->header, key, 0) != NULL;
}

int64_t zan_shared_table_count(int64_t handle) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_purge_expired(table->header);
    return (int64_t)table->header->count;
}

void zan_shared_table_clear(int64_t handle) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return;
    /* 模块核心语义抽象与接口调用契约 */
    zan_struct_lock(table->header);
    for (uint64_t i = 0; i < table->header->capacity; i++) {
        unsigned char *row = zan_row_at(table->header, i);
        uint32_t state = zan_load_state(row);
        if (state == ZAN_SLOT_EMPTY) continue;   /* 核心系统底层抽象与内存语义契约 */
        zan_row_lock(row);
        if (zan_load_state(row) != ZAN_SLOT_EMPTY) {
            /* 内部辅助逻辑 */
            if (zan_load_state(row) == ZAN_SLOT_USED)
                memset(row + 8, 0, table->header->row_stride - 8);
            zan_publish_state(row, ZAN_SLOT_EMPTY);
        }
        zan_row_unlock(row);
    }
    /* 内部辅助逻辑 */
    memset(zan_expiry_heap(table->header), 0,
           (size_t)table->header->capacity * sizeof(uint64_t));
    table->header->count = 0;
    table->header->expiry_count = 0;
    zan_struct_unlock(table->header);
}

/* 底层系统交互与数据协议契约 */

/* 内部辅助逻辑 */
long long zan_exe_dir_into(char *out, long long cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
#ifdef _WIN32
    char buf[1024];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    while (n > 0 && buf[n - 1] != '\\') n--;
    if (n > 0) n--; /* 核心系统底层抽象与内存语义契约 */
    if ((long long)n >= cap) n = (DWORD)(cap - 1);
    memcpy(out, buf, n);
    out[n] = '\0';
    return (long long)n;
#else
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    while (n > 0 && buf[n - 1] != '/') n--;
    if (n > 0) n--;
    if ((long long)n >= cap) n = (ssize_t)(cap - 1);
    memcpy(out, buf, (size_t)n);
    out[n] = '\0';
    return (long long)n;
#endif
}

/* 模块核心语义抽象与接口调用契约 */
long long zan_dir_list_into(const char *pattern, char *out, long long cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    if (!pattern) return 0;
    long long len = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        long long nl = (long long)strlen(fd.cFileName);
        if (len + nl + 2 > cap) break;
        memcpy(out + len, fd.cFileName, (size_t)nl);
        len += nl;
        out[len++] = '\n';
        out[len] = '\0';
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    glob_t g;
    if (glob(pattern, 0, NULL, &g) != 0) return 0;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        const char *base = strrchr(g.gl_pathv[i], '/');
        base = base ? base + 1 : g.gl_pathv[i];
        long long nl = (long long)strlen(base);
        if (len + nl + 2 > cap) break;
        memcpy(out + len, base, (size_t)nl);
        len += nl;
        out[len++] = '\n';
        out[len] = '\0';
    }
    globfree(&g);
#endif
    return len;
}

/* 核心系统底层抽象与内存语义契约 */
#ifdef _WIN32
typedef struct {
    HANDLE h;
} zan_mmap_handle;
#else
typedef struct {
    int fd;
    int owner;         /* 内部辅助逻辑 */
    char name[256];    /* 模块核心语义抽象与接口调用契约 */
} zan_mmap_handle;
#endif

/* 创建指定字节大小的具名共享内存区 */
long long zan_mmap_create(const char *name, long long size) {
    if (!name || !name[0] || size <= 0) return 0;
#ifdef _WIN32
    DWORD hi = (DWORD)(((unsigned long long)size) >> 32);
    DWORD lo = (DWORD)((unsigned long long)size & 0xFFFFFFFFULL);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (wlen <= 0) return 0;   /* 内部辅助逻辑 */
    wchar_t *wname = (wchar_t *)calloc((size_t)wlen, sizeof(wchar_t));
    if (!wname) return 0;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, wlen);
    HANDLE named = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, hi, lo, wname);
    DWORD err = GetLastError();
    free(wname);
    if (!named) return 0;
    if (err == ERROR_ALREADY_EXISTS) {
        CloseHandle(named);
        return 0;
    }
    return (long long)(intptr_t)named;
#else
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/%s", name);
    int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return 0;
    if (ftruncate(fd, (off_t)size) != 0) {
        shm_unlink(shm_name);
        close(fd);
        return 0;
    }
    zan_mmap_handle *h = (zan_mmap_handle *)calloc(1, sizeof(zan_mmap_handle));
    if (!h) { shm_unlink(shm_name); close(fd); return 0; }
    h->fd = fd;
    h->owner = 1;
    snprintf(h->name, sizeof(h->name), "%s", shm_name);
    return (long long)(intptr_t)h;
#endif
}

/* 打开已存在的具名共享内存区 */
long long zan_mmap_open(const char *name, long long size) {
    if (!name || !name[0]) return 0;
#ifdef _WIN32
    (void)size;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (wlen <= 0) return 0;   /* 底层系统交互与数据协议契约 */
    wchar_t *wname = (wchar_t *)calloc((size_t)wlen, sizeof(wchar_t));
    if (!wname) return 0;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, wlen);
    HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname);
    free(wname);
    if (!h) return 0;
    return (long long)(intptr_t)h;
#else
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/%s", name);
    int fd = shm_open(shm_name, O_RDWR, 0600);
    if (fd < 0) return 0;
    if (size > 0) {
        struct stat st;
        if (fstat(fd, &st) != 0 || st.st_size != size) {
            close(fd);
            return 0;
        }
    }
    zan_mmap_handle *h = (zan_mmap_handle *)calloc(1, sizeof(zan_mmap_handle));
    if (!h) { close(fd); return 0; }
    h->fd = fd;
    snprintf(h->name, sizeof(h->name), "%s", shm_name);
    return (long long)(intptr_t)h;
#endif
}

/* 在已有文件句柄上建立内存映射 */
long long zan_mmap_from_file(const char *path, long long size) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return 0;   /* 底层系统交互与数据协议契约 */
    wchar_t *wpath = (wchar_t *)calloc((size_t)wlen, sizeof(wchar_t));
    if (!wpath) return 0;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    HANDLE fh = CreateFileW(wpath, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (fh == INVALID_HANDLE_VALUE) return 0;
    DWORD hi = 0, lo = 0;
    if (size > 0) {
        hi = (DWORD)(((unsigned long long)size) >> 32);
        lo = (DWORD)((unsigned long long)size & 0xFFFFFFFFULL);
    }
    HANDLE h = CreateFileMappingW(fh, NULL, PAGE_READWRITE, hi, lo, NULL);
    CloseHandle(fh);
    if (!h) return 0;
    return (long long)(intptr_t)h;
#else
    int fd = open(path, O_RDWR);
    if (fd < 0) return 0;
    zan_mmap_handle *h = (zan_mmap_handle *)calloc(1, sizeof(zan_mmap_handle));
    if (!h) { close(fd); return 0; }
    h->fd = fd;
    h->name[0] = '\0';
    return (long long)(intptr_t)h;
#endif
}

/* 模块核心语义抽象与接口调用契约 */
long long zan_mmap_map(long long handle, long long size) {
    if (!handle || size <= 0) return 0;
#ifdef _WIN32
    HANDLE h = (HANDLE)(intptr_t)handle;
    void *p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, (size_t)size);
    return p ? (long long)(intptr_t)p : 0;
#else
    zan_mmap_handle *h = (zan_mmap_handle *)(intptr_t)handle;
    void *p = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   h->fd, 0);
    if (p == MAP_FAILED) return 0;
    return (long long)(intptr_t)p;
#endif
}

/* 底层系统交互与数据协议契约 */
long long zan_mmap_unmap(long long ptr, long long size) {
    if (!ptr) return 0;
#ifdef _WIN32
    (void)size;
    return UnmapViewOfFile((void *)(intptr_t)ptr) ? 1 : 0;
#else
    return munmap((void *)(intptr_t)ptr, (size_t)size) == 0 ? 1 : 0;
#endif
}

/* 底层系统交互与数据协议契约 */
long long zan_mmap_flush(long long ptr, long long size) {
    if (!ptr) return 1;
#ifdef _WIN32
    return FlushViewOfFile((void *)(intptr_t)ptr, (size_t)size) ? 1 : 0;
#else
    return msync((void *)(intptr_t)ptr, (size_t)size, MS_SYNC) == 0 ? 1 : 0;
#endif
}

/* 核心系统底层抽象与内存语义契约 */
long long zan_mmap_close(long long handle) {
    if (!handle) return 0;
#ifdef _WIN32
    HANDLE h = (HANDLE)(intptr_t)handle;
    return CloseHandle(h) ? 1 : 0;
#else
    zan_mmap_handle *h = (zan_mmap_handle *)(intptr_t)handle;
    close(h->fd);
    if (h->owner && h->name[0]) shm_unlink(h->name);
    free(h);
    return 1;
#endif
}

/* 模块核心语义抽象与接口调用契约 */
long long zan_mmap_unlink(const char *name) {
    if (!name || !name[0]) return 0;
#ifdef _WIN32
    (void)name;
    return 1;
#else
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/%s", name);
    return shm_unlink(shm_name) == 0 ? 1 : 0;
#endif
}

/* 内部辅助实现 */

#ifndef _WIN32
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#if defined(__linux__)
#include <netpacket/packet.h>
#else
#include <net/if_dl.h>
#endif
#endif

/* 内部辅助逻辑 */
#ifndef _WIN32
#define ZAN_PLAT_TEXT_MAX 65536
static pthread_key_t zan_plat_text_key;

static void zan_plat_text_dtor(void *buffer) { free(buffer); }

static void zan_plat_text_key_create(void) {
    if (pthread_key_create(&zan_plat_text_key, zan_plat_text_dtor) != 0)
        zan_rt_fatal("oom", "sync: text TLS init failed");
}

static char *zan_get_plat_text(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, zan_plat_text_key_create);
    char *buffer = (char *)pthread_getspecific(zan_plat_text_key);
    if (!buffer) {
        buffer = (char *)calloc(ZAN_PLAT_TEXT_MAX, 1);
        if (!buffer) zan_rt_fatal("oom", "sync: text TLS alloc failed");
        if (pthread_setspecific(zan_plat_text_key, buffer) != 0) {
            free(buffer);
            zan_rt_fatal("oom", "sync: text TLS set failed");
        }
    }
    return buffer;
}
#endif

#ifndef _WIN32
static void zan_plat_mac_from_sockaddr(struct sockaddr *sa, char *out,
                                       size_t out_size) {
    out[0] = '\0';
    if (!sa) return;
    const unsigned char *bytes = NULL;
    int len = 0;
#if defined(__linux__)
    if (sa->sa_family != AF_PACKET) return;
    struct sockaddr_ll *ll = (struct sockaddr_ll *)sa;
    bytes = ll->sll_addr;
    len = ll->sll_halen;
#else
    if (sa->sa_family != AF_LINK) return;
    struct sockaddr_dl *dl = (struct sockaddr_dl *)sa;
    bytes = (const unsigned char *)LLADDR(dl);
    len = dl->sdl_alen;
#endif
    if (len <= 0 || len > 8) return;
    size_t used = 0;
    for (int i = 0; i < len && used + 3 < out_size; i++) {
        used += (size_t)snprintf(out + used, out_size - used, "%s%02X",
                                 i ? ":" : "", bytes[i]);
    }
}
#endif

/* 内部辅助逻辑 */
const char *zan_plat_net_interfaces(void) {
#ifdef _WIN32
    return "";
#else
    char *zan_plat_text = zan_get_plat_text();
    zan_plat_text[0] = '\0';
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return zan_plat_text;

    size_t used = 0;
    for (struct ifaddrs *it = list; it; it = it->ifa_next) {
        if (!it->ifa_name) continue;
        /* 模块核心语义抽象与接口调用契约 */
        int seen = 0;
        for (struct ifaddrs *p = list; p != it; p = p->ifa_next) {
            if (p->ifa_name && strcmp(p->ifa_name, it->ifa_name) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen) continue;

        char mac[32];
        mac[0] = '\0';
        int up = (it->ifa_flags & IFF_UP) && (it->ifa_flags & IFF_RUNNING);
        char addrs[2048];
        size_t addr_used = 0;
        addrs[0] = '\0';
        for (struct ifaddrs *p = list; p; p = p->ifa_next) {
            if (!p->ifa_name || strcmp(p->ifa_name, it->ifa_name) != 0) continue;
            if (!p->ifa_addr) continue;
            if (!mac[0]) zan_plat_mac_from_sockaddr(p->ifa_addr, mac, sizeof mac);
            char text[INET6_ADDRSTRLEN];
            text[0] = '\0';
            if (p->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *v4 = (struct sockaddr_in *)p->ifa_addr;
                inet_ntop(AF_INET, &v4->sin_addr, text, sizeof text);
            } else if (p->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)p->ifa_addr;
                inet_ntop(AF_INET6, &v6->sin6_addr, text, sizeof text);
            }
            if (!text[0]) continue;
            if (addr_used + strlen(text) + 2 >= sizeof addrs) continue;
            addr_used += (size_t)snprintf(addrs + addr_used,
                                          sizeof addrs - addr_used, "%s%s",
                                          addr_used ? "," : "", text);
        }

        unsigned index = if_nametoindex(it->ifa_name);
        int written = snprintf(zan_plat_text + used, ZAN_PLAT_TEXT_MAX - used,
                               "%s\t%u\t%d\t%s\t%s\n", it->ifa_name, index,
                               up ? 1 : 0, mac, addrs);
        if (written < 0 || (size_t)written >= ZAN_PLAT_TEXT_MAX - used) break;
        used += (size_t)written;
    }
    freeifaddrs(list);
    return zan_plat_text;
#endif
}

#ifndef _WIN32
static uint16_t zan_plat_icmp_checksum(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint32_t)((bytes[0] << 8) | bytes[1]);
        bytes += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)(bytes[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static long long zan_plat_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
#endif

/* 内部辅助逻辑 */
int32_t zan_plat_icmp_ping(const char *address, int32_t timeout_ms) {
#ifdef _WIN32
    (void)address;
    (void)timeout_ms;
    return -3;
#else
    if (!address || !address[0]) return -4;
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    if (inet_pton(AF_INET, address, &dst.sin_addr) != 1) return -4;
    if (timeout_ms < 0) timeout_ms = 0;

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    int datagram = fd >= 0;
    if (fd < 0) fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) return -3;

    /* 模块核心语义抽象与接口调用契约 */
    unsigned char packet[16];
    memset(packet, 0, sizeof packet);
    packet[0] = 8;                                  /* ICMP_ECHO */
    uint16_t ident = (uint16_t)(getpid() & 0xFFFF);
    packet[4] = (unsigned char)(ident >> 8);
    packet[5] = (unsigned char)(ident & 0xFF);
    packet[6] = 0;
    packet[7] = 1;                                  /* sequence */
    memcpy(packet + 8, "zan-ping", 8);
    uint16_t sum = zan_plat_icmp_checksum(packet, sizeof packet);
    packet[2] = (unsigned char)(sum >> 8);
    packet[3] = (unsigned char)(sum & 0xFF);

    long long start = zan_plat_now_us();
    if (sendto(fd, packet, sizeof packet, 0, (struct sockaddr *)&dst,
               sizeof dst) < 0) {
        int err = errno;
        close(fd);
        if (err == EHOSTUNREACH || err == ENETUNREACH) return -2;
        return -3;
    }

    for (;;) {
        long long elapsed_ms = (zan_plat_now_us() - start) / 1000;
        int remain = (int)((long long)timeout_ms - elapsed_ms);
        if (remain <= 0) {
            close(fd);
            return -1;
        }
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ready = poll(&pfd, 1, remain);
        if (ready == 0) {
            close(fd);
            return -1;
        }
        if (ready < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -3;
        }

        unsigned char reply[1024];
        struct sockaddr_in from;
        socklen_t from_len = sizeof from;
        ssize_t got = recvfrom(fd, reply, sizeof reply, 0,
                               (struct sockaddr *)&from, &from_len);
        if (got < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -3;
        }
        /* 模块核心语义抽象与接口调用契约 */
        size_t offset = 0;
        if (!datagram) {
            if (got < 20) continue;
            offset = (size_t)((reply[0] & 0x0F) * 4);
            if ((size_t)got < offset + 8) continue;
        } else if (got < 8) {
            continue;
        }
        unsigned type = reply[offset];
        if (type == 0) {                            /* ICMP_ECHOREPLY */
            long long rtt_us = zan_plat_now_us() - start;
            close(fd);
            long long rtt_ms = rtt_us / 1000;
            return (int32_t)(rtt_ms > 0x7FFFFFFF ? 0x7FFFFFFF : rtt_ms);
        }
        if (type == 3) {                            /* 核心系统底层抽象与内存语义契约 */
            close(fd);
            return -2;
        }
        if (type == 11) {                           /* 核心系统底层抽象与内存语义契约 */
            close(fd);
            return -2;
        }
        /* 核心系统底层抽象与内存语义契约 */
    }
#endif
}

/* 跨平台安全子进程创建与执行 */

#ifdef _WIN32
static void zan_win_buf_append(char *buf, size_t cap, size_t *len, const char *s) {
    if (!s) return;
    size_t slen = strlen(s);
    if (*len + slen >= cap) return;
    memcpy(buf + *len, s, slen);
    *len += slen;
    buf[*len] = '\0';
}

static void zan_win_append_arg(char *buf, size_t cap, size_t *len, const char *a) {
    if (!a || !*a) {
        zan_win_buf_append(buf, cap, len, "\"\"");
        return;
    }
    int need_q = 0;
    for (const char *p = a; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\"' || *p == '\n' || *p == '\r') {
            need_q = 1;
            break;
        }
    }
    if (!need_q) {
        zan_win_buf_append(buf, cap, len, a);
        return;
    }
    zan_win_buf_append(buf, cap, len, "\"");
    int bs = 0;
    for (const char *p = a; *p; p++) {
        if (*p == '\\') {
            bs++;
        } else if (*p == '\"') {
            for (int b = 0; b < bs * 2 + 1; b++) zan_win_buf_append(buf, cap, len, "\\");
            zan_win_buf_append(buf, cap, len, "\"");
            bs = 0;
        } else {
            for (int b = 0; b < bs; b++) zan_win_buf_append(buf, cap, len, "\\");
            bs = 0;
            char ch[2] = { *p, '\0' };
            zan_win_buf_append(buf, cap, len, ch);
        }
    }
    for (int b = 0; b < bs * 2; b++) zan_win_buf_append(buf, cap, len, "\\");
    zan_win_buf_append(buf, cap, len, "\"");
}

static char *zan_win_quote_cmdline(const char *exe, const char **args, int32_t argc) {
    size_t cap = 256;
    for (int32_t i = 0; i < argc; i++) {
        if (args[i]) cap += strlen(args[i]) * 2 + 8;
    }
    if (exe) cap += strlen(exe) * 2 + 8;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';
    size_t len = 0;

    zan_win_append_arg(buf, cap, &len, exe);
    for (int32_t i = 0; i < argc; i++) {
        zan_win_buf_append(buf, cap, &len, " ");
        zan_win_append_arg(buf, cap, &len, args[i]);
    }
    return buf;
}
#endif

int32_t zan_proc_run_safe(const char *exe, const char **args, int32_t argc) {
    if (!exe) return -1;
#ifdef _WIN32
    char *cmdline = zan_win_quote_cmdline(exe, args, argc);
    if (!cmdline) return -1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, NULL, 0);
    if (wlen <= 0) { free(cmdline); return -1; }
    wchar_t *wcmd = (wchar_t *)malloc(wlen * sizeof(wchar_t));
    if (!wcmd) { free(cmdline); return -1; }
    MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd, wlen);
    free(cmdline);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, FALSE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(wcmd);
    if (!ok) return -1;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int32_t)code;
#else
    /* 内部辅助逻辑 */
    char **argv = (char **)malloc(((size_t)argc + 2) * sizeof(char *));
    if (!argv) return -1;
    argv[0] = (char *)exe;
    for (int32_t i = 0; i < argc; i++) {
        argv[i + 1] = (char *)args[i];
    }
    argv[argc + 1] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        free(argv);
        return -1;
    }
    if (pid == 0) {
        /* 内部实现与并发/内存约束规范 */
        execvp(exe, argv);
        _exit(127);
    }
    free(argv);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return (int32_t)WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + (int32_t)WTERMSIG(status);
    return -1;
#endif
}

#ifndef _WIN32
#if !defined(__linux__)
/* 模块核心语义抽象与接口调用契约 */
static pthread_mutex_t g_proc_pipe_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_proc_pipe_once = PTHREAD_ONCE_INIT;
static int g_proc_pipe_guard_error;
static void zan_proc_pipe_fork_prepare(void) { pthread_mutex_lock(&g_proc_pipe_guard); }
static void zan_proc_pipe_fork_done(void) { pthread_mutex_unlock(&g_proc_pipe_guard); }
static void zan_proc_pipe_guard_init(void) {
    g_proc_pipe_guard_error = pthread_atfork(zan_proc_pipe_fork_prepare,
                                            zan_proc_pipe_fork_done,
                                            zan_proc_pipe_fork_done);
}
#endif

static int zan_proc_launch_pipe(int fds[2]) {
#if defined(__linux__)
    if (pipe2(fds, O_CLOEXEC) < 0) return -1;
#else
    pthread_once(&g_proc_pipe_once, zan_proc_pipe_guard_init);
    if (g_proc_pipe_guard_error) return -1;
    pthread_mutex_lock(&g_proc_pipe_guard);
    if (pipe(fds) < 0) {
        pthread_mutex_unlock(&g_proc_pipe_guard);
        return -1;
    }
    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) goto failed;
#endif
    /* 模块核心语义抽象与接口调用契约 */
    for (int i = 0; i < 2; i++) {
        if (fds[i] >= 3) continue;
        int moved = fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
        if (moved < 0) goto failed;
        close(fds[i]);
        fds[i] = moved;
    }
#if !defined(__linux__)
    pthread_mutex_unlock(&g_proc_pipe_guard);
#endif
    return 0;
failed:
    close(fds[0]);
    close(fds[1]);
#if !defined(__linux__)
    pthread_mutex_unlock(&g_proc_pipe_guard);
#endif
    return -1;
}
#endif

int32_t zan_proc_start_detached_safe(const char *exe, const char **args, int32_t argc) {
    if (!exe) return -1;
#ifdef _WIN32
    char *cmdline = zan_win_quote_cmdline(exe, args, argc);
    if (!cmdline) return -1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, NULL, 0);
    if (wlen <= 0) { free(cmdline); return -1; }
    wchar_t *wcmd = (wchar_t *)malloc(wlen * sizeof(wchar_t));
    if (!wcmd) { free(cmdline); return -1; }
    MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd, wlen);
    free(cmdline);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, FALSE,
                             CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi);
    free(wcmd);
    if (!ok) return -1;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
#else
    /* 模块核心语义抽象与接口调用契约 */
    char **argv = (char **)malloc(((size_t)argc + 2) * sizeof(char *));
    if (!argv) return -1;
    argv[0] = (char *)exe;
    for (int32_t i = 0; i < argc; i++) {
        argv[i + 1] = (char *)args[i];
    }
    argv[argc + 1] = NULL;

    /* 内部辅助逻辑 */
    int launch_pipe[2];
    if (zan_proc_launch_pipe(launch_pipe) < 0) { free(argv); return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        close(launch_pipe[0]);
        close(launch_pipe[1]);
        free(argv);
        return -1;
    }
    if (pid == 0) {
        close(launch_pipe[0]);
        /* 双重 fork 隔离守护子进程 */
        if (setsid() < 0) goto launch_failed;
        pid_t gc = fork();
        if (gc < 0) goto launch_failed;
        if (gc == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO) close(devnull);
            }
            execvp(exe, argv);
            goto launch_failed;
        }
        close(launch_pipe[1]);
        _exit(0);
launch_failed: {
            char failed = 1;
            while (write(launch_pipe[1], &failed, 1) < 0 && errno == EINTR) {}
            _exit(127);
        }
    }
    free(argv);
    close(launch_pipe[1]);
    char failed;
    ssize_t launch_result;
    do { launch_result = read(launch_pipe[0], &failed, 1); }
    while (launch_result < 0 && errno == EINTR);
    close(launch_pipe[0]);
    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return launch_result == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
#endif
}

int32_t zan_proc_start_program_safe(const char *exe, const char *log_path) {
    if (!exe) return -1;
#ifdef _WIN32
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    HANDLE hLog = INVALID_HANDLE_VALUE;
    DWORD flags = DETACHED_PROCESS;
    BOOL inherit = TRUE;

    if (log_path && log_path[0]) {
        int wpath_len = MultiByteToWideChar(CP_UTF8, 0, log_path, -1, NULL, 0);
        if (wpath_len > 0) {
            wchar_t *wlog = (wchar_t *)malloc(wpath_len * sizeof(wchar_t));
            if (wlog) {
                MultiByteToWideChar(CP_UTF8, 0, log_path, -1, wlog, wpath_len);
                SECURITY_ATTRIBUTES sa;
                memset(&sa, 0, sizeof(sa));
                sa.nLength = sizeof(sa);
                sa.bInheritHandle = TRUE;
                hLog = CreateFileW(wlog, FILE_APPEND_DATA | SYNCHRONIZE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                free(wlog);
            }
        }
    }

    if (hLog != INVALID_HANDLE_VALUE) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = hLog;
        si.hStdError = hLog;
        flags = CREATE_NO_WINDOW;
    }

    /* 路径安全转义包装防注入 */
    int wexe_len = MultiByteToWideChar(CP_UTF8, 0, exe, -1, NULL, 0);
    if (wexe_len <= 0) {
        if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
        return -1;
    }
    wchar_t *wcmd = (wchar_t *)malloc((wexe_len + 4) * sizeof(wchar_t));
    if (!wcmd) {
        if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
        return -1;
    }
    wcmd[0] = L'"';
    MultiByteToWideChar(CP_UTF8, 0, exe, -1, wcmd + 1, wexe_len);
    /* 模块核心语义抽象与接口调用契约 */
    wcmd[wexe_len] = L'"';
    wcmd[wexe_len + 1] = L'\0';

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, inherit, flags, NULL, NULL, &si, &pi);
    free(wcmd);
    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    if (!ok) return -1;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* 内部辅助逻辑 */
        if (setsid() < 0) _exit(127);
        pid_t gc = fork();
        if (gc < 0) _exit(127);
        if (gc == 0) {
            /* 核心系统底层抽象与内存语义契约 */
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                if (devnull > STDIN_FILENO) close(devnull);
            }
            if (log_path && log_path[0]) {
                int outfd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (outfd >= 0) {
                    dup2(outfd, STDOUT_FILENO);
                    dup2(outfd, STDERR_FILENO);
                    if (outfd > STDERR_FILENO) close(outfd);
                }
            } else {
                int outnull = open("/dev/null", O_WRONLY);
                if (outnull >= 0) {
                    dup2(outnull, STDOUT_FILENO);
                    dup2(outnull, STDERR_FILENO);
                    if (outnull > STDERR_FILENO) close(outnull);
                }
            }
            char *argv[2];
            argv[0] = (char *)exe;
            argv[1] = NULL;
            execvp(exe, argv);
            _exit(127);
        }
        _exit(0);
    }
    /* 回收中间引导进程防止僵尸进程 */
    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return 0;
#endif
}

int32_t zan_proc_capture_safe(const char *exe, const char **args, int32_t argc,
                              char **out_buf, int32_t *out_len, int32_t *exit_code) {
    if (!exe || !out_buf || !out_len || !exit_code) return -1;
    *out_buf = NULL;
    *out_len = 0;
    *exit_code = -1;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = NULL;
    HANDLE hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return -1;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    char *cmdline = zan_win_quote_cmdline(exe, args, argc);
    if (!cmdline) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, NULL, 0);
    wchar_t *wcmd = (wchar_t *)malloc(wlen * sizeof(wchar_t));
    if (!wcmd) {
        free(cmdline);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd, wlen);
    free(cmdline);

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(wcmd);
    CloseHandle(hWrite); /* 底层系统交互与数据协议契约 */

    if (!ok) {
        CloseHandle(hRead);
        return -1;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        CloseHandle(hRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return -1;
    }

    DWORD bytesRead = 0;
    while (ReadFile(hRead, buf + len, (DWORD)(cap - len - 1), &bytesRead, NULL) && bytesRead > 0) {
        len += bytesRead;
        if (len + 1024 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
    }
    CloseHandle(hRead);
    buf[len] = '\0';

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    *out_buf = buf;
    *out_len = (int32_t)len;
    *exit_code = (int32_t)code;
    return 0;
#else
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;
    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    /* 模块核心语义抽象与接口调用契约 */
    char **argv = (char **)malloc(((size_t)argc + 2) * sizeof(char *));
    if (!argv) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    argv[0] = (char *)exe;
    for (int32_t i = 0; i < argc; i++) {
        argv[i + 1] = (char *)args[i];
    }
    argv[argc + 1] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        free(argv);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        /* Child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(exe, argv);
        _exit(127);
    }
    free(argv);

    close(pipefd[1]);
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        close(pipefd[0]);
        return -1;
    }

    ssize_t r;
    while ((r = read(pipefd[0], buf + len, cap - len - 1)) > 0 ||
           (r < 0 && errno == EINTR)) {
        if (r > 0) {
            len += (size_t)r;
            if (len + 1024 >= cap) {
                cap *= 2;
                char *nb = (char *)realloc(buf, cap);
                if (!nb) break;
                buf = nb;
            }
        }
    }
    close(pipefd[0]);
    buf[len] = '\0';

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }
    int code = -1;
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);

    *out_buf = buf;
    *out_len = (int32_t)len;
    *exit_code = code;
    return 0;
#endif
}

void zan_proc_free_buf(char *buf) {
    if (buf) free(buf);
}

