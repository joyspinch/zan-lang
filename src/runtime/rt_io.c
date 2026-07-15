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
#include <stdio.h>
#include <string.h>


/* The reactor serves two clients:
 *   - stackless CPS frames (the async/await state machine) via zan_io_wait_co,
 *     woken through the co ready queue (zan_co_ready);
 *   - stackful rt_sched fibers via zan_io_wait_readable/writable, woken by
 *     switching fibers (zan_io_resume).
 * When built with ZAN_IO_STACKLESS_ONLY (the object shipped with zanc and
 * linked into produced async programs), the fiber half is dropped so the
 * reactor has no dependency on rt_sched. */
#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#endif
#if defined(__linux__)
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#include <fcntl.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>          /* ConnectEx + WSAID_CONNECTEX */
#include <windows.h>
#include <malloc.h>           /* _aligned_malloc / _aligned_free (op pool) */
#else
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef ZAN_CO_DRIVER
#include <stdlib.h>
#ifndef _WIN32
#include <poll.h>
#endif
#endif
#ifndef ZAN_IO_STACKLESS_ONLY
#include "rt_sched.h"
/* ---- forward declarations from rt_sched (internal) ---- */
extern void zan_io_suspend_current(void);   /* implemented in rt_sched.c */
extern void zan_io_resume(void *co);        /* implemented in rt_sched.c */
extern void *zan_io_get_current_co(void);   /* implemented in rt_sched.c */
#endif
#include "../common/host_oom.h"

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
    void *rbuf;             /* recv sink (zan_io_recv_co); NULL => plain probe */
    int   rlen;
    int64_t *out_n;         /* recv byte-count sink (NULL => plain probe) */
    int64_t *out_accept;    /* accepted fd sink (NULL => not an accept op) */
    struct zan_io_entry *next;
} zan_io_entry_t;

static zan_io_entry_t *g_io_entries;    /* linked list of active watchers */

/* Pending recv-op parameters, consumed by the next io_register. The POSIX
 * backends run only under the single-thread inline driver, so plain statics are
 * safe (the multi-worker driver is Windows/IOCP-only). */
static void   *g_pending_rbuf;
static int      g_pending_rlen;
static int64_t *g_pending_out_n;
static int64_t *g_pending_accept_out;

/* On POSIX there is no overlapped recv, so zan_io_recv_co registers a readiness
 * watcher and performs the recv here once the fd is readable -- same net effect
 * (one recv per await, byte count delivered via out_n). No-op for plain probes. */
static void io_deliver_recv(zan_io_entry_t *e) {
    if (e->out_accept) {
        *e->out_accept = (int64_t)accept(e->fd, NULL, NULL);
        return;
    }
    if (!e->out_n) return;
    ssize_t rn = recv(e->fd, e->rbuf, (size_t)e->rlen, 0);
    *e->out_n = (rn < 0) ? 0 : (int64_t)rn;
}
#endif

#if defined(_WIN32)
static volatile LONG g_socket_cleanup_requested;

void zan_io_socket_cleanup(void) {
#if defined(ZAN_CO_DRIVER)
    InterlockedIncrement(&g_socket_cleanup_requested);
#else
    WSACleanup();
#endif
}
#else
void zan_io_socket_cleanup(void) {}
#endif

int64_t zan_io_socket_send(int64_t fd, const void *buf, int64_t len,
                           int64_t flags) {
#if defined(_WIN32)
    return (int64_t)send((SOCKET)fd, (const char *)buf, (int)len, (int)flags);
#else
    return (int64_t)send((int)fd, buf, (size_t)len, (int)flags);
#endif
}

int64_t zan_io_socket_recv(int64_t fd, void *buf, int64_t len,
                           int64_t flags) {
#if defined(_WIN32)
    return (int64_t)recv((SOCKET)fd, (char *)buf, (int)len, (int)flags);
#else
    return (int64_t)recv((int)fd, buf, (size_t)len, (int)flags);
#endif
}

