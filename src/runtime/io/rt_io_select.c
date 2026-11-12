/* rt_io_select.c: POSIX 通用 select / WASI 平台回退多路复用反应堆实现 */

static int io_shard_open(int i) {
    return i == 0;
}

static void io_init_locked(void) {
    if (g_io_started) return;
    io_shard_mutexes_prime();
    zan_io_ignore_sigpipe();
    g_io_entries = NULL;
    g_io_count = 0;
    if (g_dns_wake_fd < 0) {
        int pfd[2];
        if (pipe(pfd) == 0) {
            g_dns_wake_fd = pfd[0];
            g_dns_wake_wfd = pfd[1];
            fcntl(pfd[0], F_SETFL, O_NONBLOCK);
            zan_io_fd_cloexec(pfd[0]);
            zan_io_fd_cloexec(pfd[1]);
        }
    }
    g_io_started = 1;
}

void zan_io_init(void) {
    if (g_io_started) return;
    IO_INIT_LOCK();
    io_init_locked();
    IO_INIT_UNLOCK();
}

static void io_shutdown_locked(void) {
    zan_io_entry_t *e = g_io_entries;
    while (e) {
        zan_io_entry_t *n = e->next;
        free(e);
        e = n;
    }
    g_io_entries = NULL;
    g_io_count = 0;
    io_dead_clear(&g_ioshard[0]);
    while (g_ioshard[0].rto) {
        zan_io_rto_t *rt = g_ioshard[0].rto;
        g_ioshard[0].rto = rt->next;
        free(rt);
    }
    dns_shutdown_cleanup();
    if (g_dns_wake_fd >= 0) { close(g_dns_wake_fd); g_dns_wake_fd = -1; }
    if (g_dns_wake_wfd >= 0) { close(g_dns_wake_wfd); g_dns_wake_wfd = -1; }
    g_io_started = 0;
}

void zan_io_shutdown(void) {
    shard_lock(&g_ioshard[0]);
    io_shutdown_locked();
    shard_unlock(&g_ioshard[0]);
}

static void io_register_locked(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    if (fd >= FD_SETSIZE) { io_reject_dead_fd(-1, co, step); return; }
    if (io_reject_dead_fd(fd, co, step)) return;
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
    if (!e) { io_reject_dead_fd(-1, co, step); return; }
    e->fd = (int)fd;
    e->interest = interest;
    e->co = co;
    e->step = step;
    e->rbuf = g_pending_rbuf;
    e->rlen = g_pending_rlen;
    e->out_n = g_pending_out_n;
    e->out_accept = g_pending_accept_out;
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
    g_pending_accept_out = NULL;
    e->next = g_io_entries;
    g_io_entries = e;
    InterlockedIncrement(&g_io_count);
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    io_register_locked(fd, interest, co, step);
    shard_unlock(sh);
}

static long long rto_wait_ms(long long caller_ms) {
    zan_io_shard_t *sh = &g_ioshard[0];
    if (!sh->rto) return caller_ms;
    long long now = dns_now_ms();
    long long nearest = -1;
    for (zan_io_rto_t *e = sh->rto; e; e = e->next)
        if (nearest < 0 || e->due_ms < nearest) nearest = e->due_ms;
    if (nearest < 0) return caller_ms;
    long long wait = nearest - now;
    if (wait < 0) wait = 0;
    if (caller_ms < 0) return wait;
    return wait < caller_ms ? wait : caller_ms;
}

static int rto_timeout_scan(zan_io_shard_t *sh) {
    if (!sh->rto) return 0;
    int woke = 0;
    long long now = dns_now_ms();
    zan_io_rto_t **ep = &sh->rto;
    while (*ep) {
        zan_io_rto_t *e = *ep;
        if (e->due_ms > now) { ep = &e->next; continue; }
        *ep = e->next;
        zan_io_entry_t **pp = &g_io_entries;
        while (*pp && !((*pp)->fd == e->fd && (*pp)->co == e->frame &&
                        (*pp)->interest == ZAN_IO_READ))
            pp = &(*pp)->next;
        if (*pp) {
            zan_io_entry_t *x = *pp;
            *pp = x->next;
            free(x);
            if (e->out_n) *e->out_n = -1;
            io_wake(e->frame, e->step);
            InterlockedDecrement(&g_io_count);
            woke++;
        }
        free(e);
    }
    return woke;
}

