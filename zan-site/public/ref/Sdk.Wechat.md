# Sdk.Wechat

> 源码: `stdlib/Sdk/Wechat/CheckSignature.zan`, `stdlib/Sdk/Wechat/Message.zan`, `stdlib/Sdk/Wechat/WXBizMsgCrypt.zan`, `stdlib/Sdk/Wechat/WechatApiTransport.zan`, `stdlib/Sdk/Wechat/WechatClient.zan`, `stdlib/Sdk/Wechat/WechatCredential.zan`, `stdlib/Sdk/Wechat/WechatException.zan`, `stdlib/Sdk/Wechat/WechatMultipart.zan`, `stdlib/Sdk/Wechat/WechatRawResponse.zan`, `stdlib/Sdk/Wechat/WechatResponse.zan`, `stdlib/Sdk/Wechat/WechatTypedRequest.zan`, `stdlib/Sdk/Wechat/XmlUtil.zan`


## CheckSignature (class)

微信服务器 URL 接入时的签名校验（公众号 / 小程序服务器配置）。

规则与官方一致：把 token、timestamp、nonce 三个字符串按字典序排序后拼接，
做 SHA-1，得到 40 位小写十六进制；与微信传来的 signature 比对。

直接使用 `Sha1` / `Hex`，不自造摘要实现。

- public static string DefaultToken="weixin";
  - 默认 Token；生产环境应传入自己的 Token。

- static bool Check(string signature, string timestamp, string nonce, string token)
  - 检查 signature 是否与 token/timestamp/nonce 匹配。

- static string GetSignature(string timestamp, string nonce, string token)
  - 计算签名。token 为空时回落到 `DefaultToken`。

- static string JoinSorted(string a, string b, string c)
  - 把三个字符串按字典序（ordinal）排序后拼接。数量固定为 3，插入排序即可。


## WXBizMsgCrypt (class)

微信消息加解密（公众号 / 企业微信兼容的 Tencent 方案）。

算法要点（官方文档 + Senparc.Weixin.Tencent.Cryptography）：
1. EncodingAESKey 是 43 字符的 Base64（缺一个 '='），解码得 32 字节 AES-256 密钥；
2. IV = Key 的前 16 字节；
3. 明文结构：16 字节随机 + 4 字节网络序 msg 长度 + msg + appId；
4. 填充是 **32 字节块** 的 PKCS#7（不是标准 AES 的 16）——因此这里用
`Aes.EncryptBlockEcb` 自拼 CBC，不走会自动 16 字节补位的 EncryptCbc；
5. 密文签名：token/timestamp/nonce/encrypt 四串字典序拼接后 SHA-1。

加解密原语全部来自 <c>System.Security.Cryptography</c>，本类只编排微信协议。

- string token;

- string appId;

- byte[]aesKey;

- byte[]aesIv;

- public WXBizMsgCrypt(string token, string encodingAesKey, string appId)
  - <paramref 名称="encodingAesKey"/> 必须是公众平台上配置的 43 字符 EncodingAESKey。

- string DecryptMsg(string msgSignature, string timestamp, string nonce, string postData)
  - 校验并解密微信推送的加密 XML，返回明文 XML。
    签名错误 / AES 失败 / appId 不匹配一律抛 `WechatException`。

- string EncryptMsg(string replyMsg, string timestamp, string nonce)
  - 加密回复 XML，返回可直接写给微信的加密 XML
    （含 Encrypt / MsgSignature / TimeStamp / Nonce）。

- static string MsgSignature(string token, string timestamp, string nonce, string encrypt)
  - 四参数消息签名：token、timestamp、nonce、encrypt 字典序拼接后 SHA-1 小写 hex。

- static string JoinSorted4(string a, string b, string c, string d)
  - 四个字符串字典序排序后拼接。

- WXBizPlain AesDecrypt(string cipherB64)
  - AES-256-CBC 解密 Base64 密文，按 32 字节 PKCS#7 去垫，拆出 msg 与 appId。

