# Zan 网络库性能报告

日期：2026-07-22。所有数字均为本轮实测，无估算值。

## 1. 测试环境

| 项 | 值 |
| --- | --- |
| CPU | Intel Core i9-14900HX（32 逻辑核） |
| OS | Windows |
| Zan 编译 | `build/zanc.exe <src> --auto-stdlib --publish`（Release） |
| C# 运行时 | .NET SDK 10.0.301，ASP.NET Core Kestrel（Release） |
| 网络 | 本机回环 127.0.0.1（客户端与服务端同机） |

## 2. 方法

- 负载客户端统一使用 C#（原始 Socket，非 HttpClient），代码在 `_scratch/bench/client/Program.cs`。
  每个连接一个线程循环执行"发请求 → 收完整响应"，记录每次往返的微秒级延迟。
- 预热 3 秒（不计数），测量 10 秒；QPS = 测量窗口内完成的操作数 / 实际耗时。
- 延迟分位数由测量窗口内全部样本排序得到（p50/p95/p99/max）。
- 服务端 CPU 与内存由 PowerShell 每 500ms 采样（`TotalProcessorTime`、`WorkingSet64`），
  CPU% 以单核为 100%（32 核机器满载 = 3200%）。脚本：`_scratch/bench/sample.ps1`，原始 CSV 在 `_scratch/bench/s_*.csv`。
- Zan 服务端：`_scratch/bench/zsrv.zan`（单进程，6 协议）与 `zsrv4.zan`/`zsrv8.zan`
  （HTTP，master + 4/8 worker）。均关闭状态刷新（`Worker.SetStatusInterval(0)`），
  热路径无控制台输出。
- 多进程模型（Windows）：master 在自己的 IOCP 上 `AcceptEx` 接受全部连接，把
  已接受的 socket 经 `WSADuplicateSocket` 轮转分发给 worker（每监听器每 worker
  一条专用交接连接），全程事件驱动、无任何轮询/定时等待。
- C# 服务端：`_scratch/bench/csrv/Program.cs`，Kestrel 最小 API，与 Zan 返回完全相同的
  `text/plain` "hello world"（11 字节）。

## 3. HTTP：Zan vs C#（同负载、同客户端、同响应）

| 服务端 | 连接数 | QPS | p50 | p95 | p99 | CPU（单核=100%） | 内存峰值 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Zan 单进程 | 64 | 72,519 | 550µs | 778µs | 1050µs | ~100%（单核饱和） | ≈8 MB |
| Zan 单进程 | 128 | 70,982 | 597µs | 742µs | 869µs | ~100%（单核饱和） | ≈8 MB |
| Zan 4 worker | 64 | 235,790 | 165µs | 296µs | 414µs | 343% | 5×≈5.6 MB |
| Zan 4 worker | 128 | 239,556 | 161µs | 291µs | 401µs | 343% | 5×≈5.6 MB |
| Zan 8 worker | 64 | 268,091 | 150µs | 230µs | 302µs | 504% | 9×≈5.1 MB |
| Zan 8 worker | 128 | 294,923 | 137µs | 204µs | 274µs | 504% | 9×≈5.1 MB |
| Zan 16 worker | 256 | 281,914 | 142µs | 209µs | 273µs | 532% | 17×≈5.3 MB |
| C# Kestrel | 64 | 242,815 | 134µs | 186µs | 436µs | 932% | 58.8 MB |
| C# Kestrel | 128 | 243,241 | 132µs | 187µs | 485µs | 1188% | 58.4 MB |

（CPU 列为该配置两轮中采样窗口的总核数占用；4/8/16 worker 的 CPU/RSS 原始 CSV 在
`_scratch/bench/s2_http*.csv`。）

要点：

- **绝对吞吐**：Zan 8 worker ≈ **295k QPS**，超过 Kestrel（243k）约 **21%**，
  且只用约 5 个核（Kestrel 用约 11.9 核）。16 worker 不再提升（282k），
  瓶颈转移到客户端与回环栈。
- **每核效率**：Zan 8 worker ≈ 295k/5.0 核 ≈ **59k QPS/核**；4 worker ≈ 240k/3.4 核 ≈
  **70k QPS/核**；Kestrel ≈ 243k/11.9 核 ≈ **20k QPS/核**。同等吞吐下 Zan 的 CPU
  成本约为 Kestrel 的 1/3。
- **内存**：Zan 每进程 ≈ 5-8 MB（8 worker 合计约 46 MB）；Kestrel ≈ 59 MB。
- **相对上一轮的提升**：单进程 64k→73k（HTTP 解析改为单次、去掉重复 Parse），
  4 worker 229k→240k 且 max 延迟 508ms→14ms（去掉 1ms 轮询 accept，
  改为 master IOCP 接受 + 每连接事件驱动交接）。
- Zan 单进程是单核事件循环，吞吐受单核限制；进一步扩展靠 `count = N` 多
  worker，多进程模型详见 `stdlib/System/Net/Worker.zan` 头注释。

## 4. 其他协议（Zan 单进程自身基线）

