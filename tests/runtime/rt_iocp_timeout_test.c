/* Deterministic Windows IOCP lifetime test. Compile this TU instead of
 * rt_io.c, with rt_timer.c and rt_crash.c; link ws2_32. */
#define _WIN32_WINNT 0x0601
#define ZAN_CO_DRIVER
#define ZAN_IO_STACKLESS_ONLY
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static int cancel_calls, submit_calls, cancel_error;
static OVERLAPPED *submitted;
static OVERLAPPED *completion;
static OVERLAPPED *completion_batch[4];
static ULONG completion_count;
static void check_submission_lock(void);
static void check_cancel_lock(void);
static int WSAAPI test_recv(SOCKET s, LPWSABUF bufs, DWORD count,
    LPDWORD got, LPDWORD flags, LPWSAOVERLAPPED ov,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE fn) {
    (void)s; (void)bufs; (void)count; (void)got; (void)flags; (void)fn;
    submitted = ov;
    submit_calls++;
    check_submission_lock();
    WSASetLastError(WSA_IO_PENDING);
    return SOCKET_ERROR;
}
static BOOL WINAPI test_cancel(HANDLE h, LPOVERLAPPED ov) {
    (void)h;
    if (ov != submitted) abort();
    cancel_calls++;
    check_cancel_lock();
    SetLastError(cancel_error);
    return cancel_error ? FALSE : TRUE;
}
static BOOL WINAPI test_dequeue(HANDLE h, LPOVERLAPPED_ENTRY entries,
    ULONG cap, PULONG removed, DWORD timeout, BOOL alertable) {
    (void)h; (void)cap; (void)timeout; (void)alertable;
    if (completion_count) {
        *removed = completion_count;
        for (ULONG i = 0; i < completion_count; i++) {
            entries[i].lpOverlapped = completion_batch[i];
            entries[i].lpCompletionKey = 0;
            entries[i].dwNumberOfBytesTransferred = 7;
            entries[i].Internal = 0;
        }
        completion_count = 0;
        return TRUE;
    }
    if (!completion) { *removed = 0; SetLastError(WAIT_TIMEOUT); return FALSE; }
    entries[0].lpOverlapped = completion;
    entries[0].lpCompletionKey = 0;
    entries[0].dwNumberOfBytesTransferred = 7;
    entries[0].Internal = 0;
    *removed = 1;
    completion = NULL;
    return TRUE;
}
#include "rt_io_owner_checks.h"
static void owner_count_release(volatile LONG *counter);
static void check_completion_handoff(volatile LONG *counter);
static LONG test_decrement(volatile LONG *counter) {
    LONG result = InterlockedDecrement(counter);
    check_completion_handoff(counter);
    owner_count_release(counter);
    return result;
}
#define WSARecv test_recv
#define CancelIoEx test_cancel
#define GetQueuedCompletionStatusEx test_dequeue
#undef InterlockedDecrement
#define InterlockedDecrement test_decrement
#include "../../src/runtime/rt_io.c"
#undef WSARecv
#undef CancelIoEx
#undef GetQueuedCompletionStatusEx
#undef InterlockedDecrement

static int failures, lock_checks;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); failures++; \
} } while (0)

/* A second thread tries the lock while submission/cancellation is paused
 * in the fake OS call. Events/joins enforce the order; no timing sleeps. */
static DWORD WINAPI try_lock(LPVOID arg) {
    int *acquired = arg;
    *acquired = TryEnterCriticalSection(&g_rto_lock) != 0;
    if (*acquired) LeaveCriticalSection(&g_rto_lock);
    return 0;
}
static void check_lock(void) {
    int acquired = -1;
    HANDLE t = CreateThread(NULL, 0, try_lock, &acquired, 0, NULL);
    CHECK(t != NULL);
    if (!t) return;
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    CHECK(acquired == 0);
    lock_checks++;
}
static void check_submission_lock(void) { check_lock(); }
static void check_cancel_lock(void) { check_lock(); }

static int handoff_active, handoff_checks, resumed;
static DWORD WINAPI observe_idle(LPVOID arg) {
    *(int *)arg = co_all_idle();
    return 0;
}
static void check_completion_handoff(volatile LONG *counter) {
    if (!handoff_active || (counter != (volatile LONG *)&g_io_count &&
                            counter != (volatile LONG *)&g_blocking_inflight)) return;
    /* Pause the completion consumer exactly after releasing its pending IO
     * ownership. An independent worker must still see the continuation. */
    int idle = -1;
    HANDLE observer = CreateThread(NULL, 0, observe_idle, &idle, 0, NULL);
    CHECK(observer != NULL);
    if (!observer) return;
    CHECK(WaitForSingleObject(observer, 5000) == WAIT_OBJECT_0);
    CloseHandle(observer);
    CHECK(idle == 0);
    handoff_checks++;
}
#define ZAN_OWNER_CHECKS_IMPLEMENTATION
#include "rt_io_owner_checks.h"
static void resume_handoff(void *frame) { (void)frame; resumed++; }
static void run_handoff(int worker_path, int count) {
    char buf[4][32];
    int64_t out[4] = {1234, 1234, 1234, 1234};
    zan_co_fhdr frames[4] = {0};
    int initial_resumed = resumed;
    for (int i = 0; i < count; i++) {
        zan_io_recv_to_co((intptr_t)INVALID_SOCKET, buf[i], sizeof(buf[i]), 60000,
                          &frames[i], resume_handoff, &out[i]);
        completion_batch[i] = submitted;
    }
    completion_count = count;
    handoff_active = 1;
    if (worker_path) {
        g_co_parked = 1;
        co_wait_io(&g_wk[0], 0);
    } else {
        CHECK(zan_io_poll(0) == count);
    }
    handoff_active = 0;
    CHECK(g_io_count == 0);
    CHECK(co_cnt_outstanding() == count);
    for (int i = 0; i < count; i++) {
        CHECK(out[i] == 7);
        zan_co_task task;
        int found = co_next_task(&g_wk[0], &task);
        CHECK(found && task.step == resume_handoff);
        if (found) co_run(&g_wk[0], &task);
    }
    CHECK(resumed == initial_resumed + count);
    CHECK(co_all_idle());
}

