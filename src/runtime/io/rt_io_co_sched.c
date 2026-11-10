/* rt_io_co_sched.c: M:N 协程调度引擎、工作线程池与时间片监控实现 */
/* UNITY-ORDER: after io/rt_io_co_pool.c —— co_sched 的跨平台原语依赖
 * co_pool 先拼入提供的 pthread→Win32 shim（scripts/unity_order_check.py 门禁）。 */

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
int zan_co_poll(void) {
    zan_co_worker_t *w = co_self();
    if (!w) return 0;
    if ((++w->poll_tick & (ZAN_POLL_GATE - 1)) != 0) return 0;
    long long q = zan_co_quantum_ms();
    if (q <= 0 || w->slice_start_us <= 0) return 0;
    long long now = zan_co_precise_us();
    return (now - w->slice_start_us >= q * 1000) ? 1 : 0;
}

static void co_stats_dump(void);

void zan_co_sched_init(void) {
    if (!g_co_inited) {
        InitializeCriticalSection(&g_co_lock);
        InitializeCriticalSection(&g_inj_lock);
        g_co_tls = TlsAlloc();
        atexit(co_stats_dump);
        g_co_inited = 1;
    }
    if (!g_wk) {
        g_wk = (zan_co_worker_t *)calloc(ZAN_CO_MAXW, sizeof(zan_co_worker_t));
    }
    g_co_workers = co_worker_count();
    g_rq_head = g_rq_tail = NULL;
    zan_timer_runtime_reset();
    zan_timer_set_ready_hook(zan_co_ready);
    g_co_running = 0;
    g_co_parked = 0;
    g_co_searching = 0;
    g_co_wake = 0;
    g_co_stop = 0;
    g_co_pool_live = 0;
    g_co_pool_gen = 0;
    g_io_shards_live = 0;
    g_timer_owner = 0;
    g_timer_pumped_us = 0;
    g_timer_next_ms = -1;
    EnterCriticalSection(&g_inj_lock);
    while (g_inj_head) {
        zan_co_node *n = g_inj_head;
        g_inj_head = n->next;
        n->next = g_inj_free;
        g_inj_free = n;
    }
    g_inj_tail = NULL;
    g_inj_len = 0;
    g_inj_push_ext = 0;
    LeaveCriticalSection(&g_inj_lock);
    for (int i = 0; i < ZAN_CO_MAXW; i++) {
        zan_co_worker_t *w = &g_wk[i];
        w->head = w->tail = 0;
        w->lifo_full = 0;
        w->lifo_budget = ZAN_LIFO_BUDGET;
        w->index = i;
        w->searching = 0;
        w->parked = 0;
        w->tick = 0;
        w->rng = 0x9e3779b9u ^ (unsigned)(i * 2654435761u);
        memset(&w->st, 0, sizeof(w->st));
        w->cnt_act = 0;
        w->cnt_out = 0;
    }
    g_co_cnt_ext.act = 0;
    g_co_cnt_ext.out = 0;
}


static int co_search_begin(zan_co_worker_t *w) {
    if (w->searching) return 1;
    LONG s = g_co_searching;
    if (2 * s >= g_co_workers) return 0;
    InterlockedIncrement(&g_co_searching);
    w->searching = 1;
    return 1;
}

static void co_search_end(zan_co_worker_t *w, int found) {
    if (!w->searching) return;
    w->searching = 0;
    if (InterlockedDecrement(&g_co_searching) == 0 && found) co_notify();
}

static int co_steal(zan_co_worker_t *w, zan_co_task *out) {
    if (g_co_workers <= 1) return 0;
    if (!co_search_begin(w)) return 0;
    for (int round = 0; round < ZAN_STEAL_ROUNDS; round++) {
        int start = (int)(co_rand(w) % (unsigned)g_co_workers);
        for (int k = 0; k < g_co_workers; k++) {
            int idx = (start + k) % g_co_workers;
            if (idx == w->index) continue;
            if (lq_steal(&g_wk[idx], w, out)) return 1;
        }
        if (inj_pop(out)) return 1;
    }
    return 0;
}

