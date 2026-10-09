# Sdk.Jd

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/JdClient.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/JdException.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/JdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/JdResponse.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/JdSign.zan`


## JdClient (class)

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

- JdClient SetAccessToken(string token)

- JdClient Server(string host, int port, string path, bool useTls)

- JdClient TimeZoneOffset(int minutes)

- JdClient Timeout(int ms)

- JdClient MaxRetries(int n)

- async JdResponse ExecuteAsync(JdRequest request)

- HttpClient NewHttp()

- string BuildBody(JdRequest request)

- string Timestamp()

- static string FormatTimestamp(long unixSeconds, int offsetMinutes)

- static string Pad2(int n)

- static string FormEncode(Dictionary <string, string> p)


## JdException (class)

- public string Code;

- public string Body;

- public JdException(string code, string message, string body)


## JdRequest (class)

- public string ApiName;

- public string ApiVersion;

- JsonValue param;

- public JdRequest(string apiName)

- JdRequest Set(string key, string paramValue)

- JdRequest SetInt(string key, int paramValue)

- JdRequest SetLong(string key, long paramValue)

- JdRequest SetBool(string key, bool paramValue)

- JdRequest SetValue(string key, JsonValue paramValue)

- string ParamJson()


## JdResponse (class)

- public string Data;

- public string Raw;

- public JsonValue Value;

- static string lastErrCode="";

- static string lastErrMsg="";

- JdResponse(string data, string raw, JsonValue v)

- static JdResponse Parse(string body)

- static JdResponse Inspect(string body)

- static JdResponse Failure(string code, string msg)

- static string Field(JsonValue obj, string key, string dflt)


## JdSign (class)

- static string Compute(Dictionary <string, string> parameters, string secret)

- static List<string> SortedKeys(Dictionary <string, string> parameters)
