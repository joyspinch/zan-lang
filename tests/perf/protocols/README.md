# 通讯协议压测基线

度量 Zan 网络栈**不经 MVC** 的每请求开销：协议解析/编码 + 路由匹配 + 应答写出。
与 [`../web_mvc`](../web_mvc/README.md)（全框架路径）互补——两边的差就是框架
（属性路由派发、控制器、模板渲染）的开销。

| 场景 | 协议 | 客户端 | 内容 |
|---|---|---|---|
| `h1-hello` | 明文 HTTP/1.1 keep-alive | `../web_mvc/load.zan`（裸套接字） | `GET /hello`，5 字节文本；8 / 32 连接 |
| `h1-4k` | 同上 | 同上 | `GET /4k`，4 KB 文本；8 连接 |
| `https-hello` | HTTPS/1.1 keep-alive | `HttpClient` | `GET /hello`；8 连接 |
| `h2-hello` | HTTP/2 over TLS | `Http2ClientConnection` | `GET /hello`；8 连接 × 1 / 8 并发流 |
| `ws-echo64` | WebSocket（明文） | `WebSocketClient` | 64 字节文本回显；8 / 32 连接 |

服务端单进程起两个 `WebApp`：明文端口（HTTP/1.1、WebSocket）与 TLS 端口
（HTTPS/1.1、HTTP/2——`WebApp` 只在 TLS 上接 h2 前言，没有明文 h2c）。

## 运行

```bash
python3 tests/perf/protocols/run_bench.py --out tests/perf/protocols/baselines/<机器>.json
python3 tests/perf/protocols/run_bench.py --compare tests/perf/protocols/baselines/<机器>.json
python3 tests/perf/protocols/run_bench.py --only=h2,ws --seconds=5   # 只跑部分场景
```

- 需要已构建的 `build/zanc`；可执行文件产出到 `build/perf/`，证书与服务端日志在
  `_scratch/bench_protocols/`。
- TLS 场景用 `openssl` 现生成一次性自签证书（不进仓库）；PATH 上没有 openssl 就跳过。
- `--compare`：吞吐跌幅 > 15% 或 p99 涨幅 > 30% 即非零退出；任何 `errors` /
  `non200` / `stuck` 也非零退出。
- **只在同一台机器、空闲状态下比对才有意义**；驱动记录 1 分钟负载，忙时打印警告。
- 不进 ctest：一轮几分钟、数字机器相关。

## 读数注意

- 客户端也是 Zan 写的，和服务端同机抢 CPU。`h1` 用裸套接字客户端，客户端开销最小；
  `https`/`h2`/`ws` 走标准库客户端，**它们的数字同时反映客户端与服务端**。
- `rps` 对 ws 是每秒回显消息数（一来一回算一次）。

## 历史与已知问题

- **已修（2026-11-23）：h2 客户端每请求 ≥2 ms 延迟底。** `Http2ClientConnection.SendAsync`
  等流完成用 `Task.Delay(2)` 轮询。改为在流的门上带时限等待（`Http2Stream.gate` 由
  `AsyncGate` 换成 `Gate`：带超时等待走运行时定时器，Signal/Close 加锁，顺带消除
  读循环 `Complete→Signal` 与 `SendAsync` 收尾 `Close` 的竞态）。同机交替 A/B：
  8 连接单流 3.0k → 15.7k rps，p50 2.4 → 0.4 ms。
- **已修（2026-11-23）：WebSocket 客户端每帧一次 fopen/fread/fclose /dev/urandom。**
  每帧 4 字节掩码走 `RandomNumberGenerator.Fill`，POSIX 实现每次 `fopen` + 整块缓冲
  `fread`（KB 级）+ `fclose`，客户端 CPU 大半耗在读 urandom。改为 Apple 走
  `getentropy`、其余 POSIX 走无缓冲 `open/read`。回显 1 连接 4.6k → 15.6k、
  8 连接 7.0k → 39.5k msg/s。所有用 CSPRNG 的路径（TLS、令牌、UUID）同受益。
- **已修（2026-11-23）：同进程两个 WebApp 并发 Start 偶发启动即 SIGSEGV。**
  `Worker.ShareHandle` 的两张静态 List 无锁，明文 + TLS 两个 WebApp 在不同调度线程上
  同时登记共享表句柄，并发 Add 扩容崩（约 1/16 次启动）。加锁，派生 worker 时取快照。
  回归用例 `conformance/worker_share_handle_concurrent`（旧代码 26/30 崩）。
- **已修（2026-11-23）：h2 单连接 8 并发流时 p99 50–60 ms（单流约 1 ms）。** 服务端与客户端
  埋点 + 请求/应答头里互打同机单调时钟戳拆单程：慢在"客户端发出 → 服务端读循环派发
  HEADERS"，且派发前读循环并未停泊——每连接**一个串行读循环积压**，请求在套接字里排队。
  三处：① `Http2ServerConnection.ReadExactAsync` 原以 `FillTo(count)` 只收恰好 count
  字节，每帧帧头 9 字节与载荷各一次接收（TLS 下各过一次锁/Step/flush 检查）；改有界预读
  max(count, 32 KB)。② 客户端每个 DATA 帧回一个连接级 WINDOW_UPDATE（小应答即每请求
  多一帧入站）；改攒满半窗（32 KB）再补。③ 服务端小应答 HEADERS 与 DATA 各一次发送、各一条
  TLS 记录；窗口够且单帧装得下时合成一次写。同机交替 A/B 4 轮中位：8×8 20.1k → 34.3k rps、
  p99 59 → 12.7 ms；8×1 14.9k → 24.8k；1×8 12.0k → 20.7k（4/4 胜）。剩余：8×8 p99 仍为
  单流的 ~20 倍（读循环仍串行）；流量窗口耗尽时两端仍以 `Task.Delay(2)` 轮询（>64 KB 应答）；
  服务端对请求正文 DATA 仍逐帧回两个 WINDOW_UPDATE。
- **已修（2026-11-23）：协程注册表一把全进程自旋锁。** 与同机 C 参考服务端（每连接一线程、
  阻塞 IO、OpenSSL，同一 C 客户端压）对比：h1/https 吞吐约为 C 的 1/3、ws 约 55%，每请求
  CPU 约 7 倍；剖析显示 38% 忙 CPU 在 `zan_task_child/frame_config/retire`——
  `rt_timer_task.c` 每个协程创建、每次 await 设/清边都拿同一把忙等锁（实测每 HTTP 请求约
  110 次，近半争用）。改为按帧地址 64 分片、await 边带子帧化身戳（不再维护反向边，退役只删
  自己），每操作至多持一把分片锁；争用降到 0.2%。同机交替 A/B 3 轮（负载 16–25）：h1 8 连接
  47k → 64k rps、每请求 CPU 58 → 40 µs；ws 74k → 85k；https 34k → 41k（全胜）。之后的
  热点是包层逐请求锁：`ServerMetrics.Entered/Left`、`HttpBufferPool.Rent/Return`。
- **未覆盖**：明文 h2c（`WebApp` 不支持）、HTTP/2 大响应、WSS、数据库/Redis 线协议。
