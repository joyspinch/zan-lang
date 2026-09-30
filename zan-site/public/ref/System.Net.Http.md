# System.Net.Http

> 源码: `stdlib/System/Net/Http/Http2Frame.zan`, `stdlib/System/Net/Http/HttpFramer.Tls.zan`, `stdlib/System/Net/Http/HttpFramer.zan`, `stdlib/System/Net/Http/HttpRequest.zan`, `stdlib/System/Net/Http/HttpResponse.zan`, `stdlib/System/Net/Http/HttpServer.zan`


## Http2Flags (class)

- public const int END_STREAM=1;

- public const int END_HEADERS=4;

- public const int PADDED=8;

- public const int PRIORITY=32;

- public const int ACK=1;


## Http2Frame (class)

- public static const string PREFACE="PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
  - HTTP/2 客户端连接序言（24 字节明文）。

- public int length;

- public int type;

- public int flags;

- public int streamId;

- public byte[]payload;

- public Http2Frame()

- public static byte[]EncodeHeader(int length, int type, int flags, int streamId)
  - 编码 9 字节固定帧头：
    Length (24-bit) + Type (8-bit) + Flags (8-bit) + StreamID (31-bit)。

- public static byte[]BuildSettingsAck()
  - 构造空的 SETTINGS 确认帧（ACK）。


## Http2FrameType (class)

HTTP/2 二进制分帧基础规范（RFC 7540）。
提供连接序言检测、9 字节固定帧头编解码与核心帧类型枚举。

- public const int DATA=0;

- public const int HEADERS=1;

- public const int PRIORITY=2;

- public const int RST_STREAM=3;

- public const int SETTINGS=4;

- public const int PUSH_PROMISE=5;

- public const int PING=6;

- public const int GOAWAY=7;

- public const int WINDOW_UPDATE=8;

- public const int CONTINUATION=9;


## Http2Settings (class)

- public const int HEADER_TABLE_SIZE=1;

- public const int ENABLE_PUSH=2;

- public const int MAX_CONCURRENT_STREAMS=3;

- public const int INITIAL_WINDOW_SIZE=4;

- public const int MAX_FRAME_SIZE=5;

- public const int MAX_HEADER_LIST_SIZE=6;


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

- static int activeCount=0;

- static long nowMs=0;

- static HttpDeadlineToken Arm(nint sock, int timeoutMs)
  - 注册一个连接并返回带 generation 的 token；
    截止时间被禁用（timeoutMs <= 0）时返回 null。

- static HttpDeadlineToken Arm(nint sock, int timeoutMs, int totalMs)
  - 注册阶段 deadline，并可附带从 Arm 时刻起的 total budget。
    totalMs <= 0 表示不设总预算。

- static void Touch(HttpDeadlineToken token, int timeoutMs)
  - 为 token 重启阶段窗口。阶段预算为非正值时仍保留
    total deadline；不能因为关闭某一阶段而把总截止时间一并关闭。

- static bool Expired(HttpDeadlineToken token)
  - 当扫描器已对 token 执行挂断时返回 true。
    必须在 Disarm 前查询。

- static void Disarm(HttpDeadlineToken token)
  - 连接结束时释放 token；重复释放安全，旧 token 不能释放新连接。

- static bool SweepOnce()
  - 单次扫描：刷新粗粒度时钟并对过期连接执行挂断
    （shutdown 而非 close，见类文档）。返回是否仍有活动 token。

- static async void Sweep()
  - 每秒扫描一次的常驻协程；只在有活动 token 时存活。
    SweepOnce 返回 false 后仍需在锁内再确认一次 activeCount，
    避免 Arm 与扫描器退出之间丢失唤醒。


## HttpDeadlineSlot (class)

槽位表中的一条：连接套接字、阶段/总截止时间与 generation。

- nint sock;

- long deadline;

- int generation;

- bool active;

- bool fired;

- long totalDeadline;

- HttpDeadlineSlot(nint sock, long deadline)
  - 新槽位：generation 从 1 起，槽位复用时递增。


## HttpDeadlineToken (class)

Arm 返回的截止时间句柄：记录槽位下标与 generation，供 Disarm 使用。

- int slot;

- int generation;

- bool disarmed;

- HttpDeadlineToken(int slot, int generation)
  - token 记录槽位下标与创建时的 generation，
    用于槽位复用后旧 token 不再误操作新连接。

- ~HttpDeadlineToken()
  - 传输异常可能在到达显式清理点之前就把请求展开掉。
    析构只是兜底；正常路径仍然显式 Disarm。Disarm 幂等，
    两条路径都安全。


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

- FramerRecvFn recv;

- string buf;

- int bufCap;

- ByteBuffer pend;

- int scanned;

- bool closed;

- int wireBytes;

- HttpFramer()
  - 私有构造：用 `Create` 创建；TLS 变体见 HttpFramerTls.Create。

