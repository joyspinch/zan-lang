# System.Net.Https

> 源码: `stdlib/System/Net/Https/HttpsServer.zan`


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

- int maxRequestBytes;

- int maxHeaderBytes;

- int requestTimeoutMs;

- HttpRequestHandler requestHandler;

- HttpsServer(string host, int port)

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

- void Stop()
  - 停止接受新连接。

- async void HandleConnection(nint clientSock)
  - 对一个客户端连接执行 TLS 握手，随后用与
    `HttpServer.HandleConnection` 相同的帧解析
    服务其上的 HTTP/1.1 请求（支持 keep-alive 与流水线）。

- async HttpResponse ProcessRequest(HttpRequest request)