static void run_dns_handoff(int timeout, int started) {
    zan_co_fhdr frame = {0};
    int64_t out = 1234;
    zan_blocking_job_t *job = calloc(1, sizeof(*job));
    job->frame = &frame;
    job->step = resume_handoff;
    job->out64 = &out;
    job->result = 17;
    job->deadline_ms = 1;
    job->started = started;
    g_blocking_inflight = 1;
    if (timeout) g_blocking_pending = job;
    else g_blocking_done = job;
    if (started) g_blocking_active = 1;
    handoff_active = 1;
    CHECK((timeout ? dns_timeout_scan() : dns_drain()) == 1);
    handoff_active = 0;
    CHECK(out == (timeout ? 0 : 17));
    CHECK(g_blocking_inflight == 0 && co_cnt_outstanding() == 1);
    if (started) {
        /* A timed-out worker may finish later, but timeout already released
         * its liveness owner. The late result must neither wake nor decrement. */
        blocking_finish_worker(job);
        CHECK(g_blocking_inflight == 0 && co_cnt_outstanding() == 1);
    }
    zan_co_task task;
    int found = co_next_task(&g_wk[0], &task);
    CHECK(found && task.frame == &frame && task.step == resume_handoff);
    if (found) co_run(&g_wk[0], &task);
    CHECK(co_all_idle());
}

static void run_case(int worker_path, int error, int expire) {
    char buf[32];
    int64_t out = 1234;
    zan_co_fhdr frame = {0};
    submitted = NULL;
    int cancels = cancel_calls;
    cancel_error = error;
    zan_io_recv_to_co((intptr_t)INVALID_SOCKET, buf, sizeof(buf), 60000,
                      &frame, NULL, &out);
    CHECK(submitted != NULL && g_io_count == 1 && g_rto != NULL);
    zan_io_op_t *op = CONTAINING_RECORD(submitted, zan_io_op_t, ov);
    if (expire) {
        EnterCriticalSection(&g_rto_lock);
        g_rto->due_ms = 0;
        LeaveCriticalSection(&g_rto_lock);
        CHECK(rto_timeout_scan() == 0);
        CHECK(cancel_calls == cancels + 1);
        CHECK(out == 1234); /* no early result publication or wake */
        CHECK(g_io_count == 1 && op->rto == 2 && g_rto == NULL);
        CHECK(rto_timeout_scan() == 0 && cancel_calls == cancels + 1);
    }
    completion = submitted;
    if (worker_path) {
        zan_co_worker_t *w = &g_wk[0];
        w->index = 0;
        g_co_parked = 1;
        co_wait_io(w, 0);
    } else {
        CHECK(zan_io_poll(0) == 1);
    }
    CHECK(out == (expire ? -1 : 7));
    CHECK(g_io_count == 0 && g_rto == NULL);
    CHECK(completion == NULL);
}
int main(void) {
    _putenv_s("ZAN_IO_SYNCFAST", "0");
    /* co_wait_io uses g_wk, allocated by scheduler init, not reactor init. */
    zan_co_sched_init();
    /* Own queue draining below; do not let ready publication auto-start a
     * background pool and race the observer or our stack-backed test frames. */
    InterlockedExchange(&g_co_pool_fg, 1);
    zan_io_init();
    /* Timeout wins; completion already queued / ERROR_NOT_FOUND still must
     * drain the completion before releasing the borrowed buffer. */
    for (int path = 0; path < 2; path++) {
        run_case(path, 0, 1);
        run_case(path, ERROR_NOT_FOUND, 1);
        run_case(path, 0, 0); /* completion wins and removes deadline */
    }
    CHECK(submit_calls == 6 && cancel_calls == 4 && lock_checks == 10);
    for (int path = 0; path < 2; path++) {
        run_handoff(path, 1);
        run_handoff(path, 4);
    }
    CHECK(handoff_checks == 10 && resumed == 10);
    run_dns_handoff(0, 0);
    run_dns_handoff(1, 0);
    run_dns_handoff(1, 1);
    CHECK(handoff_checks == 13 && resumed == 13);
    run_owner_checks();
    zan_io_shutdown();
    if (!failures) puts("PASS IOCP timeout ownership and completion handoff in both polling paths");
    return failures != 0;
}
