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
#include "rt_sched.h"
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

/* ---- forward declarations from rt_sched (internal) ---- */
extern void zan_io_suspend_current(void);   /* implemented in rt_sched.c */
extern void zan_io_resume(void *co);        /* implemented in rt_sched.c */
extern void *zan_io_get_current_co(void);   /* implemented in rt_sched.c */

/* ---- interest flags ---- */
#define ZAN_IO_READ  1
#define ZAN_IO_WRITE 2

/* Number of in-flight watchers / operations; used by zan_io_has_pending()
 * and to short-circuit an empty poll on every backend. */
static int g_io_count;

#if !defined(_WIN32)
/* ---- readiness watcher (epoll / kqueue / select backends) ---- */
typedef struct zan_io_entry {
    int fd;
    int interest;           /* ZAN_IO_READ or ZAN_IO_WRITE */
    void *co;               /* suspended coroutine handle */
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
    g_io_entries = NULL;
    g_io_count = 0;
    g_epoll_fd = epoll_create1(0);
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
}

int zan_io_set_nonblocking(int64_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static void io_register(int fd, int interest, void *co) {
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
    e->fd = fd;
    e->interest = interest;
    e->co = co;
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
                       (int)(timeout_ms < 0 ? 0 : timeout_ms));
    int woke = 0;
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        zan_io_entry_t **pp = &g_io_entries;
        while (*pp) {
            if ((*pp)->fd == fd) {
                void *co = (*pp)->co;
                zan_io_entry_t *e = *pp;
                *pp = e->next;
                epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                free(e);
                g_io_count--;
                zan_io_resume(co);
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
    g_io_entries = NULL;
    g_io_count = 0;
    g_kq_fd = kqueue();
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
}

int zan_io_set_nonblocking(int64_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static void io_register(int fd, int interest, void *co) {
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
    e->fd = fd;
    e->interest = interest;
    e->co = co;
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
                void *co = (*pp)->co;
                zan_io_entry_t *e = *pp;
                *pp = e->next;
                free(e);
                g_io_count--;
                zan_io_resume(co);
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
    void      *co;
} zan_io_op_t;

static HANDLE g_iocp;

/* Sockets already associated with the completion port (association is
 * one-shot and permanent for the life of the socket). */
static SOCKET *g_assoc;
static int     g_assoc_len;
static int     g_assoc_cap;

static int assoc_contains(SOCKET s) {
    for (int i = 0; i < g_assoc_len; i++)
        if (g_assoc[i] == s) return 1;
    return 0;
}

static void ensure_assoc(SOCKET s) {
    if (assoc_contains(s)) return;
    if (g_assoc_len == g_assoc_cap) {
        g_assoc_cap = g_assoc_cap ? g_assoc_cap * 2 : 64;
        g_assoc = (SOCKET *)realloc(g_assoc, (size_t)g_assoc_cap * sizeof(SOCKET));
    }
    CreateIoCompletionPort((HANDLE)s, g_iocp, (ULONG_PTR)s, 0);
    g_assoc[g_assoc_len++] = s;
}

void zan_io_init(void) {
    g_io_count = 0;
    g_assoc = NULL;
    g_assoc_len = 0;
    g_assoc_cap = 0;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

void zan_io_shutdown(void) {
    if (g_iocp) { CloseHandle(g_iocp); g_iocp = NULL; }
    free(g_assoc);
    g_assoc = NULL;
    g_assoc_len = g_assoc_cap = 0;
    g_io_count = 0;
    WSACleanup();
}

int zan_io_set_nonblocking(int64_t fd) {
    u_long mode = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &mode);
}

/* Post a zero-byte overlapped op to learn when `fd` is read/write ready. */
static void io_register(int fd, int interest, void *co) {
    SOCKET s = (SOCKET)fd;
    ensure_assoc(s);

    zan_io_op_t *op = (zan_io_op_t *)calloc(1, sizeof(*op));
    op->sock = s;
    op->interest = interest;
    op->co = co;
    g_io_count++;

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
    if (r == 0) return;                  /* completed inline; still queued to IOCP */
    e = WSAGetLastError();
    if (e == WSA_IO_PENDING) return;
    /* Hard error: queue a completion so the coroutine is still resumed. */
    PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov);
}

int zan_io_poll(int64_t timeout_ms) {
    if (g_io_count == 0) return 0;
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    DWORD to = (DWORD)(timeout_ms < 0 ? 0 : timeout_ms);
    if (!GetQueuedCompletionStatusEx(g_iocp, entries, 64, &removed, to, FALSE))
        return 0;
    int woke = 0;
    for (ULONG i = 0; i < removed; i++) {
        zan_io_op_t *op = CONTAINING_RECORD(entries[i].lpOverlapped,
                                            zan_io_op_t, ov);
        void *co = op->co;
        free(op);
        g_io_count--;
        zan_io_resume(co);
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
    g_io_entries = NULL;
    g_io_count = 0;
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
}

int zan_io_set_nonblocking(int64_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static void io_register(int fd, int interest, void *co) {
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
    e->fd = fd;
    e->interest = interest;
    e->co = co;
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
            *pp = cur->next;
            free(cur);
            g_io_count--;
            zan_io_resume(co);
            woke++;
        } else {
            pp = &cur->next;
        }
    }
    return woke;
}

#endif /* platform */

/* ================= coroutine-facing ABI ================= */

int64_t zan_io_wait_readable(int64_t fd) {
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register((int)fd, ZAN_IO_READ, co);
    zan_io_suspend_current();
    return 0;
}

int64_t zan_io_wait_writable(int64_t fd) {
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register((int)fd, ZAN_IO_WRITE, co);
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
    io_register((int)fd, ZAN_IO_READ, co);
    zan_io_suspend_current();
    return 1;
}

int zan_io_has_pending(void) {
    return g_io_count > 0;
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

    setsockopt(s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
    int err = 0;
    int len = sizeof(err);
    getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
    return err == 0 ? 0 : -1;
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
