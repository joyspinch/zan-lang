# Sdk.Jd.Api.Jos

> 源码: `stdlib/Sdk/Jd/Api/Jos/JdJosApi.zan`, `stdlib/Sdk/Jd/Api/Jos/JosIsvTokenEncryptionRequest.zan`, `stdlib/Sdk/Jd/Api/Jos/JosMasterKeyGetRequest.zan`, `stdlib/Sdk/Jd/Api/Jos/JosOauthRpcXidPin2XidRequest.zan`, `stdlib/Sdk/Jd/Api/Jos/JosOrderOaidWaitingRequest.zan`, `stdlib/Sdk/Jd/Api/Jos/JosSecretApiReportGetRequest.zan`, `stdlib/Sdk/Jd/Api/Jos/JosVoucherInfoGetRequest.zan`


## JdJosApi (class)

jingdong.jos.* 的强类型客户端。

- JdClient client;

- public JdJosApi(JdClient client)

- async JosIsvTokenEncryptionResponse IsvTokenEncryptionAsync(JosIsvTokenEncryptionRequest request)
  - 执行 <c>jingdong.jos.isv.token.encryption</c>。

- async JosMasterKeyGetResponse MasterKeyGetAsync(JosMasterKeyGetRequest request)
  - 执行 <c>jingdong.jos.master.key.get</c>。

- async JosOauthRpcXidPin2XidResponse OauthRpcXidPin2XidAsync(JosOauthRpcXidPin2XidRequest request)
  - 执行 <c>jingdong.jos.oauth.rpc.xid.pin2Xid</c>。

- async JosOrderOaidWaitingResponse OrderOaidWaitingAsync(JosOrderOaidWaitingRequest request)
  - 执行 <c>jingdong.jos.order.oaid.waiting</c>。

- async JosSecretApiReportGetResponse SecretApiReportGetAsync(JosSecretApiReportGetRequest request)
  - 执行 <c>jingdong.jos.secret.api.report.get</c>。

- async JosVoucherInfoGetResponse VoucherInfoGetAsync(JosVoucherInfoGetRequest request)
  - 执行 <c>jingdong.jos.voucher.info.get</c>。


## JosIsvTokenEncryptionRequest (class)

<c>jingdong.jos.isv.token.encryption</c> 的请求。

- JdRequest req;

- public JosIsvTokenEncryptionRequest()

- JosIsvTokenEncryptionRequest TokenStr(string tokenStr)
  - 设置 <c>tokenStr</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## JosIsvTokenEncryptionResponse (class)

<c>jingdong.jos.isv.token.encryption</c> 的响应。

- public Result returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## JosMasterKeyGetRequest (class)

<c>jingdong.jos.master.key.get</c> 的请求。

- JdRequest req;

- public JosMasterKeyGetRequest()

- JosMasterKeyGetRequest Sig(string sig)
  - 设置 <c>sig</c> 参数。

- JosMasterKeyGetRequest SdkVer(int sdkVer)
  - 设置 <c>sdk_ver</c> 参数。

- JosMasterKeyGetRequest Ts(long ts)
  - 设置 <c>ts</c> 参数。

- JosMasterKeyGetRequest Tid(string tid)
  - 设置 <c>tid</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## JosMasterKeyGetResponse (class)

<c>jingdong.jos.master.key.get</c> 的响应。

- public KeyResponse response;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## JosOauthRpcXidPin2XidRequest (class)

<c>jingdong.jos.oauth.rpc.xid.pin2Xid</c> 的请求。

- JdRequest req;

- public JosOauthRpcXidPin2XidRequest()

- JosOauthRpcXidPin2XidRequest UserPin(string userPin)
  - 设置 <c>userPin</c> 参数。

- JosOauthRpcXidPin2XidRequest AppKey(string appKey)
  - 设置 <c>appKey</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## JosOauthRpcXidPin2XidResponse (class)

<c>jingdong.jos.oauth.rpc.xid.pin2Xid</c> 的响应。

- public Result returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## JosOrderOaidWaitingRequest (class)

<c>jingdong.jos.order.oaid.waiting</c> 的请求。

- JdRequest req;

- public JosOrderOaidWaitingRequest()

- JosOrderOaidWaitingRequest OrderType(string orderType)
  - 设置 <c>orderType</c> 参数。

- JosOrderOaidWaitingRequest IsTelephoneCalculate(bool isTelephoneCalculate)
  - 设置 <c>isTelephoneCalculate</c> 参数。

- JosOrderOaidWaitingRequest OrderId(string orderId)
  - 设置 <c>orderId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## JosOrderOaidWaitingResponse (class)

<c>jingdong.jos.order.oaid.waiting</c> 的响应。

- public Result returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## JosSecretApiReportGetRequest (class)

<c>jingdong.jos.secret.api.report.get</c> 的请求。

- JdRequest req;

- public JosSecretApiReportGetRequest()

- JosSecretApiReportGetRequest AccessToken(string accessToken)
  - 设置 <c>access_token</c> 参数。

- JosSecretApiReportGetRequest BusinessId(string businessId)
  - 设置 <c>businessId</c> 参数。

- JosSecretApiReportGetRequest Text(string text)
  - 设置 <c>text</c> 参数。

- JosSecretApiReportGetRequest Attribute(string attribute)
  - 设置 <c>attribute</c> 参数。

- JosSecretApiReportGetRequest CustomerUserId(long customerUserId)
  - 设置 <c>customer_user_id</c> 参数。

- JosSecretApiReportGetRequest ServerUrl(string serverUrl)
  - 设置 <c>server_url</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## JosSecretApiReportGetResponse (class)

<c>jingdong.jos.secret.api.report.get</c> 的响应。

- public ResponseVO response;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## JosVoucherInfoGetRequest (class)

<c>jingdong.jos.voucher.info.get</c> 的请求。

- JdRequest req;

- public JosVoucherInfoGetRequest()

- JosVoucherInfoGetRequest AccessToken(string accessToken)
  - 设置 <c>access_token</c> 参数。

- JosVoucherInfoGetRequest CustomerUserId(long customerUserId)
  - 设置 <c>customer_user_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## JosVoucherInfoGetResponse (class)

<c>jingdong.jos.voucher.info.get</c> 的响应。

- public ResponseVO response;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
