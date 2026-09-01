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

/* glibc gates poll()/pollfd behind _DEFAULT_SOURCE; musl (the bundled
 * sysroots) does not, so this only bites native glibc builds (rt_test).
 * Define it before any header so the feature tests see it. */
#if !defined(_WIN32) && defined(__GLIBC__)
#define _DEFAULT_SOURCE
#endif

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601   /* Windows 7+: GetQueuedCompletionStatusEx */
#endif

#if defined(_WIN32)
#include <winsock2.h>   /* must precede <windows.h> (pulled in by rt_crash.h) */
#endif
#include "rt_io.h"
#include "rt_co.h"
#include "rt_timer.h"
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
#include <netdb.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <time.h>

/* A peer that hangs up while the process is writing to it raises SIGPIPE,
 * whose default action is to terminate the process -- so a browser closing a
 * Server-Sent Events stream, or any client walking away mid-response, would
 * kill the whole server without a word. Every send site already classifies
 * EPIPE/ECONNRESET as a dead connection, so the signal carries nothing the
 * caller does not learn from the return value. */
static void zan_io_ignore_sigpipe(void) {
    struct sigaction sa;
    struct sigaction cur;
    /* Only if the program has not chosen its own disposition. */
    if (sigaction(SIGPIPE, NULL, &cur) == 0
        && cur.sa_handler != SIG_DFL) {
        return;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);
}
#endif
#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <poll.h>             /* zan_io_wait_readable_timeout (fiber half) */
#include <fcntl.h>
#include <unistd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#include <poll.h>             /* zan_io_wait_readable_timeout (fiber half) */
#include <fcntl.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>          /* ConnectEx + WSAID_CONNECTEX */
#include <windows.h>
#include <wincrypt.h>
#include <malloc.h>           /* _aligned_malloc / _aligned_free (op pool) */
#else
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
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
/* Set when the backend could not start (epoll_create1/kqueue failed, usually
 * EMFILE): every later registration fails its waiter through the dead queue
 * instead of parking a coroutine that nothing will ever wake -- the old
 * behaviour was every await in the process hanging silently forever. */
static int g_io_broken;

/* ---- async hostname resolution (shared by all backends) ----
 * zan_io_resolve_co runs the lookup on a worker thread so the reactor never
 * blocks on the resolver. The worker parks the result in g_blocking_done and pokes
 * the backend's wake fd (eventfd/pipe on POSIX, a NULL-overlapped IOCP packet
 * on Windows); the next zan_io_poll drains the list, stores each result into
 * its frame's RESULT slot and re-readies the frame. g_blocking_inflight counts
 * jobs whose completion has not yet been delivered, so the scheduler keeps
 * polling while one is outstanding (zan_io_has_pending). A DNS lookup still in
 * flight past ZAN_DNS_TIMEOUT_MS is delivered as a failure by the reactor
 * (dns_timeout_scan); the worker then discards its result. The machinery
 * itself lives in the coroutine-facing ABI section at the bottom; these are
 * the pieces the platform backends must see. */
static int32_t g_blocking_inflight;
static int32_t g_dns_wake_fd = -1;    /* reactor-visible wake fd (POSIX) */
#if !defined(__linux__) && !defined(_WIN32)
static int32_t g_dns_wake_wfd = -1;   /* pipe write end (kqueue/select) */
#endif

static int dns_drain(void);           /* deliver completed lookups (reactor) */
static int64_t dns_wait_ms(int64_t caller_ms);  /* cap a backend wait at the
                                                   earliest lookup deadline */
static int dns_timeout_scan(void);    /* deliver failures for late lookups */
static void dns_wake_read(void);      /* consume the wake fd signal (POSIX) */
static void dns_wake_notify(void);    /* worker -> reactor wake (all backends) */
static void dns_shutdown_cleanup(void); /* free DNS jobs at shutdown */

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

/* Longest an otherwise unbounded reactor wait may sleep. It bounds how long a
 * waiter stranded on a closed fd stays parked before io_sweep_slots finds it,
 * and it is what keeps that sweep off the hot path. */
#define ZAN_IO_SWEEP_MS 20

/* How many reads may be served straight from the socket buffer (zan_io_recv_co's
 * speculative recv) between two reactor turns. The fast path keeps a coroutine
 * out of the epoll round trip, but a coroutine that never parks also never lets
 * the ready queue empty -- and the queue emptying is what gives the reactor its
 * turn. Spending the budget makes the next read park, so connections whose data
 * arrived while the queue was busy wait a bounded number of requests, not a
 * whole burst. */
#define ZAN_IO_FAST_BURST 64

static zan_io_dead_t *g_io_dead;
static int g_io_dead_count;
static int g_io_fast_budget = ZAN_IO_FAST_BURST;

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
    ssize_t rn;
    do {
        rn = recv(e->fd, e->rbuf, (size_t)e->rlen, 0);
    } while (rn < 0 && errno == EINTR);
    /* Retry transient EINTR: readiness was real, and reporting a signal
     * interruption as 0 bytes would fabricate a peer close and tear down a
     * healthy keep-alive connection. */
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
    /* The runtime is the last validation before the kernel: a Zan-side
     * underflow or -1 sentinel used to reach send() as a negative int
     * (Windows truncation) or ~2^64 size_t (POSIX wrap). Callers loop for
     * anything larger than one OS call anyway. */
    if (len <= 0) return len == 0 ? 0 : -2;
    if (len > 0x7FFFFFFF) len = 0x7FFFFFFF;
#if defined(_WIN32)
    for (;;) {
        int _sr = send((SOCKET)fd, (const char *)buf, (int)len, (int)flags);
        if (_sr != SOCKET_ERROR) {
            IOTRACE("send fd=%lld len=%lld -> %d", (long long)fd, (long long)len, _sr);
            return (int64_t)_sr;
        }
        int e = WSAGetLastError();
        IOTRACE("send fd=%lld len=%lld -> ERR err=%d", (long long)fd, (long long)len, e);
        if (e == WSAEWOULDBLOCK) return -1;
        /* EINTR means nothing was transferred -- retrying cannot duplicate
         * bytes, and reporting it as fatal (-2) tore down healthy
         * connections whenever a signal arrived mid-send. */
        if (e == WSAEINTR) continue;
        return -2;
    }
#else
#if defined(MSG_NOSIGNAL)
    flags |= MSG_NOSIGNAL;   /* a dead peer is a return value, not a signal */
#endif
    for (;;) {
        ssize_t r = send((int)fd, buf, (size_t)len, (int)flags);
        if (r >= 0) return (int64_t)r;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        /* Same nothing-transferred guarantee: retry instead of fabricating
         * a fatal error on signal arrival. */
        if (errno == EINTR) continue;
        return -2;
    }
#endif
}

int64_t zan_io_socket_recv(intptr_t fd, void *buf, int64_t len,
                           int32_t flags) {
    /* Same clamp discipline as send: reject negatives, cap at INT_MAX. */
    if (len < 0) return -2;
    if (len > 0x7FFFFFFF) len = 0x7FFFFFFF;
#if defined(_WIN32)
    return (int64_t)recv((SOCKET)fd, (char *)buf, (int)len, (int)flags);
#else
    return (int64_t)recv((int)fd, buf, (size_t)len, (int)flags);
#endif
}

const char *zan_io_socket_peer_ip(intptr_t fd) {
    struct sockaddr_storage peer;
#if defined(_WIN32)
    int length = (int)sizeof(peer);
    if (getpeername((SOCKET)fd, (struct sockaddr *)&peer, &length) != 0) return "";
#else
    socklen_t length = (socklen_t)sizeof(peer);
    if (getpeername((int)fd, (struct sockaddr *)&peer, &length) != 0) return "";
#endif
    return zan_io_sockaddr_ip_str(&peer);
}

/* Format the address part of a sockaddr (IPv4 or IPv6) as text. */
const char *zan_io_sockaddr_ip_str(const void *sa) {
    /* Thread-local, for the same multi-worker reason as peer_ip above. */
    static _Thread_local char address[INET6_ADDRSTRLEN];
    const struct sockaddr *a = (const struct sockaddr *)sa;
    if (a->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)a;
        if (!inet_ntop(AF_INET, &in->sin_addr, address, sizeof(address)))
            return "";
        return address;
    }
    if (a->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)a;
        if (!inet_ntop(AF_INET6, &in6->sin6_addr, address, sizeof(address)))
            return "";
        return address;
    }
    return "";
}

/* ---- copy-out variants of the two IP-text helpers above -------------------
 * The plain const char* returns are ADOPTED by the FFI without a copy
 * (docs/ABI.md), but their backing storage is per-thread shared state: a
 * value stored into a managed string before an await silently changed
 * content after the next call on this worker -- or, under --async-workers,
 * after fiber migration (UDP reply paths read the peer IP post-await). The
 * stdlib wrappers now copy into caller-owned storage through these; they
 * return the text length excluding NUL, or -1 on failure / bad args /
 * too-small buffer. */
static int32_t zan_io_ip_copy_out(const char *text, char *buf, int32_t cap) {
    if (!buf || cap <= 0) return -1;
    size_t n = strlen(text);
    if ((size_t)cap <= n) return -1;
    memcpy(buf, text, n + 1);
    return (int32_t)n;
}

int32_t zan_io_socket_peer_ip_into(intptr_t fd, char *buf, int32_t cap) {
    struct sockaddr_storage peer;
#if defined(_WIN32)
    int length = (int)sizeof(peer);
    if (getpeername((SOCKET)fd, (struct sockaddr *)&peer, &length) != 0)
        return -1;
#else
    socklen_t length = (socklen_t)sizeof(peer);
    if (getpeername((int)fd, (struct sockaddr *)&peer, &length) != 0)
        return -1;
#endif
    return zan_io_ip_copy_out(zan_io_sockaddr_ip_str(&peer), buf, cap);
}

int32_t zan_io_sockaddr_ip_str_into(const void *sa, char *buf, int32_t cap) {
    return zan_io_ip_copy_out(zan_io_sockaddr_ip_str(sa), buf, cap);
}

/* Resolve `name` (hostname or literal IP) to a complete sockaddr. The port is
 * baked into the sockaddr by getaddrinfo. Returns the sockaddr length (16 or
 * 28), or 0 on failure.
 *
 * Family policy: a name containing ':' is IPv6-or-nothing (AF_UNSPEC), so a
 * literal like "::1" resolves to v6; everything else prefers IPv4 -- the
 * default CreateTcp/CreateUdp sockets are v4, and gethostbyname-based
 * resolution always returned v4, so plain hostnames keep their historical
 * behavior. A v6-only hostname (only AAAA records) falls back to AF_UNSPEC
 * below. */
int32_t zan_io_resolve_sa(const char *name, int32_t port, void *buf,
                          int32_t cap) {
    if (!name || !*name) return 0;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = strchr(name, ':') ? AF_UNSPEC : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res) {
        if (hints.ai_family == AF_INET) {
            hints.ai_family = AF_UNSPEC;    /* v6-only hostname */
            if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res)
                return 0;
        } else {
            return 0;
        }
    }
    int len = (res->ai_family == AF_INET6) ? (int)sizeof(struct sockaddr_in6)
             : (res->ai_family == AF_INET) ? (int)sizeof(struct sockaddr_in)
             : 0;
    int32_t r = 0;
    if (len && len <= cap && res->ai_addrlen == (size_t)len) {
        memcpy(buf, res->ai_addr, (size_t)len);
        r = len;
    }
    freeaddrinfo(res);
    return r;
}

/* Resolve a hostname to an IPv4 address in the same byte order as inet_addr.
 * Returns 0 on failure.  Uses getaddrinfo/AF_INET so concurrent lookups are
 * thread-safe (gethostbyname returns a process-wide static buffer). */
int32_t zan_io_resolve_ipv4(const char *hostname) {
    if (!hostname || !*hostname) return 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(hostname, NULL, &hints, &res) != 0 || !res) return 0;
    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    int32_t addr = 0;
    if (res->ai_family == AF_INET && sin->sin_family == AF_INET)
        addr = (int32_t)sin->sin_addr.s_addr;
    freeaddrinfo(res);
    return addr;
}

int32_t zan_io_sockaddr_family(const void *sa, int32_t len) {
    if (!sa || len < 2) return 0;
    const struct sockaddr *a = (const struct sockaddr *)sa;
    if (a->sa_family == AF_INET && len >= (int32_t)sizeof(struct sockaddr_in))
        return AF_INET;
    if (a->sa_family == AF_INET6 && len >= (int32_t)sizeof(struct sockaddr_in6))
        return AF_INET6;
    return 0;
}

