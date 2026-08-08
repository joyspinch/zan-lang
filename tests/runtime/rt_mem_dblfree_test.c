/* rt_mem_dblfree_test.c -- the allocator's double-free guard (rt_mem.c).
 *
 * Every produced Zan program links the size-class allocator with
 * `ld --wrap=malloc,free,calloc,realloc`. A freed block keeps its 16-byte
 * allocator header and is marked ZAN_MEM_FREED until it is re-allocated, so
 * freeing the same block twice -- a memory-aliasing bug under ARC -- aborts
 * with a clear message instead of handing the same address to two owners.
 *
 * The slab path is mmap-based, so it only exists on POSIX (on Windows the
 * allocator falls through to the libc heap and the guard never runs); this
 * file is therefore POSIX-only and must be linked with the same --wrap flags:
 *
 * Build (POSIX):
 *   cc -O2 -Wall -Wextra tests/runtime/rt_mem_dblfree_test.c \
 *        src/runtime/rt_mem.c \
 *        -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc \
 *        -o rt_mem_dblfree_test
 *
 * Exit code 0 = all checks passed, non-zero = at least one failure.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* The wrapped entry points, declared here so the test talks to the allocator
 * the same way produced code does (the standard headers still resolve the
 * unwrapped names for everything else in this file). */
void *__wrap_malloc(size_t n);
void __wrap_free(void *p);
void *__wrap_calloc(size_t n, size_t m);

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

/* Run `child_fn` in a forked child with its stderr captured, and require the
 * child to die on SIGABRT with `needle` on stderr. */
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
        _exit(3);                 /* reached only if the guard did not fire */
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

static void double_free_child(void) {
    void *p = __wrap_malloc(64);
    if (!p) _exit(2);
    __wrap_free(p);
    __wrap_free(p);               /* must abort here */
}

static void corrupt_header_child(void) {
    /* A block whose header class is out of range is not a block start the
     * allocator knows: it must refuse to trust the header, not dereference a
     * free list indexed by garbage. */
    void *p = __wrap_malloc(32);
    if (!p) _exit(2);
    unsigned char *hdr = (unsigned char *)p - 16;
    hdr[7] = (unsigned char)0xff; /* cls = 0xffff..., past ZAN_MEM_NCLASS */
    __wrap_free(p);               /* must abort here */
}

int main(void) {
    /* Sanity: the allocator works and legal free/reuse cycles are not
     * flagged. malloc(64) lands in a slab; calloc and a >2048-byte block
     * exercise the fallthrough paths. */
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

    /* The guards themselves: each runs in a child so the abort does not take
     * down the test runner. */
    expect_abort(double_free_child, "double free");
    expect_abort(corrupt_header_child, "corrupt block header");

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("rt_mem double-free guard: all checks passed\n");
    return 0;
}
