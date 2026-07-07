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

/* ---- lifecycle ---- */
void zan_io_init(void);
void zan_io_shutdown(void);

/* ---- coroutine-facing ABI ---- */

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