int64_t zan_io_socket_ready(int64_t fd, int64_t write_ready) {
    fd_set fds;
    FD_ZERO(&fds);
    struct timeval timeout = {0, 0};
#if defined(_WIN32)
    SOCKET sock = (SOCKET)fd;
    if (sock == INVALID_SOCKET) return -1;
    FD_SET(sock, &fds);
    return (int64_t)select(0, write_ready ? NULL : &fds,
                          write_ready ? &fds : NULL, NULL, &timeout);
#else
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    int sock = (int)fd;
    FD_SET(sock, &fds);
    return (int64_t)select(sock + 1, write_ready ? NULL : &fds,
                          write_ready ? &fds : NULL, NULL, &timeout);
#endif
}

/* ---- platform backend ---- */

#if defined(__linux__)
/* ==================== EPOLL ==================== */

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
    e->rbuf = g_pending_rbuf;
    e->rlen = g_pending_rlen;
    e->out_n = g_pending_out_n;
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
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
                io_deliver_recv(e);
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
    e->rbuf = g_pending_rbuf;
    e->rlen = g_pending_rlen;
    e->out_n = g_pending_out_n;
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
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
                io_deliver_recv(e);
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
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

/* One in-flight overlapped operation.  Recovered from its OVERLAPPED via
 * CONTAINING_RECORD when its completion is dequeued.  `ov` MUST be first. */
typedef struct zan_io_op {
    OVERLAPPED ov;
    SOCKET     sock;
    int        interest;     /* ZAN_IO_READ / ZAN_IO_WRITE */
    int        kind;         /* readiness/recv/accept */
    void      *co;           /* fiber handle, or stackless frame pointer */
    zan_co_step_t step;      /* stackless resume fn (NULL => stackful fiber) */
    int64_t   *out_n;        /* recv byte-count sink (NULL => readiness probe) */
    SOCKET     accepted;
    void      *accept_buf;
} zan_io_op_t;

enum {
    ZAN_IO_OP_READY,
    ZAN_IO_OP_RECV,
    ZAN_IO_OP_ACCEPT
};

static HANDLE g_iocp;
#if defined(ZAN_CO_DRIVER)
static volatile LONG g_skip_mode = -1;  /* -1 unknown, 1 on, 0 unsupported */
#endif

/* ---- overlapped-op pool ----
 * Each overlapped IO needs a heap zan_io_op_t; recycling them removes a
 * malloc/free pair from every request on the hot path. The single-threaded
 * reactor uses a plain intrusive free list. The multi-worker driver uses a
 * Windows interlocked SLIST (lock-free) because several workers issue ops
 * (recv/accept/connect) and complete/free them concurrently. A pooled op's
 * leading bytes hold the SLIST_ENTRY; that overlaps the (unused-while-pooled)
 * OVERLAPPED at offset 0, so pool ops are 16-byte aligned via _aligned_malloc
 * to satisfy the interlocked-SLIST alignment contract. */
#if defined(ZAN_CO_DRIVER)
/* The interlocked SLIST head must be MEMORY_ALLOCATION_ALIGNMENT-aligned (16 on
 * x64) for the 128-bit compare-exchange; SLIST_HEADER's natural alignment is
 * only 8, so force it (GCC/MSVC spell the attribute differently). */
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
static zan_io_op_t *g_op_pool;   /* singly-linked free list (next @ offset 0) */

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

/* ---- completion-port association cache ----
 * A socket only needs to be associated with the IOCP once, yet the recv
 * registration path re-ran CreateIoCompletionPort on every op -- a syscall per
 * request. Cache the sockets the reactor freshly created (accept/connect) so
 * their later registrations skip the redundant association. Windows recycles
 * closed handle numbers, which a naive value cache would mishandle; that is
 * safe here because mark_assoc() -- called for every newly created socket --
 * always issues the real association for the new kernel object, so a recycled
 * number never makes ensure_assoc() skip associating a fresh socket (which is
 * why ensure_assoc() itself never records sockets it did not force-associate).
 * The table is guarded by an SRWLOCK under the multi-worker driver (many
 * workers register concurrently); in the single-threaded reactor the lock
 * macros expand to nothing. */
#if defined(ZAN_CO_DRIVER)
static SRWLOCK g_assoc_lock = SRWLOCK_INIT;
#define ASSOC_RLOCK()   AcquireSRWLockShared(&g_assoc_lock)
#define ASSOC_RUNLOCK() ReleaseSRWLockShared(&g_assoc_lock)
#define ASSOC_WLOCK()   AcquireSRWLockExclusive(&g_assoc_lock)
#define ASSOC_WUNLOCK() ReleaseSRWLockExclusive(&g_assoc_lock)
#else
#define ASSOC_RLOCK()
#define ASSOC_RUNLOCK()
#define ASSOC_WLOCK()
#define ASSOC_WUNLOCK()
#endif

