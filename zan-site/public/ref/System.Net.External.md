# System.Net.External

> 源码: `packages/Zan.Net/src/System/Net/External/ExternalCallPolicy.zan`, `packages/Zan.Net/src/System/Net/External/ExternalTarget.zan`, `packages/Zan.Net/src/System/Net/External/ExternalTargetPolicy.zan`


## ExternalCallPolicy (class)

一次外部调用的有限预算。该类只保存策略，不启动线程、不执行
网络请求；传输层应在每个阶段检查 total/idle deadline 与字节预算。

- int connectTimeoutMs;

- int writeTimeoutMs;

- int readTimeoutMs;

- int idleTimeoutMs;

- int totalTimeoutMs;

- int maxRequestBytes;

- int maxResponseBytes;

- int maxErrorBytes;

- int maxStreamBytes;

- int maxRedirects;

- int retryAttempts;

- int retryBackoffMs;

- bool allowHttp;

- bool allowLoopback;

- bool localHttpOnly;

- bool retryNetworkErrors;

- bool idempotent;

- string idempotencyKey;

- ExternalCallPolicy()

- static ExternalCallPolicy Default()
  - 安全的远程请求默认值：HTTPS、无 loopback、无重定向、无
    自动重试，适合 POST/副作用调用。

- ExternalCallPolicy ConnectTimeout(int ms)
  - 建立 TCP 连接的超时（毫秒）。非法值（≤0）被忽略。

- ExternalCallPolicy WriteTimeout(int ms)
  - 发送请求正文的写超时（毫秒）。非法值被忽略。

- ExternalCallPolicy ReadTimeout(int ms)
  - 单次阻塞读的超时（毫秒）。非法值被忽略。

- ExternalCallPolicy IdleTimeout(int ms)
  - 两次有数据到达之间的最大间隔（毫秒）；超时判定连接停滞。
    非法值被忽略。

- ExternalCallPolicy TotalTimeout(int ms)
  - 整次调用（含重试与重定向）的总预算（毫秒）。非法值被忽略。

- ExternalCallPolicy RequestLimit(int bytes)
  - 请求正文最大字节数。非法值被忽略。

- ExternalCallPolicy ResponseLimit(int bytes)
  - 缓冲式响应正文最大字节数。非法值被忽略。

- ExternalCallPolicy ErrorLimit(int bytes)
  - 错误响应体最大读取字节数。非法值被忽略。

- ExternalCallPolicy StreamLimit(int bytes)
  - 流式读取（下载）最大字节数。非法值被忽略。

- ExternalCallPolicy Redirects(int count)
  - 允许的最大重定向次数，范围 0-10；范围外被忽略。
    默认 0 表示不跟随重定向。

- ExternalCallPolicy RetryNetwork(bool enabled)
  - 是否允许网络错误重试。仅是开关；实际能否重试还须通过
    CanRetry 的幂等门禁。

- ExternalCallPolicy RetryAttempts(int count)
  - 最大重试次数，范围 0-3；范围外被忽略。

- ExternalCallPolicy RetryBackoff(int ms)
  - 重试前的退避间隔（毫秒），范围 0-30000；范围外被忽略。

- ExternalCallPolicy AllowLocalHttp()
  - 同时放行明文 HTTP 与 loopback，且限定只能访问本地 HTTP
    （远程目标仍须 HTTPS）。三个标志联动设置，用于本地开发/自托管
    服务场景；默认策略下不可用。

- ExternalCallPolicy AllowLoopback()
  - 额外放行 loopback 目标（127/8、::1）。不改变 HTTP 明文
    限制——loopback 上发 HTTPS 仍按目标 scheme 校验。

- ExternalCallPolicy Idempotent(string key)
  - 声明该调用幂等并携带幂等键（如 Idempotency-Key 头）。
    这是非 GET/HEAD 请求获得重试资格的唯一途径。key 传 null 视为
    空串（有幂等标志但无键，CanRetry 仍拒绝）。

- int ConnectTimeoutMs()
  - 已配置的连接超时（毫秒）。

- int WriteTimeoutMs()
  - 已配置的写超时（毫秒）。

- int ReadTimeoutMs()
  - 已配置的单次读超时（毫秒）。

