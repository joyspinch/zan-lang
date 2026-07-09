/* rt_io.c -- Zan async-IO event loop.
 *
 * A thin readiness abstraction over each platform's scalable IO facility:
 *   - Linux:   epoll
 *   - macOS:   kqueue
 *   - Windows: IOCP (I/O Completion Ports)
 *   - Fallback: select
 *
 * The scheduler calls zan_io_poll() in its main loop to wake coroutines
 * whose sockets became ready.  Coroutines call zan_io_wait_readable /
 * zan_io_wait_writable / zan_io_connect to suspend until an operation can
 * proceed.  These are the async-IO ABI the compiler / stdlib target.
 *
 * On the readiness backends (epoll/kqueue/select) a watcher maps a fd to a
 * waiting coroutine.  On IOCP -- which is completion based -- readiness is
 * synthesised with a zero-byte overlapped WSARecv/WSASend, and connect is a
 * real ConnectEx completion; each in-flight operation carries the coroutine
 * to resume when its completion is dequeued.
 */

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601   /* Windows 7+: GetQueuedCompletionStatusEx */
#endif

#include "rt_io.h"
#include "rt_co.h"
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

/* The reactor serves two clients:
 *   - stackless CPS frames (the async/await state machine) via zan_io_wait_co,
 *     woken through the co ready queue (zan_co_ready);
 *   - stackful rt_sched fibers via zan_io_wait_readable/writable, woken by
 *     switching fibers (zan_io_resume).
 * When built with ZAN_IO_STACKLESS_ONLY (the object shipped with zanc and
 * linked into produced async programs), the fiber half is dropped so the
 * reactor has no dependency on rt_sched. */
#ifndef ZAN_IO_STACKLESS_ONLY
#include "rt_sched.h"
/* ---- forward declarations from rt_sched (internal) ---- */
extern void zan_io_suspend_current(void);   /* implemented in rt_sched.c */
extern void zan_io_resume(void *co);        /* implemented in rt_sched.c */
extern void *zan_io_get_current_co(void);   /* implemented in rt_sched.c */
#endif

/* ZAN_IO_READ / ZAN_IO_WRITE come from rt_io.h. */

/* Number of in-flight watchers / operations; used by zan_io_has_pending()
 * and to short-circuit an empty poll on every backend. */
static int g_io_count;

/* Whether the backend has been initialized. Generated async programs never
 * call zan_io_init explicitly, so zan_io_wait_co lazy-inits on first use. */
static int g_io_started;

/* Re-schedule a stackless frame after `ms` milliseconds. Provided by the
 * coroutine driver (emitted inline by the compiler, or by the multi-worker
 * driver below); declared here so the reactor can poll listen-socket
 * readiness with a timer. Not part of rt_co.h. */
void zan_co_delay(long long ms, void *frame, zan_co_step_t step);

/* Wake a watcher whose fd became ready. A stackless watcher (registered via
 * zan_io_wait_co) carries a resume `step` and is re-entered through the co
 * driver's ready queue; a stackful watcher (rt_sched fiber) has step==NULL and
 * is resumed by switching to its fiber. */
static void io_wake(void *co, zan_co_step_t step) {
#ifdef ZAN_IO_STACKLESS_ONLY
    zan_co_ready(co, step);      /* stackless programs never register fibers */
#else
    if (step) zan_co_ready(co, step);
    else      zan_io_resume(co);
#endif
}

#if !defined(_WIN32)
/* ---- readiness watcher (epoll / kqueue / select backends) ---- */
typedef struct zan_io_entry {
    int fd;
    int interest;           /* ZAN_IO_READ or ZAN_IO_WRITE */
    void *co;               /* fiber handle, or stackless frame pointer */
    zan_co_step_t step;     /* stackless resume fn (NULL => stackful fiber) */
    struct zan_io_entry *next;
} zan_io_entry_t;

static zan_io_entry_t *g_io_entries;    /* linked list of active watchers */
#endif

/* ---- platform backend ---- */

#if defined(__linux__)
/* ==================== EPOLL ==================== */
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