static SOCKET *g_assoc_tab;      /* open-addressed, power-of-two, linear probe */
static size_t  g_assoc_cap;
static size_t  g_assoc_len;

static void assoc_insert_locked(SOCKET s);

static void assoc_grow_locked(void) {
    size_t ncap = g_assoc_cap ? g_assoc_cap * 2 : 64;
    SOCKET *old = g_assoc_tab;
    size_t oldcap = g_assoc_cap;
    SOCKET *nt = (SOCKET *)malloc(ncap * sizeof(SOCKET));
    for (size_t i = 0; i < ncap; i++) nt[i] = INVALID_SOCKET;
    g_assoc_tab = nt;
    g_assoc_cap = ncap;
    g_assoc_len = 0;
    for (size_t i = 0; i < oldcap; i++)
        if (old[i] != INVALID_SOCKET) assoc_insert_locked(old[i]);
    free(old);
}

static void assoc_insert_locked(SOCKET s) {
    if ((g_assoc_len + 1) * 4 >= g_assoc_cap * 3) assoc_grow_locked();
    size_t mask = g_assoc_cap - 1;
    size_t i = ((size_t)s / 4) & mask;
    while (g_assoc_tab[i] != INVALID_SOCKET) {
        if (g_assoc_tab[i] == s) return;
        i = (i + 1) & mask;
    }
    g_assoc_tab[i] = s;
    g_assoc_len++;
}

static int assoc_contains(SOCKET s) {
    int found = 0;
    ASSOC_RLOCK();
    if (g_assoc_cap) {
        size_t mask = g_assoc_cap - 1;
        size_t i = ((size_t)s / 4) & mask;
        while (g_assoc_tab[i] != INVALID_SOCKET) {
            if (g_assoc_tab[i] == s) { found = 1; break; }
            i = (i + 1) & mask;
        }
    }
    ASSOC_RUNLOCK();
    return found;
}

/* Associate a brand-new kernel socket with the port and record it. Always
 * performs the association: a recycled handle number may still sit in the cache
 * from a since-closed socket, but this new kernel object is unassociated. */
