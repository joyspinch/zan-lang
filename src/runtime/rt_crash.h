#ifndef ZAN_RT_CRASH_H
#define ZAN_RT_CRASH_H

#ifndef ZAN_MAYBE_UNUSED
#  if defined(__GNUC__) || defined(__clang__)
#    define ZAN_MAYBE_UNUSED __attribute__((unused))
#  else
#    define ZAN_MAYBE_UNUSED
#  endif
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

/* B-ID106: 崩溃报告/恢复实现已自本头文件拆出至 rt_crash.c（历史上 1413 行
 * 实现内联在头里，被 rt_io / rt_timer / gui_runtime 三个 TU 重复编译三份）。
 * 本头只留导出原型；凡链 zanrt_io.o 或 zan_gui 的目标必须同链 zanrt_crash.o。
 * 链外单测静态函数走 #include "rt_crash.c"（runtime_crash_log_rotation）。 */
void zan__crash_install(void);
int zan__guard_call(void (*fn)(void *), void *arg);
long zan__guard_recovered_count(void);

#endif /* ZAN_RT_CRASH_H */
