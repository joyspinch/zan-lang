/* rt_io_iocp.c: Windows 原生完成端口 (IOCP) 异步套接字与反应堆实现 */

#ifndef _WIN32
#error "rt_io_iocp.c is only intended for Windows targets"
#endif

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

/* Windows IOCP 进行中的重叠 I/O 状态记录 */
typedef struct zan_io_op {
    OVERLAPPED ov;
    SOCKET     sock;
    int        interest;     /* ZAN_IO_READ / ZAN_IO_WRITE */
    int        kind;         /* readiness/recv/accept */
    int        rto;          
    void      *co;
    zan_co_step_t step;
    int64_t   *out_n;
    SOCKET     accepted;
    void      *accept_buf;
} zan_io_op_t;

enum {
    ZAN_IO_OP_READY,
    ZAN_IO_OP_RECV,
    ZAN_IO_OP_ACCEPT
};

static HANDLE g_iocp;

typedef struct zan_io_rto zan_io_rto_t;
static zan_io_rto_t *g_rto;
static CRITICAL_SECTION g_rto_lock;

#if defined(ZAN_CO_DRIVER)
/* IOCP 立即完成同步快路径优化 */
static volatile LONG g_syncfast = -1;   /* -1 unread, 1 on, 0 off */
static volatile LONG g_sync_inline;

static int syncfast_on(void) {
    LONG v = g_syncfast;
    if (v < 0) {
        int cfg = zan_async_cfg_sync_fast();
        if (cfg >= 0) {
            v = cfg;
        } else {
            const char *e = getenv("ZAN_IO_SYNCFAST");
            v = (e && *e && e[0] != '0') ? 1 : 0;
        }
        InterlockedExchange(&g_syncfast, v);
    }
    return (int)v;
}

#define ZAN_IO_MAXSHARD 256
static HANDLE        g_shard[ZAN_IO_MAXSHARD];
static volatile LONG g_shards = 1;

static HANDLE io_shard(int i) {
    if (i <= 0 || i >= (int)g_shards) return g_iocp;
    return g_shard[i] ? g_shard[i] : g_iocp;
}

static HANDLE io_shard_of(SOCKET s) {
    int n = (int)g_shards;
    if (n <= 1) return g_iocp;
    return io_shard((int)((((uintptr_t)s) >> 2) % (unsigned)n));
}

/* 初始化 n 个 IOCP 分片完成端口 */
static void io_shards_start(int n) {
    int cfg = zan_async_cfg_io_shards();
    if (cfg > 0 && cfg < n) n = cfg;
    const char *e = getenv("ZAN_IO_SHARDS");
    if (e && *e) {
        int v = atoi(e);
        if (v > 0 && v < n) n = v;
    }
    if (n > ZAN_IO_MAXSHARD) n = ZAN_IO_MAXSHARD;
    for (int i = 1; i < n; i++) {
        if (!g_shard[i])
            g_shard[i] = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (!g_shard[i]) { n = i; break; }
    }
    InterlockedExchange(&g_shards, n < 1 ? 1 : n);
}

static void io_shards_stop(void) {
    InterlockedExchange(&g_shards, 1);
    for (int i = 1; i < ZAN_IO_MAXSHARD; i++) {
        if (g_shard[i]) { CloseHandle(g_shard[i]); g_shard[i] = NULL; }
    }
}
#endif

#if defined(ZAN_CO_DRIVER)
#if defined(__GNUC__)
static SLIST_HEADER g_op_slist __attribute__((aligned(MEMORY_ALLOCATION_ALIGNMENT)));
#else
static __declspec(align(MEMORY_ALLOCATION_ALIGNMENT)) SLIST_HEADER g_op_slist;
#endif

static zan_io_op_t *op_alloc(void) {
    zan_io_op_t *op = (zan_io_op_t *)InterlockedPopEntrySList(&g_op_slist);
    if (!op)
        op = (zan_io_op_t *)_aligned_malloc(sizeof(zan_io_op_t),
                                            MEMORY_ALLOCATION_ALIGNMENT);
    if (op) memset(op, 0, sizeof(*op));
    return op;
}
static void op_free(zan_io_op_t *op) {
    InterlockedPushEntrySList(&g_op_slist, (PSLIST_ENTRY)op);
}
#else
static zan_io_op_t *g_op_pool;