/* Classify the binary address that will actually be connected to.  This is
 * deliberately kept beside the resolver so literal and DNS paths share the
 * same CIDR table; text-prefix checks are not sufficient for mapped IPv6 or
 * alternate textual spellings.  Return 1 only for a usable public address,
 * or an explicit loopback exception. */
int32_t zan_io_sockaddr_is_safe(const void *sa, int32_t len,
                               int32_t allow_loopback) {
    int family = zan_io_sockaddr_family(sa, len);
    if (family == AF_INET) {
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)sa;
        uint32_t a = ntohl(v4->sin_addr.s_addr);
        uint32_t first = a >> 24;
        uint32_t second = (a >> 16) & 255u;
        if (first == 127u) return allow_loopback ? 1 : 0;
        if (first == 0u || first == 10u || first >= 224u || first >= 240u)
            return 0;
        if (first == 169u && second == 254u) return 0;
        if (first == 172u && second >= 16u && second <= 31u) return 0;
        if (first == 192u && second == 168u) return 0;
        if (first == 192u && second == 0u) return 0;
        uint32_t third = (a >> 8) & 255u;
        if (first == 192u && second == 2u) return 0;
        if (first == 192u && second == 88u && third == 99u) return 0;
        if (first == 198u && second >= 18u && second <= 19u) return 0;
        if (first == 198u && second == 51u && third == 100u) return 0;
        if (first == 203u && second == 0u && third == 113u) return 0;
        if (first == 100u && second >= 64u && second <= 127u) return 0;
        if (a == 0xffffffffu) return 0;
        return 1;
    }
    if (family == AF_INET6) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)sa;
        const unsigned char *b = (const unsigned char *)&v6->sin6_addr;
        int all_zero = 1;
        for (int i = 0; i < 16; i++) if (b[i] != 0) all_zero = 0;
        if (all_zero) return 0;
        int loop = 1;
        for (int i = 0; i < 15; i++) if (b[i] != 0) loop = 0;
        if (loop && b[15] == 1) return allow_loopback ? 1 : 0;
        if ((b[0] & 0xfeu) == 0xfcu) return 0;       /* ULA fc00::/7 */
        if ((b[0] & 0xfeu) == 0xfeu && (b[1] & 0xc0u) == 0x80u)
            return 0;                                 /* link-local fe80::/10 */
        if (b[0] == 0xffu) return 0;                  /* multicast */
        if (b[0] == 0x20u && b[1] == 0x01u && b[2] == 0x0du
            && b[3] == 0xb8u) return 0;               /* documentation */
        int mapped = 1;
        for (int i = 0; i < 10; i++) if (b[i] != 0) mapped = 0;
        if (mapped && b[10] == 0xffu && b[11] == 0xffu) {
            struct sockaddr_in mapped4;
            memset(&mapped4, 0, sizeof(mapped4));
            mapped4.sin_family = AF_INET;
            memcpy(&mapped4.sin_addr, b + 12, 4);
            return zan_io_sockaddr_is_safe(&mapped4,
                (int32_t)sizeof(mapped4), allow_loopback);
        }
        return 1;
    }
    return 0;
}

int32_t zan_io_resolve_all(const char *name, int32_t port, void *buf,
                           int32_t cap) {
    if (!buf || cap <= 0) return 0;
    if (!name || !*name || cap < ZAN_IO_SA_STRIDE) {
        memset(buf, 0, (size_t)cap);
        return 0;
    }
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    /* Do not set AI_ADDRCONFIG: it suppresses valid A or AAAA answers based
     * on the local interface configuration, which would make the reviewed set
     * differ between machines. The caller needs every resolver candidate. */
    hints.ai_flags = 0;
    struct addrinfo *res = NULL;
    if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res) {
        memset(buf, 0, (size_t)cap);
        return 0;
    }
    int32_t capacity = cap / ZAN_IO_SA_STRIDE;
    int32_t count = 0;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        int32_t family = zan_io_sockaddr_family(p->ai_addr,
            (int32_t)p->ai_addrlen);
        int32_t len = family == AF_INET6 ? (int32_t)sizeof(struct sockaddr_in6)
                     : family == AF_INET ? (int32_t)sizeof(struct sockaddr_in) : 0;
        if (!len || p->ai_addrlen < (size_t)len) continue;
        /* Stable de-duplication also makes a resolver returning duplicate A/AAAA
         * records unable to consume an unbounded number of caller slots. */
        int duplicate = 0;
        for (int32_t i = 0; i < count; i++) {
            unsigned char *old = (unsigned char *)buf + i * ZAN_IO_SA_STRIDE;
            int oldfam = zan_io_sockaddr_family(old, len);
            if (oldfam == family && memcmp(old, p->ai_addr, (size_t)len) == 0) {
                duplicate = 1; break;
            }
        }
        if (duplicate) continue;
        if (count >= capacity) {
            /* Never return a partial reviewable set: the caller must either
             * inspect every resolver answer or receive failure. */
            memset(buf, 0, (size_t)cap);
            freeaddrinfo(res);
            return 0;
        }
        unsigned char *dst = (unsigned char *)buf + count * ZAN_IO_SA_STRIDE;
        memset(dst, 0, ZAN_IO_SA_STRIDE);
        memcpy(dst, p->ai_addr, (size_t)len);
        count++;
    }
    freeaddrinfo(res);
    return count;
}

int64_t zan_io_resolve_all_async(intptr_t name_ptr, int32_t port,
                                 intptr_t buf_ptr, int32_t cap) {
    return (int64_t)zan_io_resolve_all((const char *)name_ptr, port,
                                       (void *)buf_ptr, cap);
}

#if defined(_WIN32)
/* Read the encoded DER pointer/length from a Windows PCCERT_CONTEXT.
 * Returns the pointer to the encoded bytes and writes the length to *out_len.
 * Returns NULL when the context is invalid.  This keeps the CERT_CONTEXT
 * layout out of the Zan standard library. */
const unsigned char *zan_crypto_cert_encoded(const void *cert, int *out_len) {
    const CERT_CONTEXT *ctx = (const CERT_CONTEXT *)cert;
    if (!ctx || !out_len) return NULL;
    *out_len = (int)ctx->cbCertEncoded;
    return ctx->pbCertEncoded;
}
#endif

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

int32_t zan_io_connect_sa_start(intptr_t fd, const void *sa, int32_t salen) {
    if (!sa || zan_io_sockaddr_family(sa, salen) == 0) return -1;
    if (zan_io_set_nonblocking(fd) != 0) return -1;
#if defined(_WIN32)
    SOCKET s = (SOCKET)fd;
    int r = connect(s, (const struct sockaddr *)sa, salen);
    if (r == 0) return 0;
    int e = WSAGetLastError();
    if (e == WSAEINPROGRESS || e == WSAEWOULDBLOCK || e == WSAEALREADY)
        return -2;
    return e > 0 ? e : -1;
#else
    int r = connect((int)fd, (const struct sockaddr *)sa, (socklen_t)salen);
    if (r == 0) return 0;
    if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EALREADY)
        return -2;
    return errno > 0 ? errno : -1;
#endif
}

/* Blocking-worker variant of exact connect. The caller's sockaddr is consumed
 * only during the initial syscall; no DNS or text conversion is performed. */
int64_t zan_io_connect_sa(intptr_t fd, const void *sa, int32_t salen,
                           int32_t timeout_ms) {
    int32_t r = zan_io_connect_sa_start(fd, sa, salen);
    if (r == 0 || r != -2) return r;
#if defined(_WIN32)
    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_ZERO(&efds);
    FD_SET((SOCKET)fd, &wfds); FD_SET((SOCKET)fd, &efds);
    struct timeval tv, *ptv = NULL;
    if (timeout_ms >= 0) { tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000; ptv = &tv; }
    if (select(0, NULL, &wfds, &efds, ptv) <= 0) return -3;
    int err = 0, len = (int)sizeof(err);
    if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0)
        return -1;
#else
    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_ZERO(&efds);
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    FD_SET((int)fd, &wfds); FD_SET((int)fd, &efds);
    struct timeval tv, *ptv = NULL;
    if (timeout_ms >= 0) { tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000; ptv = &tv; }
    int sr;
    do { sr = select((int)fd + 1, NULL, &wfds, &efds, ptv); }
    while (sr < 0 && errno == EINTR);
    if (sr <= 0) return sr == 0 ? -3 : -1;
    int err = 0;
    socklen_t len = (socklen_t)sizeof(err);
    if (getsockopt((int)fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
        return -1;
#endif
    return err == 0 ? 0 : err;
}

/* ---- fd-indexed watcher slots (POSIX readiness backends) ----
 * Shared by the epoll and kqueue backends: a watcher maps a fd to the
 * waiting coroutine, and both registering and waking are O(1) with no
 * per-await malloc. One waiter per direction lives inline in the slot
 * (the common case: one coroutine reading and/or writing a socket); the
 * rare extra waiter on the same fd+direction chains through `next`. The
 * select backend stays on the linked list (FD_SETSIZE keeps it small).
 */
#if !defined(_WIN32)

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




/* Perform the pending recv/accept for a woken waiter (see io_deliver_recv). */
static void io_deliver_waiter(int fd, zan_io_waiter_t *w) {
    if (w->out_accept) {
        *w->out_accept = (intptr_t)accept(fd, NULL, NULL);
        return;
    }
    if (!w->out_n) return;
    ssize_t rn;
    do {
        rn = recv(fd, w->rbuf, (size_t)w->rlen, 0);
    } while (rn < 0 && errno == EINTR);
    /* Retry transient EINTR rather than fabricate an EOF: see the twin in
     * io_deliver_recv. */
    *w->out_n = (rn < 0) ? 0 : (int64_t)rn;
}

/* Detach up to `max` waiters of one direction into `out`, returning how many
 * were taken. Anything past `max` stays parked -- dropping it would strand the
 * coroutine -- so the direction keeps its waiters and the next event or sweep
 * picks the rest up. */
static int io_take(zan_io_slot_t *s, int fd, int read_dir,
                   zan_io_waiter_t *out, int max) {
    unsigned char *has = read_dir ? &s->has_r : &s->has_w;
    if (!*has || max <= 0) return 0;
    zan_io_waiter_t *inl = read_dir ? &s->r : &s->w;
    int n = 0;
    out[n++] = *inl;
    zan_io_waiter_t *x = inl->next;
    while (x && n < max) {
        zan_io_waiter_t *nx = x->next;
        out[n++] = *x;
        free(x);
        x = nx;
    }
    if (x) {
        *inl = *x;   /* the overflow head moves into the inline slot */
        free(x);
    } else {
        inl->next = NULL;
        *has = 0;
    }
    (void)fd;
    return n;
}

/* Remove a waiter that was just inserted, for the registration-failure paths:
 * io_mark_dead queues its own delivery, so a waiter still reachable from the
 * slot would let the sweep wake the same frame a second time (an 8-byte UAF
 * through *out_n/*out_accept, whose storage typically belongs to an already
 * finished frame) and leaks one g_io_count. Mirrors io_take's unlink order. */
static void io_unlink_waiter(zan_io_slot_t *s, int read_dir,
                             zan_io_waiter_t *w) {
    unsigned char *has = read_dir ? &s->has_r : &s->has_w;
    zan_io_waiter_t *inl = read_dir ? &s->r : &s->w;
    if (w == inl) {
        zan_io_waiter_t *rest = inl->next;
        if (rest) {
            *inl = *rest;   /* promote the first chained node into the slot */
            free(rest);
        } else {
            inl->next = NULL;
            *has = 0;
        }
        return;
    }
    zan_io_waiter_t *p = inl;
    while (p && p->next != w) p = p->next;
    if (!p) return;   /* not reachable: nothing to undo */
    p->next = w->next;
    free(w);
}

/* Fail every waiter still parked on this slot (both directions). Used when
 * the backend refuses to (re-)arm the fd: such waiters are invisible to the
 * backend AND to the sweep, which only rescues dead fds. recv gets the
 * end-of-stream shape (0) and accept -1 -- the same contract io_flush_dead
 * applies when the reactor itself is unusable. Returns how many waiters were
 * failed (each represented one g_io_count). Shared by the epoll and kqueue
 * backends (same slot/waiter layout, same dead-delivery contract). */
static int io_fail_slot_waiters(zan_io_slot_t *s) {
    int failed = 0;
    for (int dir = 0; dir < 2; dir++) {
        unsigned char *has = dir ? &s->has_r : &s->has_w;
        zan_io_waiter_t *inl = dir ? &s->r : &s->w;
        if (!*has) continue;
        io_mark_dead(inl->co, inl->step, inl->out_n, inl->out_accept);
        failed++;
        zan_io_waiter_t *x = inl->next;
        while (x) {
            zan_io_waiter_t *nx = x->next;
            io_mark_dead(x->co, x->step, x->out_n, x->out_accept);
            free(x);
            failed++;
            x = nx;
        }
        inl->next = NULL;
        *has = 0;
    }
    g_io_count -= failed;
    return failed;
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
        s->in_epoll = 0;
        for (;;) {
            zan_io_waiter_t ready[16];
            int nr = io_take(s, fd, 1, ready, 16);
            nr += io_take(s, fd, 0, ready + nr, 16 - nr);
            if (nr == 0) break;
            for (int k = 0; k < nr; k++) {
                if (ready[k].out_accept)   *ready[k].out_accept = -1;
                else if (ready[k].out_n)   *ready[k].out_n = 0;
                g_io_count--;
                io_wake(ready[k].co, ready[k].step);
                woke++;
            }
        }
    }
    return woke;
}
#endif /* !_WIN32 */

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

