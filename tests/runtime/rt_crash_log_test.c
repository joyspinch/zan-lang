/* 崩溃兜底日志的体积上限回归。
 *
 * zan_crash.log 随每次进程启动/退出各追加一行，一轮全量门禁能写进几 MB，
 * 真崩的那条记录会淹没在噪声里。zan__crash_open_log() 在打开时查文件体积，
 * 超过 ZAN_CRASH_LOG_MAX_BYTES 就把旧文件轮转成 <name>.1，只保留最近一代：
 * 崩溃现场永远在当前文件里，历史不丢但有界。
 *
 * 轮转逻辑是 rt_crash.h 里的 static 函数，只能直接 include 该头文件来测。
 * 该头文件的文件日志路径只在 Windows 分支实现，POSIX 分支走 signal+管道，
 * 所以本用例只编 Windows。
 */
#if defined(_WIN32)

#include "rt_crash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(msg)                                                          \
    do {                                                                   \
        fprintf(stderr, "FAIL: %s\n", (msg));                              \
        return 1;                                                          \
    } while (0)

static const char *k_log = "rt_crash_log_test.log";

static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static int write_blob(const char *path, long n) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    for (long i = 0; i < n; i++) fputc('x', f);
    fclose(f);
    return 1;
}

int main(void) {
    char rotated[256];
    snprintf(rotated, sizeof rotated, "%s.1", k_log);
    remove(rotated);
    remove(k_log);

    /* 1) 未超限：不动，追加内容留在原文件里。 */
    if (!write_blob(k_log, 1024)) FAIL("cannot write small log");
    FILE *f = zan__crash_open_log(k_log);
    if (!f) FAIL("open failed on small log");
    fputs("small\n", f);
    fclose(f);
    if (file_size(rotated) >= 0) FAIL("small log must not rotate");
    if (file_size(k_log) != 1024 + 6) FAIL("small log content changed");

    /* 2) 超限：旧内容整体搬进 .1，当前文件清空重开。 */
    if (!write_blob(k_log, ZAN_CRASH_LOG_MAX_BYTES + 4096))
        FAIL("cannot write big log");
    long big = file_size(k_log);
    f = zan__crash_open_log(k_log);
    if (!f) FAIL("open failed on big log");
    fputs("after-rotation\n", f);
    fclose(f);
    if (file_size(rotated) != big) FAIL("rotated file must keep the old content");
    if (file_size(k_log) != 15) FAIL("current log must start empty after rotation");

    /* 3) 只保留一代：再超限时上一代 .1 被丢弃，换成新的。 */
    if (!write_blob(k_log, ZAN_CRASH_LOG_MAX_BYTES + 8192))
        FAIL("cannot rewrite big log");
    long bigger = file_size(k_log);
    f = zan__crash_open_log(k_log);
    if (!f) FAIL("open failed on second big log");
    fclose(f);
    if (file_size(rotated) != bigger)
        FAIL("only one generation is kept: stale .1 was not replaced");
    if (file_size(rotated) == big) FAIL("rotated file was not refreshed");

    remove(rotated);
    remove(k_log);
    printf("ok: crash log rotation\n");
    return 0;
}

#else /* !_WIN32 */

int main(void) {
    /* POSIX 分支不写 zan_crash.log 文件，无可测行为。 */
    printf("skip: crash log rotation is Windows-only\n");
    return 0;
}

#endif