- int IdleTimeoutMs()
  - 已配置的空闲超时（毫秒）。

- int TotalTimeoutMs()
  - 已配置的总预算（毫秒）。

- int MaxRequestBytes()
  - 已配置的请求正文上限（字节）。

- int MaxResponseBytes()
  - 已配置的缓冲式响应上限（字节）。

- int MaxErrorBytes()
  - 已配置的错误正文上限（字节）。

- int MaxStreamBytes()
  - 已配置的流式读取上限（字节）。

- int MaxBodyBytes()
  - 一次性正文（下载/图片）可使用的统一上限：取响应和
    流预算中已配置的较小值；两者都未配置时返回 0。

- int MaxRedirects()
  - 已配置的最大重定向次数。

- int RetryAttempts()
  - 已配置的最大重试次数。

- int RetryBackoffMs()
  - 已配置的重试退避（毫秒）。

- bool AllowsHttp()
  - 是否放行明文 HTTP。

- bool AllowsLoopback()
  - 是否放行 loopback 目标。

- bool LocalHttpOnly()
  - 是否限定只能访问本地 HTTP（远程仍须 HTTPS）。

- bool IsIdempotent()
  - 是否声明了幂等。

- string IdempotencyKey()
  - 已配置的幂等键（可能为空串）。

- bool CanRetry(string method, bool requestStarted, int statusCode)
  - 只有明确声明幂等，且请求尚未产生副作用时，网络错误才可
    重试。POST 没有幂等键时永远返回 false。
    重试判定门禁。requestStarted=true（请求已被对端接收，
    可能已产生副作用）一律不可重试。GET/HEAD 在连接失败（0）或
    5xx 时可重试；其余方法须已声明幂等且幂等键非空，且状态码为
    0/408/429/5xx 之一。statusCode 为 0 表示网络层失败、无响应。


## ExternalTarget (class)

已规范化的外部 HTTP(S) 目标。实例只描述目标，不执行 DNS
或连接；调用方必须在连接前对主机名的全部解析结果做地址策略检查。

- string scheme;

- string host;

- string path;

- string query;

- string canonical;

- int port;

- bool literal;

- bool safeLiteral;

- bool loopback;

- ExternalTarget()
  - 内部构造：全默认值；字段由 Parse 填充。

- static ExternalTarget Parse(string url)
  - 严格解析绝对 HTTP(S) URL。失败返回 null；不会把输入
    猜测修正为另一个 URL。拒绝项：空串、空格/控制字符、片段 "#"、
    反斜杠 "\"、userinfo "@"、百分号编码 "%"；端口限 1-65535；主机须为
    严格 IPv4/IPv6 字面量或合法 DNS 名（拒绝 localhost、.local/.localhost
    结尾、空标签、连字符位置非法）。path 恒以 "/" 开头；query 保留 "?"
    前缀原样返回，不做解码。

- string Scheme()
  - 请求协议，小写 "http" 或 "https"。

- string Host()
  - 主机名，已转小写；IPv6 字面量不含方括号。

- int Port()
  - 端口；URL 未写明时取协议默认（http=80、https=443）。

- string Path()
  - 请求路径，恒以 "/" 开头，未编码原样。

- string Query()
  - 查询串，含 "?" 前缀；无查询时为空串。

- string Canonical()
  - 规范化形式：scheme://host[:port]path[query]。IPv6 主机加
    方括号；非默认端口才附加 ":port"。可直接作为请求目标使用。

- bool IsTls()
  - 是否 https。

- bool IsLiteral()
  - 主机是否为 IP 字面量（true=不会再做 DNS 解析）。

- bool IsSafeLiteral()
  - 非字面量恒 true；字面量时表示该地址是否落在公网范围。
    内网/保留段（10/8、127/8、172.16/12、192.168/16、169.254/16、
    100.64/10、0/8、组播、IPv6 本地链路 fc00::/7、fe80::/10 等）为 false。

- bool IsLoopback()
  - 是否回环目标：127/8 IPv4 或 "::1"。

- static bool ValidText(string s)
  - URL 文本预检：非空且不含控制字符与空格。

