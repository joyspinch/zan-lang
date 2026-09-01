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
/* Fail-soft fault report (see rt_timer.c): append `text` to the runtime log
 * (deduped per site) and return. The compiler guards call this on the soft
 * path so a null reference or an out-of-range index logs and lets the program
 * keep running instead of exiting; ZAN_RT_HARD=1 flips every guard back to
 * the historical exit(70). */
void zan_rt_soft_note(const char *text);
/* Two-part soft report: `prefix` ("file.zan:818:40: runtime error: ") and
 * `msg` ("null reference ...") are separate compiler-emitted string globals
 * so the ~780 distinct message templates are stored once instead of being
 * duplicated inside every guard site's text. Deduped per SITE (by `prefix`
 * pointer identity, exactly like zan_rt_soft_note's text-pointer dedup);
 * the full text is composed on first sight, printed once, and logged. */
void zan_rt_soft_note2(const char *prefix, const char *msg);
/* The whole compiler guard in one runtime call: soft mode reports (note2) and
 * RETURNS so the caller continues with a default value; hard mode (ZAN_RT_HARD=1)
 * composes prefix+msg, prints it, raises the ZAN_RT_FAULT_MESSAGE record for
 * the crash log, and exits(70) -- the raise resumes this thread, so exit runs.
 * One call site per guard keeps 50k guard sites from each inlining this whole
 * sequence (was several MB of .text across a big program). */
void zan_rt_guard_fail2(const char *prefix, const char *msg);
/* Three-part soft/hard report: the compiler interns the file name once per
 * module and passes line/col as immediates, so thousands of guard sites share
 * one file string instead of each carrying a "file:line:col: runtime error: "
 * prefix global (~1 MB of .rdata across a big GUI publish). Dedup keys on the
 * composed text, exactly one report per site per process. */
void zan_rt_soft_note3(const char *file, unsigned line, unsigned col,
                       const char *msg);
/* The fail path matching note3: soft mode reports (note3) and returns; hard
 * mode composes, prints, raises the fault record, and exits(70). */
void zan_rt_guard_fail3(const char *file, unsigned line, unsigned col,
                        const char *msg);
/* Non-zero when ZAN_RT_HARD=1 was set at startup: guards must exit(70)
 * instead of reporting and continuing. */
int zan_rt_soft_is_hard(void);
/* A small zeroed, readable/writable page the soft guards substitute for a
 * null base before the lowered GEP+load runs: the fault-free load then reads
 * 0 and stores land in scratch memory instead of page 0. Only the soft path
 * ever selects it; hard mode exits inside the guard report. */
unsigned char *zan_rt_soft_scratch(void);
/* Windows starts a console program through a narrow CRT argv whose encoding
 * follows the active ANSI code page. This helper rebuilds it from the Unicode
 * command line as UTF-8. It returns non-zero on success and otherwise leaves
 * the caller-owned argc/argv values unchanged. */
int zan_utf8_argv(int *argc, char ***argv);
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