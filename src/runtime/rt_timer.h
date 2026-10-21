#ifndef ZAN_RT_TIMER_H
#define ZAN_RT_TIMER_H

#define ZAN_OOM_TO_RUNTIME 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*zan_timer_callback_t)(void);
typedef void (*zan_timer_step_t)(void *frame);

void zan_timer_runtime_reset(void);
void zan_rt_soft_note(const char *text);
void zan_rt_soft_note2(const char *prefix, const char *msg);
void zan_rt_guard_fail2(const char *prefix, const char *msg);
void zan_rt_soft_note3(const char *file, unsigned line, unsigned col,
                       const char *msg);
void zan_rt_guard_fail3(const char *file, unsigned line, unsigned col,
                        const char *msg);
int zan_rt_soft_is_hard(void);
void zan_rt_set_strict(void);
typedef void (*zan_fatal_fn)(const char *category, const char *message);
void zan_rt_set_fatal_handler(zan_fatal_fn fn);
void zan_rt_fatal(const char *category, const char *message);
unsigned char *zan_rt_soft_scratch(void);
void zan_rt_dbl_str(char *buf, unsigned long long cap, double v);
/* double */
double zan_rt_dbl_parse(const char *s, char **endp);
int zan_utf8_argv(int *argc, char ***argv);
void zan_timer_set_ready_hook(void (*ready)(void *frame, zan_timer_step_t step));
long long zan_timer_now_ms(void);
void zan_timer_delay(long long ms, void *frame, zan_timer_step_t step);
int zan_timer_cancel_delay(void *frame);
long long zan_timer_next_timeout(void);
long long zan_co_quantum_ms(void);
long long zan_co_precise_us(void);
long long zan_timer_dispatch_due(void);
/* 等到能派发为止再派发一轮（与上面的区别：别的线程正派发时等它结束，
 * 而不是跳过）。同步上下文里的定时等待用它——跳过就等于没修。 */
long long zan_timer_dispatch_due_wait(void);
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

#include "rt_hw_accel.h"

#ifdef __cplusplus
}
#endif

#endif
