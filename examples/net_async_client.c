/* net_async_client.c -- multi-connection concurrent TCP client on the Zan
 * async runtime (rt_sched coroutines + rt_io: epoll on Linux, IOCP on Windows).
 *
 * Each client is a coroutine that connects to a loopback echo server and runs
 * a request/response ping-pong.  All connections are driven concurrently on a
 * single OS thread, multiplexed by the platform's scalable IO backend -- this
 * is the whole point of the async IO abstraction layer.
 *
 * A tiny threaded echo server runs in-process so the demo is self-contained.
 *
 * Build (Linux):
 *   cc -O2 -I. examples/net_async_client.c \
 *        src/runtime/rt_sched.c src/runtime/rt_io.c -o net_async_client -lpthread
 * Build (Windows, MinGW):
 *   gcc -O2 -I. examples/net_async_client.c \
 *        src/runtime/rt_sched.c src/runtime/rt_io.c -o net_async_client.exe -lws2_32 -lmswsock
 *
 * Usage: net_async_client [connections] [iterations] [msg_size] [port]
 */

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601   /* GetTickCount64, GetQueuedCompletionStatusEx */
#endif

#include "src/runtime/rt_sched.h"
#include "src/runtime/rt_io.h"

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
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <errno.h>
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

/* ---- configuration (overridable via argv) ---- */
static int g_conns    = 200;
static int g_iters    = 100;
static int g_msg_size = 64;
static int g_port     = 9099;

/* ---- results ---- */
static long g_total_bytes = 0;
static int  g_ok_conns    = 0;

/* ---- server state ---- */
static sock_t g_listen = BAD_SOCK;

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
    if (listen(s, 512) != 0) { closesock(s); return BAD_SOCK; }
    return s;
}

/* ================= echo server (blocking, thread-per-connection) ========= */

#if defined(_WIN32)
static DWORD WINAPI handler_thread(LPVOID arg) {
#else
static void *handler_thread(void *arg) {
#endif
    sock_t c = (sock_t)(intptr_t)arg;
    char buf[16384];
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
        if (c == BAD_SOCK) break;    /* listener closed -> shut down */
        spawn_detached(handler_thread, (void *)(intptr_t)c);
    }
    return 0;
}

/* ================= async client coroutine ================= */

static void client_body(zan_task_t *t) {
    long total = 0;
    int msg_size = g_msg_size;
    char *sbuf = (char *)malloc(msg_size);
    char *rbuf = (char *)malloc(msg_size);
    memset(sbuf, 'Z', msg_size);

    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCK) goto done_nosock;

    if (zan_io_connect((int64_t)s, "127.0.0.1", g_port) != 0) goto done;
    zan_io_set_nonblocking((int64_t)s);

    for (int i = 0; i < g_iters; i++) {
        int sent = 0;
        while (sent < msg_size) {
            int n = send(s, sbuf + sent, msg_size - sent, SEND_FLAGS);
            if (n > 0) { sent += n; continue; }
            if (WOULD_BLOCK(sock_errno())) { zan_io_wait_writable((int64_t)s); continue; }
            goto done;
        }
        int got = 0;
        while (got < msg_size) {
            int n = recv(s, rbuf, msg_size - got, 0);
            if (n > 0) { got += n; total += n; continue; }
            if (n == 0) goto done;   /* peer closed */
            if (WOULD_BLOCK(sock_errno())) { zan_io_wait_readable((int64_t)s); continue; }
            goto done;
        }
    }

done:
    closesock(s);
done_nosock:
    free(sbuf);
    free(rbuf);
    zan_task_return(t, total);
}

static void main_body(zan_task_t *t) {
    zan_task_t **tasks = (zan_task_t **)malloc((size_t)g_conns * sizeof(*tasks));
    for (int i = 0; i < g_conns; i++)
        tasks[i] = zan_spawn(client_body, (void *)(intptr_t)i);

    long total = 0;
    int ok = 0;
    for (int i = 0; i < g_conns; i++) {
        long r = (long)zan_task_await(tasks[i]);
        total += r;
        if (r == (long)g_msg_size * g_iters) ok++;
    }
    g_total_bytes = total;
    g_ok_conns = ok;
    free(tasks);
    zan_task_return(t, 0);
}

/* ================= entry point ================= */

int main(int argc, char **argv) {
    if (argc > 1) g_conns    = atoi(argv[1]);
    if (argc > 2) g_iters    = atoi(argv[2]);
    if (argc > 3) g_msg_size = atoi(argv[3]);
    if (argc > 4) g_port     = atoi(argv[4]);
    if (g_conns    <= 0) g_conns = 1;
    if (g_iters    <= 0) g_iters = 1;
    if (g_msg_size <= 0) g_msg_size = 1;

    printf("=== Zan async multi-connection client ===\n");
    printf("connections=%d iterations=%d msg_size=%d port=%d\n",
           g_conns, g_iters, g_msg_size, g_port);

    /* Bring up the coroutine + IO runtime first (WSAStartup happens here). */
    zan_sched_init();

    g_listen = make_listener(g_port);
    if (g_listen == BAD_SOCK) {
        printf("FAIL: could not start echo server on port %d\n", g_port);
        zan_sched_shutdown();
        return 1;
    }
    thread_t srv;
#if defined(_WIN32)
    srv = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
#else
    pthread_create(&srv, NULL, server_thread, NULL);
#endif

    int64_t t0 = now_ms();
    zan_spawn(main_body, NULL);
    zan_sched_run();
    int64_t elapsed = now_ms() - t0;

    /* All client coroutines have finished (zan_sched_run returned once no
     * coroutines remain).  The echo server + its per-connection handler
     * threads are background daemons; closing the listener releases the port
     * and process exit reaps them -- we intentionally do not join, since a
     * blocking accept() in another thread is not reliably interrupted by
     * close() across platforms. */
    closesock(g_listen);
    g_listen = BAD_SOCK;
#if defined(_WIN32)
    (void)srv;
#else
    (void)srv;
#endif

    zan_sched_shutdown();

    long expected = (long)g_conns * g_iters * g_msg_size;
    long total_msgs = (long)g_ok_conns * g_iters;
    printf("--- results ---\n");
    printf("  connections ok : %d / %d\n", g_ok_conns, g_conns);
    printf("  bytes echoed   : %ld / %ld\n", g_total_bytes, expected);
    printf("  elapsed_ms     : %lld\n", (long long)elapsed);
    if (elapsed > 0) {
        printf("  msgs/sec       : %lld\n", (long long)(total_msgs * 1000 / elapsed));
        printf("  MB/sec (r+w)   : %lld\n",
               (long long)(g_total_bytes * 2 / 1024 / 1024 * 1000 / elapsed));
    }
    printf("=== %s ===\n",
           (g_ok_conns == g_conns && g_total_bytes == expected) ? "PASS" : "FAIL");
    return (g_ok_conns == g_conns && g_total_bytes == expected) ? 0 : 1;
}