static int g_epoll_fd = -1;

void zan_io_init(void) {
    if (g_io_started) return;
    g_io_entries = NULL;
    g_io_count = 0;
    g_epoll_fd = epoll_create1(0);
    g_io_started = 1;
}

void zan_io_shutdown(void) {
    /* remove all entries */
    zan_io_entry_t *e = g_io_entries;
    while (e) {
        zan_io_entry_t *n = e->next;
        epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, e->fd, NULL);
        free(e);
        e = n;
    }
    g_io_entries = NULL;
    g_io_count = 0;
    if (g_epoll_fd >= 0) { close(g_epoll_fd); g_epoll_fd = -1; }
    g_io_started = 0;
}

int zan_io_set_nonblocking(int64_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static void io_register(int fd, int interest, void *co, zan_co_step_t step) {
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
    e->fd = fd;
    e->interest = interest;
    e->co = co;
    e->step = step;
    e->next = g_io_entries;
    g_io_entries = e;
    g_io_count++;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd;
    ev.events = (interest == ZAN_IO_READ) ? EPOLLIN : EPOLLOUT;
    ev.events |= EPOLLONESHOT;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

int zan_io_poll(int64_t timeout_ms) {
    if (g_io_count == 0) return 0;
    struct epoll_event events[64];
    int n = epoll_wait(g_epoll_fd, events, 64,
                       timeout_ms < 0 ? -1 : (int)timeout_ms);
    int woke = 0;
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        zan_io_entry_t **pp = &g_io_entries;
        while (*pp) {
            if ((*pp)->fd == fd) {
                zan_io_entry_t *e = *pp;
                void *co = e->co;
                zan_co_step_t step = e->step;
                *pp = e->next;
                epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                free(e);
                g_io_count--;
                io_wake(co, step);
                woke++;
                break;
            }
            pp = &(*pp)->next;
        }
    }
    return woke;
}

#elif defined(__APPLE__) || defined(__FreeBSD__)
/* ==================== KQUEUE ==================== */
#include <sys/event.h>
#include <fcntl.h>
#include <unistd.h>

static int g_kq_fd = -1;

void zan_io_init(void) {
    if (g_io_started) return;
    g_io_entries = NULL;
    g_io_count = 0;
    g_kq_fd = kqueue();
    g_io_started = 1;
}

void zan_io_shutdown(void) {
    zan_io_entry_t *e = g_io_entries;
    while (e) {
        zan_io_entry_t *n = e->next;
        free(e);
        e = n;
    }
    g_io_entries = NULL;
    g_io_count = 0;
    if (g_kq_fd >= 0) { close(g_kq_fd); g_kq_fd = -1; }
    g_io_started = 0;
}

int zan_io_set_nonblocking(int64_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static void io_register(int fd, int interest, void *co, zan_co_step_t step) {
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
    e->fd = fd;
    e->interest = interest;
    e->co = co;
    e->step = step;
    e->next = g_io_entries;
    g_io_entries = e;
    g_io_count++;

    struct kevent kev;
    short filter = (interest == ZAN_IO_READ) ? EVFILT_READ : EVFILT_WRITE;
    EV_SET(&kev, fd, filter, EV_ADD | EV_ONESHOT, 0, 0, NULL);
    kevent(g_kq_fd, &kev, 1, NULL, 0, NULL);
}

int zan_io_poll(int64_t timeout_ms) {
    if (g_io_count == 0) return 0;
    struct kevent events[64];
    struct timespec ts = { timeout_ms / 1000, (timeout_ms % 1000) * 1000000L };
    int n = kevent(g_kq_fd, NULL, 0, events, 64,
                   timeout_ms < 0 ? NULL : &ts);
    int woke = 0;
    for (int i = 0; i < n; i++) {
        int fd = (int)events[i].ident;
        zan_io_entry_t **pp = &g_io_entries;
        while (*pp) {
            if ((*pp)->fd == fd) {
                zan_io_entry_t *e = *pp;
                void *co = e->co;
                zan_co_step_t step = e->step;
                *pp = e->next;
                free(e);
                g_io_count--;
                io_wake(co, step);
                woke++;
                break;
            }
            pp = &(*pp)->next;
        }
    }
    return woke;
}

#elif defined(_WIN32)
/* ==================== WINDOWS IOCP ==================== */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>          /* ConnectEx + WSAID_CONNECTEX */
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

/* One in-flight overlapped operation.  Recovered from its OVERLAPPED via
 * CONTAINING_RECORD when its completion is dequeued.  `ov` MUST be first. */
typedef struct zan_io_op {
    OVERLAPPED ov;
    SOCKET     sock;
    int        interest;     /* ZAN_IO_READ / ZAN_IO_WRITE */
    void      *co;           /* fiber handle, or stackless frame pointer */
    zan_co_step_t step;      /* stackless resume fn (NULL => stackful fiber) */
} zan_io_op_t;

static HANDLE g_iocp;
#if defined(ZAN_CO_DRIVER)
static volatile LONG g_skip_mode = -1;  /* -1 unknown, 1 on, 0 unsupported */
#endif

/* Associate a socket with the completion port. Association is one-shot per
 * socket *kernel object*, but socket HANDLE numbers are recycled by Windows
 * after a socket is closed. Caching associated handles (by value) would then
 * wrongly skip associating a brand-new socket that happens to reuse a closed
 * handle's number, and that socket's overlapped completions would never be
 * delivered to the port (the coroutine awaiting it blocks forever). So just
 * (re)associate on every registration and treat "already associated with this
 * port" (ERROR_INVALID_PARAMETER) as success. */
static void ensure_assoc(SOCKET s) {
    if (CreateIoCompletionPort((HANDLE)s, g_iocp, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;   /* ERROR_INVALID_PARAMETER == already bound to this port */
    }
#if defined(ZAN_CO_DRIVER)
    /* Skip queuing an IOCP packet when an overlapped op completes inline
     * (data already buffered). Under the multi-worker driver such inline
     * completions were being lost under high load -- a zero-byte WSARecv on an
     * already-readable socket returns 0, but the assumed follow-up completion
     * packet did not reliably arrive, leaking the in-flight count until every
     * connection parked (server hang). With this mode set, io_register resumes
     * the waiter directly on inline success instead of awaiting a packet. */
#ifndef FILE_SKIP_COMPLETION_PORT_ON_SUCCESS
#define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#endif
    BOOL ok = SetFileCompletionNotificationModes(
                  (HANDLE)s, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
    /* Record whether the OS honoured skip-on-success. io_register only takes
     * the inline-resume shortcut when it did; otherwise an inline completion
     * still posts a packet and must be left to the IOCP (resuming inline too
     * would double-complete the op). The result is uniform per OS. */
    if (g_skip_mode < 0) InterlockedExchange(&g_skip_mode, ok ? 1 : 0);
#endif
}

void zan_io_init(void) {
    if (g_io_started) return;
    g_io_count = 0;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    g_io_started = 1;
}

void zan_io_shutdown(void) {
    if (g_iocp) { CloseHandle(g_iocp); g_iocp = NULL; }
    g_io_count = 0;
    WSACleanup();
    g_io_started = 0;
}

int zan_io_set_nonblocking(int64_t fd) {
    u_long mode = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &mode);
}

/* In-flight op counter updates. The multi-worker driver (ZAN_CO_DRIVER) has
 * several threads registering / completing ops concurrently, so make the
 * counter atomic there; the single-threaded reactor keeps a plain int. */
#if defined(ZAN_CO_DRIVER)
#define IO_CNT_INC() InterlockedIncrement((LONG *)&g_io_count)
#define IO_CNT_DEC() InterlockedDecrement((LONG *)&g_io_count)
#else
#define IO_CNT_INC() (g_io_count++)
#define IO_CNT_DEC() (g_io_count--)
#endif

#if defined(ZAN_CO_DRIVER)
/* True if `s` is a listening socket (accept, not recv/send). */
static int io_is_listening(SOCKET s) {
    int val = 0, len = (int)sizeof(val);
    if (getsockopt(s, SOL_SOCKET, SO_ACCEPTCONN, (char *)&val, &len) == 0)
        return val != 0;
    return 0;
}
#endif

/* Post a zero-byte overlapped op to learn when `fd` is read/write ready. */
static void io_register(int fd, int interest, void *co, zan_co_step_t step) {
    SOCKET s = (SOCKET)fd;

#if defined(ZAN_CO_DRIVER)
    /* A listening socket cannot be probed with a zero-byte WSARecv: it fails
     * immediately, so the accept loop (await ReadReady; accept()) turns into a
     * busy spin. Under the multi-worker driver that spin floods the IOCP and
     * ready-queue lock and starves real request completions, so poll
     * accept-readiness with a short timer instead; the waiting coroutine does
     * the non-blocking accept(). (Stackless frames only -- stackful fibers
     * have no timer primitive.) The single-threaded inline driver keeps the
     * legacy spin: its run loop services timers XOR IO, so a permanently
     * pending accept timer would starve connection IO there. */
    if (interest == ZAN_IO_READ && step && io_is_listening(s)) {
        zan_co_delay(1, co, step);
        return;
    }
#endif

    ensure_assoc(s);

    zan_io_op_t *op = (zan_io_op_t *)calloc(1, sizeof(*op));
    op->sock = s;
    op->interest = interest;
    op->co = co;
    op->step = step;
    IO_CNT_INC();

    WSABUF b;
    b.len = 0;
    b.buf = NULL;
    int r, e;
    if (interest == ZAN_IO_READ) {
        DWORD flags = 0;
        r = WSARecv(s, &b, 1, NULL, &flags, &op->ov, NULL);
    } else {
        r = WSASend(s, &b, 1, NULL, 0, &op->ov, NULL);
    }
    if (r == 0) {
#if defined(ZAN_CO_DRIVER)
        /* Inline completion. With skip-on-success in effect no IOCP packet is
         * posted, so resume the waiter here rather than blocking it on a
         * completion that never comes. If skip-on-success is unsupported, a
         * packet is still queued -- leave it to the IOCP to avoid a double
         * completion. */
        if (g_skip_mode == 1) {
            free(op);
            IO_CNT_DEC();
            if (step) zan_co_ready(co, step);
        }
        return;
#else
        return;                  /* single-thread reactor: still queued to IOCP */
#endif
    }
    e = WSAGetLastError();
    if (e == WSA_IO_PENDING) return;
    /* Hard error: queue a completion so the coroutine is still resumed. */
    PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov);
}

int zan_io_poll(int64_t timeout_ms) {
    if (g_io_count == 0) return 0;
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    DWORD to = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    if (!GetQueuedCompletionStatusEx(g_iocp, entries, 64, &removed, to, FALSE))
        return 0;
    int woke = 0;
    for (ULONG i = 0; i < removed; i++) {
        zan_io_op_t *op = CONTAINING_RECORD(entries[i].lpOverlapped,
                                            zan_io_op_t, ov);
        void *co = op->co;
        zan_co_step_t step = op->step;
        free(op);
        g_io_count--;
        io_wake(co, step);
        woke++;
    }
    return woke;
}

#else
/* ==================== FALLBACK SELECT (POSIX) ==================== */
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>

void zan_io_init(void) {
    if (g_io_started) return;
    g_io_entries = NULL;
    g_io_count = 0;
    g_io_started = 1;
}

void zan_io_shutdown(void) {
    zan_io_entry_t *e = g_io_entries;
    while (e) {
        zan_io_entry_t *n = e->next;
        free(e);
        e = n;
    }
    g_io_entries = NULL;
    g_io_count = 0;
    g_io_started = 0;
}

int zan_io_set_nonblocking(int64_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static void io_register(int fd, int interest, void *co, zan_co_step_t step) {
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
    e->fd = fd;
    e->interest = interest;
    e->co = co;
    e->step = step;
    e->next = g_io_entries;
    g_io_entries = e;
    g_io_count++;
}

int zan_io_poll(int64_t timeout_ms) {
    if (g_io_count == 0) return 0;

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    int max_fd = 0;

    zan_io_entry_t *e = g_io_entries;
    while (e) {
        if (e->interest == ZAN_IO_READ) FD_SET(e->fd, &read_fds);
        else                             FD_SET(e->fd, &write_fds);
        if (e->fd > max_fd) max_fd = e->fd;
        e = e->next;
    }

    struct timeval tv;
    tv.tv_sec  = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);

    int n = select(max_fd + 1, &read_fds, &write_fds, NULL,
                   timeout_ms < 0 ? NULL : &tv);
    if (n <= 0) return 0;

    int woke = 0;
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
            free(cur);
            g_io_count--;
            io_wake(co, step);
            woke++;
        } else {
            pp = &cur->next;
        }
    }
    return woke;
}

