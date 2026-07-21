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

typedef long long i64;

int zan_w32_snprintf(char *s, i64 n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, (size_t)n, fmt, ap);
    va_end(ap);
    return r;
}
