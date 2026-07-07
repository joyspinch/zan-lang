/* rt_test.c -- cross-platform unit tests for the Zan async runtime.
 *
 * Exercises the coroutine scheduler (rt_sched) and the async-IO abstraction
 * (rt_io: epoll on Linux, kqueue on macOS, IOCP on Windows, select fallback)
 * without any dependency on LLVM or the compiler, so it builds and runs fast
 * on every platform's CI runner.
 *
 * Build (POSIX):
 *   cc -O2 -Wall -Wextra -I. tests/runtime/rt_test.c \
 *        src/runtime/rt_sched.c src/runtime/rt_io.c -o rt_test -lpthread
 * Build (Windows, MinGW):
 *   gcc -O2 -Wall -Wextra -I. tests/runtime/rt_test.c \
 *        src/runtime/rt_sched.c src/runtime/rt_io.c -o rt_test.exe -lws2_32 -lmswsock
 *
 * Exit code 0 = all checks passed, non-zero = at least one failure.
 */

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#include "src/runtime/rt_sched.h"
#include "src/runtime/rt_io.h"
#include "src/runtime/rt_co.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define BAD_SOCK       INVALID_SOCKET
  #define closesock(s)   closesocket(s)
  #define sock_errno()   WSAGetLastError()
  #define WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK)
  #define SEND_FLAGS     0
  typedef HANDLE thread_t;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <errno.h>
  #include <signal.h>
  #include <pthread.h>
  #include <time.h>
  typedef int sock_t;
  #define BAD_SOCK       (-1)
  #define closesock(s)   close(s)
  #define sock_errno()   errno
  #define WOULD_BLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
  #ifdef MSG_NOSIGNAL
    #define SEND_FLAGS   MSG_NOSIGNAL
  #else
    #define SEND_FLAGS   0
  #endif
  typedef pthread_t thread_t;
#endif

/* ================= tiny assertion framework ================= */

static int g_checks = 0;
static int g_fails  = 0;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_fails++;                                                       \
            fprintf(stderr, "  [FAIL] %s:%d: ", __FILE__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                    \
            fprintf(stderr, "\n");                                           \
        }                                                                    \
    } while (0)

static void banner(const char *name) { printf("[test] %s\n", name); }

/* ================= portable helpers ================= */

static int64_t now_ms(void) {
#if defined(_WIN32)
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static sock_t make_listener(int port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCK) return BAD_SOCK;
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0) { closesock(s); return BAD_SOCK; }
    if (listen(s, 128) != 0) { closesock(s); return BAD_SOCK; }
    return s;
}

/* ================= threaded echo server ================= */

static sock_t g_listen = BAD_SOCK;

