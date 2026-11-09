/* rt_sync_thread.c: 操作系统原生线程、监视器锁、UI 派发队列与原子整数原语实现 */

/* 在独立操作系统线程上派生运行 Zan 委托 */

typedef void (*zan_thread_body_fn)(void);

static void *zan_delegate_record(void *d);
static void zan_delegate_retain(void *d);
static void zan_delegate_release(void *d);

static int zan_is_code_pointer(void *p) {
    if (!p) return 0;
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi))) {
        DWORD prot = mbi.Protect;
        if (prot & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) {
            return 1;
        }
    }
    return 0;
#else
    return 1;
#endif
}

static void zan_thread_invoke(void *body) {
    void *rec = zan_delegate_record(body);
    if (!rec) {
        ((zan_thread_body_fn)body)();
        return;
    }
    void **slots = (void **)rec;
    void (*fn)(void *) = (void (*)(void *))slots[0];
    void *second = slots[1];
    /* C 宿主闭包：slots[1] 为 dtor（代码段函数指针），调用传 rec 本身：fn(rec)
     * 自举后端闭包：slots[1] 为 env（堆数据指针或 NULL），调用传 env：fn(env) */
    if (zan_is_code_pointer(second)) {
        if (fn) fn(rec);
    } else {
        if (fn) fn(second);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void __zan_eh_release(void) { }
#else
void __zan_eh_release(void) { }
#endif

void zan_thread_detach(void) {
    __zan_eh_release();
}

#ifdef _WIN32
static DWORD WINAPI zan_thread_trampoline(LPVOID arg) {
    zan_thread_invoke(arg);
    zan_delegate_release(arg);
    zan_thread_detach();
    return 0;
}

int32_t zan_thread_start(void *body) {
    if (!body) return 0;
    zan_delegate_retain(body);
    HANDLE h = CreateThread(NULL, 0, zan_thread_trampoline, body, 0, NULL);
    if (!h) {
        zan_delegate_release(body);
        return 0;
    }
    CloseHandle(h);
    return 1;
}
#else
static void *zan_thread_trampoline(void *arg) {
    zan_thread_invoke(arg);
    zan_delegate_release(arg);
    zan_thread_detach();
    return NULL;
}

int32_t zan_thread_start(void *body) {
    if (!body) return 0;
    zan_delegate_retain(body);
    pthread_t t;
    if (pthread_create(&t, NULL, zan_thread_trampoline, body) != 0) {
        zan_delegate_release(body);
        return 0;
    }
    pthread_detach(t);
    return 1;
}
#endif

#if defined(__linux__)
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
    uint64_t tid = 0;
    if (pthread_threadid_np(NULL, &tid) != 0) return 0;
    return (int64_t)tid;
#elif defined(__FreeBSD__) || defined(__DragonFly__)
    return (int64_t)pthread_getthreadid_np();
#elif defined(__OpenBSD__)
    return (int64_t)getthrid();
#elif defined(__NetBSD__)
    return (int64_t)_lwp_self();
#else
    uintptr_t self = (uintptr_t)pthread_self();
    return (int64_t)((self >> 16) ^ (self & 0xffffffffu));
#endif
}


/* 生产级自适应 SyncTable：细粒度分桶锁 + 独立 SyncBlock 链表
 * 彻底消除静态条带哈希碰撞引发的不相干对象伪死锁 */
#define ZAN_SYNC_BUCKETS 256

typedef struct zan_sync_block {
    void *obj;
    int32_t ref_count;
#ifdef _WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t mx;
#endif
    struct zan_sync_block *next;
} zan_sync_block_t;

typedef struct {
#ifdef _WIN32
    CRITICAL_SECTION bucket_cs;
#else
    pthread_mutex_t bucket_mx;
#endif
    zan_sync_block_t *head;
} zan_sync_bucket_t;

static zan_sync_bucket_t g_sync_buckets[ZAN_SYNC_BUCKETS];
#ifdef _WIN32
static INIT_ONCE g_sync_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK zan_sync_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    for (unsigned i = 0; i < ZAN_SYNC_BUCKETS; i++) {
        InitializeCriticalSection(&g_sync_buckets[i].bucket_cs);
        g_sync_buckets[i].head = NULL;
    }
    return TRUE;
}
#else
static pthread_once_t g_sync_once = PTHREAD_ONCE_INIT;
static void zan_sync_init(void) {
    for (unsigned i = 0; i < ZAN_SYNC_BUCKETS; i++) {
        pthread_mutex_init(&g_sync_buckets[i].bucket_mx, NULL);
        g_sync_buckets[i].head = NULL;
    }
}
#endif

