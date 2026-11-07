
#if !defined(_WIN32) && defined(__GLIBC__)
#define _DEFAULT_SOURCE
#endif

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601   /* Windows 7+: GetQueuedCompletionStatusEx */
#endif

#if defined(_WIN32)
#include <winsock2.h>
#endif
#include "rt_io.h"
#include "rt_co.h"
#include "rt_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "rt_crash.h"

static int zan_io_trace(void){static volatile int t=-1;int c=__atomic_load_n(&t,__ATOMIC_RELAXED);if(c<0){const char*e=getenv("ZAN_IO_TRACE");c=(e&&*e&&*e!='0')?1:0;__atomic_store_n(&t,c,__ATOMIC_RELAXED);}return c;}
#define IOTRACE(...) do{if(zan_io_trace()){fprintf(stderr,"[iot] " __VA_ARGS__);fprintf(stderr,"\n");fflush(stderr);}}while(0)

static ZAN_MAYBE_UNUSED int zan_sched_trace(void){static volatile int t=-1;int c=__atomic_load_n(&t,__ATOMIC_RELAXED);if(c<0){const char*e=getenv("ZAN_SCHED_TRACE");c=(e&&*e&&*e!='0')?1:0;__atomic_store_n(&t,c,__ATOMIC_RELAXED);}return c;}
#define STRACE(...) do{if(zan_sched_trace()){fprintf(stderr,"[st] " __VA_ARGS__);fprintf(stderr,"\n");fflush(stderr);}}while(0)

#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <time.h>

static void zan_io_ignore_sigpipe(void) {
    struct sigaction sa;
    struct sigaction cur;
    /* 仅在程序未自定义处理时生效 */
    if (sigaction(SIGPIPE, NULL, &cur) == 0
        && cur.sa_handler != SIG_DFL) {
        return;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);
}
#endif

/* 设置描述符执行时关闭 (O_CLOEXEC)，防止子进程泄漏句柄 */
static ZAN_MAYBE_UNUSED void zan_io_fd_cloexec(int fd) {
#if defined(FD_CLOEXEC)
    if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC);
#else
    (void)fd;
#endif
}

#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>          /* ConnectEx + WSAID_CONNECTEX */
#include <windows.h>
#include <wincrypt.h>
#include <malloc.h>           /* _aligned_malloc / _aligned_free (op pool) */
#else
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include "../common/host_oom.h"

#if !defined(_WIN32)
typedef long LONG;
#ifndef InterlockedIncrement
#define InterlockedIncrement(dst) \
    (LONG)__sync_add_and_fetch((LONG volatile *)(dst), 1)
#endif
#ifndef InterlockedDecrement
#define InterlockedDecrement(dst) \
    (LONG)__sync_sub_and_fetch((LONG volatile *)(dst), 1)
#endif
#ifndef InterlockedExchangeAdd
#define InterlockedExchangeAdd(dst, val) \
    (LONG)__sync_fetch_and_add((LONG volatile *)(dst), (LONG)(val))
#endif
#ifndef InterlockedExchange
#define InterlockedExchange(dst, val) \
    (LONG)__sync_lock_test_and_set((LONG volatile *)(dst), (LONG)(val))
#endif
#endif /* !_WIN32 */

/* 逻辑处理器数（Environment.ProcessorCount 的探针）：Windows 用
 * GetSystemInfo，POSIX 用 sysconf(_SC_NPROCESSORS_ONLN)，查询失败
 * 回 1。曾只读 NUMBER_OF_PROCESSORS 环境变量——Linux/macOS 上没有
 * 这个变量，ProcessorCount 恒 1，线程池全部退化单线程。
 * 必须所有编译面可见：曾误放 ZAN_CO_DRIVER 门内协同调度段，而仓内
 * CMake 对象与 selfhost 运行时束不定义该宏，gen2 链接缺符号（B-ID109）。 */
int32_t zan_cpu_count(void) {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (si.dwNumberOfProcessors > 0) ? (int32_t)si.dwNumberOfProcessors : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int32_t)n : 1;
#endif
}

static volatile LONG g_io_count;

/* 检查是否the backend has been initialized */
static int g_io_started;
static int g_io_broken;
static ZAN_MAYBE_UNUSED int g_io_shards_live;

static int32_t g_blocking_inflight;
static ZAN_MAYBE_UNUSED int32_t g_dns_wake_fd = -1;
#if defined(_WIN32)
/* 记录阻塞/DNS 完成包投递失败异常 */
static volatile LONG g_blocking_wake_lost;
#endif
#if !defined(__linux__) && !defined(_WIN32)
static int32_t g_dns_wake_wfd = -1;
#endif

