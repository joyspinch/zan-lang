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

/* ---- stackless (CPS state-machine) ABI ----
 *
 * The compiler's async lowering (see docs/ASYNC_CPS_DESIGN.md) has no fiber to
 * park: an `await` on IO records its resume point in the heap frame and returns
 * to the scheduler. So instead of suspending the caller inline, register a
 * one-shot watcher that, when `fd` becomes ready for `interest`
 * (ZAN_IO_READ / ZAN_IO_WRITE), calls `zan_co_ready(frame, step)` to re-enter
 * the state machine. Returns immediately (does not block or suspend). */
void zan_io_wait_co(int64_t fd, int interest, void *frame, zan_co_step_t step);

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
void zan_io_recv_co(int64_t fd, void *buf, int len, void *frame,
                    zan_co_step_t step, int64_t *out_n);

/* Idle bridge for the stackless scheduler: if IO watchers are pending, block
 * until at least one fires (readying its frame via zan_co_ready) and return the
 * number woken; otherwise return 0. Wire into the co driver with
 * zan_co_set_idle(zan_io_pump). */
int zan_io_pump(void);

/* ---- coroutine-facing ABI (stackful rt_sched fibers) ---- */

/* Suspend the current coroutine until `fd` is readable.
 * Returns 0 on success, -1 on error (fd closed, etc.). */
int64_t zan_io_wait_readable(int64_t fd);

/* Suspend the current coroutine until `fd` is writable. */
int64_t zan_io_wait_writable(int64_t fd);

/* Suspend until `fd` is readable OR a timeout (ms) expires.
 * Returns 1 if readable, 0 if timeout, -1 on error. */
int64_t zan_io_wait_readable_timeout(int64_t fd, int64_t timeout_ms);

/* Asynchronously connect socket `fd` to `ip`:`port` (IPv4 dotted-quad).
 * Suspends the current coroutine until the connection completes.
 * Returns 0 on success, -1 on error.  Backend: ConnectEx on Windows,
 * non-blocking connect + writable readiness on POSIX. */
int64_t zan_io_connect(int64_t fd, const char *ip, int port);

/* ---- scheduler-facing ---- */

/* Poll for IO events with at most `timeout_ms` wait.
 * Returns the number of coroutines moved to the ready queue. */
int zan_io_poll(int64_t timeout_ms);

/* Returns non-zero if there are pending IO watchers. */
int zan_io_has_pending(void);

/* Set a file descriptor to non-blocking mode. */
int zan_io_set_nonblocking(int64_t fd);

#endif /* ZAN_RT_IO_H */
