/* rt_sync_thread.c: 操作系统原生线程、监视器锁、UI 派发队列与原子整数原语实现
 */

/* 在独立操作系统线程上派生运行 Zan 委托 */

typedef void (*zan_thread_body_fn)(void);

static void *zan_delegate_record(void *d);
void zan_backend_delegate_retain(void *d);
void zan_backend_delegate_release(void *d);
void zan_backend_delegate_invoke(void *d);

/* MSVC COFF needs an unresolved-symbol fallback rather than a weak body.
 * GNU/MinGW and ELF use weak definitions. Selfhost supplies strong hooks. */
#if defined(_MSC_VER)
#pragma comment(                                                               \
    linker,                                                                    \
    "/alternatename:zan_backend_delegate_retain=zan_default_delegate_retain")
#pragma comment(                                                               \
    linker,                                                                    \
    "/alternatename:zan_backend_delegate_release=zan_default_delegate_release")
#pragma comment(                                                               \
    linker,                                                                    \
    "/alternatename:zan_backend_delegate_invoke=zan_default_delegate_invoke")
#define ZAN_DELEGATE_HOOK(op) void zan_default_delegate_##op
#else
#define ZAN_DELEGATE_HOOK(op)                                                  \
  __attribute__((weak)) void zan_backend_delegate_##op
#endif

