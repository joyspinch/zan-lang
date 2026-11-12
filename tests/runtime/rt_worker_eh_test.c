/* Link with the ZAN_CO_DRIVER rt_io.c flavor, rt_timer.c and rt_crash.c.
 * The strong definition below stands in for compiler-emitted EH teardown.
 * Foreground runs join every owned thread, so counts require no sleeps. */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/runtime/rt_co.h"

static volatile int releases;
static volatile int caller_releases;
static __thread int caller_thread;

void __zan_eh_release(void) {
    __atomic_add_fetch(&releases, 1, __ATOMIC_RELAXED);
    if (caller_thread)
        __atomic_add_fetch(&caller_releases, 1, __ATOMIC_RELAXED);
}

int main(void) {
#ifdef _WIN32
    _putenv_s("ZAN_CO_WORKERS", "4");
#else
    setenv("ZAN_CO_WORKERS", "4", 1);
#endif
    caller_thread = 1;
    zan_co_sched_init();
    for (int round = 1; round <= 2; round++) {
        zan_co_sched_run();
        int n = __atomic_load_n(&releases, __ATOMIC_RELAXED);
        if (n != 3 * round || caller_releases != 0) {
            fprintf(stderr, "FAIL round=%d owned=%d expected=%d caller=%d\n",
                    round, n, 3 * round, caller_releases);
            return 1;
        }
    }
    puts("PASS native workers release EH once; foreground caller preserves EH");
    return 0;
}
