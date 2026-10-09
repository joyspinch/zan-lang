/* 内部辅助实现 */
#ifndef ZAN_RT_IO_H
#define ZAN_RT_IO_H

#include <stdint.h>
#include "rt_co.h"   /* 核心系统底层抽象与内存语义契约 */

/* 底层系统交互与数据协议契约 */
#define ZAN_IO_READ  1
#define ZAN_IO_WRITE 2

/* ---- lifecycle ---- */
void zan_io_init(void);
void zan_io_shutdown(void);
void zan_io_socket_cleanup(void);
/* 内部辅助实现 */
int64_t zan_io_socket_send(intptr_t fd, const void *buf, int64_t len,
                           int32_t flags);
int64_t zan_io_socket_recv(intptr_t fd, void *buf, int64_t len,
                           int32_t flags);
int32_t zan_io_socket_ready(intptr_t fd, int32_t write_ready);

/* 探测非阻塞连接结果：成功返回 0，失败返回 SO_ERROR 错误码，仍在连接中返回 -1 */
int32_t zan_io_connect_status(intptr_t fd);

/* 检查是否`fd` still refers to an open socket */
int32_t zan_io_socket_alive(intptr_t fd);

/* 内部辅助实现 */
void zan_io_close_notify(intptr_t fd);
int64_t zan_io_socket_peer_ipv4(intptr_t fd);

/* 域名 IPv4 解析：成功返回 0 并填充网络序地址，失败返回 -1 */
int32_t zan_io_resolve_ipv4(const char *hostname);

/* 内部辅助实现 */
int32_t zan_io_resolve_sa(const char *name, int32_t port, void *buf,
                          int32_t cap);

/* 底层系统交互与数据协议契约 */
#define ZAN_IO_SA_STRIDE 32
int32_t zan_io_resolve_all(const char *name, int32_t port, void *buf,
                           int32_t cap);
/* 异步原生 extern 专用纯标量指针 ABI 域名解析 */
int64_t zan_io_resolve_all_async(intptr_t name_ptr, int32_t port,
                                 intptr_t buf_ptr, int32_t cap);

/* 获取 sockaddr 地址族 (AF_INET / AF_INET6)，无效或不支持返回 0 */
int32_t zan_io_sockaddr_family(const void *sa, int32_t len);
/* 检查目标地址是否为公网可路由地址（排除本地回环） */
int32_t zan_io_sockaddr_is_safe(const void *sa, int32_t len,
                               int32_t allow_loopback);

/* 启动底层 sockaddr 原始连接：已连接返回 0，进行中返回 EINPROGRESS，错误返回正数错误码 */
int32_t zan_io_connect_sa_start(intptr_t fd, const void *sa, int32_t salen);
/* 工作线程安全异步 ABI：原始 sockaddr 连接 */
int64_t zan_io_connect_sa(intptr_t fd, const void *sa, int32_t salen,
                           int32_t timeout_ms);

/* 将 sockaddr 二进制地址格式化为标准 IP 字符串 (IPv4 点分十进制 / IPv6 冒号十六进制) */
const char *zan_io_sockaddr_ip_str(const void *sa);

/* 内部辅助实现 */
void zan_io_resolve_sa_co(const char *name, int32_t port, void *buf,
                          int32_t cap, void *frame, zan_co_step_t step,
                          int32_t *out);

#if defined(_WIN32)
/* 从 Windows PCCERT_CONTEXT 读取编码的 DER 证书指针与长度 */
const unsigned char *zan_crypto_cert_encoded(const void *cert, int *out_len);
/* 底层系统交互与数据协议契约 */
int32_t zan_io_crypto_windows_ssl_policy(const unsigned char *certs, int32_t total_len,
                                       int32_t count, const char *host, int32_t host_len);
#endif

/* 无栈协程 (CPS 状态机) 运行时 ABI：支撑 await 原语在反应堆上的挂起与恢复 */
void zan_io_wait_co(intptr_t fd, int32_t interest, void *frame, zan_co_step_t step);

/* 内部辅助实现 */
void zan_io_recv_co(intptr_t fd, void *buf, int32_t len, void *frame,
                    zan_co_step_t step, int64_t *out_n);

/* Windows IOCP 带超时重叠接收：在反应堆中挂起并在就绪或超时后恢复协程帧 */
void zan_io_recv_to_co(intptr_t fd, void *buf, int32_t len, int64_t timeout_ms,
                       void *frame, zan_co_step_t step, int64_t *out_n);

/* Windows AcceptEx 重叠接受连接挂起 */
void zan_io_accept_co(intptr_t fd, void *frame, zan_co_step_t step,
                      intptr_t *out_fd);

/* 内部辅助实现 */
void zan_io_resolve_co(const char *hostname, void *frame, zan_co_step_t step,
                       int32_t *out);

/* 脱离反应堆主循环执行阻塞原生调用：在线程池中派发并在完成后唤醒协程 */
void zan_rt_blocking_co(void *fn, int32_t argc,
                        int64_t a0, int64_t a1, int64_t a2, int64_t a3,
                        void *frame, zan_co_step_t step, int64_t *out);

/* 内部辅助实现 */
int32_t zan_io_pump(void);

/* 定时器感知的空闲调度桥接：在超时时间内轮询 IO 事件并驱动定时任务 */
int32_t zan_io_pump_timeout(int64_t timeout_ms);

/* 底层系统交互与数据协议契约 */

/* 挂起当前协程直至 fd 可读：成功返回 0，失败返回 -1 */
int64_t zan_io_wait_readable(intptr_t fd);

/* 底层系统交互与数据协议契约 */
int64_t zan_io_wait_writable(intptr_t fd);

/* 挂起当前协程直至 fd 可读或超时：可读返回 1，超时返回 0，错误返回 -1 */
int64_t zan_io_wait_readable_timeout(intptr_t fd, int64_t timeout_ms);

/* 底层系统交互与数据协议契约 */
int64_t zan_io_connect(intptr_t fd, const char *ip, int32_t port);

/* ---- scheduler-facing ---- */

/* 带超时轮询底层 IO 事件：返回唤醒并投递至就绪队列的协程数 */
int32_t zan_io_poll(int64_t timeout_ms);

/* 底层系统交互与数据协议契约 */
int32_t zan_io_has_pending(void);

/* 底层系统交互与数据协议契约 */
int32_t zan_io_set_nonblocking(intptr_t fd);

#endif /* ZAN_RT_IO_H */
