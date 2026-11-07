/*
 * zan_fortify_compat.c — glibc fortify 运行时接口 shim
 *
 * === 存在原因 ===
 * Zan runtime 本身默认用 -O0 构建，不需要 glibc 的 _FORTIFY_SOURCE
 * 边界检查。但链接 Zan 的宿主程序可能用 -O2 编译，此时 gcc 会把宿主
 * 代码里的 memcpy/snprintf 等重写为 __memcpy_chk/__snprintf_chk 等 fortify
 * 版本。若 Zan runtime 库（libzan_gui.a / zanrt_*.obj）不导出这些符号，
 * 宿主程序链接会报 undefined reference to __memcpy_chk。
 *
 * 本文件提供这些符号的实现，使 Zan runtime 自身或宿主程序在 -O2 下
 * 都能正常链接。build_linux_gui_static.sh 用 -U_FORTIFY_SOURCE 单独
 * 编译本文件，确保 shim 内部的 memcpy/snprintf 不会被编译器再套一层
 * __memcpy_chk（无限递归）。
 *
 * === 为什么不直接转发到底层 __*_chk ===
 * 某些构建目标（嵌入式 WASM / tcc / 自定义 libc）没有 glibc 提供的
 * 真实 __*_chk 实现，所以 shim 必须自包含——不能假设宿主一定有。
 *
 * === 边界检查策略 ===
 * ds / sl 参数是编译器在编译时推导出来的目标缓冲区大小（对于栈对象）。
 * 我们把它用作实际上限：若调用者传递的 n / sl 超过 ds / sl 所声明的
 * 上限，立即 abort 并打印定位信息，不做静默截断。这保证了 fortify 语义
 * 在 Zan runtime 下**不降级**——宿主编译器的边界检查假设是可信的。
 *
 * 注意：本文件只对"Zan runtime 作为被链接库"的场景生效；Zan 编译器
 * 构建用户程序时，用户代码的 memcpy/snprintf 安全由编译器前端（snprintf
 * 返回值检查、静态分析）负责，与本 shim 无关。
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

/* abort 时打印定位信息，帮助定位溢出源 */
static void zan_fortify_abort(const char *fn, const char *what,
                              size_t actual, size_t declared) {
    fprintf(stderr, "zan_fortify: %s overflow — %s size %zu exceeds declared buffer size %zu\n",
            fn, what, actual, declared);
    fflush(stderr);
    abort();
}

/* === 内存操作 — ds 是目标缓冲区声明大小 === */

void *__memcpy_chk(void *d, const void *s, size_t n, size_t ds) {
    if (n > ds) zan_fortify_abort("memcpy", "copy", n, ds);
    return memcpy(d, s, n);
}
void *__memmove_chk(void *d, const void *s, size_t n, size_t ds) {
    if (n > ds) zan_fortify_abort("memmove", "move", n, ds);
    return memmove(d, s, n);
}
void *__memset_chk(void *d, int c, size_t n, size_t ds) {
    if (n > ds) zan_fortify_abort("memset", "set", n, ds);
    return memset(d, c, n);
}

/* === 字符串复制 — ds 是目标缓冲区声明大小 ===
 *
 * 注意：strcpy/strcat 的 ds 必须足够装下结果串 + '\0'。我们按 strlen 校验：
 *   strcpy: strlen(s) + 1 <= ds
 *   strcat: strlen(d) + strlen(s) + 1 <= ds
 * 超过则 abort，不做截断（截断会丢数据，掩盖逻辑错误）。
 */

char *__strcpy_chk(char *d, const char *s, size_t ds) {
    size_t sl = strlen(s) + 1;
    if (sl > ds) zan_fortify_abort("strcpy", "copy", sl, ds);
    return strcpy(d, s);
}
char *__strncpy_chk(char *d, const char *s, size_t n, size_t ds) {
    /* 实际拷贝字节数 = min(n, strlen(s))，加上终止 NUL 至少还要 1 字节 */
    size_t copy = n < strlen(s) ? n : strlen(s);
    if (copy + 1 > ds) zan_fortify_abort("strncpy", "copy", copy + 1, ds);
    return strncpy(d, s, n);
}
char *__strcat_chk(char *d, const char *s, size_t ds) {
    size_t dl = strlen(d), sl = strlen(s);
    size_t need = dl + sl + 1;
    if (need > ds) zan_fortify_abort("strcat", "append", need, ds);
    return strcat(d, s);
}
char *__strncat_chk(char *d, const char *s, size_t n, size_t ds) {
    size_t dl = strlen(d);
    size_t sl = n < strlen(s) ? n : strlen(s);
    size_t need = dl + sl + 1;
    if (need > ds) zan_fortify_abort("strncat", "append", need, ds);
    return strncat(d, s, n);
}
char *__stpcpy_chk(char *d, const char *s, size_t ds) {
    size_t sl = strlen(s) + 1;
    if (sl > ds) zan_fortify_abort("stpcpy", "copy", sl, ds);
    return stpcpy(d, s);
}

