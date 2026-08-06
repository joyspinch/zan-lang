#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "rt_sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <pthread.h>
#include <sched.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__linux__) && !defined(__GLIBC_PREREQ)
#include <features.h>
#endif

#if !defined(_WIN32) && !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif
#if !defined(_WIN32) && !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif

#define ZAN_TABLE_MAGIC UINT64_C(0x5a414e54424c3031)
#define ZAN_TABLE_VERSION 2
#define ZAN_TABLE_MAX_COLUMNS 16
#define ZAN_TABLE_COLUMN_NAME 32
/* Schema ceilings. The checker rejects a constant width past these where it
 * is declared (checker.c: CHECKER_SHARED_MAX_*), so raising one here means
 * raising it there. */
#define ZAN_TABLE_MAX_KEY 256
#define ZAN_TABLE_MAX_STRING 65536
#define ZAN_TABLE_MAX_CAPACITY (UINT64_C(1) << 20)
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
    /* Structural lock, held only while a slot is claimed, tombstoned or the
     * table is cleared. It lives in the mapping itself, so it serializes
     * processes without a syscall; value reads and writes never take it. */
    uint32_t struct_lock;
    zan_shared_column columns[ZAN_TABLE_MAX_COLUMNS];
} zan_shared_header;

typedef struct {
    zan_shared_header *header;
    size_t mapped_size;
    char map_name[96];
    char lock_name[96];
#ifdef _WIN32
    HANDLE mapping;
    HANDLE mutex;
#else
    int fd;
    int lock_fd;
    pthread_mutex_t local_mutex;
    int local_mutex_ready;
#endif
} zan_shared_table;

#ifdef _WIN32
static INIT_ONCE zan_shared_string_once = INIT_ONCE_STATIC_INIT;
static DWORD zan_shared_string_slot = FLS_OUT_OF_INDEXES;
static char zan_shared_string_fallback[ZAN_TABLE_MAX_STRING + 1];

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
        return zan_shared_string_fallback;
    }
    buffer = (char *)FlsGetValue(zan_shared_string_slot);
    if (!buffer) {
        buffer = (char *)calloc(ZAN_TABLE_MAX_STRING + 1, 1);
        if (!buffer || !FlsSetValue(zan_shared_string_slot, buffer)) {
            free(buffer);
            return zan_shared_string_fallback;
        }
    }
    return buffer;
}
#else
static _Thread_local char zan_shared_string[ZAN_TABLE_MAX_STRING + 1];

