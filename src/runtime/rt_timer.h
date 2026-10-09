#ifndef ZAN_RT_TIMER_H
#define ZAN_RT_TIMER_H

/* 内部辅助逻辑 */
#define ZAN_OOM_TO_RUNTIME 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*zan_timer_callback_t)(void);
typedef void (*zan_timer_step_t)(void *frame);

void zan_timer_runtime_reset(void);
/* Fail-soft fault report (see rt_timer */
void zan_rt_soft_note(const char *text);
/* Two-part soft report: `prefix` ("file */
void zan_rt_soft_note2(const char *prefix, const char *msg);
/* 内部辅助逻辑 */
void zan_rt_guard_fail2(const char *prefix, const char *msg);
/* 内部辅助逻辑 */
void zan_rt_soft_note3(const char *file, unsigned line, unsigned col,
                       const char *msg);
/* 内部辅助逻辑 */
void zan_rt_guard_fail3(const char *file, unsigned line, unsigned col,
                        const char *msg);
/* 内部辅助逻辑 */
int zan_rt_soft_is_hard(void);
/* 内部辅助逻辑 */
void zan_rt_set_strict(void);
/* 内部辅助逻辑 */
typedef void (*zan_fatal_fn)(const char *category, const char *message);
void zan_rt_set_fatal_handler(zan_fatal_fn fn);
/* The funnel itself */
void zan_rt_fatal(const char *category, const char *message);
/* 内部辅助逻辑 */
unsigned char *zan_rt_soft_scratch(void);
/* Shortest round-trip double -> C#-style "G" string (see rt_timer */
void zan_rt_dbl_str(char *buf, unsigned long long cap, double v);
/* double */
double zan_rt_dbl_parse(const char *s, char **endp);
/* 内部辅助逻辑 */
int zan_utf8_argv(int *argc, char ***argv);
void zan_timer_set_ready_hook(void (*ready)(void *frame, zan_timer_step_t step));
long long zan_timer_now_ms(void);
void zan_timer_delay(long long ms, void *frame, zan_timer_step_t step);
/* Cancel every pending DELAY entry naming `frame` (see zan_timer_delay) */
int zan_timer_cancel_delay(void *frame);
long long zan_timer_next_timeout(void);
/* 内部辅助逻辑 */
long long zan_co_quantum_ms(void);
/* Microsecond monotonic clock for slice/throttle accounting */
long long zan_co_precise_us(void);
/* 内部辅助逻辑 */
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

#include "rt_hw_accel.h"

#ifdef __cplusplus
}
#endif

#endif