static void mark_assoc(SOCKET s) {
    if (CreateIoCompletionPort((HANDLE)s, g_iocp, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;
    }
    ASSOC_WLOCK();
    assoc_insert_locked(s);
    ASSOC_WUNLOCK();
}

/* Associate a socket with the completion port on first use. Sockets the reactor
 * created itself (accept/connect) were already force-associated by mark_assoc
 * and sit in the cache, so their hot-path registrations skip the syscall.
 * Everything else -- e.g. the listener -- (re)associates and is treated as
 * success when already bound (ERROR_INVALID_PARAMETER). ensure_assoc never
 * records a socket, so a recycled handle number can never make it skip
 * associating a socket that was not force-associated by mark_assoc. */
static void ensure_assoc(SOCKET s) {
#if defined(ZAN_CO_DRIVER)
    /* Do NOT enable FILE_SKIP_COMPLETION_PORT_ON_SUCCESS under the multi-worker
     * driver. Skip-on-success means an inline completion posts no IOCP packet,
     * so the op site must resume the waiter itself -- but that resume happens
     * mid-step, while the coroutine is still executing (before it saves its live
     * slots, advances its resume state, and returns). Re-queueing the frame that
     * early lets another worker pop and re-enter it at the wrong state, racing
     * the still-running step and eventually deadlocking the pool under load.
     * Leaving skip-on-success off makes every completion (inline or pending)
     * arrive as an IOCP packet that co_wait_io delivers only after the frame has
     * fully suspended -- the serialization point the pool needs. g_skip_mode
     * stays 0 so io_register / zan_io_recv_co never take the inline shortcut. */
    if (g_skip_mode < 0) InterlockedExchange(&g_skip_mode, 0);
#endif
    if (assoc_contains(s)) return;
    if (CreateIoCompletionPort((HANDLE)s, g_iocp, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;   /* ERROR_INVALID_PARAMETER == already bound to this port */
    }
}

void zan_io_init(void) {
    if (g_io_started) return;
    g_io_count = 0;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
#if defined(ZAN_CO_DRIVER)
    InitializeSListHead(&g_op_slist);   /* lock-free op pool for the workers */
#endif
    g_io_started = 1;
}

void zan_io_shutdown(void) {
    if (g_iocp) { CloseHandle(g_iocp); g_iocp = NULL; }
    g_io_count = 0;
    /* Drop pooled ops and the association cache: the completion port is gone,
     * so the cached socket->associated facts and recycled op structs are all
     * stale for any future zan_io_init(). */
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
    ASSOC_WLOCK();
    free(g_assoc_tab);
    g_assoc_tab = NULL;
    g_assoc_cap = 0;
    g_assoc_len = 0;
    ASSOC_WUNLOCK();
    WSACleanup();
#if defined(ZAN_CO_DRIVER)
    LONG requested = InterlockedExchange(&g_socket_cleanup_requested, 0);
    while (requested-- > 0) WSACleanup();
#endif
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

    zan_io_op_t *op = op_alloc();
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
            op_free(op);
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

/* Overlapped receive. Issues the real WSARecv into `buf` (no zero-byte probe),
 * so exactly one op delivers the data and its byte count -- eliminating the
 * probe/synchronous-recv window that leaked completions under the multi-worker
 * driver at high load. The byte count reaches the coroutine via `*out_n`, set
 * either inline (immediate completion) or when the completion is dequeued. */
void zan_io_recv_co(int64_t fd, void *buf, int len, void *frame,
                    zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
    SOCKET s = (SOCKET)fd;
    ensure_assoc(s);

    zan_io_op_t *op = op_alloc();
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
    if (r == 0) {
#if defined(ZAN_CO_DRIVER)
        /* Immediate completion. With skip-on-success no packet is queued, so
         * deliver the count and resume here; otherwise a packet is still queued
         * and zan_io_poll/co_wait_io will deliver it (avoid double-completion). */
        if (g_skip_mode == 1) {
            if (out_n) *out_n = (int64_t)got;
            op_free(op);
            IO_CNT_DEC();
            if (step) zan_co_ready(frame, step);
        }
        return;
#else
        return;   /* single-thread reactor: packet still queued to the IOCP */
#endif
    }
    int e = WSAGetLastError();
    if (e == WSA_IO_PENDING) return;
    /* Hard error: report 0 bytes (peer-close semantics) via a queued packet. */
    if (out_n) *out_n = 0;
    PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov);
}

void zan_io_accept_co(int64_t fd, void *frame, zan_co_step_t step,
                      int64_t *out_fd) {
    zan_io_init();
    SOCKET listener = (SOCKET)fd;
    ensure_assoc(listener);

    LPFN_ACCEPTEX accept_ex = NULL;
    GUID guid = WSAID_ACCEPTEX;
    DWORD got = 0;
    if (WSAIoctl(listener, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guid, sizeof(guid), &accept_ex, sizeof(accept_ex),
                 &got, NULL, NULL) == SOCKET_ERROR) {
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }

    SOCKET accepted = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                                 WSA_FLAG_OVERLAPPED);
    if (accepted == INVALID_SOCKET) {
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }

    const DWORD addr_len = (DWORD)sizeof(struct sockaddr_in) + 16;
    zan_io_op_t *op = op_alloc();
    op->sock = listener;
    op->kind = ZAN_IO_OP_ACCEPT;
    op->co = frame;
    op->step = step;
    op->out_n = out_fd;
    op->accepted = accepted;
    op->accept_buf = calloc(1, addr_len * 2);
    IO_CNT_INC();

    got = 0;
    BOOL ok = accept_ex(listener, accepted, op->accept_buf, 0,
                        addr_len, addr_len, &got, &op->ov);
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
            /* Force-associate the freshly accepted socket once, so its recv
             * registrations can skip CreateIoCompletionPort (ensure_assoc). */
            mark_assoc(accepted);
            result = (int64_t)accepted;
        } else {
            closesocket(accepted);
        }
        if (op->out_n) *op->out_n = result;
        free(op->accept_buf);
        return;
    }
    if (op->out_n) *op->out_n = (int64_t)transferred;
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
        io_complete_op(op, entries[i].dwNumberOfBytesTransferred,
                       entries[i].Internal);
        op_free(op);
        g_io_count--;
        io_wake(co, step);
        woke++;
    }
    return woke;
}

