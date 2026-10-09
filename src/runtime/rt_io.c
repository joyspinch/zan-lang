/* 内部辅助实现 */

/* 内部辅助实现 */
#if !defined(_WIN32) && defined(__GLIBC__)
#define _DEFAULT_SOURCE
#endif

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601   /* Windows 7+: GetQueuedCompletionStatusEx */
#endif

#if defined(_WIN32)
#include <winsock2.h>   /* 核心系统底层抽象与内存语义契约 */
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

/* 内部辅助实现 */
static ZAN_MAYBE_UNUSED int zan_sched_trace(void){static volatile int t=-1;int c=__atomic_load_n(&t,__ATOMIC_RELAXED);if(c<0){const char*e=getenv("ZAN_SCHED_TRACE");c=(e&&*e&&*e!='0')?1:0;__atomic_store_n(&t,c,__ATOMIC_RELAXED);}return c;}
#define STRACE(...) do{if(zan_sched_trace()){fprintf(stderr,"[st] " __VA_ARGS__);fprintf(stderr,"\n");fflush(stderr);}}while(0)

/* 内部辅助实现 */
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

/* 内部辅助实现 */
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
#include <poll.h>             /* 底层系统交互与数据协议契约 */
#include <fcntl.h>
#include <unistd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#include <poll.h>             /* 底层系统交互与数据协议契约 */
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
#ifndef ZAN_IO_STACKLESS_ONLY
#include "rt_sched.h"
/* 核心系统底层抽象与内存语义契约 */
extern void zan_io_suspend_current(void);   /* implemented in rt_sched.c */
extern void zan_io_resume(void *co);        /* implemented in rt_sched.c */
extern void *zan_io_get_current_co(void);   /* implemented in rt_sched.c */
#endif
#include "../common/host_oom.h"

#if !defined(_WIN32)
/* 内部辅助实现 */
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

/* 底层系统交互与数据协议契约 */

/* 内部辅助实现 */
static volatile LONG g_io_count;

/* 检查是否the backend has been initialized */
static int g_io_started;
/* 内部辅助实现 */
static int g_io_broken;
/* 内部辅助逻辑 */
static ZAN_MAYBE_UNUSED int g_io_shards_live;

/* 内部辅助实现 */
static int32_t g_blocking_inflight;
static ZAN_MAYBE_UNUSED int32_t g_dns_wake_fd = -1;    /* 核心系统底层抽象与内存语义契约 */
#if defined(_WIN32)
/* 记录阻塞/DNS 完成包投递失败异常 */
static volatile LONG g_blocking_wake_lost;
#endif
#if !defined(__linux__) && !defined(_WIN32)
static int32_t g_dns_wake_wfd = -1;   /* 核心系统底层抽象与内存语义契约 */
#endif

static int dns_drain(void);           /* 核心系统底层抽象与内存语义契约 */
static int64_t dns_wait_ms(int64_t caller_ms);  
/* 按最早截止时间上限截断后端等待超时 */
static int dns_timeout_scan(void);    /* 核心系统底层抽象与内存语义契约 */
static int64_t dns_now_ms(void);      /* 核心系统底层抽象与内存语义契约 */
static void dns_wake_read(void);      /* 核心系统底层抽象与内存语义契约 */
static void dns_wake_notify(void);    /* 核心系统底层抽象与内存语义契约 */
static void dns_shutdown_cleanup(void); /* 核心系统底层抽象与内存语义契约 */

/* 毫秒超时后将无栈协程帧重新推入就绪队列 */
void zan_co_delay(long long ms, void *frame, zan_co_step_t step);

/* 唤醒就绪文件描述符关联的等待观察者 */
static void io_wake(void *co, zan_co_step_t step) {
#ifdef ZAN_IO_STACKLESS_ONLY
    zan_co_ready(co, step);      /* 核心系统底层抽象与内存语义契约 */
#else
    if (step) zan_co_ready(co, step);
    else      zan_io_resume(co);
#endif
}

#if !defined(_WIN32)
/* 多路复用反应堆就绪观察者结构 (epoll / kqueue / select) */
typedef struct zan_io_entry {
    int fd;
    int interest;           /* ZAN_IO_READ or ZAN_IO_WRITE */
    void *co;               /* 核心系统底层抽象与内存语义契约 */
    zan_co_step_t step;     /* 核心系统底层抽象与内存语义契约 */
    void *rbuf;             /* 底层系统交互与数据协议契约 */
    int   rlen;
    int64_t *out_n;         /* 底层系统交互与数据协议契约 */
    intptr_t *out_accept;   /* accepted fd sink (NULL => not an accept op) */
    struct zan_io_entry *next;
} zan_io_entry_t;

static zan_io_entry_t *g_io_entries;    /* 核心系统底层抽象与内存语义契约 */

/* 挂起的接收操作参数暂存区 */
static _Thread_local void   *g_pending_rbuf;
static _Thread_local int32_t  g_pending_rlen;
static _Thread_local int64_t *g_pending_out_n;
static _Thread_local intptr_t *g_pending_accept_out;

/* 内部辅助实现 */
typedef struct zan_io_dead zan_io_dead_t;
typedef struct zan_io_rto zan_io_rto_t;

#define ZAN_IO_MAXSHARD 256
typedef struct zan_io_shard {
#if defined(ZAN_CO_DRIVER)
    pthread_mutex_t mx;         /* 核心系统底层抽象与内存语义契约 */
#endif
    int poll_fd;                /* epoll fd / kq fd (unused by select) */
    int wake_rfd, wake_wfd;     /* 底层系统交互与数据协议契约 */
    zan_io_rto_t *rto;          /* 底层系统交互与数据协议契约 */
    zan_io_dead_t *dead;        /* 核心系统底层抽象与内存语义契约 */
} zan_io_shard_t;
static zan_io_shard_t g_ioshard[ZAN_IO_MAXSHARD];

/* 内部辅助实现 */
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
/* 内部辅助实现 */
static int g_shard_mx_ready;
static void io_shard_mutexes_prime(void) {
    if (g_shard_mx_ready) return;
    for (int i = 0; i < ZAN_IO_MAXSHARD; i++)
        pthread_mutex_init(&g_ioshard[i].mx, NULL);
    g_shard_mx_ready = 1;
}
/* 内部辅助实现 */
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

/* 内部辅助实现 */
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

/* 内部辅助逻辑 */
static void io_init_locked(void);
static void io_shutdown_locked(void);
static int32_t io_poll_shard(int shard, int64_t timeout_ms);

/* 内部辅助实现 */
typedef struct zan_io_dead {
    void *co;
    zan_co_step_t step;
    int64_t *out_n;
    intptr_t *out_accept;
    struct zan_io_dead *next;
} zan_io_dead_t;

/* 反应堆单次睡眠最大超时阈值 */
#define ZAN_IO_SWEEP_MS 20

/* 内部辅助实现 */
#define ZAN_IO_FAST_BURST 64

/* 反应堆分片独立维护的惰性清理队列 */
static volatile LONG g_io_dead_count;
/* 内部辅助实现 */
static _Thread_local int g_io_fast_budget = ZAN_IO_FAST_BURST;

/* 探测描述符在内核中是否依然有效 */
static int io_fd_usable(intptr_t fd) {
    return zan_io_socket_alive(fd) != 0;
}

/* 核心系统底层抽象与内存语义契约 */
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
/* 内部辅助逻辑 */
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
    /* 内部辅助逻辑 */
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

/* 内部辅助实现 */
static int io_reject_dead_fd(intptr_t fd, void *co, zan_co_step_t step) {
    if (io_fd_usable(fd)) return 0;
    io_mark_dead(io_shard_of_fd(fd), co, step, g_pending_out_n, g_pending_accept_out);
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
    g_pending_accept_out = NULL;
    return 1;
}

/* 内部辅助实现 */
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
    /* 内部辅助实现 */
    *e->out_n = (rn < 0) ? 0 : (int64_t)rn;
}

/* 内部辅助实现 */
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

/* 内部辅助实现 */
int64_t zan_io_socket_send(intptr_t fd, const void *buf, int64_t len,
                           int32_t flags) {
    /* 内部辅助实现 */
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
        /* 内部辅助实现 */
        if (e == WSAEINTR) continue;
        return -2;
    }
#else
#if defined(MSG_NOSIGNAL)
    flags |= MSG_NOSIGNAL;   /* 底层系统交互与数据协议契约 */
#endif
    for (;;) {
        ssize_t r = send((int)fd, buf, (size_t)len, (int)flags);
        if (r >= 0) return (int64_t)r;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        /* 内部辅助逻辑 */
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

const char *zan_io_socket_peer_ip(intptr_t fd) {
    struct sockaddr_storage peer;
#if defined(_WIN32)
    int length = (int)sizeof(peer);
    if (getpeername((SOCKET)fd, (struct sockaddr *)&peer, &length) != 0) return "";
#else
    socklen_t length = (socklen_t)sizeof(peer);
    if (getpeername((int)fd, (struct sockaddr *)&peer, &length) != 0) return "";
#endif
    return zan_io_sockaddr_ip_str(&peer);
}

/* 格式化网络套接字地址为点分十进制/冒号文本 */
const char *zan_io_sockaddr_ip_str(const void *sa) {
    /* 线程局部缓冲区，支持多工作线程并发无锁格式化 */
    static _Thread_local char address[INET6_ADDRSTRLEN];
    const struct sockaddr *a = (const struct sockaddr *)sa;
    if (a->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)a;
        if (!inet_ntop(AF_INET, &in->sin_addr, address, sizeof(address)))
            return "";
        return address;
    }
    if (a->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)a;
        if (!inet_ntop(AF_INET6, &in6->sin6_addr, address, sizeof(address)))
            return "";
        return address;
    }
    return "";
}

/* 内部辅助实现 */
static int32_t zan_io_ip_copy_out(const char *text, char *buf, int32_t cap) {
    if (!buf || cap <= 0) return -1;
    size_t n = strlen(text);
    if ((size_t)cap <= n) return -1;
    memcpy(buf, text, n + 1);
    return (int32_t)n;
}

int32_t zan_io_socket_peer_ip_into(intptr_t fd, char *buf, int32_t cap) {
    struct sockaddr_storage peer;
#if defined(_WIN32)
    int length = (int)sizeof(peer);
    if (getpeername((SOCKET)fd, (struct sockaddr *)&peer, &length) != 0)
        return -1;
#else
    socklen_t length = (socklen_t)sizeof(peer);
    if (getpeername((int)fd, (struct sockaddr *)&peer, &length) != 0)
        return -1;
#endif
    return zan_io_ip_copy_out(zan_io_sockaddr_ip_str(&peer), buf, cap);
}

int32_t zan_io_sockaddr_ip_str_into(const void *sa, char *buf, int32_t cap) {
    return zan_io_ip_copy_out(zan_io_sockaddr_ip_str(sa), buf, cap);
}

/* 解析主机名或字面量 IP 为完整 sockaddr 结构 */
int32_t zan_io_resolve_sa(const char *name, int32_t port, void *buf,
                          int32_t cap) {
    if (!name || !*name) return 0;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = strchr(name, ':') ? AF_UNSPEC : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res) {
        if (hints.ai_family == AF_INET) {
            hints.ai_family = AF_UNSPEC;    /* 核心系统底层抽象与内存语义契约 */
            if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res)
                return 0;
        } else {
            return 0;
        }
    }
    int len = (res->ai_family == AF_INET6) ? (int)sizeof(struct sockaddr_in6)
             : (res->ai_family == AF_INET) ? (int)sizeof(struct sockaddr_in)
             : 0;
    int32_t r = 0;
    if (len && len <= cap && res->ai_addrlen == (size_t)len) {
        memcpy(buf, res->ai_addr, (size_t)len);
        r = len;
    }
    freeaddrinfo(res);
    return r;
}

/* 域名解析 IPv4 网络序地址 */
int32_t zan_io_resolve_ipv4(const char *hostname) {
    if (!hostname || !*hostname) return 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(hostname, NULL, &hints, &res) != 0 || !res) return 0;
    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    uint32_t addr_bits = 0;
    if (res->ai_family == AF_INET && sin->sin_family == AF_INET)
        addr_bits = sin->sin_addr.s_addr;
    freeaddrinfo(res);
    int32_t addr;
    memcpy(&addr, &addr_bits, sizeof(addr));
    return addr;
}

int32_t zan_io_sockaddr_family(const void *sa, int32_t len) {
    if (!sa || len < 2) return 0;
    const struct sockaddr *a = (const struct sockaddr *)sa;
    if (a->sa_family == AF_INET && len >= (int32_t)sizeof(struct sockaddr_in))
        return AF_INET;
    if (a->sa_family == AF_INET6 && len >= (int32_t)sizeof(struct sockaddr_in6))
        return AF_INET6;
    return 0;
}

/* 连接目标地址类别判定 (环回/私网/公网) */
int32_t zan_io_sockaddr_is_safe(const void *sa, int32_t len,
                               int32_t allow_loopback) {
    int family = zan_io_sockaddr_family(sa, len);
    if (family == AF_INET) {
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)sa;
        uint32_t a = ntohl(v4->sin_addr.s_addr);
        uint32_t first = a >> 24;
        uint32_t second = (a >> 16) & 255u;
        if (first == 127u) return allow_loopback ? 1 : 0;
        if (first == 0u || first == 10u || first >= 224u || first >= 240u)
            return 0;
        if (first == 169u && second == 254u) return 0;
        if (first == 172u && second >= 16u && second <= 31u) return 0;
        if (first == 192u && second == 168u) return 0;
        if (first == 192u && second == 0u) return 0;
        uint32_t third = (a >> 8) & 255u;
        if (first == 192u && second == 2u) return 0;
        if (first == 192u && second == 88u && third == 99u) return 0;
        if (first == 198u && second >= 18u && second <= 19u) return 0;
        if (first == 198u && second == 51u && third == 100u) return 0;
        if (first == 203u && second == 0u && third == 113u) return 0;
        if (first == 100u && second >= 64u && second <= 127u) return 0;
        if (a == 0xffffffffu) return 0;
        return 1;
    }
    if (family == AF_INET6) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)sa;
        const unsigned char *b = (const unsigned char *)&v6->sin6_addr;
        int all_zero = 1;
        for (int i = 0; i < 16; i++) if (b[i] != 0) all_zero = 0;
        if (all_zero) return 0;
        int loop = 1;
        for (int i = 0; i < 15; i++) if (b[i] != 0) loop = 0;
        if (loop && b[15] == 1) return allow_loopback ? 1 : 0;
        if ((b[0] & 0xfeu) == 0xfcu) return 0;       /* ULA fc00::/7 */
        if ((b[0] & 0xfeu) == 0xfeu && (b[1] & 0xc0u) == 0x80u)
            return 0;                                 /* link-local fe80::/10 */
        if (b[0] == 0xffu) return 0;                  /* multicast */
        if (b[0] == 0x20u && b[1] == 0x01u && b[2] == 0x0du
            && b[3] == 0xb8u) return 0;               /* documentation */
        int mapped = 1;
        for (int i = 0; i < 10; i++) if (b[i] != 0) mapped = 0;
        if (mapped && b[10] == 0xffu && b[11] == 0xffu) {
            struct sockaddr_in mapped4;
            memset(&mapped4, 0, sizeof(mapped4));
            mapped4.sin_family = AF_INET;
            memcpy(&mapped4.sin_addr, b + 12, 4);
            return zan_io_sockaddr_is_safe(&mapped4,
                (int32_t)sizeof(mapped4), allow_loopback);
        }
        /* Teredo 隧道 IPv6 地址解析 */
        if (b[0] == 0x20u && b[1] == 0x01u && b[2] == 0x00u && b[3] == 0x00u) {
            struct sockaddr_in t4;
            unsigned char cli[4];
            memset(&t4, 0, sizeof(t4));
            t4.sin_family = AF_INET;
            memcpy(&t4.sin_addr, b + 4, 4);
            if (!zan_io_sockaddr_is_safe(&t4, (int32_t)sizeof(t4), allow_loopback))
                return 0;
            for (int i = 0; i < 4; i++) cli[i] = (unsigned char)(b[12 + i] ^ 0xffu);
            memcpy(&t4.sin_addr, cli, 4);
            return zan_io_sockaddr_is_safe(&t4, (int32_t)sizeof(t4), allow_loopback);
        }
        /* 内部辅助逻辑 */
        int v4_off = -1;
        if (b[0] == 0x20u && b[1] == 0x02u)
            v4_off = 2;
        else if (b[0] == 0x00u && b[1] == 0x64u && b[2] == 0xffu && b[3] == 0x9bu)
            v4_off = 12;
        else if (mapped)
            v4_off = 12;
        if (v4_off >= 0) {
            struct sockaddr_in e4;
            memset(&e4, 0, sizeof(e4));
            e4.sin_family = AF_INET;
            memcpy(&e4.sin_addr, b + v4_off, 4);
            return zan_io_sockaddr_is_safe(&e4, (int32_t)sizeof(e4), allow_loopback);
        }
        return 1;
    }
    return 0;
}

int32_t zan_io_resolve_all(const char *name, int32_t port, void *buf,
                           int32_t cap) {
    if (!buf || cap <= 0) return 0;
    if (!name || !*name || cap < ZAN_IO_SA_STRIDE) {
        memset(buf, 0, (size_t)cap);
        return 0;
    }
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    /* 内部辅助实现 */
    hints.ai_flags = 0;
    struct addrinfo *res = NULL;
    if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res) {
        memset(buf, 0, (size_t)cap);
        return 0;
    }
    int32_t capacity = cap / ZAN_IO_SA_STRIDE;
    int32_t count = 0;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        int32_t family = zan_io_sockaddr_family(p->ai_addr,
            (int32_t)p->ai_addrlen);
        int32_t len = family == AF_INET6 ? (int32_t)sizeof(struct sockaddr_in6)
                     : family == AF_INET ? (int32_t)sizeof(struct sockaddr_in) : 0;
        if (!len || p->ai_addrlen < (size_t)len) continue;
        /* 内部辅助实现 */
        int duplicate = 0;
        for (int32_t i = 0; i < count; i++) {
            unsigned char *old = (unsigned char *)buf + i * ZAN_IO_SA_STRIDE;
            int oldfam = zan_io_sockaddr_family(old, len);
            if (oldfam == family && memcmp(old, p->ai_addr, (size_t)len) == 0) {
                duplicate = 1; break;
            }
        }
        if (duplicate) continue;
        if (count >= capacity) {
            /* 内部辅助逻辑 */
            memset(buf, 0, (size_t)cap);
            freeaddrinfo(res);
            return 0;
        }
        unsigned char *dst = (unsigned char *)buf + count * ZAN_IO_SA_STRIDE;
        memset(dst, 0, ZAN_IO_SA_STRIDE);
        memcpy(dst, p->ai_addr, (size_t)len);
        count++;
    }
    freeaddrinfo(res);
    return count;
}

int64_t zan_io_resolve_all_async(intptr_t name_ptr, int32_t port,
                                 intptr_t buf_ptr, int32_t cap) {
    return (int64_t)zan_io_resolve_all((const char *)name_ptr, port,
                                       (void *)buf_ptr, cap);
}

#if defined(_WIN32)
/* 从证书上下文解析 DER 证书数据指针与长度 */
const unsigned char *zan_crypto_cert_encoded(const void *cert, int *out_len) {
    const CERT_CONTEXT *ctx = (const CERT_CONTEXT *)cert;
    if (!ctx || !out_len) return NULL;
    *out_len = (int)ctx->cbCertEncoded;
    return ctx->pbCertEncoded;
}