#endif /* platform */

/* ================= coroutine-facing ABI ================= */

/* ---- stackless (CPS) registration + idle bridge ---- */

void zan_io_wait_co(int64_t fd, int interest, void *frame, zan_co_step_t step) {
    zan_io_init();      /* lazy: generated programs never call zan_io_init */
    io_register((int)fd, interest, frame, step);
}

int zan_io_pump(void) {
    if (!zan_io_has_pending()) return 0;
    return zan_io_poll(-1);   /* block until an fd is ready (wakes via zan_co_ready) */
}

int zan_io_has_pending(void) {
    return g_io_count > 0;
}

#ifndef ZAN_IO_STACKLESS_ONLY
/* ---- stackful (rt_sched fiber) ABI: excluded from the reactor object
 * shipped with zanc so it needs no rt_sched. ---- */

int64_t zan_io_wait_readable(int64_t fd) {
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register((int)fd, ZAN_IO_READ, co, NULL);
    zan_io_suspend_current();
    return 0;
}

int64_t zan_io_wait_writable(int64_t fd) {
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register((int)fd, ZAN_IO_WRITE, co, NULL);
    zan_io_suspend_current();
    return 0;
}

int64_t zan_io_wait_readable_timeout(int64_t fd, int64_t timeout_ms) {
    /* For simplicity, delegate to wait_readable; timeout is handled
     * by the scheduler's timer integration.  A proper implementation
     * would register both an IO watcher and a timer, and cancel whichever
     * fires second. */
    (void)timeout_ms;
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register((int)fd, ZAN_IO_READ, co, NULL);
    zan_io_suspend_current();
    return 1;
}

