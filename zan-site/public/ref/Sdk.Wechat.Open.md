# Sdk.Wechat.Open

> 源码: `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenAccountApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenClient.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenCodeApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenCodeTemplateApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenComponentApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenComponentCurrentApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenComponentOpenApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenDomainApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenIcpApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenModifyDomainApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenNewTmplApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenNickNameApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenOAuthApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenOpenApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenP1Api.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenQRConnectApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenSearchStatusApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenSecApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenSecOrderApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenSetWebViewDomainApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenSnsApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenSpecialApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenWxOpenApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenWxOpenManagedOfficialAccountApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenWxaApi.zan`, `packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Open/WechatOpenWxaEmbeddedApi.zan`


## WechatOpenAccountApi (class)

- WechatOpenClient client;

- public WechatOpenAccountApi(WechatOpenClient client)

- async WechatOpenAccountApiFastRegisterResponse FastRegisterAsync(WechatOpenAccountApiFastRegisterRequest request)

- async WechatResponse FastRegisterRawAsync(string query, string jsonBody)

- async WechatOpenAccountApiGetAccountBasicInfoResponse GetAccountBasicInfoAsync()

- async WechatResponse GetAccountBasicInfoRawAsync(string query)

- async WechatOpenAccountApiModifyHeadImageResponse ModifyHeadImageAsync(WechatOpenAccountApiModifyHeadImageRequest request)

- async WechatResponse ModifyHeadImageRawAsync(string query, string jsonBody)

- async WechatOpenAccountApiModifySignatureResponse ModifySignatureAsync(WechatOpenAccountApiModifySignatureRequest request)

- async WechatResponse ModifySignatureRawAsync(string query, string jsonBody)

- async WechatOpenAccountApiComponentRebindAdminResponse ComponentRebindAdminAsync(WechatOpenAccountApiComponentRebindAdminRequest request)

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

- WechatApiTransport transport;

- string componentAccessToken;

- string authorizerAccessToken;

- public WechatOpenClient()

- WechatOpenClient Server(string host, int port)

- WechatOpenClient Timeout(int ms)

- WechatOpenClient SetComponentAccessToken(string token)

- WechatOpenClient SetAuthorizerAccessToken(string token)

- WechatOpenClient SetAccessToken(string token)

- string Credential(WechatCredentialKind kind)

- async WechatResponse RequestAsync(string method, string path, string query, string jsonBody, WechatCredentialKind credential)

- async WechatRawResponse RequestRawAsync(string method, string path, string query, string body, string contentType, WechatCredentialKind credential)

- async WechatRawResponse RequestMultipartAsync(string method, string path, string query, WechatMultipart multipart, WechatCredentialKind credential)


## WechatOpenCodeApi (class)

- WechatOpenClient client;

- public WechatOpenCodeApi(WechatOpenClient client)

- async WechatOpenCodeApiCommitResponse CommitAsync(WechatOpenCodeApiCommitRequest request)

- async WechatResponse CommitRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiGetCategoryResponse GetCategoryAsync()

- async WechatResponse GetCategoryRawAsync(string query)

- async WechatOpenCodeApiGetPageResponse GetPageAsync()

- async WechatResponse GetPageRawAsync(string query)

- async WechatOpenCodeApiSubmitAuditResponse SubmitAuditAsync(WechatOpenCodeApiSubmitAuditRequest request)

- async WechatResponse SubmitAuditRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiGetAuditStatusResponse GetAuditStatusAsync(WechatOpenCodeApiGetAuditStatusRequest request)

- async WechatResponse GetAuditStatusRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiGetLatestAuditStatusResponse GetLatestAuditStatusAsync()

- async WechatResponse GetLatestAuditStatusRawAsync(string query)

- async WechatOpenCodeApiUndoCodeAuditResponse UndoCodeAuditAsync()

- async WechatResponse UndoCodeAuditRawAsync(string query)

- async WechatOpenCodeApiReleaseResponse ReleaseAsync()

- async WechatResponse ReleaseRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiChangeVisitStatusResponse ChangeVisitStatusAsync(WechatOpenCodeApiChangeVisitStatusRequest request)

