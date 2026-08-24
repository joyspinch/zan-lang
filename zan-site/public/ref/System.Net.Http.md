# System.Net.Http

> 源码: `stdlib/System/Net/Http/HttpFramer.zan`, `stdlib/System/Net/Http/HttpRequest.zan`, `stdlib/System/Net/Http/HttpResponse.zan`, `stdlib/System/Net/Http/HttpServer.zan`


## HttpDeadline (class)

按连接管理的请求截止时间，由整个进程中的一个协程统一
扫描。

无法直接在 receive 上设置超时：连接协程
停靠在 IO reactor 中，没有其他机制驱动它。因此每个连接
注册一个截止时间槽位，单个扫描器每秒唤醒一次，对超时者
执行挂断。这使每个请求只需 O(1) 开销（一次整数写入），
整个进程一个协程，而非每个连接一个定时器协程。

扫描器对套接字执行 shutdown 而非 close，因此等待中的
协程会因读取到 0 字节被唤醒，沿正常断连路径
展开（释放槽位）；若在此处直接关闭描述符，会卡住
该协程并泄漏槽位。

槽位表是进程级的，而连接协程可能跑在不同 worker 线程上
（<c>--async-workers</c>），因此每一处表访问都在
<c>lock (slots)</c> 里：否则 `Arm` 的 <c>slots.Add</c>
会与另一个线程的 <c>slots[slot]</c> 同时发生，或两个连接
从自由列表拿到同一个槽位。临界区里只有列表操作和
shutdown 系统调用，不包含 await。

- static List<HttpDeadlineSlot> slots=new List<HttpDeadlineSlot>();

- static List<int> freeSlots=new List<int>();

- static bool sweeping=false;

- static long nowMs=0;

- static int Arm(nint sock, int timeoutMs)
  - 注册一个连接并返回其槽位；
    截止时间被禁用（timeoutMs <= 0）时返回 -1。

- static void Touch(int slot, int timeoutMs)
  - 为某个槽位重启时间窗口（每个请求调用一次）。

- static bool Expired(int slot)
  - 当扫描器已对该槽位执行挂断时返回 true，即唤醒
    归属者读取的是截止时间而非对端。需要报告
    超时的调用方（HTTP 客户端）必须在 Disarm 之前查询。

- static void Disarm(int slot)
  - 连接结束时释放槽位。

- static bool SweepOnce()

- static void StopSweeping()

- static async void Sweep()


## HttpDeadlineSlot (class)

- nint sock;

- int deadline;

- bool fired;

- HttpDeadlineSlot(nint sock, int deadline)


## HttpFramer (class)

一条 HTTP/1.1 连接上的请求拆包器：把字节流切成请求，
全程按显式字节数处理。

stdlib 里的每个 HTTP 连接循环（HttpServer/Worker 的 "http" 协议、
System.Web 的 WebApp、HttpsServer 的 TLS 连接）都用它拆包，因此
定界的修复对所有入口同时生效，不会再出现多套实现各自漂移。

为什么不能用字符串拼接来攒请求：Zan 字符串以 NUL 结尾，
<c>s.Length</c> 是 strlen，<c>a + b</c> 也按 strlen 定长。请求体
（文件上传、protobuf、gzip 内容）里的 NUL 会让长度提前截断，于是
声明的 Content-Length 永远读不满，请求超时或被判 400。这里把未
消费的字节放在带显式长度的 `ByteBuffer` 里，接收用
`Socket.RecvIntoAsync` 的精确返回值，切片用带长度的
`ByteBuffer.Str`，全程不依赖 NUL。

用法（一次请求）：
HttpFramer f = HttpFramer.Create(sock);
int he = await f.ReadHead(maxHeaderBytes);        // 头部块到齐
HttpRequest req = HttpRequest.Parse(f.Head(he), he);
int st = await f.ReadBody(req, maxBodyBytes);     // 正文到齐
f.Consume(f.RequestBytes());                     // 只丢弃本请求
剩余字节留在拆包器里，就是下一条流水线请求的开头。

- nint sock;

- TlsStream tls;

- string buf;

- int bufCap;

- ByteBuffer pend;

- int scanned;

- bool closed;

- int wireBytes;

- HttpFramer()

- static HttpFramer Create(nint sock)

- static HttpFramer CreateTls(TlsStream stream)
  - 从 TLS 连接拆包：除了字节源不同，定界规则与明文
    HTTP 完全一样（TlsStream.RecvIntoAsync 同样给出精确字节数）。

- void Prime(string bytes, int len)
  - 把已经从套接字读出的字节放回流头部。同一端口同时
    服务 HTTP 与 WebSocket 时，要先看过请求头才能判断协议，
    已消费的字节由此交还拆包器。

