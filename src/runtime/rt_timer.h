#ifndef ZAN_RT_TIMER_H
#define ZAN_RT_TIMER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*zan_timer_callback_t)(void);
typedef void (*zan_timer_step_t)(void *frame);

void zan_timer_runtime_reset(void);
void zan_timer_set_ready_hook(void (*ready)(void *frame, zan_timer_step_t step));
long long zan_timer_now_ms(void);
void zan_timer_delay(long long ms, void *frame, zan_timer_step_t step);
long long zan_timer_next_timeout(void);
/* These return counts; `long long` (not size_t) because the compiler's IR
 * declares them with Zan's 64-bit int and wasm32's size_t is 32-bit. */
long long zan_timer_dispatch_due(void);
long long zan_timer_pending(void);

long long zan_timer_tick(long long interval, zan_timer_callback_t callback);
long long zan_timer_after(long long delay, zan_timer_callback_t callback);
int zan_timer_clear(long long id);
long long zan_timer_clear_all(void);
int zan_timer_info(long long id, long long *exec_msec, long long *exec_count,
                   long long *interval, long long *round, int *removed);
long long zan_timer_list_count(void);
long long zan_timer_list_at(long long index);
void zan_timer_stats(long long *initialized, long long *num, long long *round);

/* Swoole-compatible native aliases. */
long long swoole_timer_tick(long long interval, zan_timer_callback_t callback);
long long swoole_timer_after(long long delay, zan_timer_callback_t callback);
int swoole_timer_clear(long long id);
long long swoole_timer_clear_all(void);
int swoole_timer_info(long long id, long long *exec_msec, long long *exec_count,
                      long long *interval, long long *round, int *removed);
long long swoole_timer_list_count(void);
long long swoole_timer_list_at(long long index);
void swoole_timer_stats(long long *initialized, long long *num, long long *round);

#ifdef __cplusplus
}
#endif

#endif