- async WechatResponse ChangeVisitStatusRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiRevertCodeReleaseResponse RevertCodeReleaseAsync(WechatOpenCodeApiRevertCodeReleaseRequest request)

- async WechatResponse RevertCodeReleaseRawAsync(string query)

- async WechatOpenCodeApiGetWeappSupportVersionResponse GetWeappSupportVersionAsync()

- async WechatResponse GetWeappSupportVersionRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiSetWeappSupportVersionResponse SetWeappSupportVersionAsync(WechatOpenCodeApiSetWeappSupportVersionRequest request)

- async WechatResponse SetWeappSupportVersionRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiGrayReleaseResponse GrayReleaseAsync(WechatOpenCodeApiGrayReleaseRequest request)

- async WechatResponse GrayReleaseRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiRevertGrayReleaseResponse RevertGrayReleaseAsync()

- async WechatResponse RevertGrayReleaseRawAsync(string query)

- async WechatOpenCodeApiGetGrayReleasePlanResponse GetGrayReleasePlanAsync()

- async WechatResponse GetGrayReleasePlanRawAsync(string query)

- async WechatOpenCodeApiQueryQuotaResponse QueryQuotaAsync()

- async WechatResponse QueryQuotaRawAsync(string query, string jsonBody)

- async WechatOpenCodeApiSpeedupAuditResponse SpeedupAuditAsync(WechatOpenCodeApiSpeedupAuditRequest request)

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

- WechatOpenClient client;

- public WechatOpenCodeTemplateApi(WechatOpenClient client)

- async WechatOpenCodeTemplateApiGetTemplateDraftListResponse GetTemplateDraftListAsync()

- async WechatResponse GetTemplateDraftListRawAsync(string query)

- async WechatOpenCodeTemplateApiGetTemplateListResponse GetTemplateListAsync()

- async WechatResponse GetTemplateListRawAsync(string query)

- async WechatOpenCodeTemplateApiAddToTemplateResponse AddToTemplateAsync(WechatOpenCodeTemplateApiAddToTemplateRequest request)

- async WechatResponse AddToTemplateRawAsync(string query, string jsonBody)

- async WechatOpenCodeTemplateApiDeleteTemplateResponse DeleteTemplateAsync(WechatOpenCodeTemplateApiDeleteTemplateRequest request)

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

- public string Raw;


## WechatOpenCodeTemplateApiGetTemplateListResponse (class)

- public string Raw;


## WechatOpenComponentApi (class)

- WechatOpenClient client;

- public WechatOpenComponentApi(WechatOpenClient client)

- async WechatOpenComponentApiGetComponentAccessTokenResponse GetComponentAccessTokenAsync(WechatOpenComponentApiGetComponentAccessTokenRequest request)

- async WechatResponse GetComponentAccessTokenRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetPreAuthCodeResponse GetPreAuthCodeAsync(WechatOpenComponentApiGetPreAuthCodeRequest request)

- async WechatResponse GetPreAuthCodeRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiQueryAuthResponse QueryAuthAsync(WechatOpenComponentApiQueryAuthRequest request)

- async WechatResponse QueryAuthRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiApiConfirmAuthResponse ApiConfirmAuthAsync(WechatOpenComponentApiApiConfirmAuthRequest request)

- async WechatResponse ApiConfirmAuthRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiApiAuthorizerTokenResponse ApiAuthorizerTokenAsync(WechatOpenComponentApiApiAuthorizerTokenRequest request)

- async WechatResponse ApiAuthorizerTokenRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetAuthorizerInfoResponse GetAuthorizerInfoAsync(WechatOpenComponentApiGetAuthorizerInfoRequest request)

- async WechatResponse GetAuthorizerInfoRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetAuthorizerOptionResponse GetAuthorizerOptionAsync(WechatOpenComponentApiGetAuthorizerOptionRequest request)

- async WechatResponse GetAuthorizerOptionRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiSetAuthorizerOptionResponse SetAuthorizerOptionAsync(WechatOpenComponentApiSetAuthorizerOptionRequest request)

- async WechatResponse SetAuthorizerOptionRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetJsApiTicketResponse GetJsApiTicketAsync(WechatOpenComponentApiGetJsApiTicketRequest request)

- async WechatResponse GetJsApiTicketRawAsync(string query)

