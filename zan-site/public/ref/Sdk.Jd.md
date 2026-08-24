# Sdk.Jd

> 源码: `stdlib/Sdk/Jd/JdClient.zan`, `stdlib/Sdk/Jd/JdException.zan`, `stdlib/Sdk/Jd/JdRequest.zan`, `stdlib/Sdk/Jd/JdResponse.zan`, `stdlib/Sdk/Jd/JdSign.zan`


## JdClient (class)

京东开放平台（JOS）客户端。

用法（一行可用）：

JdClient client = new JdClient("your-app-key", "your-app-secret");
client.SetAccessToken("oauth-token");

JdRequest req = new JdRequest("jingdong.area.province.get");
JdResponse resp = await client.ExecuteAsync(req);
ProvinceResult r = Json.Deserialize<ProvinceResult>(resp.Data);


可选配置一律链式，均有合理默认值：
client.Timeout(15000).MaxRetries(3).TimeZoneOffset(480)
.Server("api.jd.com", 443, "/routerjson", true);

相对 C# 原版的改动（原版设计缺陷，未照搬）：
- 原版 <c>goto retry</c> + <c>Thread.Sleep</c> 阻塞整个线程重试
→ 这里用 <c>await Task.Delay</c> 指数退避，真正挂起到 reactor；
- 原版把校验/网关错误吞成一个字段被填好的空 Response 返回
→ 这里一律抛 `JdException`，失败无法被静默忽略；
- 原版残留 XML 分支和自造 JdDictionary → 删除，只留 JSON + 标准 Dictionary；
- 原版手拼签名与业务 JSON 字符串 → 业务参数走 JsonValue，签名走 JdSign。

线程/协程安全：单个实例的配置在构造后只读，可被多个协程并发用于
<c>ExecuteAsync</c>；<c>SetAccessToken</c> 等 setter 应在并发调用前完成。

- static string PARAM_JSON="360buy_param_json";

- static string PARAM_METHOD="method";

- static string PARAM_APP_KEY="app_key";

- static string PARAM_VERSION="v";

- static string PARAM_TIMESTAMP="timestamp";

- static string PARAM_TOKEN="access_token";

- static string PARAM_SIGN="sign";

- string appKey;

- string appSecret;

- string accessToken;

- string host;

- int port;

- string path;

- bool useTls;

- int timeoutMs;

- int maxRetries;

- int tzOffsetMinutes;

- public JdClient(string appKey, string appSecret)
  - 用 appKey / appSecret 创建客户端，默认指向正式网关
    <c>https://api.jd.com/routerjson</c>。

- JdClient SetAccessToken(string token)
  - 设置 OAuth access_token（调用需授权的接口时必填）。

- JdClient Server(string host, int port, string path, bool useTls)
  - 指向另一个网关地址（沙箱 / 自建代理）。

- JdClient TimeZoneOffset(int minutes)
  - 时间戳使用的时区偏移（分钟），默认 480（<c>+0800</c>，京东网关所在时区）。
    例：印度 <c>TimeZoneOffset(330)</c>（+0530）、UTC <c>TimeZoneOffset(0)</c>。
    
    之所以要显式配置而不是读系统时区：<c>DateTime.Now()</c> 是
    <c>DateTime.UtcNow()</c> 的别名，标准库不做本地时区换算，
    直接用它会给每个请求打上 UTC 时间戳，在 <c>+0800</c> 的机器上差 8 小时。

- JdClient Timeout(int ms)
  - 请求超时（毫秒），默认 30000。

- JdClient MaxRetries(int n)
  - 网关限流（错误码 66）时的最大重试次数，默认 3；设 0 关闭重试。
    退避是 <c>await Task.Delay</c>，不占线程。

- async JdResponse ExecuteAsync(JdRequest request)
  - 执行一次调用。成功返回已剥掉信封的 `JdResponse`；
    任何失败（本地校验、网络、网关错误码、业务错误码）都抛
    `JdException`。

- HttpClient NewHttp()

- string BuildBody(JdRequest request)
  - 组装协议参数、算签名，编码成 form-urlencoded 请求体。

- string Timestamp()
  - 京东要求的时间戳格式：<c>yyyy-MM-dd HH:mm:ss+0800</c>。
    墙钟时间由 UTC 加上 `TimeZoneOffset` 得到，后缀就是同一个偏移，
    两者永远自洽（原版 C# 取本机时区，这里改成显式配置，见 TimeZoneOffset）。
    精度到秒；原版多带的 <c>.fff</c> 网关不要求，标准库 DateTime 也只到秒。

- static string FormatTimestamp(long unixSeconds, int offsetMinutes)
  - 把 Unix 秒按给定分钟偏移格式化成网关要求的时间戳。

- static string Pad2(int n)

- static string FormEncode(Dictionary <string, string> p)
  - 把参数字典编码成 application/x-www-form-urlencoded 请求体。


## JdException (class)

京东开放平台（JOS）调用失败时抛出的异常。

与 C# 原版不同：原版把参数校验失败和网关错误"吞"成一个字段被填好的
空 Response 对象返回，调用方不检查 IsError 就会拿着假数据继续跑。
Zan 版一律抛异常，失败无法被静默忽略。