static char *zan_get_shared_string(void) {
    return zan_shared_string;
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

static void zan_make_names(
    const char *name, char map_name[96], char lock_name[96]) {
    unsigned long long hash = (unsigned long long)zan_hash_bytes(name);
#ifdef _WIN32
    snprintf(map_name, 96, "Local\\zan_table_%016llx", hash);
    snprintf(lock_name, 96, "Local\\zan_table_lock_%016llx", hash);
#else
    snprintf(
        map_name, 96, "/tmp/zan_table_%lu_%016llx.shm",
        (unsigned long)getuid(), hash);
    snprintf(
        lock_name, 96, "/tmp/zan_table_%lu_%016llx.lock",
        (unsigned long)getuid(), hash);
#endif
}

/* Structural lock: a compare-and-swap spinlock on a word inside the shared
 * mapping. It replaces the per-operation flock()/named-mutex pair, which cost
 * two syscalls on every get and set and serialized every process on one kernel
 * lock -- with four processes that collapsed the table from ~4M to ~110k
 * operations per second each. Only slot allocation, delete and clear take it
 * now; reads and writes of an existing row are guarded by that row's own
 * spinlock, so unrelated keys never contend. */
static void zan_struct_lock(zan_shared_header *header) {
    for (;;) {
#ifdef _WIN32
        if (InterlockedCompareExchange(
                (volatile LONG *)&header->struct_lock, 1, 0) == 0) {
            return;
        }
        SwitchToThread();
#else
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(
                &header->struct_lock, &expected, 1, 0,
                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return;
        }
        sched_yield();
#endif
    }
}

static void zan_struct_unlock(zan_shared_header *header) {
#ifdef _WIN32
    InterlockedExchange((volatile LONG *)&header->struct_lock, 0);
#else
    __atomic_store_n(&header->struct_lock, 0, __ATOMIC_RELEASE);
#endif
}

/* Resolves a key to its row, claiming a slot when `create` is set. The lookup
 * runs without the structural lock (slot states only move forward and the key
 * bytes are published before the state), so the hot path -- a key that already
 * exists -- is lock-free apart from the row spinlock the caller takes. */
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

static void zan_row_lock(unsigned char *row) {
    uint32_t *lock = zan_row_lock_word(row);
    for (;;) {
#ifdef _WIN32
        if (InterlockedCompareExchange(
                (volatile LONG *)lock, 1, 0) == 0) {
            return;
        }
        SwitchToThread();
#else
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(
                lock, &expected, 1, 0,
                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return;
        }
        sched_yield();
#endif
    }
}

static void zan_row_unlock(unsigned char *row) {
#ifdef _WIN32
    InterlockedExchange((volatile LONG *)zan_row_lock_word(row), 0);
#else
    __atomic_store_n(zan_row_lock_word(row), 0, __ATOMIC_RELEASE);
#endif
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

static void zan_purge_expired_locked(zan_shared_header *header, int64_t now_ms) {
    uint64_t *heap = zan_expiry_heap(header);
    while (header->expiry_count > 0) {
        unsigned char *row = zan_row_at(header, heap[0]);
        if (*zan_row_expires_at(row) > now_ms) break;
        zan_row_lock(row);
        zan_expiry_remove(header, row);
        memset(row, 0, header->row_stride);
        zan_publish_state(row, ZAN_SLOT_TOMBSTONE);
        header->count--;
        zan_row_unlock(row);
    }
}

static void zan_purge_expired(zan_shared_header *header) {
    if (header->expiry_count == 0) return;
    uint64_t *heap = zan_expiry_heap(header);
    unsigned char *row = zan_row_at(header, heap[0]);
    int64_t now_ms = zan_wall_now_ms();
    if (*zan_row_expires_at(row) > now_ms) return;
    zan_struct_lock(header);
    zan_purge_expired_locked(header, now_ms);
    zan_struct_unlock(header);
}

static unsigned char *zan_find_row(
    zan_shared_header *header, const char *key, int create) {
    size_t key_len = zan_strnlen(key, header->key_size + 1u);
    if (!key || key_len == 0 || key_len > header->key_size) return NULL;

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
        memset(row, 0, header->row_stride);
        /* Key and hash are published before the slot becomes visible as USED,
         * so a concurrent lock-free lookup never matches a half-written row. */
        *zan_row_hash(row) = hash;
        memcpy(zan_row_key(row), key, key_len);
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }

    if (create && first_tombstone != UINT64_MAX) {
        unsigned char *row = zan_row_at(header, first_tombstone);
        memset(row, 0, header->row_stride);
        *zan_row_hash(row) = hash;
        memcpy(zan_row_key(row), key, key_len);
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
    /* Missing and we may create it: take the structural lock and look again,
     * since another process may have inserted the same key meanwhile. */
    zan_struct_lock(header);
    row = zan_find_row(header, key, 1);
    zan_struct_unlock(header);
    return row;
}

/* ---- rows addressed by a caller-computed hash ----
 * A server hashes "GET_/user/list" once per request and reuses that value for
 * the route attribute table and the statistics table, instead of formatting it
 * back into a key string and having the table hash it again. Identity is the
 * hash itself, so a table is used either by key or by hash, never both. */

static unsigned char *zan_find_row_hash(
    zan_shared_header *header, uint64_t hash, int create) {
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
        memset(row, 0, header->row_stride);
        *zan_row_hash(row) = hash;
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }
    if (create && first_tombstone != UINT64_MAX) {
        unsigned char *row = zan_row_at(header, first_tombstone);
        memset(row, 0, header->row_stride);
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
    if (table->mutex) CloseHandle(table->mutex);
    if (table->mapping) CloseHandle(table->mapping);
#else
    if (table->header && table->mapped_size) {
        munmap(table->header, table->mapped_size);
    }
    if (table->fd >= 0) close(table->fd);
    if (table->lock_fd >= 0) close(table->lock_fd);
    if (table->local_mutex_ready) pthread_mutex_destroy(&table->local_mutex);
#endif
    free(table);
}

/* ---- Thread spawn: run a Zan delegate on a new detached OS thread ---- */

typedef void (*zan_thread_body_fn)(void);

/* Emitted code defines this when the program can throw; it drops the calling
 * thread's exception-handling state. The fallback here is a weak *definition*
 * rather than a weak reference: a weak undefined symbol resolves to null on
 * ELF but is a link error on Mach-O, and this object is linked into static
 * macOS binaries too. The emitted strong definition wins wherever it exists. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void __zan_eh_release(void) { }
#else
void __zan_eh_release(void) { }
#endif

/* Release whatever per-thread runtime state the calling thread accumulated.
 * Public so a thread the runtime did not start -- an X11 / SDL / Cocoa
 * callback thread that ran Zan code -- can hand its slot back instead of
 * holding one for the life of the process. */
void zan_thread_detach(void) {
    __zan_eh_release();
}

#ifdef _WIN32
static DWORD WINAPI zan_thread_trampoline(LPVOID arg) {
    zan_thread_body_fn body = (zan_thread_body_fn)arg;
    if (body) body();
    zan_thread_detach();
    return 0;
}

int32_t zan_thread_start(void *body) {
    if (!body) return 0;
    HANDLE h = CreateThread(NULL, 0, zan_thread_trampoline, body, 0, NULL);
    if (!h) return 0;
    CloseHandle(h); /* detach: the worker runs to completion on its own */
    return 1;
}
#else
static void *zan_thread_trampoline(void *arg) {
    zan_thread_body_fn body = (zan_thread_body_fn)arg;
    if (body) body();
    zan_thread_detach();
    return NULL;
}

int32_t zan_thread_start(void *body) {
    if (!body) return 0;
    pthread_t t;
    if (pthread_create(&t, NULL, zan_thread_trampoline, body) != 0) return 0;
    pthread_detach(t);
    return 1;
}
#endif

#if defined(__linux__)
/* glibc >= 2.30 exposes gettid(); older libcs need the syscall directly.
 * The TID is the per-thread id -- getpid() returns the same PID for every
 * thread, which is exactly the confusion Thread.CurrentId() must avoid. */
#if defined(__GLIBC__) && (__GLIBC_PREREQ(2, 30))
#define zan_tid() gettid()
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
    /* pthread_self() is a pointer and varies run to run; pthread_threadid_np
     * yields a stable per-thread numeric id. */
    uint64_t tid = 0;
    if (pthread_threadid_np(NULL, &tid) != 0) return 0;
    return (int64_t)tid;
#else
    /* Other POSIX: hash the pthread_t (typically a pointer) into a stable
     * number. Address-space layout randomization makes the raw pointer vary
     * between runs, so fold it to a 32-bit id. */
    uintptr_t self = (uintptr_t)pthread_self();
    return (int64_t)((self >> 16) ^ (self & 0xffffffffu));
#endif
}

/* ---- lock-statement monitor ---------------------------------------------
 * Backs the `lock (obj) { ... }` statement. C# gives every object its own
 * monitor; a single process-wide mutex would make two threads locking two
 * unrelated objects wait on each other, so the monitors are striped: the
 * object's address picks one of ZAN_MONITOR_STRIPES recursive locks. Two
 * objects can still share a stripe, which only over-serializes -- it never
 * loses mutual exclusion, and re-entry stays legal because each stripe is
 * recursive. Enter and exit hash the same pointer, so they always agree. */

#define ZAN_MONITOR_STRIPES 64

static unsigned zan_monitor_stripe(void *obj) {
    uintptr_t bits = (uintptr_t)obj;
    /* Allocator addresses are 8- or 16-byte aligned, so the low bits are dead;
     * fold the pointer before taking the index. */
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

/* ---- UI-thread dispatch queue (Control.Invoke equivalent) --------------
 * A fixed-capacity ring of Zan delegate function pointers. Background threads
 * enqueue with zan_dispatch_post(); the UI thread drains with
 * zan_dispatch_take() once per frame and invokes each on the UI thread. Only
 * the raw function pointer crosses the thread boundary here — no Zan heap
 * object is allocated off-thread — so the ARC runtime is never touched from a
 * background thread. The queue itself is guarded by an OS mutex. */

#define ZAN_DISPATCH_CAP 1024
static void *g_dispatch_ring[ZAN_DISPATCH_CAP];
static int g_dispatch_head = 0;
static int g_dispatch_tail = 0;

#ifdef _WIN32
/* The lock is created by the OS on first use rather than by a `ready` flag the
 * callers test: two threads posting for the first time both saw the flag clear
 * and both ran InitializeCriticalSection on the same section, and a thread
 * could enter it while the other was still initializing it. zan_dispatch_take
 * did not check the flag at all, so draining before the first post entered an
 * uninitialized section. */
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

/* Initialise the queue on the UI thread before any worker is spawned. */
void zan_dispatch_init(void) {
    zan_dispatch_lock();
    g_dispatch_head = 0;
    g_dispatch_tail = 0;
    zan_dispatch_unlock();
}

/* Enqueue a delegate to run on the UI thread. Thread-safe. Returns 1 on
 * success, 0 if the delegate was null or the queue was full. */
int32_t zan_dispatch_post(void *fn) {
    if (!fn) return 0;
    int32_t ok = 0;
    zan_dispatch_lock();
    int next = (g_dispatch_tail + 1) % ZAN_DISPATCH_CAP;
    if (next != g_dispatch_head) {
        g_dispatch_ring[g_dispatch_tail] = fn;
        g_dispatch_tail = next;
        ok = 1;
    }
    zan_dispatch_unlock();
    return ok;
}

/* Pop the next queued delegate (UI thread), or NULL when the queue is empty. */
void *zan_dispatch_take(void) {
    void *fn = NULL;
    zan_dispatch_lock();
    if (g_dispatch_head != g_dispatch_tail) {
        fn = g_dispatch_ring[g_dispatch_head];
        g_dispatch_head = (g_dispatch_head + 1) % ZAN_DISPATCH_CAP;
    }
    zan_dispatch_unlock();
    return fn;
}

/* Drop every queued delegate. Called on the UI thread when a window closes:
 * work a background worker posted for the dead window must not run on the
 * next window, whose frame loop reuses the same global queue. */
void zan_dispatch_clear(void) {
    zan_dispatch_lock();
    g_dispatch_head = 0;
    g_dispatch_tail = 0;
    zan_dispatch_unlock();
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

int64_t zan_shared_table_create(
    const char *name, int32_t capacity_value, int32_t key_size_value,
    const char *schema) {
    if (!name || !*name || capacity_value <= 0 ||
        capacity_value > (int32_t)ZAN_TABLE_MAX_CAPACITY ||
        key_size_value <= 0 || key_size_value > ZAN_TABLE_MAX_KEY) {
        return 0;
    }

    uint64_t capacity = zan_round_capacity((uint64_t)capacity_value);
    uint32_t key_size = (uint32_t)key_size_value;
    zan_shared_column columns[ZAN_TABLE_MAX_COLUMNS];
    uint32_t column_count = 0;
    uint32_t row_stride = 0;
    memset(columns, 0, sizeof(columns));
    if (!zan_parse_schema(
            schema, key_size, columns, &column_count, &row_stride)) {
        return 0;
    }

    size_t rows_offset = zan_align8(sizeof(zan_shared_header));
    if ((size_t)capacity > (SIZE_MAX - rows_offset) / row_stride) return 0;
    size_t rows_size = (size_t)capacity * row_stride;
    if ((size_t)capacity > (SIZE_MAX - rows_offset - rows_size) / sizeof(uint64_t)) return 0;
    size_t total_size = rows_offset + rows_size +
        (size_t)capacity * sizeof(uint64_t);

    zan_shared_table *table = (zan_shared_table *)calloc(1, sizeof(*table));
    if (!table) return 0;
#ifndef _WIN32
    table->fd = -1;
    table->lock_fd = -1;
    if (pthread_mutex_init(&table->local_mutex, NULL) != 0) {
        free(table);
        return 0;
    }
    table->local_mutex_ready = 1;
#endif
    zan_make_names(name, table->map_name, table->lock_name);

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
    table->mutex = CreateMutexA(NULL, FALSE, table->lock_name);
    if (!table->mutex) {
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
    table->lock_fd = open(
        table->lock_name, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (table->lock_fd < 0) {
        unlink(table->map_name);
        zan_shared_table_free(table);
        return 0;
    }
#endif

    table->mapped_size = total_size;
    memset(table->header, 0, total_size);
    table->header->magic = ZAN_TABLE_MAGIC;
    table->header->version = ZAN_TABLE_VERSION;
    table->header->total_size = total_size;
    table->header->capacity = capacity;
    table->header->key_size = key_size;
    table->header->row_stride = row_stride;
    table->header->column_count = column_count;
    memcpy(table->header->columns, columns, sizeof(columns));
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
    table->header->ready = 1;
    return (int64_t)(intptr_t)table;
}

int64_t zan_shared_table_open(const char *name) {
    if (!name || !*name) return 0;
    zan_shared_table *table = (zan_shared_table *)calloc(1, sizeof(*table));
    if (!table) return 0;
#ifndef _WIN32
    table->fd = -1;
    table->lock_fd = -1;
    if (pthread_mutex_init(&table->local_mutex, NULL) != 0) {
        free(table);
        return 0;
    }
    table->local_mutex_ready = 1;
#endif
    zan_make_names(name, table->map_name, table->lock_name);

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
    table->mutex = OpenMutexA(
        SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, table->lock_name);
    if (!table->mutex) {
        zan_shared_table_free(table);
        return 0;
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
    table->lock_fd = open(
        table->lock_name, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (table->lock_fd < 0) {
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
        table->header->total_size < sizeof(zan_shared_header)) {
        zan_shared_table_free(table);
        return 0;
    }
#ifdef _WIN32
    table->mapped_size = (size_t)table->header->total_size;
#else
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
    if (unlink(table->map_name) != 0 && errno != ENOENT) result = 0;
    if (unlink(table->lock_name) != 0 && errno != ENOENT) result = 0;
#endif
    zan_shared_table_free(table);
    return result;
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
    char *destination = (char *)(row + column->offset);
    memset(destination, 0, column->size);
    memcpy(destination, value, value_len);
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
    size_t max_len = column->size - 1u;
    size_t len = zan_strnlen((const char *)(row + column->offset), max_len);
    memcpy(result, row + column->offset, len);
    result[len] = '\0';
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
    memcpy(&value, row + column->offset, sizeof(value));
    value += delta;
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return value;
}

/* Monotonic microseconds. Per-request timing needs better resolution than
 * GetTickCount64's ~15ms and must not allocate, which the Zan-side
 * clock_gettime wrapper does on every call. */
int64_t zan_monotonic_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (int64_t)((now.QuadPart * 1000000) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
#endif
}

/* Monotonic nanoseconds, for System.Diagnostics.Stopwatch. The clock id must
 * not be hardcoded on the Zan side: CLOCK_MONOTONIC is 1 on Linux but 6 on
 * macOS, and the struct timespec layout is not portable either. C compiles
 * the real macro and layout in. */
int64_t zan_monotonic_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (int64_t)((now.QuadPart * 1000000000) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
#endif
}

/* ---- the same operations against a caller-computed hash ---- */

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
    memcpy(&value, row + column->offset, sizeof(value));
    value += delta;
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return value;
}

/* Keeps the smaller (or larger, when `keep_larger`) of the stored value and
 * `value`, in one row-locked step. Slowest/fastest response times are the
 * reason the statistics table exists and read-modify-write from Zan would race
 * between processes. A zero stored value counts as unset for the minimum. */
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
    char *destination = (char *)(row + column->offset);
    memset(destination, 0, column->size);
    memcpy(destination, value, value_len);
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
    size_t max_len = column->size - 1u;
    size_t len = zan_strnlen((const char *)(row + column->offset), max_len);
    memcpy(result, row + column->offset, len);
    result[len] = '\0';
    zan_row_unlock(row);
    return result;
}

/* Compares a string column against `text` without handing the stored bytes
 * back to the caller. The route table verifies the original path on every
 * lookup to rule out a hash collision, and returning the column would allocate
 * a string per request just to throw it away. */
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
    int equal = strncmp(
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms());
    unsigned char *row = zan_find_row_hash(
        table->header, (uint64_t)key_hash, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_remove(table->header, row);
        memset(row, 0, table->header->row_stride);
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms());
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
    int64_t expires_at = *zan_row_expires_at(row);
    zan_row_unlock(row);
    return expires_at;
}

int64_t zan_shared_table_purge_expired(int64_t handle, int64_t now_ms) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    if (now_ms < 0) now_ms = zan_wall_now_ms();
    zan_struct_lock(table->header);
    uint64_t before = table->header->count;
    zan_purge_expired_locked(table->header, now_ms);
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
    zan_purge_expired_locked(table->header, now_ms);
    unsigned char *row = zan_find_row(table->header, key, 1);
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
    zan_purge_expired_locked(table->header, now_ms);
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms());
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        int64_t current_owner = 0;
        memcpy(&current_owner, row + owner_column->offset, sizeof(current_owner));
        if (current_owner == owner) {
            zan_expiry_remove(table->header, row);
            memset(row, 0, table->header->row_stride);
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms());
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_remove(table->header, row);
        memset(row, 0, table->header->row_stride);
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
    size_t rows_offset = zan_align8(sizeof(*table->header));
    for (uint64_t i = 0; i < table->header->capacity; i++) {
        unsigned char *row = zan_row_at(table->header, i);
        zan_row_lock(row);
        zan_row_unlock(row);
    }
    memset(
        (unsigned char *)table->header + rows_offset, 0,
        table->mapped_size - rows_offset);
    table->header->count = 0;
    table->header->expiry_count = 0;
    zan_struct_unlock(table->header);
}

/* ---- filesystem helpers for the compiler driver ---- */

/* Write the directory containing the running executable into `out`
 * (NUL-terminated, truncated at `cap`). Returns the length written. */
long long zan_exe_dir_into(char *out, long long cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
#ifdef _WIN32
    char buf[1024];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    while (n > 0 && buf[n - 1] != '\\') n--;
    if (n > 0) n--; /* drop the trailing separator */
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

/* List the file names matching `pattern` (a glob such as dir\*.zan) into
 * `out`, one name per line ('\n'-separated, NUL-terminated, truncated at
 * `cap`). Returns the length written; 0 when nothing matches. */
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

/* --- File metadata -------------------------------------------------------
 *
 * The standard library had whole-file text IO and nothing else, so nothing
 * could ask when a file last changed -- which is what incremental builds and
 * hot reload are made of. These report Unix seconds (0 when the file does not
 * exist) and the DOS/Unix attribute bits behind System.IO.FileInfo.
 */

/* 0 = last write, 1 = creation, 2 = last access. */
long long zan_file_time(const char *path, int which) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) return 0;
    FILETIME ft = d.ftLastWriteTime;
    if (which == 1) ft = d.ftCreationTime;
    else if (which == 2) ft = d.ftLastAccessTime;
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32)
                         | (unsigned long long)ft.dwLowDateTime;
    if (t == 0) return 0;
    /* 100 ns ticks since 1601 -> seconds since 1970 */
    return (long long)(t / 10000000ULL) - 11644473600LL;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (which == 1) return (long long)st.st_ctime;
    if (which == 2) return (long long)st.st_atime;
    return (long long)st.st_mtime;
#endif
}

