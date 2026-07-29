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

#if defined(_WIN32)
#include <winsock2.h>   /* must precede <windows.h> (pulled in by rt_crash.h) */
#endif
#include "rt_io.h"
#include "rt_co.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "rt_crash.h"

static int zan_io_trace(void){static int t=-1;if(t<0){const char*e=getenv("ZAN_IO_TRACE");t=(e&&*e&&*e!='0')?1:0;}return t;}
#define IOTRACE(...) do{if(zan_io_trace()){fprintf(stderr,"[iot] " __VA_ARGS__);fprintf(stderr,"\n");fflush(stderr);}}while(0)


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
#include <limits.h>
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
    intptr_t *out_accept;   /* accepted fd sink (NULL => not an accept op) */
    struct zan_io_entry *next;
} zan_io_entry_t;

static zan_io_entry_t *g_io_entries;    /* linked list of active watchers */

/* Pending recv-op parameters, consumed by the next io_register. The POSIX
 * backends run only under the single-thread inline driver, so plain statics are
 * safe (the multi-worker driver is Windows/IOCP-only). */
static void   *g_pending_rbuf;
static int32_t  g_pending_rlen;
static int64_t *g_pending_out_n;
static intptr_t *g_pending_accept_out;

/* ---- watchers stranded on a dead fd ----
 * A watcher whose fd is already closed (or was never valid: -1 out of a failed
 * accept, a listener taken down by Socket.Stop while a coroutine is parked in
 * accept) can never be reported ready: the kernel refuses to register it, and a
 * closed fd silently leaves the epoll set. Parking such a coroutine forever
 * hangs the whole program, so they are queued here and woken by the next poll
 * with their operation failed -- accept/recv then return an error the Zan side
 * already handles. Never FD_SET such an fd either: an out-of-range fd is
 * undefined behaviour and a negative one clobbers the fd_set's own storage. */
typedef struct zan_io_dead {
    void *co;
    zan_co_step_t step;
    int64_t *out_n;
    intptr_t *out_accept;
    struct zan_io_dead *next;
} zan_io_dead_t;

static zan_io_dead_t *g_io_dead;
static int g_io_dead_count;

/* An fd the kernel still knows (zan_io_socket_alive: a single fcntl here). */
static int io_fd_usable(intptr_t fd) {
    return zan_io_socket_alive(fd) != 0;
}

static void io_mark_dead(void *co, zan_co_step_t step, int64_t *out_n,
                         intptr_t *out_accept) {
    zan_io_dead_t *d = (zan_io_dead_t *)calloc(1, sizeof(*d));
    if (!d) return;
    d->co = co;
    d->step = step;
    d->out_n = out_n;
    d->out_accept = out_accept;
    d->next = g_io_dead;
    g_io_dead = d;
    g_io_dead_count++;
}

/* Fail and wake every stranded watcher. recv reports 0 bytes (the end-of-stream
 * shape its callers already expect) and accept reports -1. */
static int io_flush_dead(void) {
    int woke = 0;
    while (g_io_dead) {
        zan_io_dead_t *d = g_io_dead;
        g_io_dead = d->next;
        g_io_dead_count--;
        if (d->out_accept) *d->out_accept = -1;
        else if (d->out_n)  *d->out_n = 0;
        io_wake(d->co, d->step);
        free(d);
        woke++;
    }
    return woke;
}

/* Drop the queue without waking anyone (shutdown: the coroutines are gone). */
static void io_dead_clear(void) {
    while (g_io_dead) {
        zan_io_dead_t *d = g_io_dead;
        g_io_dead = d->next;
        free(d);
    }
    g_io_dead_count = 0;
}

/* Registration-time guard shared by the POSIX backends: returns 1 when the
 * watcher was stranded (and thus must not be registered). */
static int io_reject_dead_fd(intptr_t fd, void *co, zan_co_step_t step) {
    if (io_fd_usable(fd)) return 0;
    io_mark_dead(co, step, g_pending_out_n, g_pending_accept_out);
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
    g_pending_accept_out = NULL;
    return 1;
}

/* On POSIX there is no overlapped recv, so zan_io_recv_co registers a readiness
 * watcher and performs the recv here once the fd is readable -- same net effect
 * (one recv per await, byte count delivered via out_n). No-op for plain probes.
 * (The epoll backend has its own slot-based io_deliver_waiter.) */
#if !defined(__linux__)
static void io_deliver_recv(zan_io_entry_t *e) {
    if (e->out_accept) {
        *e->out_accept = (intptr_t)accept(e->fd, NULL, NULL);
        return;
    }
    if (!e->out_n) return;
    ssize_t rn = recv(e->fd, e->rbuf, (size_t)e->rlen, 0);
    *e->out_n = (rn < 0) ? 0 : (int64_t)rn;
}

