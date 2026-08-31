/* rt_io.h -- Zan async-IO event loop for coroutine integration.
 *
 * Provides a thin abstraction over platform IO multiplexing:
 *   - Linux:   epoll
 *   - macOS:   kqueue
 *   - Windows: IOCP
 *   - Fallback: select
 *
 * The scheduler calls zan_io_poll() in its main loop to wake coroutines
 * whose file descriptors became readable or writable.
 *
 * Coroutines call zan_io_wait_readable / zan_io_wait_writable to suspend
 * until their fd is ready.  These functions are the async-IO ABI that the
 * Zan compiler (or stdlib DllImport) can call.
 */
#ifndef ZAN_RT_IO_H
#define ZAN_RT_IO_H

#include <stdint.h>
#include "rt_co.h"   /* zan_co_step_t (stackless bridge) */

/* Readiness interest flags accepted by zan_io_wait_co(). */
#define ZAN_IO_READ  1
#define ZAN_IO_WRITE 2

/* ---- lifecycle ---- */
void zan_io_init(void);
void zan_io_shutdown(void);
void zan_io_socket_cleanup(void);
/* `fd` is a socket handle, so it is pointer-width (`intptr_t`): a Windows
 * SOCKET is a UINT_PTR and only a POSIX fd fits in 32 bits. Byte counts are
 * `int64_t`; flags, status codes and 0/1 results are `int32_t` -- none of
 * these may be spelled `int`/`long`, whose width differs across our targets. A
 * status that fits in 32 bits stays 32 bits: widening it would force every
 * Zan caller to spell an errno/SO_ERROR code or a boolean `long`. */
int64_t zan_io_socket_send(intptr_t fd, const void *buf, int64_t len,
                           int32_t flags);
int64_t zan_io_socket_recv(intptr_t fd, void *buf, int64_t len,
                           int32_t flags);
int32_t zan_io_socket_ready(intptr_t fd, int32_t write_ready);

/* Probe the outcome of a non-blocking connect on `fd`.
 * Returns 0 once connected, a positive SO_ERROR code (or -1 when no code is
 * available) once the connect has failed, and -2 while still in progress. */
int32_t zan_io_connect_status(intptr_t fd);

/* Whether `fd` still refers to an open socket. Non-zero when it does.
 * A readiness-retry loop (accept, in particular) needs this: once its socket
 * has been closed under it -- a listener taken down by Stop() while a coroutine
 * was parked in accept -- the fd can never become ready again, and the loop
 * must report the failure instead of re-arming the watcher forever. */
int32_t zan_io_socket_alive(intptr_t fd);
int64_t zan_io_socket_peer_ipv4(intptr_t fd);

/* Resolve a hostname to an IPv4 address in the same byte order as inet_addr.
 * Returns 0 when the hostname cannot be resolved. */
int32_t zan_io_resolve_ipv4(const char *hostname);

/* Resolve `name` (hostname or literal IPv4/IPv6) to a complete sockaddr --
 * IPv4 or IPv6, whatever the resolver returns first -- and copy it into
 * `buf` (which must hold at least 28 bytes). Returns the sockaddr length
 * (16 or 28), or 0 on failure. Keeps the sockaddr layout out of the Zan
 * standard library. */
int32_t zan_io_resolve_sa(const char *name, int32_t port, void *buf,
                          int32_t cap);

/* Resolve every IPv4/IPv6 candidate into caller-owned fixed-size records.
 * Each record occupies ZAN_IO_SA_STRIDE bytes, starts with the native
 * sockaddr (16 bytes for AF_INET or 28 bytes for AF_INET6), and is zero-filled
 * through the rest of the record. The return value is the number of records
 * written; malformed arguments, resolver failure, or a buffer too small for
 * one record return 0. No pointers from getaddrinfo escape this call. */
#define ZAN_IO_SA_STRIDE 32
int32_t zan_io_resolve_all(const char *name, int32_t port, void *buf,
                           int32_t cap);
/* Same operation with a scalar pointer ABI, intended for await native externs:
 * the caller keeps name_ptr and buf_ptr valid until the await resumes. */
int64_t zan_io_resolve_all_async(intptr_t name_ptr, int32_t port,
                                 intptr_t buf_ptr, int32_t cap);

/* Return AF_INET / AF_INET6 for a binary sockaddr, or 0 for an invalid or
 * unsupported address. This keeps family classification out of Zan code. */
int32_t zan_io_sockaddr_family(const void *sa, int32_t len);

/* Start an exact sockaddr connect without resolving or rewriting the address.
 * Returns 0 when connected, -2 while in progress, or a positive platform
 * error code (-1 when no code is available). The socket is made nonblocking. */
int32_t zan_io_connect_sa_start(intptr_t fd, const void *sa, int32_t salen);
/* Worker-safe async ABI: exact sockaddr connect, returning 0 on success or
 * the platform error code. This function never performs DNS. */
int64_t zan_io_connect_sa(intptr_t fd, const void *sa, int32_t salen);

/* Format the address part of a sockaddr (IPv4 or IPv6) as a dotted-quad /
 * colon-hex string. Returns a static buffer (INET6_ADDRSTRLEN), or "" when
 * the family is neither AF_INET nor AF_INET6. */
const char *zan_io_sockaddr_ip_str(const void *sa);