/* 内部辅助实现 */
int32_t zan_io_crypto_windows_ssl_policy(const unsigned char *certs, int32_t total_len,
                                       int32_t count, const char *host, int32_t host_len) {
    typedef PCCERT_CONTEXT (WINAPI *create_cert_fn)(DWORD, const BYTE *, DWORD);
    typedef HCERTSTORE (WINAPI *open_store_fn)(LPCSTR, DWORD, HCRYPTPROV_LEGACY, DWORD, const void *);
    typedef BOOL (WINAPI *add_cert_fn)(HCERTSTORE, DWORD, const BYTE *, DWORD, DWORD, PCCERT_CONTEXT *);
    typedef BOOL (WINAPI *get_chain_fn)(HCERTCHAINENGINE, PCCERT_CONTEXT, LPFILETIME,
                                        HCERTSTORE, PCERT_CHAIN_PARA, DWORD, LPVOID, PCCERT_CHAIN_CONTEXT *);
    typedef BOOL (WINAPI *verify_policy_fn)(LPCSTR, PCCERT_CHAIN_CONTEXT,
                                            PCERT_CHAIN_POLICY_PARA, PCERT_CHAIN_POLICY_STATUS);
    typedef BOOL (WINAPI *close_store_fn)(HCERTSTORE, DWORD);
    typedef BOOL (WINAPI *free_cert_fn)(PCCERT_CONTEXT);
    typedef void (WINAPI *free_chain_fn)(PCCERT_CHAIN_CONTEXT);
    HMODULE lib = NULL;
    HCERTSTORE store = NULL;
    PCCERT_CONTEXT leaf = NULL;
    PCCERT_CHAIN_CONTEXT chain = NULL;
    int32_t result = 0;
    WCHAR wide_host[254];
    create_cert_fn create_cert;
    open_store_fn open_store;
    add_cert_fn add_cert;
    get_chain_fn get_chain;
    verify_policy_fn verify_policy;
    close_store_fn close_store;
    free_cert_fn free_cert;
    free_chain_fn free_chain;

    if (!certs || !host || total_len < 5 || total_len > 524288 ||
        count < 1 || count > 8 || host_len < 1 || host_len > 253) return 0;
    for (int32_t i = 0; i < host_len; ++i) {
        unsigned char ch = (unsigned char)host[i];
        if (ch <= 32 || ch >= 127 || ch == '/' || ch == '\\' || ch == ':') return 0;
        wide_host[i] = (WCHAR)ch;
    }
    wide_host[host_len] = 0;

    lib = LoadLibraryW(L"crypt32.dll");
    if (!lib) return -1;
    create_cert = (create_cert_fn)GetProcAddress(lib, "CertCreateCertificateContext");
    open_store = (open_store_fn)GetProcAddress(lib, "CertOpenStore");
    add_cert = (add_cert_fn)GetProcAddress(lib, "CertAddEncodedCertificateToStore");
    get_chain = (get_chain_fn)GetProcAddress(lib, "CertGetCertificateChain");
    verify_policy = (verify_policy_fn)GetProcAddress(lib, "CertVerifyCertificateChainPolicy");
    close_store = (close_store_fn)GetProcAddress(lib, "CertCloseStore");
    free_cert = (free_cert_fn)GetProcAddress(lib, "CertFreeCertificateContext");
    free_chain = (free_chain_fn)GetProcAddress(lib, "CertFreeCertificateChain");
    if (!create_cert || !open_store || !add_cert || !get_chain || !verify_policy ||
        !close_store || !free_cert || !free_chain) { FreeLibrary(lib); return -1; }

    store = open_store(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
    if (!store) goto cleanup;
    int32_t pos = 0;
    for (int32_t i = 0; i < count; ++i) {
        if (total_len - pos < 4) goto cleanup;
        uint32_t n = (uint32_t)certs[pos] | ((uint32_t)certs[pos+1] << 8) |
                     ((uint32_t)certs[pos+2] << 16) | ((uint32_t)certs[pos+3] << 24);
        pos += 4;
        if (n < 1 || n > 65536 || n > (uint32_t)(total_len - pos)) goto cleanup;
        if (i == 0) {
            leaf = create_cert(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, certs + pos, n);
            if (!leaf) goto cleanup;
        } else if (!add_cert(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                             certs + pos, n, CERT_STORE_ADD_ALWAYS, NULL)) {
            goto cleanup;
        }
        pos += (int32_t)n;
    }
    if (pos != total_len) goto cleanup;

    CERT_CHAIN_PARA chain_para;
    memset(&chain_para, 0, sizeof(chain_para));
    chain_para.cbSize = sizeof(chain_para);
    LPSTR server_auth_oid = szOID_PKIX_KP_SERVER_AUTH;
    chain_para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    chain_para.RequestedUsage.Usage.cUsageIdentifier = 1;
    chain_para.RequestedUsage.Usage.rgpszUsageIdentifier = &server_auth_oid;
    if (!get_chain(NULL, leaf, NULL, store, &chain_para,
                   CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT |
                   CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL | CERT_CHAIN_DISABLE_AIA,
                   NULL, &chain) || !chain) { result = -2; goto cleanup; }
    {
        /* 内部辅助逻辑 */
        uint32_t trust_err = chain->TrustStatus.dwErrorStatus;
        if (trust_err != 0) {
            if (trust_err & CERT_TRUST_IS_PARTIAL_CHAIN) result = -3;
            else if (trust_err & CERT_TRUST_IS_UNTRUSTED_ROOT) result = -4;
            else if (trust_err & CERT_TRUST_IS_REVOKED) result = -10;
            else if (trust_err & CERT_TRUST_IS_NOT_TIME_VALID) result = -5;
            else if (trust_err & CERT_TRUST_REVOCATION_STATUS_UNKNOWN) result = -6;
            else result = -7;
            goto cleanup;
        }
    }

    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl;
    memset(&ssl, 0, sizeof(ssl));
    ssl.cbSize = sizeof(ssl);
    ssl.dwAuthType = AUTHTYPE_SERVER;
    ssl.pwszServerName = wide_host;
    CERT_CHAIN_POLICY_PARA policy;
    memset(&policy, 0, sizeof(policy));
    policy.cbSize = sizeof(policy);
    policy.pvExtraPolicyPara = &ssl;
    CERT_CHAIN_POLICY_STATUS status;
    memset(&status, 0, sizeof(status));
    status.cbSize = sizeof(status);
    if (!verify_policy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status)) { result = -9; goto cleanup; }
    if (status.dwError != 0) {
        result = ((HRESULT)status.dwError == CERT_E_CN_NO_MATCH) ? -8 : -9;
        goto cleanup;
    }
    result = 1;
cleanup:
    if (chain) free_chain(chain);
    if (leaf) free_cert(leaf);
    if (store) close_store(store, 0);
    FreeLibrary(lib);
    return result;
}
#endif

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

/* 核心系统底层抽象与内存语义契约 */
int32_t zan_io_socket_alive(intptr_t fd) {
#if defined(_WIN32)
    if ((SOCKET)fd == INVALID_SOCKET) return 0;
    int type = 0;
    int tlen = (int)sizeof(type);
    /* 底层系统交互与数据协议契约 */
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

/* 内部辅助实现 */
#if !defined(_WIN32)

typedef struct zan_io_waiter {
    void *co;
    zan_co_step_t step;
    void *rbuf;
    int   rlen;
    int64_t *out_n;
    intptr_t *out_accept;
    struct zan_io_waiter *next;   /* 核心系统底层抽象与内存语义契约 */
} zan_io_waiter_t;

typedef struct {
    zan_io_waiter_t r;
    zan_io_waiter_t w;
    unsigned char has_r;
    unsigned char has_w;
    unsigned char in_epoll;   /* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */
static int io_slots_ensure(int need) {
    if (need <= g_slots_cap) return 1;
    int cap = g_slots_cap ? g_slots_cap : 256;
    while (cap > 0 && cap < need) cap *= 2;
    if (cap <= 0) return 0;   /* 底层系统交互与数据协议契约 */
    int n = io_shard_count();
    for (int i = 0; i < n; i++) shard_lock(&g_ioshard[i]);
    if (need <= g_slots_cap) {   /* 核心系统底层抽象与内存语义契约 */
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

/* 模块核心语义抽象与接口调用契约 */

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
    /* 内部辅助逻辑 */
    *w->out_n = (rn < 0) ? 0 : (int64_t)rn;
}

/* 内部辅助逻辑 */
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
        *inl = *x;   /* 模块核心语义抽象与接口调用契约 */
        free(x);
    } else {
        inl->next = NULL;
        *has = 0;
    }
    (void)fd;
    /* 内部辅助实现 */
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    if (sh->rto)
        for (int i = 0; i < n; i++) rto_drop(sh, out[i].co);
    return n;
}

/* 内部辅助实现 */
static void io_unlink_waiter(zan_io_slot_t *s, int read_dir,
                             zan_io_waiter_t *w) {
    unsigned char *has = read_dir ? &s->has_r : &s->has_w;
    zan_io_waiter_t *inl = read_dir ? &s->r : &s->w;
    if (w == inl) {
        zan_io_waiter_t *rest = inl->next;
        if (rest) {
            *inl = *rest;   /* 模块核心语义抽象与接口调用契约 */
            free(rest);
        } else {
            inl->next = NULL;
            *has = 0;
        }
        return;
    }
    zan_io_waiter_t *p = inl;
    while (p && p->next != w) p = p->next;
    if (!p) return;   /* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */

#if defined(__linux__)
/* 内部辅助实现 */

/* 内部辅助实现 */
static int io_shard_open(int i) {
    io_shards_prime();
    zan_io_shard_t *sh = &g_ioshard[i];
    if (sh->poll_fd >= 0) return 1;
    sh->poll_fd = epoll_create1(0);
    if (sh->poll_fd < 0) return 0;
    zan_io_fd_cloexec(sh->poll_fd);
    sh->wake_rfd = sh->wake_wfd = eventfd(0, EFD_NONBLOCK);
    zan_io_fd_cloexec(sh->wake_rfd);
    if (sh->wake_rfd >= 0) {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.fd = sh->wake_rfd;
        epoll_ctl(sh->poll_fd, EPOLL_CTL_ADD, sh->wake_rfd, &ev);
    }
    if (i == 0) {
        /* 内部辅助实现 */
        g_dns_wake_fd = eventfd(0, EFD_NONBLOCK);
        zan_io_fd_cloexec(g_dns_wake_fd);
        if (g_dns_wake_fd >= 0) {
            struct epoll_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.events = EPOLLIN;
            ev.data.fd = g_dns_wake_fd;
            epoll_ctl(sh->poll_fd, EPOLL_CTL_ADD, g_dns_wake_fd, &ev);
        }
    }
    return 1;
}

static void io_init_locked(void) {
    if (g_io_started) return;
    io_shard_mutexes_prime();
    zan_io_ignore_sigpipe();
    /* 内部辅助实现 */
    g_io_broken = 0;
    g_io_entries = NULL;
    g_io_count = 0;
    /* Shard 0 here; shards 1 */
    if (!io_shard_open(0))
        /* 内部辅助实现 */
        g_io_broken = 1;
    g_io_started = 1;
}

void zan_io_init(void) {
    /* 内部辅助实现 */
    if (g_io_started) return;
    IO_INIT_LOCK();
    io_init_locked();
    IO_INIT_UNLOCK();
}

static void io_shutdown_locked(void) {
    if (g_slots) {
        for (int fd = 0; fd < g_slots_cap; fd++) {
            zan_io_slot_t *s = &g_slots[fd];
            if (s->in_epoll)
                epoll_ctl(io_shard_of_fd(fd)->poll_fd, EPOLL_CTL_DEL, fd, NULL);
            zan_io_waiter_t *x = s->r.next;
            while (x) { zan_io_waiter_t *n = x->next; free(x); x = n; }
            x = s->w.next;
            while (x) { zan_io_waiter_t *n = x->next; free(x); x = n; }
        }
        free(g_slots);
        g_slots = NULL;
        g_slots_cap = 0;
    }
    g_io_entries = NULL;
    g_io_count = 0;
    int n = io_shard_count();
    for (int i = 0; i < n; i++) {
        zan_io_shard_t *sh = &g_ioshard[i];
        io_dead_clear(sh);
        while (sh->rto) { zan_io_rto_t *e = sh->rto; sh->rto = e->next; free(e); }
    }
    dns_shutdown_cleanup();
    for (int i = 0; i < n; i++) {
        zan_io_shard_t *sh = &g_ioshard[i];
        if (sh->poll_fd >= 0) { close(sh->poll_fd); sh->poll_fd = -1; }
        if (sh->wake_rfd >= 0) { close(sh->wake_rfd); sh->wake_rfd = -1; }
        sh->wake_wfd = -1;   /* 核心系统底层抽象与内存语义契约 */
    }
    if (g_dns_wake_fd >= 0) { close(g_dns_wake_fd); g_dns_wake_fd = -1; }
    g_io_started = 0;
    g_ioshard_prim = 0;
    /* 回退至单反应堆模式 */
#if defined(ZAN_CO_DRIVER)
    InterlockedExchange(&g_shards, 1);
#endif
    g_io_shards_live = 0;
}

void zan_io_shutdown(void) {
    int n = io_shard_count();
    for (int i = 0; i < n; i++) shard_lock(&g_ioshard[i]);
    io_shutdown_locked();
    for (int i = n - 1; i >= 0; i--) shard_unlock(&g_ioshard[i]);
}

static int io_arm(int fd, zan_io_slot_t *s) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd;
    ev.events = EPOLLONESHOT;
    if (s->has_r) ev.events |= EPOLLIN;
    if (s->has_w) ev.events |= EPOLLOUT;
    /* 返回1 when armed, 0 when the backend ultimately refused the fd (EPERM/ENOMEM/ */
    for (int attempt = 0; attempt < 8; attempt++) {
        if (!s->in_epoll) {
            if (epoll_ctl(sh->poll_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
                s->in_epoll = 1;
                return 1;
            }
            if (errno == EINTR) continue;
            if (errno == EEXIST) { s->in_epoll = 1; continue; } /* fall to MOD */
            STRACE("io_arm ADD fail fd=%d epfd=%d errno=%d", fd,
                   (int)sh->poll_fd, errno);
            return 0;
        }
        if (epoll_ctl(sh->poll_fd, EPOLL_CTL_MOD, fd, &ev) == 0) return 1;
        if (errno == EINTR) continue;
        if (errno == ENOENT) { s->in_epoll = 0; continue; } /* re-add */
        STRACE("io_arm MOD fail fd=%d epfd=%d errno=%d", fd,
               (int)sh->poll_fd, errno);
        return 0;
    }
    return 0;
}

static void io_register_locked(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    if (g_io_broken) {   /* 底层系统交互与数据协议契约 */
        io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        return;
    }
    if (io_reject_dead_fd(fd, co, step)) return;
    if ((int)fd >= g_slots_cap) {
        /* 内部辅助实现 */
        shard_unlock(sh);
        int ok = io_slots_ensure((int)fd + 1);
        shard_lock(sh);
        if (!ok) {
            io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
            g_pending_rbuf = NULL;
            g_pending_out_n = NULL;
            g_pending_accept_out = NULL;
            return;
        }
    }
    zan_io_slot_t *s = io_slot((int)fd);
    if (!s) {   /* 模块核心语义抽象与接口调用契约 */
        io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        return;
    }
    zan_io_waiter_t *w;
    unsigned char *has = (interest == ZAN_IO_READ) ? &s->has_r : &s->has_w;
    zan_io_waiter_t *inl = (interest == ZAN_IO_READ) ? &s->r : &s->w;
    if (!*has) {
        w = inl;
        w->next = NULL;
        *has = 1;
    } else {
        /* 同一描述符同向等待者级联链表 */
        w = (zan_io_waiter_t *)calloc(1, sizeof(*w));
        if (!w) {   
/* 内部辅助逻辑 */
            io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
            g_pending_rbuf = NULL;
            g_pending_out_n = NULL;
            g_pending_accept_out = NULL;
            return;
        }
        zan_io_waiter_t *tail = inl;
        while (tail->next) tail = tail->next;
        tail->next = w;
    }
    w->co = co;
    w->step = step;
    w->rbuf = g_pending_rbuf;
    w->rlen = g_pending_rlen;
    w->out_n = g_pending_out_n;
    w->out_accept = g_pending_accept_out;
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
    g_pending_accept_out = NULL;
    InterlockedIncrement(&g_io_count);

    if (!io_arm(fd, s)) {
        /* 内部辅助实现 */
        /* 内部辅助实现 */
        int64_t *fail_out_n = w->out_n;
        intptr_t *fail_accept = w->out_accept;
        io_unlink_waiter(s, interest == ZAN_IO_READ ? 1 : 0, w);
        InterlockedDecrement(&g_io_count);
        io_mark_dead(sh, co, step, fail_out_n, fail_accept);
    }
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    io_register_locked(fd, interest, co, step);
    shard_unlock(sh);
}

/* 内部辅助逻辑 */

/* 接收超时：向协程传递 -1 错误码并唤醒 */
static int rto_timeout_scan(zan_io_shard_t *sh) {
    if (!sh->rto) return 0;
    int woke = 0;
    long long now = dns_now_ms();
    zan_io_rto_t **pp = &sh->rto;
    while (*pp) {
        zan_io_rto_t *e = *pp;
        if (e->due_ms > now) { pp = &e->next; continue; }
        *pp = e->next;
        int fd = e->fd;
        zan_io_slot_t *s = (fd >= 0 && fd < g_slots_cap) ? &g_slots[fd] : NULL;
        zan_io_waiter_t *w = s ? io_find_read_waiter(s, e->frame) : NULL;
        if (w) {
            io_unlink_waiter(s, 1, w);
            InterlockedDecrement(&g_io_count);
            if (s->has_r || s->has_w) {
                if (!io_arm(fd, s)) io_fail_slot_waiters(fd, s);
            }
            if (e->out_n) *e->out_n = -1;
            io_wake(e->frame, e->step);
            woke++;
        }
        free(e);
    }
    return woke;
}

int32_t zan_io_poll(int64_t timeout_ms) {
    return io_poll_shard(io_thread_shard(), timeout_ms);
}

/* 核心系统底层抽象与内存语义契约 */
static int32_t io_poll_shard(int shard, int64_t timeout_ms) {
    zan_io_shard_t *sh = &g_ioshard[shard];
    shard_lock(sh);
    if (sh->dead) {
        int wd = io_flush_dead(sh);
        shard_unlock(sh);
        return wd;
    }
    if (sh->rto) {   /* 底层系统交互与数据协议契约 */
        int wr = rto_timeout_scan(sh);
        if (wr) {
            shard_unlock(sh);
            return wr;
        }
    }
    int empty = (g_io_count == 0 && g_blocking_inflight == 0);
    shard_unlock(sh);
    if (empty) return 0;
    /* 内部辅助实现 */
    struct epoll_event events[256];
    for (;;) {
        int64_t wait = dns_wait_ms(timeout_ms);
        int capped = (wait < 0 || wait > ZAN_IO_SWEEP_MS);
        if (capped) wait = ZAN_IO_SWEEP_MS;
        STRACE("shard%d epoll wait=%lld to=%lld", shard, (long long)wait, (long long)timeout_ms);
        int n = epoll_wait(sh->poll_fd, events, 256, (int)wait);
        if (n < 0) STRACE("shard%d epoll n=%d errno=%d", shard, n, errno);
        else STRACE("shard%d epoll n=%d", shard, n);
        shard_lock(sh);
        /* 内部辅助实现 */
        if (n < 0) n = 0;
        if (n != 0) {
            int woke = 0;
            for (int i = 0; i < n; i++) {
                int fd = events[i].data.fd;
                if (fd == sh->wake_rfd) {
                    /* 调度器内部唤醒事件消费 */
                    io_shard_wake_consume(sh);
                    continue;
                }
                if (fd == g_dns_wake_fd) {
                    /* 异步 DNS 解析完成派发 */
                    dns_wake_read();
                    woke += dns_drain();
                    continue;
                }
                if (fd < 0 || fd >= g_slots_cap) continue;
                zan_io_slot_t *s = &g_slots[fd];
                uint32_t ev = events[i].events;
                int err = (ev & (EPOLLERR | EPOLLHUP)) != 0;

                zan_io_waiter_t ready[8];
                int nr = 0;
                if ((ev & EPOLLIN) || err)  nr += io_take(s, fd, 1, ready + nr, 8 - nr);
                if ((ev & EPOLLOUT) || err) nr += io_take(s, fd, 0, ready + nr, 8 - nr);

                /* 内部辅助实现 */
                if (s->has_r || s->has_w) {
                    if (!io_arm(fd, s)) io_fail_slot_waiters(fd, s);
                }

                for (int k = 0; k < nr; k++) {
                    io_deliver_waiter(fd, &ready[k]);
                    InterlockedDecrement(&g_io_count);
                    io_wake(ready[k].co, ready[k].step);
                    woke++;
                }
            }
            shard_unlock(sh);
            return woke;
        }
        int w2 = dns_timeout_scan();
        int w3 = w2 ? 0 : dns_drain();
        int w4 = w3 ? 0 : io_sweep_slots(shard);
        /* 内部辅助实现 */
        int w5 = w4 ? 0 : rto_timeout_scan(sh);
        int done = 1, val = 0;
        if      (w2) val = w2;
        else if (w3) val = w3;
        else if (w4) val = w4;
        else if (w5) val = w5;
        /* 内部辅助实现 */
        else if (capped && g_io_count + g_blocking_inflight > 0 && timeout_ms < 0) done = 0;
        shard_unlock(sh);
        if (done) return val;
    }
}

/* 内部辅助实现 */
static void io_close_notify_slots(intptr_t fdp) {
    int fd = (int)fdp;
    if (fd < 0 || fd >= g_slots_cap) return;
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    zan_io_slot_t *s = &g_slots[fd];
    if (s->in_epoll) {
        s->in_epoll = 0;
#if defined(__linux__)
        epoll_ctl(sh->poll_fd, EPOLL_CTL_DEL, fd, NULL);
#else
        struct kevent ke;
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(sh->poll_fd, &ke, 1, NULL, 0, NULL);
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(sh->poll_fd, &ke, 1, NULL, 0, NULL);
#endif
        /* 内部辅助逻辑 */
    }
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
        }
    }
}
void zan_io_close_notify(intptr_t fd) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    io_close_notify_slots(fd);
    shard_unlock(sh);
}