void zan_io_init(void) {
    if (g_io_started) return;
    zan_io_ignore_sigpipe();
    /* A fresh reactor lifetime clears any poison from a failed previous one:
     * shutdown+re-init is real (schedulers and embedded hosts do it). */
    g_io_broken = 0;
    g_io_entries = NULL;
    g_io_count = 0;
    g_epoll_fd = epoll_create1(0);
    /* e.g. EMFILE: mark the backend broken so every later await fails
     * through the dead queue instead of parking forever with zero
     * diagnostics -- epoll_wait on -1 never reports anything. */
    if (g_epoll_fd < 0) g_io_broken = 1;
    /* Wake fd for async-DNS completions: a persistent (non-ONESHOT) EPOLLIN
     * member of the set, so a worker thread's eventfd write wakes zan_io_poll
     * even when no socket watcher is ready. */
    g_dns_wake_fd = eventfd(0, EFD_NONBLOCK);
    if (g_dns_wake_fd >= 0 && g_epoll_fd >= 0) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.fd = g_dns_wake_fd;
        epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, g_dns_wake_fd, &ev);
    }
    g_io_started = 1;
}void zan_io_shutdown(void) {
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
    dns_shutdown_cleanup();
    if (g_epoll_fd >= 0) { close(g_epoll_fd); g_epoll_fd = -1; }
    if (g_dns_wake_fd >= 0) { close(g_dns_wake_fd); g_dns_wake_fd = -1; }
    g_io_started = 0;
}

static int io_arm(int fd, zan_io_slot_t *s) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd;
    ev.events = EPOLLONESHOT;
    if (s->has_r) ev.events |= EPOLLIN;
    if (s->has_w) ev.events |= EPOLLOUT;
    /* Returns 1 when armed, 0 when the backend ultimately refused the fd
     * (EPERM/ENOMEM/... even after EINTR retries). Callers MUST fail the
     * slot's waiters on 0: a live fd the backend will not report can never
     * wake them, and the sweep skips live fds -- parking here means forever. */
    for (int attempt = 0; attempt < 8; attempt++) {
        if (!s->in_epoll) {
            if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
                s->in_epoll = 1;
                return 1;
            }
            if (errno == EINTR) continue;
            if (errno == EEXIST) { s->in_epoll = 1; continue; } /* fall to MOD */
            return 0;
        }
        if (epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &ev) == 0) return 1;
        if (errno == EINTR) continue;
        if (errno == ENOENT) { s->in_epoll = 0; continue; } /* re-add */
        return 0;
    }
    return 0;
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    if (g_io_broken) {   /* backend never started: fail, don't park forever */
        io_mark_dead(co, step, g_pending_out_n, g_pending_accept_out);
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        return;
    }
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
        if (!w) {   /* same as a slot table that cannot grow: fail the waiter
                     * rather than park a coroutine nothing will ever resume */
            io_mark_dead(co, step, g_pending_out_n, g_pending_accept_out);
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
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
    g_pending_accept_out = NULL;
    g_io_count++;

    if (!io_arm(fd, s)) {
        /* The backend refused this fd even after retries: without an arm,
         * nothing will ever report it ready and the sweep skips live fds.
         * Unlink the just-queued waiter and fail it through its own sinks
         * (recv -> 0, accept -> -1) instead of parking it forever. */
        io_unlink_waiter(s, interest == ZAN_IO_READ ? 1 : 0, w);
        g_io_count--;
        io_mark_dead(co, step, w->out_n, w->out_accept);
    }
}

/* One waiter per direction is stored inline in the slot (the common case:
 * one coroutine reading and/or writing a socket). The rare extra waiter on
 * the same fd+direction chains through `next`. */


