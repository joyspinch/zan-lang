# System.Net.Sip

> 源码: `stdlib/System/Net/Sip/SipClient.zan`, `stdlib/System/Net/Sip/SipMessage.zan`


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

- async SipMessage TransactAsync(SipMessage req)


## SipMessage (class)

单条 SIP 消息（RFC 3261）：请求或响应、头部、可选正文。
这只是文本模型——事务/重传在
SipClient 中，媒体（RTP/SDP 协商）不在范围内。

- bool isRequest;

- string method;

- string uri;

- int status;

- string reason;

- List<string> headerNames;

- List<string> headerValues;

- string body;

- SipMessage()

- static SipMessage Request(string method, string uri)
  - 创建请求消息。

- SipMessage Add(string name, string headerValue)
  - 添加头部（追加；SIP 允许重复，如 Via）。

- string Header(string name)
  - 头部（名称不区分大小写）的第一个值，没有则返回 ""。

- static bool NameEq(string a, string b)

- string Serialize()
  - 序列化为线上形式（头部 + CRLF CRLF + 正文）。
    Content-Length 自动生成。

- static SipMessage Parse(string data)
  - 解析线上消息；起始行不是 SIP 时返回 null。

- static int FindChar(string s, int c, int from)

- static int FindCrlf(string s, int from)