#elif defined(__APPLE__) || defined(__FreeBSD__)
/* ==================== KQUEUE ==================== */

/* 内部辅助实现 */
static int io_shard_open(int i) {
    io_shards_prime();
    zan_io_shard_t *sh = &g_ioshard[i];
    if (sh->poll_fd >= 0) return 1;
    sh->poll_fd = kqueue();
    if (sh->poll_fd < 0) return 0;
    zan_io_fd_cloexec(sh->poll_fd);
    int pfd[2];
    if (pipe(pfd) == 0) {
        sh->wake_rfd = pfd[0];
        sh->wake_wfd = pfd[1];
        fcntl(pfd[0], F_SETFL, O_NONBLOCK);
        zan_io_fd_cloexec(pfd[0]);
        zan_io_fd_cloexec(pfd[1]);
        struct kevent kev;
        EV_SET(&kev, (uintptr_t)pfd[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
        kevent(sh->poll_fd, &kev, 1, NULL, 0, NULL);
    }
    if (i == 0 && g_dns_wake_fd < 0) {
        /* 内部辅助实现 */
        if (pipe(pfd) == 0) {
            g_dns_wake_fd = pfd[0];
            g_dns_wake_wfd = pfd[1];
            fcntl(pfd[0], F_SETFL, O_NONBLOCK);
            zan_io_fd_cloexec(pfd[0]);
            zan_io_fd_cloexec(pfd[1]);
            struct kevent kev;
            EV_SET(&kev, (uintptr_t)pfd[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
            kevent(sh->poll_fd, &kev, 1, NULL, 0, NULL);
        }
    }
    return 1;
}

static void io_init_locked(void) {
    if (g_io_started) return;
    io_shard_mutexes_prime();
    zan_io_ignore_sigpipe();
    /* 初始化反应堆上下文状态 */
    g_io_broken = 0;
    g_io_entries = NULL;
    g_io_count = 0;
    /* Shard 0 here; shards 1 */
    if (!io_shard_open(0))
        /* 反应堆后端故障标记与快速失败处理 */
        g_io_broken = 1;
    g_io_started = 1;
}

void zan_io_init(void) {
    /* 内部辅助实现 */
    if (g_io_started) return;
    IO_INIT_LOCK();
    io_init_locked();
    IO_INIT_UNLOCK();
}

void zan_io_shutdown(void) {
    int n = io_shard_count();
    for (int i = 0; i < n; i++) shard_lock(&g_ioshard[i]);
    io_shutdown_locked();
    for (int i = n - 1; i >= 0; i--) shard_unlock(&g_ioshard[i]);
}

static void io_shutdown_locked(void) {
    if (g_slots) {
        for (int fd = 0; fd < g_slots_cap; fd++) {
            zan_io_slot_t *s = &g_slots[fd];
            zan_io_waiter_t *x = s->r.next;
            while (x) { zan_io_waiter_t *n = x->next; free(x); x = n; }
            x = s->w.next;
            while (x) { zan_io_waiter_t *n = x->next; free(x); x = n; }
        }
        free(g_slots);
        g_slots = NULL;
        g_slots_cap = 0;
    }
    g_io_count = 0;
    int n = io_shard_count();
    for (int i = 0; i < n; i++) {
        zan_io_shard_t *sh = &g_ioshard[i];
        io_dead_clear(sh);
        while (sh->rto) { zan_io_rto_t *e = sh->rto; sh->rto = e->next; free(e); }
    }
    dns_shutdown_cleanup();
    for (int i = 0; i < n; i++) {
        zan_io_shard_t *sh = &g_ioshard[i];
        if (sh->poll_fd >= 0) { close(sh->poll_fd); sh->poll_fd = -1; }
        if (sh->wake_rfd >= 0) { close(sh->wake_rfd); sh->wake_rfd = -1; }
        if (sh->wake_wfd >= 0) { close(sh->wake_wfd); sh->wake_wfd = -1; }
    }
    if (g_dns_wake_fd >= 0) { close(g_dns_wake_fd); g_dns_wake_fd = -1; }
    if (g_dns_wake_wfd >= 0) { close(g_dns_wake_wfd); g_dns_wake_wfd = -1; }
    g_io_started = 0;
    g_ioshard_prim = 0;
    /* 回退至单反应堆模式 */
#if defined(ZAN_CO_DRIVER)
    InterlockedExchange(&g_shards, 1);
#endif
    g_io_shards_live = 0;
}

static void io_register_locked(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    if (g_io_broken) {   /* 底层系统交互与数据协议契约 */
        io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        return;
    }
    if (io_reject_dead_fd(fd, co, step)) return;
    if ((int)fd >= g_slots_cap) {
        /* 内部辅助实现 */
        shard_unlock(sh);
        int ok = io_slots_ensure((int)fd + 1);
        shard_lock(sh);
        if (!ok) {
            io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
            g_pending_rbuf = NULL;
            g_pending_out_n = NULL;
            g_pending_accept_out = NULL;
            return;
        }
    }
    zan_io_slot_t *s = io_slot((int)fd);
    if (!s) {   /* 模块核心语义抽象与接口调用契约 */
        io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        return;
    }
    zan_io_waiter_t *w;
    unsigned char *has = (interest == ZAN_IO_READ) ? &s->has_r : &s->has_w;
    zan_io_waiter_t *inl = (interest == ZAN_IO_READ) ? &s->r : &s->w;
    if (!*has) {
        w = inl;
        w->next = NULL;
        *has = 1;
    } else {
        /* 同一描述符同向等待者级联链表 */
        w = (zan_io_waiter_t *)calloc(1, sizeof(*w));
        if (!w) {   
/* 内部辅助逻辑 */
            io_mark_dead(sh, co, step, g_pending_out_n, g_pending_accept_out);
            g_pending_rbuf = NULL;
            g_pending_out_n = NULL;
            g_pending_accept_out = NULL;
            return;
        }
        zan_io_waiter_t *tail = inl;
        while (tail->next) tail = tail->next;
        tail->next = w;
    }
    w->co = co;
    w->step = step;
    w->rbuf = g_pending_rbuf;
    w->rlen = g_pending_rlen;
    w->out_n = g_pending_out_n;
    w->out_accept = g_pending_accept_out;
    /* 内部辅助实现 */
    {
        int64_t *fail_out_n = g_pending_out_n;
        intptr_t *fail_accept = g_pending_accept_out;
        g_pending_rbuf = NULL;
        g_pending_out_n = NULL;
        g_pending_accept_out = NULL;
        InterlockedIncrement(&g_io_count);

        /* 内部辅助实现 */
        struct kevent kev;
        short filter = (interest == ZAN_IO_READ) ? EVFILT_READ : EVFILT_WRITE;
        EV_SET(&kev, fd, filter, EV_ADD | EV_ONESHOT, 0, 0, NULL);
        if (kevent(sh->poll_fd, &kev, 1, NULL, 0, NULL) != 0) {
            /* 描述符失效：立即失败唤醒防止永久挂起 */
            io_unlink_waiter(s, interest == ZAN_IO_READ ? 1 : 0, w);
            InterlockedDecrement(&g_io_count);
            io_mark_dead(sh, co, step, fail_out_n, fail_accept);
            return;
        }
    }
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    io_register_locked(fd, interest, co, step);
    shard_unlock(sh);
}

/* 接收超时：向协程传递 -1 错误码并唤醒 */
static int rto_timeout_scan(zan_io_shard_t *sh) {
    if (!sh->rto) return 0;
    int woke = 0;
    long long now = dns_now_ms();
    zan_io_rto_t **pp = &sh->rto;
    while (*pp) {
        zan_io_rto_t *e = *pp;
        if (e->due_ms > now) { pp = &e->next; continue; }
        *pp = e->next;
        int fd = e->fd;
        zan_io_slot_t *s = (fd >= 0 && fd < g_slots_cap) ? &g_slots[fd] : NULL;
        zan_io_waiter_t *w = s ? io_find_read_waiter(s, e->frame) : NULL;
        if (w) {
            io_unlink_waiter(s, 1, w);
            InterlockedDecrement(&g_io_count);
            if (e->out_n) *e->out_n = -1;
            io_wake(e->frame, e->step);
            woke++;
        }
        free(e);
    }
    return woke;
}

int32_t zan_io_poll(int64_t timeout_ms) {
    return io_poll_shard(io_thread_shard(), timeout_ms);
}

/* 核心系统底层抽象与内存语义契约 */
static int32_t io_poll_shard(int shard, int64_t timeout_ms) {
    zan_io_shard_t *sh = &g_ioshard[shard];
    shard_lock(sh);
    if (sh->dead) {
        int wd = io_flush_dead(sh);
        shard_unlock(sh);
        return wd;
    }
    if (sh->rto) {   /* 底层系统交互与数据协议契约 */
        int wr = rto_timeout_scan(sh);
        if (wr) {
            shard_unlock(sh);
            return wr;
        }
    }
    int empty = (g_io_count == 0 && g_blocking_inflight == 0);
    shard_unlock(sh);
    if (empty) return 0;
    /* 内部辅助实现 */
    struct kevent events[64];
    for (;;) {
        int64_t wait = dns_wait_ms(timeout_ms);
        int capped = (wait < 0 || wait > ZAN_IO_SWEEP_MS);
        if (capped) wait = ZAN_IO_SWEEP_MS;
        struct timespec ts = { wait / 1000, (wait % 1000) * 1000000L };
        int n = kevent(sh->poll_fd, NULL, 0, events, 64, &ts);
        shard_lock(sh);
        /* 内部辅助实现 */
        if (n < 0) n = 0;
        if (n != 0) {
            int woke = 0;
            for (int i = 0; i < n; i++) {
                int fd = (int)events[i].ident;
                if (fd == sh->wake_rfd) {
                    /* 调度器内部唤醒事件消费 */
                    io_shard_wake_consume(sh);
                    continue;
                }
                if (fd == g_dns_wake_fd) {
                    /* 异步 DNS 解析完成派发 */
                    dns_wake_read();
                    woke += dns_drain();
                    continue;
                }
                if (fd < 0 || fd >= g_slots_cap) continue;
                zan_io_slot_t *s = &g_slots[fd];
                zan_io_waiter_t ready[8];
                int nr = 0;
                if (events[i].filter == EVFILT_READ)
                    nr += io_take(s, fd, 1, ready + nr, 8 - nr);
                if (events[i].filter == EVFILT_WRITE)
                    nr += io_take(s, fd, 0, ready + nr, 8 - nr);
                /* 消费单次触发过滤器事件 */
                if ((events[i].filter == EVFILT_READ && s->has_r) ||
                    (events[i].filter == EVFILT_WRITE && s->has_w)) {
                    struct kevent rk;
                    int retries;
                    EV_SET(&rk, (uintptr_t)fd, events[i].filter,
                           EV_ADD | EV_ONESHOT, 0, 0, NULL);
                    int rearmed = 0;
                    for (retries = 0; retries < 8 && !rearmed; retries++) {
                        if (kevent(sh->poll_fd, &rk, 1, NULL, 0, NULL) == 0)
                            rearmed = 1;
                        else if (errno != EINTR) break;
                    }
                    /* 内部辅助实现 */
                    if (!rearmed &&
                        ((events[i].filter == EVFILT_READ && s->has_r) ||
                         (events[i].filter == EVFILT_WRITE && s->has_w))) {
                        io_fail_slot_waiters(fd, s);
                    }
                }
                for (int k = 0; k < nr; k++) {
                    io_deliver_waiter(fd, &ready[k]);
                    InterlockedDecrement(&g_io_count);
                    io_wake(ready[k].co, ready[k].step);
                    woke++;
                }
            }
            shard_unlock(sh);
            return woke;
        }
        int w2 = dns_timeout_scan();
        int w3 = w2 ? 0 : dns_drain();
        int w4 = w3 ? 0 : io_sweep_slots(shard);
        /* 内部辅助实现 */
        int w5 = w4 ? 0 : rto_timeout_scan(sh);
        int done = 1, val = 0;
        if      (w2) val = w2;
        else if (w3) val = w3;
        else if (w4) val = w4;
        else if (w5) val = w5;
        /* 内部辅助实现 */
        else if (capped && g_io_count + g_blocking_inflight > 0 && timeout_ms < 0) done = 0;
        shard_unlock(sh);
        if (done) return val;
    }
}

/* 内部辅助实现 */
static void io_close_notify_slots(intptr_t fdp) {
    int fd = (int)fdp;
    if (fd < 0 || fd >= g_slots_cap) return;
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    zan_io_slot_t *s = &g_slots[fd];
    if (s->in_epoll) {
        s->in_epoll = 0;
#if defined(__linux__)
        epoll_ctl(sh->poll_fd, EPOLL_CTL_DEL, fd, NULL);
#else
        struct kevent ke;
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(sh->poll_fd, &ke, 1, NULL, 0, NULL);
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(sh->poll_fd, &ke, 1, NULL, 0, NULL);
#endif
        /* 内部辅助逻辑 */
    }
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
        }
    }
}
void zan_io_close_notify(intptr_t fd) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    io_close_notify_slots(fd);
    shard_unlock(sh);
}

#elif defined(_WIN32)
/* 核心系统底层抽象与内存语义契约 */
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

/* Windows IOCP 进行中的重叠 I/O 状态记录 */
typedef struct zan_io_op {
    OVERLAPPED ov;
    SOCKET     sock;
    int        interest;     /* ZAN_IO_READ / ZAN_IO_WRITE */
    int        kind;         /* readiness/recv/accept */
    int        rto;          
/* 内部辅助实现 */
    void      *co;           /* 核心系统底层抽象与内存语义契约 */
    zan_co_step_t step;      /* 核心系统底层抽象与内存语义契约 */
    int64_t   *out_n;        /* 底层系统交互与数据协议契约 */
    SOCKET     accepted;
    void      *accept_buf;
} zan_io_op_t;

enum {
    ZAN_IO_OP_READY,
    ZAN_IO_OP_RECV,
    ZAN_IO_OP_ACCEPT
};

static HANDLE g_iocp;

/* 内部辅助实现 */
typedef struct zan_io_rto zan_io_rto_t;
static zan_io_rto_t *g_rto;
static CRITICAL_SECTION g_rto_lock;

#if defined(ZAN_CO_DRIVER)
/* IOCP 立即完成同步快路径优化 */
static volatile LONG g_syncfast = -1;   /* -1 unread, 1 on, 0 off */

static volatile LONG g_sync_inline;     /* 核心系统底层抽象与内存语义契约 */

static int syncfast_on(void) {
    LONG v = g_syncfast;
    if (v < 0) {
        /* 内部辅助实现 */
        int cfg = zan_async_cfg_sync_fast();
        if (cfg >= 0) {
            v = cfg;
        } else {
            const char *e = getenv("ZAN_IO_SYNCFAST");
            v = (e && *e && e[0] != '0') ? 1 : 0;
        }
        InterlockedExchange(&g_syncfast, v);
    }
    return (int)v;
}

/* 内部辅助逻辑 */
#define ZAN_IO_MAXSHARD 256
static HANDLE        g_shard[ZAN_IO_MAXSHARD];
static volatile LONG g_shards = 1;

static HANDLE io_shard(int i) {
    if (i <= 0 || i >= (int)g_shards) return g_iocp;
    return g_shard[i] ? g_shard[i] : g_iocp;
}

/* 核心系统底层抽象与内存语义契约 */
static HANDLE io_shard_of(SOCKET s) {
    int n = (int)g_shards;
    if (n <= 1) return g_iocp;
    return io_shard((int)((((uintptr_t)s) >> 2) % (unsigned)n));
}

/* 初始化 n 个 IOCP 分片完成端口 */
static void io_shards_start(int n) {
    /* AsyncRuntime */
    int cfg = zan_async_cfg_io_shards();
    if (cfg > 0 && cfg < n) n = cfg;
    const char *e = getenv("ZAN_IO_SHARDS");
    if (e && *e) {
        int v = atoi(e);
        if (v > 0 && v < n) n = v;
    }
    if (n > ZAN_IO_MAXSHARD) n = ZAN_IO_MAXSHARD;
    for (int i = 1; i < n; i++) {
        if (!g_shard[i])
            g_shard[i] = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (!g_shard[i]) { n = i; break; }   /* 核心系统底层抽象与内存语义契约 */
    }
    InterlockedExchange(&g_shards, n < 1 ? 1 : n);
}

static void io_shards_stop(void) {
    InterlockedExchange(&g_shards, 1);
    for (int i = 1; i < ZAN_IO_MAXSHARD; i++) {
        if (g_shard[i]) { CloseHandle(g_shard[i]); g_shard[i] = NULL; }
    }
}
#endif

/* 内部辅助实现 */
#if defined(ZAN_CO_DRIVER)
/* 内部辅助实现 */
#if defined(__GNUC__)
static SLIST_HEADER g_op_slist __attribute__((aligned(MEMORY_ALLOCATION_ALIGNMENT)));
#else
static __declspec(align(MEMORY_ALLOCATION_ALIGNMENT)) SLIST_HEADER g_op_slist;
#endif

static zan_io_op_t *op_alloc(void) {
    zan_io_op_t *op = (zan_io_op_t *)InterlockedPopEntrySList(&g_op_slist);
    if (!op)
        op = (zan_io_op_t *)_aligned_malloc(sizeof(zan_io_op_t),
                                            MEMORY_ALLOCATION_ALIGNMENT);
    if (op) memset(op, 0, sizeof(*op));
    return op;
}
static void op_free(zan_io_op_t *op) {
    InterlockedPushEntrySList(&g_op_slist, (PSLIST_ENTRY)op);
}
#else
static zan_io_op_t *g_op_pool;   /* 底层系统交互与数据协议契约 */

static zan_io_op_t *op_alloc(void) {
    zan_io_op_t *op = g_op_pool;
    if (op) {
        g_op_pool = *(zan_io_op_t **)op;
        memset(op, 0, sizeof(*op));
    } else {
        op = (zan_io_op_t *)calloc(1, sizeof(*op));
    }
    return op;
}
static void op_free(zan_io_op_t *op) {
    *(zan_io_op_t **)op = g_op_pool;
    g_op_pool = op;
}
#endif

/* 内部辅助逻辑 */

/* 将新建套接字关联至 IOCP 完成端口 */
static ZAN_MAYBE_UNUSED void mark_assoc(SOCKET s) {
#if defined(ZAN_CO_DRIVER)
    HANDLE port = io_shard_of(s);
#else
    HANDLE port = g_iocp;
#endif
    if (CreateIoCompletionPort((HANDLE)s, port, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;
    }
}

static void io_complete_op(zan_io_op_t *op, DWORD transferred, ULONG_PTR status);

/* 在首次重叠调用前绑定套接字与 IOCP 完成端口 */
static int ensure_assoc(SOCKET s) {
#if defined(ZAN_CO_DRIVER)
    HANDLE port = io_shard_of(s);
#else
    HANDLE port = g_iocp;
#endif
    if (CreateIoCompletionPort((HANDLE)s, port, (ULONG_PTR)s, 0) == NULL) {
        DWORD e = GetLastError();
        (void)e;   /* 底层系统交互与数据协议契约 */
    }
#if defined(ZAN_CO_DRIVER)
    /* 内部辅助实现 */
    if (!syncfast_on()) return 0;
    return SetFileCompletionNotificationModes(
               (HANDLE)s, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS) ? 1 : 0;
#else
    return 0;
#endif
}

/* 内部辅助实现 */
static SRWLOCK g_io_init_srw = SRWLOCK_INIT;
static int g_rto_lock_ready;

void zan_io_init(void) {
    if (g_io_started) return;
    AcquireSRWLockExclusive(&g_io_init_srw);
    if (g_io_started) { ReleaseSRWLockExclusive(&g_io_init_srw); return; }
    zan__crash_install();
    g_io_count = 0;
    /* 内部辅助实现 */
    g_io_broken = 0;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (g_iocp == NULL) g_io_broken = 1;
    if (!g_rto_lock_ready) {
        InitializeCriticalSection(&g_rto_lock);
        g_rto_lock_ready = 1;
    }
#if defined(ZAN_CO_DRIVER)
    InitializeSListHead(&g_op_slist);   /* 底层系统交互与数据协议契约 */
#endif
    g_io_started = 1;
    ReleaseSRWLockExclusive(&g_io_init_srw);
}

void zan_io_shutdown(void) {
#if defined(ZAN_CO_DRIVER)
    io_shards_stop();
#endif
    if (g_iocp) { CloseHandle(g_iocp); g_iocp = NULL; }
    g_io_count = 0;
    /* 内部辅助逻辑 */
    dns_shutdown_cleanup();
    while (g_rto) {   /* 模块核心语义抽象与接口调用契约 */
        void *e = g_rto;
        g_rto = *(zan_io_rto_t **)e;   /* next @ offset 0 (fwd-decl'd type) */
        free(e);
    }
#if defined(ZAN_CO_DRIVER)
    for (;;) {
        PSLIST_ENTRY e = InterlockedPopEntrySList(&g_op_slist);
        if (!e) break;
        _aligned_free(e);
    }
#else
    while (g_op_pool) {
        zan_io_op_t *nx = *(zan_io_op_t **)g_op_pool;
        free(g_op_pool);
        g_op_pool = nx;
    }
#endif
    WSACleanup();
#if defined(ZAN_CO_DRIVER)
    LONG requested = InterlockedExchange(&g_socket_cleanup_requested, 0);
    while (requested-- > 0) WSACleanup();
#endif
    g_io_started = 0;
}

int32_t zan_io_set_nonblocking(intptr_t fd) {
    u_long mode = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &mode);
}

/* 原子更新进行中重叠 I/O 操作计数 */
#if defined(ZAN_CO_DRIVER)
#define IO_CNT_INC() InterlockedIncrement((volatile LONG *)&g_io_count)
#define IO_CNT_DEC() InterlockedDecrement((volatile LONG *)&g_io_count)
#else
#define IO_CNT_INC() (g_io_count++)
#define IO_CNT_DEC() (g_io_count--)
#endif

#if defined(ZAN_CO_DRIVER)
/* 监听套接字类别判定 */
static int io_is_listening(SOCKET s) {
    int val = 0, len = (int)sizeof(val);
    if (getsockopt(s, SOL_SOCKET, SO_ACCEPTCONN, (char *)&val, &len) == 0)
        return val != 0;
    return 0;
}
#endif

/* 投递 0 字节重叠 I/O 用于就绪事件探测 */
static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    if (g_io_broken) {
        /* 内部辅助逻辑 */
        if (step) zan_co_ready(co, step);
        return;
    }
    SOCKET s = (SOCKET)fd;

#if defined(ZAN_CO_DRIVER)
    /* 内部辅助实现 */
    if (interest == ZAN_IO_READ && step && io_is_listening(s)) {
        zan_co_delay(1, co, step);
        return;
    }
#endif

    int skip = ensure_assoc(s);
    (void)skip;   /* 底层系统交互与数据协议契约 */
    IOTRACE("io_register(READY) fd=%lld interest=%d cnt=%ld", (long long)s, interest, g_io_count);

    zan_io_op_t *op = op_alloc();
    if (!op) {   /* 模块核心语义抽象与接口调用契约 */
        if (step) zan_co_ready(co, step);
        return;
    }
    op->sock = s;
    op->interest = interest;
    op->kind = ZAN_IO_OP_READY;
    op->co = co;
    op->step = step;
    IO_CNT_INC();

    WSABUF b;
    b.len = 0;
    b.buf = NULL;
    int r, e;
    if (interest == ZAN_IO_READ) {
        /* 内部辅助逻辑 */
        DWORD flags = 0;
        int sotype = 0, solen = (int)sizeof(sotype);
        if (getsockopt(s, SOL_SOCKET, SO_TYPE, (char *)&sotype, &solen) == 0 &&
            sotype == SOCK_DGRAM)
            flags = MSG_PEEK;
        r = WSARecv(s, &b, 1, NULL, &flags, &op->ov, NULL);
    } else {
        r = WSASend(s, &b, 1, NULL, 0, &op->ov, NULL);
    }
    IOTRACE("ready_co fd=%lld interest=%d op=%p -> r=%d err=%d cnt=%ld",
            (long long)s, interest, (void*)op, r, r?WSAGetLastError():0, g_io_count);
    if (r == 0) {
#if defined(ZAN_CO_DRIVER)
        /* 核心系统底层抽象与内存语义契约 */
        if (skip) {
            op_free(op);
            IO_CNT_DEC();
            InterlockedIncrement(&g_sync_inline);
            if (step) zan_co_ready(co, step);
        }
        return;
#else
        return;                  /* 底层系统交互与数据协议契约 */
#endif
    }
    e = WSAGetLastError();
    if (e == WSA_IO_PENDING) return;
    /* 致命错误：排队完成包以确保协程正常恢复 */
#if defined(ZAN_CO_DRIVER)
    if (!PostQueuedCompletionStatus(io_shard_of(s), 0, (ULONG_PTR)s, &op->ov)) {
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov)) {
#endif
        /* 内部辅助逻辑 */
        io_wake(co, step);
        op_free(op);
        IO_CNT_DEC();
    }
}

