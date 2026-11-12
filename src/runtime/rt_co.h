/* rt_co */
#ifndef ZAN_RT_CO_H
#define ZAN_RT_CO_H

#include <stddef.h>
#include <stdint.h>

typedef void (*zan_co_step_t)(void *frame);

typedef struct {
    void         *frame;
    zan_co_step_t step;
} zan_co_task_t;

typedef zan_co_task_t zan_co_task;

void   zan_co_sched_init(void);
void   zan_co_ready(void *frame, zan_co_step_t step);
void   zan_co_ready_batch(const zan_co_task_t *tasks, size_t count);
void   zan_co_sched_run(void);
void   zan_co_sched_run_until(const volatile int *done);
size_t zan_co_pending(void);

void  *__zan_co_frame_alloc(size_t size);
void   __zan_co_frame_free(void *frame);

/* Public handles are generations; internal scheduler/child values are frames. */
void     zan_task_frame_config(void *frame, const volatile int *done, volatile int *cancel);
void     zan_task_frame_allocator(void *frame, void (*release)(void *));
void     zan_task_child(void *parent, void *child);
void     zan_task_cancel(uint64_t token);
void     zan_task_frame_cancel(void *frame);
uint64_t zan_task_register(void *frame, const volatile int *done);
int      zan_task_isdone(uint64_t token);
void    *zan_task_acquire(uint64_t token, int consume);
void     zan_task_unpin(void *frame);
void     zan_task_discard_config(void *frame, void (*discard)(void *));
void     zan_task_discard(void *frame);
void     zan_task_retire(void *frame, void (*release)(void *));
size_t   zan_task_live_count(void);
size_t   zan_task_slot_capacity(void);

typedef int (*zan_co_idle_fn)(void);
void   zan_co_set_idle(zan_co_idle_fn fn);

void   zan_co_live_add(void *frame);
void   zan_co_live_del(void *frame);
int    zan_co_live_has(void *frame);
int    zan_co_live_count(void);
void   zan_co_live_reset(void);

void    zan_async_set_workers(int32_t workers);
void    zan_async_set_io_shards(int32_t shards);
void    zan_async_set_sync_fast(int32_t on);
int32_t zan_async_cfg_workers(void);
int32_t zan_async_cfg_io_shards(void);
int32_t zan_async_cfg_sync_fast(void);

#endif /* ZAN_RT_CO_H */
