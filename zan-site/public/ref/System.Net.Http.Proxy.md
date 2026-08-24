# System.Net.Http.Proxy

> 源码: `stdlib/System/Net/Http/Proxy/HttpForwarder.zan`


## FwdChannel (class)

转发链路的一端：明文套接字或 TLS 流。两种传输的收发
接口在此统一，转发逻辑因此只写一遍，而不是每处都判断
http/https。

`RecvAsync` 返回本次真正到达的字节（对端关闭
时为空串），`SendAllAsync` 循环直到整段写出，
因此调用方永远不必自己处理部分写。

- nint sock;

- TlsStream tls;

- TlsContext ctx;

- bool closed;

- FwdChannel(nint sock, TlsStream tls, TlsContext ctx)

- static FwdChannel OfSocket(nint sock)
  - 明文 TCP 链路。

- static FwdChannel OfTls(nint sock, TlsStream tls, TlsContext ctx)
  - TLS 链路；`ctx` 由本链路持有，关闭时释放。

- async string RecvAsync(int max)
  - 本次到达的字节，最多 `max`；对端关闭或出错时为空串。

- async int SendAllAsync(string data)
  - 写出整段 `data`；成功返回写出的字节数，
    对端提前关闭时返回已写出的部分长度。

- void Close()
  - 关闭链路（幂等）。TLS 链路同时释放流与上下文。

- bool IsClosed()

- nint Sock()
  - 底层套接字（连接池用它做零阻塞的存活检查）。


## FwdPool (class)

上游连接池：转发器只有一个上游（host:port + 是否 TLS），因此
池子不需要键，一条空闲链路对下一个请求总是可用的。

意义在于握手：转发 AI 接口时每个请求都要一次 TCP 三次握手加
一次 TLS 握手（跨机房是毫秒级，比转发本身贵一到两个数量级），
而复用一条已握手的链路把这部分降到零。

借出前用 <c>select()</c> 零超时检查一次：空闲链路上"可读"只
意味着对端已关闭或发来了不属于任何请求的字节，两种都不可再用，
直接丢弃。上游在检查与发送之间关闭的竞态由调用方兜底
（无正文的请求换新连接重发一次）。

池表是转发器实例级的，而连接协程可能落在不同 worker 线程上，
因此每处访问都在 <c>lock (idle)</c> 内；临界区里只有列表操作与
关闭套接字，不含 await。

- List<FwdChannel> idle;

- List<long> expiry;

- int maxIdle;

- int idleMs;

- long hits;

- long misses;

- FwdPool(int maxIdle, int idleMs)

- FwdChannel Take()
  - 取一条可用的空闲链路，没有则返回 null（调用方新建）。

- void Give(FwdChannel ch)
  - 归还一条仍然干净的链路（响应已按声明的长度读完）。
    池满时直接关闭，空闲链路数因此有上界。

- void CloseAll()
  - 关闭所有空闲链路（转发器停止时调用）。

- long Hits()
  - 复用成功的次数（省下的握手数）。

- long Misses()
  - 池中无可用链路、必须新建的次数。

- int IdleCount()
  - 当前空闲链路数。


## FwdReader (class)

带前瞻缓冲的链路读取器。转发要按行/按定长切出协议帧
（请求头块、分块长度行），又不能吞掉属于下一帧的字节，
因此多读到的部分留在缓冲里，由 `TakeBuffered`
交给下一段直接转发——这是"边收边转"不丢字节的关键。

- FwdChannel ch;

- string buf;

- bool eof;

- int chunkBytes;

- FwdReader(FwdChannel ch, int chunkBytes)

- async bool FillAsync()
  - 再读一批到缓冲；对端关闭时返回 false。

- async string ReadHeadAsync(int cap)
  - 读到请求/响应头块结束（含空行），最多 `cap` 字节；
    未见结束空行就断开或超过上限时返回空串。多读的正文字节
    留在缓冲中。

- async string ReadLineAsync(int cap)
  - 读到行尾（含 CRLF），最多 `cap` 字节；行不完整就
    断开时返回空串。分块编码的长度行/尾部用它。

- async string ReadSomeAsync(int max)
  - 取出当前已缓冲的字节（可能为空），最多 `max` 个；
    缓冲为空时先读一批。对端关闭且无缓冲时返回空串。

- string TakeBuffered()
  - 取走并清空缓冲（不再读取）。

- bool AtEof()

- bool Drained()
  - 缓冲已空且对端未关闭——即这条链路正好停在一帧的
    边界上，可以安全地留给下一个请求。


## HttpForwarder (class)

流式 HTTP 反向代理/转发器。

