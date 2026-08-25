#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
/* getifaddrs/IFF_* and the BSD socket extras used by the zan_plat_* platform
 * services live outside strict POSIX; glibc gates them on _DEFAULT_SOURCE. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
/* glibc declares gettid() only under _GNU_SOURCE; without it the call below
 * is an implicit declaration, which clang 16+ rejects outright. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "rt_sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../common/host_oom.h"
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
/* Version 3: lock words gained the held-bit + holder-pid encoding below.
 * Version 2 tables store bare 0/1 words that a version-3 reader would decode
 * as "held by pid 1" and never reclaim, so old mappings are rejected. */
#define ZAN_TABLE_VERSION 3
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
    /* Anonymous tables have no name at all: they are reached through an
     * inherited descriptor, so map_name stays empty and there is
     * nothing for destroy to unlink. */
    int anonymous;
    int attached;   /* mapped a handle a parent created, does not own the table */
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

static void zan_make_names(const char *name, char map_name[96]) {
    unsigned long long hash = (unsigned long long)zan_hash_bytes(name);
#ifdef _WIN32
    snprintf(map_name, 96, "Local\\zan_table_%016llx", hash);
#else
    snprintf(
        map_name, 96, "/tmp/zan_table_%lu_%016llx.shm",
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
/* ---- cross-process lock words -------------------------------------------
 * A lock word is 0 when free; while held, bit 31 is set and bits 30:0 carry
 * the holding process's id. A plain 0/1 word cannot survive a crashed
 * holder -- every other process would spin on it forever. With the id in
 * the word, a waiter that has exhausted its backoff ladder tests whether
 * the holder still exists (OpenProcess / kill(pid,0)) and reclaims the word
 * with a CAS when it does not, so a killed writer cannot wedge the table
 * for every other process attached to it.
 *
 * PID truncation can alias a live process; the probe then reports alive and
 * the waiter keeps spinning. Reclamation is a heuristic safety net, never
 * the primary mutual-exclusion mechanism: a live holder's word is only ever
 * cleared by the holder's own release-store of 0.
 *
 * KNOWN LIMITATION (documented, not yet fixed): the word carries only
 * bit31|pid, so a RECYCLED pid of a dead holder reads as alive and reclaim
 * never fires -- a waiter can spin on an orphaned lock until the holder
 * slot is otherwise released. Sharpest case: the waiter's own pid was
 * recycled from the dead holder. Mutual exclusion itself is never violated
 * (reclaim is gated on the liveness probe); fixing this properly means
 * mixing a per-process creation-time nonce into bits 30:0 across both
 * platforms and every probe site. */
#define ZAN_LOCK_HELD_BIT 0x80000000u
#define ZAN_LOCK_PID_MASK 0x7FFFFFFFu

static int zan_pid_alive(uint32_t pid) {
    if (!pid) return 0;
#ifdef _WIN32
    /* SYNCHRONIZE is required for WaitForSingleObject to work at all: a
     * QUERY_LIMITED_INFORMATION-only handle fails every wait with
     * WAIT_FAILED (verified empirically), which would report every live
     * holder as dead and let a waiter steal a held lock. Any wait result
     * other than WAIT_TIMEOUT keeps the holder conservatively alive --
     * reclaim only ever fires when OpenProcess itself says the pid is
     * gone. */
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
            if ((held & (LONG)ZAN_LOCK_HELD_BIT) &&
                !zan_pid_alive((uint32_t)held & ZAN_LOCK_PID_MASK)) {
                InterlockedCompareExchange((volatile LONG *)word, 0, held);
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
            if ((held & ZAN_LOCK_HELD_BIT) &&
                !zan_pid_alive(held & ZAN_LOCK_PID_MASK)) {
                __atomic_compare_exchange_n(word, &held, 0, 0,
                                            __ATOMIC_RELEASE,
                                            __ATOMIC_RELAXED);
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

/* Re-validate a resolved row after acquiring its lock. Between the lock-free
 * lookup and the row lock another process can tombstone the slot (delete or
 * expiry) and a third can claim it for a DIFFERENT key -- without this check
 * a suspended writer commits its value into the new owner's row. Key-based
 * callers pass the key and compare bytes exactly as zan_find_row does;
 * hash-keyed (_at) callers pass key=NULL and rely on the 64-bit hash. A 0
 * return means "the row I resolved no longer exists": callers fail the
 * operation (0 / empty / -1), which is what racing a concurrent delete means. */
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

/* Opportunistic expiry sweep tuning: the hot path calls this before every
 * table op. The interval skips even the clock read when nothing can have
 * expired recently; the batch cap bounds how long ONE unlucky caller holds
 * the structural lock when many rows share a deadline -- the rest drain on
 * later ops instead of stalling every other process behind one giant
 * critical section. */
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
        /* Wipe the payload but keep word 4 -- it IS this thread's lock:
         * zeroing it lets another process acquire the row mid-purge and
         * write into memory being erased. State is republished last so a
         * lock-free lookup never sees content whose state disagrees. */
        memset(row + 8, 0, header->row_stride - 8);
        zan_publish_state(row, ZAN_SLOT_TOMBSTONE);
        header->count--;
        zan_row_unlock(row);
        batch++;
    }
}

static void zan_purge_expired(zan_shared_header *header) {
    /* Relaxed loads: this peek races writers that hold the struct lock. On
     * LP64 the aligned 64-bit loads are single-copy atomic and every
     * consequence is re-validated under the struct lock below, so a torn or
     * stale peek can only cause a wasted attempt -- but read them as atomics
     * anyway and range-check the index before it becomes a row pointer. */
    if (__atomic_load_n(&header->expiry_count, __ATOMIC_RELAXED) == 0)
        return;
    /* Process-wide throttle: once any TTL exists, every op would otherwise
     * pay a wall-clock syscall plus a heap-top compare. A plain static with
     * relaxed atomics is enough -- worst case two threads both run the peek,
     * which is exactly what happened before the throttle existed. */
    static int64_t last_attempt_ms;
    int64_t now_ms = zan_wall_now_ms();
    int64_t last = __atomic_load_n(&last_attempt_ms, __ATOMIC_RELAXED);
    if (last > now_ms - ZAN_PURGE_MIN_INTERVAL_MS && last <= now_ms) return;
    __atomic_store_n(&last_attempt_ms, now_ms, __ATOMIC_RELAXED);
    uint64_t *heap = zan_expiry_heap(header);
    uint64_t top = __atomic_load_n(&heap[0], __ATOMIC_RELAXED);
    if (top >= header->capacity) return; /* torn peek: let the real pass run */
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

    /* Refuse inserts past a 7/8 load factor. Linear probing degrades towards
     * O(capacity) per op as the table fills, and the last percent is where
     * every request lands once writers pile up; failing the insert early
     * (the caller sees 0 / empty, same as a full table) keeps the probe
     * chains short for the readers that share the mapping. */
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
        /* No payload wipe here: EMPTY and TOMBSTONE rows are already
         * payload-zero -- fresh mappings hand out zero pages, and clear()
         * plus every tombstone producer wipe row+8..stride before they
         * publish. Writing only identity keeps this O(key) instead of
         * O(stride) under the structural lock (a stride can reach ~64KB),
         * and it never touches word 4, which a purger may transiently still
         * hold when its TOMBSTONE becomes visible. */
        *zan_row_hash(row) = hash;
        memcpy(zan_row_key(row), key, key_len);
        if ((size_t)key_len < header->key_size)
            zan_row_key(row)[key_len] = '\0';
        /* Key and hash are published before the slot becomes visible as USED,
         * so a concurrent lock-free lookup never matches a half-written row. */
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }

    if (create && first_tombstone != UINT64_MAX) {
        unsigned char *row = zan_row_at(header, first_tombstone);
        /* Same payload-zero reasoning as the claim above. */
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
    /* Same 7/8 load-factor refusal as zan_find_row. */
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
        /* No payload wipe: EMPTY and TOMBSTONE rows are already payload-zero
         * (see zan_find_row); write identity only. */
        *zan_row_hash(row) = hash;
        zan_publish_state(row, ZAN_SLOT_USED);
        header->count++;
        return row;
    }
    if (create && first_tombstone != UINT64_MAX) {
        unsigned char *row = zan_row_at(header, first_tombstone);
        /* Same payload-zero reasoning as the claim above. */
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
 * thread, which is exactly the confusion Thread.CurrentId() must avoid.
 * Nested #ifs keep __GLIBC_PREREQ out of any #if expression on musl (which
 * sets __GLIBC__ for compatibility but never defines the glibc-only feature
 * macro; zig cc escalates the resulting -Wundef to an error). */
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
 * A fixed-capacity ring of Zan delegate values. Background threads enqueue
 * with zan_dispatch_post(); the UI thread drains with zan_dispatch_take()
 * once per frame and invokes each on the UI thread. The queue itself is
 * guarded by an OS mutex.
 *
 * A queued entry is an owning reference: a capturing lambda is a heap closure
 * record (see ZAN_CLOSURE_* in zan_abi.h), and while it sits in the ring the
 * only thing keeping it alive is this queue. Post retains it, take hands that
 * count to the caller (emitted code treats a call result as owned and releases
 * it after invoking), and clear releases what it drops. Without the retain the
 * drain's release dropped a count the queue never held, so a handler an event
 * still owned -- `btn.Click += () => { ... }` -- was freed the first time it
 * was posted, and the next use or rebuild of that handler list read freed
 * memory. Posting still allocates nothing, so a background thread only touches
 * the record's refcount, never the Zan allocator. */

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

/* Is this delegate value a heap closure record (rather than a bare function
 * pointer, which owns nothing and needs no counting)? */
static void *zan_delegate_record(void *d) {
    uintptr_t v = (uintptr_t)d;
    if (!(v & (uintptr_t)ZAN_CLOSURE_TAG)) return NULL;
    return (void *)(v & ~(uintptr_t)ZAN_CLOSURE_TAG);
}

/* Take a reference to a queued delegate. Safe from any thread: the refcount is
 * the first header word and is updated atomically, exactly as emitted code
 * does it (zan_rt_retain). */
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

/* Drop a reference the queue held. Goes through the record's own destructor,
 * which releases the captured values and frees the record at zero -- so this
 * touches the Zan heap and belongs on the UI thread (its only caller,
 * zan_dispatch_clear, is documented as UI-thread only). */
static void zan_delegate_release(void *d) {
    void *rec = zan_delegate_record(d);
    if (!rec) return;
    void *dtor = *(void **)((char *)rec + ZAN_CLOSURE_DTOR_OFF);
    if (dtor) ((void (*)(void *))dtor)(rec);
}

/* Enqueue a delegate to run on the UI thread. Thread-safe. Returns 1 on
 * success, 0 if the delegate was null or the queue was full. */
int32_t zan_dispatch_post(void *fn) {
    if (!fn) return 0;
    int32_t ok = 0;
    zan_dispatch_lock();
    int next = (g_dispatch_tail + 1) % ZAN_DISPATCH_CAP;
    if (next != g_dispatch_head) {
        zan_delegate_retain(fn);
        g_dispatch_ring[g_dispatch_tail] = fn;
        g_dispatch_tail = next;
        ok = 1;
    }
    zan_dispatch_unlock();
    return ok;
}

/* Pop the next queued delegate (UI thread), or NULL when the queue is empty.
 * The queue's reference travels with the value: the caller invokes it and then
 * releases it, which is what emitted code does with any call result. */
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
    void *drop[ZAN_DISPATCH_CAP];
    int n = 0;
    zan_dispatch_lock();
    while (g_dispatch_head != g_dispatch_tail) {
        drop[n++] = g_dispatch_ring[g_dispatch_head];
        g_dispatch_head = (g_dispatch_head + 1) % ZAN_DISPATCH_CAP;
    }
    g_dispatch_head = 0;
    g_dispatch_tail = 0;
    zan_dispatch_unlock();
    /* Released outside the lock: a record's destructor runs Zan code, which
     * must not re-enter the queue while it is held. */
    for (int i = 0; i < n; i++) zan_delegate_release(drop[i]);
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

/* The geometry a schema implies: how big the mapping has to be and what the
 * header must say about it. Shared by the named and the anonymous constructor,
 * which differ only in where the memory comes from. */
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

/* Re-derive the geometry a header claims from its own fields. open/attach
 * validate magic/version/ready but used to trust capacity, row_stride,
 * key_size and column_count blindly afterwards -- and every operation
 * derives addresses from them. One stray write to a shared header (buggy
 * writer, torn multi-word update, hostile co-process mapping the same name)
 * then sent zan_row_at or zan_find_column far outside the mapping. */
static int zan_header_geometry_ok(const zan_shared_header *header) {
    if (header->capacity == 0 ||
        header->capacity > ZAN_TABLE_MAX_CAPACITY ||
        (header->capacity & (header->capacity - 1)) != 0)
        return 0;   /* power of two, probed with & mask everywhere */
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
        /* Column lookup does strcmp(column->name, ...); require a terminator
         * inside the field so a hostile header cannot walk the compare off
         * the end of the mapping. */
        if (!memchr(c->name, '\0', sizeof(c->name)))
            return 0;
    }
    uint64_t expected =
        (uint64_t)rows_offset +
        header->capacity * (uint64_t)header->row_stride +
        header->capacity * sizeof(uint64_t);
    return expected == header->total_size;
}

/* Stamps the header of a freshly mapped table and publishes it. */
static void zan_table_init_header(
    zan_shared_table *table, const zan_table_layout *layout) {
    table->mapped_size = layout->total_size;
    /* Only the header is zeroed. The rows and the index behind it are already
     * zero -- the mapping is freshly created, page-file backed (Windows) or a
     * newly created, ftruncate'd file (POSIX), and both hand out zero pages --
     * and writing them here would fault in the whole mapping, making a table's
     * reserved size its resident size in every process that maps it. */
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

/* ---- anonymous tables: no name, reached through an inherited handle ----
 *
 * A named table is addressable by anything running on the machine, which makes
 * it two problems at once: an unrelated process can read and write a server's
 * table, and on POSIX the name is a file that outlives a killed process, so the
 * next run finds the previous run's rows. Anonymous tables have neither: the
 * memory is owned by the process that created it and handed to its children as
 * an inheritable descriptor (POSIX fd / Windows HANDLE), the way Swoole's
 * fork-inherited tables are reachable only inside the server. The kernel frees
 * it when the last of those processes goes, whether they exited or were killed.
 *
 * The creator passes zan_shared_table_handle() to a child (an environment
 * variable), and the child attaches to it with zan_shared_table_attach(). */
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
    /* Inheritable, so CreateProcess with bInheritHandles hands the child the
     * same handle value; NULL name keeps it out of the object namespace. */
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
    /* shm_open + immediate shm_unlink: the mapping keeps living through the fd
     * while its name is gone the moment it exists, so nothing can open it by
     * name and nothing is left in /dev/shm if this process is killed. The name
     * only has to survive between the two calls, hence pid + a counter.
     *
     * memfd_create would do the same in one call, but it is Linux-only and this
     * runtime also builds for macOS. */
    static uint32_t anon_seq = 0;
    char shm_name[64];
    snprintf(shm_name, sizeof(shm_name), "/zan_table_%ld_%u_%u",
             (long)getpid(), (unsigned)time(NULL), anon_seq++);
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        zan_shared_table_free(table);
        return 0;
    }
    shm_unlink(shm_name);
    /* shm_open sets FD_CLOEXEC, which a worker started by exec would lose the
     * table to; the descriptor has to survive into the child. */
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

/* The value a child needs to attach: a file descriptor on POSIX, a handle on
 * Windows. 0 for a named table, which is reached by name instead. */
int64_t zan_shared_table_handle(int64_t handle) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !table->anonymous) return 0;
#ifdef _WIN32
    return (int64_t)(intptr_t)table->mapping;
#else
    return (int64_t)table->fd;
#endif
}

