/* rt_io_co_pool.c: 协程工作窃取队列 (Work-Stealing Deque)、注入器与帧检疫区实现 */

#if !defined(_WIN32)
typedef pthread_mutex_t CRITICAL_SECTION;
static inline void InitializeCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_init(cs, NULL); }
static inline void EnterCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_lock(cs); }
static inline void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(cs); }
static inline void DeleteCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_destroy(cs); }

#ifndef InterlockedCompareExchange64
#define InterlockedCompareExchange64(dst, exch, comp) \
    __sync_val_compare_and_swap((long long volatile *)(dst), (long long)(comp), (long long)(exch))
#endif

#ifndef InterlockedIncrement
#define InterlockedIncrement(dst) \
    (LONG)__sync_add_and_fetch((LONG volatile *)(dst), 1)
#endif

#ifndef InterlockedDecrement
#define InterlockedDecrement(dst) \
    (LONG)__sync_sub_and_fetch((LONG volatile *)(dst), 1)
#endif

#ifndef InterlockedExchange
#define InterlockedExchange(dst, val) \
    (LONG)__sync_lock_test_and_set((LONG volatile *)(dst), (LONG)(val))
#endif

#ifndef InterlockedCompareExchange
#define InterlockedCompareExchange(dst, exch, comp) \
    (LONG)__sync_val_compare_and_swap((LONG volatile *)(dst), (LONG)(comp), (LONG)(exch))
#endif

#ifndef InterlockedExchangeAdd
#define InterlockedExchangeAdd(dst, val) \
    (LONG)__sync_fetch_and_add((LONG volatile *)(dst), (LONG)(val))
#endif

#ifndef MemoryBarrier
#define MemoryBarrier() __sync_synchronize()
#endif

typedef pthread_key_t DWORD;
#ifndef TLS_OUT_OF_INDEXES
#define TLS_OUT_OF_INDEXES ((DWORD)-1)
#endif

static inline DWORD TlsAlloc(void) {
    pthread_key_t k;
    return (pthread_key_create(&k, NULL) == 0) ? k : TLS_OUT_OF_INDEXES;
}
#define TlsGetValue(k) pthread_getspecific(k)
#define TlsSetValue(k, v) pthread_setspecific((k), (v))

#define SwitchToThread() sched_yield()
#define Sleep(ms) usleep((ms) * 1000)

static inline unsigned long long GetTickCount64(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)(ts.tv_nsec / 1000000ULL);
}
#define GetTickCount() ((DWORD)GetTickCount64())
typedef int BOOL;
typedef unsigned long ULONG;
typedef uintptr_t ULONG_PTR;
typedef void* LPVOID;
typedef void* HANDLE;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif
#ifndef WINAPI
#define WINAPI
#endif
#endif /* !defined(_WIN32) */

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define ZAN_CO_MAXW      256
#define ZAN_LQ_CAP       256u
#define ZAN_LQ_MASK      (ZAN_LQ_CAP - 1u)
#define ZAN_LIFO_BUDGET  3
#define ZAN_GLOBAL_TICK  61
#define ZAN_POLL_GATE     256u
#define ZAN_STEAL_ROUNDS 2
#define ZAN_WAKE_KEY     ((ULONG_PTR)-3)

/* 协程调度器维护的异步帧前缀结构 */
typedef struct {
    volatile long long sched;    /* CO_* bits */
    zan_co_step_t      pending;
} zan_co_fhdr;

#define CO_QUEUED    1LL
#define CO_RUNNING   2LL
#define CO_NOTIFIED  4LL
#define CO_DEAD      8LL
#define CO_SELFQ     16LL  

typedef struct { void *frame; zan_co_step_t step; } zan_co_task;

typedef struct {
    unsigned long long ran;
    unsigned long long lifo_put;
    unsigned long long lifo_hit;
    unsigned long long lifo_demote;
    unsigned long long lq_push;
    unsigned long long lq_pop;
    unsigned long long spill;
    unsigned long long inj_push;
    unsigned long long inj_pop;
    unsigned long long steal_ok;
    unsigned long long steal_fail;
    unsigned long long park;
    unsigned long long wake_post;
    unsigned long long selfq;
} zan_co_stats_t;