static int dns_drain(void);
static int64_t dns_wait_ms(int64_t caller_ms);  
/* 按最早截止时间上限截断后端等待超时 */
static int dns_timeout_scan(void);
static int64_t dns_now_ms(void);
static void dns_wake_read(void);
static void dns_wake_notify(void);
static void dns_shutdown_cleanup(void);

/* 毫秒超时后将无栈协程帧重新推入就绪队列 */
void zan_co_delay(long long ms, void *frame, zan_co_step_t step);

/* 唤醒就绪文件描述符关联的等待观察者 */
static void io_wake(void *co, zan_co_step_t step) {
    zan_co_ready(co, step);
}

#if !defined(_WIN32)
/* 多路复用反应堆就绪观察者结构 (epoll / kqueue / select) */
typedef struct zan_io_entry {
    int fd;
    int interest;           /* ZAN_IO_READ or ZAN_IO_WRITE */
    void *co;
    zan_co_step_t step;
    void *rbuf;
    int   rlen;
    int64_t *out_n;
    intptr_t *out_accept;   /* accepted fd sink (NULL => not an accept op) */
    struct zan_io_entry *next;
} zan_io_entry_t;

static zan_io_entry_t *g_io_entries;

/* 挂起的接收操作参数暂存区 */
static _Thread_local void   *g_pending_rbuf;
static _Thread_local int32_t  g_pending_rlen;
static _Thread_local int64_t *g_pending_out_n;
static _Thread_local intptr_t *g_pending_accept_out;

typedef struct zan_io_dead zan_io_dead_t;
typedef struct zan_io_rto zan_io_rto_t;

#define ZAN_IO_MAXSHARD 256
typedef struct zan_io_shard {
#if defined(ZAN_CO_DRIVER)
    pthread_mutex_t mx;
#endif
    int poll_fd;                /* epoll fd / kq fd (unused by select) */
    int wake_rfd, wake_wfd;
    zan_io_rto_t *rto;
    zan_io_dead_t *dead;
} zan_io_shard_t;
static zan_io_shard_t g_ioshard[ZAN_IO_MAXSHARD];

static int g_ioshard_prim;
static void io_shards_prime(void) {
    if (g_ioshard_prim) return;
    for (int i = 0; i < ZAN_IO_MAXSHARD; i++) {
        g_ioshard[i].poll_fd = -1;
        g_ioshard[i].wake_rfd = -1;
        g_ioshard[i].wake_wfd = -1;
    }
    g_ioshard_prim = 1;
}

#if defined(ZAN_CO_DRIVER)
static int g_shard_mx_ready;
static void io_shard_mutexes_prime(void) {
    if (g_shard_mx_ready) return;
    for (int i = 0; i < ZAN_IO_MAXSHARD; i++)
        pthread_mutex_init(&g_ioshard[i].mx, NULL);
    g_shard_mx_ready = 1;
}
static pthread_mutex_t g_io_init_mx = PTHREAD_MUTEX_INITIALIZER;
#define IO_INIT_LOCK()    pthread_mutex_lock(&g_io_init_mx)
#define IO_INIT_UNLOCK()  pthread_mutex_unlock(&g_io_init_mx)
#else
static void io_shard_mutexes_prime(void) { }
#define IO_INIT_LOCK()    ((void)0)
#define IO_INIT_UNLOCK()  ((void)0)
#endif

#if defined(ZAN_CO_DRIVER)
/* 反应堆分片总数（1 表示单反应堆模式） */
static volatile LONG g_shards = 1;
#endif

static int io_shard_count(void) {
#if defined(ZAN_CO_DRIVER)
    return (int)g_shards;
#else
    return 1;
#endif
}

#if defined(ZAN_CO_DRIVER)
static void shard_lock(zan_io_shard_t *sh)   { pthread_mutex_lock(&sh->mx); }
static void shard_unlock(zan_io_shard_t *sh) { pthread_mutex_unlock(&sh->mx); }

/* 根据描述符哈希分配所属反应堆分片索引 */
static int io_shard_index_of_fd(intptr_t fd) {
    int n = (int)g_shards;
    if (n <= 1 || fd <= 0) return 0;
    return (int)((unsigned long)fd % (unsigned long)n);
}

static int io_thread_shard(void);

#else
static void shard_lock(zan_io_shard_t *sh)   { (void)sh; }
static void shard_unlock(zan_io_shard_t *sh) { (void)sh; }
static int io_shard_index_of_fd(intptr_t fd) { (void)fd; return 0; }
static int io_thread_shard(void) { return 0; }
#endif