与"用 HttpClient 请求上游、拿到完整 HttpResponse 再回给
下游"的写法不同，这里全程只用套接字原语按块搬运：上游
响应头一到就立刻写给下游，之后每收到一块正文就立刻转出，
任何时刻都不缓冲整个响应。因此流式聊天（SSE /
<c>text/event-stream</c>、逐 token 的分块响应）在下游是
实时可见的，CONNECT 与 101 Upgrade（WebSocket）则退化为
双向裸字节隧道。

用法：
HttpForwarder f = new HttpForwarder("127.0.0.1", 8787,
"https://api.openai.com");
f.SetTimeout(600000);          // 长连接的流式响应要给足时间
await f.Start();

逐跳（hop-by-hop）头部按 RFC 7230 6.1 剥离，Host 改写为上游
主机，并追加 X-Forwarded-For。

上游连接默认复用（<c>Connection: keep-alive</c> + `FwdPool`）：
转发 AI 接口时，每个请求重新做一次 TCP+TLS 握手是毫秒级的
开销，比搬字节本身贵得多。只有帧格式能确定结尾的响应
（Content-Length、chunked、或按定义无正文的 204/304/HEAD）才会把
链路归还进池；以连接关闭定界的响应（无长度的流）、101
Upgrade 与 CONNECT 仍照旧方式用完即关。用
`DisableUpstreamKeepAlive` 可退回每请求一连。
下游仍然是每连接一个请求（向客户端声明 <c>Connection: close</c>）。

- string listenHost;

- int listenPort;

- string upHost;

- int upPort;

- bool upTls;

- bool verifyTls;

- bool allowConnect;

- int timeoutMs;

- int maxHeadBytes;

- int chunkBytes;

- int maxConnections;

- int active;

- bool running;

- TcpListener listener;

- FwdPool pool;

- int maxIdleUp;

- int idleUpMs;

- HttpForwarder(string listenHost, int listenPort, string upstream)
  - `upstream` 可写为 <c>https://host[:port]</c>、
    <c>http://host[:port]</c> 或 <c>host:port</c>（无协议时按
    明文 http，缺端口时 https 用 443、http 用 80）。

- void ParseUpstream(string upstream)

- HttpForwarder SetTimeout(int ms)
  - 空闲超时（毫秒）：两个方向都这么久没有字节
    才判定链路失效。流式响应之间的静默间隔可能很长，
    因此默认 5 分钟。

- HttpForwarder SetChunkBytes(int bytes)
  - 单次搬运的块大小（字节）。

- HttpForwarder SetMaxHeadBytes(int bytes)
  - 请求/响应头块的上限（字节）。

- HttpForwarder SetMaxConnections(int max)
  - 最大并发连接数。

- HttpForwarder SetUpstreamKeepAlive(int maxIdle, int idleMs)
  - 上游空闲链路上限与空闲存活时长（毫秒）。
    存活时长应短于上游自己的 keep-alive 超时，否则每次复用
    都要先碰一下已被对端关掉的链路。

- HttpForwarder DisableUpstreamKeepAlive()
  - 关掉上游连接复用，退回“每请求一连 + Connection:
    close”的旧行为（上游对 keep-alive 处理有问题时的逃生口）。

- FwdPool UpstreamPool()
  - 上游连接池（未启用复用时为 null）：命中/未命中
    次数与当前空闲数可直接报给监控。

- HttpForwarder DisableTlsVerify()
  - 不校验上游证书（自签名上游/抓包调试用）。

- HttpForwarder DenyConnect()
  - 禁用 CONNECT 隧道（默认允许，指向配置的上游）。

- bool UpstreamTls()
  - 上游是否为 https。

- void Stop()
  - 停止 accept 循环（已建立的连接自然收尾）。

- async void Start()
  - 启动 accept 循环。每个下游连接在独立协程里转发，因此
    一路慢速流式响应不会挡住其他连接。运行期间不返回。

- static async void RejectBusy(nint clientSock)

- async void Serve(nint clientSock)

- async void ServeOnce(nint clientSock)
  - 转发一个下游连接：读请求头 → 连上游 → 边收边转
    请求体 → 上游响应头即刻回传 → 边收边转响应体。

- async void ServeConnect(FwdChannel client, FwdReader creader)
  - CONNECT：连上游后回 200，然后双向裸字节对拷
    （TLS 直通，转发器不解密）。

- async FwdChannel ConnectUpstream()
  - 建立上游连接（必要时完成 TLS 握手）。

- string RewriteRequestHead(string head, string peer)
  - 改写请求头：Host 指向上游、剥离逐跳头部、
    追加 X-Forwarded-For，并要求上游按请求关闭连接。
    对 Transfer-Encoding: chunked 的请求保留该头部，确保上游
    能正确解析后续 chunk 正文。