/* Detach every watcher whose fd died under it (or is unusable with this
 * backend: select cannot represent fd >= FD_SETSIZE) and fail them.
 * `fd_limit` is the first fd number the backend cannot watch. */
static int io_sweep_entries(int fd_limit) {
    int found = 0;
    zan_io_entry_t **pp = &g_io_entries;
    while (*pp) {
        zan_io_entry_t *cur = *pp;
        if (cur->fd < fd_limit && io_fd_usable(cur->fd)) {
            pp = &cur->next;
            continue;
        }
        *pp = cur->next;
        io_mark_dead(cur->co, cur->step, cur->out_n, cur->out_accept);
        free(cur);
        g_io_count--;
        found = 1;
    }
    return found ? io_flush_dead() : 0;
}
#endif
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

/* Non-blocking send with a classified result so callers can tell a transient
 * "try again once writable" from a dead connection:
 *   >= 0  bytes accepted this call
 *   -1    would block (EWOULDBLOCK/EAGAIN) -- retry after the socket is writable
 *   -2    fatal (connection reset/aborted, broken pipe, not connected, ...)
 * Returning a single -1 for every error made the async SendAsync loop wait
 * forever for a writability that never comes when the peer had reset the
 * connection mid-send (e.g. a server that answers 413 and closes). */
int64_t zan_io_socket_send(intptr_t fd, const void *buf, int64_t len,
                           int32_t flags) {
#if defined(_WIN32)
    int _sr = send((SOCKET)fd, (const char *)buf, (int)len, (int)flags);
    if (_sr == SOCKET_ERROR) {
        int e = WSAGetLastError();
        IOTRACE("send fd=%lld len=%lld -> ERR err=%d", (long long)fd, (long long)len, e);
        if (e == WSAEWOULDBLOCK) return -1;
        return -2;
    }
    IOTRACE("send fd=%lld len=%lld -> %d", (long long)fd, (long long)len, _sr);
    return (int64_t)_sr;
#else
    ssize_t r = send((int)fd, buf, (size_t)len, (int)flags);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        return -2;
    }
    return (int64_t)r;
#endif
}

int64_t zan_io_socket_recv(intptr_t fd, void *buf, int64_t len,
                           int32_t flags) {
#if defined(_WIN32)
    return (int64_t)recv((SOCKET)fd, (char *)buf, (int)len, (int)flags);
#else
    return (int64_t)recv((int)fd, buf, (size_t)len, (int)flags);
#endif
}

int32_t zan_io_socket_ready(intptr_t fd, int32_t write_ready) {
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

/* See rt_io.h: whether `fd` is still an open socket. */
int32_t zan_io_socket_alive(intptr_t fd) {
#if defined(_WIN32)
    if ((SOCKET)fd == INVALID_SOCKET) return 0;
    int type = 0;
    int tlen = (int)sizeof(type);
    /* SO_TYPE on a closed handle fails with WSAENOTSOCK. */
    return getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE, (char *)&type, &tlen) == 0;
#else
    if (fd < 0) return 0;
    return fcntl((int)fd, F_GETFD) != -1;
#endif
}

/* Non-blocking connect status probe. Returns 0 once the connection is
 * established, a positive SO_ERROR code (or -1 when no code is available)
 * once it has failed, and -2 while the connect is still in progress.
 *
 * The probe selects on both the write and exception sets: POSIX reports a
 * failed connect as writable with SO_ERROR set, while Windows reports it
 * only through the exception set -- and on some stacks SO_ERROR is not
 * populated until the exception set has actually been polled, so a bare
 * getsockopt(SO_ERROR) after the reactor wake reads 0 for a refused
 * connection. */
int32_t zan_io_connect_status(intptr_t fd) {
    fd_set wfds, efds;
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    struct timeval timeout = {0, 0};
    int err = 0;
#if defined(_WIN32)
    int elen = (int)sizeof(err);
    SOCKET sock = (SOCKET)fd;
    if (sock == INVALID_SOCKET) return -1;
    FD_SET(sock, &wfds);
    FD_SET(sock, &efds);
    if (select(0, NULL, &wfds, &efds, &timeout) < 0) return -1;
#else
    socklen_t elen = (socklen_t)sizeof(err);
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    int sock = (int)fd;
    FD_SET(sock, &wfds);
    FD_SET(sock, &efds);
    if (select(sock + 1, NULL, &wfds, &efds, &timeout) < 0) return -1;
#endif
    if (FD_ISSET(sock, &efds)) {
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0)
            return -1;
        return err != 0 ? (int64_t)err : -1;
    }
    if (FD_ISSET(sock, &wfds)) {
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0)
            return -1;
        return (int64_t)err;
    }
    return -2;
}

/* ---- platform backend ---- */