/* 核心系统底层抽象与内存语义契约 */
void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
                    zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
    /* 内部辅助实现 */
    if (g_io_broken) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }
    SOCKET s = (SOCKET)fd;
    int skip = ensure_assoc(s);
    (void)skip;

    /* 内部辅助逻辑 */
    if (len <= 0 || len > 0x7FFFFFF0) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }

    zan_io_op_t *op = op_alloc();
    if (!op) {
        if (out_n) *out_n = 0;   /* 底层系统交互与数据协议契约 */
        if (step) zan_co_ready(frame, step);
        return;
    }
    op->sock = s;
    op->interest = ZAN_IO_READ;
    op->kind = ZAN_IO_OP_RECV;
    op->co = frame;
    op->step = step;
    op->out_n = out_n;
    IO_CNT_INC();

    WSABUF b;
    b.len = (ULONG)len;
    b.buf = (char *)buf;
    DWORD flags = 0, got = 0;
    int r = WSARecv(s, &b, 1, &got, &flags, &op->ov, NULL);
    IOTRACE("recv_co fd=%lld len=%d op=%p -> r=%d got=%lu err=%d cnt=%ld", (long long)fd, len, (void*)op, r, (unsigned long)got, r?WSAGetLastError():0, g_io_count);
    if (r == 0) {
#if defined(ZAN_CO_DRIVER)
        /* 核心系统底层抽象与内存语义契约 */
        if (skip) {
            if (out_n) *out_n = (int64_t)got;
            op_free(op);
            IO_CNT_DEC();
            InterlockedIncrement(&g_sync_inline);
            if (step) zan_co_ready(frame, step);
        }
        return;
#else
        return;   /* 模块核心语义抽象与接口调用契约 */
#endif
    }
    int e = WSAGetLastError();
    if (e == WSA_IO_PENDING) return;
    /* 对端关闭：投递 0 字节事件包通知协程 */
    if (out_n) *out_n = 0;
#if defined(ZAN_CO_DRIVER)
    if (!PostQueuedCompletionStatus(io_shard_of(s), 0, (ULONG_PTR)s, &op->ov)) {
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov)) {
#endif
        /* 内部辅助实现 */
        if (step) zan_co_ready(frame, step);
        op_free(op);
        IO_CNT_DEC();
    }
}

/* 核心系统底层抽象与内存语义契约 */
struct zan_io_rto {
    SOCKET          s;
    void           *frame;
    zan_co_step_t   step;
    OVERLAPPED     *ov;      /* 核心系统底层抽象与内存语义契约 */
    int64_t        *out_n;
    long long       due_ms;
    struct zan_io_rto *next;
};

static void rto_insert(zan_io_rto_t *e) {
    EnterCriticalSection(&g_rto_lock);
    e->next = g_rto;
    g_rto = e;
    LeaveCriticalSection(&g_rto_lock);
}

/* 注销并回收目标协程帧的等待节点 */
static int rto_claim(void *frame) {
    int found = 0;
    EnterCriticalSection(&g_rto_lock);
    zan_io_rto_t **pp = &g_rto;
    while (*pp) {
        if ((*pp)->frame == frame) {
            zan_io_rto_t *e = *pp;
            *pp = e->next;
            free(e);
            found = 1;
            break;
        }
        pp = &(*pp)->next;
    }
    LeaveCriticalSection(&g_rto_lock);
    return found;
}

/* 内部辅助实现 */
static long long rto_wait_ms(long long caller_ms) {
    if (!g_rto) return caller_ms;
    long long now = dns_now_ms();
    long long nearest = -1;
    EnterCriticalSection(&g_rto_lock);
    for (zan_io_rto_t *e = g_rto; e; e = e->next)
        if (nearest < 0 || e->due_ms < nearest) nearest = e->due_ms;
    LeaveCriticalSection(&g_rto_lock);
    if (nearest < 0) return caller_ms;
    long long wait = nearest - now;
    if (wait < 0) wait = 0;
    if (caller_ms < 0) return wait;
    return wait < caller_ms ? wait : caller_ms;
}

static int rto_timeout_scan(void) {
    if (!g_rto) return 0;
    int woke = 0;
    long long now = dns_now_ms();
    for (;;) {
        zan_io_rto_t *e = NULL;
        EnterCriticalSection(&g_rto_lock);
        zan_io_rto_t **pp = &g_rto;
        while (*pp) {
            if ((*pp)->due_ms <= now) {
                e = *pp;
                *pp = e->next;
                break;
            }
            pp = &(*pp)->next;
        }
        LeaveCriticalSection(&g_rto_lock);
        if (!e) break;
        /* 超时竞态胜出：派发 -1 并就绪协程 */
        zan_io_op_t *win_op = CONTAINING_RECORD(e->ov, zan_io_op_t, ov);
        win_op->rto = 2;
        CancelIoEx((HANDLE)e->s, e->ov);   /* 核心系统底层抽象与内存语义契约 */
        if (e->out_n) *e->out_n = -1;
        io_wake(e->frame, e->step);
        free(e);
        woke++;
    }
    return woke;
}

void zan_io_recv_to_co(intptr_t fd, void *buf, int32_t len, int64_t timeout_ms,
                       void *frame, zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
    if (g_io_broken) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }
    SOCKET s = (SOCKET)fd;
    int skip = ensure_assoc(s);
    (void)skip;
    /* 内部辅助逻辑 */
    if (len <= 0 || len > 0x7FFFFFF0) {
        if (out_n) *out_n = 0;
        if (step) zan_co_ready(frame, step);
        return;
    }
    zan_io_op_t *op = op_alloc();
    if (!op) {
        if (out_n) *out_n = 0;   /* 底层系统交互与数据协议契约 */
        if (step) zan_co_ready(frame, step);
        return;
    }
    op->sock = s;
    op->interest = ZAN_IO_READ;
    op->kind = ZAN_IO_OP_RECV;
    op->rto = 1;
    op->co = frame;
    op->step = step;
    op->out_n = out_n;
    IO_CNT_INC();
    /* 内部辅助实现 */
    zan_io_rto_t *e = (zan_io_rto_t *)calloc(1, sizeof(*e));
    if (e) {
        e->s = s;
        e->frame = frame;
        e->step = step;
        e->ov = &op->ov;
        e->out_n = out_n;
        e->due_ms = dns_now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
        rto_insert(e);
    }
    /* 内部辅助实现 */
    else op->rto = 0;

    WSABUF b;
    b.len = (ULONG)len;
    b.buf = (char *)buf;
    DWORD flags = 0, got = 0;
    int r = WSARecv(s, &b, 1, &got, &flags, &op->ov, NULL);
    IOTRACE("recv_to_co fd=%lld len=%d to=%lld op=%p -> r=%d got=%lu err=%d cnt=%ld",
            (long long)fd, len, (long long)timeout_ms, (void*)op, r,
            (unsigned long)got, r ? WSAGetLastError() : 0, g_io_count);
    if (r == 0) {
#if defined(ZAN_CO_DRIVER)
        if (skip) {
            /* 内部辅助逻辑 */
            rto_claim(frame);
            if (out_n) *out_n = (int64_t)got;
            op_free(op);
            IO_CNT_DEC();
            InterlockedIncrement(&g_sync_inline);
            if (step) zan_co_ready(frame, step);
        }
        return;
#else
        return;   /* 模块核心语义抽象与接口调用契约 */
#endif
    }
    int err = WSAGetLastError();
    if (err == WSA_IO_PENDING) return;
    /* 内部辅助实现 */
    if (out_n) *out_n = 0;
#if defined(ZAN_CO_DRIVER)
    if (!PostQueuedCompletionStatus(io_shard_of(s), 0, (ULONG_PTR)s, &op->ov)) {
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)s, &op->ov)) {
#endif
        /* 内部辅助逻辑 */
        rto_claim(frame);
        if (step) zan_co_ready(frame, step);
        op_free(op);
        IO_CNT_DEC();
    }
}

void zan_io_accept_co(intptr_t fd, void *frame, zan_co_step_t step,
                      intptr_t *out_fd) {
    zan_io_init();
    /* 后端异常：上报接受连接失败避免协程阻塞 */
    if (g_io_broken) {
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }
    SOCKET listener = (SOCKET)fd;
    IOTRACE("accept_co ENTER listener=%lld", (long long)fd);
    int skip = ensure_assoc(listener);
    (void)skip;

    static LPFN_ACCEPTEX s_cached_accept_ex = NULL;
    LPFN_ACCEPTEX accept_ex = s_cached_accept_ex;
    DWORD got = 0;
    if (!accept_ex) {
        GUID guid = WSAID_ACCEPTEX;
        if (WSAIoctl(listener, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guid, sizeof(guid), &accept_ex, sizeof(accept_ex),
                     &got, NULL, NULL) == SOCKET_ERROR) {
            if (out_fd) *out_fd = -1;
            if (step) zan_co_ready(frame, step);
            return;
        }
        s_cached_accept_ex = accept_ex;
    }

    /* 内部辅助实现 */
    struct sockaddr_storage lss;
    int llen = (int)sizeof(lss);
    int fam = AF_INET;
    if (getsockname(listener, (struct sockaddr *)&lss, &llen) == 0 &&
        lss.ss_family == AF_INET6)
        fam = AF_INET6;

    SOCKET accepted = WSASocketW(fam, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                                 WSA_FLAG_OVERLAPPED);
    if (accepted == INVALID_SOCKET) {
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }

    const DWORD addr_len = (DWORD)((fam == AF_INET6)
        ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)) + 16;
    zan_io_op_t *op = op_alloc();
    if (!op) {
        closesocket(accepted);
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }
    op->sock = listener;
    op->kind = ZAN_IO_OP_ACCEPT;
    op->co = frame;
    op->step = step;
    op->out_n = (int64_t *)out_fd;
    op->accepted = accepted;
    op->accept_buf = calloc(1, addr_len * 2);
    if (!op->accept_buf) {
        /* 内部辅助逻辑 */
        closesocket(accepted);
        op_free(op);
        if (out_fd) *out_fd = -1;
        if (step) zan_co_ready(frame, step);
        return;
    }
    IO_CNT_INC();

    got = 0;
    BOOL ok = accept_ex(listener, accepted, op->accept_buf, 0,
                        addr_len, addr_len, &got, &op->ov);
    IOTRACE("accept_co listener=%lld op=%p ok=%d err=%d cnt=%ld", (long long)listener, (void*)op, (int)ok, WSAGetLastError(), g_io_count);
#if defined(ZAN_CO_DRIVER)
    /* 同步内联完成接受连接，直接派发 */
    if (ok && skip) {
        io_complete_op(op, got, 0);
        op_free(op);
        IO_CNT_DEC();
        InterlockedIncrement(&g_sync_inline);
        if (step) zan_co_ready(frame, step);
        return;
    }
#endif
    if (ok || WSAGetLastError() == WSA_IO_PENDING) return;

    closesocket(accepted);
    free(op->accept_buf);
    op_free(op);
    IO_CNT_DEC();
    if (out_fd) *out_fd = -1;
    if (step) zan_co_ready(frame, step);
}

static void io_complete_op(zan_io_op_t *op, DWORD transferred,
                           ULONG_PTR status) {
    if (op->kind == ZAN_IO_OP_ACCEPT) {
        SOCKET accepted = op->accepted;
        int64_t result = -1;
        if (status == 0 &&
            setsockopt(accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                       (const char *)&op->sock, (int)sizeof(op->sock)) == 0) {
            u_long mode = 1;
            ioctlsocket(accepted, FIONBIO, &mode);
            /* 接受的新套接字暂不在此绑定 IOCP，交由调用方注册 */
            result = (int64_t)accepted;
        } else {
            closesocket(accepted);
        }
        IOTRACE("accept_complete listener=%lld accepted=%lld status=%llu", (long long)op->sock, (long long)result, (unsigned long long)status);
        if (op->out_n) *op->out_n = result;
        free(op->accept_buf);
        return;
    }
    IOTRACE("complete op=%p kind=%d transferred=%lu status=%llu", (void*)op, op->kind, (unsigned long)transferred, (unsigned long long)status);
    if (op->out_n) *op->out_n = (int64_t)transferred;
}

#if defined(ZAN_CO_DRIVER)
/* 阻塞式轮询出队 */
static BOOL io_poll_any(OVERLAPPED_ENTRY *entries, ULONG cap, ULONG *removed,
                        DWORD to) {
    int n = (int)g_shards;
    if (n <= 1)
        return GetQueuedCompletionStatusEx(g_iocp, entries, cap, removed, to, FALSE);
    DWORD slice = (to == INFINITE || to > 2) ? 2 : to;
    for (;;) {
        for (int i = 0; i < n; i++) {
            if (GetQueuedCompletionStatusEx(io_shard(i), entries, cap, removed,
                                            slice, FALSE))
                return TRUE;
        }
        if (to == INFINITE) continue;
        if (to <= (DWORD)slice * (DWORD)n) return FALSE;
        to -= (DWORD)slice * (DWORD)n;
    }
}
#endif