#if defined(_WIN32)
static DWORD WINAPI handler_thread(LPVOID arg) {
#else
static void *handler_thread(void *arg) {
#endif
    sock_t c = (sock_t)(intptr_t)arg;
    char buf[8192];
    for (;;) {
        int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        int off = 0;
        while (off < n) {
            int m = send(c, buf + off, n - off, SEND_FLAGS);
            if (m <= 0) { off = n; break; }
            off += m;
        }
    }
    closesock(c);
    return 0;
}

static void spawn_detached(
#if defined(_WIN32)
    LPTHREAD_START_ROUTINE fn,
#else
    void *(*fn)(void *),
#endif
    void *arg) {
#if defined(_WIN32)
    HANDLE h = CreateThread(NULL, 0, fn, arg, 0, NULL);
    if (h) CloseHandle(h);
#else
    pthread_t th;
    if (pthread_create(&th, NULL, fn, arg) == 0) pthread_detach(th);
#endif
}

#if defined(_WIN32)
static DWORD WINAPI server_thread(LPVOID arg) {
#else
static void *server_thread(void *arg) {
#endif
    (void)arg;
    for (;;) {
        sock_t c = accept(g_listen, NULL, NULL);
        if (c == BAD_SOCK) break;
        spawn_detached(handler_thread, (void *)(intptr_t)c);
    }
    return 0;
}

/* ================= scheduler tests ================= */

/* worker returns arg*arg after yielding a few times. */
static void square_body(zan_task_t *t) {
    int64_t n = (int64_t)(intptr_t)zan_task_arg(t);
    for (int i = 0; i < 3; i++) zan_task_yield();
    zan_task_return(t, n * n);
}

static int64_t g_sched_ok = 0;
static void sched_main(zan_task_t *t) {
    zan_task_t *a = zan_spawn(square_body, (void *)(intptr_t)7);
    zan_task_t *b = zan_spawn(square_body, (void *)(intptr_t)11);
    int64_t ra = zan_task_await(a);
    int64_t rb = zan_task_await(b);
    /* result readable again after completion */
    int64_t ra2 = zan_task_result(a);
    g_sched_ok = (ra == 49 && rb == 121 && ra2 == 49) ? 1 : 0;
    zan_task_return(t, 0);
}

static void test_scheduler(void) {
    banner("scheduler: spawn / yield / await / result");
    zan_sched_init();
    zan_spawn(sched_main, NULL);
    zan_sched_run();
    zan_sched_shutdown();
    CHECK(g_sched_ok == 1, "expected 7*7=49 and 11*11=121 from awaited coroutines");
}

/* ================= timer test ================= */

static int64_t g_delay_elapsed = -1;
static void delay_body(zan_task_t *t) {
    int64_t t0 = now_ms();
    zan_task_await(zan_task_delay(80));
    g_delay_elapsed = now_ms() - t0;
    zan_task_return(t, 0);
}

static void test_timer(void) {
    banner("scheduler: task_delay honors wall-clock time");
    zan_sched_init();
    zan_spawn(delay_body, NULL);
    zan_sched_run();
    zan_sched_shutdown();
    /* Allow slack for timer granularity/scheduling, but it must actually wait. */
    CHECK(g_delay_elapsed >= 60, "delay(80ms) only waited %lldms", (long long)g_delay_elapsed);
    CHECK(g_delay_elapsed < 2000, "delay(80ms) took way too long: %lldms", (long long)g_delay_elapsed);
}

/* ================= async IO: single echo ================= */

static int64_t g_echo_bytes = -1;
static void echo_body(zan_task_t *t) {
    const char *msg = "hello-zan-async-io";
    int len = (int)strlen(msg);
    char rbuf[64];
    long total = 0;

    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCK) { zan_task_return(t, -1); return; }
    if (zan_io_connect((int64_t)s, "127.0.0.1", (int)(intptr_t)zan_task_arg(t)) != 0) {
        closesock(s); zan_task_return(t, -2); return;
    }
    zan_io_set_nonblocking((int64_t)s);

    for (int rep = 0; rep < 5; rep++) {
        int sent = 0;
        while (sent < len) {
            int n = send(s, msg + sent, len - sent, SEND_FLAGS);
            if (n > 0) { sent += n; continue; }
            if (WOULD_BLOCK(sock_errno())) { zan_io_wait_writable((int64_t)s); continue; }
            goto done;
        }
        int got = 0;
        while (got < len) {
            int n = recv(s, rbuf + got, len - got, 0);
            if (n > 0) { got += n; total += n; continue; }
            if (n == 0) goto done;
            if (WOULD_BLOCK(sock_errno())) { zan_io_wait_readable((int64_t)s); continue; }
            goto done;
        }
        if (memcmp(rbuf, msg, len) != 0) { total = -3; goto done; }
    }
done:
    closesock(s);
    g_echo_bytes = total;
    zan_task_return(t, total);
}

static void test_io_echo(int port) {
    banner("async-io: connect + echo round-trips (readiness ABI)");
    zan_sched_init();
    g_listen = make_listener(port);
    CHECK(g_listen != BAD_SOCK, "could not bind echo server on port %d", port);
    if (g_listen == BAD_SOCK) { zan_sched_shutdown(); return; }
    thread_t srv;
#if defined(_WIN32)
    srv = CreateThread(NULL, 0, server_thread, NULL, 0, NULL); (void)srv;
#else
    pthread_create(&srv, NULL, server_thread, NULL); (void)srv;
#endif
    zan_spawn(echo_body, (void *)(intptr_t)port);
    zan_sched_run();
    closesock(g_listen); g_listen = BAD_SOCK;
    zan_sched_shutdown();
    CHECK(g_echo_bytes == 5 * 18, "echo returned %lld bytes, expected 90",
          (long long)g_echo_bytes);
}

/* ================= async IO: many concurrent connections ================= */

#define CONC_N 128
static int g_conc_port = 0;
static int g_conc_ok = 0;

static void conc_client(zan_task_t *t) {
    char sbuf[32], rbuf[32];
    memset(sbuf, 'Q', sizeof(sbuf));
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCK) { zan_task_return(t, 0); return; }
    if (zan_io_connect((int64_t)s, "127.0.0.1", g_conc_port) != 0) {
        closesock(s); zan_task_return(t, 0); return;
    }
    zan_io_set_nonblocking((int64_t)s);
    int len = (int)sizeof(sbuf), sent = 0, got = 0;
    while (sent < len) {
        int n = send(s, sbuf + sent, len - sent, SEND_FLAGS);
        if (n > 0) { sent += n; continue; }
        if (WOULD_BLOCK(sock_errno())) { zan_io_wait_writable((int64_t)s); continue; }
        closesock(s); zan_task_return(t, 0); return;
    }
    while (got < len) {
        int n = recv(s, rbuf + got, len - got, 0);
        if (n > 0) { got += n; continue; }
        if (n == 0) break;
        if (WOULD_BLOCK(sock_errno())) { zan_io_wait_readable((int64_t)s); continue; }
        break;
    }
    closesock(s);
    zan_task_return(t, (got == len && memcmp(sbuf, rbuf, len) == 0) ? 1 : 0);
}

