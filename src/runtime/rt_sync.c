#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if (defined(__FreeBSD__) || defined(__DragonFly__)) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "rt_sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rt_timer.h"
#include "../common/zan_abi.h"
#include "rt_oom.h"  /* B-ID98 站点化 OOM 注入 */

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <pthread_np.h>
#endif
#if defined(__NetBSD__)
#include <lwp.h>
#endif

#if defined(__linux__) && !defined(__GLIBC_PREREQ)
#include <features.h>
#endif

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#if defined(__linux__)
#include <netpacket/packet.h>
#else
#include <net/if_dl.h>
#endif

#if !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif
#if !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif
#endif

/* ========================================================================= */
/* 组合分发器 (Composite Dispatcher) 架构：包含各内聚领域模块                */
/* ========================================================================= */

/* 高精度单调时钟与计时器秒表 */
#include "sync/rt_sync_time.c"

/* 跨平台具名共享内存与映射 */
#include "sync/rt_sync_shm.c"

/* 操作系统原生线程、监视器锁与 UI 任务派发 */
#include "sync/rt_sync_thread.c"

/* 跨平台共享内存表核心结构与槽位探测 */
#include "sync/rt_sync_table.c"

/* 共享内存表生命周期与挂载管理 */
#include "sync/rt_sync_table_life.c"

/* 共享内存表强类型列值存取操作器 */
#include "sync/rt_sync_table_ops.c"

/* 跨平台网络接口与 ICMP 探测 */
#include "sync/rt_sync_net.c"

/* 跨平台安全子进程创建与执行 */
#include "sync/rt_sync_proc.c"
