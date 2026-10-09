/* 底层系统交互与数据协议契约 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

void *__memcpy_chk(void *d, const void *s, size_t n, size_t ds) {
    (void)ds; return memcpy(d, s, n);
}
void *__memmove_chk(void *d, const void *s, size_t n, size_t ds) {
    (void)ds; return memmove(d, s, n);
}
void *__memset_chk(void *d, int c, size_t n, size_t ds) {
    (void)ds; return memset(d, c, n);
}
char *__strcpy_chk(char *d, const char *s, size_t ds) {
    (void)ds; return strcpy(d, s);
}
char *__strncpy_chk(char *d, const char *s, size_t n, size_t ds) {
    (void)ds; return strncpy(d, s, n);
}
char *__strcat_chk(char *d, const char *s, size_t ds) {
    (void)ds; return strcat(d, s);
}
char *__strncat_chk(char *d, const char *s, size_t n, size_t ds) {
    (void)ds; return strncat(d, s, n);
}
char *__stpcpy_chk(char *d, const char *s, size_t ds) {
    (void)ds; return stpcpy(d, s);
}
int __snprintf_chk(char *s, size_t n, int f, size_t sl, const char *fmt, ...) {
    va_list ap; int r; (void)f; (void)sl;
    va_start(ap, fmt); r = vsnprintf(s, n, fmt, ap); va_end(ap);
    return r;
}
int __vsnprintf_chk(char *s, size_t n, int f, size_t sl, const char *fmt, va_list ap) {
    (void)f; (void)sl; return vsnprintf(s, n, fmt, ap);
}
int __sprintf_chk(char *s, int f, size_t sl, const char *fmt, ...) {
    va_list ap; int r; (void)f; (void)sl;
    va_start(ap, fmt); r = vsprintf(s, fmt, ap); va_end(ap);
    return r;
}
int __vsprintf_chk(char *s, int f, size_t sl, const char *fmt, va_list ap) {
    (void)f; (void)sl; return vsprintf(s, fmt, ap);
}
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
ssize_t __read_chk(int fd, void *buf, size_t n, size_t bs) {
    (void)bs; return read(fd, buf, n);
}
/* 底层系统交互与数据协议契约 */
unsigned long __fdelt_chk(unsigned long d) {
    return d >> 6;
}
/* 底层系统交互与数据协议契约 */
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