- string AesEncrypt(string plain)
  - 构造 16 随机 + 4 长度 + msg + appId，32 字节 PKCS#7 补位后 AES-CBC 加密并 Base64。

- static byte[]CbcEncryptNoPad(string key, int keyLen, byte[]iv, byte[]data, int len)
  - AES-CBC 加密，不做额外填充（调用方保证 len % 16 == 0）。

- static byte[]CbcDecryptNoPad(string key, int keyLen, byte[]iv, byte[]data, int len)
  - AES-CBC 解密，不去垫（调用方处理 32 字节 PKCS#7）。

- static string BytesToString(byte[]raw, int off, int len)
  - 把 raw 字节缓冲的一段解释为 Latin-1/UTF-8 字节透传字符串（微信 XML 按字节）。

- static string RandAscii(int n)
  - 生成 n 字节可打印随机前缀（与官方 CreateRandCode 字符集一致）。
    失败时退回固定可重复填充（仍满足长度），加密结果可解密。


## WXBizPlain (class)

AES 解密后的明文消息 + 尾部 appId。

- public string Msg;

- public string AppId;

- public WXBizPlain()


## WechatApiTransport (class)

微信各产品线共用的 HTTPS 传输层。支持 JSON、XML、multipart、二进制响应、
任意请求头以及需要 PEM 客户端证书的双向 TLS。

- string host;

- int port;

- int timeoutMs;

- string clientCertificateFile;

- string clientPrivateKeyFile;

- public WechatApiTransport(string host)

- WechatApiTransport Server(string host, int port)

- WechatApiTransport Timeout(int ms)

- WechatApiTransport SetClientCertificate(string certFile, string keyFile)

- static string BuildPath(string path, string query)

- static string AppendQuery(string path, string name, string tokenValue)

- async WechatRawResponse RequestRawAsync(string method, string path, string body, string contentType, List<string> headers)

- async WechatRawResponse RequestSimpleRawAsync(string method, string path, string body, string contentType)

- async WechatResponse RequestJsonAsync(string method, string path, string body)

- async WechatRawResponse RequestMultipartAsync(string method, string path, WechatMultipart multipart)


## WechatClient (class)

微信公众号（MP）客户端。

用法：

WechatClient client = new WechatClient("appId", "appSecret");
string token = await client.GetAccessTokenAsync();
WechatResponse r = await client.SendTextAsync("openid", "hello");


AccessToken 在实例内缓存：未过期直接返回，过期才重新请求
<c>/cgi-bin/token</c>。客服消息走 <c>/cgi-bin/message/custom/send</c>。

依赖标准库：
- `HttpClient` 发 HTTPS；
- `JsonValue` 组请求 / 解析响应；
- `Encoding.UrlEncode` 拼 query；
不重复封装 HTTP / 摘要 / JSON。

相对 Senparc 原版的改动：
- 去掉 AccessTokenContainer / DI / 中间件，一个实例管一个公众号；
- 失败一律抛 `WechatException`，不返回带 errcode 的空结果；
- Token 过期提前 5 分钟刷新（微信官方有效期 7200 秒）。

- static string DEFAULT_HOST="api.weixin.qq.com";

- static int DEFAULT_PORT=443;

- static int TOKEN_SKEW_SECONDS=300;

- string appId;

- string appSecret;

- string host;

- int port;

- int timeoutMs;

- string accessToken;

- long accessTokenExpireUnix;

- public WechatClient(string appId, string appSecret)
  - 用 appId / appSecret 创建客户端，默认指向正式网关。

- WechatClient Server(string host, int port)
  - 指向另一个网关（沙箱 / 自建代理）。

- WechatClient Timeout(int ms)
  - 请求超时（毫秒），默认 30000。

- WechatClient SetAccessToken(string token, int expiresIn)
  - 手动注入已有 access_token（例如从外部缓存读入），并设置剩余有效秒数。
    expiresIn 按微信返回的 expires_in 理解；内部会扣掉 TOKEN_SKEW_SECONDS。

