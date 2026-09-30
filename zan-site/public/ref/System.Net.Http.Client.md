# System.Net.Http.Client

> 源码: `stdlib/System/Net/Http/Client/CookieJar.zan`, `stdlib/System/Net/Http/Client/HttpClient.zan`, `stdlib/System/Net/Http/Client/SseSink.zan`


## Cookie (class)

一条已存储的 cookie。缺少 PSL 时显式 Domain 也只回给响应主机；
<c>hostOnly</c> 控制回放时是否允许匹配子域。
<c>expires</c> 为 0 表示会话 cookie（进程内一直有效）。

- string name;

- string cookieValue;

- string domain;

- string path;

- bool hostOnly;

- bool secure;

- long expires;

- Cookie(string name, string cookieValue)
  - 内部构造：域/路径等属性由 SetCookie 按 Set-Cookie 参数补齐。

- string Name()
  - cookie 名。

- string Value()
  - cookie 值。

- string Domain()
  - 生效域（小写）。

- string Path()
  - 生效路径。

- bool Secure()
  - 是否仅 HTTPS 通道外发。

- long Expires()
  - 过期时刻（Unix 秒），0 表示会话 cookie。


## CookieJar (class)

客户端 cookie 存储：吸收响应里的 Set-Cookie，并在后续请求上按
域名/路径/Secure 规则回放（RFC 6265 的匹配与排序）。

之前 HttpClient 对 cookie 一无所知——Set-Cookie 只是留在响应头
列表里，调用方要自己 <c>SetHeader("Cookie", ...)</c> 才能接着发
已登录的请求。<c>HttpClient.UseCookies()</c> 把这件事交给本类。

- List<Cookie> items;

- CookieJar()
  - 内部构造：空存储；经 HttpClient.UseCookies() 创建。

- int Count()
  - 已存储的 cookie 条数（过期的不计）。

- string Get(string name)
  - 按名字取值，没有则返回空串。

- bool Remove(string name)
  - 按名字删除，删掉了返回 true。

- void Clear()
  - 清空存储。

- void SetCookie(string host, string requestPath, string setCookieValue)
  - 吸收一条 Set-Cookie 头的值（不含头名字），<paramref name="host"/> 和
    <paramref name="requestPath"/> 是发出该请求的主机与路径，用于补齐
    缺失的 Domain / Path。Max-Age=0 或已过去的 Expires 表示删除，
    同名同域同路径的旧值被覆盖。

- string HeaderValue(string host, string requestPath, bool secureChannel)
  - 该请求应带的 Cookie 头值（"a=1; b=2"），没有匹配的 cookie 则为空串。
    <paramref name="secureChannel"/> 为 false 时标了 Secure 的 cookie 不外发。
    顺序按 RFC 6265：路径长的在前，同长度按写入顺序。

- void AbsorbHead(string host, string requestPath, string head)
  - 从一段响应头里吸收所有 Set-Cookie 行。<paramref name="head"/> 是
    状态行 + 头部（到空行为止即可，多给了也无妨）。

- void DropExpired()
  - 丢掉已过期的条目（会话 cookie 的 expires 为 0，永不丢）。

- static bool DomainMatches(string host, string domain, bool hostOnly)
  - 请求主机 `host` 是否落在 cookie 的域内：host-only 要求完全相等，
    域 cookie 则允许子域（"api.example.com" 命中 "example.com"）。

- static bool DomainAttributeOk(string host, string domain)
  - 没有 PSL 就无法分辨任意公共后缀与可注册域。Domain 属性
    只接受响应主机的完全相同拼写，且按 host-only 存储；这是
    保守的安全子集，不提供 RFC 6265 的跨子域 Domain 语义。

- static bool IsIpHost(string host)
  - host 是否是 IP 字面量：IPv4 四段全数字，或含 ':' 的 IPv6。

- static bool PathMatches(string path, string cookiePath)
  - RFC 6265 的 path-match：相等、cookie 路径以 "/" 结尾的前缀，
    或前缀之后紧跟一个 "/"。

- static string DefaultPath(string requestPath)
  - RFC 6265 的 default-path：请求路径去掉最后一段（"/a/b" -> "/a"）。

- static string PathOnly(string requestPath)
  - 去掉查询串和片段，只留路径。

- static long ParseHttpDate(string s)
  - "Sun, 06 Nov 1994 08:49:37 GMT" -> Unix 秒（解析不出来返回 0）。
    三种 HTTP 日期格式的日/月/年/时分秒都是按记号取的，因此
    RFC 1123、RFC 850 和 asctime 都能过。

- static List<string> Tokens(string s)
  - 按空格、逗号和 '-' 切成记号（"06-Nov-1994" -> 06 / Nov / 1994）。

- static List<string> SplitColon(string s)
  - 按冒号切分（"08:49:37" -> 08 / 49 / 37）。

- static int MonthOf(string t)
  - 月份英文缩写（前三个字母，大小写不敏感）→ 1..12；不匹配返回 0。

- static bool AllDigits(string s)
  - 是否为非空纯数字串。

- static long ParseLong(string s)
  - 解析十进制整数（可带前导负号），非数字处停止；无数字为 0。

- static int IndexOfChar(string s, int ch, int from)
  - 从 from 起查找字符 ch 的下标；没有返回 -1。

- static string Trim(string s)
  - 去掉两侧空白（空格/制表符/CR/LF）。

- static string Lower(string s)
  - ASCII 大写转小写。

- static bool NameEquals(string head, int at, string lower)
  - head[at..] 的头名字（到冒号前）是否等于 lower（已小写）。


## HttpClient (class)

HTTP 客户端，支持 GET、POST、PUT、DELETE，走纯 TCP 或 TLS
（通过 CreateHttps / UseTls 使用 https）。