- static HttpFramer Create(nint sock)
  - 为明文套接字创建拆包器（64KB 复用接收缓冲）。

- void Prime(string bytes, int len)
  - 把已经从套接字读出的字节放回流头部。同一端口同时
    服务 HTTP 与 WebSocket 时，要先看过请求头才能判断协议，
    已消费的字节由此交还拆包器。

- void PrimeBytes(byte[]bytes, int offset, int len)
  - 以原生字节数组把已读出的前缀放回流头部。完全二进制安全，避免字符串转换导致的 NUL 截断。

- int Pending()
  - 尚未消费的字节数。

- bool Closed()
  - 对端已关闭且缓冲区里没有剩余字节。

- async int Fill()
  - 接收一次，把收到的字节追加到待处理缓冲区。
    返回收到的字节数，0 表示对端关闭。

- async int FillTo(int maxPending)
  - 分块元数据/正文的受限接收；不能用 Fill 的固定 64KB
    读取无终止符的长度行，也不能让小块的线缆开销无限驻留。

- int HeaderEnd()
  - 头部结束空行之后的下标（即正文首字节的位置），
    空行尚未到达时返回 -1。同时识别 CRLF 与单独 LF 分隔。

- async int FillHead(int maxHeaderBytes)
  - 头部读取专用的受限接收（不能调用无上限的 Fill：一次
    64KB 的 recv 可能把 maxHeaderBytes 直接冲过头，随后才检查已经
    太迟）。只接收仍在 cap 内的字节，并在追加后再次核验，保证恶意
    头不会先进入待处理缓冲。返回收到的字节数；超限返回 -431，
    对端关闭返回 0。

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

- async int ReadResponseBody(HttpResponse response, int headerLen, int maxBodyBytes)
  - 响应体定界使用响应解析器，不能把状态行交给要求
    request-target 与 Host 的 HttpRequest.Parse。ReadChunked 只借用
    req 作为解码结果容器，不重新解析响应头。

- async int SaveBodyToFile(HttpRequest req, string filePath, int maxBytes)
  - 流式将请求体分块直接落盘，避免超大正文（如 GB 级上传）引发 OOM。
    内存仅占用当前分块缓冲区（<=64KB），落盘完成后保留后续流水线请求字节。

- int RequestBytes()
  - 本请求在线路上占用的字节数（头部 + 正文，分块编码则
    是整个分块序列）。`Consume` 用它精确丢弃，
    缓冲区里剩下的字节属于下一条流水线请求。

- void Consume(int count)
  - 丢弃已处理请求的字节，保留其后的流水线字节。

- async int ReadChunkedBody(HttpRequest req, HttpResponse response, int he, int maxBodyBytes)
  - 解码 Transfer-Encoding: chunked 的请求体。
    
    逐块读取 "长度(十六进制)[;扩展]CRLF 数据 CRLF"，直到长度为 0 的
    结束块，其后的尾部头部（trailer）一并跳过。解码结果按字节
    累积，因此块里的 NUL 不会截断正文；累计长度超过
    <paramref name="maxBodyBytes"/> 立即拒绝，避免用无限多个小块
    撑爆内存。畸形的长度行按 400 拒绝——不能猜，猜错就等于把
    剩余字节当成下一条请求（走私）。等待 LF 的字节同样有上限
    （8KB）：一条不终止的"长度行"不计入正文限额，若无上限，
    单条连接就能把待处理缓冲喂到任意大小。

- async int SkipTrailer(int from)
  - 跳过结束块之后的 trailer 头部，返回其后的线路下标；
    对端中途关闭返回 -1，行或累计长度超过 8KB 返回 -2。

- int IndexOfLf(int from)
  - 从 <paramref name="from"/> 起第一个 LF 的下标，没有则 -1。

- static int HexVal(int c)
  - 十六进制字符的数值；非十六进制字符返回 -1。

- void Dispose()
  - 归还接收缓冲区并释放待处理缓冲区。连接关闭时调用；
    之后不得再使用本拆包器。


## HttpFramerTls (class)

HTTPS 拆包装配（A332 肥边④）：从 TLS 流构造 HttpFramer——字节源以
实例方法组注入（FramerRecvFn），核心 HttpFramer 因此不拼 TlsStream
类型名，TLS 全家（连带 Base64/Sha256 与 ssl/crypto 驱动拷贝）只进
真正说 TLS 的程序：HttpClient / HttpsServer / Wss* 拼本类即 opt-in，
纯 HTTP 服务不背。

- static HttpFramer Create(TlsStream stream)
  - 从 TLS 连接拆包：除了字节源不同，定界规则与明文
    HTTP 完全一样（TlsStream.RecvIntoAsync 同样给出精确字节数）。


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

- string userAgent;

- int contentLength;