static zan_io_op_t *op_alloc(void) {
    zan_io_op_t *op = g_op_pool;
    if (op) {
        g_op_pool = *(zan_io_op_t **)op;
        memset(op, 0, sizeof(*op));
    } else {
        op = (zan_io_op_t *)calloc(1, sizeof(*op));
    }
    return op;
}
static void op_free(zan_io_op_t *op) {
    *(zan_io_op_t **)op = g_op_pool;
    g_op_pool = op;
}
#endif

/* 将新建套接字关联至 IOCP 完成端口 */
static ZAN_MAYBE_UNUSED void mark_assoc(SOCKET s) {
#if defined(ZAN_CO_DRIVER)
    HANDLE port = io_shard_of(s);
#else
    HANDLE port = g_iocp;
#endif
    if (CreateIoCompletionPort((HANDLE)s, port, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;
    }
}

static void io_complete_op(zan_io_op_t *op, DWORD transferred, ULONG_PTR status);

/* 在首次重叠调用前绑定套接字与 IOCP 完成端口 */
static int ensure_assoc(SOCKET s) {
#if defined(ZAN_CO_DRIVER)
    HANDLE port = io_shard_of(s);
#else
    HANDLE port = g_iocp;
#endif
    if (CreateIoCompletionPort((HANDLE)s, port, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;
    }
#if defined(ZAN_CO_DRIVER)
    if (!syncfast_on()) return 0;
    if (SetFileCompletionNotificationModes(
               (HANDLE)s, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) {
        return 1;
    }
    if (GetLastError() == ERROR_INVALID_PARAMETER) {
        return 1;
    }
    return 0;
#else
    return 0;
#endif
}

static SRWLOCK g_io_init_srw = SRWLOCK_INIT;
static int g_rto_lock_ready;

void zan_io_init(void) {
    if (g_io_started) return;
    AcquireSRWLockExclusive(&g_io_init_srw);
    if (g_io_started) { ReleaseSRWLockExclusive(&g_io_init_srw); return; }
    zan__crash_install();
    g_io_count = 0;
    g_io_broken = 0;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (g_iocp == NULL) g_io_broken = 1;
    if (!g_rto_lock_ready) {
        InitializeCriticalSection(&g_rto_lock);
        g_rto_lock_ready = 1;
    }
#if defined(ZAN_CO_DRIVER)
    InitializeSListHead(&g_op_slist);
#endif
    g_io_started = 1;
    ReleaseSRWLockExclusive(&g_io_init_srw);
}

void zan_io_shutdown(void) {
#if defined(ZAN_CO_DRIVER)
    io_shards_stop();
#endif
    if (g_iocp) { CloseHandle(g_iocp); g_iocp = NULL; }
    g_io_count = 0;
    dns_shutdown_cleanup();
    while (g_rto) {
        void *e = g_rto;
        g_rto = *(zan_io_rto_t **)e;
        free(e);
    }
#if defined(ZAN_CO_DRIVER)
    for (;;) {
        PSLIST_ENTRY e = InterlockedPopEntrySList(&g_op_slist);
        if (!e) break;
        _aligned_free(e);
    }
#else
    while (g_op_pool) {
        zan_io_op_t *nx = *(zan_io_op_t **)g_op_pool;
        free(g_op_pool);
        g_op_pool = nx;
    }
#endif
    WSACleanup();
#if defined(ZAN_CO_DRIVER)
    LONG requested = InterlockedExchange(&g_socket_cleanup_requested, 0);
    while (requested-- > 0) WSACleanup();
#endif
    g_io_started = 0;
}

int32_t zan_io_set_nonblocking(intptr_t fd) {
    u_long mode = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &mode);
}

#if defined(ZAN_CO_DRIVER)
#define IO_CNT_INC() InterlockedIncrement((volatile LONG *)&g_io_count)
#define IO_CNT_DEC() InterlockedDecrement((volatile LONG *)&g_io_count)
#else
#define IO_CNT_INC() (g_io_count++)
#define IO_CNT_DEC() (g_io_count--)
#endif

#if defined(ZAN_CO_DRIVER)
static int io_is_listening(SOCKET s) {
    int val = 0, len = (int)sizeof(val);
    if (getsockopt(s, SOL_SOCKET, SO_ACCEPTCONN, (char *)&val, &len) == 0)
        return val != 0;
    return 0;
}
#endif

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    if (g_io_broken) {
        if (step) zan_co_ready(co, step);
        return;
    }
    SOCKET s = (SOCKET)fd;

#if defined(ZAN_CO_DRIVER)
    if (interest == ZAN_IO_READ && step && io_is_listening(s)) {
        zan_co_delay(1, co, step);
        return;
    }
#endif

    int skip = ensure_assoc(s);
    (void)skip;
    IOTRACE("io_register(READY) fd=%lld interest=%d cnt=%ld", (long long)s, interest, g_io_count);

    zan_io_op_t *op = op_alloc();
    if (!op) {
        if (step) zan_co_ready(co, step);
        return;
    }
    op->sock = s;
    op->interest = interest;
    op->kind = ZAN_IO_OP_READY;
    op->co = co;
    op->step = step;
    IO_CNT_INC();

    WSABUF b;
    b.len = 0;
    b.buf = NULL;
    int r, e;
    if (interest == ZAN_IO_READ) {
        DWORD flags = 0;
        int sotype = 0, solen = (int)sizeof(sotype);
        if (getsockopt(s, SOL_SOCKET, SO_TYPE, (char *)&sotype, &solen) == 0 &&
            sotype == SOCK_DGRAM)
            flags = MSG_PEEK;
        r = WSARecv(s, &b, 1, NULL, &flags, &op->ov, NULL);
    } else {
        r = WSASend(s, &b, 1, NULL, 0, &op->ov, NULL);
    }
    IOTRACE("ready_co fd=%lld interest=%d op=%p -> r=%d err=%d cnt=%ld",
            (long long)s, interest, (void*)op, r, r?WSAGetLastError():0, g_io_count);
    if (r == 0) {
#if defined(ZAN_CO_DRIVER)
        if (skip) {
            op_free(op);
            IO_CNT_DEC();
            InterlockedIncrement(&g_sync_inline);
            if (step) zan_co_ready(co, step);
        }
        return;
#else
        return;
#endif
    }
    e = WSAGetLastError();
    if (e == WSA_IO_PENDING) return;
#if defined(ZAN_CO_DRIVER)
    if (!PostQueuedCompletionStatus(io_shard_of(s), 0, (ULONG_PTR)s, &op->ov)) {
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov)) {
#endif
        io_wake(co, step);
        op_free(op);
        IO_CNT_DEC();
    }
}

