/* 模块核心语义抽象与接口调用契约 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* 模块核心语义抽象与接口调用契约 */
void *__wrap_malloc(size_t n);
void __wrap_free(void *p);
void *__wrap_calloc(size_t n, size_t m);

/* 模块核心语义抽象与接口调用契约 */
#include "src/runtime/rt_timer.h"

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

/* 模块核心语义抽象与接口调用契约 */
static void expect_abort(void (*child_fn)(void), const char *needle) {
    int fds[2];
    CHECK(pipe(fds) == 0, "pipe");
    if (failures) return;
    pid_t pid = fork();
    CHECK(pid >= 0, "fork");
    if (pid < 0) { close(fds[0]); close(fds[1]); return; }
    if (pid == 0) {
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]); close(fds[1]);
        child_fn();
        _exit(3);                 /* 底层系统交互与数据协议契约 */
    }
    close(fds[1]);
    char buf[4096];
    size_t n = 0;
    ssize_t r;
    while (n < sizeof(buf) - 1 &&
           (r = read(fds[0], buf + n, sizeof(buf) - 1 - n)) > 0)
        n += (size_t)r;
    close(fds[0]);
    buf[n] = '\0';
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
          "double free must abort (SIGABRT)");
    CHECK(strstr(buf, needle) != NULL, "abort message must name the failure");
}

/* 模块核心语义抽象与接口调用契约 */
static void expect_exit(void (*child_fn)(void), int code,
                        const char *needle) {
    int fds[2];
    CHECK(pipe(fds) == 0, "pipe");
    if (failures) return;
    pid_t pid = fork();
    CHECK(pid >= 0, "fork");
    if (pid < 0) { close(fds[0]); close(fds[1]); return; }
    if (pid == 0) {
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]); close(fds[1]);
        child_fn();
        _exit(3);                 /* 底层系统交互与数据协议契约 */
    }
    close(fds[1]);
    char buf[4096];
    size_t n = 0;
    ssize_t r;
    while (n < sizeof(buf) - 1 &&
           (r = read(fds[0], buf + n, sizeof(buf) - 1 - n)) > 0)
        n += (size_t)r;
    close(fds[0]);
    buf[n] = '\0';
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == code,
          "fatal handler must take over and exit() with its own code");
    CHECK(strstr(buf, needle) != NULL, "handler must see category and message");
}

static void double_free_child(void) {
    void *p = __wrap_malloc(64);
    if (!p) _exit(2);
    __wrap_free(p);
    __wrap_free(p);               /* 核心系统底层抽象与内存语义契约 */
}

static void corrupt_header_child(void) {
    /* 模块核心语义抽象与接口调用契约 */
    void *p = __wrap_malloc(32);
    if (!p) _exit(2);
    unsigned char *hdr = (unsigned char *)p - 16;
    hdr[7] = (unsigned char)0xff; /* 底层系统交互与数据协议契约 */
    __wrap_free(p);               /* 核心系统底层抽象与内存语义契约 */
}

/* 模块核心语义抽象与接口调用契约 */

static void takeover_handler(const char *category, const char *message) {
    /* 模块核心语义抽象与接口调用契约 */
    fprintf(stderr, "fatal-handler-saw: %s: %s\n", category, message);
    fflush(stderr);
    _exit(71);
}

static void takeover_child(void) {
    zan_rt_set_fatal_handler(takeover_handler);
    void *p = __wrap_malloc(64);
    if (!p) _exit(2);
    __wrap_free(p);
    __wrap_free(p);               /* 底层系统交互与数据协议契约 */
    _exit(3);
}

static void returning_handler(const char *category, const char *message) {
    (void)category; (void)message;
    /* 模块核心语义抽象与接口调用契约 */
}

static void returning_handler_child(void) {
    zan_rt_set_fatal_handler(returning_handler);
    void *p = __wrap_malloc(64);
    if (!p) _exit(2);
    __wrap_free(p);
    __wrap_free(p);               /* 核心系统底层抽象与内存语义契约 */
    _exit(3);
}

int main(void) {
    /* 模块核心语义抽象与接口调用契约 */
    void *p = __wrap_malloc(64);
    CHECK(p != NULL, "malloc small");
    __wrap_free(p);
    void *q = __wrap_malloc(64);
    CHECK(q != NULL, "malloc reuse after free");
    __wrap_free(q);
    void *z = __wrap_calloc(16, 8);
    CHECK(z != NULL, "calloc small");
    __wrap_free(z);
    void *big = __wrap_malloc(3000);
    CHECK(big != NULL, "malloc large (libc fallthrough)");
    __wrap_free(big);

    /* 模块核心语义抽象与接口调用契约 */
    expect_abort(double_free_child, "double free");
    expect_abort(corrupt_header_child, "corrupt block header");

    /* 模块核心语义抽象与接口调用契约 */
    expect_exit(takeover_child, 71, "fatal-handler-saw: mem: double free");
    expect_abort(returning_handler_child, "double free");

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("rt_mem double-free guard: all checks passed\n");
    return 0;
}