static zan_io_shard_t *io_shard_of_fd(intptr_t fd) {
    return &g_ioshard[io_shard_index_of_fd(fd)];
}

/* 消费分片内部唤醒事件信号 */
static void io_shard_wake_consume(zan_io_shard_t *sh) {
#if defined(__linux__)
    uint64_t v;
    ssize_t r = read(sh->wake_rfd, &v, sizeof(v));
    (void)r;
#else
    char c[64];
    while (read(sh->wake_rfd, c, sizeof(c)) > 0) {}
#endif
}

static void io_init_locked(void);
static void io_shutdown_locked(void);
static int32_t io_poll_shard(int shard, int64_t timeout_ms);

typedef struct zan_io_dead {
    void *co;
    zan_co_step_t step;
    int64_t *out_n;
    intptr_t *out_accept;
    struct zan_io_dead *next;
} zan_io_dead_t;

/* 反应堆单次睡眠最大超时阈值 */
#define ZAN_IO_SWEEP_MS 20

#define ZAN_IO_FAST_BURST 64

/* 反应堆分片独立维护的惰性清理队列 */
static volatile LONG g_io_dead_count;
static _Thread_local int g_io_fast_budget = ZAN_IO_FAST_BURST;

/* 探测描述符在内核中是否依然有效 */
static int io_fd_usable(intptr_t fd) {
    return zan_io_socket_alive(fd) != 0;
}

typedef struct zan_io_rto {
    int fd;
    void *frame;
    zan_co_step_t step;
    int64_t *out_n;
    long long due_ms;
    struct zan_io_rto *next;
} zan_io_rto_t;

static zan_io_rto_t *g_rto;

/* 注销并回收指定协程帧的超时项 */
static void rto_drop(zan_io_shard_t *sh, void *frame) {
    zan_io_rto_t **pp = &sh->rto;
    while (*pp) {
        zan_io_rto_t *e = *pp;
        if (e->frame == frame) {
            *pp = e->next;
            free(e);
            return;
        }
        pp = &e->next;
    }
}

/* 注册带超时接收的等待截止时间 */
static void rto_arm(zan_io_shard_t *sh, int fd, void *frame, zan_co_step_t step,
                    int64_t *out_n, int64_t timeout_ms) {
    zan_io_rto_t *e = (zan_io_rto_t *)calloc(1, sizeof(*e));
    if (!e) return;   
    e->fd = fd;
    e->frame = frame;
    e->step = step;
    e->out_n = out_n;
    e->due_ms = dns_now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
    e->next = sh->rto;
    sh->rto = e;
}

static void io_mark_dead(zan_io_shard_t *sh, void *co, zan_co_step_t step,
                         int64_t *out_n, intptr_t *out_accept) {
    zan_io_dead_t *d = (zan_io_dead_t *)calloc(1, sizeof(*d));
    rto_drop(sh, co);
    if (!d) return;
    d->co = co;
    d->step = step;
    d->out_n = out_n;
    d->out_accept = out_accept;
    d->next = sh->dead;
    sh->dead = d;
    InterlockedIncrement(&g_io_dead_count);
    STRACE("mark_dead sh=%d co=%p dead=%d", (int)(sh - g_ioshard), co,
           (int)g_io_dead_count);
}

/* 中断并唤醒当前插槽所有滞留的等待者 */
static int io_flush_dead(zan_io_shard_t *sh) {
    int woke = 0;
    while (sh->dead) {
        zan_io_dead_t *d = sh->dead;
        sh->dead = d->next;
        InterlockedDecrement(&g_io_dead_count);
        if (d->out_accept) *d->out_accept = -1;
        else if (d->out_n)  *d->out_n = 0;
        io_wake(d->co, d->step);
        free(d);
        woke++;
    }
    if (woke) STRACE("flush_dead sh=%d woke=%d dead=%d", (int)(sh - g_ioshard),
                     woke, (int)g_io_dead_count);
    return woke;
}

/* 优雅关闭：清空队列并不再派发唤醒事件 */
static void io_dead_clear(zan_io_shard_t *sh) {
    while (sh->dead) {
        zan_io_dead_t *d = sh->dead;
        sh->dead = d->next;
        InterlockedDecrement(&g_io_dead_count);
        free(d);
    }
}

static int io_reject_dead_fd(intptr_t fd, void *co, zan_co_step_t step) {
    if (io_fd_usable(fd)) return 0;
    io_mark_dead(io_shard_of_fd(fd), co, step, g_pending_out_n, g_pending_accept_out);
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
    g_pending_accept_out = NULL;
    return 1;
}