/* ================= async connect ================= */

#if defined(_WIN32)
/* Async connect via ConnectEx; suspends until the connection completes.
 * Returns 0 on success, -1 on error. */
int64_t zan_io_connect(int64_t fd, const char *ip, int port) {
    SOCKET s = (SOCKET)fd;

    /* ConnectEx requires a bound socket. */
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    bind(s, (struct sockaddr *)&local, sizeof(local));

    ensure_assoc(s);

    GUID guid = WSAID_CONNECTEX;
    LPFN_CONNECTEX pConnectEx = NULL;
    DWORD bytes = 0;
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guid, sizeof(guid), &pConnectEx, sizeof(pConnectEx),
                 &bytes, NULL, NULL) != 0 || !pConnectEx)
        return -1;

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons((u_short)port);
    target.sin_addr.s_addr = inet_addr(ip);

    void *co = zan_io_get_current_co();
    if (!co) return -1;

    zan_io_op_t *op = (zan_io_op_t *)calloc(1, sizeof(*op));
    op->sock = s;
    op->interest = ZAN_IO_WRITE;
    op->co = co;
    g_io_count++;

    BOOL ok = pConnectEx(s, (struct sockaddr *)&target, sizeof(target),
                         NULL, 0, NULL, &op->ov);
    if (!ok && WSAGetLastError() != ERROR_IO_PENDING) {
        free(op);
        g_io_count--;
        return -1;
    }
    zan_io_suspend_current();   /* completion frees op and resumes us */

    /* A failed ConnectEx reports its error via the completion packet, not via
     * SO_ERROR, so query the socket state directly: only a socket that truly
     * connected has a peer.  SO_UPDATE_CONNECT_CONTEXT makes getpeername (and
     * later shutdown/recv) valid on the connected socket. */
    if (setsockopt(s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) != 0)
        return -1;
    struct sockaddr_in peer;
    int plen = sizeof(peer);
    return getpeername(s, (struct sockaddr *)&peer, &plen) == 0 ? 0 : -1;
}