/* Async sockaddr resolution: like zan_io_resolve_co, but stores a complete
 * sockaddr (16 or 28 bytes) into `buf` and writes its length into `*out`
 * (0 on failure / timeout). `buf` must stay valid until the frame resumes;
 * the caller keeps it reachable across the await. */
void zan_io_resolve_sa_co(const char *name, int32_t port, void *buf,
                          int32_t cap, void *frame, zan_co_step_t step,
                          int32_t *out);

#if defined(_WIN32)
/* Read the encoded DER pointer/length from a Windows PCCERT_CONTEXT.
 * Keeps the CERT_CONTEXT layout out of the Zan standard library. */
const unsigned char *zan_crypto_cert_encoded(const void *cert, int *out_len);
#endif

/* ---- stackless (CPS state-machine) ABI ----
 *
 * The compiler's async lowering (see docs/ASYNC_CPS_DESIGN.md) has no fiber to
 * park: an `await` on IO records its resume point in the heap frame and returns
 * to the scheduler. So instead of suspending the caller inline, register a
 * one-shot watcher that, when `fd` becomes ready for `interest`
 * (ZAN_IO_READ / ZAN_IO_WRITE), calls `zan_co_ready(frame, step)` to re-enter
 * the state machine. Returns immediately (does not block or suspend). */
void zan_io_wait_co(intptr_t fd, int32_t interest, void *frame, zan_co_step_t step);

/* Overlapped receive: post a real recv of up to `len` bytes into `buf` and
 * suspend `frame` until it completes, then re-enter via `step`. The number of
 * bytes received (0 on peer close) is stored into `*out_n` before the frame is
 * re-readied, so the resumed state machine can read it as the await value.
 *
 * Unlike the zero-byte readiness probe used by zan_io_wait_co (which needs a
 * separate synchronous recv after waking), this issues the receive itself as a
 * single overlapped op, so there is no probe/recv window -- the pattern the
 * multi-worker IOCP driver needs to avoid lost completions under high load.
 * On POSIX backends the recv is performed at readiness (same effect). */
void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
                    zan_co_step_t step, int64_t *out_n);

/* Overlapped accept: post AcceptEx for listener `fd` and suspend `frame`
 * until a connection completes. The accepted socket is stored in `*out_fd`,
 * or -1 when the operation cannot be posted or completed. */
void zan_io_accept_co(intptr_t fd, void *frame, zan_co_step_t step,
                      intptr_t *out_fd);

/* Async hostname resolution: run `hostname` through the resolver on a worker
 * thread (the reactor never blocks on DNS) and suspend `frame` until it
 * finishes. The resolved IPv4 address -- same value and byte order as
 * zan_io_resolve_ipv4, 0 on failure -- is stored into `*out` before the frame
 * is re-readied via `step`. Returns immediately. */
void zan_io_resolve_co(const char *hostname, void *frame, zan_co_step_t step,
                       int32_t *out);

/* Run one scalar-only native call away from the reactor.  The worker invokes
 * fn(a0..a[argc-1]) exactly once, stores its int64 result in *out, and
 * re-readies frame through step.  A NULL out denotes a void native call. */
void zan_rt_blocking_co(void *fn, int32_t argc,
                        int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                        void *frame, zan_co_step_t step, int64_t *out);

/* Idle bridge for the stackless scheduler: if IO watchers are pending, block
 * until at least one fires (readying its frame via zan_co_ready) and return the
 * number woken; otherwise return 0. Wire into the co driver with
 * zan_co_set_idle(zan_io_pump). */
int32_t zan_io_pump(void);

/* Timer-aware idle bridge used by generated schedulers. Blocks for IO for at
 * most timeout_ms, or sleeps for that duration when no IO is pending. A
 * negative timeout waits indefinitely when IO is pending. */
int32_t zan_io_pump_timeout(int64_t timeout_ms);

/* ---- coroutine-facing ABI (stackful rt_sched fibers) ---- */

/* Suspend the current coroutine until `fd` is readable.
 * Returns 0 on success, -1 on error (fd closed, etc.). */
int64_t zan_io_wait_readable(intptr_t fd);

/* Suspend the current coroutine until `fd` is writable. */
int64_t zan_io_wait_writable(intptr_t fd);

/* Suspend until `fd` is readable OR a timeout (ms) expires.
 * Returns 1 if readable, 0 if timeout, -1 on error. */
int64_t zan_io_wait_readable_timeout(intptr_t fd, int64_t timeout_ms);

/* Asynchronously connect socket `fd` to `ip`:`port` (IPv4 dotted-quad).
 * Suspends the current coroutine until the connection completes.
 * Returns 0 on success, -1 on error.  Backend: ConnectEx on Windows,
 * non-blocking connect + writable readiness on POSIX. */
int64_t zan_io_connect(intptr_t fd, const char *ip, int32_t port);

/* ---- scheduler-facing ---- */

/* Poll for IO events with at most `timeout_ms` wait.
 * Returns the number of coroutines moved to the ready queue. */
int32_t zan_io_poll(int64_t timeout_ms);

/* Returns non-zero if there are pending IO watchers. */
int32_t zan_io_has_pending(void);

/* Set a file descriptor to non-blocking mode. */
int32_t zan_io_set_nonblocking(intptr_t fd);

#endif /* ZAN_RT_IO_H */
