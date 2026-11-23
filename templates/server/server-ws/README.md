# {{NAME}} — Gateway-Worker WebSocket 聊天服务

一个可横向扩展思路的 WebSocket 服务骨架，跑在标准库 `Worker` 统一宿主上：
监听、RFC 6455 握手、帧编解码、ping/pong、分片重组全部由运行时负责，
模板只写回调与业务。

## 运行

在 IDE 里直接运行（输出进终端面板），然后浏览器打开
<http://127.0.0.1:8090/>，多开几个标签页互相聊天。

```
zanc src/main.zan src/**/*.zan --auto-stdlib -o app
./app            # 在项目目录运行：读取 ./config/app.json，发 ./wwwroot
```

同一端口上，普通 HTTP 请求发 `wwwroot/` 里的页面，带 `Upgrade: websocket`
的请求升级为 WebSocket；`GET /health` 返回在线人数。

## 目录

```
config/app.json              端口、wwwroot、昵称/消息字节上限、历史条数、进程数（运行期读取）
src/main.zan                 建 http Worker，挂 HTTP 与 WebSocket 回调
src/framework/Config.zan     读 config/app.json，缺键回退默认值
src/framework/StaticFiles.zan 静态文件（防目录穿越、按扩展名给 Content-Type）
src/gateway/Gateway.zan      在线连接表：登记 / 移除 / 广播
src/gateway/ClientConn.zan   一个在线客户端：连接引用 + 会话状态（昵称）
src/worker/ChatMessage.zan   JSON 协议定义（收发消息的唯一真源）
src/worker/ChatWorker.zan    聊天业务：改名、发言、历史回放、在线列表
wwwroot/                     浏览器端聊天页（index.html + css + js）
```

## 协议

| 方向 | 消息 |
|---|---|
| 客户端 → 服务端 | `{"type":"nick","nick":"小明"}` 改昵称；`{"type":"say","text":"大家好"}` 发言 |
| 服务端 → 客户端 | `welcome`（你的 id 与显示名）、`chat`（发言）、`system`（进出/改名通知）、`users`（在线列表）、`error`（校验失败） |

命令行也能测：`websocat ws://127.0.0.1:8090/ws`，然后输入
`{"type":"say","text":"hi"}`。

## 为什么分 Gateway 与 Worker

**Worker**（标准库）搬字节；**Gateway** 维护在线连接表并负责扇出；
**ChatWorker** 只处理业务、从不碰 socket。要换成推送服务、协同编辑或游戏房间，
只需替换 `src/worker/` 下的两个文件，传输层不动。

## 多进程与多机

把 `config/app.json` 的 `worker.count` 设为 N，同一个二进制就变成"主进程 + N 个
工作进程"（崩溃自动拉起，Linux/macOS 用 `SO_REUSEPORT` 分摊 accept，Windows 走
句柄移交），代码不用改。

但进程 A 的在线表看不到进程 B 的连接，所以跨进程/跨机器广播需要一条共享总线——
即 Gateway/Worker/Register 模型里的 **Register** 角色：

1. 每个网关订阅同一个 Redis 频道；
2. `Gateway.Broadcast` 改为向 Redis 发布（或本地发送 + 发布）；
3. 每个网关收到发布的消息后，再扇出给**本进程**的连接。

`System.Data.Redis` 提供异步 pub/sub 客户端。它是预留的扩展点，模板里没有接入，
以保证零外部依赖即可编译运行。
