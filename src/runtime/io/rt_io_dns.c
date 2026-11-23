/* rt_io_dns.c: 异步 DNS 解析服务与后台阻塞型工作任务线程池 */

/* 异步主机名与域名解析服务 */

#define ZAN_DNS_TIMEOUT_MS 10000

typedef struct zan_blocking_job {
    struct zan_blocking_job *next;
    struct zan_blocking_job *queue_next;
    int64_t  deadline_ms;
    int      timed_out;
    /* Protected by dns_lock: timeout delivery owns the job through io_wake. */
    int      timeout_delivering;
    int      started;
    int64_t  result;        
/* 已解析的 IPv4 网络序地址 */
    void    *sabuf;
    int32_t  sacap;
    int32_t  port;
    void    *frame;
    zan_co_step_t step;
    int32_t *out;
    int64_t *out64;
    void (*run)(struct zan_blocking_job *job);
} zan_blocking_job_t;

typedef struct zan_dns_work {
    zan_blocking_job_t job;
    char hostname[256];
    /* 工作线程解析的 sockaddr 结果暂存区 */
    unsigned char sa_staging[sizeof(struct sockaddr_in6)];
} zan_dns_work_t;

static zan_blocking_job_t *g_blocking_pending; /* in flight, owned by workers */
static zan_blocking_job_t *g_blocking_done;
static zan_blocking_job_t *g_blocking_queued;
static int32_t g_blocking_queued_n;            /* length of g_blocking_queued */
static int32_t g_blocking_active;

#define ZAN_BLOCKING_MAX_ACTIVE 256
#define ZAN_BLOCKING_MAX_QUEUED 16384

