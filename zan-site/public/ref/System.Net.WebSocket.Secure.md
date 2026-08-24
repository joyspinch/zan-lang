# System.Net.WebSocket.Secure

> 源码: `stdlib/System/Net/WebSocket/Secure/WssClient.zan`, `stdlib/System/Net/WebSocket/Secure/WssServer.zan`


## WssClient (class)

基于 TLS 的 RFC 6455 WebSocket 客户端，客户端帧带掩码。

- TcpClient conn;

- TlsContext context;

- TlsStream stream;

- string host;

- string path;

- string pending;

- bool connected;

- WssClient()

- static async WssClient ConnectAsync(string host, int port, string path)

- bool IsConnected()

- async void SendText(string text)

- async void SendBinary(string bytes, int len)

- async void Ping()

- async string RecvText()

- async void Close()

- async void SendFrame(int opcode, string payload, int payloadLen)

- int FrameLength()

- void CloseNow()

- static string Header(string headers, string name)

- static bool AsciiEquals(string a, string b)

- static int IndexOf(string hay, string needle, int from)


## WssServer (class)

基于 TLS 的 WebSocket（wss://）服务器。帧格式与
`WebSocketServer` 相同，所有字节都流经
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

- WssMessageHandler messageHandler;

- WssServer(string host, int port)

- static WssServer Create(string host, int port, string certFile, string keyFile)
  - 从 PEM 证书链和 PEM 私钥创建
    wss 服务器。证书/私钥无法加载时返回 null。

- WssServer OnMessage(WssMessageHandler handler)
  - 注册每消息处理器；其返回值会发回
    客户端。

- async void Start()

- void Stop()

- async void HandleConnection(nint clientSock)

- async string OnMessageInternal(string message)


## string (delegate)

处理一条已解码的文本/二进制消息；返回值会发回
客户端（纯 echo 服务器只需原样返回参数）。异步设计使
处理器可以 await I/O 而不会阻塞连接的协程。

`delegate string WssMessageHandler(string message);`