#if defined(__linux__)
/* ==================== EPOLL ====================
 *
 * Watchers live in an fd-indexed slot table, not a linked list: registering
 * and waking are O(1) with no per-await malloc, and a fd stays a member of
 * the epoll set for its whole life. Readiness is armed with EPOLLONESHOT and
 * re-armed with EPOLL_CTL_MOD, so an await costs one epoll_ctl instead of the
 * ADD + DEL pair (and none of the list walking) the previous version needed.
 */

static int g_epoll_fd = -1;

/* One waiter per direction is stored inline in the slot (the common case:
 * one coroutine reading and/or writing a socket). The rare extra waiter on
 * the same fd+direction chains through `next`. */
typedef struct zan_io_waiter {
    void *co;
    zan_co_step_t step;
    void *rbuf;
    int   rlen;
    int64_t *out_n;
    intptr_t *out_accept;
    struct zan_io_waiter *next;   /* overflow chain (usually NULL) */
} zan_io_waiter_t;

typedef struct {
    zan_io_waiter_t r;
    zan_io_waiter_t w;
    unsigned char has_r;
    unsigned char has_w;
    unsigned char in_epoll;   /* fd is a member of the epoll set */
} zan_io_slot_t;

static zan_io_slot_t *g_slots;
static int g_slots_cap;

void zan_io_init(void) {
    if (g_io_started) return;
    g_io_entries = NULL;
    g_io_count = 0;
    g_epoll_fd = epoll_create1(0);
    g_io_started = 1;
}

void zan_io_shutdown(void) {
    if (g_slots) {
        for (int fd = 0; fd < g_slots_cap; fd++) {
            zan_io_slot_t *s = &g_slots[fd];
            if (s->in_epoll) epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            zan_io_waiter_t *x = s->r.next;
            while (x) { zan_io_waiter_t *n = x->next; free(x); x = n; }
            x = s->w.next;
            while (x) { zan_io_waiter_t *n = x->next; free(x); x = n; }
        }
        free(g_slots);
        g_slots = NULL;
        g_slots_cap = 0;
    }
    g_io_entries = NULL;
    g_io_count = 0;
    io_dead_clear();
    if (g_epoll_fd >= 0) { close(g_epoll_fd); g_epoll_fd = -1; }
    g_io_started = 0;
}

int32_t zan_io_set_nonblocking(intptr_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static zan_io_slot_t *io_slot(int fd) {
    if (fd < 0) return NULL;
    if (fd >= g_slots_cap) {
        int cap = g_slots_cap ? g_slots_cap * 2 : 256;
        while (cap <= fd) cap *= 2;
        zan_io_slot_t *ns = (zan_io_slot_t *)realloc(g_slots, (size_t)cap * sizeof(*ns));
        if (!ns) return NULL;
        memset(ns + g_slots_cap, 0, (size_t)(cap - g_slots_cap) * sizeof(*ns));
        g_slots = ns;
        g_slots_cap = cap;
    }
    return &g_slots[fd];
}

/* (Re-)arm `fd` for the directions that still have waiters. EPOLLONESHOT keeps
 * the fd registered but disabled after each wake, so re-arming is a MOD; the
 * ADD/ENOENT fallbacks cover a first registration and a recycled fd number. */
static void io_arm(int fd, zan_io_slot_t *s) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd;
    ev.events = EPOLLONESHOT;
    if (s->has_r) ev.events |= EPOLLIN;
    if (s->has_w) ev.events |= EPOLLOUT;
    if (!s->in_epoll) {
        if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
            s->in_epoll = 1;
        } else if (errno == EEXIST) {
            epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
            s->in_epoll = 1;
        }
        return;
    }
    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0 && errno == ENOENT) {
        /* fd was closed and its number reused: re-add it. */
        if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0) s->in_epoll = 0;
    }
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    if (io_reject_dead_fd(fd, co, step)) return;
    zan_io_slot_t *s = io_slot((int)fd);
    if (!s) {   /* slot table could not grow: fail rather than drop the waiter */
        io_mark_dead(co, step, g_pending_out_n, g_pending_accept_out);
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
        /* second waiter on the same fd+direction: chain it (rare) */
        w = (zan_io_waiter_t *)calloc(1, sizeof(*w));
        if (!w) return;
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
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
    g_pending_accept_out = NULL;
    g_io_count++;

    io_arm(fd, s);
}

/* Perform the pending recv/accept for a woken waiter (see io_deliver_recv). */
static void io_deliver_waiter(int fd, zan_io_waiter_t *w) {
    if (w->out_accept) {
        *w->out_accept = (intptr_t)accept(fd, NULL, NULL);
        return;
    }
    if (!w->out_n) return;
    ssize_t rn = recv(fd, w->rbuf, (size_t)w->rlen, 0);
    *w->out_n = (rn < 0) ? 0 : (int64_t)rn;
}

/* Detach every waiter of one direction into `out` (an array of at most
 * `max` entries), returning how many were taken. */