#if !defined(__linux__)
static void io_deliver_recv(zan_io_entry_t *e) {
    if (e->out_accept) {
        int cfd = accept(e->fd, NULL, NULL);
        if (cfd >= 0) {
#if defined(FD_CLOEXEC)
            int flags = fcntl(cfd, F_GETFD);
            if (flags >= 0) fcntl(cfd, F_SETFD, flags | FD_CLOEXEC);
#endif
#if defined(O_NONBLOCK)
            int fl = fcntl(cfd, F_GETFL);
            if (fl >= 0) fcntl(cfd, F_SETFL, fl | O_NONBLOCK);
#endif
        }
        *e->out_accept = (intptr_t)cfd;
        return;
    }
    if (!e->out_n) return;
    ssize_t rn;
    do {
        rn = recv(e->fd, e->rbuf, (size_t)e->rlen, 0);
    } while (rn < 0 && errno == EINTR);
    *e->out_n = (rn < 0) ? 0 : (int64_t)rn;
}

static int io_sweep_entries(int fd_limit) {
    int found = 0;
    zan_io_entry_t **pp = &g_io_entries;
    while (*pp) {
        zan_io_entry_t *cur = *pp;
        if (cur->fd < fd_limit && io_fd_usable(cur->fd)) {
            pp = &cur->next;
            continue;
        }
        *pp = cur->next;
        io_mark_dead(io_shard_of_fd(cur->fd), cur->co, cur->step,
                     cur->out_n, cur->out_accept);
        free(cur);
        InterlockedDecrement(&g_io_count);
        found = 1;
    }
    if (!found) return 0;
    int woke = 0;
    for (int i = 0; i < io_shard_count(); i++)
        woke += io_flush_dead(&g_ioshard[i]);
    return woke;
}
#endif
#endif

#if defined(_WIN32)
static ZAN_MAYBE_UNUSED volatile LONG g_socket_cleanup_requested;

void zan_io_socket_cleanup(void) {
#if defined(ZAN_CO_DRIVER)
    InterlockedIncrement(&g_socket_cleanup_requested);
#else
    WSACleanup();
#endif
}
#else
void zan_io_socket_cleanup(void) {}
#endif

int64_t zan_io_socket_send(intptr_t fd, const void *buf, int64_t len,
                           int32_t flags) {
    if (len <= 0) return len == 0 ? 0 : -2;
    if (len > 0x7FFFFFFF) len = 0x7FFFFFFF;
#if defined(_WIN32)
    for (;;) {
        int _sr = send((SOCKET)fd, (const char *)buf, (int)len, (int)flags);
        if (_sr != SOCKET_ERROR) {
            IOTRACE("send fd=%lld len=%lld -> %d", (long long)fd, (long long)len, _sr);
            return (int64_t)_sr;
        }
        int e = WSAGetLastError();
        IOTRACE("send fd=%lld len=%lld -> ERR err=%d", (long long)fd, (long long)len, e);
        if (e == WSAEWOULDBLOCK) return -1;
        if (e == WSAEINTR) continue;
        return -2;
    }
#else
#if defined(MSG_NOSIGNAL)
    flags |= MSG_NOSIGNAL;
#endif
    for (;;) {
        ssize_t r = send((int)fd, buf, (size_t)len, (int)flags);
        if (r >= 0) return (int64_t)r;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        if (errno == EINTR) continue;
        return -2;
    }
#endif
}

int64_t zan_io_socket_recv(intptr_t fd, void *buf, int64_t len,
                           int32_t flags) {
    /* 边界保护：拒绝负数并截断至 INT_MAX */
    if (len < 0) return -2;
    if (len > 0x7FFFFFFF) len = 0x7FFFFFFF;
#if defined(_WIN32)
    for (;;) {
        int _rr = recv((SOCKET)fd, (char *)buf, (int)len, (int)flags);
        if (_rr != SOCKET_ERROR) return (int64_t)_rr;
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return -1;
        if (e == WSAEINTR) continue;
        return -2;
    }
#else
    for (;;) {
        ssize_t r = recv((int)fd, buf, (size_t)len, (int)flags);
        if (r >= 0) return (int64_t)r;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        /* 捕获 EINTR 信号并安全重试 */
        if (errno == EINTR) continue;
        return -2;
    }
#endif
}


/* 1. 套接字地址提取、IP 规范化与安全证书策略 */
#include "io/rt_io_addr.c"

