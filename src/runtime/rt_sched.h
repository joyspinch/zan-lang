/* rt_sched.h -- Zan M1 coroutine runtime.
 *
 * A cooperative, stackful-fiber scheduler that backs zan's async/await.
 * M1 runs all coroutines on a single OS thread (M:1); the interface is kept
 * neutral so an M:N work-stealing scheduler can slot in later without changing
 * the compiler-facing ABI.
 *
 * Backends:
 *   - Windows: Fibers (ConvertThreadToFiber / CreateFiber / SwitchToFiber)
 *   - POSIX:   ucontext
 *
 * Compiler-facing ABI (called from emitted code):
 *   zan_sched_init / zan_sched_run / zan_sched_shutdown  -- bootstrap in main()
 *   zan_spawn(body, arg)   -- start a coroutine, returns its Task*
 *   zan_task_arg(t)        -- captured argument struct for the running body
 *   zan_task_return(t, r)  -- set result, complete the task, never returns
 *   zan_task_await(t)      -- suspend caller until t completes, returns result
 *   zan_task_delay(ms)     -- Task that completes after ms
 *   zan_task_yield()       -- cooperative yield to the scheduler
 *   zan_task_result(t)     -- read a completed task's result
 *
 * IO integration (called from rt_io.c and stdlib):
 *   zan_io_suspend_current()   -- park current co for IO wait
 *   zan_io_resume(co)          -- move a parked co back to the ready queue
 *   zan_io_get_current_co()    -- get current coroutine handle for IO reg
 */
#ifndef ZAN_RT_SCHED_H
#define ZAN_RT_SCHED_H

#include <stdint.h>

typedef struct zan_task zan_task_t;

/* Coroutine body emitted by the compiler for each async method. */
typedef void (*zan_co_body_t)(zan_task_t *task);

/* ---- scheduler lifecycle ---- */
void zan_sched_init(void);
void zan_sched_run(void);
void zan_sched_shutdown(void);

/* ---- coroutine / task ABI ---- */
zan_task_t *zan_spawn(zan_co_body_t body, void *arg);
void       *zan_task_arg(zan_task_t *task);
void        zan_task_return(zan_task_t *task, int64_t result);
int64_t     zan_task_await(zan_task_t *task);
zan_task_t *zan_task_delay(int64_t ms);
void        zan_task_yield(void);
int64_t     zan_task_result(zan_task_t *task);

/* ---- IO integration (used by rt_io.c) ---- */
void  zan_io_suspend_current(void);
void  zan_io_resume(void *co);
void *zan_io_get_current_co(void);

#endif /* ZAN_RT_SCHED_H */