| 协议 | 负载 | 吞吐 | p50 | p95 | p99 | CPU | 内存峰值 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| TCP echo | 64 连接，16B 消息 | 38,013 msg/s | 1064µs | 1394µs | 1459µs | 85% | 7.4 MB |
| UDP echo | 32 socket，22B 数据报 | 35,230 dgram/s | 896µs | 989µs | 1092µs | 84% | 32.5 MB |
| WebSocket echo | 64 连接，4B 文本帧 | 80,950 msg/s | 506µs | 651µs | 764µs | 86% | 72.7 MB |
| SSE 推送 | 16 订阅者，服务端全速推 | 97,403 event/s | 32µs* | 35µs* | 115µs* | 82% | 72.7 MB |
| MQTT QoS1 发布 | 32 连接，16B 载荷 | 59,597 pub/s | 530µs | 635µs | 766µs | 82% | 215.8 MB† |

\* SSE 的"延迟"是订阅端相邻事件间隔（服务端主动推送，无请求-响应往返），不是投递延迟。
† MQTT 内存持续增长：broker 每包 `calloc` 读缓冲且未释放（与 MqttClient 相同的模式），
600k 包后 RSS 达 216 MB。这是已知问题，修复方向是复用读缓冲/接入运行时回收；
其余协议 RSS 稳定。

以上均为单核事件循环（CPU ≈ 85% 单核即饱和）。TCP/WS/SSE/MQTT 也可用
`count = N` 起多 worker 分流（Windows 由 master 接受后按连接分发，POSIX 用
`SO_REUSEPORT`；Windows 下 UDP 无 accept、由 master 进程直接服务），本轮只测了
HTTP 的多进程扩展。

## 5. 复现

```text
# Zan 服务端
build/zanc.exe _scratch/bench/zsrv.zan  --auto-stdlib --publish -o _scratch/bench/zsrv.exe
build/zanc.exe _scratch/bench/zsrv4.zan --auto-stdlib --publish -o _scratch/bench/zsrv4.exe
build/zanc.exe _scratch/bench/zsrv8.zan --auto-stdlib --publish -o _scratch/bench/zsrv8.exe
_scratch\bench\zsrv.exe            # http:56001 tcp:56002 ws:56003 udp:56004 sse:56005 mqtt:56006
_scratch\bench\zsrv4.exe           # http:56011，master + 4 worker
_scratch\bench\zsrv8.exe           # http:56012，master + 8 worker

# C# 服务端
dotnet build _scratch/bench/csrv -c Release && _scratch\bench\csrv\bin\Release\net10.0\csrv.exe   # http:56101

# 负载客户端：client <mode> <host> <port> <连接数> <秒数> [预热秒数]
dotnet build _scratch/bench/client -c Release
_scratch\bench\client\bin\Release\net10.0\client.exe http 127.0.0.1 56001 64 10 3

# CPU/内存采样（与客户端并行）
powershell -File _scratch/bench/sample.ps1 -names zsrv -seconds 16 -outfile out.csv
```

## 6. 局限与注意事项

- 回环测试：无网卡/网络栈瓶颈，数字代表框架/协议栈开销上限，不代表跨机吞吐。
- 客户端与服务端同机：客户端本身消耗若干核（32 核机器上不构成瓶颈，但在小核机上会）。
- 每组只跑一轮 10 秒；未做多轮方差统计。
- HTTP 响应为 11 字节小包，未覆盖大包/流式场景。
- UDP 在回环上未观测到丢包；跨机场景需要按丢包重发另行设计客户端。
- Kestrel 使用默认配置（未调 `ThreadPool`/`ListenOptions`），Zan 侧也未做专项调优。

## 7. 过程中发现并处理的问题

1. **编译器 bug（已绕过，待修复）**：实例 `async` 方法内写类静态字段时，async 降级把静态
   按值捕获进协程帧，写入落在副本上丢失（探针：10 个协程各自增 2 次，期望 20 实得 1；
   静态方法/静态 async 方法中则正确）。Worker 的统计计数改为经静态辅助方法
   `BumpStat()` 写入。修复应让 async 降级对静态字段保持引用语义。
2. **Windows 跨进程环境变量**：`cmd /c set X=..&& exe` 与 CRT `getenv` 都读不到
   继承环境，改用 `SetEnvironmentVariableA` + `CreateProcess` 继承 +
   `GetEnvironmentVariableA` 读取（`ProcessHost.Env` 已改）。
3. **运行时 bug（已修复）**：`rt_io.c` 在 `AcceptEx` 完成时把新 socket 立即绑定到
   本进程 IOCP。IOCP 绑定作用于内核文件对象且终生只能绑一个端口，导致该 socket
   经 `WSADuplicateSocket` 交给 worker 后，worker 的收发完成事件仍投递到 master
   的端口、worker 永远收不到（此前误判为"复制的监听 socket 上 AcceptEx 不可靠"
   并用 1ms 轮询 accept 规避）。修复：接受完成时不再预绑定，由实际执行 IO 的
   进程在首个重叠操作时经 `ensure_assoc` 绑定自己的 IOCP。轮询规避代码已删除，
   多进程路径现为纯事件驱动（master `AcceptEx` 接受 + 每连接 `WSADuplicateSocket`
   交接 + worker IOCP 收发）。
4. **MQTT broker 读缓冲未释放导致内存增长**（见上表†）。