int32_t zan_io_socket_ready(intptr_t fd, int32_t write_ready) {
    fd_set fds;
    FD_ZERO(&fds);
    struct timeval timeout = {0, 0};
#if defined(_WIN32)
    SOCKET sock = (SOCKET)fd;
    if (sock == INVALID_SOCKET) return -1;
    FD_SET(sock, &fds);
    return (int64_t)select(0, write_ready ? NULL : &fds,
                          write_ready ? &fds : NULL, NULL, &timeout);
#else
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    int sock = (int)fd;
    FD_SET(sock, &fds);
    return (int64_t)select(sock + 1, write_ready ? NULL : &fds,
                          write_ready ? &fds : NULL, NULL, &timeout);
#endif
}

int32_t zan_io_socket_alive(intptr_t fd) {
#if defined(_WIN32)
    if ((SOCKET)fd == INVALID_SOCKET) return 0;
    int type = 0;
    int tlen = (int)sizeof(type);
    return getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE, (char *)&type, &tlen) == 0;
#else
    if (fd < 0) return 0;
    return fcntl((int)fd, F_GETFD) != -1;
#endif
}

/* 非阻塞套接字连接就绪状态探测 */
int32_t zan_io_connect_status(intptr_t fd) {
    fd_set wfds, efds;
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    struct timeval timeout = {0, 0};
    int err = 0;
#if defined(_WIN32)
    int elen = (int)sizeof(err);
    SOCKET sock = (SOCKET)fd;
    if (sock == INVALID_SOCKET) return -1;
    FD_SET(sock, &wfds);
    FD_SET(sock, &efds);
    if (select(0, NULL, &wfds, &efds, &timeout) < 0) return -1;
#else
    socklen_t elen = (socklen_t)sizeof(err);
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    int sock = (int)fd;
    FD_SET(sock, &wfds);
    FD_SET(sock, &efds);
    if (select(sock + 1, NULL, &wfds, &efds, &timeout) < 0) return -1;
#endif
    if (FD_ISSET(sock, &efds)) {
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0)
            return -1;
        return err != 0 ? (int64_t)err : -1;
    }
    if (FD_ISSET(sock, &wfds)) {
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0)
            return -1;
        return (int64_t)err;
    }
    return -2;
}

int32_t zan_io_connect_sa_start(intptr_t fd, const void *sa, int32_t salen) {
    if (!sa || zan_io_sockaddr_family(sa, salen) == 0) return -1;
    if (zan_io_set_nonblocking(fd) != 0) return -1;
#if defined(_WIN32)
    SOCKET s = (SOCKET)fd;
    int r = connect(s, (const struct sockaddr *)sa, salen);
    if (r == 0) return 0;
    int e = WSAGetLastError();
    if (e == WSAEINPROGRESS || e == WSAEWOULDBLOCK || e == WSAEALREADY)
        return -2;
    return e > 0 ? e : -1;
#else
    int r = connect((int)fd, (const struct sockaddr *)sa, (socklen_t)salen);
    if (r == 0) return 0;
    if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EALREADY)
        return -2;
    return errno > 0 ? errno : -1;
#endif
}

/* 阻塞工作线程连接适配器 */
int64_t zan_io_connect_sa(intptr_t fd, const void *sa, int32_t salen,
                           int32_t timeout_ms) {
    int32_t r = zan_io_connect_sa_start(fd, sa, salen);
    if (r == 0 || r != -2) return r;
#if defined(_WIN32)
    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_ZERO(&efds);
    FD_SET((SOCKET)fd, &wfds); FD_SET((SOCKET)fd, &efds);
    struct timeval tv, *ptv = NULL;
    if (timeout_ms >= 0) { tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000; ptv = &tv; }
    if (select(0, NULL, &wfds, &efds, ptv) <= 0) return -3;
    int err = 0, len = (int)sizeof(err);
    if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0)
        return -1;