- static bool HasRequestBody(string head)
  - 请求头声明了正文（Content-Length > 0 或 chunked）。
    无正文的请求才可以在复用链路失效时原样重发。

- static bool RespHasNoBody(string method, int status)
  - 该响应按定义没有正文：1xx、204、304，以及对
    HEAD 的应答（后者即使带 Content-Length 也不发正文）。

- static bool RespSelfDelimited(string head)
  - 响应的结尾可由帧格式自行确定（chunked 或
    Content-Length），而不是靠连接关闭。

- static bool RespKeepsAlive(string head)
  - 上游愿意保持这条链路：HTTP/1.1 且 Connection 里没有
    close。HTTP/1.0 响应默认关闭，不复用。

- static string RewriteResponseHead(string head)
  - 改写响应头：剥离逐跳头部并标明连接将关闭。
    Transfer-Encoding 保留（分块帧原样透传给下游）。

- static async long PumpBody(FwdReader src, FwdChannel dst, string head, bool isRequest, int chunkBytes)
  - 按 `head` 声明的帧格式把正文从 `src` 搬到 `dst`，每收到
    一块立即写出：
    Transfer-Encoding: chunked —— 逐块转发，见到 0 块与尾部结束；
    Content-Length: n         —— 精确转发 n 字节；
    两者都没有                 —— 请求侧视为无正文，
    响应侧一直转发到上游关闭
    （SSE / 无长度的流式响应）。

- static async long PumpExact(FwdReader src, FwdChannel dst, long n, int chunkBytes)
  - 精确搬运 `n` 字节。

- static async long PumpToEof(FwdReader src, FwdChannel dst, int chunkBytes)
  - 一直搬运到源端关闭（无 Content-Length 的流式响应）。

- static async long PumpChunked(FwdReader src, FwdChannel dst, int chunkBytes)
  - 逐块搬运分块编码：长度行原样透传，随后转发该块
    的数据与 CRLF；0 长度块后转发尾部直到空行结束。逐块转发
    正是流式聊天在下游能立刻显示的原因。
    
    返回搬运的数据字节数；未读到终结块就中断（对端先关、分块头
    非法、下游写失败）时返回 -1：这条链路停在了帧中间，不能再
    留给下一个请求用。

- static async void Tunnel(FwdChannel a, FwdReader ar, FwdChannel b, FwdReader br, int chunkBytes)
  - 双向裸字节隧道（CONNECT、101 Upgrade）。任一方向
    结束就关闭两端，另一方向随即收到 EOF 退出，因此
    WhenAll 不会卡在半关闭的链路上。

- static async void PumpUntilClose(FwdChannel src, FwdChannel dst, int chunkBytes)

- static List<string> HeadLines(string head)
  - 头块按行切分（不含各行 CRLF，也不含结束空行）。

- static string MethodOf(string head)
  - 请求行的方法（大写形式原样返回）。

- static int StatusOf(string head)
  - 状态行的状态码，无法解析时返回 0。

- static string LowerName(string line)
  - 头部行的字段名（小写）；不是头部行时返回空串。

- static string HeaderValueOfLine(string line)
  - 头部行的字段值（两端空白已去除）。

- static string HeaderValue(string head, string name)
  - 头块中 `name`（小写）的值，缺失时为空串。

- static bool IsHopByHop(string name)
  - 逐跳头部（RFC 7230 6.1）：只对单段连接有意义，
    不应转发给上游。

- static bool HeadIsChunked(string head)
  - 头块声明了分块传输编码。
    按 RFC 7230，只有列表最后一项严格等于 "chunked" 时才成立，
    且 "chunked" 之后不能再有其他编码。

- static long ContentLengthOf(string head)
  - Content-Length 的值；未声明时返回 -1，
    包含非数字字符时返回 -2（非法）。

- static bool IsBodyFramingValid(string head, bool isRequest)
  - 检查请求/响应头块的正文定界是否合法。
    非法定界（TE+CL 冲突、非 chunked 的 TE、chunked 不在最后、
    非法 Content-Length）返回 false，调用方应返回 400。

- static List<string> SplitCsv(string s)
  - 按 ',' 拆分 TE/CL 等逗号分隔值，并 trim 每项。

- static long ParseChunkSize(string line)
  - 分块长度行（十六进制，可带 ";ext"）的字节数；
    非法时返回 -1。

- static int ParseInt(string s)

- static string Lower(string s)

- static string Trim(string s)

- static bool StartsWith(string s, string prefix)

- static int IndexOf(string hay, string needle, int from)

- static int LastIndexOf(string hay, string needle)