static int co_next_task(zan_co_worker_t *w, zan_co_task *out) {
    if (++w->tick % ZAN_GLOBAL_TICK == 0 && g_inj_len > 0) {
        zan_co_task batch[ZAN_INJ_BATCH];
        int n = inj_pop_batch(batch, ZAN_INJ_BATCH);
        if (n > 0) {
            w->lifo_budget = ZAN_LIFO_BUDGET;
            w->st.inj_pop += (unsigned long long)n;
            for (int i = 1; i < n; i++) lq_push(w, batch[i]);
            *out = batch[0];
            return 1;
        }
    }
    if (w->lifo_full) {
        if (w->lifo_budget > 0) {
            *out = w->lifo;
            w->lifo_full = 0;
            w->lifo_budget--;
            w->st.lifo_hit++;
            return 1;
        }
        lq_push(w, w->lifo);
        w->lifo_full = 0;
        w->st.lifo_demote++;
    }
    w->lifo_budget = ZAN_LIFO_BUDGET;
    if (lq_pop(w, out)) { w->st.lq_pop++; return 1; }
    if (inj_pop(out)) { w->st.inj_pop++; return 1; }
    if (co_steal(w, out)) { w->st.steal_ok++; return 1; }
    w->st.steal_fail++;
    return 0;
}

static int co_has_runnable(void) {
    if (g_inj_len > 0) return 1;
    for (int i = 0; i < g_co_workers; i++) {
        zan_co_worker_t *w = &g_wk[i];
        if (w->lifo_full) return 1;
        if (__atomic_load_n(&w->tail, __ATOMIC_ACQUIRE) !=
            __atomic_load_n(&w->head, __ATOMIC_ACQUIRE)) return 1;
    }
    return 0;
}

static long long co_pump_timers(void);