#else
    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_ZERO(&efds);
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    FD_SET((int)fd, &wfds); FD_SET((int)fd, &efds);
    struct timeval tv, *ptv = NULL;
    if (timeout_ms >= 0) { tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000; ptv = &tv; }
    int sr;
    do { sr = select((int)fd + 1, NULL, &wfds, &efds, ptv); }
    while (sr < 0 && errno == EINTR);
    if (sr <= 0) return sr == 0 ? -3 : -1;
    int err = 0;
    socklen_t len = (socklen_t)sizeof(err);
    if (getsockopt((int)fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
        return -1;
#endif
    return err == 0 ? 0 : err;
}

#if !defined(_WIN32)

typedef struct zan_io_waiter {
    void *co;
    zan_co_step_t step;
    void *rbuf;
    int   rlen;
    int64_t *out_n;
    intptr_t *out_accept;
    struct zan_io_waiter *next;
} zan_io_waiter_t;

typedef struct {
    zan_io_waiter_t r;
    zan_io_waiter_t w;
    unsigned char has_r;
    unsigned char has_w;
    unsigned char in_epoll;
} zan_io_slot_t;

static zan_io_slot_t *g_slots;
static int g_slots_cap;

int32_t zan_io_set_nonblocking(intptr_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

static zan_io_slot_t *io_slot(int fd) {
    if (fd < 0 || fd >= g_slots_cap) return NULL;
    return &g_slots[fd];
}

static int io_slots_ensure(int need) {
    if (need <= g_slots_cap) return 1;
    int cap = g_slots_cap ? g_slots_cap : 256;
    while (cap > 0 && cap < need) cap *= 2;
    if (cap <= 0) return 0;
    int n = io_shard_count();
    for (int i = 0; i < n; i++) shard_lock(&g_ioshard[i]);
    if (need <= g_slots_cap) {
        for (int i = n - 1; i >= 0; i--) shard_unlock(&g_ioshard[i]);
        return 1;
    }
    zan_io_slot_t *ns = (zan_io_slot_t *)realloc(g_slots, (size_t)cap * sizeof(*ns));
    int ok = ns != NULL;
    if (ok) {
        memset(ns + g_slots_cap, 0, (size_t)(cap - g_slots_cap) * sizeof(*ns));
        g_slots = ns;
        g_slots_cap = cap;
    }
    for (int i = n - 1; i >= 0; i--) shard_unlock(&g_ioshard[i]);
    return ok;
}


/* 唤醒后执行挂起的接收/接受连接操作 */
static void io_deliver_waiter(int fd, zan_io_waiter_t *w) {
    if (w->out_accept) {
#if defined(__linux__) && defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
        *w->out_accept = (intptr_t)accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
        int cfd = accept(fd, NULL, NULL);
        if (cfd >= 0) {
#if defined(FD_CLOEXEC)
            int flags = fcntl(cfd, F_GETFD);
            if (flags >= 0) fcntl(cfd, F_SETFD, flags | FD_CLOEXEC);
#endif
#if defined(O_NONBLOCK)
            int fl = fcntl(cfd, F_GETFL);
            if (fl >= 0) fcntl(cfd, F_SETFL, fl | O_NONBLOCK);
#endif
        }
        *w->out_accept = (intptr_t)cfd;
#endif
        return;
    }
    if (!w->out_n) return;
    ssize_t rn;
    do {
        rn = recv(fd, w->rbuf, (size_t)w->rlen, 0);
    } while (rn < 0 && errno == EINTR);
    *w->out_n = (rn < 0) ? 0 : (int64_t)rn;
}

static int io_take(zan_io_slot_t *s, int fd, int read_dir,
                   zan_io_waiter_t *out, int max) {
    unsigned char *has = read_dir ? &s->has_r : &s->has_w;
    if (!*has || max <= 0) return 0;
    zan_io_waiter_t *inl = read_dir ? &s->r : &s->w;
    int n = 0;
    out[n++] = *inl;
    zan_io_waiter_t *x = inl->next;
    while (x && n < max) {
        zan_io_waiter_t *nx = x->next;
        out[n++] = *x;
        free(x);
        x = nx;
    }
    if (x) {
        *inl = *x;
        free(x);
    } else {
        inl->next = NULL;
        *has = 0;
    }
    (void)fd;
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    if (sh->rto)
        for (int i = 0; i < n; i++) rto_drop(sh, out[i].co);
    return n;
}

static void io_unlink_waiter(zan_io_slot_t *s, int read_dir,
                             zan_io_waiter_t *w) {
    unsigned char *has = read_dir ? &s->has_r : &s->has_w;
    zan_io_waiter_t *inl = read_dir ? &s->r : &s->w;
    if (w == inl) {
        zan_io_waiter_t *rest = inl->next;
        if (rest) {
            *inl = *rest;
            free(rest);
        } else {
            inl->next = NULL;
            *has = 0;
        }
        return;
    }
    zan_io_waiter_t *p = inl;
    while (p && p->next != w) p = p->next;
    if (!p) return;
    p->next = w->next;
    free(w);
}

/* 按协程查找挂起的读等待者 */
static zan_io_waiter_t *io_find_read_waiter(zan_io_slot_t *s, void *co) {
    if (!s->has_r) return NULL;
    if (s->r.co == co) return &s->r;
    for (zan_io_waiter_t *x = s->r.next; x; x = x->next)
        if (x->co == co) return x;
    return NULL;
}

/* 中断当前插槽上的所有等待者 */
static int io_fail_slot_waiters(int fd, zan_io_slot_t *s) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    int failed = 0;
    for (int dir = 0; dir < 2; dir++) {
        unsigned char *has = dir ? &s->has_r : &s->has_w;
        zan_io_waiter_t *inl = dir ? &s->r : &s->w;
        if (!*has) continue;
        io_mark_dead(sh, inl->co, inl->step, inl->out_n, inl->out_accept);
        failed++;
        zan_io_waiter_t *x = inl->next;
        while (x) {
            zan_io_waiter_t *nx = x->next;
            io_mark_dead(sh, x->co, x->step, x->out_n, x->out_accept);
            free(x);
            failed++;
            x = nx;
        }
        inl->next = NULL;
        *has = 0;
    }
    (void)InterlockedExchangeAdd(&g_io_count, -failed);
    return failed;
}

/* 清理已关闭描述符上的残留等待者 */
static int io_sweep_slots(int idx) {
    int woke = 0;
    for (int fd = 0; fd < g_slots_cap; fd++) {
        if (io_shard_index_of_fd(fd) != idx) continue;
        zan_io_slot_t *s = &g_slots[fd];
        if (!s->has_r && !s->has_w) continue;
        if (io_fd_usable(fd)) continue;
        s->in_epoll = 0;
        for (;;) {
            zan_io_waiter_t ready[16];
            int nr = io_take(s, fd, 1, ready, 16);
            nr += io_take(s, fd, 0, ready + nr, 16 - nr);
            if (nr == 0) break;
            for (int k = 0; k < nr; k++) {
                if (ready[k].out_accept)   *ready[k].out_accept = -1;
                else if (ready[k].out_n)   *ready[k].out_n = 0;
                InterlockedDecrement(&g_io_count);
                io_wake(ready[k].co, ready[k].step);
                woke++;
            }
        }
    }
    return woke;
}

#endif /* !_WIN32 */


/* 多路复用反应堆驱动（按平台组合分发） */
#if defined(__linux__)
#include "io/rt_io_epoll.c"
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include "io/rt_io_kqueue.c"
#elif defined(_WIN32)
#include "io/rt_io_iocp.c"
#else
#include "io/rt_io_select.c"
#endif


/* 3. 异步 DNS 解析服务与后台阻塞型工作任务线程池 */
#include "io/rt_io_dns.c"

void zan_io_wait_co(intptr_t fd, int32_t interest, void *frame, zan_co_step_t step) {
    zan_io_init();
    io_register(fd, interest, frame, step);
}

#if !defined(_WIN32)
void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
                    zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
#if defined(MSG_DONTWAIT)
    if (fd >= 0 && buf && len > 0 && out_n && g_io_fast_budget > 0) {
        ssize_t rn = recv((int)fd, buf, (size_t)len, MSG_DONTWAIT);
        int again = (rn < 0 && (errno == EAGAIN || errno == EWOULDBLOCK
                               || errno == EINTR));
        if (!again) {
            g_io_fast_budget--;
            *out_n = (rn < 0) ? 0 : (int64_t)rn;
            io_wake(frame, step);
            return;
        }
    }
#endif
    g_pending_rbuf = buf;
    g_pending_rlen = len;
    g_pending_out_n = out_n;
    io_register(fd, ZAN_IO_READ, frame, step);
}