static int64_t dns_now_ms(void) {
#if defined(_WIN32)
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

#if defined(_WIN32)
static SRWLOCK g_dns_srw = SRWLOCK_INIT;
static void dns_lock(void) { AcquireSRWLockExclusive(&g_dns_srw); }
static void dns_unlock(void) { ReleaseSRWLockExclusive(&g_dns_srw); }
#else
static pthread_mutex_t g_dns_mx = PTHREAD_MUTEX_INITIALIZER;
static void dns_lock(void) { pthread_mutex_lock(&g_dns_mx); }
static void dns_unlock(void) { pthread_mutex_unlock(&g_dns_mx); }
#endif

/* 空闲线程槽位启动排队任务 */
static void blocking_start_next(void);

static void blocking_spawn(zan_blocking_job_t *job);

static void blocking_finish_worker(zan_blocking_job_t *job) {
    zan_blocking_job_t *next = NULL;
    int discard = 0, notify = 0;
    dns_lock();
    if (job->timed_out) {
        job->started = 0;
        if (g_blocking_active > 0) g_blocking_active--;
        discard = !job->timeout_delivering;
    } else {
        zan_blocking_job_t **pp = &g_blocking_pending;
        while (*pp && *pp != job) pp = &(*pp)->next;
        if (!*pp) {
            discard = 1;
        } else {
            *pp = job->next;
            job->next = g_blocking_done;
            g_blocking_done = job;
            notify = 1;
            job->started = 0;
            if (g_blocking_active > 0) g_blocking_active--;
        }
    }
    if (g_blocking_active < ZAN_BLOCKING_MAX_ACTIVE && g_blocking_queued) {
        next = g_blocking_queued;
        g_blocking_queued = next->queue_next;
        next->queue_next = NULL;
        g_blocking_queued_n--;
        next->started = 1;
        g_blocking_active++;
    }
    dns_unlock();
    if (discard) free(job);
    if (notify) dns_wake_notify();
    if (next) blocking_spawn(next);
}

/* ---- 阻塞作业线程复用 ----
 * 曾经每个作业 pthread_create/CreateThread 一个线程、跑完即退：SQLite 的
 * QueryAsync 每行一次 sqlite3_step 卸载，列表页 ~4k rps 就是每秒 ~8 万次建
 * 线程。现在跑完作业的线程不退出，挂起等待下一个移交作业（最多
 * ZAN_BLOCKING_IDLE_MS），超时才退出；blocking_spawn 有空闲线程时直接移交，
 * 没有才新建。作业的计数/排队/超时语义完全不变：started 与 g_blocking_active
 * 仍按作业记，线程只是执行载体。移交链表复用 queue_next（已开始的作业不在
 * g_blocking_queued 里）。全部状态受 dns_lock 保护。 */
#define ZAN_BLOCKING_IDLE_MS 2000

static zan_blocking_job_t *g_blocking_handoff; /* started jobs awaiting a parked worker */
static int32_t g_blocking_idle;                /* parked workers not yet claimed */
#if defined(_WIN32)
static CONDITION_VARIABLE g_blocking_cv = CONDITION_VARIABLE_INIT;
#else
static pthread_cond_t g_blocking_cv = PTHREAD_COND_INITIALIZER;
#endif

/* 已持 dns_lock：挂起至多 ms 毫秒等移交。返回 0 表示超时。 */
static int blocking_park_locked(int64_t ms) {
#if defined(_WIN32)
    return SleepConditionVariableSRW(&g_blocking_cv, &g_dns_srw, (DWORD)ms, 0) ? 1 : 0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(ms / 1000);
    ts.tv_nsec += (long)((ms % 1000) * 1000000L);
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return pthread_cond_timedwait(&g_blocking_cv, &g_dns_mx, &ts) == 0 ? 1 : 0;
#endif
}

static void blocking_wake_parked(void) {
#if defined(_WIN32)
    WakeConditionVariable(&g_blocking_cv);
#else
    pthread_cond_signal(&g_blocking_cv);
#endif
}

/* 有空闲线程就把作业移交给它（返回 1），否则返回 0 由调用方新建线程。 */
static int blocking_handoff(zan_blocking_job_t *job) {
    int taken = 0;
    dns_lock();
    if (g_blocking_idle > 0) {
        g_blocking_idle--;
        job->queue_next = g_blocking_handoff;
        g_blocking_handoff = job;
        taken = 1;
        blocking_wake_parked();
    }
    dns_unlock();
    return taken;
}

/* 工作线程调度跳板入口：跑完一个作业后挂起等下一个移交，空闲超时退出。 */
#if defined(_WIN32)
static DWORD WINAPI blocking_worker(void *arg) {
#else
static void *blocking_worker(void *arg) {
#endif
    zan_blocking_job_t *job = (zan_blocking_job_t *)arg;
    while (job) {
        job->run(job);
        blocking_finish_worker(job);
        job = NULL;
        dns_lock();
        g_blocking_idle++;
        int64_t deadline = dns_now_ms() + ZAN_BLOCKING_IDLE_MS;
        for (;;) {
            if (g_blocking_handoff) {
                /* 移交方已替本线程扣过 g_blocking_idle。 */
                job = g_blocking_handoff;
                g_blocking_handoff = job->queue_next;
                job->queue_next = NULL;
                break;
            }
            int64_t left = deadline - dns_now_ms();
            if (left <= 0) {
                g_blocking_idle--;
                break;
            }
            (void)blocking_park_locked(left);
        }
        dns_unlock();
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void blocking_start_failed(zan_blocking_job_t *job) {
    int deliver = 0, discard = 0;
    dns_lock();
    zan_blocking_job_t **pp = &g_blocking_pending;
    while (*pp && *pp != job) pp = &(*pp)->next;
    if (*pp == job) {
        *pp = job->next;
        deliver = 1;
    }
    if (job->started && g_blocking_active > 0) g_blocking_active--;
    job->started = 0;
    job->result = 0;
    if (deliver) {
        job->next = g_blocking_done;
        g_blocking_done = job;
    }
    discard = !deliver && !job->timeout_delivering;
    dns_unlock();
    if (deliver) dns_wake_notify();
    if (discard) free(job);
    blocking_start_next();
}

static void blocking_spawn(zan_blocking_job_t *job) {
    if (blocking_handoff(job)) return;
#if defined(_WIN32)
    HANDLE h = CreateThread(NULL, 0, blocking_worker, job, 0, NULL);
    if (!h) {
        blocking_start_failed(job);
        return;
    }
    CloseHandle(h);
#else
    pthread_t tid;
    if (pthread_create(&tid, NULL, blocking_worker, job) != 0) {
        blocking_start_failed(job);
        return;
    }
    pthread_detach(tid);
#endif
}

static void blocking_start_next(void) {
    zan_blocking_job_t *next = NULL;
    dns_lock();
    if (g_blocking_active < ZAN_BLOCKING_MAX_ACTIVE && g_blocking_queued) {
        next = g_blocking_queued;
        g_blocking_queued = next->queue_next;
        next->queue_next = NULL;
        g_blocking_queued_n--;
        next->started = 1;
        g_blocking_active++;
    }
    dns_unlock();
    if (next) blocking_spawn(next);
}

static void blocking_submit(zan_blocking_job_t *job) {
    int start = 0, oversubscribed = 0;
    dns_lock();
    if (g_blocking_active >= ZAN_BLOCKING_MAX_ACTIVE &&
        g_blocking_queued_n >= ZAN_BLOCKING_MAX_QUEUED) {
        oversubscribed = 1;
    } else {
        job->next = g_blocking_pending;
        g_blocking_pending = job;
        InterlockedIncrement((volatile LONG *)&g_blocking_inflight);
        if (g_blocking_active < ZAN_BLOCKING_MAX_ACTIVE) {
            job->started = 1;
            g_blocking_active++;
            start = 1;
        } else {
            zan_blocking_job_t **tail = &g_blocking_queued;
            while (*tail) tail = &(*tail)->queue_next;
            *tail = job;
            g_blocking_queued_n++;
        }
    }
    dns_unlock();
    if (oversubscribed) {
        if (job->out) *job->out = 0;
        if (job->out64) *job->out64 = 0;
        io_wake(job->frame, job->step);
        free(job);
        return;
    }
    if (start) blocking_spawn(job);
}

static void dns_job_run(zan_blocking_job_t *job) {
    zan_dns_work_t *w = (zan_dns_work_t *)job;
    if (w->job.sabuf) {
        int32_t cap = w->job.sacap;
        if (cap < 0 || cap > (int32_t)sizeof w->sa_staging)
            cap = (int32_t)sizeof w->sa_staging;
        w->job.result = zan_io_resolve_sa(w->hostname, w->job.port,
                                          w->sa_staging, cap);
    } else {
        w->job.result = zan_io_resolve_ipv4(w->hostname);
    }
}

static int64_t dns_wait_ms(int64_t caller_ms) {
    if (g_blocking_inflight <= 0) return caller_ms;
    int64_t now = dns_now_ms();
    int64_t nearest = -1;
    dns_lock();
    for (zan_blocking_job_t *j = g_blocking_pending; j; j = j->next)
        if (j->deadline_ms > 0 &&
            (nearest < 0 || j->deadline_ms < nearest))
            nearest = j->deadline_ms;
    dns_unlock();
    if (nearest < 0) return caller_ms;
    int64_t wait = nearest - now;
    if (wait <= 0) wait = 0;
    if (caller_ms < 0) return wait;
    return wait < caller_ms ? wait : caller_ms;
}

/* 域名解析超时失败派发 */
static int dns_timeout_scan(void) {
    int woke = 0;
    int64_t now = dns_now_ms();
    zan_blocking_job_t *timed_out = NULL;
    dns_lock();
    zan_blocking_job_t **pp = &g_blocking_pending;
    while (*pp) {
        zan_blocking_job_t *j = *pp;
        if (j->deadline_ms <= 0 || j->deadline_ms > now) {
            pp = &j->next;
            continue;
        }
        *pp = j->next;
        j->timed_out = 1;
        j->timeout_delivering = 1;
        if (!j->started) {
            zan_blocking_job_t **qp = &g_blocking_queued;
            while (*qp && *qp != j) qp = &(*qp)->queue_next;
            if (*qp == j) {
                *qp = j->queue_next;
                g_blocking_queued_n--;
            }
            j->queue_next = NULL;
            j->next = timed_out;
            timed_out = j;
        } else {
            j->next = timed_out;
            timed_out = j;
        }
    }
    dns_unlock();
    while (timed_out) {
        zan_blocking_job_t *j = timed_out;
        timed_out = j->next;
        if (j->out) *j->out = 0;
        if (j->out64) *j->out64 = 0;
        io_wake(j->frame, j->step);
        dns_lock();
        j->timeout_delivering = 0;
        int discard = !j->started;
        dns_unlock();
        /* Either this delivery or the still-running worker owns destruction. */
        if (discard) free(j);
        InterlockedDecrement((volatile LONG *)&g_blocking_inflight);
        woke++;
    }
    return woke;
}

static void dns_wake_notify(void) {
#if defined(_WIN32)
    /* 空重叠结构标识 DNS 完成通知包 */
#if defined(ZAN_CO_DRIVER)
    for (int i = 0; i < (int)g_shards; i++)
        if (!PostQueuedCompletionStatus(io_shard(i), 0, (ULONG_PTR)-2, NULL))
            InterlockedExchange(&g_blocking_wake_lost, 1);
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)-2, NULL))
        InterlockedExchange(&g_blocking_wake_lost, 1);
#endif
#elif defined(__linux__)
    uint64_t one = 1;
    ssize_t r = write(g_dns_wake_fd, &one, sizeof(one));
    (void)r;
#else
    /* kqueue / select 后端的唤醒通道是 pipe：g_dns_wake_fd 是读端（登记在
     * 轮询器里），必须写写端 g_dns_wake_wfd。曾写读端——EBADF、唤醒全丢，
     * 每个 await 原生阻塞调用（SQLite 每行一次 step、DNS）都要等
     * ZAN_IO_SWEEP_MS 扫描才收割，单次往返 10–40ms。 */
    char c = 1;
    ssize_t r = write(g_dns_wake_wfd, &c, 1);
    (void)r;
#endif
}

static ZAN_MAYBE_UNUSED void dns_wake_read(void) {
#if defined(__linux__)
    uint64_t v;
    ssize_t r = read(g_dns_wake_fd, &v, sizeof(v));
    (void)r;
#elif !defined(_WIN32)
    char c[64];
    while (read(g_dns_wake_fd, c, sizeof(c)) > 0) {}
#endif
}

/* 派发所有已完成的域名解析结果至等待协程 */
static int dns_drain(void) {
    int woke = 0;
    zan_blocking_job_t *j;
    dns_lock();
    j = g_blocking_done;
    g_blocking_done = NULL;
    dns_unlock();
    while (j) {
        zan_blocking_job_t *n = j->next;
        if (j->out) *j->out = (int32_t)j->result;
        if (j->out64) *j->out64 = j->result;
        if (j->sabuf && j->sacap > 0 && j->result > 0) {
            size_t len = (size_t)j->result;
            if (len <= (size_t)j->sacap)
                memcpy(j->sabuf, ((zan_dns_work_t *)j)->sa_staging, len);
        }
        io_wake(j->frame, j->step);
        free(j);
        /* Keep the blocking owner until the ready continuation owns liveness. */
        InterlockedDecrement((volatile LONG *)&g_blocking_inflight);
        woke++;
        j = n;
    }
    return woke;
}

static void dns_shutdown_cleanup(void) {
    dns_lock();
    zan_blocking_job_t *j = g_blocking_done;
    g_blocking_done = NULL;
    while (j) {
        zan_blocking_job_t *n = j->next;
        free(j);
        j = n;
    }
    zan_blocking_job_t *queued = g_blocking_queued;
    g_blocking_queued = NULL;
    g_blocking_queued_n = 0;
    while (queued) {
        zan_blocking_job_t *n = queued->queue_next;
        zan_blocking_job_t **pp = &g_blocking_pending;
        while (*pp && *pp != queued) pp = &(*pp)->next;
        if (*pp == queued) *pp = queued->next;
        free(queued);
        queued = n;
    }
    for (zan_blocking_job_t *p = g_blocking_pending; p; p = p->next)
        p->timed_out = 1;
    g_blocking_pending = NULL;
    g_blocking_inflight = 0;
    dns_unlock();
}

void zan_io_resolve_sa_co(const char *name, int32_t port, void *buf,
                          int32_t cap, void *frame, zan_co_step_t step,
                          int32_t *out) {
    if (!name || !*name) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    zan_io_init();
    zan_dns_work_t *w = (zan_dns_work_t *)calloc(1, sizeof(*w));
    if (!w) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    strncpy(w->hostname, name, sizeof(w->hostname) - 1);
    w->hostname[sizeof(w->hostname) - 1] = 0;
    w->job.frame = frame;
    w->job.step = step;
    w->job.out = out;
    w->job.sabuf = buf;
    w->job.sacap = cap;
    w->job.port = port;
    w->job.deadline_ms = dns_now_ms() + ZAN_DNS_TIMEOUT_MS;
    w->job.run = dns_job_run;
    blocking_submit(&w->job);
}

void zan_io_resolve_co(const char *hostname, void *frame, zan_co_step_t step,
                       int32_t *out) {
    if (!hostname || !*hostname) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    zan_io_init();
    zan_dns_work_t *w = (zan_dns_work_t *)calloc(1, sizeof(*w));
    if (!w) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    strncpy(w->hostname, hostname, sizeof(w->hostname) - 1);
    w->hostname[sizeof(w->hostname) - 1] = 0;
    w->job.frame = frame;
    w->job.step = step;
    w->job.out = out;
    w->job.deadline_ms = dns_now_ms() + ZAN_DNS_TIMEOUT_MS;
    w->job.run = dns_job_run;
    blocking_submit(&w->job);
}

typedef struct zan_blocking_work {
    zan_blocking_job_t job;
    void *fn;
    int32_t argc;
    int64_t args[4];
} zan_blocking_work_t;

typedef int64_t (*zan_blocking_fn0)(void);
typedef int64_t (*zan_blocking_fn1)(int64_t);
typedef int64_t (*zan_blocking_fn2)(int64_t, int64_t);
typedef int64_t (*zan_blocking_fn3)(int64_t, int64_t, int64_t);
typedef int64_t (*zan_blocking_fn4)(int64_t, int64_t, int64_t, int64_t);
typedef void (*zan_blocking_void0)(void);
typedef void (*zan_blocking_void1)(int64_t);
typedef void (*zan_blocking_void2)(int64_t, int64_t);
typedef void (*zan_blocking_void3)(int64_t, int64_t, int64_t);
typedef void (*zan_blocking_void4)(int64_t, int64_t, int64_t, int64_t);

/* 编译器标量 ABI 调用规约适配 */
static void blocking_native_run(zan_blocking_job_t *base) {
    zan_blocking_work_t *w = (zan_blocking_work_t *)base;
    if (!w->fn || w->argc < 0 || w->argc > 4) {
        w->job.result = 0;
        return;
    }
    if (!w->job.out64) {
        switch (w->argc) {
        case 0: ((zan_blocking_void0)w->fn)(); break;
        case 1: ((zan_blocking_void1)w->fn)(w->args[0]); break;
        case 2: ((zan_blocking_void2)w->fn)(w->args[0], w->args[1]); break;
        case 3: ((zan_blocking_void3)w->fn)(w->args[0], w->args[1], w->args[2]); break;
        case 4: ((zan_blocking_void4)w->fn)(w->args[0], w->args[1], w->args[2], w->args[3]); break;
        }
        w->job.result = 0;
        return;
    }
    switch (w->argc) {
    case 0: w->job.result = ((zan_blocking_fn0)w->fn)(); break;
    case 1: w->job.result = ((zan_blocking_fn1)w->fn)(w->args[0]); break;
    case 2: w->job.result = ((zan_blocking_fn2)w->fn)(w->args[0], w->args[1]); break;
    case 3: w->job.result = ((zan_blocking_fn3)w->fn)(w->args[0], w->args[1], w->args[2]); break;
    case 4: w->job.result = ((zan_blocking_fn4)w->fn)(w->args[0], w->args[1], w->args[2], w->args[3]); break;
    }
}

void zan_rt_blocking_co(void *fn, int32_t argc,
                        int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                        void *frame, zan_co_step_t step, int64_t *out) {
    if (!fn || argc < 0 || argc > 4) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    zan_io_init();
    zan_blocking_work_t *w = (zan_blocking_work_t *)calloc(1, sizeof(*w));
    if (!w) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    w->fn = fn;
    w->argc = argc;
    w->args[0] = a0;
    w->args[1] = a1;
    w->args[2] = a2;
    w->args[3] = a3;
    w->job.frame = frame;
    w->job.step = step;
    w->job.out64 = out;
    w->job.run = blocking_native_run;
    blocking_submit(&w->job);
}