- public string Code;
  - 网关或业务错误码，本地校验失败时为 "client.paramError"。

- public string Body;
  - 原始响应报文，便于排查；本地失败时为空串。

- public JdException(string code, string message, string body)


## JdRequest (class)

一次京东开放平台调用的请求描述：API 名 + 业务参数。

与 C# 原版每个接口一个 Request 类（211 个纯样板）不同，这里只有一个类：
API 名用字符串给出，参数用链式 Set 累积。业务参数一律走 JsonValue 构建，
不做字符串拼接——值里带引号、换行、中文都不会拼出非法 JSON。


JdRequest req = new JdRequest("jingdong.area.province.get");
req.Set("parentId", 0).Set("keyword", "北京");


空值语义与官方一致：Set 传入空串会被忽略（京东网关不接受空参数）。

- public string ApiName;
  - API 方法名，如 "jingdong.area.province.获取"。

- public string ApiVersion;
  - API 版本号，默认 "2.0"。JOS 接口不区分版本，开普勒接口需要。

- JsonValue param;

- public JdRequest(string apiName)

- JdRequest Set(string key, string paramValue)
  - 设置一个字符串业务参数；空串被忽略。返回自身以便链式调用。

- JdRequest SetInt(string key, int paramValue)
  - 设置一个整数业务参数。

- JdRequest SetLong(string key, long paramValue)
  - 设置一个 64 位整数业务参数（京东的 wareId / skuId / orderId 都是大数，
    不能走 int 通道被截断）。

- JdRequest SetBool(string key, bool paramValue)
  - 设置一个布尔业务参数。

- JdRequest SetValue(string key, JsonValue paramValue)
  - 设置一个任意结构的业务参数（嵌套对象 / 数组）。
    复杂入参用 <c>Json.Serialize<T></c> 得到文本后 JsonValue.Parse 传入，
    或直接构造 JsonValue 树。

- string ParamJson()
  - 业务参数序列化成 360buy_param_json 的取值。


## JdResponse (class)

一次成功调用的返回结果。

京东网关的响应是一层信封：
`{"jingdong_area_province_get_responce": { ...业务数据... }}`
本类已把信封剥掉：<c>Data</c> 是业务数据本身，<c>Raw</c> 保留原始报文。

与 C# 原版每个接口一个 Response 类（211 个纯样板）不同，这里不预置任何
业务模型——用编译期绑定把 <c>Data</c> 转成你自己的类即可：

JdResponse resp = await client.ExecuteAsync(req);
MyResult r = Json.Deserialize<MyResult>(resp.Data);


失败不会走到这里：网关返回错误码时 JdClient 直接抛 `JdException`。

- public string Data;
  - 剥掉信封后的业务数据 JSON 文本，可直接喂 Json.Deserialize。

- public string Raw;
  - 网关返回的完整原始报文。

- public JsonValue Value;
  - 已解析的业务数据树，需要动态取值时用。

- static string lastErrCode="";

- static string lastErrMsg="";

- JdResponse(string data, string raw, JsonValue v)

- static JdResponse Parse(string body)
  - 解析网关响应：剥信封、识别错误。
    
    错误有两种形态，都抛 JdException：
    1. <c>{"error_response":{"code":"...","zh_desc":"..."}}</c> 网关级错误；
    2. 信封内业务数据带非 "0" 的 code 字段。
    
    实现上分两步：先在 Inspect 里把结论（错误码 + 消息，或成功结果）取出来，
    返回后才抛异常。原因是 <c>throw</c> 不会释放当前帧里活着的对象
    （见 docs/bugs/throw-leaks-live-locals.md），若在持有 JsonValue 树或
    结果对象时直接抛，它们都会泄漏。所以失败原因走 static 字段传出，
    抛异常那一刻本帧没有任何活跃分配。

- static JdResponse Inspect(string body)
  - 解析并判定结果，不抛异常：成功返回填好 Data/Raw/Value 的实例，
    失败返回 null 并把原因写入 lastErrCode / lastErrMsg。JsonValue 树在本
    方法返回时按正常路径释放。

- static JdResponse Failure(string code, string msg)

- static string Field(JsonValue obj, string key, string dflt)


## JdSign (class)

京东开放平台的协议参数容器：签名前的系统级参数 + 业务参数 JSON。

与 C# 原版的 JdDictionary 不同，这里不自造字典类型——直接用标准
Dictionary<string,string>，只把"京东特有的排序拼接签名规则"作为
静态方法留在本类里。

- static string Compute(Dictionary <string, string> parameters, string secret)
  - 按京东规则计算 MD5 签名：参数按 键 升序拼成 "秘钥+k1v1k2v2...+秘钥"，
    取 MD5 的大写十六进制。空 键 / 空值不参与签名。

- static List<string> SortedKeys(Dictionary <string, string> parameters)
  - 字典的 键 升序列表。Dictionary 保持插入序，签名必须自己排序。
    数量恒定在十几个，用插入排序即可（规范禁止冒泡）。
