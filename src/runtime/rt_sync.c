#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if (defined(__FreeBSD__) || defined(__DragonFly__)) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "rt_sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rt_timer.h"
#include "../common/zan_abi.h"
#include "rt_oom.h"  /* B-ID98 站点化 OOM 注入 */

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
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <pthread_np.h>
#endif
#if defined(__NetBSD__)
#include <lwp.h>
#endif
#endif

#if defined(__linux__) && !defined(__GLIBC_PREREQ)
#include <features.h>
#endif

#if defined(__ANDROID__) || (defined(__OHOS__) && !defined(ZAN_SYNC_NO_SHM_SHIM))
static const char *zan_android_shm_dir(void) {
    const char *d = getenv("ZAN_SHM_DIR");
    return (d && d[0]) ? d : "/data/local/tmp";
}
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
#define ZAN_TABLE_VERSION 5
#define ZAN_TABLE_MAX_COLUMNS 64
#define ZAN_TABLE_COLUMN_NAME 32
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
    uint32_t struct_lock;
    zan_shared_column columns[ZAN_TABLE_MAX_COLUMNS];
} zan_shared_header;

typedef struct {
    zan_shared_header *header;
    size_t mapped_size;
    char map_name[256];
    int anonymous;
    int attached;
#ifdef _WIN32
    HANDLE mapping;
#else
    int fd;
    pthread_mutex_t local_mutex;
    int local_mutex_ready;
#endif
} zan_shared_table;

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
#define ZAN_LOCK_HELD_BIT 0x80000000u
#define ZAN_LOCK_PID_MASK 0x7FFFFFFFu

static int zan_pid_alive(uint32_t pid) {
    if (!pid) return 0;
#ifdef _WIN32
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
            YieldProcessor();
        } else if (spin < 4096 || (spin & 1023u) != 0) {
            SwitchToThread();
        } else {
            LONG held = InterlockedCompareExchange((volatile LONG *)word,
                                                   0, 0);
            LONG hpid = held & (LONG)ZAN_LOCK_PID_MASK;
            if ((held & (LONG)ZAN_LOCK_HELD_BIT) && hpid &&
                !zan_pid_alive((uint32_t)hpid)) {
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
#elif defined(__aarch64__) || defined(__arm__)
            __asm__ __volatile__("yield" ::: "memory");
#elif defined(__riscv)
            __asm__ __volatile__(".insn i 0x0F, 0, x0, x0, 0x010" ::: "memory");
#endif
        } else if (spin < 4096 || (spin & 1023u) != 0) {
            sched_yield();
        } else {
            uint32_t held = __atomic_load_n(word, __ATOMIC_RELAXED);
            uint32_t hpid = held & ZAN_LOCK_PID_MASK;
            if ((held & ZAN_LOCK_HELD_BIT) && hpid &&
                !zan_pid_alive(hpid)) {
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
        memset(row + 8, 0, header->row_stride - 8);
        zan_publish_state(row, ZAN_SLOT_TOMBSTONE);
        header->count--;
        zan_row_unlock(row);
        batch++;
    }
}

static void zan_purge_expired(zan_shared_header *header) {
    if (__atomic_load_n(&header->expiry_count, __ATOMIC_RELAXED) == 0)
        return;
    static int64_t last_attempt_ms;
    int64_t now_ms = zan_wall_now_ms();
    int64_t last = __atomic_load_n(&last_attempt_ms, __ATOMIC_RELAXED);
    if (last > now_ms - ZAN_PURGE_MIN_INTERVAL_MS && last <= now_ms) return;
    __atomic_store_n(&last_attempt_ms, now_ms, __ATOMIC_RELAXED);
    uint64_t *heap = zan_expiry_heap(header);
    uint64_t top = __atomic_load_n(&heap[0], __ATOMIC_RELAXED);
    if (top >= header->capacity) return;
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
        *zan_row_hash(row) = hash;
        memcpy(zan_row_key(row), key, key_len);
        if ((size_t)key_len < header->key_size)
            zan_row_key(row)[key_len] = '\0';
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }

    if (create && first_tombstone != UINT64_MAX) {
        unsigned char *row = zan_row_at(header, first_tombstone);
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
    zan_struct_lock(header);
    row = zan_find_row(header, key, 1);
    zan_struct_unlock(header);
    return row;
}


static unsigned char *zan_find_row_hash(
    zan_shared_header *header, uint64_t hash, int create) {
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
        *zan_row_hash(row) = hash;
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }
    if (create && first_tombstone != UINT64_MAX) {
        unsigned char *row = zan_row_at(header, first_tombstone);
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

/* 操作系统原生线程、监视器锁与 UI 任务派发 */
#include "sync/rt_sync_thread.c"

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

static int zan_header_geometry_ok(const zan_shared_header *header) {
    if (header->capacity == 0 ||
        header->capacity > ZAN_TABLE_MAX_CAPACITY ||
        (header->capacity & (header->capacity - 1)) != 0)
        return 0;
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
    static uint32_t anon_seq = 0;
    char shm_name[64];
    uint32_t seq = __atomic_fetch_add(&anon_seq, 1, __ATOMIC_RELAXED);
    snprintf(shm_name, sizeof(shm_name), "/zan_table_%ld_%u_%u",
             (long)getpid(), (unsigned)time(NULL), seq);
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        zan_shared_table_free(table);
        return 0;
    }
    shm_unlink(shm_name);
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

int64_t zan_shared_table_handle(int64_t handle) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !table->anonymous) return 0;
#ifdef _WIN32
    return (int64_t)(intptr_t)table->mapping;
#else
    return (int64_t)table->fd;
#endif
}

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
    enum { ZAN_MINCORE_BATCH = 16384 };
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

