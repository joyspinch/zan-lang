/* 内部辅助实现 */
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
/* 内部辅助实现 */
int64_t zan_io_socket_send(intptr_t fd, const void *buf, int64_t len,
                           int32_t flags);
int64_t zan_io_socket_recv(intptr_t fd, void *buf, int64_t len,
                           int32_t flags);
int32_t zan_io_socket_ready(intptr_t fd, int32_t write_ready);

/* Probe the outcome of a non-blocking connect on `fd`.
 * Returns 0 once connected, a positive SO_ERROR code (or -1 when no code is
 * available) once the connect has failed, and -2 while still in progress. */
int32_t zan_io_connect_status(intptr_t fd);

/* 检查是否`fd` still refers to an open socket */
int32_t zan_io_socket_alive(intptr_t fd);

/* 内部辅助实现 */
void zan_io_close_notify(intptr_t fd);
int64_t zan_io_socket_peer_ipv4(intptr_t fd);

/* Resolve a hostname to an IPv4 address in the same byte order as inet_addr.
 * Returns 0 when the hostname cannot be resolved. */
int32_t zan_io_resolve_ipv4(const char *hostname);

/* 内部辅助实现 */
int32_t zan_io_resolve_sa(const char *name, int32_t port, void *buf,
                          int32_t cap);

/* Resolve every IPv4/IPv6 candidate into caller-owned fixed-size records */
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
/* Return 1 only when the binary destination is public and routable. The
 * explicit loopback exception is reserved for local test/development callers;
 * all other special ranges (including mapped IPv4) are rejected. */
int32_t zan_io_sockaddr_is_safe(const void *sa, int32_t len,
                               int32_t allow_loopback);

/* Start an exact sockaddr connect without resolving or rewriting the address.
 * Returns 0 when connected, -2 while in progress, or a positive platform
 * error code (-1 when no code is available). The socket is made nonblocking. */
int32_t zan_io_connect_sa_start(intptr_t fd, const void *sa, int32_t salen);
/* Worker-safe async ABI: exact sockaddr connect, returning 0 on success or
 * the platform error code. This function never performs DNS. */
int64_t zan_io_connect_sa(intptr_t fd, const void *sa, int32_t salen,
                           int32_t timeout_ms);

/* Format the address part of a sockaddr (IPv4 or IPv6) as a dotted-quad /
 * colon-hex string. Returns a static buffer (INET6_ADDRSTRLEN), or "" when
 * the family is neither AF_INET nor AF_INET6. */
const char *zan_io_sockaddr_ip_str(const void *sa);

/* 内部辅助实现 */
void zan_io_resolve_sa_co(const char *name, int32_t port, void *buf,
                          int32_t cap, void *frame, zan_co_step_t step,
                          int32_t *out);

#if defined(_WIN32)
/* Read the encoded DER pointer/length from a Windows PCCERT_CONTEXT.
 * Keeps the CERT_CONTEXT layout out of the Zan standard library. */
const unsigned char *zan_crypto_cert_encoded(const void *cert, int *out_len);
/* Verify the exact TLS peer DER sequence against Windows chain + SSL policy */
int32_t zan_io_crypto_windows_ssl_policy(const unsigned char *certs, int32_t total_len,
                                       int32_t count, const char *host, int32_t host_len);
#endif

/* ---- stackless (CPS state-machine) ABI ---- The compiler's async lowering (see docs/ASYNC_CPS_DESIGN */
void zan_io_wait_co(intptr_t fd, int32_t interest, void *frame, zan_co_step_t step);

/* 内部辅助实现 */
void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
                    zan_co_step_t step, int64_t *out_n);

/* Overlapped receive with a deadline: like zan_io_recv_co, but the await also carries `timeout_ms` */
void zan_io_recv_to_co(intptr_t fd, void *buf, int32_t len, int64_t timeout_ms,
                       void *frame, zan_co_step_t step, int64_t *out_n);

/* Overlapped accept: post AcceptEx for listener `fd` and suspend `frame`
 * until a connection completes. The accepted socket is stored in `*out_fd`,
 * or -1 when the operation cannot be posted or completed. */
void zan_io_accept_co(intptr_t fd, void *frame, zan_co_step_t step,
                      intptr_t *out_fd);

/* 内部辅助实现 */
void zan_io_resolve_co(const char *hostname, void *frame, zan_co_step_t step,
                       int32_t *out);

/* Run one scalar-only native call away from the reactor.  The worker invokes
 * fn(a0..a[argc-1]) exactly once, stores its int64 result in *out, and
 * re-readies frame through step.  A NULL out denotes a void native call. */
void zan_rt_blocking_co(void *fn, int32_t argc,
                        int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                        void *frame, zan_co_step_t step, int64_t *out);

/* 内部辅助实现 */
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

/* Asynchronously connect socket `fd` to `ip`:`port` (IPv4 dotted-quad) */
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