static int32_t io_poll_shard_locked(int shard, int64_t timeout_ms) {
    zan_io_shard_t *sh = &g_ioshard[shard];
    if (sh->dead) return io_flush_dead(sh);
    if (sh->rto) {
        int wr = rto_timeout_scan(sh);
        if (wr) return wr;
    }
    if (g_io_count == 0 && g_blocking_inflight == 0) return 0;

    {
        int woke = io_sweep_entries(FD_SETSIZE);
        if (woke) return woke;
        if (g_io_count == 0 && g_blocking_inflight == 0) return 0;
    }

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    int max_fd = 0;

    if (g_dns_wake_fd >= 0 && g_dns_wake_fd < FD_SETSIZE) {
        FD_SET(g_dns_wake_fd, &read_fds);
        max_fd = g_dns_wake_fd;
    }

    zan_io_entry_t *e = g_io_entries;
    while (e) {
        if (e->interest == ZAN_IO_READ) FD_SET(e->fd, &read_fds);
        else                             FD_SET(e->fd, &write_fds);
        if (e->fd > max_fd) max_fd = e->fd;
        e = e->next;
    }

    int64_t wait = rto_wait_ms(dns_wait_ms(timeout_ms));
    if (wait > 0x7FFFFFFF) wait = 0x7FFFFFFF;
    struct timeval tv;
    tv.tv_sec  = (long)(wait / 1000);
    tv.tv_usec = (long)((wait % 1000) * 1000);

    int n = select(max_fd + 1, &read_fds, &write_fds, NULL,
                   wait < 0 ? NULL : &tv);
    if (n <= 0) {
        int w2 = dns_timeout_scan();
        if (w2) return w2;
        int wr = rto_timeout_scan(sh);
        if (wr) return wr;
        return dns_drain();
    }

    int woke = 0;
    if (g_dns_wake_fd >= 0 && FD_ISSET(g_dns_wake_fd, &read_fds)) {
        dns_wake_read();
        woke += dns_drain();
    }
    zan_io_entry_t **pp = &g_io_entries;
    while (*pp) {
        zan_io_entry_t *cur = *pp;
        int ready = 0;
        if (cur->interest == ZAN_IO_READ && FD_ISSET(cur->fd, &read_fds))
            ready = 1;
        if (cur->interest == ZAN_IO_WRITE && FD_ISSET(cur->fd, &write_fds))
            ready = 1;
        if (ready) {
            void *co = cur->co;
            zan_co_step_t step = cur->step;
            *pp = cur->next;
            io_deliver_recv(cur);
            free(cur);
            rto_drop(sh, co);
            io_wake(co, step);
            InterlockedDecrement(&g_io_count);
            woke++;
        } else {
            pp = &cur->next;
        }
    }
    return woke;
}

int32_t zan_io_poll(int64_t timeout_ms) {
    int idx = io_thread_shard();
    zan_io_shard_t *sh = &g_ioshard[idx];
    shard_lock(sh);
    int32_t woke = io_poll_shard_locked(idx, timeout_ms);
    shard_unlock(sh);
    return woke;
}

void zan_io_close_notify(intptr_t fdp) {
    int fd = (int)fdp;
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    zan_io_entry_t **pp = &g_io_entries;
    while (*pp) {
        zan_io_entry_t *cur = *pp;
        if (cur->fd != fd) { pp = &cur->next; continue; }
        *pp = cur->next;
        io_mark_dead(sh, cur->co, cur->step, cur->out_n, cur->out_accept);
        free(cur);
        InterlockedDecrement(&g_io_count);
    }
    io_flush_dead(sh);
    shard_unlock(sh);
}