/* Child side: map the table its parent created, given the inherited handle.
 * Everything the mapping needs to be read is inside it, so no schema is
 * passed -- and a handle that is not a table of this version is rejected. */
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
    /* Anchor the extent in what the OS actually mapped, not in the header
     * (which a hostile co-process controls): a squatting process can publish
     * a self-consistent header for a section far smaller than total_size.
     * The validation chain below then compares against the real extent. */
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
    /* Unlike open() -- which demands exact equality -- attach tolerates a
     * file LARGER than the header claims: named table files can be extended
     * by anyone with write access, and geometry identity plus the later
     * total_size <= mapped_size check already bound every access, so the
     * extra bytes are simply never touched. */
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
    /* Same OS-anchored extent as attach: the header is untrusted input. */
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
    /* File-backed POSIX mappings must match the header's claim exactly; a
     * Windows section may be larger than total_size (allocation granularity)
     * and that is harmless -- only exceeding the mapped extent is, and the
     * chain above rejects it. */
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
    /* An anonymous table has no name in the filesystem to remove: dropping the
     * last reference IS its destruction. */
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

/* How much of the mapping this process is actually paying for. A table reserves
 * its whole capacity up front, but a page only becomes resident when a row on
 * it is touched, so the reserved size says nothing about memory in use. */
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
    enum { ZAN_MINCORE_BATCH = 16384 };  /* pages per call: 16 KB of vector */
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
    /* O(len), not O(column): bytes past the terminator are never observable
     * (readers use a size-bounded strnlen / strncmp), and the old full-column
     * wipe turned every short update into a up-to-64KB critical section under
     * this row's spinlock. value_len < column->size is checked by the caller. */
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
    /* O(len), not O(column): bytes past the terminator are never observable
     * (readers use a size-bounded strnlen / strncmp), and the old full-column
     * wipe turned every short update into a up-to-64KB critical section under
     * this row's spinlock. value_len < column->size is checked by the caller. */
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), UINT64_MAX);
    unsigned char *row = zan_find_row_hash(
        table->header, (uint64_t)key_hash, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_remove(table->header, row);
        /* Keep the lock word we hold (offset 4): see zan_purge_expired_locked */
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), UINT64_MAX);
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
    zan_purge_expired_locked(table->header, now_ms, UINT64_MAX);
    /* Existence-first, like zan_lock_row_for: the 7/8 load-factor refusal in
     * zan_find_row(create=1) fires before the probe loop, so a direct create
     * call would deny requests for LONG-EXISTING keys once the table nears
     * capacity -- an invisible fail-closed outage for a rate limiter. */
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), UINT64_MAX);
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        int64_t current_owner = 0;
        memcpy(&current_owner, row + owner_column->offset, sizeof(current_owner));
        if (current_owner == owner) {
            zan_expiry_remove(table->header, row);
            /* Keep the lock word we hold (offset 4): see
             * zan_purge_expired_locked. */
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
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), UINT64_MAX);
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_remove(table->header, row);
        /* Keep the lock word we hold (offset 4): see zan_purge_expired_locked */
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
    /* Clear row by row under each row's own lock. The previous version took
     * and immediately released every row lock and then wiped the whole row
     * area under only the structural lock -- but value writers never take
     * the structural lock, so a set that acquired its row lock after the
     * walk returned success and then had its value erased by the memset:
     * an acknowledged write silently lost. Holding each row's lock across
     * its own wipe makes clear serialize against writers per row. The
     * structural lock is still taken so slot claims and expiry mutations
     * cannot interleave with the sweep. */
    zan_struct_lock(table->header);
    for (uint64_t i = 0; i < table->header->capacity; i++) {
        unsigned char *row = zan_row_at(table->header, i);
        uint32_t state = zan_load_state(row);
        if (state == ZAN_SLOT_EMPTY) continue;   /* untouched page: skip */
        zan_row_lock(row);
        if (zan_load_state(row) != ZAN_SLOT_EMPTY) {
            /* Keep the lock word we hold (offset 4); see
             * zan_purge_expired_locked for why word 4 must survive. A
             * tombstone row is already all-zero by invariant, so only USED
             * rows need the payload wipe. */
            if (zan_load_state(row) == ZAN_SLOT_USED)
                memset(row + 8, 0, table->header->row_stride - 8);
            zan_publish_state(row, ZAN_SLOT_EMPTY);
        }
        zan_row_unlock(row);
    }
    /* The expiry heap sits behind the row area; every mutation of it holds
     * the structural lock, which we hold, so a wholesale reset is safe. */
    memset(zan_expiry_heap(table->header), 0,
           (size_t)table->header->capacity * sizeof(uint64_t));
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