#else
/* POSIX async connect: non-blocking connect + writable readiness. */
int64_t zan_io_connect(int64_t fd, const char *ip, int port) {
    int s = (int)fd;
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons((uint16_t)port);
    target.sin_addr.s_addr = inet_addr(ip);

    zan_io_set_nonblocking(fd);
    int r = connect(s, (struct sockaddr *)&target, sizeof(target));
    if (r == 0) return 0;
    if (errno != EINPROGRESS && errno != EWOULDBLOCK)
        return -1;

    zan_io_wait_writable(fd);

    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
    return err == 0 ? 0 : -1;
}
#endif
#endif /* ZAN_IO_STACKLESS_ONLY */

/* ======================================================================
 * Multi-worker cooperative coroutine driver (opt-in, -DZAN_CO_DRIVER)
 * ----------------------------------------------------------------------
 * Built into the alternate reactor object (zanrt_io_mt) that zanc links when
 * a program is compiled with --async-workers. In that mode zanc does NOT emit
 * its inline single-threaded ready-queue driver; this one takes its place and
 * provides the same compiler-facing ABI (zan_co_sched_init / zan_co_ready /
 * zan_co_sched_run / zan_co_delay), but runs the ready queue across a pool of
 * OS worker threads that all pull completions from the shared IOCP and resume
 * coroutines concurrently -- so CPU-bound coroutine work (e.g. HTTP parsing)
 * scales across cores. The ready queue and timer list are guarded by a lock;
 * the IOCP itself is kernel-thread-safe. Worker count comes from the
 * ZAN_CO_WORKERS env var, defaulting to the number of logical processors.
 * ====================================================================== */
