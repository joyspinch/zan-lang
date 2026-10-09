# Sdk.Wechat

> 源码: `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/CheckSignature.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Message.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/WXBizMsgCrypt.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/WechatApiTransport.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/WechatClient.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/WechatCredential.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/WechatException.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/WechatMultipart.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/WechatRawResponse.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/WechatResponse.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/WechatTypedRequest.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/XmlUtil.zan`


## CheckSignature (class)

- public static string DefaultToken="weixin";

- static bool Check(string signature, string timestamp, string nonce, string token)

- static string GetSignature(string timestamp, string nonce, string token)

- static string JoinSorted(string a, string b, string c)


## WXBizMsgCrypt (class)

- string token;

- string appId;

- byte[]aesKey;

- byte[]aesIv;

- public WXBizMsgCrypt(string token, string encodingAesKey, string appId)

- string DecryptMsg(string msgSignature, string timestamp, string nonce, string postData)

- string EncryptMsg(string replyMsg, string timestamp, string nonce)

- static string MsgSignature(string token, string timestamp, string nonce, string encrypt)

- static string JoinSorted4(string a, string b, string c, string d)

- WXBizPlain AesDecrypt(string cipherB64)

- string AesEncrypt(string plain)

- static byte[]CbcEncryptNoPad(string key, int keyLen, byte[]iv, byte[]data, int len)

- static byte[]CbcDecryptNoPad(string key, int keyLen, byte[]iv, byte[]data, int len)

- static string BytesToString(byte[]raw, int off, int len)

- static string RandAscii(int n)


## WXBizPlain (class)

- public string Msg;

- public string AppId;

- public WXBizPlain()


## WechatApiTransport (class)

- string host;

- int port;

- int timeoutMs;

- string clientCertificateFile;

- string clientPrivateKeyFile;

- ExternalCallPolicy callPolicy;

- public WechatApiTransport(string host)

- WechatApiTransport Server(string host, int port)

- WechatApiTransport Timeout(int ms)

- WechatApiTransport SetClientCertificate(string certFile, string keyFile)

- WechatApiTransport SetCallPolicy(ExternalCallPolicy policy)

- ExternalCallPolicy CallPolicy()

- static string BuildPath(string path, string query)

- static string AppendQuery(string path, string name, string tokenValue)

- async WechatRawResponse RequestRawAsync(string method, string path, string body, string contentType, List<string> headers)

- async WechatRawResponse RequestSimpleRawAsync(string method, string path, string body, string contentType)

- async WechatResponse RequestJsonAsync(string method, string path, string body)

- async WechatRawResponse RequestMultipartAsync(string method, string path, WechatMultipart multipart)


## WechatClient (class)

- static string DEFAULT_HOST="api.weixin.qq.com";

- static int DEFAULT_PORT=443;

- static int TOKEN_SKEW_SECONDS=300;

- string appId;

- string appSecret;

- string host;

- int port;

- int timeoutMs;

- ExternalCallPolicy callPolicy;

- string accessToken;

- long accessTokenExpireUnix;

- public WechatClient(string appId, string appSecret)

- WechatClient Server(string host, int port)

- WechatClient Timeout(int ms)

- WechatClient SetCallPolicy(ExternalCallPolicy policy)

- ExternalCallPolicy CallPolicy()

- WechatClient SetAccessToken(string token, int expiresIn)

- async string GetAccessTokenAsync()

- async WechatResponse SendTextAsync(string openId, string content)

- async WechatResponse GetAsync(string path)

- async WechatResponse PostJsonAsync(string path, string jsonBody)

- async WechatResponse GetWithTokenAsync(string path)

- async WechatResponse PostJsonWithTokenAsync(string path, string jsonBody)

- string WithToken(string path, string token)

- async string CredentialAsync(WechatCredentialKind kind)

- async WechatRawResponse RequestRawAsync(string method, string path, string query, string body, string contentType, WechatCredentialKind credential)

- async WechatRawResponse RequestMultipartAsync(string method, string path, string query, WechatMultipart multipart, WechatCredentialKind credential)

- async WechatResponse CreateMenuAsync(string menuJson)

- async WechatResponse GetMenuAsync()

- async WechatResponse DeleteMenuAsync()

- async WechatResponse GetUserInfoAsync(string openId, string lang)

- async WechatResponse GetUserListAsync(string nextOpenId)

- async WechatResponse CreateQrCodeAsync(string actionName, int expireSeconds, string sceneJson)

- async WechatResponse GetTicketAsync(string type)

- async WechatResponse SendTemplateAsync(string openId, string templateId, string url, string dataJson)

- async WechatResponse OAuthAccessTokenAsync(string code)

- async WechatResponse OAuthRefreshTokenAsync(string refreshToken)

- async WechatResponse OAuthUserInfoAsync(string oauthAccessToken, string openId, string lang)

- string BuildOAuthUrl(string redirectUri, string scope, string state)

- WechatApiTransport NewTransport()

- HttpClient NewHttp()


## WechatCredential (class)

- static string QueryName(WechatCredentialKind kind)


## WechatException (class)

- public string Code;

- public string Body;

- public WechatException(string code, string message, string body)


## WechatMultipart (class)

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

- static string Text(string toUser, string fromUser, string content)

- static string Image(string toUser, string fromUser, string mediaId)

- static string NewsItem(string title, string description, string picUrl, string url)

- static string News(string toUser, string fromUser, int count, string itemsXml)

- static string Build(string toUser, string fromUser, string msgType, string body)


## WechatRequestMessage (class)

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

- bool IsText()

- bool IsImage()

- bool IsEvent()

- bool IsSubscribe()

- bool IsUnsubscribe()

- bool IsClick()

- bool IsScan()


## WechatResponse (class)

- public string Data;

- public string Raw;

- public JsonValue Value;

- static string lastErrCode="";

- static string lastErrMsg="";

- WechatResponse(string data, string raw, JsonValue v)

- static WechatResponse Parse(string body)

- static WechatResponse Inspect(string body)

- static WechatResponse Failure(string code, string msg)

- string Str(string key, string dflt)

- int Int(string key, int dflt)


## WechatTypedRequest (class)

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

- static string GetTag(string xml, string tag)

- static string StripCdata(string s)

- static string CdataTag(string tag, string text)

- static string TextTag(string tag, string text)

- static string Trim(string s)

- static int IndexOf(string hay, string needle, int from)


## WechatCredentialKind (enum)

- None

- AccessToken

- ProviderAccessToken

- SuiteAccessToken

- ComponentAccessToken

- AuthorizerAccessToken