#else
/* ==================== FALLBACK SELECT (POSIX) ==================== */

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
    e->rbuf = g_pending_rbuf;
    e->rlen = g_pending_rlen;
    e->out_n = g_pending_out_n;
    e->out_accept = g_pending_accept_out;
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
    g_pending_accept_out = NULL;
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
            io_deliver_recv(cur);
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

#if !defined(_WIN32)
/* POSIX overlapped-recv shim: register a read-readiness watcher carrying the
 * recv sink, so the poll loop performs the recv and delivers the byte count
 * (see io_deliver_recv). Windows has a true overlapped implementation above. */
void zan_io_recv_co(int64_t fd, void *buf, int len, void *frame,
                    zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
    g_pending_rbuf = buf;
    g_pending_rlen = len;
    g_pending_out_n = out_n;
    io_register((int)fd, ZAN_IO_READ, frame, step);
}

void zan_io_accept_co(int64_t fd, void *frame, zan_co_step_t step,
                      int64_t *out_fd) {
    zan_io_init();
    g_pending_accept_out = out_fd;
    io_register((int)fd, ZAN_IO_READ, frame, step);
}
#endif

int zan_io_pump_timeout(int64_t timeout_ms) {
    if (zan_io_has_pending())
        return zan_io_poll(timeout_ms);
    if (timeout_ms <= 0)
        return 0;
#ifdef _WIN32
    Sleep((DWORD)timeout_ms);
#else
    struct timespec ts = {
        (time_t)(timeout_ms / 1000),
        (long)((timeout_ms % 1000) * 1000000)
    };
    nanosleep(&ts, NULL);
#endif
    return 0;
}