- async WechatOpenComponentApiFastRegisterEnterpriseWeAppResponse FastRegisterEnterpriseWeAppAsync(WechatOpenComponentApiFastRegisterEnterpriseWeAppRequest request)

- async WechatResponse FastRegisterEnterpriseWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiFastRegisterWeAppResponse FastRegisterWeAppAsync(WechatOpenComponentApiFastRegisterWeAppRequest request)

- async WechatResponse FastRegisterWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiFastRegisterPersonalWeAppResponse FastRegisterPersonalWeAppAsync(WechatOpenComponentApiFastRegisterPersonalWeAppRequest request)

- async WechatResponse FastRegisterPersonalWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiFastRegisterBetaWeAppResponse FastRegisterBetaWeAppAsync(WechatOpenComponentApiFastRegisterBetaWeAppRequest request)

- async WechatResponse FastRegisterBetaWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiVerifyBetaWeAppResponse VerifyBetaWeAppAsync(WechatOpenComponentApiVerifyBetaWeAppRequest request)

- async WechatResponse VerifyBetaWeAppRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiSetBetaWeAppNickNameResponse SetBetaWeAppNickNameAsync(WechatOpenComponentApiSetBetaWeAppNickNameRequest request)

- async WechatResponse SetBetaWeAppNickNameRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetAuthorizerListResponse GetAuthorizerListAsync(WechatOpenComponentApiGetAuthorizerListRequest request)

- async WechatResponse GetAuthorizerListRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiSetPrivacySettingResponse SetPrivacySettingAsync(WechatOpenComponentApiSetPrivacySettingRequest request)

- async WechatResponse SetPrivacySettingRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetPrivacySettingResponse GetPrivacySettingAsync(WechatOpenComponentApiGetPrivacySettingRequest request)

- async WechatResponse GetPrivacySettingRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiModifyWxaServerDomainResponse ModifyWxaServerDomainAsync(WechatOpenComponentApiModifyWxaServerDomainRequest request)

- async WechatResponse ModifyWxaServerDomainRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiGetDomainConfirmFileResponse GetDomainConfirmFileAsync(WechatOpenComponentApiGetDomainConfirmFileRequest request)

- async WechatResponse GetDomainConfirmFileRawAsync(string query, string jsonBody)

- async WechatOpenComponentApiModifyWxaJumpDomainResponse ModifyWxaJumpDomainAsync(WechatOpenComponentApiModifyWxaJumpDomainRequest request)

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

- WechatOpenClient client;

- public WechatOpenComponentCurrentApi(WechatOpenClient client)

- async WechatOpenComponentCurrentApiGetAuthorizerOptionInfoResponse GetAuthorizerOptionInfoAsync(WechatOpenComponentCurrentApiGetAuthorizerOptionInfoRequest request)

- async WechatResponse GetAuthorizerOptionInfoRawAsync(string query, string jsonBody)

- async WechatOpenComponentCurrentApiSetAuthorizerOptionInfoResponse SetAuthorizerOptionInfoAsync(WechatOpenComponentCurrentApiSetAuthorizerOptionInfoRequest request)

- async WechatResponse SetAuthorizerOptionInfoRawAsync(string query, string jsonBody)


## WechatOpenComponentCurrentApiGetAuthorizerOptionInfoRequest (class)

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

- WechatOpenClient client;

- public WechatOpenComponentOpenApi(WechatOpenClient client)

- async WechatOpenComponentOpenApiStartPushTicketResponse StartPushTicketAsync(WechatOpenComponentOpenApiStartPushTicketRequest request)

- async WechatResponse StartPushTicketRawAsync(string query, string jsonBody)

- async WechatOpenComponentOpenApiClearComponentQuotaByAppSecretResponse ClearComponentQuotaByAppSecretAsync(WechatOpenComponentOpenApiClearComponentQuotaByAppSecretRequest request)

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

- WechatOpenClient client;

- public WechatOpenDomainApi(WechatOpenClient client)

- async WechatOpenDomainApiModifyDomainResponse ModifyDomainAsync(WechatOpenDomainApiModifyDomainRequest request)

- async WechatResponse ModifyDomainRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiSetWebViewDomainResponse SetWebViewDomainAsync(WechatOpenDomainApiSetWebViewDomainRequest request)