#ifdef ZAN_CO_DRIVER
#include <stdlib.h>

typedef struct zan_co_node {
    struct zan_co_node *next;
    void               *frame;
    zan_co_step_t       step;
} zan_co_node;

typedef struct zan_co_timer {
    struct zan_co_timer *next;
    long long            due_ms;
    void                *frame;
    zan_co_step_t        step;
} zan_co_timer;

static zan_co_node  *g_rq_head, *g_rq_tail;
static zan_co_timer *g_tq;

#if defined(_WIN32)
/* ---------------- Windows: real multi-worker pool over IOCP ------------- */
static CRITICAL_SECTION g_co_lock;
static volatile LONG    g_co_running;   /* workers currently inside a step() */
static volatile LONG    g_co_stop;
static int              g_co_inited;
static int              g_co_workers;

static long long co_now_ms(void) { return (long long)GetTickCount64(); }

static void co_wake_port(void) {
    if (g_iocp) PostQueuedCompletionStatus(g_iocp, 0, 0, NULL);
}

void zan_co_sched_init(void) {
    if (!g_co_inited) { InitializeCriticalSection(&g_co_lock); g_co_inited = 1; }
    g_rq_head = g_rq_tail = NULL;
    g_tq = NULL;
    g_co_running = 0;
    g_co_stop = 0;
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step) return;
    zan_co_node *n = (zan_co_node *)malloc(sizeof(*n));
    n->next = NULL; n->frame = frame; n->step = step;
    EnterCriticalSection(&g_co_lock);
    if (g_rq_tail) g_rq_tail->next = n; else g_rq_head = n;
    g_rq_tail = n;
    LeaveCriticalSection(&g_co_lock);
    co_wake_port();   /* nudge a worker to pick up the new frame */
}

void zan_co_delay(long long ms, void *frame, zan_co_step_t step) {
    if (!step) return;
    zan_co_timer *t = (zan_co_timer *)malloc(sizeof(*t));
    t->next = NULL;
    t->due_ms = co_now_ms() + (ms > 0 ? ms : 0);
    t->frame = frame; t->step = step;
    EnterCriticalSection(&g_co_lock);
    t->next = g_tq; g_tq = t;
    LeaveCriticalSection(&g_co_lock);
    co_wake_port();
}