/* Size in bytes, or -1 when the path does not exist. */
long long zan_file_length(const char *path) {
    if (!path || !path[0]) return -1;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) return -1;
    return (long long)(((unsigned long long)d.nFileSizeHigh << 32)
                       | (unsigned long long)d.nFileSizeLow);
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
#endif
}

/* Bit 0 read-only, bit 1 hidden, bit 2 directory; -1 when missing. */
long long zan_file_attributes(const char *path) {
    if (!path || !path[0]) return -1;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) return -1;
    long long r = 0;
    if (a & FILE_ATTRIBUTE_READONLY)  r |= 1;
    if (a & FILE_ATTRIBUTE_HIDDEN)    r |= 2;
    if (a & FILE_ATTRIBUTE_DIRECTORY) r |= 4;
    return r;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    long long r = 0;
    if (access(path, W_OK) != 0) r |= 1;
    { const char *base = strrchr(path, '/');
      base = base ? base + 1 : path;
      if (base[0] == '.') r |= 2; }
    if (S_ISDIR(st.st_mode)) r |= 4;
    return r;
#endif
}

#ifndef _WIN32
/* POSIX: resolve `path` to a descriptor once, then operate on the fd. This
 * closes the TOCTOU window between stat()-ing a path and chmod/utimens-ing
 * it (a swapped path now acts on the file actually opened), and O_NOFOLLOW
 * refuses to reach a target through a symlink (a symlink chain is ELOOP).
 * O_RDONLY opens files and directories on every POSIX; O_PATH (Linux) is the
 * fallback for read-protected targets where O_RDONLY would fail EACCES.
 * Returns the fd, or -1 with errno set on failure. */
