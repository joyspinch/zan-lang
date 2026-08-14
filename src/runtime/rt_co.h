/* rt_co.h -- Zan stackless-coroutine driver.
 *
 * Backs the compiler's CPS state-machine lowering of async/await (see
 * docs/ASYNC_CPS_DESIGN.md). Unlike rt_sched.c (stackful ucontext/Fiber
 * coroutines), this driver is a plain cooperative ready-queue: the compiler
 * emits each async function as a heap "frame" plus a resume/step function, and
 * this driver just pops (frame, step) pairs and calls step(frame) until the
 * ready queue (and, once wired, the IO reactor) drains.
 *
 * Compiler-facing ABI (emitted code / await protocol):
 *   zan_co_sched_init()          -- reset the driver (call once in main())
 *   zan_co_ready(frame, step)    -- enqueue a frame to be (re)stepped
 *   zan_co_sched_run()           -- pump until no runnable frames remain
 *   zan_co_pending()             -- runnable frames currently queued (tests)
 *
 * The frame layout and the await/complete handshake live entirely in the
 * compiler; this driver is intentionally agnostic to frame contents.
 */
#ifndef ZAN_RT_CO_H
#define ZAN_RT_CO_H

#include <stddef.h>
#include <stdint.h>

/* Resume/step function emitted per async method: re-enters the state machine
 * whose saved state lives in `frame`. */
typedef void (*zan_co_step_t)(void *frame);

void   zan_co_sched_init(void);
void   zan_co_ready(void *frame, zan_co_step_t step);
void   zan_co_sched_run(void);
/* Pump like zan_co_sched_run, but return as soon as *done is non-zero. Emitted
 * code passes the DONE flag of the frame a synchronous context is awaiting, so
 * a background coroutine that never completes (a flusher loop, a spawned
 * server) cannot hold that await open. NULL = drain, i.e. zan_co_sched_run. */
void   zan_co_sched_run_until(const volatile int *done);
size_t zan_co_pending(void);

/* Release an async frame. Emitted code (--async-workers) calls this instead of
 * free() for coroutine frames: under the multi-worker driver the scheduler may
 * still hold a reference (the frame is running, or a task naming it sits in a
 * worker queue), and the driver defers the free until that reference is gone.
 * The single-threaded drivers free immediately. */
void   __zan_co_frame_free(void *frame);

/* Optional idle hook. When the ready queue empties, the scheduler calls this to
 * block for external events (the IO reactor / timers) and enqueue any newly
 * runnable frames. It returns the number of frames it made ready; a return of 0
 * means nothing is left to wait for and the scheduler stops. This is how the IO
 * reactor bridges into the driver without rt_co depending on rt_io directly
 * (wire it with zan_co_set_idle(zan_io_pump)). NULL (the default) = queue-only.
 */
typedef int (*zan_co_idle_fn)(void);
void   zan_co_set_idle(zan_co_idle_fn fn);

/* Registry of live detached (Task.Spawn) frames (rt_colive.c). A spawn handle
 * is a raw frame pointer that outlives the coroutine once the reaper frees the
 * frame, so emitted code (Task.Cancel, Task.WhenAll) asks whether a handle is
 * still live before dereferencing it. Safe to call from any thread the driver
 * runs on. */
void   zan_co_live_add(void *frame);
void   zan_co_live_del(void *frame);
int    zan_co_live_has(void *frame);
void   zan_co_live_reset(void);

/* Per-program async runtime settings (rt_timer.c, i.e. always linked), exposed
 * to Zan code as System.Threading.AsyncRuntime. Set them from Main, before the
 * pool starts; the multi-worker driver reads them in zan_co_sched_run. An
 * unset value (0, or -1 for sync_fast) keeps the driver's own default, which
 * still honours the ZAN_CO_WORKERS / ZAN_IO_SHARDS / ZAN_IO_SYNCFAST
 * environment variables. */
void    zan_async_set_workers(int32_t workers);
void    zan_async_set_io_shards(int32_t shards);
void    zan_async_set_sync_fast(int32_t on);
int32_t zan_async_cfg_workers(void);
int32_t zan_async_cfg_io_shards(void);
int32_t zan_async_cfg_sync_fast(void);

#endif /* ZAN_RT_CO_H */
