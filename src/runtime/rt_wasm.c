/* rt_wasm.c -- wasm32 (WASI) libc adapter.
 *
 * Zan IR declares libc functions with 64-bit sizes (Zan int is i64), but
 * wasm32's size_t/long are 32-bit and wasm enforces exact call signatures.
 * Non-variadic declarations are adapted directly in IR (see
 * zan_irgen_write_obj); variadic ones cannot forward varargs in IR, so they
 * are renamed to these C wrappers instead.
 *
 * Compile with: clang --target=wasm32-wasi -O2 -c rt_wasm.c
 * (shipped pre-compiled as toolchain/wasm32/zanrt_wasm.o)
 */
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

/* ---- single-thread fallbacks ---------------------------------------------
 * The auto-stdlib pull-in compiles whole namespace directories, so the
 * emitted object references System/Threading's mutex helpers and the
 * exception path's longjmp even when nothing is ever called. wasi-libc has
 * neither pthreads nor setjmp/longjmp (wasm has no threads and no stack
 * unwinding), so those references would not link. Provide no-op / abort
 * definitions: on a single-threaded wasm a mutex never needs to block, and
 * reaching longjmp means the program hit a path it cannot take. Programs
 * that actually CALL Thread/AtomicInt/SharedTable APIs are rejected earlier
 * (see main.c's wasm_obj_refs_any check). */
int pthread_mutex_init(void *m, const void *a) { (void)m; (void)a; return 0; }
int pthread_mutex_lock(void *m) { (void)m; return 0; }
int pthread_mutex_unlock(void *m) { (void)m; return 0; }
int pthread_mutex_destroy(void *m) { (void)m; return 0; }

void longjmp(void *env, int val) { (void)env; (void)val; abort(); }
