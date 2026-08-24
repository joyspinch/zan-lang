# Sdk.Wechat.Work

> 源码: `stdlib/Sdk/Wechat/Work/WechatWorkAppApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkAsynchronousApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkCalendarApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkChatApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkClient.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkConcernApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkContactP1Api.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkCorpgroupApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkCustomerAcquisitionApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkCustomerTagApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkDataIntelligenceApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkExternalApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkIdConvertApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkInvoiceApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkKFApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkLinkedCorpApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkLivingApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkLoginAuthApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkMailListApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkMailListCurrentApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkMassApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkMassLinkerCorpApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkMediaApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkMiniProgramMiniApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkMobileApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkOAuth2Api.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkOaApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkOaDataOpenApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkOaDataOpenCheckinP2Api.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkScheduleApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkShakeAroundApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkSmartRobotClient.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkSpecialApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkSsoApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkThirdPartyAuthApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkWebhookApi.zan`, `stdlib/Sdk/Wechat/Work/WechatWorkWorkBenchApi.zan`


## WechatWorkAppApi (class)

App/AppApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkAppApi(WechatWorkClient client)

- async WechatWorkAppApiGetAppInfoResponse GetAppInfoAsync(WechatWorkAppApiGetAppInfoRequest request)
  - GET /cgi-bin/agent/get

- async WechatResponse GetAppInfoRawAsync(string query)

- async WechatWorkAppApiSetAppResponse SetAppAsync(WechatWorkAppApiSetAppRequest request)
  - POST /cgi-bin/agent/set

- async WechatResponse SetAppRawAsync(string query, string jsonBody)

- async WechatWorkAppApiGetAppListResponse GetAppListAsync()
  - GET /cgi-bin/agent/list

- async WechatResponse GetAppListRawAsync(string query)


## WechatWorkAppApiGetAppInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkAppApiGetAppInfoRequest()

- WechatWorkAppApiGetAppInfoRequest AgentId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkAppApiGetAppInfoResponse (class)

- public string Raw;


## WechatWorkAppApiGetAppListResponse (class)

- public string Raw;


## WechatWorkAppApiSetAppRequest (class)

- WechatTypedRequest request;

- public WechatWorkAppApiSetAppRequest()

- WechatWorkAppApiSetAppRequest Agentid(string fieldValue)

- WechatWorkAppApiSetAppRequest ReportLocationFlag(string fieldValue)

- WechatWorkAppApiSetAppRequest LogoMediaid(string fieldValue)

- WechatWorkAppApiSetAppRequest Name(string fieldValue)

- WechatWorkAppApiSetAppRequest Description(string fieldValue)

- WechatWorkAppApiSetAppRequest RedirectDomain(string fieldValue)

- WechatWorkAppApiSetAppRequest Isreportuser(int fieldValue)

- WechatWorkAppApiSetAppRequest HomeUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkAppApiSetAppResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkAsynchronousApi (class)

Asynchronous/AsynchronousApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkAsynchronousApi(WechatWorkClient client)

- async WechatWorkAsynchronousApiBatchSyncUserResponse BatchSyncUserAsync(WechatWorkAsynchronousApiBatchSyncUserRequest request)
  - POST /cgi-bin/batch/syncuser

- async WechatResponse BatchSyncUserRawAsync(string query, string jsonBody)

- async WechatWorkAsynchronousApiBatchReplaceUserResponse BatchReplaceUserAsync(WechatWorkAsynchronousApiBatchReplaceUserRequest request)
  - POST /cgi-bin/batch/replaceuser

- async WechatResponse BatchReplaceUserRawAsync(string query, string jsonBody)

- async WechatWorkAsynchronousApiBatchReplacePartyResponse BatchReplacePartyAsync(WechatWorkAsynchronousApiBatchReplacePartyRequest request)
  - POST /cgi-bin/batch/replaceparty

- async WechatResponse BatchReplacePartyRawAsync(string query, string jsonBody)

- async WechatWorkAsynchronousApiGetReplaceUserResultResponse GetReplaceUserResultAsync(WechatWorkAsynchronousApiGetReplaceUserResultRequest request)
  - GET /cgi-bin/batch/getresult

- async WechatResponse GetReplaceUserResultRawAsync(string query)

- async WechatWorkAsynchronousApiGetReplacePartyResultResponse GetReplacePartyResultAsync(WechatWorkAsynchronousApiGetReplacePartyResultRequest request)
  - GET /cgi-bin/batch/getresult

- async WechatResponse GetReplacePartyResultRawAsync(string query)


## WechatWorkAsynchronousApiBatchReplacePartyRequest (class)

- WechatTypedRequest request;

- public WechatWorkAsynchronousApiBatchReplacePartyRequest()

- WechatWorkAsynchronousApiBatchReplacePartyRequest Url(string fieldValue)

- WechatWorkAsynchronousApiBatchReplacePartyRequest Token(string fieldValue)

- WechatWorkAsynchronousApiBatchReplacePartyRequest Encodingaeskey(string fieldValue)

- WechatWorkAsynchronousApiBatchReplacePartyRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkAsynchronousApiBatchReplacePartyResponse (class)

- public string Raw;


## WechatWorkAsynchronousApiBatchReplaceUserRequest (class)

- WechatTypedRequest request;

- public WechatWorkAsynchronousApiBatchReplaceUserRequest()

- WechatWorkAsynchronousApiBatchReplaceUserRequest Url(string fieldValue)

- WechatWorkAsynchronousApiBatchReplaceUserRequest Token(string fieldValue)

- WechatWorkAsynchronousApiBatchReplaceUserRequest Encodingaeskey(string fieldValue)

- WechatWorkAsynchronousApiBatchReplaceUserRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkAsynchronousApiBatchReplaceUserResponse (class)

- public string Raw;


## WechatWorkAsynchronousApiBatchSyncUserRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkAsynchronousApiBatchSyncUserRequest()

- WechatWorkAsynchronousApiBatchSyncUserRequest Url(string fieldValue)

- WechatWorkAsynchronousApiBatchSyncUserRequest Token(string fieldValue)

- WechatWorkAsynchronousApiBatchSyncUserRequest Encodingaeskey(string fieldValue)

- WechatWorkAsynchronousApiBatchSyncUserRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkAsynchronousApiBatchSyncUserResponse (class)

- public string Raw;


## WechatWorkAsynchronousApiGetReplacePartyResultRequest (class)

- WechatTypedRequest request;

- public WechatWorkAsynchronousApiGetReplacePartyResultRequest()

- WechatWorkAsynchronousApiGetReplacePartyResultRequest JobId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkAsynchronousApiGetReplacePartyResultResponse (class)

- public string Raw;


## WechatWorkAsynchronousApiGetReplaceUserResultRequest (class)

- WechatTypedRequest request;

- public WechatWorkAsynchronousApiGetReplaceUserResultRequest()

- WechatWorkAsynchronousApiGetReplaceUserResultRequest JobId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkAsynchronousApiGetReplaceUserResultResponse (class)

- public string Raw;


## WechatWorkCalendarApi (class)

Calendar/CalendarApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkCalendarApi(WechatWorkClient client)

- async WechatWorkCalendarApiAddResponse AddAsync(WechatWorkCalendarApiAddRequest request)
  - POST /cgi-bin/oa/calendar/add

- async WechatResponse AddRawAsync(string query, string jsonBody)

- async WechatWorkCalendarApiUpdateResponse UpdateAsync(WechatWorkCalendarApiUpdateRequest request)
  - POST /cgi-bin/oa/calendar/update

- async WechatResponse UpdateRawAsync(string query, string jsonBody)

- async WechatWorkCalendarApiGetResponse GetAsync(WechatWorkCalendarApiGetRequest request)
  - POST /cgi-bin/oa/calendar/get

- async WechatResponse GetRawAsync(string query, string jsonBody)

- async WechatWorkCalendarApiDelResponse DelAsync(WechatWorkCalendarApiDelRequest request)
  - POST /cgi-bin/oa/calendar/del

- async WechatResponse DelRawAsync(string query, string jsonBody)


## WechatWorkCalendarApiAddRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkCalendarApiAddRequest()

- WechatWorkCalendarApiAddRequest Calendar(WechatWorkCalendar fieldValue)

- WechatWorkCalendarApiAddRequest Agentid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCalendarApiAddResponse (class)

- public string Raw;


## WechatWorkCalendarApiDelRequest (class)

- WechatTypedRequest request;

- public WechatWorkCalendarApiDelRequest()

- WechatWorkCalendarApiDelRequest CalId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCalendarApiDelResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkCalendarApiGetRequest (class)

- WechatTypedRequest request;

- public WechatWorkCalendarApiGetRequest()

- WechatWorkCalendarApiGetRequest CalIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCalendarApiGetResponse (class)

- public string Raw;


## WechatWorkCalendarApiUpdateRequest (class)

- WechatTypedRequest request;

- public WechatWorkCalendarApiUpdateRequest()

- WechatWorkCalendarApiUpdateRequest SkipPublicRange(int fieldValue)