int32_t zan_io_poll(int64_t timeout_ms) {
    if (g_io_count == 0 && g_blocking_inflight == 0) return 0;
    if (g_rto) {   /* 底层系统交互与数据协议契约 */
        int wr = rto_timeout_scan();
        if (wr) return wr;
    }
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    int64_t wait = rto_wait_ms(dns_wait_ms(timeout_ms));
    /* 截断溢出的等待超时时间为 DWORD 毫秒 */
    if (wait > 0x7FFFFFFF) wait = 0x7FFFFFFF;
    DWORD to = (wait < 0) ? INFINITE : (DWORD)wait;
    /* 内部辅助实现 */
    if (g_blocking_wake_lost && InterlockedExchange(&g_blocking_wake_lost, 0)) {
        int wd = dns_drain();
        if (wd) return wd;
    }
#if defined(ZAN_CO_DRIVER)
    if (!io_poll_any(entries, 64, &removed, to)) {
#else
    if (!GetQueuedCompletionStatusEx(g_iocp, entries, 64, &removed, to, FALSE)) {
#endif
        IOTRACE("poll GQCS=0 err=%lu to=%lu cnt=%ld", (unsigned long)GetLastError(), (unsigned long)to, g_io_count);
        int w = dns_timeout_scan();
        if (w) return w;
        int wr = rto_timeout_scan();
        if (wr) return wr;
        return dns_drain();
    }
    IOTRACE("poll removed=%lu cnt=%ld", (unsigned long)removed, g_io_count);
    int woke = 0;
    for (ULONG i = 0; i < removed; i++) {
        if (!entries[i].lpOverlapped) {
            /* 内部辅助逻辑 */
            woke += dns_drain();
            continue;
        }
        zan_io_op_t *op = CONTAINING_RECORD(entries[i].lpOverlapped,
                                            zan_io_op_t, ov);
        void *co = op->co;
        zan_co_step_t step = op->step;
        if (op->rto == 2) {
            /* 内部辅助实现 */
            IOTRACE("poll_op rto-dead op=%p kind=%d", (void*)op, op->kind);
            op_free(op);
            g_io_count--;
            continue;
        }
        if (op->rto && !rto_claim(co)) {
            /* 内部辅助实现 */
            IOTRACE("poll_op rto-late op=%p kind=%d", (void*)op, op->kind);
            op_free(op);
            g_io_count--;
            continue;
        }
        IOTRACE("poll_op op=%p kind=%d bytes=%lu status=%llu", (void*)op, op->kind, (unsigned long)entries[i].dwNumberOfBytesTransferred, (unsigned long long)entries[i].Internal);
        io_complete_op(op, entries[i].dwNumberOfBytesTransferred,
                       entries[i].Internal);
        op_free(op);
        g_io_count--;
        io_wake(co, step);
        woke++;
    }
    return woke;
}

/* 内部辅助实现 */
void zan_io_close_notify(intptr_t fd) {
    if (fd > 0)
        CancelIoEx((HANDLE)fd, NULL);
}

#else
/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助实现 */
static int io_shard_open(int i) {
    return i == 0;   /* 底层系统交互与数据协议契约 */
}

void zan_io_init(void) {
    /* 内部辅助实现 */
    if (g_io_started) return;
    IO_INIT_LOCK();
    io_init_locked();
    IO_INIT_UNLOCK();
}

static void io_init_locked(void) {
    if (g_io_started) return;
    io_shard_mutexes_prime();
    zan_io_ignore_sigpipe();
    g_io_entries = NULL;
    g_io_count = 0;
    /* 内部辅助实现 */
    if (g_dns_wake_fd < 0) {
        int pfd[2];
        if (pipe(pfd) == 0) {
            g_dns_wake_fd = pfd[0];
            g_dns_wake_wfd = pfd[1];
            fcntl(pfd[0], F_SETFL, O_NONBLOCK);
            zan_io_fd_cloexec(pfd[0]);
            zan_io_fd_cloexec(pfd[1]);
        }
    }
    g_io_started = 1;
}

void zan_io_shutdown(void) {
    shard_lock(&g_ioshard[0]);
    io_shutdown_locked();
    shard_unlock(&g_ioshard[0]);
}

static void io_shutdown_locked(void) {
    zan_io_entry_t *e = g_io_entries;
    while (e) {
        zan_io_entry_t *n = e->next;
        free(e);
        e = n;
    }
    g_io_entries = NULL;
    g_io_count = 0;
    io_dead_clear(&g_ioshard[0]);
    while (g_ioshard[0].rto) {
        zan_io_rto_t *rt = g_ioshard[0].rto;
        g_ioshard[0].rto = rt->next;
        free(rt);
    }
    dns_shutdown_cleanup();
    if (g_dns_wake_fd >= 0) { close(g_dns_wake_fd); g_dns_wake_fd = -1; }
    if (g_dns_wake_wfd >= 0) { close(g_dns_wake_wfd); g_dns_wake_wfd = -1; }
    g_io_started = 0;
}

static void io_register_locked(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    /* 内部辅助实现 */
    if (fd >= FD_SETSIZE) { io_reject_dead_fd(-1, co, step); return; }
    if (io_reject_dead_fd(fd, co, step)) return;
    zan_io_entry_t *e = (zan_io_entry_t *)calloc(1, sizeof(*e));
    if (!e) { io_reject_dead_fd(-1, co, step); return; }
    e->fd = (int)fd;
    e->interest = interest;
    e->co = co;
    e->step = step;
    e->rbuf = g_pending_rbuf;
    e->rlen = g_pending_rlen;
    e->out_n = g_pending_out_n;
    e->out_accept = g_pending_accept_out;
    g_pending_rbuf = NULL;
    g_pending_out_n = NULL;
    g_pending_accept_out = NULL;
    e->next = g_io_entries;
    g_io_entries = e;
    InterlockedIncrement(&g_io_count);
}

static void io_register(intptr_t fd, int32_t interest, void *co, zan_co_step_t step) {
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    io_register_locked(fd, interest, co, step);
    shard_unlock(sh);
}

int32_t zan_io_poll(int64_t timeout_ms) {
    int idx = io_thread_shard();
    zan_io_shard_t *sh = &g_ioshard[idx];
    shard_lock(sh);
    int32_t woke = io_poll_shard_locked(idx, timeout_ms);
    shard_unlock(sh);
    return woke;
}

static long long rto_wait_ms(long long caller_ms) {
    zan_io_shard_t *sh = &g_ioshard[0];
    if (!sh->rto) return caller_ms;
    long long now = dns_now_ms();
    long long nearest = -1;
    for (zan_io_rto_t *e = sh->rto; e; e = e->next)
        if (nearest < 0 || e->due_ms < nearest) nearest = e->due_ms;
    if (nearest < 0) return caller_ms;
    long long wait = nearest - now;
    if (wait < 0) wait = 0;
    if (caller_ms < 0) return wait;
    return wait < caller_ms ? wait : caller_ms;
}

static int32_t io_poll_shard_locked(int shard, int64_t timeout_ms) {
    zan_io_shard_t *sh = &g_ioshard[shard];   /* 核心系统底层抽象与内存语义契约 */
    if (sh->dead) return io_flush_dead(sh);
    if (sh->rto) {   /* 底层系统交互与数据协议契约 */
        int wr = rto_timeout_scan(sh);
        if (wr) return wr;
    }
    if (g_io_count == 0 && g_blocking_inflight == 0) return 0;

    /* 内部辅助实现 */
    {
        int woke = io_sweep_entries(FD_SETSIZE);
        if (woke) return woke;
        if (g_io_count == 0 && g_blocking_inflight == 0) return 0;
    }

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    int max_fd = 0;

    /* 异步 DNS 唤醒管道读端加入监听集合 */
    if (g_dns_wake_fd >= 0 && g_dns_wake_fd < FD_SETSIZE) {
        FD_SET(g_dns_wake_fd, &read_fds);
        max_fd = g_dns_wake_fd;
    }

    zan_io_entry_t *e = g_io_entries;
    while (e) {
        if (e->interest == ZAN_IO_READ) FD_SET(e->fd, &read_fds);
        else                             FD_SET(e->fd, &write_fds);
        if (e->fd > max_fd) max_fd = e->fd;
        e = e->next;
    }

    int64_t wait = rto_wait_ms(dns_wait_ms(timeout_ms));
    /* 内部辅助实现 */
    if (wait > 0x7FFFFFFF) wait = 0x7FFFFFFF;
    struct timeval tv;
    tv.tv_sec  = (long)(wait / 1000);
    tv.tv_usec = (long)((wait % 1000) * 1000);

    int n = select(max_fd + 1, &read_fds, &write_fds, NULL,
                   wait < 0 ? NULL : &tv);
    if (n <= 0) {
        int w2 = dns_timeout_scan();
        if (w2) return w2;
        int wr = rto_timeout_scan(sh);
        if (wr) return wr;
        return dns_drain();
    }

    int woke = 0;
    if (g_dns_wake_fd >= 0 && FD_ISSET(g_dns_wake_fd, &read_fds)) {
        dns_wake_read();
        woke += dns_drain();
    }
    zan_io_entry_t **pp = &g_io_entries;
    while (*pp) {
        zan_io_entry_t *cur = *pp;
        int ready = 0;
        if (cur->interest == ZAN_IO_READ && FD_ISSET(cur->fd, &read_fds))
            ready = 1;
        if (cur->interest == ZAN_IO_WRITE && FD_ISSET(cur->fd, &write_fds))
            ready = 1;
        if (ready) {
            void *co = cur->co;
            zan_co_step_t step = cur->step;
            *pp = cur->next;
            io_deliver_recv(cur);
            free(cur);
            InterlockedDecrement(&g_io_count);
            rto_drop(sh, co);   /* 底层系统交互与数据协议契约 */
            io_wake(co, step);
            woke++;
        } else {
            pp = &cur->next;
        }
    }
    return woke;
}

/* 内部辅助实现 */
static int rto_timeout_scan(zan_io_shard_t *sh) {
    if (!sh->rto) return 0;
    int woke = 0;
    long long now = dns_now_ms();
    zan_io_rto_t **ep = &sh->rto;
    while (*ep) {
        zan_io_rto_t *e = *ep;
        if (e->due_ms > now) { ep = &e->next; continue; }
        *ep = e->next;
        zan_io_entry_t **pp = &g_io_entries;
        while (*pp && !((*pp)->fd == e->fd && (*pp)->co == e->frame &&
                        (*pp)->interest == ZAN_IO_READ))
            pp = &(*pp)->next;
        if (*pp) {
            zan_io_entry_t *x = *pp;
            *pp = x->next;
            free(x);
            InterlockedDecrement(&g_io_count);
            if (e->out_n) *e->out_n = -1;
            io_wake(e->frame, e->step);
            woke++;
        }
        free(e);
    }
    return woke;
}

/* 内部辅助实现 */
void zan_io_close_notify(intptr_t fdp) {
    int fd = (int)fdp;
    zan_io_shard_t *sh = io_shard_of_fd(fd);
    shard_lock(sh);
    zan_io_entry_t **pp = &g_io_entries;
    while (*pp) {
        zan_io_entry_t *cur = *pp;
        if (cur->fd != fd) { pp = &cur->next; continue; }
        *pp = cur->next;
        io_mark_dead(sh, cur->co, cur->step, cur->out_n, cur->out_accept);
        free(cur);
        InterlockedDecrement(&g_io_count);
    }
    io_flush_dead(sh);
    shard_unlock(sh);
}
#endif /* platform */

/* 异步主机名与域名解析服务 */

/* 内部辅助实现 */
#define ZAN_DNS_TIMEOUT_MS 10000

typedef struct zan_blocking_job {
    struct zan_blocking_job *next;
    struct zan_blocking_job *queue_next;
    int64_t  deadline_ms;   /* 底层系统交互与数据协议契约 */
    int      timed_out;     /* 底层系统交互与数据协议契约 */
    int      started;       /* 核心系统底层抽象与内存语义契约 */
    int64_t  result;        
/* 已解析的 IPv4 网络序地址 */
    void    *sabuf;         /* 底层系统交互与数据协议契约 */
    int32_t  sacap;         /* 核心系统底层抽象与内存语义契约 */
    int32_t  port;          /* 核心系统底层抽象与内存语义契约 */
    void    *frame;         /* 核心系统底层抽象与内存语义契约 */
    zan_co_step_t step;     /* 核心系统底层抽象与内存语义契约 */
    int32_t *out;           /* 核心系统底层抽象与内存语义契约 */
    int64_t *out64;         /* 核心系统底层抽象与内存语义契约 */
    void (*run)(struct zan_blocking_job *job);
} zan_blocking_job_t;

typedef struct zan_dns_work {
    zan_blocking_job_t job;
    char hostname[256];     /* 底层系统交互与数据协议契约 */
    /* 工作线程解析的 sockaddr 结果暂存区 */
    unsigned char sa_staging[sizeof(struct sockaddr_in6)];
} zan_dns_work_t;

static zan_blocking_job_t *g_blocking_pending; /* in flight, owned by workers */
static zan_blocking_job_t *g_blocking_done;    /* 核心系统底层抽象与内存语义契约 */
static zan_blocking_job_t *g_blocking_queued;  /* 核心系统底层抽象与内存语义契约 */
static int32_t g_blocking_queued_n;            /* length of g_blocking_queued */
static int32_t g_blocking_active;

#define ZAN_BLOCKING_MAX_ACTIVE 256
/* 内部辅助实现 */
#define ZAN_BLOCKING_MAX_QUEUED 16384

static int64_t dns_now_ms(void) {
#if defined(_WIN32)
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

#if defined(_WIN32)
static SRWLOCK g_dns_srw = SRWLOCK_INIT;
static void dns_lock(void) { AcquireSRWLockExclusive(&g_dns_srw); }
static void dns_unlock(void) { ReleaseSRWLockExclusive(&g_dns_srw); }
#else
static pthread_mutex_t g_dns_mx = PTHREAD_MUTEX_INITIALIZER;
static void dns_lock(void) { pthread_mutex_lock(&g_dns_mx); }
static void dns_unlock(void) { pthread_mutex_unlock(&g_dns_mx); }
#endif

/* 空闲线程槽位启动排队任务 */
static void blocking_start_next(void);

static void blocking_spawn(zan_blocking_job_t *job);

static void blocking_finish_worker(zan_blocking_job_t *job) {
    zan_blocking_job_t *next = NULL;
    int discard = 0;
    dns_lock();
    if (job->timed_out) {
        job->started = 0;
        if (g_blocking_active > 0) g_blocking_active--;
        discard = 1;
    } else {
        zan_blocking_job_t **pp = &g_blocking_pending;
        while (*pp && *pp != job) pp = &(*pp)->next;
        if (!*pp) {
            discard = 1;
        } else {
            *pp = job->next;
            job->next = g_blocking_done;
            g_blocking_done = job;
            job->started = 0;
            if (g_blocking_active > 0) g_blocking_active--;
        }
    }
    if (g_blocking_active < ZAN_BLOCKING_MAX_ACTIVE && g_blocking_queued) {
        next = g_blocking_queued;
        g_blocking_queued = next->queue_next;
        next->queue_next = NULL;
        g_blocking_queued_n--;
        next->started = 1;
        g_blocking_active++;
    }
    dns_unlock();
    if (discard) free(job);
    if (!discard) dns_wake_notify();
    if (next) blocking_spawn(next);
}

/* 工作线程调度跳板入口 */
#if defined(_WIN32)
static DWORD WINAPI blocking_worker(void *arg) {
#else
static void *blocking_worker(void *arg) {
#endif
    zan_blocking_job_t *job = (zan_blocking_job_t *)arg;
    job->run(job);
    blocking_finish_worker(job);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void blocking_start_failed(zan_blocking_job_t *job) {
    int deliver = 0;
    dns_lock();
    zan_blocking_job_t **pp = &g_blocking_pending;
    while (*pp && *pp != job) pp = &(*pp)->next;
    if (*pp == job) {
        *pp = job->next;
        deliver = 1;
    }
    if (job->started && g_blocking_active > 0) g_blocking_active--;
    job->started = 0;
    job->result = 0;
    if (deliver) {
        job->next = g_blocking_done;
        g_blocking_done = job;
    }
    dns_unlock();
    if (deliver) dns_wake_notify();
    else free(job);
    blocking_start_next();
}

static void blocking_spawn(zan_blocking_job_t *job) {
#if defined(_WIN32)
    HANDLE h = CreateThread(NULL, 0, blocking_worker, job, 0, NULL);
    if (!h) {
        blocking_start_failed(job);
        return;
    }
    CloseHandle(h);
#else
    pthread_t tid;
    if (pthread_create(&tid, NULL, blocking_worker, job) != 0) {
        blocking_start_failed(job);
        return;
    }
    pthread_detach(tid);
#endif
}

static void blocking_start_next(void) {
    zan_blocking_job_t *next = NULL;
    dns_lock();
    if (g_blocking_active < ZAN_BLOCKING_MAX_ACTIVE && g_blocking_queued) {
        next = g_blocking_queued;
        g_blocking_queued = next->queue_next;
        next->queue_next = NULL;
        g_blocking_queued_n--;
        next->started = 1;
        g_blocking_active++;
    }
    dns_unlock();
    if (next) blocking_spawn(next);
}

static void blocking_submit(zan_blocking_job_t *job) {
    int start = 0, oversubscribed = 0;
    dns_lock();
    if (g_blocking_active >= ZAN_BLOCKING_MAX_ACTIVE &&
        g_blocking_queued_n >= ZAN_BLOCKING_MAX_QUEUED) {
        oversubscribed = 1;
    } else {
        job->next = g_blocking_pending;
        g_blocking_pending = job;
        g_blocking_inflight++;
        if (g_blocking_active < ZAN_BLOCKING_MAX_ACTIVE) {
            job->started = 1;
            g_blocking_active++;
            start = 1;
        } else {
            zan_blocking_job_t **tail = &g_blocking_queued;
            while (*tail) tail = &(*tail)->queue_next;
            *tail = job;
            g_blocking_queued_n++;
        }
    }
    dns_unlock();
    if (oversubscribed) {
        /* 内部辅助实现 */
        if (job->out) *job->out = 0;
        if (job->out64) *job->out64 = 0;
        io_wake(job->frame, job->step);
        free(job);
        return;
    }
    if (start) blocking_spawn(job);
}

static void dns_job_run(zan_blocking_job_t *job) {
    zan_dns_work_t *w = (zan_dns_work_t *)job;
    if (w->job.sabuf) {
        /* 内部辅助实现 */
        int32_t cap = w->job.sacap;
        if (cap < 0 || cap > (int32_t)sizeof w->sa_staging)
            cap = (int32_t)sizeof w->sa_staging;
        w->job.result = zan_io_resolve_sa(w->hostname, w->job.port,
                                          w->sa_staging, cap);
    } else {
        w->job.result = zan_io_resolve_ipv4(w->hostname);
    }
}

/* 内部辅助实现 */
static int64_t dns_wait_ms(int64_t caller_ms) {
    if (g_blocking_inflight <= 0) return caller_ms;
    int64_t now = dns_now_ms();
    int64_t nearest = -1;
    dns_lock();
    for (zan_blocking_job_t *j = g_blocking_pending; j; j = j->next)
        if (j->deadline_ms > 0 &&
            (nearest < 0 || j->deadline_ms < nearest))
            nearest = j->deadline_ms;
    dns_unlock();
    if (nearest < 0) return caller_ms;
    int64_t wait = nearest - now;
    if (wait <= 0) wait = 0;               /* 底层系统交互与数据协议契约 */
    if (caller_ms < 0) return wait;
    return wait < caller_ms ? wait : caller_ms;
}

/* 域名解析超时失败派发 */
static int dns_timeout_scan(void) {
    int woke = 0;
    int64_t now = dns_now_ms();
    zan_blocking_job_t *timed_out = NULL;
    dns_lock();
    zan_blocking_job_t **pp = &g_blocking_pending;
    while (*pp) {
        zan_blocking_job_t *j = *pp;
        if (j->deadline_ms <= 0 || j->deadline_ms > now) {
            pp = &j->next;
            continue;
        }
        *pp = j->next;
        j->timed_out = 1;
        g_blocking_inflight--;
        if (!j->started) {
            zan_blocking_job_t **qp = &g_blocking_queued;
            while (*qp && *qp != j) qp = &(*qp)->queue_next;
            if (*qp == j) {
                *qp = j->queue_next;
                g_blocking_queued_n--;
            }
            j->queue_next = NULL;
            j->next = timed_out;
            timed_out = j;
        } else {
            j->next = timed_out;
            timed_out = j;
        }
    }
    dns_unlock();
    while (timed_out) {
        zan_blocking_job_t *j = timed_out;
        timed_out = j->next;
        if (j->out) *j->out = 0;
        if (j->out64) *j->out64 = 0;
        io_wake(j->frame, j->step);
        if (!j->started) free(j);
        woke++;
    }
    return woke;
}

static void dns_wake_notify(void) {
#if defined(_WIN32)
    /* 空重叠结构标识 DNS 完成通知包 */
#if defined(ZAN_CO_DRIVER)
    /* 内部辅助实现 */
    for (int i = 0; i < (int)g_shards; i++)
        if (!PostQueuedCompletionStatus(io_shard(i), 0, (ULONG_PTR)-2, NULL))
            InterlockedExchange(&g_blocking_wake_lost, 1);
#else
    if (!PostQueuedCompletionStatus(g_iocp, 0, (ULONG_PTR)-2, NULL))
        InterlockedExchange(&g_blocking_wake_lost, 1);
#endif
#elif defined(__linux__)
    uint64_t one = 1;
    ssize_t r = write(g_dns_wake_fd, &one, sizeof(one));
    (void)r;
#else
    char c = 1;
    ssize_t r = write(g_dns_wake_fd, &c, 1);
    (void)r;
#endif
}

static ZAN_MAYBE_UNUSED void dns_wake_read(void) {
#if defined(__linux__)
    uint64_t v;
    ssize_t r = read(g_dns_wake_fd, &v, sizeof(v));
    (void)r;
#elif !defined(_WIN32)
    char c[64];
    while (read(g_dns_wake_fd, c, sizeof(c)) > 0) {}
#endif
}

/* 派发所有已完成的域名解析结果至等待协程 */
static int dns_drain(void) {
    int cnt = 0, woke = 0;
    zan_blocking_job_t *j;
    dns_lock();
    j = g_blocking_done;
    g_blocking_done = NULL;
    for (zan_blocking_job_t *x = j; x; x = x->next) cnt++;
    g_blocking_inflight -= cnt;
    dns_unlock();
    while (j) {
        zan_blocking_job_t *n = j->next;
        if (j->out) *j->out = (int32_t)j->result;
        if (j->out64) *j->out64 = j->result;
        /* 内部辅助实现 */
        if (j->sabuf && j->sacap > 0 && j->result > 0) {
            size_t len = (size_t)j->result;
            /* 内部辅助实现 */
            if (len <= (size_t)j->sacap)
                memcpy(j->sabuf, ((zan_dns_work_t *)j)->sa_staging, len);
        }
        io_wake(j->frame, j->step);
        free(j);
        woke++;
        j = n;
    }
    return woke;
}

/* 内部辅助逻辑 */
static void dns_shutdown_cleanup(void) {
    dns_lock();
    zan_blocking_job_t *j = g_blocking_done;
    g_blocking_done = NULL;
    while (j) {
        zan_blocking_job_t *n = j->next;
        free(j);
        j = n;
    }
    zan_blocking_job_t *queued = g_blocking_queued;
    g_blocking_queued = NULL;
    g_blocking_queued_n = 0;
    while (queued) {
        zan_blocking_job_t *n = queued->queue_next;
        zan_blocking_job_t **pp = &g_blocking_pending;
        while (*pp && *pp != queued) pp = &(*pp)->next;
        if (*pp == queued) *pp = queued->next;
        free(queued);
        queued = n;
    }
    for (zan_blocking_job_t *p = g_blocking_pending; p; p = p->next)
        p->timed_out = 1;
    g_blocking_pending = NULL;
    g_blocking_inflight = 0;
    dns_unlock();
}

void zan_io_resolve_sa_co(const char *name, int32_t port, void *buf,
                          int32_t cap, void *frame, zan_co_step_t step,
                          int32_t *out) {
    if (!name || !*name) {
        /* 内部辅助实现 */
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    zan_io_init();     /* 底层系统交互与数据协议契约 */
    zan_dns_work_t *w = (zan_dns_work_t *)calloc(1, sizeof(*w));
    if (!w) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    strncpy(w->hostname, name, sizeof(w->hostname) - 1);
    w->hostname[sizeof(w->hostname) - 1] = 0;
    w->job.frame = frame;
    w->job.step = step;
    w->job.out = out;
    w->job.sabuf = buf;
    w->job.sacap = cap;
    w->job.port = port;
    w->job.deadline_ms = dns_now_ms() + ZAN_DNS_TIMEOUT_MS;
    w->job.run = dns_job_run;
    blocking_submit(&w->job);
}

void zan_io_resolve_co(const char *hostname, void *frame, zan_co_step_t step,
                       int32_t *out) {
    if (!hostname || !*hostname) {
        /* 内部辅助实现 */
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    zan_io_init();     /* 底层系统交互与数据协议契约 */
    zan_dns_work_t *w = (zan_dns_work_t *)calloc(1, sizeof(*w));
    if (!w) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    strncpy(w->hostname, hostname, sizeof(w->hostname) - 1);
    w->hostname[sizeof(w->hostname) - 1] = 0;
    w->job.frame = frame;
    w->job.step = step;
    w->job.out = out;
    w->job.deadline_ms = dns_now_ms() + ZAN_DNS_TIMEOUT_MS;
    w->job.run = dns_job_run;
    blocking_submit(&w->job);
}

typedef struct zan_blocking_work {
    zan_blocking_job_t job;
    void *fn;
    int32_t argc;
    int64_t args[4];
} zan_blocking_work_t;

typedef int64_t (*zan_blocking_fn0)(void);
typedef int64_t (*zan_blocking_fn1)(int64_t);
typedef int64_t (*zan_blocking_fn2)(int64_t, int64_t);
typedef int64_t (*zan_blocking_fn3)(int64_t, int64_t, int64_t);
typedef int64_t (*zan_blocking_fn4)(int64_t, int64_t, int64_t, int64_t);
typedef void (*zan_blocking_void0)(void);
typedef void (*zan_blocking_void1)(int64_t);
typedef void (*zan_blocking_void2)(int64_t, int64_t);
typedef void (*zan_blocking_void3)(int64_t, int64_t, int64_t);
typedef void (*zan_blocking_void4)(int64_t, int64_t, int64_t, int64_t);

/* 编译器标量 ABI 调用规约适配 */
static void blocking_native_run(zan_blocking_job_t *base) {
    zan_blocking_work_t *w = (zan_blocking_work_t *)base;
    if (!w->fn || w->argc < 0 || w->argc > 4) {
        w->job.result = 0;
        return;
    }
    if (!w->job.out64) {
        switch (w->argc) {
        case 0: ((zan_blocking_void0)w->fn)(); break;
        case 1: ((zan_blocking_void1)w->fn)(w->args[0]); break;
        case 2: ((zan_blocking_void2)w->fn)(w->args[0], w->args[1]); break;
        case 3: ((zan_blocking_void3)w->fn)(w->args[0], w->args[1], w->args[2]); break;
        case 4: ((zan_blocking_void4)w->fn)(w->args[0], w->args[1], w->args[2], w->args[3]); break;
        }
        w->job.result = 0;
        return;
    }
    switch (w->argc) {
    case 0: w->job.result = ((zan_blocking_fn0)w->fn)(); break;
    case 1: w->job.result = ((zan_blocking_fn1)w->fn)(w->args[0]); break;
    case 2: w->job.result = ((zan_blocking_fn2)w->fn)(w->args[0], w->args[1]); break;
    case 3: w->job.result = ((zan_blocking_fn3)w->fn)(w->args[0], w->args[1], w->args[2]); break;
    case 4: w->job.result = ((zan_blocking_fn4)w->fn)(w->args[0], w->args[1], w->args[2], w->args[3]); break;
    }
}

void zan_rt_blocking_co(void *fn, int32_t argc,
                        int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                        void *frame, zan_co_step_t step, int64_t *out) {
    if (!fn || argc < 0 || argc > 4) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    zan_io_init();
    zan_blocking_work_t *w = (zan_blocking_work_t *)calloc(1, sizeof(*w));
    if (!w) {
        if (out) *out = 0;
        io_wake(frame, step);
        return;
    }
    w->fn = fn;
    w->argc = argc;
    w->args[0] = a0;
    w->args[1] = a1;
    w->args[2] = a2;
    w->args[3] = a3;
    w->job.frame = frame;
    w->job.step = step;
    w->job.out64 = out;
    w->job.run = blocking_native_run;
    blocking_submit(&w->job);
}

/* 核心系统底层抽象与内存语义契约 */

/* 核心系统底层抽象与内存语义契约 */

void zan_io_wait_co(intptr_t fd, int32_t interest, void *frame, zan_co_step_t step) {
    zan_io_init();      /* 底层系统交互与数据协议契约 */
    io_register(fd, interest, frame, step);
}

#if !defined(_WIN32)
/* 内部辅助实现 */
void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
                    zan_co_step_t step, int64_t *out_n) {
    zan_io_init();
#if defined(MSG_DONTWAIT)
    /* 内部辅助实现 */
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

/* 核心系统底层抽象与内存语义契约 */
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
    /* 内部辅助实现 */
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
    g_io_fast_budget = ZAN_IO_FAST_BURST;   /* 核心系统底层抽象与内存语义契约 */
#endif
    if (zan_io_has_pending())
        return zan_io_poll(timeout_ms);
    if (timeout_ms <= 0)
        return 0;
    /* 内部辅助逻辑 */
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

int32_t zan_io_pump(void) {
    return zan_io_pump_timeout(-1);
}

int32_t zan_io_has_pending(void) {
#if !defined(_WIN32)
    if (g_io_dead_count > 0) return 1;   /* 核心系统底层抽象与内存语义契约 */
#endif
    /* 内部辅助实现 */
    return (g_io_count > 0) || (g_blocking_inflight > 0);
}

#ifndef ZAN_IO_STACKLESS_ONLY
/* 内部辅助逻辑 */

int64_t zan_io_wait_readable(intptr_t fd) {
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register(fd, ZAN_IO_READ, co, NULL);
    zan_io_suspend_current();
    return 0;
}

int64_t zan_io_wait_writable(intptr_t fd) {
    void *co = zan_io_get_current_co();
    if (!co) return -1;
    io_register(fd, ZAN_IO_WRITE, co, NULL);
    zan_io_suspend_current();
    return 0;
}

int64_t zan_io_wait_readable_timeout(intptr_t fd, int64_t timeout_ms) {
    /* 核心系统底层抽象与内存语义契约 */
    if (timeout_ms < 0) {
        void *co = zan_io_get_current_co();
        if (!co) return -1;
        io_register(fd, ZAN_IO_READ, co, NULL);
        zan_io_suspend_current();
        return 1;
    }
#if defined(_WIN32)
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET((SOCKET)fd, &fds);
    struct timeval tv;
    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
    int r = select(0, &fds, NULL, NULL, &tv);
    if (r < 0) return -1;
    if (r == 0) return 0;
    return 1;
#else
    struct pollfd pfd;
    pfd.fd = (int)fd;
    pfd.events = POLLIN;
    int r = poll(&pfd, 1, (int)timeout_ms);
    if (r < 0) return -1;
    if (r == 0) return 0;
    return 1;
#endif
}

/* 核心系统底层抽象与内存语义契约 */

#if defined(_WIN32)
/* Windows ConnectEx 异步连接：挂起协程直至连接完成 */
int64_t zan_io_connect(intptr_t fd, const char *ip, int32_t port) {
    SOCKET s = (SOCKET)fd;

    /* ConnectEx 契约：套接字必须预先绑定地址 */
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    bind(s, (struct sockaddr *)&local, sizeof(local));

    /* 新连接套接字首次强制关联完成端口 */
    mark_assoc(s);

    GUID guid = WSAID_CONNECTEX;
    LPFN_CONNECTEX pConnectEx = NULL;
    DWORD bytes = 0;
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guid, sizeof(guid), &pConnectEx, sizeof(pConnectEx),
                 &bytes, NULL, NULL) != 0 || !pConnectEx)
        return -1;

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons((u_short)port);
    target.sin_addr.s_addr = inet_addr(ip);

    void *co = zan_io_get_current_co();
    if (!co) return -1;

    zan_io_op_t *op = op_alloc();
    if (!op) return -1;   /* 底层系统交互与数据协议契约 */
    op->sock = s;
    op->interest = ZAN_IO_WRITE;
    op->co = co;
    g_io_count++;

    BOOL ok = pConnectEx(s, (struct sockaddr *)&target, sizeof(target),
                         NULL, 0, NULL, &op->ov);
    if (!ok && WSAGetLastError() != ERROR_IO_PENDING) {
        op_free(op);
        g_io_count--;
        return -1;
    }
    zan_io_suspend_current();   /* 核心系统底层抽象与内存语义契约 */

    /* 内部辅助实现 */
    if (setsockopt(s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) != 0)
        return -1;
    struct sockaddr_in peer;
    int plen = sizeof(peer);
    return getpeername(s, (struct sockaddr *)&peer, &plen) == 0 ? 0 : -1;
}

#else
/* POSIX 异步连接：非阻塞连接与可写就绪挂起 */
int64_t zan_io_connect(intptr_t fd, const char *ip, int32_t port) {
    int s = (int)fd;
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons((uint16_t)port);
    target.sin_addr.s_addr = inet_addr(ip);

    zan_io_set_nonblocking(fd);
    int r = connect(s, (struct sockaddr *)&target, sizeof(target));
    if (r == 0) return 0;
    if (errno != EINPROGRESS && errno != EWOULDBLOCK)
        return -1;

    zan_io_wait_writable(fd);

    int err = 0;
    socklen_t len = sizeof(err);
    /* 连接探测失败：套接字已在等待期间关闭 */
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) != 0) return -1;
    return err == 0 ? 0 : -1;
}
#endif
#endif /* ZAN_IO_STACKLESS_ONLY */

/* 内部辅助实现 */
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
/* 核心系统底层抽象与内存语义契约 */
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

typedef pthread_mutex_t CRITICAL_SECTION;
static inline void InitializeCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_init(cs, NULL); }
static inline void EnterCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_lock(cs); }
static inline void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(cs); }
static inline void DeleteCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_destroy(cs); }

