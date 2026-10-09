# Sdk.Jd.Api.Jos

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Jos/JdJosApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Jos/JosIsvTokenEncryptionRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Jos/JosMasterKeyGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Jos/JosOauthRpcXidPin2XidRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Jos/JosOrderOaidWaitingRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Jos/JosSecretApiReportGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Jos/JosVoucherInfoGetRequest.zan`


## JdJosApi (class)

- JdClient client;

- public JdJosApi(JdClient client)

- async JosIsvTokenEncryptionResponse IsvTokenEncryptionAsync(JosIsvTokenEncryptionRequest request)

- async JosMasterKeyGetResponse MasterKeyGetAsync(JosMasterKeyGetRequest request)

- async JosOauthRpcXidPin2XidResponse OauthRpcXidPin2XidAsync(JosOauthRpcXidPin2XidRequest request)

- async JosOrderOaidWaitingResponse OrderOaidWaitingAsync(JosOrderOaidWaitingRequest request)

- async JosSecretApiReportGetResponse SecretApiReportGetAsync(JosSecretApiReportGetRequest request)

- async JosVoucherInfoGetResponse VoucherInfoGetAsync(JosVoucherInfoGetRequest request)


## JosIsvTokenEncryptionRequest (class)

- JdRequest req;

- public JosIsvTokenEncryptionRequest()

- JosIsvTokenEncryptionRequest TokenStr(string tokenStr)

- JdRequest Raw()


## JosIsvTokenEncryptionResponse (class)

- public Result returnType;

- public string Raw;


## JosMasterKeyGetRequest (class)

- JdRequest req;

- public JosMasterKeyGetRequest()

- JosMasterKeyGetRequest Sig(string sig)

- JosMasterKeyGetRequest SdkVer(int sdkVer)

- JosMasterKeyGetRequest Ts(long ts)

- JosMasterKeyGetRequest Tid(string tid)

- JdRequest Raw()


## JosMasterKeyGetResponse (class)

- public KeyResponse response;

- public string Raw;


## JosOauthRpcXidPin2XidRequest (class)

- JdRequest req;

- public JosOauthRpcXidPin2XidRequest()

- JosOauthRpcXidPin2XidRequest UserPin(string userPin)

- JosOauthRpcXidPin2XidRequest AppKey(string appKey)

- JdRequest Raw()


## JosOauthRpcXidPin2XidResponse (class)

- public Result returnType;

- public string Raw;


## JosOrderOaidWaitingRequest (class)

- JdRequest req;

- public JosOrderOaidWaitingRequest()

- JosOrderOaidWaitingRequest OrderType(string orderType)

- JosOrderOaidWaitingRequest IsTelephoneCalculate(bool isTelephoneCalculate)

- JosOrderOaidWaitingRequest OrderId(string orderId)

- JdRequest Raw()


## JosOrderOaidWaitingResponse (class)

- public Result returnType;

- public string Raw;


## JosSecretApiReportGetRequest (class)

- JdRequest req;

- public JosSecretApiReportGetRequest()

- JosSecretApiReportGetRequest AccessToken(string accessToken)

- JosSecretApiReportGetRequest BusinessId(string businessId)

- JosSecretApiReportGetRequest Text(string text)

- JosSecretApiReportGetRequest Attribute(string attribute)

- JosSecretApiReportGetRequest CustomerUserId(long customerUserId)

- JosSecretApiReportGetRequest ServerUrl(string serverUrl)

- JdRequest Raw()


## JosSecretApiReportGetResponse (class)

- public ResponseVO response;

- public string Raw;


## JosVoucherInfoGetRequest (class)

- JdRequest req;

- public JosVoucherInfoGetRequest()

- JosVoucherInfoGetRequest AccessToken(string accessToken)

- JosVoucherInfoGetRequest CustomerUserId(long customerUserId)

- JdRequest Raw()


## JosVoucherInfoGetResponse (class)

- public ResponseVO response;

- public string Raw;