<c>SetTimeout</c> 是强制性的，而非建议性的：连接阶段受
带截止时间轮询的非阻塞连接限制，发送/接收阶段受共享的
`HttpDeadline` 清扫器限制，它对迟到的套接字执行挂起，使
挂起的协程被唤醒并报告超时。清扫器每秒运行一次，
因此有效粒度约 1 秒（30 秒超时会在 30 到 31 秒之间触发）
；<c>SetTimeout(0)</c> 完全禁用截止时间。

状态行可通过 `SendAsync` 获取，它返回
解析后的 `HttpResponse`；各动词辅助方法仍只返回
响应体。客户端不缓存上次响应的任何内容，因此单个
客户端可被多个协程同时使用。

连接复用：同一客户端串行的请求默认复用一条 keep-alive 连接
（HTTP/1.1 持久连接），省去每个请求的三次握手；空闲连接被对端
回收时换新连接重试一次，对调用方不可见。同一时刻只允许一个
请求占用复用连接，并发请求各自走一次性 Connection: close 通道，
互不干扰。对端宣布 close、EOF 定界的响应、以及 `Close`
都会终结复用。`DisableKeepAlive` 可整体退回旧行为。

错误模型（三类 API，三种约定，调用前先认清用的是哪一类）：
1. 请求/动词方法（`SendAsync` 及 Get/Post 等动词）：
传输层失败一律抛 `HttpRequestException`——连接、TLS
握手、超时、响应中途断开都在其中。HTTP 状态本身不是错误：
到达的响应（含 4xx/5xx）正常返回，状态码由调用方检查。
2. 文件下载方法（`DownloadRangeToFileAsync` /
`DownloadBinaryToFileAsync`）：不抛错，以负数哨兵
返回——-1 = 连接/握手失败或响应不完整（文本见
`GetLastError`），-2 = 取消探针触发（已收字节保留，
可续传）；成功返回落盘字节数。
3. 探测路径（`SseSink` 系）：绝不抛错，每次尝试以
"ok" 或 "err: ..." 标记结束，便于轮询方统一判别。

- string host;

- int port;

- int timeout;

- bool useTls;

- bool verifyTls;

- string clientCertificateFile;

- string clientPrivateKeyFile;

- List<string> defaultHeaders;

- List<string> tlsPins;

- bool pinningEnabled;

- CertPolicyFn certPolicy;

- CookieJar jar;

- HttpCancelFn cancelProbe;

- HttpUploadProgressFn uploadProbe;

- ExternalCallPolicy callPolicy;

- string lastDownloadError;

- long totalDeadlineUs;

- HttpFramer ka;

- TcpClient kaConn;

- TlsStream kaTls;

- TlsContext kaCtx;

- bool kaBusy;

- bool kaEnabled;

- [DllImport("crt")]static extern nint fopen(string path, string mode);

- [DllImport("crt")]static extern int fputs(string str, nint fp);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt")]static extern long fwrite(string buf, long size, long count, nint fp);

- [DllImport("crt")]static extern int fseek(nint fp, int offset, int origin);

- [DllImport("crt")]static extern int ftell(nint fp);

- HttpClient(string host, int port)
  - 为指定的主机和端口创建 HTTP 客户端。

- static HttpClient CreateHttps(string host, int port)
  - 为指定的主机和端口创建 HTTPS 客户端。

- HttpClient UseTls()
  - 为此客户端启用 TLS（https）。

- HttpClient DisableTlsVerify()
  - 禁用服务器证书校验（仅用于开发/
    自签名服务器）。

- HttpClient SetCertPolicy(CertPolicyFn fn)
  - 设置证书策略：链/主机名校验不通过（verify_result
    != 0）时以 (verifyResult, 对端证书文本) 调用，返回 true 放行
    本次连接，false 拒绝（拒绝原因带证书文本）。校验本身不关——
    与 DisableTlsVerify 的整体放开不同，每个连接都过策略，
    典型用途是老系统缺根证书时按已知签发者（GlobalSign/
    Certum 签发的 Alibaba 证书）放行。

- TlsContext ClientTlsContext()
  - 客户端 TLS 上下文（客户端证书/策略统一在此挂上）。

- HttpClient UseCookies()
  - 为此客户端启用 cookie：响应里的 Set-Cookie 被存下来，
    后续请求按域名/路径/Secure 规则自动带上 Cookie 头。调用方自己用
    `SetHeader` 写的 Cookie 头优先，存储不会覆盖它。

- CookieJar Cookies()
  - 本客户端的 cookie 存储，未启用时为 null。

- HttpClient SetCancelProbe(HttpCancelFn f)
  - 装上取消探针：长下载在每收到一块正文后问它一次，
    返回 true 就在块边界收工。传 null 取消装配。
    
    探针在驱动请求的那个线程上被调用（下载跑在工作线程时就是工作
    线程），因此它只应读一个共享标志，不要在里面做别的事。已落盘的
    字节会保留，下一次调用同一个 `DownloadBinaryToFileAsync`
    会用 Range 从断点续下。

- HttpClient SetUploadProbe(HttpUploadProgressFn f)
  - 安装上传进度探针：请求正文（头部、文件分块、尾部）
    每上线一段就回调一次（sentBytes/totalBytes，二者与
    Content-Length 取自同一处）。探针在驱动请求的线程上被调用
    （上传跑在工作线程时就是工作线程），只应更新共享进度并封送
    回 UI，不要在里面做别的事。目前 `UploadFileBytesAsync`
    汇报进度；其余请求不装探针。

- void UploadTick(int sentBytes, int totalBytes)
  - 探针非空时上报一次进度；装探针的调用点在发送循环里，成本只有
    一次空判 + 一次委托调用。

- string LastDownloadError()
  - 最近一次二进制下载失败的阶段诊断；成功或尚未下载时为空。

- void SetDownloadError(string error)
  - 更新 LastDownloadError 报告的阶段诊断。

- bool CancelRequested()
  - 探针是否要求收工（没装探针时恒为 false）。