static void co_run(zan_co_worker_t *w, zan_co_task *t) {
    zan_co_fhdr *h = co_hdr(t->frame);
    long long s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
    for (;;) {
        if (s & CO_DEAD) {
            free(t->frame);
            co_cnt_add(w, +1, -1);
            return;
        }
        long long n = (s & ~(CO_QUEUED | CO_NOTIFIED | CO_SELFQ)) | CO_RUNNING;
        if (__atomic_compare_exchange_n(&h->sched, &s, n, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    InterlockedIncrement(&g_co_running);
    w->st.ran++;
    co_trace("run", t->frame);
    co_pump_timers();
    /* 嵌套运行要保住外层的 cur 与 slice_start_us。
     *
     * cur：zan_co_ready 用 (sw->cur == frame) 判断"是不是自己唤醒自己"
     *   —— 命中挂 CO_SELFQ（重排走环尾 = 真让出），没命中当普通唤醒塞进
     *   LIFO cell（下一个就跑自己 = 没让出）。内层 co_run 若把 cur 清成
     *   NULL，外层协程的自唤醒就被误判成普通唤醒，Task.Yield 变成空转，
     *   排队的协程被压住。
     *
     * slice_start_us：抢占配额的起点。内层覆盖后，外层任务已跑的时长
     *   被清零，配额判定跟着失真（外层再也攒不满时间片）。恢复成外层
     *   自己的起点，与"非嵌套时一次设定、整个任务期间不变"的语义一致。
     *
     * 嵌套是会发生的：同步等待 zan_co_sync_sleep 要在协程内部顶上 worker
     * 的活（把被定时器唤醒的协程取出来跑），见 co_run_ready_tasks。 */
    void *prev_cur = w->cur;
    long long prev_slice = w->slice_start_us;
    w->slice_start_us = zan_co_precise_us();
    w->cur = t->frame;
    t->step(t->frame);
    w->cur = prev_cur;
    w->slice_start_us = prev_slice;
    co_trace("done", t->frame);
    for (;;) {
        s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
        if (s & CO_DEAD) { free(t->frame); break; }
        if (s & CO_NOTIFIED) {
            zan_co_step_t step = __atomic_load_n(&h->pending, __ATOMIC_RELAXED);
            int selfq = (s & CO_SELFQ) != 0;
            if (!__atomic_compare_exchange_n(&h->sched, &s, CO_QUEUED, 0,
                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                continue;
            if (selfq) {
                zan_co_task rq;
                rq.frame = t->frame;
                rq.step = step ? step : t->step;
                co_cnt_add(w, +1, +1);
                lq_push(w, rq);
                w->st.selfq++;
            } else {
                co_submit(t->frame, step ? step : t->step);
            }
            break;
        }
        if (__atomic_compare_exchange_n(&h->sched, &s, 0LL, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    InterlockedDecrement(&g_co_running);
    co_cnt_add(w, +1, -1);
}

/* 派发到期定时器并返回下一截止剩余毫秒数 */
static long long co_pump_timers_now(void) {
    if (InterlockedCompareExchange(&g_timer_owner, 1, 0) != 0) {
        long long live = zan_timer_next_timeout();
        if (live > 0) return live;
        long long cached = g_timer_next_ms;
        return (cached <= 0) ? 1 : cached;
    }
    InterlockedIncrement(&g_co_running);
    zan_timer_dispatch_due();
    long long next = zan_timer_next_timeout();
    g_timer_next_ms = next;
    g_timer_pumped_us = zan_co_precise_us();
    InterlockedDecrement(&g_co_running);
    InterlockedExchange(&g_timer_owner, 0);
    return next;
}

static long long co_pump_timers(void) {
    long long now = zan_co_precise_us();
    if (now - g_timer_pumped_us >= 1000)
        return co_pump_timers_now();
    long long cached = g_timer_next_ms;
    return (cached == 0) ? 1 : cached;
}

/* 阻塞在完成端口等待 I/O 就绪并恢复协程 */
static void co_wait_io(zan_co_worker_t *w, long long timeout_ms) {
    if (timeout_ms > 0x7FFFFFFF) timeout_ms = 0x7FFFFFFF;
    DWORD to = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
#if defined(_WIN32)
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    if (g_blocking_wake_lost && InterlockedExchange(&g_blocking_wake_lost, 0)) {
        if (dns_drain() > 0) return;
    }
    BOOL ok = GetQueuedCompletionStatusEx(io_shard(co_worker_shard(w->index)),
                                          entries, 64, &removed, to, FALSE);
    InterlockedExchange(&w->parked, 0);
    InterlockedDecrement(&g_co_parked);
    if (!ok) {
        /* Timeout or error */
        dns_timeout_scan();
        rto_timeout_scan();
        dns_drain();
        return;
    }
    zan_co_task_t ready_batch[64];
    size_t ready_count = 0;
    for (ULONG i = 0; i < removed; i++) {
        if (entries[i].lpOverlapped == NULL) {
            if (entries[i].lpCompletionKey == ZAN_WAKE_KEY) {
                if (g_co_wake > 0) InterlockedDecrement(&g_co_wake);
            } else {
                dns_drain();
            }
            continue;
        }
        zan_io_op_t *op = CONTAINING_RECORD(entries[i].lpOverlapped,
                                            zan_io_op_t, ov);
        void *co = op->co;
        zan_co_step_t step = op->step;
        if (op->rto == 2) {
            IOTRACE("poll_op rto-dead op=%p kind=%d", (void*)op, op->kind);
            op_free(op);
            IO_CNT_DEC();
            continue;
        }
        if (op->rto && !rto_claim(co)) {
            IOTRACE("poll_op rto-late op=%p kind=%d", (void*)op, op->kind);
            op_free(op);
            IO_CNT_DEC();
            continue;
        }
        IOTRACE("poll_op op=%p kind=%d bytes=%lu status=%llu", (void*)op, op->kind, (unsigned long)entries[i].dwNumberOfBytesTransferred, (unsigned long long)entries[i].Internal);
        io_complete_op(op, entries[i].dwNumberOfBytesTransferred,
                       entries[i].Internal);
        op_free(op);
        IO_CNT_DEC();
        if (step && co) {
            ready_batch[ready_count].frame = co;
            ready_batch[ready_count].step = step;
            ready_count++;
        }
    }
    if (ready_count > 0) {
        zan_co_ready_batch(ready_batch, ready_count);
    }
#else
    (void)w;
    STRACE("w%d waitio enter to=%lld haspend=%d", w->index, (long long)to, zan_io_has_pending());
    int polled = zan_io_pump_timeout(to == INFINITE ? -1 : (int64_t)to);
    BOOL ok = (polled >= 0);
    if (g_co_wake > 0) InterlockedDecrement(&g_co_wake);
    InterlockedExchange(&w->parked, 0);
    InterlockedDecrement(&g_co_parked);
    STRACE("w%d waitio ret=%d ok=%d", w->index, polled, (int)ok);
    if (!ok) {
        dns_timeout_scan();
        zan_io_shard_t *psh = &g_ioshard[io_thread_shard()];
        shard_lock(psh);
        rto_timeout_scan(psh);
        shard_unlock(psh);
        dns_drain();
    }
#endif
}

static int co_all_idle(void) {
    if (co_cnt_outstanding() != 0 || g_co_running != 0 ||
        g_io_count != 0 || g_blocking_inflight != 0 || zan_timer_pending() != 0)
        return 0;
    int idle;
    EnterCriticalSection(&g_co_lock);
    idle = !(zan_timer_pending() != 0 || co_cnt_outstanding() != 0 ||
             g_co_running != 0 || g_io_count != 0 ||
             g_blocking_inflight != 0 || co_has_runnable());
    LeaveCriticalSection(&g_co_lock);
    return idle;
}

static void co_pool_retire(zan_co_worker_t *w);

#if defined(_WIN32)
static volatile LONG g_co_timeperiod_done = 0;
static void co_timeperiod_raise(void) {
    if (InterlockedExchange(&g_co_timeperiod_done, 1)) return;
    HMODULE mm = LoadLibraryA("winmm.dll");
    if (!mm) return;
    ULONG (WINAPI *begin)(UINT) =
        (ULONG (WINAPI *)(UINT))(void *)GetProcAddress(mm, "timeBeginPeriod");
    if (begin) begin(1);
}
#else
static void co_timeperiod_raise(void) { }
#endif

/* 跑掉当前 worker 队列里就绪的任务。
 *
 * 同步等待期间（zan_co_sync_sleep）必须做这件事：派发到期定时器只是把
 * 协程标记成"就绪"并入队，没人执行它它就不会跑。单 worker 下唯一的
 * worker 正卡在同步等待里，所以等待者自己得顶上 worker 的活。
 *
 * 一次只跑一个就够：跑完再回到等待循环，下一轮继续派发+取任务。
 * 限制条数是为了不让一个长任务把整个同步等待饿死（那是另一个极端的
 * 饥饿：等待迟迟不结束）。
 */
static void co_run_ready_tasks(void) {
    zan_co_worker_t *w = co_self();
    if (!w) return;
    for (int i = 0; i < 64; i++) {
        zan_co_task t;
        if (!co_next_task(w, &t)) return;
        co_run(w, &t);
    }
}

/* 同步上下文里的定时等待：等 ms 毫秒，期间**继续派发到期定时器**。
 *
 * 背景（A470）：编译器把非 async 方法里的 `await Task.Delay(ms)` 降级成
 * 同步休眠。原来的实现是裸的 Sleep / poll——**整段占住调用线程**：
 * 单 worker 下，这段时间里其它协程的定时器要等它结束才能被派发。
 * 实测：主协程同步睡 500ms，另一个协程 200ms 的定时器到 531ms 才到期
 * （应 200ms）。
 *
 * 修法：把一次长睡拆成**按下一个定时器截止时间切片**的等待，每片醒来
 * 做两件事：派发到期定时器，再把被唤醒的协程跑起来。于是 200ms 的定时器
 * 在 200ms 准时到期，500ms 的同步等待本身也照样等到 500ms。
 *
 * 实测（单 worker）：一个协程在 async 内部同步睡 500ms，另一个协程
 * 200ms 的定时器从 503ms（改前）回到 204ms（改后）。
 *
 * 派发用 zan_timer_dispatch_due_wait（rt_timer.c）：它对 delay 定时器调
 * ready 钩子把协程唤醒，跨线程安全。
 */
void zan_co_sync_sleep(long long ms) {
    if (ms <= 0) return;
    long long deadline = zan_co_precise_us() + ms * 1000;
    for (;;) {
        /* 1) 派发到期定时器——同步等待期间，别的协程的定时器照样按时
         * 被唤醒。用 _wait 变体：别的线程正在派发时**等它结束**再派
         *（普通版会跳过，跳过就等于没修）。 */
        zan_timer_dispatch_due_wait();

        /* 2) **把被唤醒的协程跑起来**——这一步不能少。派发只是把协程
         * 标记成"就绪"并入队；没人去执行它，它照样不会跑。单 worker
         * 下唯一的 worker 正卡在这次同步等待里，所以这里要自己顶上
         * worker 的活：取一个就绪任务跑起来。
         * 多 worker 时这段是多余的（别的 worker 会跑），但跑掉也无害——
         * 协程本来就要有人跑。 */
        co_run_ready_tasks();

        long long remain_us = deadline - zan_co_precise_us();
        if (remain_us <= 0) return;

        /* 3) 下一个定时器的截止：睡到它为止，醒来后先把它派发掉再继续
         * 睡。没有定时器时（next < 0）就一路睡到 deadline。 */
        long long next_ms = zan_timer_next_timeout();
        long long slice_ms = (remain_us + 999) / 1000;
        if (next_ms >= 0 && next_ms < slice_ms) slice_ms = next_ms;
        if (slice_ms <= 0) slice_ms = 1;
        /* zan_io_pump_timeout 内部把超时夹在 int32 范围内，这里同理。 */
        if (slice_ms > 0x7FFFFFFF) slice_ms = 0x7FFFFFFF;

        zan_io_pump_timeout(slice_ms);
    }
}

static void co_worker(int worker) {
    co_timeperiod_raise();
    zan_co_worker_t *w = &g_wk[worker];
    w->bg_gen = g_co_pool_gen;
    if (g_co_tls != TLS_OUT_OF_INDEXES) TlsSetValue(g_co_tls, w);
    LONG idle_seen = -1;
    for (;;) {
        if (g_co_stop) { co_pool_retire(w); return; }
        /* 队列繁忙时持续推进到期定时器 */
        if (zan_timer_pending() > 0) co_pump_timers();
        zan_co_task t;
        if (co_next_task(w, &t)) {
            co_search_end(w, 1);
            idle_seen = -1;
            co_run(w, &t);
            continue;
        }
        long long tnext = co_pump_timers_now();
        if (co_next_task(w, &t)) {
            co_search_end(w, 1);
            idle_seen = -1;
            co_run(w, &t);
            continue;
        }
        co_search_end(w, 0);
        if (co_all_idle()) {
            LONG seq = co_cnt_activity();
            if (idle_seen == seq) {
                g_co_stop = 1;
                for (int i = 0; i < g_co_workers; i++)
                    co_wake_shard(co_worker_shard(i));
                co_pool_retire(w);
                return;
            }
            idle_seen = seq;
            Sleep(1);
            continue;
        }
        idle_seen = -1;
        long long to;
        if (tnext >= 0)               to = tnext;
        else if (g_io_count > 0)      to = 1000;
        else                          to = 50;
        if (g_blocking_inflight > 0) {
            long long dw = dns_wait_ms(-1);
            if (dw >= 0 && dw < to) to = dw;
        }
#if defined(_WIN32)
        {
            long long rw = rto_wait_ms(-1);
            if (rw >= 0 && rw < to) to = rw;
        }
#endif
        InterlockedIncrement(&g_co_parked);
        InterlockedExchange(&w->parked, 1);
        w->st.park++;
        long long tnow = zan_timer_next_timeout();
        if (tnow >= 0 && tnow < to) to = tnow;
        if (tnow == 0 || co_has_runnable()) {
            InterlockedExchange(&w->parked, 0);
            InterlockedDecrement(&g_co_parked);
            STRACE("w%d park-skip tnow=%lld runnable=%d", w->index, tnow, co_has_runnable());
            continue;
        }
        STRACE("w%d park to=%lld tnext=%lld io=%d blk=%d dead=%d parked=%d stop=%d",
               w->index, to, tnext, (int)g_io_count, (int)g_blocking_inflight,
#if !defined(_WIN32)
               (int)g_io_dead_count,
#else
               -1,
#endif
               (int)g_co_parked, (int)g_co_stop);
        co_wait_io(w, to);
    }
}

static void co_stats_dump(void) {
    const char *e = getenv("ZAN_CO_STATS");
    if (!e || !*e || e[0] == '0') return;
    zan_co_stats_t tot;
    memset(&tot, 0, sizeof(tot));
    for (int i = 0; i < g_co_workers; i++) {
        zan_co_stats_t *s = &g_wk[i].st;
        fprintf(stderr,
                "COSTAT worker=%d ran=%llu lifo_put=%llu lifo_hit=%llu "
                "lifo_demote=%llu lq_push=%llu lq_pop=%llu spill=%llu "
                "inj_push=%llu inj_pop=%llu steal_ok=%llu steal_fail=%llu "
                "park=%llu wake_post=%llu selfq=%llu\n",
                i, s->ran, s->lifo_put, s->lifo_hit, s->lifo_demote,
                s->lq_push, s->lq_pop, s->spill, s->inj_push, s->inj_pop,
                s->steal_ok, s->steal_fail, s->park, s->wake_post, s->selfq);
        tot.ran += s->ran;                 tot.lifo_put += s->lifo_put;
        tot.lifo_hit += s->lifo_hit;       tot.lifo_demote += s->lifo_demote;
        tot.lq_push += s->lq_push;         tot.lq_pop += s->lq_pop;
        tot.spill += s->spill;             tot.inj_push += s->inj_push;
        tot.inj_pop += s->inj_pop;         tot.steal_ok += s->steal_ok;
        tot.steal_fail += s->steal_fail;   tot.park += s->park;
        tot.wake_post += s->wake_post;     tot.selfq += s->selfq;
    }
    fprintf(stderr,
            "COSTAT total workers=%d shards=%d ran=%llu lifo_put=%llu lifo_hit=%llu "
            "lifo_demote=%llu lq_push=%llu lq_pop=%llu spill=%llu "
            "inj_push=%llu inj_pop=%llu steal_ok=%llu steal_fail=%llu "
            "park=%llu wake_post=%llu selfq=%llu inj_push_ext=%ld sync_inline=%ld\n",
            g_co_workers, (int)g_shards, tot.ran, tot.lifo_put, tot.lifo_hit,
            tot.lifo_demote, tot.lq_push, tot.lq_pop, tot.spill,
            tot.inj_push, tot.inj_pop, tot.steal_ok, tot.steal_fail,
            tot.park, tot.wake_post, tot.selfq,
            (long)g_inj_push_ext, (long)g_sync_inline);
    fflush(stderr);
}

static DWORD WINAPI co_worker_thunk(LPVOID p) {
    co_worker((int)(uintptr_t)p);
    return 0;
}

/* 启动后台工作线程池（进程单例） */
static void co_pool_start_background(void) {
    zan_io_init();
    g_co_stop = 0;
    g_co_workers = co_worker_count();
    int w = g_co_workers;
    if (!g_io_shards_live) { io_shards_start(w); g_io_shards_live = 1; }
    g_co_pool_gen = g_co_pool_gen + 1;
    g_co_pool_out = w;
    co_trace("start", NULL);
    int made = 0;
    for (int i = 0; i < w; i++) {
        g_wk[i].bg_gen = g_co_pool_gen;
#if defined(_WIN32)
        HANDLE th = CreateThread(NULL, 0, co_worker_thunk,
                                 (LPVOID)(uintptr_t)i, 0, NULL);
        if (th) { CloseHandle(th); made++; }
#else
        pthread_t th;
        if (pthread_create(&th, NULL, (void*(*)(void*))co_worker_thunk, (void*)(uintptr_t)i) == 0) {
            pthread_detach(th);
            made++;
        }
#endif
    }
    g_co_pool_out = made;
    if (made == 0) {
        g_co_pool_live = 0;
    }
}

static void co_pool_retire(zan_co_worker_t *w) {
    co_frame_pool_flush_tls();
    if (w->bg_gen != g_co_pool_gen || g_co_pool_gen == 0) { return; }
    co_trace("retire", w);
    if (InterlockedDecrement(&g_co_pool_out) != 0) { return; }
    EnterCriticalSection(&g_inj_lock);
    if (g_inj_len > 0) {
        LeaveCriticalSection(&g_inj_lock);
        co_pool_start_background();
        return;
    }
    g_co_pool_live = 0;
    LeaveCriticalSection(&g_inj_lock);
}

static void co_pool_ensure(void) {
    if (g_co_pool_live || g_co_pool_fg) return;
    if (InterlockedCompareExchange(&g_co_pool_live, 1, 0) == 0) {
        co_pool_start_background();
    }
}

void zan_co_sched_run(void) {
    if (g_co_pool_live || InterlockedCompareExchange(&g_co_pool_fg, 1, 0) != 0) {
        for (;;) {
            LONG activity = co_cnt_activity();
            int busy = zan_io_has_pending() || zan_timer_pending() > 0 ||
                InterlockedCompareExchange(&g_co_running, 0, 0) > 0 ||
                co_cnt_outstanding() > 0;
            if (!busy && activity == co_cnt_activity()) break;
            Sleep(1);
            static __thread DWORD stuck_since = 0;
            DWORD nowk = GetTickCount();
            if (stuck_since == 0) { stuck_since = nowk; continue; }
            if (nowk - stuck_since > 3000) {
                co_trace_dump(zan_co_live_count());
                stuck_since = nowk;
            }
        }
        return;
    }
    zan_io_init();
    g_co_workers = co_worker_count();
    int w = g_co_workers;
    g_co_stop = 0;
    io_shards_start(w);

#if defined(_WIN32)
    HANDLE th[ZAN_CO_MAXW]; int nt = 0;
    for (int i = 1; i < w; i++) {
        th[nt] = CreateThread(NULL, 0, co_worker_thunk,
                              (LPVOID)(uintptr_t)i, 0, NULL);
        if (th[nt]) nt++;
    }
    co_worker(0);
    for (int i = 0; i < nt; i++) {
        WaitForSingleObject(th[i], INFINITE);
        CloseHandle(th[i]);
    }
#else
    pthread_t th[ZAN_CO_MAXW]; int nt = 0;
    for (int i = 1; i < w; i++) {
        if (pthread_create(&th[nt], NULL, (void*(*)(void*))co_worker_thunk, (void*)(uintptr_t)i) == 0) {
            nt++;
        }
    }
    co_worker(0);
    for (int i = 0; i < nt; i++) {
        pthread_join(th[i], NULL);
    }
#endif
    if (InterlockedCompareExchange(&g_co_pool_live, 1, 0) != 0) {
        return;
    }
    InterlockedExchange(&g_co_pool_fg, 0);
    if (!co_all_idle() || g_inj_len > 0) {
        g_co_pool_live = 0;
        co_pool_ensure();
        return;
    }
    co_stats_dump();
    zan_io_shutdown();
    g_co_pool_live = 0;
    if (g_inj_len > 0 || !co_all_idle()) co_pool_ensure();
}

void zan_co_sched_run_until(const volatile int *done) {
    if ((g_co_pool_live || g_co_pool_fg) && done != NULL) {
        /* 从 worker 线程进来：这个调用点在等一个**子协程**跑完（根驱动
         * await：非 async 方法里 await 一个 async 调用，编译器分配子帧后
         * 让当前线程把它驱动到完成）。
         *
         * 这里**不能自旋等 done**。子协程是 co_submit 提交的，而 co_submit
         * 从 worker 线程提交时把任务放进**调用者自己那个 worker 的 LIFO
         * 槽**（w->lifo）——别的 worker 偷不到（co_steal 偷的是别人的
         * 队列，不偷 lifo 槽）。当前 worker 正卡在这个自旋里，压根没机会
         * 去取那个 LIFO 槽，于是子协程永远跑不起来，done 永远不置位：
         * 死锁。worker 数再多也一样（实测 1/2/4/8 全卡）。
         *
         * 正确做法：等的时候**自己顶上 worker 的活**——就地取就绪任务跑
         * 起来（同 co_run_ready_tasks 的思路，A470 的 zan_co_sync_sleep
         * 就是这么等的）。子协程跑完，done 置位，等待结束。
         *
         * 非 worker 线程（主线程）进来时没有自己的队列，自旋是对的：
         * 子协程会被别的 worker 跑掉。 */
        zan_co_worker_t *w = co_self();
        if (w) {
            int spins = 0;
            for (;;) {
                if (__atomic_load_n(done, __ATOMIC_ACQUIRE) != 0) return;
                /* 就地跑就绪任务：子协程多半就在本 worker 的 LIFO 槽里 */
                zan_co_task t;
                if (co_next_task(w, &t)) {
                    co_run(w, &t);
                    spins = 0;
                    continue;
                }
                /* 队列空：派发到期定时器（子协程可能在等定时器） */
                if (zan_timer_pending() > 0) co_pump_timers();
                if (__atomic_load_n(done, __ATOMIC_ACQUIRE) != 0) return;
                if (spins < 512) { Sleep(0); spins++; }
                else Sleep(1);
            }
        }
        int spins = 0;
        while (__atomic_load_n(done, __ATOMIC_ACQUIRE) == 0) {
            if (spins < 512) { Sleep(0); spins++; }
            else Sleep(1);
        }
        return;
    }
    (void)done;
    zan_co_sched_run();
}

size_t zan_co_pending(void) {
    size_t n = (size_t)(g_inj_len > 0 ? g_inj_len : 0);
    for (int i = 0; i < g_co_workers; i++) {
        zan_co_worker_t *w = &g_wk[i];
        long long h = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&w->tail, __ATOMIC_ACQUIRE);
        if (t > h) n += (size_t)(t - h);
        if (w->lifo_full) n++;
    }
    return n;
}
#else

void zan_co_sched_init(void) {
    g_rq_head = g_rq_tail = NULL;
    zan_timer_runtime_reset();
    zan_timer_set_ready_hook(zan_co_ready);
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step) return;
    zan_co_node *n = (zan_co_node *)malloc(sizeof(*n));
    if (!n) {
        step(frame);
        return;
    }
    n->next = NULL; n->frame = frame; n->step = step;
    if (g_rq_tail) g_rq_tail->next = n; else g_rq_head = n;
    g_rq_tail = n;
}

void zan_co_ready_batch(const zan_co_task_t *tasks, size_t count) {
    if (!tasks || count == 0) return;
    for (size_t i = 0; i < count; i++) {
        zan_co_ready(tasks[i].frame, tasks[i].step);
    }
}

void zan_co_delay(long long ms, void *frame, zan_co_step_t step) {
    zan_timer_delay(ms, frame, step);
}

void *__zan_co_frame_alloc(size_t size) {
    return calloc(1, size ? size : 1);
}

void __zan_co_frame_free(void *frame) { free(frame); }

size_t zan_co_pending(void) {
    size_t n = 0;
    for (zan_co_node *p = g_rq_head; p; p = p->next) n++;
    return n;
}

void zan_co_sched_run_until(const volatile int *done) {
    for (;;) {
        while (g_rq_head) {
            if (done && __atomic_load_n(done, __ATOMIC_ACQUIRE) != 0) return;
            zan_co_node *n = g_rq_head;
            g_rq_head = n->next; if (!g_rq_head) g_rq_tail = NULL;
            void *frame = n->frame; zan_co_step_t step = n->step;
            free(n);
            step(frame);
        }
        if (done && __atomic_load_n(done, __ATOMIC_ACQUIRE) != 0) return;
        long long timeout = zan_timer_next_timeout();
        if (timeout >= 0) {
            if (timeout > 0) {
                zan_io_pump_timeout(timeout);
                continue;
            }
            zan_timer_dispatch_due();
            continue;
        }
        if (zan_io_pump() > 0) continue;
        return;
    }
}

void zan_co_sched_run(void) {
    zan_co_sched_run_until(NULL);
}
#endif /* _WIN32 */