void zan_io_recv_to_co(intptr_t fd, void *buf, int32_t len, int64_t timeout_ms,
                       void *frame, zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
#if defined(MSG_DONTWAIT)
    if (fd >= 0 && buf && len > 0 && out_n && g_io_fast_budget > 0) {
        ssize_t rn = recv((int)fd, buf, (size_t)len, MSG_DONTWAIT);
        int again = (rn < 0 && (errno == EAGAIN || errno == EWOULDBLOCK
                               || errno == EINTR));
        if (!again) {
            g_io_fast_budget--;
            *out_n = (rn < 0) ? 0 : (int64_t)rn;
            io_wake(frame, step);
            return;
        }
    }
#endif
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    rto_arm(sh, (int)fd, frame, step, out_n, timeout_ms);
    g_pending_rbuf = buf;
    g_pending_rlen = len;
    g_pending_out_n = out_n;
    io_register_locked(fd, ZAN_IO_READ, frame, step);
    shard_unlock(sh);
}

void zan_io_accept_co(intptr_t fd, void *frame, zan_co_step_t step,
                      intptr_t *out_fd) {
    zan_io_init();
    g_pending_accept_out = out_fd;
    io_register(fd, ZAN_IO_READ, frame, step);
}
#endif

int32_t zan_io_pump_timeout(int64_t timeout_ms) {
#if !defined(_WIN32)
    g_io_fast_budget = ZAN_IO_FAST_BURST;
#endif
    if (zan_io_has_pending())
        return zan_io_poll(timeout_ms);
    if (timeout_ms <= 0)
        return 0;
    if (timeout_ms > 0x7FFFFFFF) timeout_ms = 0x7FFFFFFF;
#ifdef _WIN32
    Sleep((DWORD)timeout_ms);
#else
    struct timespec ts = {
        (time_t)(timeout_ms / 1000),
        (long)((timeout_ms % 1000) * 1000000)
    };
    nanosleep(&ts, NULL);
#endif
    return 0;
}