void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
                    zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
    if (g_io_broken) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }
    SOCKET s = (SOCKET)fd;
    int skip = ensure_assoc(s);
    (void)skip;

    if (len <= 0 || len > 0x7FFFFFF0) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }

    zan_io_op_t *op = op_alloc();
    if (!op) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }
    op->sock = s;
    op->interest = ZAN_IO_READ;
    op->kind = ZAN_IO_OP_RECV;
    op->co = frame;
    op->step = step;
    op->out_n = out_n;
    IO_CNT_INC();

    WSABUF b;
    b.len = (ULONG)len;
    b.buf = (char *)buf;
    DWORD flags = 0, got = 0;
    int r = WSARecv(s, &b, 1, &got, &flags, &op->ov, NULL);
    IOTRACE("recv_co fd=%lld len=%d op=%p -> r=%d got=%lu err=%d cnt=%ld", (long long)fd, len, (void*)op, r, (unsigned long)got, r?WSAGetLastError():0, g_io_count);
    if (r == 0) {
#if defined(ZAN_CO_DRIVER)
        if (skip) {
            if (out_n) *out_n = (int64_t)got;
            op_free(op);
            IO_CNT_DEC();
            InterlockedIncrement(&g_sync_inline);
            if (step) zan_co_ready(frame, step);
        }
        return;
#else
        return;
#endif
    }
    int e = WSAGetLastError();
    if (e == WSA_IO_PENDING) return;
    if (out_n) *out_n = 0;
