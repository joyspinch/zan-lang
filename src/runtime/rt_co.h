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

/* Resume/step function emitted per async method: re-enters the state machine
 * whose saved state lives in `frame`. */
typedef void (*zan_co_step_t)(void *frame);

void   zan_co_sched_init(void);
void   zan_co_ready(void *frame, zan_co_step_t step);
void   zan_co_sched_run(void);
size_t zan_co_pending(void);

/* Optional idle hook. When the ready queue empties, the scheduler calls this to
 * block for external events (the IO reactor / timers) and enqueue any newly
 * runnable frames. It returns the number of frames it made ready; a return of 0
 * means nothing is left to wait for and the scheduler stops. This is how the IO
 * reactor bridges into the driver without rt_co depending on rt_io directly
 * (wire it with zan_co_set_idle(zan_io_pump)). NULL (the default) = queue-only.
 */
typedef int (*zan_co_idle_fn)(void);
void   zan_co_set_idle(zan_co_idle_fn fn);

#endif /* ZAN_RT_CO_H */
