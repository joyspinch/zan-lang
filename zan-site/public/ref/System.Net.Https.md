# System.Net.Https

> 源码: `packages/Zan.Net/src/System/Net/Https/HttpsServer.zan`


## HttpsServer (class)

基于协程的 HTTPS（HTTP/1.1 over TLS）服务器。编程模型与
`HttpServer` 相同，每个连接通过
`TlsStream`（OpenSSL）进行 TLS 加密。

用法：
HttpsServer server = HttpsServer.Create("0.0.0.0", 8443,
"cert.pem", "key.pem");
server.OnRequest(HandleRequest);
await server.Start();

- TcpListener listener;

- TlsContext tls;

- string host;

- int port;

- bool running;

- int activeConnections;

- int maxConnections;

- int maxRequestBytes;

- int maxHeaderBytes;

- int requestTimeoutMs;

- HttpRequestHandler requestHandler;

- HttpsServer(string host, int port)
  - 私有构造：默认限额与 30 秒超时；用 `Create` 创建。

- static HttpsServer Create(string host, int port, string certFile, string keyFile)
  - 从 PEM 证书链与 PEM 私钥创建 HTTPS 服务器；
    证书/私钥无法加载时返回 null。

- HttpsServer SetMaxRequestBytes(int bytes)
  - 设置单个请求的最大可接受大小（字节），
    （请求头 + 请求体）。

- HttpsServer SetMaxHeaderBytes(int bytes)
  - 设置请求头块的最大字节数，超过则返回 431。

- HttpsServer SetTimeout(int ms)
  - 设置读取请求头和请求体的截止时间（毫秒）。
    0 表示禁用。默认 30 秒。

- HttpsServer OnRequest(HttpRequestHandler handler)
  - 注册每个传入请求都会调用的处理器。

- async void Start()
  - 启动 HTTPS 服务器事件循环。持续运行，在协程上
    接受并处理连接。

- HttpsServer SetMaxConnections(int max)
  - 设置最大并发连接数；超过上限的连接立即关闭（回 503 之前
    不做 TLS 握手——每个 TLS 连接都持有 SSL/BIO 与缓冲，必须像
    HttpServer 一样设闸）。

- void Stop()
  - 停止接受新连接：关闭监听套接字让挂起在
    AcceptAsync 上的协程立即返回（只置 running 标志要等到
    下一条连接到达才会生效）。已建立的连接自然收尾。

- async void HandleConnection(nint clientSock)
  - 对一个客户端连接执行 TLS 握手，随后用与
    `HttpServer.HandleConnection` 相同的帧解析
    服务其上的 HTTP/1.1 请求（支持 keep-alive 与流水线）。

- async void HandleConnectionInner(nint clientSock)
  - HandleConnection 的服务体；计数归还见 HandleConnection 的 finally。

- async HttpResponse ProcessRequest(HttpRequest request)
  - 分发到 OnRequest 注册的处理器；未注册时返回占位页。
