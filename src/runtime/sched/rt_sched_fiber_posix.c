/* rt_sched_fiber_posix.c: POSIX (Linux / Darwin / BSD) 原生纤程、换栈与 Worker 线程底座实现 */

#ifdef _WIN32
#error "rt_sched_fiber_posix.c is not intended for Windows targets"
#endif

#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include <errno.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

#if defined(ZAN_USE_UCONTEXT) || (!defined(__x86_64__) && !defined(_M_X64) && !defined(__aarch64__) && !defined(_M_ARM64))
#include <ucontext.h>
#define ZAN_POSIX_UCONTEXT 1
#else
#define ZAN_POSIX_ASM_FIBER 1
#endif

#if defined(__SANITIZE_ADDRESS__)
#define ZAN_ASAN_FIBER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ZAN_ASAN_FIBER 1
#endif
#endif
#if ZAN_ASAN_FIBER
#include <sanitizer/asan_interface.h>
#endif

/* ---- 互斥锁与原子操作 ---- */
typedef pthread_mutex_t zan_mutex_t;
static inline void zan_mutex_init(zan_mutex_t *m) { pthread_mutex_init(m, NULL); }
static inline void zan_mutex_lock(zan_mutex_t *m) { pthread_mutex_lock(m); }
static inline void zan_mutex_unlock(zan_mutex_t *m) { pthread_mutex_unlock(m); }
static inline void zan_mutex_destroy(zan_mutex_t *m) { pthread_mutex_destroy(m); }

