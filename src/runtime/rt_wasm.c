/* rt_wasm */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef long long i64;

int zan_w32_snprintf(char *s, i64 n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, (size_t)n, fmt, ap);
    va_end(ap);
    return r;
}

int pthread_mutex_init(void *m, const void *a) { (void)m; (void)a; return 0; }
int pthread_mutex_lock(void *m) { (void)m; return 0; }
int pthread_mutex_unlock(void *m) { (void)m; return 0; }
int pthread_mutex_destroy(void *m) { (void)m; return 0; }

void longjmp(void *env, int val) { (void)env; (void)val; abort(); }

__attribute__((returns_twice))
int setjmp(void *env) { (void)env; return 0; }

/* __zan_co_frame_free：wasm 单线程、无协程调度器（CPS 驱动直接跑帧），
 * 帧没有调度头可用——裸 free 即语义（A380 的守卫+检疫区属于 mt 驱动
 * 的运行时集；external_async_executor 路径引用本符号，wasm 侧由此提供
 * 定义）。 */
void __zan_co_frame_free(void *frame) { free(frame); }
