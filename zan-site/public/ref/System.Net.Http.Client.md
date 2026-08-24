# System.Net.Http.Client

> 源码: `stdlib/System/Net/Http/Client/CookieJar.zan`, `stdlib/System/Net/Http/Client/HttpClient.zan`, `stdlib/System/Net/Http/Client/SseSink.zan`


## Cookie (class)

一条已存储的 cookie。<c>hostOnly</c> 区分 Set-Cookie 是否带了
Domain 属性：不带时只回给设置它的那台主机，带时连子域一起匹配。
<c>expires</c> 为 0 表示会话 cookie（进程内一直有效）。

- string name;

- string cookieValue;

- string domain;

- string path;

- bool hostOnly;

- bool secure;

- long expires;

- Cookie(string name, string cookieValue)

- string Name()

- string Value()

- string Domain()

- string Path()

- bool Secure()

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

- static bool DomainMatches(string host, string domain, bool hostOnly)

- static bool PathMatches(string path, string cookiePath)

- static string DefaultPath(string requestPath)

- static string PathOnly(string requestPath)

- static long ParseHttpDate(string s)

- static List<string> Tokens(string s)

- static List<string> SplitColon(string s)

- static int MonthOf(string t)

- static bool AllDigits(string s)

- static long ParseLong(string s)

- static int IndexOfChar(string s, int ch, int from)

- static string Trim(string s)

- static string Lower(string s)

- static bool NameEquals(string head, int at, string lower)


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

- CookieJar jar;

- HttpCancelFn cancelProbe;

- string lastDownloadError;

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

- string LastDownloadError()
  - 最近一次二进制下载失败的阶段诊断；成功或尚未下载时为空。

- void SetDownloadError(string error)

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

- HttpClient SetClientCertificate(string certFile, string keyFile)
  - 配置 PEM 证书链和匹配的 PEM 私钥，
    用于双向 TLS 客户端认证。

- HttpClient SetHeader(string name, string headerValue)
  - 为所有请求添加默认头。

- HttpClient SetTimeout(int ms)
  - 设置请求超时时间（毫秒）：连接阶段和
    请求/响应阶段各有独立的窗口。小于等于 0
    则禁用超时（协程会一直等待对端
    ，永无返回）。

- bool HasHeaderName(string name)
  - 当 `SetHeader` 已提供过一个名字
    （ASCII 大小写不敏感）匹配的头部时返回 true。这样 BuildRequest 可以只保留
    调用者自选的 Content-Type，而非总是强制表单编码。

- static bool AsciiNameEquals(string hay, string name, int n)
  - 将 <paramref name="hay"/> 的前 <paramref name="n"/> 个字节与 <paramref name="name"/> 做
    ASCII 大小写不敏感的相等比较。

- string BuildRequest(string method, string path, string body)

- void AbsorbCookies(string path, string raw)

- void FailConnect()

- void FailTimeout()

- async string RequestAsync(string method, string path, string body)
  - 发送 HTTP 请求并返回原始响应。当连接（或 TLS 握手）失败
    或请求未在超时内完成时，抛出
    HttpRequestException。
    连接在请求文本构建之前建立，因此
    连接失败抛出时，本帧内没有存活的分配。

- async string RequestTlsAsync(string method, string path, string body)

- async HttpResponse SendAsync(string method, string path, string body)
  - 发送请求并返回解析后的响应，使调用者可以看到
    状态行和头部，而不只是正文：来自网关的 502
    与一个负载古怪的 200 响应本来无法区分。

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

- static int ParseHex(string s)

- static void AppendText(string file, string s)

- static void TruncateFile(string file)

- static bool HeadIsChunked(string head)

- static int HeadStatus(string head)

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

- static bool EndsWithStr(string s, string suf)

- static string OneLine(string s)

- static bool EndsWithCi(string s, string suf)

- static string GuessMime(string name)

- static int HeaderInt(string head, string lowerName)

- static int RangeTotal(string head)

- async HttpResponse UploadFileAsync(string path, string field, string localPath, string fileName)
  - 将本地文件作为 multipart/form-data 的单个字段上传到
    <paramref name="path"/>（<paramref name="field"/> 是表单名，
    <paramref name="fileName"/> 是上报给服务器的文件名）。
    会将整个文件读入内存，适合中等大小的文件。返回解析后的
    响应。

- async int DownloadRangeToFileAsync(string path, string localPath, string progressFile)
  - 将 <paramref name="path"/> 下载到
    <paramref name="localPath"/>，可续传中断的下载。
    超时是空闲窗口（每收到一个 chunk 就重新计时），因此
    慢速网络上的大文件不会在传输中途被中止。它会发送
    `Range: bytes=<existing>-`，使支持范围请求（HTTP 206）的服务器
    从本地文件已下载到的位置继续，而不是重新开始；
    忽略该头的服务器（200）会从头开始，416 表示
    文件已完整下载。进度（"<received>/<total>"）会
    随字节到达写入 <paramref name="progressFile"/>。返回
    当前磁盘上的字节数，连接/握手失败时返回 -1。

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
    （已收字节保留在 <paramref name="localPath"/>，可续传）。

- static string NewBuffer(int size)
  - 分配一个可写的 ARC 托管字节缓冲区（内容为空格）。
    原始 calloc 块不带字符串引用计数头，交给 ARC 会出错。


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

- nint lockHandle;

- string lockHandle;

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

- void Write(string piece)
  - 追加一个已解码的 chunk。

- string Join(int from)
  - parts[from..] 拼成一段。调用者必须已经持有锁。

- void Finish(string marker)
  - 以结果标记（"ok" 或 "err: ..."）结束一次尝试。
    会重试的所有者用 <c>Attempt</c> 读取它并自行发布
    最终结果；而普通的单次调用者（文件模式）则
    在此直接把它写入标记文件。

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

- string Done()
  - 结果标记，流仍在运行时为 ""。

- int Total()
  - 目前收到的字节数（空闲看门狗可监视此值）。

- void Dispose()
  - 释放锁句柄。此后再使用该 sink 即非法。


## bool (delegate)

取消探针：返回 true 表示调用方已经不要这个传输了。
见 `HttpClient.SetCancelProbe`。

`delegate bool HttpCancelFn();`