static inline unsigned zan_sync_bucket_idx(void *obj) {
    uintptr_t bits = (uintptr_t)obj;
    bits ^= bits >> 20;
    bits ^= bits >> 8;
    return (unsigned)((bits >> 4) & (ZAN_SYNC_BUCKETS - 1));
}

void zan_monitor_enter(void *obj) {
    if (!obj) return;
#ifdef _WIN32
    InitOnceExecuteOnce(&g_sync_once, zan_sync_init, NULL, NULL);
#else
    pthread_once(&g_sync_once, zan_sync_init);
#endif
    unsigned bidx = zan_sync_bucket_idx(obj);
    zan_sync_bucket_t *bucket = &g_sync_buckets[bidx];

#ifdef _WIN32
    EnterCriticalSection(&bucket->bucket_cs);
#else
    pthread_mutex_lock(&bucket->bucket_mx);
#endif

    zan_sync_block_t *sb = bucket->head;
    while (sb && sb->obj != obj) {
        sb = sb->next;
    }

    if (!sb) {
        sb = (zan_sync_block_t *)ZAN_OOM_MALLOC("sync.monitor_block",
                                                sizeof(zan_sync_block_t));
        if (!sb) {
#ifdef _WIN32
            LeaveCriticalSection(&bucket->bucket_cs);
#else
            pthread_mutex_unlock(&bucket->bucket_mx);
#endif
            /* B-ID98：monitor 块分配失败即 fatal，不得未持锁裸奔回返回（对齐本文件 TLS 路径口径） */
            zan_rt_fatal("oom", "sync: monitor block alloc failed");
        }
        sb->obj = obj;
        sb->ref_count = 0;
#ifdef _WIN32
        InitializeCriticalSection(&sb->cs);
#else
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&sb->mx, &attr);
        pthread_mutexattr_destroy(&attr);
#endif
        sb->next = bucket->head;
        bucket->head = sb;
    }

    sb->ref_count++;

#ifdef _WIN32
    LeaveCriticalSection(&bucket->bucket_cs);
    EnterCriticalSection(&sb->cs);
#else
    pthread_mutex_unlock(&bucket->bucket_mx);
    pthread_mutex_lock(&sb->mx);
#endif
}

void zan_monitor_exit(void *obj) {
    if (!obj) return;
    unsigned bidx = zan_sync_bucket_idx(obj);
    zan_sync_bucket_t *bucket = &g_sync_buckets[bidx];

    /* 必须先释放该对象内部持有的递归互斥锁，再进入桶锁更新引用计数并判定销毁，
       彻底避免跨线程争用时提前在桶内销毁锁引发的释放 UAF 崩溃 */
#ifdef _WIN32
    EnterCriticalSection(&bucket->bucket_cs);
    zan_sync_block_t **curr_p = &bucket->head;
    zan_sync_block_t *sb = NULL;
    while (*curr_p) {
        if ((*curr_p)->obj == obj) {
            sb = *curr_p;
            break;
        }
        curr_p = &(*curr_p)->next;
    }

    if (!sb) {
        LeaveCriticalSection(&bucket->bucket_cs);
        return;
    }

    LeaveCriticalSection(&sb->cs);

    sb->ref_count--;
    if (sb->ref_count <= 0) {
        *curr_p = sb->next;
        DeleteCriticalSection(&sb->cs);
        free(sb);
    }
    LeaveCriticalSection(&bucket->bucket_cs);
#else
    pthread_mutex_lock(&bucket->bucket_mx);
    zan_sync_block_t **curr_p = &bucket->head;
    zan_sync_block_t *sb = NULL;
    while (*curr_p) {
        if ((*curr_p)->obj == obj) {
            sb = *curr_p;
            break;
        }
        curr_p = &(*curr_p)->next;
    }

    if (!sb) {
        pthread_mutex_unlock(&bucket->bucket_mx);
        return;
    }

    pthread_mutex_unlock(&sb->mx);

    sb->ref_count--;
    if (sb->ref_count <= 0) {
        *curr_p = sb->next;
        pthread_mutex_destroy(&sb->mx);
        free(sb);
    }
    pthread_mutex_unlock(&bucket->bucket_mx);
#endif
}