int32_t zan_io_poll(int64_t timeout_ms) {
    if (g_io_dead) return io_flush_dead();
    if (g_io_count == 0 && g_blocking_inflight == 0) return 0;
    /* A waiter whose fd was closed under it is invisible to the backend, so it
     * is the sweep below -- reached when the (now bounded) wait times out --
     * that keeps it from becoming a hang. Sweeping before every wait instead
     * costs one liveness probe per parked waiter, i.e. a syscall per connection
     * on every idle turn of the loop. */
    struct epoll_event events[256];
    int n;
    for (;;) {
        int64_t wait = dns_wait_ms(timeout_ms);
        int capped = (wait < 0 || wait > ZAN_IO_SWEEP_MS);
        if (capped) wait = ZAN_IO_SWEEP_MS;
        n = epoll_wait(g_epoll_fd, events, 256, (int)wait);
        /* n < 0 (EINTR from a stray signal, or a transient backend error) is
         * NOT quiescence: falling through with 0 would make
         * zan_co_sched_run_until declare the program finished and drop every
         * parked coroutine. Fold it into the timeout path, whose housekeeping
         * passes and loop-exit checks are exactly what a spurious wake needs. */
        if (n < 0) n = 0;
        if (n != 0) break;
        int w2 = dns_timeout_scan();
        if (w2) return w2;
        int w3 = dns_drain();
        if (w3) return w3;
        int w4 = io_sweep_slots();
        if (w4) return w4;
        /* A wait shortened only to schedule the sweep has not expired for the
         * caller: keep waiting rather than report "nothing to wait for". */
        if (!capped || (g_io_count == 0 && g_blocking_inflight == 0)) return 0;
        if (timeout_ms >= 0) return 0;
    }
    int woke = 0;
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        if (fd == g_dns_wake_fd) {
            /* Async-DNS completion: deliver every finished lookup. */
            dns_wake_read();
            woke += dns_drain();
            continue;
        }
        if (fd < 0 || fd >= g_slots_cap) continue;
        zan_io_slot_t *s = &g_slots[fd];
        uint32_t ev = events[i].events;
        int err = (ev & (EPOLLERR | EPOLLHUP)) != 0;

        zan_io_waiter_t ready[8];
        int nr = 0;
        if ((ev & EPOLLIN) || err)  nr += io_take(s, fd, 1, ready + nr, 8 - nr);
        if ((ev & EPOLLOUT) || err) nr += io_take(s, fd, 0, ready + nr, 8 - nr);

        /* Re-arm before waking: a woken coroutine may register again and the
         * EPOLLONESHOT arm state must already reflect the remaining waiters.
         * If the backend refuses the re-arm, those remaining waiters would
         * never be reported again -- fail them now (they carry their own
         * result sinks). */
        if (s->has_r || s->has_w) {
            if (!io_arm(fd, s)) io_fail_slot_waiters(s);
        }

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
    zan_io_ignore_sigpipe();
    /* Fresh reactor lifetime: clear any poison from a failed previous one. */
    g_io_broken = 0;
    g_io_entries = NULL;
    g_io_count = 0;
    g_kq_fd = kqueue();
    /* Same broken-marking as the epoll backend: fail awaits loudly. */
    if (g_kq_fd < 0) g_io_broken = 1;
    /* Async-DNS wake pipe: the read end joins the kqueue with a persistent
     * (non-ONESHOT) EVFILT_READ, so a worker thread's write wakes zan_io_poll
     * even when no socket watcher is ready. */
    if (g_kq_fd >= 0 && g_dns_wake_fd < 0) {
        int pfd[2];
        if (pipe(pfd) == 0) {
            g_dns_wake_fd = pfd[0];
            g_dns_wake_wfd = pfd[1];
            fcntl(pfd[0], F_SETFL, O_NONBLOCK);
            struct kevent kev;
            EV_SET(&kev, (uintptr_t)pfd[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
            kevent(g_kq_fd, &kev, 1, NULL, 0, NULL);
        }
    }
    g_io_started = 1;
}

void zan_io_shutdown(void) {
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
    io_dead_clear();
    dns_shutdown_cleanup();
    if (g_kq_fd >= 0) { close(g_kq_fd); g_kq_fd = -1; }
    if (g_dns_wake_fd >= 0) { close(g_dns_wake_fd); g_dns_wake_fd = -1; }
    if (g_dns_wake_wfd >= 0) { close(g_dns_wake_wfd); g_dns_wake_wfd = -1; }
    g_io_started = 0;
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    if (g_io_broken) {   /* backend never started: fail, don't park forever */
        io_mark_dead(co, step, g_pending_out_n, g_pending_accept_out);
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        return;
    }
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
        if (!w) {   /* same as a slot table that cannot grow: fail the waiter
                     * rather than park a coroutine nothing will ever resume */
            io_mark_dead(co, step, g_pending_out_n, g_pending_accept_out);
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
    /* Snapshot the result sinks BEFORE clearing the pending globals: if the
     * kevent below fails, the waiter must still be failed THROUGH its sinks.
     * Passing the already-NULL globals made io_flush_dead wake the frame
     * without writing anything, so the resumed coroutine read its previous
     * RESULT back as a recv byte count or accepted fd. */
    {
        int64_t *fail_out_n = g_pending_out_n;
        intptr_t *fail_accept = g_pending_accept_out;
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        g_io_count++;

        /* EV_ONESHOT arms the fd for one event; the next io_register issues a
         * fresh EV_ADD, so there is no per-event re-arm bookkeeping. */
        struct kevent kev;
        short filter = (interest == ZAN_IO_READ) ? EVFILT_READ : EVFILT_WRITE;
        EV_SET(&kev, fd, filter, EV_ADD | EV_ONESHOT, 0, 0, NULL);
        if (kevent(g_kq_fd, &kev, 1, NULL, 0, NULL) != 0) {
            /* fd was closed or otherwise invalid: fail the waiter so it does
             * not park forever. Unlink it from the slot FIRST -- io_mark_dead
             * queues its own delivery, and a waiter still reachable here would
             * let the sweep wake this same frame a second time (an 8-byte UAF
             * through *out_n/*out_accept) and leak one g_io_count. */
            io_unlink_waiter(s, interest == ZAN_IO_READ ? 1 : 0, w);
            g_io_count--;
            io_mark_dead(co, step, fail_out_n, fail_accept);
            return;
        }
    }
}

int32_t zan_io_poll(int64_t timeout_ms) {
    if (g_io_dead) return io_flush_dead();
    if (g_io_count == 0 && g_blocking_inflight == 0) return 0;
    /* A waiter whose fd was closed under it is invisible to the backend, so it
     * is the sweep below -- reached when the (now bounded) wait times out --
     * that keeps it from becoming a hang. Sweeping before every wait instead
     * costs one liveness probe per parked waiter, i.e. a syscall per connection
     * on every idle turn of the loop. */
    struct kevent events[64];
    int n;
    for (;;) {
        int64_t wait = dns_wait_ms(timeout_ms);
        int capped = (wait < 0 || wait > ZAN_IO_SWEEP_MS);
        if (capped) wait = ZAN_IO_SWEEP_MS;
        struct timespec ts = { wait / 1000, (wait % 1000) * 1000000L };
        n = kevent(g_kq_fd, NULL, 0, events, 64, &ts);
        /* n < 0 (EINTR from a stray signal) is NOT quiescence: returning 0
         * here would make zan_co_sched_run_until declare the program
         * finished and drop every parked coroutine. Fold it into the timeout
         * path -- its housekeeping passes and loop-exit checks are exactly
         * what a spurious wake needs. */
        if (n < 0) n = 0;
        if (n != 0) break;
        int w2 = dns_timeout_scan();
        if (w2) return w2;
        int w3 = dns_drain();
        if (w3) return w3;
        int w4 = io_sweep_slots();
        if (w4) return w4;
        /* A wait shortened only to schedule the sweep has not expired for the
         * caller: keep waiting rather than report "nothing to wait for". */
        if (!capped || (g_io_count == 0 && g_blocking_inflight == 0)) return 0;
        if (timeout_ms >= 0) return 0;
    }
    int woke = 0;
    for (int i = 0; i < n; i++) {
        int fd = (int)events[i].ident;
        if (fd == g_dns_wake_fd) {
            /* Async-DNS completion: deliver every finished lookup. */
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
        /* This delivery consumed the filter's EV_ONESHOT. When waiters are
         * still queued in that direction -- a readiness burst larger than
         * the batch of 8 -- nothing else would re-arm it: io_register only
         * runs when a NEW waiter parks, and these are already parked. The
         * epoll backend re-arms with EPOLL_CTL_MOD in exactly this spot;
         * keep the two backends at parity or the overflow waiters hang.
         * A failed kevent here means the fd died mid-burst; the sweep's
         * liveness probes fail those waiters on the next capped timeout. */
        if ((events[i].filter == EVFILT_READ && s->has_r) ||
            (events[i].filter == EVFILT_WRITE && s->has_w)) {
            struct kevent rk;
            int retries;
            EV_SET(&rk, (uintptr_t)fd, events[i].filter,
                   EV_ADD | EV_ONESHOT, 0, 0, NULL);
            int rearmed = 0;
            for (retries = 0; retries < 8 && !rearmed; retries++) {
                if (kevent(g_kq_fd, &rk, 1, NULL, 0, NULL) == 0) rearmed = 1;
                else if (errno != EINTR) break;
            }
            /* A persistent failure with a LIVE fd means those remaining
             * waiters will never be reported again; fail them through their
             * own sinks instead of parking them past the end of time. */
            if (!rearmed &&
                ((events[i].filter == EVFILT_READ && s->has_r) ||
                 (events[i].filter == EVFILT_WRITE && s->has_w))) {
                io_fail_slot_waiters(s);
            }
        }
        for (int k = 0; k < nr; k++) {
            io_deliver_waiter(fd, &ready[k]);
            g_io_count--;
            io_wake(ready[k].co, ready[k].step);
            woke++;
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
/* Opt-in synchronous-completion fast path (see ensure_assoc). */
static volatile LONG g_syncfast = -1;   /* -1 unread, 1 on, 0 off */

static volatile LONG g_sync_inline;     /* completions delivered without a packet */

static int syncfast_on(void) {
    LONG v = g_syncfast;
    if (v < 0) {
        const char *e = getenv("ZAN_IO_SYNCFAST");
        v = (e && *e && e[0] != '0') ? 1 : 0;
        InterlockedExchange(&g_syncfast, v);
    }
    return (int)v;
}

/* ---- reactor shards ----
 * One completion port per worker instead of one for the whole pool. A socket is
 * owned by the shard its handle hashes to, so every completion for that socket
 * reaches the same worker: the connection's steps run on one thread (hot LIFO
 * slot, no steal) and readying them needs no cross-worker wake packet. The
 * measured cost of the single shared port was a park/wake round trip per few
 * messages -- 418k parks and 134k wake packets for 2M messages on 4 workers.
 *
 * Shard 0 is g_iocp, and shards exist only while the worker pool runs: a port
 * nobody waits on would strand its completions, so anything that runs outside
 * the pool (or ZAN_IO_SHARDS=1) keeps using the single port it used before. */
#define ZAN_IO_MAXSHARD 64
static HANDLE        g_shard[ZAN_IO_MAXSHARD];
static volatile LONG g_shards = 1;

static HANDLE io_shard(int i) {
    if (i <= 0 || i >= (int)g_shards) return g_iocp;
    return g_shard[i] ? g_shard[i] : g_iocp;
}

/* Owning shard of a socket. Hashing the handle keeps the answer stable without
 * a registry -- important because association is issued before every op and
 * must always name the same port -- and spreads accepted connections over the
 * workers, which binding "to the caller's shard" would not: an accepted socket's
 * first op runs on the worker that got the accept completion, i.e. always the
 * listener's. Windows socket handles are multiples of 4, hence the shift. */
static HANDLE io_shard_of(SOCKET s) {
    int n = (int)g_shards;
    if (n <= 1) return g_iocp;
    return io_shard((int)((((uintptr_t)s) >> 2) % (unsigned)n));
}

/* Open `n` shards (shard 0 is the already-created g_iocp). Called by the pool
 * before its workers start, so every shard has a waiter from the first packet
 * on. ZAN_IO_SHARDS overrides the count: 1 restores the single shared port,
 * which is the escape hatch for comparing the two reactors. */
static void io_shards_start(int n) {
    const char *e = getenv("ZAN_IO_SHARDS");
    if (e && *e) {
        int v = atoi(e);
        if (v > 0 && v < n) n = v;
    }
    if (n > ZAN_IO_MAXSHARD) n = ZAN_IO_MAXSHARD;
    for (int i = 1; i < n; i++) {
        if (!g_shard[i])
            g_shard[i] = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (!g_shard[i]) { n = i; break; }   /* keep only what we could open */
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
 * so always doing it is the correct and race-free choice.
 *
 * Returns 1 when this socket will NOT post a packet for an operation that
 * completes inline (FILE_SKIP_COMPLETION_PORT_ON_SUCCESS), so the op site must
 * deliver that completion itself. The answer is returned rather than kept in a
 * global because the mode is per socket and may fail for one handle while
 * holding for others: a stale global would either lose a completion or deliver
 * it twice. Callers ask right before issuing their op, on the same thread. */
static int ensure_assoc(SOCKET s) {
#if defined(ZAN_CO_DRIVER)
    HANDLE port = io_shard_of(s);
#else
    HANDLE port = g_iocp;
#endif
    if (CreateIoCompletionPort((HANDLE)s, port, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;   /* ERROR_INVALID_PARAMETER == already bound to this port */
    }
#if defined(ZAN_CO_DRIVER)
    /* Skip-on-success saves a kernel packet and, when the pool is otherwise
     * idle, the park/wake round trip behind it -- on this workload most recvs
     * complete inline because the bytes are already in the socket buffer.
     *
     * Resuming from the op site happens mid-step, while the coroutine that
     * issued the op is still executing. That is safe with the frame state word:
     * zan_co_ready sees CO_RUNNING and banks the step (CO_NOTIFIED) instead of
     * queueing, and co_run re-submits the frame after the step has saved its
     * live slots and returned. It was NOT safe before that state word existed,
     * which is why this used to be disabled outright under the multi-worker
     * driver -- hence the ZAN_IO_SYNCFAST gate rather than a silent default. */
    if (!syncfast_on()) return 0;
    return SetFileCompletionNotificationModes(
               (HANDLE)s, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS) ? 1 : 0;
#else
    return 0;
#endif
}

void zan_io_init(void) {
    if (g_io_started) return;
    zan__crash_install();
    g_io_count = 0;
    /* Fresh reactor lifetime; and if the port itself cannot be created
     * (handle exhaustion -- the Windows analogue of EMFILE), mark the
     * backend broken so every await fails loudly through the dead queue
     * instead of parking forever with its completion packet going nowhere. */
    g_io_broken = 0;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (g_iocp == NULL) g_io_broken = 1;
#if defined(ZAN_CO_DRIVER)
    InitializeSListHead(&g_op_slist);   /* lock-free op pool for the workers */
#endif
    g_io_started = 1;
}

void zan_io_shutdown(void) {
#if defined(ZAN_CO_DRIVER)
    io_shards_stop();
#endif
    if (g_iocp) { CloseHandle(g_iocp); g_iocp = NULL; }
    g_io_count = 0;
    /* Drop pooled ops: the completion port is gone, so recycled op structs are
     * all stale for any future zan_io_init(). */
    dns_shutdown_cleanup();
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
    if (g_io_broken) {
        /* Port creation failed: an issued op's completion packet would go
         * nowhere and park this waiter forever. Readiness waits carry no
         * result sink, so resuming is the whole failure report. */
        if (step) zan_co_ready(co, step);
        return;
    }
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

    int skip = ensure_assoc(s);
    (void)skip;   /* only the multi-worker driver completes inline */
    IOTRACE("io_register(READY) fd=%lld interest=%d cnt=%d", (long long)s, interest, g_io_count);

    zan_io_op_t *op = op_alloc();
    if (!op) {   /* out of memory: resume the waiter rather than park it forever */
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
        if (skip) {
            op_free(op);
            IO_CNT_DEC();
            InterlockedIncrement(&g_sync_inline);
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
#if defined(ZAN_CO_DRIVER)
    if (!PostQueuedCompletionStatus(io_shard_of(s), 0, (ULONG_PTR)s, &op->ov)) {
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov)) {
#endif
        /* IOCP is gone or otherwise unusable; resume the waiter directly so
         * it does not hang forever, and recycle the op. */
        io_wake(co, step);
        op_free(op);
        IO_CNT_DEC();
    }
}

/* Overlapped receive. Issues the real WSARecv into `buf` (no zero-byte probe),
 * so exactly one op delivers the data and its byte count -- eliminating the
 * probe/synchronous-recv window that leaked completions under the multi-worker
 * driver at high load. The byte count reaches the coroutine via `*out_n`, set
 * either inline (immediate completion) or when the completion is dequeued. */
void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
                    zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
    /* Broken backend (port creation failed): fail with peer-close semantics
     * instead of issuing an op whose completion packet has nowhere to go. */
    if (g_io_broken) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }
    SOCKET s = (SOCKET)fd;
    int skip = ensure_assoc(s);
    (void)skip;

    /* A negative or >INT_MAX length would wrap in the WSABUF cast and come
     * back as WSAEINVAL -- reported as EOF. Reject it explicitly instead. */
    if (len <= 0 || len > 0x7FFFFFF0) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }

    zan_io_op_t *op = op_alloc();
    if (!op) {
        if (out_n) *out_n = 0;   /* peer-close semantics, as for a hard error */
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
    IOTRACE("recv_co fd=%lld len=%d op=%p -> r=%d got=%lu err=%d cnt=%d", (long long)fd, len, (void*)op, r, (unsigned long)got, r?WSAGetLastError():0, g_io_count);
    if (r == 0) {
#if defined(ZAN_CO_DRIVER)
        /* Immediate completion. With skip-on-success no packet is queued, so
         * deliver the count and resume here; otherwise a packet is still queued
         * and zan_io_poll/co_wait_io will deliver it (avoid double-completion). */
        if (skip) {
            if (out_n) *out_n = (int64_t)got;
            op_free(op);
            IO_CNT_DEC();
            InterlockedIncrement(&g_sync_inline);
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
#if defined(ZAN_CO_DRIVER)
    if (!PostQueuedCompletionStatus(io_shard_of(s), 0, (ULONG_PTR)s, &op->ov)) {
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov)) {
#endif
        /* The port is gone or the post failed for another reason: nothing
         * will dequeue this completion, so deliver inline like io_register's
         * failure path does -- otherwise the frame parks forever, the op
         * leaks and g_io_count stays elevated past quiescence. */
        if (step) zan_co_ready(frame, step);
        op_free(op);
        IO_CNT_DEC();
    }
}

void zan_io_accept_co(intptr_t fd, void *frame, zan_co_step_t step,
                      intptr_t *out_fd) {
    zan_io_init();
    /* Broken backend: report a failed accept rather than parking forever. */
    if (g_io_broken) {
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }
    SOCKET listener = (SOCKET)fd;
    IOTRACE("accept_co ENTER listener=%lld", (long long)fd);
    int skip = ensure_assoc(listener);
    (void)skip;

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

    /* The accepted socket's family must match the listener's (AcceptEx
     * rejects a mismatched one), and its local/remote address buffer must
     * fit a sockaddr of that family plus the extra 16 bytes AcceptEx
     * appends. Probe the listener's family instead of hard-coding IPv4. */
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
        /* AcceptEx would fail cleanly on NULL, but failing the waiter here
         * with its proper sink keeps the contract explicit. */
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
    IOTRACE("accept_co listener=%lld op=%p ok=%d err=%d cnt=%d", (long long)listener, (void*)op, (int)ok, WSAGetLastError(), g_io_count);
#if defined(ZAN_CO_DRIVER)
    /* Inline accept, no packet coming: deliver it here (as for recv above). */
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

#if defined(ZAN_CO_DRIVER)
/* Blocking dequeue for the legacy pump (zan_io_pump / a Task.Wait outside the
 * pool). With the reactor sharded there is no one port to block on, so walk the
 * shards with a short bounded wait each: blocking on a single shard would miss
 * completions sitting on the others. This path is off the pool's hot loop --
 * workers use co_wait_io, which waits on their own shard only. */
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
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    int64_t wait = dns_wait_ms(timeout_ms);
    DWORD to = (wait < 0) ? INFINITE : (DWORD)wait;
#if defined(ZAN_CO_DRIVER)
    if (!io_poll_any(entries, 64, &removed, to)) {
#else
    if (!GetQueuedCompletionStatusEx(g_iocp, entries, 64, &removed, to, FALSE)) {
#endif
        IOTRACE("poll GQCS=0 err=%lu to=%lu cnt=%d", (unsigned long)GetLastError(), (unsigned long)to, g_io_count);
        int w = dns_timeout_scan();
        if (w) return w;
        return dns_drain();
    }
    IOTRACE("poll removed=%lu cnt=%d", (unsigned long)removed, g_io_count);
    int woke = 0;
    for (ULONG i = 0; i < removed; i++) {
        if (!entries[i].lpOverlapped) {
            /* Async-DNS completion: a NULL-overlapped packet posted by a DNS
             * worker thread; deliver every finished lookup. */
            woke += dns_drain();
            continue;
        }
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
    zan_io_ignore_sigpipe();
    g_io_entries = NULL;
    g_io_count = 0;
    /* Async-DNS wake pipe: the read end joins the select set in zan_io_poll,
     * so a worker thread's write wakes it even when no socket is ready. */
    if (g_dns_wake_fd < 0) {
        int pfd[2];
        if (pipe(pfd) == 0) {
            g_dns_wake_fd = pfd[0];
            g_dns_wake_wfd = pfd[1];
            fcntl(pfd[0], F_SETFL, O_NONBLOCK);
        }
    }
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
    dns_shutdown_cleanup();
    if (g_dns_wake_fd >= 0) { close(g_dns_wake_fd); g_dns_wake_fd = -1; }
    if (g_dns_wake_wfd >= 0) { close(g_dns_wake_wfd); g_dns_wake_wfd = -1; }
    g_io_started = 0;
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    /* select can only represent fd numbers below FD_SETSIZE; fail anything it
     * (or the kernel) cannot watch instead of parking the coroutine. */
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
    g_io_count++;
}

int32_t zan_io_poll(int64_t timeout_ms) {
    if (g_io_dead) return io_flush_dead();
    if (g_io_count == 0 && g_blocking_inflight == 0) return 0;

    /* Every fd goes into an fd_set below, so a watcher whose socket has been
     * closed under it must leave the list first: FD_SET of a negative or
     * out-of-range fd is undefined behaviour (with -1 it writes outside the
     * fd_set, and select then waits on whatever bit that landed on -- fd 0 in
     * practice, which never becomes ready). */
    {
        int woke = io_sweep_entries(FD_SETSIZE);
        if (woke) return woke;
        if (g_io_count == 0 && g_blocking_inflight == 0) return 0;
    }

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    int max_fd = 0;

    /* The async-DNS wake pipe read end joins the read set (persistent).
     * FD_SET of an fd >= FD_SETSIZE is UB -- it clobbers adjacent fd_set
     * storage and select waits on a random bit. Skip the wake fd in that
     * case: async-DNS completions still arrive via the deadline scan. */
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

    int64_t wait = dns_wait_ms(timeout_ms);
    struct timeval tv;
    tv.tv_sec  = (long)(wait / 1000);
    tv.tv_usec = (long)((wait % 1000) * 1000);

    int n = select(max_fd + 1, &read_fds, &write_fds, NULL,
                   wait < 0 ? NULL : &tv);
    if (n <= 0) {
        int w2 = dns_timeout_scan();
        if (w2) return w2;
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

/* ================= ASYNC HOSTNAME RESOLUTION ================= */

/* Lookups still in flight past this deadline are delivered as failures so
 * the reactor never parks on a black-hole resolver. The worker thread keeps
 * running and frees its work item when it notices the timeout flag; its
 * result is discarded. */
#define ZAN_DNS_TIMEOUT_MS 10000

typedef struct zan_blocking_job {
    struct zan_blocking_job *next;
    struct zan_blocking_job *queue_next;
    int64_t  deadline_ms;   /* reactor-clock deadline; timed out when passed */
    int      timed_out;     /* reactor delivered a timeout failure for this */
    int      started;       /* a detached worker owns this job */
    int64_t  result;        /* resolved IPv4 (network byte order) or sockaddr
                               length, 0 on failure */
    void    *sabuf;         /* sockaddr sink (resolve_sa jobs), else NULL */
    int32_t  sacap;         /* sink capacity */
    int32_t  port;          /* target port for resolve_sa jobs */
    void    *frame;         /* stackless frame, or stackful fiber handle */
    zan_co_step_t step;     /* resume fn (NULL => stackful fiber) */
    int32_t *out;           /* result sink (&frame->result) */
    int64_t *out64;         /* scalar blocking-call result sink */
    void (*run)(struct zan_blocking_job *job);
} zan_blocking_job_t;

typedef struct zan_dns_work {
    zan_blocking_job_t job;
    char hostname[256];     /* NUL-terminated copy for the worker thread */
    /* Resolved sockaddr staged here by the worker thread. resolve_sa jobs
     * must NOT write job.sabuf directly: past the deadline the reactor flags
     * the job, wakes the destination frame with a failure, and that frame can
     * complete and be freed while this thread is still blocked inside
     * getaddrinfo -- a late direct copy would be a cross-thread UAF write.
     * Instead the reactor copies staging -> sabuf in dns_drain, under
     * dns_lock, while the destination frame is still parked. Timed-out jobs
     * never reach dns_drain (the worker discards them), so the sink is only
     * ever written for a lookup that is being delivered. */
    unsigned char sa_staging[sizeof(struct sockaddr_in6)];
} zan_dns_work_t;

static zan_blocking_job_t *g_blocking_pending; /* in flight, owned by workers */
static zan_blocking_job_t *g_blocking_done;    /* completed, reactor delivery */
static zan_blocking_job_t *g_blocking_queued;  /* accepted, awaiting worker */
static int32_t g_blocking_active;

#define ZAN_BLOCKING_MAX_ACTIVE 64

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

/* Start one queued job if a worker slot is available.  Queue overflow is
 * deliberately kept asynchronous: a failed thread creation completes the
 * operation with a failure rather than running it on the reactor. */
static void blocking_start_next(void);

static void blocking_spawn(zan_blocking_job_t *job);

static void blocking_finish_worker(zan_blocking_job_t *job) {
    zan_blocking_job_t *next = NULL;
    int discard = 0;
    dns_lock();
    if (job->timed_out) {
        job->started = 0;
        if (g_blocking_active > 0) g_blocking_active--;
        discard = 1;
    } else {
        zan_blocking_job_t **pp = &g_blocking_pending;
        while (*pp && *pp != job) pp = &(*pp)->next;
        if (!*pp) {
            discard = 1;
        } else {
            *pp = job->next;
            job->next = g_blocking_done;
            g_blocking_done = job;
            job->started = 0;
            if (g_blocking_active > 0) g_blocking_active--;
        }
    }
    if (g_blocking_active < ZAN_BLOCKING_MAX_ACTIVE && g_blocking_queued) {
        next = g_blocking_queued;
        g_blocking_queued = next->queue_next;
        next->queue_next = NULL;
        next->started = 1;
        g_blocking_active++;
    }
    dns_unlock();
    if (discard) free(job);
    if (!discard) dns_wake_notify();
    if (next) blocking_spawn(next);
}

/* Worker thread trampoline: `run` is a C native operation only.  It never
 * enters Zan, touches ARC, allocates through the Zan allocator, or mutates
 * coroutine state.  Completion ownership stays with the reactor. */
#if defined(_WIN32)
static DWORD WINAPI blocking_worker(void *arg) {
#else
static void *blocking_worker(void *arg) {
#endif
    zan_blocking_job_t *job = (zan_blocking_job_t *)arg;
    job->run(job);
    blocking_finish_worker(job);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void blocking_start_failed(zan_blocking_job_t *job) {
    int deliver = 0;
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
    dns_unlock();
    if (deliver) dns_wake_notify();
    else free(job);
    blocking_start_next();
}

static void blocking_spawn(zan_blocking_job_t *job) {
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
        next->started = 1;
        g_blocking_active++;
    }
    dns_unlock();
    if (next) blocking_spawn(next);
}

static void blocking_submit(zan_blocking_job_t *job) {
    int start = 0;
    dns_lock();
    job->next = g_blocking_pending;
    g_blocking_pending = job;
    g_blocking_inflight++;
    if (g_blocking_active < ZAN_BLOCKING_MAX_ACTIVE) {
        job->started = 1;
        g_blocking_active++;
        start = 1;
    } else {
        zan_blocking_job_t **tail = &g_blocking_queued;
        while (*tail) tail = &(*tail)->queue_next;
        *tail = job;
    }
    dns_unlock();
    if (start) blocking_spawn(job);
}

static void dns_job_run(zan_blocking_job_t *job) {
    zan_dns_work_t *w = (zan_dns_work_t *)job;
    if (w->job.sabuf) {
        /* Resolve into the job-local staging buffer, never into sabuf (see
         * the comment on sa_staging): the frame this sink points into may be
         * woken and freed the moment the deadline passes, while this thread
         * is still inside getaddrinfo. */
        int32_t cap = w->job.sacap;
        if (cap < 0 || cap > (int32_t)sizeof w->sa_staging)
            cap = (int32_t)sizeof w->sa_staging;
        w->job.result = zan_io_resolve_sa(w->hostname, w->job.port,
                                          w->sa_staging, cap);
    } else {
        w->job.result = zan_io_resolve_ipv4(w->hostname);
    }
}

/* Milliseconds until the earliest in-flight lookup times out, or caller_ms if
 * none would fire sooner (-1 meaning unbounded). Caps every backend wait so a
 * black-hole resolver cannot hang the reactor forever. */
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
    if (wait <= 0) wait = 0;               /* already late: let the scan fire */
    if (caller_ms < 0) return wait;
    return wait < caller_ms ? wait : caller_ms;
}

/* Deliver timeout failures for lookups past their deadline (reactor thread).
 * Each job is flagged so its worker frees it when it finishes; the frame is
 * re-readied with a failure value, same as a resolution failure. */
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
        g_blocking_inflight--;
        if (!j->started) {
            zan_blocking_job_t **qp = &g_blocking_queued;
            while (*qp && *qp != j) qp = &(*qp)->queue_next;
            if (*qp == j) *qp = j->queue_next;
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
        if (!j->started) free(j);
        woke++;
    }
    return woke;
}

static void dns_wake_notify(void) {
#if defined(_WIN32)
    /* NULL overlapped marks a DNS completion packet in zan_io_poll. */
#if defined(ZAN_CO_DRIVER)
    /* A lookup belongs to no socket, so it has no owning shard: nudge them all
     * (dns_drain is idempotent) rather than pick one whose worker may be parked
     * on a long timeout. Lookups are rare enough for the extra packets. */
    for (int i = 0; i < (int)g_shards; i++)
        PostQueuedCompletionStatus(io_shard(i), 0, (ULONG_PTR)-2, NULL);
#else
    PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)-2, NULL);
#endif
#elif defined(__linux__)
    uint64_t one = 1;
    ssize_t r = write(g_dns_wake_fd, &one, sizeof(one));
    (void)r;
#else
    char c = 1;
    ssize_t r = write(g_dns_wake_fd, &c, 1);
    (void)r;
#endif
}

static void dns_wake_read(void) {
#if defined(__linux__)
    uint64_t v;
    ssize_t r = read(g_dns_wake_fd, &v, sizeof(v));
    (void)r;
#elif !defined(_WIN32)
    char c[64];
    while (read(g_dns_wake_fd, c, sizeof(c)) > 0) {}
#endif
}

/* Deliver every completed lookup to its waiting frame (reactor thread). */
static int dns_drain(void) {
    int cnt = 0, woke = 0;
    zan_blocking_job_t *j;
    dns_lock();
    j = g_blocking_done;
    g_blocking_done = NULL;
    for (zan_blocking_job_t *x = j; x; x = x->next) cnt++;
    g_blocking_inflight -= cnt;
    dns_unlock();
    while (j) {
        zan_blocking_job_t *n = j->next;
        if (j->out) *j->out = (int32_t)j->result;
        if (j->out64) *j->out64 = j->result;
        /* resolve_sa jobs staged their sockaddr in the work item; copy it
         * into the caller's sink here, on the reactor thread, before the
         * frame is woken. result > 0 implies len <= sacap: dns_job_run
         * resolved against the sink capacity. Timed-out jobs are discarded
         * by their worker and never appear on this list, so the sink --
         * an interior pointer into the destination frame -- is guaranteed
         * still valid here. The staging write happened-before this read via
         * the worker's dns_lock handoff in blocking_finish_worker. */
        if (j->sabuf && j->result > 0) {
            size_t len = (size_t)j->result;
            if (len <= (size_t)j->sacap)
                memcpy(j->sabuf, ((zan_dns_work_t *)j)->sa_staging, len);
        }
        io_wake(j->frame, j->step);
        free(j);
        woke++;
        j = n;
    }
    return woke;
}

/* Free completed jobs and mark pending jobs as timed-out so their workers
 * release them.  Called from zan_io_shutdown; workers may still be running,
 * so we must not free jobs out from under them. */
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
        /* Empty name cannot resolve: fail immediately, same value the worker
         * would have produced, so the caller never parks on a thread. */
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    zan_io_init();     /* ensure the wake fd / IOCP exists before spawning */
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
        /* Empty name cannot resolve: fail immediately, same value the worker
         * would have produced, so the caller never parks on a thread. */
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    zan_io_init();     /* ensure the wake fd / IOCP exists before spawning */
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

/* The compiler passes only scalar ABI values.  A NULL result sink identifies
 * a void declaration, avoiding a return-value mismatch for void FFI calls. */
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

/* ---- coroutine-facing ABI ================= */

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
#if defined(MSG_DONTWAIT)
    /* A keep-alive peer usually has its next request in the socket buffer
     * already, so try the recv before parking: the readiness round trip costs
     * an epoll_ctl re-arm, an epoll_wait wake and a liveness probe per request
     * for data that is there. MSG_DONTWAIT makes the attempt non-blocking
     * whatever the fd's own O_NONBLOCK state is, and the coroutine is readied
     * rather than resumed inline so the caller still returns to the scheduler
     * exactly as a parked await would. */
    if (fd >= 0 && buf && len > 0 && out_n && g_io_fast_budget > 0) {
        ssize_t rn = recv((int)fd, buf, (size_t)len, MSG_DONTWAIT);
        int again = (rn < 0 && (errno == EAGAIN || errno == EWOULDBLOCK
                               || errno == EINTR));
        if (!again) {
            g_io_fast_budget--;
            *out_n = (rn < 0) ? 0 : (int64_t)rn;
            io_wake(frame, step);
            return;
        }
    }
#endif
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
#if !defined(_WIN32)
    g_io_fast_budget = ZAN_IO_FAST_BURST;   /* a reactor turn refills the burst */
#endif
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
    /* An outstanding DNS lookup (or one parked waiting for the reactor) also
     * owes a wake, so the scheduler must keep polling instead of sleeping. */
    return (g_io_count > 0) || (g_blocking_inflight > 0);
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
    /* Synchronous poll/select with timeout.  This function currently has no
     * callers; when one is added it should ideally be reimplemented as a
     * non-blocking watcher + timer.  For now this at least honors the
     * timeout semantics instead of ignoring them.
     *   timeout_ms < 0  -> wait indefinitely until the fd becomes readable
     *   timeout_ms == 0 -> poll once and return immediately
     *   timeout_ms > 0  -> block up to timeout_ms for readability */
    if (timeout_ms < 0) {
        void *co = zan_io_get_current_co();
        if (!co) return -1;
        io_register(fd, ZAN_IO_READ, co, NULL);
        zan_io_suspend_current();
        return 1;
    }
#if defined(_WIN32)
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET((SOCKET)fd, &fds);
    struct timeval tv;
    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
    int r = select(0, &fds, NULL, NULL, &tv);
    if (r < 0) return -1;
    if (r == 0) return 0;
    return 1;
#else
    struct pollfd pfd;
    pfd.fd = (int)fd;
    pfd.events = POLLIN;
    int r = poll(&pfd, 1, (int)timeout_ms);
    if (r < 0) return -1;
    if (r == 0) return 0;
    return 1;
#endif
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
    if (!op) return -1;   /* like every other op site: fail the connect */
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
    /* A failed getsockopt means the fd died under the wait (closed via
     * Socket.Stop -- the stranded-watcher case): report connect failure, not
     * success with a stale err==0. */
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) != 0) return -1;
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
 * scales across cores. Each worker owns a private run queue and steals from
 * the others when it runs dry (see the Windows section below); the timer heap
 * remains shared and the IOCP itself is kernel-thread-safe. Worker count comes
 * from ZAN_CO_WORKERS, defaulting to the logical processor count.
 * ====================================================================== */
#ifdef ZAN_CO_DRIVER

typedef struct zan_co_node {
    struct zan_co_node *next;
    void               *frame;
    zan_co_step_t       step;
} zan_co_node;

static zan_co_node *g_rq_head, *g_rq_tail;

#if defined(_WIN32)
/* ---------------- Windows: multi-worker pool over IOCP ------------------
 * Scheduler shape (the Go/Tokio arrangement):
 *
 *   - Every worker owns a bounded FIFO run queue plus a one-slot LIFO cell.
 *     A ready frame is pushed by the thread that readied it, so a coroutine
 *     woken by an IO completion is resumed on the core that reaped the
 *     completion -- its frame is already in that core's cache.
 *   - The LIFO cell holds the most recently readied frame and is run next,
 *     ahead of the queue, with a budget (ZAN_LIFO_BUDGET) so a pair of
 *     coroutines that keep waking each other cannot monopolise a worker.
 *   - A worker with nothing left steals half of a random victim's queue.
 *     At most half the pool searches at once (g_co_searching) -- past that,
 *     searchers mostly contend on each other's queue heads.
 *   - Readies from non-worker threads and local-queue overflow go to a
 *     shared injector queue, which every worker polls every 61st schedule
 *     so injected work cannot starve behind a busy worker's LIFO chain.
 *   - Parking is coordinated by g_co_searching / g_co_parked: a burst of
 *     readies costs at most one wake packet per parked worker instead of one
 *     per frame, and a worker publishes itself as parked *before* its last
 *     queue scan, so a producer either is seen by that scan or sees the
 *     parked worker and posts a packet (no lost wakeup).
 *
 * Per-frame scheduler state lives in the first two frame-header slots
 * (ASYNC_FRAME_SCHED / ASYNC_FRAME_SCHED_STEP, see irgen_expr_core.c):
 * duplicate-ready suppression, the "already running on another worker"
 * guard and the deferred-free handshake are all CAS on that one word. The
 * previous design needed a per-shard lock, an open-addressed hash set of
 * queued frames and a linear scan of the frames a shard was running.
 * ----------------------------------------------------------------------- */

#define ZAN_CO_MAXW      64
#define ZAN_LQ_CAP       256u        /* per-worker ring slots, power of two */
#define ZAN_LQ_MASK      (ZAN_LQ_CAP - 1u)
#define ZAN_LIFO_BUDGET  3           /* consecutive LIFO-cell resumes */
#define ZAN_GLOBAL_TICK  61          /* injector fairness poll (Go's number) */
#define ZAN_STEAL_ROUNDS 2           /* randomized victim scans per search */
#define ZAN_WAKE_KEY     ((ULONG_PTR)-3)  /* scheduler wake packet (DNS is -2) */

/* Scheduler-owned prefix of every async frame. Mirrors the LLVM struct
 * { i64, void(i8*)* } the compiler emits at frame indices 0 and 1. */
typedef struct {
    volatile long long sched;    /* CO_* bits */
    zan_co_step_t      pending;  /* resume step banked by a notify */
} zan_co_fhdr;

#define CO_QUEUED    1LL   /* a task naming this frame sits in a run queue */
#define CO_RUNNING   2LL   /* a worker is inside step(frame) right now */
#define CO_NOTIFIED  4LL   /* readied while running: re-queue after the step */
#define CO_DEAD      8LL   /* released; whoever holds it does the free */

typedef struct { void *frame; zan_co_step_t step; } zan_co_task;

typedef struct {
    unsigned long long ran;         /* frames stepped */
    unsigned long long lifo_put;    /* readied into the run-next cell */
    unsigned long long lifo_hit;    /* resumed straight from that cell */
    unsigned long long lifo_demote; /* cell pushed to the queue, budget spent */
    unsigned long long lq_push;
    unsigned long long lq_pop;
    unsigned long long spill;       /* queue overflow halves moved to injector */
    unsigned long long inj_push;
    unsigned long long inj_pop;
    unsigned long long steal_ok;
    unsigned long long steal_fail;  /* searches that found nothing */
    unsigned long long park;        /* blocking waits on the port */
    unsigned long long wake_post;   /* wake packets this worker posted */
} zan_co_stats_t;

typedef struct {
    /* Ring: consumers (the owner and thieves) move `head` with CAS, only the
     * owner moves `tail`. A slot is never overwritten while a consumer may
     * still read it: the owner refuses to push past head + ZAN_LQ_CAP. */
    volatile long long head;
    volatile long long tail;
    zan_co_task        buf[ZAN_LQ_CAP];
    zan_co_task        lifo;
    int                lifo_full;
    int                lifo_budget;
    int                index;
    int                searching;
    /* Set while this worker blocks on its shard, so a producer can aim the wake
     * packet at a port that actually has a waiter. */
    volatile LONG      parked;
    unsigned           tick;
    unsigned           rng;
    /* Scheduling counters, written only by the owning worker (plain adds, no
     * atomics) and read once at shutdown. They exist so a scheduler change can
     * be attributed: throughput alone cannot say whether a worker ran its own
     * LIFO cell, drained its queue, stole, or spun searching. Printed when
     * ZAN_CO_STATS is set in the environment. */
    zan_co_stats_t     st;
    char               pad[64];      /* keep neighbours off this cache line */
} zan_co_worker_t;

static CRITICAL_SECTION g_co_lock;
static volatile LONG    g_co_running;    /* workers inside a step or a pump */
static volatile LONG    g_co_parked;     /* workers blocked on the port */
static volatile LONG    g_co_searching;  /* workers hunting for work */
static volatile LONG    g_co_wake;       /* posted, unconsumed wake packets */
static volatile LONG    g_co_stop;
static int              g_co_inited;
static int              g_co_workers;
static DWORD            g_co_tls = TLS_OUT_OF_INDEXES;
static zan_co_worker_t  g_wk[ZAN_CO_MAXW];

/* Shared injector queue: readies from threads that are not workers (program
 * start-up, DNS workers) and local-queue overflow. */
static CRITICAL_SECTION g_inj_lock;
static zan_co_node     *g_inj_head, *g_inj_tail, *g_inj_free;
static volatile LONG    g_inj_len;
/* Readies that arrived from a thread outside the pool (no worker to charge). */
static volatile LONG    g_inj_push_ext;

static long long co_now_ms(void) { return (long long)GetTickCount64(); }

/* Timer pumping state. The timer heap sits behind one global lock, so having
 * every worker dispatch due timers per loop iteration turned that lock into
 * the pool's hottest contention point (16 workers burned 20x the
 * single-threaded driver's CPU for the same 2M messages). One worker at a
 * time owns the pump, at most once per millisecond for the whole pool;
 * workers that lose the race reuse the cached deadline instead of queueing on
 * the lock. */
static volatile LONG      g_timer_owner;
static volatile long long g_timer_pumped_ms;
static volatile long long g_timer_next_ms;

/* Bumped whenever work is created (a frame is queued or a timer armed). The
 * termination check samples it around a short re-check window: nothing may be
 * declared quiescent if any work appeared while we were looking. */
static volatile LONG      g_co_activity;

static void co_wake_shard(int shard) {
    HANDLE p = io_shard(shard);
    if (p) PostQueuedCompletionStatus(p, 0, ZAN_WAKE_KEY, NULL);
}

/* Shard a worker waits on. Workers and shards are 1:1 unless ZAN_IO_SHARDS
 * lowered the count, in which case several workers share one port. */
static int co_worker_shard(int worker) {
    int n = (int)g_shards;
    return (n <= 1) ? 0 : (worker % n);
}

/* Wake one parked worker -- but only when nobody is searching already (that
 * worker will find the task) and only up to one unconsumed packet per parked
 * worker. Posting one packet per ready frame is what made --async-workers
 * burn CPU linearly in the worker count at flat throughput. */
static zan_co_worker_t *co_self(void);

static void co_notify(void) {
    if (g_co_searching > 0) return;
    if (g_co_parked <= 0) return;
    if (InterlockedIncrement(&g_co_wake) > g_co_parked) {
        InterlockedDecrement(&g_co_wake);
        return;
    }
    zan_co_worker_t *self = co_self();
    if (self) self->st.wake_post++;
    /* Aim the packet at a worker that is actually parked. With one port per
     * worker a packet posted to the wrong shard wakes nobody, so the wake
     * budget above would be spent without anyone picking the frame up. */
    for (int i = 0; i < g_co_workers; i++) {
        if (g_wk[i].parked) { co_wake_shard(co_worker_shard(i)); return; }
    }
    co_wake_shard(0);
}

static zan_co_worker_t *co_self(void) {
    if (g_co_tls == TLS_OUT_OF_INDEXES) return NULL;
    return (zan_co_worker_t *)TlsGetValue(g_co_tls);
}

static unsigned co_rand(zan_co_worker_t *w) {
    unsigned x = w->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    w->rng = x ? x : 0x9e3779b9u;
    return w->rng;
}

/* ---- injector ---- */

static void inj_push(zan_co_task t) {
    EnterCriticalSection(&g_inj_lock);
    zan_co_node *n = g_inj_free;
    if (n) g_inj_free = n->next;
    else {
        n = (zan_co_node *)malloc(sizeof(*n));
        if (!n) abort();
    }
    n->next = NULL; n->frame = t.frame; n->step = t.step;
    if (g_inj_tail) g_inj_tail->next = n; else g_inj_head = n;
    g_inj_tail = n;
    InterlockedIncrement(&g_inj_len);
    LeaveCriticalSection(&g_inj_lock);
}

static int inj_pop(zan_co_task *out) {
    if (g_inj_len <= 0) return 0;          /* unlocked hint */
    EnterCriticalSection(&g_inj_lock);
    zan_co_node *n = g_inj_head;
    if (n) {
        g_inj_head = n->next;
        if (!g_inj_head) g_inj_tail = NULL;
        out->frame = n->frame; out->step = n->step;
        n->next = g_inj_free; g_inj_free = n;
        InterlockedDecrement(&g_inj_len);
    }
    LeaveCriticalSection(&g_inj_lock);
    return n != NULL;
}

/* ---- per-worker ring ---- */

static void lq_push(zan_co_worker_t *w, zan_co_task t);

/* Move half of the owner's queue to the injector, so an overflowing producer
 * never blocks and the backlog stays visible to every worker. */
static void lq_spill(zan_co_worker_t *w) {
    zan_co_task tmp[ZAN_LQ_CAP / 2];
    for (;;) {
        long long h = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&w->tail, __ATOMIC_RELAXED);
        long long n = (t - h) / 2;
        if (n <= 0) return;
        if (n > (long long)(ZAN_LQ_CAP / 2)) n = (long long)(ZAN_LQ_CAP / 2);
        for (long long i = 0; i < n; i++)
            tmp[i] = w->buf[(unsigned long long)(h + i) & ZAN_LQ_MASK];
        if (!__atomic_compare_exchange_n(&w->head, &h, h + n, 0,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;
        for (long long i = 0; i < n; i++) inj_push(tmp[i]);
        w->st.spill++;
        w->st.inj_push += (unsigned long long)n;
        return;
    }
}

static void lq_push(zan_co_worker_t *w, zan_co_task t) {
    long long tail = __atomic_load_n(&w->tail, __ATOMIC_RELAXED);
    long long head = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
    if (tail - head >= (long long)ZAN_LQ_CAP) {
        lq_spill(w);
        head = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        if (tail - head >= (long long)ZAN_LQ_CAP) {
            inj_push(t);
            w->st.inj_push++;
            return;
        }
    }
    w->buf[(unsigned long long)tail & ZAN_LQ_MASK] = t;
    __atomic_store_n(&w->tail, tail + 1, __ATOMIC_RELEASE);
    w->st.lq_push++;
}

static int lq_pop(zan_co_worker_t *w, zan_co_task *out) {
    for (;;) {
        long long h = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&w->tail, __ATOMIC_ACQUIRE);
        if (h >= t) return 0;
        zan_co_task task = w->buf[(unsigned long long)h & ZAN_LQ_MASK];
        if (__atomic_compare_exchange_n(&w->head, &h, h + 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            *out = task;
            return 1;
        }
    }
}

/* Take half of `v`'s queue: one task is returned to run now, the rest goes
 * into the thief's own queue (so one steal feeds several schedules and the
 * victim's head is touched once instead of per task). */
static int lq_steal(zan_co_worker_t *v, zan_co_worker_t *w, zan_co_task *out) {
    zan_co_task tmp[ZAN_LQ_CAP / 2];
    for (;;) {
        long long h = __atomic_load_n(&v->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&v->tail, __ATOMIC_ACQUIRE);
        long long n = t - h;
        if (n <= 0) return 0;
        n = n - n / 2;                                   /* half, rounded up */
        if (n > (long long)(ZAN_LQ_CAP / 2)) n = (long long)(ZAN_LQ_CAP / 2);
        for (long long i = 0; i < n; i++)
            tmp[i] = v->buf[(unsigned long long)(h + i) & ZAN_LQ_MASK];
        if (!__atomic_compare_exchange_n(&v->head, &h, h + n, 0,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;
        *out = tmp[0];
        for (long long i = 1; i < n; i++) lq_push(w, tmp[i]);
        return 1;
    }
}

/* ---- frame state ---- */

static zan_co_fhdr *co_hdr(void *frame) { return (zan_co_fhdr *)frame; }

static void co_submit(void *frame, zan_co_step_t step) {
    zan_co_task t;
    t.frame = frame; t.step = step;
    InterlockedIncrement(&g_co_activity);
    zan_co_worker_t *w = co_self();
    if (!w) {
        inj_push(t);
        InterlockedIncrement(&g_inj_push_ext);
        co_notify();
        return;
    }
    if (!w->lifo_full) {
        /* Run-next cell: the frame a worker just readied is almost always the
         * continuation of the work it is already doing (the IO completion of
         * the request it is serving), so keep it on this core. */
        w->lifo = t;
        w->lifo_full = 1;
        w->st.lifo_put++;
        return;
    }
    lq_push(w, t);
    co_notify();
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step || !frame) return;
    zan_co_fhdr *h = co_hdr(frame);
    for (;;) {
        long long s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
        if (s & CO_DEAD) return;                 /* frame is being released */
        /* QUEUED dedup: dropping a second ready while one task is already in
         * flight is safe because a frame has exactly ONE step. The compiler
         * passes the frame's unique $resume (state dispatch happens inside
         * it) at every zan_co_ready site; the timer and gate waiters store
         * the (frame, step) pair handed to them, which is that same $resume.
         * Wakeups issued on behalf of ANOTHER frame carry that other frame's
         * handle, so they never collide here. A reaper/complete wakeup that
         * targets a running frame lands in the CO_RUNNING branch below and
         * banks the step instead. There is therefore never a second, different
         * step to lose -- only a redundant wake of the same one. */
        if (s & CO_QUEUED) return;
        if (s & CO_RUNNING) {
            /* Readied while it runs (an IO completion racing the step that
             * issued the next op). Bank the resume step; the worker running
             * it re-queues the frame when the step returns. */
            __atomic_store_n(&h->pending, step, __ATOMIC_RELAXED);
            if (__atomic_compare_exchange_n(&h->sched, &s, s | CO_NOTIFIED, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
            continue;
        }
        if (__atomic_compare_exchange_n(&h->sched, &s, s | CO_QUEUED, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    co_submit(frame, step);
}

/* Release an async frame. Called by the compiler (--async-workers) instead of
 * free() wherever an awaiter, a reaper or the frame's own cleanup drops a
 * frame, because the scheduler may still hold a reference to it:
 *   - running: the worker frees it when the step returns (it must still read
 *     the state word afterwards, and the awaiter that completed can run
 *     concurrently on another core);
 *   - queued: whoever pops the stale task frees it and skips the step.
 * Anything else is unreferenced and freed right here. */
void __zan_co_frame_free(void *frame) {
    if (!frame) return;
    zan_co_fhdr *h = co_hdr(frame);
    for (;;) {
        long long s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
        if (s & CO_DEAD) return;                 /* already handed over */
        if (s & (CO_QUEUED | CO_RUNNING)) {
            if (__atomic_compare_exchange_n(&h->sched, &s, s | CO_DEAD, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
            continue;
        }
        if (__atomic_compare_exchange_n(&h->sched, &s, CO_DEAD, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    free(frame);
}

void zan_co_delay(long long ms, void *frame, zan_co_step_t step) {
    zan_timer_delay(ms, frame, step);
    InterlockedIncrement(&g_co_activity);
    co_notify();
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
    if (w > ZAN_CO_MAXW) w = ZAN_CO_MAXW;
    return w;
}

void zan_co_sched_init(void) {
    if (!g_co_inited) {
        InitializeCriticalSection(&g_co_lock);
        InitializeCriticalSection(&g_inj_lock);
        g_co_tls = TlsAlloc();
        g_co_inited = 1;
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
    g_timer_owner = 0;
    g_timer_pumped_ms = 0;
    g_timer_next_ms = -1;
    g_co_activity = 0;
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
    }
}

/* ---- work discovery ---- */

static int co_search_begin(zan_co_worker_t *w) {
    if (w->searching) return 1;
    LONG s = g_co_searching;
    if (2 * s >= g_co_workers) return 0;   /* enough hunters already */
    InterlockedIncrement(&g_co_searching);
    w->searching = 1;
    return 1;
}

static void co_search_end(zan_co_worker_t *w, int found) {
    if (!w->searching) return;
    w->searching = 0;
    /* The last searcher leaving with work in hand hands the search on: the
     * queues it did not reach may still hold tasks nobody is looking for. */
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
    /* Fairness poll: a worker fed by its own LIFO cell would never look at the
     * injector, starving frames readied by non-worker threads. */
    if (++w->tick % ZAN_GLOBAL_TICK == 0 && inj_pop(out)) {
        w->lifo_budget = ZAN_LIFO_BUDGET;
        w->st.inj_pop++;
        return 1;
    }
    if (w->lifo_full) {
        if (w->lifo_budget > 0) {
            *out = w->lifo;
            w->lifo_full = 0;
            w->lifo_budget--;
            w->st.lifo_hit++;
            return 1;
        }
        /* Budget spent: demote the cell into the queue, where it is stealable
         * and takes its turn behind the frames waiting there. */
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

/* Is there a task somewhere for a worker about to park? Called after the
 * worker published itself as parked, so a producer that pushes after this
 * scan is guaranteed to see g_co_parked > 0 and post a wake packet. */
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

/* Run one task. The frame is claimed (CO_QUEUED -> CO_RUNNING) before the
 * step and released afterwards; a concurrent free saw CO_RUNNING and only set
 * CO_DEAD, so the state word is still ours to read when the step returns. */
static void co_run(zan_co_worker_t *w, zan_co_task *t) {
    zan_co_fhdr *h = co_hdr(t->frame);
    long long s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
    for (;;) {
        if (s & CO_DEAD) {
            /* Released while it sat in a queue: drop the stale task. */
            free(t->frame);
            return;
        }
        long long n = (s & ~(CO_QUEUED | CO_NOTIFIED)) | CO_RUNNING;
        if (__atomic_compare_exchange_n(&h->sched, &s, n, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    InterlockedIncrement(&g_co_running);
    w->st.ran++;
    t->step(t->frame);
    for (;;) {
        s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
        if (s & CO_DEAD) { free(t->frame); break; }
        if (s & CO_NOTIFIED) {
            zan_co_step_t step = __atomic_load_n(&h->pending, __ATOMIC_RELAXED);
            if (!__atomic_compare_exchange_n(&h->sched, &s, CO_QUEUED, 0,
                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                continue;
            co_submit(t->frame, step ? step : t->step);
            break;
        }
        if (__atomic_compare_exchange_n(&h->sched, &s, 0LL, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    InterlockedDecrement(&g_co_running);
}

/* Dispatch due Delay and public timers; return ms until the next deadline.
 * zan_timer_dispatch_due is serialized to one worker pool-wide via the
 * g_timer_owner gate taken here -- the gate MUST cover every caller: the
 * timer's g_dispatching slot holds exactly one in-flight callback, so a
 * second concurrent dispatcher makes clear-from-callback miss the running
 * entry and a cancelled tick timer fires again. A caller that cannot become
 * the owner still needs a deadline to park on, so it reports the live heap
 * head without dispatching; the owner refreshes g_timer_next_ms when it
 * finishes. */
static long long co_pump_timers_now(void) {
    if (InterlockedCompareExchange(&g_timer_owner, 1, 0) != 0) {
        long long live = zan_timer_next_timeout();
        if (live > 0) return live;
        long long cached = g_timer_next_ms;
        return (cached <= 0) ? 1 : cached;
    }
    /* Count the pump as running work. Dispatching a Delay pops its entry from
     * the timer heap before the ready hook queues the coroutine, and in that
     * window the pool has no timer pending, no queued frame and no running
     * step -- another worker reaching co_all_idle right then would declare the
     * whole pool finished and stop a live program. */
    InterlockedIncrement(&g_co_running);
    zan_timer_dispatch_due();
    long long next = zan_timer_next_timeout();
    g_timer_next_ms = next;
    g_timer_pumped_ms = co_now_ms();
    InterlockedDecrement(&g_co_running);
    InterlockedExchange(&g_timer_owner, 0);
    return next;
}

static long long co_pump_timers(void) {
    long long now = co_now_ms();
    /* Throttled pool-wide to ~1ms: while the pump ran recently every caller
     * reuses the cached deadline so the timer lock stays cold on busy loops. */
    if (now - g_timer_pumped_ms >= 1)
        return co_pump_timers_now();
    long long cached = g_timer_next_ms;
    return (cached == 0) ? 1 : cached;
}

/* Block on the completion port, re-readying coroutines whose IO completed.
 * A NULL overlapped is a notification packet: the scheduler's wake (key
 * ZAN_WAKE_KEY) or a DNS completion. */
static void co_wait_io(zan_co_worker_t *w, long long timeout_ms) {
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    DWORD to = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    /* Own shard only: every socket this worker's connections use is bound to
     * it, so their completions arrive here and nowhere else. */
    BOOL ok = GetQueuedCompletionStatusEx(io_shard(co_worker_shard(w->index)),
                                          entries, 64, &removed, to, FALSE);
    /* No longer parked. Dropping this before re-readying the completed
     * coroutines below lets those zan_co_ready calls skip the wake syscall
     * when this worker is the only idle one -- it drains the whole batch
     * itself on its next loop iterations. */
    InterlockedExchange(&w->parked, 0);
    InterlockedDecrement(&g_co_parked);
    if (!ok) {
        /* Timeout or error: deliver DNS timeouts so lookups past their
         * deadline fail instead of parking their coroutines forever. */
        dns_timeout_scan();
        return;
    }
    for (ULONG i = 0; i < removed; i++) {
        if (entries[i].lpOverlapped == NULL) {
            if (entries[i].lpCompletionKey == ZAN_WAKE_KEY) {
                /* Our own nudge: consuming it lets the next producer post a
                 * fresh one. */
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
    /* Cheap unlocked reject first: this runs on the empty-queue path of every
     * worker, and a live server almost always has IO in flight or a timer
     * pending. */
    if (g_co_running != 0 || g_io_count != 0 || g_blocking_inflight != 0 ||
        zan_timer_pending() != 0)
        return 0;
    int idle;
    EnterCriticalSection(&g_co_lock);
    idle = !(zan_timer_pending() != 0 || g_co_running != 0 ||
             g_io_count != 0 || g_blocking_inflight != 0 || co_has_runnable());
    LeaveCriticalSection(&g_co_lock);
    return idle;
}

static void co_worker(int worker) {
    zan_co_worker_t *w = &g_wk[worker];
    if (g_co_tls != TLS_OUT_OF_INDEXES) TlsSetValue(g_co_tls, w);
    /* Quiescence is confirmed, not assumed: see the co_all_idle call below. */
    LONG idle_seen = -1;
    for (;;) {
        if (g_co_stop) return;
        /* Pump due timers even while the run queues stay busy. Otherwise, at
         * high request rates co_next_task always succeeds first and the pump
         * (only reached when the queues drain) never runs, starving
         * timer-driven work -- notably the accept-readiness poll that keeps
         * the listen loop alive -- so new connections stop being accepted.
         * co_pump_timers throttles itself pool-wide and the pending count is
         * an unlocked read, so this costs nothing without timer work. */
        if (zan_timer_pending() > 0) co_pump_timers();
        zan_co_task t;
        if (co_next_task(w, &t)) {
            co_search_end(w, 1);
            idle_seen = -1;
            co_run(w, &t);
            continue;
        }
        /* About to park: pump for real rather than reusing a cached deadline.
         * The throttled variant above exists to keep the timer lock cold while
         * the pool is busy; here the deadline decides how long this worker
         * blocks, and a stale one turns every timer-driven step into a
         * multi-millisecond stall. */
        long long tnext = co_pump_timers_now();
        if (co_next_task(w, &t)) {
            co_search_end(w, 1);
            idle_seen = -1;
            co_run(w, &t);
            continue;
        }
        co_search_end(w, 0);
        /* Terminate only on quiescence observed twice with no activity in
         * between. A single observation is not proof: dispatching a timer or
         * completing an IO passes through a window where the work is no longer
         * in the timer heap / in-flight count and not yet in a run queue, and
         * a worker sampling exactly then would stop a live program. Requiring
         * an unchanged activity counter across a 1ms re-check closes that
         * window -- any queued frame or armed timer bumps the counter. */
        if (co_all_idle()) {
            LONG seq = g_co_activity;
            if (idle_seen == seq) {
                g_co_stop = 1;
                for (int i = 0; i < g_co_workers; i++)
                    co_wake_shard(co_worker_shard(i));
                return;
            }
            idle_seen = seq;
            Sleep(1);
            continue;
        }
        idle_seen = -1;
        /* Choose how long to block. A pending timer bounds it tightest; else
         * with IO in flight block for real completions (with a 1s ceiling as a
         * missed-wake safety net); else nothing is pending, so use a short cap
         * that keeps termination detection responsive. An in-flight DNS lookup
         * caps the wait at its deadline so it cannot hang the pool. */
        long long to;
        if (tnext >= 0)               to = tnext;
        else if (g_io_count > 0)      to = 1000;
        else                          to = 50;
        if (g_blocking_inflight > 0) {
            long long dw = dns_wait_ms(-1);
            if (dw >= 0 && dw < to) to = dw;
        }
        /* Publish the parked state before the final scan. A producer that
         * pushes first is found by the scan; one that pushes afterward
         * observes g_co_parked > 0 and posts a wake packet. */
        InterlockedIncrement(&g_co_parked);
        InterlockedExchange(&w->parked, 1);
        w->st.park++;
        /* Re-sample the timer deadline now that this worker counts as parked.
         * An armed timer is not a queued frame, so co_has_runnable cannot see
         * it: a timer armed between the pump above and this point would
         * otherwise leave the worker parked for the full fallback timeout
         * (Task.Delay(2) taking tens of milliseconds instead of two). */
        long long tnow = zan_timer_next_timeout();
        if (tnow >= 0 && tnow < to) to = tnow;
        if (tnow == 0 || co_has_runnable()) {
            InterlockedExchange(&w->parked, 0);
            InterlockedDecrement(&g_co_parked);
            continue;
        }
        co_wait_io(w, to);         /* clears parked state on return */
    }
}

/* Dump the per-worker counters when ZAN_CO_STATS is set, one line per worker
 * plus a total, on stderr so a benchmark's stdout stays machine-readable. The
 * lines are `key=value` pairs for the same reason. */
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
                "park=%llu wake_post=%llu\n",
                i, s->ran, s->lifo_put, s->lifo_hit, s->lifo_demote,
                s->lq_push, s->lq_pop, s->spill, s->inj_push, s->inj_pop,
                s->steal_ok, s->steal_fail, s->park, s->wake_post);
        tot.ran += s->ran;                 tot.lifo_put += s->lifo_put;
        tot.lifo_hit += s->lifo_hit;       tot.lifo_demote += s->lifo_demote;
        tot.lq_push += s->lq_push;         tot.lq_pop += s->lq_pop;
        tot.spill += s->spill;             tot.inj_push += s->inj_push;
        tot.inj_pop += s->inj_pop;         tot.steal_ok += s->steal_ok;
        tot.steal_fail += s->steal_fail;   tot.park += s->park;
        tot.wake_post += s->wake_post;
    }
    fprintf(stderr,
            "COSTAT total workers=%d shards=%d ran=%llu lifo_put=%llu lifo_hit=%llu "
            "lifo_demote=%llu lq_push=%llu lq_pop=%llu spill=%llu "
            "inj_push=%llu inj_pop=%llu steal_ok=%llu steal_fail=%llu "
            "park=%llu wake_post=%llu inj_push_ext=%ld sync_inline=%ld\n",
            g_co_workers, (int)g_shards, tot.ran, tot.lifo_put, tot.lifo_hit,
            tot.lifo_demote, tot.lq_push, tot.lq_pop, tot.spill,
            tot.inj_push, tot.inj_pop, tot.steal_ok, tot.steal_fail,
            tot.park, tot.wake_post, (long)g_inj_push_ext, (long)g_sync_inline);
    fflush(stderr);
}

static DWORD WINAPI co_worker_thunk(LPVOID p) {
    co_worker((int)(uintptr_t)p);
    return 0;
}

void zan_co_sched_run(void) {
    zan_io_init();   /* ensure the port exists before workers block on it */
    int w = g_co_workers;
    g_co_stop = 0;
    /* Shard the reactor before any worker starts: a shard with no waiter would
     * hold completions nobody dequeues. */
    io_shards_start(w);

    HANDLE th[ZAN_CO_MAXW]; int nt = 0;
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
    co_stats_dump();
    zan_io_shutdown();
}

/* The multi-worker driver owns the whole thread pool for the duration of a run,
 * so it has no way to hand control back mid-pool: awaiting one frame from a
 * synchronous context drains, as it always has. */
void zan_co_sched_run_until(const volatile int *done) {
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
/* ---------------- Non-Windows: single-threaded fallback ---------------- */

void zan_co_sched_init(void) {
    g_rq_head = g_rq_tail = NULL;
    zan_timer_runtime_reset();
    zan_timer_set_ready_hook(zan_co_ready);
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step) return;
    zan_co_node *n = (zan_co_node *)malloc(sizeof(*n));
    if (!n) {
        /* Out of memory: stepping inline keeps the frame's release contract
         * intact. This driver is single-threaded, so the only new hazard is
         * re-entrancy through the step -- strictly better than dropping the
         * wake and parking the coroutine forever. */
        step(frame);
        return;
    }
    n->next = NULL; n->frame = frame; n->step = step;
    if (g_rq_tail) g_rq_tail->next = n; else g_rq_head = n;
    g_rq_tail = n;
}

void zan_co_delay(long long ms, void *frame, zan_co_step_t step) {
    zan_timer_delay(ms, frame, step);
}

/* Frame release hook the compiler emits under --async-workers. This driver is
 * single-threaded, so nothing can hold a reference the program does not know
 * about and the free needs no handshake. */
void __zan_co_frame_free(void *frame) { free(frame); }

size_t zan_co_pending(void) {
    size_t n = 0;
    for (zan_co_node *p = g_rq_head; p; p = p->next) n++;
    return n;
}

void zan_co_sched_run_until(const volatile int *done) {
    for (;;) {
        while (g_rq_head) {
            if (done && *done) return;
            zan_co_node *n = g_rq_head;
            g_rq_head = n->next; if (!g_rq_head) g_rq_tail = NULL;
            void *frame = n->frame; zan_co_step_t step = n->step;
            free(n);
            step(frame);
        }
        if (done && *done) return;
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