static int zan_posix_open_nofollow(const char *path) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
#if defined(O_PATH)
    if (fd < 0 && errno == EACCES) {
        fd = open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    }
#endif
    return fd;
}
#endif

/* Marks the file read-only (`on`) or writable. Returns 1 on success. */
long long zan_file_set_readonly(const char *path, int on) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    if (on) a |= FILE_ATTRIBUTE_READONLY;
    else    a &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
    return SetFileAttributesA(path, a) ? 1 : 0;
#else
    int fd = zan_posix_open_nofollow(path);
    if (fd < 0) return 0;   /* missing, ELOOP symlink, EACCES, ... */
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return 0; }
    mode_t m = st.st_mode;
    if (on) m &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
    else    m |= S_IWUSR;
    int ok = fchmod(fd, m) == 0 ? 1 : 0;
    close(fd);
    return ok;
#endif
}

/* Sets one file timestamp to a Unix timestamp. `which` matches zan_file_time:
 * 0 = last write, 1 = creation (best-effort), 2 = last access. Returns 1 on
 * success. Creation time is only settable on Windows; elsewhere it is a no-op
 * that returns 0 so callers can degrade gracefully. */
long long zan_file_set_time(const char *path, int which, long long unix_sec) {
    if (!path || !path[0]) return 0;
    if (which != 0 && which != 1 && which != 2) return 0;
#ifdef _WIN32
    HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    /* Unix seconds since 1970 -> 100ns ticks since 1601. */
    unsigned long long ticks = (unsigned long long)(unix_sec + 11644473600LL)
                             * 10000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFULL);
    ft.dwHighDateTime = (DWORD)(ticks >> 32);
    int ok = 0;
    if (which == 0)      ok = SetFileTime(h, NULL, NULL, &ft);
    else if (which == 1) ok = SetFileTime(h, &ft, NULL, NULL);
    else                 ok = SetFileTime(h, NULL, &ft, NULL);
    CloseHandle(h);
    return ok ? 1 : 0;
