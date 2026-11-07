/* rt_sync_table.c: 跨平台共享内存表核心结构、CAS 自旋锁与槽位查找算法 */

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