- bool keepAlive;

- int bodyLength;

- bool chunked;

- int bodyStart;

- int parseStatus;

- HttpRequest()
  - 私有构造：HTTP/1.1 keep-alive 的默认状态，由 Parse 使用。

- static bool IsOws(int c)
  - 是否为可选空白（SP / HTAB，RFC 7230 OWS）。

- static int TrimOwsStart(string raw, int start, int stop)
  - [start, stop) 起点跳过 RFC 7230 OWS（SP / HTAB）。

- static int TrimOwsStop(string raw, int start, int stop)
  - [start, stop) 终点回退 RFC 7230 OWS（SP / HTAB）。

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

- static bool RegionExact(string raw, int pos, int stop, string str)
  - 比对 raw[pos..stop) 与 str 是否逐字节完全匹配（零分配）。

- string GetQueryParam(string name)
  - 按名称获取查询参数值（值已 url 解码，与
    `GetFormField` 一致）。采用零分配切片匹配，未命中项不产生任何堆分配。

- string GetFormField(string name)
  - 从 application/x-www-form-urlencoded 正文中读取字段，
    对值做 url 解码。相当于 GetQueryParam 的 POST 正文版；
    采用零拷贝切片匹配，字段不存在时返回 ""，且不分配任何字符串。

- static string Build(string method, string path, string host, string body)
  - 构建用于发送的 HTTP 请求字符串。

- static string BuildGet(string path, string host)
  - 构建 GET 请求。

- static string BuildPost(string path, string host, string body)
  - 构建 POST 请求。

- string UserAgent()
  - 获取请求的 User-Agent 标头值。

- Dict <string, string> GetQueryParams()
  - 一次性解析所有查询参数为键值对字典（零多余反序列化）。


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

- int framingLength;

- bool framingHasLength;

- bool framingChunked;

- HttpResponse()
  - 私有构造：200 / keep-alive 的默认状态，由各静态工厂使用。

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

- static bool HasCrlf(string s)
  - 字符串是否含 CR/LF（头部注入检查用）。

- static bool HeaderFramingSafe(bool isName, string s)
  - 头部名/值是否不含会破坏报文分帧或被下游误读的字符：
    值禁所有控制字符（HTAB 除外）与 DEL——只禁 NUL/CR/LF 会让
    0x01 这类 CTL 溜进报文，宽容下游可能按不同规则切分；名字另禁
    冒号——否则可夹带出第二个头字段（头注入）。名字按 HTTP token
    字符集判定。
    <paramref name="isName"/> 为 true 时按名字判定。

- static bool IsValidHeader(string name, string value)
  - 头部名与值是否可安全写入：名字非空且无冒号，两者都
    不含分帧字符。请求侧（HttpClient）与响应侧共用同一防线。

- static HttpResponse Redirect(string url)
  - 创建重定向响应。Location 不允许注入折行；非法值
    被保守拒绝，不产生一个可被下游解释的重定向头。

- static HttpResponse NotFound()
  - 创建 404 Not Found 响应。

- static HttpResponse ServerError(string message)
  - 创建 500 Internal Server Error 响应。message 原样进入
    HTML 正文，先做 HTML 转义：消息里常含异常文本、URL 或用户
    输入片段，未转义就是把反射型 XSS 内置在错误页里。

- static string EscapeHtmlText(string text)
  - 最小 HTML 实体转义（& < > " '），供内嵌消息进正文用。

- static HttpResponse WithStatus(int code, string text, string body)
  - 用自定义状态码创建响应。

- void Dispose()
  - 主动释放响应持有的头部存储。

- HttpResponse SetHeader(string name, string headerValue)
  - 设置响应头。定界与连接模式由 BuildHeadersSb 唯一生成；
    拒绝这些保留字段，避免客户端看到相互矛盾的长度或分块边界。

- string GetHeader(string name)
  - Returns one response header by name. Header names are
    case-insensitive as required by HTTP; an absent header returns "".

- HttpResponse SetContentType(string ct)
  - 设置 content type。

- HttpResponse SetKeepAlive(bool keep)
  - 设置连接模式。

- StringBuilder BuildHeadersSb()
  - 构建包含状态行与所有头部的 StringBuilder（含结尾空行 CRLF）。

- string BuildHeaders()
  - 只序列化状态行与头部（含结尾空行）。二进制正文时
    调用方先发这个字符串，再按字节发 <c>bodyBytes</c>。

- string Build()
  - 将响应序列化为原始 HTTP 响应字符串。仅适用于文本正文；
    头部与正文在同一个 StringBuilder 内完成拼装，消除二次大内存分配。
    二进制正文请用 `BuildHeaders` + <c>bodyBytes</c>。

- static bool IsOws(int c)
  - 是否为可选空白（SP / HTAB，RFC 7230 OWS）。