#else
    if (which == 1) return 0;   /* creation time: not settable on POSIX */
    int fd = zan_posix_open_nofollow(path);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return 0; }
    struct timespec times[2];
    /* atime, mtime */
    times[0].tv_sec = (which == 2) ? unix_sec : st.st_atime;
    times[0].tv_nsec = 0;
    times[1].tv_sec = (which == 0) ? unix_sec : st.st_mtime;
    times[1].tv_nsec = 0;
    int ok = futimens(fd, times) == 0 ? 1 : 0;
    close(fd);
    return ok;
#endif
}

/* ---- file handles (System.IO.FileStream) ---------------------------------
 * FILE*-based stream IO with 64-bit offsets, exposed as an opaque 64-bit
 * handle so zan code never spells FILE* (its size and the width of fseek's
 * offset both vary by platform). Handles are unforgeable: a handle is
 * (gen << 32) | index into a fixed table of { FILE*, gen, open } slots, and
 * every operation first checks the index range, the generation and the open
 * flag under a mutex. A fabricated integer, a stale handle from a closed
 * slot, or a double close no longer reaches fread/fclose on a bogus FILE*.
 * A handle is 0 when the open failed; every other call treats 0 (and every
 * other invalid handle) as a no-op so a failed open cannot corrupt memory.
 * Table access is serialized; the captured FILE* is used outside the lock so
 * blocking I/O never holds it.
 */