static int io_take(zan_io_slot_t *s, int fd, int read_dir,
                   zan_io_waiter_t *out, int max) {
    unsigned char *has = read_dir ? &s->has_r : &s->has_w;
    if (!*has) return 0;
    zan_io_waiter_t *inl = read_dir ? &s->r : &s->w;
    int n = 0;
    if (n < max) out[n++] = *inl;
    zan_io_waiter_t *x = inl->next;
    while (x) {
        zan_io_waiter_t *nx = x->next;
        if (n < max) out[n++] = *x;
        free(x);
        x = nx;
    }
    inl->next = NULL;
    *has = 0;
    (void)fd;
    return n;
}

/* Fail every waiter parked on an fd that has since been closed. Closing an fd
 * removes it from the epoll set without any event, so such waiters are
 * invisible to epoll_wait and would otherwise never be resumed. */
static int io_sweep_slots(void) {
    int woke = 0;
    for (int fd = 0; fd < g_slots_cap; fd++) {
        zan_io_slot_t *s = &g_slots[fd];
        if (!s->has_r && !s->has_w) continue;
        if (io_fd_usable(fd)) continue;
        zan_io_waiter_t ready[8];
        int nr = io_take(s, fd, 1, ready, 8);
        nr += io_take(s, fd, 0, ready + nr, 8 - nr);
        s->in_epoll = 0;
        for (int k = 0; k < nr; k++) {
            if (ready[k].out_accept)   *ready[k].out_accept = -1;
            else if (ready[k].out_n)   *ready[k].out_n = 0;
            g_io_count--;
            io_wake(ready[k].co, ready[k].step);
            woke++;
        }
    }
    return woke;
}