static inline int zan_atomic_inc(volatile int *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
static inline int zan_atomic_dec(volatile int *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
static inline int zan_atomic_read(volatile int *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }

static inline void co_acquire(zan_co_t *co) {
    while (__atomic_test_and_set(&co->running, __ATOMIC_ACQUIRE)) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield" ::: "memory");
#elif defined(__riscv)
        __asm__ __volatile__(".insn i 0x0F, 0, x0, x0, 0x010" ::: "memory");
#endif
    }
}

static inline void co_release(zan_co_t *co) {
    __atomic_clear(&co->running, __ATOMIC_RELEASE);
}

/* ---- 线程与唤醒同步 ---- */
typedef pthread_t plat_thread_t;

typedef struct {
    pthread_cond_t  wake_cond;
    pthread_mutex_t wake_mutex;
} plat_worker_sync_t;

static inline void plat_worker_sync_init(plat_worker_sync_t *ws) {
    pthread_cond_init(&ws->wake_cond, NULL);
    pthread_mutex_init(&ws->wake_mutex, NULL);
}

static inline void plat_worker_sync_destroy(plat_worker_sync_t *ws) {
    pthread_cond_destroy(&ws->wake_cond);
    pthread_mutex_destroy(&ws->wake_mutex);
}

static inline void plat_worker_sync_wake_one(plat_worker_sync_t *ws) {
    pthread_mutex_lock(&ws->wake_mutex);
    pthread_cond_signal(&ws->wake_cond);
    pthread_mutex_unlock(&ws->wake_mutex);
}

static inline void plat_worker_sync_wake_all(plat_worker_sync_t *ws) {
    pthread_mutex_lock(&ws->wake_mutex);
    pthread_cond_broadcast(&ws->wake_cond);
    pthread_mutex_unlock(&ws->wake_mutex);
}

static inline void plat_worker_sync_wait(plat_worker_sync_t *ws, int timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long nsec = ts.tv_nsec + (long)(timeout_ms > 0 ? timeout_ms : 1) * 1000000L;
    ts.tv_sec += nsec / 1000000000L;
    ts.tv_nsec = nsec % 1000000000L;
    pthread_mutex_lock(&ws->wake_mutex);
    pthread_cond_timedwait(&ws->wake_cond, &ws->wake_mutex, &ts);
    pthread_mutex_unlock(&ws->wake_mutex);
}

static inline int plat_thread_create(plat_thread_t *th, void *(*fn)(void *), void *arg) {
    return pthread_create(th, NULL, fn, arg);
}

static inline void plat_thread_join(plat_thread_t th) {
    pthread_join(th, NULL);
}

/* ---- 纤程与换栈上下文 ---- */
#if ZAN_POSIX_ASM_FIBER

typedef struct {
    void   *sp;
    char   *stack;
    size_t  stack_size;
} posix_fiber_t;

static ZAN_THREAD_LOCAL void *g_sched_sp;

extern void zan_fiber_switch(void **from_sp, void *to_sp);
extern void zan_fiber_entry(void);
void co_trampoline_posix(zan_co_t *co);

#if defined(__APPLE__)
#define ZAN_ASM_SYM(name) "_" #name
#define ZAN_ASM_TYPE(name) ""
#else
#define ZAN_ASM_SYM(name) #name
#define ZAN_ASM_TYPE(name) ".type " #name ", @function\n"
#endif

#if defined(__x86_64__) || defined(_M_X64)

__asm__(
    ".text\n"
    ".globl " ZAN_ASM_SYM(zan_fiber_switch) "\n"
    ZAN_ASM_TYPE(zan_fiber_switch)
    ZAN_ASM_SYM(zan_fiber_switch) ":\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq  %rsp, (%rdi)\n"
    "    movq  %rsi, %rsp\n"
    "    popq  %r15\n"
    "    popq  %r14\n"
    "    popq  %r13\n"
    "    popq  %r12\n"
    "    popq  %rbx\n"
    "    popq  %rbp\n"
    "    ret\n"
);

__asm__(
    ".text\n"
    ".globl " ZAN_ASM_SYM(zan_fiber_entry) "\n"
    ZAN_ASM_TYPE(zan_fiber_entry)
    ZAN_ASM_SYM(zan_fiber_entry) ":\n"
    "    movq %rbx, %rdi\n"
    "    call " ZAN_ASM_SYM(co_trampoline_posix) "\n"
    "    ud2\n"
);

#elif defined(__aarch64__) || defined(_M_ARM64)

__asm__(
    ".text\n"
    ".globl " ZAN_ASM_SYM(zan_fiber_switch) "\n"
    ZAN_ASM_TYPE(zan_fiber_switch)
    ZAN_ASM_SYM(zan_fiber_switch) ":\n"
    "    stp  x19, x20, [sp, #-16]!\n"
    "    stp  x21, x22, [sp, #-16]!\n"
    "    stp  x23, x24, [sp, #-16]!\n"
    "    stp  x25, x26, [sp, #-16]!\n"
    "    stp  x27, x28, [sp, #-16]!\n"
    "    stp  x29, x30, [sp, #-16]!\n"
    "    stp  d8,  d9,  [sp, #-16]!\n"
    "    stp  d10, d11, [sp, #-16]!\n"
    "    stp  d12, d13, [sp, #-16]!\n"
    "    stp  d14, d15, [sp, #-16]!\n"
    "    mov  x2, sp\n"
    "    str  x2, [x0]\n"
    "    mov  sp, x1\n"
    "    ldp  d14, d15, [sp], #16\n"
    "    ldp  d12, d13, [sp], #16\n"
    "    ldp  d10, d11, [sp], #16\n"
    "    ldp  d8,  d9,  [sp], #16\n"
    "    ldp  x29, x30, [sp], #16\n"
    "    ldp  x27, x28, [sp], #16\n"
    "    ldp  x25, x26, [sp], #16\n"
    "    ldp  x23, x24, [sp], #16\n"
    "    ldp  x21, x22, [sp], #16\n"
    "    ldp  x19, x20, [sp], #16\n"
    "    ret\n"
);

__asm__(
    ".text\n"
    ".globl " ZAN_ASM_SYM(zan_fiber_entry) "\n"
    ZAN_ASM_TYPE(zan_fiber_entry)
    ZAN_ASM_SYM(zan_fiber_entry) ":\n"
    "    mov  x0, x19\n"
    "    bl   " ZAN_ASM_SYM(co_trampoline_posix) "\n"
    "    brk  #0\n"
);

#endif /* Arch */

#else /* ZAN_POSIX_UCONTEXT */

static ZAN_THREAD_LOCAL ucontext_t g_sched_ctx;
#if UINTPTR_MAX > 0xFFFFFFFFu
static void co_trampoline_posix_ucontext(unsigned hi, unsigned lo);
#else
static void co_trampoline_posix_ucontext(unsigned ptr);
#endif

typedef struct { ucontext_t ctx; char *stack; } posix_fiber_t;

#endif /* ZAN_POSIX_ASM_FIBER */

#define ZAN_CO_STACK_POOL_MAX 256

static zan_mutex_t g_stack_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static void *g_stack_pool;
static int g_stack_pool_n;

static char *plat_stack_alloc(void) {
    zan_mutex_lock(&g_stack_pool_lock);
    if (g_stack_pool) {
        char *s = (char *)g_stack_pool;
        g_stack_pool = *(void **)s;
        g_stack_pool_n--;
        zan_mutex_unlock(&g_stack_pool_lock);
        return s;
    }
    zan_mutex_unlock(&g_stack_pool_lock);
    long page = sysconf(_SC_PAGESIZE);
    size_t total = co_stack_size() + (size_t)page;
    char *base = (char *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    if (mprotect(base, (size_t)page, PROT_NONE) != 0) {
        munmap(base, total);
        return NULL;
    }
    return base + page;
}

static void plat_stack_free(char *stack) {
    zan_mutex_lock(&g_stack_pool_lock);
    if (g_stack_pool_n < ZAN_CO_STACK_POOL_MAX) {
        *(void **)stack = g_stack_pool;
        g_stack_pool = stack;
        g_stack_pool_n++;
        zan_mutex_unlock(&g_stack_pool_lock);
        return;
    }
    zan_mutex_unlock(&g_stack_pool_lock);
    long page = sysconf(_SC_PAGESIZE);
    munmap((char *)stack - page, co_stack_size() + (size_t)page);
}

static void plat_sched_drain_stacks(void) {
    zan_mutex_lock(&g_stack_pool_lock);
    long page = sysconf(_SC_PAGESIZE);
    size_t total = co_stack_size() + (size_t)page;
    while (g_stack_pool) {
        char *s = (char *)g_stack_pool;
        g_stack_pool = *(void **)s;
        g_stack_pool_n--;
        munmap(s - page, total);
    }
    zan_mutex_unlock(&g_stack_pool_lock);
}

#if ZAN_POSIX_ASM_FIBER
static void plat_sched_enter(void) { g_sched_sp = NULL; }
static void plat_sched_leave(void) {}
#else
static void plat_sched_enter(void) { g_sched_fiber = &g_sched_ctx; }
static void plat_sched_leave(void) {}
#endif

static void *plat_fiber_new(zan_co_t *co) {
    posix_fiber_t *pf = (posix_fiber_t *)calloc(1, sizeof(*pf));
    if (!pf) zan_rt_fatal("oom", "scheduler: fiber control block alloc failed");
    pf->stack = plat_stack_alloc();
    if (!pf->stack) zan_rt_fatal("oom", "scheduler: fiber stack alloc failed");
#if ZAN_POSIX_ASM_FIBER
    pf->stack_size = co_stack_size();
    char *stack_top = pf->stack + pf->stack_size;
#if defined(__x86_64__) || defined(_M_X64)
    uintptr_t sp = (uintptr_t)stack_top & ~15ULL;
    uintptr_t *s = (uintptr_t *)sp;
    s[-1] = (uintptr_t)zan_fiber_entry;
    s[-2] = 0;                  /* rbp */
    s[-3] = (uintptr_t)co;      /* rbx -> co argument passed to trampoline */
    s[-4] = 0;                  /* r12 */
    s[-5] = 0;                  /* r13 */
    s[-6] = 0;                  /* r14 */
    s[-7] = 0;                  /* r15 */
    pf->sp = (void *)(s - 7);
#elif defined(__aarch64__) || defined(_M_ARM64)
    uintptr_t sp = (uintptr_t)stack_top & ~15ULL;
    uintptr_t *s = (uintptr_t *)(sp - 160);
    memset(s, 0, 160);
    s[0] = (uintptr_t)co;              /* x19 */
    s[10] = 0;                         /* x29 (fp) */
    s[11] = (uintptr_t)zan_fiber_entry;/* x30 (lr) */
    pf->sp = (void *)s;
#endif
#else /* UCONTEXT */
    getcontext(&pf->ctx);
    pf->ctx.uc_stack.ss_sp = pf->stack;
    pf->ctx.uc_stack.ss_size = co_stack_size();
    pf->ctx.uc_link = &g_sched_ctx;
#if UINTPTR_MAX > 0xFFFFFFFFu
    uintptr_t p = (uintptr_t)co;
    makecontext(&pf->ctx, (void (*)(void))co_trampoline_posix_ucontext, 2,
                (unsigned)(p >> 32), (unsigned)(p & 0xffffffffu));
#else
    makecontext(&pf->ctx, (void (*)(void))co_trampoline_posix_ucontext, 1,
                (unsigned)(uintptr_t)co);
#endif
#endif
    return pf;
}

static void plat_fiber_delete(void *f) {
    posix_fiber_t *pf = (posix_fiber_t *)f;
    plat_stack_free(pf->stack);
    free(pf);
}

static void plat_switch(void *to) {
    posix_fiber_t *pf = (posix_fiber_t *)to;
#if ZAN_POSIX_ASM_FIBER
#if ZAN_ASAN_FIBER
    __sanitizer_start_switch_fiber(NULL, pf->stack, pf->stack_size);
    zan_fiber_switch(&g_sched_sp, pf->sp);
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#else
    zan_fiber_switch(&g_sched_sp, pf->sp);
#endif
#else /* UCONTEXT */
    pf->ctx.uc_link = &g_sched_ctx;
#if ZAN_ASAN_FIBER
    __sanitizer_start_switch_fiber(NULL, pf->stack, co_stack_size());
    swapcontext(&g_sched_ctx, &pf->ctx);
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#else
    swapcontext(&g_sched_ctx, &pf->ctx);
#endif
#endif
}

static void plat_sleep(int64_t ms) {
    if (ms < 0) ms = 0;
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int64_t plat_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void plat_switch_to_sched(void) {
    if (g_current) {
        g_current->last_error = (int32_t)errno;
    }
    posix_fiber_t *pf = (posix_fiber_t *)g_current->fiber;
#if ZAN_POSIX_ASM_FIBER
#if ZAN_ASAN_FIBER
    __sanitizer_start_switch_fiber(NULL, NULL, 0);
    zan_fiber_switch(&pf->sp, g_sched_sp);
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#else
    zan_fiber_switch(&pf->sp, g_sched_sp);
#endif
#else /* UCONTEXT */
#if ZAN_ASAN_FIBER
    __sanitizer_start_switch_fiber(NULL, NULL, 0);
    swapcontext(&pf->ctx, &g_sched_ctx);
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#else
    swapcontext(&pf->ctx, &g_sched_ctx);
#endif
#endif
    if (g_current) {
        errno = (int)g_current->last_error;
    }
}

static inline void plat_switch_co(zan_co_t *co) {
    co_acquire(co);
    g_current = co;
    co->slice_start_ms = plat_now_ms();
    co->slice_ticks = 0;
    co->preempt_requested = 0;
    errno = (int)co->last_error;
    plat_switch(co->fiber);
    g_current = NULL;
}

#if ZAN_POSIX_ASM_FIBER
void co_trampoline_posix(zan_co_t *co) {
#if ZAN_ASAN_FIBER
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#endif
    errno = 0;
    co->body(co->task);
    co->finished = 1;
    complete_task(co->task, 0);
    plat_switch_to_sched();
}
#else
#if UINTPTR_MAX > 0xFFFFFFFFu
static void co_trampoline_posix_ucontext(unsigned hi, unsigned lo) {
    zan_co_t *co = (zan_co_t *)(((uintptr_t)hi << 32) | (uintptr_t)lo);
#else
static void co_trampoline_posix_ucontext(unsigned ptr) {
    zan_co_t *co = (zan_co_t *)(uintptr_t)ptr;
#endif
#if ZAN_ASAN_FIBER
    __sanitizer_finish_switch_fiber(NULL, NULL, NULL);
#endif
    errno = 0;
    co->body(co->task);
    co->finished = 1;
    complete_task(co->task, 0);
    plat_switch_to_sched();
}
#endif

/* ---- Watchdog 抢占巡检后台线程 ---- */
static plat_thread_t g_watchdog_thread;
static volatile int g_watchdog_active = 0;
static void *watchdog_thread_main(void *arg);

static void plat_watchdog_start(void) {
    g_watchdog_active = 1;
    plat_thread_create(&g_watchdog_thread, watchdog_thread_main, NULL);
}

static void plat_watchdog_stop(void) {
    if (g_watchdog_active) {
        plat_thread_join(g_watchdog_thread);
        g_watchdog_active = 0;
    }
}
