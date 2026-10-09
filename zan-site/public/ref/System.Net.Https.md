# System.Net.Https

> 源码: `packages/Zan.Net/src/System/Net/Https/HttpsServer.zan`


## HttpsServer (class)

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

- static HttpsServer Create(string host, int port, string certFile, string keyFile)

- HttpsServer SetMaxRequestBytes(int bytes)

- HttpsServer SetMaxHeaderBytes(int bytes)

- HttpsServer SetTimeout(int ms)

- HttpsServer OnRequest(HttpRequestHandler handler)

- async void Start()

- HttpsServer SetMaxConnections(int max)

- void Stop()

- async void HandleConnection(nint clientSock)

- async void HandleConnectionInner(nint clientSock)

- async HttpResponse ProcessRequest(HttpRequest request)