typedef struct {
    volatile long long head;
    char pad_head[56];
    volatile long long tail;
    char pad_tail[56];
    zan_co_task        buf[ZAN_LQ_CAP];
    zan_co_task        lifo;
    int                lifo_full;
    int                lifo_budget;
    int         index;
    int         bg_gen;
    int                searching;
    long long          slice_start_us;
    /* 当前工作线程正在步进执行的协程帧指针 */
    void              *cur;
    volatile LONG      parked;
    unsigned           tick;
    unsigned           poll_tick;
    unsigned           rng;
    zan_co_stats_t     st;
    volatile LONG      cnt_act;
    volatile LONG      cnt_out;
    char               pad[64];
} __attribute__((aligned(64))) zan_co_worker_t;

static CRITICAL_SECTION g_co_lock;
static volatile LONG    g_co_running;
static volatile LONG    g_co_parked;
static volatile LONG    g_co_searching;
static volatile LONG    g_co_wake;
static volatile LONG    g_co_stop;
static int              g_co_inited;
static int              g_co_workers;
static DWORD            g_co_tls = TLS_OUT_OF_INDEXES;
static zan_co_worker_t *g_wk;

static volatile LONG g_co_pool_live;
static volatile LONG g_co_pool_fg;
static int           g_co_pool_gen;
static volatile LONG g_co_pool_out;
static void co_pool_ensure(void);

static CRITICAL_SECTION g_inj_lock;
static zan_co_node     *g_inj_head, *g_inj_tail, *g_inj_free;
static volatile LONG    g_inj_len;
static volatile LONG    g_inj_push_ext;

static long long co_now_ms(void) { return (long long)GetTickCount64(); }

static volatile LONG      g_timer_owner;
static volatile long long g_timer_pumped_us;   /* zan_co_precise_us stamp */
static volatile long long g_timer_next_ms;

/* 跨线程分片任务生成计量统计 */
static struct {
    _Alignas(64) volatile LONG act; char pad1[64 - sizeof(LONG)];
    _Alignas(64) volatile LONG out; char pad2[64 - sizeof(LONG)];
} g_co_cnt_ext;

static void co_wake_shard(int shard) {
#if defined(_WIN32)
    HANDLE p = io_shard(shard);
    if (p) PostQueuedCompletionStatus(p, 0, ZAN_WAKE_KEY, NULL);
#elif defined(__linux__)
    if (shard >= 0 && shard < (int)g_shards && g_ioshard[shard].wake_wfd >= 0) {
        uint64_t one = 1;
        (void)write(g_ioshard[shard].wake_wfd, &one, sizeof(one));
    }
#elif defined(__APPLE__)
    if (shard >= 0 && shard < (int)g_shards && g_ioshard[shard].wake_wfd >= 0) {
        char b = 1;
        (void)write(g_ioshard[shard].wake_wfd, &b, 1);
    }
#endif
}

static int co_worker_shard(int worker) {
    int n = (int)g_shards;
    return (n <= 1) ? 0 : (worker % n);
}

static zan_co_worker_t *co_self(void);

static void co_notify(void) {
    if (g_co_searching > 0) return;
    if (g_co_parked <= 0) return;
    if (InterlockedIncrement(&g_co_wake) > g_co_parked) {
        InterlockedDecrement(&g_co_wake);
        return;
    }
    zan_co_worker_t *self = co_self();
    if (self) self->st.wake_post++;
    for (int i = 0; i < g_co_workers; i++) {
        if (g_wk[i].parked) { co_wake_shard(co_worker_shard(i)); return; }
    }
    co_wake_shard(0);
}

static zan_co_worker_t *co_self(void) {
    if (g_co_tls == TLS_OUT_OF_INDEXES) return NULL;
    return (zan_co_worker_t *)TlsGetValue(g_co_tls);
}

#if !defined(_WIN32)
static int io_thread_shard(void) {
    zan_co_worker_t *w = co_self();
    int i = w ? w->index : 0;
    int n = (int)g_shards;
    return (n <= 1) ? 0 : (i % n);
}
#endif

static unsigned co_rand(zan_co_worker_t *w) {
    unsigned x = w->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    w->rng = x ? x : 0x9e3779b9u;
    return w->rng;
}

/* ---- injector ---- */