- async WechatResponse SetWebViewDomainRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiModifyDomainDirectlyResponse ModifyDomainDirectlyAsync(WechatOpenDomainApiModifyDomainDirectlyRequest request)

- async WechatResponse ModifyDomainDirectlyRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiGetWebViewDomainConfirmFileResponse GetWebViewDomainConfirmFileAsync()

- async WechatResponse GetWebViewDomainConfirmFileRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiSetWebViewDomainDirectlyResponse SetWebViewDomainDirectlyAsync(WechatOpenDomainApiSetWebViewDomainDirectlyRequest request)

- async WechatResponse SetWebViewDomainDirectlyRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiGetEffectiveDomainResponse GetEffectiveDomainAsync()

- async WechatResponse GetEffectiveDomainRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiGetEffectiveWebViewDomainResponse GetEffectiveWebViewDomainAsync()

- async WechatResponse GetEffectiveWebViewDomainRawAsync(string query, string jsonBody)

- async WechatOpenDomainApiGetPrefetchDNSDomainResponse GetPrefetchDNSDomainAsync()

- async WechatResponse GetPrefetchDNSDomainRawAsync(string query)

- async WechatOpenDomainApiSetPrefetchDNSDomainResponse SetPrefetchDNSDomainAsync(WechatOpenDomainApiSetPrefetchDNSDomainRequest request)

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

- WechatOpenClient client;

- public WechatOpenIcpApi(WechatOpenClient client)

- async WechatOpenIcpApiQueryIcpVerifyTaskResponse QueryIcpVerifyTaskAsync(WechatOpenIcpApiQueryIcpVerifyTaskRequest request)

- async WechatResponse QueryIcpVerifyTaskRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiCreateIcpVerifyTaskResponse CreateIcpVerifyTaskAsync()

- async WechatResponse CreateIcpVerifyTaskRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiCancelApplyIcpFilingResponse CancelApplyIcpFilingAsync()

- async WechatResponse CancelApplyIcpFilingRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiApplyIcpFilingResponse ApplyIcpFilingAsync(WechatOpenIcpApiApplyIcpFilingRequest request)

- async WechatResponse ApplyIcpFilingRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiCancelIcpFilingResponse CancelIcpFilingAsync(WechatOpenIcpApiCancelIcpFilingRequest request)

- async WechatResponse CancelIcpFilingRawAsync(string query, string jsonBody)

- async WechatOpenIcpApiGetIcpEntranceInfoResponse GetIcpEntranceInfoAsync()

- async WechatResponse GetIcpEntranceInfoRawAsync(string query)

- async WechatOpenIcpApiGetOnlineIcpOrderResponse GetOnlineIcpOrderAsync()

- async WechatResponse GetOnlineIcpOrderRawAsync(string query)

- async WechatOpenIcpApiQueryIcpServiceContentTypesResponse QueryIcpServiceContentTypesAsync()

- async WechatResponse QueryIcpServiceContentTypesRawAsync(string query)

- async WechatOpenIcpApiQueryIcpCertificateTypesResponse QueryIcpCertificateTypesAsync()

- async WechatResponse QueryIcpCertificateTypesRawAsync(string query)

- async WechatOpenIcpApiQueryIcpDistrictCodeResponse QueryIcpDistrictCodeAsync()

- async WechatResponse QueryIcpDistrictCodeRawAsync(string query)

- async WechatOpenIcpApiQueryIcpNrlxTypesResponse QueryIcpNrlxTypesAsync()

- async WechatResponse QueryIcpNrlxTypesRawAsync(string query)

- async WechatOpenIcpApiQueryIcpSubjectTypesResponse QueryIcpSubjectTypesAsync()

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

- WechatTypedRequest request;

- public WechatOpenIcpApiQueryIcpVerifyTaskRequest()

- WechatOpenIcpApiQueryIcpVerifyTaskRequest TaskId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenIcpApiQueryIcpVerifyTaskResponse (class)

- public string Raw;


## WechatOpenModifyDomainApi (class)

- WechatOpenClient client;

- public WechatOpenModifyDomainApi(WechatOpenClient client)

- async WechatOpenModifyDomainApiModifyDomainResponse ModifyDomainAsync(WechatOpenModifyDomainApiModifyDomainRequest request)

- async WechatResponse ModifyDomainRawAsync(string query, string jsonBody)