- HttpClient PinPublicKey(string spkiSha256Base64)
  - 固定一个受信任的服务器公钥：其 DER SubjectPublicKeyInfo 的
    base64 SHA-256 值。一旦设置了任意固定值，握手就会拒绝
    公钥不匹配任何固定值的服务器，即使其证书
    本可被系统信任——从而阻止 MITM 代理。调用两次可为轮换
    固定一个备用密钥。

- HttpClient DisablePinning()
  - 仅开发用的应急通道，放宽固定限制，使本地
    代理或自签名开发服务器可用。由 ZAN_DEV 保护，因此发布
    构建会将其编译掉并始终强制执行已配置的固定值。

- void ApplyPinning(TlsContext ctx)
  - 将已配置的固定值复制到新建的客户端上下文上并
    启用强制。当固定被禁用或未配置时不执行任何操作。

- HttpClient SetClientCertificate(string certFile, string keyFile)
  - 配置 PEM 证书链和匹配的 PEM 私钥，
    用于双向 TLS 客户端认证。

- HttpClient SetHeader(string name, string headerValue)
  - 为所有请求添加默认头。名字或值含 CR/LF/NUL/DEL
    （会注入额外头字段或提前结束头部）、或名字含冒号（可夹带出
    第二个头字段）时不添加——与 HttpResponse.SetHeader 同一防线。

- HttpClient SetTimeout(int ms)
  - 设置请求超时时间（毫秒）：连接阶段和
    请求/响应阶段各有独立的窗口。小于等于 0
    则禁用超时（协程会一直等待对端
    ，永无返回）。

- HttpClient SetCallPolicy(ExternalCallPolicy policy)
  - 绑定统一外部调用预算。连接/读写仍复用现有 HTTP
    实现；请求/响应大小和重试能力由调用方按 policy 显式声明。

- ExternalCallPolicy CallPolicy()
  - 当前外部调用预算；未绑定时返回 null。

- void CheckTargetPolicy()
  - 绑定 policy 后，目标必须在建连前通过同一个 URL/地址策略；
    这样图片、下载、上传和 AI 即使误用了裸 host API，也不会绕过
    ExternalTargetPolicy。DNS 多地址复核仍由 socket 层继续补齐。
    不允许时抛 HttpRequestException。

- void CheckRequestLimit(int bodyBytes)
  - 绑定 policy 后，正文在建连前按统一 max request bytes 拒绝，
    避免副作用请求已经上线。超限时抛 HttpRequestException。

- int ResponseLimit()
  - HttpFramer 的响应正文上限：无 policy 时沿用此前的 64 MiB，
    有 policy 时由调用方显式预算覆盖。

- int BudgetMin(int phaseMs)
  - 将阶段预算与总预算取较小值。返回值 <= 0 表示该阶段不设截止时间；
    policy 的四类 timeout 因而不会再被 SetCallPolicy 的 total 值静默覆盖。
    totalDeadlineUs 已武装时，阶段预算还要再与"总剩余时间"取较小值：
    重定向每一跳继承同一个绝对时刻，任何通道都无法超出共享 total。

- int ConnectBudget()
  - 绑定 policy 后的连接阶段预算；未绑定时退回客户端 timeout。

- int WriteBudget()
  - 绑定 policy 后的写阶段预算；未绑定时退回客户端 timeout。

- int ReadBudget()
  - 绑定 policy 后的读阶段预算；未绑定时退回客户端 timeout。

- int IdleBudget()
  - 绑定 policy 后的流式空闲窗口预算（SSE/下载逐块续期）；未绑定时退回客户端 timeout。

- int HandshakeBudget()
  - TLS 握手预算，与连接阶段共用同一档。

- int TotalBudget()
  - 绑定 policy 后的总预算（已扣除总截止时间的已耗部分）；
    未绑定时返回 0（表示无总截止）。

- void ArmTotalBudget()
  - 武装（或重置）跨跳共享的绝对总截止时间。绑定 policy 的请求通道
    入口调用；total <= 0 时不武装，保持该通道各自预算的旧行为。
    已武装时不重置：一次外呼（含 UploadFileBytesAsync 先行武装、
    通道随后调用）从第一跳起点计时，重定向各跳继承同一时刻。

- void ResetTotalBudget()
  - 每个公共入口重新武装；只有内部重定向跳数继承已有截止时间。

- void Close()
  - 关闭复用的 keep-alive 连接（若在）。客户端仍可继续
    使用：下一次请求会重新建立连接。进程退出前或长寿命客户端
    空闲时调用，避免悬挂的半开连接。

- HttpClient DisableKeepAlive()
  - 禁用连接复用：每个请求各建一条连接并在响应后关闭
    （与旧版行为一致）。返回自身以便链式调用。

- void DropAlive()
  - 丢弃池中的连接。连接可能已被对端半关或处于错误状态，
    因此四个句柄都要收干净；幂等，可在任何时候调用。

- bool HasHeaderName(string name)
  - 当 `SetHeader` 已提供过一个名字
    （ASCII 大小写不敏感）匹配的头部时返回 true。这样 BuildRequest 可以只保留
    调用者自选的 Content-Type，而非总是强制表单编码。

- static bool AsciiNameEquals(string hay, string name, int n)
  - 将 <paramref name="hay"/> 的前 <paramref name="n"/> 个字节与 <paramref name="name"/> 做
    ASCII 大小写不敏感的相等比较。

- static bool IsRequestTargetSafe(string path)
  - 请求目标（request-target）是否可安全写入请求行：CR/LF/NUL/DEL
    会把一行撕成两行（请求行注入/第二个请求），空格会把请求行切成
    多个 token。SetHeader 有同一防线；请求行是调用方字符串进入
    报文的另一入口，不能漏。

- static bool IsMethodSafe(string method)

- static bool IsHostSafe(string host)

- string BuildRequestHead(string method, string path, int bodyLen, bool close)
  - 构建请求头部半区；bodyLen<=0 时不写 Content-Length。

- string BuildRequest(string method, string path, string body, bool close)
  - BuildRequestHead 加上文本正文（一次性通道用）。

