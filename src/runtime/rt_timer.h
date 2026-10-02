#ifndef ZAN_RT_TIMER_H
#define ZAN_RT_TIMER_H

/* Every TU that carries this header ships in the runtime and is always linked
 * with this file (zan_rt_fatal's definition, see main.c: the timer object is
 * injected unconditionally), so host_oom.h routes its allocation-failure
 * abort through the zan_rt_fatal funnel and OOM becomes embedder-takeover
 * observable like every other unrecoverable fault. Include this header before
 * ../common/host_oom.h. */
#define ZAN_OOM_TO_RUNTIME 1

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
/* Non-zero when ZAN_RT_HARD=1 was set at startup (or the program was built
 * with --strict-runtime): guards must exit(70) instead of reporting and
 * continuing. */
int zan_rt_soft_is_hard(void);
/* Program-baked fail-fast: --strict-runtime compiles a main() prologue that
 * calls this before any user code, so a strict binary exits(70) on a guard
 * failure even where the operator never set ZAN_RT_HARD=1. The env var still
 * wins when explicitly set to 0 (escape hatch without a rebuild). */
void zan_rt_set_strict(void);
/* Host takeover for unrecoverable runtime failures: the allocator's
 * slab-consistency checks (double free, corrupt header), the scheduler's OOM
 * fail-fasts and its "task released before completion" contract check, and
 * the io reactor's node-allocation failure all funnel through zan_rt_fatal.
 * With no handler the behavior is the historical one: one diagnostic line on
 * stderr and abort(). An embedder (game server, service supervisor) registers
 * a handler to log, flush state and terminate itself -- exit() INSIDE the
 * handler, with the host's own status code. The handler runs on the faulting
 * thread and must not return: the condition that brought us here (a NULL
 * about to be dereferenced, a corrupt slab header) is unrecoverable, and a
 * returning handler falls back to abort(). Register it once at startup,
 * before any worker exists; the setter is a plain store because it is only
 * raced by handlers that were never registered. */
typedef void (*zan_fatal_fn)(const char *category, const char *message);
void zan_rt_set_fatal_handler(zan_fatal_fn fn);
/* The funnel itself. `category` is one of "mem" (slab consistency), "sched"
 * (scheduler contract violation), "oom" (allocation failure in the runtime's
 * own bookkeeping); `message` is a static description, safe to retain. */
void zan_rt_fatal(const char *category, const char *message);
/* A small zeroed, readable/writable page the soft guards substitute for a
 * null base before the lowered GEP+load runs: the fault-free load then reads
 * 0 and stores land in scratch memory instead of page 0. Only the soft path
 * ever selects it; hard mode exits inside the guard report. */
unsigned char *zan_rt_soft_scratch(void);
/* Shortest round-trip double -> C#-style "G" string (see rt_timer.c): the
 * shortest digit string strtod reads back bit-identical, fixed-point for
 * first-digit exponents -4..14, d.dddE+xx outside, NaN/Infinity spelled
 * out. `buf` receives at most 40 bytes including the NUL. */
void zan_rt_dbl_str(char *buf, unsigned long long cap, double v);
/* double.Parse / double.TryParse backing: matches the NaN / +/-Infinity
 * spellings zan_rt_dbl_str emits (legacy msvcrt strtod answers 0 for them),
 * then falls through to strtod. endp follows strtod semantics. */
double zan_rt_dbl_parse(const char *s, char **endp);
/* Windows starts a console program through a narrow CRT argv whose encoding
 * follows the active ANSI code page. This helper rebuilds it from the Unicode
 * command line as UTF-8. It returns non-zero on success and otherwise leaves
 * the caller-owned argc/argv values unchanged. */
int zan_utf8_argv(int *argc, char ***argv);
void zan_timer_set_ready_hook(void (*ready)(void *frame, zan_timer_step_t step));
long long zan_timer_now_ms(void);
void zan_timer_delay(long long ms, void *frame, zan_timer_step_t step);
/* Cancel every pending DELAY entry naming `frame` (see zan_timer_delay).
 * Called on the coroutine frame-release paths: an entry that outlives its
 * frame would wake freed memory when it comes due. Returns the count of
 * entries cancelled. */
int zan_timer_cancel_delay(void *frame);
long long zan_timer_next_timeout(void);
/* Cooperative scheduling quantum in ms (ZAN_CO_QUANTUM_MS, default 2, 0
 * disables): read by the drivers' zan_co_poll to decide when a frame that has
 * run past its slice must requeue. Lives with the timer because both drivers
 * already link this object for their clocks. */
long long zan_co_quantum_ms(void);
/* Microsecond monotonic clock for slice/throttle accounting. The ms clock
 * above is wall-deadline grade: on Windows GetTickCount64 ticks at ~15.6ms,
 * which would turn a 2ms quantum into a ~15ms slice and a 1ms pump throttle
 * into ~15ms. QPC is sub-microsecond on every supported Windows; POSIX
 * CLOCK_MONOTONIC already is. The timer heap's deadlines run here too
 * (B-ID48): due_us is zan_co_precise_us-based, so Delay precision no longer
 * carries the wall clock's tick granularity. zan_timer_next_timeout still
 * ANSWERS in ms (round-up) because its callers park on ms waits. */
long long zan_co_precise_us(void);
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

#include "rt_hw_accel.h"

#ifdef __cplusplus
}
#endif

#endif