## WechatOpenModifyDomainApiModifyDomainRequest (class)

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

- WechatOpenClient client;

- public WechatOpenNewTmplApi(WechatOpenClient client)

- async WechatOpenNewTmplApiGetPubTemplateTitlesResponse GetPubTemplateTitlesAsync(WechatOpenNewTmplApiGetPubTemplateTitlesRequest request)

- async WechatResponse GetPubTemplateTitlesRawAsync(string query)

- async WechatOpenNewTmplApiGetPubTemplateKeyWordsByIdResponse GetPubTemplateKeyWordsByIdAsync(WechatOpenNewTmplApiGetPubTemplateKeyWordsByIdRequest request)

- async WechatResponse GetPubTemplateKeyWordsByIdRawAsync(string query)

- async WechatOpenNewTmplApiAddTemplateResponse AddTemplateAsync(WechatOpenNewTmplApiAddTemplateRequest request)

- async WechatResponse AddTemplateRawAsync(string query, string jsonBody)

- async WechatOpenNewTmplApiGetTemplateListResponse GetTemplateListAsync()

- async WechatResponse GetTemplateListRawAsync(string query)

- async WechatOpenNewTmplApiGetCategoryResponse GetCategoryAsync()

- async WechatResponse GetCategoryRawAsync(string query)

- async WechatOpenNewTmplApiDelTemplateResponse DelTemplateAsync(WechatOpenNewTmplApiDelTemplateRequest request)

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

- WechatOpenClient client;

- public WechatOpenNickNameApi(WechatOpenClient client)

- async WechatOpenNickNameApiSetNickNameResponse SetNickNameAsync(WechatOpenNickNameApiSetNickNameRequest request)

- async WechatResponse SetNickNameRawAsync(string query, string jsonBody)

- async WechatOpenNickNameApiQueryNickNameResponse QueryNickNameAsync(WechatOpenNickNameApiQueryNickNameRequest request)

- async WechatResponse QueryNickNameRawAsync(string query, string jsonBody)

- async WechatOpenNickNameApiCheckWxVerifyNickNameResponse CheckWxVerifyNickNameAsync(WechatOpenNickNameApiCheckWxVerifyNickNameRequest request)

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

- WechatOpenClient client;

- public WechatOpenOAuthApi(WechatOpenClient client)

- async WechatOpenOAuthApiGetAccessTokenResponse GetAccessTokenAsync(WechatOpenOAuthApiGetAccessTokenRequest request)

- async WechatResponse GetAccessTokenRawAsync(string query)

- async WechatOpenOAuthApiRefreshTokenResponse RefreshTokenAsync(WechatOpenOAuthApiRefreshTokenRequest request)

- async WechatResponse RefreshTokenRawAsync(string query)

- async WechatOpenOAuthApiGetUserInfoResponse GetUserInfoAsync(WechatOpenOAuthApiGetUserInfoRequest request)

- async WechatResponse GetUserInfoRawAsync(string query)


## WechatOpenOAuthApiGetAccessTokenRequest (class)

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

- WechatOpenClient client;

- public WechatOpenOpenApi(WechatOpenClient client)

- async WechatOpenOpenApiCreateResponse CreateAsync(WechatOpenOpenApiCreateRequest request)

- async WechatResponse CreateRawAsync(string query, string jsonBody)

- async WechatOpenOpenApiBindResponse BindAsync(WechatOpenOpenApiBindRequest request)

- async WechatResponse BindRawAsync(string query, string jsonBody)

- async WechatOpenOpenApiUnbindResponse UnbindAsync(WechatOpenOpenApiUnbindRequest request)

- async WechatResponse UnbindRawAsync(string query, string jsonBody)

- async WechatOpenOpenApiGetResponse GetAsync(WechatOpenOpenApiGetRequest request)

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

- WechatOpenClient client;

- public WechatOpenP1Api(WechatOpenClient client)

- async WechatOpenP1ApiSetPreFetchDataSettingResponse SetPreFetchDataSettingAsync(WechatOpenP1ApiSetPreFetchDataSettingRequest request)

- async WechatResponse SetPreFetchDataSettingRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiSetPeriodFetchDataSettingResponse SetPeriodFetchDataSettingAsync(WechatOpenP1ApiSetPeriodFetchDataSettingRequest request)