int32_t zan_io_poll(int64_t timeout_ms) {
    if (g_io_dead) return io_flush_dead();
    if (g_io_count == 0) return 0;
    /* An unbounded wait is the one that turns a stranded waiter into a hang,
     * so check for dead fds before committing to it. */
    if (timeout_ms < 0) {
        int woke = io_sweep_slots();
        if (woke) return woke;
        if (g_io_count == 0) return 0;
    }
    struct epoll_event events[256];
    int n = epoll_wait(g_epoll_fd, events, 256,
                       timeout_ms < 0 ? -1 : (int)timeout_ms);
    if (n == 0) return io_sweep_slots();
    int woke = 0;
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        if (fd < 0 || fd >= g_slots_cap) continue;
        zan_io_slot_t *s = &g_slots[fd];
        uint32_t ev = events[i].events;
        int err = (ev & (EPOLLERR | EPOLLHUP)) != 0;

        zan_io_waiter_t ready[8];
        int nr = 0;
        if ((ev & EPOLLIN) || err)  nr += io_take(s, fd, 1, ready + nr, 8 - nr);
        if ((ev & EPOLLOUT) || err) nr += io_take(s, fd, 0, ready + nr, 8 - nr);

        /* Re-arm before waking: a woken coroutine may register again and the
         * EPOLLONESHOT arm state must already reflect the remaining waiters. */
        if (s->has_r || s->has_w) io_arm(fd, s);

        for (int k = 0; k < nr; k++) {
            io_deliver_waiter(fd, &ready[k]);
            g_io_count--;
            io_wake(ready[k].co, ready[k].step);
            woke++;
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
    io_dead_clear();
    if (g_kq_fd >= 0) { close(g_kq_fd); g_kq_fd = -1; }
    g_io_started = 0;
}

int32_t zan_io_set_nonblocking(intptr_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    if (io_reject_dead_fd(fd, co, step)) return;
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
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
    g_io_count++;

    struct kevent kev;
    short filter = (interest == ZAN_IO_READ) ? EVFILT_READ : EVFILT_WRITE;
    EV_SET(&kev, fd, filter, EV_ADD | EV_ONESHOT, 0, 0, NULL);
    kevent(g_kq_fd, &kev, 1, NULL, 0, NULL);
}

int32_t zan_io_poll(int64_t timeout_ms) {
    if (g_io_dead) return io_flush_dead();
    if (g_io_count == 0) return 0;
    if (timeout_ms < 0) {
        int woke = io_sweep_entries(INT_MAX);
        if (woke) return woke;
        if (g_io_count == 0) return 0;
    }
    struct kevent events[64];
    struct timespec ts = { timeout_ms / 1000, (timeout_ms % 1000) * 1000000L };
    int n = kevent(g_kq_fd, NULL, 0, events, 64,
                   timeout_ms < 0 ? NULL : &ts);
    if (n == 0) return io_sweep_entries(INT_MAX);
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

/* ---- completion-port association ----
 * Every socket must be associated with the IOCP once before its first
 * overlapped op. A previous version cached associated handle *numbers* to skip
 * the CreateIoCompletionPort syscall on the hot recv path, but that was unsafe:
 * Windows recycles the handle number of a closed socket and the runtime has no
 * hook on socket close, so a fresh un-associated socket could reuse a cached
 * number and wrongly skip association -- dropping it off the port so its
 * completions never arrived (a low-activity lost-wakeup hang). Association is
 * now unconditional and idempotent (re-binding an already-bound handle is a
 * cheap ERROR_INVALID_PARAMETER no-op), which is correct and race-free. */

/* Associate a brand-new kernel socket with the port. */
static void mark_assoc(SOCKET s) {
    if (CreateIoCompletionPort((HANDLE)s, g_iocp, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;
    }
}

/* Associate a socket with the completion port before its first overlapped op.
 *
 * This ALWAYS issues CreateIoCompletionPort and treats "already bound"
 * (ERROR_INVALID_PARAMETER) as success -- it never skips based on a
 * remembered handle. An earlier version cached associated handle *numbers*
 * and skipped the syscall on a cache hit, but Windows recycles the handle
 * number of a closed socket, and the runtime has no hook on socket close
 * (stdlib calls closesocket directly). So a freshly created, still
 * UN-associated client socket could reuse the number of a since-closed
 * server socket left in the cache; the skip then left the new socket off the
 * IOCP entirely and its completion (connect/recv) was never delivered -- a
 * lost-wakeup hang that surfaced at low activity, where handle reuse is
 * common. Re-associating an already-bound socket is a cheap, harmless no-op,
 * so always doing it is the correct and race-free choice. */
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
    if (CreateIoCompletionPort((HANDLE)s, g_iocp, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;   /* ERROR_INVALID_PARAMETER == already bound to this port */
    }
}

void zan_io_init(void) {
    if (g_io_started) return;
    zan__crash_install();
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
    /* Drop pooled ops: the completion port is gone, so recycled op structs are
     * all stale for any future zan_io_init(). */
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
static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
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
    IOTRACE("io_register(READY) fd=%lld interest=%d cnt=%d", (long long)s, interest, g_io_count);

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
        /* On a datagram socket a zero-byte WSARecv would dequeue and truncate
         * the pending datagram (payload silently lost). MSG_PEEK keeps the
         * datagram queued: the op completes (typically WSAEMSGSIZE) when data
         * is ready and the waiter's real recvfrom then reads it intact. */
        DWORD flags = 0;
        int sotype = 0, solen = (int)sizeof(sotype);
        if (getsockopt(s, SOL_SOCKET, SO_TYPE, (char *)&sotype, &solen) == 0 &&
            sotype == SOCK_DGRAM)
            flags = MSG_PEEK;
        r = WSARecv(s, &b, 1, NULL, &flags, &op->ov, NULL);
    } else {
        r = WSASend(s, &b, 1, NULL, 0, &op->ov, NULL);
    }
    IOTRACE("ready_co fd=%lld interest=%d op=%p -> r=%d err=%d cnt=%d",
            (long long)s, interest, (void*)op, r, r?WSAGetLastError():0, g_io_count);
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
void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
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
    IOTRACE("recv_co fd=%lld len=%d op=%p -> r=%d got=%lu err=%d cnt=%d", (long long)fd, len, (void*)op, r, (unsigned long)got, r?WSAGetLastError():0, g_io_count);
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

void zan_io_accept_co(intptr_t fd, void *frame, zan_co_step_t step,
                      intptr_t *out_fd) {
    zan_io_init();
    SOCKET listener = (SOCKET)fd;
    IOTRACE("accept_co ENTER listener=%lld", (long long)fd);
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
    op->out_n = (int64_t *)out_fd;
    op->accepted = accepted;
    op->accept_buf = calloc(1, addr_len * 2);
    IO_CNT_INC();

    got = 0;
    BOOL ok = accept_ex(listener, accepted, op->accept_buf, 0,
                        addr_len, addr_len, &got, &op->ov);
    IOTRACE("accept_co listener=%lld op=%p ok=%d err=%d cnt=%d", (long long)listener, (void*)op, (int)ok, WSAGetLastError(), g_io_count);
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
            /* Do NOT associate the accepted socket with this process's IOCP
             * here. Association binds the underlying file object to one port
             * for its whole lifetime, and an accepted socket may be handed to
             * another process (Worker multi-process handoff via
             * WSADuplicateSocket) whose own IOCP must receive its completions.
             * The first overlapped op on the socket associates it via
             * ensure_assoc in whichever process actually performs IO. */
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

int32_t zan_io_poll(int64_t timeout_ms) {
    if (g_io_count == 0) return 0;
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    DWORD to = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    if (!GetQueuedCompletionStatusEx(g_iocp, entries, 64, &removed, to, FALSE)) {
        IOTRACE("poll GQCS=0 err=%lu to=%lu cnt=%d", (unsigned long)GetLastError(), (unsigned long)to, g_io_count);
        return 0;
    }
    IOTRACE("poll removed=%lu cnt=%d", (unsigned long)removed, g_io_count);
    int woke = 0;
    for (ULONG i = 0; i < removed; i++) {
        zan_io_op_t *op = CONTAINING_RECORD(entries[i].lpOverlapped,
                                            zan_io_op_t, ov);
        void *co = op->co;
        zan_co_step_t step = op->step;
        IOTRACE("poll_op op=%p kind=%d bytes=%lu status=%llu", (void*)op, op->kind, (unsigned long)entries[i].dwNumberOfBytesTransferred, (unsigned long long)entries[i].Internal);
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
    io_dead_clear();
    g_io_started = 0;
}

int32_t zan_io_set_nonblocking(intptr_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    /* select can only represent fd numbers below FD_SETSIZE; fail anything it
     * (or the kernel) cannot watch instead of parking the coroutine. */
    if (fd >= FD_SETSIZE) { io_reject_dead_fd(-1, co, step); return; }
    if (io_reject_dead_fd(fd, co, step)) return;
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
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
    g_io_count++;
}

int32_t zan_io_poll(int64_t timeout_ms) {
    if (g_io_dead) return io_flush_dead();
    if (g_io_count == 0) return 0;

    /* Every fd goes into an fd_set below, so a watcher whose socket has been
     * closed under it must leave the list first: FD_SET of a negative or
     * out-of-range fd is undefined behaviour (with -1 it writes outside the
     * fd_set, and select then waits on whatever bit that landed on -- fd 0 in
     * practice, which never becomes ready). */
    {
        int woke = io_sweep_entries(FD_SETSIZE);
        if (woke) return woke;
        if (g_io_count == 0) return 0;
    }

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

void zan_io_wait_co(intptr_t fd, int32_t interest, void *frame, zan_co_step_t step) {
    zan_io_init();      /* lazy: generated programs never call zan_io_init */
    io_register(fd, interest, frame, step);
}

#if !defined(_WIN32)
/* POSIX overlapped-recv shim: register a read-readiness watcher carrying the
 * recv sink, so the poll loop performs the recv and delivers the byte count
 * (see io_deliver_recv). Windows has a true overlapped implementation above. */
void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
                    zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
    g_pending_rbuf = buf;
    g_pending_rlen = len;
    g_pending_out_n = out_n;
    io_register(fd, ZAN_IO_READ, frame, step);
}

void zan_io_accept_co(intptr_t fd, void *frame, zan_co_step_t step,
                      intptr_t *out_fd) {
    zan_io_init();
    g_pending_accept_out = out_fd;
    io_register(fd, ZAN_IO_READ, frame, step);
}
#endif

int32_t zan_io_pump_timeout(int64_t timeout_ms) {
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

int32_t zan_io_pump(void) {
    return zan_io_pump_timeout(-1);
}

int32_t zan_io_has_pending(void) {
#if !defined(_WIN32)
    if (g_io_dead_count > 0) return 1;   /* stranded watchers still owe a wake */
#endif
    return g_io_count > 0;
}

#ifndef ZAN_IO_STACKLESS_ONLY
/* ---- stackful (rt_sched fiber) ABI: excluded from the reactor object
 * shipped with zanc so it needs no rt_sched. ---- */

int64_t zan_io_wait_readable(intptr_t fd) {
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register(fd, ZAN_IO_READ, co, NULL);
    zan_io_suspend_current();
    return 0;
}

int64_t zan_io_wait_writable(intptr_t fd) {
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register(fd, ZAN_IO_WRITE, co, NULL);
    zan_io_suspend_current();
    return 0;
}

int64_t zan_io_wait_readable_timeout(intptr_t fd, int64_t timeout_ms) {
    /* For simplicity, delegate to wait_readable; timeout is handled
     * by the scheduler's timer integration.  A proper implementation
     * would register both an IO watcher and a timer, and cancel whichever
     * fires second. */
    (void)timeout_ms;
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register(fd, ZAN_IO_READ, co, NULL);
    zan_io_suspend_current();
    return 1;
}

/* ================= async connect ================= */

#if defined(_WIN32)
/* Async connect via ConnectEx; suspends until the connection completes.
 * Returns 0 on success, -1 on error. */
int64_t zan_io_connect(intptr_t fd, const char *ip, int32_t port) {
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
int64_t zan_io_connect(intptr_t fd, const char *ip, int32_t port) {
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
    /* Open-addressed set of frames currently queued, so the duplicate check
     * in zan_co_ready is O(1) instead of walking the whole list (which made
     * enqueue O(n^2) under load). Tombstones mark removed slots. */
    void            **qset;
    size_t            qset_cap;   /* power of two */
    size_t            qset_len;   /* live entries */
    size_t            qset_tombs;
    zan_co_node      *free_nodes; /* recycled list nodes */
} zan_co_worker_queue;

#define ZAN_QSET_TOMB ((void *)(uintptr_t)1)

static size_t qset_hash(void *f) {
    return (size_t)((((uintptr_t)f) >> 4) * (uintptr_t)2654435761u);
}

static void qset_rehash(zan_co_worker_queue *q, size_t ncap) {
    void **ns = (void **)calloc(ncap, sizeof(void *));
    if (!ns) abort();
    for (size_t i = 0; i < q->qset_cap; i++) {
        void *f = q->qset[i];
        if (f && f != ZAN_QSET_TOMB) {
            size_t j = qset_hash(f) & (ncap - 1);
            while (ns[j]) j = (j + 1) & (ncap - 1);
            ns[j] = f;
        }
    }
    free(q->qset);
    q->qset = ns;
    q->qset_cap = ncap;
    q->qset_tombs = 0;
}

/* Insert frame; returns 0 when it was already present. Caller holds q->lock. */
static int qset_add(zan_co_worker_queue *q, void *f) {
    if (q->qset_cap == 0) qset_rehash(q, 64);
    else if ((q->qset_len + q->qset_tombs + 1) * 4 >= q->qset_cap * 3)
        qset_rehash(q, q->qset_len * 4 >= q->qset_cap ? q->qset_cap * 2
                                                      : q->qset_cap);
    size_t mask = q->qset_cap - 1;
    size_t j = qset_hash(f) & mask;
    size_t tomb = (size_t)-1;
    while (q->qset[j]) {
        if (q->qset[j] == f) return 0;
        if (q->qset[j] == ZAN_QSET_TOMB && tomb == (size_t)-1) tomb = j;
        j = (j + 1) & mask;
    }
    if (tomb != (size_t)-1) { j = tomb; q->qset_tombs--; }
    q->qset[j] = f;
    q->qset_len++;
    return 1;
}

static void qset_remove(zan_co_worker_queue *q, void *f) {
    if (q->qset_cap == 0) return;
    size_t mask = q->qset_cap - 1;
    size_t j = qset_hash(f) & mask;
    while (q->qset[j]) {
        if (q->qset[j] == f) {
            q->qset[j] = ZAN_QSET_TOMB;
            q->qset_len--;
            q->qset_tombs++;
            return;
        }
        j = (j + 1) & mask;
    }
}

/* Append a ready frame if it is not already queued. Caller holds q->lock. */
static int co_enqueue_locked(zan_co_worker_queue *q, void *frame,
                             zan_co_step_t step) {
    if (!qset_add(q, frame)) return 0;
    zan_co_node *n = q->free_nodes;
    if (n) q->free_nodes = n->next;
    else {
        n = (zan_co_node *)malloc(sizeof(*n));
        if (!n) abort();
    }
    n->next = NULL; n->frame = frame; n->step = step;
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    return 1;
}

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
        g_co_queues[i].qset_len = 0;
        g_co_queues[i].qset_tombs = 0;
        if (g_co_queues[i].qset)
            memset(g_co_queues[i].qset, 0,
                   g_co_queues[i].qset_cap * sizeof(void *));
    }
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step) return;
    zan_co_worker_queue *q = &g_co_queues[co_queue_index(frame)];
    EnterCriticalSection(&q->lock);
    int added = co_enqueue_locked(q, frame, step);
    LeaveCriticalSection(&q->lock);
    if (!added) return;
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
            qset_remove(q, n->frame);
            n->next = q->free_nodes;
            q->free_nodes = n;
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
            zan_co_worker_queue *q = &g_co_queues[co_queue_index(t->frame)];
            EnterCriticalSection(&q->lock);
            co_enqueue_locked(q, t->frame, t->step);
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
        IOTRACE("poll_op op=%p kind=%d bytes=%lu status=%llu", (void*)op, op->kind, (unsigned long)entries[i].dwNumberOfBytesTransferred, (unsigned long long)entries[i].Internal);
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


/* ======================================================================
 * Coroutine gate -- event-driven, counting wait primitive.
 *
 * Purpose: replace the `await Task.Delay(1)` spin-poll used by connection
 * pools (MySqlPool / SqlitePool / RedisPool) and reconnect loops with a
 * genuine suspend/wake handshake. A coroutine that cannot make progress
 * parks on a gate (no CPU, no timer); whoever makes progress possible
 * calls signal, which re-readies exactly one parked coroutine through the
 * same ready queue the rest of the async machinery uses.
 *
 * Semantics (counting / condition-variable style, no lost wakeup):
 *   - park: if a surplus signal is banked, consume it and re-ready the
 *     caller immediately; otherwise enqueue the caller as a FIFO waiter.
 *   - signal: wake the oldest waiter, or bank a surplus signal when none
 *     is parked. Surplus lets signal-before-park work, so callers that use
 *     a recheck loop (while (!avail) await gate.Wait()) never deadlock.
 *
 * Compiled unconditionally (outside ZAN_CO_DRIVER) so it lives in both
 * zanrt_io.o (single-thread inline driver) and zanrt_io_mt.o (multi-worker
 * driver). zan_co_ready is resolved from whichever driver is linked. Under
 * the multi-worker driver several OS threads may park/signal the same gate
 * concurrently, so the waiter list is guarded by a lock there; the
 * single-thread cooperative driver has no concurrency and needs none.
 * ====================================================================== */
#if defined(ZAN_CO_DRIVER) && !defined(_WIN32)
#include <pthread.h>
#endif
typedef struct zan_gate_waiter {
    struct zan_gate_waiter *next;
    void                   *frame;
    zan_co_step_t           step;
} zan_gate_waiter;

typedef struct zan_gate {
    zan_gate_waiter *head;
    zan_gate_waiter *tail;
    long long        surplus;   /* signals banked while no waiter parked */
#if defined(ZAN_CO_DRIVER) && defined(_WIN32)
    CRITICAL_SECTION lock;
#elif defined(ZAN_CO_DRIVER)
    pthread_mutex_t  lock;
#endif
} zan_gate;

#if defined(ZAN_CO_DRIVER) && defined(_WIN32)
static void zan_gate_lock(zan_gate *g)   { EnterCriticalSection(&g->lock); }
static void zan_gate_unlock(zan_gate *g) { LeaveCriticalSection(&g->lock); }
static void zan_gate_lock_init(zan_gate *g) { InitializeCriticalSection(&g->lock); }
static void zan_gate_lock_free(zan_gate *g) { DeleteCriticalSection(&g->lock); }
#elif defined(ZAN_CO_DRIVER)
static void zan_gate_lock(zan_gate *g)   { pthread_mutex_lock(&g->lock); }
static void zan_gate_unlock(zan_gate *g) { pthread_mutex_unlock(&g->lock); }
static void zan_gate_lock_init(zan_gate *g) { pthread_mutex_init(&g->lock, NULL); }
static void zan_gate_lock_free(zan_gate *g) { pthread_mutex_destroy(&g->lock); }
#else
static void zan_gate_lock(zan_gate *g)   { (void)g; }
static void zan_gate_unlock(zan_gate *g) { (void)g; }
static void zan_gate_lock_init(zan_gate *g) { (void)g; }
static void zan_gate_lock_free(zan_gate *g) { (void)g; }
#endif

/* Allocate a gate; returns an opaque handle (its address as an integer). */
long long zan_gate_new(void) {
    zan_gate *g = (zan_gate *)calloc(1, sizeof(*g));
    if (!g) return 0;
    zan_gate_lock_init(g);
    return (long long)(intptr_t)g;
}

/* Suspend the calling coroutine on the gate. The compiler emits this after
 * saving the frame's live slots and setting its resume state, then returns
 * void from the step; the frame is re-entered via `step(frame)` once a
 * signal is delivered (or immediately if one was already banked). */
void zan_gate_park(long long handle, void *frame, zan_co_step_t step) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) { if (step) zan_co_ready(frame, step); return; }
    if (!step) return;
    zan_gate_lock(g);
    if (g->surplus > 0) {
        g->surplus--;
        zan_gate_unlock(g);
        zan_co_ready(frame, step);   /* a signal was already available */
        return;
    }
    zan_gate_waiter *w = (zan_gate_waiter *)malloc(sizeof(*w));
    if (!w) { zan_gate_unlock(g); zan_co_ready(frame, step); return; }
    w->next = NULL; w->frame = frame; w->step = step;
    if (g->tail) g->tail->next = w; else g->head = w;
    g->tail = w;
    zan_gate_unlock(g);
    /* parked: nothing more to do -- zan_gate_signal will re-ready us. */
}

/* Wake exactly one parked waiter, or bank a surplus signal when none is
 * parked so a subsequent park does not block. */
void zan_gate_signal(long long handle) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_lock(g);
    zan_gate_waiter *w = g->head;
    if (w) {
        g->head = w->next;
        if (!g->head) g->tail = NULL;
        zan_gate_unlock(g);
        zan_co_ready(w->frame, w->step);
        free(w);
        return;
    }
    g->surplus++;
    zan_gate_unlock(g);
}

/* Destroy a gate. Any stragglers are re-readied (so they don't hang) before
 * the storage is released. */
void zan_gate_free(long long handle) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_lock(g);
    zan_gate_waiter *w = g->head;
    g->head = g->tail = NULL;
    zan_gate_unlock(g);
    while (w) {
        zan_gate_waiter *n = w->next;
        zan_co_ready(w->frame, w->step);
        free(w);
        w = n;
    }
    zan_gate_lock_free(g);
    free(g);
}
