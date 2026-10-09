/* rt_sched */
#ifndef ZAN_RT_SCHED_H
#define ZAN_RT_SCHED_H

#include <stddef.h>
#include <stdint.h>

typedef struct zan_task zan_task_t;

/* Coroutine body emitted by the compiler for each async method */
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
void        zan_task_release(zan_task_t *task);
size_t      zan_task_live(void);

/* ---- IO integration (used by rt_io.c) ---- */
void  zan_io_suspend_current(void);
void  zan_io_resume(void *co);
void *zan_io_get_current_co(void);

#endif /* ZAN_RT_SCHED_H */