- async WechatResponse SetPeriodFetchDataSettingRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiGetBindOpenAccountResponse GetBindOpenAccountAsync()

- async WechatResponse GetBindOpenAccountRawAsync(string query)

- async WechatOpenP1ApiGetBindOpenAccountEntityResponse GetBindOpenAccountEntityAsync()

- async WechatResponse GetBindOpenAccountEntityRawAsync(string query)

- async WechatOpenP1ApiGetSettingCategoriesResponse GetSettingCategoriesAsync()

- async WechatResponse GetSettingCategoriesRawAsync(string query)

- async WechatOpenP1ApiGetCategoriesByTypeResponse GetCategoriesByTypeAsync(WechatOpenP1ApiGetCategoriesByTypeRequest request)

- async WechatResponse GetCategoriesByTypeRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiGetCategoryNamesResponse GetCategoryNamesAsync()

- async WechatResponse GetCategoryNamesRawAsync(string query)

- async WechatOpenP1ApiGetVisitStatusResponse GetVisitStatusAsync()

- async WechatResponse GetVisitStatusRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiGetCodePrivacyInfoResponse GetCodePrivacyInfoAsync()

- async WechatResponse GetCodePrivacyInfoRawAsync(string query)

- async WechatOpenP1ApiSubmitAuthAndIcpResponse SubmitAuthAndIcpAsync(WechatOpenP1ApiSubmitAuthAndIcpRequest request)

- async WechatResponse SubmitAuthAndIcpRawAsync(string query, string jsonBody)

- async WechatOpenP1ApiQueryAuthAndIcpResponse QueryAuthAndIcpAsync(WechatOpenP1ApiQueryAuthAndIcpRequest request)

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

- WechatOpenClient client;

- public WechatOpenQRConnectApi(WechatOpenClient client)

- async WechatOpenQRConnectApiGetAccessTokenResponse GetAccessTokenAsync(WechatOpenQRConnectApiGetAccessTokenRequest request)

- async WechatResponse GetAccessTokenRawAsync(string query)

- async WechatOpenQRConnectApiRefreshTokenResponse RefreshTokenAsync(WechatOpenQRConnectApiRefreshTokenRequest request)

- async WechatResponse RefreshTokenRawAsync(string query)

- async WechatOpenQRConnectApiGetUserInfoResponse GetUserInfoAsync(WechatOpenQRConnectApiGetUserInfoRequest request)

- async WechatResponse GetUserInfoRawAsync(string query)

- async WechatOpenQRConnectApiAuthResponse AuthAsync(WechatOpenQRConnectApiAuthRequest request)

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

- WechatOpenClient client;

- public WechatOpenSearchStatusApi(WechatOpenClient client)

- async WechatOpenSearchStatusApiGetWxaSearchStatusResponse GetWxaSearchStatusAsync()

- async WechatResponse GetWxaSearchStatusRawAsync(string query)

- async WechatOpenSearchStatusApiChangeWxaSearchStatusResponse ChangeWxaSearchStatusAsync(WechatOpenSearchStatusApiChangeWxaSearchStatusRequest request)

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

- public string Raw;


## WechatOpenSecApi (class)

- WechatOpenClient client;

- public WechatOpenSecApi(WechatOpenClient client)

- async WechatOpenSecApiWxaAuthResponse WxaAuthAsync(WechatOpenSecApiWxaAuthRequest request)

- async WechatResponse WxaAuthRawAsync(string query, string jsonBody)

- async WechatOpenSecApiQueryAuthResponse QueryAuthAsync(WechatOpenSecApiQueryAuthRequest request)

- async WechatResponse QueryAuthRawAsync(string query, string jsonBody)

- async WechatOpenSecApiReauthResponse ReauthAsync(WechatOpenSecApiReauthRequest request)

- async WechatResponse ReauthRawAsync(string query, string jsonBody)

- async WechatOpenSecApiAuthIdentityTreeResponse AuthIdentityTreeAsync()

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

- WechatOpenClient client;

- public WechatOpenSecOrderApi(WechatOpenClient client)

- async WechatOpenSecOrderApiUploadShippingInfoResponse UploadShippingInfoAsync(WechatOpenSecOrderApiUploadShippingInfoRequest request)