- string BuildDownloadRequest(string path, int have)
  - 下载请求沿用统一 method/host/path/header 注入校验，只额外插入
    Range 字段；Range 不从调用方原文拼接进请求行。

- void AbsorbCookies(string path, string raw)
  - 把响应里的 Set-Cookie 交给存储。未启用 cookie 时什么都不做，
    所以调用点不必自己判空。

- void FailConnect()
  - 报告连接失败，区分被拒绝的连接与
    超时耗尽的截止时间。不放在 RequestAsync 里，以便消息在
    无其他分配的帧中构建（`throw` 不会释放存活局部变量，
    见 docs/bugs/throw-leaks-live-locals.md）。

- void FailTimeout()
  - 报告请求截止时间耗尽（清扫器已挂起
    套接字，这结束了调用者的读取循环）。

- async TcpClient ConnectTarget()
  - Resolve once, review every A/AAAA answer in TcpClient, and connect using
    the reviewed sockaddr. A local exception is explicit in the policy; a
    hostname that merely contains "localhost" never receives that exception.

- async string RequestAsync(string method, string path, string body)
  - 执行请求并按策略逐跳处理重定向。默认策略的跳数为 0，
    因而返回原始 3xx；显式启用时只接受绝对 HTTP(S) Location。
    相对 Location 和循环会拒绝；到达显式跳数上限时返回当前 3xx，
    不会猜测另一个目标，也不会继续发出请求。逐跳规则（跳数/防环/
    动作改写/下一跳派生）与字节通道共用同一组 helper
    （RedirectWanted/RedirectReplay/RedirectNextClient）。

- string RedirectHost()
  - 本客户端 host 的小写形式，剥掉 IPv6 字面量的方括号；
    与 ExternalTarget.Host() 比较重定向是否同源。

- string CanonicalRequestTarget(string requestPath)
  - 本客户端当前请求目标的规范 URL（scheme://host[:port]/path），
    供重定向防环的 visited 集合使用；解析失败返回 ""。

- bool IsRedirectHeader(string line, bool sameOrigin)
  - 同下，默认不丢弃实体类头。

- bool IsRedirectHeader(string line, bool sameOrigin, bool dropEntity)
  - 重定向下一跳是否继承该默认头：Host/Range 永不继承；
    dropEntity 时实体类头（Content-Length/Transfer-Encoding/Expect/
    Content-Type/Content-Encoding/Content-MD5/Trailer）不继承；
    跨 origin 时敏感凭据头（Authorization/Cookie/Proxy-Authorization）
    不继承。

- bool RedirectWanted(HttpResponse response, int hops)
  - 下一跳是否值得继续跟随：响应必须是 3xx 重定向、policy
    已绑定且仍有跳数。Location 头的 CRLF 注入防线在这里统一落地：
    带折行的 Location 直接拒绝（与其传给 ExternalTarget.Parse 让它
    以文本校验失败，不如显式报注入拒绝）。

- bool RedirectReplay(string method, int code, out string nextMethod)
  - 3xx 响应里的 307/308 重放守卫与 301/302/303 动作改写。
    nextMethod 为改写结果；返回 false 表示该响应不允许自动重放
    （调用方原样返回 3xx）。

- HttpClient RedirectNextClient(HttpClient current, ExternalTarget target, bool sameOrigin, bool dropEntityHeaders)
  - 为重定向的下一跳派生客户端：继承 TLS/验证/固定值/证书/
    策略/超时/cookie 与共享 totalDeadlineUs，按 sameOrigin 与动作改写
    过滤默认头。中转客户端是一次性连接（kaEnabled=false），池仍归
    发起客户端独有。

- async HttpResponse FollowBytesRedirects(string method, string path, string body, HttpBytesDriver driver)
  - 字节通道（SendBytesAsync/UploadFileBytesAsync/下载）共用的
    重定向跟随循环。driver 在当前客户端上按给定方法发出一次请求并返回
    响应；本方法在拿到 3xx 时按 policy 解析绝对 Location、改写动作、
    跨 origin 清敏感头，并在派生的下一跳客户端上重放 driver。非 3xx
    原样返回。301/302/303 的动作改写同时把请求正文清空——字节通道
    的实体无法安全降级重放，下载驱动的 Range 语义由驱动端用 method
    参数表达。

- async HttpResponse FollowBytesFromResponse(string method, string path, string body, HttpBytesDriver driver, HttpResponse resp)
  - Uploads send their multipart body first; only subsequent rewritten GET
    hops use the bodyless redirect driver.

- void ValidateTextResponse(string raw, string method)
  - 发送 HTTP 请求并返回原始响应。当连接（或 TLS 握手）失败
    或请求未在超时内完成时，抛出
    HttpRequestException。
    连接在请求文本构建之前建立，因此
    连接失败抛出时，本帧内没有存活的分配。
    
    默认走 keep-alive 复用通道（同一客户端同时只允许一个在途
    请求）；已有请求在途时，并发调用退回一次性 Connection: close
    通道，各自读到对端关闭为止。
    Reject absent or incomplete fixed-length text responses instead of
    letting HttpResponse.Parse manufacture a successful empty 200.

- async string RequestOnceAsync(string method, string path, string body)

- async string RequestTlsAsync(string method, string path, string body)
  - 一次性 TLS 请求：新建 TLS 连接后发送请求并读取完整响应。

- async TlsStream ConnectTls(TlsContext ctx, nint sock)
  - 在同一 deadline 清扫下执行 TLS 握手；超时返回 null。

- async string RequestAliveAsync(string method, string path, string body)
  - keep-alive 复用通道的请求入口；同一客户端同时只允许一个在途请求。

- async string RequestAliveCore(string method, string path, string body)
  - 复用通道的实际收发；响应按显式长度拼接，二进制正文不被截断。

- async bool ConnectAlive()
  - 为复用通道建立底层连接（明文或 TLS 握手）；连接失败返回 false。