#define ZAN_FH_CAP 1024
typedef struct {
    FILE *fp;
    uint32_t gen;   /* bumped on every close; stale handles stop matching */
    int open;
} zan_fh_slot;
static zan_fh_slot g_fh_table[ZAN_FH_CAP];
static int g_fh_ready = 0;

static void zan_fh_ensure(void) {
    if (g_fh_ready) return;
    int i = 0;
    while (i < ZAN_FH_CAP) {
        g_fh_table[i].fp = NULL;
        g_fh_table[i].gen = 1;   /* gen 0 would make slot 0's handle read 0 */
        g_fh_table[i].open = 0;
        i = i + 1;
    }
    g_fh_ready = 1;
}

#ifdef _WIN32
/* Same lazy-init discipline as the dispatch queue above: INIT_ONCE runs the
 * critical-section constructor exactly once, so two threads opening files for
 * the first time cannot race the table's first use. */
static CRITICAL_SECTION g_fh_cs;
static INIT_ONCE g_fh_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK zan_fh_cs_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&g_fh_cs);
    return TRUE;
}

static void zan_fh_lock(void) {
    InitOnceExecuteOnce(&g_fh_once, zan_fh_cs_init, NULL, NULL);
    EnterCriticalSection(&g_fh_cs);
}
static void zan_fh_unlock(void) { LeaveCriticalSection(&g_fh_cs); }
#else
static pthread_mutex_t g_fh_mx = PTHREAD_MUTEX_INITIALIZER;
static void zan_fh_lock(void) { pthread_mutex_lock(&g_fh_mx); }
static void zan_fh_unlock(void) { pthread_mutex_unlock(&g_fh_mx); }
#endif