- async WechatResponse UploadShippingInfoRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiUploadCombinedShippingInfoResponse UploadCombinedShippingInfoAsync(WechatOpenSecOrderApiUploadCombinedShippingInfoRequest request)

- async WechatResponse UploadCombinedShippingInfoRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiGetOrderResponse GetOrderAsync(WechatOpenSecOrderApiGetOrderRequest request)

- async WechatResponse GetOrderRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiGetOrderListResponse GetOrderListAsync(WechatOpenSecOrderApiGetOrderListRequest request)

- async WechatResponse GetOrderListRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiNotifyConfirmReceiveResponse NotifyConfirmReceiveAsync(WechatOpenSecOrderApiNotifyConfirmReceiveRequest request)

- async WechatResponse NotifyConfirmReceiveRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiSetMsgJumpPathResponse SetMsgJumpPathAsync(WechatOpenSecOrderApiSetMsgJumpPathRequest request)

- async WechatResponse SetMsgJumpPathRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiIsTradeManagedResponse IsTradeManagedAsync(WechatOpenSecOrderApiIsTradeManagedRequest request)

- async WechatResponse IsTradeManagedRawAsync(string query, string jsonBody)

- async WechatOpenSecOrderApiIsTradeManagementConfirmationCompletedResponse IsTradeManagementConfirmationCompletedAsync(WechatOpenSecOrderApiIsTradeManagementConfirmationCompletedRequest request)

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

- WechatOpenClient client;

- public WechatOpenSetWebViewDomainApi(WechatOpenClient client)

- async WechatOpenSetWebViewDomainApiSetWebViewDomainResponse SetWebViewDomainAsync(WechatOpenSetWebViewDomainApiSetWebViewDomainRequest request)

- async WechatResponse SetWebViewDomainRawAsync(string query, string jsonBody)


## WechatOpenSetWebViewDomainApiSetWebViewDomainRequest (class)

- WechatTypedRequest request;

- public WechatOpenSetWebViewDomainApiSetWebViewDomainRequest()

- WechatOpenSetWebViewDomainApiSetWebViewDomainRequest Action(int fieldValue)

