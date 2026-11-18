/* Link with await_extern_dispatch.zan and a runtime built from this translation unit. */
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#include <stddef.h>

/* Fail only the next job allocation. Reactor setup should run normally. */
static int fail_next, native_calls;
static void *audit_calloc(size_t size);
#define ZAN_BLOCKING_JOB_CALLOC audit_calloc
#include "../../src/runtime/rt_io.c"

static void *audit_calloc(size_t size) {
    if (fail_next) {
        fail_next = 0;
        return NULL;
    }
    return calloc(1, size);
}

void audit_dispatch_fail_next(void) { fail_next = 1; }
int audit_dispatch_failure_pending(void) { return fail_next; }
int audit_native_calls(void) { return native_calls; }
int audit_native_int(int value) { native_calls++; return value; }
intptr_t audit_native_handle(void) { native_calls++; return (intptr_t)4294967297LL; }
void audit_native_void(void) { native_calls++; }