- int Pending()
  - 尚未消费的字节数。

- bool Closed()
  - 对端已关闭且缓冲区里没有剩余字节。

- async int Fill()
  - 接收一次，把收到的字节追加到待处理缓冲区。
    返回收到的字节数，0 表示对端关闭。

- int HeaderEnd()
  - 头部结束空行之后的下标（即正文首字节的位置），
    空行尚未到达时返回 -1。同时识别 CRLF 与单独 LF 分隔。

- async int ReadHead(int maxHeaderBytes)
  - 一直接收，直到完整的头部块到齐。
    返回头部块长度（含结束空行）；对端在请求中途关闭返回 0；
    头部块超过 <paramref name="maxHeaderBytes"/> 返回 -431——
    违规字节在被无限缓冲之前就被拒绝（slowloris 的一种形态是
    只发头部却永不发空行）。

- string Head(int headerLen)
  - 头部块（前 <paramref name="headerLen"/> 个字节）的字符串
    切片，交给 `HttpRequest.Parse`。

- string Slice(int off, int count)
  - 待处理字节中 [off, off+count) 的切片（按长度复制，
    内嵌 NUL 得以保留）。流式上传用它取出"与头部同包到达"的
    那段正文，其余部分直接从套接字续读。

- async int ReadBody(HttpRequest req, int maxBodyBytes)
  - 一直接收，直到 <paramref name="req"/> 的正文到齐，并按
    精确字节数写入 <c>req.body</c> / <c>req.bodyLength</c>。
    
    返回 0 表示成功；返回 HTTP 状态码表示应当据此拒绝
    （413 超限、400 分块编码畸形）；返回 -1 表示对端在正文
    中途关闭。分块编码在这里解码，解码后的正文与固定长度
    正文一样，对处理器不可区分。

- int RequestBytes()
  - 本请求在线路上占用的字节数（头部 + 正文，分块编码则
    是整个分块序列）。`Consume` 用它精确丢弃，
    缓冲区里剩下的字节属于下一条流水线请求。

- void Consume(int count)
  - 丢弃已处理请求的字节，保留其后的流水线字节。

- async int ReadChunked(HttpRequest req, int he, int maxBodyBytes)
  - 解码 Transfer-Encoding: chunked 的请求体。
    
    逐块读取 "长度(十六进制)[;扩展]CRLF 数据 CRLF"，直到长度为 0 的
    结束块，其后的尾部头部（trailer）一并跳过。解码结果按字节
    累积，因此块里的 NUL 不会截断正文；累计长度超过
    <paramref name="maxBodyBytes"/> 立即拒绝，避免用无限多个小块
    撑爆内存。畸形的长度行按 400 拒绝——不能猜，猜错就等于把
    剩余字节当成下一条请求（走私）。

- async int SkipTrailer(int from)
  - 跳过结束块之后的 trailer 头部，返回其后的线路下标；
    对端中途关闭返回 -1。

- int IndexOfLf(int from)
  - 从 <paramref name="from"/> 起第一个 LF 的下标，没有则 -1。

- static int HexVal(int c)

- void Dispose()
  - 归还接收缓冲区并释放待处理缓冲区。连接关闭时调用；
    之后不得再使用本拆包器。


## HttpRequest (class)

HTTP 请求的表示与解析器。
从 TCP 数据解析原始 HTTP/1.1 请求。

- string method;

- string path;

- string version;

- string body;

- string rawHeaders;

- string queryString;

- string host;

- string contentType;

- string authorization;

- string cookie;

- int contentLength;

- bool keepAlive;

- int bodyLength;

- bool chunked;

- int bodyStart;

- int parseStatus;

- HttpRequest()

- static HttpRequest Parse(string raw)
  - 把原始 HTTP 请求字符串解析为 HttpRequest。
    
    按字节下标扫描（<c>raw[i] & 255</c>），而不是
    <c>raw.Substring(i, 1)</c>。后者每检查一个字节都会在堆上分配一个
    单字符字符串，因此旧解析器每个请求要做 O(请求大小) 次分配；
    本实现原地遍历缓冲区，只分配
    实际提取的那几个字段。

- static HttpRequest Parse(string raw, int rawLen)
  - 把 <paramref name="raw"/> 开头的 <paramref name="rawLen"/>
    个字节解析为请求。字节数由调用方给出：连接循环从套接字
    得到的是精确计数，而 <c>raw.Length</c> 会在正文的第一个 NUL
    处停下，据此切分正文会把二进制上传截断。

- static bool RegionIs(string raw, int pos, int stop, string name)
  - 当 raw[pos..stop) 与 name 相等（ASCII 大小写不敏感）时为 true
    （name 必须为小写）。无内存分配的头部匹配。