- static bool HeadHasContentLength(string head)
  - 头部块里是否声明了 Content-Length。HttpRequest.Parse 把缺失的
    Content-Length 记为 0，与"声明为 0"无法区分，所以要在头部
    文本里找（头部块是 ASCII，整体小写后按行首匹配）。

- static bool HeadSaysClose(string head)
  - 头部块是否带 Connection: close（按逗号 token、OWS 和大小写解析）。

- async HttpResponse SendAsync(string method, string path, string body)
  - 发送请求并返回解析后的响应，使调用者可以看到
    状态行和头部，而不只是正文：来自网关的 502
    与一个负载古怪的 200 响应本来无法区分。

- async HttpResponse SendBytesAsync(string method, string path, string body)
  - 二进制安全的一次性请求。通道与 `SendAsync`
    相同（Connection: close，读至对端关闭或定界完成），但请求正文
    按显式字节长度发送（strlen 定长的发送会在内嵌 NUL 处截断，
    TLS 变体本就按 req.Length 发送，这里对齐），响应全程按
    显式字节数组装（`HttpFramer` + ByteBuffer）：
    正文里的 NUL 不会把响应截断，结果的 bodyBytes/bodyBytesLen
    有效，`HttpResponse.IsBinary` 与
    `HttpResponse.BodyLength` 据此工作。适合图片、
    字体等二进制资源的下载与上传；文本响应照常可用 body 读取。
    绑定 policy 时按同一规则跟随重定向（3xx 原样返回仍是默认）。

- async HttpResponse SendBytesAsync(string method, string path, string body, int bodyLen)
  - 发送原始字节正文（带显式字节长度，二进制安全，可含 NUL 字节）。

- async HttpResponse SendBytesAsync(string method, string path, byte[]body, int bodyLen)
  - 发送原始字节数组（带显式字节长度，二进制安全，可含 NUL 字节）。

- static async HttpResponse SendBytesOnceAsync(HttpClient client, string method, string path, string body)
  - 一次真实上线：SendBytesAsync 的无重定向主体，同时作为
    FollowBytesRedirects 的驱动回调。method 随驱动签名显式传递：
    重定向改写（301/302/303 → GET）后驱动端据此发空 GET。

- static async HttpResponse SendBytesOnceWithLenAsync(HttpClient client, string method, string path, string body, int bodyLen)

- async HttpResponse ReadFramedResponse(TcpClient conn, string path, string method, HttpDeadlineToken watch, TlsStream stream, TlsContext ctx)
  - SendBytesAsync / SendBytesTlsAsync / SendBytesBodyAsync 共用的
    响应读取：从已发出请求的连接上按 HttpFramer 组装解析后的响应，
    超时/对端关闭/超限走异常路径。stream/ctx 非 null 表示 TLS 通道，
    清理时随连接一并关闭。（throw 不释放存活局部变量的缺陷已由
    A8-3/A8-12 修复，不再需要 throw 前逐个置空的历史卫生。）

- void CloseChannel(TcpClient conn, HttpFramer fr, TlsStream stream, TlsContext ctx)
  - 响应读取后的统一通道清理：先拆 framer，再关 TLS 流（如有）、
    连接，最后释放 TLS 上下文——与既有各变体的清理次序一致。
    fr 允许为 null（请求尚未发完就放弃时还没有 framer）。

- async HttpResponse SendBytesTlsAsync(string method, string path, string body)
  - SendBytesAsync 的 TLS 变体：握手与一次性通道一致，只是读取
    走 HttpFramerTls.Create（TlsStream.RecvIntoAsync 同样给出
    精确字节数）。

- async HttpResponse SendBytesTlsWithLenAsync(string method, string path, string body, int bodyLen)

- async HttpResponse SendBytesBodyAsync(string method, string path, string head, string localPath, int fileLen, string tail, int total)
  - UploadFileBytesAsync 的明文通道：头部 + 文件字节 + 尾部三段式，
    每段按显式字节长度发送——strlen 定长的 SendAsync 会把内嵌 NUL
    后的正文截掉；Content-Length（total）与各段发送长度取自同一处
    （与 HttpServer 发送二进制响应同一纪律）。文件按 64KB 分块
    读取上线，进度探针随字节推进。绑定 policy 时接入字节重定向链：
    3xx 原样返回仍是默认；301/302/303 改写为 GET 后实体不重放
    （multipart 上传无法降级），由驱动端发空 GET。

- static async HttpResponse UploadBytesOnceAsync(HttpClient client, string method, string path, string body)
  - 上传通道的重定向驱动回调：只发一次真实上线。重定向跟随中动作用
    method 参数表达（改写后的 GET 不带实体）；原动作 POST 的实体分块
    无法通过该回调重放——multipart 边界态头尾在 307/308 才需要原样
    重传，而 307/308 重放由 RedirectReplay 直接拒绝（非 GET/HEAD），
    因此实体分块只存在于首发路径。

- async HttpResponse SendBytesBodyTlsAsync(string method, string path, string head, string localPath, int fileLen, string tail, int total)
  - UploadFileBytesAsync 的 TLS 变体：握手同 SendBytesTlsAsync，
    三段式正文按显式长度发送，读取走 HttpFramerTls.Create。

- async string GetAsync(string path)
  - 发送 GET 请求并返回响应体。

- async string PostAsync(string path, string body)
  - 发送带表单数据的 POST 请求。

- async string PutAsync(string path, string body)
  - 发送 PUT 请求。

- async string DeleteAsync(string path)
  - 发送 DELETE 请求。

- static async string GetHttpsAsync(string host, int port, string path)
  - 快捷静态 https GET（一次性）；校验保持开启，因此
    服务器证书必须链到受信任的 CA。

- static async string GetAsync(string host, int port, string path)
  - 快捷静态 GET 方法（一次性）。

- static async string PostAsync(string host, int port, string path, string body)
  - 快捷静态 POST 方法（一次性）。

