# examples/net — 网络库示例

每个示例都是单文件、可独立编译运行的完整程序：同一进程内先启动服务端协程，
再用客户端访问它，因此不需要任何外部服务。

编译运行（在仓库根目录）：

```
build/zanc.exe examples/net/tcp_echo.zan --auto-stdlib -o tcp_echo.exe
./tcp_echo.exe
```

| 文件 | 协议 | 演示要点 |
| --- | --- | --- |
| `tcp_echo.zan` | TCP | 监听/异步 accept、一协程一连接、字节流分帧（按行）、多客户端并发、对端关闭检测 |
| `udp_messaging.zan` | UDP | 无连接数据报、消息边界、请求-应答模式、丢包/重发注意事项 |
| `http_server.zan` | HTTP 服务端 | 路由分发、JSON API、查询串、400/404 错误处理、连接数/超时/请求体上限保护 |
| `http_client.zan` | HTTP 客户端 | GET/POST/PUT/DELETE、默认请求头、静态便捷方法、JSON 响应解析 |
| `sse_events.zan` | SSE | 服务器单向推送：订阅握手、命名事件、多行 data、保活注释、广播多个订阅者 |
| `websocket_chat.zan` | WebSocket | 双向长连接：握手、文本帧收发、Ping 心跳、优雅关闭、多客户端 |
| `mqtt_pubsub.zan` | MQTT | 内置 MqttBroker（无需外部服务）：连接、订阅（+/# 通配符）、QoS 0/1 发布、多订阅者扇出 |
| `worker_server.zan` | Worker 统一服务端 | workerman 风格开箱即用：一个进程同时服务 6 个协议、回调式业务、count=N 一键多进程分流、状态自动刷新 |

选型速查：

- 请求/响应、可缓存 → HTTP（`HttpServer` / `HttpClient`）
- 服务器持续单向推送（行情、日志、进度） → SSE（`SseServer` / `SseClient`）
- 双向低延迟长连接（聊天、游戏、协同） → WebSocket
- 自定义二进制协议、极致控制 → 直接用 TCP（`TcpListener` / `TcpClient`）
- 允许丢包但要低开销（心跳、发现、遥测） → UDP（`UdpClient`）
- 物联网发布/订阅 → MQTT（`System.Net.Mqtt.MqttClient` + 内置 `MqttBroker`，见 `mqtt_pubsub.zan`）
- 生产形态统一服务端（master+多 worker 守护分流） → `System.Net.Worker`（见 `worker_server.zan`）