#ifndef InterlockedCompareExchange64
#define InterlockedCompareExchange64(dst, exch, comp) \
    __sync_val_compare_and_swap((long long volatile *)(dst), (long long)(comp), (long long)(exch))
#endif

#ifndef InterlockedIncrement
#define InterlockedIncrement(dst) \
    (LONG)__sync_add_and_fetch((LONG volatile *)(dst), 1)
#endif

#ifndef InterlockedDecrement
#define InterlockedDecrement(dst) \
    (LONG)__sync_sub_and_fetch((LONG volatile *)(dst), 1)
#endif

#ifndef InterlockedExchange
#define InterlockedExchange(dst, val) \
    (LONG)__sync_lock_test_and_set((LONG volatile *)(dst), (LONG)(val))
#endif

#ifndef InterlockedCompareExchange
#define InterlockedCompareExchange(dst, exch, comp) \
    (LONG)__sync_val_compare_and_swap((LONG volatile *)(dst), (LONG)(comp), (LONG)(exch))
#endif

#ifndef InterlockedExchangeAdd
#define InterlockedExchangeAdd(dst, val) \
    (LONG)__sync_fetch_and_add((LONG volatile *)(dst), (LONG)(val))
#endif

#ifndef MemoryBarrier
#define MemoryBarrier() __sync_synchronize()
#endif

typedef pthread_key_t DWORD;
#ifndef TLS_OUT_OF_INDEXES
#define TLS_OUT_OF_INDEXES ((DWORD)-1)
#endif

static inline DWORD TlsAlloc(void) {
    pthread_key_t k;
    return (pthread_key_create(&k, NULL) == 0) ? k : TLS_OUT_OF_INDEXES;
}
#define TlsGetValue(k) pthread_getspecific(k)
#define TlsSetValue(k, v) pthread_setspecific((k), (v))

#define SwitchToThread() sched_yield()
#define Sleep(ms) usleep((ms) * 1000)

static inline unsigned long long GetTickCount64(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)(ts.tv_nsec / 1000000ULL);
}
#define GetTickCount() ((DWORD)GetTickCount64())
typedef int BOOL;
typedef unsigned long ULONG;
typedef uintptr_t ULONG_PTR;
typedef void* LPVOID;
typedef void* HANDLE;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif
#ifndef WINAPI
#define WINAPI
#endif
#endif

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
/* 内部辅助实现 */

#define ZAN_CO_MAXW      256
#define ZAN_LQ_CAP       256u        /* 底层系统交互与数据协议契约 */
#define ZAN_LQ_MASK      (ZAN_LQ_CAP - 1u)
#define ZAN_LIFO_BUDGET  3           /* 核心系统底层抽象与内存语义契约 */
#define ZAN_GLOBAL_TICK  61          /* 核心系统底层抽象与内存语义契约 */
/* 内部辅助实现 */
#define ZAN_POLL_GATE     256u
#define ZAN_STEAL_ROUNDS 2           /* 核心系统底层抽象与内存语义契约 */
#define ZAN_WAKE_KEY     ((ULONG_PTR)-3)  /* 核心系统底层抽象与内存语义契约 */

/* 协程调度器维护的异步帧前缀结构 */
typedef struct {
    volatile long long sched;    /* CO_* bits */
    zan_co_step_t      pending;  /* 核心系统底层抽象与内存语义契约 */
} zan_co_fhdr;

#define CO_QUEUED    1LL   /* 底层系统交互与数据协议契约 */
#define CO_RUNNING   2LL   /* 底层系统交互与数据协议契约 */
#define CO_NOTIFIED  4LL   /* 底层系统交互与数据协议契约 */
#define CO_DEAD      8LL   /* 底层系统交互与数据协议契约 */
#define CO_SELFQ     16LL  
/* 模块核心语义抽象与接口调用契约 */

typedef struct { void *frame; zan_co_step_t step; } zan_co_task;

typedef struct {
    unsigned long long ran;         /* 核心系统底层抽象与内存语义契约 */
    unsigned long long lifo_put;    /* 底层系统交互与数据协议契约 */
    unsigned long long lifo_hit;    /* 核心系统底层抽象与内存语义契约 */
    unsigned long long lifo_demote; /* 底层系统交互与数据协议契约 */
    unsigned long long lq_push;
    unsigned long long lq_pop;
    unsigned long long spill;       /* 核心系统底层抽象与内存语义契约 */
    unsigned long long inj_push;
    unsigned long long inj_pop;
    unsigned long long steal_ok;
    unsigned long long steal_fail;  /* 核心系统底层抽象与内存语义契约 */
    unsigned long long park;        /* 核心系统底层抽象与内存语义契约 */
    unsigned long long wake_post;   /* 核心系统底层抽象与内存语义契约 */
    unsigned long long selfq;       /* 底层系统交互与数据协议契约 */
} zan_co_stats_t;

typedef struct {
    /* 内部辅助逻辑 */
    volatile long long head;
    char pad_head[56];
    volatile long long tail;
    char pad_tail[56];
    zan_co_task        buf[ZAN_LQ_CAP];
    zan_co_task        lifo;
    int                lifo_full;
    int                lifo_budget;
    int         index;
    int         bg_gen;
    int                searching;
    /* 内部辅助实现 */
    long long          slice_start_us;
    /* 当前工作线程正在步进执行的协程帧指针 */
    void              *cur;
    volatile LONG      parked;
    unsigned           tick;
    unsigned           poll_tick;    /* 底层系统交互与数据协议契约 */
    unsigned           rng;
    /* 内部辅助逻辑 */
    zan_co_stats_t     st;
    /* 内部辅助实现 */
    volatile LONG      cnt_act;
    volatile LONG      cnt_out;
    char               pad[64];      /* 底层系统交互与数据协议契约 */
} __attribute__((aligned(64))) zan_co_worker_t;

static CRITICAL_SECTION g_co_lock;
/* 内部辅助实现 */
static volatile LONG    g_co_running;    /* 核心系统底层抽象与内存语义契约 */
static volatile LONG    g_co_parked;     /* 核心系统底层抽象与内存语义契约 */
static volatile LONG    g_co_searching;  /* 核心系统底层抽象与内存语义契约 */
static volatile LONG    g_co_wake;       /* 核心系统底层抽象与内存语义契约 */
static volatile LONG    g_co_stop;
static int              g_co_inited;
static int              g_co_workers;
static DWORD            g_co_tls = TLS_OUT_OF_INDEXES;
/* 内部辅助实现 */
static zan_co_worker_t  g_wk[ZAN_CO_MAXW];

/* 内部辅助实现 */
static volatile LONG g_co_pool_live;   /* 核心系统底层抽象与内存语义契约 */
static volatile LONG g_co_pool_fg;     /* 底层系统交互与数据协议契约 */
static int           g_co_pool_gen;     /* 底层系统交互与数据协议契约 */
static volatile LONG g_co_pool_out;     /* 底层系统交互与数据协议契约 */
static void co_pool_ensure(void);

/* 内部辅助实现 */
static CRITICAL_SECTION g_inj_lock;
static zan_co_node     *g_inj_head, *g_inj_tail, *g_inj_free;
static volatile LONG    g_inj_len;
/* 模块核心语义抽象与接口调用契约 */
static volatile LONG    g_inj_push_ext;

static long long co_now_ms(void) { return (long long)GetTickCount64(); }

/* 核心系统底层抽象与内存语义契约 */
static volatile LONG      g_timer_owner;
static volatile long long g_timer_pumped_us;   /* zan_co_precise_us stamp */
static volatile long long g_timer_next_ms;

/* 跨线程分片任务生成计量统计 */
static struct {
    _Alignas(64) volatile LONG act; char pad1[64 - sizeof(LONG)];
    _Alignas(64) volatile LONG out; char pad2[64 - sizeof(LONG)];
} g_co_cnt_ext;

static void co_wake_shard(int shard) {
#if defined(_WIN32)
    HANDLE p = io_shard(shard);
    if (p) PostQueuedCompletionStatus(p, 0, ZAN_WAKE_KEY, NULL);
#elif defined(__linux__)
    /* 内部辅助逻辑 */
    if (shard >= 0 && shard < (int)g_shards && g_ioshard[shard].wake_wfd >= 0) {
        uint64_t one = 1;
        (void)write(g_ioshard[shard].wake_wfd, &one, sizeof(one));
    }
#elif defined(__APPLE__)
    if (shard >= 0 && shard < (int)g_shards && g_ioshard[shard].wake_wfd >= 0) {
        char b = 1;
        (void)write(g_ioshard[shard].wake_wfd, &b, 1);
    }
#endif
}

/* 核心系统底层抽象与内存语义契约 */
static int co_worker_shard(int worker) {
    int n = (int)g_shards;
    return (n <= 1) ? 0 : (worker % n);
}

/* 内部辅助实现 */
static zan_co_worker_t *co_self(void);

static void co_notify(void) {
    if (g_co_searching > 0) return;
    if (g_co_parked <= 0) return;
    if (InterlockedIncrement(&g_co_wake) > g_co_parked) {
        InterlockedDecrement(&g_co_wake);
        return;
    }
    zan_co_worker_t *self = co_self();
    if (self) self->st.wake_post++;
    /* 底层系统交互与数据协议契约 */
    for (int i = 0; i < g_co_workers; i++) {
        if (g_wk[i].parked) { co_wake_shard(co_worker_shard(i)); return; }
    }
    co_wake_shard(0);
}

static zan_co_worker_t *co_self(void) {
    if (g_co_tls == TLS_OUT_OF_INDEXES) return NULL;
    return (zan_co_worker_t *)TlsGetValue(g_co_tls);
}

#if !defined(_WIN32)
/* 内部辅助实现 */
static int io_thread_shard(void) {
    zan_co_worker_t *w = co_self();
    int i = w ? w->index : 0;
    int n = (int)g_shards;
    return (n <= 1) ? 0 : (i % n);
}
#endif

static unsigned co_rand(zan_co_worker_t *w) {
    unsigned x = w->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    w->rng = x ? x : 0x9e3779b9u;
    return w->rng;
}

/* ---- injector ---- */

static void inj_push(zan_co_task t) {
    EnterCriticalSection(&g_inj_lock);
    zan_co_node *n = g_inj_free;
    if (n) g_inj_free = n->next;
    else {
        n = (zan_co_node *)malloc(sizeof(*n));
        if (!n) zan_rt_fatal("oom", "io: injection node alloc failed");
    }
    n->next = NULL; n->frame = t.frame; n->step = t.step;
    if (g_inj_tail) g_inj_tail->next = n; else g_inj_head = n;
    g_inj_tail = n;
    InterlockedIncrement(&g_inj_len);
    LeaveCriticalSection(&g_inj_lock);
}

