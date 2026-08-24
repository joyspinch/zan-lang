# System.Net.Sse

> 源码: `stdlib/System/Net/Sse/Sse.zan`


## SseClient (class)

Server-Sent Events 客户端：建立连接、完成 HTTP 握手，然后
逐个产出解析好的事件。

SseClient c = await SseClient.ConnectAsync("127.0.0.1", 8080, "/events");
SseEvent e = await c.NextAsync();

- TcpClient client;

- string buf;

- bool connected;

- static async SseClient ConnectAsync(string host, int port, string path)
  - 建立连接并发送订阅请求。服务器拒绝该流时，返回的客户端
    其 IsConnected() 为 false。

- bool IsConnected()

- async SseEvent NextAsync()
  - 阻塞（挂起协程）直到下一条完整事件
    到达。服务器关闭流时返回 null。注释行
    （keep-alive 心跳）会被透明跳过。

- SseEvent ParseBlock(string block)
  - 解析一个事件块（"字段: 值" 形式的行）。块中
    没有 event/data/id 字段（纯注释）时返回 null。

- void Close()


## SseConnection (class)

服务器端一条已接受的 SSE 订阅者连接。通过
SseServer.AcceptAsync 在 HTTP 握手后取得；用
SendAsync/SendDataAsync 推送事件，最后以 Close 收尾。

- TcpClient client;

- string path;

- bool open;

- static SseConnection Of(TcpClient client, string path)

- string Path()
  - 订阅者请求的路径（如 "/events"）。

- bool IsOpen()

- async int SendAsync(string eventName, string data)
  - 发送具名事件。data 中的每个 '\n' 都会变成独立的
    `data:` 行，保证多行负载在线上格式中不被破坏。

- async int SendDataAsync(string data)
  - 发送匿名（"message"）事件。

- async int PingAsync()
  - 发送一条 SSE 注释行，可用作 keep-alive 心跳。

- void Close()


## SseEvent (class)

一条 Server-Sent Events 消息：可选的 `event` 名称、`data`
负载（按 SSE 规范，多行 data 字段用 '\n' 连接），
以及可选的 last-event `id`。

- string name;

- string data;

- string id;

- SseEvent()

- string Name()
  - 事件名（流未设置时默认为 "message"）。

- string Data()

- string Id()


## SseServer (class)

极简 Server-Sent Events 服务器：接受 HTTP GET 订阅者，响应
`text/event-stream` 握手，并交回一个 SseConnection 供
应用推送事件。

SseServer srv = new SseServer("127.0.0.1", 8080);
srv.Start();
SseConnection c = await srv.AcceptAsync();
await c.SendAsync("tick", "hello");

- TcpListener listener;

- bool running;

- SseServer(string host, int port)

- void Start()

- int GetPort()

- async SseConnection AcceptAsync()
  - 等待下一个订阅者，读取其 HTTP 请求头，
    并完成 event-stream 握手。客户端在握手中途断开时
    返回 null。

- void Stop()


## SseText (class)

SSE 各内部类共用的逐字节子串搜索
（避免逐字符 Substring 分配）。

- static int Find(string s, string needle, int from)
  - `needle` 在 `s` 中从 `from` 起（含）首次出现的位置，找不到返回 -1。