- static async void DownloadFileAsync(string host, int port, string path, string localPath)
  - 将内容下载到文件。

- static int IndexOf(string s, string needle, int from)
  - 逐字节的子串搜索（利用原生机器指令向量化搜索）。

- static int ParseHex(string s)
  - 解析十六进制 chunk 大小前缀，在第一个非十六进制字节处停止
    （例如 ';' 分块扩展或 CR）。累计上限 0x00FFFFFF（16 MiB-1）：恶意
    服务器发超长十六进制串会让 v*16 回绕成负数，下游
    need = crlf+2+size+2 随之为负、Substring 触发数组越界陷阱直接杀死
    进程——服务器端解析器（HttpFramer.ReadChunked）早有同样的上限纪律，
    客户端此前从未对齐。超限返回 -1，调用方按坏帧处理。

- static void AppendText(string file, string s)
  - 以追加模式把文本写入文件（fopen "ab" + fputs）；打不开时静默放弃。

- static void TruncateFile(string file)
  - 清空文件内容（fopen "wb" 后立即关闭）；打不开时静默放弃。

- static bool HeadIsChunked(string head)
  - 当响应头声明分块传输编码（chunked transfer-encoding）时返回 true。
    调用者必须只传头部（"\r\n\r\n" 之前的字节）。
    按行解析：字段名必须是行首（大小写不敏感）"transfer-encoding"，
    且只有逗号列表的最后一项等于 "chunked" 才算。此前在整个头块里
    包含匹配 "chunked"，恶意上游一个 ETag: "chunked" 就能把真实的
    SSE 流按假尺寸行解析到乱码/截断。

- static int HeadStatus(string head)
  - 从状态行解析数字状态码；不是严格的 "HTTP/1.0|1.1 OWS 3DIGIT"
    （版本后缀、缺 OWS、非数字、第 4 位数字）时返回 0，与
    HttpResponse.Parse 的拒收口径一致——下载/SSE 路径按 <100
    拒收，不按数字前缀猜。

- async int PostSseToFileAsync(string path, string body, string outFile, string doneFile)
  - 将响应体边到达边流入 <paramref name="outFile"/>
    （解码分块传输编码），因此轮询该文件的调用者
    能看到增量输出——例如 text/event-stream（SSE）应答。
    此处的超时是空闲窗口，不是总预算：每收到一个
    chunk 都会重新计时，因此只要对端持续发送，长连接流就能保持存活，
    而对端沉默时依然会被挂起。
    从不抛异常：结果写入 <paramref name="doneFile"/>，正常结束为
    "ok"，否则为 "err: ..."（包括非 2xx 状态，
    其错误正文仍会流入 outFile）。

- async int PostSseToSinkAsync(string path, string body, SseSink sink)
  - 同样的流，但交付给 `SseSink`。
    内存 sink 让整个交互都留在进程内：请求
    可在工作线程上运行，同时渲染应答的线程
    从 sink 中取走数据，无需临时文件，也无需辅助进程。

- static string KeepHead(string acc, string piece)
  - 当 `acc` 不足 512 字节时把 `piece` 追加进去：足够容纳
    说明原因的报错正文，并设上限，使流式发送数兆
    报错正文的对端也无法撑大缓冲区。

- static bool EndsWithStr(string s, string suf)
  - 当 `s` 以 `suf` 结尾时返回 true（按字节比较）。

- static string OneLine(string s)
  - 将连续空白合并，使 JSON 报错正文能放进一行状态栏，并
    将结果限制在 300 字节以内。

- static bool EndsWithCi(string s, string suf)
  - 当 `s` 末尾字节等于 `suf` 时返回 true（ASCII 大小写不敏感）。

- static void ValidateMultipartToken(string what, string value)
  - multipart 的 name/filename 原样进入 Content-Disposition 的引号内：
    含 CR/LF/NUL/DEL 会注入额外头字段或提前结束头部，含引号会逃出
    引号串。非法即抛，与 WebDavClient.ValidateHeader 同一防线。

- static string GuessMime(string name)
  - 将文件后缀映射为 content type，默认 octet-stream。

- static string HeadHeader(string head, string lowerName)
  - 从响应头文本里取单个字符串头（大小写不敏感，从行首匹配），
    例如 HeadHeader(head, "Location")。不存在时返回 ""。下载通道
    不走 HttpResponse.Parse（头部与正文共享一次手写读取），重定向
    的 Location 就在这里解析。

- static int HeaderInt(string head, string lowerName)
  - 读取整数值的响应头（大小写不敏感，从行首匹配），
    例如 HeaderInt(head, "content-length")。不存在时返回 0。

- static int RangeTotal(string head)
  - 从 "Content-Range: bytes X-Y/Z" 头取得资源总大小（Z 值），
    无该头时返回 0。

- static bool RangeAlreadyComplete(string head, int have)
  - A 416 proves completion only when the server supplies an unsatisfied
    Content-Range with a known total matching the local file size.

- async HttpResponse UploadFileAsync(string path, string field, string localPath, string fileName)
  - 将本地文件作为 multipart/form-data 的单个字段上传到
    <paramref name="path"/>（<paramref name="field"/> 是表单名，
    <paramref name="fileName"/> 是上报给服务器的文件名）。
    会将整个文件读入内存，适合中等大小的文件。返回解析后的
    响应。注意正文经 ReadAllText 读取，只适合文本文件；
    二进制内容（图片、字体等）请用
    `UploadFileBytesAsync`。

- async HttpResponse UploadFileBytesAsync(string path, string field, string localPath, string fileName)
  - 字节安全的 multipart/form-data 上传：
    <paramref name="localPath"/> 按原始字节分块读取上线
    （`UploadFileAsync` 走 ReadAllText，二进制内容会
    损坏），ASCII 头尾与文件字节都按显式字节长度发送，
    Content-Length 与发送长度取自同一处，内嵌 NUL 原样到达。
    <paramref name="field"/> 是表单名，<paramref name="fileName"/>
    是上报给服务器的文件名。传输层失败抛
    `HttpRequestException`，HTTP 状态由调用方检查；
    进度（已上线字节/总字节）经 `SetUploadProbe` 上报。