ZAN_DELEGATE_HOOK(invoke)(void *body) {
  if (!body)
    return;
  void *rec = zan_delegate_record(body);
  if (!rec) {
    ((zan_thread_body_fn)body)();
    return;
  }
  void **slots = (void **)rec;
  void (*fn)(void *) = (void (*)(void *))slots[0];
  if (fn)
    fn(rec);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void __zan_eh_release(void) {}
#else
void __zan_eh_release(void) {}
#endif

void zan_thread_detach(void) { __zan_eh_release(); }

#ifdef _WIN32
static DWORD WINAPI zan_thread_trampoline(LPVOID arg) {
  zan_backend_delegate_invoke(arg);
  zan_backend_delegate_release(arg);
  zan_thread_detach();
  return 0;
}

int32_t zan_thread_start(void *body) {
  if (!body)
    return 0;
  zan_backend_delegate_retain(body);
  HANDLE h = CreateThread(NULL, 0, zan_thread_trampoline, body, 0, NULL);
  if (!h) {
    zan_backend_delegate_release(body);
    return 0;
  }
  CloseHandle(h);
  return 1;
}
#else
static void *zan_thread_trampoline(void *arg) {
  zan_backend_delegate_invoke(arg);
  zan_backend_delegate_release(arg);
  zan_thread_detach();
  return NULL;
}

int32_t zan_thread_start(void *body) {
  if (!body)
    return 0;
  zan_backend_delegate_retain(body);
  pthread_t t;
  if (pthread_create(&t, NULL, zan_thread_trampoline, body) != 0) {
    zan_backend_delegate_release(body);
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
  if (pthread_threadid_np(NULL, &tid) != 0)
    return 0;
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

/* ---- 监视器锁：Zan `lock (obj) { ... }` ------------------------------------
 * 对象没有可用的头部字段，监视状态放在按地址散列的 256 桶表里。每桶一把短临界区锁
 * （Apple os_unfair_lock / Windows SRWLOCK / 其余 pthread 互斥锁），只守护链表与块内
 * owner/recursion/waiters；临界区内无系统调用，空闲链命中时也无分配。
 *
 * 无争用（瘦锁）：enter = 桶锁 + 查链 + 取块（优先本桶空闲链）+ 记 owner/递归计数；
 *   exit = 桶锁 + 递归计数归零 + 块回空闲链。没有 malloc/free，没有递归互斥锁初始化，
 *   重入只是计数加一。
 * 争用：后来者先登记 waiters（块因此不会被回收），放开桶锁短暂自旋观察 owner，仍被占
 *   就在块自带的 mutex+cond 上停泊；唤醒代数 wake_seq 防丢失唤醒（桶锁与停泊锁双持才
 *   写，持其一即可读）。持有者 exit 时若 waiters>0，在桶锁内推进代数并 signal——锁序
 *   恒为 桶锁 → 停泊锁，停泊方等待期间不持桶锁。允许插队（与常见互斥锁同为非公平）。
 * 回收：owner 归零且 waiters==0 时块离链，进本桶空闲链（每桶至多 ZAN_SYNC_FREE_CAP 个），
 *   超出即销毁释放。常驻块数 ≤ 在持/在争对象数 + 256×CAP，锁大量短命对象不增长；
 *   块只在被持有或被等待期间挂链，对象释放后地址复用拿到的必是新块，无 UAF。 */
#define ZAN_SYNC_BUCKETS 256
#define ZAN_SYNC_FREE_CAP 4
#define ZAN_SYNC_SPIN 128

#if defined(__APPLE__) && !defined(_WIN32)
#include <os/lock.h>
#endif

#ifdef _WIN32
typedef SRWLOCK zan_mon_lock_t;
#define zan_mon_lock(l) AcquireSRWLockExclusive(l)
#define zan_mon_unlock(l) ReleaseSRWLockExclusive(l)
#elif defined(__APPLE__)
typedef os_unfair_lock zan_mon_lock_t;
#define zan_mon_lock(l) os_unfair_lock_lock(l)
#define zan_mon_unlock(l) os_unfair_lock_unlock(l)
#else
typedef pthread_mutex_t zan_mon_lock_t;
#define zan_mon_lock(l) pthread_mutex_lock(l)
#define zan_mon_unlock(l) pthread_mutex_unlock(l)
#endif

typedef struct zan_sync_block {
    void *obj;
    struct zan_sync_block *next;
    uintptr_t owner;     /* 0 = 无主；桶锁内写，自旋期无锁原子读 */
    uint32_t recursion;  /* 桶锁内读写 */
    uint32_t waiters;    /* 自旋/停泊中的线程数，桶锁内维护；>0 时块不离链 */
    uint32_t wake_seq;   /* 唤醒代数：桶锁 + 停泊锁双持写 */
#ifdef _WIN32
    SRWLOCK park_lock;
    CONDITION_VARIABLE park_cv;
#else
    pthread_mutex_t park_mx;
    pthread_cond_t park_cv;
#endif
} zan_sync_block_t;

typedef struct {
    _Alignas(64) zan_mon_lock_t lock; /* 每桶独占缓存行，免相邻桶伪共享 */
    zan_sync_block_t *head;           /* 被持有/被等待的块 */
    zan_sync_block_t *free_list;      /* 回收待复用的块（停泊原语保持已初始化） */
    uint32_t free_count;
} zan_sync_bucket_t;

/* SRWLOCK_INIT 与 OS_UNFAIR_LOCK_INIT 均为全零，静态零初始化即就绪 */
#if defined(_WIN32) || defined(__APPLE__)
static zan_sync_bucket_t g_sync_buckets[ZAN_SYNC_BUCKETS];
#else
static zan_sync_bucket_t g_sync_buckets[ZAN_SYNC_BUCKETS] = {
    [0 ... ZAN_SYNC_BUCKETS - 1] = { PTHREAD_MUTEX_INITIALIZER, NULL, NULL, 0 }
};
#endif
static int64_t g_sync_blocks_allocated; /* 仅冷路径（分配/释放块）原子增减 */

static inline unsigned zan_sync_bucket_idx(void *obj) {
  uintptr_t bits = (uintptr_t)obj;
  bits ^= bits >> 20;
  bits ^= bits >> 8;
  return (unsigned)((bits >> 4) & (ZAN_SYNC_BUCKETS - 1));
}

static inline uintptr_t zan_mon_self(void) {
#ifdef _WIN32
    return (uintptr_t)GetCurrentThreadId(); /* 线程 ID 永不为 0 */
#else
    return (uintptr_t)pthread_self();
#endif
}

static inline uintptr_t zan_mon_owner(zan_sync_block_t *sb) {
#ifdef _WIN32
    return *(volatile uintptr_t *)&sb->owner; /* 对齐指针宽读单拷贝原子；自旋只作提示，桶锁内复核 */
#else
    return __atomic_load_n(&sb->owner, __ATOMIC_RELAXED);
#endif
}

static inline void zan_mon_set_owner(zan_sync_block_t *sb, uintptr_t v) {
#ifdef _WIN32
    *(volatile uintptr_t *)&sb->owner = v;
#else
    __atomic_store_n(&sb->owner, v, __ATOMIC_RELAXED);
#endif
}

static inline void zan_mon_relax(void) {
#ifdef _WIN32
    YieldProcessor();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#endif
}

static void zan_mon_block_destroy(zan_sync_block_t *sb) {
#ifndef _WIN32
    pthread_cond_destroy(&sb->park_cv);
    pthread_mutex_destroy(&sb->park_mx);
#endif
    free(sb);
#ifdef _WIN32
    InterlockedDecrement64((volatile LONG64 *)&g_sync_blocks_allocated);
#else
    __atomic_sub_fetch(&g_sync_blocks_allocated, 1, __ATOMIC_RELAXED);
#endif
}

/* 持桶锁调用：优先复用本桶空闲块，否则新分配（停泊原语只在此初始化一次）。 */
static zan_sync_block_t *zan_mon_block_take(zan_sync_bucket_t *b) {
    zan_sync_block_t *sb = b->free_list;
    if (sb) {
        b->free_list = sb->next;
        b->free_count--;
        return sb;
    }
    sb = (zan_sync_block_t *)ZAN_OOM_MALLOC("sync.monitor_block", sizeof(zan_sync_block_t));
    if (!sb) return NULL;
    sb->wake_seq = 0;
#ifdef _WIN32
    InitializeSRWLock(&sb->park_lock);
    InitializeConditionVariable(&sb->park_cv);
    InterlockedIncrement64((volatile LONG64 *)&g_sync_blocks_allocated);
#else
    pthread_mutex_init(&sb->park_mx, NULL);
    pthread_cond_init(&sb->park_cv, NULL);
    __atomic_add_fetch(&g_sync_blocks_allocated, 1, __ATOMIC_RELAXED);
#endif
    return sb;
}

void zan_monitor_enter(void *obj) {
    if (!obj) return;
    uintptr_t self = zan_mon_self();
    zan_sync_bucket_t *b = &g_sync_buckets[zan_sync_bucket_idx(obj)];

    zan_mon_lock(&b->lock);
    zan_sync_block_t *sb = b->head;
    while (sb && sb->obj != obj) sb = sb->next;

    if (!sb) {
        sb = zan_mon_block_take(b);
        if (!sb) {
            zan_mon_unlock(&b->lock);
            /* B-ID98：monitor 块分配失败即 fatal，不得未持锁裸奔回返回（对齐本文件 TLS 路径口径） */
            zan_rt_fatal("oom", "sync: monitor block alloc failed");
        }
        sb->obj = obj;
        sb->recursion = 1;
        sb->waiters = 0;
        zan_mon_set_owner(sb, self);
        sb->next = b->head;
        b->head = sb;
        zan_mon_unlock(&b->lock);
        return;
    }

    uintptr_t cur = zan_mon_owner(sb);
    if (cur == self) {
        sb->recursion++;
        zan_mon_unlock(&b->lock);
        return;
    }
    if (cur == 0) {
        zan_mon_set_owner(sb, self);
        sb->recursion = 1;
        zan_mon_unlock(&b->lock);
        return;
    }

    /* 争用：登记 waiters 钉住块，再放桶锁去自旋 / 停泊 */
    sb->waiters++;
    for (;;) {
        uint32_t seen = sb->wake_seq;
        zan_mon_unlock(&b->lock);

        int freed = 0;
        for (int i = 0; i < ZAN_SYNC_SPIN; i++) {
            zan_mon_relax();
            if (zan_mon_owner(sb) == 0) { freed = 1; break; }
        }
        if (!freed) {
#ifdef _WIN32
            AcquireSRWLockExclusive(&sb->park_lock);
            while (sb->wake_seq == seen)
                SleepConditionVariableSRW(&sb->park_cv, &sb->park_lock, INFINITE, 0);
            ReleaseSRWLockExclusive(&sb->park_lock);
#else
            pthread_mutex_lock(&sb->park_mx);
            while (sb->wake_seq == seen)
                pthread_cond_wait(&sb->park_cv, &sb->park_mx);
            pthread_mutex_unlock(&sb->park_mx);
#endif
        }

        zan_mon_lock(&b->lock);
        if (zan_mon_owner(sb) == 0) {
            zan_mon_set_owner(sb, self);
            sb->recursion = 1;
            sb->waiters--;
            zan_mon_unlock(&b->lock);
            return;
        }
    }
}

void zan_monitor_exit(void *obj) {
    if (!obj) return;
    uintptr_t self = zan_mon_self();
    zan_sync_bucket_t *b = &g_sync_buckets[zan_sync_bucket_idx(obj)];

    zan_mon_lock(&b->lock);
    zan_sync_block_t **pp = &b->head;
    while (*pp && (*pp)->obj != obj) pp = &(*pp)->next;
    zan_sync_block_t *sb = *pp;

    /* 未持有（含非本线程持有）的 exit 静默忽略，绝不替别的线程放锁 */
    if (!sb || zan_mon_owner(sb) != self) {
        zan_mon_unlock(&b->lock);
        return;
    }
    if (--sb->recursion > 0) {
        zan_mon_unlock(&b->lock);
        return;
    }
    zan_mon_set_owner(sb, 0);

    if (sb->waiters > 0) {
        /* 桶锁内唤醒：waiters>0 保证块不被回收；被唤醒者要先拿桶锁才能复核 */
#ifdef _WIN32
        AcquireSRWLockExclusive(&sb->park_lock);
        sb->wake_seq++;
        WakeConditionVariable(&sb->park_cv);
        ReleaseSRWLockExclusive(&sb->park_lock);
#else
        pthread_mutex_lock(&sb->park_mx);
        sb->wake_seq++;
        pthread_cond_signal(&sb->park_cv);
        pthread_mutex_unlock(&sb->park_mx);
#endif
        zan_mon_unlock(&b->lock);
        return;
    }

    *pp = sb->next;
    sb->obj = NULL;
    if (b->free_count < ZAN_SYNC_FREE_CAP) {
        sb->next = b->free_list;
        b->free_list = sb;
        b->free_count++;
        sb = NULL;
    }
    zan_mon_unlock(&b->lock);
    if (sb) zan_mon_block_destroy(sb); /* 已离链且无人等待，桶锁外销毁 */
}

/* 自检用：in_use = 挂链（被持有/被等待）块数，allocated = 已分配未释放块数（含空闲链）。 */
void zan_monitor_debug_stats(int64_t *in_use, int64_t *allocated) {
    int64_t used = 0;
    for (unsigned i = 0; i < ZAN_SYNC_BUCKETS; i++) {
        zan_sync_bucket_t *b = &g_sync_buckets[i];
        zan_mon_lock(&b->lock);
        for (zan_sync_block_t *sb = b->head; sb; sb = sb->next) used++;
        zan_mon_unlock(&b->lock);
    }
    if (in_use) *in_use = used;
#ifdef _WIN32
    if (allocated) *allocated = (int64_t)InterlockedCompareExchange64(
        (volatile LONG64 *)&g_sync_blocks_allocated, 0, 0);
#else
    if (allocated) *allocated = __atomic_load_n(&g_sync_blocks_allocated, __ATOMIC_RELAXED);
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
  (void)once;
  (void)param;
  (void)ctx;
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
  if (!g_ui_thread_id)
    return 1;
#ifdef _WIN32
  return (uintptr_t)GetCurrentThreadId() == g_ui_thread_id ? 1 : 0;
#else
  return pthread_equal((pthread_t)g_ui_thread_id, pthread_self()) ? 1 : 0;
#endif
}

void zan_ui_thread_assert(const char *msg) {
  if (!zan_ui_thread_check()) {
    fprintf(stderr,
            "fatal error: UI thread assertion failed: %s (called from "
            "background thread)\n",
            msg ? msg : "must be called on UI thread");
    abort();
  }
}

void zan_dispatch_clear(void);

void zan_dispatch_init(void) {
  zan_ui_thread_set();
  /* Reinitialization releases queued owners outside the queue lock. */
  zan_dispatch_clear();
}

static void *zan_delegate_record(void *d) {
  uintptr_t v = (uintptr_t)d;
  if (!(v & (uintptr_t)ZAN_CLOSURE_TAG))
    return NULL;
  return (void *)(v & ~(uintptr_t)ZAN_CLOSURE_TAG);
}

ZAN_DELEGATE_HOOK(retain)(void *d) {
  void *rec = zan_delegate_record(d);
  if (!rec)
    return;
  volatile uint32_t *rc = ZAN_RC_PTR(rec);
#ifdef _WIN32
  if ((uint32_t)InterlockedCompareExchange((volatile LONG *)rc, 0, 0) ==
      ZAN_RC_IMMORTAL)
    return;
  InterlockedIncrement((volatile LONG *)rc);
#else
  if (__atomic_load_n((uint32_t *)rc, __ATOMIC_SEQ_CST) == ZAN_RC_IMMORTAL)
    return;
  __atomic_add_fetch((uint32_t *)rc, 1u, __ATOMIC_SEQ_CST);
#endif
}

ZAN_DELEGATE_HOOK(release)(void *d) {
  void *rec = zan_delegate_record(d);
  if (!rec)
    return;
  void **slots = (void **)rec;
  void *dtor = slots[1];
  if (dtor)
    ((void (*)(void *))dtor)(rec);
}
#undef ZAN_DELEGATE_HOOK

/* A444 安全栏：object 槽里的堆值是不是字符串负载。
 * C 宿主的字符串，[-8] 高 32 位恒为 ZAN_STRING_TAG（"ZANS"）：静态字面量
 * (TAG<<32)|len、动态拼接/字符串集合 TAG<<32 + 长度另存、嵌入资源 MAGIC
 * 高位同样是 ZANS。类实例的 [-8] 头字高 32 位是 tw（对象标记 | 站点号，
 * 站点号远到不了 0x1a414e53），不会误判。判不出的（野指针）按非字符串
 * 处理，调用方 fail-closed。自举后端的串头形状不同（[-24]=0、[-8]=
 * 低位 ZNST），自举在自己内部内联判别，不走这个 helper。 */
int zan_obj_slot_is_string(const void *p) {
  if (!p)
    return 0;
  const uint64_t *w = (const uint64_t *)((const char *)p + ZAN_HDR_WORD_OFF);
  return (w[0] >> 32) == ZAN_STRING_TAG;
}

static int zan_dispatch_grow(void) {
  if ((unsigned)g_dispatch_cap >= ZAN_DISPATCH_CAP_MAX)
    return 0;
  int ncap = g_dispatch_cap * 2;
  void **nring = (void **)malloc((size_t)ncap * sizeof *nring);
  if (!nring)
    return 0;
  int count = 0;
  for (int i = g_dispatch_head; i != g_dispatch_tail;
       i = (i + 1) % g_dispatch_cap)
    nring[count++] = g_dispatch_ring[i];
  if (g_dispatch_ring != g_dispatch_static)
    free(g_dispatch_ring);
  g_dispatch_ring = nring;
  g_dispatch_cap = ncap;
  g_dispatch_head = 0;
  g_dispatch_tail = count;
  return 1;
}

/* Enqueue a delegate to run on the UI thread */
int32_t zan_dispatch_post(void *fn) {
  if (!fn)
    return 0;
  int32_t ok = 0;
  zan_dispatch_lock();
  int next = (g_dispatch_tail + 1) % g_dispatch_cap;
  if (next == g_dispatch_head && zan_dispatch_grow())
    next = (g_dispatch_tail + 1) % g_dispatch_cap;
  if (next != g_dispatch_head) {
    zan_backend_delegate_retain(fn);
    g_dispatch_ring[g_dispatch_tail] = fn;
    g_dispatch_tail = next;
    ok = 1;
  }
  zan_dispatch_unlock();
  if (!ok) {
    static int reported;
    if (!reported) {
      reported = 1;
      fprintf(stderr,
              "zan: UI dispatch queue full (%d pending); "
              "posted handler dropped\n",
              g_dispatch_cap - 1);
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
    for (int i = 0; i < n; i++)
      zan_backend_delegate_release(drop[i]);
    if (n < (int)(sizeof drop / sizeof *drop))
      return;
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
  if (!atomic)
    return 0;
#ifdef _WIN32
  atomic->value = (LONG64)initial_value;
#else
  __atomic_store_n(&atomic->value, initial_value, __ATOMIC_SEQ_CST);
#endif
  return (int64_t)(intptr_t)atomic;
}

void zan_atomic_int_destroy(int64_t handle) { free((void *)(intptr_t)handle); }

int64_t zan_atomic_int_load(int64_t handle) {
  zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
  if (!atomic)
    return 0;
#ifdef _WIN32
  return (int64_t)InterlockedCompareExchange64(&atomic->value, 0, 0);
#else
  return __atomic_load_n(&atomic->value, __ATOMIC_SEQ_CST);
#endif
}

void zan_atomic_int_store(int64_t handle, int64_t value) {
  zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
  if (!atomic)
    return;
#ifdef _WIN32
  InterlockedExchange64(&atomic->value, (LONG64)value);
#else
  __atomic_store_n(&atomic->value, value, __ATOMIC_SEQ_CST);
#endif
}

int64_t zan_atomic_int_exchange(int64_t handle, int64_t value) {
  zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
  if (!atomic)
    return 0;
#ifdef _WIN32
  return (int64_t)InterlockedExchange64(&atomic->value, (LONG64)value);
#else
  return __atomic_exchange_n(&atomic->value, value, __ATOMIC_SEQ_CST);
#endif
}

int64_t zan_atomic_int_compare_exchange(int64_t handle, int64_t expected,
                                        int64_t desired) {
  zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
  if (!atomic)
    return 0;
#ifdef _WIN32
  return (int64_t)InterlockedCompareExchange64(&atomic->value, (LONG64)desired,
                                               (LONG64)expected);
#else
  __atomic_compare_exchange_n(&atomic->value, &expected, desired, 0,
                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return expected;
#endif
}

int64_t zan_atomic_int_add(int64_t handle, int64_t delta) {
  zan_atomic_int *atomic = (zan_atomic_int *)(intptr_t)handle;
  if (!atomic)
    return 0;
#ifdef _WIN32
  return (int64_t)InterlockedExchangeAdd64(&atomic->value, (LONG64)delta) +
         delta;
#else
  return __atomic_add_fetch(&atomic->value, delta, __ATOMIC_SEQ_CST);
#endif
}

/* ---- Scoped zero-copy Parallel.For / Parallel.ForRange worker pool ---- */

typedef struct zan_par_job {
  void *fn_ptr;
  void *clo_rec;
  int32_t from;
  int32_t to;
  int32_t chunk;
  int32_t is_range;
#ifdef _WIN32
  volatile LONG next_idx;
#else
  int32_t next_idx;
#endif
} zan_par_job_t;

#define ZAN_PAR_MAX_WORKERS 31

static _Thread_local int t_in_parallel_for = 0;

static struct {
#ifdef _WIN32
  INIT_ONCE init_once;
  SRWLOCK lock;
  CONDITION_VARIABLE cv_work;
  CONDITION_VARIABLE cv_done;
#else
  pthread_once_t init_once;
  pthread_mutex_t lock;
  pthread_cond_t cv_work;
  pthread_cond_t cv_done;
#endif
  int32_t num_workers;
  uint64_t generation;
  zan_par_job_t *active_job;
  int32_t active_workers;
  int32_t workers_finished;
  int32_t dispatch_busy;
} g_par_pool = {
#ifdef _WIN32
    INIT_ONCE_STATIC_INIT,
    SRWLOCK_INIT,
    CONDITION_VARIABLE_INIT,
    CONDITION_VARIABLE_INIT,
#else
    PTHREAD_ONCE_INIT,
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
#endif
    0, 0, NULL, 0, 0, 0
};

int32_t zan_parallel_processor_count(void) {
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  int32_t n = (int32_t)si.dwNumberOfProcessors;
  return n > 0 ? n : 1;
#else
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? (int32_t)n : 1;
#endif
}

static void zan_par_decode_delegate(void *body, void **out_fn, void **out_rec) {
  uintptr_t v = (uintptr_t)body;
  if (v & (uintptr_t)ZAN_CLOSURE_TAG) {
    void *rec = (void *)(v & ~(uintptr_t)ZAN_CLOSURE_TAG);
    *out_rec = rec;
    *out_fn = *(void **)rec;
  } else {
    *out_rec = NULL;
    *out_fn = body;
  }
}

static void zan_par_run_job(zan_par_job_t *job) {
  int32_t to = job->to;
  int32_t chunk = job->chunk;
  void *fn_ptr = job->fn_ptr;
  void *clo_rec = job->clo_rec;
  int32_t is_range = job->is_range;
  for (;;) {
#ifdef _WIN32
    int32_t s = (int32_t)InterlockedExchangeAdd(&job->next_idx, (LONG)chunk);
#else
    int32_t s = __atomic_fetch_add(&job->next_idx, chunk, __ATOMIC_RELAXED);
#endif
    if (s >= to)
      break;
    int32_t e = s + chunk;
    if (e > to)
      e = to;
    if (is_range) {
      if (clo_rec)
        ((void (*)(void *, int32_t, int32_t))fn_ptr)(clo_rec, s, e);
      else
        ((void (*)(int32_t, int32_t))fn_ptr)(s, e);
    } else {
      if (clo_rec) {
        void (*f)(void *, int32_t) = (void (*)(void *, int32_t))fn_ptr;
        for (int32_t i = s; i < e; i++)
          f(clo_rec, i);
      } else {
        void (*f)(int32_t) = (void (*)(int32_t))fn_ptr;
        for (int32_t i = s; i < e; i++)
          f(i);
      }
    }
  }
}

#ifdef _WIN32
static DWORD WINAPI zan_par_worker_main(LPVOID unused) {
  (void)unused;
  uint64_t my_gen = 0;
  for (;;) {
    AcquireSRWLockExclusive(&g_par_pool.lock);
    while (g_par_pool.generation == my_gen)
      SleepConditionVariableSRW(&g_par_pool.cv_work, &g_par_pool.lock, INFINITE, 0);
    my_gen = g_par_pool.generation;
    zan_par_job_t *job = g_par_pool.active_job;
    ReleaseSRWLockExclusive(&g_par_pool.lock);

    if (job) {
      t_in_parallel_for = 1;
      zan_par_run_job(job);
      t_in_parallel_for = 0;
    }

    AcquireSRWLockExclusive(&g_par_pool.lock);
    g_par_pool.workers_finished++;
    if (g_par_pool.workers_finished == g_par_pool.active_workers)
      WakeAllConditionVariable(&g_par_pool.cv_done);
    ReleaseSRWLockExclusive(&g_par_pool.lock);
  }
  return 0;
}

static BOOL CALLBACK zan_par_init_cb(PINIT_ONCE once, PVOID param, PVOID *ctx) {
  (void)once; (void)param; (void)ctx;
  int32_t procs = zan_parallel_processor_count();
  int32_t nw = procs > 1 ? procs - 1 : 0;
  if (nw > ZAN_PAR_MAX_WORKERS)
    nw = ZAN_PAR_MAX_WORKERS;
  int32_t spawned = 0;
  for (int32_t i = 0; i < nw; i++) {
    HANDLE h = CreateThread(NULL, 0, zan_par_worker_main, NULL, 0, NULL);
    if (h) {
      CloseHandle(h);
      spawned++;
    }
  }
  g_par_pool.num_workers = spawned;
  return TRUE;
}
#else
static void *zan_par_worker_main(void *unused) {
  (void)unused;
  uint64_t my_gen = 0;
  for (;;) {
    pthread_mutex_lock(&g_par_pool.lock);
    while (g_par_pool.generation == my_gen)
      pthread_cond_wait(&g_par_pool.cv_work, &g_par_pool.lock);
    my_gen = g_par_pool.generation;
    zan_par_job_t *job = g_par_pool.active_job;
    pthread_mutex_unlock(&g_par_pool.lock);

    if (job) {
      t_in_parallel_for = 1;
      zan_par_run_job(job);
      t_in_parallel_for = 0;
    }

    pthread_mutex_lock(&g_par_pool.lock);
    g_par_pool.workers_finished++;
    if (g_par_pool.workers_finished == g_par_pool.active_workers)
      pthread_cond_broadcast(&g_par_pool.cv_done);
    pthread_mutex_unlock(&g_par_pool.lock);
  }
  return NULL;
}

static void zan_par_init_cb(void) {
  int32_t procs = zan_parallel_processor_count();
  int32_t nw = procs > 1 ? procs - 1 : 0;
  if (nw > ZAN_PAR_MAX_WORKERS)
    nw = ZAN_PAR_MAX_WORKERS;
  int32_t spawned = 0;
  for (int32_t i = 0; i < nw; i++) {
    pthread_t th;
    if (pthread_create(&th, NULL, zan_par_worker_main, NULL) == 0) {
      pthread_detach(th);
      spawned++;
    }
  }
  g_par_pool.num_workers = spawned;
}
#endif

static void zan_par_dispatch(int32_t from, int32_t to, void *body,
                             int32_t is_range) {
  if (!body || to <= from)
    return;
  void *fn_ptr = NULL;
  void *clo_rec = NULL;
  zan_par_decode_delegate(body, &fn_ptr, &clo_rec);
  if (!fn_ptr)
    return;

  int32_t total = to - from;
  if (t_in_parallel_for || total <= 1) {
    if (is_range) {
      if (clo_rec)
        ((void (*)(void *, int32_t, int32_t))fn_ptr)(clo_rec, from, to);
      else
        ((void (*)(int32_t, int32_t))fn_ptr)(from, to);
    } else {
      if (clo_rec) {
        void (*f)(void *, int32_t) = (void (*)(void *, int32_t))fn_ptr;
        for (int32_t i = from; i < to; i++)
          f(clo_rec, i);
      } else {
        void (*f)(int32_t) = (void (*)(int32_t))fn_ptr;
        for (int32_t i = from; i < to; i++)
          f(i);
      }
    }
    return;
  }

#ifdef _WIN32
  InitOnceExecuteOnce(&g_par_pool.init_once, zan_par_init_cb, NULL, NULL);
#else
  pthread_once(&g_par_pool.init_once, zan_par_init_cb);
#endif

  int32_t nw = g_par_pool.num_workers;
  if (nw <= 0) {
    t_in_parallel_for = 1;
    zan_par_dispatch(from, to, body, is_range);
    t_in_parallel_for = 0;
    return;
  }

  int32_t nthreads = nw + 1;
  int32_t denom = is_range ? (nthreads * 4) : (nthreads * 8);
  int32_t chunk = total / denom;
  if (chunk < 1)
    chunk = 1;

  zan_par_job_t job;
  job.fn_ptr = fn_ptr;
  job.clo_rec = clo_rec;
  job.from = from;
  job.to = to;
  job.chunk = chunk;
  job.is_range = is_range;
  job.next_idx = from;

#ifdef _WIN32
  AcquireSRWLockExclusive(&g_par_pool.lock);
  while (g_par_pool.dispatch_busy)
    SleepConditionVariableSRW(&g_par_pool.cv_done, &g_par_pool.lock, INFINITE, 0);
  g_par_pool.dispatch_busy = 1;
  g_par_pool.active_job = &job;
  g_par_pool.workers_finished = 0;
  g_par_pool.active_workers = nw;
  g_par_pool.generation++;
  WakeAllConditionVariable(&g_par_pool.cv_work);
  ReleaseSRWLockExclusive(&g_par_pool.lock);

  t_in_parallel_for = 1;
  zan_par_run_job(&job);
  t_in_parallel_for = 0;

  AcquireSRWLockExclusive(&g_par_pool.lock);
  while (g_par_pool.workers_finished < g_par_pool.active_workers)
    SleepConditionVariableSRW(&g_par_pool.cv_done, &g_par_pool.lock, INFINITE, 0);
  g_par_pool.active_job = NULL;
  g_par_pool.dispatch_busy = 0;
  WakeAllConditionVariable(&g_par_pool.cv_done);
  ReleaseSRWLockExclusive(&g_par_pool.lock);
#else
  pthread_mutex_lock(&g_par_pool.lock);
  while (g_par_pool.dispatch_busy)
    pthread_cond_wait(&g_par_pool.cv_done, &g_par_pool.lock);
  g_par_pool.dispatch_busy = 1;
  g_par_pool.active_job = &job;
  g_par_pool.workers_finished = 0;
  g_par_pool.active_workers = nw;
  g_par_pool.generation++;
  pthread_cond_broadcast(&g_par_pool.cv_work);
  pthread_mutex_unlock(&g_par_pool.lock);

  t_in_parallel_for = 1;
  zan_par_run_job(&job);
  t_in_parallel_for = 0;

  pthread_mutex_lock(&g_par_pool.lock);
  while (g_par_pool.workers_finished < g_par_pool.active_workers)
    pthread_cond_wait(&g_par_pool.cv_done, &g_par_pool.lock);
  g_par_pool.active_job = NULL;
  g_par_pool.dispatch_busy = 0;
  pthread_cond_broadcast(&g_par_pool.cv_done);
  pthread_mutex_unlock(&g_par_pool.lock);
#endif
}

void zan_parallel_for(int32_t from, int32_t to, void *body) {
  zan_par_dispatch(from, to, body, 0);
}

void zan_parallel_for_range(int32_t from, int32_t to, void *body) {
  zan_par_dispatch(from, to, body, 1);
}