/* UI 主线程安全分发任务队列 */

#define ZAN_DISPATCH_CAP0 64
#define ZAN_DISPATCH_CAP_MAX (1u << 20)
static void *g_dispatch_static[ZAN_DISPATCH_CAP0];
static void **g_dispatch_ring = g_dispatch_static;
static int g_dispatch_cap = ZAN_DISPATCH_CAP0;
static int g_dispatch_head = 0;
static int g_dispatch_tail = 0;

#ifdef _WIN32
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
    if (!g_ui_thread_id) return 1;
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

void zan_dispatch_init(void) {
    zan_ui_thread_set();
    zan_dispatch_lock();
    g_dispatch_head = 0;
    g_dispatch_tail = 0;
    zan_dispatch_unlock();
}

static void *zan_delegate_record(void *d) {
    uintptr_t v = (uintptr_t)d;
    if (!(v & (uintptr_t)ZAN_CLOSURE_TAG)) return NULL;
    return (void *)(v & ~(uintptr_t)ZAN_CLOSURE_TAG);
}

static void zan_delegate_retain(void *d) {
    void *rec = zan_delegate_record(d);
    if (!rec) return;
    void **slots = (void **)rec;
    /* 仅 C 宿主带 ARC 头且 slots[1] 为 dtor 的闭包记录需要递增引用计数；
     * 自举后端裸 malloc(16) 闭包无 ARC 头 */
    if (!zan_is_code_pointer(slots[1])) return;
    volatile int64_t *rc = (volatile int64_t *)((char *)rec + ZAN_OBJ_RC_OFF);
#ifdef _WIN32
    InterlockedIncrement64((volatile LONG64 *)rc);
#else
    __atomic_add_fetch((int64_t *)rc, (int64_t)1, __ATOMIC_SEQ_CST);
#endif
}

static void zan_delegate_release(void *d) {
    void *rec = zan_delegate_record(d);
    if (!rec) return;
    void **slots = (void **)rec;
    /* 仅 C 宿主闭包具有 dtor（代码指针），自举后端 slots[1] 为 env 数据指针不得调用 */
    if (!zan_is_code_pointer(slots[1])) return;
    void *dtor = slots[1];
    if (dtor) ((void (*)(void *))dtor)(rec);
}

/* A444 安全栏：object 槽里的堆值是不是字符串负载。
 * C 宿主的字符串，[-8] 高 32 位恒为 ZAN_STRING_TAG（"ZANS"）：静态字面量
 * (TAG<<32)|len、动态拼接/字符串集合 TAG<<32 + 长度另存、嵌入资源 MAGIC
 * 高位同样是 ZANS。类实例的 [-8] 是 ARC 站点/析构指针，用户态地址高
 * 32 位远到不了 0x5a414e53，不会误判。判不出的（野指针）按非字符串
 * 处理，调用方 fail-closed。自举后端的串头形状不同（[-24]=0、[-8]=
 * 低位 ZNST），自举在自己内部内联判别，不走这个 helper。 */
int zan_obj_slot_is_string(const void *p) {
    if (!p) return 0;
    const uint64_t *w = (const uint64_t *)((const char *)p + ZAN_OBJ_RC_OFF);
    return (w[1] >> 32) == ZAN_STRING_TAG;
}

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
        static int reported;
        if (!reported) {
            reported = 1;
            fprintf(stderr, "zan: UI dispatch queue full (%d pending); "
                            "posted handler dropped\n", g_dispatch_cap - 1);
        }
    }
    return ok;
}

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

void zan_dispatch_clear(void) {
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

typedef struct {
#ifdef _WIN32
    volatile LONG64 value;
#else
    int64_t value;
#endif
} zan_atomic_int;

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
