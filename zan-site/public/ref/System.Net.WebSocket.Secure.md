# System.Net.WebSocket.Secure

> 源码: `packages/Zan.Net/src/System/Net/WebSocket/Secure/WssClient.zan`, `packages/Zan.Net/src/System/Net/WebSocket/Secure/WssServer.zan`


## WssClient (class)

基于 TLS 的 RFC 6455 WebSocket 客户端，客户端帧带掩码。

- TcpClient conn;

- TlsContext context;

- TlsStream stream;

- string host;

- string path;

- WsReader reader;

- WsAssembler asm;

- bool connected;

- bool sendBusy;

- List<AsyncGate> sendWaiters;

- int lastOpcode;

- int lastLen;

- byte[]lastBinary;

- WssClient()
  - 内部构造：未连接；经 ConnectAsync 使用。

- static async WssClient ConnectAsync(string host, int port, string path)
  - 发起 wss://host:port/path 握手：TCP 连接 → TLS 握手（带 SNI
    与证书校验）→ WebSocket Upgrade（随机 key，校验 Sec-WebSocket-
    Accept）。绝不返回 null：任何一步失败都返回 connected=false
    的对象（失败路径已清理连接/上下文），用 IsConnected() 判断。

- bool IsConnected()
  - 握手是否成功且连接未断。

- async void SendText(string text)
  - 发送一条 Text 消息（fire-and-forget；发送失败会就地断开）。

- async void SendBinary(string bytes, int len)
  - 发送一条二进制消息（bytes 可含 NUL，len 为精确字节数）。

- async void SendBinary(byte[]bytes, int offset, int len)
  - 发送一条二进制消息（byte[] 原生切片，无需经过字符串中转）。

- async void SendBinary(byte[]bytes)
  - 发送一条完整二进制消息。

- async void Ping()
  - 发送空 Ping 帧（对端的 Pong 由 RecvText 静默消费）。

- int LastOpcode()
  - 上一次成功接收并交付的消息操作码（WsOpcode.Text 或 WsOpcode.Binary）。

- int LastLength()
  - 上一次成功接收并交付的消息精确字节数。

- async string RecvText()
  - 挂起直到下一个文本/二进制消息（分片消息重组到 fin 后整条
    交付；Ping 以 Pong 应答后继续；Close、协议错误（-2/-3）或
    断开返回 ""）。

- async byte[]RecvBinary()
  - 挂起直到下一个文本/二进制消息。返回未截断的原生 byte[]，
    彻底消除二进制消息中 NUL 字符被字符串截断的隐患；Close 或断开返回 null。

- async void Close()
  - 协礼关闭：先发空关闭帧（未连接时跳过），随后无条件断开并
    释放 TLS 流与上下文；重复调用安全。

- async void AcquireSend()

- void ReleaseSend()

- async void SendFrame(int opcode, string payload, int payloadLen)
  - 客户端发送入口：RFC 6455 要求客户端帧必须掩码（每帧 CSPRNG
    掩码键）+ 正确的扩展长度编码。并发发送受锁保护，保证原子串行。发送失败（<= 0）就地断开。

- async void SendFrameBytes(int opcode, byte[]payload, int offset, int payloadLen)
  - 客户端原生字节数组发送入口：掩码 + 帧长 + 互斥串行发送。

- void CloseNow()
  - 立即断开并释放全部句柄（TLS 流、连接、上下文、reader）。

- static string Header(string headers, string name)
  - 从响应头文本里取单个头值（ASCII 大小写不敏感，跳过状态行，
    去掉值前导空格）；不存在为 ""。

- static bool AsciiEquals(string a, string b)
  - ASCII 大小写不敏感的相等比较。

- static int IndexOf(string hay, string needle, int from)
  - 从 from 起的子串搜索；没有返回 -1。


## WssServer (class)

基于 TLS 的 WebSocket（wss://）服务器。帧格式与
Worker.HandleWebSocket 的帧循环相同，所有字节都流经
每条连接各自的 `TlsStream`。

用法：
WssServer server = WssServer.Create("0.0.0.0", 8443,
"cert.pem", "key.pem");
server.OnMessage(Handle);
await server.Start();

- TcpListener listener;

- TlsContext tls;

- string host;

- int port;

- bool running;

- int activeConnections;

- int maxConnections;

- int requestTimeoutMs;

- WssMessageHandler messageHandler;

- WssServer(string host, int port)
  - 内部构造：无 TLS 上下文；经 Create 使用。

- static WssServer Create(string host, int port, string certFile, string keyFile)
  - 从 PEM 证书链和 PEM 私钥创建
    wss 服务器。证书/私钥无法加载时返回 null。

- WssServer OnMessage(WssMessageHandler handler)
  - 注册每消息处理器；其返回值会发回
    客户端。不注册时回退到 `OnMessage(string)`
    （子类重写路径，与 Worker 的 onMessage 同款）。

- WssServer SetMaxConnections(int max)
  - 设置最大并发连接数；超限连接立即关闭（TLS 连接各持
    SSL/BIO 与缓冲，必须设闸）。

- WssServer SetTimeout(int ms)
  - 设置 TLS 握手及 Upgrade 头的超时（毫秒）。默认 30 秒；
    非正值禁用超时，建立连接后的帧循环不受此限制。

- async void Start()
  - 开始接受连接并逐条派发处理协程；running 置位后阻塞循环，
    直到 Stop() 被调用（从其他协程）。

- void Stop()
  - 请求停止接受连接；在途连接处理完当前消息后自然收尾。

- async void HandleConnection(nint clientSock)
  - 单条连接的生命周期：TLS 握手 → WebSocket Upgrade（缺 Upgrade
    或 Sec-WebSocket-Key 时回 400）→ 帧循环（Ping→Pong、Close 应答
    并断开、协议违规回 1002/1009 关闭帧）；每条消息交
    OnMessageInternal，返回值按原 opcode 发回。任何退出路径都
    关闭流与 socket 并归还连接计数——清理在 finally 中执行：
    处理器抛出的异常会跳过顺序清理，泄漏 fd/SSL 与计数
    （计数漂移会让服务器在 maxConnections 处永久拒绝新连接）。

- async void HandleConnectionInner(nint clientSock)
  - HandleConnection 的服务体；计数归还见 HandleConnection 的 finally。

- async string OnMessageInternal(string message)
  - 每消息分发：注册了委托走委托，否则走可重写的 OnMessage。

- async string OnMessage(string message)
  - 子类重写路径：未注册委托时每条消息走这里，
    返回值发回客户端（默认原样 echo）。


## string (delegate)

处理一条已解码的文本/二进制消息；返回值会发回
客户端（纯 echo 服务器只需原样返回参数）。异步设计使
处理器可以 await I/O 而不会阻塞连接的协程。

`delegate string WssMessageHandler(string message);`