static int inj_pop(zan_co_task *out) {
    if (g_inj_len <= 0) return 0;          /* 核心系统底层抽象与内存语义契约 */
    EnterCriticalSection(&g_inj_lock);
    zan_co_node *n = g_inj_head;
    if (n) {
        g_inj_head = n->next;
        if (!g_inj_head) g_inj_tail = NULL;
        out->frame = n->frame; out->step = n->step;
        n->next = g_inj_free; g_inj_free = n;
        InterlockedDecrement(&g_inj_len);
    }
    LeaveCriticalSection(&g_inj_lock);
    return n != NULL;
}

/* 单次加锁批量抽取注入的任务队列 */
#define ZAN_INJ_BATCH 64
static int inj_pop_batch(zan_co_task *out, int max) {
    if (g_inj_len <= 0) return 0;          /* 核心系统底层抽象与内存语义契约 */
    EnterCriticalSection(&g_inj_lock);
    int n = 0;
    while (n < max && g_inj_head) {
        zan_co_node *nd = g_inj_head;
        g_inj_head = nd->next;
        if (!g_inj_head) g_inj_tail = NULL;
        out[n].frame = nd->frame; out[n].step = nd->step;
        nd->next = g_inj_free; g_inj_free = nd;
        InterlockedDecrement(&g_inj_len);
        n++;
    }
    LeaveCriticalSection(&g_inj_lock);
    return n;
}

/* 核心系统底层抽象与内存语义契约 */

static void lq_push(zan_co_worker_t *w, zan_co_task t);