- async int DownloadRangeToFileAsync(string path, string localPath, string progressFile)
  - 将 <paramref name="path"/> 下载到
    <paramref name="localPath"/>，可续传中断的下载。
    超时是空闲窗口（每收到一个 chunk 就重新计时），因此
    慢速网络上的大文件不会在传输中途被中止。它会发送
    `Range: bytes=<existing>-`，使支持范围请求（HTTP 206）的服务器
    从本地文件已下载到的位置继续，而不是重新开始；
    忽略该头的服务器（200）会从头开始，416 表示
    文件已完整下载。进度（"<received>/<total>"）会
    随字节到达写入 <paramref name="progressFile"/>。绑定了
    `ExternalCallPolicy` 且 `Redirects(n)>0` 时，
    3xx 会按策略逐跳跟随（每跳重审目标、共享同一总预算、防环）；
    未启用时 3xx 仍按错误状态拒绝落盘。返回当前磁盘上的字节数，
    连接/握手失败时返回 -1。

- async int DownloadRangeOnceAsync(string path, string localPath, string progressFile, List<string> visited, int hops)
  - DownloadRangeToFileAsync 的单次尝试体：3xx 且 policy
    显式启用（Redirects>0）且未到跳数上限时，在派生的下一跳
    客户端上递归重开整段下载——have 从本地文件重建，Range 行为
    逐跳一致。visited/hops 由入口初始化，跨跳共享防循环。

- async long DownloadBinaryToFileAsync(string path, string localPath, string progressFile)
  - 把 <paramref name="path"/> 下载到
    <paramref name="localPath"/>，正文按字节搬运——正文里的 NUL
    不会被当成结尾，因此适合 .tar.bz2 / .zip 之类的二进制归档
    （`DownloadRangeToFileAsync` 把正文当字符串拼接，
    遇到第一个 NUL 就会以为对端关闭了）。
    
    语义与 `DownloadRangeToFileAsync` 一致：发送
    `Range: bytes=<已有>-` 续传，206 追加、200 重下、416 视为
    已完成；超时是空闲窗口。进度（"<已收>/<总计>"）写入
    <paramref name="progressFile"/>。返回磁盘上的字节数；连接/握手
    失败、响应不完整、或对端用 chunked 传输（本方法只支持带
    Content-Length/Range 的 identity 正文）时返回 -1；装了
    `SetCancelProbe` 的探针并在下载中路取消时返回 -2
    （已收字节保留在 <paramref name="localPath"/>，可续传）。绑定了
    `ExternalCallPolicy` 且 `Redirects(n)>0` 时，
    3xx 按策略逐跳跟随（语义与
    `DownloadRangeToFileAsync` 相同）；未启用时 3xx 仍按
    错误状态拒绝落盘。

- async long DownloadBinaryOnceAsync(string path, string localPath, string progressFile, List<string> visited, int hops)
  - DownloadBinaryToFileAsync 的单次尝试体：3xx 的跟随
    语义与 DownloadRangeOnceAsync 相同（防环、每跳重审目标、
    共享 totalDeadlineUs），have/Range 由递归体从磁盘重建。

- static string NewBuffer(int size)
  - 分配一个可写的 ARC 托管字节缓冲区（内容为空格）。
    原始 calloc 块不带字符串引用计数头，交给 ARC 会出错。


## SseDelivery (class)

`SseSink.TakeCo` 的返回值：取走的数据与
尝试是否已结束。finished=true 时 data 是最后一批（可能为空串）。

- string data;

- bool finished;


## SseSink (class)

流式响应体的目的地（text/event-stream，或
任何边到达边消费的应答）。

两种模式：
* file  —— chunk 追加到文件，结果写入标记文件。
由独立进程或轮询者持续读取。
* memory —— chunk 在互斥锁保护的缓冲区中累积，
请求可在工作线程上运行，另一线程（例如 GUI 循环）
每帧用 <c>Take</c> 取走数据。完全不碰磁盘。

正是内存模式让流式请求可以存活在渲染它的进程内部：
跨线程传递的只有缓冲区和结果字符串，
两者都在锁的保护下交接。

- string path;
  - 文件模式下的输出文件；内存模式下为 ""。

- string donePath;
  - 文件模式下的标记文件；内存模式下为 ""。

- nint fp;
  - 文件模式下整个流的打开句柄（0 表示无）。

- List<string> parts;
  - 已写入的 chunk（内存模式），按到达顺序原样保存。存一串 chunk 而不是
    一整块字符串：`buf = buf + piece` 每个 chunk 都要复制一遍全文，一次
    几千个 chunk 的长回答就是 O(n²) 的拷贝；而且逐帧刷新预览的读者只需要
    「上次之后新到的那几段」（<c>Since</c>），不必每一拍把已经收到的全文
    再复制一遍（<c>All</c> 就是那份复制，还占着写入线程的锁）。

- int cut;
  - parts 中已经被 <c>Since</c> 交给读者的段数。

- string attempt;
  - 刚结束的那次尝试的结果（有尝试在运行时为 ""）。

- string done;
  - 所有者确定不再重试后发布的结果（此前为 ""）。

- int total;
  - 已写入的总字节数，使轮询者无需排空缓冲区就能区分
    "仍在流式传输" 与 "尚无数据"。

- bool aborted;
  - 终态：Abort/Overflow 后拒绝后续写入，避免消费者断开后
    producer 继续触碰已无效的输出目标。

- bool overflowed;

- int maxBytes;
  - 单次流的最大正文长度；0 表示由 transport 自己决定或不限制。

- int responseStatus;
  - 上游本次握手的响应状态、原始头部和失败正文。普通成功 SSE 只需状态
    与头部；非 2xx 的正文供调用方在还没有向下游输出前作出重试决策。

