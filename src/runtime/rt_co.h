/* rt_co */
#ifndef ZAN_RT_CO_H
#define ZAN_RT_CO_H

#include <stddef.h>
#include <stdint.h>

typedef void (*zan_co_step_t)(void *frame);

void   zan_co_sched_init(void);
void   zan_co_ready(void *frame, zan_co_step_t step);
void   zan_co_sched_run(void);
void   zan_co_sched_run_until(const volatile int *done);
size_t zan_co_pending(void);

void   __zan_co_frame_free(void *frame);

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