/* === 格式化输出 — sl 是目标缓冲区声明大小 ===
 *
 * __sprintf_chk 没有长度参数 n，因此 sl 是唯一上限。我们把它转发到
 * __vsnprintf_chk 语义：用 sl-1 作为 snprintf 的上限，保证终止 NUL 不越界。
 * 若格式化结果截断（snprintf 返回 >= sl），abort 而非静默截断。
 *
 * __snprintf_chk 同样：n 是调用者显式给的上限，sl 是编译器推导的声明上限。
 * 两个上限取 min 作为真正上限。
 */

int __snprintf_chk(char *s, size_t n, int f, size_t sl, const char *fmt, ...) {
    va_list ap;
    size_t cap = n < sl ? n : sl;
    if (cap == 0) { return -1; }
    va_start(ap, fmt);
    int r = vsnprintf(s, cap, fmt, ap);
    va_end(ap);
    if (r >= 0 && (size_t)r >= cap) {
        zan_fortify_abort("snprintf", "format", (size_t)r + 1, cap);
    }
    return r;
}
int __vsnprintf_chk(char *s, size_t n, int f, size_t sl, const char *fmt, va_list ap) {
    size_t cap = n < sl ? n : sl;
    if (cap == 0) { return -1; }
    int r = vsnprintf(s, cap, fmt, ap);
    if (r >= 0 && (size_t)r >= cap) {
        zan_fortify_abort("vsnprintf", "format", (size_t)r + 1, cap);
    }
    return r;
}
int __sprintf_chk(char *s, int f, size_t sl, const char *fmt, ...) {
    va_list ap;
    if (sl == 0) { return -1; }
    va_start(ap, fmt);
    int r = vsnprintf(s, sl, fmt, ap);
    va_end(ap);
    if (r >= 0 && (size_t)r >= sl) {
        zan_fortify_abort("sprintf", "format", (size_t)r + 1, sl);
    }
    return r;
}
int __vsprintf_chk(char *s, int f, size_t sl, const char *fmt, va_list ap) {
    if (sl == 0) { return -1; }
    int r = vsnprintf(s, sl, fmt, ap);
    if (r >= 0 && (size_t)r >= sl) {
        zan_fortify_abort("vsprintf", "format", (size_t)r + 1, sl);
    }
    return r;
}

/* printf/fprintf 族没有 ds/sl 约束，直接转发（它们往 FILE*/stdout 输出，
 * 不存在栈缓冲区溢出问题）。 */
int __printf_chk(int f, const char *fmt, ...) {
    va_list ap; int r; (void)f;
    va_start(ap, fmt); r = vprintf(fmt, ap); va_end(ap);
    return r;
}
int __vprintf_chk(int f, const char *fmt, va_list ap) {
    (void)f; return vprintf(fmt, ap);
}
int __fprintf_chk(FILE *o, int f, const char *fmt, ...) {
    va_list ap; int r; (void)f;
    va_start(ap, fmt); r = vfprintf(o, fmt, ap); va_end(ap);
    return r;
}
int __vfprintf_chk(FILE *o, int f, const char *fmt, va_list ap) {
    (void)f; return vfprintf(o, fmt, ap);
}

/* === 其他 glibc fortify 接口 === */

ssize_t __read_chk(int fd, void *buf, size_t n, size_t bs) {
    if (n > bs) zan_fortify_abort("read", "read", n, bs);
    return read(fd, buf, n);
}

unsigned long __fdelt_chk(unsigned long d) {
    /* 这个接口参数 d 是 fd 值，不是缓冲区大小。glibc 原版做了范围检查
     * （d >= FD_SETSIZE 时 abort），这里保持同行为。 */
#define ZAN_FD_SETSIZE 1024
    if (d >= ZAN_FD_SETSIZE) {
        fprintf(stderr, "zan_fortify: fdelt %lu exceeds FD_SETSIZE\n", d);
        fflush(stderr);
        abort();
    }
#undef ZAN_FD_SETSIZE
    return d >> 6;
}

/* === C99 / C23 兼容接口 === */

int __isoc99_sscanf(const char *s, const char *fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vsscanf(s, fmt, ap); va_end(ap);
    return r;
}
int __isoc23_sscanf(const char *s, const char *fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vsscanf(s, fmt, ap); va_end(ap);
    return r;
}
int __isoc99_vsscanf(const char *s, const char *fmt, va_list ap) {
    return vsscanf(s, fmt, ap);
}
long __isoc23_strtol(const char *n, char **e, int b) {
    return strtol(n, e, b);
}
unsigned long __isoc23_strtoul(const char *n, char **e, int b) {
    return strtoul(n, e, b);
}
long long __isoc23_strtoll(const char *n, char **e, int b) {
    return strtoll(n, e, b);
}
unsigned long long __isoc23_strtoull(const char *n, char **e, int b) {
    return strtoull(n, e, b);
}