#if defined(ZAN_CO_DRIVER)
    if (!PostQueuedCompletionStatus(io_shard_of(s), 0, (ULONG_PTR)s, &op->ov)) {
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov)) {
#endif
        if (step) zan_co_ready(frame, step);
        op_free(op);
        IO_CNT_DEC();
    }
}

struct zan_io_rto {
    SOCKET          s;
    void           *frame;
    zan_co_step_t   step;
    OVERLAPPED     *ov;
    int64_t        *out_n;
    long long       due_ms;
    struct zan_io_rto *next;
};

static void rto_insert(zan_io_rto_t *e) {
    EnterCriticalSection(&g_rto_lock);
    e->next = g_rto;
    g_rto = e;
    LeaveCriticalSection(&g_rto_lock);
}

static int rto_claim(void *frame) {
    int found = 0;
    EnterCriticalSection(&g_rto_lock);
    zan_io_rto_t **pp = &g_rto;
    while (*pp) {
        if ((*pp)->frame == frame) {
            zan_io_rto_t *e = *pp;
            *pp = e->next;
            free(e);
            found = 1;
            break;
        }
        pp = &(*pp)->next;
    }
    LeaveCriticalSection(&g_rto_lock);
    return found;
}

static long long rto_wait_ms(long long caller_ms) {
    if (!g_rto) return caller_ms;
    long long now = dns_now_ms();
    long long nearest = -1;
    EnterCriticalSection(&g_rto_lock);
    for (zan_io_rto_t *e = g_rto; e; e = e->next)
        if (nearest < 0 || e->due_ms < nearest) nearest = e->due_ms;
    LeaveCriticalSection(&g_rto_lock);
    if (nearest < 0) return caller_ms;
    long long wait = nearest - now;
    if (wait < 0) wait = 0;
    if (caller_ms < 0) return wait;
    return wait < caller_ms ? wait : caller_ms;
}

static int rto_timeout_scan(void) {
    if (!g_rto) return 0;
    int woke = 0;
    long long now = dns_now_ms();
    for (;;) {
        zan_io_rto_t *e = NULL;
        EnterCriticalSection(&g_rto_lock);
        zan_io_rto_t **pp = &g_rto;
        while (*pp) {
            if ((*pp)->due_ms <= now) {
                e = *pp;
                *pp = e->next;
                break;
            }
            pp = &(*pp)->next;
        }
        LeaveCriticalSection(&g_rto_lock);
        if (!e) break;
        zan_io_op_t *win_op = CONTAINING_RECORD(e->ov, zan_io_op_t, ov);
        win_op->rto = 2;
        CancelIoEx((HANDLE)e->s, e->ov);
        if (e->out_n) *e->out_n = -1;
        io_wake(e->frame, e->step);
        free(e);
        woke++;
    }
    return woke;
}