#ifndef ZAN_CO_DRIVER
/* 单线程 reactor 变体：这里没有独立的 worker 池，就绪钩子（zan_co_ready）
 * 会**就地**恢复协程。所以派发到期定时器就等于"把被唤醒的协程跑起来"，
 * 不需要额外的取任务步骤。 */
void zan_co_sync_sleep(long long ms) {
    if (ms <= 0) return;
    long long deadline = zan_co_precise_us() + ms * 1000;
    for (;;) {
        zan_timer_dispatch_due_wait();
        long long remain_us = deadline - zan_co_precise_us();
        if (remain_us <= 0) return;
        long long next_ms = zan_timer_next_timeout();
        long long slice_ms = (remain_us + 999) / 1000;
        if (next_ms >= 0 && next_ms < slice_ms) slice_ms = next_ms;
        if (slice_ms <= 0) slice_ms = 1;
        if (slice_ms > 0x7FFFFFFF) slice_ms = 0x7FFFFFFF;
        zan_io_pump_timeout(slice_ms);
    }
}
#endif /* !ZAN_CO_DRIVER */
int32_t zan_io_pump(void) {
    return zan_io_pump_timeout(-1);
}

int32_t zan_io_has_pending(void) {
#if !defined(_WIN32)
    if (g_io_dead_count > 0) return 1;
#endif
    return (g_io_count > 0) || (g_blocking_inflight > 0);
}


#ifdef ZAN_CO_DRIVER

typedef struct zan_co_node {
    struct zan_co_node *next;
    void               *frame;
    zan_co_step_t       step;
} zan_co_node;

static zan_co_node *g_rq_head, *g_rq_tail;

#if !defined(_WIN32)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

typedef long LONG;
#define GetCurrentThreadId() ((unsigned long)(uintptr_t)pthread_self())
#ifndef InterlockedIncrement64
#define InterlockedIncrement64(dst) \
    (long long)__sync_add_and_fetch((long long volatile *)(dst), 1)
#endif
#if defined(ZAN_CO_DRIVER)
static void io_shards_start(int n) {
    io_shards_prime();
    int cfg = zan_async_cfg_io_shards();
    if (cfg > 0 && cfg < n) n = cfg;
    const char *e = getenv("ZAN_IO_SHARDS");
    if (e && *e) {
        int v = atoi(e);
        if (v > 0 && v < n) n = v;
    }
    if (n > ZAN_IO_MAXSHARD) n = ZAN_IO_MAXSHARD;
    for (int i = 1; i < n; i++) {
        if (g_ioshard[i].poll_fd < 0 && !io_shard_open(i)) { n = i; break; }
    }
    if (n < 1) n = 1;
    InterlockedExchange(&g_shards, n);
}
static inline void io_shards_stop(void) {}
#else
static inline void io_shards_start(int n) { (void)n; }
static inline void io_shards_stop(void) {}
#endif
static volatile LONG g_sync_inline = 0;

#endif /* !defined(_WIN32) */

/* 4. 协程工作窃取队列与帧检疫区底座 (仅多工作线程并发模式) */
#include "io/rt_io_co_pool.c"

/* 5. M:N 协程调度执行引擎与工作线程池 */
#include "io/rt_io_co_sched.c"

#endif /* ZAN_CO_DRIVER */

/* 6. 异步协程门禁同步原语与看门狗 */
#include "io/rt_io_gate.c"

/* Console.ErrorWriteLine：stderr 行输出（A383）。引擎/工具的告警通道——
 * 与 stdout 的 WriteLine 分流，黄金比对（stdout）不受告警影响。
 * wasm 目标的运行时集不含本符号，调用前见 irgen_call.c 的收窄说明。 */
void zan_rt_eprint_line(const char *text) {
    if (text && text[0]) { fputs(text, stderr); }
#if defined(_WIN32)
    /* B-ID113：stderr 同走二进制模式（__zan_stdio_init），行尾显式 CRLF
     * 对齐 C# Console.Error.WriteLine 的 Environment.NewLine */
    fputc('\r', stderr);
#endif
    fputc('\n', stderr);
    fflush(stderr);
}
