# Sdk.Wechat.Open

> 源码: `stdlib/Sdk/Wechat/Open/WechatOpenAccountApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenClient.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenCodeApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenCodeTemplateApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenComponentApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenComponentCurrentApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenComponentOpenApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenDomainApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenIcpApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenModifyDomainApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenNewTmplApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenNickNameApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenOAuthApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenOpenApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenP1Api.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenQRConnectApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenSearchStatusApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenSecApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenSecOrderApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenSetWebViewDomainApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenSnsApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenSpecialApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenWxOpenApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenWxOpenManagedOfficialAccountApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenWxaApi.zan`, `stdlib/Sdk/Wechat/Open/WechatOpenWxaEmbeddedApi.zan`


## WechatOpenAccountApi (class)

AccountAPIs/AccountApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenAccountApi(WechatOpenClient client)

- async WechatOpenAccountApiFastRegisterResponse FastRegisterAsync(WechatOpenAccountApiFastRegisterRequest request)
  - POST /cgi-bin/account/fastregister

- async WechatResponse FastRegisterRawAsync(string query, string jsonBody)

- async WechatOpenAccountApiGetAccountBasicInfoResponse GetAccountBasicInfoAsync()
  - GET /cgi-bin/account/getaccountbasicinfo

- async WechatResponse GetAccountBasicInfoRawAsync(string query)

- async WechatOpenAccountApiModifyHeadImageResponse ModifyHeadImageAsync(WechatOpenAccountApiModifyHeadImageRequest request)
  - POST /cgi-bin/account/modifyheadimage

- async WechatResponse ModifyHeadImageRawAsync(string query, string jsonBody)

- async WechatOpenAccountApiModifySignatureResponse ModifySignatureAsync(WechatOpenAccountApiModifySignatureRequest request)
  - POST /cgi-bin/account/modifysignature

- async WechatResponse ModifySignatureRawAsync(string query, string jsonBody)

- async WechatOpenAccountApiComponentRebindAdminResponse ComponentRebindAdminAsync(WechatOpenAccountApiComponentRebindAdminRequest request)
  - POST /cgi-bin/account/componentrebindadmin

- async WechatResponse ComponentRebindAdminRawAsync(string query, string jsonBody)


## WechatOpenAccountApiComponentRebindAdminRequest (class)

- WechatTypedRequest request;

- public WechatOpenAccountApiComponentRebindAdminRequest()

- WechatOpenAccountApiComponentRebindAdminRequest Taskid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenAccountApiComponentRebindAdminResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenAccountApiFastRegisterRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenAccountApiFastRegisterRequest()

- WechatOpenAccountApiFastRegisterRequest Ticket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenAccountApiFastRegisterResponse (class)

- public string Raw;


## WechatOpenAccountApiGetAccountBasicInfoResponse (class)

- public string Raw;


## WechatOpenAccountApiModifyHeadImageRequest (class)

- WechatTypedRequest request;

- public WechatOpenAccountApiModifyHeadImageRequest()

- WechatOpenAccountApiModifyHeadImageRequest HeadImgMediaId(string fieldValue)

- WechatOpenAccountApiModifyHeadImageRequest X1(double fieldValue)

- WechatOpenAccountApiModifyHeadImageRequest Y1(double fieldValue)

- WechatOpenAccountApiModifyHeadImageRequest X2(double fieldValue)

