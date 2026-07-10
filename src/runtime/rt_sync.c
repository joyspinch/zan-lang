#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "rt_sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define ZAN_TABLE_MAGIC UINT64_C(0x5a414e54424c3031)
#define ZAN_TABLE_VERSION 1
#define ZAN_TABLE_MAX_COLUMNS 16
#define ZAN_TABLE_COLUMN_NAME 32
#define ZAN_TABLE_MAX_KEY 256
#define ZAN_TABLE_MAX_STRING 4096
#define ZAN_TABLE_MAX_CAPACITY (UINT64_C(1) << 20)
#define ZAN_TABLE_INT 1
#define ZAN_TABLE_STRING 2
#define ZAN_TABLE_FLOAT 3
#define ZAN_SLOT_EMPTY 0
#define ZAN_SLOT_USED 1
#define ZAN_SLOT_TOMBSTONE 2
#define ZAN_SLOT_PREFIX 16

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
    uint32_t key_size;
    uint32_t row_stride;
    uint32_t column_count;
    uint32_t reserved;
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
    sem_t *semaphore;
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
    snprintf(map_name, 96, "/zan_table_%016llx", hash);
    snprintf(lock_name, 96, "/zan_l_%016llx", hash);
#endif
}

static int zan_table_lock(zan_shared_table *table) {
#ifdef _WIN32
    DWORD result = WaitForSingleObject(table->mutex, INFINITE);
    return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
#else
    int result;
    do {
        result = sem_wait(table->semaphore);
    } while (result != 0 && errno == EINTR);
    return result == 0;
#endif
}

static void zan_table_unlock(zan_shared_table *table) {
#ifdef _WIN32
    ReleaseMutex(table->mutex);
#else
    sem_post(table->semaphore);
#endif
}

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

static uint32_t *zan_row_state(unsigned char *row) {
    return (uint32_t *)row;
}

static uint32_t *zan_row_lock_word(unsigned char *row) {
    return (uint32_t *)(row + 4);
}

static uint64_t *zan_row_hash(unsigned char *row) {
    return (uint64_t *)(row + 8);
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

static unsigned char *zan_find_row(
    zan_shared_header *header, const char *key, int create) {
    size_t key_len = zan_strnlen(key, header->key_size + 1u);
    if (!key || key_len == 0 || key_len > header->key_size) return NULL;

    uint64_t hash = zan_hash_bytes(key);
    uint64_t first_tombstone = UINT64_MAX;
    for (uint64_t probe = 0; probe < header->capacity; probe++) {
        uint64_t index = (hash + probe) & (header->capacity - 1);
        unsigned char *row = zan_row_at(header, index);
        uint32_t state = *zan_row_state(row);
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
        *zan_row_hash(row) = hash;
        memcpy(zan_row_key(row), key, key_len);
        *zan_row_state(row) = ZAN_SLOT_USED;
        header->count++;
        return row;
    }

    if (create && first_tombstone != UINT64_MAX) {
        unsigned char *row = zan_row_at(header, first_tombstone);
        memset(row, 0, header->row_stride);
        *zan_row_hash(row) = hash;
        memcpy(zan_row_key(row), key, key_len);
        *zan_row_state(row) = ZAN_SLOT_USED;
        header->count++;
        return row;
    }
    return NULL;
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
    if (table->semaphore && table->semaphore != SEM_FAILED) {
        sem_close(table->semaphore);
    }
    if (table->fd >= 0) close(table->fd);
#endif
    free(table);
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
    const char *name, int64_t capacity_value, int64_t key_size_value,
    const char *schema) {
    if (!name || !*name || capacity_value <= 0 ||
        capacity_value > (int64_t)ZAN_TABLE_MAX_CAPACITY ||
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
    size_t total_size = rows_offset + (size_t)capacity * row_stride;

    zan_shared_table *table = (zan_shared_table *)calloc(1, sizeof(*table));
    if (!table) return 0;
#ifndef _WIN32
    table->fd = -1;
    table->semaphore = SEM_FAILED;
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
    table->fd = shm_open(table->map_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (table->fd < 0) {
        zan_shared_table_free(table);
        return 0;
    }
    if (ftruncate(table->fd, (off_t)total_size) != 0) {
        shm_unlink(table->map_name);
        zan_shared_table_free(table);
        return 0;
    }
    table->header = (zan_shared_header *)mmap(
        NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, table->fd, 0);
    if (table->header == MAP_FAILED) {
        table->header = NULL;
        shm_unlink(table->map_name);
        zan_shared_table_free(table);
        return 0;
    }
    table->semaphore = sem_open(
        table->lock_name, O_CREAT | O_EXCL, 0600, 1);
    if (table->semaphore == SEM_FAILED && errno == EEXIST) {
        sem_unlink(table->lock_name);
        table->semaphore = sem_open(
            table->lock_name, O_CREAT | O_EXCL, 0600, 1);
    }
    if (table->semaphore == SEM_FAILED) {
        shm_unlink(table->map_name);
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
    table->semaphore = SEM_FAILED;
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
    table->fd = shm_open(table->map_name, O_RDWR, 0600);
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
    table->semaphore = sem_open(table->lock_name, 0);
    if (table->semaphore == SEM_FAILED) {
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

int64_t zan_shared_table_destroy(int64_t handle) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t result = 1;
#ifndef _WIN32
    if (shm_unlink(table->map_name) != 0 && errno != ENOENT) result = 0;
    if (sem_unlink(table->lock_name) != 0 && errno != ENOENT) result = 0;
#endif
    zan_shared_table_free(table);
    return result;
}

int64_t zan_shared_table_set_int(
    int64_t handle, const char *key, const char *column_name, int64_t value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !zan_table_lock(table)) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_find_row(table->header, key, 1) : NULL;
    if (row) zan_row_lock(row);
    zan_table_unlock(table);
    if (row) memcpy(row + column->offset, &value, sizeof(value));
    if (row) zan_row_unlock(row);
    return row != NULL;
}

int64_t zan_shared_table_get_int(
    int64_t handle, const char *key, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !zan_table_lock(table)) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_find_row(table->header, key, 0) : NULL;
    if (row) zan_row_lock(row);
    zan_table_unlock(table);
    if (row) memcpy(&value, row + column->offset, sizeof(value));
    if (row) zan_row_unlock(row);
    return value;
}

int64_t zan_shared_table_set_float(
    int64_t handle, const char *key, const char *column_name, double value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !zan_table_lock(table)) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_FLOAT);
    unsigned char *row = column ? zan_find_row(table->header, key, 1) : NULL;
    if (row) zan_row_lock(row);
    zan_table_unlock(table);
    if (row) memcpy(row + column->offset, &value, sizeof(value));
    if (row) zan_row_unlock(row);
    return row != NULL;
}

double zan_shared_table_get_float(
    int64_t handle, const char *key, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !zan_table_lock(table)) return 0.0;
    double value = 0.0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_FLOAT);
    unsigned char *row = column ? zan_find_row(table->header, key, 0) : NULL;
    if (row) zan_row_lock(row);
    zan_table_unlock(table);
    if (row) memcpy(&value, row + column->offset, sizeof(value));
    if (row) zan_row_unlock(row);
    return value;
}