- WechatOpenSetWebViewDomainApiSetWebViewDomainRequest Webviewdomain(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatOpenSetWebViewDomainApiSetWebViewDomainResponse (class)

- public string Raw;


## WechatOpenSnsApi (class)

- WechatOpenClient client;

- public WechatOpenSnsApi(WechatOpenClient client)

- async WechatOpenSnsApiJsCode2JsonResponse JsCode2JsonAsync(WechatOpenSnsApiJsCode2JsonRequest request)

- async WechatResponse JsCode2JsonRawAsync(string query)


## WechatOpenSnsApiJsCode2JsonRequest (class)

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

- WechatOpenClient client;

- public WechatOpenWxOpenApi(WechatOpenClient client)

- async WechatOpenWxOpenApiGetAllCategoriesResponse GetAllCategoriesAsync()

- async WechatResponse GetAllCategoriesRawAsync(string query)

- async WechatOpenWxOpenApiAddCategoryResponse AddCategoryAsync(WechatOpenWxOpenApiAddCategoryRequest request)

- async WechatResponse AddCategoryRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenApiDeleteCategoryResponse DeleteCategoryAsync(WechatOpenWxOpenApiDeleteCategoryRequest request)

- async WechatResponse DeleteCategoryRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenApiGetCategoryResponse GetCategoryAsync()

- async WechatResponse GetCategoryRawAsync(string query)

- async WechatOpenWxOpenApiModifyCategoryResponse ModifyCategoryAsync(WechatOpenWxOpenApiModifyCategoryRequest request)

- async WechatResponse ModifyCategoryRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenApiWxaMpLinkGetResponse WxaMpLinkGetAsync()

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

- WechatOpenClient client;

- public WechatOpenWxOpenManagedOfficialAccountApi(WechatOpenClient client)

- async WechatOpenWxOpenManagedOfficialAccountApiGetLinkMiniprogramResponse GetLinkMiniprogramAsync()

- async WechatResponse GetLinkMiniprogramRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramResponse LinkMiniprogramAsync(WechatOpenWxOpenManagedOfficialAccountApiLinkMiniprogramRequest request)

- async WechatResponse LinkMiniprogramRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenManagedOfficialAccountApiUnlinkMiniprogramResponse UnlinkMiniprogramAsync(WechatOpenWxOpenManagedOfficialAccountApiUnlinkMiniprogramRequest request)

- async WechatResponse UnlinkMiniprogramRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenManagedOfficialAccountApiGetResponse GetAsync(WechatOpenWxOpenManagedOfficialAccountApiGetRequest request)

- async WechatResponse GetRawAsync(string query, string jsonBody)

- async WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateResponse AddOrUpdateAsync(WechatOpenWxOpenManagedOfficialAccountApiAddOrUpdateRequest request)

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

- WechatOpenClient client;

- public WechatOpenWxaApi(WechatOpenClient client)

- async WechatOpenWxaApiGetShowWxaItemResponse GetShowWxaItemAsync()

- async WechatResponse GetShowWxaItemRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetWxaMpLinkForShowResponse GetWxaMpLinkForShowAsync(WechatOpenWxaApiGetWxaMpLinkForShowRequest request)

- async WechatResponse GetWxaMpLinkForShowRawAsync(string query)

- async WechatOpenWxaApiUpdateShowWxaItemResponse UpdateShowWxaItemAsync(WechatOpenWxaApiUpdateShowWxaItemRequest request)

- async WechatResponse UpdateShowWxaItemRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetIllegalRecordsResponse GetIllegalRecordsAsync(WechatOpenWxaApiGetIllegalRecordsRequest request)

- async WechatResponse GetIllegalRecordsRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetAppealRecordsResponse GetAppealRecordsAsync(WechatOpenWxaApiGetAppealRecordsRequest request)

- async WechatResponse GetAppealRecordsRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetPrivacyInterfaceResponse GetPrivacyInterfaceAsync()

- async WechatResponse GetPrivacyInterfaceRawAsync(string query)

- async WechatOpenWxaApiApplyPrivacyInterfaceResponse ApplyPrivacyInterfaceAsync(WechatOpenWxaApiApplyPrivacyInterfaceRequest request)

- async WechatResponse ApplyPrivacyInterfaceRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetVersionInfoResponse GetVersionInfoAsync()

- async WechatResponse GetVersionInfoRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiGetOrderPathInfoResponse GetOrderPathInfoAsync(WechatOpenWxaApiGetOrderPathInfoRequest request)

- async WechatResponse GetOrderPathInfoRawAsync(string query, string jsonBody)

- async WechatOpenWxaApiApplySetOrderPathInfoResponse ApplySetOrderPathInfoAsync(WechatOpenWxaApiApplySetOrderPathInfoRequest request)

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

- WechatOpenClient client;

- public WechatOpenWxaEmbeddedApi(WechatOpenClient client)

- async WechatOpenWxaEmbeddedApiAddEmbeddedResponse AddEmbeddedAsync(WechatOpenWxaEmbeddedApiAddEmbeddedRequest request)

- async WechatResponse AddEmbeddedRawAsync(string query, string jsonBody)

- async WechatOpenWxaEmbeddedApiDelEmbeddedResponse DelEmbeddedAsync(WechatOpenWxaEmbeddedApiDelEmbeddedRequest request)

- async WechatResponse DelEmbeddedRawAsync(string query, string jsonBody)

- async WechatOpenWxaEmbeddedApiDelAuthorizeResponse DelAuthorizeAsync(WechatOpenWxaEmbeddedApiDelAuthorizeRequest request)

- async WechatResponse DelAuthorizeRawAsync(string query, string jsonBody)

- async WechatOpenWxaEmbeddedApiGetListResponse GetListAsync(WechatOpenWxaEmbeddedApiGetListRequest request)

- async WechatResponse GetListRawAsync(string query)

- async WechatOpenWxaEmbeddedApiGetOwnListResponse GetOwnListAsync(WechatOpenWxaEmbeddedApiGetOwnListRequest request)

- async WechatResponse GetOwnListRawAsync(string query)

- async WechatOpenWxaEmbeddedApiSetAuthorizeResponse SetAuthorizeAsync(WechatOpenWxaEmbeddedApiSetAuthorizeRequest request)

- async WechatResponse SetAuthorizeRawAsync(string query, string jsonBody)


## WechatOpenWxaEmbeddedApiAddEmbeddedRequest (class)

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