- static int PortOf(string authority, int at)
  - 解析 authority 从 at 起的 ":端口"：1-65535 返回端口，未写端口
    返回 0，畸形返回 -1。

- static bool ValidDnsName(string h)
  - 严格 DNS 名校验：小写字母/数字/连字符，标签 1-63 字符且不以
    连字符开头/结尾，总长 ≤253；拒绝 localhost、"." 结尾和
    .localhost/.local 结尾。

- static bool ValidIPv4(string h)
  - 严格 IPv4 校验：恰好 4 段、每段 0-255、拒绝前导零。

- static int HexValue(int c)
  - 单个十六进制字符 → 0..15；非法返回 -1。

- static int ParseHex(string s)
  - 解析 1-4 位十六进制组；越界/非法返回 -1。

- static bool ValidIPv6(string h)
  - 严格 IPv6 校验（纯十六进制组形式，含一个 "::" 压缩或恰好 8 组）；
    拒绝内嵌 IPv4 尾段。空组只在属于唯一的 "::" 压缩时合法：
    "::1"（前导压缩）、"2001:db8::"（尾随压缩）此前被
    i==0 / i==len 分支整体错杀，而它们正是回环与常见形态。

- static bool IsLoopbackHost(string h)
  - 主机是否为回环地址：IPv4 第一段为 127，或精确 "::1"。

- static bool SafeLiteral(string h)
  - 判定字面量地址是否安全公网。IPv4 排除：0/8、10/8、127/8、
    224/4 及以上（组播+保留）、169.254/16、172.16/12、192.0.0/24、
    192.168/16、100.64/10、198.18/15、198.51.100/24、203.0.113/24、
    255.255.255.255。IPv6 排除：::1、::、fe80::/10、fc00::/7、ff00::/8、
    2001:db8::/32、::ffff: 映射段。


## ExternalTargetPolicy (class)

外部 HTTP(S) 目标的默认安全策略。解析目标与执行连接分离：
域名目标还必须在传输层解析全部 A/AAAA 并逐一复核，不能仅凭这里的
hostname 字符串放行。

- static bool IsAllowed(string url)
  - 默认远程策略：只允许 HTTPS，并拒绝已知危险的字面量地址。
    域名的 DNS 地址审核由传输层继续完成。

- static bool IsLocalAllowed(string url)
  - 本地开发策略：只额外放行 loopback，并仍要求调用方显式
    使用此入口；不会因为 hostname 包含 localhost 就自动放行。

- static bool IsLocalTarget(ExternalTarget t, bool allowHttp)
  - IsAllowed 的目标级判定：必须是 loopback 字面量目标；
    allowHttp 为 false 时还要求 TLS。

- static bool AllowExact(string url, string expected)
  - 按规范化目标做精确 provider allowlist 匹配。传入的
    expected 必须是固定 canonical URL，而不是用户可配置的后缀。

- static bool AllowExactLocal(string url, string expected)
  - 本地 endpoint 的精确匹配。loopback 放行只在此类显式
    helper 中发生，不会改变默认远程策略。

- static bool AllowHostPort(string url, string host, int port, bool tls)
  - 为显式 host/port allowlist 提供边界安全的比较；不会把
    evil.example.com 当成 example.com 的匹配项。

- static bool IsAllowedTarget(ExternalTarget t, bool allowLoopback, bool allowHttp)
  - 目标已解析后应用策略。allowLoopback 和 allowHttp 都必须
    由受信调用方显式传入；生产默认值均为 false。

- static bool IsPublicIPv4(string host)
  - 对已知 IPv4 字面量做 SSRF 分类。仅返回“可作为公网候选”
    的结果；域名仍必须经过 DNS 全地址检查。

- static bool IsClientAllowed(string host, int port, bool tls, bool allowLoopback, bool allowHttp, bool localHttpOnly)
  - 把已拆出的 host/port 重新组成一个严格目标并执行策略。
    这是 HttpClient 的连接前门：调用方不能只验证 hostname 字符串后再
    绕过 URL 策略。IPv6 host 由此处补上 authority 所需的方括号。

- static bool IsLoopback(string host)
  - loopback 只接受明确的 IPv4 127/8 或 ::1 字面量。
