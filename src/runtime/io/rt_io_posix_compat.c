/* rt_io_posix_compat.c: POSIX 面的 Win32 原语 shim（自包含，唯一真相源）。
 *
 * 历史包袱：这套 pthread→Win32 映射曾内嵌在 rt_io_co_pool.c 头部，靠
 * "co_pool 恰好先于 co_sched 拼入同一 TU"这一拼接巧合向全 TU 供货；
 * rt_io.c 又各自维护第二份 LONG/Interlocked 映射。重排即断，且只靠
 * 自然语言注释约定——现收敛为本单元，自带系统头，多个消费者指向它。
 *
 * 契约（unity_order_check.py 机检）：
 *   - 本 TU 内恰好拼入一次（rt_io.c 的 POSIX 段最顶，先于全部消费者）；
 *   - rt_io_co_pool.c / rt_io_co_sched.c 以 UNITY-ORDER: after 标注依赖；
 *   - 仅拼入 rt_io.c TU，不参与链接导出（无链接期符号）。
 * Windows（_WIN32）下本单元整体为空，零行为变化。 */

#ifndef ZAN_RT_IO_POSIX_COMPAT_INCLUDED
#define ZAN_RT_IO_POSIX_COMPAT_INCLUDED

#if !defined(_WIN32)

#include <pthread.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>
#include <stddef.h> /* NULL：不再依赖 pthread.h 的传递可见性 */
#include <stdint.h> /* uintptr_t（ULONG_PTR） */

typedef long LONG;

typedef pthread_mutex_t CRITICAL_SECTION;
static inline void InitializeCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_init(cs, NULL); }
static inline void EnterCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_lock(cs); }
static inline void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(cs); }
static inline void DeleteCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_destroy(cs); }

#ifndef InterlockedCompareExchange64
#define InterlockedCompareExchange64(dst, exch, comp) \
    __sync_val_compare_and_swap((long long volatile *)(dst), (long long)(comp), (long long)(exch))
#endif

#ifndef InterlockedIncrement
#define InterlockedIncrement(dst) \
    (LONG)__sync_add_and_fetch((LONG volatile *)(dst), 1)
#endif

#ifndef InterlockedDecrement
#define InterlockedDecrement(dst) \
    (LONG)__sync_sub_and_fetch((LONG volatile *)(dst), 1)
#endif

#ifndef InterlockedExchange
#define InterlockedExchange(dst, val) \
    (LONG)__sync_lock_test_and_set((LONG volatile *)(dst), (LONG)(val))
#endif

#ifndef InterlockedCompareExchange
#define InterlockedCompareExchange(dst, exch, comp) \
    (LONG)__sync_val_compare_and_swap((LONG volatile *)(dst), (LONG)(comp), (LONG)(exch))
#endif

#ifndef InterlockedExchangeAdd
#define InterlockedExchangeAdd(dst, val) \
    (LONG)__sync_fetch_and_add((LONG volatile *)(dst), (LONG)(val))
#endif

#ifndef MemoryBarrier
#define MemoryBarrier() __sync_synchronize()
#endif

typedef pthread_key_t DWORD;
#ifndef TLS_OUT_OF_INDEXES
#define TLS_OUT_OF_INDEXES ((DWORD)-1)
#endif

static inline DWORD TlsAlloc(void) {
    pthread_key_t k;
    return (pthread_key_create(&k, NULL) == 0) ? k : TLS_OUT_OF_INDEXES;
}
#define TlsGetValue(k) pthread_getspecific(k)
#define TlsSetValue(k, v) pthread_setspecific((k), (v))

#define SwitchToThread() sched_yield()
#define Sleep(ms) usleep((ms) * 1000)

static inline unsigned long long GetTickCount64(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)(ts.tv_nsec / 1000000ULL);
}
#define GetTickCount() ((DWORD)GetTickCount64())
typedef int BOOL;
typedef unsigned long ULONG;
typedef uintptr_t ULONG_PTR;
typedef void* LPVOID;
typedef void* HANDLE;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif
#ifndef WINAPI
#define WINAPI
#endif

#endif /* !defined(_WIN32) */

#endif /* ZAN_RT_IO_POSIX_COMPAT_INCLUDED */