/* ========================================================================
 * POSIX platform services (zan_plat_*)
 *
 * Native backends for the stdlib classes that used to be Windows-only:
 * adapter enumeration (System.Net.NetworkInterface) and ICMP echo
 * (System.Net.Ping). Windows keeps its own iphlpapi path in Zan, so these
 * are POSIX-only; the Windows builds compile to explicit failures rather
 * than silent success.
 * ======================================================================== */

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

#define ZAN_PLAT_TEXT_MAX 65536
static _Thread_local char zan_plat_text[ZAN_PLAT_TEXT_MAX];

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

/* '\n'-separated adapter snapshot, one line per interface:
 *   name '\t' index '\t' up(0|1) '\t' mac '\t' addr[,addr...]
 * getifaddrs reports one node per address, so addresses are folded into the
 * line of the interface that owns them (matching GetAdaptersAddresses, which
 * hands out one adapter record with a unicast list). */
const char *zan_plat_net_interfaces(void) {
    zan_plat_text[0] = '\0';
#ifdef _WIN32
    return zan_plat_text;
#else
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return zan_plat_text;

    size_t used = 0;
    for (struct ifaddrs *it = list; it; it = it->ifa_next) {
        if (!it->ifa_name) continue;
        /* One line per name: skip a name already emitted. */
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

/* Sends one ICMP echo request to the IPv4 literal `address` and waits up to
 * `timeout_ms` for the reply. Returns the round-trip time in milliseconds
 * (>= 0), or a negative status: -1 timed out, -2 destination unreachable,
 * -3 socket/permission error, -4 malformed address.
 *
 * SOCK_DGRAM/IPPROTO_ICMP ("ping sockets") needs no privileges when
 * net.ipv4.ping_group_range covers the caller's gid, and the kernel rewrites
 * the echo id for us; SOCK_RAW is the fallback for older kernels and for
 * macOS, and needs root or CAP_NET_RAW. */
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

    /* 8-byte ICMP header + "zan-ping" payload, matching the Windows path. */
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
        /* A raw socket hands back the IPv4 header; a ping socket does not. */
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
        if (type == 3) {                            /* destination unreachable */
            close(fd);
            return -2;
        }
        if (type == 11) {                           /* TTL expired in transit */
            close(fd);
            return -2;
        }
        /* Anything else (e.g. our own echo request looped back on a raw
         * socket) is not an answer: keep waiting until the deadline. */
    }
#endif
}