static void inj_push(zan_co_task t) {
    EnterCriticalSection(&g_inj_lock);
    zan_co_node *n = g_inj_free;
    if (n) g_inj_free = n->next;
    else {
        n = (zan_co_node *)malloc(sizeof(*n));
        if (!n) zan_rt_fatal("oom", "io: injection node alloc failed");
    }
    n->next = NULL; n->frame = t.frame; n->step = t.step;
    if (g_inj_tail) g_inj_tail->next = n; else g_inj_head = n;
    g_inj_tail = n;
    InterlockedIncrement(&g_inj_len);
    LeaveCriticalSection(&g_inj_lock);
}

static int inj_pop(zan_co_task *out) {
    if (g_inj_len <= 0) return 0;
    EnterCriticalSection(&g_inj_lock);
    zan_co_node *n = g_inj_head;
    if (n) {
        g_inj_head = n->next;
        if (!g_inj_head) g_inj_tail = NULL;
        out->frame = n->frame; out->step = n->step;
        n->next = g_inj_free; g_inj_free = n;
        InterlockedDecrement(&g_inj_len);
    }
    LeaveCriticalSection(&g_inj_lock);
    return n != NULL;
}

/* 单次加锁批量抽取注入的任务队列 */
#define ZAN_INJ_BATCH 64
static int inj_pop_batch(zan_co_task *out, int max) {
    if (g_inj_len <= 0) return 0;
    EnterCriticalSection(&g_inj_lock);
    int n = 0;
    while (n < max && g_inj_head) {
        zan_co_node *nd = g_inj_head;
        g_inj_head = nd->next;
        if (!g_inj_head) g_inj_tail = NULL;
        out[n].frame = nd->frame; out[n].step = nd->step;
        nd->next = g_inj_free; g_inj_free = nd;
        InterlockedDecrement(&g_inj_len);
        n++;
    }
    LeaveCriticalSection(&g_inj_lock);
    return n;
}


static void lq_push(zan_co_worker_t *w, zan_co_task t);