int zan_io_pump(void) {
    return zan_io_pump_timeout(-1);
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

    /* Freshly-connecting socket: force-associate once and record it. */
    mark_assoc(s);

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

    zan_io_op_t *op = op_alloc();
    op->sock = s;
    op->interest = ZAN_IO_WRITE;
    op->co = co;
    g_io_count++;

    BOOL ok = pConnectEx(s, (struct sockaddr *)&target, sizeof(target),
                         NULL, 0, NULL, &op->ov);
    if (!ok && WSAGetLastError() != ERROR_IO_PENDING) {
        op_free(op);
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
 * scales across cores. Ready frames and active-frame state are sharded by
 * frame address so workers do not serialize on one scheduler lock; the timer
 * list remains shared and the IOCP itself is kernel-thread-safe. Worker count
 * comes from ZAN_CO_WORKERS, defaulting to the logical processor count.
 * ====================================================================== */
#ifdef ZAN_CO_DRIVER

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
typedef struct zan_co_worker_queue {
    CRITICAL_SECTION lock;
    zan_co_node      *head;
    zan_co_node      *tail;
    void             *active[64];
    int               active_count;
} zan_co_worker_queue;

static CRITICAL_SECTION g_co_lock;
static volatile LONG    g_co_running;   /* workers currently inside a step() */
static volatile LONG    g_co_idle;      /* workers blocked in co_wait_io */
static volatile LONG    g_co_stop;
static int              g_co_inited;
static int              g_co_workers;
static zan_co_worker_queue g_co_queues[64];

static long long co_now_ms(void) { return (long long)GetTickCount64(); }

static void co_wake_port(void) {
    if (g_iocp) PostQueuedCompletionStatus(g_iocp, 0, 0, NULL);
}

static int co_worker_count(void) {
    int w = 0;
    const char *e = getenv("ZAN_CO_WORKERS");
    if (e && *e) w = atoi(e);
    if (w <= 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        w = (int)si.dwNumberOfProcessors;
    }
    if (w < 1) w = 1;
    if (w > 64) w = 64;
    return w;
}

static int co_queue_index(void *frame) {
    uintptr_t value = (uintptr_t)frame;
    return (int)((value >> 4) % (uintptr_t)g_co_workers);
}

void zan_co_sched_init(void) {
    if (!g_co_inited) {
        InitializeCriticalSection(&g_co_lock);
        for (int i = 0; i < 64; i++)
            InitializeCriticalSection(&g_co_queues[i].lock);
        g_co_inited = 1;
    }
    g_co_workers = co_worker_count();
    g_rq_head = g_rq_tail = NULL;
    g_tq = NULL;
    g_co_running = 0;
    g_co_idle = 0;
    g_co_stop = 0;
    for (int i = 0; i < 64; i++) {
        g_co_queues[i].head = NULL;
        g_co_queues[i].tail = NULL;
        g_co_queues[i].active_count = 0;
    }
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step) return;
    zan_co_node *n = (zan_co_node *)malloc(sizeof(*n));
    n->next = NULL; n->frame = frame; n->step = step;
    zan_co_worker_queue *q = &g_co_queues[co_queue_index(frame)];
    EnterCriticalSection(&q->lock);
    for (zan_co_node *queued = q->head; queued; queued = queued->next) {
        if (queued->frame == frame) {
            LeaveCriticalSection(&q->lock);
            free(n);
            return;
        }
    }
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    LeaveCriticalSection(&q->lock);
    /* Only nudge the port when a worker is actually blocked waiting for work.
     * While every worker is busy (g_co_idle == 0) the frame will be picked up
     * by a worker's own run-loop iteration, so posting a completion packet per
     * ready is pure overhead -- that wake storm is what made --async-workers
     * slower than single-threaded under load. The idle count is registered
     * under g_co_lock together with a final queue check in co_worker, so a
     * worker about to block is guaranteed visible here (no lost wakeup). */
    if (g_co_idle > 0) co_wake_port();
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
    if (g_co_idle > 0) co_wake_port();
}

/* Pop one ready frame, marking a worker busy (g_co_running) atomically with
 * the dequeue so the idle test can never race a just-popped-but-not-yet-run
 * step. Returns 1 and fills *frame/*step when a node was taken. */
static int co_pop(int worker, void **frame, zan_co_step_t *step) {
    for (int offset = 0; offset < g_co_workers; offset++) {
        int index = (worker + offset) % g_co_workers;
        zan_co_worker_queue *q = &g_co_queues[index];
        EnterCriticalSection(&q->lock);
        zan_co_node *prev = NULL;
        zan_co_node *n = q->head;
        while (n) {
            int active = 0;
            for (int i = 0; i < q->active_count; i++) {
                if (q->active[i] == n->frame) { active = 1; break; }
            }
            if (!active) break;
            prev = n;
            n = n->next;
        }
        if (n) {
            if (prev) prev->next = n->next;
            else q->head = n->next;
            if (q->tail == n) q->tail = prev;
            *frame = n->frame;
            *step = n->step;
            free(n);
            q->active[q->active_count++] = *frame;
            InterlockedIncrement(&g_co_running);
            LeaveCriticalSection(&q->lock);
            return 1;
        }
        LeaveCriticalSection(&q->lock);
    }
    return 0;
}

static int co_has_runnable(void) {
    for (int index = 0; index < g_co_workers; index++) {
        zan_co_worker_queue *q = &g_co_queues[index];
        EnterCriticalSection(&q->lock);
        for (zan_co_node *n = q->head; n; n = n->next) {
            int active = 0;
            for (int i = 0; i < q->active_count; i++) {
                if (q->active[i] == n->frame) { active = 1; break; }
            }
            if (!active) {
                LeaveCriticalSection(&q->lock);
                return 1;
            }
        }
        LeaveCriticalSection(&q->lock);
    }
    return 0;
}

static void co_step_done(void *frame) {
    int wake;
    zan_co_worker_queue *q = &g_co_queues[co_queue_index(frame)];
    EnterCriticalSection(&q->lock);
    for (int i = 0; i < q->active_count; i++) {
        if (q->active[i] == frame) {
            q->active[i] = q->active[--q->active_count];
            break;
        }
    }
    InterlockedDecrement(&g_co_running);
    wake = q->head != NULL && g_co_idle > 0;
    LeaveCriticalSection(&q->lock);
    if (wake) co_wake_port();
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
            zan_co_worker_queue *q = &g_co_queues[co_queue_index(t->frame)];
            EnterCriticalSection(&q->lock);
            if (q->tail) q->tail->next = n; else q->head = n;
            q->tail = n;
            LeaveCriticalSection(&q->lock);
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
    BOOL ok = GetQueuedCompletionStatusEx(g_iocp, entries, 64, &removed, to, FALSE);
    /* We are no longer parked: drop the idle count before re-readying the
     * completed coroutines below. This lets those zan_co_ready calls skip the
     * wake syscall when this worker is the only idle one -- it will drain the
     * whole batch itself on its next loop iterations -- instead of posting a
     * wake packet per completion. */
    InterlockedDecrement(&g_co_idle);
    if (!ok) return;
    for (ULONG i = 0; i < removed; i++) {
        if (entries[i].lpOverlapped == NULL) continue;   /* wake packet */
        zan_io_op_t *op = CONTAINING_RECORD(entries[i].lpOverlapped,
                                            zan_io_op_t, ov);
        void *co = op->co;
        zan_co_step_t step = op->step;
        io_complete_op(op, entries[i].dwNumberOfBytesTransferred,
                       entries[i].Internal);
        op_free(op);
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
    int idle = 1;
    EnterCriticalSection(&g_co_lock);
    if (g_tq != NULL || g_co_running != 0 || g_io_count != 0)
        idle = 0;
    for (int i = 0; idle && i < g_co_workers; i++) {
        zan_co_worker_queue *q = &g_co_queues[i];
        EnterCriticalSection(&q->lock);
        if (q->head != NULL) idle = 0;
        LeaveCriticalSection(&q->lock);
    }
    LeaveCriticalSection(&g_co_lock);
    return idle;
}

static void co_worker(int worker) {
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
        if (co_pop(worker, &frame, &step)) {
            step(frame);
            co_step_done(frame);
            continue;
        }
        long long tnext = co_pump_timers();
        if (co_pop(worker, &frame, &step)) {
            step(frame);
            co_step_done(frame);
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
        /* Publish the idle state before the final shard scan. A producer that
         * enqueues first is found by the scan; one that enqueues afterward
         * observes g_co_idle > 0 and posts a wake packet. */
        InterlockedIncrement(&g_co_idle);
        int have_work = co_has_runnable();
        if (have_work) {
            InterlockedDecrement(&g_co_idle);
            continue;
        }
        co_wait_io(to);            /* decrements g_co_idle on return */
    }
}

static DWORD WINAPI co_worker_thunk(LPVOID p) {
    co_worker((int)(uintptr_t)p);
    return 0;
}

void zan_co_sched_run(void) {
    zan_io_init();   /* ensure the port exists before workers block on it */
    int w = g_co_workers;
    g_co_stop = 0;

    HANDLE th[64]; int nt = 0;
    for (int i = 1; i < w; i++) {
        th[nt] = CreateThread(NULL, 0, co_worker_thunk,
                              (LPVOID)(uintptr_t)i, 0, NULL);
        if (th[nt]) nt++;
    }
    co_worker(0);   /* the calling thread is a worker too */
    for (int i = 0; i < nt; i++) {
        WaitForSingleObject(th[i], INFINITE);
        CloseHandle(th[i]);
    }
    zan_io_shutdown();
}

size_t zan_co_pending(void) {
    size_t n = 0;
    for (int i = 0; i < g_co_workers; i++) {
        zan_co_worker_queue *q = &g_co_queues[i];
        EnterCriticalSection(&q->lock);
        for (zan_co_node *p = q->head; p; p = p->next) n++;
        LeaveCriticalSection(&q->lock);
    }
    return n;
}

#else
/* ---------------- Non-Windows: single-threaded fallback ---------------- */

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
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    t->next = g_tq;
    t->due_ms = now + (ms > 0 ? ms : 0);
    t->frame = frame;
    t->step = step;
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
            zan_co_timer **bp = &g_tq, **pp = &g_tq;
            for (pp = &(*pp)->next; *pp; pp = &(*pp)->next)
                if ((*pp)->due_ms < (*bp)->due_ms) bp = pp;
            zan_co_timer *best = *bp;
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            long long now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            if (best->due_ms > now) {
                zan_io_pump_timeout(best->due_ms - now);
                continue;
            }
            *bp = best->next;
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