/* Pop one ready frame, marking a worker busy (g_co_running) atomically with
 * the dequeue so the idle test can never race a just-popped-but-not-yet-run
 * step. Returns 1 and fills *frame/*step when a node was taken. */
static int co_pop(void **frame, zan_co_step_t *step) {
    int got = 0;
    EnterCriticalSection(&g_co_lock);
    if (g_rq_head) {
        zan_co_node *n = g_rq_head;
        g_rq_head = n->next;
        if (!g_rq_head) g_rq_tail = NULL;
        *frame = n->frame; *step = n->step;
        free(n);
        InterlockedIncrement(&g_co_running);
        got = 1;
    }
    LeaveCriticalSection(&g_co_lock);
    return got;
}

/* Move any due timers onto the ready queue; return ms until the next pending
 * timer, or -1 if none remain. */
static long long co_pump_timers(void) {
    long long now = co_now_ms(), next = -1;
    EnterCriticalSection(&g_co_lock);
    zan_co_timer **pp = &g_tq;
    while (*pp) {
        zan_co_timer *t = *pp;
        if (t->due_ms <= now) {
            *pp = t->next;
            zan_co_node *n = (zan_co_node *)malloc(sizeof(*n));
            n->next = NULL; n->frame = t->frame; n->step = t->step;
            if (g_rq_tail) g_rq_tail->next = n; else g_rq_head = n;
            g_rq_tail = n;
            free(t);
        } else {
            long long d = t->due_ms - now;
            if (next < 0 || d < next) next = d;
            pp = &t->next;
        }
    }
    LeaveCriticalSection(&g_co_lock);
    return next;
}

/* Block on the completion port, re-readying coroutines whose IO completed.
 * Wake packets (lpOverlapped == NULL) just unblock a worker to re-check. */
static void co_wait_io(long long timeout_ms) {
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    DWORD to = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    if (!GetQueuedCompletionStatusEx(g_iocp, entries, 64, &removed, to, FALSE))
        return;
    for (ULONG i = 0; i < removed; i++) {
        if (entries[i].lpOverlapped == NULL) continue;   /* wake packet */
        zan_io_op_t *op = CONTAINING_RECORD(entries[i].lpOverlapped,
                                            zan_io_op_t, ov);
        void *co = op->co;
        zan_co_step_t step = op->step;
        free(op);
        /* Re-ready the coroutine BEFORE dropping the in-flight count: if the
         * order were reversed, another worker could momentarily observe an
         * empty queue with io==0 and running==0 and wrongly declare the whole
         * pool idle (terminating a live server). Enqueue first keeps the work
         * visible across the whole transition. */
        if (step) zan_co_ready(co, step);
        IO_CNT_DEC();
    }
}

static int co_all_idle(void) {
    int idle;
    EnterCriticalSection(&g_co_lock);
    idle = (g_rq_head == NULL) && (g_tq == NULL) &&
           (g_co_running == 0) && (g_io_count == 0);
    LeaveCriticalSection(&g_co_lock);
    return idle;
}

static void co_worker(void) {
    long long last_pump = 0;
    for (;;) {
        if (g_co_stop) return;
        /* Pump due timers even while the ready queue stays busy. Otherwise, at
         * high request rates co_pop always succeeds first and co_pump_timers
         * (only reached when the queue drains) never runs, starving timer-driven
         * work -- notably the accept-readiness poll that keeps the listen loop
         * alive -- so new connections stop being accepted. Throttle to ~1ms to
         * bound lock traffic; g_tq is read unlocked as a cheap hint. */
        if (g_tq) {
            long long now = co_now_ms();
            if (now - last_pump >= 1) { co_pump_timers(); last_pump = now; }
        }
        void *frame; zan_co_step_t step;
        if (co_pop(&frame, &step)) {
            step(frame);
            InterlockedDecrement(&g_co_running);
            continue;
        }
        long long tnext = co_pump_timers();
        if (co_pop(&frame, &step)) {
            step(frame);
            InterlockedDecrement(&g_co_running);
            continue;
        }
        if (co_all_idle()) {
            g_co_stop = 1;
            for (int i = 0; i < g_co_workers; i++) co_wake_port();
            return;
        }
        /* Choose how long to block. A pending timer bounds it tightest; else
         * with IO in flight block for real completions (with a 1s ceiling as a
         * missed-wake safety net); else nothing is pending, so use a short cap
         * that keeps termination detection responsive. */
        long long to;
        if (tnext >= 0)               to = tnext;
        else if (g_io_count > 0)      to = 1000;
        else                          to = 50;
        co_wait_io(to);
    }
}