- static bool NameIs(string name, string lower)
  - 当 name 与 lower（须为小写字面量）相等（ASCII 大小写不敏感）
    时为 true。不分配：用于按名查找头部前的常见头部判定。

- static bool RegionIsCi(string raw, int pos, int stop, string name)
  - 与 `RegionIs` 相同，但 name 的大小写也不敏感——
    调用方传进来的名字不保证是小写，而把它 ToLower 一份只为比较，
    等于每次查找都在堆上分配一个副本。

- string GetHeader(string name)
  - 按名称获取某个头部值（大小写不敏感）。

- string GetQueryParam(string name)
  - 按名称获取查询参数值（值已 url 解码，与
    `GetFormField` 一致）。

- string GetFormField(string name)
  - 从 application/x-www-form-urlencoded 正文中读取字段，
    对值做 url 解码。相当于 GetQueryParam 的 POST 正文版；
    字段不存在时返回 ""。

- static string Build(string method, string path, string host, string body)
  - 构建用于发送的 HTTP 请求字符串。

- static string BuildGet(string path, string host)
  - 构建 GET 请求。

- static string BuildPost(string path, string host, string body)
  - 构建 POST 请求。


## HttpResponse (class)

HTTP 响应的构建器与解析器。

- int statusCode;

- string statusText;

- string body;

- byte[]bodyBytes;
  - 二进制正文（与 body 二选一）。Zan 字符串以 NUL 结尾，
    无法携带任意字节，因此图片/可执行文件等正文走 byte[]。

- int bodyBytesLen;

- string contentType;

- List<string> headers;

- bool keepAlive;

- HttpResponse()

- static HttpResponse Ok(string body)
  - 用给定正文创建 200 OK 响应。

- static HttpResponse Json(string jsonBody)
  - 创建 JSON 响应。

- static HttpResponse Text(string text)
  - 创建纯文本响应。

- static HttpResponse Bytes(byte[]data, int len, string ct)
  - 用二进制正文创建响应。正文按 <paramref name="len"/> 字节
    原样发送，内嵌 NUL 不会截断报文。

- int BodyLength()
  - 正文字节数（文本或二进制）。

- bool IsBinary()
  - 正文是否为二进制缓冲区。

- static HttpResponse Redirect(string url)
  - 创建重定向响应。

- static HttpResponse NotFound()
  - 创建 404 Not Found 响应。

- static HttpResponse ServerError(string message)
  - 创建 500 Internal Server Error 响应。

- static HttpResponse WithStatus(int code, string text, string body)
  - 用自定义状态码创建响应。

- void Dispose()
  - 主动释放响应持有的头部存储。

- HttpResponse SetHeader(string name, string headerValue)
  - 设置响应头。

- HttpResponse SetContentType(string ct)
  - 设置 content type。

- HttpResponse SetKeepAlive(bool keep)
  - 设置连接模式。

- string BuildHeaders()
  - 只序列化状态行与头部（含结尾空行）。二进制正文时
    调用方先发这个字符串，再按字节发 <c>bodyBytes</c>。

- string Build()
  - 将响应序列化为原始 HTTP 响应字符串。仅适用于文本正文；
    二进制正文请用 `BuildHeaders` + <c>bodyBytes</c>。

- static HttpResponse Parse(string raw)
  - 解析原始 HTTP 响应（客户端用法）。


## HttpRoute (class)

一条已注册的路由：method + path + 静态响应体，作为一个
整体保存，而非三个索引对齐的并行列表。

- string method;

- string path;

- string response;

- HttpRoute(string method, string path, string response)


## HttpRouter (class)

简单的 HTTP 路由器，用于将路径映射到处理器。

- List<HttpRoute> routes;

- HttpRouter()

- HttpRouter Get(string path, string responseBody)
  - 注册一条 GET 路由。

- HttpRouter Post(string path, string responseBody)
  - 注册一条 POST 路由。

- HttpRouter Route(string method, string path, string responseBody)
  - 注册适用于任意方法的路由。

- HttpResponse Match(HttpRequest request)
  - 将请求匹配到已注册的路由。


## HttpServer (class)

基于协程的 HTTP/1.1 服务器：System.Net.Worker 之上的 http 门面。
每个客户端连接由各自的协程服务，Workers(n) 在 n 个进程中
运行同一服务器（Linux/macOS 用 SO_REUSEPORT，Windows 用 master accept
加 WSADuplicateSocket 移交），与 workerman 的
$worker->count 一致。

用法：
class MyApp {
static void Main() {
HttpServer server = new HttpServer("0.0.0.0", 8080);
server.Workers(4);
server.OnRequest(MyApp.HandleRequest);
await server.Start();
}
static async HttpResponse HandleRequest(HttpRequest req) {
if (req.path == "/") {
return HttpResponse.Ok("Hello Zan!");
}
return HttpResponse.NotFound();
}
}