/* Resolve `handle` to the table slot of a live open file, or -1 when the
 * handle is forged: index out of range, wrong generation, or a closed slot.
 * Call under the table lock. */
static long zan_fh_index(long long handle) {
    unsigned long long h = (unsigned long long)handle;
    uint32_t idx = (uint32_t)(h & 0xFFFFFFFFULL);
    uint32_t gen = (uint32_t)(h >> 32);
    if (idx >= ZAN_FH_CAP) return -1;
    zan_fh_slot *s = &g_fh_table[idx];
    if (!s->open || s->fp == NULL || s->gen != gen) return -1;
    return (long)idx;
}

/* `mode` is a stdio mode string ("rb", "wb", "r+b", "ab", ...). */
long long zan_file_open(const char *path, const char *mode) {
    if (!path || !path[0] || !mode || !mode[0]) return 0;
    FILE *f = fopen(path, mode);
    if (!f) return 0;
    zan_fh_lock();
    zan_fh_ensure();
    long long handle = 0;
    int i = 0;
    while (i < ZAN_FH_CAP) {
        zan_fh_slot *s = &g_fh_table[i];
        if (!s->open) {
            if (s->gen == 0) { s->gen = 1; }   /* keep slot 0's handle nonzero */
            s->fp = f;
            s->open = 1;
            /* build in unsigned to avoid signed-shift UB when gen's high bit
             * is set (a long long is bit-preserving on the Zan side) */
            handle = (long long)(((unsigned long long)s->gen << 32)
                                 | (unsigned long long)(uint32_t)i);
            break;
        }
        i = i + 1;
    }
    zan_fh_unlock();
    if (handle == 0) { fclose(f); return 0; }  /* table full: fail gracefully */
    return handle;
}

long long zan_file_read(long long handle, long long buf, long long count) {
    if (!buf || count <= 0) return 0;
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return 0;
    return (long long)fread((void *)(intptr_t)buf, 1, (size_t)count, f);
}

long long zan_file_write(long long handle, long long buf, long long count) {
    if (!buf || count <= 0) return 0;
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return 0;
    return (long long)fwrite((const void *)(intptr_t)buf, 1, (size_t)count, f);
}

/* `origin`: 0 = begin, 1 = current, 2 = end. Returns the new absolute
 * position, or -1 on failure. */
long long zan_file_seek(long long handle, long long offset, int origin) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return -1;
    int whence = origin == 1 ? SEEK_CUR : (origin == 2 ? SEEK_END : SEEK_SET);
#ifdef _WIN32
    if (_fseeki64(f, (__int64)offset, whence) != 0) return -1;
    return (long long)_ftelli64(f);
#else
    if (fseeko(f, (off_t)offset, whence) != 0) return -1;
    return (long long)ftello(f);
#endif
}

long long zan_file_tell(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return -1;
#ifdef _WIN32
    return (long long)_ftelli64(f);
#else
    return (long long)ftello(f);
#endif
}

long long zan_file_flush(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return -1;
    return fflush(f) == 0 ? 1 : 0;
}

long long zan_file_close(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = NULL;
    if (idx >= 0) {
        zan_fh_slot *s = &g_fh_table[idx];
        s->open = 0;
        f = s->fp;
        s->fp = NULL;
        s->gen = s->gen + 1;   /* invalidate every outstanding copy of the handle */
        if (s->gen == 0) { s->gen = 1; }
    }
    zan_fh_unlock();
    if (!f) return 0;   /* unknown or already-closed handle */
    return fclose(f) == 0 ? 1 : 0;
}

/* 1 once a read hit end-of-file on this handle. */
long long zan_file_eof(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return 1;
    return feof(f) ? 1 : 0;
}

/* ---- memory-mapped files (System.IO.MemoryMappedFile) -------------------
 * A handle is an opaque 64-bit value: on Windows a HANDLE to a file mapping
 * object, on POSIX a heap pointer to a small struct holding the shm/fd pair.
 * 0 means failure. `map` returns the view address (0 on failure); `unmap`
 * must be called with the same size that was mapped.
 */