static void lq_spill(zan_co_worker_t *w) {
    zan_co_task tmp[ZAN_LQ_CAP / 2];
    for (;;) {
        long long h = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&w->tail, __ATOMIC_RELAXED);
        long long n = (t - h) / 2;
        if (n <= 0) return;
        if (n > (long long)(ZAN_LQ_CAP / 2)) n = (long long)(ZAN_LQ_CAP / 2);
        for (long long i = 0; i < n; i++)
            tmp[i] = w->buf[(unsigned long long)(h + i) & ZAN_LQ_MASK];
        if (!__atomic_compare_exchange_n(&w->head, &h, h + n, 0,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;
        for (long long i = 0; i < n; i++) inj_push(tmp[i]);
        w->st.spill++;
        w->st.inj_push += (unsigned long long)n;
        return;
    }
}

static void lq_push(zan_co_worker_t *w, zan_co_task t) {
    long long tail = __atomic_load_n(&w->tail, __ATOMIC_RELAXED);
    long long head = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
    if (tail - head >= (long long)ZAN_LQ_CAP) {
        lq_spill(w);
        head = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        if (tail - head >= (long long)ZAN_LQ_CAP) {
            inj_push(t);
            w->st.inj_push++;
            return;
        }
    }
    w->buf[(unsigned long long)tail & ZAN_LQ_MASK] = t;
    __atomic_store_n(&w->tail, tail + 1, __ATOMIC_RELEASE);
    w->st.lq_push++;
}

static int lq_pop(zan_co_worker_t *w, zan_co_task *out) {
    for (;;) {
        long long h = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&w->tail, __ATOMIC_ACQUIRE);
        if (h >= t) return 0;
        zan_co_task task = w->buf[(unsigned long long)h & ZAN_LQ_MASK];
        if (__atomic_compare_exchange_n(&w->head, &h, h + 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            *out = task;
            return 1;
        }
    }
}

static int lq_steal(zan_co_worker_t *v, zan_co_worker_t *w, zan_co_task *out) {
    zan_co_task tmp[ZAN_LQ_CAP / 2];
    int attempts = 0;
    for (;;) {
        long long h = __atomic_load_n(&v->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&v->tail, __ATOMIC_ACQUIRE);
        long long n = t - h;
        if (n <= 0) return 0;
        n = n - n / 2;                                   /* half, rounded up */
        if (n > (long long)(ZAN_LQ_CAP / 2)) n = (long long)(ZAN_LQ_CAP / 2);
        for (long long i = 0; i < n; i++)
            tmp[i] = v->buf[(unsigned long long)(h + i) & ZAN_LQ_MASK];
        if (!__atomic_compare_exchange_n(&v->head, &h, h + n, 0,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            attempts++;
            if (attempts > 16) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
                __builtin_ia32_pause();   /* no <emmintrin.h> on musl/wasi */
#endif
            }
            if (attempts > 64) {
                return 0;
            }
            continue;
        }
        *out = tmp[0];
        for (long long i = 1; i < n; i++) lq_push(w, tmp[i]);
        return 1;
    }
}


static zan_co_fhdr *co_hdr(void *frame) { return (zan_co_fhdr *)frame; }

#define ZAN_TR_RING 8192
typedef struct { long long seq; unsigned long tid; const char* tag; const void* fp; } zan_tr_ent;
static zan_tr_ent *g_tr_ring;
static volatile long long g_tr_seq = -1;
static int g_co_trace = -1;
static void co_trace(const char *what, const void *fp) {
    if (g_co_trace < 0) {
        const char *e = getenv("ZAN_CO_TRACE");
        g_co_trace = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
        if (g_co_trace && !g_tr_ring) {
            g_tr_ring = (zan_tr_ent *)calloc(ZAN_TR_RING, sizeof(zan_tr_ent));
        }
    }
    if (!g_co_trace || !g_tr_ring) return;
    long long i = InterlockedIncrement64(&g_tr_seq);
    zan_tr_ent *e = &g_tr_ring[i & (ZAN_TR_RING - 1)];
    e->seq = i;
    e->tid = GetCurrentThreadId();
    e->tag = what;
    e->fp = fp;
}

static void co_trace_dump(long long live) {
    if (g_co_trace < 0) {
        const char *e = getenv("ZAN_CO_TRACE");
        g_co_trace = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
    }
    if (!g_co_trace || !g_tr_ring) return;
    long long total = g_tr_seq;
    long long first = total - ZAN_TR_RING;
    if (first < 0) first = -1;
    fprintf(stderr, "[cot DUMP live=%lld seq=%lld]\n",
        live, total);
    for (long long s = (first > 0 ? first : 0) + 1; s <= total; s++) {
        zan_tr_ent *e = &g_tr_ring[s & (ZAN_TR_RING - 1)];
        if (e->seq != s) continue;
        fprintf(stderr, "[cot %lld %lu %s %p]\n",
            e->seq, (unsigned long)e->tid, e->tag, e->fp);
    }
    fflush(stderr);
}

static LONG co_cnt_activity(void) {
    long long s = g_co_cnt_ext.act;
    for (int i = 0; i < g_co_workers; i++) s += g_wk[i].cnt_act;
    return (LONG)s;
}
static LONG co_cnt_outstanding(void) {
    long long s = g_co_cnt_ext.out;
    for (int i = 0; i < g_co_workers; i++) s += g_wk[i].cnt_out;
    return (LONG)s;
}
static void co_cnt_add(zan_co_worker_t *w, int act, int out) {
    volatile LONG *pa = w ? &w->cnt_act : &g_co_cnt_ext.act;
    volatile LONG *po = w ? &w->cnt_out : &g_co_cnt_ext.out;
    if (act > 0)      InterlockedIncrement(pa);
    else if (act < 0) InterlockedDecrement(pa);
    if (out > 0)      InterlockedIncrement(po);
    else if (out < 0) InterlockedDecrement(po);
}

static void co_submit(void *frame, zan_co_step_t step) {
    zan_co_task t;
    t.frame = frame; t.step = step;
    zan_co_worker_t *w = co_self();
    co_cnt_add(w, +1, +1);
    if (!w) {
        inj_push(t);
        InterlockedIncrement(&g_inj_push_ext);
        co_trace("inj", t.frame);
        co_notify();
        return;
    }
    if (!w->lifo_full) {
        w->lifo = t;
        w->lifo_full = 1;
        w->st.lifo_put++;
        return;
    }
    lq_push(w, t);
    co_notify();
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step || !frame) return;
    zan_co_fhdr *h = co_hdr(frame);
    for (;;) {
        long long s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
        if (s & CO_DEAD) return;
        if (s & CO_QUEUED) return;
        if (s & CO_RUNNING) {
            zan_co_worker_t *sw = co_self();
            long long selfq = (sw && sw->cur == frame) ? CO_SELFQ : 0;
            __atomic_store_n(&h->pending, step, __ATOMIC_RELAXED);
            if (__atomic_compare_exchange_n(&h->sched, &s,
                                            s | CO_NOTIFIED | selfq, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
            continue;
        }
    if (__atomic_compare_exchange_n(&h->sched, &s, s | CO_QUEUED, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        break;
    }
    co_submit(frame, step);
    /* 跨线程安全就绪：支持任意线程恢复协程 */
    co_pool_ensure();
}

/* ---- 帧检疫区（A380 方向 b）----
 * 帧的释放方（读 .Result 后的 zan_emit_frame_free）与完成方可能对同一
 * 帧各走一次 __zan_co_frame_free。幂等守卫读帧首 CO_DEAD 位，但通用
 * 分配器在 free 后会把块首 8 字节写成 freelist 指针（rt_mem.c），守卫
 * 读到垃圾 → 二次释放直接 abort（double free of block）。检疫区把释放
 * 的帧延迟归还：守卫位在被覆盖之前，第二次释放稳定读到 DEAD 并返回。
 * 封顶 ZAN_FRAME_QUAR_MAX 帧，超限按 FIFO 真放前一半——检疫窗口过后
 * 的陈旧双释放本就超出指针身份能判定的范围（A380 的结论）。 */
/* 线程本地检疫区（Per-Thread TLS Quarantine）：
 * 彻底消除多线程并发下释放协程帧时的全局排他锁争用（Lock Contention）。
 * 每个工作线程独立维护 64 帧容量的无锁环形检疫窗口；满后平滑真释放最老的一半，
 * 为幂等守卫提供完全充足的 DEAD 状态检疫时间窗口，零动态分配、零跨核锁竞争。 */
#define ZAN_TLS_FRAME_QUAR_CAP 64
static _Thread_local void  *t_frame_quar[ZAN_TLS_FRAME_QUAR_CAP];
static _Thread_local size_t t_frame_quar_head = 0;
static _Thread_local size_t t_frame_quar_len = 0;

static void co_frame_quarantine_add(void *frame) {
    if (t_frame_quar_len == ZAN_TLS_FRAME_QUAR_CAP) {
        size_t half = ZAN_TLS_FRAME_QUAR_CAP / 2;
        for (size_t i = 0; i < half; i++) {
            void *old_frame = t_frame_quar[(t_frame_quar_head + i) % ZAN_TLS_FRAME_QUAR_CAP];
            free(old_frame);
        }
        t_frame_quar_head = (t_frame_quar_head + half) % ZAN_TLS_FRAME_QUAR_CAP;
        t_frame_quar_len -= half;
    }
    size_t tail = (t_frame_quar_head + t_frame_quar_len) % ZAN_TLS_FRAME_QUAR_CAP;
    t_frame_quar[tail] = frame;
    t_frame_quar_len++;
}

void __zan_co_frame_free(void *frame) {
    if (!frame) return;
    zan_co_fhdr *h = co_hdr(frame);
    for (;;) {
        long long s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
        if (s & CO_DEAD) return;
        if (s & (CO_QUEUED | CO_RUNNING)) {
            if (__atomic_compare_exchange_n(&h->sched, &s, s | CO_DEAD, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
            continue;
        }
        if (__atomic_compare_exchange_n(&h->sched, &s, CO_DEAD, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    co_frame_quarantine_add(frame);
}

void zan_co_delay(long long ms, void *frame, zan_co_step_t step) {
    zan_timer_delay(ms, frame, step);
    co_cnt_add(co_self(), +1, 0);
    co_notify();
}

static int co_worker_count(void) {
    int w = 0;
    w = zan_async_cfg_workers();
    if (w <= 0) {
#if defined(_WIN32)
        char envbuf[32];
        DWORD r = GetEnvironmentVariableA("ZAN_CO_WORKERS", envbuf, sizeof(envbuf));
        if (r > 0 && r < sizeof(envbuf)) w = atoi(envbuf);
#endif
    }
    if (w <= 0) {
        const char *e = getenv("ZAN_CO_WORKERS");
        if (e && *e) w = atoi(e);
    }
    if (w <= 0) {
#if defined(_WIN32)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        w = (int)si.dwNumberOfProcessors;
#else
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        w = (n > 0) ? (int)n : 1;
#endif
        if (w > 4) w = 4;
    }
    if (w < 1) w = 1;
    if (w > ZAN_CO_MAXW) w = ZAN_CO_MAXW;
    return w;
}

#endif /* _WIN32 || __linux__ || __APPLE__ */