- string responseHeaders;

- string responseErrorBody;

- Gate ready;
  - 事件门：Write/Finish 时发信号，让消费者协程真挂起地等数据，
    而不是定时轮询 Take。 surplus 语义保证「先写后等」不丢唤醒。

- nint lockHandle;

- nint lockHandle;

- [DllImport("crt", EntryPoint="fopen")]static extern nint fopen(string path, string mode);

- [DllImport("crt", EntryPoint="fclose")]static extern int fclose(nint fp);

- [DllImport("crt", EntryPoint="fwrite")]static extern long fwrite(string buf, long size, long count, nint fp);

- static SseSink ToFile(string outFile, string doneFile)
  - 一种 sink，把正文追加到 <paramref name="outFile"/>，
    把结果写到 <paramref name="doneFile"/>。

- static SseSink ToMemory()
  - 一种 sink，把正文保存在内存中供另一线程
    用 <c>All</c> 读取（或用 <c>Take</c> 取走）。

- void Begin()
  - 为新流准备 sink（截断文件、丢弃
    任何已缓冲的文本）。由传输层在第一个 chunk 前调用。

- void SetMaxBytes(int bytes)
  - 设置本次流的最大正文长度。0 表示不由 sink 限制。

- void Write(string piece)
  - 追加一个已解码的 chunk。终态后的写入会安全丢弃。

- bool TryWrite(string piece)
  - 追加 chunk 并返回是否被接受。超过上限时原子地进入
    overflow 终态；调用方可以停止读取上游，后续写入都会丢弃。

- void Abort()
  - 消费者主动放弃时进入终态；重复调用安全。

- void Overflow()
  - 标记流超限并唤醒消费者；重复调用安全。

- bool Aborted()
  - 消费者是否已主动放弃（Abort 进入的终态）。

- bool Overflowed()
  - 流是否已超过 maxBytes 上限（Overflow 进入的终态）。

- void SetResponse(int status, string headers, string errorBody)
  - Publishes the upstream handshake metadata for the current
    attempt. It is deliberately separate from stream chunks so a caller can
    inspect a 429 before opening or writing its own SSE response.

- int ResponseStatus()
  - 本次握手的响应状态码（尚未设置时为 0）。

- string ResponseHeaders()
  - 本次握手的原始响应头。

- string ResponseErrorBody()
  - 非 2xx 时的失败正文（供调用方做重试决策）。

- string Join(int from)
  - parts[from..] 拼成一段。调用者必须已经持有锁。

- void Finish(string marker)
  - 以结果标记（"ok" 或 "err: ..."）结束一次尝试。
    会重试的所有者用 <c>Attempt</c> 读取它并自行发布
    最终结果；而普通的单次调用者（文件模式）则
    在此直接把它写入标记文件。重复结束安全且只发布一次。

- string Attempt()
  - 最近一次已结束尝试的结果（有尝试在运行时为 ""）。

- void Publish(string marker)
  - 向 <c>Done</c> 的读者发布最终结果。

- string All()
  - 自 <c>Begin</c> 以来收到的全部内容，且不消费它们
    （内存模式）。每帧重新解析整个正文的读者——比如
    展示答案逐步生成的 UI——适合用这个而不是 <c>Take</c>。

- string Since()
  - 自上次调用以来新到的那部分（内存模式），已收到的内容仍然
    留在 sink 里供 <c>All</c> 读取。边到边显示的读者用这个而不是
    <c>All</c>：一个长回答的最后那几十秒里，每一拍 <c>All</c> 都要把已经
    收到的几百 KB 全文复制一遍——那份复制（以及它占住的写入锁）就是流式
    输出时烧掉的那一核。

- string Take()
  - 移除并返回目前缓冲的全部内容（内存模式）。

- async SseDelivery TakeCo()
  - 协程版消费：取走目前缓冲的全部内容；没有数据时
    挂起在事件门上直到 <c>Write</c> 或 <c>Finish</c> 唤醒（事件驱动，
    无轮询）。同时给出结束标志——true 表示本次尝试已终结
    （成功或失败），返回值是最后一批数据。
    仅应在协程内调用；跨真实线程的消费者请用 <c>Take</c> 轮询。

- async SseDelivery TakeCo(int timeoutMs)
  - TakeCo 的带超时版本：等待超过 <paramref name="timeoutMs"/>
    仍无数据且尝试未结束时返回空 data、finished=false。
    超时返回使消费者能在等待中插入保活或断连检查。

- string Done()
  - 结果标记，流仍在运行时为 ""。

- int Total()
  - 目前收到的字节数（空闲看门狗可监视此值）。

- void Dispose()
  - 释放锁句柄与事件门引用。门本身不 Close：TakeCo 超时
    路径留下的看门狗协程在到点后仍会 zan_gate_signal 这个句柄，
    提前 free 存储就是段错误。与连接池（PoolCore）同一策略——
    门的存活期与对象一致，不提前销毁；ARC 会随 sink 一起回收它。
    此后再使用该 sink 即非法。


## HttpResponse (delegate)

字节通道的重定向驱动：在给定客户端上以指定方法发出一次
请求并返回响应。HttpClient 的字节重定向循环以它为回调在当前/下一跳
客户端上重放；方法由循环按 301/302/303 改写规则传入。

`delegate HttpResponse HttpBytesDriver(HttpClient client, string method, string path, string body);`


## bool (delegate)

取消探针：返回 true 表示调用方已经不要这个传输了。
见 `HttpClient.SetCancelProbe`。

`delegate bool HttpCancelFn();`


## void (delegate)

上传进度探针：随字节上线推进（sentBytes/totalBytes）。
在传输所在的 worker 线程上回调，GUI 调用方需自行封送回 UI 线程。
见 `HttpClient.SetUploadProbe`。

`delegate void HttpUploadProgressFn(int sentBytes, int totalBytes);`