连接循环（ServeConnection）是
HTTP/1.1 帧解析与限制在 stdlib 中的唯一实现；Worker 的 "http" 协议
运行同一份代码，两者不会产生偏差。

- TcpListener listener;

- string host;

- int port;

- bool running;

- int maxConnections;

- int activeConnections;

- int requestTimeout;

- int maxRequestBytes;

- int maxHeaderBytes;

- int workerCount;

- HttpRequestHandler requestHandler;

- HttpServer(string host, int port)

- HttpServer SetMaxConnections(int max)
  - 设置最大并发连接数。

- HttpServer SetTimeout(int ms)
  - 设置请求超时时间（毫秒）。

- HttpServer SetMaxRequestBytes(int bytes)
  - 设置单个请求的最大可接受大小（字节，
    请求头 + 请求体）。超过该大小的请求会返回 413，
    并关闭连接，防止客户端用
    无限制的请求耗尽内存。

- HttpServer SetMaxHeaderBytes(int bytes)
  - 设置请求头块的最大大小（字节）。
    客户端若持续发送请求头却不带结束空行，
    会在缓冲区增长到整请求上限前
    收到 431 并被断开连接。

- HttpServer Workers(int count)
  - 服务该端口的进程数（对应 workerman 的
    $worker->count）。1（默认）表示在本进程内服务。

- static int FindHeaderEnd(string buf)
  - 返回请求头块结束位置之后的索引（即
    <paramref name="buf"/> 中第一个请求的请求体首字节），
    若请求头的结束空行尚未到达则返回 -1。
    同时识别 CRLF（\r\n\r\n）与单独 LF（\n\n）分隔符。

- HttpServer OnRequest(HttpRequestHandler handler)
  - 注册每个传入请求都会调用的处理器。
    设置后取代默认的占位响应。

- async void Start()
  - 启动服务器。当 Workers(n>1) 时，将监听器交给
    System.Net.Worker，由其创建并监管 n 个进程；否则
    在本进程运行 accept 循环。服务器运行期间不会返回；
    每次 accept 以及每个连接上的读取都在 IO reactor 上挂起，
    而非忙等。

- static async void RejectOverloaded(nint clientSock)

- async void HandleConnection(nint clientSock)
  - 处理单个客户端连接。

- static async void ServeConnection(nint clientSock, HttpRequestHandler handler, int maxHeaderBytes, int maxRequestBytes, int timeoutMs)
  - 将一个 HTTP/1.1 连接服务到底：帧解析（分段
    读取、按 Content-Length 精确读取请求体、流水线）、keep-alive，以及
    防止恶意客户端耗尽进程的各项限制，并分发到
    <paramref name="handler"/>。
    
    这是 stdlib 中唯一的 HTTP 连接循环：HttpServer 与 Worker 的
    "http" 协议都运行它，因此帧解析的修复与加固
    对两者同样生效。以下限制都在违规字节被缓冲前强制生效：
    431——请求头块超过 <paramref name="maxHeaderBytes"/>；
    413——请求超过 <paramref name="maxRequestBytes"/>，
    依据 Content-Length 判定，而非在缓冲完请求体之后；
    挂断——请求未在 <paramref name="timeoutMs"/> 内完成
    （slowloris 防护：截止时间按请求单独设定，
    不会因零散到达的字节而刷新）。

- static async void ServePrimed(nint clientSock, HttpRequestHandler handler, string pending, int maxHeaderBytes, int maxRequestBytes, int timeoutMs)
  - 用于首字节已被读取的套接字的 ServeConnection：
    将 `pending` 放回流头部。同时服务 HTTP 与 WebSocket 的端口
    需要先查看请求头部才能确定协议类型，
    并把已消费的字节交给本函数。

- static async void ServeFramed(nint clientSock, HttpRequestHandler handler, HttpFramer framer, int maxHeaderBytes, int maxRequestBytes, int timeoutMs)
  - 已经用 `HttpFramer` 读过若干字节的连接
    （如 WebSocket 端口先读握手头部）交回 HTTP 循环：拆包器连同
    其中未消费的字节一并交接，无需把字节转成字符串再放回——
    二进制正文在这一步转换里会被 NUL 截断。

- void Stop()
  - 停止服务器。

- int GetActiveConnections()
  - 返回当前活动连接数。

- bool IsRunning()
  - 返回服务器是否正在运行。


## HttpResponse (delegate)

处理解析后的请求并生成要发送的响应。
异步声明，使处理器可以等待 I/O（Db/Redis 等）而不阻塞
连接的协程或 worker 事件循环。

`delegate HttpResponse HttpRequestHandler(HttpRequest request);`