- WechatOpenAccountApiModifyHeadImageRequest Y2(double fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenAccountApiModifyHeadImageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenAccountApiModifySignatureRequest (class)

- WechatTypedRequest request;

- public WechatOpenAccountApiModifySignatureRequest()

- WechatOpenAccountApiModifySignatureRequest Signature(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenAccountApiModifySignatureResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenClient (class)

微信开放平台 JSON API 客户端。开放平台 token 的取得依赖 verify_ticket、
authorizer refresh token 等业务状态，因此由调用方注入，不在传输层伪造。

- WechatApiTransport transport;

- string componentAccessToken;

- string authorizerAccessToken;

- public WechatOpenClient()

- WechatOpenClient Server(string host, int port)

- WechatOpenClient Timeout(int ms)

- WechatOpenClient SetComponentAccessToken(string token)

- WechatOpenClient SetAuthorizerAccessToken(string token)

- WechatOpenClient SetAccessToken(string token)
  - SetAuthorizerAccessToken 的通用别名，也可注入 OAuth access_token。

- string Credential(WechatCredentialKind kind)

- async WechatResponse RequestAsync(string method, string path, string query, string jsonBody, WechatCredentialKind credential)

- async WechatRawResponse RequestRawAsync(string method, string path, string query, string body, string contentType, WechatCredentialKind credential)

- async WechatRawResponse RequestMultipartAsync(string method, string path, string query, WechatMultipart multipart, WechatCredentialKind credential)


## WechatOpenCodeApi (class)

WxaAPIs/Code/CodeApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenCodeApi(WechatOpenClient client)

- async WechatOpenCodeApiCommitResponse CommitAsync(WechatOpenCodeApiCommitRequest request)
  - POST /wxa/commit

- async WechatResponse CommitRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiGetCategoryResponse GetCategoryAsync()
  - GET /wxa/get_category

- async WechatResponse GetCategoryRawAsync(string query)

- async WechatOpenCodeApiGetPageResponse GetPageAsync()
  - GET /wxa/get_page

- async WechatResponse GetPageRawAsync(string query)

- async WechatOpenCodeApiSubmitAuditResponse SubmitAuditAsync(WechatOpenCodeApiSubmitAuditRequest request)
  - POST /wxa/submit_audit

- async WechatResponse SubmitAuditRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiGetAuditStatusResponse GetAuditStatusAsync(WechatOpenCodeApiGetAuditStatusRequest request)
  - POST /wxa/get_auditstatus

- async WechatResponse GetAuditStatusRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiGetLatestAuditStatusResponse GetLatestAuditStatusAsync()
  - GET /wxa/get_latest_auditstatus

- async WechatResponse GetLatestAuditStatusRawAsync(string query)

- async WechatOpenCodeApiUndoCodeAuditResponse UndoCodeAuditAsync()
  - GET /wxa/undocodeaudit

- async WechatResponse UndoCodeAuditRawAsync(string query)

- async WechatOpenCodeApiReleaseResponse ReleaseAsync()
  - POST /wxa/release

- async WechatResponse ReleaseRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiChangeVisitStatusResponse ChangeVisitStatusAsync(WechatOpenCodeApiChangeVisitStatusRequest request)
  - POST /wxa/change_visitstatus

- async WechatResponse ChangeVisitStatusRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiRevertCodeReleaseResponse RevertCodeReleaseAsync(WechatOpenCodeApiRevertCodeReleaseRequest request)
  - GET /wxa/revertcoderelease

- async WechatResponse RevertCodeReleaseRawAsync(string query)

- async WechatOpenCodeApiGetWeappSupportVersionResponse GetWeappSupportVersionAsync()
  - POST /cgi-bin/wxopen/getweappsupportversion

- async WechatResponse GetWeappSupportVersionRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiSetWeappSupportVersionResponse SetWeappSupportVersionAsync(WechatOpenCodeApiSetWeappSupportVersionRequest request)
  - POST /cgi-bin/wxopen/setweappsupportversion

- async WechatResponse SetWeappSupportVersionRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiGrayReleaseResponse GrayReleaseAsync(WechatOpenCodeApiGrayReleaseRequest request)
  - POST /wxa/grayrelease

- async WechatResponse GrayReleaseRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiRevertGrayReleaseResponse RevertGrayReleaseAsync()
  - GET /wxa/revertgrayrelease

- async WechatResponse RevertGrayReleaseRawAsync(string query)

- async WechatOpenCodeApiGetGrayReleasePlanResponse GetGrayReleasePlanAsync()
  - GET /wxa/getgrayreleaseplan

- async WechatResponse GetGrayReleasePlanRawAsync(string query)

- async WechatOpenCodeApiQueryQuotaResponse QueryQuotaAsync()
  - POST /wxa/queryquota

- async WechatResponse QueryQuotaRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiSpeedupAuditResponse SpeedupAuditAsync(WechatOpenCodeApiSpeedupAuditRequest request)
  - POST /wxa/speedupaudit

- async WechatResponse SpeedupAuditRawAsync(string query, string jsonBody)


## WechatOpenCodeApiChangeVisitStatusRequest (class)

- WechatTypedRequest request;

- public WechatOpenCodeApiChangeVisitStatusRequest()

- WechatOpenCodeApiChangeVisitStatusRequest Action(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeApiChangeVisitStatusResponse (class)

- public string Raw;


## WechatOpenCodeApiCommitRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenCodeApiCommitRequest()

- WechatOpenCodeApiCommitRequest TemplateId(int fieldValue)

- WechatOpenCodeApiCommitRequest ExtJson(string fieldValue)

- WechatOpenCodeApiCommitRequest UserVersion(string fieldValue)

- WechatOpenCodeApiCommitRequest UserDesc(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeApiCommitResponse (class)

- public string Raw;


## WechatOpenCodeApiGetAuditStatusRequest (class)

- WechatTypedRequest request;

- public WechatOpenCodeApiGetAuditStatusRequest()

- WechatOpenCodeApiGetAuditStatusRequest Auditid(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeApiGetAuditStatusResponse (class)

- public string Raw;


## WechatOpenCodeApiGetCategoryResponse (class)

- public string Raw;


## WechatOpenCodeApiGetGrayReleasePlanResponse (class)

- public string Raw;


## WechatOpenCodeApiGetLatestAuditStatusResponse (class)

- public string Raw;


## WechatOpenCodeApiGetPageResponse (class)

- public string Raw;


## WechatOpenCodeApiGetWeappSupportVersionResponse (class)

- public string Raw;


## WechatOpenCodeApiGrayReleaseRequest (class)

- WechatTypedRequest request;

- public WechatOpenCodeApiGrayReleaseRequest()

- WechatOpenCodeApiGrayReleaseRequest GrayPercentage(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeApiGrayReleaseResponse (class)

- public string Raw;


## WechatOpenCodeApiQueryQuotaResponse (class)

- public string Raw;


## WechatOpenCodeApiReleaseResponse (class)

- public string Raw;


## WechatOpenCodeApiRevertCodeReleaseRequest (class)

- WechatTypedRequest request;

- public WechatOpenCodeApiRevertCodeReleaseRequest()

- WechatOpenCodeApiRevertCodeReleaseRequest AppVersion(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeApiRevertCodeReleaseResponse (class)

- public string Raw;


## WechatOpenCodeApiRevertGrayReleaseResponse (class)

- public string Raw;


## WechatOpenCodeApiSetWeappSupportVersionRequest (class)

- WechatTypedRequest request;

- public WechatOpenCodeApiSetWeappSupportVersionRequest()

- WechatOpenCodeApiSetWeappSupportVersionRequest Version(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeApiSetWeappSupportVersionResponse (class)

- public string Raw;


## WechatOpenCodeApiSpeedupAuditRequest (class)

- WechatTypedRequest request;

- public WechatOpenCodeApiSpeedupAuditRequest()

- WechatOpenCodeApiSpeedupAuditRequest Auditid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeApiSpeedupAuditResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenCodeApiSubmitAuditRequest (class)

- WechatTypedRequest request;

- public WechatOpenCodeApiSubmitAuditRequest()

- WechatOpenCodeApiSubmitAuditRequest VideoIdList(List<string> fieldValue)

- WechatOpenCodeApiSubmitAuditRequest PicIdList(List<string> fieldValue)

- WechatOpenCodeApiSubmitAuditRequest Scene(List<int> fieldValue)

- WechatOpenCodeApiSubmitAuditRequest OtherSceneDesc(string fieldValue)

- WechatOpenCodeApiSubmitAuditRequest Method(List<int> fieldValue)

- WechatOpenCodeApiSubmitAuditRequest HasAuditTeam(int fieldValue)

- WechatOpenCodeApiSubmitAuditRequest AuditDesc(string fieldValue)

- WechatOpenCodeApiSubmitAuditRequest ItemList(List<WechatOpenSubmitAuditPageInfo> fieldValue)

- WechatOpenCodeApiSubmitAuditRequest VersionDesc(string fieldValue)

- WechatOpenCodeApiSubmitAuditRequest FeedbackInfo(string fieldValue)

- WechatOpenCodeApiSubmitAuditRequest FeedbackStuff(string fieldValue)

- WechatOpenCodeApiSubmitAuditRequest PrivacyApiNotUse(bool fieldValue)

- WechatOpenCodeApiSubmitAuditRequest OrderPath(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeApiSubmitAuditResponse (class)

- public string Raw;


## WechatOpenCodeApiUndoCodeAuditResponse (class)

- public string Raw;


## WechatOpenCodeTemplateApi (class)

WxaAPIs/CodeTemplate/CodeTemplateApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenCodeTemplateApi(WechatOpenClient client)

- async WechatOpenCodeTemplateApiGetTemplateDraftListResponse GetTemplateDraftListAsync()
  - GET /wxa/gettemplatedraftlist

- async WechatResponse GetTemplateDraftListRawAsync(string query)

- async WechatOpenCodeTemplateApiGetTemplateListResponse GetTemplateListAsync()
  - GET /wxa/gettemplatelist

- async WechatResponse GetTemplateListRawAsync(string query)

- async WechatOpenCodeTemplateApiAddToTemplateResponse AddToTemplateAsync(WechatOpenCodeTemplateApiAddToTemplateRequest request)
  - POST /wxa/addtotemplate

- async WechatResponse AddToTemplateRawAsync(string query, string jsonBody)

- async WechatOpenCodeTemplateApiDeleteTemplateResponse DeleteTemplateAsync(WechatOpenCodeTemplateApiDeleteTemplateRequest request)
  - POST /wxa/deletetemplate

- async WechatResponse DeleteTemplateRawAsync(string query, string jsonBody)


## WechatOpenCodeTemplateApiAddToTemplateRequest (class)

- WechatTypedRequest request;

- public WechatOpenCodeTemplateApiAddToTemplateRequest()

- WechatOpenCodeTemplateApiAddToTemplateRequest DraftId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeTemplateApiAddToTemplateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenCodeTemplateApiDeleteTemplateRequest (class)

- WechatTypedRequest request;

- public WechatOpenCodeTemplateApiDeleteTemplateRequest()

- WechatOpenCodeTemplateApiDeleteTemplateRequest TemplateId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenCodeTemplateApiDeleteTemplateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenCodeTemplateApiGetTemplateDraftListResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- public string Raw;


## WechatOpenCodeTemplateApiGetTemplateListResponse (class)

- public string Raw;


## WechatOpenComponentApi (class)

ComponentAPIs/ComponentApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenComponentApi(WechatOpenClient client)

- async WechatOpenComponentApiGetComponentAccessTokenResponse GetComponentAccessTokenAsync(WechatOpenComponentApiGetComponentAccessTokenRequest request)
  - POST /cgi-bin/component/api_component_token

- async WechatResponse GetComponentAccessTokenRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetPreAuthCodeResponse GetPreAuthCodeAsync(WechatOpenComponentApiGetPreAuthCodeRequest request)
  - POST /cgi-bin/component/api_create_preauthcode

- async WechatResponse GetPreAuthCodeRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiQueryAuthResponse QueryAuthAsync(WechatOpenComponentApiQueryAuthRequest request)
  - POST /cgi-bin/component/api_query_auth

- async WechatResponse QueryAuthRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiApiConfirmAuthResponse ApiConfirmAuthAsync(WechatOpenComponentApiApiConfirmAuthRequest request)
  - POST /cgi-bin/component/api_confirm_authorization

- async WechatResponse ApiConfirmAuthRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiApiAuthorizerTokenResponse ApiAuthorizerTokenAsync(WechatOpenComponentApiApiAuthorizerTokenRequest request)
  - POST /cgi-bin/component/api_authorizer_token

- async WechatResponse ApiAuthorizerTokenRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetAuthorizerInfoResponse GetAuthorizerInfoAsync(WechatOpenComponentApiGetAuthorizerInfoRequest request)
  - POST /cgi-bin/component/api_get_authorizer_info

- async WechatResponse GetAuthorizerInfoRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetAuthorizerOptionResponse GetAuthorizerOptionAsync(WechatOpenComponentApiGetAuthorizerOptionRequest request)
  - POST /cgi-bin/component/api_get_authorizer_option

- async WechatResponse GetAuthorizerOptionRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiSetAuthorizerOptionResponse SetAuthorizerOptionAsync(WechatOpenComponentApiSetAuthorizerOptionRequest request)
  - POST /cgi-bin/component/api_set_authorizer_option

- async WechatResponse SetAuthorizerOptionRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetJsApiTicketResponse GetJsApiTicketAsync(WechatOpenComponentApiGetJsApiTicketRequest request)
  - GET /cgi-bin/ticket/getticket

- async WechatResponse GetJsApiTicketRawAsync(string query)

- async WechatOpenComponentApiFastRegisterEnterpriseWeAppResponse FastRegisterEnterpriseWeAppAsync(WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest request)
  - POST /wxa/component/fastregisterenterpriseweapp

- async WechatResponse FastRegisterEnterpriseWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiFastRegisterWeAppResponse FastRegisterWeAppAsync(WechatOpenComponentApiFastRegisterWeAppRequest request)
  - POST /cgi-bin/component/fastregisterweapp

- async WechatResponse FastRegisterWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiFastRegisterPersonalWeAppResponse FastRegisterPersonalWeAppAsync(WechatOpenComponentApiFastRegisterPersonalWeAppRequest request)
  - POST /wxa/component/fastregisterpersonalweapp

- async WechatResponse FastRegisterPersonalWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiFastRegisterBetaWeAppResponse FastRegisterBetaWeAppAsync(WechatOpenComponentApiFastRegisterBetaWeAppRequest request)
  - POST /wxa/component/fastregisterbetaweapp

- async WechatResponse FastRegisterBetaWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiVerifyBetaWeAppResponse VerifyBetaWeAppAsync(WechatOpenComponentApiVerifyBetaWeAppRequest request)
  - POST /wxa/verifybetaweapp

- async WechatResponse VerifyBetaWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiSetBetaWeAppNickNameResponse SetBetaWeAppNickNameAsync(WechatOpenComponentApiSetBetaWeAppNickNameRequest request)
  - POST /wxa/setbetaweappnickname

- async WechatResponse SetBetaWeAppNickNameRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetAuthorizerListResponse GetAuthorizerListAsync(WechatOpenComponentApiGetAuthorizerListRequest request)
  - POST /cgi-bin/component/api_get_authorizer_list

- async WechatResponse GetAuthorizerListRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiSetPrivacySettingResponse SetPrivacySettingAsync(WechatOpenComponentApiSetPrivacySettingRequest request)
  - POST /cgi-bin/component/setprivacysetting

- async WechatResponse SetPrivacySettingRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetPrivacySettingResponse GetPrivacySettingAsync(WechatOpenComponentApiGetPrivacySettingRequest request)
  - POST /cgi-bin/component/getprivacysetting

- async WechatResponse GetPrivacySettingRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiModifyWxaServerDomainResponse ModifyWxaServerDomainAsync(WechatOpenComponentApiModifyWxaServerDomainRequest request)
  - POST /cgi-bin/component/modify_wxa_server_domain

- async WechatResponse ModifyWxaServerDomainRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetDomainConfirmFileResponse GetDomainConfirmFileAsync(WechatOpenComponentApiGetDomainConfirmFileRequest request)
  - POST /cgi-bin/component/get_domain_confirmfile

- async WechatResponse GetDomainConfirmFileRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiModifyWxaJumpDomainResponse ModifyWxaJumpDomainAsync(WechatOpenComponentApiModifyWxaJumpDomainRequest request)
  - POST /cgi-bin/component/modify_wxa_jump_domain

- async WechatResponse ModifyWxaJumpDomainRawAsync(string query, string jsonBody)


## WechatOpenComponentApiApiAuthorizerTokenRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiApiAuthorizerTokenRequest()

- WechatOpenComponentApiApiAuthorizerTokenRequest ComponentAppId(string fieldValue)

- WechatOpenComponentApiApiAuthorizerTokenRequest AuthorizerAppId(string fieldValue)

- WechatOpenComponentApiApiAuthorizerTokenRequest AuthorizerRefreshToken(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiApiAuthorizerTokenResponse (class)

- public string Raw;


## WechatOpenComponentApiApiConfirmAuthRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiApiConfirmAuthRequest()

- WechatOpenComponentApiApiConfirmAuthRequest ComponentAppId(string fieldValue)

- WechatOpenComponentApiApiConfirmAuthRequest AuthorizerAppid(string fieldValue)

- WechatOpenComponentApiApiConfirmAuthRequest FunscopeCategoryId(int fieldValue)

- WechatOpenComponentApiApiConfirmAuthRequest ConfirmValue(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiApiConfirmAuthResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenComponentApiFastRegisterBetaWeAppRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiFastRegisterBetaWeAppRequest()

- WechatOpenComponentApiFastRegisterBetaWeAppRequest Name(string fieldValue)

- WechatOpenComponentApiFastRegisterBetaWeAppRequest Openid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiFastRegisterBetaWeAppResponse (class)

- public string Raw;


## WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest()

- WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest EntName(string fieldValue)

- WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest EntCode(string fieldValue)

- WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest CodeType(int fieldValue)

- WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest LegalPersonaOpenId(string fieldValue)

- WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest LegalPersonaName(string fieldValue)

- WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest ComponentPhone(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiFastRegisterEnterpriseWeAppResponse (class)

- public string Raw;


## WechatOpenComponentApiFastRegisterPersonalWeAppRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiFastRegisterPersonalWeAppRequest()

- WechatOpenComponentApiFastRegisterPersonalWeAppRequest WxUser(string fieldValue)

- WechatOpenComponentApiFastRegisterPersonalWeAppRequest ComponentPhone(string fieldValue)

- WechatOpenComponentApiFastRegisterPersonalWeAppRequest NewVersion(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiFastRegisterPersonalWeAppResponse (class)

- public string Raw;


## WechatOpenComponentApiFastRegisterWeAppRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiFastRegisterWeAppRequest()

- WechatOpenComponentApiFastRegisterWeAppRequest EntName(string fieldValue)

- WechatOpenComponentApiFastRegisterWeAppRequest EntCode(string fieldValue)

- WechatOpenComponentApiFastRegisterWeAppRequest CodeType(int fieldValue)

- WechatOpenComponentApiFastRegisterWeAppRequest LegalPersonaWechat(string fieldValue)

- WechatOpenComponentApiFastRegisterWeAppRequest LegalPersonaName(string fieldValue)

- WechatOpenComponentApiFastRegisterWeAppRequest ComponentPhone(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiFastRegisterWeAppResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenComponentApiGetAuthorizerInfoRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiGetAuthorizerInfoRequest()

- WechatOpenComponentApiGetAuthorizerInfoRequest ComponentAppId(string fieldValue)

- WechatOpenComponentApiGetAuthorizerInfoRequest AuthorizerAppId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiGetAuthorizerInfoResponse (class)

- public string Raw;


## WechatOpenComponentApiGetAuthorizerListRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiGetAuthorizerListRequest()

- WechatOpenComponentApiGetAuthorizerListRequest ComponentAppId(string fieldValue)

- WechatOpenComponentApiGetAuthorizerListRequest Offset(int fieldValue)

- WechatOpenComponentApiGetAuthorizerListRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiGetAuthorizerListResponse (class)

- public string Raw;


## WechatOpenComponentApiGetAuthorizerOptionRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiGetAuthorizerOptionRequest()

- WechatOpenComponentApiGetAuthorizerOptionRequest ComponentAppId(string fieldValue)

- WechatOpenComponentApiGetAuthorizerOptionRequest AuthorizerAppId(string fieldValue)

- WechatOpenComponentApiGetAuthorizerOptionRequest OptionName(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiGetAuthorizerOptionResponse (class)

- public string Raw;


## WechatOpenComponentApiGetComponentAccessTokenRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenComponentApiGetComponentAccessTokenRequest()

- WechatOpenComponentApiGetComponentAccessTokenRequest ComponentAppId(string fieldValue)

- WechatOpenComponentApiGetComponentAccessTokenRequest ComponentAppSecret(string fieldValue)

- WechatOpenComponentApiGetComponentAccessTokenRequest ComponentVerifyTicket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiGetComponentAccessTokenResponse (class)

- public string Raw;


## WechatOpenComponentApiGetDomainConfirmFileRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiGetDomainConfirmFileRequest()

- WechatOpenComponentApiGetDomainConfirmFileRequest ComponentAccessToken(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiGetDomainConfirmFileResponse (class)

- public string Raw;


## WechatOpenComponentApiGetJsApiTicketRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiGetJsApiTicketRequest()

- WechatOpenComponentApiGetJsApiTicketRequest Type(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiGetJsApiTicketResponse (class)

- public string Raw;


## WechatOpenComponentApiGetPreAuthCodeRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiGetPreAuthCodeRequest()

- WechatOpenComponentApiGetPreAuthCodeRequest ComponentAppId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiGetPreAuthCodeResponse (class)

- public string Raw;


## WechatOpenComponentApiGetPrivacySettingRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiGetPrivacySettingRequest()

- WechatOpenComponentApiGetPrivacySettingRequest PrivacyVer(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiGetPrivacySettingResponse (class)

- public string Raw;


## WechatOpenComponentApiModifyWxaJumpDomainRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiModifyWxaJumpDomainRequest()

- WechatOpenComponentApiModifyWxaJumpDomainRequest Action(int fieldValue)

- WechatOpenComponentApiModifyWxaJumpDomainRequest WxaJumpH5Domain(string fieldValue)

- WechatOpenComponentApiModifyWxaJumpDomainRequest IsModifyPublishedTogether(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiModifyWxaJumpDomainResponse (class)

- public string Raw;


## WechatOpenComponentApiModifyWxaServerDomainRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiModifyWxaServerDomainRequest()

- WechatOpenComponentApiModifyWxaServerDomainRequest Action(int fieldValue)

- WechatOpenComponentApiModifyWxaServerDomainRequest WxaServerDomain(string fieldValue)

- WechatOpenComponentApiModifyWxaServerDomainRequest IsModifyPublishedTogether(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiModifyWxaServerDomainResponse (class)

- public string Raw;


## WechatOpenComponentApiQueryAuthRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiQueryAuthRequest()

- WechatOpenComponentApiQueryAuthRequest ComponentAppId(string fieldValue)

- WechatOpenComponentApiQueryAuthRequest AuthorizationCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiQueryAuthResponse (class)

- public string Raw;


## WechatOpenComponentApiSetAuthorizerOptionRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiSetAuthorizerOptionRequest()

- WechatOpenComponentApiSetAuthorizerOptionRequest ComponentAppId(string fieldValue)

- WechatOpenComponentApiSetAuthorizerOptionRequest AuthorizerAppId(string fieldValue)

- WechatOpenComponentApiSetAuthorizerOptionRequest OptionName(int fieldValue)

- WechatOpenComponentApiSetAuthorizerOptionRequest OptionValue(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiSetAuthorizerOptionResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenComponentApiSetBetaWeAppNickNameRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiSetBetaWeAppNickNameRequest()

- WechatOpenComponentApiSetBetaWeAppNickNameRequest Name(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiSetBetaWeAppNickNameResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenComponentApiSetPrivacySettingRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiSetPrivacySettingRequest()

- WechatOpenComponentApiSetPrivacySettingRequest ContactEmail(string fieldValue)

- WechatOpenComponentApiSetPrivacySettingRequest ContactPhone(string fieldValue)

- WechatOpenComponentApiSetPrivacySettingRequest ContactQq(string fieldValue)

- WechatOpenComponentApiSetPrivacySettingRequest ContactWeixin(string fieldValue)

- WechatOpenComponentApiSetPrivacySettingRequest ExtFileMediaId(string fieldValue)

- WechatOpenComponentApiSetPrivacySettingRequest NoticeMethod(string fieldValue)

- WechatOpenComponentApiSetPrivacySettingRequest StoreExpireTimestamp(string fieldValue)

- WechatOpenComponentApiSetPrivacySettingRequest ComponentAccessToken(string fieldValue)

- WechatOpenComponentApiSetPrivacySettingRequest SettingList(List<WechatOpenSetPrivacySettingDataSettingList> fieldValue)

- WechatOpenComponentApiSetPrivacySettingRequest PrivacyVer(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiSetPrivacySettingResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenComponentApiVerifyBetaWeAppRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentApiVerifyBetaWeAppRequest()

- WechatOpenComponentApiVerifyBetaWeAppRequest EntName(string fieldValue)

- WechatOpenComponentApiVerifyBetaWeAppRequest EntCode(string fieldValue)

- WechatOpenComponentApiVerifyBetaWeAppRequest CodeType(int fieldValue)

- WechatOpenComponentApiVerifyBetaWeAppRequest LegalPersonaWechat(string fieldValue)

- WechatOpenComponentApiVerifyBetaWeAppRequest LegalPersonaName(string fieldValue)

- WechatOpenComponentApiVerifyBetaWeAppRequest LegalPersonaIDCard(string fieldValue)

- WechatOpenComponentApiVerifyBetaWeAppRequest ComponentPhone(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentApiVerifyBetaWeAppResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenComponentCurrentApi (class)

ComponentAPIs/ComponentApi.Current.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenComponentCurrentApi(WechatOpenClient client)

- async WechatOpenComponentCurrentApiGetAuthorizerOptionInfoResponse GetAuthorizerOptionInfoAsync(WechatOpenComponentCurrentApiGetAuthorizerOptionInfoRequest request)
  - POST /cgi-bin/component/get_authorizer_option

- async WechatResponse GetAuthorizerOptionInfoRawAsync(string query, string jsonBody)

- async WechatOpenComponentCurrentApiSetAuthorizerOptionInfoResponse SetAuthorizerOptionInfoAsync(WechatOpenComponentCurrentApiSetAuthorizerOptionInfoRequest request)
  - POST /cgi-bin/component/set_authorizer_option

- async WechatResponse SetAuthorizerOptionInfoRawAsync(string query, string jsonBody)


## WechatOpenComponentCurrentApiGetAuthorizerOptionInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenComponentCurrentApiGetAuthorizerOptionInfoRequest()

- WechatOpenComponentCurrentApiGetAuthorizerOptionInfoRequest OptionName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentCurrentApiGetAuthorizerOptionInfoResponse (class)

- public string Raw;


## WechatOpenComponentCurrentApiSetAuthorizerOptionInfoRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentCurrentApiSetAuthorizerOptionInfoRequest()

- WechatOpenComponentCurrentApiSetAuthorizerOptionInfoRequest OptionName(string fieldValue)

- WechatOpenComponentCurrentApiSetAuthorizerOptionInfoRequest OptionValue(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentCurrentApiSetAuthorizerOptionInfoResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenComponentOpenApi (class)

ComponentAPIs/ComponentOpenApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenComponentOpenApi(WechatOpenClient client)

- async WechatOpenComponentOpenApiStartPushTicketResponse StartPushTicketAsync(WechatOpenComponentOpenApiStartPushTicketRequest request)
  - POST /cgi-bin/component/api_start_push_ticket

- async WechatResponse StartPushTicketRawAsync(string query, string jsonBody)

- async WechatOpenComponentOpenApiClearComponentQuotaByAppSecretResponse ClearComponentQuotaByAppSecretAsync(WechatOpenComponentOpenApiClearComponentQuotaByAppSecretRequest request)
  - POST /cgi-bin/component/clear_quota/v2

- async WechatResponse ClearComponentQuotaByAppSecretRawAsync(string query, string jsonBody)


## WechatOpenComponentOpenApiClearComponentQuotaByAppSecretRequest (class)

- WechatTypedRequest request;

- public WechatOpenComponentOpenApiClearComponentQuotaByAppSecretRequest()

- WechatOpenComponentOpenApiClearComponentQuotaByAppSecretRequest AppId(string fieldValue)

- WechatOpenComponentOpenApiClearComponentQuotaByAppSecretRequest ComponentAppId(string fieldValue)

- WechatOpenComponentOpenApiClearComponentQuotaByAppSecretRequest ComponentSecret(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentOpenApiClearComponentQuotaByAppSecretResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenComponentOpenApiStartPushTicketRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenComponentOpenApiStartPushTicketRequest()

- WechatOpenComponentOpenApiStartPushTicketRequest ComponentAppId(string fieldValue)

- WechatOpenComponentOpenApiStartPushTicketRequest ComponentSecret(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenComponentOpenApiStartPushTicketResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenDomainApi (class)

WxaAPIs/Domain/DomainApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenDomainApi(WechatOpenClient client)

- async WechatOpenDomainApiModifyDomainResponse ModifyDomainAsync(WechatOpenDomainApiModifyDomainRequest request)
  - POST /wxa/modify_domain

- async WechatResponse ModifyDomainRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiSetWebViewDomainResponse SetWebViewDomainAsync(WechatOpenDomainApiSetWebViewDomainRequest request)
  - POST /wxa/setwebviewdomain

- async WechatResponse SetWebViewDomainRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiModifyDomainDirectlyResponse ModifyDomainDirectlyAsync(WechatOpenDomainApiModifyDomainDirectlyRequest request)
  - POST /wxa/modify_domain_directly

- async WechatResponse ModifyDomainDirectlyRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiGetWebViewDomainConfirmFileResponse GetWebViewDomainConfirmFileAsync()
  - POST /wxa/get_webviewdomain_confirmfile

- async WechatResponse GetWebViewDomainConfirmFileRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiSetWebViewDomainDirectlyResponse SetWebViewDomainDirectlyAsync(WechatOpenDomainApiSetWebViewDomainDirectlyRequest request)
  - POST /wxa/setwebviewdomain_directly

- async WechatResponse SetWebViewDomainDirectlyRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiGetEffectiveDomainResponse GetEffectiveDomainAsync()
  - POST /wxa/get_effective_domain

- async WechatResponse GetEffectiveDomainRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiGetEffectiveWebViewDomainResponse GetEffectiveWebViewDomainAsync()
  - POST /wxa/get_effective_webviewdomain

- async WechatResponse GetEffectiveWebViewDomainRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiGetPrefetchDNSDomainResponse GetPrefetchDNSDomainAsync()
  - GET /wxa/get_prefetchdnsdomain

- async WechatResponse GetPrefetchDNSDomainRawAsync(string query)

- async WechatOpenDomainApiSetPrefetchDNSDomainResponse SetPrefetchDNSDomainAsync(WechatOpenDomainApiSetPrefetchDNSDomainRequest request)
  - POST /wxa/set_prefetchdnsdomain

- async WechatResponse SetPrefetchDNSDomainRawAsync(string query, string jsonBody)


## WechatOpenDomainApiGetEffectiveDomainResponse (class)

- public string Raw;


## WechatOpenDomainApiGetEffectiveWebViewDomainResponse (class)

- public string Raw;


## WechatOpenDomainApiGetPrefetchDNSDomainResponse (class)

- public string Raw;


## WechatOpenDomainApiGetWebViewDomainConfirmFileResponse (class)

- public string Raw;


## WechatOpenDomainApiModifyDomainDirectlyRequest (class)

- WechatTypedRequest request;

- public WechatOpenDomainApiModifyDomainDirectlyRequest()

- WechatOpenDomainApiModifyDomainDirectlyRequest Action(int fieldValue)

- WechatOpenDomainApiModifyDomainDirectlyRequest Requestdomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainDirectlyRequest Wsrequestdomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainDirectlyRequest Uploaddomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainDirectlyRequest Downloaddomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainDirectlyRequest Udpdomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainDirectlyRequest Tcpdomain(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenDomainApiModifyDomainDirectlyResponse (class)

- public string Raw;


## WechatOpenDomainApiModifyDomainRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenDomainApiModifyDomainRequest()

- WechatOpenDomainApiModifyDomainRequest Action(int fieldValue)

- WechatOpenDomainApiModifyDomainRequest Requestdomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainRequest Wsrequestdomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainRequest Uploaddomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainRequest Downloaddomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainRequest Udpdomain(List<string> fieldValue)

- WechatOpenDomainApiModifyDomainRequest Tcpdomain(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenDomainApiModifyDomainResponse (class)

- public string Raw;


## WechatOpenDomainApiSetPrefetchDNSDomainRequest (class)

- WechatTypedRequest request;

- public WechatOpenDomainApiSetPrefetchDNSDomainRequest()

- WechatOpenDomainApiSetPrefetchDNSDomainRequest PrefetchDnsDomain(List<WechatOpenSetPrefetchDNSDomainData> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenDomainApiSetPrefetchDNSDomainResponse (class)

- public string Raw;


## WechatOpenDomainApiSetWebViewDomainDirectlyRequest (class)

- WechatTypedRequest request;

- public WechatOpenDomainApiSetWebViewDomainDirectlyRequest()

- WechatOpenDomainApiSetWebViewDomainDirectlyRequest Action(int fieldValue)

- WechatOpenDomainApiSetWebViewDomainDirectlyRequest Webviewdomain(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenDomainApiSetWebViewDomainDirectlyResponse (class)

- public string Raw;


## WechatOpenDomainApiSetWebViewDomainRequest (class)

- WechatTypedRequest request;

- public WechatOpenDomainApiSetWebViewDomainRequest()

- WechatOpenDomainApiSetWebViewDomainRequest Action(int fieldValue)

- WechatOpenDomainApiSetWebViewDomainRequest Webviewdomain(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenDomainApiSetWebViewDomainResponse (class)

- public string Raw;


## WechatOpenIcpApi (class)

WxaAPIs/Icp/IcpApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenIcpApi(WechatOpenClient client)

- async WechatOpenIcpApiQueryIcpVerifyTaskResponse QueryIcpVerifyTaskAsync(WechatOpenIcpApiQueryIcpVerifyTaskRequest request)
  - POST /wxa/icp/query_icp_verifytask

- async WechatResponse QueryIcpVerifyTaskRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiCreateIcpVerifyTaskResponse CreateIcpVerifyTaskAsync()
  - POST /wxa/icp/create_icp_verifytask

- async WechatResponse CreateIcpVerifyTaskRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiCancelApplyIcpFilingResponse CancelApplyIcpFilingAsync()
  - POST /wxa/icp/cancel_apply_icp_filing

- async WechatResponse CancelApplyIcpFilingRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiApplyIcpFilingResponse ApplyIcpFilingAsync(WechatOpenIcpApiApplyIcpFilingRequest request)
  - POST /wxa/icp/apply_icp_filing

- async WechatResponse ApplyIcpFilingRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiCancelIcpFilingResponse CancelIcpFilingAsync(WechatOpenIcpApiCancelIcpFilingRequest request)
  - POST /wxa/icp/cancel_icp_filing

- async WechatResponse CancelIcpFilingRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiGetIcpEntranceInfoResponse GetIcpEntranceInfoAsync()
  - GET /wxa/icp/get_icp_entrance_info

- async WechatResponse GetIcpEntranceInfoRawAsync(string query)

- async WechatOpenIcpApiGetOnlineIcpOrderResponse GetOnlineIcpOrderAsync()
  - GET /wxa/icp/get_online_icp_order

- async WechatResponse GetOnlineIcpOrderRawAsync(string query)

- async WechatOpenIcpApiQueryIcpServiceContentTypesResponse QueryIcpServiceContentTypesAsync()
  - GET /wxa/icp/query_icp_service_content_types

- async WechatResponse QueryIcpServiceContentTypesRawAsync(string query)

- async WechatOpenIcpApiQueryIcpCertificateTypesResponse QueryIcpCertificateTypesAsync()
  - GET /wxa/icp/query_icp_certificate_types

- async WechatResponse QueryIcpCertificateTypesRawAsync(string query)

- async WechatOpenIcpApiQueryIcpDistrictCodeResponse QueryIcpDistrictCodeAsync()
  - GET /wxa/icp/query_icp_district_code

- async WechatResponse QueryIcpDistrictCodeRawAsync(string query)

- async WechatOpenIcpApiQueryIcpNrlxTypesResponse QueryIcpNrlxTypesAsync()
  - GET /wxa/icp/query_icp_nrlx_types

- async WechatResponse QueryIcpNrlxTypesRawAsync(string query)

- async WechatOpenIcpApiQueryIcpSubjectTypesResponse QueryIcpSubjectTypesAsync()
  - GET /wxa/icp/query_icp_subject_types

- async WechatResponse QueryIcpSubjectTypesRawAsync(string query)


## WechatOpenIcpApiApplyIcpFilingRequest (class)

- WechatTypedRequest request;

- public WechatOpenIcpApiApplyIcpFilingRequest()

- WechatOpenIcpApiApplyIcpFilingRequest IcpSubject(WechatOpenIcpSubjectModel fieldValue)

- WechatOpenIcpApiApplyIcpFilingRequest IcpApplets(WechatOpenIcpAppletsModel fieldValue)

- WechatOpenIcpApiApplyIcpFilingRequest IcpMaterials(WechatOpenIcpMaterialsModel fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenIcpApiApplyIcpFilingResponse (class)

- public string Raw;


## WechatOpenIcpApiCancelApplyIcpFilingResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenIcpApiCancelIcpFilingRequest (class)

- WechatTypedRequest request;

- public WechatOpenIcpApiCancelIcpFilingRequest()

- WechatOpenIcpApiCancelIcpFilingRequest CancelType(int fieldValue)

- WechatOpenIcpApiCancelIcpFilingRequest ReasonType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenIcpApiCancelIcpFilingResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenIcpApiCreateIcpVerifyTaskResponse (class)

- public string Raw;


## WechatOpenIcpApiGetIcpEntranceInfoResponse (class)

- public string Raw;


## WechatOpenIcpApiGetOnlineIcpOrderResponse (class)

- public string Raw;


## WechatOpenIcpApiQueryIcpCertificateTypesResponse (class)

- public string Raw;


## WechatOpenIcpApiQueryIcpDistrictCodeResponse (class)

- public string Raw;


## WechatOpenIcpApiQueryIcpNrlxTypesResponse (class)

- public string Raw;


## WechatOpenIcpApiQueryIcpServiceContentTypesResponse (class)

- public string Raw;


## WechatOpenIcpApiQueryIcpSubjectTypesResponse (class)

- public string Raw;


## WechatOpenIcpApiQueryIcpVerifyTaskRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenIcpApiQueryIcpVerifyTaskRequest()

- WechatOpenIcpApiQueryIcpVerifyTaskRequest TaskId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenIcpApiQueryIcpVerifyTaskResponse (class)

- public string Raw;


## WechatOpenModifyDomainApi (class)

WxaAPIs/ModifyDomain/ModifyDomainApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenModifyDomainApi(WechatOpenClient client)

- async WechatOpenModifyDomainApiModifyDomainResponse ModifyDomainAsync(WechatOpenModifyDomainApiModifyDomainRequest request)
  - POST /wxa/modify_domain

- async WechatResponse ModifyDomainRawAsync(string query, string jsonBody)


## WechatOpenModifyDomainApiModifyDomainRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenModifyDomainApiModifyDomainRequest()

- WechatOpenModifyDomainApiModifyDomainRequest Action(int fieldValue)

- WechatOpenModifyDomainApiModifyDomainRequest Requestdomain(List<string> fieldValue)

- WechatOpenModifyDomainApiModifyDomainRequest Wsrequestdomain(List<string> fieldValue)

- WechatOpenModifyDomainApiModifyDomainRequest Uploaddomain(List<string> fieldValue)

- WechatOpenModifyDomainApiModifyDomainRequest Downloaddomain(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenModifyDomainApiModifyDomainResponse (class)

- public string Raw;


## WechatOpenNewTmplApi (class)

WxaAPIs/NewTmpl/NewTmplApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenNewTmplApi(WechatOpenClient client)

- async WechatOpenNewTmplApiGetPubTemplateTitlesResponse GetPubTemplateTitlesAsync(WechatOpenNewTmplApiGetPubTemplateTitlesRequest request)
  - GET /wxaapi/newtmpl/getpubtemplatetitles

- async WechatResponse GetPubTemplateTitlesRawAsync(string query)

- async WechatOpenNewTmplApiGetPubTemplateKeyWordsByIdResponse GetPubTemplateKeyWordsByIdAsync(WechatOpenNewTmplApiGetPubTemplateKeyWordsByIdRequest request)
  - GET /wxaapi/newtmpl/getpubtemplatekeywords

- async WechatResponse GetPubTemplateKeyWordsByIdRawAsync(string query)

- async WechatOpenNewTmplApiAddTemplateResponse AddTemplateAsync(WechatOpenNewTmplApiAddTemplateRequest request)
  - POST /wxaapi/newtmpl/addtemplate

- async WechatResponse AddTemplateRawAsync(string query, string jsonBody)

- async WechatOpenNewTmplApiGetTemplateListResponse GetTemplateListAsync()
  - GET /wxaapi/newtmpl/gettemplate

- async WechatResponse GetTemplateListRawAsync(string query)

- async WechatOpenNewTmplApiGetCategoryResponse GetCategoryAsync()
  - GET /wxaapi/newtmpl/getcategory

- async WechatResponse GetCategoryRawAsync(string query)

- async WechatOpenNewTmplApiDelTemplateResponse DelTemplateAsync(WechatOpenNewTmplApiDelTemplateRequest request)
  - POST /wxaapi/newtmpl/deltemplate

- async WechatResponse DelTemplateRawAsync(string query, string jsonBody)


## WechatOpenNewTmplApiAddTemplateRequest (class)

- WechatTypedRequest request;

- public WechatOpenNewTmplApiAddTemplateRequest()

- WechatOpenNewTmplApiAddTemplateRequest Tid(string fieldValue)

- WechatOpenNewTmplApiAddTemplateRequest KidList(List<int> fieldValue)

- WechatOpenNewTmplApiAddTemplateRequest SceneDesc(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenNewTmplApiAddTemplateResponse (class)

- public string Raw;


## WechatOpenNewTmplApiDelTemplateRequest (class)

- WechatTypedRequest request;

- public WechatOpenNewTmplApiDelTemplateRequest()

- WechatOpenNewTmplApiDelTemplateRequest PriTmplId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenNewTmplApiDelTemplateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenNewTmplApiGetCategoryResponse (class)

- public string Raw;


## WechatOpenNewTmplApiGetPubTemplateKeyWordsByIdRequest (class)

- WechatTypedRequest request;

- public WechatOpenNewTmplApiGetPubTemplateKeyWordsByIdRequest()

- WechatOpenNewTmplApiGetPubTemplateKeyWordsByIdRequest Tid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenNewTmplApiGetPubTemplateKeyWordsByIdResponse (class)

- public string Raw;


## WechatOpenNewTmplApiGetPubTemplateTitlesRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenNewTmplApiGetPubTemplateTitlesRequest()

- WechatOpenNewTmplApiGetPubTemplateTitlesRequest Ids(string fieldValue)

- WechatOpenNewTmplApiGetPubTemplateTitlesRequest Start(int fieldValue)

- WechatOpenNewTmplApiGetPubTemplateTitlesRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenNewTmplApiGetPubTemplateTitlesResponse (class)

- public string Raw;


## WechatOpenNewTmplApiGetTemplateListResponse (class)

- public string Raw;


## WechatOpenNickNameApi (class)

WxaAPIs/NickName/NickNameApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenNickNameApi(WechatOpenClient client)

- async WechatOpenNickNameApiSetNickNameResponse SetNickNameAsync(WechatOpenNickNameApiSetNickNameRequest request)
  - POST /wxa/setnickname

- async WechatResponse SetNickNameRawAsync(string query, string jsonBody)

- async WechatOpenNickNameApiQueryNickNameResponse QueryNickNameAsync(WechatOpenNickNameApiQueryNickNameRequest request)
  - POST /wxa/api_wxa_querynickname

- async WechatResponse QueryNickNameRawAsync(string query, string jsonBody)

- async WechatOpenNickNameApiCheckWxVerifyNickNameResponse CheckWxVerifyNickNameAsync(WechatOpenNickNameApiCheckWxVerifyNickNameRequest request)
  - POST /cgi-bin/wxverify/checkwxverifynickname

- async WechatResponse CheckWxVerifyNickNameRawAsync(string query, string jsonBody)


## WechatOpenNickNameApiCheckWxVerifyNickNameRequest (class)

- WechatTypedRequest request;

- public WechatOpenNickNameApiCheckWxVerifyNickNameRequest()

- WechatOpenNickNameApiCheckWxVerifyNickNameRequest NickName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenNickNameApiCheckWxVerifyNickNameResponse (class)

- public string Raw;


## WechatOpenNickNameApiQueryNickNameRequest (class)

- WechatTypedRequest request;

- public WechatOpenNickNameApiQueryNickNameRequest()

- WechatOpenNickNameApiQueryNickNameRequest AuditId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenNickNameApiQueryNickNameResponse (class)

- public string Raw;


## WechatOpenNickNameApiSetNickNameRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenNickNameApiSetNickNameRequest()

- WechatOpenNickNameApiSetNickNameRequest NickName(string fieldValue)

- WechatOpenNickNameApiSetNickNameRequest IdCard(string fieldValue)

- WechatOpenNickNameApiSetNickNameRequest License(string fieldValue)

- WechatOpenNickNameApiSetNickNameRequest NamingOtherStuff1(string fieldValue)

- WechatOpenNickNameApiSetNickNameRequest NamingOtherStuff2(string fieldValue)

- WechatOpenNickNameApiSetNickNameRequest NamingOtherStuff3(string fieldValue)

- WechatOpenNickNameApiSetNickNameRequest NamingOtherStuff4(string fieldValue)

- WechatOpenNickNameApiSetNickNameRequest NamingOtherStuff5(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenNickNameApiSetNickNameResponse (class)

- public string Raw;


## WechatOpenOAuthApi (class)

OAuthAPIs/OAuthAPI.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenOAuthApi(WechatOpenClient client)

- async WechatOpenOAuthApiGetAccessTokenResponse GetAccessTokenAsync(WechatOpenOAuthApiGetAccessTokenRequest request)
  - GET /sns/oauth2/component/access_token

- async WechatResponse GetAccessTokenRawAsync(string query)

- async WechatOpenOAuthApiRefreshTokenResponse RefreshTokenAsync(WechatOpenOAuthApiRefreshTokenRequest request)
  - GET /sns/oauth2/component/refresh_token

- async WechatResponse RefreshTokenRawAsync(string query)

- async WechatOpenOAuthApiGetUserInfoResponse GetUserInfoAsync(WechatOpenOAuthApiGetUserInfoRequest request)
  - GET /sns/userinfo

- async WechatResponse GetUserInfoRawAsync(string query)


## WechatOpenOAuthApiGetAccessTokenRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenOAuthApiGetAccessTokenRequest()

- WechatOpenOAuthApiGetAccessTokenRequest AppId(string fieldValue)

- WechatOpenOAuthApiGetAccessTokenRequest ComponentAppid(string fieldValue)

- WechatOpenOAuthApiGetAccessTokenRequest Code(string fieldValue)

- WechatOpenOAuthApiGetAccessTokenRequest GrantType(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenOAuthApiGetAccessTokenResponse (class)

- public string Raw;


## WechatOpenOAuthApiGetUserInfoRequest (class)

- WechatTypedRequest request;

- public WechatOpenOAuthApiGetUserInfoRequest()

- WechatOpenOAuthApiGetUserInfoRequest OpenId(string fieldValue)

- WechatOpenOAuthApiGetUserInfoRequest Lang(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenOAuthApiGetUserInfoResponse (class)

- public string Raw;


## WechatOpenOAuthApiRefreshTokenRequest (class)

- WechatTypedRequest request;

- public WechatOpenOAuthApiRefreshTokenRequest()

- WechatOpenOAuthApiRefreshTokenRequest AppId(string fieldValue)

- WechatOpenOAuthApiRefreshTokenRequest RefreshToken(string fieldValue)

- WechatOpenOAuthApiRefreshTokenRequest ComponentAppid(string fieldValue)

- WechatOpenOAuthApiRefreshTokenRequest GrantType(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenOAuthApiRefreshTokenResponse (class)

- public string Raw;


## WechatOpenOpenApi (class)

MpAPIs/Open/OpenApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenOpenApi(WechatOpenClient client)

- async WechatOpenOpenApiCreateResponse CreateAsync(WechatOpenOpenApiCreateRequest request)
  - POST /cgi-bin/open/create

- async WechatResponse CreateRawAsync(string query, string jsonBody)

- async WechatOpenOpenApiBindResponse BindAsync(WechatOpenOpenApiBindRequest request)
  - POST /cgi-bin/open/bind

- async WechatResponse BindRawAsync(string query, string jsonBody)

- async WechatOpenOpenApiUnbindResponse UnbindAsync(WechatOpenOpenApiUnbindRequest request)
  - POST /cgi-bin/open/unbind

- async WechatResponse UnbindRawAsync(string query, string jsonBody)

- async WechatOpenOpenApiGetResponse GetAsync(WechatOpenOpenApiGetRequest request)
  - POST /cgi-bin/open/get

- async WechatResponse GetRawAsync(string query, string jsonBody)


## WechatOpenOpenApiBindRequest (class)

- WechatTypedRequest request;

- public WechatOpenOpenApiBindRequest()

- WechatOpenOpenApiBindRequest AppId(string fieldValue)

- WechatOpenOpenApiBindRequest OpenAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenOpenApiBindResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenOpenApiCreateRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenOpenApiCreateRequest()

- WechatOpenOpenApiCreateRequest AppId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenOpenApiCreateResponse (class)

- public string Raw;


## WechatOpenOpenApiGetRequest (class)

- WechatTypedRequest request;

- public WechatOpenOpenApiGetRequest()

- WechatOpenOpenApiGetRequest AppId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenOpenApiGetResponse (class)

- public string Raw;


## WechatOpenOpenApiUnbindRequest (class)

- WechatTypedRequest request;

- public WechatOpenOpenApiUnbindRequest()

- WechatOpenOpenApiUnbindRequest AppId(string fieldValue)

- WechatOpenOpenApiUnbindRequest OpenAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenOpenApiUnbindResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenP1Api (class)

WxaAPIs/P1/P1Api.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenP1Api(WechatOpenClient client)

- async WechatOpenP1ApiSetPreFetchDataSettingResponse SetPreFetchDataSettingAsync(WechatOpenP1ApiSetPreFetchDataSettingRequest request)
  - POST /wxa/fetchdatasetting

- async WechatResponse SetPreFetchDataSettingRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiSetPeriodFetchDataSettingResponse SetPeriodFetchDataSettingAsync(WechatOpenP1ApiSetPeriodFetchDataSettingRequest request)
  - POST /wxa/fetchdatasetting

- async WechatResponse SetPeriodFetchDataSettingRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiGetBindOpenAccountResponse GetBindOpenAccountAsync()
  - GET /cgi-bin/open/have

- async WechatResponse GetBindOpenAccountRawAsync(string query)

- async WechatOpenP1ApiGetBindOpenAccountEntityResponse GetBindOpenAccountEntityAsync()
  - GET /cgi-bin/open/sameentity

- async WechatResponse GetBindOpenAccountEntityRawAsync(string query)

- async WechatOpenP1ApiGetSettingCategoriesResponse GetSettingCategoriesAsync()
  - GET /cgi-bin/wxopen/getcategory

- async WechatResponse GetSettingCategoriesRawAsync(string query)

- async WechatOpenP1ApiGetCategoriesByTypeResponse GetCategoriesByTypeAsync(WechatOpenP1ApiGetCategoriesByTypeRequest request)
  - POST /cgi-bin/wxopen/getcategoriesbytype

- async WechatResponse GetCategoriesByTypeRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiGetCategoryNamesResponse GetCategoryNamesAsync()
  - GET /wxa/get_category

- async WechatResponse GetCategoryNamesRawAsync(string query)

- async WechatOpenP1ApiGetVisitStatusResponse GetVisitStatusAsync()
  - POST /wxa/getvisitstatus

- async WechatResponse GetVisitStatusRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiGetCodePrivacyInfoResponse GetCodePrivacyInfoAsync()
  - GET /wxa/security/get_code_privacy_info

- async WechatResponse GetCodePrivacyInfoRawAsync(string query)

- async WechatOpenP1ApiSubmitAuthAndIcpResponse SubmitAuthAndIcpAsync(WechatOpenP1ApiSubmitAuthAndIcpRequest request)
  - POST /wxa/sec/submit_auth_and_icp

- async WechatResponse SubmitAuthAndIcpRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiQueryAuthAndIcpResponse QueryAuthAndIcpAsync(WechatOpenP1ApiQueryAuthAndIcpRequest request)
  - POST /wxa/sec/query_auth_and_icp

- async WechatResponse QueryAuthAndIcpRawAsync(string query, string jsonBody)


## WechatOpenP1ApiGetBindOpenAccountEntityResponse (class)

- public string Raw;


## WechatOpenP1ApiGetBindOpenAccountResponse (class)

- public string Raw;


## WechatOpenP1ApiGetCategoriesByTypeRequest (class)

- WechatTypedRequest request;

- public WechatOpenP1ApiGetCategoriesByTypeRequest()

- WechatOpenP1ApiGetCategoriesByTypeRequest VerifyType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenP1ApiGetCategoriesByTypeResponse (class)

- public string Raw;


## WechatOpenP1ApiGetCategoryNamesResponse (class)

- public string Raw;


## WechatOpenP1ApiGetCodePrivacyInfoResponse (class)

- public string Raw;


## WechatOpenP1ApiGetSettingCategoriesResponse (class)

- public string Raw;


## WechatOpenP1ApiGetVisitStatusResponse (class)

- public string Raw;


## WechatOpenP1ApiQueryAuthAndIcpRequest (class)

- WechatTypedRequest request;

- public WechatOpenP1ApiQueryAuthAndIcpRequest()

- WechatOpenP1ApiQueryAuthAndIcpRequest ProcedureId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenP1ApiQueryAuthAndIcpResponse (class)

- public string Raw;


## WechatOpenP1ApiSetPeriodFetchDataSettingRequest (class)

- WechatTypedRequest request;

- public WechatOpenP1ApiSetPeriodFetchDataSettingRequest()

- WechatOpenP1ApiSetPeriodFetchDataSettingRequest IsOpen(bool fieldValue)

- WechatOpenP1ApiSetPeriodFetchDataSettingRequest FetchType(int fieldValue)

- WechatOpenP1ApiSetPeriodFetchDataSettingRequest FetchUrl(string fieldValue)

- WechatOpenP1ApiSetPeriodFetchDataSettingRequest EnvironmentId(string fieldValue)

- WechatOpenP1ApiSetPeriodFetchDataSettingRequest FunctionName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenP1ApiSetPeriodFetchDataSettingResponse (class)

- public string Raw;


## WechatOpenP1ApiSetPreFetchDataSettingRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenP1ApiSetPreFetchDataSettingRequest()

- WechatOpenP1ApiSetPreFetchDataSettingRequest IsOpen(bool fieldValue)

- WechatOpenP1ApiSetPreFetchDataSettingRequest FetchType(int fieldValue)

- WechatOpenP1ApiSetPreFetchDataSettingRequest FetchUrl(string fieldValue)

- WechatOpenP1ApiSetPreFetchDataSettingRequest EnvironmentId(string fieldValue)

- WechatOpenP1ApiSetPreFetchDataSettingRequest FunctionName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenP1ApiSetPreFetchDataSettingResponse (class)

- public string Raw;


## WechatOpenP1ApiSubmitAuthAndIcpRequest (class)

- WechatTypedRequest request;

- public WechatOpenP1ApiSubmitAuthAndIcpRequest()

- WechatOpenP1ApiSubmitAuthAndIcpRequest CustomerType(int fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest Taskid(string fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest ContactInfo(WechatOpenWxaAuthAuthDataContactInfo fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest InvoiceInfo(WechatOpenWxaAuthAuthDataInvoiceInfo fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest Qualification(string fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest QualificationOther(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest AccountName(string fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest AccountNameType(int fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest AccountSupplemental(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest PayType(int fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest AuthIdentification(string fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest AuthIdentMaterial(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest ThirdPartyPhone(string fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest ServiceAppid(string fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest BaseInfo(WechatOpenBaseInfoModel fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest PersonalInfo(WechatOpenPersonalInfoModel fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest OrganizeInfo(WechatOpenOrganizeInfoModel fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest PrincipalInfo(WechatOpenPrincipalInfoModel fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest LegalPersonInfo(WechatOpenLegalPersonInfoModel fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest CommitmentLetter(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest BusinessNameChangeLetter(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest PartyBuildingConfirmationLetter(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest PromiseVideo(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest AuthenticityResponsibilityLetter(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest AuthenticityCommitmentLetter(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest WebsiteConstructionProposal(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest SubjectOtherMaterials(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest AppletsOtherMaterials(List<string> fieldValue)

- WechatOpenP1ApiSubmitAuthAndIcpRequest HoldingCertificatePhoto(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenP1ApiSubmitAuthAndIcpResponse (class)

- public string Raw;


## WechatOpenQRConnectApi (class)

QRConnect/QRConnectAPI.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenQRConnectApi(WechatOpenClient client)

- async WechatOpenQRConnectApiGetAccessTokenResponse GetAccessTokenAsync(WechatOpenQRConnectApiGetAccessTokenRequest request)
  - GET /sns/oauth2/access_token

- async WechatResponse GetAccessTokenRawAsync(string query)

- async WechatOpenQRConnectApiRefreshTokenResponse RefreshTokenAsync(WechatOpenQRConnectApiRefreshTokenRequest request)
  - GET /sns/oauth2/refresh_token

- async WechatResponse RefreshTokenRawAsync(string query)

- async WechatOpenQRConnectApiGetUserInfoResponse GetUserInfoAsync(WechatOpenQRConnectApiGetUserInfoRequest request)
  - GET /sns/userinfo

- async WechatResponse GetUserInfoRawAsync(string query)

- async WechatOpenQRConnectApiAuthResponse AuthAsync(WechatOpenQRConnectApiAuthRequest request)
  - GET /sns/auth

- async WechatResponse AuthRawAsync(string query)


## WechatOpenQRConnectApiAuthRequest (class)

- WechatTypedRequest request;

- public WechatOpenQRConnectApiAuthRequest()

- WechatOpenQRConnectApiAuthRequest OpenId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenQRConnectApiAuthResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenQRConnectApiGetAccessTokenRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenQRConnectApiGetAccessTokenRequest()

- WechatOpenQRConnectApiGetAccessTokenRequest AppId(string fieldValue)

- WechatOpenQRConnectApiGetAccessTokenRequest AppSecret(string fieldValue)

- WechatOpenQRConnectApiGetAccessTokenRequest Code(string fieldValue)

- WechatOpenQRConnectApiGetAccessTokenRequest GrantType(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenQRConnectApiGetAccessTokenResponse (class)

- public string Raw;


## WechatOpenQRConnectApiGetUserInfoRequest (class)

- WechatTypedRequest request;

- public WechatOpenQRConnectApiGetUserInfoRequest()

- WechatOpenQRConnectApiGetUserInfoRequest OpenId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenQRConnectApiGetUserInfoResponse (class)

- public string Raw;


## WechatOpenQRConnectApiRefreshTokenRequest (class)

- WechatTypedRequest request;

- public WechatOpenQRConnectApiRefreshTokenRequest()

- WechatOpenQRConnectApiRefreshTokenRequest AppId(string fieldValue)

- WechatOpenQRConnectApiRefreshTokenRequest RefreshToken(string fieldValue)

- WechatOpenQRConnectApiRefreshTokenRequest GrantType(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenQRConnectApiRefreshTokenResponse (class)

- public string Raw;


## WechatOpenSearchStatusApi (class)

WxaAPIs/SearchStatus/SearchStatusApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenSearchStatusApi(WechatOpenClient client)

- async WechatOpenSearchStatusApiGetWxaSearchStatusResponse GetWxaSearchStatusAsync()
  - GET /wxa/getwxasearchstatus

- async WechatResponse GetWxaSearchStatusRawAsync(string query)

- async WechatOpenSearchStatusApiChangeWxaSearchStatusResponse ChangeWxaSearchStatusAsync(WechatOpenSearchStatusApiChangeWxaSearchStatusRequest request)
  - POST /wxa/changewxasearchstatus

- async WechatResponse ChangeWxaSearchStatusRawAsync(string query, string jsonBody)


## WechatOpenSearchStatusApiChangeWxaSearchStatusRequest (class)

- WechatTypedRequest request;

- public WechatOpenSearchStatusApiChangeWxaSearchStatusRequest()

- WechatOpenSearchStatusApiChangeWxaSearchStatusRequest Status(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSearchStatusApiChangeWxaSearchStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenSearchStatusApiGetWxaSearchStatusResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- public string Raw;


## WechatOpenSecApi (class)

WxaAPIs/Sec/SecApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenSecApi(WechatOpenClient client)

- async WechatOpenSecApiWxaAuthResponse WxaAuthAsync(WechatOpenSecApiWxaAuthRequest request)
  - POST /wxa/sec/wxaauth

- async WechatResponse WxaAuthRawAsync(string query, string jsonBody)

- async WechatOpenSecApiQueryAuthResponse QueryAuthAsync(WechatOpenSecApiQueryAuthRequest request)
  - POST /wxa/sec/queryauth

- async WechatResponse QueryAuthRawAsync(string query, string jsonBody)

- async WechatOpenSecApiReauthResponse ReauthAsync(WechatOpenSecApiReauthRequest request)
  - POST /wxa/sec/reauth

- async WechatResponse ReauthRawAsync(string query, string jsonBody)

- async WechatOpenSecApiAuthIdentityTreeResponse AuthIdentityTreeAsync()
  - POST /wxa/sec/authidentitytree

- async WechatResponse AuthIdentityTreeRawAsync(string query, string jsonBody)


## WechatOpenSecApiAuthIdentityTreeResponse (class)

- public string Raw;


## WechatOpenSecApiQueryAuthRequest (class)

- WechatTypedRequest request;

- public WechatOpenSecApiQueryAuthRequest()

- WechatOpenSecApiQueryAuthRequest Taskid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecApiQueryAuthResponse (class)

- public string Raw;


## WechatOpenSecApiReauthRequest (class)

- WechatTypedRequest request;

- public WechatOpenSecApiReauthRequest()

- WechatOpenSecApiReauthRequest CustomerType(int fieldValue)

- WechatOpenSecApiReauthRequest Taskid(string fieldValue)

- WechatOpenSecApiReauthRequest ContactInfo(WechatOpenWxaAuthAuthDataContactInfo fieldValue)

- WechatOpenSecApiReauthRequest InvoiceInfo(WechatOpenWxaAuthAuthDataInvoiceInfo fieldValue)

- WechatOpenSecApiReauthRequest Qualification(string fieldValue)

- WechatOpenSecApiReauthRequest QualificationOther(List<string> fieldValue)

- WechatOpenSecApiReauthRequest AccountName(string fieldValue)

- WechatOpenSecApiReauthRequest AccountNameType(int fieldValue)

- WechatOpenSecApiReauthRequest AccountSupplemental(List<string> fieldValue)

- WechatOpenSecApiReauthRequest PayType(int fieldValue)

- WechatOpenSecApiReauthRequest AuthIdentification(string fieldValue)

- WechatOpenSecApiReauthRequest AuthIdentMaterial(List<string> fieldValue)

- WechatOpenSecApiReauthRequest ThirdPartyPhone(string fieldValue)

- WechatOpenSecApiReauthRequest ServiceAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecApiReauthResponse (class)

- public string Raw;


## WechatOpenSecApiWxaAuthRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenSecApiWxaAuthRequest()

- WechatOpenSecApiWxaAuthRequest CustomerType(int fieldValue)

- WechatOpenSecApiWxaAuthRequest Taskid(string fieldValue)

- WechatOpenSecApiWxaAuthRequest ContactInfo(WechatOpenWxaAuthAuthDataContactInfo fieldValue)

- WechatOpenSecApiWxaAuthRequest InvoiceInfo(WechatOpenWxaAuthAuthDataInvoiceInfo fieldValue)

- WechatOpenSecApiWxaAuthRequest Qualification(string fieldValue)

- WechatOpenSecApiWxaAuthRequest QualificationOther(List<string> fieldValue)

- WechatOpenSecApiWxaAuthRequest AccountName(string fieldValue)

- WechatOpenSecApiWxaAuthRequest AccountNameType(int fieldValue)

- WechatOpenSecApiWxaAuthRequest AccountSupplemental(List<string> fieldValue)

- WechatOpenSecApiWxaAuthRequest PayType(int fieldValue)

- WechatOpenSecApiWxaAuthRequest AuthIdentification(string fieldValue)

- WechatOpenSecApiWxaAuthRequest AuthIdentMaterial(List<string> fieldValue)

- WechatOpenSecApiWxaAuthRequest ThirdPartyPhone(string fieldValue)

- WechatOpenSecApiWxaAuthRequest ServiceAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecApiWxaAuthResponse (class)

- public string Raw;


## WechatOpenSecOrderApi (class)

WxaAPIs/SecOrder/SecOrderApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenSecOrderApi(WechatOpenClient client)

- async WechatOpenSecOrderApiUploadShippingInfoResponse UploadShippingInfoAsync(WechatOpenSecOrderApiUploadShippingInfoRequest request)
  - POST /wxa/sec/order/upload_shipping_info

- async WechatResponse UploadShippingInfoRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiUploadCombinedShippingInfoResponse UploadCombinedShippingInfoAsync(WechatOpenSecOrderApiUploadCombinedShippingInfoRequest request)
  - POST /wxa/sec/order/upload_combined_shipping_info

- async WechatResponse UploadCombinedShippingInfoRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiGetOrderResponse GetOrderAsync(WechatOpenSecOrderApiGetOrderRequest request)
  - POST /wxa/sec/order/get_order

- async WechatResponse GetOrderRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiGetOrderListResponse GetOrderListAsync(WechatOpenSecOrderApiGetOrderListRequest request)
  - POST /wxa/sec/order/get_order_list

- async WechatResponse GetOrderListRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiNotifyConfirmReceiveResponse NotifyConfirmReceiveAsync(WechatOpenSecOrderApiNotifyConfirmReceiveRequest request)
  - POST /wxa/sec/order/notify_confirm_receive

- async WechatResponse NotifyConfirmReceiveRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiSetMsgJumpPathResponse SetMsgJumpPathAsync(WechatOpenSecOrderApiSetMsgJumpPathRequest request)
  - POST /wxa/sec/order/set_msg_jump_path

- async WechatResponse SetMsgJumpPathRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiIsTradeManagedResponse IsTradeManagedAsync(WechatOpenSecOrderApiIsTradeManagedRequest request)
  - POST /wxa/sec/order/is_trade_managed

- async WechatResponse IsTradeManagedRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiIsTradeManagementConfirmationCompletedResponse IsTradeManagementConfirmationCompletedAsync(WechatOpenSecOrderApiIsTradeManagementConfirmationCompletedRequest request)
  - POST /wxa/sec/order/is_trade_management_confirmation_completed

- async WechatResponse IsTradeManagementConfirmationCompletedRawAsync(string query, string jsonBody)


## WechatOpenSecOrderApiGetOrderListRequest (class)

- WechatTypedRequest request;

- public WechatOpenSecOrderApiGetOrderListRequest()

- WechatOpenSecOrderApiGetOrderListRequest BeginTime(long fieldValue)

- WechatOpenSecOrderApiGetOrderListRequest EndTime(long fieldValue)

- WechatOpenSecOrderApiGetOrderListRequest OrderState(int fieldValue)

- WechatOpenSecOrderApiGetOrderListRequest Openid(string fieldValue)

- WechatOpenSecOrderApiGetOrderListRequest LastIndex(string fieldValue)

- WechatOpenSecOrderApiGetOrderListRequest PageSize(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecOrderApiGetOrderListResponse (class)

- public string Raw;


## WechatOpenSecOrderApiGetOrderRequest (class)

- WechatTypedRequest request;

- public WechatOpenSecOrderApiGetOrderRequest()

- WechatOpenSecOrderApiGetOrderRequest TransactionId(string fieldValue)

- WechatOpenSecOrderApiGetOrderRequest MerchantId(string fieldValue)

- WechatOpenSecOrderApiGetOrderRequest SubMerchantId(string fieldValue)

- WechatOpenSecOrderApiGetOrderRequest MerchantTradeNo(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecOrderApiGetOrderResponse (class)

- public string Raw;


## WechatOpenSecOrderApiIsTradeManagedRequest (class)

- WechatTypedRequest request;

- public WechatOpenSecOrderApiIsTradeManagedRequest()

- WechatOpenSecOrderApiIsTradeManagedRequest Appid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecOrderApiIsTradeManagedResponse (class)

- public string Raw;


## WechatOpenSecOrderApiIsTradeManagementConfirmationCompletedRequest (class)

- WechatTypedRequest request;

- public WechatOpenSecOrderApiIsTradeManagementConfirmationCompletedRequest()

- WechatOpenSecOrderApiIsTradeManagementConfirmationCompletedRequest Appid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecOrderApiIsTradeManagementConfirmationCompletedResponse (class)

- public string Raw;


## WechatOpenSecOrderApiNotifyConfirmReceiveRequest (class)

- WechatTypedRequest request;

- public WechatOpenSecOrderApiNotifyConfirmReceiveRequest()

- WechatOpenSecOrderApiNotifyConfirmReceiveRequest TransactionId(string fieldValue)

- WechatOpenSecOrderApiNotifyConfirmReceiveRequest MerchantId(string fieldValue)

- WechatOpenSecOrderApiNotifyConfirmReceiveRequest SubMerchantId(string fieldValue)

- WechatOpenSecOrderApiNotifyConfirmReceiveRequest MerchantTradeNo(string fieldValue)

- WechatOpenSecOrderApiNotifyConfirmReceiveRequest ReceivedTime(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecOrderApiNotifyConfirmReceiveResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenSecOrderApiSetMsgJumpPathRequest (class)

- WechatTypedRequest request;

- public WechatOpenSecOrderApiSetMsgJumpPathRequest()

- WechatOpenSecOrderApiSetMsgJumpPathRequest Path(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecOrderApiSetMsgJumpPathResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenSecOrderApiUploadCombinedShippingInfoRequest (class)

- WechatTypedRequest request;

- public WechatOpenSecOrderApiUploadCombinedShippingInfoRequest()

- WechatOpenSecOrderApiUploadCombinedShippingInfoRequest OrderKey(WechatOpenOrderKey fieldValue)

- WechatOpenSecOrderApiUploadCombinedShippingInfoRequest SubOrders(List<WechatOpenUploadCombinedShippingInfoSubOrder> fieldValue)

- WechatOpenSecOrderApiUploadCombinedShippingInfoRequest UploadTime(string fieldValue)

- WechatOpenSecOrderApiUploadCombinedShippingInfoRequest Payer(WechatOpenPayer fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecOrderApiUploadCombinedShippingInfoResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenSecOrderApiUploadShippingInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenSecOrderApiUploadShippingInfoRequest()

- WechatOpenSecOrderApiUploadShippingInfoRequest OrderKey(WechatOpenOrderKey fieldValue)

- WechatOpenSecOrderApiUploadShippingInfoRequest LogisticsType(int fieldValue)

- WechatOpenSecOrderApiUploadShippingInfoRequest DeliveryMode(int fieldValue)

- WechatOpenSecOrderApiUploadShippingInfoRequest IsAllDelivered(bool fieldValue)

- WechatOpenSecOrderApiUploadShippingInfoRequest ShippingList(List<WechatOpenShipping> fieldValue)

- WechatOpenSecOrderApiUploadShippingInfoRequest UploadTime(string fieldValue)

- WechatOpenSecOrderApiUploadShippingInfoRequest Payer(WechatOpenPayer fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSecOrderApiUploadShippingInfoResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenSetWebViewDomainApi (class)

WxaAPIs/SetWebViewDomain/SetWebViewDomainApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenSetWebViewDomainApi(WechatOpenClient client)

- async WechatOpenSetWebViewDomainApiSetWebViewDomainResponse SetWebViewDomainAsync(WechatOpenSetWebViewDomainApiSetWebViewDomainRequest request)
  - POST /wxa/setwebviewdomain

- async WechatResponse SetWebViewDomainRawAsync(string query, string jsonBody)


## WechatOpenSetWebViewDomainApiSetWebViewDomainRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenSetWebViewDomainApiSetWebViewDomainRequest()

- WechatOpenSetWebViewDomainApiSetWebViewDomainRequest Action(int fieldValue)

- WechatOpenSetWebViewDomainApiSetWebViewDomainRequest Webviewdomain(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSetWebViewDomainApiSetWebViewDomainResponse (class)

- public string Raw;


## WechatOpenSnsApi (class)

WxaAPIs/Sns/SnsApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenSnsApi(WechatOpenClient client)

- async WechatOpenSnsApiJsCode2JsonResponse JsCode2JsonAsync(WechatOpenSnsApiJsCode2JsonRequest request)
  - GET /sns/component/jscode2session

- async WechatResponse JsCode2JsonRawAsync(string query)


## WechatOpenSnsApiJsCode2JsonRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenSnsApiJsCode2JsonRequest()

- WechatOpenSnsApiJsCode2JsonRequest AppId(string fieldValue)

- WechatOpenSnsApiJsCode2JsonRequest ComponentAppId(string fieldValue)

- WechatOpenSnsApiJsCode2JsonRequest JsCode(string fieldValue)

- WechatOpenSnsApiJsCode2JsonRequest GrantType(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSnsApiJsCode2JsonResponse (class)

- public string Raw;


## WechatOpenSpecialApi (class)

开放平台文件上传、媒体下载和兼容模板接口。

- WechatOpenClient client;

- public WechatOpenSpecialApi(WechatOpenClient client)

- async WechatResponse UploadPrivacyExtFileAsync(string fileName, string contentType, string fileBytes)

- async WechatRawResponse GetQRCodeAsync(WechatOpenSpecialQrCodeRequest request)

- async WechatRawResponse GetQRCodeRawAsync(string query)

- async WechatResponse UploadMediaAsync(string fileName, string contentType, string fileBytes)

- async WechatResponse UploadIcpMediaAsync(string type, string icpOrderField, int certificateType, string fileName, string contentType, string fileBytes)

- async WechatRawResponse GetIcpMediaAsync(string mediaId)

- async WechatResponse UploadAuthMaterialAsync(string fileName, string contentType, string fileBytes)

- async WechatResponse UploadWxaMediaAsync(string fileName, string contentType, string fileBytes)

- async WechatResponse UploadAsync(string path, string fieldName, string fileName, string contentType, string fileBytes, WechatCredentialKind credential)

- async WechatOpenSpecialLibraryListResponse LibraryListAsync(WechatOpenSpecialLibraryListRequest request)

- async WechatOpenSpecialLibraryGetResponse LibraryGetAsync(WechatOpenSpecialLibraryGetRequest request)

- async WechatOpenSpecialTemplateAddResponse AddAsync(WechatOpenSpecialTemplateAddRequest request)

- async WechatOpenSpecialTemplateListResponse ListAsync(WechatOpenSpecialTemplateListRequest request)

- async WechatOpenSpecialOperationResponse DelAsync(WechatOpenSpecialTemplateDeleteRequest request)

- async WechatResponse LibraryListRawAsync(string query, string jsonBody)

- async WechatResponse LibraryGetRawAsync(string query, string jsonBody)

- async WechatResponse AddRawAsync(string query, string jsonBody)

- async WechatResponse ListRawAsync(string query, string jsonBody)

- async WechatResponse DelRawAsync(string query, string jsonBody)


## WechatOpenSpecialLibraryGetRequest (class)

- WechatTypedRequest request;

- public WechatOpenSpecialLibraryGetRequest()

- WechatOpenSpecialLibraryGetRequest Id(string fieldValue)

- string JsonBody()


## WechatOpenSpecialLibraryGetResponse (class)

- public string Raw;

- public string id;

- public string title;

- public JsonValue keyword_list;


## WechatOpenSpecialLibraryListRequest (class)

- WechatTypedRequest request;

- public WechatOpenSpecialLibraryListRequest()

- WechatOpenSpecialLibraryListRequest Offset(int fieldValue)

- WechatOpenSpecialLibraryListRequest Count(int fieldValue)

- string JsonBody()


## WechatOpenSpecialLibraryListResponse (class)

- public string Raw;

- public int total_count;

- public JsonValue list;


## WechatOpenSpecialOperationResponse (class)

- public string Raw;


## WechatOpenSpecialQrCodeRequest (class)

- WechatTypedRequest request;

- public WechatOpenSpecialQrCodeRequest()

- WechatOpenSpecialQrCodeRequest Path(string fieldValue)

- string QueryText()


## WechatOpenSpecialTemplateAddRequest (class)

- WechatTypedRequest request;

- public WechatOpenSpecialTemplateAddRequest()

- WechatOpenSpecialTemplateAddRequest Id(string fieldValue)

- WechatOpenSpecialTemplateAddRequest KeywordIdList(List<int> fieldValue)

- string JsonBody()


## WechatOpenSpecialTemplateAddResponse (class)

- public string Raw;

- public string template_id;


## WechatOpenSpecialTemplateDeleteRequest (class)

- WechatTypedRequest request;

- public WechatOpenSpecialTemplateDeleteRequest()

- WechatOpenSpecialTemplateDeleteRequest TemplateId(string fieldValue)

- string JsonBody()


## WechatOpenSpecialTemplateListRequest (class)

- WechatTypedRequest request;

- public WechatOpenSpecialTemplateListRequest()

- WechatOpenSpecialTemplateListRequest Offset(int fieldValue)

- WechatOpenSpecialTemplateListRequest Count(int fieldValue)

- string JsonBody()


## WechatOpenSpecialTemplateListResponse (class)

- public string Raw;

- public int total_count;

- public JsonValue list;


## WechatOpenWxOpenApi (class)

WxOpenAPIs/WxOpenApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenWxOpenApi(WechatOpenClient client)

- async WechatOpenWxOpenApiGetAllCategoriesResponse GetAllCategoriesAsync()
  - GET /cgi-bin/wxopen/getallcategories

- async WechatResponse GetAllCategoriesRawAsync(string query)

- async WechatOpenWxOpenApiAddCategoryResponse AddCategoryAsync(WechatOpenWxOpenApiAddCategoryRequest request)
  - POST /cgi-bin/wxopen/addcategory

- async WechatResponse AddCategoryRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenApiDeleteCategoryResponse DeleteCategoryAsync(WechatOpenWxOpenApiDeleteCategoryRequest request)
  - POST /cgi-bin/wxopen/deletecategory

- async WechatResponse DeleteCategoryRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenApiGetCategoryResponse GetCategoryAsync()
  - GET /cgi-bin/wxopen/getcategory

- async WechatResponse GetCategoryRawAsync(string query)

- async WechatOpenWxOpenApiModifyCategoryResponse ModifyCategoryAsync(WechatOpenWxOpenApiModifyCategoryRequest request)
  - POST /cgi-bin/wxopen/modifycategory

- async WechatResponse ModifyCategoryRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenApiWxaMpLinkGetResponse WxaMpLinkGetAsync()
  - POST /cgi-bin/wxopen/wxamplinkget

- async WechatResponse WxaMpLinkGetRawAsync(string query, string jsonBody)


## WechatOpenWxOpenApiAddCategoryRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxOpenApiAddCategoryRequest()

- WechatOpenWxOpenApiAddCategoryRequest AddCategoryData(List<WechatOpenAddCategoryData> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxOpenApiAddCategoryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxOpenApiDeleteCategoryRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxOpenApiDeleteCategoryRequest()

- WechatOpenWxOpenApiDeleteCategoryRequest First(int fieldValue)

- WechatOpenWxOpenApiDeleteCategoryRequest Second(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxOpenApiDeleteCategoryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxOpenApiGetAllCategoriesResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- public string Raw;


## WechatOpenWxOpenApiGetCategoryResponse (class)

- public string Raw;


## WechatOpenWxOpenApiModifyCategoryRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxOpenApiModifyCategoryRequest()

- WechatOpenWxOpenApiModifyCategoryRequest First(int fieldValue)

- WechatOpenWxOpenApiModifyCategoryRequest Second(int fieldValue)

- WechatOpenWxOpenApiModifyCategoryRequest Certicates(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxOpenApiModifyCategoryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxOpenApiWxaMpLinkGetResponse (class)

- public string Raw;


## WechatOpenWxOpenManagedOfficialAccountApi (class)

WxOpenAPIs/ManagedOfficialAccountApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenWxOpenManagedOfficialAccountApi(WechatOpenClient client)

- async WechatOpenWxOpenManagedOfficialAccountApiGetLinkMiniprogramResponse GetLinkMiniprogramAsync()
  - POST /cgi-bin/wxopen/wxamplinkget

- async WechatResponse GetLinkMiniprogramRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramResponse LinkMiniprogramAsync(WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramRequest request)
  - POST /cgi-bin/wxopen/wxamplink

- async WechatResponse LinkMiniprogramRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenManagedOfficialAccountApiUnlinkMiniprogramResponse UnlinkMiniprogramAsync(WechatOpenWxOpenManagedOfficialAccountApiUnlinkMiniprogramRequest request)
  - POST /cgi-bin/wxopen/wxampunlink

- async WechatResponse UnlinkMiniprogramRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenManagedOfficialAccountApiGetResponse GetAsync(WechatOpenWxOpenManagedOfficialAccountApiGetRequest request)
  - POST /cgi-bin/wxopen/qrcodejumpget

- async WechatResponse GetRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateResponse AddOrUpdateAsync(WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest request)
  - POST /cgi-bin/wxopen/qrcodejumpadd

- async WechatResponse AddOrUpdateRawAsync(string query, string jsonBody)


## WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest()

- WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest Prefix(string fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest AppId(string fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest Path(string fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest IsEdit(bool fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest OpenVersion(int fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest DebugUrls(List<string> fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest PermitSubRule(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxOpenManagedOfficialAccountApiGetLinkMiniprogramResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- public string Raw;


## WechatOpenWxOpenManagedOfficialAccountApiGetRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxOpenManagedOfficialAccountApiGetRequest()

- WechatOpenWxOpenManagedOfficialAccountApiGetRequest AppId(string fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiGetRequest GetType(int fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiGetRequest PrefixList(List<string> fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiGetRequest PageNumber(int fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiGetRequest PageSize(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxOpenManagedOfficialAccountApiGetResponse (class)

- public string Raw;


## WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramRequest()

- WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramRequest AppId(string fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramRequest NotifyUsers(bool fieldValue)

- WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramRequest ShowProfile(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxOpenManagedOfficialAccountApiUnlinkMiniprogramRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxOpenManagedOfficialAccountApiUnlinkMiniprogramRequest()

- WechatOpenWxOpenManagedOfficialAccountApiUnlinkMiniprogramRequest AppId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxOpenManagedOfficialAccountApiUnlinkMiniprogramResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxaApi (class)

WxaAPIs/WxaApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenWxaApi(WechatOpenClient client)

- async WechatOpenWxaApiGetShowWxaItemResponse GetShowWxaItemAsync()
  - POST /wxa/getshowwxaitem

- async WechatResponse GetShowWxaItemRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetWxaMpLinkForShowResponse GetWxaMpLinkForShowAsync(WechatOpenWxaApiGetWxaMpLinkForShowRequest request)
  - GET /wxa/getwxamplinkforshow

- async WechatResponse GetWxaMpLinkForShowRawAsync(string query)

- async WechatOpenWxaApiUpdateShowWxaItemResponse UpdateShowWxaItemAsync(WechatOpenWxaApiUpdateShowWxaItemRequest request)
  - POST /wxa/updateshowwxaitem

- async WechatResponse UpdateShowWxaItemRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetIllegalRecordsResponse GetIllegalRecordsAsync(WechatOpenWxaApiGetIllegalRecordsRequest request)
  - POST /wxa/getillegalrecords

- async WechatResponse GetIllegalRecordsRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetAppealRecordsResponse GetAppealRecordsAsync(WechatOpenWxaApiGetAppealRecordsRequest request)
  - POST /wxa/getappealrecords

- async WechatResponse GetAppealRecordsRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetPrivacyInterfaceResponse GetPrivacyInterfaceAsync()
  - GET /wxa/security/get_privacy_interface

- async WechatResponse GetPrivacyInterfaceRawAsync(string query)

- async WechatOpenWxaApiApplyPrivacyInterfaceResponse ApplyPrivacyInterfaceAsync(WechatOpenWxaApiApplyPrivacyInterfaceRequest request)
  - POST /wxa/security/apply_privacy_interface

- async WechatResponse ApplyPrivacyInterfaceRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetVersionInfoResponse GetVersionInfoAsync()
  - POST /wxa/getversioninfo

- async WechatResponse GetVersionInfoRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetOrderPathInfoResponse GetOrderPathInfoAsync(WechatOpenWxaApiGetOrderPathInfoRequest request)
  - POST /wxa/security/getorderpathinfo

- async WechatResponse GetOrderPathInfoRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiApplySetOrderPathInfoResponse ApplySetOrderPathInfoAsync(WechatOpenWxaApiApplySetOrderPathInfoRequest request)
  - POST /wxa/security/applysetorderpathinfo

- async WechatResponse ApplySetOrderPathInfoRawAsync(string query, string jsonBody)


## WechatOpenWxaApiApplyPrivacyInterfaceRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaApiApplyPrivacyInterfaceRequest()

- WechatOpenWxaApiApplyPrivacyInterfaceRequest ApiName(string fieldValue)

- WechatOpenWxaApiApplyPrivacyInterfaceRequest Content(string fieldValue)

- WechatOpenWxaApiApplyPrivacyInterfaceRequest UrlList(List<string> fieldValue)

- WechatOpenWxaApiApplyPrivacyInterfaceRequest PicList(List<string> fieldValue)

- WechatOpenWxaApiApplyPrivacyInterfaceRequest VideoList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaApiApplyPrivacyInterfaceResponse (class)

- public string Raw;


## WechatOpenWxaApiApplySetOrderPathInfoRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaApiApplySetOrderPathInfoRequest()

- WechatOpenWxaApiApplySetOrderPathInfoRequest Path(string fieldValue)

- WechatOpenWxaApiApplySetOrderPathInfoRequest ImgList(List<string> fieldValue)

- WechatOpenWxaApiApplySetOrderPathInfoRequest Video(string fieldValue)

- WechatOpenWxaApiApplySetOrderPathInfoRequest TestAccount(string fieldValue)

- WechatOpenWxaApiApplySetOrderPathInfoRequest TestPwd(string fieldValue)

- WechatOpenWxaApiApplySetOrderPathInfoRequest TestRemark(string fieldValue)

- WechatOpenWxaApiApplySetOrderPathInfoRequest AppidList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaApiApplySetOrderPathInfoResponse (class)

- public string Raw;


## WechatOpenWxaApiGetAppealRecordsRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaApiGetAppealRecordsRequest()

- WechatOpenWxaApiGetAppealRecordsRequest IllegalRecordId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaApiGetAppealRecordsResponse (class)

- public string Raw;


## WechatOpenWxaApiGetIllegalRecordsRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaApiGetIllegalRecordsRequest()

- WechatOpenWxaApiGetIllegalRecordsRequest StartTime(long fieldValue)

- WechatOpenWxaApiGetIllegalRecordsRequest EndTime(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaApiGetIllegalRecordsResponse (class)

- public string Raw;


## WechatOpenWxaApiGetOrderPathInfoRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaApiGetOrderPathInfoRequest()

- WechatOpenWxaApiGetOrderPathInfoRequest InfoType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaApiGetOrderPathInfoResponse (class)

- public string Raw;


## WechatOpenWxaApiGetPrivacyInterfaceResponse (class)

- public string Raw;


## WechatOpenWxaApiGetShowWxaItemResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- public string Raw;


## WechatOpenWxaApiGetVersionInfoResponse (class)

- public string Raw;


## WechatOpenWxaApiGetWxaMpLinkForShowRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaApiGetWxaMpLinkForShowRequest()

- WechatOpenWxaApiGetWxaMpLinkForShowRequest Page(int fieldValue)

- WechatOpenWxaApiGetWxaMpLinkForShowRequest Num(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaApiGetWxaMpLinkForShowResponse (class)

- public string Raw;


## WechatOpenWxaApiUpdateShowWxaItemRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaApiUpdateShowWxaItemRequest()

- WechatOpenWxaApiUpdateShowWxaItemRequest WxaSubscribeBizFlag(int fieldValue)

- WechatOpenWxaApiUpdateShowWxaItemRequest Appid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaApiUpdateShowWxaItemResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxaEmbeddedApi (class)

WxaAPIs/WxaEmbedded/WxaEmbeddedApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatOpenClient client;

- public WechatOpenWxaEmbeddedApi(WechatOpenClient client)

- async WechatOpenWxaEmbeddedApiAddEmbeddedResponse AddEmbeddedAsync(WechatOpenWxaEmbeddedApiAddEmbeddedRequest request)
  - POST /wxaapi/wxaembedded/add_embedded

- async WechatResponse AddEmbeddedRawAsync(string query, string jsonBody)

- async WechatOpenWxaEmbeddedApiDelEmbeddedResponse DelEmbeddedAsync(WechatOpenWxaEmbeddedApiDelEmbeddedRequest request)
  - POST /wxaapi/wxaembedded/del_embedded

- async WechatResponse DelEmbeddedRawAsync(string query, string jsonBody)

- async WechatOpenWxaEmbeddedApiDelAuthorizeResponse DelAuthorizeAsync(WechatOpenWxaEmbeddedApiDelAuthorizeRequest request)
  - POST /wxaapi/wxaembedded/del_authorize

- async WechatResponse DelAuthorizeRawAsync(string query, string jsonBody)

- async WechatOpenWxaEmbeddedApiGetListResponse GetListAsync(WechatOpenWxaEmbeddedApiGetListRequest request)
  - GET /wxaapi/wxaembedded/get_list

- async WechatResponse GetListRawAsync(string query)

- async WechatOpenWxaEmbeddedApiGetOwnListResponse GetOwnListAsync(WechatOpenWxaEmbeddedApiGetOwnListRequest request)
  - GET /wxaapi/wxaembedded/get_own_list

- async WechatResponse GetOwnListRawAsync(string query)

- async WechatOpenWxaEmbeddedApiSetAuthorizeResponse SetAuthorizeAsync(WechatOpenWxaEmbeddedApiSetAuthorizeRequest request)
  - POST /wxaapi/wxaembedded/set_authorize

- async WechatResponse SetAuthorizeRawAsync(string query, string jsonBody)


## WechatOpenWxaEmbeddedApiAddEmbeddedRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Open。

- WechatTypedRequest request;

- public WechatOpenWxaEmbeddedApiAddEmbeddedRequest()

- WechatOpenWxaEmbeddedApiAddEmbeddedRequest Appid(string fieldValue)

- WechatOpenWxaEmbeddedApiAddEmbeddedRequest ApplyReason(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaEmbeddedApiAddEmbeddedResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxaEmbeddedApiDelAuthorizeRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaEmbeddedApiDelAuthorizeRequest()

- WechatOpenWxaEmbeddedApiDelAuthorizeRequest Flag(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaEmbeddedApiDelAuthorizeResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxaEmbeddedApiDelEmbeddedRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaEmbeddedApiDelEmbeddedRequest()

- WechatOpenWxaEmbeddedApiDelEmbeddedRequest Appid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaEmbeddedApiDelEmbeddedResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatOpenWxaEmbeddedApiGetListRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaEmbeddedApiGetListRequest()

- WechatOpenWxaEmbeddedApiGetListRequest Start(int fieldValue)

- WechatOpenWxaEmbeddedApiGetListRequest Num(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaEmbeddedApiGetListResponse (class)

- public string Raw;


## WechatOpenWxaEmbeddedApiGetOwnListRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaEmbeddedApiGetOwnListRequest()

- WechatOpenWxaEmbeddedApiGetOwnListRequest Start(int fieldValue)

- WechatOpenWxaEmbeddedApiGetOwnListRequest Num(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaEmbeddedApiGetOwnListResponse (class)

- public string Raw;


## WechatOpenWxaEmbeddedApiSetAuthorizeRequest (class)

- WechatTypedRequest request;

- public WechatOpenWxaEmbeddedApiSetAuthorizeRequest()

- WechatOpenWxaEmbeddedApiSetAuthorizeRequest Flag(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenWxaEmbeddedApiSetAuthorizeResponse (class)

- public string Raw;

- public JsonValue Value;