static DWORD WINAPI co_worker_thunk(LPVOID p) { (void)p; co_worker(); return 0; }

void zan_co_sched_run(void) {
    zan_io_init();   /* ensure the port exists before workers block on it */
    int w = 0;
    const char *e = getenv("ZAN_CO_WORKERS");
    if (e && *e) w = atoi(e);
    if (w <= 0) {
        SYSTEM_INFO si; GetSystemInfo(&si);
        w = (int)si.dwNumberOfProcessors;
    }
    if (w < 1) w = 1;
    if (w > 64) w = 64;
    g_co_workers = w;
    g_co_stop = 0;

    HANDLE th[64]; int nt = 0;
    for (int i = 1; i < w; i++) {
        th[nt] = CreateThread(NULL, 0, co_worker_thunk, NULL, 0, NULL);
        if (th[nt]) nt++;
    }
    co_worker();   /* the calling thread is a worker too */
    for (int i = 0; i < nt; i++) {
        WaitForSingleObject(th[i], INFINITE);
        CloseHandle(th[i]);
    }
}

size_t zan_co_pending(void) {
    size_t n = 0;
    EnterCriticalSection(&g_co_lock);
    for (zan_co_node *p = g_rq_head; p; p = p->next) n++;
    LeaveCriticalSection(&g_co_lock);
    return n;
}

#else
/* ---------------- Non-Windows: single-threaded fallback ---------------- */
#include <unistd.h>
#include <poll.h>

void zan_co_sched_init(void) { g_rq_head = g_rq_tail = NULL; g_tq = NULL; }

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step) return;
    zan_co_node *n = (zan_co_node *)malloc(sizeof(*n));
    n->next = NULL; n->frame = frame; n->step = step;
    if (g_rq_tail) g_rq_tail->next = n; else g_rq_head = n;
    g_rq_tail = n;
}

void zan_co_delay(long long ms, void *frame, zan_co_step_t step) {
    if (!step) return;
    zan_co_timer *t = (zan_co_timer *)malloc(sizeof(*t));
    t->next = g_tq; t->due_ms = ms; t->frame = frame; t->step = step;
    g_tq = t;
}

size_t zan_co_pending(void) {
    size_t n = 0;
    for (zan_co_node *p = g_rq_head; p; p = p->next) n++;
    return n;
}

void zan_co_sched_run(void) {
    for (;;) {
        while (g_rq_head) {
            zan_co_node *n = g_rq_head;
            g_rq_head = n->next; if (!g_rq_head) g_rq_tail = NULL;
            void *frame = n->frame; zan_co_step_t step = n->step;
            free(n);
            step(frame);
        }
        if (g_tq) {
            /* earliest (relative-ms) timer, matching the legacy model */
            zan_co_timer **bp = &g_tq, **pp = &g_tq;
            for (pp = &(*pp)->next; *pp; pp = &(*pp)->next)
                if ((*pp)->due_ms < (*bp)->due_ms) bp = pp;
            zan_co_timer *best = *bp; *bp = best->next;
            if (best->due_ms > 0) poll(NULL, 0, (int)best->due_ms);
            zan_co_ready(best->frame, best->step);
            free(best);
            continue;
        }
        if (zan_io_pump() > 0) continue;
        return;
    }
}
#endif /* _WIN32 */
#endif /* ZAN_CO_DRIVER */