void zan_io_recv_to_co(intptr_t fd, void *buf, int32_t len, int64_t timeout_ms,
                       void *frame, zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
    if (g_io_broken) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }
    SOCKET s = (SOCKET)fd;
    int skip = ensure_assoc(s);
    (void)skip;
    if (len <= 0 || len > 0x7FFFFFF0) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }
    zan_io_op_t *op = op_alloc();
    if (!op) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }
    op->sock = s;
    op->interest = ZAN_IO_READ;
    op->kind = ZAN_IO_OP_RECV;
    op->rto = 1;
    op->co = frame;
    op->step = step;
    op->out_n = out_n;
    IO_CNT_INC();
    zan_io_rto_t *e = (zan_io_rto_t *)calloc(1, sizeof(*e));
    if (e) {
        e->s = s;
        e->frame = frame;
        e->step = step;
        e->ov = &op->ov;
        e->out_n = out_n;
        e->due_ms = dns_now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
        rto_insert(e);
    }
    else op->rto = 0;

    WSABUF b;
    b.len = (ULONG)len;
    b.buf = (char *)buf;
    DWORD flags = 0, got = 0;
    int r = WSARecv(s, &b, 1, &got, &flags, &op->ov, NULL);
    IOTRACE("recv_to_co fd=%lld len=%d to=%lld op=%p -> r=%d got=%lu err=%d cnt=%ld",
            (long long)fd, len, (long long)timeout_ms, (void*)op, r,
            (unsigned long)got, r ? WSAGetLastError() : 0, g_io_count);
    if (r == 0) {
#if defined(ZAN_CO_DRIVER)
        if (skip) {
            rto_claim(frame);
            if (out_n) *out_n = (int64_t)got;
            op_free(op);
            IO_CNT_DEC();
            InterlockedIncrement(&g_sync_inline);
            if (step) zan_co_ready(frame, step);
        }
        return;
#else
        return;
#endif
    }
    int err = WSAGetLastError();
    if (err == WSA_IO_PENDING) return;
    if (out_n) *out_n = 0;
#if defined(ZAN_CO_DRIVER)
    if (!PostQueuedCompletionStatus(io_shard_of(s), 0, (ULONG_PTR)s, &op->ov)) {
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov)) {
#endif
        rto_claim(frame);
        if (step) zan_co_ready(frame, step);
        op_free(op);
        IO_CNT_DEC();
    }
}

void zan_io_accept_co(intptr_t fd, void *frame, zan_co_step_t step,
                      intptr_t *out_fd) {
    zan_io_init();
    if (g_io_broken) {
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }
    SOCKET listener = (SOCKET)fd;
    IOTRACE("accept_co ENTER listener=%lld", (long long)fd);
    int skip = ensure_assoc(listener);
    (void)skip;

    static LPFN_ACCEPTEX s_cached_accept_ex = NULL;
    LPFN_ACCEPTEX accept_ex = s_cached_accept_ex;
    DWORD got = 0;
    if (!accept_ex) {
        GUID guid = WSAID_ACCEPTEX;
        if (WSAIoctl(listener, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid, sizeof(guid), &accept_ex, sizeof(accept_ex),
                     &got, NULL, NULL) == SOCKET_ERROR) {
            if (out_fd) *out_fd = -1;
            if (step) zan_co_ready(frame, step);
            return;
        }
        s_cached_accept_ex = accept_ex;
    }

    struct sockaddr_storage lss;
    int llen = (int)sizeof(lss);
    int fam = AF_INET;
    if (getsockname(listener, (struct sockaddr *)&lss, &llen) == 0 &&
        lss.ss_family == AF_INET6)
        fam = AF_INET6;

    SOCKET accepted = WSASocketW(fam, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                                 WSA_FLAG_OVERLAPPED);
    if (accepted == INVALID_SOCKET) {
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }

    const DWORD addr_len = (DWORD)((fam == AF_INET6)
        ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)) + 16;
    zan_io_op_t *op = op_alloc();
    if (!op) {
        closesocket(accepted);
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }
    op->sock = listener;
    op->kind = ZAN_IO_OP_ACCEPT;
    op->co = frame;
    op->step = step;
    op->out_n = (int64_t *)out_fd;
    op->accepted = accepted;
    op->accept_buf = calloc(1, addr_len * 2);
    if (!op->accept_buf) {
        closesocket(accepted);
        op_free(op);
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }
    IO_CNT_INC();

    got = 0;
    BOOL ok = accept_ex(listener, accepted, op->accept_buf, 0,
                        addr_len, addr_len, &got, &op->ov);
    IOTRACE("accept_co listener=%lld op=%p ok=%d err=%d cnt=%ld", (long long)listener, (void*)op, (int)ok, WSAGetLastError(), g_io_count);
#if defined(ZAN_CO_DRIVER)
    if (ok && skip) {
        io_complete_op(op, got, 0);
        op_free(op);
        IO_CNT_DEC();
        InterlockedIncrement(&g_sync_inline);
        if (step) zan_co_ready(frame, step);
        return;
    }