static void conc_main(zan_task_t *t) {
    zan_task_t *ts[CONC_N];
    for (int i = 0; i < CONC_N; i++) ts[i] = zan_spawn(conc_client, NULL);
    int ok = 0;
    for (int i = 0; i < CONC_N; i++) ok += (int)zan_task_await(ts[i]);
    g_conc_ok = ok;
    zan_task_return(t, 0);
}

static void test_io_concurrent(int port) {
    banner("async-io: 128 concurrent connections on one thread");
    g_conc_port = port;
    zan_sched_init();
    g_listen = make_listener(port);
    CHECK(g_listen != BAD_SOCK, "could not bind echo server on port %d", port);
    if (g_listen == BAD_SOCK) { zan_sched_shutdown(); return; }
    thread_t srv;
#if defined(_WIN32)
    srv = CreateThread(NULL, 0, server_thread, NULL, 0, NULL); (void)srv;
#else
    pthread_create(&srv, NULL, server_thread, NULL); (void)srv;
#endif
    zan_spawn(conc_main, NULL);
    zan_sched_run();
    closesock(g_listen); g_listen = BAD_SOCK;
    zan_sched_shutdown();
    CHECK(g_conc_ok == CONC_N, "only %d/%d concurrent connections echoed", g_conc_ok, CONC_N);
}

/* ================= async IO: connect refused (error path) ================= */

static int64_t g_refused_rc = 0;
static void refused_body(zan_task_t *t) {
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCK) { zan_task_return(t, 0); return; }
    /* Port with nothing listening -> connect must fail, not hang. */
    int64_t rc = zan_io_connect((int64_t)s, "127.0.0.1", (int)(intptr_t)zan_task_arg(t));
    closesock(s);
    g_refused_rc = rc;
    zan_task_return(t, rc);
}

static void test_connect_refused(int port) {
    banner("async-io: connect to closed port fails gracefully");
    zan_sched_init();
    zan_spawn(refused_body, (void *)(intptr_t)port);
    zan_sched_run();
    zan_sched_shutdown();
    CHECK(g_refused_rc == -1, "connect to unused port returned %lld, expected -1",
          (long long)g_refused_rc);
}

/* ================= main ================= */

/* ================= stackless-coroutine state-machine driver =================
 *
 * Hand-written frames + step functions that mirror EXACTLY what the compiler's
 * CPS lowering emits (see docs/ASYNC_CPS_DESIGN.md). This validates the rt_co
 * ready-queue and the await/complete handshake independently of codegen, so the
 * runtime ABI is proven before irgen targets it.
 *
 * Models:
 *   async int Add(int a, int b)  { return a + b; }              // no await
 *   async int Compute(int x)     { int r = await Add(x, x);     // chained
 *                                  return await Add(r, 100); }
 */