int64_t zan_shared_table_set_string(
    int64_t handle, const char *key, const char *column_name, const char *value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !value || !zan_table_lock(table)) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    size_t value_len = column ? strlen(value) : 0;
    unsigned char *row =
        column && value_len < column->size
            ? zan_find_row(table->header, key, 1)
            : NULL;
    if (row) zan_row_lock(row);
    zan_table_unlock(table);
    if (row) {
        char *destination = (char *)(row + column->offset);
        memset(destination, 0, column->size);
        memcpy(destination, value, value_len);
    }
    if (row) zan_row_unlock(row);
    return row != NULL;
}

const char *zan_shared_table_get_string(
    int64_t handle, const char *key, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    char *result = zan_get_shared_string();
    result[0] = '\0';
    if (!table || !zan_table_lock(table)) return result;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    unsigned char *row = column ? zan_find_row(table->header, key, 0) : NULL;
    if (row) zan_row_lock(row);
    zan_table_unlock(table);
    if (row) {
        size_t max_len = column->size - 1u;
        size_t len = zan_strnlen((const char *)(row + column->offset), max_len);
        memcpy(result, row + column->offset, len);
        result[len] = '\0';
    }
    if (row) zan_row_unlock(row);
    return result;
}

int64_t zan_shared_table_increment(
    int64_t handle, const char *key, const char *column_name, int64_t delta) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !zan_table_lock(table)) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_find_row(table->header, key, 1) : NULL;
    if (row) zan_row_lock(row);
    zan_table_unlock(table);
    if (row) {
        memcpy(&value, row + column->offset, sizeof(value));
        value += delta;
        memcpy(row + column->offset, &value, sizeof(value));
    }
    if (row) zan_row_unlock(row);
    return value;
}

int64_t zan_shared_table_delete(int64_t handle, const char *key) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !zan_table_lock(table)) return 0;
    unsigned char *row = zan_find_row(table->header, key, 0);
    if (row) {
        zan_row_lock(row);
        memset(row, 0, table->header->row_stride);
        *zan_row_state(row) = ZAN_SLOT_TOMBSTONE;
        table->header->count--;
        zan_row_unlock(row);
    }
    zan_table_unlock(table);
    return row != NULL;
}

int64_t zan_shared_table_exists(int64_t handle, const char *key) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !zan_table_lock(table)) return 0;
    int64_t exists = zan_find_row(table->header, key, 0) != NULL;
    zan_table_unlock(table);
    return exists;
}

int64_t zan_shared_table_count(int64_t handle) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !zan_table_lock(table)) return 0;
    int64_t count = (int64_t)table->header->count;
    zan_table_unlock(table);
    return count;
}

void zan_shared_table_clear(int64_t handle) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !zan_table_lock(table)) return;
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
    zan_table_unlock(table);
}