/* get_int 的容错版：命中写出 *out_value 并返回 1；键不存在/列缺失/
 * 行失效返回 0（不写出）。区分"值恰好是 0"与"没读到"。 */
int32_t zan_shared_table_try_get_int(
    int64_t handle, const char *key, const char *column_name,
    int64_t *out_value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !out_value) return 0;
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
    memcpy(out_value, row + column->offset, sizeof(*out_value));
    zan_row_unlock(row);
    return 1;
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

int64_t zan_monotonic_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (int64_t)((now.QuadPart / frequency.QuadPart) * 1000000
        + ((now.QuadPart % frequency.QuadPart) * 1000000) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
#endif
}

int64_t zan_monotonic_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
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

/* get_int_at 的容错版，口径同 zan_shared_table_try_get_int。 */
int32_t zan_shared_table_try_get_int_at(
    int64_t handle, int64_t key_hash, const char *column_name,
    int64_t *out_value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !out_value) return 0;
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
    memcpy(out_value, row + column->offset, sizeof(*out_value));
    zan_row_unlock(row);
    return 1;
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), ZAN_PURGE_MAX_BATCH);
    unsigned char *row = zan_find_row_hash(
        table->header, (uint64_t)key_hash, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_remove(table->header, row);
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
    zan_purge_expired_locked(table->header, now_ms, ZAN_PURGE_MAX_BATCH);
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), ZAN_PURGE_MAX_BATCH);
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        int64_t current_owner = 0;
        memcpy(&current_owner, row + owner_column->offset, sizeof(current_owner));
        if (current_owner == owner) {
            zan_expiry_remove(table->header, row);
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), ZAN_PURGE_MAX_BATCH);
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_remove(table->header, row);
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
    zan_struct_lock(table->header);
    for (uint64_t i = 0; i < table->header->capacity; i++) {
        unsigned char *row = zan_row_at(table->header, i);
        uint32_t state = zan_load_state(row);
        if (state == ZAN_SLOT_EMPTY) continue;
        zan_row_lock(row);
        if (zan_load_state(row) != ZAN_SLOT_EMPTY) {
            if (zan_load_state(row) == ZAN_SLOT_USED)
                memset(row + 8, 0, table->header->row_stride - 8);
            zan_publish_state(row, ZAN_SLOT_EMPTY);
        }
        zan_row_unlock(row);
    }
    memset(zan_expiry_heap(table->header), 0,
           (size_t)table->header->capacity * sizeof(uint64_t));
    table->header->count = 0;
    table->header->expiry_count = 0;
    zan_struct_unlock(table->header);
}


long long zan_exe_dir_into(char *out, long long cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
#ifdef _WIN32
    char buf[1024];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    while (n > 0 && buf[n - 1] != '\\') n--;
    if (n > 0) n--;
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

#ifdef _WIN32
typedef struct {
    HANDLE h;
} zan_mmap_handle;
#else
typedef struct {
    int fd;
    int owner;
    char name[256];
} zan_mmap_handle;
#endif

/* 创建指定字节大小的具名共享内存区 */
long long zan_mmap_create(const char *name, long long size) {
    if (!name || !name[0] || size <= 0) return 0;
#ifdef _WIN32
    DWORD hi = (DWORD)(((unsigned long long)size) >> 32);
    DWORD lo = (DWORD)((unsigned long long)size & 0xFFFFFFFFULL);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (wlen <= 0) return 0;
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
    if (wlen <= 0) return 0;
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
    if (wlen <= 0) return 0;
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

long long zan_mmap_unmap(long long ptr, long long size) {
    if (!ptr) return 0;
#ifdef _WIN32
    (void)size;
    return UnmapViewOfFile((void *)(intptr_t)ptr) ? 1 : 0;
#else
    return munmap((void *)(intptr_t)ptr, (size_t)size) == 0 ? 1 : 0;
#endif
}

long long zan_mmap_flush(long long ptr, long long size) {
    if (!ptr) return 1;
#ifdef _WIN32
    return FlushViewOfFile((void *)(intptr_t)ptr, (size_t)size) ? 1 : 0;
#else
    return msync((void *)(intptr_t)ptr, (size_t)size, MS_SYNC) == 0 ? 1 : 0;
#endif
}

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

/* 跨平台网络接口与 ICMP 探测 */
#include "sync/rt_sync_net.c"

/* 跨平台安全子进程创建与执行 */
#include "sync/rt_sync_proc.c"

