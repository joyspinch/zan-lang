/* rt_io_kqueue.c: Darwin (macOS) 与 FreeBSD 原生 kqueue 事件多路复用反应堆实现 */

#if !defined(__APPLE__) && !defined(__FreeBSD__)
#error "rt_io_kqueue.c is only intended for Darwin and FreeBSD targets"
#endif

static int io_shard_open(int i) {
    io_shards_prime();
    zan_io_shard_t *sh = &g_ioshard[i];
    if (sh->poll_fd >= 0) return 1;
    sh->poll_fd = kqueue();
    if (sh->poll_fd < 0) return 0;
    zan_io_fd_cloexec(sh->poll_fd);
    int pfd[2];
    if (pipe(pfd) == 0) {
        sh->wake_rfd = pfd[0];
        sh->wake_wfd = pfd[1];
        fcntl(pfd[0], F_SETFL, O_NONBLOCK);
        zan_io_fd_cloexec(pfd[0]);
        zan_io_fd_cloexec(pfd[1]);
        struct kevent kev;
        EV_SET(&kev, (uintptr_t)pfd[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
        kevent(sh->poll_fd, &kev, 1, NULL, 0, NULL);
    }
    if (i == 0 && g_dns_wake_fd < 0) {
        if (pipe(pfd) == 0) {
            g_dns_wake_fd = pfd[0];
            g_dns_wake_wfd = pfd[1];
            fcntl(pfd[0], F_SETFL, O_NONBLOCK);
            zan_io_fd_cloexec(pfd[0]);
            zan_io_fd_cloexec(pfd[1]);
            struct kevent kev;
            EV_SET(&kev, (uintptr_t)pfd[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
            kevent(sh->poll_fd, &kev, 1, NULL, 0, NULL);
        }
    }
    return 1;
}

static void io_init_locked(void) {
    if (g_io_started) return;
    io_shard_mutexes_prime();
    zan_io_ignore_sigpipe();
    g_io_broken = 0;
    g_io_entries = NULL;
    g_io_count = 0;
    if (!io_shard_open(0))
        g_io_broken = 1;
    g_io_started = 1;
}

void zan_io_init(void) {
    if (g_io_started) return;
    IO_INIT_LOCK();
    io_init_locked();
    IO_INIT_UNLOCK();
}

static void io_shutdown_locked(void) {
    if (g_slots) {
        for (int fd = 0; fd < g_slots_cap; fd++) {
            zan_io_slot_t *s = &g_slots[fd];
            zan_io_waiter_t *x = s->r.next;
            while (x) { zan_io_waiter_t *n = x->next; free(x); x = n; }
            x = s->w.next;
            while (x) { zan_io_waiter_t *n = x->next; free(x); x = n; }
        }
        free(g_slots);
        g_slots = NULL;
        g_slots_cap = 0;
    }
    g_io_count = 0;
    int n = io_shard_count();
    for (int i = 0; i < n; i++) {
        zan_io_shard_t *sh = &g_ioshard[i];
        io_dead_clear(sh);
        while (sh->rto) { zan_io_rto_t *e = sh->rto; sh->rto = e->next; free(e); }
    }
    dns_shutdown_cleanup();
    for (int i = 0; i < n; i++) {
        zan_io_shard_t *sh = &g_ioshard[i];
        if (sh->poll_fd >= 0) { close(sh->poll_fd); sh->poll_fd = -1; }
        if (sh->wake_rfd >= 0) { close(sh->wake_rfd); sh->wake_rfd = -1; }
        if (sh->wake_wfd >= 0) { close(sh->wake_wfd); sh->wake_wfd = -1; }
    }
    if (g_dns_wake_fd >= 0) { close(g_dns_wake_fd); g_dns_wake_fd = -1; }
    if (g_dns_wake_wfd >= 0) { close(g_dns_wake_wfd); g_dns_wake_wfd = -1; }
    g_io_started = 0;
    g_ioshard_prim = 0;
#if defined(ZAN_CO_DRIVER)
    InterlockedExchange(&g_shards, 1);
#endif
    g_io_shards_live = 0;
}

void zan_io_shutdown(void) {
    int n = io_shard_count();
    for (int i = 0; i < n; i++) shard_lock(&g_ioshard[i]);
    io_shutdown_locked();
    for (int i = n - 1; i >= 0; i--) shard_unlock(&g_ioshard[i]);
}

static void io_register_locked(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    if (g_io_broken) {
        io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        return;
    }
    if (io_reject_dead_fd(fd, co, step)) return;
    if ((int)fd >= g_slots_cap) {
        shard_unlock(sh);
        int ok = io_slots_ensure((int)fd + 1);
        shard_lock(sh);
        if (!ok) {
            io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
            g_pending_rbuf = NULL;
            g_pending_out_n = NULL;
            g_pending_accept_out = NULL;
            return;
        }
    }
    zan_io_slot_t *s = io_slot((int)fd);
    if (!s) {
        io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        return;
    }
    zan_io_waiter_t *w;
    unsigned char *has = (interest == ZAN_IO_READ) ? &s->has_r : &s->has_w;
    zan_io_waiter_t *inl = (interest == ZAN_IO_READ) ? &s->r : &s->w;
    if (!*has) {
        w = inl;
        w->next = NULL;
        *has = 1;
    } else {
        w = (zan_io_waiter_t *)calloc(1, sizeof(*w));
        if (!w) {   
            io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
            g_pending_rbuf = NULL;
            g_pending_out_n = NULL;
            g_pending_accept_out = NULL;
            return;
        }
        zan_io_waiter_t *tail = inl;
        while (tail->next) tail = tail->next;
        tail->next = w;
    }
    w->co = co;
    w->step = step;
    w->rbuf = g_pending_rbuf;
    w->rlen = g_pending_rlen;
    w->out_n = g_pending_out_n;
    w->out_accept = g_pending_accept_out;
    {
        int64_t *fail_out_n = g_pending_out_n;
        intptr_t *fail_accept = g_pending_accept_out;
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        InterlockedIncrement(&g_io_count);

        struct kevent kev;
        short filter = (interest == ZAN_IO_READ) ? EVFILT_READ : EVFILT_WRITE;
        EV_SET(&kev, fd, filter, EV_ADD | EV_ONESHOT, 0, 0, NULL);
        if (kevent(sh->poll_fd, &kev, 1, NULL, 0, NULL) != 0) {
            io_unlink_waiter(s, interest == ZAN_IO_READ ? 1 : 0, w);
            io_mark_dead(sh, co, step, fail_out_n, fail_accept);
            InterlockedDecrement(&g_io_count);
            return;
        }
    }
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    io_register_locked(fd, interest, co, step);
    shard_unlock(sh);
}

/* 接收超时：向协程传递 -1 错误码并唤醒 */
static int rto_timeout_scan(zan_io_shard_t *sh) {
    if (!sh->rto) return 0;
    int woke = 0;
    long long now = dns_now_ms();
    zan_io_rto_t **pp = &sh->rto;
    while (*pp) {
        zan_io_rto_t *e = *pp;
        if (e->due_ms > now) { pp = &e->next; continue; }
        *pp = e->next;
        int fd = e->fd;
        zan_io_slot_t *s = (fd >= 0 && fd < g_slots_cap) ? &g_slots[fd] : NULL;
        zan_io_waiter_t *w = s ? io_find_read_waiter(s, e->frame) : NULL;
        if (w) {
            io_unlink_waiter(s, 1, w);
            if (e->out_n) *e->out_n = -1;
            io_wake(e->frame, e->step);
            InterlockedDecrement(&g_io_count);
            woke++;
        }
        free(e);
    }
    return woke;
}

static int32_t io_poll_shard(int shard, int64_t timeout_ms) {
    zan_io_shard_t *sh = &g_ioshard[shard];
    shard_lock(sh);
    if (sh->dead) {
        int wd = io_flush_dead(sh);
        shard_unlock(sh);
        return wd;
    }
    if (sh->rto) {
        int wr = rto_timeout_scan(sh);
        if (wr) {
            shard_unlock(sh);
            return wr;
        }
    }
    int empty = (g_io_count == 0 && g_blocking_inflight == 0);
    shard_unlock(sh);
    if (empty) return 0;
    struct kevent events[64];
    for (;;) {
        int64_t wait = dns_wait_ms(timeout_ms);
        int capped = (wait < 0 || wait > ZAN_IO_SWEEP_MS);
        if (capped) wait = ZAN_IO_SWEEP_MS;
        struct timespec ts = { wait / 1000, (wait % 1000) * 1000000L };
        int n = kevent(sh->poll_fd, NULL, 0, events, 64, &ts);
        shard_lock(sh);
        if (n < 0) n = 0;
        if (n != 0) {
            int woke = 0;
            for (int i = 0; i < n; i++) {
                int fd = (int)events[i].ident;
                if (fd == sh->wake_rfd) {
                    io_shard_wake_consume(sh);
                    continue;
                }
                if (fd == g_dns_wake_fd) {
                    dns_wake_read();
                    woke += dns_drain();
                    continue;
                }
                if (fd < 0 || fd >= g_slots_cap) continue;
                zan_io_slot_t *s = &g_slots[fd];
                zan_io_waiter_t ready[8];
                int nr = 0;
                if (events[i].filter == EVFILT_READ)
                    nr += io_take(s, fd, 1, ready + nr, 8 - nr);
                if (events[i].filter == EVFILT_WRITE)
                    nr += io_take(s, fd, 0, ready + nr, 8 - nr);
                if ((events[i].filter == EVFILT_READ && s->has_r) ||
                    (events[i].filter == EVFILT_WRITE && s->has_w)) {
                    struct kevent rk;
                    int retries;
                    EV_SET(&rk, (uintptr_t)fd, events[i].filter,
                           EV_ADD | EV_ONESHOT, 0, 0, NULL);
                    int rearmed = 0;
                    for (retries = 0; retries < 8 && !rearmed; retries++) {
                        if (kevent(sh->poll_fd, &rk, 1, NULL, 0, NULL) == 0)
                            rearmed = 1;
                        else if (errno != EINTR) break;
                    }
                    if (!rearmed &&
                        ((events[i].filter == EVFILT_READ && s->has_r) ||
                         (events[i].filter == EVFILT_WRITE && s->has_w))) {
                        io_fail_slot_waiters(fd, s);
                    }
                }
                for (int k = 0; k < nr; k++) {
                    io_deliver_waiter(fd, &ready[k]);
                    io_wake(ready[k].co, ready[k].step);
                    InterlockedDecrement(&g_io_count);
                    woke++;
                }
            }
            shard_unlock(sh);
            return woke;
        }
        int w2 = dns_timeout_scan();
        int w3 = w2 ? 0 : dns_drain();
        int w4 = w3 ? 0 : io_sweep_slots(shard);
        int w5 = w4 ? 0 : rto_timeout_scan(sh);
        int done = 1, val = 0;
        if      (w2) val = w2;
        else if (w3) val = w3;
        else if (w4) val = w4;
        else if (w5) val = w5;
        else if (capped && g_io_count + g_blocking_inflight > 0 && timeout_ms < 0) done = 0;
        shard_unlock(sh);
        if (done) return val;
    }
}

int32_t zan_io_poll(int64_t timeout_ms) {
    return io_poll_shard(io_thread_shard(), timeout_ms);
}

static void io_close_notify_slots(intptr_t fdp) {
    int fd = (int)fdp;
    if (fd < 0 || fd >= g_slots_cap) return;
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    zan_io_slot_t *s = &g_slots[fd];
    if (s->in_epoll) {
        s->in_epoll = 0;
        struct kevent ke;
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(sh->poll_fd, &ke, 1, NULL, 0, NULL);
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(sh->poll_fd, &ke, 1, NULL, 0, NULL);
    }
    for (;;) {
        zan_io_waiter_t ready[16];
        int nr = io_take(s, fd, 1, ready, 16);
        nr += io_take(s, fd, 0, ready + nr, 16 - nr);
        if (nr == 0) break;
        for (int k = 0; k < nr; k++) {
            if (ready[k].out_accept)   *ready[k].out_accept = -1;
            else if (ready[k].out_n)   *ready[k].out_n = 0;
            io_wake(ready[k].co, ready[k].step);
            InterlockedDecrement(&g_io_count);
        }
    }
}

void zan_io_close_notify(intptr_t fd) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    io_close_notify_slots(fd);
    shard_unlock(sh);
}