- static int TrimOwsStart(string s, int start, int stop)
  - [start, stop) 起点跳过 OWS，返回首个非 OWS 字节下标。

- static int TrimOwsStop(string s, int start, int stop)
  - [start, stop) 终点回退 OWS，返回尾后第一个非 OWS 下标。

- bool ParseFraming()
  - Parse response framing independently from the strict request
    parser. Conflicting/invalid lengths and unsupported transfer codings
    fail closed before any body is read.

- static bool ValidStatusLine(string line)
  - 状态行严格校验：HTTP/1.0|HTTP/1.1 + 至少一个 OWS +
    三位状态码（1xx-5xx）+ 可选 OWS reason（不含控制字符，HTAB
    除外）。版本与状态码间缺 OWS、状态码带后缀（"HTTP/1.1x 200"）
    或 600+ 一律拒绝——否则状态码可被下游按不同切分方式读出。

- static HttpResponse Parse(string raw)
  - 解析原始 HTTP 响应（客户端用法）。状态行、字段行或
    分帧不合法时返回 statusCode=0 的空响应——合法状态码恒 >= 100，
    调用方以 <100 判定拒收并断开链路；绝不把畸形报文解释成
    "200 空响应"（响应错位的起点）。


## HttpRoute (class)

一条已注册的路由：method + path + 静态响应体，作为一个
整体保存，而非三个索引对齐的并行列表。

- string method;

- string path;

- string response;

- HttpRoute(string method, string path, string response)
  - 私有构造：由 HttpRouter 的注册方法创建。


## HttpRouter (class)

简单的 HTTP 路由器，用于将路径映射到处理器。

- List<HttpRoute> routes;

- HttpRouter()
  - 创建空路由表。

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
  - 创建未启动的服务器；用 OnRequest / Workers 等配置后调用 Start。

- HttpServer SetMaxConnections(int max)
  - 设置最大并发连接数。

- HttpServer SetTimeout(int ms)
  - 设置请求超时时间（毫秒）。

- HttpServer SetMaxRequestBytes(int bytes)
  - 设置单个请求的最大可接受大小（字节，
    请求头 + 请求体）。超过该大小的请求会返回 413，
    并关闭连接，防止客户端用
    无限制的请求耗尽内存。上限钳在 1 GiB：再大时帧解析里的
    `headerEnd + bodyLen` 会 int 回绕，判限失效而放行超额请求；
    下限 1 KiB 避免 0/负数让每个请求都被 413。

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
  - 超过 maxConnections 时对下游回 503（Connection: close）并关闭。

- async void HandleConnection(nint clientSock)
  - 处理单个客户端连接。连接计数必须在 finally 中归还：
    处理器抛出的异常会跳过顺序清理，泄漏的计数会让服务器在
    maxConnections 处永久 503。

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

- static async void ServePrimedBytes(nint clientSock, HttpRequestHandler handler, byte[]pending, int pendingLen, int maxHeaderBytes, int maxRequestBytes, int timeoutMs)
  - 原生字节数组重载：将已读前缀放回流头部。完全二进制安全，杜绝 NUL 截断。

- static async void ServeFramed(nint clientSock, HttpRequestHandler handler, HttpFramer framer, int maxHeaderBytes, int maxRequestBytes, int timeoutMs)
  - 已经用 `HttpFramer` 读过若干字节的连接
    （如 WebSocket 端口先读握手头部）交回 HTTP 循环：拆包器连同
    其中未消费的字节一并交接，无需把字节转成字符串再放回——
    二进制正文在这一步转换里会被 NUL 截断。

- static async int ServeFramedCore(nint clientSock, HttpRequestHandler handler, HttpFramer framer, int maxHeaderBytes, int maxRequestBytes, int timeoutMs, HttpDeadlineToken watch)
  - HTTP 连接循环本体：读头块 → 解析 → 读正文 → 分发
    处理器 → 按字节长度发响应，keep-alive 循环；431/413/400/501
    拒绝响应在此统一发送并关闭连接。

- static async int ServeFramedInner(nint clientSock, HttpRequestHandler handler, HttpFramer framer, int maxHeaderBytes, int maxRequestBytes, int timeoutMs, HttpDeadlineToken watch, TcpClient client)
  - ServeFramedCore 的循环体；释放见 ServeFramedCore 的 finally。

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


## int (delegate)

TLS 分片注入的字节源：从 TLS 记录层精确收 (buf, cap) 内的字节，
返回收到的字节数（0 = 对端关闭），形态跟随 Socket.RecvIntoAsync，
使分片可以逐参转发。核心文件不拼 TlsStream 类型名——拼了会把
TLS 全家（连带 Base64/Sha256 与 ssl/crypto 驱动拷贝）拉进一切
HTTP 程序（A332 肥边④）。

`delegate int FramerRecvFn(string buf, int cap);`
