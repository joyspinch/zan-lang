/* rt_crash.c -- 崩溃报告与线程级恢复实现（B-ID106 自 rt_crash.h 拆出）。
 * 历史上实现整体内联在头文件，被 rt_io / rt_timer / gui_runtime 三个 TU
 * 各自重复编译一份（selectany 共享态 + 三份代码 + 三个构造器）；现在
 * 实现单点住本 TU，头文件只留导出原型，消费 TU 调用点零改动。
 * Windows：VEH + SetUnhandledExceptionFilter + __builtin_setjmp 恢复；
 * POSIX：sigaction + sigaltstack。
 * 链接面：凡链 zanrt_io.o 或 zan_gui 的目标必须同链 zanrt_crash.o
 * （rt_io.c 调 install；gui_runtime.c 调 guard_call/recovered_count/install）。
 * POSIX 编译面按文档需 _GNU_SOURCE（musl 的 sigaltstack/SS_DISABLE）。 */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#include "rt_crash.h"

#if defined(_WIN32)
#include <windows.h>
WINBASEAPI ULONGLONG WINAPI GetTickCount64(VOID);
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define ZAN_CRASH_SHARED __attribute__((selectany))
#elif defined(_MSC_VER)
#define ZAN_CRASH_SHARED __declspec(selectany)
#else
#define ZAN_CRASH_SHARED
#endif

/* 跨架构 CPU 寄存器上下文解析 */
#if defined(_M_ARM64) || defined(__aarch64__)
#define ZAN_CTX_SP(c) ((c)->Sp)
#define ZAN_CTX_PC(c) ((c)->Pc)
#elif defined(_M_X64) || defined(__x86_64__)
#define ZAN_CTX_SP(c) ((c)->Rsp)
#define ZAN_CTX_PC(c) ((c)->Rip)
#else
#define ZAN_CTX_SP(c) (0)
#define ZAN_CTX_PC(c) (0)
#endif

/* Windows 组合分发器（Composite Dispatcher）子模块拓扑引入：
 * 1. crash/rt_crash_win_io.c:    无锁 IO 句柄包装器与纯栈格式化器（零 CRT 锁）
 * 2. crash/rt_crash_win_sym.c:   addr2line/llvm-symbolizer 管道符号化与 PE 栈扫描
 * 3. crash/rt_crash_win_guard.c: VEH 向量化异常过滤、线程级崩溃恢复与保护调用
 */
#include "crash/rt_crash_win_io.c"
#include "crash/rt_crash_win_sym.c"
#include "crash/rt_crash_win_guard.c"

#else /* !_WIN32 */

/* POSIX 组合分发器（Composite Dispatcher）子模块拓扑引入：
 * crash/rt_crash_posix.c: sigaltstack、信号安全写日志与帧回溯解析
 */
#include "crash/rt_crash_posix.c"

#endif