#ifdef _WIN32
typedef struct {
    HANDLE h;
} zan_mmap_handle;
#else
typedef struct {
    int fd;
    int owner;         /* 1 = this handle created the region: only its close
                        * (or an explicit Unlink) may shm_unlink the name */
    char name[64];     /* shm name (kept for shm_unlink), or "" for file-backed */
} zan_mmap_handle;
#endif

/* Creates a NEW named shared-memory region of `size` bytes. Fails (returns 0)
 * when a region with the same name already exists. `name` may include a
 * leading "Global\\" or "Local\\" prefix (Windows). */
long long zan_mmap_create(const char *name, long long size) {
    if (!name || !name[0] || size <= 0) return 0;
#ifdef _WIN32
    DWORD hi = (DWORD)(((unsigned long long)size) >> 32);
    DWORD lo = (DWORD)((unsigned long long)size & 0xFFFFFFFFULL);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    wchar_t *wname = (wchar_t *)calloc((size_t)wlen, sizeof(wchar_t));
    if (!wname) return 0;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, wlen);
    HANDLE named = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, hi, lo, wname);
    free(wname);
    if (!named) return 0;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(named);
        return 0;
    }
    return (long long)(intptr_t)named;
#else
    char shm_name[96];
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

/* Opens an EXISTING named shared-memory region. Returns 0 when it does not
 * exist or `size` is nonzero and does not match the region. */
long long zan_mmap_open(const char *name, long long size) {
    if (!name || !name[0]) return 0;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    wchar_t *wname = (wchar_t *)calloc((size_t)wlen, sizeof(wchar_t));
    if (!wname) return 0;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, wlen);
    HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname);
    free(wname);
    if (!h) return 0;
    return (long long)(intptr_t)h;
#else
    char shm_name[96];
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

/* Creates a mapping over an existing FILE. `size` <= 0 maps the whole file.
 * The view is read-write; writes are visible to other mappers of the same
 * file (MAP_SHARED). */
long long zan_mmap_from_file(const char *path, long long size) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
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
    if (size > 0) {
        if (ftruncate(fd, (off_t)size) != 0) { close(fd); return 0; }
    }
    zan_mmap_handle *h = (zan_mmap_handle *)calloc(1, sizeof(zan_mmap_handle));
    if (!h) { close(fd); return 0; }
    h->fd = fd;
    h->name[0] = '\0';
    return (long long)(intptr_t)h;
#endif
}

/* Maps `size` bytes of the region into the address space. Returns the view
 * address, or 0 on failure. */
long long zan_mmap_map(long long handle, long long size) {
    if (!handle || size <= 0) return 0;
#ifdef _WIN32
    HANDLE h = (HANDLE)(intptr_t)handle;
    DWORD hi = (DWORD)(((unsigned long long)size) >> 32);
    DWORD lo = (DWORD)((unsigned long long)size & 0xFFFFFFFFULL);
    void *p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    return p ? (long long)(intptr_t)p : 0;
#else
    zan_mmap_handle *h = (zan_mmap_handle *)(intptr_t)handle;
    void *p = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   h->fd, 0);
    if (p == MAP_FAILED) return 0;
    return (long long)(intptr_t)p;
#endif
}

/* Unmaps a view returned by zan_mmap_map. `size` must match the mapped size. */
long long zan_mmap_unmap(long long ptr, long long size) {
    if (!ptr) return 0;
#ifdef _WIN32
    return UnmapViewOfFile((void *)(intptr_t)ptr) ? 1 : 0;
#else
    return munmap((void *)(intptr_t)ptr, (size_t)size) == 0 ? 1 : 0;
#endif
}

/* Flushes a mapped view to disk (no-op for shm on Windows). */
long long zan_mmap_flush(long long ptr, long long size) {
    if (!ptr) return 1;
#ifdef _WIN32
    return FlushViewOfFile((void *)(intptr_t)ptr, (size_t)size) ? 1 : 0;
#else
    return msync((void *)(intptr_t)ptr, (size_t)size, MS_SYNC) == 0 ? 1 : 0;
#endif
}

/* Closes a mapping handle. Only the handle that CREATED a named POSIX
 * region unlinks it on close; a plain opener's close leaves the name in
 * place so the creator and other clients keep working. */
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

/* Explicitly removes a named POSIX region, regardless of open handles.
 * On Windows named regions are reference-counted kernel objects -- the name
 * disappears with the last handle -- so there is nothing to unlink and this
 * is a harmless no-op returning success. */
long long zan_mmap_unlink(const char *name) {
    if (!name || !name[0]) return 0;
#ifdef _WIN32
    (void)name;
    return 1;
#else
    char shm_name[96];
    snprintf(shm_name, sizeof(shm_name), "/%s", name);
    return shm_unlink(shm_name) == 0 ? 1 : 0;
#endif
}

