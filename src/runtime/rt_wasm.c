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

/* 内部辅助逻辑 */
int pthread_mutex_init(void *m, const void *a) { (void)m; (void)a; return 0; }
int pthread_mutex_lock(void *m) { (void)m; return 0; }
int pthread_mutex_unlock(void *m) { (void)m; return 0; }
int pthread_mutex_destroy(void *m) { (void)m; return 0; }

void longjmp(void *env, int val) { (void)env; (void)val; abort(); }

/* 底层系统交互与数据协议契约 */
__attribute__((returns_twice))
int setjmp(void *env) { (void)env; return 0; }