/* 内部辅助实现 */
static void lq_spill(zan_co_worker_t *w) {
    zan_co_task tmp[ZAN_LQ_CAP / 2];
    for (;;) {
        long long h = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&w->tail, __ATOMIC_RELAXED);
        long long n = (t - h) / 2;
        if (n <= 0) return;
        if (n > (long long)(ZAN_LQ_CAP / 2)) n = (long long)(ZAN_LQ_CAP / 2);
        for (long long i = 0; i < n; i++)
            tmp[i] = w->buf[(unsigned long long)(h + i) & ZAN_LQ_MASK];
        if (!__atomic_compare_exchange_n(&w->head, &h, h + n, 0,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;
        for (long long i = 0; i < n; i++) inj_push(tmp[i]);
        w->st.spill++;
        w->st.inj_push += (unsigned long long)n;
        return;
    }
}

static void lq_push(zan_co_worker_t *w, zan_co_task t) {
    long long tail = __atomic_load_n(&w->tail, __ATOMIC_RELAXED);
    long long head = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
    if (tail - head >= (long long)ZAN_LQ_CAP) {
        lq_spill(w);
        head = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        if (tail - head >= (long long)ZAN_LQ_CAP) {
            inj_push(t);
            w->st.inj_push++;
            return;
        }
    }
    w->buf[(unsigned long long)tail & ZAN_LQ_MASK] = t;
    __atomic_store_n(&w->tail, tail + 1, __ATOMIC_RELEASE);
    w->st.lq_push++;
}

static int lq_pop(zan_co_worker_t *w, zan_co_task *out) {
    for (;;) {
        long long h = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&w->tail, __ATOMIC_ACQUIRE);
        if (h >= t) return 0;
        zan_co_task task = w->buf[(unsigned long long)h & ZAN_LQ_MASK];
        if (__atomic_compare_exchange_n(&w->head, &h, h + 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            *out = task;
            return 1;
        }
    }
}

/* 内部辅助实现 */
static int lq_steal(zan_co_worker_t *v, zan_co_worker_t *w, zan_co_task *out) {
    zan_co_task tmp[ZAN_LQ_CAP / 2];
    int attempts = 0;
    for (;;) {
        long long h = __atomic_load_n(&v->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&v->tail, __ATOMIC_ACQUIRE);
        long long n = t - h;
        if (n <= 0) return 0;
        n = n - n / 2;                                   /* half, rounded up */
        if (n > (long long)(ZAN_LQ_CAP / 2)) n = (long long)(ZAN_LQ_CAP / 2);
        for (long long i = 0; i < n; i++)
            tmp[i] = v->buf[(unsigned long long)(h + i) & ZAN_LQ_MASK];
        if (!__atomic_compare_exchange_n(&v->head, &h, h + n, 0,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            attempts++;
            if (attempts > 16) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
                __builtin_ia32_pause();   /* no <emmintrin.h> on musl/wasi */
#endif
            }
            if (attempts > 64) {
                return 0; /* 模块核心语义抽象与接口调用契约 */
            }
            continue;
        }
        *out = tmp[0];
        for (long long i = 1; i < n; i++) lq_push(w, tmp[i]);
        return 1;
    }
}

/* 核心系统底层抽象与内存语义契约 */

static zan_co_fhdr *co_hdr(void *frame) { return (zan_co_fhdr *)frame; }

#define ZAN_TR_RING 8192
typedef struct { long long seq; unsigned long tid; const char* tag; const void* fp; } zan_tr_ent;
static zan_tr_ent g_tr_ring[ZAN_TR_RING];
static volatile long long g_tr_seq = -1;
static int g_co_trace = -1;
static void co_trace(const char *what, const void *fp) {
    if (g_co_trace < 0) {
        const char *e = getenv("ZAN_CO_TRACE");
        g_co_trace = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
    }
    if (!g_co_trace) return;
    long long i = InterlockedIncrement64(&g_tr_seq);
    zan_tr_ent *e = &g_tr_ring[i & (ZAN_TR_RING - 1)];
    e->seq = i;
    e->tid = GetCurrentThreadId();
    e->tag = what;
    e->fp = fp;
}

/* 模块核心语义抽象与接口调用契约 */
static void co_trace_dump(long long live) {
    if (g_co_trace < 0) {
        const char *e = getenv("ZAN_CO_TRACE");
        g_co_trace = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
    }
    if (!g_co_trace) return;
    long long total = g_tr_seq;
    long long first = total - ZAN_TR_RING;
    if (first < 0) first = -1;
    fprintf(stderr, "[cot DUMP live=%lld seq=%lld]\n",
        live, total);
    for (long long s = (first > 0 ? first : 0) + 1; s <= total; s++) {
        zan_tr_ent *e = &g_tr_ring[s & (ZAN_TR_RING - 1)];
        if (e->seq != s) continue;
        fprintf(stderr, "[cot %lld %lu %s %p]\n",
            e->seq, (unsigned long)e->tid, e->tag, e->fp);
    }
    fflush(stderr);
}

/* 内部辅助实现 */
static LONG co_cnt_activity(void) {
    long long s = g_co_cnt_ext.act;
    for (int i = 0; i < g_co_workers; i++) s += g_wk[i].cnt_act;
    return (LONG)s;
}
static LONG co_cnt_outstanding(void) {
    long long s = g_co_cnt_ext.out;
    for (int i = 0; i < g_co_workers; i++) s += g_wk[i].cnt_out;
    return (LONG)s;
}
/* 内部辅助实现 */
static void co_cnt_add(zan_co_worker_t *w, int act, int out) {
    volatile LONG *pa = w ? &w->cnt_act : &g_co_cnt_ext.act;
    volatile LONG *po = w ? &w->cnt_out : &g_co_cnt_ext.out;
    if (act > 0)      InterlockedIncrement(pa);
    else if (act < 0) InterlockedDecrement(pa);
    if (out > 0)      InterlockedIncrement(po);
    else if (out < 0) InterlockedDecrement(po);
}

static void co_submit(void *frame, zan_co_step_t step) {
    zan_co_task t;
    t.frame = frame; t.step = step;
    zan_co_worker_t *w = co_self();
    co_cnt_add(w, +1, +1);
    if (!w) {
        inj_push(t);
        InterlockedIncrement(&g_inj_push_ext);
        co_trace("inj", t.frame);
        co_notify();
        return;
    }
    if (!w->lifo_full) {
        /* 内部辅助实现 */
        w->lifo = t;
        w->lifo_full = 1;
        w->st.lifo_put++;
        return;
    }
    lq_push(w, t);
    co_notify();
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step || !frame) return;
    zan_co_fhdr *h = co_hdr(frame);
    for (;;) {
        long long s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
        if (s & CO_DEAD) return;                 /* 核心系统底层抽象与内存语义契约 */
        /* 内部辅助逻辑 */
        if (s & CO_QUEUED) return;
        if (s & CO_RUNNING) {
            /* 核心系统底层抽象与内存语义契约 */
            zan_co_worker_t *sw = co_self();
            long long selfq = (sw && sw->cur == frame) ? CO_SELFQ : 0;
            __atomic_store_n(&h->pending, step, __ATOMIC_RELAXED);
            if (__atomic_compare_exchange_n(&h->sched, &s,
                                            s | CO_NOTIFIED | selfq, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
            continue;
        }
    if (__atomic_compare_exchange_n(&h->sched, &s, s | CO_QUEUED, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        break;
    }
    co_submit(frame, step);
    /* 跨线程安全就绪：支持任意线程恢复协程 */
    co_pool_ensure();
}

/* 核心系统底层抽象与内存语义契约 */
void __zan_co_frame_free(void *frame) {
    if (!frame) return;
    zan_co_fhdr *h = co_hdr(frame);
    for (;;) {
        long long s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
        if (s & CO_DEAD) return;                 /* 核心系统底层抽象与内存语义契约 */
        if (s & (CO_QUEUED | CO_RUNNING)) {
            if (__atomic_compare_exchange_n(&h->sched, &s, s | CO_DEAD, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
            continue;
        }
        if (__atomic_compare_exchange_n(&h->sched, &s, CO_DEAD, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    free(frame);
}

void zan_co_delay(long long ms, void *frame, zan_co_step_t step) {
    zan_timer_delay(ms, frame, step);
    co_cnt_add(co_self(), +1, 0);
    co_notify();
}

static int co_worker_count(void) {
    int w = 0;
    /* 内部辅助实现 */
    w = zan_async_cfg_workers();
    if (w <= 0) {
#if defined(_WIN32)
        char envbuf[32];
        DWORD r = GetEnvironmentVariableA("ZAN_CO_WORKERS", envbuf, sizeof(envbuf));
        if (r > 0 && r < sizeof(envbuf)) w = atoi(envbuf);
#endif
    }
    if (w <= 0) {
        const char *e = getenv("ZAN_CO_WORKERS");
        if (e && *e) w = atoi(e);
    }
    if (w <= 0) {
#if defined(_WIN32)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        w = (int)si.dwNumberOfProcessors;
#else
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        w = (n > 0) ? (int)n : 1;
#endif
        /* 内部辅助实现 */
        if (w > 4) w = 4;
    }
    if (w < 1) w = 1;
    if (w > ZAN_CO_MAXW) w = ZAN_CO_MAXW;
    return w;
}

/* 内部辅助实现 */
int zan_co_poll(void) {
    zan_co_worker_t *w = co_self();
    if (!w) return 0;
    /* 内部辅助实现 */
    if ((++w->poll_tick & (ZAN_POLL_GATE - 1)) != 0) return 0;
    long long q = zan_co_quantum_ms();
    if (q <= 0 || w->slice_start_us <= 0) return 0;
    long long now = zan_co_precise_us();
    return (now - w->slice_start_us >= q * 1000) ? 1 : 0;
}

static void co_stats_dump(void);

void zan_co_sched_init(void) {
    if (!g_co_inited) {
        InitializeCriticalSection(&g_co_lock);
        InitializeCriticalSection(&g_inj_lock);
        g_co_tls = TlsAlloc();
        /* 核心系统底层抽象与内存语义契约 */
        atexit(co_stats_dump);
        g_co_inited = 1;
    }
    g_co_workers = co_worker_count();
    g_rq_head = g_rq_tail = NULL;
    zan_timer_runtime_reset();
    zan_timer_set_ready_hook(zan_co_ready);
    g_co_running = 0;
    g_co_parked = 0;
    g_co_searching = 0;
    g_co_wake = 0;
    g_co_stop = 0;
    g_co_pool_live = 0;
    g_co_pool_gen = 0;
    g_io_shards_live = 0;
    g_timer_owner = 0;
    g_timer_pumped_us = 0;
    g_timer_next_ms = -1;
    EnterCriticalSection(&g_inj_lock);
    while (g_inj_head) {
        zan_co_node *n = g_inj_head;
        g_inj_head = n->next;
        n->next = g_inj_free;
        g_inj_free = n;
    }
    g_inj_tail = NULL;
    g_inj_len = 0;
    g_inj_push_ext = 0;
    LeaveCriticalSection(&g_inj_lock);
    for (int i = 0; i < ZAN_CO_MAXW; i++) {
        zan_co_worker_t *w = &g_wk[i];
        w->head = w->tail = 0;
        w->lifo_full = 0;
        w->lifo_budget = ZAN_LIFO_BUDGET;
        w->index = i;
        w->searching = 0;
        w->parked = 0;
        w->tick = 0;
        w->rng = 0x9e3779b9u ^ (unsigned)(i * 2654435761u);
        memset(&w->st, 0, sizeof(w->st));
        w->cnt_act = 0;
        w->cnt_out = 0;
    }
    g_co_cnt_ext.act = 0;
    g_co_cnt_ext.out = 0;
}

/* 核心系统底层抽象与内存语义契约 */

static int co_search_begin(zan_co_worker_t *w) {
    if (w->searching) return 1;
    LONG s = g_co_searching;
    if (2 * s >= g_co_workers) return 0;   /* 核心系统底层抽象与内存语义契约 */
    InterlockedIncrement(&g_co_searching);
    w->searching = 1;
    return 1;
}

static void co_search_end(zan_co_worker_t *w, int found) {
    if (!w->searching) return;
    w->searching = 0;
    /* 内部辅助实现 */
    if (InterlockedDecrement(&g_co_searching) == 0 && found) co_notify();
}

static int co_steal(zan_co_worker_t *w, zan_co_task *out) {
    if (g_co_workers <= 1) return 0;
    if (!co_search_begin(w)) return 0;
    for (int round = 0; round < ZAN_STEAL_ROUNDS; round++) {
        int start = (int)(co_rand(w) % (unsigned)g_co_workers);
        for (int k = 0; k < g_co_workers; k++) {
            int idx = (start + k) % g_co_workers;
            if (idx == w->index) continue;
            if (lq_steal(&g_wk[idx], w, out)) return 1;
        }
        if (inj_pop(out)) return 1;
    }
    return 0;
}

static int co_next_task(zan_co_worker_t *w, zan_co_task *out) {
    /* 内部辅助实现 */
    if (++w->tick % ZAN_GLOBAL_TICK == 0 && g_inj_len > 0) {
        zan_co_task batch[ZAN_INJ_BATCH];
        int n = inj_pop_batch(batch, ZAN_INJ_BATCH);
        if (n > 0) {
            w->lifo_budget = ZAN_LIFO_BUDGET;
            w->st.inj_pop += (unsigned long long)n;
            for (int i = 1; i < n; i++) lq_push(w, batch[i]);
            *out = batch[0];
            return 1;
        }
    }
    if (w->lifo_full) {
        if (w->lifo_budget > 0) {
            *out = w->lifo;
            w->lifo_full = 0;
            w->lifo_budget--;
            w->st.lifo_hit++;
            return 1;
        }
        /* 内部辅助逻辑 */
        lq_push(w, w->lifo);
        w->lifo_full = 0;
        w->st.lifo_demote++;
    }
    w->lifo_budget = ZAN_LIFO_BUDGET;
    if (lq_pop(w, out)) { w->st.lq_pop++; return 1; }
    if (inj_pop(out)) { w->st.inj_pop++; return 1; }
    if (co_steal(w, out)) { w->st.steal_ok++; return 1; }
    w->st.steal_fail++;
    return 0;
}

/* 内部辅助实现 */
static int co_has_runnable(void) {
    if (g_inj_len > 0) return 1;
    for (int i = 0; i < g_co_workers; i++) {
        zan_co_worker_t *w = &g_wk[i];
        if (w->lifo_full) return 1;
        if (__atomic_load_n(&w->tail, __ATOMIC_ACQUIRE) !=
            __atomic_load_n(&w->head, __ATOMIC_ACQUIRE)) return 1;
    }
    return 0;
}

/* 核心系统底层抽象与内存语义契约 */
static long long co_pump_timers(void);

static void co_run(zan_co_worker_t *w, zan_co_task *t) {
    zan_co_fhdr *h = co_hdr(t->frame);
    long long s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
    for (;;) {
        if (s & CO_DEAD) {
            /* 模块核心语义抽象与接口调用契约 */
            free(t->frame);
            co_cnt_add(w, +1, -1);
            return;
        }
        long long n = (s & ~(CO_QUEUED | CO_NOTIFIED | CO_SELFQ)) | CO_RUNNING;
        if (__atomic_compare_exchange_n(&h->sched, &s, n, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    InterlockedIncrement(&g_co_running);
    w->st.ran++;
    co_trace("run", t->frame);
    /* 内部辅助实现 */
    co_pump_timers();
    w->slice_start_us = zan_co_precise_us();
    w->cur = t->frame;
    t->step(t->frame);
    w->cur = NULL;
    co_trace("done", t->frame);
    for (;;) {
        s = __atomic_load_n(&h->sched, __ATOMIC_ACQUIRE);
        if (s & CO_DEAD) { free(t->frame); break; }
        if (s & CO_NOTIFIED) {
            zan_co_step_t step = __atomic_load_n(&h->pending, __ATOMIC_RELAXED);
            int selfq = (s & CO_SELFQ) != 0;
            if (!__atomic_compare_exchange_n(&h->sched, &s, CO_QUEUED, 0,
                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                continue;
            if (selfq) {
                /* 内部辅助逻辑 */
                zan_co_task rq;
                rq.frame = t->frame;
                rq.step = step ? step : t->step;
                co_cnt_add(w, +1, +1);
                lq_push(w, rq);
                w->st.selfq++;
            } else {
                co_submit(t->frame, step ? step : t->step);
            }
            break;
        }
        if (__atomic_compare_exchange_n(&h->sched, &s, 0LL, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    InterlockedDecrement(&g_co_running);
    co_cnt_add(w, +1, -1);
}

/* 派发到期定时器并返回下一截止剩余毫秒数 */
static long long co_pump_timers_now(void) {
    if (InterlockedCompareExchange(&g_timer_owner, 1, 0) != 0) {
        long long live = zan_timer_next_timeout();
        if (live > 0) return live;
        long long cached = g_timer_next_ms;
        return (cached <= 0) ? 1 : cached;
    }
    /* 核心系统底层抽象与内存语义契约 */
    InterlockedIncrement(&g_co_running);
    zan_timer_dispatch_due();
    long long next = zan_timer_next_timeout();
    g_timer_next_ms = next;
    g_timer_pumped_us = zan_co_precise_us();
    InterlockedDecrement(&g_co_running);
    InterlockedExchange(&g_timer_owner, 0);
    return next;
}

static long long co_pump_timers(void) {
    long long now = zan_co_precise_us();
    /* 内部辅助实现 */
    if (now - g_timer_pumped_us >= 1000)
        return co_pump_timers_now();
    long long cached = g_timer_next_ms;
    return (cached == 0) ? 1 : cached;
}

/* 阻塞在完成端口等待 I/O 就绪并恢复协程 */
static void co_wait_io(zan_co_worker_t *w, long long timeout_ms) {
    /* 内部辅助实现 */
    if (timeout_ms > 0x7FFFFFFF) timeout_ms = 0x7FFFFFFF;
    DWORD to = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
#if defined(_WIN32)
    OVERLAPPED_ENTRY entries[64];
    ULONG removed = 0;
    /* 内部辅助逻辑 */
    if (g_blocking_wake_lost && InterlockedExchange(&g_blocking_wake_lost, 0)) {
        if (dns_drain() > 0) return;
    }
    /* 内部辅助实现 */
    BOOL ok = GetQueuedCompletionStatusEx(io_shard(co_worker_shard(w->index)),
                                          entries, 64, &removed, to, FALSE);
    /* 核心系统底层抽象与内存语义契约 */
    InterlockedExchange(&w->parked, 0);
    InterlockedDecrement(&g_co_parked);
    if (!ok) {
        /* Timeout or error */
        dns_timeout_scan();
        rto_timeout_scan();   /* 核心系统底层抽象与内存语义契约 */
        dns_drain();
        return;
    }
    for (ULONG i = 0; i < removed; i++) {
        if (entries[i].lpOverlapped == NULL) {
            if (entries[i].lpCompletionKey == ZAN_WAKE_KEY) {
                /* 模块核心语义抽象与接口调用契约 */
                if (g_co_wake > 0) InterlockedDecrement(&g_co_wake);
            } else {
                dns_drain();
            }
            continue;
        }
        zan_io_op_t *op = CONTAINING_RECORD(entries[i].lpOverlapped,
                                            zan_io_op_t, ov);
        void *co = op->co;
        zan_co_step_t step = op->step;
        if (op->rto == 2) {
            /* 内部辅助实现 */
            IOTRACE("poll_op rto-dead op=%p kind=%d", (void*)op, op->kind);
            op_free(op);
            IO_CNT_DEC();
            continue;
        }
        if (op->rto && !rto_claim(co)) {
            /* 内部辅助实现 */
            IOTRACE("poll_op rto-late op=%p kind=%d", (void*)op, op->kind);
            op_free(op);
            IO_CNT_DEC();
            continue;
        }
        IOTRACE("poll_op op=%p kind=%d bytes=%lu status=%llu", (void*)op, op->kind, (unsigned long)entries[i].dwNumberOfBytesTransferred, (unsigned long long)entries[i].Internal);
        io_complete_op(op, entries[i].dwNumberOfBytesTransferred,
                       entries[i].Internal);
        op_free(op);
        /* 内部辅助实现 */
        if (step) zan_co_ready(co, step);
        IO_CNT_DEC();
    }
#else
    (void)w;
    STRACE("w%d waitio enter to=%lld haspend=%d", w->index, (long long)to, zan_io_has_pending());
    int polled = zan_io_pump_timeout(to == INFINITE ? -1 : (int64_t)to);
    BOOL ok = (polled >= 0);
    if (g_co_wake > 0) InterlockedDecrement(&g_co_wake);
    InterlockedExchange(&w->parked, 0);
    InterlockedDecrement(&g_co_parked);
    STRACE("w%d waitio ret=%d ok=%d", w->index, polled, (int)ok);
    if (!ok) {
        dns_timeout_scan();
        /* 内部辅助逻辑 */
        zan_io_shard_t *psh = &g_ioshard[io_thread_shard()];
        shard_lock(psh);
        rto_timeout_scan(psh);
        shard_unlock(psh);
        dns_drain();
    }
#endif
}

static int co_all_idle(void) {
    /* 内部辅助实现 */
    if (co_cnt_outstanding() != 0 || g_co_running != 0 ||
        g_io_count != 0 || g_blocking_inflight != 0 || zan_timer_pending() != 0)
        return 0;
    int idle;
    EnterCriticalSection(&g_co_lock);
    idle = !(zan_timer_pending() != 0 || co_cnt_outstanding() != 0 ||
             g_co_running != 0 || g_io_count != 0 ||
             g_blocking_inflight != 0 || co_has_runnable());
    LeaveCriticalSection(&g_co_lock);
    return idle;
}

static void co_pool_retire(zan_co_worker_t *w);

/* 内部辅助逻辑 */
#if defined(_WIN32)
static volatile LONG g_co_timeperiod_done = 0;
static void co_timeperiod_raise(void) {
    if (InterlockedExchange(&g_co_timeperiod_done, 1)) return;
    HMODULE mm = LoadLibraryA("winmm.dll");
    if (!mm) return;
    ULONG (WINAPI *begin)(UINT) =
        (ULONG (WINAPI *)(UINT))(void *)GetProcAddress(mm, "timeBeginPeriod");
    if (begin) begin(1);
}
#else
/* 内部辅助实现 */
static void co_timeperiod_raise(void) { }
#endif

static void co_worker(int worker) {
    co_timeperiod_raise();
    zan_co_worker_t *w = &g_wk[worker];
    w->bg_gen = g_co_pool_gen;
    if (g_co_tls != TLS_OUT_OF_INDEXES) TlsSetValue(g_co_tls, w);
    /* 模块核心语义抽象与接口调用契约 */
    LONG idle_seen = -1;
    for (;;) {
        if (g_co_stop) { co_pool_retire(w); return; }
        /* 队列繁忙时持续推进到期定时器 */
        if (zan_timer_pending() > 0) co_pump_timers();
        zan_co_task t;
        if (co_next_task(w, &t)) {
            co_search_end(w, 1);
            idle_seen = -1;
            co_run(w, &t);
            continue;
        }
        /* 模块核心语义抽象与接口调用契约 */
        long long tnext = co_pump_timers_now();
        if (co_next_task(w, &t)) {
            co_search_end(w, 1);
            idle_seen = -1;
            co_run(w, &t);
            continue;
        }
        co_search_end(w, 0);
        /* 模块核心语义抽象与接口调用契约 */
        if (co_all_idle()) {
            LONG seq = co_cnt_activity();
            if (idle_seen == seq) {
                g_co_stop = 1;
                for (int i = 0; i < g_co_workers; i++)
                    co_wake_shard(co_worker_shard(i));
                co_pool_retire(w);
                return;
            }
            idle_seen = seq;
            Sleep(1);
            continue;
        }
        idle_seen = -1;
        /* 核心系统底层抽象与内存语义契约 */
        long long to;
        if (tnext >= 0)               to = tnext;
        else if (g_io_count > 0)      to = 1000;
        else                          to = 50;
        if (g_blocking_inflight > 0) {
            long long dw = dns_wait_ms(-1);
            if (dw >= 0 && dw < to) to = dw;
        }
        /* 内部辅助实现 */
#if defined(_WIN32)
        {
            long long rw = rto_wait_ms(-1);
            if (rw >= 0 && rw < to) to = rw;
        }
#endif
        /* 模块核心语义抽象与接口调用契约 */
        InterlockedIncrement(&g_co_parked);
        InterlockedExchange(&w->parked, 1);
        w->st.park++;
        /* 模块核心语义抽象与接口调用契约 */
        long long tnow = zan_timer_next_timeout();
        if (tnow >= 0 && tnow < to) to = tnow;
        if (tnow == 0 || co_has_runnable()) {
            InterlockedExchange(&w->parked, 0);
            InterlockedDecrement(&g_co_parked);
            STRACE("w%d park-skip tnow=%lld runnable=%d", w->index, tnow, co_has_runnable());
            continue;
        }
        STRACE("w%d park to=%lld tnext=%lld io=%d blk=%d dead=%d parked=%d stop=%d",
               w->index, to, tnext, (int)g_io_count, (int)g_blocking_inflight,
#if !defined(_WIN32)
               (int)g_io_dead_count,
#else
               -1,   /* 核心系统底层抽象与内存语义契约 */
#endif
               (int)g_co_parked, (int)g_co_stop);
        co_wait_io(w, to);         /* 核心系统底层抽象与内存语义契约 */
    }
}

/* 内部辅助实现 */
static void co_stats_dump(void) {
    const char *e = getenv("ZAN_CO_STATS");
    if (!e || !*e || e[0] == '0') return;
    zan_co_stats_t tot;
    memset(&tot, 0, sizeof(tot));
    for (int i = 0; i < g_co_workers; i++) {
        zan_co_stats_t *s = &g_wk[i].st;
        fprintf(stderr,
                "COSTAT worker=%d ran=%llu lifo_put=%llu lifo_hit=%llu "
                "lifo_demote=%llu lq_push=%llu lq_pop=%llu spill=%llu "
                "inj_push=%llu inj_pop=%llu steal_ok=%llu steal_fail=%llu "
                "park=%llu wake_post=%llu selfq=%llu\n",
                i, s->ran, s->lifo_put, s->lifo_hit, s->lifo_demote,
                s->lq_push, s->lq_pop, s->spill, s->inj_push, s->inj_pop,
                s->steal_ok, s->steal_fail, s->park, s->wake_post, s->selfq);
        tot.ran += s->ran;                 tot.lifo_put += s->lifo_put;
        tot.lifo_hit += s->lifo_hit;       tot.lifo_demote += s->lifo_demote;
        tot.lq_push += s->lq_push;         tot.lq_pop += s->lq_pop;
        tot.spill += s->spill;             tot.inj_push += s->inj_push;
        tot.inj_pop += s->inj_pop;         tot.steal_ok += s->steal_ok;
        tot.steal_fail += s->steal_fail;   tot.park += s->park;
        tot.wake_post += s->wake_post;     tot.selfq += s->selfq;
    }
    fprintf(stderr,
            "COSTAT total workers=%d shards=%d ran=%llu lifo_put=%llu lifo_hit=%llu "
            "lifo_demote=%llu lq_push=%llu lq_pop=%llu spill=%llu "
            "inj_push=%llu inj_pop=%llu steal_ok=%llu steal_fail=%llu "
            "park=%llu wake_post=%llu selfq=%llu inj_push_ext=%ld sync_inline=%ld\n",
            g_co_workers, (int)g_shards, tot.ran, tot.lifo_put, tot.lifo_hit,
            tot.lifo_demote, tot.lq_push, tot.lq_pop, tot.spill,
            tot.inj_push, tot.inj_pop, tot.steal_ok, tot.steal_fail,
            tot.park, tot.wake_post, tot.selfq,
            (long)g_inj_push_ext, (long)g_sync_inline);
    fflush(stderr);
}

static DWORD WINAPI co_worker_thunk(LPVOID p) {
    co_worker((int)(uintptr_t)p);
    return 0;
}

/* 启动后台工作线程池（进程单例） */
static void co_pool_start_background(void) {
    zan_io_init();   /* 底层系统交互与数据协议契约 */
    g_co_stop = 0;
    /* 内部辅助逻辑 */
    g_co_workers = co_worker_count();
    int w = g_co_workers;
    if (!g_io_shards_live) { io_shards_start(w); g_io_shards_live = 1; }
    g_co_pool_gen = g_co_pool_gen + 1;
    g_co_pool_out = w;
    co_trace("start", NULL);
    int made = 0;
    for (int i = 0; i < w; i++) {
        g_wk[i].bg_gen = g_co_pool_gen;
#if defined(_WIN32)
        HANDLE th = CreateThread(NULL, 0, co_worker_thunk,
                                 (LPVOID)(uintptr_t)i, 0, NULL);
        if (th) { CloseHandle(th); made++; }   /* 底层系统交互与数据协议契约 */
#else
        pthread_t th;
        if (pthread_create(&th, NULL, (void*(*)(void*))co_worker_thunk, (void*)(uintptr_t)i) == 0) {
            pthread_detach(th);
            made++;
        }
#endif
    }
    /* 内部辅助逻辑 */
    g_co_pool_out = made;
    if (made == 0) {
        /* 内部辅助逻辑 */
        g_co_pool_live = 0;
    }
}

/* 内部辅助实现 */
static void co_pool_retire(zan_co_worker_t *w) {
    if (w->bg_gen != g_co_pool_gen || g_co_pool_gen == 0) { return; }
    co_trace("retire", w);
    if (InterlockedDecrement(&g_co_pool_out) != 0) { return; }
    EnterCriticalSection(&g_inj_lock);
    if (g_inj_len > 0) {
        LeaveCriticalSection(&g_inj_lock);
        co_pool_start_background();
        return;
    }
    g_co_pool_live = 0;
    LeaveCriticalSection(&g_inj_lock);
}

static void co_pool_ensure(void) {
    /* 内部辅助实现 */
    if (g_co_pool_live || g_co_pool_fg) return;
    if (InterlockedCompareExchange(&g_co_pool_live, 1, 0) == 0) {
        co_pool_start_background();
    }
}

void zan_co_sched_run(void) {
    /* 内部辅助实现 */
    if (g_co_pool_live || InterlockedCompareExchange(&g_co_pool_fg, 1, 0) != 0) {
        /* 内部辅助实现 */
        for (;;) {
            /* 核心系统底层抽象与内存语义契约 */
            LONG activity = co_cnt_activity();
            int busy = zan_io_has_pending() || zan_timer_pending() > 0 ||
                InterlockedCompareExchange(&g_co_running, 0, 0) > 0 ||
                co_cnt_outstanding() > 0;
            if (!busy && activity == co_cnt_activity()) break;
            Sleep(1);
            /* 内部辅助实现 */
            static __thread DWORD stuck_since = 0;
            DWORD nowk = GetTickCount();
            if (stuck_since == 0) { stuck_since = nowk; continue; }
            if (nowk - stuck_since > 3000) {
                co_trace_dump(zan_co_live_count());
                stuck_since = nowk;
            }
        }
        return;
    }
    zan_io_init();   /* 底层系统交互与数据协议契约 */
    /* 内部辅助逻辑 */
    g_co_workers = co_worker_count();
    int w = g_co_workers;
    g_co_stop = 0;
    /* 内部辅助逻辑 */
    /* 内部辅助逻辑 */
    io_shards_start(w);

#if defined(_WIN32)
    HANDLE th[ZAN_CO_MAXW]; int nt = 0;
    for (int i = 1; i < w; i++) {
        th[nt] = CreateThread(NULL, 0, co_worker_thunk,
                              (LPVOID)(uintptr_t)i, 0, NULL);
        if (th[nt]) nt++;
    }
    co_worker(0);   /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < nt; i++) {
        WaitForSingleObject(th[i], INFINITE);
        CloseHandle(th[i]);
    }
#else
    pthread_t th[ZAN_CO_MAXW]; int nt = 0;
    for (int i = 1; i < w; i++) {
        if (pthread_create(&th[nt], NULL, (void*(*)(void*))co_worker_thunk, (void*)(uintptr_t)i) == 0) {
            nt++;
        }
    }
    co_worker(0);   /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < nt; i++) {
        pthread_join(th[i], NULL);
    }
#endif
    /* 内部辅助实现 */
    if (InterlockedCompareExchange(&g_co_pool_live, 1, 0) != 0) {
        return;             /* 底层系统交互与数据协议契约 */
    }
    InterlockedExchange(&g_co_pool_fg, 0);
    if (!co_all_idle() || g_inj_len > 0) {
        g_co_pool_live = 0;     /* 模块核心语义抽象与接口调用契约 */
        co_pool_ensure();
        return;
    }
    co_stats_dump();
    zan_io_shutdown();
    g_co_pool_live = 0;
    /* 内部辅助实现 */
    if (g_inj_len > 0 || !co_all_idle()) co_pool_ensure();
}

/* 内部辅助实现 */
void zan_co_sched_run_until(const volatile int *done) {
    if ((g_co_pool_live || g_co_pool_fg) && done != NULL) {
        /* 内部辅助实现 */
        int spins = 0;
        /* 内部辅助实现 */
        while (__atomic_load_n(done, __ATOMIC_ACQUIRE) == 0) {
            if (spins < 512) { Sleep(0); spins++; }
            else Sleep(1);
        }
        return;
    }
    (void)done;
    zan_co_sched_run();
}

size_t zan_co_pending(void) {
    size_t n = (size_t)(g_inj_len > 0 ? g_inj_len : 0);
    for (int i = 0; i < g_co_workers; i++) {
        zan_co_worker_t *w = &g_wk[i];
        long long h = __atomic_load_n(&w->head, __ATOMIC_ACQUIRE);
        long long t = __atomic_load_n(&w->tail, __ATOMIC_ACQUIRE);
        if (t > h) n += (size_t)(t - h);
        if (w->lifo_full) n++;
    }
    return n;
}
#else
/* 核心系统底层抽象与内存语义契约 */

void zan_co_sched_init(void) {
    g_rq_head = g_rq_tail = NULL;
    zan_timer_runtime_reset();
    zan_timer_set_ready_hook(zan_co_ready);
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step) return;
    zan_co_node *n = (zan_co_node *)malloc(sizeof(*n));
    if (!n) {
        /* 模块核心语义抽象与接口调用契约 */
        step(frame);
        return;
    }
    n->next = NULL; n->frame = frame; n->step = step;
    if (g_rq_tail) g_rq_tail->next = n; else g_rq_head = n;
    g_rq_tail = n;
}

void zan_co_delay(long long ms, void *frame, zan_co_step_t step) {
    zan_timer_delay(ms, frame, step);
}

/* 模块核心语义抽象与接口调用契约 */
void __zan_co_frame_free(void *frame) { free(frame); }

size_t zan_co_pending(void) {
    size_t n = 0;
    for (zan_co_node *p = g_rq_head; p; p = p->next) n++;
    return n;
}

void zan_co_sched_run_until(const volatile int *done) {
    for (;;) {
        while (g_rq_head) {
            /* 内部辅助逻辑 */
            if (done && __atomic_load_n(done, __ATOMIC_ACQUIRE) != 0) return;
            zan_co_node *n = g_rq_head;
            g_rq_head = n->next; if (!g_rq_head) g_rq_tail = NULL;
            void *frame = n->frame; zan_co_step_t step = n->step;
            free(n);
            step(frame);
        }
        if (done && __atomic_load_n(done, __ATOMIC_ACQUIRE) != 0) return;
        long long timeout = zan_timer_next_timeout();
        if (timeout >= 0) {
            if (timeout > 0) {
                zan_io_pump_timeout(timeout);
                continue;
            }
            zan_timer_dispatch_due();
            continue;
        }
        if (zan_io_pump() > 0) continue;
        return;
    }
}

void zan_co_sched_run(void) {
    zan_co_sched_run_until(NULL);
}
#endif /* _WIN32 */
#endif /* ZAN_CO_DRIVER */

/* 协程计数信号门原语 */
#if defined(ZAN_CO_DRIVER) && !defined(_WIN32)
#include <pthread.h>
#endif
typedef struct zan_gate_waiter {
    struct zan_gate_waiter *next;
    void                   *frame;
    zan_co_step_t           step;
} zan_gate_waiter;

typedef struct zan_gate {
    zan_gate_waiter *head;
    zan_gate_waiter *tail;
    long long        surplus;   /* 核心系统底层抽象与内存语义契约 */
#if defined(ZAN_CO_DRIVER) && defined(_WIN32)
    CRITICAL_SECTION lock;
#elif defined(ZAN_CO_DRIVER)
    pthread_mutex_t  lock;
#endif
} zan_gate;

#if defined(ZAN_CO_DRIVER) && defined(_WIN32)
static void zan_gate_lock(zan_gate *g)   { EnterCriticalSection(&g->lock); }
static void zan_gate_unlock(zan_gate *g) { LeaveCriticalSection(&g->lock); }
static void zan_gate_lock_init(zan_gate *g) { InitializeCriticalSection(&g->lock); }
static void zan_gate_lock_free(zan_gate *g) { DeleteCriticalSection(&g->lock); }
#elif defined(ZAN_CO_DRIVER)
static void zan_gate_lock(zan_gate *g)   { pthread_mutex_lock(&g->lock); }
static void zan_gate_unlock(zan_gate *g) { pthread_mutex_unlock(&g->lock); }
static void zan_gate_lock_init(zan_gate *g) { pthread_mutex_init(&g->lock, NULL); }
static void zan_gate_lock_free(zan_gate *g) { pthread_mutex_destroy(&g->lock); }
#else
static void zan_gate_lock(zan_gate *g)   { (void)g; }
static void zan_gate_unlock(zan_gate *g) { (void)g; }
static void zan_gate_lock_init(zan_gate *g) { (void)g; }
static void zan_gate_lock_free(zan_gate *g) { (void)g; }
#endif

/* 分配a gate; returns an opaque handle (its address as an integer) */
long long zan_gate_new(void) {
    zan_gate *g = (zan_gate *)calloc(1, sizeof(*g));
    if (!g) return 0;
    zan_gate_lock_init(g);
    return (long long)(intptr_t)g;
}

/* 将当前协程挂起在信号门等待队列 */
void zan_gate_park(long long handle, void *frame, zan_co_step_t step) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) { if (step) zan_co_ready(frame, step); return; }
    if (!step) return;
    zan_gate_lock(g);
    if (g->surplus > 0) {
        g->surplus--;
        zan_gate_unlock(g);
        zan_co_ready(frame, step);   /* 核心系统底层抽象与内存语义契约 */
        return;
    }
    zan_gate_waiter *w = (zan_gate_waiter *)malloc(sizeof(*w));
    if (!w) { zan_gate_unlock(g); zan_co_ready(frame, step); return; }
    w->next = NULL; w->frame = frame; w->step = step;
    if (g->tail) g->tail->next = w; else g->head = w;
    g->tail = w;
    zan_gate_unlock(g);
    /* 模块核心语义抽象与接口调用契约 */
}

/* 内部辅助逻辑 */
void zan_gate_signal(long long handle) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_lock(g);
    zan_gate_waiter *w = g->head;
    if (w) {
        g->head = w->next;
        if (!g->head) g->tail = NULL;
        zan_gate_unlock(g);
        zan_co_ready(w->frame, w->step);
        free(w);
        return;
    }
    g->surplus++;
    zan_gate_unlock(g);
}

/* Destroy a gate */
void zan_gate_free(long long handle) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_lock(g);
    zan_gate_waiter *w = g->head;
    g->head = g->tail = NULL;
    zan_gate_unlock(g);
    while (w) {
        zan_gate_waiter *n = w->next;
        zan_co_ready(w->frame, w->step);
        free(w);
        w = n;
    }
    zan_gate_lock_free(g);
    free(g);
}