/* Fixed frame header the await protocol relies on (offsets known to codegen). */
typedef struct co_hdr {
    int32_t       state;        /* 0 start, k resume point, -1 done */
    int32_t       done;         /* 1 once result is valid */
    void         *awaiter;      /* frame blocked on this one */
    zan_co_step_t awaiter_step; /* awaiter's resume fn */
    int64_t       result;       /* return value slot */
} co_hdr_t;

typedef struct { co_hdr_t h; int64_t a, b; } add_frame_t;
typedef struct { co_hdr_t h; int64_t x, r; void *sub; } compute_frame_t;

static void add_resume(void *fp);
static void compute_resume(void *fp);

/* Complete `self`: publish result and re-schedule whoever awaited it. */
static void co_complete(co_hdr_t *self, int64_t result) {
    self->result = result;
    self->done   = 1;
    self->state  = -1;
    if (self->awaiter) zan_co_ready(self->awaiter, self->awaiter_step);
}

/* Ramp: allocate + init a frame, but do not run the body yet. */
static void *add_ramp(int64_t a, int64_t b) {
    add_frame_t *f = (add_frame_t *)calloc(1, sizeof(*f));
    f->a = a; f->b = b;
    return f;
}
static void add_resume(void *fp) {
    add_frame_t *f = (add_frame_t *)fp;
    switch (f->h.state) {
    case 0:
        co_complete(&f->h, f->a + f->b);
        return;
    default:
        return;
    }
}

static void *compute_ramp(int64_t x) {
    compute_frame_t *f = (compute_frame_t *)calloc(1, sizeof(*f));
    f->x = x;
    return f;
}
static void compute_resume(void *fp) {
    compute_frame_t *f = (compute_frame_t *)fp;
    switch (f->h.state) {
    case 0: {
        /* r = await Add(x, x); */
        void *sub = add_ramp(f->x, f->x);
        co_hdr_t *sh = (co_hdr_t *)sub;
        sh->awaiter = f; sh->awaiter_step = compute_resume;
        f->sub = sub;
        zan_co_ready(sub, add_resume);
        f->h.state = 1;
        return;                     /* SUSPEND */
    }
    case 1: {
        add_frame_t *sub = (add_frame_t *)f->sub;
        f->r = sub->h.result;
        free(sub);
        /* return await Add(r, 100); */
        void *sub2 = add_ramp(f->r, 100);
        co_hdr_t *sh = (co_hdr_t *)sub2;
        sh->awaiter = f; sh->awaiter_step = compute_resume;
        f->sub = sub2;
        zan_co_ready(sub2, add_resume);
        f->h.state = 2;
        return;                     /* SUSPEND */
    }
    case 2: {
        add_frame_t *sub = (add_frame_t *)f->sub;
        int64_t r2 = sub->h.result;
        free(sub);
        co_complete(&f->h, r2);
        return;
    }
    default:
        return;
    }
}

static void test_co_statemachine(void) {
    banner("co state machine (chained await)");
    zan_co_sched_init();
    compute_frame_t *root = (compute_frame_t *)compute_ramp(21);
    zan_co_ready(root, compute_resume);
    zan_co_sched_run();
    CHECK(root->h.done == 1, "root not completed");
    CHECK(root->h.result == 142, "expected 142 (21+21 then +100), got %lld",
          (long long)root->h.result);
    CHECK(zan_co_pending() == 0, "ready queue not drained: %zu", zan_co_pending());
    free(root);
}

int main(void) {
#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif
    printf("=== Zan runtime test suite ===\n");

    test_scheduler();
    test_timer();
    test_co_statemachine();
    test_io_echo(19801);
    test_io_concurrent(19802);
    test_connect_refused(19899);

    printf("--- %d checks, %d failures ---\n", g_checks, g_fails);
    printf("=== %s ===\n", g_fails == 0 ? "PASS" : "FAIL");
    return g_fails == 0 ? 0 : 1;
}