- WechatWorkCalendarApiUpdateRequest Calendar(WechatWorkCalendarUpdate fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCalendarApiUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkChatApi (class)

Chat/ChatApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkChatApi(WechatWorkClient client)

- async WechatWorkChatApiCreateChatResponse CreateChatAsync(WechatWorkChatApiCreateChatRequest request)
  - POST /cgi-bin/appchat/create

- async WechatResponse CreateChatRawAsync(string query, string jsonBody)

- async WechatWorkChatApiGetChatResponse GetChatAsync(WechatWorkChatApiGetChatRequest request)
  - GET /cgi-bin/appchat/get

- async WechatResponse GetChatRawAsync(string query)

- async WechatWorkChatApiUpdateChatResponse UpdateChatAsync(WechatWorkChatApiUpdateChatRequest request)
  - POST /cgi-bin/appchat/update

- async WechatResponse UpdateChatRawAsync(string query, string jsonBody)

- async WechatWorkChatApiSendChatSimpleMessageResponse SendChatSimpleMessageAsync(WechatWorkChatApiSendChatSimpleMessageRequest request)
  - POST /cgi-bin/appchat/send

- async WechatResponse SendChatSimpleMessageRawAsync(string query, string jsonBody)

- async WechatWorkChatApiSendChatVideoMessageResponse SendChatVideoMessageAsync(WechatWorkChatApiSendChatVideoMessageRequest request)
  - POST /cgi-bin/appchat/send

- async WechatResponse SendChatVideoMessageRawAsync(string query, string jsonBody)

- async WechatWorkChatApiSendChatTextCardMessageResponse SendChatTextCardMessageAsync(WechatWorkChatApiSendChatTextCardMessageRequest request)
  - POST /cgi-bin/appchat/send

- async WechatResponse SendChatTextCardMessageRawAsync(string query, string jsonBody)

- async WechatWorkChatApiSendChatNewsMessageResponse SendChatNewsMessageAsync(WechatWorkChatApiSendChatNewsMessageRequest request)
  - POST /cgi-bin/appchat/send

- async WechatResponse SendChatNewsMessageRawAsync(string query, string jsonBody)

- async WechatWorkChatApiSendChatMpNewsMessageResponse SendChatMpNewsMessageAsync(WechatWorkChatApiSendChatMpNewsMessageRequest request)
  - POST /cgi-bin/appchat/send

- async WechatResponse SendChatMpNewsMessageRawAsync(string query, string jsonBody)

- async WechatWorkChatApiQuitChatResponse QuitChatAsync(WechatWorkChatApiQuitChatRequest request)
  - POST /cgi-bin/chat/quit

- async WechatResponse QuitChatRawAsync(string query, string jsonBody)

- async WechatWorkChatApiClearNotifyResponse ClearNotifyAsync(WechatWorkChatApiClearNotifyRequest request)
  - POST /cgi-bin/chat/clearnotify

- async WechatResponse ClearNotifyRawAsync(string query, string jsonBody)

- async WechatWorkChatApiSetMuteResponse SetMuteAsync(WechatWorkChatApiSetMuteRequest request)
  - POST /cgi-bin/chat/setmute

- async WechatResponse SetMuteRawAsync(string query, string jsonBody)


## WechatWorkChatApiClearNotifyRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiClearNotifyRequest()

- WechatWorkChatApiClearNotifyRequest OpUser(string fieldValue)

- WechatWorkChatApiClearNotifyRequest Type(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiClearNotifyResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkChatApiCreateChatRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkChatApiCreateChatRequest()

- WechatWorkChatApiCreateChatRequest ChatId(string fieldValue)

- WechatWorkChatApiCreateChatRequest Name(string fieldValue)

- WechatWorkChatApiCreateChatRequest Owner(string fieldValue)

- WechatWorkChatApiCreateChatRequest Userlist(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiCreateChatResponse (class)

- public string Raw;


## WechatWorkChatApiGetChatRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiGetChatRequest()

- WechatWorkChatApiGetChatRequest ChatId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiGetChatResponse (class)

- public string Raw;


## WechatWorkChatApiQuitChatRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiQuitChatRequest()

- WechatWorkChatApiQuitChatRequest ChatId(string fieldValue)

- WechatWorkChatApiQuitChatRequest OpUser(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiQuitChatResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkChatApiSendChatMpNewsMessageRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiSendChatMpNewsMessageRequest()

- WechatWorkChatApiSendChatMpNewsMessageRequest ThumbMediaId(string fieldValue)

- WechatWorkChatApiSendChatMpNewsMessageRequest Author(string fieldValue)

- WechatWorkChatApiSendChatMpNewsMessageRequest ContentSourceUrl(string fieldValue)

- WechatWorkChatApiSendChatMpNewsMessageRequest Content(string fieldValue)

- WechatWorkChatApiSendChatMpNewsMessageRequest Digest(string fieldValue)

- WechatWorkChatApiSendChatMpNewsMessageRequest Articles(List<WechatWorkWorkArticle> fieldValue)

- WechatWorkChatApiSendChatMpNewsMessageRequest Title(string fieldValue)

- WechatWorkChatApiSendChatMpNewsMessageRequest ChatId(string fieldValue)

- WechatWorkChatApiSendChatMpNewsMessageRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiSendChatMpNewsMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkChatApiSendChatNewsMessageRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiSendChatNewsMessageRequest()

- WechatWorkChatApiSendChatNewsMessageRequest Url(string fieldValue)

- WechatWorkChatApiSendChatNewsMessageRequest Description(string fieldValue)

- WechatWorkChatApiSendChatNewsMessageRequest Picurl(string fieldValue)

- WechatWorkChatApiSendChatNewsMessageRequest Btntxt(string fieldValue)

- WechatWorkChatApiSendChatNewsMessageRequest Articles(List<WechatWorkWorkArticle> fieldValue)

- WechatWorkChatApiSendChatNewsMessageRequest Title(string fieldValue)

- WechatWorkChatApiSendChatNewsMessageRequest ChatId(string fieldValue)

- WechatWorkChatApiSendChatNewsMessageRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiSendChatNewsMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkChatApiSendChatSimpleMessageRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiSendChatSimpleMessageRequest()

- WechatWorkChatApiSendChatSimpleMessageRequest ChatId(string fieldValue)

- WechatWorkChatApiSendChatSimpleMessageRequest MsgType(JsonValue fieldValue)

- WechatWorkChatApiSendChatSimpleMessageRequest ContentOrMediaId(string fieldValue)

- WechatWorkChatApiSendChatSimpleMessageRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiSendChatSimpleMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkChatApiSendChatTextCardMessageRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiSendChatTextCardMessageRequest()

- WechatWorkChatApiSendChatTextCardMessageRequest ChatId(string fieldValue)

- WechatWorkChatApiSendChatTextCardMessageRequest Title(string fieldValue)

- WechatWorkChatApiSendChatTextCardMessageRequest Description(string fieldValue)

- WechatWorkChatApiSendChatTextCardMessageRequest Url(string fieldValue)

- WechatWorkChatApiSendChatTextCardMessageRequest Btntxt(string fieldValue)

- WechatWorkChatApiSendChatTextCardMessageRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiSendChatTextCardMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkChatApiSendChatVideoMessageRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiSendChatVideoMessageRequest()

- WechatWorkChatApiSendChatVideoMessageRequest ChatId(string fieldValue)

- WechatWorkChatApiSendChatVideoMessageRequest MediaId(string fieldValue)

- WechatWorkChatApiSendChatVideoMessageRequest Title(string fieldValue)

- WechatWorkChatApiSendChatVideoMessageRequest Description(string fieldValue)

- WechatWorkChatApiSendChatVideoMessageRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiSendChatVideoMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkChatApiSetMuteRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiSetMuteRequest()

- WechatWorkChatApiSetMuteRequest UserMuteList(List<WechatWorkUserMute> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiSetMuteResponse (class)

- public string Raw;


## WechatWorkChatApiUpdateChatRequest (class)

- WechatTypedRequest request;

- public WechatWorkChatApiUpdateChatRequest()

- WechatWorkChatApiUpdateChatRequest ChatId(string fieldValue)

- WechatWorkChatApiUpdateChatRequest Name(string fieldValue)

- WechatWorkChatApiUpdateChatRequest Owner(string fieldValue)

- WechatWorkChatApiUpdateChatRequest AddUserList(List<string> fieldValue)

- WechatWorkChatApiUpdateChatRequest DelUserList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkChatApiUpdateChatResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkClient (class)

企业微信 JSON API 客户端，自动缓存 corp access_token。

- WechatApiTransport transport;

- string corpId;

- string corpSecret;

- string accessToken;

- long accessTokenExpireUnix;

- string providerAccessToken;

- string suiteAccessToken;

- public WechatWorkClient(string corpId, string corpSecret)

- WechatWorkClient Server(string host, int port)

- WechatWorkClient Timeout(int ms)

- WechatWorkClient SetAccessToken(string token, int expiresIn)

- WechatWorkClient SetProviderAccessToken(string token)

- WechatWorkClient SetSuiteAccessToken(string token)

- async string GetAccessTokenAsync()

- async string CredentialAsync(WechatCredentialKind kind)

- async WechatResponse RequestAsync(string method, string path, string query, string jsonBody, WechatCredentialKind credential)

- async WechatRawResponse RequestRawAsync(string method, string path, string query, string body, string contentType, WechatCredentialKind credential)

- async WechatRawResponse RequestMultipartAsync(string method, string path, string query, WechatMultipart multipart, WechatCredentialKind credential)


## WechatWorkConcernApi (class)

Concern/ConcernApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkConcernApi(WechatWorkClient client)

- async WechatWorkConcernApiTwoVerificationResponse TwoVerificationAsync(WechatWorkConcernApiTwoVerificationRequest request)
  - GET /cgi-bin/user/authsucc

- async WechatResponse TwoVerificationRawAsync(string query)


## WechatWorkConcernApiTwoVerificationRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkConcernApiTwoVerificationRequest()

- WechatWorkConcernApiTwoVerificationRequest UserId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkConcernApiTwoVerificationResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkContactP1Api (class)

Contact/ContactP1Api.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkContactP1Api(WechatWorkClient client)

- async WechatWorkContactP1ApiGetExportResultResponse GetExportResultAsync(WechatWorkContactP1ApiGetExportResultRequest request)
  - GET /cgi-bin/export/get_result

- async WechatResponse GetExportResultRawAsync(string query)


## WechatWorkContactP1ApiGetExportResultRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkContactP1ApiGetExportResultRequest()

- WechatWorkContactP1ApiGetExportResultRequest JobId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkContactP1ApiGetExportResultResponse (class)

- public string Raw;


## WechatWorkCorpgroupApi (class)

Corpgroup/CorpgroupApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkCorpgroupApi(WechatWorkClient client)

- async WechatWorkCorpgroupApiCorpListAppShareInfoResponse CorpListAppShareInfoAsync(WechatWorkCorpgroupApiCorpListAppShareInfoRequest request)
  - POST /cgi-bin/corpgroup/corp/list_app_share_info

- async WechatResponse CorpListAppShareInfoRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiCorpGetTokenResponse CorpGetTokenAsync(WechatWorkCorpgroupApiCorpGetTokenRequest request)
  - POST /cgi-bin/corpgroup/corp/gettoken

- async WechatResponse CorpGetTokenRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiTransferSessionResponse TransferSessionAsync(WechatWorkCorpgroupApiTransferSessionRequest request)
  - POST /cgi-bin/miniprogram/transfer_session

- async WechatResponse TransferSessionRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiUnionIdToExternalUserIdResponse UnionIdToExternalUserIdAsync(WechatWorkCorpgroupApiUnionIdToExternalUserIdRequest request)
  - POST /cgi-bin/corpgroup/unionid_to_external_userid

- async WechatResponse UnionIdToExternalUserIdRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiUnionIdToPendingIdResponse UnionIdToPendingIdAsync(WechatWorkCorpgroupApiUnionIdToPendingIdRequest request)
  - POST /cgi-bin/corpgroup/unionid_to_pending_id

- async WechatResponse UnionIdToPendingIdRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiCorpGetChainListResponse CorpGetChainListAsync()
  - GET /cgi-bin/corpgroup/corp/get_chain_list

- async WechatResponse CorpGetChainListRawAsync(string query)

- async WechatWorkCorpgroupApiCorpGetChainGroupResponse CorpGetChainGroupAsync(WechatWorkCorpgroupApiCorpGetChainGroupRequest request)
  - POST /cgi-bin/corpgroup/corp/get_chain_group

- async WechatResponse CorpGetChainGroupRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiCorpGetChainCorpInfoListResponse CorpGetChainCorpInfoListAsync(WechatWorkCorpgroupApiCorpGetChainCorpInfoListRequest request)
  - POST /cgi-bin/corpgroup/corp/get_chain_corpinfo_list

- async WechatResponse CorpGetChainCorpInfoListRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiCorpGetChainCorpInfoResponse CorpGetChainCorpInfoAsync(WechatWorkCorpgroupApiCorpGetChainCorpInfoRequest request)
  - POST /cgi-bin/corpgroup/corp/get_chain_corpinfo

- async WechatResponse CorpGetChainCorpInfoRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiImportChainContactResponse ImportChainContactAsync(WechatWorkCorpgroupApiImportChainContactRequest request)
  - POST /cgi-bin/corpgroup/import_chain_contact

- async WechatResponse ImportChainContactRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiGetResultResponse GetResultAsync(WechatWorkCorpgroupApiGetResultRequest request)
  - GET /cgi-bin/corpgroup/getresult

- async WechatResponse GetResultRawAsync(string query)

- async WechatWorkCorpgroupApiCorpRemoveCorpResponse CorpRemoveCorpAsync(WechatWorkCorpgroupApiCorpRemoveCorpRequest request)
  - POST /cgi-bin/corpgroup/corp/remove_corp

- async WechatResponse CorpRemoveCorpRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiCorpGetChainUserCustomIdResponse CorpGetChainUserCustomIdAsync(WechatWorkCorpgroupApiCorpGetChainUserCustomIdRequest request)
  - POST /cgi-bin/corpgroup/corp/get_chain_user_custom_id

- async WechatResponse CorpGetChainUserCustomIdRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiGetCorpSharedChainListResponse GetCorpSharedChainListAsync(WechatWorkCorpgroupApiGetCorpSharedChainListRequest request)
  - POST /cgi-bin/corpgroup/get_corp_shared_chain_list

- async WechatResponse GetCorpSharedChainListRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiRuleListIdsResponse RuleListIdsAsync(WechatWorkCorpgroupApiRuleListIdsRequest request)
  - POST /cgi-bin/corpgroup/rule/list_ids

- async WechatResponse RuleListIdsRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiRuleDeleteRuleResponse RuleDeleteRuleAsync(WechatWorkCorpgroupApiRuleDeleteRuleRequest request)
  - POST /cgi-bin/corpgroup/rule/delete_rule

- async WechatResponse RuleDeleteRuleRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiRuleGetRuleInfoResponse RuleGetRuleInfoAsync(WechatWorkCorpgroupApiRuleGetRuleInfoRequest request)
  - POST /cgi-bin/corpgroup/rule/get_rule_info

- async WechatResponse RuleGetRuleInfoRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiRuleAddRuleResponse RuleAddRuleAsync(WechatWorkCorpgroupApiRuleAddRuleRequest request)
  - POST /cgi-bin/corpgroup/rule/add_rule

- async WechatResponse RuleAddRuleRawAsync(string query, string jsonBody)

- async WechatWorkCorpgroupApiRuleModifyRuleResponse RuleModifyRuleAsync(WechatWorkCorpgroupApiRuleModifyRuleRequest request)
  - POST /cgi-bin/corpgroup/rule/modify_rule

- async WechatResponse RuleModifyRuleRawAsync(string query, string jsonBody)


## WechatWorkCorpgroupApiCorpGetChainCorpInfoListRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiCorpGetChainCorpInfoListRequest()

- WechatWorkCorpgroupApiCorpGetChainCorpInfoListRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiCorpGetChainCorpInfoListRequest Groupid(int fieldValue)

- WechatWorkCorpgroupApiCorpGetChainCorpInfoListRequest NeedPending(bool fieldValue)

- WechatWorkCorpgroupApiCorpGetChainCorpInfoListRequest Cursor(string fieldValue)

- WechatWorkCorpgroupApiCorpGetChainCorpInfoListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiCorpGetChainCorpInfoListResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiCorpGetChainCorpInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiCorpGetChainCorpInfoRequest()

- WechatWorkCorpgroupApiCorpGetChainCorpInfoRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiCorpGetChainCorpInfoRequest Corpid(string fieldValue)

- WechatWorkCorpgroupApiCorpGetChainCorpInfoRequest PendingCorpid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiCorpGetChainCorpInfoResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiCorpGetChainGroupRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiCorpGetChainGroupRequest()

- WechatWorkCorpgroupApiCorpGetChainGroupRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiCorpGetChainGroupRequest Groupid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiCorpGetChainGroupResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiCorpGetChainListResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiCorpGetChainUserCustomIdRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiCorpGetChainUserCustomIdRequest()

- WechatWorkCorpgroupApiCorpGetChainUserCustomIdRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiCorpGetChainUserCustomIdRequest Corpid(string fieldValue)

- WechatWorkCorpgroupApiCorpGetChainUserCustomIdRequest Userid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiCorpGetChainUserCustomIdResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiCorpGetTokenRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiCorpGetTokenRequest()

- WechatWorkCorpgroupApiCorpGetTokenRequest Agentid(int fieldValue)

- WechatWorkCorpgroupApiCorpGetTokenRequest BusinessType(int fieldValue)

- WechatWorkCorpgroupApiCorpGetTokenRequest Corpid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiCorpGetTokenResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiCorpListAppShareInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiCorpListAppShareInfoRequest()

- WechatWorkCorpgroupApiCorpListAppShareInfoRequest Agentid(int fieldValue)

- WechatWorkCorpgroupApiCorpListAppShareInfoRequest BusinessType(int fieldValue)

- WechatWorkCorpgroupApiCorpListAppShareInfoRequest Corpid(string fieldValue)

- WechatWorkCorpgroupApiCorpListAppShareInfoRequest Limit(int fieldValue)

- WechatWorkCorpgroupApiCorpListAppShareInfoRequest Cursor(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiCorpListAppShareInfoResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiCorpRemoveCorpRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiCorpRemoveCorpRequest()

- WechatWorkCorpgroupApiCorpRemoveCorpRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiCorpRemoveCorpRequest Corpid(string fieldValue)

- WechatWorkCorpgroupApiCorpRemoveCorpRequest PendingCorpid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiCorpRemoveCorpResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkCorpgroupApiGetCorpSharedChainListRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiGetCorpSharedChainListRequest()

- WechatWorkCorpgroupApiGetCorpSharedChainListRequest Corpid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiGetCorpSharedChainListResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiGetResultRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiGetResultRequest()

- WechatWorkCorpgroupApiGetResultRequest Jobid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiGetResultResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiImportChainContactRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiImportChainContactRequest()

- WechatWorkCorpgroupApiImportChainContactRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiImportChainContactRequest Openid(string fieldValue)

- WechatWorkCorpgroupApiImportChainContactRequest Corpid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiImportChainContactResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiRuleAddRuleRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiRuleAddRuleRequest()

- WechatWorkCorpgroupApiRuleAddRuleRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiRuleAddRuleRequest RuleInfo(WechatWorkAddRuleRequestRuleInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiRuleAddRuleResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiRuleDeleteRuleRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiRuleDeleteRuleRequest()

- WechatWorkCorpgroupApiRuleDeleteRuleRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiRuleDeleteRuleRequest RuleId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiRuleDeleteRuleResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkCorpgroupApiRuleGetRuleInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiRuleGetRuleInfoRequest()

- WechatWorkCorpgroupApiRuleGetRuleInfoRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiRuleGetRuleInfoRequest RuleId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiRuleGetRuleInfoResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiRuleListIdsRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiRuleListIdsRequest()

- WechatWorkCorpgroupApiRuleListIdsRequest ChainId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiRuleListIdsResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiRuleModifyRuleRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiRuleModifyRuleRequest()

- WechatWorkCorpgroupApiRuleModifyRuleRequest ChainId(string fieldValue)

- WechatWorkCorpgroupApiRuleModifyRuleRequest RuleId(int fieldValue)

- WechatWorkCorpgroupApiRuleModifyRuleRequest RuleInfo(WechatWorkModifyRuleRequestRuleInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiRuleModifyRuleResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkCorpgroupApiTransferSessionRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiTransferSessionRequest()

- WechatWorkCorpgroupApiTransferSessionRequest Userid(string fieldValue)

- WechatWorkCorpgroupApiTransferSessionRequest SessionKey(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiTransferSessionResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiUnionIdToExternalUserIdRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiUnionIdToExternalUserIdRequest()

- WechatWorkCorpgroupApiUnionIdToExternalUserIdRequest Unionid(string fieldValue)

- WechatWorkCorpgroupApiUnionIdToExternalUserIdRequest Openid(string fieldValue)

- WechatWorkCorpgroupApiUnionIdToExternalUserIdRequest Corpid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiUnionIdToExternalUserIdResponse (class)

- public string Raw;


## WechatWorkCorpgroupApiUnionIdToPendingIdRequest (class)

- WechatTypedRequest request;

- public WechatWorkCorpgroupApiUnionIdToPendingIdRequest()

- WechatWorkCorpgroupApiUnionIdToPendingIdRequest Unionid(string fieldValue)

- WechatWorkCorpgroupApiUnionIdToPendingIdRequest Openid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCorpgroupApiUnionIdToPendingIdResponse (class)

- public string Raw;


## WechatWorkCustomerAcquisitionApi (class)

CustomerAcquisition/CustomerAcquisitionApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkCustomerAcquisitionApi(WechatWorkClient client)

- async WechatWorkCustomerAcquisitionApiGetLinkListResponse GetLinkListAsync(WechatWorkCustomerAcquisitionApiGetLinkListRequest request)
  - POST /cgi-bin/externalcontact/customer_acquisition/list_link

- async WechatResponse GetLinkListRawAsync(string query, string jsonBody)

- async WechatWorkCustomerAcquisitionApiGetLinkDetailResponse GetLinkDetailAsync(WechatWorkCustomerAcquisitionApiGetLinkDetailRequest request)
  - POST /cgi-bin/externalcontact/customer_acquisition/get

- async WechatResponse GetLinkDetailRawAsync(string query, string jsonBody)

- async WechatWorkCustomerAcquisitionApiCreateLinkResponse CreateLinkAsync(WechatWorkCustomerAcquisitionApiCreateLinkRequest request)
  - POST /cgi-bin/externalcontact/customer_acquisition/create_link

- async WechatResponse CreateLinkRawAsync(string query, string jsonBody)

- async WechatWorkCustomerAcquisitionApiModifyLinkResponse ModifyLinkAsync(WechatWorkCustomerAcquisitionApiModifyLinkRequest request)
  - POST /cgi-bin/externalcontact/customer_acquisition/update_link

- async WechatResponse ModifyLinkRawAsync(string query, string jsonBody)

- async WechatWorkCustomerAcquisitionApiDeleteLinkResponse DeleteLinkAsync(WechatWorkCustomerAcquisitionApiDeleteLinkRequest request)
  - POST /cgi-bin/externalcontact/customer_acquisition/delete_link

- async WechatResponse DeleteLinkRawAsync(string query, string jsonBody)


## WechatWorkCustomerAcquisitionApiCreateLinkRequest (class)

- WechatTypedRequest request;

- public WechatWorkCustomerAcquisitionApiCreateLinkRequest()

- WechatWorkCustomerAcquisitionApiCreateLinkRequest LinkName(string fieldValue)

- WechatWorkCustomerAcquisitionApiCreateLinkRequest Range(WechatWorkCustomerAcquisitionRange fieldValue)

- WechatWorkCustomerAcquisitionApiCreateLinkRequest SkipVerify(bool fieldValue)

- WechatWorkCustomerAcquisitionApiCreateLinkRequest PriorityOption(WechatWorkCustomerAcquisitionPriority fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerAcquisitionApiCreateLinkResponse (class)

- public string Raw;


## WechatWorkCustomerAcquisitionApiDeleteLinkRequest (class)

- WechatTypedRequest request;

- public WechatWorkCustomerAcquisitionApiDeleteLinkRequest()

- WechatWorkCustomerAcquisitionApiDeleteLinkRequest LinkId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerAcquisitionApiDeleteLinkResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkCustomerAcquisitionApiGetLinkDetailRequest (class)

- WechatTypedRequest request;

- public WechatWorkCustomerAcquisitionApiGetLinkDetailRequest()

- WechatWorkCustomerAcquisitionApiGetLinkDetailRequest LinkId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerAcquisitionApiGetLinkDetailResponse (class)

- public string Raw;


## WechatWorkCustomerAcquisitionApiGetLinkListRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkCustomerAcquisitionApiGetLinkListRequest()

- WechatWorkCustomerAcquisitionApiGetLinkListRequest Limit(int fieldValue)

- WechatWorkCustomerAcquisitionApiGetLinkListRequest Cursor(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerAcquisitionApiGetLinkListResponse (class)

- public string Raw;


## WechatWorkCustomerAcquisitionApiModifyLinkRequest (class)

- WechatTypedRequest request;

- public WechatWorkCustomerAcquisitionApiModifyLinkRequest()

- WechatWorkCustomerAcquisitionApiModifyLinkRequest LinkId(string fieldValue)

- WechatWorkCustomerAcquisitionApiModifyLinkRequest LinkName(string fieldValue)

- WechatWorkCustomerAcquisitionApiModifyLinkRequest Range(WechatWorkCustomerAcquisitionRange fieldValue)

- WechatWorkCustomerAcquisitionApiModifyLinkRequest SkipVerify(bool fieldValue)

- WechatWorkCustomerAcquisitionApiModifyLinkRequest AcquisitionPriority(WechatWorkCustomerAcquisitionPriority fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerAcquisitionApiModifyLinkResponse (class)

- public string Raw;


## WechatWorkCustomerTagApi (class)

CustomerTag/CustomerTagApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkCustomerTagApi(WechatWorkClient client)

- async WechatWorkCustomerTagApiGetCustomerTagListResponse GetCustomerTagListAsync(WechatWorkCustomerTagApiGetCustomerTagListRequest request)
  - POST /cgi-bin/externalcontact/get_corp_tag_list

- async WechatResponse GetCustomerTagListRawAsync(string query, string jsonBody)

- async WechatWorkCustomerTagApiAddCorpCustomerTagResponse AddCorpCustomerTagAsync(WechatWorkCustomerTagApiAddCorpCustomerTagRequest request)
  - POST /cgi-bin/externalcontact/add_corp_tag

- async WechatResponse AddCorpCustomerTagRawAsync(string query, string jsonBody)

- async WechatWorkCustomerTagApiEditCorpCustomerTagResponse EditCorpCustomerTagAsync(WechatWorkCustomerTagApiEditCorpCustomerTagRequest request)
  - POST /cgi-bin/externalcontact/edit_corp_tag

- async WechatResponse EditCorpCustomerTagRawAsync(string query, string jsonBody)

- async WechatWorkCustomerTagApiDeleteCorpCustomerTagResponse DeleteCorpCustomerTagAsync(WechatWorkCustomerTagApiDeleteCorpCustomerTagRequest request)
  - POST /cgi-bin/externalcontact/del_corp_tag

- async WechatResponse DeleteCorpCustomerTagRawAsync(string query, string jsonBody)

- async WechatWorkCustomerTagApiExternalContactMarkTagResponse ExternalContactMarkTagAsync(WechatWorkCustomerTagApiExternalContactMarkTagRequest request)
  - POST /cgi-bin/externalcontact/mark_tag

- async WechatResponse ExternalContactMarkTagRawAsync(string query, string jsonBody)


## WechatWorkCustomerTagApiAddCorpCustomerTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkCustomerTagApiAddCorpCustomerTagRequest()

- WechatWorkCustomerTagApiAddCorpCustomerTagRequest GroupId(string fieldValue)

- WechatWorkCustomerTagApiAddCorpCustomerTagRequest GroupName(string fieldValue)

- WechatWorkCustomerTagApiAddCorpCustomerTagRequest CreateTime(long fieldValue)

- WechatWorkCustomerTagApiAddCorpCustomerTagRequest Order(long fieldValue)

- WechatWorkCustomerTagApiAddCorpCustomerTagRequest Deleted(bool fieldValue)

- WechatWorkCustomerTagApiAddCorpCustomerTagRequest Tag(List<WechatWorkCorpTag> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerTagApiAddCorpCustomerTagResponse (class)

- public string Raw;


## WechatWorkCustomerTagApiDeleteCorpCustomerTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkCustomerTagApiDeleteCorpCustomerTagRequest()

- WechatWorkCustomerTagApiDeleteCorpCustomerTagRequest TagId(List<string> fieldValue)

- WechatWorkCustomerTagApiDeleteCorpCustomerTagRequest GroupId(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerTagApiDeleteCorpCustomerTagResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkCustomerTagApiEditCorpCustomerTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkCustomerTagApiEditCorpCustomerTagRequest()

- WechatWorkCustomerTagApiEditCorpCustomerTagRequest Id(string fieldValue)

- WechatWorkCustomerTagApiEditCorpCustomerTagRequest Name(string fieldValue)

- WechatWorkCustomerTagApiEditCorpCustomerTagRequest Order(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerTagApiEditCorpCustomerTagResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkCustomerTagApiExternalContactMarkTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkCustomerTagApiExternalContactMarkTagRequest()

- WechatWorkCustomerTagApiExternalContactMarkTagRequest Userid(string fieldValue)

- WechatWorkCustomerTagApiExternalContactMarkTagRequest ExternalUserid(string fieldValue)

- WechatWorkCustomerTagApiExternalContactMarkTagRequest AddTag(List<string> fieldValue)

- WechatWorkCustomerTagApiExternalContactMarkTagRequest RemoveTag(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerTagApiExternalContactMarkTagResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkCustomerTagApiGetCustomerTagListRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkCustomerTagApiGetCustomerTagListRequest()

- WechatWorkCustomerTagApiGetCustomerTagListRequest AccessTokenOrAppkey(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkCustomerTagApiGetCustomerTagListResponse (class)

- public string Raw;


## WechatWorkDataIntelligenceApi (class)

DataIntelligence/DataIntelligenceApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkDataIntelligenceApi(WechatWorkClient client)

- async WechatWorkDataIntelligenceApiGetConversationRecordsResponse GetConversationRecordsAsync(WechatWorkDataIntelligenceApiGetConversationRecordsRequest request)
  - POST /cgi-bin/data/get_conversation_records

- async WechatResponse GetConversationRecordsRawAsync(string query, string jsonBody)

- async WechatWorkDataIntelligenceApiGetMessageStatisticsResponse GetMessageStatisticsAsync(WechatWorkDataIntelligenceApiGetMessageStatisticsRequest request)
  - POST /cgi-bin/data/get_message_statistics

- async WechatResponse GetMessageStatisticsRawAsync(string query, string jsonBody)


## WechatWorkDataIntelligenceApiGetConversationRecordsRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkDataIntelligenceApiGetConversationRecordsRequest()

- WechatWorkDataIntelligenceApiGetConversationRecordsRequest ChatId(string fieldValue)

- WechatWorkDataIntelligenceApiGetConversationRecordsRequest StartTime(string fieldValue)

- WechatWorkDataIntelligenceApiGetConversationRecordsRequest EndTime(string fieldValue)

- WechatWorkDataIntelligenceApiGetConversationRecordsRequest Cursor(string fieldValue)

- WechatWorkDataIntelligenceApiGetConversationRecordsRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkDataIntelligenceApiGetConversationRecordsResponse (class)

- public string Raw;


## WechatWorkDataIntelligenceApiGetMessageStatisticsRequest (class)

- WechatTypedRequest request;

- public WechatWorkDataIntelligenceApiGetMessageStatisticsRequest()

- WechatWorkDataIntelligenceApiGetMessageStatisticsRequest StartTime(string fieldValue)

- WechatWorkDataIntelligenceApiGetMessageStatisticsRequest EndTime(string fieldValue)

- WechatWorkDataIntelligenceApiGetMessageStatisticsRequest Type(string fieldValue)

- WechatWorkDataIntelligenceApiGetMessageStatisticsRequest AgentId(string fieldValue)

- WechatWorkDataIntelligenceApiGetMessageStatisticsRequest UserIds(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkDataIntelligenceApiGetMessageStatisticsResponse (class)

- public string Raw;


## WechatWorkExternalApi (class)

External/ExternalApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkExternalApi(WechatWorkClient client)

- async WechatWorkExternalApiTransferExternalResponse TransferExternalAsync(WechatWorkExternalApiTransferExternalRequest request)
  - POST /cgi-bin/crm/transfer_external_contact

- async WechatResponse TransferExternalRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetExternalContactResponse GetExternalContactAsync(WechatWorkExternalApiGetExternalContactRequest request)
  - GET /cgi-bin/crm/get_external_contact

- async WechatResponse GetExternalContactRawAsync(string query)

- async WechatWorkExternalApiGroupChatListResponse GroupChatListAsync(WechatWorkExternalApiGroupChatListRequest request)
  - POST /cgi-bin/externalcontact/groupchat/list

- async WechatResponse GroupChatListRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupChatGetResponse GroupChatGetAsync(WechatWorkExternalApiGroupChatGetRequest request)
  - POST /cgi-bin/externalcontact/groupchat/get

- async WechatResponse GroupChatGetRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetExternalContactListResponse GetExternalContactListAsync(WechatWorkExternalApiGetExternalContactListRequest request)
  - GET /cgi-bin/externalcontact/list

- async WechatResponse GetExternalContactListRawAsync(string query)

- async WechatWorkExternalApiGetExternalContactInfoResponse GetExternalContactInfoAsync(WechatWorkExternalApiGetExternalContactInfoRequest request)
  - GET /cgi-bin/externalcontact/get

- async WechatResponse GetExternalContactInfoRawAsync(string query)

- async WechatWorkExternalApiGetExternalContactInfoBatchResponse GetExternalContactInfoBatchAsync(WechatWorkExternalApiGetExternalContactInfoBatchRequest request)
  - POST /cgi-bin/externalcontact/batch/get_by_user

- async WechatResponse GetExternalContactInfoBatchRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiUpdateExternalContactRemarkResponse UpdateExternalContactRemarkAsync(WechatWorkExternalApiUpdateExternalContactRemarkRequest request)
  - POST /cgi-bin/externalcontact/remark

- async WechatResponse UpdateExternalContactRemarkRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetFollowUserListResponse GetFollowUserListAsync()
  - GET /cgi-bin/externalcontact/get_follow_user_list

- async WechatResponse GetFollowUserListRawAsync(string query)

- async WechatWorkExternalApiGetUserBehaviorDataResponse GetUserBehaviorDataAsync(WechatWorkExternalApiGetUserBehaviorDataRequest request)
  - POST /cgi-bin/externalcontact/get_user_behavior_data

- async WechatResponse GetUserBehaviorDataRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupChatStatisticOwnerResponse GroupChatStatisticOwnerAsync(WechatWorkExternalApiGroupChatStatisticOwnerRequest request)
  - POST /cgi-bin/externalcontact/groupchat/statistic

- async WechatResponse GroupChatStatisticOwnerRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupChatStatisticGroupByDayResponse GroupChatStatisticGroupByDayAsync(WechatWorkExternalApiGroupChatStatisticGroupByDayRequest request)
  - POST /cgi-bin/externalcontact/groupchat/statistic_group_by_day

- async WechatResponse GroupChatStatisticGroupByDayRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetCropTagListResponse GetCropTagListAsync(WechatWorkExternalApiGetCropTagListRequest request)
  - POST /cgi-bin/externalcontact/get_corp_tag_list

- async WechatResponse GetCropTagListRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiAddCropTagResponse AddCropTagAsync(WechatWorkExternalApiAddCropTagRequest request)
  - POST /cgi-bin/externalcontact/add_corp_tag

- async WechatResponse AddCropTagRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiEditCropTagResponse EditCropTagAsync(WechatWorkExternalApiEditCropTagRequest request)
  - POST /cgi-bin/externalcontact/edit_corp_tag

- async WechatResponse EditCropTagRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiDeleteCropTagResponse DeleteCropTagAsync(WechatWorkExternalApiDeleteCropTagRequest request)
  - POST /cgi-bin/externalcontact/del_corp_tag

- async WechatResponse DeleteCropTagRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetStrategyTagListResponse GetStrategyTagListAsync(WechatWorkExternalApiGetStrategyTagListRequest request)
  - POST /cgi-bin/externalcontact/get_strategy_tag_list

- async WechatResponse GetStrategyTagListRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiAddStrategyTagResponse AddStrategyTagAsync(WechatWorkExternalApiAddStrategyTagRequest request)
  - POST /cgi-bin/externalcontact/add_strategy_tag

- async WechatResponse AddStrategyTagRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiEditStrategyTagResponse EditStrategyTagAsync(WechatWorkExternalApiEditStrategyTagRequest request)
  - POST /cgi-bin/externalcontact/edit_strategy_tag

- async WechatResponse EditStrategyTagRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiDeleteStrategyTagResponse DeleteStrategyTagAsync(WechatWorkExternalApiDeleteStrategyTagRequest request)
  - POST /cgi-bin/externalcontact/del_strategy_tag

- async WechatResponse DeleteStrategyTagRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetMomentListResponse GetMomentListAsync(WechatWorkExternalApiGetMomentListRequest request)
  - POST /cgi-bin/externalcontact/get_moment_list

- async WechatResponse GetMomentListRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetMomentTaskResponse GetMomentTaskAsync(WechatWorkExternalApiGetMomentTaskRequest request)
  - POST /cgi-bin/externalcontact/get_moment_task

- async WechatResponse GetMomentTaskRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiAddContactWayResponse AddContactWayAsync(WechatWorkExternalApiAddContactWayRequest request)
  - POST /cgi-bin/externalcontact/add_contact_way

- async WechatResponse AddContactWayRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiListContactWayResponse ListContactWayAsync(WechatWorkExternalApiListContactWayRequest request)
  - POST /cgi-bin/externalcontact/list_contact_way

- async WechatResponse ListContactWayRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiUpdateContactWayResponse UpdateContactWayAsync(WechatWorkExternalApiUpdateContactWayRequest request)
  - POST /cgi-bin/externalcontact/update_contact_way

- async WechatResponse UpdateContactWayRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiDeleteContactWayResponse DeleteContactWayAsync(WechatWorkExternalApiDeleteContactWayRequest request)
  - POST /cgi-bin/externalcontact/del_contact_way

- async WechatResponse DeleteContactWayRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiCloseTempChatResponse CloseTempChatAsync(WechatWorkExternalApiCloseTempChatRequest request)
  - POST /cgi-bin/externalcontact/close_temp_chat

- async WechatResponse CloseTempChatRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupChat_AddJoinWayResponse GroupChat_AddJoinWayAsync(WechatWorkExternalApiGroupChat_AddJoinWayRequest request)
  - POST /cgi-bin/externalcontact/groupchat/add_join_way

- async WechatResponse GroupChat_AddJoinWayRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupChat_GetJoinWayResponse GroupChat_GetJoinWayAsync(WechatWorkExternalApiGroupChat_GetJoinWayRequest request)
  - POST /cgi-bin/externalcontact/groupchat/get_join_way

- async WechatResponse GroupChat_GetJoinWayRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupChat_UpdateJoinWayResponse GroupChat_UpdateJoinWayAsync(WechatWorkExternalApiGroupChat_UpdateJoinWayRequest request)
  - POST /cgi-bin/externalcontact/groupchat/update_join_way

- async WechatResponse GroupChat_UpdateJoinWayRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupChat_DelJoinWayResponse GroupChat_DelJoinWayAsync(WechatWorkExternalApiGroupChat_DelJoinWayRequest request)
  - POST /cgi-bin/externalcontact/groupchat/del_join_way

- async WechatResponse GroupChat_DelJoinWayRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiAddMsgTemplateResponse AddMsgTemplateAsync(WechatWorkExternalApiAddMsgTemplateRequest request)
  - POST /cgi-bin/externalcontact/add_msg_template

- async WechatResponse AddMsgTemplateRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetGroupMsgListV2Response GetGroupMsgListV2Async(WechatWorkExternalApiGetGroupMsgListV2Request request)
  - POST /cgi-bin/externalcontact/get_groupmsg_list_v2

- async WechatResponse GetGroupMsgListV2RawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetGroupMsgTaskResponse GetGroupMsgTaskAsync(WechatWorkExternalApiGetGroupMsgTaskRequest request)
  - POST /cgi-bin/externalcontact/get_groupmsg_task

- async WechatResponse GetGroupMsgTaskRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGetGroupMsgSendResultResponse GetGroupMsgSendResultAsync(WechatWorkExternalApiGetGroupMsgSendResultRequest request)
  - POST /cgi-bin/externalcontact/get_groupmsg_send_result

- async WechatResponse GetGroupMsgSendResultRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiSendWelcomeMsgResponse SendWelcomeMsgAsync(WechatWorkExternalApiSendWelcomeMsgRequest request)
  - POST /cgi-bin/externalcontact/send_welcome_msg

- async WechatResponse SendWelcomeMsgRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupWelcomeTemplateAddResponse GroupWelcomeTemplateAddAsync(WechatWorkExternalApiGroupWelcomeTemplateAddRequest request)
  - POST /cgi-bin/externalcontact/group_welcome_template/add

- async WechatResponse GroupWelcomeTemplateAddRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupWelcomeTemplateEditResponse GroupWelcomeTemplateEditAsync(WechatWorkExternalApiGroupWelcomeTemplateEditRequest request)
  - POST /cgi-bin/externalcontact/group_welcome_template/edit

- async WechatResponse GroupWelcomeTemplateEditRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupWelcomeTemplateGetResponse GroupWelcomeTemplateGetAsync(WechatWorkExternalApiGroupWelcomeTemplateGetRequest request)
  - POST /cgi-bin/externalcontact/group_welcome_template/get

- async WechatResponse GroupWelcomeTemplateGetRawAsync(string query, string jsonBody)

- async WechatWorkExternalApiGroupWelcomeTemplateDelResponse GroupWelcomeTemplateDelAsync(WechatWorkExternalApiGroupWelcomeTemplateDelRequest request)
  - POST /cgi-bin/externalcontact/group_welcome_template/del

- async WechatResponse GroupWelcomeTemplateDelRawAsync(string query, string jsonBody)


## WechatWorkExternalApiAddContactWayRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiAddContactWayRequest()

- WechatWorkExternalApiAddContactWayRequest Type(int fieldValue)

- WechatWorkExternalApiAddContactWayRequest Scene(int fieldValue)

- WechatWorkExternalApiAddContactWayRequest Style(int fieldValue)

- WechatWorkExternalApiAddContactWayRequest Remark(string fieldValue)

- WechatWorkExternalApiAddContactWayRequest SkipVerify(bool fieldValue)

- WechatWorkExternalApiAddContactWayRequest State(string fieldValue)

- WechatWorkExternalApiAddContactWayRequest User(List<string> fieldValue)

- WechatWorkExternalApiAddContactWayRequest Party(List<int> fieldValue)

- WechatWorkExternalApiAddContactWayRequest IsTemp(bool fieldValue)

- WechatWorkExternalApiAddContactWayRequest ExpiresIn(int fieldValue)

- WechatWorkExternalApiAddContactWayRequest ChatExpiresIn(int fieldValue)

- WechatWorkExternalApiAddContactWayRequest Unionid(string fieldValue)

- WechatWorkExternalApiAddContactWayRequest Conclusions(WechatWorkConclusions fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiAddContactWayResponse (class)

- public string Raw;


## WechatWorkExternalApiAddCropTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiAddCropTagRequest()

- WechatWorkExternalApiAddCropTagRequest GroupId(string fieldValue)

- WechatWorkExternalApiAddCropTagRequest GroupName(string fieldValue)

- WechatWorkExternalApiAddCropTagRequest Order(int fieldValue)

- WechatWorkExternalApiAddCropTagRequest Tag(List<WechatWorkAddCorpTagRequestTag> fieldValue)

- WechatWorkExternalApiAddCropTagRequest Agentid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiAddCropTagResponse (class)

- public string Raw;


## WechatWorkExternalApiAddMsgTemplateRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiAddMsgTemplateRequest()

- WechatWorkExternalApiAddMsgTemplateRequest ChatType(string fieldValue)

- WechatWorkExternalApiAddMsgTemplateRequest ExternalUserid(List<string> fieldValue)

- WechatWorkExternalApiAddMsgTemplateRequest Sender(string fieldValue)

- WechatWorkExternalApiAddMsgTemplateRequest Text(WechatWorkExternalExternalJsonAddMessageTemplateRequestText fieldValue)

- WechatWorkExternalApiAddMsgTemplateRequest Attachments(List<WechatWorkExternalExternalJsonAddMessageTemplateRequestAttachment> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiAddMsgTemplateResponse (class)

- public string Raw;


## WechatWorkExternalApiAddStrategyTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiAddStrategyTagRequest()

- WechatWorkExternalApiAddStrategyTagRequest StrategyId(int fieldValue)

- WechatWorkExternalApiAddStrategyTagRequest GroupId(string fieldValue)

- WechatWorkExternalApiAddStrategyTagRequest GroupName(string fieldValue)

- WechatWorkExternalApiAddStrategyTagRequest Order(int fieldValue)

- WechatWorkExternalApiAddStrategyTagRequest Tag(List<WechatWorkAddStrategyTagRequestTag> fieldValue)

- WechatWorkExternalApiAddStrategyTagRequest Agentid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiAddStrategyTagResponse (class)

- public string Raw;


## WechatWorkExternalApiCloseTempChatRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiCloseTempChatRequest()

- WechatWorkExternalApiCloseTempChatRequest Userid(string fieldValue)

- WechatWorkExternalApiCloseTempChatRequest ExternalUserid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiCloseTempChatResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiDeleteContactWayRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiDeleteContactWayRequest()

- WechatWorkExternalApiDeleteContactWayRequest ConfigId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiDeleteContactWayResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiDeleteCropTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiDeleteCropTagRequest()

- WechatWorkExternalApiDeleteCropTagRequest TagId(List<string> fieldValue)

- WechatWorkExternalApiDeleteCropTagRequest GroupId(List<string> fieldValue)

- WechatWorkExternalApiDeleteCropTagRequest Agentid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiDeleteCropTagResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiDeleteStrategyTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiDeleteStrategyTagRequest()

- WechatWorkExternalApiDeleteStrategyTagRequest TagId(List<string> fieldValue)

- WechatWorkExternalApiDeleteStrategyTagRequest GroupId(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiDeleteStrategyTagResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiEditCropTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiEditCropTagRequest()

- WechatWorkExternalApiEditCropTagRequest Id(int fieldValue)

- WechatWorkExternalApiEditCropTagRequest Name(string fieldValue)

- WechatWorkExternalApiEditCropTagRequest Order(int fieldValue)

- WechatWorkExternalApiEditCropTagRequest Agentid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiEditCropTagResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiEditStrategyTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiEditStrategyTagRequest()

- WechatWorkExternalApiEditStrategyTagRequest Id(string fieldValue)

- WechatWorkExternalApiEditStrategyTagRequest Name(string fieldValue)

- WechatWorkExternalApiEditStrategyTagRequest Order(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiEditStrategyTagResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiGetCropTagListRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetCropTagListRequest()

- WechatWorkExternalApiGetCropTagListRequest TagId(List<string> fieldValue)

- WechatWorkExternalApiGetCropTagListRequest GroupId(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetCropTagListResponse (class)

- public string Raw;


## WechatWorkExternalApiGetExternalContactInfoBatchRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetExternalContactInfoBatchRequest()

- WechatWorkExternalApiGetExternalContactInfoBatchRequest UseridList(List<string> fieldValue)

- WechatWorkExternalApiGetExternalContactInfoBatchRequest Cursor(string fieldValue)

- WechatWorkExternalApiGetExternalContactInfoBatchRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetExternalContactInfoBatchResponse (class)

- public string Raw;


## WechatWorkExternalApiGetExternalContactInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetExternalContactInfoRequest()

- WechatWorkExternalApiGetExternalContactInfoRequest ExternalUserId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetExternalContactInfoResponse (class)

- public string Raw;


## WechatWorkExternalApiGetExternalContactListRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetExternalContactListRequest()

- WechatWorkExternalApiGetExternalContactListRequest Userid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetExternalContactListResponse (class)

- public string Raw;


## WechatWorkExternalApiGetExternalContactRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetExternalContactRequest()

- WechatWorkExternalApiGetExternalContactRequest ExternalUserId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetExternalContactResponse (class)

- public string Raw;


## WechatWorkExternalApiGetFollowUserListResponse (class)

- public string Raw;


## WechatWorkExternalApiGetGroupMsgListV2Request (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetGroupMsgListV2Request()

- WechatWorkExternalApiGetGroupMsgListV2Request ChatType(string fieldValue)

- WechatWorkExternalApiGetGroupMsgListV2Request StartTime(long fieldValue)

- WechatWorkExternalApiGetGroupMsgListV2Request EndTime(long fieldValue)

- WechatWorkExternalApiGetGroupMsgListV2Request Creator(string fieldValue)

- WechatWorkExternalApiGetGroupMsgListV2Request FilterType(int fieldValue)

- WechatWorkExternalApiGetGroupMsgListV2Request Limit(int fieldValue)

- WechatWorkExternalApiGetGroupMsgListV2Request Cursor(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetGroupMsgListV2Response (class)

- public string Raw;


## WechatWorkExternalApiGetGroupMsgSendResultRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetGroupMsgSendResultRequest()

- WechatWorkExternalApiGetGroupMsgSendResultRequest Msgid(string fieldValue)

- WechatWorkExternalApiGetGroupMsgSendResultRequest Userid(string fieldValue)

- WechatWorkExternalApiGetGroupMsgSendResultRequest Limit(int fieldValue)

- WechatWorkExternalApiGetGroupMsgSendResultRequest Cursor(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetGroupMsgSendResultResponse (class)

- public string Raw;


## WechatWorkExternalApiGetGroupMsgTaskRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetGroupMsgTaskRequest()

- WechatWorkExternalApiGetGroupMsgTaskRequest Msgid(string fieldValue)

- WechatWorkExternalApiGetGroupMsgTaskRequest Limit(int fieldValue)

- WechatWorkExternalApiGetGroupMsgTaskRequest Cursor(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetGroupMsgTaskResponse (class)

- public string Raw;


## WechatWorkExternalApiGetMomentListRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetMomentListRequest()

- WechatWorkExternalApiGetMomentListRequest StartTime(long fieldValue)

- WechatWorkExternalApiGetMomentListRequest EndTime(long fieldValue)

- WechatWorkExternalApiGetMomentListRequest Creator(string fieldValue)

- WechatWorkExternalApiGetMomentListRequest FilterType(int fieldValue)

- WechatWorkExternalApiGetMomentListRequest Cursor(string fieldValue)

- WechatWorkExternalApiGetMomentListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetMomentListResponse (class)

- public string Raw;


## WechatWorkExternalApiGetMomentTaskRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetMomentTaskRequest()

- WechatWorkExternalApiGetMomentTaskRequest MomentId(string fieldValue)

- WechatWorkExternalApiGetMomentTaskRequest Cursor(string fieldValue)

- WechatWorkExternalApiGetMomentTaskRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetMomentTaskResponse (class)

- public string Raw;


## WechatWorkExternalApiGetStrategyTagListRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetStrategyTagListRequest()

- WechatWorkExternalApiGetStrategyTagListRequest TagId(List<string> fieldValue)

- WechatWorkExternalApiGetStrategyTagListRequest GroupId(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetStrategyTagListResponse (class)

- public string Raw;


## WechatWorkExternalApiGetUserBehaviorDataRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGetUserBehaviorDataRequest()

- WechatWorkExternalApiGetUserBehaviorDataRequest Userid(List<string> fieldValue)

- WechatWorkExternalApiGetUserBehaviorDataRequest Partyid(List<string> fieldValue)

- WechatWorkExternalApiGetUserBehaviorDataRequest StartTime(long fieldValue)

- WechatWorkExternalApiGetUserBehaviorDataRequest EndTime(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGetUserBehaviorDataResponse (class)

- public string Raw;


## WechatWorkExternalApiGroupChatGetRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupChatGetRequest()

- WechatWorkExternalApiGroupChatGetRequest ChatId(string fieldValue)

- WechatWorkExternalApiGroupChatGetRequest NeedName(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupChatGetResponse (class)

- public string Raw;


## WechatWorkExternalApiGroupChatListRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupChatListRequest()

- WechatWorkExternalApiGroupChatListRequest StatusFilter(int fieldValue)

- WechatWorkExternalApiGroupChatListRequest OwnerFilter(WechatWorkOwnerFilter fieldValue)

- WechatWorkExternalApiGroupChatListRequest Cursor(string fieldValue)

- WechatWorkExternalApiGroupChatListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupChatListResponse (class)

- public string Raw;


## WechatWorkExternalApiGroupChatStatisticGroupByDayRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupChatStatisticGroupByDayRequest()

- WechatWorkExternalApiGroupChatStatisticGroupByDayRequest DayBeginTime(long fieldValue)

- WechatWorkExternalApiGroupChatStatisticGroupByDayRequest DayEndTime(long fieldValue)

- WechatWorkExternalApiGroupChatStatisticGroupByDayRequest OwnerFilter(WechatWorkGroupChatOwnerFilter fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupChatStatisticGroupByDayResponse (class)

- public string Raw;


## WechatWorkExternalApiGroupChatStatisticOwnerRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupChatStatisticOwnerRequest()

- WechatWorkExternalApiGroupChatStatisticOwnerRequest DayBeginTime(long fieldValue)

- WechatWorkExternalApiGroupChatStatisticOwnerRequest DayEndTime(long fieldValue)

- WechatWorkExternalApiGroupChatStatisticOwnerRequest OwnerFilter(WechatWorkGroupChatOwnerFilter fieldValue)

- WechatWorkExternalApiGroupChatStatisticOwnerRequest OrderBy(int fieldValue)

- WechatWorkExternalApiGroupChatStatisticOwnerRequest OrderAsc(int fieldValue)

- WechatWorkExternalApiGroupChatStatisticOwnerRequest Offset(int fieldValue)

- WechatWorkExternalApiGroupChatStatisticOwnerRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupChatStatisticOwnerResponse (class)

- public string Raw;


## WechatWorkExternalApiGroupChat_AddJoinWayRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupChat_AddJoinWayRequest()

- WechatWorkExternalApiGroupChat_AddJoinWayRequest Scene(int fieldValue)

- WechatWorkExternalApiGroupChat_AddJoinWayRequest Remark(string fieldValue)

- WechatWorkExternalApiGroupChat_AddJoinWayRequest AutoCreateRoom(int fieldValue)

- WechatWorkExternalApiGroupChat_AddJoinWayRequest RoomBaseName(string fieldValue)

- WechatWorkExternalApiGroupChat_AddJoinWayRequest RoomBaseId(int fieldValue)

- WechatWorkExternalApiGroupChat_AddJoinWayRequest ChatIdList(List<string> fieldValue)

- WechatWorkExternalApiGroupChat_AddJoinWayRequest State(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupChat_AddJoinWayResponse (class)

- public string Raw;


## WechatWorkExternalApiGroupChat_DelJoinWayRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupChat_DelJoinWayRequest()

- WechatWorkExternalApiGroupChat_DelJoinWayRequest ConfigId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupChat_DelJoinWayResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiGroupChat_GetJoinWayRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupChat_GetJoinWayRequest()

- WechatWorkExternalApiGroupChat_GetJoinWayRequest ConfigId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupChat_GetJoinWayResponse (class)

- public string Raw;


## WechatWorkExternalApiGroupChat_UpdateJoinWayRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupChat_UpdateJoinWayRequest()

- WechatWorkExternalApiGroupChat_UpdateJoinWayRequest ConfigId(string fieldValue)

- WechatWorkExternalApiGroupChat_UpdateJoinWayRequest Scene(int fieldValue)

- WechatWorkExternalApiGroupChat_UpdateJoinWayRequest Remark(string fieldValue)

- WechatWorkExternalApiGroupChat_UpdateJoinWayRequest AutoCreateRoom(int fieldValue)

- WechatWorkExternalApiGroupChat_UpdateJoinWayRequest RoomBaseName(string fieldValue)

- WechatWorkExternalApiGroupChat_UpdateJoinWayRequest RoomBaseId(int fieldValue)

- WechatWorkExternalApiGroupChat_UpdateJoinWayRequest ChatIdList(List<string> fieldValue)

- WechatWorkExternalApiGroupChat_UpdateJoinWayRequest State(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupChat_UpdateJoinWayResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiGroupWelcomeTemplateAddRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupWelcomeTemplateAddRequest()

- WechatWorkExternalApiGroupWelcomeTemplateAddRequest Notify(int fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateAddRequest Text(WechatWorkExternalGetExternalContactResultText fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateAddRequest Image(WechatWorkGroupWelcomeTemplateImage fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateAddRequest Link(WechatWorkExternalExternalJsonAddMessageTemplateRequestLink fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateAddRequest Miniprogram(WechatWorkExternalGetExternalContactResultMiniprogram fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateAddRequest Agentid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupWelcomeTemplateAddResponse (class)

- public string Raw;


## WechatWorkExternalApiGroupWelcomeTemplateDelRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupWelcomeTemplateDelRequest()

- WechatWorkExternalApiGroupWelcomeTemplateDelRequest TemplateId(string fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateDelRequest Agentid(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupWelcomeTemplateDelResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiGroupWelcomeTemplateEditRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupWelcomeTemplateEditRequest()

- WechatWorkExternalApiGroupWelcomeTemplateEditRequest TemplateId(string fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateEditRequest Text(WechatWorkExternalGetExternalContactResultText fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateEditRequest Image(WechatWorkGroupWelcomeTemplateImage fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateEditRequest Link(WechatWorkExternalExternalJsonAddMessageTemplateRequestLink fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateEditRequest Miniprogram(WechatWorkExternalGetExternalContactResultMiniprogram fieldValue)

- WechatWorkExternalApiGroupWelcomeTemplateEditRequest Agentid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupWelcomeTemplateEditResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiGroupWelcomeTemplateGetRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiGroupWelcomeTemplateGetRequest()

- WechatWorkExternalApiGroupWelcomeTemplateGetRequest TemplateId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiGroupWelcomeTemplateGetResponse (class)

- public string Raw;


## WechatWorkExternalApiListContactWayRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiListContactWayRequest()

- WechatWorkExternalApiListContactWayRequest StartTime(int fieldValue)

- WechatWorkExternalApiListContactWayRequest EndTime(int fieldValue)

- WechatWorkExternalApiListContactWayRequest Cursor(string fieldValue)

- WechatWorkExternalApiListContactWayRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiListContactWayResponse (class)

- public string Raw;


## WechatWorkExternalApiSendWelcomeMsgRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiSendWelcomeMsgRequest()

- WechatWorkExternalApiSendWelcomeMsgRequest WelcomeCode(string fieldValue)

- WechatWorkExternalApiSendWelcomeMsgRequest Text(WechatWorkExternalExternalJsonSendWelcomeMsgRequestText fieldValue)

- WechatWorkExternalApiSendWelcomeMsgRequest Attachments(List<WechatWorkExternalExternalJsonSendWelcomeMsgRequestAttachment> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiSendWelcomeMsgResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiTransferExternalRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkExternalApiTransferExternalRequest()

- WechatWorkExternalApiTransferExternalRequest ExternalUserId(string fieldValue)

- WechatWorkExternalApiTransferExternalRequest HandoverUserId(string fieldValue)

- WechatWorkExternalApiTransferExternalRequest TakeoverUserId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiTransferExternalResponse (class)

- public string Raw;


## WechatWorkExternalApiUpdateContactWayRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiUpdateContactWayRequest()

- WechatWorkExternalApiUpdateContactWayRequest ConfigId(string fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest Remark(string fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest SkipVerify(bool fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest Style(int fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest State(string fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest User(List<string> fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest Party(List<int> fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest ExpiresIn(int fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest ChatExpiresIn(int fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest Unionid(string fieldValue)

- WechatWorkExternalApiUpdateContactWayRequest Conclusions(WechatWorkConclusions fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiUpdateContactWayResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkExternalApiUpdateExternalContactRemarkRequest (class)

- WechatTypedRequest request;

- public WechatWorkExternalApiUpdateExternalContactRemarkRequest()

- WechatWorkExternalApiUpdateExternalContactRemarkRequest Userid(string fieldValue)

- WechatWorkExternalApiUpdateExternalContactRemarkRequest ExternalUserid(string fieldValue)

- WechatWorkExternalApiUpdateExternalContactRemarkRequest Remark(string fieldValue)

- WechatWorkExternalApiUpdateExternalContactRemarkRequest Description(string fieldValue)

- WechatWorkExternalApiUpdateExternalContactRemarkRequest RemarkCompany(string fieldValue)

- WechatWorkExternalApiUpdateExternalContactRemarkRequest RemarkMobiles(List<string> fieldValue)

- WechatWorkExternalApiUpdateExternalContactRemarkRequest RemarkPicMediaid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkExternalApiUpdateExternalContactRemarkResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkIdConvertApi (class)

IdConvert/IdConvertApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkIdConvertApi(WechatWorkClient client)

- async WechatWorkIdConvertApiUpgradeChatIdForNewCorpResponse UpgradeChatIdForNewCorpAsync(WechatWorkIdConvertApiUpgradeChatIdForNewCorpRequest request)
  - POST /cgi-bin/idconvert/upgrade_chatid_for_new_corp

- async WechatResponse UpgradeChatIdForNewCorpRawAsync(string query, string jsonBody)


## WechatWorkIdConvertApiUpgradeChatIdForNewCorpRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkIdConvertApiUpgradeChatIdForNewCorpRequest()

- WechatWorkIdConvertApiUpgradeChatIdForNewCorpRequest Request(WechatWorkUpgradeChatIdForNewCorpRequest fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkIdConvertApiUpgradeChatIdForNewCorpResponse (class)

- public string Raw;


## WechatWorkInvoiceApi (class)

Invoice/InvoiceApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkInvoiceApi(WechatWorkClient client)

- async WechatWorkInvoiceApiGetInvoiceInfoResponse GetInvoiceInfoAsync(WechatWorkInvoiceApiGetInvoiceInfoRequest request)
  - POST /cgi-bin/card/invoice/reimburse/getinvoiceinfo

- async WechatResponse GetInvoiceInfoRawAsync(string query, string jsonBody)

- async WechatWorkInvoiceApiGetInvoiceListInfoResponse GetInvoiceListInfoAsync(WechatWorkInvoiceApiGetInvoiceListInfoRequest request)
  - POST /cgi-bin/card/invoice/reimburse/getinvoicebatch

- async WechatResponse GetInvoiceListInfoRawAsync(string query, string jsonBody)

- async WechatWorkInvoiceApiUpdateInvoiceStatusResponse UpdateInvoiceStatusAsync(WechatWorkInvoiceApiUpdateInvoiceStatusRequest request)
  - POST /cgi-bin/card/invoice/reimburse/updateinvoicestatus

- async WechatResponse UpdateInvoiceStatusRawAsync(string query, string jsonBody)

- async WechatWorkInvoiceApiUpdateInvoiceListStatusResponse UpdateInvoiceListStatusAsync(WechatWorkInvoiceApiUpdateInvoiceListStatusRequest request)
  - POST /cgi-bin/card/invoice/reimburse/updatestatusbatch

- async WechatResponse UpdateInvoiceListStatusRawAsync(string query, string jsonBody)


## WechatWorkInvoiceApiGetInvoiceInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkInvoiceApiGetInvoiceInfoRequest()

- WechatWorkInvoiceApiGetInvoiceInfoRequest CardId(string fieldValue)

- WechatWorkInvoiceApiGetInvoiceInfoRequest EncryptCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkInvoiceApiGetInvoiceInfoResponse (class)

- public string Raw;


## WechatWorkInvoiceApiGetInvoiceListInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkInvoiceApiGetInvoiceListInfoRequest()

- WechatWorkInvoiceApiGetInvoiceListInfoRequest ItemList(List<WechatWorkInvoiceItem> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkInvoiceApiGetInvoiceListInfoResponse (class)

- public string Raw;


## WechatWorkInvoiceApiUpdateInvoiceListStatusRequest (class)

- WechatTypedRequest request;

- public WechatWorkInvoiceApiUpdateInvoiceListStatusRequest()

- WechatWorkInvoiceApiUpdateInvoiceListStatusRequest OpenId(string fieldValue)

- WechatWorkInvoiceApiUpdateInvoiceListStatusRequest ReimburseStatus(string fieldValue)

- WechatWorkInvoiceApiUpdateInvoiceListStatusRequest ItemList(List<WechatWorkInvoiceItem> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkInvoiceApiUpdateInvoiceListStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkInvoiceApiUpdateInvoiceStatusRequest (class)

- WechatTypedRequest request;

- public WechatWorkInvoiceApiUpdateInvoiceStatusRequest()

- WechatWorkInvoiceApiUpdateInvoiceStatusRequest CardId(string fieldValue)

- WechatWorkInvoiceApiUpdateInvoiceStatusRequest EncryptCode(string fieldValue)

- WechatWorkInvoiceApiUpdateInvoiceStatusRequest ReimburseStatus(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkInvoiceApiUpdateInvoiceStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkKFApi (class)

KF/KFApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkKFApi(WechatWorkClient client)

- async WechatWorkKFApiSendTextResponse SendTextAsync(WechatWorkKFApiSendTextRequest request)
  - POST /cgi-bin/kf/send

- async WechatResponse SendTextRawAsync(string query, string jsonBody)

- async WechatWorkKFApiSendImageResponse SendImageAsync(WechatWorkKFApiSendImageRequest request)
  - POST /cgi-bin/kf/send

- async WechatResponse SendImageRawAsync(string query, string jsonBody)

- async WechatWorkKFApiSendFileResponse SendFileAsync(WechatWorkKFApiSendFileRequest request)
  - POST /cgi-bin/kf/send

- async WechatResponse SendFileRawAsync(string query, string jsonBody)

- async WechatWorkKFApiSendVoiceResponse SendVoiceAsync(WechatWorkKFApiSendVoiceRequest request)
  - POST /cgi-bin/kf/send

- async WechatResponse SendVoiceRawAsync(string query, string jsonBody)

- async WechatWorkKFApiGetKFListResponse GetKFListAsync(WechatWorkKFApiGetKFListRequest request)
  - GET /cgi-bin/kf/list

- async WechatResponse GetKFListRawAsync(string query)


## WechatWorkKFApiGetKFListRequest (class)

- WechatTypedRequest request;

- public WechatWorkKFApiGetKFListRequest()

- WechatWorkKFApiGetKFListRequest Type(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkKFApiGetKFListResponse (class)

- public string Raw;


## WechatWorkKFApiSendFileRequest (class)

- WechatTypedRequest request;

- public WechatWorkKFApiSendFileRequest()

- WechatWorkKFApiSendFileRequest SenderType(JsonValue fieldValue)

- WechatWorkKFApiSendFileRequest ReceiverType(JsonValue fieldValue)

- WechatWorkKFApiSendFileRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkKFApiSendFileResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkKFApiSendImageRequest (class)

- WechatTypedRequest request;

- public WechatWorkKFApiSendImageRequest()

- WechatWorkKFApiSendImageRequest SenderType(JsonValue fieldValue)

- WechatWorkKFApiSendImageRequest ReceiverType(JsonValue fieldValue)

- WechatWorkKFApiSendImageRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkKFApiSendImageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkKFApiSendTextRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkKFApiSendTextRequest()

- WechatWorkKFApiSendTextRequest SenderType(JsonValue fieldValue)

- WechatWorkKFApiSendTextRequest ReceiverType(JsonValue fieldValue)

- WechatWorkKFApiSendTextRequest Content(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkKFApiSendTextResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkKFApiSendVoiceRequest (class)

- WechatTypedRequest request;

- public WechatWorkKFApiSendVoiceRequest()

- WechatWorkKFApiSendVoiceRequest SenderType(JsonValue fieldValue)

- WechatWorkKFApiSendVoiceRequest ReceiverType(JsonValue fieldValue)

- WechatWorkKFApiSendVoiceRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkKFApiSendVoiceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkLinkedCorpApi (class)

LinkedCorp/LinkedCorpApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkLinkedCorpApi(WechatWorkClient client)

- async WechatWorkLinkedCorpApiGetAgentPermissionListResponse GetAgentPermissionListAsync()
  - POST /cgi-bin/linkedcorp/agent/get_perm_list

- async WechatResponse GetAgentPermissionListRawAsync(string query, string jsonBody)

- async WechatWorkLinkedCorpApiGetUserResponse GetUserAsync(WechatWorkLinkedCorpApiGetUserRequest request)
  - POST /cgi-bin/linkedcorp/user/get

- async WechatResponse GetUserRawAsync(string query, string jsonBody)

- async WechatWorkLinkedCorpApiGetSimpleUserListResponse GetSimpleUserListAsync(WechatWorkLinkedCorpApiGetSimpleUserListRequest request)
  - POST /cgi-bin/linkedcorp/user/simplelist

- async WechatResponse GetSimpleUserListRawAsync(string query, string jsonBody)

- async WechatWorkLinkedCorpApiGetUserListResponse GetUserListAsync(WechatWorkLinkedCorpApiGetUserListRequest request)
  - POST /cgi-bin/linkedcorp/user/list

- async WechatResponse GetUserListRawAsync(string query, string jsonBody)

- async WechatWorkLinkedCorpApiGetDepartmentListResponse GetDepartmentListAsync(WechatWorkLinkedCorpApiGetDepartmentListRequest request)
  - POST /cgi-bin/linkedcorp/department/list

- async WechatResponse GetDepartmentListRawAsync(string query, string jsonBody)


## WechatWorkLinkedCorpApiGetAgentPermissionListResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- public string Raw;


## WechatWorkLinkedCorpApiGetDepartmentListRequest (class)

- WechatTypedRequest request;

- public WechatWorkLinkedCorpApiGetDepartmentListRequest()

- WechatWorkLinkedCorpApiGetDepartmentListRequest DepartmentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLinkedCorpApiGetDepartmentListResponse (class)

- public string Raw;


## WechatWorkLinkedCorpApiGetSimpleUserListRequest (class)

- WechatTypedRequest request;

- public WechatWorkLinkedCorpApiGetSimpleUserListRequest()

- WechatWorkLinkedCorpApiGetSimpleUserListRequest DepartmentId(string fieldValue)

- WechatWorkLinkedCorpApiGetSimpleUserListRequest FetchChild(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLinkedCorpApiGetSimpleUserListResponse (class)

- public string Raw;


## WechatWorkLinkedCorpApiGetUserListRequest (class)

- WechatTypedRequest request;

- public WechatWorkLinkedCorpApiGetUserListRequest()

- WechatWorkLinkedCorpApiGetUserListRequest DepartmentId(string fieldValue)

- WechatWorkLinkedCorpApiGetUserListRequest FetchChild(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLinkedCorpApiGetUserListResponse (class)

- public string Raw;


## WechatWorkLinkedCorpApiGetUserRequest (class)

- WechatTypedRequest request;

- public WechatWorkLinkedCorpApiGetUserRequest()

- WechatWorkLinkedCorpApiGetUserRequest Userid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLinkedCorpApiGetUserResponse (class)

- public string Raw;


## WechatWorkLivingApi (class)

Living/LivingApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkLivingApi(WechatWorkClient client)

- async WechatWorkLivingApiGetUserAllLivingidResponse GetUserAllLivingidAsync(WechatWorkLivingApiGetUserAllLivingidRequest request)
  - POST /cgi-bin/living/get_user_all_livingid

- async WechatResponse GetUserAllLivingidRawAsync(string query, string jsonBody)

- async WechatWorkLivingApiGetLivingInfoResponse GetLivingInfoAsync(WechatWorkLivingApiGetLivingInfoRequest request)
  - GET /cgi-bin/living/get_living_info

- async WechatResponse GetLivingInfoRawAsync(string query)

- async WechatWorkLivingApiGetLivingWatchStateResponse GetLivingWatchStateAsync(WechatWorkLivingApiGetLivingWatchStateRequest request)
  - POST /cgi-bin/living/get_watch_stat

- async WechatResponse GetLivingWatchStateRawAsync(string query, string jsonBody)

- async WechatWorkLivingApiCreateResponse CreateAsync(WechatWorkLivingApiCreateRequest request)
  - POST /cgi-bin/living/create

- async WechatResponse CreateRawAsync(string query, string jsonBody)

- async WechatWorkLivingApiModifyResponse ModifyAsync(WechatWorkLivingApiModifyRequest request)
  - POST /cgi-bin/living/modify

- async WechatResponse ModifyRawAsync(string query, string jsonBody)

- async WechatWorkLivingApiCancelResponse CancelAsync(WechatWorkLivingApiCancelRequest request)
  - POST /cgi-bin/living/cancel

- async WechatResponse CancelRawAsync(string query, string jsonBody)

- async WechatWorkLivingApiDeleteReplayDataResponse DeleteReplayDataAsync(WechatWorkLivingApiDeleteReplayDataRequest request)
  - POST /cgi-bin/living/delete_replay_data

- async WechatResponse DeleteReplayDataRawAsync(string query, string jsonBody)

- async WechatWorkLivingApiGetLivingCodeResponse GetLivingCodeAsync(WechatWorkLivingApiGetLivingCodeRequest request)
  - POST /cgi-bin/living/get_living_code

- async WechatResponse GetLivingCodeRawAsync(string query, string jsonBody)

- async WechatWorkLivingApiGetLivingShareInfoResponse GetLivingShareInfoAsync(WechatWorkLivingApiGetLivingShareInfoRequest request)
  - POST /cgi-bin/living/get_living_share_info

- async WechatResponse GetLivingShareInfoRawAsync(string query, string jsonBody)


## WechatWorkLivingApiCancelRequest (class)

- WechatTypedRequest request;

- public WechatWorkLivingApiCancelRequest()

- WechatWorkLivingApiCancelRequest Livingid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLivingApiCancelResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkLivingApiCreateRequest (class)

- WechatTypedRequest request;

- public WechatWorkLivingApiCreateRequest()

- WechatWorkLivingApiCreateRequest AnchorUserid(string fieldValue)

- WechatWorkLivingApiCreateRequest Theme(string fieldValue)

- WechatWorkLivingApiCreateRequest LivingStart(long fieldValue)

- WechatWorkLivingApiCreateRequest LivingDuration(int fieldValue)

- WechatWorkLivingApiCreateRequest Description(string fieldValue)

- WechatWorkLivingApiCreateRequest Type(int fieldValue)

- WechatWorkLivingApiCreateRequest Agentid(int fieldValue)

- WechatWorkLivingApiCreateRequest RemindTime(int fieldValue)

- WechatWorkLivingApiCreateRequest ActivityCoverMediaid(string fieldValue)

- WechatWorkLivingApiCreateRequest ActivityShareMediaid(string fieldValue)

- WechatWorkLivingApiCreateRequest ActivityDetail(WechatWorkLivingActivityDetail fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLivingApiCreateResponse (class)

- public string Raw;


## WechatWorkLivingApiDeleteReplayDataRequest (class)

- WechatTypedRequest request;

- public WechatWorkLivingApiDeleteReplayDataRequest()

- WechatWorkLivingApiDeleteReplayDataRequest Livingid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLivingApiDeleteReplayDataResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkLivingApiGetLivingCodeRequest (class)

- WechatTypedRequest request;

- public WechatWorkLivingApiGetLivingCodeRequest()

- WechatWorkLivingApiGetLivingCodeRequest Openid(string fieldValue)

- WechatWorkLivingApiGetLivingCodeRequest Livingid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLivingApiGetLivingCodeResponse (class)

- public string Raw;


## WechatWorkLivingApiGetLivingInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkLivingApiGetLivingInfoRequest()

- WechatWorkLivingApiGetLivingInfoRequest Livingid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLivingApiGetLivingInfoResponse (class)

- public string Raw;


## WechatWorkLivingApiGetLivingShareInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkLivingApiGetLivingShareInfoRequest()

- WechatWorkLivingApiGetLivingShareInfoRequest WwShareCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLivingApiGetLivingShareInfoResponse (class)

- public string Raw;


## WechatWorkLivingApiGetLivingWatchStateRequest (class)

- WechatTypedRequest request;

- public WechatWorkLivingApiGetLivingWatchStateRequest()

- WechatWorkLivingApiGetLivingWatchStateRequest Livingid(string fieldValue)

- WechatWorkLivingApiGetLivingWatchStateRequest NextKey(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLivingApiGetLivingWatchStateResponse (class)

- public string Raw;


## WechatWorkLivingApiGetUserAllLivingidRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkLivingApiGetUserAllLivingidRequest()

- WechatWorkLivingApiGetUserAllLivingidRequest Userid(string fieldValue)

- WechatWorkLivingApiGetUserAllLivingidRequest Cursor(string fieldValue)

- WechatWorkLivingApiGetUserAllLivingidRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLivingApiGetUserAllLivingidResponse (class)

- public string Raw;


## WechatWorkLivingApiModifyRequest (class)

- WechatTypedRequest request;

- public WechatWorkLivingApiModifyRequest()

- WechatWorkLivingApiModifyRequest Livingid(string fieldValue)

- WechatWorkLivingApiModifyRequest Theme(string fieldValue)

- WechatWorkLivingApiModifyRequest LivingStart(long fieldValue)

- WechatWorkLivingApiModifyRequest LivingDuration(int fieldValue)

- WechatWorkLivingApiModifyRequest Description(string fieldValue)

- WechatWorkLivingApiModifyRequest Type(int fieldValue)

- WechatWorkLivingApiModifyRequest RemindTime(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLivingApiModifyResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkLoginAuthApi (class)

LoginAuth/LoginAuthApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkLoginAuthApi(WechatWorkClient client)

- async WechatWorkLoginAuthApiGetLoginUrlResponse GetLoginUrlAsync(WechatWorkLoginAuthApiGetLoginUrlRequest request)
  - POST /cgi-bin/service/get_login_url

- async WechatResponse GetLoginUrlRawAsync(string query, string jsonBody)


## WechatWorkLoginAuthApiGetLoginUrlRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkLoginAuthApiGetLoginUrlRequest()

- WechatWorkLoginAuthApiGetLoginUrlRequest LoginTicket(string fieldValue)

- WechatWorkLoginAuthApiGetLoginUrlRequest Target(string fieldValue)

- WechatWorkLoginAuthApiGetLoginUrlRequest Agentid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkLoginAuthApiGetLoginUrlResponse (class)

- public string Raw;


## WechatWorkMailListApi (class)

MailList/MailListApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkMailListApi(WechatWorkClient client)

- async WechatWorkMailListApiCreateMemberResponse CreateMemberAsync(WechatWorkMailListApiCreateMemberRequest request)
  - POST /cgi-bin/user/create

- async WechatResponse CreateMemberRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiGetMemberResponse GetMemberAsync(WechatWorkMailListApiGetMemberRequest request)
  - GET /cgi-bin/user/get

- async WechatResponse GetMemberRawAsync(string query)

- async WechatWorkMailListApiUpdateMemberResponse UpdateMemberAsync(WechatWorkMailListApiUpdateMemberRequest request)
  - POST /cgi-bin/user/update

- async WechatResponse UpdateMemberRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiDeleteMemberResponse DeleteMemberAsync(WechatWorkMailListApiDeleteMemberRequest request)
  - GET /cgi-bin/user/delete

- async WechatResponse DeleteMemberRawAsync(string query)

- async WechatWorkMailListApiBatchDeleteMemberResponse BatchDeleteMemberAsync(WechatWorkMailListApiBatchDeleteMemberRequest request)
  - POST /cgi-bin/user/batchdelete

- async WechatResponse BatchDeleteMemberRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiGetUseridResponse GetUseridAsync(WechatWorkMailListApiGetUseridRequest request)
  - POST /cgi-bin/user/getuserid

- async WechatResponse GetUseridRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiGetUseridByEmailResponse GetUseridByEmailAsync(WechatWorkMailListApiGetUseridByEmailRequest request)
  - POST /cgi-bin/user/get_userid_by_email

- async WechatResponse GetUseridByEmailRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiGetMemberIdListResponse GetMemberIdListAsync(WechatWorkMailListApiGetMemberIdListRequest request)
  - POST /cgi-bin/user/list_id

- async WechatResponse GetMemberIdListRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiGetDepartmentMemberResponse GetDepartmentMemberAsync(WechatWorkMailListApiGetDepartmentMemberRequest request)
  - GET /cgi-bin/user/simplelist

- async WechatResponse GetDepartmentMemberRawAsync(string query)

- async WechatWorkMailListApiGetDepartmentMemberInfoResponse GetDepartmentMemberInfoAsync(WechatWorkMailListApiGetDepartmentMemberInfoRequest request)
  - GET /cgi-bin/user/list

- async WechatResponse GetDepartmentMemberInfoRawAsync(string query)

- async WechatWorkMailListApiCreateDepartmentResponse CreateDepartmentAsync(WechatWorkMailListApiCreateDepartmentRequest request)
  - POST /cgi-bin/department/create

- async WechatResponse CreateDepartmentRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiUpdateDepartmentResponse UpdateDepartmentAsync(WechatWorkMailListApiUpdateDepartmentRequest request)
  - POST /cgi-bin/department/update

- async WechatResponse UpdateDepartmentRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiDeleteDepartmentResponse DeleteDepartmentAsync(WechatWorkMailListApiDeleteDepartmentRequest request)
  - GET /cgi-bin/department/delete

- async WechatResponse DeleteDepartmentRawAsync(string query)

- async WechatWorkMailListApiGetDepartmentListResponse GetDepartmentListAsync(WechatWorkMailListApiGetDepartmentListRequest request)
  - GET /cgi-bin/department/list

- async WechatResponse GetDepartmentListRawAsync(string query)

- async WechatWorkMailListApiGetDepartmentIdListResponse GetDepartmentIdListAsync(WechatWorkMailListApiGetDepartmentIdListRequest request)
  - GET /cgi-bin/department/simplelist

- async WechatResponse GetDepartmentIdListRawAsync(string query)

- async WechatWorkMailListApiCreateTagResponse CreateTagAsync(WechatWorkMailListApiCreateTagRequest request)
  - POST /cgi-bin/tag/create

- async WechatResponse CreateTagRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiUpdateTagResponse UpdateTagAsync(WechatWorkMailListApiUpdateTagRequest request)
  - POST /cgi-bin/tag/update

- async WechatResponse UpdateTagRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiDeleteTagResponse DeleteTagAsync(WechatWorkMailListApiDeleteTagRequest request)
  - GET /cgi-bin/tag/delete

- async WechatResponse DeleteTagRawAsync(string query)

- async WechatWorkMailListApiGetTagMemberResponse GetTagMemberAsync(WechatWorkMailListApiGetTagMemberRequest request)
  - GET /cgi-bin/tag/get

- async WechatResponse GetTagMemberRawAsync(string query)

- async WechatWorkMailListApiAddTagMemberResponse AddTagMemberAsync(WechatWorkMailListApiAddTagMemberRequest request)
  - POST /cgi-bin/tag/addtagusers

- async WechatResponse AddTagMemberRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiDelTagMemberResponse DelTagMemberAsync(WechatWorkMailListApiDelTagMemberRequest request)
  - POST /cgi-bin/tag/deltagusers

- async WechatResponse DelTagMemberRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiGetTagListResponse GetTagListAsync()
  - GET /cgi-bin/tag/list

- async WechatResponse GetTagListRawAsync(string query)

- async WechatWorkMailListApiInviteMemberResponse InviteMemberAsync(WechatWorkMailListApiInviteMemberRequest request)
  - POST /cgi-bin/invite/send

- async WechatResponse InviteMemberRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiInviteResponse InviteAsync(WechatWorkMailListApiInviteRequest request)
  - POST /cgi-bin/batch/invite

- async WechatResponse InviteRawAsync(string query, string jsonBody)

- async WechatWorkMailListApiAuthSuccResponse AuthSuccAsync(WechatWorkMailListApiAuthSuccRequest request)
  - GET /cgi-bin/user/authsucc

- async WechatResponse AuthSuccRawAsync(string query)


## WechatWorkMailListApiAddTagMemberRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiAddTagMemberRequest()

- WechatWorkMailListApiAddTagMemberRequest TagId(int fieldValue)

- WechatWorkMailListApiAddTagMemberRequest UserList(List<string> fieldValue)

- WechatWorkMailListApiAddTagMemberRequest PartyList(List<long> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiAddTagMemberResponse (class)

- public string Raw;


## WechatWorkMailListApiAuthSuccRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiAuthSuccRequest()

- WechatWorkMailListApiAuthSuccRequest UserId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiAuthSuccResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMailListApiBatchDeleteMemberRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiBatchDeleteMemberRequest()

- WechatWorkMailListApiBatchDeleteMemberRequest Useridlist(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiBatchDeleteMemberResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMailListApiCreateDepartmentRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiCreateDepartmentRequest()

- WechatWorkMailListApiCreateDepartmentRequest Name(string fieldValue)

- WechatWorkMailListApiCreateDepartmentRequest ParentId(long fieldValue)

- WechatWorkMailListApiCreateDepartmentRequest Order(long fieldValue)

- WechatWorkMailListApiCreateDepartmentRequest Id(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiCreateDepartmentResponse (class)

- public string Raw;


## WechatWorkMailListApiCreateMemberRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkMailListApiCreateMemberRequest()

- WechatWorkMailListApiCreateMemberRequest ToInvite(bool fieldValue)

- WechatWorkMailListApiCreateMemberRequest Alias(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest Userid(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest Name(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest EnglishName(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest Mobile(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest Department(List<long> fieldValue)

- WechatWorkMailListApiCreateMemberRequest Order(List<long> fieldValue)

- WechatWorkMailListApiCreateMemberRequest Position(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest Gender(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest Email(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest BizMail(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest IsLeaderInDept(List<int> fieldValue)

- WechatWorkMailListApiCreateMemberRequest Enable(int fieldValue)

- WechatWorkMailListApiCreateMemberRequest AvatarMediaid(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest Telephone(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest Extattr(WechatWorkExtattr fieldValue)

- WechatWorkMailListApiCreateMemberRequest Address(string fieldValue)

- WechatWorkMailListApiCreateMemberRequest ExternalProfile(WechatWorkMailListMemberMemberBaseExternalProfile fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiCreateMemberResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMailListApiCreateTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiCreateTagRequest()

- WechatWorkMailListApiCreateTagRequest TagName(string fieldValue)

- WechatWorkMailListApiCreateTagRequest TagId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiCreateTagResponse (class)

- public string Raw;


## WechatWorkMailListApiDelTagMemberRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiDelTagMemberRequest()

- WechatWorkMailListApiDelTagMemberRequest TagId(int fieldValue)

- WechatWorkMailListApiDelTagMemberRequest UserList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiDelTagMemberResponse (class)

- public string Raw;


## WechatWorkMailListApiDeleteDepartmentRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiDeleteDepartmentRequest()

- WechatWorkMailListApiDeleteDepartmentRequest Id(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiDeleteDepartmentResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMailListApiDeleteMemberRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiDeleteMemberRequest()

- WechatWorkMailListApiDeleteMemberRequest UserId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiDeleteMemberResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMailListApiDeleteTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiDeleteTagRequest()

- WechatWorkMailListApiDeleteTagRequest TagId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiDeleteTagResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMailListApiGetDepartmentIdListRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiGetDepartmentIdListRequest()

- WechatWorkMailListApiGetDepartmentIdListRequest Id(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiGetDepartmentIdListResponse (class)

- public string Raw;


## WechatWorkMailListApiGetDepartmentListRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiGetDepartmentListRequest()

- WechatWorkMailListApiGetDepartmentListRequest Id(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiGetDepartmentListResponse (class)

- public string Raw;


## WechatWorkMailListApiGetDepartmentMemberInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiGetDepartmentMemberInfoRequest()

- WechatWorkMailListApiGetDepartmentMemberInfoRequest DepartmentId(long fieldValue)

- WechatWorkMailListApiGetDepartmentMemberInfoRequest FetchChild(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiGetDepartmentMemberInfoResponse (class)

- public string Raw;


## WechatWorkMailListApiGetDepartmentMemberRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiGetDepartmentMemberRequest()

- WechatWorkMailListApiGetDepartmentMemberRequest DepartmentId(long fieldValue)

- WechatWorkMailListApiGetDepartmentMemberRequest FetchChild(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiGetDepartmentMemberResponse (class)

- public string Raw;


## WechatWorkMailListApiGetMemberIdListRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiGetMemberIdListRequest()

- WechatWorkMailListApiGetMemberIdListRequest Cursor(string fieldValue)

- WechatWorkMailListApiGetMemberIdListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiGetMemberIdListResponse (class)

- public string Raw;


## WechatWorkMailListApiGetMemberRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiGetMemberRequest()

- WechatWorkMailListApiGetMemberRequest UserId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiGetMemberResponse (class)

- public string Raw;


## WechatWorkMailListApiGetTagListResponse (class)

- public string Raw;


## WechatWorkMailListApiGetTagMemberRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiGetTagMemberRequest()

- WechatWorkMailListApiGetTagMemberRequest TagId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiGetTagMemberResponse (class)

- public string Raw;


## WechatWorkMailListApiGetUseridByEmailRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiGetUseridByEmailRequest()

- WechatWorkMailListApiGetUseridByEmailRequest Email(string fieldValue)

- WechatWorkMailListApiGetUseridByEmailRequest EmailType(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiGetUseridByEmailResponse (class)

- public string Raw;


## WechatWorkMailListApiGetUseridRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiGetUseridRequest()

- WechatWorkMailListApiGetUseridRequest Mobile(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiGetUseridResponse (class)

- public string Raw;


## WechatWorkMailListApiInviteMemberRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiInviteMemberRequest()

- WechatWorkMailListApiInviteMemberRequest UserId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiInviteMemberResponse (class)

- public string Raw;


## WechatWorkMailListApiInviteRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiInviteRequest()

- WechatWorkMailListApiInviteRequest User(List<string> fieldValue)

- WechatWorkMailListApiInviteRequest Party(List<string> fieldValue)

- WechatWorkMailListApiInviteRequest Tag(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiInviteResponse (class)

- public string Raw;


## WechatWorkMailListApiUpdateDepartmentRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiUpdateDepartmentRequest()

- WechatWorkMailListApiUpdateDepartmentRequest Id(long fieldValue)

- WechatWorkMailListApiUpdateDepartmentRequest Name(string fieldValue)

- WechatWorkMailListApiUpdateDepartmentRequest ParentId(long fieldValue)

- WechatWorkMailListApiUpdateDepartmentRequest Order(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiUpdateDepartmentResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMailListApiUpdateMemberRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiUpdateMemberRequest()

- WechatWorkMailListApiUpdateMemberRequest NewUserid(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Alias(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Userid(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Name(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest EnglishName(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Mobile(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Department(List<long> fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Order(List<long> fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Position(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Gender(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Email(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest BizMail(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest IsLeaderInDept(List<int> fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Enable(int fieldValue)

- WechatWorkMailListApiUpdateMemberRequest AvatarMediaid(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Telephone(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Extattr(WechatWorkExtattr fieldValue)

- WechatWorkMailListApiUpdateMemberRequest Address(string fieldValue)

- WechatWorkMailListApiUpdateMemberRequest ExternalProfile(WechatWorkMailListMemberMemberBaseExternalProfile fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiUpdateMemberResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMailListApiUpdateTagRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListApiUpdateTagRequest()

- WechatWorkMailListApiUpdateTagRequest TagId(int fieldValue)

- WechatWorkMailListApiUpdateTagRequest TagName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListApiUpdateTagResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMailListCurrentApi (class)

MailList/MailListCurrentApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkMailListCurrentApi(WechatWorkClient client)

- async WechatWorkMailListCurrentApiGetJoinQrcodeResponse GetJoinQrcodeAsync(WechatWorkMailListCurrentApiGetJoinQrcodeRequest request)
  - GET /cgi-bin/corp/get_join_qrcode

- async WechatResponse GetJoinQrcodeRawAsync(string query)

- async WechatWorkMailListCurrentApiGetDepartmentResponse GetDepartmentAsync(WechatWorkMailListCurrentApiGetDepartmentRequest request)
  - GET /cgi-bin/department/get

- async WechatResponse GetDepartmentRawAsync(string query)


## WechatWorkMailListCurrentApiGetDepartmentRequest (class)

- WechatTypedRequest request;

- public WechatWorkMailListCurrentApiGetDepartmentRequest()

- WechatWorkMailListCurrentApiGetDepartmentRequest Id(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListCurrentApiGetDepartmentResponse (class)

- public string Raw;


## WechatWorkMailListCurrentApiGetJoinQrcodeRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkMailListCurrentApiGetJoinQrcodeRequest()

- WechatWorkMailListCurrentApiGetJoinQrcodeRequest SizeType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMailListCurrentApiGetJoinQrcodeResponse (class)

- public string Raw;


## WechatWorkMassApi (class)

Mass/MassApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkMassApi(WechatWorkClient client)

- async WechatWorkMassApiSendTextResponse SendTextAsync(WechatWorkMassApiSendTextRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendTextRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendImageResponse SendImageAsync(WechatWorkMassApiSendImageRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendImageRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendVoiceResponse SendVoiceAsync(WechatWorkMassApiSendVoiceRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendVoiceRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendVideoResponse SendVideoAsync(WechatWorkMassApiSendVideoRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendVideoRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendFileResponse SendFileAsync(WechatWorkMassApiSendFileRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendFileRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendNewsResponse SendNewsAsync(WechatWorkMassApiSendNewsRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendNewsRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendMpNewsResponse SendMpNewsAsync(WechatWorkMassApiSendMpNewsRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendMpNewsRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendTextCardResponse SendTextCardAsync(WechatWorkMassApiSendTextCardRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendTextCardRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendMarkdownResponse SendMarkdownAsync(WechatWorkMassApiSendMarkdownRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendMarkdownRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendMiniNoticeCardResponse SendMiniNoticeCardAsync(WechatWorkMassApiSendMiniNoticeCardRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendMiniNoticeCardRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendTaskCardResponse SendTaskCardAsync(WechatWorkMassApiSendTaskCardRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendTaskCardRawAsync(string query, string jsonBody)

- async WechatWorkMassApiSendTemplateCardResponse SendTemplateCardAsync(WechatWorkMassApiSendTemplateCardRequest request)
  - POST /cgi-bin/message/send

- async WechatResponse SendTemplateCardRawAsync(string query, string jsonBody)

- async WechatWorkMassApiUpdateTemplateCardResponse UpdateTemplateCardAsync(WechatWorkMassApiUpdateTemplateCardRequest request)
  - POST /cgi-bin/message/update_template_card

- async WechatResponse UpdateTemplateCardRawAsync(string query, string jsonBody)

- async WechatWorkMassApiUpdateTaskCardResponse UpdateTaskCardAsync(WechatWorkMassApiUpdateTaskCardRequest request)
  - POST /cgi-bin/message/update_taskcard

- async WechatResponse UpdateTaskCardRawAsync(string query, string jsonBody)

- async WechatWorkMassApiRecallResponse RecallAsync(WechatWorkMassApiRecallRequest request)
  - POST /cgi-bin/message/recall

- async WechatResponse RecallRawAsync(string query, string jsonBody)


## WechatWorkMassApiRecallRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiRecallRequest()

- WechatWorkMassApiRecallRequest MsgId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiRecallResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMassApiSendFileRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendFileRequest()

- WechatWorkMassApiSendFileRequest ToUser(string fieldValue)

- WechatWorkMassApiSendFileRequest ToParty(string fieldValue)

- WechatWorkMassApiSendFileRequest ToTag(string fieldValue)

- WechatWorkMassApiSendFileRequest AgentId(string fieldValue)

- WechatWorkMassApiSendFileRequest MediaId(string fieldValue)

- WechatWorkMassApiSendFileRequest Safe(int fieldValue)

- WechatWorkMassApiSendFileRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendFileRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendFileResponse (class)

- public string Raw;


## WechatWorkMassApiSendImageRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendImageRequest()

- WechatWorkMassApiSendImageRequest ToUser(string fieldValue)

- WechatWorkMassApiSendImageRequest ToParty(string fieldValue)

- WechatWorkMassApiSendImageRequest ToTag(string fieldValue)

- WechatWorkMassApiSendImageRequest AgentId(string fieldValue)

- WechatWorkMassApiSendImageRequest MediaId(string fieldValue)

- WechatWorkMassApiSendImageRequest Safe(int fieldValue)

- WechatWorkMassApiSendImageRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendImageRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendImageResponse (class)

- public string Raw;


## WechatWorkMassApiSendMarkdownRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendMarkdownRequest()

- WechatWorkMassApiSendMarkdownRequest ToUser(string fieldValue)

- WechatWorkMassApiSendMarkdownRequest ToParty(string fieldValue)

- WechatWorkMassApiSendMarkdownRequest ToTag(string fieldValue)

- WechatWorkMassApiSendMarkdownRequest AgentId(string fieldValue)

- WechatWorkMassApiSendMarkdownRequest Content(string fieldValue)

- WechatWorkMassApiSendMarkdownRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendMarkdownRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendMarkdownResponse (class)

- public string Raw;


## WechatWorkMassApiSendMiniNoticeCardRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendMiniNoticeCardRequest()

- WechatWorkMassApiSendMiniNoticeCardRequest Touser(string fieldValue)

- WechatWorkMassApiSendMiniNoticeCardRequest Toparty(string fieldValue)

- WechatWorkMassApiSendMiniNoticeCardRequest Totag(string fieldValue)

- WechatWorkMassApiSendMiniNoticeCardRequest Msgtype(string fieldValue)

- WechatWorkMassApiSendMiniNoticeCardRequest MiniprogramNotice(WechatWorkMiniprogramNotice fieldValue)

- WechatWorkMassApiSendMiniNoticeCardRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendMiniNoticeCardRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendMiniNoticeCardResponse (class)

- public string Raw;


## WechatWorkMassApiSendMpNewsRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendMpNewsRequest()

- WechatWorkMassApiSendMpNewsRequest ToUser(string fieldValue)

- WechatWorkMassApiSendMpNewsRequest ToParty(string fieldValue)

- WechatWorkMassApiSendMpNewsRequest ToTag(string fieldValue)

- WechatWorkMassApiSendMpNewsRequest AgentId(string fieldValue)

- WechatWorkMassApiSendMpNewsRequest Articles(JsonValue fieldValue)

- WechatWorkMassApiSendMpNewsRequest Safe(int fieldValue)

- WechatWorkMassApiSendMpNewsRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendMpNewsRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendMpNewsResponse (class)

- public string Raw;


## WechatWorkMassApiSendNewsRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendNewsRequest()

- WechatWorkMassApiSendNewsRequest ToUser(string fieldValue)

- WechatWorkMassApiSendNewsRequest ToParty(string fieldValue)

- WechatWorkMassApiSendNewsRequest ToTag(string fieldValue)

- WechatWorkMassApiSendNewsRequest AgentId(string fieldValue)

- WechatWorkMassApiSendNewsRequest Articles(List<WechatWorkArticle> fieldValue)

- WechatWorkMassApiSendNewsRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendNewsRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendNewsResponse (class)

- public string Raw;


## WechatWorkMassApiSendTaskCardRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendTaskCardRequest()

- WechatWorkMassApiSendTaskCardRequest Touser(string fieldValue)

- WechatWorkMassApiSendTaskCardRequest Toparty(string fieldValue)

- WechatWorkMassApiSendTaskCardRequest Totag(string fieldValue)

- WechatWorkMassApiSendTaskCardRequest Agentid(int fieldValue)

- WechatWorkMassApiSendTaskCardRequest InteractiveTaskcard(WechatWorkTaskcardNotice fieldValue)

- WechatWorkMassApiSendTaskCardRequest Taskcard(WechatWorkTaskcardNotice fieldValue)

- WechatWorkMassApiSendTaskCardRequest EnableIdTrans(int fieldValue)

- WechatWorkMassApiSendTaskCardRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendTaskCardRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendTaskCardResponse (class)

- public string Raw;


## WechatWorkMassApiSendTemplateCardRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendTemplateCardRequest()

- WechatWorkMassApiSendTemplateCardRequest Touser(string fieldValue)

- WechatWorkMassApiSendTemplateCardRequest Toparty(string fieldValue)

- WechatWorkMassApiSendTemplateCardRequest Totag(string fieldValue)

- WechatWorkMassApiSendTemplateCardRequest Msgtype(string fieldValue)

- WechatWorkMassApiSendTemplateCardRequest Agentid(int fieldValue)

- WechatWorkMassApiSendTemplateCardRequest TemplateCard(WechatWorkTemplateCardBase fieldValue)

- WechatWorkMassApiSendTemplateCardRequest EnableIdTrans(int fieldValue)

- WechatWorkMassApiSendTemplateCardRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendTemplateCardRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendTemplateCardResponse (class)

- public string Raw;


## WechatWorkMassApiSendTextCardRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendTextCardRequest()

- WechatWorkMassApiSendTextCardRequest ToUser(string fieldValue)

- WechatWorkMassApiSendTextCardRequest ToParty(string fieldValue)

- WechatWorkMassApiSendTextCardRequest ToTag(string fieldValue)

- WechatWorkMassApiSendTextCardRequest AgentId(string fieldValue)

- WechatWorkMassApiSendTextCardRequest Title(string fieldValue)

- WechatWorkMassApiSendTextCardRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendTextCardRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendTextCardResponse (class)

- public string Raw;


## WechatWorkMassApiSendTextRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkMassApiSendTextRequest()

- WechatWorkMassApiSendTextRequest ToUser(string fieldValue)

- WechatWorkMassApiSendTextRequest ToParty(string fieldValue)

- WechatWorkMassApiSendTextRequest ToTag(string fieldValue)

- WechatWorkMassApiSendTextRequest AgentId(string fieldValue)

- WechatWorkMassApiSendTextRequest Content(string fieldValue)

- WechatWorkMassApiSendTextRequest Safe(int fieldValue)

- WechatWorkMassApiSendTextRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendTextRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendTextResponse (class)

- public string Raw;


## WechatWorkMassApiSendVideoRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendVideoRequest()

- WechatWorkMassApiSendVideoRequest ToUser(string fieldValue)

- WechatWorkMassApiSendVideoRequest ToParty(string fieldValue)

- WechatWorkMassApiSendVideoRequest ToTag(string fieldValue)

- WechatWorkMassApiSendVideoRequest AgentId(string fieldValue)

- WechatWorkMassApiSendVideoRequest MediaId(string fieldValue)

- WechatWorkMassApiSendVideoRequest Safe(int fieldValue)

- WechatWorkMassApiSendVideoRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendVideoRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendVideoResponse (class)

- public string Raw;


## WechatWorkMassApiSendVoiceRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiSendVoiceRequest()

- WechatWorkMassApiSendVoiceRequest ToUser(string fieldValue)

- WechatWorkMassApiSendVoiceRequest ToParty(string fieldValue)

- WechatWorkMassApiSendVoiceRequest ToTag(string fieldValue)

- WechatWorkMassApiSendVoiceRequest AgentId(string fieldValue)

- WechatWorkMassApiSendVoiceRequest MediaId(string fieldValue)

- WechatWorkMassApiSendVoiceRequest Safe(int fieldValue)

- WechatWorkMassApiSendVoiceRequest EnableDuplicateCheck(int fieldValue)

- WechatWorkMassApiSendVoiceRequest DuplicateCheckInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiSendVoiceResponse (class)

- public string Raw;


## WechatWorkMassApiUpdateTaskCardRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiUpdateTaskCardRequest()

- WechatWorkMassApiUpdateTaskCardRequest Userids(List<string> fieldValue)

- WechatWorkMassApiUpdateTaskCardRequest Agentid(int fieldValue)

- WechatWorkMassApiUpdateTaskCardRequest TaskId(string fieldValue)

- WechatWorkMassApiUpdateTaskCardRequest ClickedKey(string fieldValue)

- WechatWorkMassApiUpdateTaskCardRequest ReplaceName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiUpdateTaskCardResponse (class)

- public string Raw;


## WechatWorkMassApiUpdateTemplateCardRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassApiUpdateTemplateCardRequest()

- WechatWorkMassApiUpdateTemplateCardRequest Userids(List<string> fieldValue)

- WechatWorkMassApiUpdateTemplateCardRequest Partyids(List<int> fieldValue)

- WechatWorkMassApiUpdateTemplateCardRequest Tagids(List<int> fieldValue)

- WechatWorkMassApiUpdateTemplateCardRequest Atall(int fieldValue)

- WechatWorkMassApiUpdateTemplateCardRequest Agentid(int fieldValue)

- WechatWorkMassApiUpdateTemplateCardRequest ResponseCode(string fieldValue)

- WechatWorkMassApiUpdateTemplateCardRequest EnableIdTrans(int fieldValue)

- WechatWorkMassApiUpdateTemplateCardRequest TemplateCard(WechatWorkTemplateCardBase fieldValue)

- WechatWorkMassApiUpdateTemplateCardRequest Button(WechatWorkMassUpdateTemplateCardUpdateTemplateCardRequestButton fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassApiUpdateTemplateCardResponse (class)

- public string Raw;


## WechatWorkMassLinkerCorpApi (class)

Mass/LinkerCorp/LinkerCorpApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkMassLinkerCorpApi(WechatWorkClient client)

- async WechatWorkMassLinkerCorpApiSendTextResponse SendTextAsync(WechatWorkMassLinkerCorpApiSendTextRequest request)
  - POST /cgi-bin/linkedcorp/message/send

- async WechatResponse SendTextRawAsync(string query, string jsonBody)

- async WechatWorkMassLinkerCorpApiSendImageResponse SendImageAsync(WechatWorkMassLinkerCorpApiSendImageRequest request)
  - POST /cgi-bin/linkedcorp/message/send

- async WechatResponse SendImageRawAsync(string query, string jsonBody)

- async WechatWorkMassLinkerCorpApiSendVoiceResponse SendVoiceAsync(WechatWorkMassLinkerCorpApiSendVoiceRequest request)
  - POST /cgi-bin/linkedcorp/message/send

- async WechatResponse SendVoiceRawAsync(string query, string jsonBody)

- async WechatWorkMassLinkerCorpApiSendVideoResponse SendVideoAsync(WechatWorkMassLinkerCorpApiSendVideoRequest request)
  - POST /cgi-bin/linkedcorp/message/send

- async WechatResponse SendVideoRawAsync(string query, string jsonBody)

- async WechatWorkMassLinkerCorpApiSendFileResponse SendFileAsync(WechatWorkMassLinkerCorpApiSendFileRequest request)
  - POST /cgi-bin/linkedcorp/message/send

- async WechatResponse SendFileRawAsync(string query, string jsonBody)

- async WechatWorkMassLinkerCorpApiSendNewsResponse SendNewsAsync(WechatWorkMassLinkerCorpApiSendNewsRequest request)
  - POST /cgi-bin/linkedcorp/message/send

- async WechatResponse SendNewsRawAsync(string query, string jsonBody)

- async WechatWorkMassLinkerCorpApiSendMpNewsResponse SendMpNewsAsync(WechatWorkMassLinkerCorpApiSendMpNewsRequest request)
  - POST /cgi-bin/linkedcorp/message/send

- async WechatResponse SendMpNewsRawAsync(string query, string jsonBody)

- async WechatWorkMassLinkerCorpApiSendTextCardResponse SendTextCardAsync(WechatWorkMassLinkerCorpApiSendTextCardRequest request)
  - POST /cgi-bin/linkedcorp/message/send

- async WechatResponse SendTextCardRawAsync(string query, string jsonBody)

- async WechatWorkMassLinkerCorpApiSendMiniNoticeCardResponse SendMiniNoticeCardAsync(WechatWorkMassLinkerCorpApiSendMiniNoticeCardRequest request)
  - POST /cgi-bin/linkedcorp/message/send

- async WechatResponse SendMiniNoticeCardRawAsync(string query, string jsonBody)


## WechatWorkMassLinkerCorpApiSendFileRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassLinkerCorpApiSendFileRequest()

- WechatWorkMassLinkerCorpApiSendFileRequest Touser(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendFileRequest Toparty(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendFileRequest Totag(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendFileRequest Toall(int fieldValue)

- WechatWorkMassLinkerCorpApiSendFileRequest Msgtype(string fieldValue)

- WechatWorkMassLinkerCorpApiSendFileRequest Agentid(int fieldValue)

- WechatWorkMassLinkerCorpApiSendFileRequest File(WechatWorkMassLinkerCorpLinkerCorpDataFile fieldValue)

- WechatWorkMassLinkerCorpApiSendFileRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassLinkerCorpApiSendFileResponse (class)

- public string Raw;


## WechatWorkMassLinkerCorpApiSendImageRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassLinkerCorpApiSendImageRequest()

- WechatWorkMassLinkerCorpApiSendImageRequest Touser(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendImageRequest Toparty(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendImageRequest Totag(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendImageRequest Toall(int fieldValue)

- WechatWorkMassLinkerCorpApiSendImageRequest Msgtype(string fieldValue)

- WechatWorkMassLinkerCorpApiSendImageRequest Agentid(int fieldValue)

- WechatWorkMassLinkerCorpApiSendImageRequest Image(WechatWorkMassLinkerCorpLinkerCorpDataImage fieldValue)

- WechatWorkMassLinkerCorpApiSendImageRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassLinkerCorpApiSendImageResponse (class)

- public string Raw;


## WechatWorkMassLinkerCorpApiSendMiniNoticeCardRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassLinkerCorpApiSendMiniNoticeCardRequest()

- WechatWorkMassLinkerCorpApiSendMiniNoticeCardRequest Touser(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendMiniNoticeCardRequest Toparty(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendMiniNoticeCardRequest Totag(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendMiniNoticeCardRequest Msgtype(string fieldValue)

- WechatWorkMassLinkerCorpApiSendMiniNoticeCardRequest MiniprogramNotice(WechatWorkMiniprogramNotice fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassLinkerCorpApiSendMiniNoticeCardResponse (class)

- public string Raw;


## WechatWorkMassLinkerCorpApiSendMpNewsRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassLinkerCorpApiSendMpNewsRequest()

- WechatWorkMassLinkerCorpApiSendMpNewsRequest Touser(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendMpNewsRequest Toparty(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendMpNewsRequest Totag(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendMpNewsRequest Toall(int fieldValue)

- WechatWorkMassLinkerCorpApiSendMpNewsRequest Msgtype(string fieldValue)

- WechatWorkMassLinkerCorpApiSendMpNewsRequest Agentid(int fieldValue)

- WechatWorkMassLinkerCorpApiSendMpNewsRequest Mpnews(WechatWorkMpnews fieldValue)

- WechatWorkMassLinkerCorpApiSendMpNewsRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassLinkerCorpApiSendMpNewsResponse (class)

- public string Raw;


## WechatWorkMassLinkerCorpApiSendNewsRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassLinkerCorpApiSendNewsRequest()

- WechatWorkMassLinkerCorpApiSendNewsRequest Touser(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendNewsRequest Toparty(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendNewsRequest Totag(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendNewsRequest Toall(int fieldValue)

- WechatWorkMassLinkerCorpApiSendNewsRequest Msgtype(string fieldValue)

- WechatWorkMassLinkerCorpApiSendNewsRequest Agentid(int fieldValue)

- WechatWorkMassLinkerCorpApiSendNewsRequest News(WechatWorkMassLinkerCorpLinkerCorpDataNews fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassLinkerCorpApiSendNewsResponse (class)

- public string Raw;


## WechatWorkMassLinkerCorpApiSendTextCardRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassLinkerCorpApiSendTextCardRequest()

- WechatWorkMassLinkerCorpApiSendTextCardRequest Touser(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendTextCardRequest Toparty(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendTextCardRequest Totag(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendTextCardRequest Toall(int fieldValue)

- WechatWorkMassLinkerCorpApiSendTextCardRequest Msgtype(string fieldValue)

- WechatWorkMassLinkerCorpApiSendTextCardRequest Agentid(int fieldValue)

- WechatWorkMassLinkerCorpApiSendTextCardRequest Textcard(WechatWorkTextcard fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassLinkerCorpApiSendTextCardResponse (class)

- public string Raw;


## WechatWorkMassLinkerCorpApiSendTextRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkMassLinkerCorpApiSendTextRequest()

- WechatWorkMassLinkerCorpApiSendTextRequest Touser(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendTextRequest Toparty(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendTextRequest Totag(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendTextRequest Toall(int fieldValue)

- WechatWorkMassLinkerCorpApiSendTextRequest Msgtype(string fieldValue)

- WechatWorkMassLinkerCorpApiSendTextRequest Agentid(int fieldValue)

- WechatWorkMassLinkerCorpApiSendTextRequest Text(WechatWorkMassLinkerCorpLinkerCorpDataText fieldValue)

- WechatWorkMassLinkerCorpApiSendTextRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassLinkerCorpApiSendTextResponse (class)

- public string Raw;


## WechatWorkMassLinkerCorpApiSendVideoRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassLinkerCorpApiSendVideoRequest()

- WechatWorkMassLinkerCorpApiSendVideoRequest Touser(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendVideoRequest Toparty(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendVideoRequest Totag(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendVideoRequest Toall(int fieldValue)

- WechatWorkMassLinkerCorpApiSendVideoRequest Msgtype(string fieldValue)

- WechatWorkMassLinkerCorpApiSendVideoRequest Agentid(int fieldValue)

- WechatWorkMassLinkerCorpApiSendVideoRequest Video(WechatWorkMassLinkerCorpLinkerCorpDataVideo fieldValue)

- WechatWorkMassLinkerCorpApiSendVideoRequest Safe(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassLinkerCorpApiSendVideoResponse (class)

- public string Raw;


## WechatWorkMassLinkerCorpApiSendVoiceRequest (class)

- WechatTypedRequest request;

- public WechatWorkMassLinkerCorpApiSendVoiceRequest()

- WechatWorkMassLinkerCorpApiSendVoiceRequest Touser(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendVoiceRequest Toparty(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendVoiceRequest Totag(List<string> fieldValue)

- WechatWorkMassLinkerCorpApiSendVoiceRequest Toall(int fieldValue)

- WechatWorkMassLinkerCorpApiSendVoiceRequest Msgtype(string fieldValue)

- WechatWorkMassLinkerCorpApiSendVoiceRequest Agentid(int fieldValue)

- WechatWorkMassLinkerCorpApiSendVoiceRequest Voice(WechatWorkVoice fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMassLinkerCorpApiSendVoiceResponse (class)

- public string Raw;


## WechatWorkMediaApi (class)

Media/MediaApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkMediaApi(WechatWorkClient client)

- async WechatWorkMediaApiAddMpNewsResponse AddMpNewsAsync(WechatWorkMediaApiAddMpNewsRequest request)
  - POST /cgi-bin/material/add_mpnews

- async WechatResponse AddMpNewsRawAsync(string query, string jsonBody)

- async WechatWorkMediaApiGetForeverMpNewsResponse GetForeverMpNewsAsync(WechatWorkMediaApiGetForeverMpNewsRequest request)
  - GET /cgi-bin/material/get

- async WechatResponse GetForeverMpNewsRawAsync(string query)

- async WechatWorkMediaApiDeleteForeverMaterialResponse DeleteForeverMaterialAsync(WechatWorkMediaApiDeleteForeverMaterialRequest request)
  - GET /cgi-bin/material/del

- async WechatResponse DeleteForeverMaterialRawAsync(string query)

- async WechatWorkMediaApiUpdateMpNewsResponse UpdateMpNewsAsync(WechatWorkMediaApiUpdateMpNewsRequest request)
  - POST /cgi-bin/material/update_mpnews

- async WechatResponse UpdateMpNewsRawAsync(string query, string jsonBody)

- async WechatWorkMediaApiGetCountResponse GetCountAsync(WechatWorkMediaApiGetCountRequest request)
  - GET /cgi-bin/material/get_count

- async WechatResponse GetCountRawAsync(string query)

- async WechatWorkMediaApiBatchGetMaterialResponse BatchGetMaterialAsync(WechatWorkMediaApiBatchGetMaterialRequest request)
  - POST /cgi-bin/material/batchget

- async WechatResponse BatchGetMaterialRawAsync(string query, string jsonBody)


## WechatWorkMediaApiAddMpNewsRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkMediaApiAddMpNewsRequest()

- WechatWorkMediaApiAddMpNewsRequest AgentId(int fieldValue)

- WechatWorkMediaApiAddMpNewsRequest MpNewsArticles(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMediaApiAddMpNewsResponse (class)

- public string Raw;


## WechatWorkMediaApiBatchGetMaterialRequest (class)

- WechatTypedRequest request;

- public WechatWorkMediaApiBatchGetMaterialRequest()

- WechatWorkMediaApiBatchGetMaterialRequest Type(JsonValue fieldValue)

- WechatWorkMediaApiBatchGetMaterialRequest AgentId(int fieldValue)

- WechatWorkMediaApiBatchGetMaterialRequest Offset(int fieldValue)

- WechatWorkMediaApiBatchGetMaterialRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMediaApiBatchGetMaterialResponse (class)

- public string Raw;


## WechatWorkMediaApiDeleteForeverMaterialRequest (class)

- WechatTypedRequest request;

- public WechatWorkMediaApiDeleteForeverMaterialRequest()

- WechatWorkMediaApiDeleteForeverMaterialRequest AgentId(int fieldValue)

- WechatWorkMediaApiDeleteForeverMaterialRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMediaApiDeleteForeverMaterialResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkMediaApiGetCountRequest (class)

- WechatTypedRequest request;

- public WechatWorkMediaApiGetCountRequest()

- WechatWorkMediaApiGetCountRequest AgentId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMediaApiGetCountResponse (class)

- public string Raw;


## WechatWorkMediaApiGetForeverMpNewsRequest (class)

- WechatTypedRequest request;

- public WechatWorkMediaApiGetForeverMpNewsRequest()

- WechatWorkMediaApiGetForeverMpNewsRequest AgentId(int fieldValue)

- WechatWorkMediaApiGetForeverMpNewsRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMediaApiGetForeverMpNewsResponse (class)

- public string Raw;


## WechatWorkMediaApiUpdateMpNewsRequest (class)

- WechatTypedRequest request;

- public WechatWorkMediaApiUpdateMpNewsRequest()

- WechatWorkMediaApiUpdateMpNewsRequest AgentId(int fieldValue)

- WechatWorkMediaApiUpdateMpNewsRequest MediaId(string fieldValue)

- WechatWorkMediaApiUpdateMpNewsRequest MpNewsArticles(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMediaApiUpdateMpNewsResponse (class)

- public string Raw;


## WechatWorkMediaSpecialApi (class)

企业微信媒体上传、下载和永久素材特殊传输接口。

- WechatWorkClient client;

- public WechatWorkMediaSpecialApi(WechatWorkClient client)

- async WechatResponse UploadAsync(string mediaType, string fileName, string contentType, string fileBytes)

- async WechatRawResponse GetAsync(string mediaId)

- async WechatResponse AddMaterialAsync(string mediaType, int agentId, string fileName, string contentType, string fileBytes)

- async WechatRawResponse GetForeverMaterialAsync(int agentId, string mediaId)

- async WechatResponse UploadimgMediaAsync(string fileName, string contentType, string fileBytes)

- async WechatResponse UploadAttachmentAsync(string mediaType, string attachmentType, string fileName, string contentType, string fileBytes)


## WechatWorkMiniProgramMiniApi (class)

MiniProgram/MiniApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkMiniProgramMiniApi(WechatWorkClient client)

- async WechatWorkMiniProgramMiniApiLoginCheckResponse LoginCheckAsync(WechatWorkMiniProgramMiniApiLoginCheckRequest request)
  - GET /cgi-bin/miniprogram/jscode2session

- async WechatResponse LoginCheckRawAsync(string query)

- async WechatWorkMiniProgramMiniApiThirdLoginCheckResponse ThirdLoginCheckAsync(WechatWorkMiniProgramMiniApiThirdLoginCheckRequest request)
  - GET /cgi-bin/service/miniprogram/jscode2session

- async WechatResponse ThirdLoginCheckRawAsync(string query)

- async WechatWorkMiniProgramMiniApiTransferSessionResponse TransferSessionAsync(WechatWorkMiniProgramMiniApiTransferSessionRequest request)
  - POST /cgi-bin/miniprogram/transfer_session

- async WechatResponse TransferSessionRawAsync(string query, string jsonBody)


## WechatWorkMiniProgramMiniApiLoginCheckRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkMiniProgramMiniApiLoginCheckRequest()

- WechatWorkMiniProgramMiniApiLoginCheckRequest Code(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMiniProgramMiniApiLoginCheckResponse (class)

- public string Raw;


## WechatWorkMiniProgramMiniApiThirdLoginCheckRequest (class)

- WechatTypedRequest request;

- public WechatWorkMiniProgramMiniApiThirdLoginCheckRequest()

- WechatWorkMiniProgramMiniApiThirdLoginCheckRequest Code(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMiniProgramMiniApiThirdLoginCheckResponse (class)

- public string Raw;


## WechatWorkMiniProgramMiniApiTransferSessionRequest (class)

- WechatTypedRequest request;

- public WechatWorkMiniProgramMiniApiTransferSessionRequest()

- WechatWorkMiniProgramMiniApiTransferSessionRequest Userid(string fieldValue)

- WechatWorkMiniProgramMiniApiTransferSessionRequest SessionKey(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkMiniProgramMiniApiTransferSessionResponse (class)

- public string Raw;


## WechatWorkMobileApi (class)

Mobile/MobileApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkMobileApi(WechatWorkClient client)

- async WechatWorkMobileApiGetTicketResponse GetTicketAsync()
  - GET /cgi-bin/ticket/get

- async WechatResponse GetTicketRawAsync(string query)

- async WechatWorkMobileApiGetJsApiTicketResponse GetJsApiTicketAsync()
  - GET /cgi-bin/ticket/get

- async WechatResponse GetJsApiTicketRawAsync(string query)


## WechatWorkMobileApiGetJsApiTicketResponse (class)

- public string Raw;


## WechatWorkMobileApiGetTicketResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- public string Raw;


## WechatWorkOAuth2Api (class)

OAuth2/OAuth2Api.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkOAuth2Api(WechatWorkClient client)

- async WechatWorkOAuth2ApiGetUserIdResponse GetUserIdAsync(WechatWorkOAuth2ApiGetUserIdRequest request)
  - GET /cgi-bin/auth/getuserinfo

- async WechatResponse GetUserIdRawAsync(string query)

- async WechatWorkOAuth2ApiGetUserDetailResponse GetUserDetailAsync(WechatWorkOAuth2ApiGetUserDetailRequest request)
  - POST /cgi-bin/auth/getuserdetail

- async WechatResponse GetUserDetailRawAsync(string query, string jsonBody)


## WechatWorkOAuth2ApiGetUserDetailRequest (class)

- WechatTypedRequest request;

- public WechatWorkOAuth2ApiGetUserDetailRequest()

- WechatWorkOAuth2ApiGetUserDetailRequest UserTicket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOAuth2ApiGetUserDetailResponse (class)

- public string Raw;


## WechatWorkOAuth2ApiGetUserIdRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkOAuth2ApiGetUserIdRequest()

- WechatWorkOAuth2ApiGetUserIdRequest Code(string fieldValue)

- WechatWorkOAuth2ApiGetUserIdRequest AgentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOAuth2ApiGetUserIdResponse (class)

- public string Raw;


## WechatWorkOaApi (class)

OA/OaApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkOaApi(WechatWorkClient client)

- async WechatWorkOaApiGetTemplateDetailResponse GetTemplateDetailAsync(WechatWorkOaApiGetTemplateDetailRequest request)
  - POST /cgi-bin/oa/gettemplatedetail

- async WechatResponse GetTemplateDetailRawAsync(string query, string jsonBody)

- async WechatWorkOaApiApplyEventResponse ApplyEventAsync(WechatWorkOaApiApplyEventRequest request)
  - POST /cgi-bin/oa/applyevent

- async WechatResponse ApplyEventRawAsync(string query, string jsonBody)

- async WechatWorkOaApiGetApprovalInfoResponse GetApprovalInfoAsync(WechatWorkOaApiGetApprovalInfoRequest request)
  - POST /cgi-bin/oa/getapprovalinfo

- async WechatResponse GetApprovalInfoRawAsync(string query, string jsonBody)

- async WechatWorkOaApiGetApprovalDetailResponse GetApprovalDetailAsync(WechatWorkOaApiGetApprovalDetailRequest request)
  - POST /cgi-bin/oa/getapprovaldetail

- async WechatResponse GetApprovalDetailRawAsync(string query, string jsonBody)

- async WechatWorkOaApiVacationGetCorpConfResponse VacationGetCorpConfAsync()
  - GET /cgi-bin/oa/vacation/getcorpconf

- async WechatResponse VacationGetCorpConfRawAsync(string query)

- async WechatWorkOaApiVacationGetUserVacationQuotaResponse VacationGetUserVacationQuotaAsync(WechatWorkOaApiVacationGetUserVacationQuotaRequest request)
  - POST /cgi-bin/oa/vacation/getuservacationquota

- async WechatResponse VacationGetUserVacationQuotaRawAsync(string query, string jsonBody)

- async WechatWorkOaApiSetOneUserQuotaResponse SetOneUserQuotaAsync(WechatWorkOaApiSetOneUserQuotaRequest request)
  - POST /cgi-bin/oa/vacation/setoneuserquota

- async WechatResponse SetOneUserQuotaRawAsync(string query, string jsonBody)

- async WechatWorkOaApiApprovalCopyTemplateResponse ApprovalCopyTemplateAsync(WechatWorkOaApiApprovalCopyTemplateRequest request)
  - POST /cgi-bin/oa/approval/copytemplate

- async WechatResponse ApprovalCopyTemplateRawAsync(string query, string jsonBody)

- async WechatWorkOaApiApprovalCreateTemplateResponse ApprovalCreateTemplateAsync(WechatWorkOaApiApprovalCreateTemplateRequest request)
  - POST /cgi-bin/oa/approval/create_template

- async WechatResponse ApprovalCreateTemplateRawAsync(string query, string jsonBody)

- async WechatWorkOaApiApprovalUpdateTemplateResponse ApprovalUpdateTemplateAsync(WechatWorkOaApiApprovalUpdateTemplateRequest request)
  - POST /cgi-bin/oa/approval/update_template

- async WechatResponse ApprovalUpdateTemplateRawAsync(string query, string jsonBody)


## WechatWorkOaApiApplyEventRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaApiApplyEventRequest()

- WechatWorkOaApiApplyEventRequest CreatorUserid(string fieldValue)

- WechatWorkOaApiApplyEventRequest TemplateId(string fieldValue)

- WechatWorkOaApiApplyEventRequest UseTemplateApprover(int fieldValue)

- WechatWorkOaApiApplyEventRequest ChooseDepartment(int fieldValue)

- WechatWorkOaApiApplyEventRequest Approver(List<WechatWorkApplyEventRequestApprover> fieldValue)

- WechatWorkOaApiApplyEventRequest Notifyer(List<string> fieldValue)

- WechatWorkOaApiApplyEventRequest NotifyType(int fieldValue)

- WechatWorkOaApiApplyEventRequest ApplyData(WechatWorkApplyEventRequestApplyData fieldValue)

- WechatWorkOaApiApplyEventRequest SummaryList(List<WechatWorkApplyEventRequestSummaryList> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaApiApplyEventResponse (class)

- public string Raw;


## WechatWorkOaApiApprovalCopyTemplateRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaApiApprovalCopyTemplateRequest()

- WechatWorkOaApiApprovalCopyTemplateRequest OpenTemplateId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaApiApprovalCopyTemplateResponse (class)

- public string Raw;


## WechatWorkOaApiApprovalCreateTemplateRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaApiApprovalCreateTemplateRequest()

- WechatWorkOaApiApprovalCreateTemplateRequest TemplateName(List<WechatWorkApprovalCreateTemplateRequestTextAndLang> fieldValue)

- WechatWorkOaApiApprovalCreateTemplateRequest TemplateContent(WechatWorkApprovalCreateTemplateRequestTemplateContent fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaApiApprovalCreateTemplateResponse (class)

- public string Raw;


## WechatWorkOaApiApprovalUpdateTemplateRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaApiApprovalUpdateTemplateRequest()

- WechatWorkOaApiApprovalUpdateTemplateRequest TemplateId(string fieldValue)

- WechatWorkOaApiApprovalUpdateTemplateRequest TemplateName(List<WechatWorkApprovalCreateTemplateRequestTextAndLang> fieldValue)

- WechatWorkOaApiApprovalUpdateTemplateRequest TemplateContent(WechatWorkApprovalCreateTemplateRequestTemplateContent fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaApiApprovalUpdateTemplateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkOaApiGetApprovalDetailRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaApiGetApprovalDetailRequest()

- WechatWorkOaApiGetApprovalDetailRequest SpNo(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaApiGetApprovalDetailResponse (class)

- public string Raw;


## WechatWorkOaApiGetApprovalInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaApiGetApprovalInfoRequest()

- WechatWorkOaApiGetApprovalInfoRequest Starttime(string fieldValue)

- WechatWorkOaApiGetApprovalInfoRequest Endtime(string fieldValue)

- WechatWorkOaApiGetApprovalInfoRequest Cursor(int fieldValue)

- WechatWorkOaApiGetApprovalInfoRequest Size(int fieldValue)

- WechatWorkOaApiGetApprovalInfoRequest Filters(List<WechatWorkGetApprovalInfoRequestFilter> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaApiGetApprovalInfoResponse (class)

- public string Raw;


## WechatWorkOaApiGetTemplateDetailRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkOaApiGetTemplateDetailRequest()

- WechatWorkOaApiGetTemplateDetailRequest TemplateId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaApiGetTemplateDetailResponse (class)

- public string Raw;


## WechatWorkOaApiSetOneUserQuotaRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaApiSetOneUserQuotaRequest()

- WechatWorkOaApiSetOneUserQuotaRequest Userid(string fieldValue)

- WechatWorkOaApiSetOneUserQuotaRequest VacationId(string fieldValue)

- WechatWorkOaApiSetOneUserQuotaRequest Leftduration(int fieldValue)

- WechatWorkOaApiSetOneUserQuotaRequest TimeAttr(int fieldValue)

- WechatWorkOaApiSetOneUserQuotaRequest Remarks(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaApiSetOneUserQuotaResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkOaApiVacationGetCorpConfResponse (class)

- public string Raw;


## WechatWorkOaApiVacationGetUserVacationQuotaRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaApiVacationGetUserVacationQuotaRequest()

- WechatWorkOaApiVacationGetUserVacationQuotaRequest Userid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaApiVacationGetUserVacationQuotaResponse (class)

- public string Raw;


## WechatWorkOaDataOpenApi (class)

OaDataOpen/OaDataOpenApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkOaDataOpenApi(WechatWorkClient client)

- async WechatWorkOaDataOpenApiGetCheckinOptionResponse GetCheckinOptionAsync(WechatWorkOaDataOpenApiGetCheckinOptionRequest request)
  - POST /cgi-bin/checkin/getcheckinoption

- async WechatResponse GetCheckinOptionRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenApiGetCheckinDataResponse GetCheckinDataAsync(WechatWorkOaDataOpenApiGetCheckinDataRequest request)
  - POST /cgi-bin/checkin/getcheckindata

- async WechatResponse GetCheckinDataRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenApiAddCheckinRecordResponse AddCheckinRecordAsync(WechatWorkOaDataOpenApiAddCheckinRecordRequest request)
  - POST /cgi-bin/checkin/add_checkin_record

- async WechatResponse AddCheckinRecordRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenApiGetCheckinDayDataResponse GetCheckinDayDataAsync(WechatWorkOaDataOpenApiGetCheckinDayDataRequest request)
  - POST /cgi-bin/checkin/getcheckin_daydata

- async WechatResponse GetCheckinDayDataRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenApiGetApprovalDataResponse GetApprovalDataAsync(WechatWorkOaDataOpenApiGetApprovalDataRequest request)
  - POST /cgi-bin/corp/getapprovaldata

- async WechatResponse GetApprovalDataRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenApiGetDialRecordResponse GetDialRecordAsync(WechatWorkOaDataOpenApiGetDialRecordRequest request)
  - POST /cgi-bin/dial/get_dial_record

- async WechatResponse GetDialRecordRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenApiGetOpenApprovalDataResponse GetOpenApprovalDataAsync(WechatWorkOaDataOpenApiGetOpenApprovalDataRequest request)
  - POST /cgi-bin/corp/getopenapprovaldata

- async WechatResponse GetOpenApprovalDataRawAsync(string query, string jsonBody)


## WechatWorkOaDataOpenApiAddCheckinRecordRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenApiAddCheckinRecordRequest()

- WechatWorkOaDataOpenApiAddCheckinRecordRequest Records(List<WechatWorkAddCheckinRecordInfo> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenApiAddCheckinRecordResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkOaDataOpenApiGetApprovalDataRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenApiGetApprovalDataRequest()

- WechatWorkOaDataOpenApiGetApprovalDataRequest StartTime(string fieldValue)

- WechatWorkOaDataOpenApiGetApprovalDataRequest EndTime(string fieldValue)

- WechatWorkOaDataOpenApiGetApprovalDataRequest NextSpnum(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenApiGetApprovalDataResponse (class)

- public string Raw;


## WechatWorkOaDataOpenApiGetCheckinDataRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenApiGetCheckinDataRequest()

- WechatWorkOaDataOpenApiGetCheckinDataRequest OpenCheckinDataType(int fieldValue)

- WechatWorkOaDataOpenApiGetCheckinDataRequest StartTime(string fieldValue)

- WechatWorkOaDataOpenApiGetCheckinDataRequest EndTime(string fieldValue)

- WechatWorkOaDataOpenApiGetCheckinDataRequest UserIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenApiGetCheckinDataResponse (class)

- public string Raw;


## WechatWorkOaDataOpenApiGetCheckinDayDataRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenApiGetCheckinDayDataRequest()

- WechatWorkOaDataOpenApiGetCheckinDayDataRequest StartTime(string fieldValue)

- WechatWorkOaDataOpenApiGetCheckinDayDataRequest EndTime(string fieldValue)

- WechatWorkOaDataOpenApiGetCheckinDayDataRequest UserIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenApiGetCheckinDayDataResponse (class)

- public string Raw;


## WechatWorkOaDataOpenApiGetCheckinOptionRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkOaDataOpenApiGetCheckinOptionRequest()

- WechatWorkOaDataOpenApiGetCheckinOptionRequest Datetime(string fieldValue)

- WechatWorkOaDataOpenApiGetCheckinOptionRequest UserIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenApiGetCheckinOptionResponse (class)

- public string Raw;


## WechatWorkOaDataOpenApiGetDialRecordRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenApiGetDialRecordRequest()

- WechatWorkOaDataOpenApiGetDialRecordRequest StartTime(string fieldValue)

- WechatWorkOaDataOpenApiGetDialRecordRequest EndTime(string fieldValue)

- WechatWorkOaDataOpenApiGetDialRecordRequest Offset(int fieldValue)

- WechatWorkOaDataOpenApiGetDialRecordRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenApiGetDialRecordResponse (class)

- public string Raw;


## WechatWorkOaDataOpenApiGetOpenApprovalDataRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenApiGetOpenApprovalDataRequest()

- WechatWorkOaDataOpenApiGetOpenApprovalDataRequest ThirdId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenApiGetOpenApprovalDataResponse (class)

- public string Raw;


## WechatWorkOaDataOpenCheckinP2Api (class)

OaDataOpen/OaDataOpenApi.CheckinP2.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkOaDataOpenCheckinP2Api(WechatWorkClient client)

- async WechatWorkOaDataOpenCheckinP2ApiGetCorpCheckinOptionResponse GetCorpCheckinOptionAsync()
  - POST /cgi-bin/checkin/getcorpcheckinoption

- async WechatResponse GetCorpCheckinOptionRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiGetCheckinMonthDataResponse GetCheckinMonthDataAsync(WechatWorkOaDataOpenCheckinP2ApiGetCheckinMonthDataRequest request)
  - POST /cgi-bin/checkin/getcheckin_monthdata

- async WechatResponse GetCheckinMonthDataRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiGetCheckinScheduleListResponse GetCheckinScheduleListAsync(WechatWorkOaDataOpenCheckinP2ApiGetCheckinScheduleListRequest request)
  - POST /cgi-bin/checkin/getcheckinschedulist

- async WechatResponse GetCheckinScheduleListRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiSetCheckinScheduleListResponse SetCheckinScheduleListAsync(WechatWorkOaDataOpenCheckinP2ApiSetCheckinScheduleListRequest request)
  - POST /cgi-bin/checkin/setcheckinschedulist

- async WechatResponse SetCheckinScheduleListRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionResponse PunchCorrectionAsync(WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionRequest request)
  - POST /cgi-bin/checkin/punch_correction

- async WechatResponse PunchCorrectionRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiAddCheckinUserFaceResponse AddCheckinUserFaceAsync(WechatWorkOaDataOpenCheckinP2ApiAddCheckinUserFaceRequest request)
  - POST /cgi-bin/checkin/addcheckinuserface

- async WechatResponse AddCheckinUserFaceRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiGetHardwareCheckinDataResponse GetHardwareCheckinDataAsync(WechatWorkOaDataOpenCheckinP2ApiGetHardwareCheckinDataRequest request)
  - POST /cgi-bin/hardware/get_hardware_checkin_data

- async WechatResponse GetHardwareCheckinDataRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiAddCheckinOptionResponse AddCheckinOptionAsync(WechatWorkOaDataOpenCheckinP2ApiAddCheckinOptionRequest request)
  - POST /cgi-bin/checkin/add_checkin_option

- async WechatResponse AddCheckinOptionRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiUpdateCheckinOptionResponse UpdateCheckinOptionAsync(WechatWorkOaDataOpenCheckinP2ApiUpdateCheckinOptionRequest request)
  - POST /cgi-bin/checkin/update_checkin_option

- async WechatResponse UpdateCheckinOptionRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiClearCheckinOptionArrayFieldResponse ClearCheckinOptionArrayFieldAsync(WechatWorkOaDataOpenCheckinP2ApiClearCheckinOptionArrayFieldRequest request)
  - POST /cgi-bin/checkin/clear_checkin_option_array_field

- async WechatResponse ClearCheckinOptionArrayFieldRawAsync(string query, string jsonBody)

- async WechatWorkOaDataOpenCheckinP2ApiDeleteCheckinOptionResponse DeleteCheckinOptionAsync(WechatWorkOaDataOpenCheckinP2ApiDeleteCheckinOptionRequest request)
  - POST /cgi-bin/checkin/del_checkin_option

- async WechatResponse DeleteCheckinOptionRawAsync(string query, string jsonBody)


## WechatWorkOaDataOpenCheckinP2ApiAddCheckinOptionRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiAddCheckinOptionRequest()

- WechatWorkOaDataOpenCheckinP2ApiAddCheckinOptionRequest EffectiveNow(bool fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiAddCheckinOptionRequest Group(WechatWorkGroup fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiAddCheckinOptionResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkOaDataOpenCheckinP2ApiAddCheckinUserFaceRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiAddCheckinUserFaceRequest()

- WechatWorkOaDataOpenCheckinP2ApiAddCheckinUserFaceRequest Userid(string fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiAddCheckinUserFaceRequest Userface(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiAddCheckinUserFaceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkOaDataOpenCheckinP2ApiClearCheckinOptionArrayFieldRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiClearCheckinOptionArrayFieldRequest()

- WechatWorkOaDataOpenCheckinP2ApiClearCheckinOptionArrayFieldRequest Groupid(int fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiClearCheckinOptionArrayFieldRequest ClearField(List<int> fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiClearCheckinOptionArrayFieldRequest EffectiveNow(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiClearCheckinOptionArrayFieldResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkOaDataOpenCheckinP2ApiDeleteCheckinOptionRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiDeleteCheckinOptionRequest()

- WechatWorkOaDataOpenCheckinP2ApiDeleteCheckinOptionRequest Groupid(int fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiDeleteCheckinOptionRequest EffectiveNow(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiDeleteCheckinOptionResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkOaDataOpenCheckinP2ApiGetCheckinMonthDataRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiGetCheckinMonthDataRequest()

- WechatWorkOaDataOpenCheckinP2ApiGetCheckinMonthDataRequest Starttime(long fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiGetCheckinMonthDataRequest Endtime(long fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiGetCheckinMonthDataRequest Useridlist(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiGetCheckinMonthDataResponse (class)

- public string Raw;


## WechatWorkOaDataOpenCheckinP2ApiGetCheckinScheduleListRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiGetCheckinScheduleListRequest()

- WechatWorkOaDataOpenCheckinP2ApiGetCheckinScheduleListRequest Starttime(long fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiGetCheckinScheduleListRequest Endtime(long fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiGetCheckinScheduleListRequest Useridlist(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiGetCheckinScheduleListResponse (class)

- public string Raw;


## WechatWorkOaDataOpenCheckinP2ApiGetCorpCheckinOptionResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- public string Raw;


## WechatWorkOaDataOpenCheckinP2ApiGetHardwareCheckinDataRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiGetHardwareCheckinDataRequest()

- WechatWorkOaDataOpenCheckinP2ApiGetHardwareCheckinDataRequest FilterType(int fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiGetHardwareCheckinDataRequest Starttime(long fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiGetHardwareCheckinDataRequest Endtime(long fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiGetHardwareCheckinDataRequest Useridlist(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiGetHardwareCheckinDataResponse (class)

- public string Raw;


## WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionRequest()

- WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionRequest Userid(string fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionRequest ScheduleDateTime(long fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionRequest ScheduleCheckinTime(int fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionRequest CheckinTime(long fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionRequest Remark(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiPunchCorrectionResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkOaDataOpenCheckinP2ApiSetCheckinScheduleListRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiSetCheckinScheduleListRequest()

- WechatWorkOaDataOpenCheckinP2ApiSetCheckinScheduleListRequest Groupid(int fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiSetCheckinScheduleListRequest Items(List<WechatWorkSetCheckinScheduleItem> fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiSetCheckinScheduleListRequest Yearmonth(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiSetCheckinScheduleListResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkOaDataOpenCheckinP2ApiUpdateCheckinOptionRequest (class)

- WechatTypedRequest request;

- public WechatWorkOaDataOpenCheckinP2ApiUpdateCheckinOptionRequest()

- WechatWorkOaDataOpenCheckinP2ApiUpdateCheckinOptionRequest EffectiveNow(bool fieldValue)

- WechatWorkOaDataOpenCheckinP2ApiUpdateCheckinOptionRequest Group(WechatWorkGroup fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkOaDataOpenCheckinP2ApiUpdateCheckinOptionResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkScheduleApi (class)

Schedule/ScheduleApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkScheduleApi(WechatWorkClient client)

- async WechatWorkScheduleApiAddResponse AddAsync(WechatWorkScheduleApiAddRequest request)
  - POST /cgi-bin/oa/schedule/add

- async WechatResponse AddRawAsync(string query, string jsonBody)

- async WechatWorkScheduleApiUpdateResponse UpdateAsync(WechatWorkScheduleApiUpdateRequest request)
  - POST /cgi-bin/oa/schedule/update

- async WechatResponse UpdateRawAsync(string query, string jsonBody)

- async WechatWorkScheduleApiUpdateRepeatResponse UpdateRepeatAsync(WechatWorkScheduleApiUpdateRepeatRequest request)
  - POST /cgi-bin/oa/schedule/update

- async WechatResponse UpdateRepeatRawAsync(string query, string jsonBody)

- async WechatWorkScheduleApiDelResponse DelAsync(WechatWorkScheduleApiDelRequest request)
  - POST /cgi-bin/oa/schedule/del

- async WechatResponse DelRawAsync(string query, string jsonBody)

- async WechatWorkScheduleApiGetResponse GetAsync(WechatWorkScheduleApiGetRequest request)
  - POST /cgi-bin/oa/schedule/get

- async WechatResponse GetRawAsync(string query, string jsonBody)

- async WechatWorkScheduleApiGetByCalendarResponse GetByCalendarAsync(WechatWorkScheduleApiGetByCalendarRequest request)
  - POST /cgi-bin/oa/schedule/get_by_calendar

- async WechatResponse GetByCalendarRawAsync(string query, string jsonBody)

- async WechatWorkScheduleApiAddAttendeesResponse AddAttendeesAsync(WechatWorkScheduleApiAddAttendeesRequest request)
  - POST /cgi-bin/oa/schedule/add_attendees

- async WechatResponse AddAttendeesRawAsync(string query, string jsonBody)

- async WechatWorkScheduleApiDelAttendeesResponse DelAttendeesAsync(WechatWorkScheduleApiDelAttendeesRequest request)
  - POST /cgi-bin/oa/schedule/del_attendees

- async WechatResponse DelAttendeesRawAsync(string query, string jsonBody)


## WechatWorkScheduleApiAddAttendeesRequest (class)

- WechatTypedRequest request;

- public WechatWorkScheduleApiAddAttendeesRequest()

- WechatWorkScheduleApiAddAttendeesRequest ScheduleId(string fieldValue)

- WechatWorkScheduleApiAddAttendeesRequest Attendees(List<WechatWorkAttendee> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkScheduleApiAddAttendeesResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkScheduleApiAddRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkScheduleApiAddRequest()

- WechatWorkScheduleApiAddRequest Schedule(WechatWorkSchedule fieldValue)

- WechatWorkScheduleApiAddRequest Agentid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkScheduleApiAddResponse (class)

- public string Raw;


## WechatWorkScheduleApiDelAttendeesRequest (class)

- WechatTypedRequest request;

- public WechatWorkScheduleApiDelAttendeesRequest()

- WechatWorkScheduleApiDelAttendeesRequest ScheduleId(string fieldValue)

- WechatWorkScheduleApiDelAttendeesRequest Attendees(List<WechatWorkAttendee> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkScheduleApiDelAttendeesResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkScheduleApiDelRequest (class)

- WechatTypedRequest request;

- public WechatWorkScheduleApiDelRequest()

- WechatWorkScheduleApiDelRequest ScheduleId(string fieldValue)

- WechatWorkScheduleApiDelRequest OpMode(int fieldValue)

- WechatWorkScheduleApiDelRequest OpStartTime(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkScheduleApiDelResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkScheduleApiGetByCalendarRequest (class)

- WechatTypedRequest request;

- public WechatWorkScheduleApiGetByCalendarRequest()

- WechatWorkScheduleApiGetByCalendarRequest CalId(string fieldValue)

- WechatWorkScheduleApiGetByCalendarRequest Offset(int fieldValue)

- WechatWorkScheduleApiGetByCalendarRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkScheduleApiGetByCalendarResponse (class)

- public string Raw;


## WechatWorkScheduleApiGetRequest (class)

- WechatTypedRequest request;

- public WechatWorkScheduleApiGetRequest()

- WechatWorkScheduleApiGetRequest ScheduleIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkScheduleApiGetResponse (class)

- public string Raw;


## WechatWorkScheduleApiUpdateRepeatRequest (class)

- WechatTypedRequest request;

- public WechatWorkScheduleApiUpdateRepeatRequest()

- WechatWorkScheduleApiUpdateRepeatRequest SkipAttendees(int fieldValue)

- WechatWorkScheduleApiUpdateRepeatRequest OpMode(int fieldValue)

- WechatWorkScheduleApiUpdateRepeatRequest OpStartTime(long fieldValue)

- WechatWorkScheduleApiUpdateRepeatRequest Schedule(WechatWorkScheduleUpdate fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkScheduleApiUpdateRepeatResponse (class)

- public string Raw;


## WechatWorkScheduleApiUpdateRequest (class)

- WechatTypedRequest request;

- public WechatWorkScheduleApiUpdateRequest()

- WechatWorkScheduleApiUpdateRequest ScheduleId(string fieldValue)

- WechatWorkScheduleApiUpdateRequest Admins(List<string> fieldValue)

- WechatWorkScheduleApiUpdateRequest Organizer(string fieldValue)

- WechatWorkScheduleApiUpdateRequest StartTime(int fieldValue)

- WechatWorkScheduleApiUpdateRequest EndTime(int fieldValue)

- WechatWorkScheduleApiUpdateRequest IsWholeDay(int fieldValue)

- WechatWorkScheduleApiUpdateRequest Attendees(List<WechatWorkAttendee> fieldValue)

- WechatWorkScheduleApiUpdateRequest Summary(string fieldValue)

- WechatWorkScheduleApiUpdateRequest Description(string fieldValue)

- WechatWorkScheduleApiUpdateRequest Reminders(WechatWorkReminders fieldValue)

- WechatWorkScheduleApiUpdateRequest Location(string fieldValue)

- WechatWorkScheduleApiUpdateRequest CalId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkScheduleApiUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkShakeAroundApi (class)

ShakeAround/ShakeAroundApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkShakeAroundApi(WechatWorkClient client)

- async WechatWorkShakeAroundApiGetSuiteTokenResponse GetSuiteTokenAsync(WechatWorkShakeAroundApiGetSuiteTokenRequest request)
  - POST /cgi-bin/shakearound/getshakeinfo

- async WechatResponse GetSuiteTokenRawAsync(string query, string jsonBody)


## WechatWorkShakeAroundApiGetSuiteTokenRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkShakeAroundApiGetSuiteTokenRequest()

- WechatWorkShakeAroundApiGetSuiteTokenRequest Ticket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkShakeAroundApiGetSuiteTokenResponse (class)

- public string Raw;


## WechatWorkSmartRobotClient (class)

企业微信智能机器人 WSS 长连接客户端。

- WssClient socket;

- string host;

- int port;

- string path;

- public WechatWorkSmartRobotClient()

- WechatWorkSmartRobotClient Endpoint(string host, int port, string path)

- async void ConnectAndSubscribeAsync(string botId, string secret)

- async string ReceiveAsync()

- async void RespondWelcomeAsync(string requestId, string replyJson)

- async void RespondAsync(string requestId, string replyJson)

- async void UpdateResponseAsync(string requestId, string replyJson)

- async void SendMessageAsync(string requestId, string messageJson)

- async void SendPingAsync(string requestId)

- async void SendCommandAsync(string command, string requestId, string bodyJson)

- async void CloseAsync()

- static string CreateRequestId()


## WechatWorkSsoApi (class)

SSO/SsoApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkSsoApi(WechatWorkClient client)

- async WechatWorkSsoApiGetLoginInfoResponse GetLoginInfoAsync(WechatWorkSsoApiGetLoginInfoRequest request)
  - POST /cgi-bin/service/get_login_info

- async WechatResponse GetLoginInfoRawAsync(string query, string jsonBody)

- async WechatWorkSsoApiGetProviderTokenResponse GetProviderTokenAsync(WechatWorkSsoApiGetProviderTokenRequest request)
  - POST /cgi-bin/service/get_provider_token

- async WechatResponse GetProviderTokenRawAsync(string query, string jsonBody)


## WechatWorkSsoApiGetLoginInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkSsoApiGetLoginInfoRequest()

- WechatWorkSsoApiGetLoginInfoRequest AuthCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkSsoApiGetLoginInfoResponse (class)

- public string Raw;


## WechatWorkSsoApiGetProviderTokenRequest (class)

- WechatTypedRequest request;

- public WechatWorkSsoApiGetProviderTokenRequest()

- WechatWorkSsoApiGetProviderTokenRequest CorpId(string fieldValue)

- WechatWorkSsoApiGetProviderTokenRequest ProviderSecret(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkSsoApiGetProviderTokenResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkThirdPartyAuthApi (class)

ThirdPartyAuth/ThirdPartyAuthApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkThirdPartyAuthApi(WechatWorkClient client)

- async WechatWorkThirdPartyAuthApiGetSuiteTokenResponse GetSuiteTokenAsync(WechatWorkThirdPartyAuthApiGetSuiteTokenRequest request)
  - POST /cgi-bin/service/get_suite_token

- async WechatResponse GetSuiteTokenRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiGetPreAuthCodeResponse GetPreAuthCodeAsync(WechatWorkThirdPartyAuthApiGetPreAuthCodeRequest request)
  - POST /cgi-bin/service/get_pre_auth_code

- async WechatResponse GetPreAuthCodeRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiSetAuthConfigResponse SetAuthConfigAsync(WechatWorkThirdPartyAuthApiSetAuthConfigRequest request)
  - POST /cgi-bin/service/set_session_info

- async WechatResponse SetAuthConfigRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiGetPermanentCodeResponse GetPermanentCodeAsync(WechatWorkThirdPartyAuthApiGetPermanentCodeRequest request)
  - POST /cgi-bin/service/get_permanent_code

- async WechatResponse GetPermanentCodeRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiGetAuthInfoResponse GetAuthInfoAsync(WechatWorkThirdPartyAuthApiGetAuthInfoRequest request)
  - POST /cgi-bin/service/get_auth_info

- async WechatResponse GetAuthInfoRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiGetAgentResponse GetAgentAsync(WechatWorkThirdPartyAuthApiGetAgentRequest request)
  - POST /cgi-bin/service/get_agent

- async WechatResponse GetAgentRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiSetAgentResponse SetAgentAsync(WechatWorkThirdPartyAuthApiSetAgentRequest request)
  - POST /cgi-bin/service/set_agent

- async WechatResponse SetAgentRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiGetCorpTokenResponse GetCorpTokenAsync(WechatWorkThirdPartyAuthApiGetCorpTokenRequest request)
  - POST /cgi-bin/service/get_corp_token

- async WechatResponse GetCorpTokenRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiGetAdminListResponse GetAdminListAsync(WechatWorkThirdPartyAuthApiGetAdminListRequest request)
  - POST /cgi-bin/service/get_admin_list

- async WechatResponse GetAdminListRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiGetUserInfoResponse GetUserInfoAsync(WechatWorkThirdPartyAuthApiGetUserInfoRequest request)
  - GET /cgi-bin/service/getuserinfo3rd

- async WechatResponse GetUserInfoRawAsync(string query)

- async WechatWorkThirdPartyAuthApiGetUserInfoByTicketResponse GetUserInfoByTicketAsync(WechatWorkThirdPartyAuthApiGetUserInfoByTicketRequest request)
  - POST /cgi-bin/service/getuserdetail3rd

- async WechatResponse GetUserInfoByTicketRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiGetRegisterCodeResponse GetRegisterCodeAsync(WechatWorkThirdPartyAuthApiGetRegisterCodeRequest request)
  - POST /cgi-bin/service/get_register_code

- async WechatResponse GetRegisterCodeRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiGetRegisterInfoResponse GetRegisterInfoAsync(WechatWorkThirdPartyAuthApiGetRegisterInfoRequest request)
  - POST /cgi-bin/service/get_register_info

- async WechatResponse GetRegisterInfoRawAsync(string query, string jsonBody)

- async WechatWorkThirdPartyAuthApiSetScopeResponse SetScopeAsync(WechatWorkThirdPartyAuthApiSetScopeRequest request)
  - GET /cgi-bin/agent/set_scope

- async WechatResponse SetScopeRawAsync(string query)

- async WechatWorkThirdPartyAuthApiContactSyncSuccessResponse ContactSyncSuccessAsync(WechatWorkThirdPartyAuthApiContactSyncSuccessRequest request)
  - GET /cgi-bin/sync/contact_sync_success

- async WechatResponse ContactSyncSuccessRawAsync(string query)


## WechatWorkThirdPartyAuthApiContactSyncSuccessRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiContactSyncSuccessRequest()

- WechatWorkThirdPartyAuthApiContactSyncSuccessRequest AccessToken(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiContactSyncSuccessResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkThirdPartyAuthApiGetAdminListRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetAdminListRequest()

- WechatWorkThirdPartyAuthApiGetAdminListRequest AuthCorpId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetAdminListRequest AgentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetAdminListResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetAgentRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetAgentRequest()

- WechatWorkThirdPartyAuthApiGetAgentRequest SuiteId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetAgentRequest AuthCorpId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetAgentRequest PermanentCode(string fieldValue)

- WechatWorkThirdPartyAuthApiGetAgentRequest AgentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetAgentResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetAuthInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetAuthInfoRequest()

- WechatWorkThirdPartyAuthApiGetAuthInfoRequest SuiteId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetAuthInfoRequest AuthCorpId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetAuthInfoRequest PermanentCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetAuthInfoResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetCorpTokenRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetCorpTokenRequest()

- WechatWorkThirdPartyAuthApiGetCorpTokenRequest SuiteId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetCorpTokenRequest AuthCorpId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetCorpTokenRequest PermanentCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetCorpTokenResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetPermanentCodeRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetPermanentCodeRequest()

- WechatWorkThirdPartyAuthApiGetPermanentCodeRequest SuiteId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetPermanentCodeRequest AuthCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetPermanentCodeResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetPreAuthCodeRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetPreAuthCodeRequest()

- WechatWorkThirdPartyAuthApiGetPreAuthCodeRequest SuiteId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetPreAuthCodeResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetRegisterCodeRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetRegisterCodeRequest()

- WechatWorkThirdPartyAuthApiGetRegisterCodeRequest TemplateId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetRegisterCodeRequest CorpName(string fieldValue)

- WechatWorkThirdPartyAuthApiGetRegisterCodeRequest AdminName(string fieldValue)

- WechatWorkThirdPartyAuthApiGetRegisterCodeRequest AdminMobile(string fieldValue)

- WechatWorkThirdPartyAuthApiGetRegisterCodeRequest State(string fieldValue)

- WechatWorkThirdPartyAuthApiGetRegisterCodeRequest FollowUser(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetRegisterCodeResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetRegisterInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetRegisterInfoRequest()

- WechatWorkThirdPartyAuthApiGetRegisterInfoRequest RegisterCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetRegisterInfoResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetSuiteTokenRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetSuiteTokenRequest()

- WechatWorkThirdPartyAuthApiGetSuiteTokenRequest SuiteId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetSuiteTokenRequest SuiteSecret(string fieldValue)

- WechatWorkThirdPartyAuthApiGetSuiteTokenRequest SuiteTicket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetSuiteTokenResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetUserInfoByTicketRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetUserInfoByTicketRequest()

- WechatWorkThirdPartyAuthApiGetUserInfoByTicketRequest UserTicket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetUserInfoByTicketResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiGetUserInfoRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiGetUserInfoRequest()

- WechatWorkThirdPartyAuthApiGetUserInfoRequest SuiteAccessToken(string fieldValue)

- WechatWorkThirdPartyAuthApiGetUserInfoRequest Code(string fieldValue)

- WechatWorkThirdPartyAuthApiGetUserInfoRequest AgentId(string fieldValue)

- WechatWorkThirdPartyAuthApiGetUserInfoRequest AuthCorpId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiGetUserInfoResponse (class)

- public string Raw;


## WechatWorkThirdPartyAuthApiSetAgentRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiSetAgentRequest()

- WechatWorkThirdPartyAuthApiSetAgentRequest Agentid(string fieldValue)

- WechatWorkThirdPartyAuthApiSetAgentRequest ReportLocationFlag(string fieldValue)

- WechatWorkThirdPartyAuthApiSetAgentRequest LogoMediaid(string fieldValue)

- WechatWorkThirdPartyAuthApiSetAgentRequest Name(string fieldValue)

- WechatWorkThirdPartyAuthApiSetAgentRequest Description(string fieldValue)

- WechatWorkThirdPartyAuthApiSetAgentRequest RedirectDomain(string fieldValue)

- WechatWorkThirdPartyAuthApiSetAgentRequest Isreportuser(JsonValue fieldValue)

- WechatWorkThirdPartyAuthApiSetAgentRequest SuiteId(string fieldValue)

- WechatWorkThirdPartyAuthApiSetAgentRequest AuthCorpId(string fieldValue)

- WechatWorkThirdPartyAuthApiSetAgentRequest PermanentCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiSetAgentResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkThirdPartyAuthApiSetAuthConfigRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiSetAuthConfigRequest()

- WechatWorkThirdPartyAuthApiSetAuthConfigRequest AuthCode(string fieldValue)

- WechatWorkThirdPartyAuthApiSetAuthConfigRequest Appid(List<int> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiSetAuthConfigResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkThirdPartyAuthApiSetScopeRequest (class)

- WechatTypedRequest request;

- public WechatWorkThirdPartyAuthApiSetScopeRequest()

- WechatWorkThirdPartyAuthApiSetScopeRequest Agentid(int fieldValue)

- WechatWorkThirdPartyAuthApiSetScopeRequest AllowUser(List<string> fieldValue)

- WechatWorkThirdPartyAuthApiSetScopeRequest AllowParty(List<int> fieldValue)

- WechatWorkThirdPartyAuthApiSetScopeRequest AllowTag(List<int> fieldValue)

- WechatWorkThirdPartyAuthApiSetScopeRequest AccessToken(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkThirdPartyAuthApiSetScopeResponse (class)

- public string Raw;


## WechatWorkWebhookApi (class)

Webhook/WebhookApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkWebhookApi(WechatWorkClient client)

- async WechatWorkWebhookApiSendTextResponse SendTextAsync(WechatWorkWebhookApiSendTextRequest request)
  - POST /cgi-bin/webhook/send

- async WechatResponse SendTextRawAsync(string query, string jsonBody)

- async WechatWorkWebhookApiSendTemplateCardResponse SendTemplateCardAsync(WechatWorkWebhookApiSendTemplateCardRequest request)
  - POST /cgi-bin/webhook/send

- async WechatResponse SendTemplateCardRawAsync(string query, string jsonBody)

- async WechatWorkWebhookApiSendFileResponse SendFileAsync(WechatWorkWebhookApiSendFileRequest request)
  - POST /cgi-bin/webhook/send

- async WechatResponse SendFileRawAsync(string query, string jsonBody)

- async WechatWorkWebhookApiSendMarkdownResponse SendMarkdownAsync(WechatWorkWebhookApiSendMarkdownRequest request)
  - POST /cgi-bin/webhook/send

- async WechatResponse SendMarkdownRawAsync(string query, string jsonBody)

- async WechatWorkWebhookApiSendMarkdownV2Response SendMarkdownV2Async(WechatWorkWebhookApiSendMarkdownV2Request request)
  - POST /cgi-bin/webhook/send

- async WechatResponse SendMarkdownV2RawAsync(string query, string jsonBody)

- async WechatWorkWebhookApiSendImageResponse SendImageAsync(WechatWorkWebhookApiSendImageRequest request)
  - POST /cgi-bin/webhook/send

- async WechatResponse SendImageRawAsync(string query, string jsonBody)

- async WechatWorkWebhookApiSendNewsResponse SendNewsAsync(WechatWorkWebhookApiSendNewsRequest request)
  - POST /cgi-bin/webhook/send

- async WechatResponse SendNewsRawAsync(string query, string jsonBody)


## WechatWorkWebhookApiSendFileRequest (class)

- WechatTypedRequest request;

- public WechatWorkWebhookApiSendFileRequest()

- WechatWorkWebhookApiSendFileRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWebhookApiSendFileResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkWebhookApiSendImageRequest (class)

- WechatTypedRequest request;

- public WechatWorkWebhookApiSendImageRequest()

- WechatWorkWebhookApiSendImageRequest Base64(string fieldValue)

- WechatWorkWebhookApiSendImageRequest Md5(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWebhookApiSendImageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkWebhookApiSendMarkdownRequest (class)

- WechatTypedRequest request;

- public WechatWorkWebhookApiSendMarkdownRequest()

- WechatWorkWebhookApiSendMarkdownRequest Content(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWebhookApiSendMarkdownResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkWebhookApiSendMarkdownV2Request (class)

- WechatTypedRequest request;

- public WechatWorkWebhookApiSendMarkdownV2Request()

- WechatWorkWebhookApiSendMarkdownV2Request Content(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWebhookApiSendMarkdownV2Response (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkWebhookApiSendNewsRequest (class)

- WechatTypedRequest request;

- public WechatWorkWebhookApiSendNewsRequest()

- WechatWorkWebhookApiSendNewsRequest Msgtype(string fieldValue)

- WechatWorkWebhookApiSendNewsRequest News(WechatWorkWebhookWebhookNewsNews fieldValue)

- WechatWorkWebhookApiSendNewsRequest Key(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWebhookApiSendNewsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkWebhookApiSendTemplateCardRequest (class)

- WechatTypedRequest request;

- public WechatWorkWebhookApiSendTemplateCardRequest()

- WechatWorkWebhookApiSendTemplateCardRequest CardType(string fieldValue)

- WechatWorkWebhookApiSendTemplateCardRequest Source(WechatWorkSourceInfo fieldValue)

- WechatWorkWebhookApiSendTemplateCardRequest MainTitle(WechatWorkMainTitleInfo fieldValue)

- WechatWorkWebhookApiSendTemplateCardRequest CardImage(WechatWorkCardImage fieldValue)

- WechatWorkWebhookApiSendTemplateCardRequest ImageTextArea(WechatWorkImageTextArea fieldValue)

- WechatWorkWebhookApiSendTemplateCardRequest EmphasisContent(WechatWorkEmphasisContentInfo fieldValue)

- WechatWorkWebhookApiSendTemplateCardRequest QuoteArea(WechatWorkQuoteAreaInfo fieldValue)

- WechatWorkWebhookApiSendTemplateCardRequest SubTitleText(string fieldValue)

- WechatWorkWebhookApiSendTemplateCardRequest CardAction(WechatWorkCardActionInfo fieldValue)

- WechatWorkWebhookApiSendTemplateCardRequest Key(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWebhookApiSendTemplateCardResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkWebhookApiSendTextRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkWebhookApiSendTextRequest()

- WechatWorkWebhookApiSendTextRequest Content(string fieldValue)

- WechatWorkWebhookApiSendTextRequest MentionedList(List<string> fieldValue)

- WechatWorkWebhookApiSendTextRequest MentionedMobileList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWebhookApiSendTextResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkWebhookSpecialApi (class)

企业微信群机器人文件上传接口，使用 webhook 键 而非 access_token。

- WechatWorkClient client;

- public WechatWorkWebhookSpecialApi(WechatWorkClient client)

- async WechatResponse UploadMediaAsync(string key, string fileName, string contentType, string fileBytes)


## WechatWorkWorkBenchApi (class)

WorkBench/WorkBenchApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWorkClient client;

- public WechatWorkWorkBenchApi(WechatWorkClient client)

- async WechatWorkWorkBenchApiSetWorkBenchTemplateResponse SetWorkBenchTemplateAsync(WechatWorkWorkBenchApiSetWorkBenchTemplateRequest request)
  - POST /cgi-bin/agent/set_workbench_template

- async WechatResponse SetWorkBenchTemplateRawAsync(string query, string jsonBody)

- async WechatWorkWorkBenchApiGetWorkBenchTemplateResponse GetWorkBenchTemplateAsync(WechatWorkWorkBenchApiGetWorkBenchTemplateRequest request)
  - GET /cgi-bin/agent/get_workbench_template

- async WechatResponse GetWorkBenchTemplateRawAsync(string query)

- async WechatWorkWorkBenchApiSetWorkBenchDataResponse SetWorkBenchDataAsync(WechatWorkWorkBenchApiSetWorkBenchDataRequest request)
  - POST /cgi-bin/agent/set_workbench_data

- async WechatResponse SetWorkBenchDataRawAsync(string query, string jsonBody)


## WechatWorkWorkBenchApiGetWorkBenchTemplateRequest (class)

- WechatTypedRequest request;

- public WechatWorkWorkBenchApiGetWorkBenchTemplateRequest()

- WechatWorkWorkBenchApiGetWorkBenchTemplateRequest Agentid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWorkBenchApiGetWorkBenchTemplateResponse (class)

- public string Raw;


## WechatWorkWorkBenchApiSetWorkBenchDataRequest (class)

- WechatTypedRequest request;

- public WechatWorkWorkBenchApiSetWorkBenchDataRequest()

- WechatWorkWorkBenchApiSetWorkBenchDataRequest Userid(string fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchDataRequest Type(string fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchDataRequest Agentid(int fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchDataRequest Keydata(WechatWorkWorkBenchKeyDataModel fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchDataRequest Image(WechatWorkWorkBenchImageModel fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchDataRequest List(WechatWorkWorkBenchListModel fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchDataRequest Webview(WechatWorkWorkBenchWebViewModel fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchDataRequest ReplaceUserData(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWorkBenchApiSetWorkBenchDataResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWorkWorkBenchApiSetWorkBenchTemplateRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Work。

- WechatTypedRequest request;

- public WechatWorkWorkBenchApiSetWorkBenchTemplateRequest()

- WechatWorkWorkBenchApiSetWorkBenchTemplateRequest Type(string fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchTemplateRequest Agentid(int fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchTemplateRequest Keydata(WechatWorkWorkBenchKeyDataModel fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchTemplateRequest Image(WechatWorkWorkBenchImageModel fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchTemplateRequest List(WechatWorkWorkBenchListModel fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchTemplateRequest Webview(WechatWorkWorkBenchWebViewModel fieldValue)

- WechatWorkWorkBenchApiSetWorkBenchTemplateRequest ReplaceUserData(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWorkWorkBenchApiSetWorkBenchTemplateResponse (class)

- public string Raw;


## WwCommandEnvelope (class)

- string cmd;

- WwCommandHeaders headers;

- JsonValue body;


## WwCommandHeaders (class)

- string req_id;


## WwRobotAuthBody (class)

- string bot_id;

- string secret;