#endif
    if (ok || WSAGetLastError() == WSA_IO_PENDING) return;

    closesocket(accepted);
    free(op->accept_buf);
    op_free(op);
    IO_CNT_DEC();
    if (out_fd) *out_fd = -1;
    if (step) zan_co_ready(frame, step);
}

static void io_complete_op(zan_io_op_t *op, DWORD transferred,
                           ULONG_PTR status) {
    if (op->kind == ZAN_IO_OP_ACCEPT) {
        SOCKET accepted = op->accepted;
        int64_t result = -1;
        if (status == 0 &&
            setsockopt(accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                       (const char *)&op->sock, (int)sizeof(op->sock)) == 0) {
            u_long mode = 1;
            ioctlsocket(accepted, FIONBIO, &mode);
            result = (int64_t)accepted;
        } else {
            closesocket(accepted);
        }
        IOTRACE("accept_complete listener=%lld accepted=%lld status=%llu", (long long)op->sock, (long long)result, (unsigned long long)status);
        if (op->out_n) *op->out_n = result;
        free(op->accept_buf);
        return;
    }
    IOTRACE("complete op=%p kind=%d transferred=%lu status=%llu", (void*)op, op->kind, (unsigned long)transferred, (unsigned long long)status);
    if (op->out_n) *op->out_n = (int64_t)transferred;
}

#if defined(ZAN_CO_DRIVER)
static BOOL io_poll_any(OVERLAPPED_ENTRY *entries, ULONG cap, ULONG *removed,
                        DWORD to) {
    int n = (int)g_shards;
    if (n <= 1)
        return GetQueuedCompletionStatusEx(g_iocp, entries, cap, removed, to, FALSE);
    DWORD slice = (to == INFINITE || to > 2) ? 2 : to;
    for (;;) {
        for (int i = 0; i < n; i++) {
            if (GetQueuedCompletionStatusEx(io_shard(i), entries, cap, removed,
                                            slice, FALSE))
                return TRUE;
        }
        if (to == INFINITE) continue;
        if (to <= (DWORD)slice * (DWORD)n) return FALSE;
        to -= (DWORD)slice * (DWORD)n;
    }
}
#endif

int32_t zan_io_poll(int64_t timeout_ms) {
    if (g_io_count == 0 && g_blocking_inflight == 0) return 0;
    if (g_rto) {
        int wr = rto_timeout_scan();
        if (wr) return wr;
    }
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    int64_t wait = rto_wait_ms(dns_wait_ms(timeout_ms));
    if (wait > 0x7FFFFFFF) wait = 0x7FFFFFFF;
    DWORD to = (wait < 0) ? INFINITE : (DWORD)wait;
    if (g_blocking_wake_lost && InterlockedExchange(&g_blocking_wake_lost, 0)) {
        int wd = dns_drain();
        if (wd) return wd;
    }
#if defined(ZAN_CO_DRIVER)
    if (!io_poll_any(entries, 64, &removed, to)) {
#else
    if (!GetQueuedCompletionStatusEx(g_iocp, entries, 64, &removed, to, FALSE)) {
#endif
        IOTRACE("poll GQCS=0 err=%lu to=%lu cnt=%ld", (unsigned long)GetLastError(), (unsigned long)to, g_io_count);
        int w = dns_timeout_scan();
        if (w) return w;
        int wr = rto_timeout_scan();
        if (wr) return wr;
        return dns_drain();
    }
    IOTRACE("poll removed=%lu cnt=%ld", (unsigned long)removed, g_io_count);
    int woke = 0;
    zan_co_task_t ready_batch[64];
    size_t ready_count = 0;
    for (ULONG i = 0; i < removed; i++) {
        if (!entries[i].lpOverlapped) {
            woke += dns_drain();
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
        woke++;
    }
    if (ready_count > 0) {
        zan_co_ready_batch(ready_batch, ready_count);
    }
    return woke;
}

void zan_io_close_notify(intptr_t fd) {
    if (fd > 0)
        CancelIoEx((HANDLE)fd, NULL);
}