- async string GetAccessTokenAsync()
  - 获取可用的 access_token。缓存未过期则直接返回；否则请求
    <c>GET /cgi-bin/token?grant_type=client_credential&appid=...&secret=...</c>。

- async WechatResponse SendTextAsync(string openId, string content)
  - 发送客服文本消息：
    <c>POST /cgi-bin/message/custom/send?access_token=...</c>，
    body 为 <c>{"touser":"...","msgtype":"text","text":{"content":"..."}}</c>。

- async WechatResponse GetAsync(string path)
  - 通用 GET：路径 已含 query（可含或不含 access_token）。
    需要 token 时先调 `GetAccessTokenAsync` 自行拼进 path。

- async WechatResponse PostJsonAsync(string path, string jsonBody)
  - 通用 JSON POST。自动带 <c>Content-Type: application/json</c>。

- async WechatResponse GetWithTokenAsync(string path)
  - 自动附带 access_token 的 GET（路径 不含 token，以 ? 或 & 开头皆可）。

- async WechatResponse PostJsonWithTokenAsync(string path, string jsonBody)
  - 自动附带 access_token 的 JSON POST。

- string WithToken(string path, string token)

- async string CredentialAsync(WechatCredentialKind kind)

- async WechatRawResponse RequestRawAsync(string method, string path, string query, string body, string contentType, WechatCredentialKind credential)

- async WechatRawResponse RequestMultipartAsync(string method, string path, string query, WechatMultipart multipart, WechatCredentialKind credential)

- async WechatResponse CreateMenuAsync(string menuJson)
  - 创建自定义菜单。body 为完整 JSON，如 <c>{"button":[...]}</c>。

- async WechatResponse GetMenuAsync()
  - 查询当前自定义菜单。

- async WechatResponse DeleteMenuAsync()
  - 删除全部自定义菜单。

- async WechatResponse GetUserInfoAsync(string openId, string lang)
  - 获取用户基本信息（UnionID 机制）。

- async WechatResponse GetUserListAsync(string nextOpenId)
  - 获取帐号的关注者列表。nextOpenId 空串表示从头拉取。

- async WechatResponse CreateQrCodeAsync(string actionName, int expireSeconds, string sceneJson)
  - 创建二维码票据。actionName 如 <c>QR_SCENE</c> / <c>QR_LIMIT_STR_SCENE</c>；
    sceneJson 是 action_info.scene 对象，例如 <c>{"scene_str":"test"}</c>。

- async WechatResponse GetTicketAsync(string type)
  - 获取 jsapi_ticket（type=jsapi）或 wx_card ticket。

- async WechatResponse SendTemplateAsync(string openId, string templateId, string url, string dataJson)
  - 发送模板消息。dataJson 是 数据 对象，例如
    <c>{"first":{"value":"hi","color":"#173177"}}</c>。

- async WechatResponse OAuthAccessTokenAsync(string code)
  - 用 code 换网页授权 access_token（sns/oauth2）。
    不走公众号 access_token 缓存——这是用户授权 token。

- async WechatResponse OAuthRefreshTokenAsync(string refreshToken)
  - 刷新网页授权 access_token。

- async WechatResponse OAuthUserInfoAsync(string oauthAccessToken, string openId, string lang)
  - 拉取用户信息（需 snsapi_userinfo 授权）。

- string BuildOAuthUrl(string redirectUri, string scope, string state)
  - 拼网页授权跳转 URL（开在浏览器里让用户点）。
    scope 通常 <c>snsapi_base</c> 或 <c>snsapi_userinfo</c>。

- WechatApiTransport NewTransport()

- HttpClient NewHttp()


## WechatCredential (class)

- static string QueryName(WechatCredentialKind kind)


## WechatException (class)

微信公众平台 API 调用失败时抛出的异常。

