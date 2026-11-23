/*
 * exports.h —— {{NAME}} 共享库的 C 头文件。
 * 与 src/lib.zan 的 public static 方法一一对应；增删导出时两边同步改。
 *
 * 链接：
 *   Windows  {{NAME}}.dll（MSVC 需配套导入库，或运行期 LoadLibrary/GetProcAddress）
 *   Linux    lib{{NAME}}.so    cc app.c -L. -l{{NAME}} -Wl,-rpath,'$ORIGIN'
 *   macOS    lib{{NAME}}.dylib cc app.c -L. -l{{NAME}} -Wl,-rpath,@loader_path
 */
#ifndef {{NAME}}_H
#define {{NAME}}_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 版本号：主*10000 + 次*100 + 修订（1.2.0 → 10200）。 */
int32_t {{NAME}}_Version(void);

int32_t {{NAME}}_Add(int32_t a, int32_t b);
int32_t {{NAME}}_Clamp(int32_t value, int32_t lo, int32_t hi);
double  {{NAME}}_Lerp(double a, double b, double t);
double  {{NAME}}_Distance(double x1, double y1, double x2, double y2);
bool    {{NAME}}_IsPrime(int32_t n);

/* n 超出 [0, 92] 返回 -1。 */
int64_t {{NAME}}_Fibonacci(int32_t n);

/* 除数为 0 时返回 {{NAME}}_ErrorValue()（INT32_MIN）。 */
int32_t {{NAME}}_SafeDivide(int32_t a, int32_t b);
int32_t {{NAME}}_ErrorValue(void);

#ifdef __cplusplus
}
#endif

#endif /* {{NAME}}_H */
