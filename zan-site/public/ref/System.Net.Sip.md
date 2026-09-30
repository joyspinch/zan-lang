# System.Net.Sip

> 源码: `packages/Zan.Net/src/System/Net/Sip/SipClient.zan`, `packages/Zan.Net/src/System/Net/Sip/SipMessage.zan`


## SipClient (class)

基于 UDP 的 SIP user-agent 客户端（RFC 3261）——仅信令侧。

范围：非 INVITE 客户端事务（OPTIONS 保活/能力探测、
REGISTER），带 Timer E/F 重传（500ms 翻倍至 4s，总计 32s）。
未实现 Digest 认证、INVITE 会话和 RTP 媒体；
401/407 响应原样返回给调用方，由应用
决定如何处理。

用法：
SipClient c = new SipClient("sip.example.com", 5060, "zan-ua");
SipMessage r = await c.OptionsAsync();          // null on timeout
if (r != null && r.status == 200) { ... }
SipMessage reg = await c.RegisterAsync("alice", 3600);

- string host;

- int port;

- string localTag;

- int cseq;

- SipClient(string host, int port, string tag)
  - 为单个 SIP 服务器/代理创建客户端。

- async SipMessage OptionsAsync()
  - 发送 OPTIONS 并等待最终响应——标准的
    "服务器是否存活 / 支持什么"探测。超时返回 null。

- async SipMessage RegisterAsync(string user, int expires)
  - 为 <paramref name="user"/> 发送 REGISTER，使用给定的
    过期时间。响应原样返回：401/407 携带
    WWW-Authenticate 质询交由调用方处理。超时返回 null。

- void AddCommon(SipMessage req, string aor, string method)
  - 填入每个请求都必须包含的头部（RFC 3261 8.1.1）：
    Via（随机 branch）、Max-Forwards、From/To、Call-ID、CSeq。

- async SipMessage TransactAsync(SipMessage req)
  - 非 INVITE 客户端事务：500ms 起重传并翻倍至 4s，
    32s 后放弃（Timer E/F）。临时 1xx 响应不重启计时器、直接跳过；
    首个最终（>=200）响应生效。超时返回 null。


## SipHeader (class)

SIP 头部实体记录（键/值对）。

- string name;

- string value;

- SipHeader(string name, string value)


## SipMessage (class)

单条 SIP 消息（RFC 3261）：请求或响应、头部、可选正文。
这只是文本模型——事务/重传在
SipClient 中，媒体（RTP/SDP 协商）不在范围内。

- bool isRequest;

- string method;

- string uri;

- int status;

- string reason;

- List<SipHeader> headers;

- string body;

- SipMessage()
  - 私有构造：空消息，由 `Request` / `Parse` 填充。

- static SipMessage Request(string method, string uri)
  - 创建请求消息。

- public bool GetIsRequest()

- public string GetMethod()

- public string GetUri()

- public int GetStatus()

- public string GetReason()

- public string GetBody()

- SipMessage Add(string name, string headerValue)
  - 添加头部（追加；SIP 允许重复，如 Via）。

- string Header(string name)
  - 头部（名称不区分大小写）的第一个值，没有则返回 ""。

- static bool NameEq(string a, string b)
  - 头部名 ASCII 大小写不敏感相等比较。

- string Serialize()
  - 序列化为线上形式（头部 + CRLF CRLF + 正文）。
    Content-Length 自动生成。

- static int ParsePositiveInt(string s)
  - 安全解析十进制非负整数，非法字符或空串返回 -1。

- static SipMessage Parse(string data)
  - 解析线上消息；起始行不是 SIP 时返回 null。

- static int FindChar(string s, int c, int from)
  - 从 <paramref name="from"/> 起第一个字节 <paramref name="c"/> 的下标，
    不存在时返回 -1。

- static int FindCrlf(string s, int from)
  - 从 <paramref name="from"/> 起第一个 CRLF 的下标，
    不存在时返回 -1。