与 Senparc 原版不同：原版大量接口把 errcode 塞进返回对象、靠调用方检查；
这里一律抛异常，失败无法被静默忽略。本地校验失败时 Code 为
"client.paramError" / "client.badResponse" / "client.networkError"。

- public string Code;
  - 微信 errcode 字符串，或本地错误码。

- public string Body;
  - 原始响应报文；本地失败时为空串。

- public WechatException(string code, string message, string body)


## WechatMultipart (class)

二进制安全的 multipart/form-data 请求构建器。

- string boundary;

- StringBuilder content;

- bool closed;

- public WechatMultipart()

- string ContentType()

- WechatMultipart AddField(string name, string fieldValue)

- WechatMultipart AddFile(string name, string fileName, string mediaType, string bytes)

- string Body()

- void EnsureOpen()


## WechatRawResponse (class)

媒体、账单、multipart 及支付 API 使用的二进制安全 HTTP 响应。

- int StatusCode;

- string StatusText;

- string ContentType;

- string Body;

- List<string> Headers;

- WechatRawResponse(int statusCode, string statusText, string contentType, string body, List<string> headers)

- static WechatRawResponse FromHttp(HttpResponse response)

- bool IsSuccess()

- string Header(string name)

- void SaveBody(string path)

- WechatResponse Json()

- static bool AsciiEquals(string a, string b)

- static int IndexOf(string hay, string needle, int from)


## WechatReply (class)

被动回复消息构造器。角色对调：ToUserName = 请求的 FromUserName。

- static string Text(string toUser, string fromUser, string content)
  - 构造文本回复 XML。

- static string Image(string toUser, string fromUser, string mediaId)
  - 构造图片回复 XML。

- static string NewsItem(string title, string description, string picUrl, string url)
  - 一条图文。多条时自行拼 项目 后调用
    <c>News(to, from, count, itemsXml)</c>。

- static string News(string toUser, string fromUser, int count, string itemsXml)

- static string Build(string toUser, string fromUser, string msgType, string body)


## WechatRequestMessage (class)

微信服务器推送的一条请求消息（明文 XML 解析结果）。

字段覆盖最常用的文本 / 图片 / 事件；其余类型仍可通过
`Raw` + `XmlUtil.GetTag` 自行取。
不照搬 Senparc 那套巨大的 RequestMessage* 继承树。

- public string ToUserName;

- public string FromUserName;

- public string CreateTime;

- public string MsgType;

- public string Content;

- public string MsgId;

- public string PicUrl;

- public string MediaId;

- public string Event;

- public string EventKey;

- public string Ticket;

- public string Latitude;

- public string Longitude;

- public string Precision;

- public string Raw;

- public WechatRequestMessage()

- static WechatRequestMessage Parse(string xml)
  - 从明文 XML 解析。

- bool IsText()

- bool IsImage()

- bool IsEvent()

- bool IsSubscribe()

- bool IsUnsubscribe()

- bool IsClick()

- bool IsScan()


## WechatResponse (class)

一次成功调用的返回结果。

微信多数接口返回扁平 JSON（token 接口是 access_token/expires_in；
业务接口是 errcode/errmsg + 业务字段）。本类不预置业务模型——
成功时 <c>Data</c> 是完整 JSON 文本，用 <c>Json.Deserialize<T></c> 绑定即可。

失败不会走到这里：errcode 非 0 时直接抛 `WechatException`。

- public string Data;
  - 完整响应 JSON 文本，可直接喂 Json.Deserialize。

- public string Raw;
  - 网关返回的完整原始报文（与 Data 相同，保留字段名以对齐 JdResponse）。

- public JsonValue Value;
  - 已解析的响应树，需要动态取值时用。

- static string lastErrCode="";

- static string lastErrMsg="";

- WechatResponse(string data, string raw, JsonValue v)

- static WechatResponse Parse(string body)
  - 解析微信响应。errcode 缺失或为 0 视为成功；其它一律抛 WechatException。
    
    实现上分两步：Inspect 先判定并释放 Json 树，失败原因走 static，
    返回后再 throw，避免 throw 泄漏活着的对象。

- static WechatResponse Inspect(string body)

- static WechatResponse Failure(string code, string msg)

- string Str(string key, string dflt)
  - 从已解析结果中读字符串字段。

- int Int(string key, int dflt)
  - 从已解析结果中读整型字段。


## WechatTypedRequest (class)

内部 JSON/query 累加器，供生成的类型化请求使用。

- JsonValue body;

- JsonValue query;

- JsonValue path;

- public WechatTypedRequest()

- WechatTypedRequest BodyString(string key, string fieldValue)

- WechatTypedRequest BodyInt(string key, int fieldValue)

- WechatTypedRequest BodyLong(string key, long fieldValue)

- WechatTypedRequest BodyDouble(string key, double fieldValue)

- WechatTypedRequest BodyBool(string key, bool fieldValue)

- WechatTypedRequest BodyStrings(string key, List<string> items)

- WechatTypedRequest BodyInts(string key, List<int> items)

- WechatTypedRequest BodyLongs(string key, List<long> items)

- WechatTypedRequest BodyDoubles(string key, List<double> items)

- WechatTypedRequest BodyBools(string key, List<bool> items)

- WechatTypedRequest BodyValue(string key, JsonValue fieldValue)

- WechatTypedRequest BodyJson(string key, string json)

- WechatTypedRequest PathString(string key, string fieldValue)

- WechatTypedRequest PathInt(string key, int fieldValue)

- WechatTypedRequest PathLong(string key, long fieldValue)

- WechatTypedRequest PathDouble(string key, double fieldValue)

- WechatTypedRequest PathBool(string key, bool fieldValue)

- WechatTypedRequest PathValue(string key, JsonValue fieldValue)

- bool HasPath(string key)

- bool HasValue(string key)

- string PathText(string key)

- string ValueText(string key)

- WechatTypedRequest QueryString(string key, string fieldValue)

- WechatTypedRequest QueryInt(string key, int fieldValue)

- WechatTypedRequest QueryLong(string key, long fieldValue)

- WechatTypedRequest QueryDouble(string key, double fieldValue)

- WechatTypedRequest QueryBool(string key, bool fieldValue)

- WechatTypedRequest QueryStrings(string key, List<string> items)

- WechatTypedRequest QueryInts(string key, List<int> items)

- WechatTypedRequest QueryLongs(string key, List<long> items)

- WechatTypedRequest QueryDoubles(string key, List<double> items)

- WechatTypedRequest QueryBools(string key, List<bool> items)

- WechatTypedRequest QueryValue(string key, JsonValue fieldValue)

- WechatTypedRequest QueryJson(string key, string json)

- string JsonBody()

- string QueryText()


## XmlUtil (class)

微信推送/回复 XML 的极简读写。

不引入完整 XML 解析器：微信报文结构固定、字段值几乎都包在
<c><![CDATA[...]]></c> 里，用标签扫描足够。
需要完整 DOM 时再换 System 层解析器，这里不做第二套 XML 库。

- static string GetTag(string xml, string tag)
  - 取 <paramref name="xml"/> 中第一个 <c><tag>...</tag></c> 的文本内容；
    支持 CDATA。找不到返回空串。

- static string StripCdata(string s)
  - 去掉一层 CDATA 包装；没有则 Trim 空白后返回。

- static string CdataTag(string tag, string text)
  - 拼一个带 CDATA 的元素。

- static string TextTag(string tag, string text)
  - 拼一个纯文本元素（CreateTime 等数字字段）。

- static string Trim(string s)

- static int IndexOf(string hay, string needle, int from)
  - 子串查找，返回起始下标，找不到 -1。不用依赖可能缺失的 IndexOf 重载。


## WechatCredentialKind (enum)

内部认证策略，由生成的端点元数据选择。

- None

- AccessToken

- ProviderAccessToken

- SuiteAccessToken

- ComponentAccessToken

- AuthorizerAccessToken
