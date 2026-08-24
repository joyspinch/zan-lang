# Sdk.Wechat.Mp

> 源码: `stdlib/Sdk/Wechat/Mp/WechatMpAnalysisApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpAutoReplyApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpCVImageApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpCVOCRApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpCardApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpCardStoreApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpCommentApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpCustomApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpCustomServiceApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpDraftApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpDraftProductCardApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpFreePublishApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpGroupMessageApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpGroupsApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpInvoiceApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpMediaApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpMedicalAssistantApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpMerchantExpressApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpMerchantGroupApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpMerchantOrderApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpMerchantProductApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpMerchantShelfApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpMerchantStockApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpNewTmplApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpNonTaxPayApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpOneCodeApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpPoiApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpQrCodeApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpRequest.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpScanApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpSemanticApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpShakeAroundApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpSpecialApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpTemplateMessageTemplateApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpUrlApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpUserApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpUserTagApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpWiFiApi.zan`, `stdlib/Sdk/Wechat/Mp/WechatMpWxaApi.zan`


## WechatMpAnalysisApi (class)

Analysis/AnalysisApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpAnalysisApi(WechatClient client)

- async WechatMpAnalysisApiGetArticleSummaryResponse GetArticleSummaryAsync(WechatMpAnalysisApiGetArticleSummaryRequest request)
  - POST /datacube/getarticlesummary

- async WechatResponse GetArticleSummaryRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetArticleTotalResponse GetArticleTotalAsync(WechatMpAnalysisApiGetArticleTotalRequest request)
  - POST /datacube/getarticletotal

- async WechatResponse GetArticleTotalRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUserReadResponse GetUserReadAsync(WechatMpAnalysisApiGetUserReadRequest request)
  - POST /datacube/getuserread

- async WechatResponse GetUserReadRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUserReadHourResponse GetUserReadHourAsync(WechatMpAnalysisApiGetUserReadHourRequest request)
  - POST /datacube/getuserreadhour

- async WechatResponse GetUserReadHourRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUserShareResponse GetUserShareAsync(WechatMpAnalysisApiGetUserShareRequest request)
  - POST /datacube/getusershare

- async WechatResponse GetUserShareRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUserShareHourResponse GetUserShareHourAsync(WechatMpAnalysisApiGetUserShareHourRequest request)
  - POST /datacube/getusersharehour

- async WechatResponse GetUserShareHourRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetPublishedArticleReadResponse GetPublishedArticleReadAsync(WechatMpAnalysisApiGetPublishedArticleReadRequest request)
  - POST /datacube/getarticleread

- async WechatResponse GetPublishedArticleReadRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetPublishedArticleShareResponse GetPublishedArticleShareAsync(WechatMpAnalysisApiGetPublishedArticleShareRequest request)
  - POST /datacube/getarticleshare

- async WechatResponse GetPublishedArticleShareRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetPublishedArticleBizSummaryResponse GetPublishedArticleBizSummaryAsync(WechatMpAnalysisApiGetPublishedArticleBizSummaryRequest request)
  - POST /datacube/getbizsummary

- async WechatResponse GetPublishedArticleBizSummaryRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetPublishedArticleTotalDetailResponse GetPublishedArticleTotalDetailAsync(WechatMpAnalysisApiGetPublishedArticleTotalDetailRequest request)
  - POST /datacube/getarticletotaldetail

- async WechatResponse GetPublishedArticleTotalDetailRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetInterfaceSummaryResponse GetInterfaceSummaryAsync(WechatMpAnalysisApiGetInterfaceSummaryRequest request)
  - POST /datacube/getinterfacesummary

- async WechatResponse GetInterfaceSummaryRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetInterfaceSummaryHourResponse GetInterfaceSummaryHourAsync(WechatMpAnalysisApiGetInterfaceSummaryHourRequest request)
  - POST /datacube/getinterfacesummaryhour

- async WechatResponse GetInterfaceSummaryHourRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUpStreamMsgResponse GetUpStreamMsgAsync(WechatMpAnalysisApiGetUpStreamMsgRequest request)
  - POST /datacube/getupstreammsg

- async WechatResponse GetUpStreamMsgRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUpStreamMsgHourResponse GetUpStreamMsgHourAsync(WechatMpAnalysisApiGetUpStreamMsgHourRequest request)
  - POST /datacube/getupstreammsghour

- async WechatResponse GetUpStreamMsgHourRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUpStreamMsgWeekResponse GetUpStreamMsgWeekAsync(WechatMpAnalysisApiGetUpStreamMsgWeekRequest request)
  - POST /datacube/getupstreammsgweek

- async WechatResponse GetUpStreamMsgWeekRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUpStreamMsgMonthResponse GetUpStreamMsgMonthAsync(WechatMpAnalysisApiGetUpStreamMsgMonthRequest request)
  - POST /datacube/getupstreammsgmonth

- async WechatResponse GetUpStreamMsgMonthRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUpStreamMsgDistResponse GetUpStreamMsgDistAsync(WechatMpAnalysisApiGetUpStreamMsgDistRequest request)
  - POST /datacube/getupstreammsgdist

- async WechatResponse GetUpStreamMsgDistRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUpStreamMsgDistWeekResponse GetUpStreamMsgDistWeekAsync(WechatMpAnalysisApiGetUpStreamMsgDistWeekRequest request)
  - POST /datacube/getupstreammsgdistweek

- async WechatResponse GetUpStreamMsgDistWeekRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUpStreamMsgDistMonthResponse GetUpStreamMsgDistMonthAsync(WechatMpAnalysisApiGetUpStreamMsgDistMonthRequest request)
  - POST /datacube/getupstreammsgdistmonth

- async WechatResponse GetUpStreamMsgDistMonthRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUserSummaryResponse GetUserSummaryAsync(WechatMpAnalysisApiGetUserSummaryRequest request)
  - POST /datacube/getusersummary

- async WechatResponse GetUserSummaryRawAsync(string query, string jsonBody)

- async WechatMpAnalysisApiGetUserCumulateResponse GetUserCumulateAsync(WechatMpAnalysisApiGetUserCumulateRequest request)
  - POST /datacube/getusercumulate

- async WechatResponse GetUserCumulateRawAsync(string query, string jsonBody)


## WechatMpAnalysisApiGetArticleSummaryRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetArticleSummaryRequest()

- WechatMpAnalysisApiGetArticleSummaryRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetArticleSummaryRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetArticleSummaryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetArticleTotalRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetArticleTotalRequest()

- WechatMpAnalysisApiGetArticleTotalRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetArticleTotalRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetArticleTotalResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetInterfaceSummaryHourRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetInterfaceSummaryHourRequest()

- WechatMpAnalysisApiGetInterfaceSummaryHourRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetInterfaceSummaryHourRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetInterfaceSummaryHourResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetInterfaceSummaryRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetInterfaceSummaryRequest()

- WechatMpAnalysisApiGetInterfaceSummaryRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetInterfaceSummaryRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetInterfaceSummaryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetPublishedArticleBizSummaryRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetPublishedArticleBizSummaryRequest()

- WechatMpAnalysisApiGetPublishedArticleBizSummaryRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetPublishedArticleBizSummaryRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetPublishedArticleBizSummaryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetPublishedArticleReadRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetPublishedArticleReadRequest()

- WechatMpAnalysisApiGetPublishedArticleReadRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetPublishedArticleReadRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetPublishedArticleReadResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetPublishedArticleShareRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetPublishedArticleShareRequest()

- WechatMpAnalysisApiGetPublishedArticleShareRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetPublishedArticleShareRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetPublishedArticleShareResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetPublishedArticleTotalDetailRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetPublishedArticleTotalDetailRequest()

- WechatMpAnalysisApiGetPublishedArticleTotalDetailRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetPublishedArticleTotalDetailRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetPublishedArticleTotalDetailResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUpStreamMsgDistMonthRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUpStreamMsgDistMonthRequest()

- WechatMpAnalysisApiGetUpStreamMsgDistMonthRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUpStreamMsgDistMonthRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUpStreamMsgDistMonthResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUpStreamMsgDistRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUpStreamMsgDistRequest()

- WechatMpAnalysisApiGetUpStreamMsgDistRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUpStreamMsgDistRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUpStreamMsgDistResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUpStreamMsgDistWeekRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUpStreamMsgDistWeekRequest()

- WechatMpAnalysisApiGetUpStreamMsgDistWeekRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUpStreamMsgDistWeekRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUpStreamMsgDistWeekResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUpStreamMsgHourRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUpStreamMsgHourRequest()

- WechatMpAnalysisApiGetUpStreamMsgHourRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUpStreamMsgHourRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUpStreamMsgHourResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUpStreamMsgMonthRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUpStreamMsgMonthRequest()

- WechatMpAnalysisApiGetUpStreamMsgMonthRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUpStreamMsgMonthRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUpStreamMsgMonthResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUpStreamMsgRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUpStreamMsgRequest()

- WechatMpAnalysisApiGetUpStreamMsgRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUpStreamMsgRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUpStreamMsgResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUpStreamMsgWeekRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUpStreamMsgWeekRequest()

- WechatMpAnalysisApiGetUpStreamMsgWeekRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUpStreamMsgWeekRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUpStreamMsgWeekResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUserCumulateRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUserCumulateRequest()

- WechatMpAnalysisApiGetUserCumulateRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUserCumulateRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUserCumulateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUserReadHourRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUserReadHourRequest()

- WechatMpAnalysisApiGetUserReadHourRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUserReadHourRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUserReadHourResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUserReadRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUserReadRequest()

- WechatMpAnalysisApiGetUserReadRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUserReadRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUserReadResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUserShareHourRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUserShareHourRequest()

- WechatMpAnalysisApiGetUserShareHourRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUserShareHourRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUserShareHourResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUserShareRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUserShareRequest()

- WechatMpAnalysisApiGetUserShareRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUserShareRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUserShareResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAnalysisApiGetUserSummaryRequest (class)

- WechatTypedRequest request;

- public WechatMpAnalysisApiGetUserSummaryRequest()

- WechatMpAnalysisApiGetUserSummaryRequest BeginDate(string fieldValue)

- WechatMpAnalysisApiGetUserSummaryRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpAnalysisApiGetUserSummaryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpAutoReplyApi (class)

AutoReply/AutoReplyApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpAutoReplyApi(WechatClient client)

- async WechatMpAutoReplyApiGetCurrentAutoreplyInfoResponse GetCurrentAutoreplyInfoAsync()
  - GET /cgi-bin/get_current_autoreply_info

- async WechatResponse GetCurrentAutoreplyInfoRawAsync(string query)


## WechatMpAutoReplyApiGetCurrentAutoreplyInfoResponse (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- public string Raw;


## WechatMpCVImageApi (class)

CV/Image/ImageApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpCVImageApi(WechatClient client)

- async WechatMpCVImageApiQrCodeResponse QrCodeAsync(WechatMpCVImageApiQrCodeRequest request)
  - POST /cv/img/qrcode

- async WechatResponse QrCodeRawAsync(string query, string jsonBody)


## WechatMpCVImageApiQrCodeRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpCVImageApiQrCodeRequest()

- WechatMpCVImageApiQrCodeRequest ImgUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCVImageApiQrCodeResponse (class)

- public string Raw;


## WechatMpCVOCRApi (class)

CV/OCR/OCRApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpCVOCRApi(WechatClient client)

- async WechatMpCVOCRApiIdCardResponse IdCardAsync(WechatMpCVOCRApiIdCardRequest request)
  - POST /cv/ocr/idcard

- async WechatResponse IdCardRawAsync(string query, string jsonBody)

- async WechatMpCVOCRApiBankCardResponse BankCardAsync(WechatMpCVOCRApiBankCardRequest request)
  - POST /cv/ocr/bankcard

- async WechatResponse BankCardRawAsync(string query, string jsonBody)

- async WechatMpCVOCRApiDrivingResponse DrivingAsync(WechatMpCVOCRApiDrivingRequest request)
  - POST /cv/ocr/driving

- async WechatResponse DrivingRawAsync(string query, string jsonBody)

- async WechatMpCVOCRApiDrivingLicenseResponse DrivingLicenseAsync(WechatMpCVOCRApiDrivingLicenseRequest request)
  - POST /cv/ocr/drivinglicense

- async WechatResponse DrivingLicenseRawAsync(string query, string jsonBody)

- async WechatMpCVOCRApiBizLicenseResponse BizLicenseAsync(WechatMpCVOCRApiBizLicenseRequest request)
  - POST /cv/ocr/bizlicense

- async WechatResponse BizLicenseRawAsync(string query, string jsonBody)

- async WechatMpCVOCRApiCommResponse CommAsync(WechatMpCVOCRApiCommRequest request)
  - POST /cv/ocr/comm

- async WechatResponse CommRawAsync(string query, string jsonBody)

- async WechatMpCVOCRApiPlateNumResponse PlateNumAsync(WechatMpCVOCRApiPlateNumRequest request)
  - POST /cv/ocr/platenum

- async WechatResponse PlateNumRawAsync(string query, string jsonBody)

- async WechatMpCVOCRApiMenuResponse MenuAsync(WechatMpCVOCRApiMenuRequest request)
  - POST /cv/ocr/menu

- async WechatResponse MenuRawAsync(string query, string jsonBody)


## WechatMpCVOCRApiBankCardRequest (class)

- WechatTypedRequest request;

- public WechatMpCVOCRApiBankCardRequest()

- WechatMpCVOCRApiBankCardRequest ImgUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCVOCRApiBankCardResponse (class)

- public string Raw;


## WechatMpCVOCRApiBizLicenseRequest (class)

- WechatTypedRequest request;

- public WechatMpCVOCRApiBizLicenseRequest()

- WechatMpCVOCRApiBizLicenseRequest ImgUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCVOCRApiBizLicenseResponse (class)

- public string Raw;


## WechatMpCVOCRApiCommRequest (class)

- WechatTypedRequest request;

- public WechatMpCVOCRApiCommRequest()

- WechatMpCVOCRApiCommRequest ImgUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCVOCRApiCommResponse (class)

- public string Raw;


## WechatMpCVOCRApiDrivingLicenseRequest (class)

- WechatTypedRequest request;

- public WechatMpCVOCRApiDrivingLicenseRequest()

- WechatMpCVOCRApiDrivingLicenseRequest ImgUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCVOCRApiDrivingLicenseResponse (class)

- public string Raw;


## WechatMpCVOCRApiDrivingRequest (class)

- WechatTypedRequest request;

- public WechatMpCVOCRApiDrivingRequest()

- WechatMpCVOCRApiDrivingRequest ImgUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCVOCRApiDrivingResponse (class)

- public string Raw;


## WechatMpCVOCRApiIdCardRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpCVOCRApiIdCardRequest()

- WechatMpCVOCRApiIdCardRequest ImgUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCVOCRApiIdCardResponse (class)

- public string Raw;


## WechatMpCVOCRApiMenuRequest (class)

- WechatTypedRequest request;

- public WechatMpCVOCRApiMenuRequest()

- WechatMpCVOCRApiMenuRequest ImgUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCVOCRApiMenuResponse (class)

- public string Raw;


## WechatMpCVOCRApiPlateNumRequest (class)

- WechatTypedRequest request;

- public WechatMpCVOCRApiPlateNumRequest()

- WechatMpCVOCRApiPlateNumRequest ImgUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCVOCRApiPlateNumResponse (class)

- public string Raw;


## WechatMpCardApi (class)

Card/CardAPI.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpCardApi(WechatClient client)

- async WechatMpCardApiCreateCardResponse CreateCardAsync(WechatMpCardApiCreateCardRequest request)
  - POST /card/create

- async WechatResponse CreateCardRawAsync(string query, string jsonBody)

- async WechatMpCardApiPayActiveResponse PayActiveAsync()
  - GET /card/pay/activate

- async WechatResponse PayActiveRawAsync(string query)

- async WechatMpCardApiGetpayPriceResponse GetpayPriceAsync(WechatMpCardApiGetpayPriceRequest request)
  - POST /card/pay/getpayprice

- async WechatResponse GetpayPriceRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetCoinsInfoResponse GetCoinsInfoAsync()
  - GET /card/pay/getcoinsinfo

- async WechatResponse GetCoinsInfoRawAsync(string query)

- async WechatMpCardApiPayConfirmResponse PayConfirmAsync(WechatMpCardApiPayConfirmRequest request)
  - POST /card/pay/confirm

- async WechatResponse PayConfirmRawAsync(string query, string jsonBody)

- async WechatMpCardApiPayRechargeResponse PayRechargeAsync(WechatMpCardApiPayRechargeRequest request)
  - POST /card/pay/recharge

- async WechatResponse PayRechargeRawAsync(string query, string jsonBody)

- async WechatMpCardApiPayGetOrderResponse PayGetOrderAsync(WechatMpCardApiPayGetOrderRequest request)
  - POST /card/pay/getorder

- async WechatResponse PayGetOrderRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetOrderListResponse GetOrderListAsync(WechatMpCardApiGetOrderListRequest request)
  - POST /card/pay/getorderlist

- async WechatResponse GetOrderListRawAsync(string query, string jsonBody)

- async WechatMpCardApiCreateQRResponse CreateQRAsync(WechatMpCardApiCreateQRRequest request)
  - POST /card/qrcode/create

- async WechatResponse CreateQRRawAsync(string query, string jsonBody)

- async WechatMpCardApiShelfCreateResponse ShelfCreateAsync(WechatMpCardApiShelfCreateRequest request)
  - POST /card/landingpage/create

- async WechatResponse ShelfCreateRawAsync(string query, string jsonBody)

- async WechatMpCardApiCodeDepositResponse CodeDepositAsync(WechatMpCardApiCodeDepositRequest request)
  - POST /card/code/deposit

- async WechatResponse CodeDepositRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetDepositCountResponse GetDepositCountAsync(WechatMpCardApiGetDepositCountRequest request)
  - POST /card/code/getdepositcount

- async WechatResponse GetDepositCountRawAsync(string query, string jsonBody)

- async WechatMpCardApiCheckCodeResponse CheckCodeAsync(WechatMpCardApiCheckCodeRequest request)
  - POST /card/code/checkcode

- async WechatResponse CheckCodeRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetHtmlResponse GetHtmlAsync(WechatMpCardApiGetHtmlRequest request)
  - POST /card/mpnews/gethtml

- async WechatResponse GetHtmlRawAsync(string query, string jsonBody)

- async WechatMpCardApiCodeMarkResponse CodeMarkAsync(WechatMpCardApiCodeMarkRequest request)
  - POST /card/code/mark

- async WechatResponse CodeMarkRawAsync(string query, string jsonBody)

- async WechatMpCardApiCardConsumeResponse CardConsumeAsync(WechatMpCardApiCardConsumeRequest request)
  - POST /card/code/consume

- async WechatResponse CardConsumeRawAsync(string query, string jsonBody)

- async WechatMpCardApiCardDecryptResponse CardDecryptAsync(WechatMpCardApiCardDecryptRequest request)
  - POST /card/code/decrypt

- async WechatResponse CardDecryptRawAsync(string query, string jsonBody)

- async WechatMpCardApiCardDeleteResponse CardDeleteAsync(WechatMpCardApiCardDeleteRequest request)
  - POST /card/delete

- async WechatResponse CardDeleteRawAsync(string query, string jsonBody)

- async WechatMpCardApiCardGetResponse CardGetAsync(WechatMpCardApiCardGetRequest request)
  - POST /card/code/get

- async WechatResponse CardGetRawAsync(string query, string jsonBody)

- async WechatMpCardApiCardBatchGetResponse CardBatchGetAsync(WechatMpCardApiCardBatchGetRequest request)
  - POST /card/batchget

- async WechatResponse CardBatchGetRawAsync(string query, string jsonBody)

- async WechatMpCardApiCardDetailGetResponse CardDetailGetAsync(WechatMpCardApiCardDetailGetRequest request)
  - POST /card/get

- async WechatResponse CardDetailGetRawAsync(string query, string jsonBody)

- async WechatMpCardApiCardChangeCodeResponse CardChangeCodeAsync(WechatMpCardApiCardChangeCodeRequest request)
  - POST /card/code/update

- async WechatResponse CardChangeCodeRawAsync(string query, string jsonBody)

- async WechatMpCardApiCardUnavailableResponse CardUnavailableAsync(WechatMpCardApiCardUnavailableRequest request)
  - POST /card/code/unavailable

- async WechatResponse CardUnavailableRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetCardBizuinInfoResponse GetCardBizuinInfoAsync(WechatMpCardApiGetCardBizuinInfoRequest request)
  - POST /datacube/getcardbizuininfo

- async WechatResponse GetCardBizuinInfoRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetCardInfoResponse GetCardInfoAsync(WechatMpCardApiGetCardInfoRequest request)
  - POST /datacube/getcardcardinfo

- async WechatResponse GetCardInfoRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetCardMemberCardInfoResponse GetCardMemberCardInfoAsync(WechatMpCardApiGetCardMemberCardInfoRequest request)
  - POST /datacube/getcardmembercardinfo

- async WechatResponse GetCardMemberCardInfoRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetCardMemberCardDetailResponse GetCardMemberCardDetailAsync(WechatMpCardApiGetCardMemberCardDetailRequest request)
  - POST /datacube/getcardmembercarddetail

- async WechatResponse GetCardMemberCardDetailRawAsync(string query, string jsonBody)

- async WechatMpCardApiCardUpdateResponse CardUpdateAsync(WechatMpCardApiCardUpdateRequest request)
  - POST /card/update

- async WechatResponse CardUpdateRawAsync(string query, string jsonBody)

- async WechatMpCardApiAuthoritySetResponse AuthoritySetAsync(WechatMpCardApiAuthoritySetRequest request)
  - POST /card/testwhitelist/set

- async WechatResponse AuthoritySetRawAsync(string query, string jsonBody)

- async WechatMpCardApiMemberCardActivateResponse MemberCardActivateAsync(WechatMpCardApiMemberCardActivateRequest request)
  - POST /card/membercard/activate

- async WechatResponse MemberCardActivateRawAsync(string query, string jsonBody)

- async WechatMpCardApiActivateUserFormSetResponse ActivateUserFormSetAsync(WechatMpCardApiActivateUserFormSetRequest request)
  - POST /card/membercard/activateuserform/set

- async WechatResponse ActivateUserFormSetRawAsync(string query, string jsonBody)

- async WechatMpCardApiUserinfoGetResponse UserinfoGetAsync(WechatMpCardApiUserinfoGetRequest request)
  - POST /card/membercard/userinfo/get

- async WechatResponse UserinfoGetRawAsync(string query, string jsonBody)

- async WechatMpCardApiRecommendSetResponse RecommendSetAsync(WechatMpCardApiRecommendSetRequest request)
  - POST /card/update

- async WechatResponse RecommendSetRawAsync(string query, string jsonBody)

- async WechatMpCardApiPayCellSetResponse PayCellSetAsync(WechatMpCardApiPayCellSetRequest request)
  - POST /card/paycell/set

- async WechatResponse PayCellSetRawAsync(string query, string jsonBody)

- async WechatMpCardApiSelfConsumecellSetResponse SelfConsumecellSetAsync(WechatMpCardApiSelfConsumecellSetRequest request)
  - POST /card/selfconsumecell/set

- async WechatResponse SelfConsumecellSetRawAsync(string query, string jsonBody)

- async WechatMpCardApiUpdateUserResponse UpdateUserAsync(WechatMpCardApiUpdateUserRequest request)
  - POST /card/membercard/updateuser

- async WechatResponse UpdateUserRawAsync(string query, string jsonBody)

- async WechatMpCardApiMemberCardDealResponse MemberCardDealAsync(WechatMpCardApiMemberCardDealRequest request)
  - POST /card/membercard/updateuser

- async WechatResponse MemberCardDealRawAsync(string query, string jsonBody)

- async WechatMpCardApiMovieCardUpdateResponse MovieCardUpdateAsync(WechatMpCardApiMovieCardUpdateRequest request)
  - POST /card/movieticket/updateuser

- async WechatResponse MovieCardUpdateRawAsync(string query, string jsonBody)

- async WechatMpCardApiBoardingPassCheckInResponse BoardingPassCheckInAsync(WechatMpCardApiBoardingPassCheckInRequest request)
  - POST /card/boardingpass/checkin

- async WechatResponse BoardingPassCheckInRawAsync(string query, string jsonBody)

- async WechatMpCardApiUpdateUserBalanceResponse UpdateUserBalanceAsync(WechatMpCardApiUpdateUserBalanceRequest request)
  - POST /card/luckymoney/updateuserbalance

- async WechatResponse UpdateUserBalanceRawAsync(string query, string jsonBody)

- async WechatMpCardApiUpdateMeetingTicketResponse UpdateMeetingTicketAsync(WechatMpCardApiUpdateMeetingTicketRequest request)
  - POST /card/meetingticket/updateuser

- async WechatResponse UpdateMeetingTicketRawAsync(string query, string jsonBody)

- async WechatMpCardApiSubmerChantSubmitResponse SubmerChantSubmitAsync(WechatMpCardApiSubmerChantSubmitRequest request)
  - POST /card/submerchant/submit

- async WechatResponse SubmerChantSubmitRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetApplyProtocolResponse GetApplyProtocolAsync()
  - GET /card/getapplyprotocol

- async WechatResponse GetApplyProtocolRawAsync(string query)

- async WechatMpCardApiGetCardMerchantResponse GetCardMerchantAsync(WechatMpCardApiGetCardMerchantRequest request)
  - POST /cgi-bin/component/get_card_merchant

- async WechatResponse GetCardMerchantRawAsync(string query, string jsonBody)

- async WechatMpCardApiBatchGetCardMerchantResponse BatchGetCardMerchantAsync(WechatMpCardApiBatchGetCardMerchantRequest request)
  - POST /cgi-bin/component/batchget_card_merchant

- async WechatResponse BatchGetCardMerchantRawAsync(string query, string jsonBody)

- async WechatMpCardApiSubmerChantUpdateResponse SubmerChantUpdateAsync(WechatMpCardApiSubmerChantUpdateRequest request)
  - POST /card/submerchant/update

- async WechatResponse SubmerChantUpdateRawAsync(string query, string jsonBody)

- async WechatMpCardApiSubmerChantGetResponse SubmerChantGetAsync(WechatMpCardApiSubmerChantGetRequest request)
  - POST /card/submerchant/get

- async WechatResponse SubmerChantGetRawAsync(string query, string jsonBody)

- async WechatMpCardApiSubmerChantBatchGetResponse SubmerChantBatchGetAsync(WechatMpCardApiSubmerChantBatchGetRequest request)
  - POST /card/submerchant/batchget

- async WechatResponse SubmerChantBatchGetRawAsync(string query, string jsonBody)

- async WechatMpCardApiAgentQualificationResponse AgentQualificationAsync(WechatMpCardApiAgentQualificationRequest request)
  - POST /cgi-bin/component/upload_card_agent_qualification

- async WechatResponse AgentQualificationRawAsync(string query, string jsonBody)

- async WechatMpCardApiCheckAgentQualificationResponse CheckAgentQualificationAsync()
  - GET /cgi-bin/component/check_card_agent_qualification

- async WechatResponse CheckAgentQualificationRawAsync(string query)

- async WechatMpCardApiMerchantQualificationResponse MerchantQualificationAsync(WechatMpCardApiMerchantQualificationRequest request)
  - POST /cgi-bin/component/upload_card_merchant_qualification

- async WechatResponse MerchantQualificationRawAsync(string query, string jsonBody)

- async WechatMpCardApiCheckMerchantQualificationResponse CheckMerchantQualificationAsync(WechatMpCardApiCheckMerchantQualificationRequest request)
  - POST /cgi-bin/component/check_card_merchant_qualification

- async WechatResponse CheckMerchantQualificationRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetCardListResponse GetCardListAsync(WechatMpCardApiGetCardListRequest request)
  - POST /card/user/getcardlist

- async WechatResponse GetCardListRawAsync(string query, string jsonBody)

- async WechatMpCardApiModifyStockResponse ModifyStockAsync(WechatMpCardApiModifyStockRequest request)
  - POST /card/modifystock

- async WechatResponse ModifyStockRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetCardUrlResponse GetCardUrlAsync(WechatMpCardApiGetCardUrlRequest request)
  - POST /card/membercard/activate/geturl

- async WechatResponse GetCardUrlRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetActivateTempInfoResponse GetActivateTempInfoAsync(WechatMpCardApiGetActivateTempInfoRequest request)
  - POST /card/membercard/activatetempinfo/get

- async WechatResponse GetActivateTempInfoRawAsync(string query, string jsonBody)

- async WechatMpCardApiAddGiftCardPageResponse AddGiftCardPageAsync(WechatMpCardApiAddGiftCardPageRequest request)
  - POST /card/giftcard/page/add

- async WechatResponse AddGiftCardPageRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetGiftCardPageInfoResponse GetGiftCardPageInfoAsync(WechatMpCardApiGetGiftCardPageInfoRequest request)
  - POST /card/giftcard/page/get

- async WechatResponse GetGiftCardPageInfoRawAsync(string query, string jsonBody)

- async WechatMpCardApiUpdateGiftCardPageResponse UpdateGiftCardPageAsync(WechatMpCardApiUpdateGiftCardPageRequest request)
  - POST /card/giftcard/page/add

- async WechatResponse UpdateGiftCardPageRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetGiftCardPageListResponse GetGiftCardPageListAsync()
  - POST /card/giftcard/page/batchget

- async WechatResponse GetGiftCardPageListRawAsync(string query, string jsonBody)

- async WechatMpCardApiDownGiftCardPageResponse DownGiftCardPageAsync(WechatMpCardApiDownGiftCardPageRequest request)
  - POST /card/giftcard/maintain/set

- async WechatResponse DownGiftCardPageRawAsync(string query, string jsonBody)

- async WechatMpCardApiPayGiftCardResponse PayGiftCardAsync(WechatMpCardApiPayGiftCardRequest request)
  - POST /card/giftcard/pay/whitelist/add

- async WechatResponse PayGiftCardRawAsync(string query, string jsonBody)

- async WechatMpCardApiBindToGiftCardResponse BindToGiftCardAsync(WechatMpCardApiBindToGiftCardRequest request)
  - POST /card/giftcard/pay/submch/bind

- async WechatResponse BindToGiftCardRawAsync(string query, string jsonBody)

- async WechatMpCardApiUploadWxaCodeResponse UploadWxaCodeAsync(WechatMpCardApiUploadWxaCodeRequest request)
  - POST /card/giftcard/pay/wxa/set

- async WechatResponse UploadWxaCodeRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetGiftCardOrderInfoResponse GetGiftCardOrderInfoAsync(WechatMpCardApiGetGiftCardOrderInfoRequest request)
  - POST /card/giftcard/order/get

- async WechatResponse GetGiftCardOrderInfoRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetGiftCardOrderListInfoResponse GetGiftCardOrderListInfoAsync(WechatMpCardApiGetGiftCardOrderListInfoRequest request)
  - POST /card/giftcard/order/batchget

- async WechatResponse GetGiftCardOrderListInfoRawAsync(string query, string jsonBody)

- async WechatMpCardApiUpdateUserGiftCardResponse UpdateUserGiftCardAsync(WechatMpCardApiUpdateUserGiftCardRequest request)
  - POST /card/generalcard/updateuser

- async WechatResponse UpdateUserGiftCardRawAsync(string query, string jsonBody)

- async WechatMpCardApiRefundResponse RefundAsync(WechatMpCardApiRefundRequest request)
  - POST /card/giftcard/order/refund

- async WechatResponse RefundRawAsync(string query, string jsonBody)

- async WechatMpCardApiAddCardAfterPayResponse AddCardAfterPayAsync(WechatMpCardApiAddCardAfterPayRequest request)
  - POST /card/paygiftcard/add

- async WechatResponse AddCardAfterPayRawAsync(string query, string jsonBody)

- async WechatMpCardApiDeleteAfterPayRuleResponse DeleteAfterPayRuleAsync(WechatMpCardApiDeleteAfterPayRuleRequest request)
  - POST /card/paygiftcard/delete

- async WechatResponse DeleteAfterPayRuleRawAsync(string query, string jsonBody)

- async WechatMpCardApiAfterPay_GetByIdResponse AfterPay_GetByIdAsync(WechatMpCardApiAfterPay_GetByIdRequest request)
  - POST /card/paygiftcard/getbyid

- async WechatResponse AfterPay_GetByIdRawAsync(string query, string jsonBody)

- async WechatMpCardApiAfterPay_BatchGetResponse AfterPay_BatchGetAsync(WechatMpCardApiAfterPay_BatchGetRequest request)
  - POST /card/paygiftcard/batchget

- async WechatResponse AfterPay_BatchGetRawAsync(string query, string jsonBody)

- async WechatMpCardApiAddPayMemberRuleResponse AddPayMemberRuleAsync(WechatMpCardApiAddPayMemberRuleRequest request)
  - POST /card/paygiftmembercard/add

- async WechatResponse AddPayMemberRuleRawAsync(string query, string jsonBody)

- async WechatMpCardApiDeletePayMemberRuleResponse DeletePayMemberRuleAsync(WechatMpCardApiDeletePayMemberRuleRequest request)
  - POST /card/paygiftmembercard/delete

- async WechatResponse DeletePayMemberRuleRawAsync(string query, string jsonBody)

- async WechatMpCardApiGetPayMemberRuleResponse GetPayMemberRuleAsync(WechatMpCardApiGetPayMemberRuleRequest request)
  - POST /card/paygiftmembercard/get

- async WechatResponse GetPayMemberRuleRawAsync(string query, string jsonBody)

- async WechatMpCardApiCreateActivityResponse CreateActivityAsync(WechatMpCardApiCreateActivityRequest request)
  - POST /card/mkt/activity/create

- async WechatResponse CreateActivityRawAsync(string query, string jsonBody)

- async WechatMpCardApiApiQueryAuthResponse ApiQueryAuthAsync(WechatMpCardApiApiQueryAuthRequest request)
  - POST /cgi-bin/component/api_query_auth

- async WechatResponse ApiQueryAuthRawAsync(string query, string jsonBody)

- async WechatMpCardApiApiConfirmAuthorizationResponse ApiConfirmAuthorizationAsync(WechatMpCardApiApiConfirmAuthorizationRequest request)
  - POST /cgi-bin/component/api_confirm_authorization

- async WechatResponse ApiConfirmAuthorizationRawAsync(string query, string jsonBody)

- async WechatMpCardApiApiGetAuthorizerInfoResponse ApiGetAuthorizerInfoAsync(WechatMpCardApiApiGetAuthorizerInfoRequest request)
  - POST /cgi-bin/component/api_get_authorizer_info

- async WechatResponse ApiGetAuthorizerInfoRawAsync(string query, string jsonBody)


## WechatMpCardApiActivateUserFormSetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiActivateUserFormSetRequest()

- WechatMpCardApiActivateUserFormSetRequest CardId(string fieldValue)

- WechatMpCardApiActivateUserFormSetRequest RequiredForm(WechatMpBaseForm fieldValue)

- WechatMpCardApiActivateUserFormSetRequest OptionalForm(WechatMpBaseForm fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiActivateUserFormSetResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiAddCardAfterPayRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiAddCardAfterPayRequest()

- WechatMpCardApiAddCardAfterPayRequest RuleInfo(WechatMpRuleInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiAddCardAfterPayResponse (class)

- public string Raw;


## WechatMpCardApiAddGiftCardPageRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiAddGiftCardPageRequest()

- WechatMpCardApiAddGiftCardPageRequest PageId(int fieldValue)

- WechatMpCardApiAddGiftCardPageRequest PageTitle(string fieldValue)

- WechatMpCardApiAddGiftCardPageRequest SupportMulti(bool fieldValue)

- WechatMpCardApiAddGiftCardPageRequest SupportBuyForSelf(bool fieldValue)

- WechatMpCardApiAddGiftCardPageRequest BannerPicUrl(string fieldValue)

- WechatMpCardApiAddGiftCardPageRequest ThemeList(List<WechatMpGiftCardThemeItem> fieldValue)

- WechatMpCardApiAddGiftCardPageRequest CategoryList(List<WechatMpThemeCategoryItem> fieldValue)

- WechatMpCardApiAddGiftCardPageRequest Address(string fieldValue)

- WechatMpCardApiAddGiftCardPageRequest ServicePhone(string fieldValue)

- WechatMpCardApiAddGiftCardPageRequest BizDescription(string fieldValue)

- WechatMpCardApiAddGiftCardPageRequest NeedReceipt(bool fieldValue)

- WechatMpCardApiAddGiftCardPageRequest Cell1(WechatMpCell fieldValue)

- WechatMpCardApiAddGiftCardPageRequest Cell2(WechatMpCell fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiAddGiftCardPageResponse (class)

- public string Raw;


## WechatMpCardApiAddPayMemberRuleRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiAddPayMemberRuleRequest()

- WechatMpCardApiAddPayMemberRuleRequest CardId(string fieldValue)

- WechatMpCardApiAddPayMemberRuleRequest JumpUrl(string fieldValue)

- WechatMpCardApiAddPayMemberRuleRequest MchidList(List<string> fieldValue)

- WechatMpCardApiAddPayMemberRuleRequest BeginTime(int fieldValue)

- WechatMpCardApiAddPayMemberRuleRequest EndTime(int fieldValue)

- WechatMpCardApiAddPayMemberRuleRequest MinCost(int fieldValue)

- WechatMpCardApiAddPayMemberRuleRequest MaxCost(int fieldValue)

- WechatMpCardApiAddPayMemberRuleRequest IsLocked(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiAddPayMemberRuleResponse (class)

- public string Raw;


## WechatMpCardApiAfterPay_BatchGetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiAfterPay_BatchGetRequest()

- WechatMpCardApiAfterPay_BatchGetRequest Type(string fieldValue)

- WechatMpCardApiAfterPay_BatchGetRequest Effective(bool fieldValue)

- WechatMpCardApiAfterPay_BatchGetRequest Offset(int fieldValue)

- WechatMpCardApiAfterPay_BatchGetRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiAfterPay_BatchGetResponse (class)

- public string Raw;


## WechatMpCardApiAfterPay_GetByIdRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiAfterPay_GetByIdRequest()

- WechatMpCardApiAfterPay_GetByIdRequest RuleId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiAfterPay_GetByIdResponse (class)

- public string Raw;


## WechatMpCardApiAgentQualificationRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiAgentQualificationRequest()

- WechatMpCardApiAgentQualificationRequest RegisterCapital(string fieldValue)

- WechatMpCardApiAgentQualificationRequest BusinessLicenseMediaid(string fieldValue)

- WechatMpCardApiAgentQualificationRequest TaxRegistRationCertificateMediaid(string fieldValue)

- WechatMpCardApiAgentQualificationRequest LastQuarterTaxListingMediaid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiAgentQualificationResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiApiConfirmAuthorizationRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiApiConfirmAuthorizationRequest()

- WechatMpCardApiApiConfirmAuthorizationRequest ComponentAppid(string fieldValue)

- WechatMpCardApiApiConfirmAuthorizationRequest AuthorizerAppid(string fieldValue)

- WechatMpCardApiApiConfirmAuthorizationRequest FuncscopeCategoryId(int fieldValue)

- WechatMpCardApiApiConfirmAuthorizationRequest ConfirmValue(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiApiConfirmAuthorizationResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiApiGetAuthorizerInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiApiGetAuthorizerInfoRequest()

- WechatMpCardApiApiGetAuthorizerInfoRequest ComponentAppid(string fieldValue)

- WechatMpCardApiApiGetAuthorizerInfoRequest AuthorizerAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiApiGetAuthorizerInfoResponse (class)

- public string Raw;


## WechatMpCardApiApiQueryAuthRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiApiQueryAuthRequest()

- WechatMpCardApiApiQueryAuthRequest ComponentAppid(string fieldValue)

- WechatMpCardApiApiQueryAuthRequest AuthorizationCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiApiQueryAuthResponse (class)

- public string Raw;


## WechatMpCardApiAuthoritySetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiAuthoritySetRequest()

- WechatMpCardApiAuthoritySetRequest OpenIds(List<string> fieldValue)

- WechatMpCardApiAuthoritySetRequest UserNames(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiAuthoritySetResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiBatchGetCardMerchantRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiBatchGetCardMerchantRequest()

- WechatMpCardApiBatchGetCardMerchantRequest NextGet(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiBatchGetCardMerchantResponse (class)

- public string Raw;


## WechatMpCardApiBindToGiftCardRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiBindToGiftCardRequest()

- WechatMpCardApiBindToGiftCardRequest SubId(string fieldValue)

- WechatMpCardApiBindToGiftCardRequest WxaAppId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiBindToGiftCardResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiBoardingPassCheckInRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiBoardingPassCheckInRequest()

- WechatMpCardApiBoardingPassCheckInRequest Code(string fieldValue)

- WechatMpCardApiBoardingPassCheckInRequest CardId(string fieldValue)

- WechatMpCardApiBoardingPassCheckInRequest PassengerName(string fieldValue)

- WechatMpCardApiBoardingPassCheckInRequest ClassType(string fieldValue)

- WechatMpCardApiBoardingPassCheckInRequest Seat(string fieldValue)

- WechatMpCardApiBoardingPassCheckInRequest EtktBnr(string fieldValue)

- WechatMpCardApiBoardingPassCheckInRequest QrcodeData(string fieldValue)

- WechatMpCardApiBoardingPassCheckInRequest IsCancel(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiBoardingPassCheckInResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiCardBatchGetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCardBatchGetRequest()

- WechatMpCardApiCardBatchGetRequest Offset(int fieldValue)

- WechatMpCardApiCardBatchGetRequest Count(int fieldValue)

- WechatMpCardApiCardBatchGetRequest StatusList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCardBatchGetResponse (class)

- public string Raw;


## WechatMpCardApiCardChangeCodeRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCardChangeCodeRequest()

- WechatMpCardApiCardChangeCodeRequest Code(string fieldValue)

- WechatMpCardApiCardChangeCodeRequest CardId(string fieldValue)

- WechatMpCardApiCardChangeCodeRequest NewCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCardChangeCodeResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiCardConsumeRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCardConsumeRequest()

- WechatMpCardApiCardConsumeRequest Code(string fieldValue)

- WechatMpCardApiCardConsumeRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCardConsumeResponse (class)

- public string Raw;


## WechatMpCardApiCardDecryptRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCardDecryptRequest()

- WechatMpCardApiCardDecryptRequest EncryptCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCardDecryptResponse (class)

- public string Raw;


## WechatMpCardApiCardDeleteRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCardDeleteRequest()

- WechatMpCardApiCardDeleteRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCardDeleteResponse (class)

- public string Raw;


## WechatMpCardApiCardDetailGetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCardDetailGetRequest()

- WechatMpCardApiCardDetailGetRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCardDetailGetResponse (class)

- public string Raw;


## WechatMpCardApiCardGetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCardGetRequest()

- WechatMpCardApiCardGetRequest Code(string fieldValue)

- WechatMpCardApiCardGetRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCardGetResponse (class)

- public string Raw;


## WechatMpCardApiCardUnavailableRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCardUnavailableRequest()

- WechatMpCardApiCardUnavailableRequest Code(string fieldValue)

- WechatMpCardApiCardUnavailableRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCardUnavailableResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiCardUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCardUpdateRequest()

- WechatMpCardApiCardUpdateRequest CardType(int fieldValue)

- WechatMpCardApiCardUpdateRequest Data(JsonValue fieldValue)

- WechatMpCardApiCardUpdateRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCardUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiCheckAgentQualificationResponse (class)

- public string Raw;


## WechatMpCardApiCheckCodeRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCheckCodeRequest()

- WechatMpCardApiCheckCodeRequest CardId(string fieldValue)

- WechatMpCardApiCheckCodeRequest CodeList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCheckCodeResponse (class)

- public string Raw;


## WechatMpCardApiCheckMerchantQualificationRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCheckMerchantQualificationRequest()

- WechatMpCardApiCheckMerchantQualificationRequest Appid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCheckMerchantQualificationResponse (class)

- public string Raw;


## WechatMpCardApiCodeDepositRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCodeDepositRequest()

- WechatMpCardApiCodeDepositRequest CardId(string fieldValue)

- WechatMpCardApiCodeDepositRequest CodeList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCodeDepositResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiCodeMarkRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCodeMarkRequest()

- WechatMpCardApiCodeMarkRequest Code(string fieldValue)

- WechatMpCardApiCodeMarkRequest CardId(string fieldValue)

- WechatMpCardApiCodeMarkRequest OpenId(string fieldValue)

- WechatMpCardApiCodeMarkRequest IsMark(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCodeMarkResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiCreateActivityRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCreateActivityRequest()

- WechatMpCardApiCreateActivityRequest Info(WechatMpCardGiftcardMktActivityDataInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCreateActivityResponse (class)

- public string Raw;


## WechatMpCardApiCreateCardRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpCardApiCreateCardRequest()

- WechatMpCardApiCreateCardRequest BaseInfo(WechatMpCardBaseInfoBase fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCreateCardResponse (class)

- public string Raw;


## WechatMpCardApiCreateQRRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiCreateQRRequest()

- WechatMpCardApiCreateQRRequest CardId(string fieldValue)

- WechatMpCardApiCreateQRRequest Code(string fieldValue)

- WechatMpCardApiCreateQRRequest OpenId(string fieldValue)

- WechatMpCardApiCreateQRRequest OuterId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiCreateQRResponse (class)

- public string Raw;


## WechatMpCardApiDeleteAfterPayRuleRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiDeleteAfterPayRuleRequest()

- WechatMpCardApiDeleteAfterPayRuleRequest RuleId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiDeleteAfterPayRuleResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiDeletePayMemberRuleRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiDeletePayMemberRuleRequest()

- WechatMpCardApiDeletePayMemberRuleRequest CardId(string fieldValue)

- WechatMpCardApiDeletePayMemberRuleRequest MchidList(List<int> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiDeletePayMemberRuleResponse (class)

- public string Raw;


## WechatMpCardApiDownGiftCardPageRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiDownGiftCardPageRequest()

- WechatMpCardApiDownGiftCardPageRequest PageId(string fieldValue)

- WechatMpCardApiDownGiftCardPageRequest All(bool fieldValue)

- WechatMpCardApiDownGiftCardPageRequest Maintain(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiDownGiftCardPageResponse (class)

- public string Raw;


## WechatMpCardApiGetActivateTempInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetActivateTempInfoRequest()

- WechatMpCardApiGetActivateTempInfoRequest ActivateTicket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetActivateTempInfoResponse (class)

- public string Raw;


## WechatMpCardApiGetApplyProtocolResponse (class)

- public string Raw;


## WechatMpCardApiGetCardBizuinInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetCardBizuinInfoRequest()

- WechatMpCardApiGetCardBizuinInfoRequest BeginDate(string fieldValue)

- WechatMpCardApiGetCardBizuinInfoRequest EndDate(string fieldValue)

- WechatMpCardApiGetCardBizuinInfoRequest CondSource(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetCardBizuinInfoResponse (class)

- public string Raw;


## WechatMpCardApiGetCardInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetCardInfoRequest()

- WechatMpCardApiGetCardInfoRequest BeginDate(string fieldValue)

- WechatMpCardApiGetCardInfoRequest EndDate(string fieldValue)

- WechatMpCardApiGetCardInfoRequest CondSource(int fieldValue)

- WechatMpCardApiGetCardInfoRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetCardInfoResponse (class)

- public string Raw;


## WechatMpCardApiGetCardListRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetCardListRequest()

- WechatMpCardApiGetCardListRequest OpenId(string fieldValue)

- WechatMpCardApiGetCardListRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetCardListResponse (class)

- public string Raw;


## WechatMpCardApiGetCardMemberCardDetailRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetCardMemberCardDetailRequest()

- WechatMpCardApiGetCardMemberCardDetailRequest BeginDate(string fieldValue)

- WechatMpCardApiGetCardMemberCardDetailRequest EndDate(string fieldValue)

- WechatMpCardApiGetCardMemberCardDetailRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetCardMemberCardDetailResponse (class)

- public string Raw;


## WechatMpCardApiGetCardMemberCardInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetCardMemberCardInfoRequest()

- WechatMpCardApiGetCardMemberCardInfoRequest BeginDate(string fieldValue)

- WechatMpCardApiGetCardMemberCardInfoRequest EndDate(string fieldValue)

- WechatMpCardApiGetCardMemberCardInfoRequest CondSource(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetCardMemberCardInfoResponse (class)

- public string Raw;


## WechatMpCardApiGetCardMerchantRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetCardMerchantRequest()

- WechatMpCardApiGetCardMerchantRequest Appid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetCardMerchantResponse (class)

- public string Raw;


## WechatMpCardApiGetCardUrlRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetCardUrlRequest()

- WechatMpCardApiGetCardUrlRequest CardId(string fieldValue)

- WechatMpCardApiGetCardUrlRequest OuterStr(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetCardUrlResponse (class)

- public string Raw;


## WechatMpCardApiGetCoinsInfoResponse (class)

- public string Raw;


## WechatMpCardApiGetDepositCountRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetDepositCountRequest()

- WechatMpCardApiGetDepositCountRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetDepositCountResponse (class)

- public string Raw;


## WechatMpCardApiGetGiftCardOrderInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetGiftCardOrderInfoRequest()

- WechatMpCardApiGetGiftCardOrderInfoRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetGiftCardOrderInfoResponse (class)

- public string Raw;


## WechatMpCardApiGetGiftCardOrderListInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetGiftCardOrderListInfoRequest()

- WechatMpCardApiGetGiftCardOrderListInfoRequest BeginTime(string fieldValue)

- WechatMpCardApiGetGiftCardOrderListInfoRequest EndTime(string fieldValue)

- WechatMpCardApiGetGiftCardOrderListInfoRequest SortType(int fieldValue)

- WechatMpCardApiGetGiftCardOrderListInfoRequest OffSet(int fieldValue)

- WechatMpCardApiGetGiftCardOrderListInfoRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetGiftCardOrderListInfoResponse (class)

- public string Raw;


## WechatMpCardApiGetGiftCardPageInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetGiftCardPageInfoRequest()

- WechatMpCardApiGetGiftCardPageInfoRequest PageId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetGiftCardPageInfoResponse (class)

- public string Raw;


## WechatMpCardApiGetGiftCardPageListResponse (class)

- public string Raw;


## WechatMpCardApiGetHtmlRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetHtmlRequest()

- WechatMpCardApiGetHtmlRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetHtmlResponse (class)

- public string Raw;


## WechatMpCardApiGetOrderListRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetOrderListRequest()

- WechatMpCardApiGetOrderListRequest Status(string fieldValue)

- WechatMpCardApiGetOrderListRequest SortKey(string fieldValue)

- WechatMpCardApiGetOrderListRequest SortType(string fieldValue)

- WechatMpCardApiGetOrderListRequest Offset(int fieldValue)

- WechatMpCardApiGetOrderListRequest Count(int fieldValue)

- WechatMpCardApiGetOrderListRequest OrderType(string fieldValue)

- WechatMpCardApiGetOrderListRequest BeginTime(int fieldValue)

- WechatMpCardApiGetOrderListRequest EndTime(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetOrderListResponse (class)

- public string Raw;


## WechatMpCardApiGetPayMemberRuleRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetPayMemberRuleRequest()

- WechatMpCardApiGetPayMemberRuleRequest MchId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetPayMemberRuleResponse (class)

- public string Raw;


## WechatMpCardApiGetpayPriceRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiGetpayPriceRequest()

- WechatMpCardApiGetpayPriceRequest CardId(string fieldValue)

- WechatMpCardApiGetpayPriceRequest Quantity(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiGetpayPriceResponse (class)

- public string Raw;


## WechatMpCardApiMemberCardActivateRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiMemberCardActivateRequest()

- WechatMpCardApiMemberCardActivateRequest InitBonus(string fieldValue)

- WechatMpCardApiMemberCardActivateRequest InitBalance(string fieldValue)

- WechatMpCardApiMemberCardActivateRequest MembershipNumber(string fieldValue)

- WechatMpCardApiMemberCardActivateRequest Code(string fieldValue)

- WechatMpCardApiMemberCardActivateRequest CardId(string fieldValue)

- WechatMpCardApiMemberCardActivateRequest ActivateBeginTime(string fieldValue)

- WechatMpCardApiMemberCardActivateRequest ActivateEndTime(string fieldValue)

- WechatMpCardApiMemberCardActivateRequest InitCustomFieldValue1(string fieldValue)

- WechatMpCardApiMemberCardActivateRequest InitCustomFieldValue2(string fieldValue)

- WechatMpCardApiMemberCardActivateRequest InitCustomFieldValue3(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiMemberCardActivateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiMemberCardDealRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiMemberCardDealRequest()

- WechatMpCardApiMemberCardDealRequest Code(string fieldValue)

- WechatMpCardApiMemberCardDealRequest CardId(string fieldValue)

- WechatMpCardApiMemberCardDealRequest RecordBonus(string fieldValue)

- WechatMpCardApiMemberCardDealRequest AddBonus(double fieldValue)

- WechatMpCardApiMemberCardDealRequest AddBalance(double fieldValue)

- WechatMpCardApiMemberCardDealRequest RecordBalance(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiMemberCardDealResponse (class)

- public string Raw;


## WechatMpCardApiMerchantQualificationRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiMerchantQualificationRequest()

- WechatMpCardApiMerchantQualificationRequest Appid(string fieldValue)

- WechatMpCardApiMerchantQualificationRequest Name(string fieldValue)

- WechatMpCardApiMerchantQualificationRequest LogoMediaid(string fieldValue)

- WechatMpCardApiMerchantQualificationRequest BusinessLicenseMediaid(string fieldValue)

- WechatMpCardApiMerchantQualificationRequest OperatorIdCardMediaid(string fieldValue)

- WechatMpCardApiMerchantQualificationRequest AgreementFileMediaid(string fieldValue)

- WechatMpCardApiMerchantQualificationRequest PrimaryCategoryId(string fieldValue)

- WechatMpCardApiMerchantQualificationRequest SecondaryCategoryId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiMerchantQualificationResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiModifyStockRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiModifyStockRequest()

- WechatMpCardApiModifyStockRequest CardId(string fieldValue)

- WechatMpCardApiModifyStockRequest IncreaseStockValue(int fieldValue)

- WechatMpCardApiModifyStockRequest ReduceStockValue(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiModifyStockResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiMovieCardUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiMovieCardUpdateRequest()

- WechatMpCardApiMovieCardUpdateRequest Code(string fieldValue)

- WechatMpCardApiMovieCardUpdateRequest CardId(string fieldValue)

- WechatMpCardApiMovieCardUpdateRequest TicketClass(string fieldValue)

- WechatMpCardApiMovieCardUpdateRequest ShowTime(string fieldValue)

- WechatMpCardApiMovieCardUpdateRequest Duration(int fieldValue)

- WechatMpCardApiMovieCardUpdateRequest ScreeningRoom(string fieldValue)

- WechatMpCardApiMovieCardUpdateRequest SeatNumbers(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiMovieCardUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiPayActiveResponse (class)

- public string Raw;


## WechatMpCardApiPayCellSetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiPayCellSetRequest()

- WechatMpCardApiPayCellSetRequest CardId(string fieldValue)

- WechatMpCardApiPayCellSetRequest IsOpen(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiPayCellSetResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiPayConfirmRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiPayConfirmRequest()

- WechatMpCardApiPayConfirmRequest CardId(string fieldValue)

- WechatMpCardApiPayConfirmRequest Quantity(int fieldValue)

- WechatMpCardApiPayConfirmRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiPayConfirmResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiPayGetOrderRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiPayGetOrderRequest()

- WechatMpCardApiPayGetOrderRequest OrderId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiPayGetOrderResponse (class)

- public string Raw;


## WechatMpCardApiPayGiftCardRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiPayGiftCardRequest()

- WechatMpCardApiPayGiftCardRequest SubId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiPayGiftCardResponse (class)

- public string Raw;


## WechatMpCardApiPayRechargeRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiPayRechargeRequest()

- WechatMpCardApiPayRechargeRequest CoinCount(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiPayRechargeResponse (class)

- public string Raw;


## WechatMpCardApiRecommendSetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiRecommendSetRequest()

- WechatMpCardApiRecommendSetRequest EndTime(long fieldValue)

- WechatMpCardApiRecommendSetRequest CardId(string fieldValue)

- WechatMpCardApiRecommendSetRequest Text(string fieldValue)

- WechatMpCardApiRecommendSetRequest Url(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiRecommendSetResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiRefundRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiRefundRequest()

- WechatMpCardApiRefundRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiRefundResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiSelfConsumecellSetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiSelfConsumecellSetRequest()

- WechatMpCardApiSelfConsumecellSetRequest CardId(string fieldValue)

- WechatMpCardApiSelfConsumecellSetRequest IsOpen(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiSelfConsumecellSetResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiShelfCreateRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiShelfCreateRequest()

- WechatMpCardApiShelfCreateRequest Banner(string fieldValue)

- WechatMpCardApiShelfCreateRequest PageTitle(string fieldValue)

- WechatMpCardApiShelfCreateRequest CanShare(bool fieldValue)

- WechatMpCardApiShelfCreateRequest Scene(int fieldValue)

- WechatMpCardApiShelfCreateRequest CardList(List<WechatMpShelfCreateDataCardList> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiShelfCreateResponse (class)

- public string Raw;


## WechatMpCardApiSubmerChantBatchGetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiSubmerChantBatchGetRequest()

- WechatMpCardApiSubmerChantBatchGetRequest BeginId(string fieldValue)

- WechatMpCardApiSubmerChantBatchGetRequest Limit(int fieldValue)

- WechatMpCardApiSubmerChantBatchGetRequest Status(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiSubmerChantBatchGetResponse (class)

- public string Raw;


## WechatMpCardApiSubmerChantGetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiSubmerChantGetRequest()

- WechatMpCardApiSubmerChantGetRequest MerchantId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiSubmerChantGetResponse (class)

- public string Raw;


## WechatMpCardApiSubmerChantSubmitRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiSubmerChantSubmitRequest()

- WechatMpCardApiSubmerChantSubmitRequest BrandName(string fieldValue)

- WechatMpCardApiSubmerChantSubmitRequest AppId(string fieldValue)

- WechatMpCardApiSubmerChantSubmitRequest LogoUrl(string fieldValue)

- WechatMpCardApiSubmerChantSubmitRequest Protocol(string fieldValue)

- WechatMpCardApiSubmerChantSubmitRequest AgreementMediaId(string fieldValue)

- WechatMpCardApiSubmerChantSubmitRequest OperatorMediaId(string fieldValue)

- WechatMpCardApiSubmerChantSubmitRequest EndTime(string fieldValue)

- WechatMpCardApiSubmerChantSubmitRequest PrimaryCategoryId(string fieldValue)

- WechatMpCardApiSubmerChantSubmitRequest SecondaryCategoryId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiSubmerChantSubmitResponse (class)

- public string Raw;


## WechatMpCardApiSubmerChantUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiSubmerChantUpdateRequest()

- WechatMpCardApiSubmerChantUpdateRequest BrandName(string fieldValue)

- WechatMpCardApiSubmerChantUpdateRequest AppId(string fieldValue)

- WechatMpCardApiSubmerChantUpdateRequest LogoUrl(string fieldValue)

- WechatMpCardApiSubmerChantUpdateRequest Protocol(string fieldValue)

- WechatMpCardApiSubmerChantUpdateRequest AgreementMediaId(string fieldValue)

- WechatMpCardApiSubmerChantUpdateRequest OperatorMediaId(string fieldValue)

- WechatMpCardApiSubmerChantUpdateRequest EndTime(string fieldValue)

- WechatMpCardApiSubmerChantUpdateRequest PrimaryCategoryId(string fieldValue)

- WechatMpCardApiSubmerChantUpdateRequest SecondaryCategoryId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiSubmerChantUpdateResponse (class)

- public string Raw;


## WechatMpCardApiUpdateGiftCardPageRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiUpdateGiftCardPageRequest()

- WechatMpCardApiUpdateGiftCardPageRequest PageId(int fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest PageTitle(string fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest SupportMulti(bool fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest SupportBuyForSelf(bool fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest BannerPicUrl(string fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest ThemeList(List<WechatMpGiftCardThemeItem> fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest CategoryList(List<WechatMpThemeCategoryItem> fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest Address(string fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest ServicePhone(string fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest BizDescription(string fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest NeedReceipt(bool fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest Cell1(WechatMpCell fieldValue)

- WechatMpCardApiUpdateGiftCardPageRequest Cell2(WechatMpCell fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiUpdateGiftCardPageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiUpdateMeetingTicketRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiUpdateMeetingTicketRequest()

- WechatMpCardApiUpdateMeetingTicketRequest Code(string fieldValue)

- WechatMpCardApiUpdateMeetingTicketRequest CardId(string fieldValue)

- WechatMpCardApiUpdateMeetingTicketRequest Zone(string fieldValue)

- WechatMpCardApiUpdateMeetingTicketRequest Entrance(string fieldValue)

- WechatMpCardApiUpdateMeetingTicketRequest SeatNumber(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiUpdateMeetingTicketResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiUpdateUserBalanceRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiUpdateUserBalanceRequest()

- WechatMpCardApiUpdateUserBalanceRequest Code(string fieldValue)

- WechatMpCardApiUpdateUserBalanceRequest CardId(string fieldValue)

- WechatMpCardApiUpdateUserBalanceRequest Balance(double fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiUpdateUserBalanceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiUpdateUserGiftCardRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiUpdateUserGiftCardRequest()

- WechatMpCardApiUpdateUserGiftCardRequest Code(string fieldValue)

- WechatMpCardApiUpdateUserGiftCardRequest CardId(string fieldValue)

- WechatMpCardApiUpdateUserGiftCardRequest BackgroundPicUrl(string fieldValue)

- WechatMpCardApiUpdateUserGiftCardRequest RecordBonus(string fieldValue)

- WechatMpCardApiUpdateUserGiftCardRequest Bonus(int fieldValue)

- WechatMpCardApiUpdateUserGiftCardRequest CustomFieldValue1(string fieldValue)

- WechatMpCardApiUpdateUserGiftCardRequest CustomFieldValue2(string fieldValue)

- WechatMpCardApiUpdateUserGiftCardRequest CustomFieldValue3(string fieldValue)

- WechatMpCardApiUpdateUserGiftCardRequest CanGiveFriend(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiUpdateUserGiftCardResponse (class)

- public string Raw;


## WechatMpCardApiUpdateUserRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiUpdateUserRequest()

- WechatMpCardApiUpdateUserRequest Code(string fieldValue)

- WechatMpCardApiUpdateUserRequest CardId(string fieldValue)

- WechatMpCardApiUpdateUserRequest BackgroundPicUrl(string fieldValue)

- WechatMpCardApiUpdateUserRequest AddBonus(int fieldValue)

- WechatMpCardApiUpdateUserRequest Bonus(int fieldValue)

- WechatMpCardApiUpdateUserRequest RecordBonus(string fieldValue)

- WechatMpCardApiUpdateUserRequest AddBalance(int fieldValue)

- WechatMpCardApiUpdateUserRequest Balance(int fieldValue)

- WechatMpCardApiUpdateUserRequest RecordBalance(string fieldValue)

- WechatMpCardApiUpdateUserRequest CustomFieldValue1(string fieldValue)

- WechatMpCardApiUpdateUserRequest CustomFieldValue2(string fieldValue)

- WechatMpCardApiUpdateUserRequest CustomFieldValue3(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiUpdateUserResponse (class)

- public string Raw;


## WechatMpCardApiUploadWxaCodeRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiUploadWxaCodeRequest()

- WechatMpCardApiUploadWxaCodeRequest WxaAppId(string fieldValue)

- WechatMpCardApiUploadWxaCodeRequest PageId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiUploadWxaCodeResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardApiUserinfoGetRequest (class)

- WechatTypedRequest request;

- public WechatMpCardApiUserinfoGetRequest()

- WechatMpCardApiUserinfoGetRequest CardId(string fieldValue)

- WechatMpCardApiUserinfoGetRequest Code(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardApiUserinfoGetResponse (class)

- public string Raw;


## WechatMpCardStoreApi (class)

Card/Store/StoreApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpCardStoreApi(WechatClient client)

- async WechatMpCardStoreApiGetMerchantCategoryResponse GetMerchantCategoryAsync()
  - GET /wxa/get_merchant_category

- async WechatResponse GetMerchantCategoryRawAsync(string query)

- async WechatMpCardStoreApiApplyMerchantResponse ApplyMerchantAsync(WechatMpCardStoreApiApplyMerchantRequest request)
  - POST /wxa/apply_merchant

- async WechatResponse ApplyMerchantRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiGetMerchantAuditInfoResponse GetMerchantAuditInfoAsync()
  - GET /wxa/get_merchant_audit_info

- async WechatResponse GetMerchantAuditInfoRawAsync(string query)

- async WechatMpCardStoreApiModifyMerchantResponse ModifyMerchantAsync(WechatMpCardStoreApiModifyMerchantRequest request)
  - POST /wxa/modify_merchant

- async WechatResponse ModifyMerchantRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiGetDistrictResponse GetDistrictAsync()
  - GET /wxa/get_district

- async WechatResponse GetDistrictRawAsync(string query)

- async WechatMpCardStoreApiSearchMapPoiResponse SearchMapPoiAsync(WechatMpCardStoreApiSearchMapPoiRequest request)
  - POST /wxa/search_map_poi

- async WechatResponse SearchMapPoiRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiCreateMapPoiResponse CreateMapPoiAsync(WechatMpCardStoreApiCreateMapPoiRequest request)
  - POST /wxa/create_map_poi

- async WechatResponse CreateMapPoiRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiAddStoreResponse AddStoreAsync(WechatMpCardStoreApiAddStoreRequest request)
  - POST /wxa/add_store

- async WechatResponse AddStoreRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiUpdateStoreResponse UpdateStoreAsync(WechatMpCardStoreApiUpdateStoreRequest request)
  - POST /wxa/update_store

- async WechatResponse UpdateStoreRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiGetStoreInfoResponse GetStoreInfoAsync(WechatMpCardStoreApiGetStoreInfoRequest request)
  - POST /wxa/get_store_info

- async WechatResponse GetStoreInfoRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiGetStoreListResponse GetStoreListAsync(WechatMpCardStoreApiGetStoreListRequest request)
  - POST /wxa/get_store_list

- async WechatResponse GetStoreListRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiDelStoreResponse DelStoreAsync(WechatMpCardStoreApiDelStoreRequest request)
  - POST /wxa/del_store

- async WechatResponse DelStoreRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiGetStoreCardResponse GetStoreCardAsync(WechatMpCardStoreApiGetStoreCardRequest request)
  - POST /card/storewxa/get

- async WechatResponse GetStoreCardRawAsync(string query, string jsonBody)

- async WechatMpCardStoreApiSetStoreCardResponse SetStoreCardAsync(WechatMpCardStoreApiSetStoreCardRequest request)
  - POST /card/storewxa/set

- async WechatResponse SetStoreCardRawAsync(string query, string jsonBody)


## WechatMpCardStoreApiAddStoreRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiAddStoreRequest()

- WechatMpCardStoreApiAddStoreRequest PoiId(string fieldValue)

- WechatMpCardStoreApiAddStoreRequest MapPoiId(string fieldValue)

- WechatMpCardStoreApiAddStoreRequest PicList(string fieldValue)

- WechatMpCardStoreApiAddStoreRequest ContractPhone(string fieldValue)

- WechatMpCardStoreApiAddStoreRequest Credential(string fieldValue)

- WechatMpCardStoreApiAddStoreRequest QualificationList(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiAddStoreResponse (class)

- public string Raw;


## WechatMpCardStoreApiApplyMerchantRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiApplyMerchantRequest()

- WechatMpCardStoreApiApplyMerchantRequest FirstCatid(int fieldValue)

- WechatMpCardStoreApiApplyMerchantRequest SecondCatid(int fieldValue)

- WechatMpCardStoreApiApplyMerchantRequest QualificationList(string fieldValue)

- WechatMpCardStoreApiApplyMerchantRequest HeadimgMediaid(string fieldValue)

- WechatMpCardStoreApiApplyMerchantRequest Nickname(string fieldValue)

- WechatMpCardStoreApiApplyMerchantRequest Intro(string fieldValue)

- WechatMpCardStoreApiApplyMerchantRequest OrgCode(string fieldValue)

- WechatMpCardStoreApiApplyMerchantRequest OtherFiles(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiApplyMerchantResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardStoreApiCreateMapPoiRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiCreateMapPoiRequest()

- WechatMpCardStoreApiCreateMapPoiRequest Name(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest Longitude(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest Latitude(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest Province(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest City(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest District(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest Address(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest Category(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest Telephone(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest Photo(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest License(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest Introduct(string fieldValue)

- WechatMpCardStoreApiCreateMapPoiRequest Districtid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiCreateMapPoiResponse (class)

- public string Raw;


## WechatMpCardStoreApiDelStoreRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiDelStoreRequest()

- WechatMpCardStoreApiDelStoreRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiDelStoreResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardStoreApiGetDistrictResponse (class)

- public string Raw;


## WechatMpCardStoreApiGetMerchantAuditInfoResponse (class)

- public string Raw;


## WechatMpCardStoreApiGetMerchantCategoryResponse (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- public string Raw;


## WechatMpCardStoreApiGetStoreCardRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiGetStoreCardRequest()

- WechatMpCardStoreApiGetStoreCardRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiGetStoreCardResponse (class)

- public string Raw;


## WechatMpCardStoreApiGetStoreInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiGetStoreInfoRequest()

- WechatMpCardStoreApiGetStoreInfoRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiGetStoreInfoResponse (class)

- public string Raw;


## WechatMpCardStoreApiGetStoreListRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiGetStoreListRequest()

- WechatMpCardStoreApiGetStoreListRequest Offset(int fieldValue)

- WechatMpCardStoreApiGetStoreListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiGetStoreListResponse (class)

- public string Raw;


## WechatMpCardStoreApiModifyMerchantRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiModifyMerchantRequest()

- WechatMpCardStoreApiModifyMerchantRequest HeadingMediaid(string fieldValue)

- WechatMpCardStoreApiModifyMerchantRequest Intro(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiModifyMerchantResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardStoreApiSearchMapPoiRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiSearchMapPoiRequest()

- WechatMpCardStoreApiSearchMapPoiRequest DistrictId(string fieldValue)

- WechatMpCardStoreApiSearchMapPoiRequest Keyword(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiSearchMapPoiResponse (class)

- public string Raw;


## WechatMpCardStoreApiSetStoreCardRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiSetStoreCardRequest()

- WechatMpCardStoreApiSetStoreCardRequest PoiId(string fieldValue)

- WechatMpCardStoreApiSetStoreCardRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiSetStoreCardResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCardStoreApiUpdateStoreRequest (class)

- WechatTypedRequest request;

- public WechatMpCardStoreApiUpdateStoreRequest()

- WechatMpCardStoreApiUpdateStoreRequest MapPoiId(string fieldValue)

- WechatMpCardStoreApiUpdateStoreRequest PoiId(string fieldValue)

- WechatMpCardStoreApiUpdateStoreRequest Hour(string fieldValue)

- WechatMpCardStoreApiUpdateStoreRequest ContractPhone(string fieldValue)

- WechatMpCardStoreApiUpdateStoreRequest PicList(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCardStoreApiUpdateStoreResponse (class)

- public string Raw;


## WechatMpCommentApi (class)

Comment/CommentApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpCommentApi(WechatClient client)

- async WechatMpCommentApiOpenResponse OpenAsync(WechatMpCommentApiOpenRequest request)
  - POST /cgi-bin/comment/open

- async WechatResponse OpenRawAsync(string query, string jsonBody)

- async WechatMpCommentApiCloseResponse CloseAsync(WechatMpCommentApiCloseRequest request)
  - POST /cgi-bin/comment/close

- async WechatResponse CloseRawAsync(string query, string jsonBody)

- async WechatMpCommentApiListResponse ListAsync(WechatMpCommentApiListRequest request)
  - POST /cgi-bin/comment/list

- async WechatResponse ListRawAsync(string query, string jsonBody)

- async WechatMpCommentApiMarkElectResponse MarkElectAsync(WechatMpCommentApiMarkElectRequest request)
  - POST /cgi-bin/comment/markelect

- async WechatResponse MarkElectRawAsync(string query, string jsonBody)

- async WechatMpCommentApiUnmarkElectResponse UnmarkElectAsync(WechatMpCommentApiUnmarkElectRequest request)
  - POST /cgi-bin/comment/unmarkelect

- async WechatResponse UnmarkElectRawAsync(string query, string jsonBody)

- async WechatMpCommentApiDeleteResponse DeleteAsync(WechatMpCommentApiDeleteRequest request)
  - POST /cgi-bin/comment/delete

- async WechatResponse DeleteRawAsync(string query, string jsonBody)

- async WechatMpCommentApiReplyAddResponse ReplyAddAsync(WechatMpCommentApiReplyAddRequest request)
  - POST /cgi-bin/comment/reply/add

- async WechatResponse ReplyAddRawAsync(string query, string jsonBody)

- async WechatMpCommentApiReplyDeleteResponse ReplyDeleteAsync(WechatMpCommentApiReplyDeleteRequest request)
  - POST /cgi-bin/comment/reply/delete

- async WechatResponse ReplyDeleteRawAsync(string query, string jsonBody)


## WechatMpCommentApiCloseRequest (class)

- WechatTypedRequest request;

- public WechatMpCommentApiCloseRequest()

- WechatMpCommentApiCloseRequest MsgDataId(long fieldValue)

- WechatMpCommentApiCloseRequest Index(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCommentApiCloseResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCommentApiDeleteRequest (class)

- WechatTypedRequest request;

- public WechatMpCommentApiDeleteRequest()

- WechatMpCommentApiDeleteRequest MsgDataId(long fieldValue)

- WechatMpCommentApiDeleteRequest Index(long fieldValue)

- WechatMpCommentApiDeleteRequest UserCommentId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCommentApiDeleteResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCommentApiListRequest (class)

- WechatTypedRequest request;

- public WechatMpCommentApiListRequest()

- WechatMpCommentApiListRequest MsgDataId(long fieldValue)

- WechatMpCommentApiListRequest Index(long fieldValue)

- WechatMpCommentApiListRequest Begin(long fieldValue)

- WechatMpCommentApiListRequest Count(long fieldValue)

- WechatMpCommentApiListRequest Type(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCommentApiListResponse (class)

- public string Raw;


## WechatMpCommentApiMarkElectRequest (class)

- WechatTypedRequest request;

- public WechatMpCommentApiMarkElectRequest()

- WechatMpCommentApiMarkElectRequest MsgDataId(long fieldValue)

- WechatMpCommentApiMarkElectRequest Index(long fieldValue)

- WechatMpCommentApiMarkElectRequest UserCommentId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCommentApiMarkElectResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCommentApiOpenRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpCommentApiOpenRequest()

- WechatMpCommentApiOpenRequest MsgDataId(long fieldValue)

- WechatMpCommentApiOpenRequest Index(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCommentApiOpenResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCommentApiReplyAddRequest (class)

- WechatTypedRequest request;

- public WechatMpCommentApiReplyAddRequest()

- WechatMpCommentApiReplyAddRequest MsgDataId(long fieldValue)

- WechatMpCommentApiReplyAddRequest Index(long fieldValue)

- WechatMpCommentApiReplyAddRequest UserCommentId(long fieldValue)

- WechatMpCommentApiReplyAddRequest Content(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCommentApiReplyAddResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCommentApiReplyDeleteRequest (class)

- WechatTypedRequest request;

- public WechatMpCommentApiReplyDeleteRequest()

- WechatMpCommentApiReplyDeleteRequest MsgDataId(long fieldValue)

- WechatMpCommentApiReplyDeleteRequest Index(long fieldValue)

- WechatMpCommentApiReplyDeleteRequest UserCommentId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCommentApiReplyDeleteResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCommentApiUnmarkElectRequest (class)

- WechatTypedRequest request;

- public WechatMpCommentApiUnmarkElectRequest()

- WechatMpCommentApiUnmarkElectRequest MsgDataId(long fieldValue)

- WechatMpCommentApiUnmarkElectRequest Index(long fieldValue)

- WechatMpCommentApiUnmarkElectRequest UserCommentId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCommentApiUnmarkElectResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApi (class)

Custom/CustomApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpCustomApi(WechatClient client)

- async WechatMpCustomApiSendTextResponse SendTextAsync(WechatMpCustomApiSendTextRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendTextRawAsync(string query, string jsonBody)

- async WechatMpCustomApiSendImageResponse SendImageAsync(WechatMpCustomApiSendImageRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendImageRawAsync(string query, string jsonBody)

- async WechatMpCustomApiSendVoiceResponse SendVoiceAsync(WechatMpCustomApiSendVoiceRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendVoiceRawAsync(string query, string jsonBody)

- async WechatMpCustomApiSendVideoResponse SendVideoAsync(WechatMpCustomApiSendVideoRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendVideoRawAsync(string query, string jsonBody)

- async WechatMpCustomApiSendMusicResponse SendMusicAsync(WechatMpCustomApiSendMusicRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendMusicRawAsync(string query, string jsonBody)

- async WechatMpCustomApiSendNewsResponse SendNewsAsync(WechatMpCustomApiSendNewsRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendNewsRawAsync(string query, string jsonBody)

- async WechatMpCustomApiSendMpNewsResponse SendMpNewsAsync(WechatMpCustomApiSendMpNewsRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendMpNewsRawAsync(string query, string jsonBody)

- async WechatMpCustomApiSendCardResponse SendCardAsync(WechatMpCustomApiSendCardRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendCardRawAsync(string query, string jsonBody)

- async WechatMpCustomApiSendMiniProgramPageResponse SendMiniProgramPageAsync(WechatMpCustomApiSendMiniProgramPageRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendMiniProgramPageRawAsync(string query, string jsonBody)

- async WechatMpCustomApiGetTypingStatusResponse GetTypingStatusAsync(WechatMpCustomApiGetTypingStatusRequest request)
  - POST /cgi-bin/message/custom/typing

- async WechatResponse GetTypingStatusRawAsync(string query, string jsonBody)

- async WechatMpCustomApiSendMenuResponse SendMenuAsync(WechatMpCustomApiSendMenuRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendMenuRawAsync(string query, string jsonBody)


## WechatMpCustomApiGetTypingStatusRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiGetTypingStatusRequest()

- WechatMpCustomApiGetTypingStatusRequest Touser(string fieldValue)

- WechatMpCustomApiGetTypingStatusRequest TypingStatus(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiGetTypingStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendCardRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiSendCardRequest()

- WechatMpCustomApiSendCardRequest CardId(string fieldValue)

- WechatMpCustomApiSendCardRequest Code(string fieldValue)

- WechatMpCustomApiSendCardRequest Openid(string fieldValue)

- WechatMpCustomApiSendCardRequest ExpireSeconds(string fieldValue)

- WechatMpCustomApiSendCardRequest IsUniqueCode(bool fieldValue)

- WechatMpCustomApiSendCardRequest OuterStr(string fieldValue)

- WechatMpCustomApiSendCardRequest OpenId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendCardResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendImageRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiSendImageRequest()

- WechatMpCustomApiSendImageRequest OpenId(string fieldValue)

- WechatMpCustomApiSendImageRequest MediaId(string fieldValue)

- WechatMpCustomApiSendImageRequest KfAccount(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendImageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendMenuRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiSendMenuRequest()

- WechatMpCustomApiSendMenuRequest Head(string fieldValue)

- WechatMpCustomApiSendMenuRequest MenuList(List<WechatMpSendMenuContent> fieldValue)

- WechatMpCustomApiSendMenuRequest Tail(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendMenuResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendMiniProgramPageRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiSendMiniProgramPageRequest()

- WechatMpCustomApiSendMiniProgramPageRequest Title(string fieldValue)

- WechatMpCustomApiSendMiniProgramPageRequest Appid(string fieldValue)

- WechatMpCustomApiSendMiniProgramPageRequest Pagepath(string fieldValue)

- WechatMpCustomApiSendMiniProgramPageRequest ThumbMediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendMiniProgramPageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendMpNewsRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiSendMpNewsRequest()

- WechatMpCustomApiSendMpNewsRequest OpenId(string fieldValue)

- WechatMpCustomApiSendMpNewsRequest MediaId(string fieldValue)

- WechatMpCustomApiSendMpNewsRequest KfAccount(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendMpNewsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendMusicRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiSendMusicRequest()

- WechatMpCustomApiSendMusicRequest Title(string fieldValue)

- WechatMpCustomApiSendMusicRequest Description(string fieldValue)

- WechatMpCustomApiSendMusicRequest MusicUrl(string fieldValue)

- WechatMpCustomApiSendMusicRequest HqMusicUrl(string fieldValue)

- WechatMpCustomApiSendMusicRequest ThumbMediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendMusicResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendNewsRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiSendNewsRequest()

- WechatMpCustomApiSendNewsRequest OpenId(string fieldValue)

- WechatMpCustomApiSendNewsRequest Articles(JsonValue fieldValue)

- WechatMpCustomApiSendNewsRequest KfAccount(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendNewsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendTextRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpCustomApiSendTextRequest()

- WechatMpCustomApiSendTextRequest OpenId(string fieldValue)

- WechatMpCustomApiSendTextRequest Content(string fieldValue)

- WechatMpCustomApiSendTextRequest KfAccount(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendTextResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendVideoRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiSendVideoRequest()

- WechatMpCustomApiSendVideoRequest MediaId(string fieldValue)

- WechatMpCustomApiSendVideoRequest ThumbMediaId(string fieldValue)

- WechatMpCustomApiSendVideoRequest Title(string fieldValue)

- WechatMpCustomApiSendVideoRequest Description(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendVideoResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomApiSendVoiceRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomApiSendVoiceRequest()

- WechatMpCustomApiSendVoiceRequest OpenId(string fieldValue)

- WechatMpCustomApiSendVoiceRequest MediaId(string fieldValue)

- WechatMpCustomApiSendVoiceRequest KfAccount(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomApiSendVoiceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomServiceApi (class)

CustomService/CustomServiceApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpCustomServiceApi(WechatClient client)

- async WechatMpCustomServiceApiGetRecordResponse GetRecordAsync(WechatMpCustomServiceApiGetRecordRequest request)
  - POST /customservice/msgrecord/getrecord

- async WechatResponse GetRecordRawAsync(string query, string jsonBody)

- async WechatMpCustomServiceApiGetCustomBasicInfoResponse GetCustomBasicInfoAsync()
  - GET /cgi-bin/customservice/getkflist

- async WechatResponse GetCustomBasicInfoRawAsync(string query)

- async WechatMpCustomServiceApiGetCustomOnlineInfoResponse GetCustomOnlineInfoAsync()
  - GET /cgi-bin/customservice/getonlinekflist

- async WechatResponse GetCustomOnlineInfoRawAsync(string query)

- async WechatMpCustomServiceApiAddCustomResponse AddCustomAsync(WechatMpCustomServiceApiAddCustomRequest request)
  - POST /customservice/kfaccount/add

- async WechatResponse AddCustomRawAsync(string query, string jsonBody)

- async WechatMpCustomServiceApiInviteWorkerResponse InviteWorkerAsync(WechatMpCustomServiceApiInviteWorkerRequest request)
  - POST /customservice/kfaccount/inviteworker

- async WechatResponse InviteWorkerRawAsync(string query, string jsonBody)

- async WechatMpCustomServiceApiUpdateCustomResponse UpdateCustomAsync(WechatMpCustomServiceApiUpdateCustomRequest request)
  - POST /customservice/kfaccount/update

- async WechatResponse UpdateCustomRawAsync(string query, string jsonBody)

- async WechatMpCustomServiceApiDeleteCustomResponse DeleteCustomAsync(WechatMpCustomServiceApiDeleteCustomRequest request)
  - GET /customservice/kfaccount/del

- async WechatResponse DeleteCustomRawAsync(string query)

- async WechatMpCustomServiceApiCreateSessionResponse CreateSessionAsync(WechatMpCustomServiceApiCreateSessionRequest request)
  - POST /customservice/kfsession/create

- async WechatResponse CreateSessionRawAsync(string query, string jsonBody)

- async WechatMpCustomServiceApiCloseSessionResponse CloseSessionAsync(WechatMpCustomServiceApiCloseSessionRequest request)
  - POST /customservice/kfsession/close

- async WechatResponse CloseSessionRawAsync(string query, string jsonBody)

- async WechatMpCustomServiceApiGetSessionStateResponse GetSessionStateAsync(WechatMpCustomServiceApiGetSessionStateRequest request)
  - GET /customservice/kfsession/getsession

- async WechatResponse GetSessionStateRawAsync(string query)

- async WechatMpCustomServiceApiGetSessionListResponse GetSessionListAsync(WechatMpCustomServiceApiGetSessionListRequest request)
  - GET /customservice/kfsession/getsessionlist

- async WechatResponse GetSessionListRawAsync(string query)

- async WechatMpCustomServiceApiGetWaitCaseResponse GetWaitCaseAsync()
  - GET /customservice/kfsession/getwaitcase

- async WechatResponse GetWaitCaseRawAsync(string query)

- async WechatMpCustomServiceApiGetMsgListResponse GetMsgListAsync(WechatMpCustomServiceApiGetMsgListRequest request)
  - POST /customservice/msgrecord/getmsglist

- async WechatResponse GetMsgListRawAsync(string query, string jsonBody)


## WechatMpCustomServiceApiAddCustomRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomServiceApiAddCustomRequest()

- WechatMpCustomServiceApiAddCustomRequest KfAccount(string fieldValue)

- WechatMpCustomServiceApiAddCustomRequest NickName(string fieldValue)

- WechatMpCustomServiceApiAddCustomRequest PassWord(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiAddCustomResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomServiceApiCloseSessionRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomServiceApiCloseSessionRequest()

- WechatMpCustomServiceApiCloseSessionRequest OpenId(string fieldValue)

- WechatMpCustomServiceApiCloseSessionRequest KfAccount(string fieldValue)

- WechatMpCustomServiceApiCloseSessionRequest Text(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiCloseSessionResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomServiceApiCreateSessionRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomServiceApiCreateSessionRequest()

- WechatMpCustomServiceApiCreateSessionRequest OpenId(string fieldValue)

- WechatMpCustomServiceApiCreateSessionRequest KfAccount(string fieldValue)

- WechatMpCustomServiceApiCreateSessionRequest Text(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiCreateSessionResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomServiceApiDeleteCustomRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomServiceApiDeleteCustomRequest()

- WechatMpCustomServiceApiDeleteCustomRequest KfAccount(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiDeleteCustomResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomServiceApiGetCustomBasicInfoResponse (class)

- public string Raw;


## WechatMpCustomServiceApiGetCustomOnlineInfoResponse (class)

- public string Raw;


## WechatMpCustomServiceApiGetMsgListRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomServiceApiGetMsgListRequest()

- WechatMpCustomServiceApiGetMsgListRequest StartTime(string fieldValue)

- WechatMpCustomServiceApiGetMsgListRequest EndTime(string fieldValue)

- WechatMpCustomServiceApiGetMsgListRequest MsgId(long fieldValue)

- WechatMpCustomServiceApiGetMsgListRequest Number(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiGetMsgListResponse (class)

- public string Raw;


## WechatMpCustomServiceApiGetRecordRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpCustomServiceApiGetRecordRequest()

- WechatMpCustomServiceApiGetRecordRequest StartTime(string fieldValue)

- WechatMpCustomServiceApiGetRecordRequest EndTime(string fieldValue)

- WechatMpCustomServiceApiGetRecordRequest PageSize(int fieldValue)

- WechatMpCustomServiceApiGetRecordRequest PageIndex(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiGetRecordResponse (class)

- public string Raw;


## WechatMpCustomServiceApiGetSessionListRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomServiceApiGetSessionListRequest()

- WechatMpCustomServiceApiGetSessionListRequest KfAccount(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiGetSessionListResponse (class)

- public string Raw;


## WechatMpCustomServiceApiGetSessionStateRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomServiceApiGetSessionStateRequest()

- WechatMpCustomServiceApiGetSessionStateRequest OpenId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiGetSessionStateResponse (class)

- public string Raw;


## WechatMpCustomServiceApiGetWaitCaseResponse (class)

- public string Raw;


## WechatMpCustomServiceApiInviteWorkerRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomServiceApiInviteWorkerRequest()

- WechatMpCustomServiceApiInviteWorkerRequest KfAccount(string fieldValue)

- WechatMpCustomServiceApiInviteWorkerRequest InviteWx(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiInviteWorkerResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomServiceApiUpdateCustomRequest (class)

- WechatTypedRequest request;

- public WechatMpCustomServiceApiUpdateCustomRequest()

- WechatMpCustomServiceApiUpdateCustomRequest KfAccount(string fieldValue)

- WechatMpCustomServiceApiUpdateCustomRequest NickName(string fieldValue)

- WechatMpCustomServiceApiUpdateCustomRequest PassWord(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpCustomServiceApiUpdateCustomResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpCustomServiceSpecialApi (class)

客服头像上传；对应 CustomServiceApi.UploadCustomHeadimgAsync。

- WechatClient client;

- public WechatMpCustomServiceSpecialApi(WechatClient client)

- async WechatMpSpecialOperationResponse UploadCustomHeadimgAsync(string kfAccount, string fileName, string contentType, string fileBytes)


## WechatMpCvSpecialApi (class)

图像裁剪、二维码识别和驾驶证 OCR 文件上传接口。

- WechatClient client;

- public WechatMpCvSpecialApi(WechatClient client)

- async WechatMpSpecialAiCropResponse AiCropAsync(string imageUrl, string ratios)

- async WechatMpSpecialAiCropResponse AiCropByFileAsync(string ratios, string fileName, string contentType, string fileBytes)

- async WechatMpSpecialQrCodeResponse QrCodeByFileAsync(string fileName, string contentType, string fileBytes)

- async WechatMpSpecialDrivingLicenseResponse DrivingLicenseByFileAsync(string fileName, string contentType, string fileBytes)


## WechatMpDraftApi (class)

Draft/DraftApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpDraftApi(WechatClient client)

- async WechatMpDraftApiAddDraftResponse AddDraftAsync(WechatMpDraftApiAddDraftRequest request)
  - POST /cgi-bin/draft/add

- async WechatResponse AddDraftRawAsync(string query, string jsonBody)

- async WechatMpDraftApiGetDraftResponse GetDraftAsync(WechatMpDraftApiGetDraftRequest request)
  - POST /cgi-bin/draft/get

- async WechatResponse GetDraftRawAsync(string query, string jsonBody)

- async WechatMpDraftApiGetDraftCountResponse GetDraftCountAsync()
  - GET /cgi-bin/draft/count

- async WechatResponse GetDraftCountRawAsync(string query)

- async WechatMpDraftApiDeleteDraftResponse DeleteDraftAsync(WechatMpDraftApiDeleteDraftRequest request)
  - POST /cgi-bin/draft/delete

- async WechatResponse DeleteDraftRawAsync(string query, string jsonBody)

- async WechatMpDraftApiUpdateDraftResponse UpdateDraftAsync(WechatMpDraftApiUpdateDraftRequest request)
  - POST /cgi-bin/draft/update

- async WechatResponse UpdateDraftRawAsync(string query, string jsonBody)

- async WechatMpDraftApiGetDraftListResponse GetDraftListAsync(WechatMpDraftApiGetDraftListRequest request)
  - POST /cgi-bin/draft/batchget

- async WechatResponse GetDraftListRawAsync(string query, string jsonBody)

- async WechatMpDraftApiSwitchResponse SwitchAsync(WechatMpDraftApiSwitchRequest request)
  - POST /cgi-bin/draft/switch

- async WechatResponse SwitchRawAsync(string query, string jsonBody)


## WechatMpDraftApiAddDraftRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpDraftApiAddDraftRequest()

- WechatMpDraftApiAddDraftRequest Draft(List<WechatMpDraftModel> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpDraftApiAddDraftResponse (class)

- public string Raw;


## WechatMpDraftApiDeleteDraftRequest (class)

- WechatTypedRequest request;

- public WechatMpDraftApiDeleteDraftRequest()

- WechatMpDraftApiDeleteDraftRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpDraftApiDeleteDraftResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpDraftApiGetDraftCountResponse (class)

- public string Raw;


## WechatMpDraftApiGetDraftListRequest (class)

- WechatTypedRequest request;

- public WechatMpDraftApiGetDraftListRequest()

- WechatMpDraftApiGetDraftListRequest Offset(int fieldValue)

- WechatMpDraftApiGetDraftListRequest Count(int fieldValue)

- WechatMpDraftApiGetDraftListRequest NoContent(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpDraftApiGetDraftListResponse (class)

- public string Raw;


## WechatMpDraftApiGetDraftRequest (class)

- WechatTypedRequest request;

- public WechatMpDraftApiGetDraftRequest()

- WechatMpDraftApiGetDraftRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpDraftApiGetDraftResponse (class)

- public string Raw;


## WechatMpDraftApiSwitchRequest (class)

- WechatTypedRequest request;

- public WechatMpDraftApiSwitchRequest()

- WechatMpDraftApiSwitchRequest CheckOnly(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpDraftApiSwitchResponse (class)

- public string Raw;


## WechatMpDraftApiUpdateDraftRequest (class)

- WechatTypedRequest request;

- public WechatMpDraftApiUpdateDraftRequest()

- WechatMpDraftApiUpdateDraftRequest Title(string fieldValue)

- WechatMpDraftApiUpdateDraftRequest Author(string fieldValue)

- WechatMpDraftApiUpdateDraftRequest Digest(string fieldValue)

- WechatMpDraftApiUpdateDraftRequest Content(string fieldValue)

- WechatMpDraftApiUpdateDraftRequest ContentSourceUrl(string fieldValue)

- WechatMpDraftApiUpdateDraftRequest ThumbMediaId(string fieldValue)

- WechatMpDraftApiUpdateDraftRequest ShowCoverPic(string fieldValue)

- WechatMpDraftApiUpdateDraftRequest NeedOpenComment(int fieldValue)

- WechatMpDraftApiUpdateDraftRequest OnlyFansCanComment(int fieldValue)

- WechatMpDraftApiUpdateDraftRequest MediaId(string fieldValue)

- WechatMpDraftApiUpdateDraftRequest Index(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpDraftApiUpdateDraftResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpDraftProductCardApi (class)

Draft/ProductCardApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpDraftProductCardApi(WechatClient client)

- async WechatMpDraftProductCardApiGetProductCardInfoResponse GetProductCardInfoAsync(WechatMpDraftProductCardApiGetProductCardInfoRequest request)
  - POST /channels/ec/service/product/getcardinfo

- async WechatResponse GetProductCardInfoRawAsync(string query, string jsonBody)


## WechatMpDraftProductCardApiGetProductCardInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpDraftProductCardApiGetProductCardInfoRequest()

- WechatMpDraftProductCardApiGetProductCardInfoRequest ProductId(string fieldValue)

- WechatMpDraftProductCardApiGetProductCardInfoRequest ArticleType(int fieldValue)

- WechatMpDraftProductCardApiGetProductCardInfoRequest CardType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpDraftProductCardApiGetProductCardInfoResponse (class)

- public string Raw;


## WechatMpFreePublishApi (class)

FreePublish/FreePublishApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpFreePublishApi(WechatClient client)

- async WechatMpFreePublishApiSubmitResponse SubmitAsync(WechatMpFreePublishApiSubmitRequest request)
  - POST /cgi-bin/freepublish/submit

- async WechatResponse SubmitRawAsync(string query, string jsonBody)

- async WechatMpFreePublishApiGetResponse GetAsync(WechatMpFreePublishApiGetRequest request)
  - POST /cgi-bin/freepublish/get

- async WechatResponse GetRawAsync(string query, string jsonBody)

- async WechatMpFreePublishApiDeleteResponse DeleteAsync(WechatMpFreePublishApiDeleteRequest request)
  - POST /cgi-bin/freepublish/delete

- async WechatResponse DeleteRawAsync(string query, string jsonBody)

- async WechatMpFreePublishApiGetArticleResponse GetArticleAsync(WechatMpFreePublishApiGetArticleRequest request)
  - POST /cgi-bin/freepublish/getarticle

- async WechatResponse GetArticleRawAsync(string query, string jsonBody)

- async WechatMpFreePublishApiGetFreePublishListResponse GetFreePublishListAsync(WechatMpFreePublishApiGetFreePublishListRequest request)
  - POST /cgi-bin/freepublish/batchget

- async WechatResponse GetFreePublishListRawAsync(string query, string jsonBody)


## WechatMpFreePublishApiDeleteRequest (class)

- WechatTypedRequest request;

- public WechatMpFreePublishApiDeleteRequest()

- WechatMpFreePublishApiDeleteRequest ArticleId(string fieldValue)

- WechatMpFreePublishApiDeleteRequest Index(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpFreePublishApiDeleteResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpFreePublishApiGetArticleRequest (class)

- WechatTypedRequest request;

- public WechatMpFreePublishApiGetArticleRequest()

- WechatMpFreePublishApiGetArticleRequest ArticleId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpFreePublishApiGetArticleResponse (class)

- public string Raw;


## WechatMpFreePublishApiGetFreePublishListRequest (class)

- WechatTypedRequest request;

- public WechatMpFreePublishApiGetFreePublishListRequest()

- WechatMpFreePublishApiGetFreePublishListRequest Offset(int fieldValue)

- WechatMpFreePublishApiGetFreePublishListRequest Count(int fieldValue)

- WechatMpFreePublishApiGetFreePublishListRequest NoContent(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpFreePublishApiGetFreePublishListResponse (class)

- public string Raw;


## WechatMpFreePublishApiGetRequest (class)

- WechatTypedRequest request;

- public WechatMpFreePublishApiGetRequest()

- WechatMpFreePublishApiGetRequest PublishId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpFreePublishApiGetResponse (class)

- public string Raw;


## WechatMpFreePublishApiSubmitRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpFreePublishApiSubmitRequest()

- WechatMpFreePublishApiSubmitRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpFreePublishApiSubmitResponse (class)

- public string Raw;


## WechatMpGroupMessageApi (class)

GroupMessage/GroupMessageApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpGroupMessageApi(WechatClient client)

- async WechatMpGroupMessageApiSendGroupMessageByGroupIdResponse SendGroupMessageByGroupIdAsync(WechatMpGroupMessageApiSendGroupMessageByGroupIdRequest request)
  - POST /cgi-bin/message/mass/sendall

- async WechatResponse SendGroupMessageByGroupIdRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiSendGroupMessageByTagIdResponse SendGroupMessageByTagIdAsync(WechatMpGroupMessageApiSendGroupMessageByTagIdRequest request)
  - POST /cgi-bin/message/mass/sendall

- async WechatResponse SendGroupMessageByTagIdRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiSendGroupMessageByOpenIdResponse SendGroupMessageByOpenIdAsync(WechatMpGroupMessageApiSendGroupMessageByOpenIdRequest request)
  - POST /cgi-bin/message/mass/send

- async WechatResponse SendGroupMessageByOpenIdRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdResponse SendVideoGroupMessageByOpenIdAsync(WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdRequest request)
  - POST /cgi-bin/message/mass/send

- async WechatResponse SendVideoGroupMessageByOpenIdRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiDeleteSendMessageResponse DeleteSendMessageAsync(WechatMpGroupMessageApiDeleteSendMessageRequest request)
  - POST /cgi-bin/message/mass/delete

- async WechatResponse DeleteSendMessageRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiSendGroupMessagePreviewResponse SendGroupMessagePreviewAsync(WechatMpGroupMessageApiSendGroupMessagePreviewRequest request)
  - POST /cgi-bin/message/mass/preview

- async WechatResponse SendGroupMessagePreviewRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiWxCardGroupMessagePreviewResponse WxCardGroupMessagePreviewAsync(WechatMpGroupMessageApiWxCardGroupMessagePreviewRequest request)
  - POST /cgi-bin/message/mass/preview

- async WechatResponse WxCardGroupMessagePreviewRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiGetGroupMessageResultResponse GetGroupMessageResultAsync(WechatMpGroupMessageApiGetGroupMessageResultRequest request)
  - POST /cgi-bin/message/mass/get

- async WechatResponse GetGroupMessageResultRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiGetVideoMediaIdResultResponse GetVideoMediaIdResultAsync(WechatMpGroupMessageApiGetVideoMediaIdResultRequest request)
  - POST /cgi-bin/media/uploadvideo

- async WechatResponse GetVideoMediaIdResultRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiGetSendSpeedResponse GetSendSpeedAsync()
  - POST /cgi-bin/message/mass/speed/get

- async WechatResponse GetSendSpeedRawAsync(string query, string jsonBody)

- async WechatMpGroupMessageApiSetSendSpeedResponse SetSendSpeedAsync(WechatMpGroupMessageApiSetSendSpeedRequest request)
  - POST /cgi-bin/message/mass/speed/set

- async WechatResponse SetSendSpeedRawAsync(string query, string jsonBody)


## WechatMpGroupMessageApiDeleteSendMessageRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupMessageApiDeleteSendMessageRequest()

- WechatMpGroupMessageApiDeleteSendMessageRequest MsgId(string fieldValue)

- WechatMpGroupMessageApiDeleteSendMessageRequest ArticleIdx(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiDeleteSendMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpGroupMessageApiGetGroupMessageResultRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupMessageApiGetGroupMessageResultRequest()

- WechatMpGroupMessageApiGetGroupMessageResultRequest MsgId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiGetGroupMessageResultResponse (class)

- public string Raw;


## WechatMpGroupMessageApiGetSendSpeedResponse (class)

- public string Raw;


## WechatMpGroupMessageApiGetVideoMediaIdResultRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupMessageApiGetVideoMediaIdResultRequest()

- WechatMpGroupMessageApiGetVideoMediaIdResultRequest MediaId(string fieldValue)

- WechatMpGroupMessageApiGetVideoMediaIdResultRequest Title(string fieldValue)

- WechatMpGroupMessageApiGetVideoMediaIdResultRequest Description(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiGetVideoMediaIdResultResponse (class)

- public string Raw;


## WechatMpGroupMessageApiSendGroupMessageByGroupIdRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpGroupMessageApiSendGroupMessageByGroupIdRequest()

- WechatMpGroupMessageApiSendGroupMessageByGroupIdRequest GroupId(string fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByGroupIdRequest Value(string fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByGroupIdRequest Type(int fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByGroupIdRequest IsToAll(bool fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByGroupIdRequest SendIgnoreReprint(bool fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByGroupIdRequest Clientmsgid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiSendGroupMessageByGroupIdResponse (class)

- public string Raw;


## WechatMpGroupMessageApiSendGroupMessageByOpenIdRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupMessageApiSendGroupMessageByOpenIdRequest()

- WechatMpGroupMessageApiSendGroupMessageByOpenIdRequest Type(int fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByOpenIdRequest Value(string fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByOpenIdRequest Clientmsgid(string fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByOpenIdRequest OpenIds(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiSendGroupMessageByOpenIdResponse (class)

- public string Raw;


## WechatMpGroupMessageApiSendGroupMessageByTagIdRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupMessageApiSendGroupMessageByTagIdRequest()

- WechatMpGroupMessageApiSendGroupMessageByTagIdRequest TagId(string fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByTagIdRequest Value(string fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByTagIdRequest Type(int fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByTagIdRequest IsToAll(bool fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByTagIdRequest SendIgnoreReprint(bool fieldValue)

- WechatMpGroupMessageApiSendGroupMessageByTagIdRequest Clientmsgid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiSendGroupMessageByTagIdResponse (class)

- public string Raw;


## WechatMpGroupMessageApiSendGroupMessagePreviewRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupMessageApiSendGroupMessagePreviewRequest()

- WechatMpGroupMessageApiSendGroupMessagePreviewRequest Type(int fieldValue)

- WechatMpGroupMessageApiSendGroupMessagePreviewRequest Value(string fieldValue)

- WechatMpGroupMessageApiSendGroupMessagePreviewRequest OpenId(string fieldValue)

- WechatMpGroupMessageApiSendGroupMessagePreviewRequest WxName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiSendGroupMessagePreviewResponse (class)

- public string Raw;


## WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdRequest()

- WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdRequest Title(string fieldValue)

- WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdRequest Description(string fieldValue)

- WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdRequest MediaId(string fieldValue)

- WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdRequest Clientmsgid(string fieldValue)

- WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdRequest OpenIds(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiSendVideoGroupMessageByOpenIdResponse (class)

- public string Raw;


## WechatMpGroupMessageApiSetSendSpeedRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupMessageApiSetSendSpeedRequest()

- WechatMpGroupMessageApiSetSendSpeedRequest Speed(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiSetSendSpeedResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpGroupMessageApiWxCardGroupMessagePreviewRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupMessageApiWxCardGroupMessagePreviewRequest()

- WechatMpGroupMessageApiWxCardGroupMessagePreviewRequest CardId(string fieldValue)

- WechatMpGroupMessageApiWxCardGroupMessagePreviewRequest Code(string fieldValue)

- WechatMpGroupMessageApiWxCardGroupMessagePreviewRequest OpenId(string fieldValue)

- WechatMpGroupMessageApiWxCardGroupMessagePreviewRequest WxName(string fieldValue)

- WechatMpGroupMessageApiWxCardGroupMessagePreviewRequest Timestamp(string fieldValue)

- WechatMpGroupMessageApiWxCardGroupMessagePreviewRequest Signature(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupMessageApiWxCardGroupMessagePreviewResponse (class)

- public string Raw;


## WechatMpGroupsApi (class)

Groups/GroupsApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpGroupsApi(WechatClient client)

- async WechatMpGroupsApiCreateResponse CreateAsync(WechatMpGroupsApiCreateRequest request)
  - POST /cgi-bin/groups/create

- async WechatResponse CreateRawAsync(string query, string jsonBody)

- async WechatMpGroupsApiGetResponse GetAsync()
  - GET /cgi-bin/groups/get

- async WechatResponse GetRawAsync(string query)

- async WechatMpGroupsApiGetIdResponse GetIdAsync(WechatMpGroupsApiGetIdRequest request)
  - POST /cgi-bin/groups/getid

- async WechatResponse GetIdRawAsync(string query, string jsonBody)

- async WechatMpGroupsApiUpdateResponse UpdateAsync(WechatMpGroupsApiUpdateRequest request)
  - POST /cgi-bin/groups/update

- async WechatResponse UpdateRawAsync(string query, string jsonBody)

- async WechatMpGroupsApiMemberUpdateResponse MemberUpdateAsync(WechatMpGroupsApiMemberUpdateRequest request)
  - POST /cgi-bin/groups/members/update

- async WechatResponse MemberUpdateRawAsync(string query, string jsonBody)

- async WechatMpGroupsApiBatchUpdateResponse BatchUpdateAsync(WechatMpGroupsApiBatchUpdateRequest request)
  - POST /cgi-bin/groups/members/batchupdate

- async WechatResponse BatchUpdateRawAsync(string query, string jsonBody)

- async WechatMpGroupsApiDeleteResponse DeleteAsync(WechatMpGroupsApiDeleteRequest request)
  - POST /cgi-bin/groups/delete

- async WechatResponse DeleteRawAsync(string query, string jsonBody)


## WechatMpGroupsApiBatchUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupsApiBatchUpdateRequest()

- WechatMpGroupsApiBatchUpdateRequest OpenIds(List<string> fieldValue)

- WechatMpGroupsApiBatchUpdateRequest ToGroupId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupsApiBatchUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpGroupsApiCreateRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpGroupsApiCreateRequest()

- WechatMpGroupsApiCreateRequest Name(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupsApiCreateResponse (class)

- public string Raw;


## WechatMpGroupsApiDeleteRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupsApiDeleteRequest()

- WechatMpGroupsApiDeleteRequest GroupId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupsApiDeleteResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpGroupsApiGetIdRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupsApiGetIdRequest()

- WechatMpGroupsApiGetIdRequest OpenId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupsApiGetIdResponse (class)

- public string Raw;


## WechatMpGroupsApiGetResponse (class)

- public string Raw;


## WechatMpGroupsApiMemberUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupsApiMemberUpdateRequest()

- WechatMpGroupsApiMemberUpdateRequest OpenId(string fieldValue)

- WechatMpGroupsApiMemberUpdateRequest ToGroupId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupsApiMemberUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpGroupsApiUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpGroupsApiUpdateRequest()

- WechatMpGroupsApiUpdateRequest Id(int fieldValue)

- WechatMpGroupsApiUpdateRequest Name(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpGroupsApiUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceApi (class)

Invoice/InvoiceApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpInvoiceApi(WechatClient client)

- async WechatMpInvoiceApiSetPayMchResponse SetPayMchAsync(WechatMpInvoiceApiSetPayMchRequest request)
  - POST /card/invoice/setbizattr

- async WechatResponse SetPayMchRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetPayMchResponse GetPayMchAsync()
  - POST /card/invoice/setbizattr

- async WechatResponse GetPayMchRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiSetAuthFieldResponse SetAuthFieldAsync(WechatMpInvoiceApiSetAuthFieldRequest request)
  - POST /card/invoice/setbizattr

- async WechatResponse SetAuthFieldRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetAuthFieldResponse GetAuthFieldAsync()
  - POST /card/invoice/setbizattr

- async WechatResponse GetAuthFieldRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetAuthDataResponse GetAuthDataAsync(WechatMpInvoiceApiGetAuthDataRequest request)
  - POST /card/invoice/getauthdata

- async WechatResponse GetAuthDataRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetAuthUrlResponse GetAuthUrlAsync(WechatMpInvoiceApiGetAuthUrlRequest request)
  - POST /card/invoice/getauthurl

- async WechatResponse GetAuthUrlRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiRejectInsertResponse RejectInsertAsync(WechatMpInvoiceApiRejectInsertRequest request)
  - POST /card/invoice/rejectinsert

- async WechatResponse RejectInsertRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiMakeOutInvoiceResponse MakeOutInvoiceAsync(WechatMpInvoiceApiMakeOutInvoiceRequest request)
  - POST /card/invoice/makeoutinvoice

- async WechatResponse MakeOutInvoiceRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiClearOutInvoiceResponse ClearOutInvoiceAsync(WechatMpInvoiceApiClearOutInvoiceRequest request)
  - POST /card/invoice/clearoutinvoice

- async WechatResponse ClearOutInvoiceRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiQueryInvoiceResponse QueryInvoiceAsync(WechatMpInvoiceApiQueryInvoiceRequest request)
  - POST /card/invoice/queryinvoceinfo

- async WechatResponse QueryInvoiceRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiInsertBillResponse InsertBillAsync(WechatMpInvoiceApiInsertBillRequest request)
  - POST /nontax/insertbill

- async WechatResponse InsertBillRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiCreateBillCardResponse CreateBillCardAsync(WechatMpInvoiceApiCreateBillCardRequest request)
  - POST /nontax/createbillcard

- async WechatResponse CreateBillCardRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetBillAuthUrlResponse GetBillAuthUrlAsync(WechatMpInvoiceApiGetBillAuthUrlRequest request)
  - POST /nontax/getbillauthurl

- async WechatResponse GetBillAuthUrlRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiUpdateStatusResponse UpdateStatusAsync(WechatMpInvoiceApiUpdateStatusRequest request)
  - POST /card/invoice/platform/updatestatus

- async WechatResponse UpdateStatusRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiSetContactResponse SetContactAsync(WechatMpInvoiceApiSetContactRequest request)
  - POST /card/invoice/setbizattr

- async WechatResponse SetContactRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetContactResponse GetContactAsync()
  - POST /card/invoice/setbizattr

- async WechatResponse GetContactRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetInvoiceInfoResponse GetInvoiceInfoAsync(WechatMpInvoiceApiGetInvoiceInfoRequest request)
  - POST /card/invoice/reimburse/getinvoiceinfo

- async WechatResponse GetInvoiceInfoRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetInvoiceListInfoResponse GetInvoiceListInfoAsync(WechatMpInvoiceApiGetInvoiceListInfoRequest request)
  - POST /card/invoice/reimburse/getinvoicebatch

- async WechatResponse GetInvoiceListInfoRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiUpdateInvoiceStatusResponse UpdateInvoiceStatusAsync(WechatMpInvoiceApiUpdateInvoiceStatusRequest request)
  - POST /card/invoice/reimburse/updateinvoicestatus

- async WechatResponse UpdateInvoiceStatusRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiUpdateInvoiceListStatusResponse UpdateInvoiceListStatusAsync(WechatMpInvoiceApiUpdateInvoiceListStatusRequest request)
  - POST /card/invoice/reimburse/updatestatusbatch

- async WechatResponse UpdateInvoiceListStatusRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiSetUrlResponse SetUrlAsync()
  - POST /card/invoice/seturl

- async WechatResponse SetUrlRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiCreateCardResponse CreateCardAsync(WechatMpInvoiceApiCreateCardRequest request)
  - POST /card/invoice/platform/createcard

- async WechatResponse CreateCardRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetPdfResponse GetPdfAsync(WechatMpInvoiceApiGetPdfRequest request)
  - POST /card/invoice/platform/getpdf

- async WechatResponse GetPdfRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiInsertCardToBagResponse InsertCardToBagAsync(WechatMpInvoiceApiInsertCardToBagRequest request)
  - POST /card/invoice/insert

- async WechatResponse InsertCardToBagRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetUserTitleUrlResponse GetUserTitleUrlAsync(WechatMpInvoiceApiGetUserTitleUrlRequest request)
  - POST /card/invoice/biz/getusertitleurl

- async WechatResponse GetUserTitleUrlRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiGetSelectTitleUrlResponse GetSelectTitleUrlAsync(WechatMpInvoiceApiGetSelectTitleUrlRequest request)
  - POST /card/invoice/biz/getselecttitleurl

- async WechatResponse GetSelectTitleUrlRawAsync(string query, string jsonBody)

- async WechatMpInvoiceApiScanTitleResponse ScanTitleAsync(WechatMpInvoiceApiScanTitleRequest request)
  - POST /card/invoice/scantitle

- async WechatResponse ScanTitleRawAsync(string query, string jsonBody)


## WechatMpInvoiceApiClearOutInvoiceRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiClearOutInvoiceRequest()

- WechatMpInvoiceApiClearOutInvoiceRequest Wxopenid(string fieldValue)

- WechatMpInvoiceApiClearOutInvoiceRequest Fpqqlsh(string fieldValue)

- WechatMpInvoiceApiClearOutInvoiceRequest Nsrsbh(string fieldValue)

- WechatMpInvoiceApiClearOutInvoiceRequest Nsrmc(string fieldValue)

- WechatMpInvoiceApiClearOutInvoiceRequest Yfpdm(string fieldValue)

- WechatMpInvoiceApiClearOutInvoiceRequest Yfphm(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiClearOutInvoiceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceApiCreateBillCardRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiCreateBillCardRequest()

- WechatMpInvoiceApiCreateBillCardRequest BaseInfo(WechatMpInvoiceBaseInfo fieldValue)

- WechatMpInvoiceApiCreateBillCardRequest Payee(string fieldValue)

- WechatMpInvoiceApiCreateBillCardRequest Type(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiCreateBillCardResponse (class)

- public string Raw;


## WechatMpInvoiceApiCreateCardRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiCreateCardRequest()

- WechatMpInvoiceApiCreateCardRequest BaseInfo(WechatMpInvoiceBaseInfo fieldValue)

- WechatMpInvoiceApiCreateCardRequest Payee(string fieldValue)

- WechatMpInvoiceApiCreateCardRequest Type(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiCreateCardResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetAuthDataRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiGetAuthDataRequest()

- WechatMpInvoiceApiGetAuthDataRequest OrderId(string fieldValue)

- WechatMpInvoiceApiGetAuthDataRequest SAppId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiGetAuthDataResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetAuthFieldResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetAuthUrlRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiGetAuthUrlRequest()

- WechatMpInvoiceApiGetAuthUrlRequest SPappid(string fieldValue)

- WechatMpInvoiceApiGetAuthUrlRequest OrderId(string fieldValue)

- WechatMpInvoiceApiGetAuthUrlRequest Money(int fieldValue)

- WechatMpInvoiceApiGetAuthUrlRequest Timestamp(int fieldValue)

- WechatMpInvoiceApiGetAuthUrlRequest Source(string fieldValue)

- WechatMpInvoiceApiGetAuthUrlRequest RedirectUrl(string fieldValue)

- WechatMpInvoiceApiGetAuthUrlRequest Ticket(string fieldValue)

- WechatMpInvoiceApiGetAuthUrlRequest Type(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiGetAuthUrlResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetBillAuthUrlRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiGetBillAuthUrlRequest()

- WechatMpInvoiceApiGetBillAuthUrlRequest SPappid(string fieldValue)

- WechatMpInvoiceApiGetBillAuthUrlRequest OrderId(string fieldValue)

- WechatMpInvoiceApiGetBillAuthUrlRequest Money(int fieldValue)

- WechatMpInvoiceApiGetBillAuthUrlRequest Timestamp(int fieldValue)

- WechatMpInvoiceApiGetBillAuthUrlRequest Source(int fieldValue)

- WechatMpInvoiceApiGetBillAuthUrlRequest RedirectUrl(string fieldValue)

- WechatMpInvoiceApiGetBillAuthUrlRequest Ticket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiGetBillAuthUrlResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetContactResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetInvoiceInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiGetInvoiceInfoRequest()

- WechatMpInvoiceApiGetInvoiceInfoRequest CardId(string fieldValue)

- WechatMpInvoiceApiGetInvoiceInfoRequest EncryptCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiGetInvoiceInfoResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetInvoiceListInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiGetInvoiceListInfoRequest()

- WechatMpInvoiceApiGetInvoiceListInfoRequest ItemList(List<WechatMpInvoiceItem> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiGetInvoiceListInfoResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetPayMchResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetPdfRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiGetPdfRequest()

- WechatMpInvoiceApiGetPdfRequest SMediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiGetPdfResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetSelectTitleUrlRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiGetSelectTitleUrlRequest()

- WechatMpInvoiceApiGetSelectTitleUrlRequest Attach(string fieldValue)

- WechatMpInvoiceApiGetSelectTitleUrlRequest BizName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiGetSelectTitleUrlResponse (class)

- public string Raw;


## WechatMpInvoiceApiGetUserTitleUrlRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiGetUserTitleUrlRequest()

- WechatMpInvoiceApiGetUserTitleUrlRequest Title(string fieldValue)

- WechatMpInvoiceApiGetUserTitleUrlRequest Phone(string fieldValue)

- WechatMpInvoiceApiGetUserTitleUrlRequest TaxNo(string fieldValue)

- WechatMpInvoiceApiGetUserTitleUrlRequest Addr(string fieldValue)

- WechatMpInvoiceApiGetUserTitleUrlRequest BankType(string fieldValue)

- WechatMpInvoiceApiGetUserTitleUrlRequest BankNo(string fieldValue)

- WechatMpInvoiceApiGetUserTitleUrlRequest UserFill(int fieldValue)

- WechatMpInvoiceApiGetUserTitleUrlRequest OutTitleId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiGetUserTitleUrlResponse (class)

- public string Raw;


## WechatMpInvoiceApiInsertBillRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiInsertBillRequest()

- WechatMpInvoiceApiInsertBillRequest OrderId(string fieldValue)

- WechatMpInvoiceApiInsertBillRequest CardId(string fieldValue)

- WechatMpInvoiceApiInsertBillRequest Appid(string fieldValue)

- WechatMpInvoiceApiInsertBillRequest CardExt(WechatMpCardExtInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiInsertBillResponse (class)

- public string Raw;


## WechatMpInvoiceApiInsertCardToBagRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiInsertCardToBagRequest()

- WechatMpInvoiceApiInsertCardToBagRequest OrderId(string fieldValue)

- WechatMpInvoiceApiInsertCardToBagRequest CardId(string fieldValue)

- WechatMpInvoiceApiInsertCardToBagRequest Appid(string fieldValue)

- WechatMpInvoiceApiInsertCardToBagRequest CardExt(WechatMpCardExtInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiInsertCardToBagResponse (class)

- public string Raw;


## WechatMpInvoiceApiMakeOutInvoiceRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiMakeOutInvoiceRequest()

- WechatMpInvoiceApiMakeOutInvoiceRequest Wxopenid(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Ddh(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Fpqqlsh(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Nsrsbh(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Nsrmc(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Nsrdz(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Nsrdh(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Nsrbank(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Nsrbankid(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Ghfmc(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Ghfnsrsbh(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Ghfdz(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Ghfdh(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Ghfbank(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Ghfbankid(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Kpr(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Skr(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Fhr(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Jshj(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Hjse(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Bz(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest Hylx(string fieldValue)

- WechatMpInvoiceApiMakeOutInvoiceRequest InvoicedetailList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiMakeOutInvoiceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceApiQueryInvoiceRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiQueryInvoiceRequest()

- WechatMpInvoiceApiQueryInvoiceRequest Fpqqlsh(string fieldValue)

- WechatMpInvoiceApiQueryInvoiceRequest Nsrsbh(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiQueryInvoiceResponse (class)

- public string Raw;


## WechatMpInvoiceApiRejectInsertRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiRejectInsertRequest()

- WechatMpInvoiceApiRejectInsertRequest SPappId(string fieldValue)

- WechatMpInvoiceApiRejectInsertRequest OrderId(string fieldValue)

- WechatMpInvoiceApiRejectInsertRequest Reason(string fieldValue)

- WechatMpInvoiceApiRejectInsertRequest Url(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiRejectInsertResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceApiScanTitleRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiScanTitleRequest()

- WechatMpInvoiceApiScanTitleRequest ScanText(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiScanTitleResponse (class)

- public string Raw;


## WechatMpInvoiceApiSetAuthFieldRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiSetAuthFieldRequest()

- WechatMpInvoiceApiSetAuthFieldRequest UserField(WechatMpUserFiledData fieldValue)

- WechatMpInvoiceApiSetAuthFieldRequest BizField(WechatMpBizField fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiSetAuthFieldResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceApiSetContactRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiSetContactRequest()

- WechatMpInvoiceApiSetContactRequest Phone(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiSetContactResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceApiSetPayMchRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpInvoiceApiSetPayMchRequest()

- WechatMpInvoiceApiSetPayMchRequest Mchid(string fieldValue)

- WechatMpInvoiceApiSetPayMchRequest SPappid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiSetPayMchResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceApiSetUrlResponse (class)

- public string Raw;


## WechatMpInvoiceApiUpdateInvoiceListStatusRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiUpdateInvoiceListStatusRequest()

- WechatMpInvoiceApiUpdateInvoiceListStatusRequest OpenId(string fieldValue)

- WechatMpInvoiceApiUpdateInvoiceListStatusRequest ReimburseStatus(int fieldValue)

- WechatMpInvoiceApiUpdateInvoiceListStatusRequest ItemList(List<WechatMpInvoiceItem> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiUpdateInvoiceListStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceApiUpdateInvoiceStatusRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiUpdateInvoiceStatusRequest()

- WechatMpInvoiceApiUpdateInvoiceStatusRequest CardId(string fieldValue)

- WechatMpInvoiceApiUpdateInvoiceStatusRequest EncryptCode(string fieldValue)

- WechatMpInvoiceApiUpdateInvoiceStatusRequest ReimburseStatus(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiUpdateInvoiceStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceApiUpdateStatusRequest (class)

- WechatTypedRequest request;

- public WechatMpInvoiceApiUpdateStatusRequest()

- WechatMpInvoiceApiUpdateStatusRequest CardId(string fieldValue)

- WechatMpInvoiceApiUpdateStatusRequest Code(string fieldValue)

- WechatMpInvoiceApiUpdateStatusRequest ReimburseStatus(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpInvoiceApiUpdateStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpInvoiceSpecialApi (class)

电子发票 PDF 上传。

- WechatClient client;

- public WechatMpInvoiceSpecialApi(WechatClient client)

- async WechatMpSpecialSetPdfResponse SetPdfAsync(string fileName, string contentType, string fileBytes)


## WechatMpMediaApi (class)

Media/MediaApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpMediaApi(WechatClient client)

- async WechatMpMediaApiUploadTemporaryNewsResponse UploadTemporaryNewsAsync(WechatMpMediaApiUploadTemporaryNewsRequest request)
  - POST /cgi-bin/media/uploadnews

- async WechatResponse UploadTemporaryNewsRawAsync(string query, string jsonBody)

- async WechatMpMediaApiUploadNewsResponse UploadNewsAsync(WechatMpMediaApiUploadNewsRequest request)
  - POST /cgi-bin/material/add_news

- async WechatResponse UploadNewsRawAsync(string query, string jsonBody)

- async WechatMpMediaApiGetForeverNewsResponse GetForeverNewsAsync(WechatMpMediaApiGetForeverNewsRequest request)
  - POST /cgi-bin/material/get_material

- async WechatResponse GetForeverNewsRawAsync(string query, string jsonBody)

- async WechatMpMediaApiGetForeverVideoResponse GetForeverVideoAsync(WechatMpMediaApiGetForeverVideoRequest request)
  - POST /cgi-bin/material/get_material

- async WechatResponse GetForeverVideoRawAsync(string query, string jsonBody)

- async WechatMpMediaApiDeleteForeverMediaResponse DeleteForeverMediaAsync(WechatMpMediaApiDeleteForeverMediaRequest request)
  - POST /cgi-bin/material/del_material

- async WechatResponse DeleteForeverMediaRawAsync(string query, string jsonBody)

- async WechatMpMediaApiUpdateForeverNewsResponse UpdateForeverNewsAsync(WechatMpMediaApiUpdateForeverNewsRequest request)
  - POST /cgi-bin/material/update_news

- async WechatResponse UpdateForeverNewsRawAsync(string query, string jsonBody)

- async WechatMpMediaApiGetMediaCountResponse GetMediaCountAsync()
  - GET /cgi-bin/material/get_materialcount

- async WechatResponse GetMediaCountRawAsync(string query)

- async WechatMpMediaApiGetNewsMediaListResponse GetNewsMediaListAsync(WechatMpMediaApiGetNewsMediaListRequest request)
  - POST /cgi-bin/material/batchget_material

- async WechatResponse GetNewsMediaListRawAsync(string query, string jsonBody)

- async WechatMpMediaApiGetOthersMediaListResponse GetOthersMediaListAsync(WechatMpMediaApiGetOthersMediaListRequest request)
  - POST /cgi-bin/material/batchget_material

- async WechatResponse GetOthersMediaListRawAsync(string query, string jsonBody)

- async WechatMpMediaApiQueryRecoResultResponse QueryRecoResultAsync(WechatMpMediaApiQueryRecoResultRequest request)
  - POST /cgi-bin/media/voice/queryrecoresultfortext

- async WechatResponse QueryRecoResultRawAsync(string query, string jsonBody)

- async WechatMpMediaApiTranslateContentResponse TranslateContentAsync(WechatMpMediaApiTranslateContentRequest request)
  - POST /cgi-bin/media/voice/translatecontent

- async WechatResponse TranslateContentRawAsync(string query, string jsonBody)


## WechatMpMediaApiDeleteForeverMediaRequest (class)

- WechatTypedRequest request;

- public WechatMpMediaApiDeleteForeverMediaRequest()

- WechatMpMediaApiDeleteForeverMediaRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiDeleteForeverMediaResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMediaApiGetForeverNewsRequest (class)

- WechatTypedRequest request;

- public WechatMpMediaApiGetForeverNewsRequest()

- WechatMpMediaApiGetForeverNewsRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiGetForeverNewsResponse (class)

- public string Raw;


## WechatMpMediaApiGetForeverVideoRequest (class)

- WechatTypedRequest request;

- public WechatMpMediaApiGetForeverVideoRequest()

- WechatMpMediaApiGetForeverVideoRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiGetForeverVideoResponse (class)

- public string Raw;


## WechatMpMediaApiGetMediaCountResponse (class)

- public string Raw;


## WechatMpMediaApiGetNewsMediaListRequest (class)

- WechatTypedRequest request;

- public WechatMpMediaApiGetNewsMediaListRequest()

- WechatMpMediaApiGetNewsMediaListRequest Offset(int fieldValue)

- WechatMpMediaApiGetNewsMediaListRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiGetNewsMediaListResponse (class)

- public string Raw;


## WechatMpMediaApiGetOthersMediaListRequest (class)

- WechatTypedRequest request;

- public WechatMpMediaApiGetOthersMediaListRequest()

- WechatMpMediaApiGetOthersMediaListRequest Type(int fieldValue)

- WechatMpMediaApiGetOthersMediaListRequest Offset(int fieldValue)

- WechatMpMediaApiGetOthersMediaListRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiGetOthersMediaListResponse (class)

- public string Raw;


## WechatMpMediaApiQueryRecoResultRequest (class)

- WechatTypedRequest request;

- public WechatMpMediaApiQueryRecoResultRequest()

- WechatMpMediaApiQueryRecoResultRequest VoiceId(string fieldValue)

- WechatMpMediaApiQueryRecoResultRequest Lang(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiQueryRecoResultResponse (class)

- public string Raw;


## WechatMpMediaApiTranslateContentRequest (class)

- WechatTypedRequest request;

- public WechatMpMediaApiTranslateContentRequest()

- WechatMpMediaApiTranslateContentRequest Ifrom(string fieldValue)

- WechatMpMediaApiTranslateContentRequest Ito(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiTranslateContentResponse (class)

- public string Raw;


## WechatMpMediaApiUpdateForeverNewsRequest (class)

- WechatTypedRequest request;

- public WechatMpMediaApiUpdateForeverNewsRequest()

- WechatMpMediaApiUpdateForeverNewsRequest ThumbMediaId(string fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest Author(string fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest Title(string fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest ContentSourceUrl(string fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest Content(string fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest Digest(string fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest ShowCoverPic(string fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest ThumbUrl(string fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest NeedOpenComment(int fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest OnlyFansCanComment(int fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest MediaId(string fieldValue)

- WechatMpMediaApiUpdateForeverNewsRequest Index(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiUpdateForeverNewsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMediaApiUploadNewsRequest (class)

- WechatTypedRequest request;

- public WechatMpMediaApiUploadNewsRequest()

- WechatMpMediaApiUploadNewsRequest News(List<WechatMpNewsModel> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiUploadNewsResponse (class)

- public string Raw;


## WechatMpMediaApiUploadTemporaryNewsRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpMediaApiUploadTemporaryNewsRequest()

- WechatMpMediaApiUploadTemporaryNewsRequest News(List<WechatMpNewsModel> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMediaApiUploadTemporaryNewsResponse (class)

- public string Raw;


## WechatMpMediaSpecialApi (class)

临时素材、永久素材和语音识别的二进制接口。

- WechatClient client;

- public WechatMpMediaSpecialApi(WechatClient client)

- async WechatMpSpecialUploadTemporaryMediaResponse UploadTemporaryMediaAsync(WechatMpMediaType mediaType, string fileName, string contentType, string fileBytes)

- async WechatRawResponse GetAsync(string mediaId)

- async WechatRawResponse GetJssdkAsync(string mediaId)

- async WechatMpSpecialUploadForeverMediaResponse UploadForeverMediaAsync(WechatMpMediaType mediaType, string fileName, string contentType, string fileBytes)

- async WechatMpSpecialUploadForeverMediaResponse UploadForeverVideoAsync(string title, string introduction, string fileName, string contentType, string fileBytes)

- async WechatRawResponse GetForeverMediaAsync(string mediaId)

- async WechatRawResponse GetForeverMediaWithContentTypeAsync(string mediaId)

- async WechatMpSpecialUploadImgResponse UploadImgAsync(string fileName, string contentType, string fileBytes)

- async WechatMpSpecialOperationResponse AddVoiceAsync(string format, string voiceId, string lang, string contentType, string voiceBytes)


## WechatMpMedicalAssistantApi (class)

MedicalAssistant/MedicalAssistantApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpMedicalAssistantApi(WechatClient client)

- async WechatMpMedicalAssistantApiSendChannelMessageResponse SendChannelMessageAsync(WechatMpMedicalAssistantApiSendChannelMessageRequest request)
  - POST /cityservice/sendchannelmsg

- async WechatResponse SendChannelMessageRawAsync(string query, string jsonBody)


## WechatMpMedicalAssistantApiSendChannelMessageRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpMedicalAssistantApiSendChannelMessageRequest()

- WechatMpMedicalAssistantApiSendChannelMessageRequest Request(WechatMpSendChannelMessageRequest fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMedicalAssistantApiSendChannelMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantExpressApi (class)

MerChant/Express/ExpressApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpMerchantExpressApi(WechatClient client)

- async WechatMpMerchantExpressApiAddExpressResponse AddExpressAsync(WechatMpMerchantExpressApiAddExpressRequest request)
  - POST /merchant/express/add

- async WechatResponse AddExpressRawAsync(string query, string jsonBody)

- async WechatMpMerchantExpressApiDeleteExpressResponse DeleteExpressAsync(WechatMpMerchantExpressApiDeleteExpressRequest request)
  - POST /merchant/express/del

- async WechatResponse DeleteExpressRawAsync(string query, string jsonBody)

- async WechatMpMerchantExpressApiUpDateExpressResponse UpDateExpressAsync(WechatMpMerchantExpressApiUpDateExpressRequest request)
  - POST /merchant/express/update

- async WechatResponse UpDateExpressRawAsync(string query, string jsonBody)

- async WechatMpMerchantExpressApiGetByIdExpressResponse GetByIdExpressAsync(WechatMpMerchantExpressApiGetByIdExpressRequest request)
  - POST /merchant/express/getbyid

- async WechatResponse GetByIdExpressRawAsync(string query, string jsonBody)

- async WechatMpMerchantExpressApiGetAllExpressResponse GetAllExpressAsync()
  - GET /merchant/express/getall

- async WechatResponse GetAllExpressRawAsync(string query)


## WechatMpMerchantExpressApiAddExpressRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpMerchantExpressApiAddExpressRequest()

- WechatMpMerchantExpressApiAddExpressRequest DeliveryTemplate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantExpressApiAddExpressResponse (class)

- public string Raw;


## WechatMpMerchantExpressApiDeleteExpressRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantExpressApiDeleteExpressRequest()

- WechatMpMerchantExpressApiDeleteExpressRequest TemplateId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantExpressApiDeleteExpressResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantExpressApiGetAllExpressResponse (class)

- public string Raw;


## WechatMpMerchantExpressApiGetByIdExpressRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantExpressApiGetByIdExpressRequest()

- WechatMpMerchantExpressApiGetByIdExpressRequest TemplateId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantExpressApiGetByIdExpressResponse (class)

- public string Raw;


## WechatMpMerchantExpressApiUpDateExpressRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantExpressApiUpDateExpressRequest()

- WechatMpMerchantExpressApiUpDateExpressRequest TemplateId(int fieldValue)

- WechatMpMerchantExpressApiUpDateExpressRequest DeliveryTemplate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantExpressApiUpDateExpressResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantGroupApi (class)

MerChant/Group/GroupApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpMerchantGroupApi(WechatClient client)

- async WechatMpMerchantGroupApiAddGroupResponse AddGroupAsync(WechatMpMerchantGroupApiAddGroupRequest request)
  - POST /merchant/group/add

- async WechatResponse AddGroupRawAsync(string query, string jsonBody)

- async WechatMpMerchantGroupApiDeleteGroupResponse DeleteGroupAsync(WechatMpMerchantGroupApiDeleteGroupRequest request)
  - POST /merchant/group/del

- async WechatResponse DeleteGroupRawAsync(string query, string jsonBody)

- async WechatMpMerchantGroupApiPropertyModGroupResponse PropertyModGroupAsync(WechatMpMerchantGroupApiPropertyModGroupRequest request)
  - POST /merchant/group/propertymod

- async WechatResponse PropertyModGroupRawAsync(string query, string jsonBody)

- async WechatMpMerchantGroupApiProductModGroupResponse ProductModGroupAsync(WechatMpMerchantGroupApiProductModGroupRequest request)
  - POST /merchant/group/productmod

- async WechatResponse ProductModGroupRawAsync(string query, string jsonBody)

- async WechatMpMerchantGroupApiGetAllGroupResponse GetAllGroupAsync()
  - GET /merchant/group/getall

- async WechatResponse GetAllGroupRawAsync(string query)

- async WechatMpMerchantGroupApiGetByIdGroupResponse GetByIdGroupAsync(WechatMpMerchantGroupApiGetByIdGroupRequest request)
  - POST /merchant/group/getbyid

- async WechatResponse GetByIdGroupRawAsync(string query, string jsonBody)


## WechatMpMerchantGroupApiAddGroupRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpMerchantGroupApiAddGroupRequest()

- WechatMpMerchantGroupApiAddGroupRequest GroupDetail(WechatMpGroupDetail fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantGroupApiAddGroupResponse (class)

- public string Raw;


## WechatMpMerchantGroupApiDeleteGroupRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantGroupApiDeleteGroupRequest()

- WechatMpMerchantGroupApiDeleteGroupRequest GroupId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantGroupApiDeleteGroupResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantGroupApiGetAllGroupResponse (class)

- public string Raw;


## WechatMpMerchantGroupApiGetByIdGroupRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantGroupApiGetByIdGroupRequest()

- WechatMpMerchantGroupApiGetByIdGroupRequest GroupId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantGroupApiGetByIdGroupResponse (class)

- public string Raw;


## WechatMpMerchantGroupApiProductModGroupRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantGroupApiProductModGroupRequest()

- WechatMpMerchantGroupApiProductModGroupRequest GroupId(int fieldValue)

- WechatMpMerchantGroupApiProductModGroupRequest Product(List<WechatMpProduct> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantGroupApiProductModGroupResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantGroupApiPropertyModGroupRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantGroupApiPropertyModGroupRequest()

- WechatMpMerchantGroupApiPropertyModGroupRequest GroupId(int fieldValue)

- WechatMpMerchantGroupApiPropertyModGroupRequest GroupName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantGroupApiPropertyModGroupResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantOrderApi (class)

MerChant/Order/OrderApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpMerchantOrderApi(WechatClient client)

- async WechatMpMerchantOrderApiGetByIdOrderResponse GetByIdOrderAsync(WechatMpMerchantOrderApiGetByIdOrderRequest request)
  - POST /merchant/order/getbyid

- async WechatResponse GetByIdOrderRawAsync(string query, string jsonBody)

- async WechatMpMerchantOrderApiGetByFilterOrderResponse GetByFilterOrderAsync(WechatMpMerchantOrderApiGetByFilterOrderRequest request)
  - POST /merchant/order/getbyfilter

- async WechatResponse GetByFilterOrderRawAsync(string query, string jsonBody)

- async WechatMpMerchantOrderApiSetdeliveryOrderResponse SetdeliveryOrderAsync(WechatMpMerchantOrderApiSetdeliveryOrderRequest request)
  - POST /merchant/order/setdelivery

- async WechatResponse SetdeliveryOrderRawAsync(string query, string jsonBody)

- async WechatMpMerchantOrderApiCloseOrderResponse CloseOrderAsync(WechatMpMerchantOrderApiCloseOrderRequest request)
  - POST /merchant/order/close

- async WechatResponse CloseOrderRawAsync(string query, string jsonBody)


## WechatMpMerchantOrderApiCloseOrderRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantOrderApiCloseOrderRequest()

- WechatMpMerchantOrderApiCloseOrderRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantOrderApiCloseOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantOrderApiGetByFilterOrderRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantOrderApiGetByFilterOrderRequest()

- WechatMpMerchantOrderApiGetByFilterOrderRequest Status(int fieldValue)

- WechatMpMerchantOrderApiGetByFilterOrderRequest BeginTime(string fieldValue)

- WechatMpMerchantOrderApiGetByFilterOrderRequest EndTime(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantOrderApiGetByFilterOrderResponse (class)

- public string Raw;


## WechatMpMerchantOrderApiGetByIdOrderRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpMerchantOrderApiGetByIdOrderRequest()

- WechatMpMerchantOrderApiGetByIdOrderRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantOrderApiGetByIdOrderResponse (class)

- public string Raw;


## WechatMpMerchantOrderApiSetdeliveryOrderRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantOrderApiSetdeliveryOrderRequest()

- WechatMpMerchantOrderApiSetdeliveryOrderRequest OrderId(string fieldValue)

- WechatMpMerchantOrderApiSetdeliveryOrderRequest DeliveryCompany(string fieldValue)

- WechatMpMerchantOrderApiSetdeliveryOrderRequest DeliveryTrackNo(string fieldValue)

- WechatMpMerchantOrderApiSetdeliveryOrderRequest NeedDelivery(int fieldValue)

- WechatMpMerchantOrderApiSetdeliveryOrderRequest IsOthers(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantOrderApiSetdeliveryOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantProductApi (class)

MerChant/Product/ProductApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpMerchantProductApi(WechatClient client)

- async WechatMpMerchantProductApiAddProductResponse AddProductAsync(WechatMpMerchantProductApiAddProductRequest request)
  - POST /merchant/create

- async WechatResponse AddProductRawAsync(string query, string jsonBody)

- async WechatMpMerchantProductApiDeleteProductResponse DeleteProductAsync(WechatMpMerchantProductApiDeleteProductRequest request)
  - POST /merchant/del

- async WechatResponse DeleteProductRawAsync(string query, string jsonBody)

- async WechatMpMerchantProductApiUpDateProductResponse UpDateProductAsync(WechatMpMerchantProductApiUpDateProductRequest request)
  - POST /merchant/update

- async WechatResponse UpDateProductRawAsync(string query, string jsonBody)

- async WechatMpMerchantProductApiGetProductResponse GetProductAsync(WechatMpMerchantProductApiGetProductRequest request)
  - POST /merchant/get

- async WechatResponse GetProductRawAsync(string query, string jsonBody)

- async WechatMpMerchantProductApiGetByStatusResponse GetByStatusAsync(WechatMpMerchantProductApiGetByStatusRequest request)
  - POST /merchant/getbystatus

- async WechatResponse GetByStatusRawAsync(string query, string jsonBody)

- async WechatMpMerchantProductApiModProductStatusResponse ModProductStatusAsync(WechatMpMerchantProductApiModProductStatusRequest request)
  - POST /merchant/modproductstatus

- async WechatResponse ModProductStatusRawAsync(string query, string jsonBody)

- async WechatMpMerchantProductApiGetSubResponse GetSubAsync(WechatMpMerchantProductApiGetSubRequest request)
  - POST /merchant/category/getsub

- async WechatResponse GetSubRawAsync(string query, string jsonBody)

- async WechatMpMerchantProductApiGetSkuResponse GetSkuAsync(WechatMpMerchantProductApiGetSkuRequest request)
  - POST /merchant/category/getsku

- async WechatResponse GetSkuRawAsync(string query, string jsonBody)

- async WechatMpMerchantProductApiGetPropertyResponse GetPropertyAsync(WechatMpMerchantProductApiGetPropertyRequest request)
  - POST /merchant/category/getproperty

- async WechatResponse GetPropertyRawAsync(string query, string jsonBody)


## WechatMpMerchantProductApiAddProductRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpMerchantProductApiAddProductRequest()

- WechatMpMerchantProductApiAddProductRequest ProductBase(WechatMpProductBase fieldValue)

- WechatMpMerchantProductApiAddProductRequest SkuList(List<WechatMpSkuList> fieldValue)

- WechatMpMerchantProductApiAddProductRequest Attrext(WechatMpAttrext fieldValue)

- WechatMpMerchantProductApiAddProductRequest DeliveryInfo(WechatMpDeliveryInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantProductApiAddProductResponse (class)

- public string Raw;


## WechatMpMerchantProductApiDeleteProductRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantProductApiDeleteProductRequest()

- WechatMpMerchantProductApiDeleteProductRequest ProductId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantProductApiDeleteProductResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantProductApiGetByStatusRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantProductApiGetByStatusRequest()

- WechatMpMerchantProductApiGetByStatusRequest Status(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantProductApiGetByStatusResponse (class)

- public string Raw;


## WechatMpMerchantProductApiGetProductRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantProductApiGetProductRequest()

- WechatMpMerchantProductApiGetProductRequest ProductId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantProductApiGetProductResponse (class)

- public string Raw;


## WechatMpMerchantProductApiGetPropertyRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantProductApiGetPropertyRequest()

- WechatMpMerchantProductApiGetPropertyRequest CateId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantProductApiGetPropertyResponse (class)

- public string Raw;


## WechatMpMerchantProductApiGetSkuRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantProductApiGetSkuRequest()

- WechatMpMerchantProductApiGetSkuRequest CateId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantProductApiGetSkuResponse (class)

- public string Raw;


## WechatMpMerchantProductApiGetSubRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantProductApiGetSubRequest()

- WechatMpMerchantProductApiGetSubRequest CateId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantProductApiGetSubResponse (class)

- public string Raw;


## WechatMpMerchantProductApiModProductStatusRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantProductApiModProductStatusRequest()

- WechatMpMerchantProductApiModProductStatusRequest ProductId(string fieldValue)

- WechatMpMerchantProductApiModProductStatusRequest Status(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantProductApiModProductStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantProductApiUpDateProductRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantProductApiUpDateProductRequest()

- WechatMpMerchantProductApiUpDateProductRequest ProductId(string fieldValue)

- WechatMpMerchantProductApiUpDateProductRequest ProductBase(WechatMpProductBase fieldValue)

- WechatMpMerchantProductApiUpDateProductRequest SkuList(List<WechatMpSkuList> fieldValue)

- WechatMpMerchantProductApiUpDateProductRequest Attrext(WechatMpAttrext fieldValue)

- WechatMpMerchantProductApiUpDateProductRequest DeliveryInfo(WechatMpDeliveryInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantProductApiUpDateProductResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantShelfApi (class)

MerChant/Shelf/ShelfApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpMerchantShelfApi(WechatClient client)

- async WechatMpMerchantShelfApiAddShelvesResponse AddShelvesAsync(WechatMpMerchantShelfApiAddShelvesRequest request)
  - POST /merchant/shelf/add

- async WechatResponse AddShelvesRawAsync(string query, string jsonBody)

- async WechatMpMerchantShelfApiDeleteShelvesResponse DeleteShelvesAsync(WechatMpMerchantShelfApiDeleteShelvesRequest request)
  - POST /merchant/shelf/del

- async WechatResponse DeleteShelvesRawAsync(string query, string jsonBody)

- async WechatMpMerchantShelfApiModShelvesResponse ModShelvesAsync(WechatMpMerchantShelfApiModShelvesRequest request)
  - POST /merchant/shelf/mod

- async WechatResponse ModShelvesRawAsync(string query, string jsonBody)

- async WechatMpMerchantShelfApiGetAllShelvesResponse GetAllShelvesAsync()
  - GET /merchant/shelf/getall

- async WechatResponse GetAllShelvesRawAsync(string query)

- async WechatMpMerchantShelfApiGetByIdShelvesResponse GetByIdShelvesAsync(WechatMpMerchantShelfApiGetByIdShelvesRequest request)
  - POST /merchant/shelf/getbyid

- async WechatResponse GetByIdShelvesRawAsync(string query, string jsonBody)


## WechatMpMerchantShelfApiAddShelvesRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpMerchantShelfApiAddShelvesRequest()

- WechatMpMerchantShelfApiAddShelvesRequest GroupInfo(WechatMpM1GroupInfo fieldValue)

- WechatMpMerchantShelfApiAddShelvesRequest Eid(int fieldValue)

- WechatMpMerchantShelfApiAddShelvesRequest GroupInfos(WechatMpM2GroupInfos fieldValue)

- WechatMpMerchantShelfApiAddShelvesRequest ShelfBanner(string fieldValue)

- WechatMpMerchantShelfApiAddShelvesRequest ShelfName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantShelfApiAddShelvesResponse (class)

- public string Raw;


## WechatMpMerchantShelfApiDeleteShelvesRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantShelfApiDeleteShelvesRequest()

- WechatMpMerchantShelfApiDeleteShelvesRequest ShelfId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantShelfApiDeleteShelvesResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantShelfApiGetAllShelvesResponse (class)

- public string Raw;


## WechatMpMerchantShelfApiGetByIdShelvesRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantShelfApiGetByIdShelvesRequest()

- WechatMpMerchantShelfApiGetByIdShelvesRequest ShelfId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantShelfApiGetByIdShelvesResponse (class)

- public string Raw;


## WechatMpMerchantShelfApiModShelvesRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantShelfApiModShelvesRequest()

- WechatMpMerchantShelfApiModShelvesRequest GroupInfo(WechatMpM1GroupInfo fieldValue)

- WechatMpMerchantShelfApiModShelvesRequest Eid(int fieldValue)

- WechatMpMerchantShelfApiModShelvesRequest GroupInfos(WechatMpM2GroupInfos fieldValue)

- WechatMpMerchantShelfApiModShelvesRequest ShelfId(int fieldValue)

- WechatMpMerchantShelfApiModShelvesRequest ShelfBanner(string fieldValue)

- WechatMpMerchantShelfApiModShelvesRequest ShelfName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantShelfApiModShelvesResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantSpecialApi (class)

微信小店图片原始二进制上传。

- WechatClient client;

- public WechatMpMerchantSpecialApi(WechatClient client)

- async WechatMpSpecialPictureResponse UploadImgAsync(string fileName, string contentType, string fileBytes)


## WechatMpMerchantStockApi (class)

MerChant/Stock/StockApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpMerchantStockApi(WechatClient client)

- async WechatMpMerchantStockApiAddStockResponse AddStockAsync(WechatMpMerchantStockApiAddStockRequest request)
  - POST /merchant/stock/add

- async WechatResponse AddStockRawAsync(string query, string jsonBody)

- async WechatMpMerchantStockApiReduceStockResponse ReduceStockAsync(WechatMpMerchantStockApiReduceStockRequest request)
  - POST /merchant/stock/reduce

- async WechatResponse ReduceStockRawAsync(string query, string jsonBody)


## WechatMpMerchantStockApiAddStockRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpMerchantStockApiAddStockRequest()

- WechatMpMerchantStockApiAddStockRequest ProductId(string fieldValue)

- WechatMpMerchantStockApiAddStockRequest SkuInfo(string fieldValue)

- WechatMpMerchantStockApiAddStockRequest Quantity(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantStockApiAddStockResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpMerchantStockApiReduceStockRequest (class)

- WechatTypedRequest request;

- public WechatMpMerchantStockApiReduceStockRequest()

- WechatMpMerchantStockApiReduceStockRequest ProductId(string fieldValue)

- WechatMpMerchantStockApiReduceStockRequest SkuInfo(string fieldValue)

- WechatMpMerchantStockApiReduceStockRequest Quantity(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpMerchantStockApiReduceStockResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpNewTmplApi (class)

NewTmpl/NewTmplApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpNewTmplApi(WechatClient client)

- async WechatMpNewTmplApiBizSendResponse BizSendAsync(WechatMpNewTmplApiBizSendRequest request)
  - POST /cgi-bin/message/subscribe/bizsend

- async WechatResponse BizSendRawAsync(string query, string jsonBody)

- async WechatMpNewTmplApiGetPubTemplateTitlesResponse GetPubTemplateTitlesAsync(WechatMpNewTmplApiGetPubTemplateTitlesRequest request)
  - GET /wxaapi/newtmpl/getpubtemplatetitles

- async WechatResponse GetPubTemplateTitlesRawAsync(string query)

- async WechatMpNewTmplApiGetPubTemplateKeyWordsByIdResponse GetPubTemplateKeyWordsByIdAsync(WechatMpNewTmplApiGetPubTemplateKeyWordsByIdRequest request)
  - GET /wxaapi/newtmpl/getpubtemplatekeywords

- async WechatResponse GetPubTemplateKeyWordsByIdRawAsync(string query)

- async WechatMpNewTmplApiAddTemplateResponse AddTemplateAsync(WechatMpNewTmplApiAddTemplateRequest request)
  - POST /wxaapi/newtmpl/addtemplate

- async WechatResponse AddTemplateRawAsync(string query, string jsonBody)

- async WechatMpNewTmplApiGetTemplateListResponse GetTemplateListAsync()
  - GET /wxaapi/newtmpl/gettemplate

- async WechatResponse GetTemplateListRawAsync(string query)

- async WechatMpNewTmplApiGetCategoryResponse GetCategoryAsync()
  - GET /wxaapi/newtmpl/getcategory

- async WechatResponse GetCategoryRawAsync(string query)

- async WechatMpNewTmplApiDelTemplateResponse DelTemplateAsync(WechatMpNewTmplApiDelTemplateRequest request)
  - POST /wxaapi/newtmpl/deltemplate

- async WechatResponse DelTemplateRawAsync(string query, string jsonBody)


## WechatMpNewTmplApiAddTemplateRequest (class)

- WechatTypedRequest request;

- public WechatMpNewTmplApiAddTemplateRequest()

- WechatMpNewTmplApiAddTemplateRequest Tid(string fieldValue)

- WechatMpNewTmplApiAddTemplateRequest KidList(List<int> fieldValue)

- WechatMpNewTmplApiAddTemplateRequest SceneDesc(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNewTmplApiAddTemplateResponse (class)

- public string Raw;


## WechatMpNewTmplApiBizSendRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpNewTmplApiBizSendRequest()

- WechatMpNewTmplApiBizSendRequest Appid(string fieldValue)

- WechatMpNewTmplApiBizSendRequest Pagepath(string fieldValue)

- WechatMpNewTmplApiBizSendRequest ToUser(string fieldValue)

- WechatMpNewTmplApiBizSendRequest TemplateId(string fieldValue)

- WechatMpNewTmplApiBizSendRequest Data(JsonValue fieldValue)

- WechatMpNewTmplApiBizSendRequest Page(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNewTmplApiBizSendResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpNewTmplApiDelTemplateRequest (class)

- WechatTypedRequest request;

- public WechatMpNewTmplApiDelTemplateRequest()

- WechatMpNewTmplApiDelTemplateRequest PriTmplId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNewTmplApiDelTemplateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpNewTmplApiGetCategoryResponse (class)

- public string Raw;


## WechatMpNewTmplApiGetPubTemplateKeyWordsByIdRequest (class)

- WechatTypedRequest request;

- public WechatMpNewTmplApiGetPubTemplateKeyWordsByIdRequest()

- WechatMpNewTmplApiGetPubTemplateKeyWordsByIdRequest Tid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNewTmplApiGetPubTemplateKeyWordsByIdResponse (class)

- public string Raw;


## WechatMpNewTmplApiGetPubTemplateTitlesRequest (class)

- WechatTypedRequest request;

- public WechatMpNewTmplApiGetPubTemplateTitlesRequest()

- WechatMpNewTmplApiGetPubTemplateTitlesRequest Ids(string fieldValue)

- WechatMpNewTmplApiGetPubTemplateTitlesRequest Start(int fieldValue)

- WechatMpNewTmplApiGetPubTemplateTitlesRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNewTmplApiGetPubTemplateTitlesResponse (class)

- public string Raw;


## WechatMpNewTmplApiGetTemplateListResponse (class)

- public string Raw;


## WechatMpNonTaxPayApi (class)

NonTaxPay/NonTaxPayApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpNonTaxPayApi(WechatClient client)

- async WechatMpNonTaxPayApiQueryFeeResponse QueryFeeAsync(WechatMpNonTaxPayApiQueryFeeRequest request)
  - POST /nontax/queryfee

- async WechatResponse QueryFeeRawAsync(string query, string jsonBody)

- async WechatMpNonTaxPayApiUnifiedOrderResponse UnifiedOrderAsync(WechatMpNonTaxPayApiUnifiedOrderRequest request)
  - POST /nontax/unifiedorder

- async WechatResponse UnifiedOrderRawAsync(string query, string jsonBody)

- async WechatMpNonTaxPayApiNotifyInconsistentOrderResponse NotifyInconsistentOrderAsync(WechatMpNonTaxPayApiNotifyInconsistentOrderRequest request)
  - POST /nontax/notifyinconsistentorder

- async WechatResponse NotifyInconsistentOrderRawAsync(string query, string jsonBody)

- async WechatMpNonTaxPayApiMockNotificationResponse MockNotificationAsync(WechatMpNonTaxPayApiMockNotificationRequest request)
  - POST /nontax/mocknotification

- async WechatResponse MockNotificationRawAsync(string query, string jsonBody)

- async WechatMpNonTaxPayApiMockQueryFeeResponse MockQueryFeeAsync(WechatMpNonTaxPayApiMockQueryFeeRequest request)
  - POST /nontax/mockqueryfee

- async WechatResponse MockQueryFeeRawAsync(string query, string jsonBody)

- async WechatMpNonTaxPayApiMicroPayResponse MicroPayAsync(WechatMpNonTaxPayApiMicroPayRequest request)
  - POST /nontax/micropay

- async WechatResponse MicroPayRawAsync(string query, string jsonBody)

- async WechatMpNonTaxPayApiGetOrderListResponse GetOrderListAsync(WechatMpNonTaxPayApiGetOrderListRequest request)
  - POST /nontax/getorderlist

- async WechatResponse GetOrderListRawAsync(string query, string jsonBody)

- async WechatMpNonTaxPayApiRefundResponse RefundAsync(WechatMpNonTaxPayApiRefundRequest request)
  - POST /nontax/refund

- async WechatResponse RefundRawAsync(string query, string jsonBody)

- async WechatMpNonTaxPayApiGetOrderResponse GetOrderAsync(WechatMpNonTaxPayApiGetOrderRequest request)
  - POST /nontax/getorder

- async WechatResponse GetOrderRawAsync(string query, string jsonBody)


## WechatMpNonTaxPayApiGetOrderListRequest (class)

- WechatTypedRequest request;

- public WechatMpNonTaxPayApiGetOrderListRequest()

- WechatMpNonTaxPayApiGetOrderListRequest Appid(string fieldValue)

- WechatMpNonTaxPayApiGetOrderListRequest RegionCode(string fieldValue)

- WechatMpNonTaxPayApiGetOrderListRequest DepartmentCode(string fieldValue)

- WechatMpNonTaxPayApiGetOrderListRequest PaymentNoticeNo(string fieldValue)

- WechatMpNonTaxPayApiGetOrderListRequest OrderNo(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNonTaxPayApiGetOrderListResponse (class)

- public string Raw;


## WechatMpNonTaxPayApiGetOrderRequest (class)

- WechatTypedRequest request;

- public WechatMpNonTaxPayApiGetOrderRequest()

- WechatMpNonTaxPayApiGetOrderRequest Appid(string fieldValue)

- WechatMpNonTaxPayApiGetOrderRequest ServiceId(long fieldValue)

- WechatMpNonTaxPayApiGetOrderRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNonTaxPayApiGetOrderResponse (class)

- public string Raw;


## WechatMpNonTaxPayApiMicroPayRequest (class)

- WechatTypedRequest request;

- public WechatMpNonTaxPayApiMicroPayRequest()

- WechatMpNonTaxPayApiMicroPayRequest Appid(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest BankId(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest BankAccount(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest MchId(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest Desc(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest Fee(long fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest UserName(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest Items(List<WechatMpNonTaxFeeItem> fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest PaymentNoticeCreateTime(long fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest PaymentExpireDate(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest PaymentNoticeNo(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest OrderNo(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest DepartmentCode(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest DepartmentName(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest PaymentNoticeType(int fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest RegionCode(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest AuthCode(string fieldValue)

- WechatMpNonTaxPayApiMicroPayRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNonTaxPayApiMicroPayResponse (class)

- public string Raw;


## WechatMpNonTaxPayApiMockNotificationRequest (class)

- WechatTypedRequest request;

- public WechatMpNonTaxPayApiMockNotificationRequest()

- WechatMpNonTaxPayApiMockNotificationRequest Appid(string fieldValue)

- WechatMpNonTaxPayApiMockNotificationRequest Url(string fieldValue)

- WechatMpNonTaxPayApiMockNotificationRequest Version(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNonTaxPayApiMockNotificationResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpNonTaxPayApiMockQueryFeeRequest (class)

- WechatTypedRequest request;

- public WechatMpNonTaxPayApiMockQueryFeeRequest()

- WechatMpNonTaxPayApiMockQueryFeeRequest Appid(string fieldValue)

- WechatMpNonTaxPayApiMockQueryFeeRequest Url(string fieldValue)

- WechatMpNonTaxPayApiMockQueryFeeRequest Version(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNonTaxPayApiMockQueryFeeResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpNonTaxPayApiNotifyInconsistentOrderRequest (class)

- WechatTypedRequest request;

- public WechatMpNonTaxPayApiNotifyInconsistentOrderRequest()

- WechatMpNonTaxPayApiNotifyInconsistentOrderRequest Appid(string fieldValue)

- WechatMpNonTaxPayApiNotifyInconsistentOrderRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNonTaxPayApiNotifyInconsistentOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpNonTaxPayApiQueryFeeRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpNonTaxPayApiQueryFeeRequest()

- WechatMpNonTaxPayApiQueryFeeRequest Appid(string fieldValue)

- WechatMpNonTaxPayApiQueryFeeRequest ServiceId(long fieldValue)

- WechatMpNonTaxPayApiQueryFeeRequest BankId(string fieldValue)

- WechatMpNonTaxPayApiQueryFeeRequest PaymentNoticeNo(string fieldValue)

- WechatMpNonTaxPayApiQueryFeeRequest DepartmentCode(string fieldValue)

- WechatMpNonTaxPayApiQueryFeeRequest PaymentNoticeType(int fieldValue)

- WechatMpNonTaxPayApiQueryFeeRequest RegionCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNonTaxPayApiQueryFeeResponse (class)

- public string Raw;


## WechatMpNonTaxPayApiRefundRequest (class)

- WechatTypedRequest request;

- public WechatMpNonTaxPayApiRefundRequest()

- WechatMpNonTaxPayApiRefundRequest Appid(string fieldValue)

- WechatMpNonTaxPayApiRefundRequest OrderId(string fieldValue)

- WechatMpNonTaxPayApiRefundRequest Reason(string fieldValue)

- WechatMpNonTaxPayApiRefundRequest RefundFee(long fieldValue)

- WechatMpNonTaxPayApiRefundRequest RefundOutId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNonTaxPayApiRefundResponse (class)

- public string Raw;


## WechatMpNonTaxPayApiUnifiedOrderRequest (class)

- WechatTypedRequest request;

- public WechatMpNonTaxPayApiUnifiedOrderRequest()

- WechatMpNonTaxPayApiUnifiedOrderRequest Appid(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest ServiceId(long fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest BankId(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest BankAccount(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest MchId(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest Openid(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest Desc(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest Fee(long fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest ReturnUrl(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest Ip(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest OrderNo(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest PaymentNoticeNo(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest DepartmentCode(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest DepartmentName(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest PaymentNoticeType(int fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest RegionCode(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest UserName(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest Items(List<WechatMpNonTaxFeeItem> fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest PaymentNoticeCreateTime(long fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest PaymentExpireDate(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest Scene(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest AppAppid(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest TradeType(string fieldValue)

- WechatMpNonTaxPayApiUnifiedOrderRequest AutoCallPay(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpNonTaxPayApiUnifiedOrderResponse (class)

- public string Raw;


## WechatMpNonTaxSpecialApi (class)

非税缴费对账单下载；成功时 内容 为原始账单文本。

- WechatClient client;

- public WechatMpNonTaxSpecialApi(WechatClient client)

- async WechatMpSpecialNonTaxBillResponse DownloadBillAsync(WechatMpNonTaxDownloadBillRequest request)


## WechatMpOneCodeApi (class)

OneCode/OneCodeApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpOneCodeApi(WechatClient client)

- async WechatMpOneCodeApiApplyCodeQueryResponse ApplyCodeQueryAsync(WechatMpOneCodeApiApplyCodeQueryRequest request)
  - POST /intp/marketcode/applycodequery

- async WechatResponse ApplyCodeQueryRawAsync(string query, string jsonBody)

- async WechatMpOneCodeApiApplyCodeDownloadResponse ApplyCodeDownloadAsync(WechatMpOneCodeApiApplyCodeDownloadRequest request)
  - POST /intp/marketcode/applycodedownload

- async WechatResponse ApplyCodeDownloadRawAsync(string query, string jsonBody)

- async WechatMpOneCodeApiCodeActiveResponse CodeActiveAsync(WechatMpOneCodeApiCodeActiveRequest request)
  - POST /intp/marketcode/codeactive

- async WechatResponse CodeActiveRawAsync(string query, string jsonBody)

- async WechatMpOneCodeApiCodeActiveQueryResponse CodeActiveQueryAsync(WechatMpOneCodeApiCodeActiveQueryRequest request)
  - POST /intp/marketcode/codeactivequery

- async WechatResponse CodeActiveQueryRawAsync(string query, string jsonBody)

- async WechatMpOneCodeApiTicketToCodeResponse TicketToCodeAsync(WechatMpOneCodeApiTicketToCodeRequest request)
  - POST /intp/marketcode/tickettocode

- async WechatResponse TicketToCodeRawAsync(string query, string jsonBody)

- async WechatMpOneCodeApiApplyCodeResponse ApplyCodeAsync(WechatMpOneCodeApiApplyCodeRequest request)
  - POST /intp/marketcode/applycode

- async WechatResponse ApplyCodeRawAsync(string query, string jsonBody)


## WechatMpOneCodeApiApplyCodeDownloadRequest (class)

- WechatTypedRequest request;

- public WechatMpOneCodeApiApplyCodeDownloadRequest()

- WechatMpOneCodeApiApplyCodeDownloadRequest ApplicationId(long fieldValue)

- WechatMpOneCodeApiApplyCodeDownloadRequest CodeStart(long fieldValue)

- WechatMpOneCodeApiApplyCodeDownloadRequest CodeEnd(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpOneCodeApiApplyCodeDownloadResponse (class)

- public string Raw;


## WechatMpOneCodeApiApplyCodeQueryRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpOneCodeApiApplyCodeQueryRequest()

- WechatMpOneCodeApiApplyCodeQueryRequest ApplicationId(long fieldValue)

- WechatMpOneCodeApiApplyCodeQueryRequest IsvApplicationId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpOneCodeApiApplyCodeQueryResponse (class)

- public string Raw;


## WechatMpOneCodeApiApplyCodeRequest (class)

- WechatTypedRequest request;

- public WechatMpOneCodeApiApplyCodeRequest()

- WechatMpOneCodeApiApplyCodeRequest CodeCount(long fieldValue)

- WechatMpOneCodeApiApplyCodeRequest IsvApplicationId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpOneCodeApiApplyCodeResponse (class)

- public string Raw;


## WechatMpOneCodeApiCodeActiveQueryRequest (class)

- WechatTypedRequest request;

- public WechatMpOneCodeApiCodeActiveQueryRequest()

- WechatMpOneCodeApiCodeActiveQueryRequest ApplicationId(long fieldValue)

- WechatMpOneCodeApiCodeActiveQueryRequest CodeIndex(long fieldValue)

- WechatMpOneCodeApiCodeActiveQueryRequest CodeUrl(string fieldValue)

- WechatMpOneCodeApiCodeActiveQueryRequest Code(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpOneCodeApiCodeActiveQueryResponse (class)

- public string Raw;


## WechatMpOneCodeApiCodeActiveRequest (class)

- WechatTypedRequest request;

- public WechatMpOneCodeApiCodeActiveRequest()

- WechatMpOneCodeApiCodeActiveRequest ApplicationId(long fieldValue)

- WechatMpOneCodeApiCodeActiveRequest ActivityName(string fieldValue)

- WechatMpOneCodeApiCodeActiveRequest ProductBrand(string fieldValue)

- WechatMpOneCodeApiCodeActiveRequest ProductTitle(string fieldValue)

- WechatMpOneCodeApiCodeActiveRequest ProductCode(string fieldValue)

- WechatMpOneCodeApiCodeActiveRequest WxaAppid(string fieldValue)

- WechatMpOneCodeApiCodeActiveRequest WxaPath(string fieldValue)

- WechatMpOneCodeApiCodeActiveRequest WxaType(int fieldValue)

- WechatMpOneCodeApiCodeActiveRequest CodeStart(long fieldValue)

- WechatMpOneCodeApiCodeActiveRequest CodeEnd(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpOneCodeApiCodeActiveResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpOneCodeApiTicketToCodeRequest (class)

- WechatTypedRequest request;

- public WechatMpOneCodeApiTicketToCodeRequest()

- WechatMpOneCodeApiTicketToCodeRequest Openid(string fieldValue)

- WechatMpOneCodeApiTicketToCodeRequest CodeTicket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpOneCodeApiTicketToCodeResponse (class)

- public string Raw;


## WechatMpPoiApi (class)

Poi/PoiApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpPoiApi(WechatClient client)

- async WechatMpPoiApiAddPoiResponse AddPoiAsync(WechatMpPoiApiAddPoiRequest request)
  - POST /cgi-bin/poi/addpoi

- async WechatResponse AddPoiRawAsync(string query, string jsonBody)

- async WechatMpPoiApiGetPoiResponse GetPoiAsync(WechatMpPoiApiGetPoiRequest request)
  - POST /cgi-bin/poi/getpoi

- async WechatResponse GetPoiRawAsync(string query, string jsonBody)

- async WechatMpPoiApiGetPoiListResponse GetPoiListAsync(WechatMpPoiApiGetPoiListRequest request)
  - POST /cgi-bin/poi/getpoilist

- async WechatResponse GetPoiListRawAsync(string query, string jsonBody)

- async WechatMpPoiApiDeletePoiResponse DeletePoiAsync(WechatMpPoiApiDeletePoiRequest request)
  - POST /cgi-bin/poi/delpoi

- async WechatResponse DeletePoiRawAsync(string query, string jsonBody)

- async WechatMpPoiApiUpdatePoiResponse UpdatePoiAsync(WechatMpPoiApiUpdatePoiRequest request)
  - POST /cgi-bin/poi/updatepoi

- async WechatResponse UpdatePoiRawAsync(string query, string jsonBody)

- async WechatMpPoiApiGetCategoryResponse GetCategoryAsync()
  - GET /cgi-bin/poi/getwxcategory

- async WechatResponse GetCategoryRawAsync(string query)


## WechatMpPoiApiAddPoiRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpPoiApiAddPoiRequest()

- WechatMpPoiApiAddPoiRequest Business(WechatMpCreateStoreBusiness fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpPoiApiAddPoiResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpPoiApiDeletePoiRequest (class)

- WechatTypedRequest request;

- public WechatMpPoiApiDeletePoiRequest()

- WechatMpPoiApiDeletePoiRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpPoiApiDeletePoiResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpPoiApiGetCategoryResponse (class)

- public string Raw;


## WechatMpPoiApiGetPoiListRequest (class)

- WechatTypedRequest request;

- public WechatMpPoiApiGetPoiListRequest()

- WechatMpPoiApiGetPoiListRequest Begin(int fieldValue)

- WechatMpPoiApiGetPoiListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpPoiApiGetPoiListResponse (class)

- public string Raw;


## WechatMpPoiApiGetPoiRequest (class)

- WechatTypedRequest request;

- public WechatMpPoiApiGetPoiRequest()

- WechatMpPoiApiGetPoiRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpPoiApiGetPoiResponse (class)

- public string Raw;


## WechatMpPoiApiUpdatePoiRequest (class)

- WechatTypedRequest request;

- public WechatMpPoiApiUpdatePoiRequest()

- WechatMpPoiApiUpdatePoiRequest Business(WechatMpUpdateStoreBusiness fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpPoiApiUpdatePoiResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpPoiSpecialApi (class)

门店图片上传。

- WechatClient client;

- public WechatMpPoiSpecialApi(WechatClient client)

- async WechatMpSpecialPoiUploadImageResponse UploadImageAsync(string fileName, string contentType, string fileBytes)


## WechatMpQrCodeApi (class)

QrCode/QrCodeApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpQrCodeApi(WechatClient client)

- async WechatMpQrCodeApiCreateResponse CreateAsync(WechatMpQrCodeApiCreateRequest request)
  - POST /cgi-bin/qrcode/create

- async WechatResponse CreateRawAsync(string query, string jsonBody)


## WechatMpQrCodeApiCreateRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpQrCodeApiCreateRequest()

- WechatMpQrCodeApiCreateRequest ExpireSeconds(int fieldValue)

- WechatMpQrCodeApiCreateRequest SceneId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpQrCodeApiCreateResponse (class)

- public string Raw;


## WechatMpQrCodeSpecialApi (class)

二维码图片下载。该端点属于 mp.weixin.qq.com，且不需要 access token。

- async WechatRawResponse ShowQrCodeAsync(string ticket)


## WechatMpRequest (class)

公众号 Advanced API 的公共 JSON 请求层。

所有生成的模块只保存“方法名 -> HTTP verb/路径”的映射；Token 缓存、HTTPS、
JSON 响应和微信错误码仍统一由 WechatClient / WechatResponse 处理。
query 不含开头的 '?'；没有 query 时传空串。

- static string BuildPath(string path, string query)

- static async WechatResponse GetAsync(WechatClient client, string path, string query)

- static async WechatResponse PostAsync(WechatClient client, string path, string query, string jsonBody)


## WechatMpScanApi (class)

Scan/ScanApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpScanApi(WechatClient client)

- async WechatMpScanApiMerchantInfoGetResponse MerchantInfoGetAsync()
  - GET /scan/merchantinfo/get

- async WechatResponse MerchantInfoGetRawAsync(string query)

- async WechatMpScanApiProductCreateResponse ProductCreateAsync(WechatMpScanApiProductCreateRequest request)
  - POST /scan/product/create

- async WechatResponse ProductCreateRawAsync(string query, string jsonBody)

- async WechatMpScanApiModStatusResponse ModStatusAsync(WechatMpScanApiModStatusRequest request)
  - POST /scan/product/modstatus

- async WechatResponse ModStatusRawAsync(string query, string jsonBody)

- async WechatMpScanApiTestWhiteListSetResponse TestWhiteListSetAsync(WechatMpScanApiTestWhiteListSetRequest request)
  - POST /scan/testwhitelist/set

- async WechatResponse TestWhiteListSetRawAsync(string query, string jsonBody)

- async WechatMpScanApiProductGetListResponse ProductGetListAsync(WechatMpScanApiProductGetListRequest request)
  - POST /scan/product/getlist

- async WechatResponse ProductGetListRawAsync(string query, string jsonBody)

- async WechatMpScanApiProductClearResponse ProductClearAsync(WechatMpScanApiProductClearRequest request)
  - POST /scan/product/clear

- async WechatResponse ProductClearRawAsync(string query, string jsonBody)

- async WechatMpScanApiScanTicketCheckResponse ScanTicketCheckAsync(WechatMpScanApiScanTicketCheckRequest request)
  - POST /scan/scanticket/check

- async WechatResponse ScanTicketCheckRawAsync(string query, string jsonBody)

- async WechatMpScanApiGetQrCodeResponse GetQrCodeAsync(WechatMpScanApiGetQrCodeRequest request)
  - POST /scan/product/getqrcode

- async WechatResponse GetQrCodeRawAsync(string query, string jsonBody)

- async WechatMpScanApiUpdateBrandResponse UpdateBrandAsync(WechatMpScanApiUpdateBrandRequest request)
  - POST /scan/product/update

- async WechatResponse UpdateBrandRawAsync(string query, string jsonBody)


## WechatMpScanApiGetQrCodeRequest (class)

- WechatTypedRequest request;

- public WechatMpScanApiGetQrCodeRequest()

- WechatMpScanApiGetQrCodeRequest Keystandard(string fieldValue)

- WechatMpScanApiGetQrCodeRequest Keystr(string fieldValue)

- WechatMpScanApiGetQrCodeRequest Extinfo(string fieldValue)

- WechatMpScanApiGetQrCodeRequest QrcodeSize(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpScanApiGetQrCodeResponse (class)

- public string Raw;


## WechatMpScanApiMerchantInfoGetResponse (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- public string Raw;


## WechatMpScanApiModStatusRequest (class)

- WechatTypedRequest request;

- public WechatMpScanApiModStatusRequest()

- WechatMpScanApiModStatusRequest KeyStandard(string fieldValue)

- WechatMpScanApiModStatusRequest KeyStr(string fieldValue)

- WechatMpScanApiModStatusRequest Status(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpScanApiModStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpScanApiProductClearRequest (class)

- WechatTypedRequest request;

- public WechatMpScanApiProductClearRequest()

- WechatMpScanApiProductClearRequest KeyStandard(int fieldValue)

- WechatMpScanApiProductClearRequest KeyStr(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpScanApiProductClearResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpScanApiProductCreateRequest (class)

- WechatTypedRequest request;

- public WechatMpScanApiProductCreateRequest()

- WechatMpScanApiProductCreateRequest BaseInfo(WechatMpBrandInfoBaseInfo fieldValue)

- WechatMpScanApiProductCreateRequest DetailInfo(WechatMpBrandInfoDetailInfo fieldValue)

- WechatMpScanApiProductCreateRequest ActionInfo(WechatMpBrandInfoActionInfo fieldValue)

- WechatMpScanApiProductCreateRequest ModuleInfo(WechatMpBrandInfoModuleInfo fieldValue)

- WechatMpScanApiProductCreateRequest KeyStandard(string fieldValue)

- WechatMpScanApiProductCreateRequest KeyStr(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpScanApiProductCreateResponse (class)

- public string Raw;


## WechatMpScanApiProductGetListRequest (class)

- WechatTypedRequest request;

- public WechatMpScanApiProductGetListRequest()

- WechatMpScanApiProductGetListRequest Offset(int fieldValue)

- WechatMpScanApiProductGetListRequest Limit(int fieldValue)

- WechatMpScanApiProductGetListRequest Status(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpScanApiProductGetListResponse (class)

- public string Raw;


## WechatMpScanApiScanTicketCheckRequest (class)

- WechatTypedRequest request;

- public WechatMpScanApiScanTicketCheckRequest()

- WechatMpScanApiScanTicketCheckRequest Ticket(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpScanApiScanTicketCheckResponse (class)

- public string Raw;


## WechatMpScanApiTestWhiteListSetRequest (class)

- WechatTypedRequest request;

- public WechatMpScanApiTestWhiteListSetRequest()

- WechatMpScanApiTestWhiteListSetRequest OpenId(string fieldValue)

- WechatMpScanApiTestWhiteListSetRequest UserName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpScanApiTestWhiteListSetResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpScanApiUpdateBrandRequest (class)

- WechatTypedRequest request;

- public WechatMpScanApiUpdateBrandRequest()

- WechatMpScanApiUpdateBrandRequest Keystandard(string fieldValue)

- WechatMpScanApiUpdateBrandRequest Keystr(string fieldValue)

- WechatMpScanApiUpdateBrandRequest BrandInfo(WechatMpBrandInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpScanApiUpdateBrandResponse (class)

- public string Raw;


## WechatMpSemanticApi (class)

Semantic/SemanticApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpSemanticApi(WechatClient client)

- async WechatMpSemanticApiSemanticSendResponse SemanticSendAsync(WechatMpSemanticApiSemanticSendRequest request)
  - POST /semantic/semproxy/search

- async WechatResponse SemanticSendRawAsync(string query, string jsonBody)


## WechatMpSemanticApiSemanticSendRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpSemanticApiSemanticSendRequest()

- WechatMpSemanticApiSemanticSendRequest Query(string fieldValue)

- WechatMpSemanticApiSemanticSendRequest Category(string fieldValue)

- WechatMpSemanticApiSemanticSendRequest Latitude(double fieldValue)

- WechatMpSemanticApiSemanticSendRequest Longitude(double fieldValue)

- WechatMpSemanticApiSemanticSendRequest City(string fieldValue)

- WechatMpSemanticApiSemanticSendRequest Region(string fieldValue)

- WechatMpSemanticApiSemanticSendRequest Appid(string fieldValue)

- WechatMpSemanticApiSemanticSendRequest Uid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpSemanticApiSemanticSendResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpShakeAroundApi (class)

ShakeAround/ShakeAroundApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpShakeAroundApi(WechatClient client)

- async WechatMpShakeAroundApiRegisterResponse RegisterAsync(WechatMpShakeAroundApiRegisterRequest request)
  - POST /shakearound/account/register

- async WechatResponse RegisterRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiGetAuditStatusResponse GetAuditStatusAsync()
  - GET /shakearound/account/auditstatus

- async WechatResponse GetAuditStatusRawAsync(string query)

- async WechatMpShakeAroundApiDeviceApplyResponse DeviceApplyAsync(WechatMpShakeAroundApiDeviceApplyRequest request)
  - POST /shakearound/device/applyid

- async WechatResponse DeviceApplyRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiDeviceApplyStatusResponse DeviceApplyStatusAsync(WechatMpShakeAroundApiDeviceApplyStatusRequest request)
  - POST /shakearound/device/applystatus

- async WechatResponse DeviceApplyStatusRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiDeviceUpdateResponse DeviceUpdateAsync(WechatMpShakeAroundApiDeviceUpdateRequest request)
  - POST /shakearound/device/update

- async WechatResponse DeviceUpdateRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiDeviceBindLocatoinResponse DeviceBindLocatoinAsync(WechatMpShakeAroundApiDeviceBindLocatoinRequest request)
  - POST /shakearound/device/bindlocation

- async WechatResponse DeviceBindLocatoinRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiSearchDeviceByIdResponse SearchDeviceByIdAsync(WechatMpShakeAroundApiSearchDeviceByIdRequest request)
  - POST /shakearound/device/search

- async WechatResponse SearchDeviceByIdRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiSearchDeviceByRangeResponse SearchDeviceByRangeAsync(WechatMpShakeAroundApiSearchDeviceByRangeRequest request)
  - POST /shakearound/device/search

- async WechatResponse SearchDeviceByRangeRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiSearchDeviceByApplyIdResponse SearchDeviceByApplyIdAsync(WechatMpShakeAroundApiSearchDeviceByApplyIdRequest request)
  - POST /shakearound/device/search

- async WechatResponse SearchDeviceByApplyIdRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiAddPageResponse AddPageAsync(WechatMpShakeAroundApiAddPageRequest request)
  - POST /shakearound/page/add

- async WechatResponse AddPageRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiUpdatePageResponse UpdatePageAsync(WechatMpShakeAroundApiUpdatePageRequest request)
  - POST /shakearound/page/update

- async WechatResponse UpdatePageRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiSearchPagesByPageIdResponse SearchPagesByPageIdAsync(WechatMpShakeAroundApiSearchPagesByPageIdRequest request)
  - POST /shakearound/page/search

- async WechatResponse SearchPagesByPageIdRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiSearchPagesByRangeResponse SearchPagesByRangeAsync(WechatMpShakeAroundApiSearchPagesByRangeRequest request)
  - POST /shakearound/page/search

- async WechatResponse SearchPagesByRangeRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiDeletePageResponse DeletePageAsync(WechatMpShakeAroundApiDeletePageRequest request)
  - POST /shakearound/page/delete

- async WechatResponse DeletePageRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiBindPageResponse BindPageAsync(WechatMpShakeAroundApiBindPageRequest request)
  - POST /shakearound/device/bindpage

- async WechatResponse BindPageRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiRelationSearchResponse RelationSearchAsync(WechatMpShakeAroundApiRelationSearchRequest request)
  - POST /shakearound/relation/search

- async WechatResponse RelationSearchRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiGetShakeInfoResponse GetShakeInfoAsync(WechatMpShakeAroundApiGetShakeInfoRequest request)
  - POST /shakearound/user/getshakeinfo

- async WechatResponse GetShakeInfoRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiStatisticsByDeviceResponse StatisticsByDeviceAsync(WechatMpShakeAroundApiStatisticsByDeviceRequest request)
  - POST /shakearound/statistics/device

- async WechatResponse StatisticsByDeviceRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiStatisticsByPageResponse StatisticsByPageAsync(WechatMpShakeAroundApiStatisticsByPageRequest request)
  - POST /shakearound/statistics/page

- async WechatResponse StatisticsByPageRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiDeviceListResponse DeviceListAsync(WechatMpShakeAroundApiDeviceListRequest request)
  - POST /shakearound/statistics/devicelist

- async WechatResponse DeviceListRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiPageListResponse PageListAsync(WechatMpShakeAroundApiPageListRequest request)
  - POST /shakearound/statistics/pagelist

- async WechatResponse PageListRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiGroupAddResponse GroupAddAsync(WechatMpShakeAroundApiGroupAddRequest request)
  - POST /shakearound/device/group/add

- async WechatResponse GroupAddRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiGroupUpdateResponse GroupUpdateAsync(WechatMpShakeAroundApiGroupUpdateRequest request)
  - POST /shakearound/device/group/update

- async WechatResponse GroupUpdateRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiGroupDeleteResponse GroupDeleteAsync(WechatMpShakeAroundApiGroupDeleteRequest request)
  - POST /shakearound/device/group/delete

- async WechatResponse GroupDeleteRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiGroupGetListResponse GroupGetListAsync(WechatMpShakeAroundApiGroupGetListRequest request)
  - POST /shakearound/device/group/getlist

- async WechatResponse GroupGetListRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiGroupGetDetailResponse GroupGetDetailAsync(WechatMpShakeAroundApiGroupGetDetailRequest request)
  - POST /shakearound/device/group/getdetail

- async WechatResponse GroupGetDetailRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiGroupGetAdddeviceResponse GroupGetAdddeviceAsync(WechatMpShakeAroundApiGroupGetAdddeviceRequest request)
  - POST /shakearound/device/group/adddevice

- async WechatResponse GroupGetAdddeviceRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiGroupDeleteDeviceResponse GroupDeleteDeviceAsync(WechatMpShakeAroundApiGroupDeleteDeviceRequest request)
  - POST /shakearound/device/group/deletedevice

- async WechatResponse GroupDeleteDeviceRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiAddLotteryInfoResponse AddLotteryInfoAsync(WechatMpShakeAroundApiAddLotteryInfoRequest request)
  - POST /shakearound/lottery/addlotteryinfo

- async WechatResponse AddLotteryInfoRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiSetPrizeBucketResponse SetPrizeBucketAsync(WechatMpShakeAroundApiSetPrizeBucketRequest request)
  - POST /shakearound/lottery/setprizebucket

- async WechatResponse SetPrizeBucketRawAsync(string query, string jsonBody)

- async WechatMpShakeAroundApiSetLotterySwitchResponse SetLotterySwitchAsync(WechatMpShakeAroundApiSetLotterySwitchRequest request)
  - GET /shakearound/lottery/setlotteryswitch

- async WechatResponse SetLotterySwitchRawAsync(string query)

- async WechatMpShakeAroundApiQueryLotteryResponse QueryLotteryAsync(WechatMpShakeAroundApiQueryLotteryRequest request)
  - GET /shakearound/lottery/querylottery

- async WechatResponse QueryLotteryRawAsync(string query)


## WechatMpShakeAroundApiAddLotteryInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiAddLotteryInfoRequest()

- WechatMpShakeAroundApiAddLotteryInfoRequest Title(string fieldValue)

- WechatMpShakeAroundApiAddLotteryInfoRequest Desc(string fieldValue)

- WechatMpShakeAroundApiAddLotteryInfoRequest Onoff(int fieldValue)

- WechatMpShakeAroundApiAddLotteryInfoRequest BeginTime(long fieldValue)

- WechatMpShakeAroundApiAddLotteryInfoRequest ExpireTime(long fieldValue)

- WechatMpShakeAroundApiAddLotteryInfoRequest SponsorAppid(string fieldValue)

- WechatMpShakeAroundApiAddLotteryInfoRequest Total(long fieldValue)

- WechatMpShakeAroundApiAddLotteryInfoRequest JumpUrl(string fieldValue)

- WechatMpShakeAroundApiAddLotteryInfoRequest Key(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiAddLotteryInfoResponse (class)

- public string Raw;


## WechatMpShakeAroundApiAddPageRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiAddPageRequest()

- WechatMpShakeAroundApiAddPageRequest Title(string fieldValue)

- WechatMpShakeAroundApiAddPageRequest Description(string fieldValue)

- WechatMpShakeAroundApiAddPageRequest PageUrl(string fieldValue)

- WechatMpShakeAroundApiAddPageRequest Comment(string fieldValue)

- WechatMpShakeAroundApiAddPageRequest IconUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiAddPageResponse (class)

- public string Raw;


## WechatMpShakeAroundApiBindPageRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiBindPageRequest()

- WechatMpShakeAroundApiBindPageRequest DeviceId(long fieldValue)

- WechatMpShakeAroundApiBindPageRequest Uuid(string fieldValue)

- WechatMpShakeAroundApiBindPageRequest Major(long fieldValue)

- WechatMpShakeAroundApiBindPageRequest Minor(long fieldValue)

- WechatMpShakeAroundApiBindPageRequest PageIds(List<long> fieldValue)

- WechatMpShakeAroundApiBindPageRequest BindType(int fieldValue)

- WechatMpShakeAroundApiBindPageRequest AppendType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiBindPageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpShakeAroundApiDeletePageRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiDeletePageRequest()

- WechatMpShakeAroundApiDeletePageRequest PageId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiDeletePageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpShakeAroundApiDeviceApplyRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiDeviceApplyRequest()

- WechatMpShakeAroundApiDeviceApplyRequest Quantity(int fieldValue)

- WechatMpShakeAroundApiDeviceApplyRequest ApplyReason(string fieldValue)

- WechatMpShakeAroundApiDeviceApplyRequest Comment(string fieldValue)

- WechatMpShakeAroundApiDeviceApplyRequest PoiId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiDeviceApplyResponse (class)

- public string Raw;


## WechatMpShakeAroundApiDeviceApplyStatusRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiDeviceApplyStatusRequest()

- WechatMpShakeAroundApiDeviceApplyStatusRequest AppId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiDeviceApplyStatusResponse (class)

- public string Raw;


## WechatMpShakeAroundApiDeviceBindLocatoinRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiDeviceBindLocatoinRequest()

- WechatMpShakeAroundApiDeviceBindLocatoinRequest DeviceId(long fieldValue)

- WechatMpShakeAroundApiDeviceBindLocatoinRequest PoiId(long fieldValue)

- WechatMpShakeAroundApiDeviceBindLocatoinRequest Type(int fieldValue)

- WechatMpShakeAroundApiDeviceBindLocatoinRequest PoiAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiDeviceBindLocatoinResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpShakeAroundApiDeviceListRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiDeviceListRequest()

- WechatMpShakeAroundApiDeviceListRequest Date(long fieldValue)

- WechatMpShakeAroundApiDeviceListRequest PageIndex(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiDeviceListResponse (class)

- public string Raw;


## WechatMpShakeAroundApiDeviceUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiDeviceUpdateRequest()

- WechatMpShakeAroundApiDeviceUpdateRequest DeviceId(long fieldValue)

- WechatMpShakeAroundApiDeviceUpdateRequest UuId(string fieldValue)

- WechatMpShakeAroundApiDeviceUpdateRequest Major(long fieldValue)

- WechatMpShakeAroundApiDeviceUpdateRequest Minor(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiDeviceUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpShakeAroundApiGetAuditStatusResponse (class)

- public string Raw;


## WechatMpShakeAroundApiGetShakeInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiGetShakeInfoRequest()

- WechatMpShakeAroundApiGetShakeInfoRequest Ticket(string fieldValue)

- WechatMpShakeAroundApiGetShakeInfoRequest NeedPoi(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiGetShakeInfoResponse (class)

- public string Raw;


## WechatMpShakeAroundApiGroupAddRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiGroupAddRequest()

- WechatMpShakeAroundApiGroupAddRequest GroupName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiGroupAddResponse (class)

- public string Raw;


## WechatMpShakeAroundApiGroupDeleteDeviceRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiGroupDeleteDeviceRequest()

- WechatMpShakeAroundApiGroupDeleteDeviceRequest DeviceId(long fieldValue)

- WechatMpShakeAroundApiGroupDeleteDeviceRequest Uuid(string fieldValue)

- WechatMpShakeAroundApiGroupDeleteDeviceRequest Major(long fieldValue)

- WechatMpShakeAroundApiGroupDeleteDeviceRequest Minor(long fieldValue)

- WechatMpShakeAroundApiGroupDeleteDeviceRequest GroupId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiGroupDeleteDeviceResponse (class)

- public string Raw;


## WechatMpShakeAroundApiGroupDeleteRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiGroupDeleteRequest()

- WechatMpShakeAroundApiGroupDeleteRequest GroupId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiGroupDeleteResponse (class)

- public string Raw;


## WechatMpShakeAroundApiGroupGetAdddeviceRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiGroupGetAdddeviceRequest()

- WechatMpShakeAroundApiGroupGetAdddeviceRequest GroupId(string fieldValue)

- WechatMpShakeAroundApiGroupGetAdddeviceRequest DeviceIdentifiers(List<WechatMpDeviceApplyDataDeviceIdentifiers> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiGroupGetAdddeviceResponse (class)

- public string Raw;


## WechatMpShakeAroundApiGroupGetDetailRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiGroupGetDetailRequest()

- WechatMpShakeAroundApiGroupGetDetailRequest GroupId(string fieldValue)

- WechatMpShakeAroundApiGroupGetDetailRequest Begin(int fieldValue)

- WechatMpShakeAroundApiGroupGetDetailRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiGroupGetDetailResponse (class)

- public string Raw;


## WechatMpShakeAroundApiGroupGetListRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiGroupGetListRequest()

- WechatMpShakeAroundApiGroupGetListRequest Begin(int fieldValue)

- WechatMpShakeAroundApiGroupGetListRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiGroupGetListResponse (class)

- public string Raw;


## WechatMpShakeAroundApiGroupUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiGroupUpdateRequest()

- WechatMpShakeAroundApiGroupUpdateRequest Groupid(string fieldValue)

- WechatMpShakeAroundApiGroupUpdateRequest GroupName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiGroupUpdateResponse (class)

- public string Raw;


## WechatMpShakeAroundApiPageListRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiPageListRequest()

- WechatMpShakeAroundApiPageListRequest Date(long fieldValue)

- WechatMpShakeAroundApiPageListRequest PageIndex(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiPageListResponse (class)

- public string Raw;


## WechatMpShakeAroundApiQueryLotteryRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiQueryLotteryRequest()

- WechatMpShakeAroundApiQueryLotteryRequest LotteryId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiQueryLotteryResponse (class)

- public string Raw;


## WechatMpShakeAroundApiRegisterRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpShakeAroundApiRegisterRequest()

- WechatMpShakeAroundApiRegisterRequest Name(string fieldValue)

- WechatMpShakeAroundApiRegisterRequest PhoneNumber(string fieldValue)

- WechatMpShakeAroundApiRegisterRequest Email(string fieldValue)

- WechatMpShakeAroundApiRegisterRequest QualificationCertUrls(List<string> fieldValue)

- WechatMpShakeAroundApiRegisterRequest ApplyReason(string fieldValue)

- WechatMpShakeAroundApiRegisterRequest IndustryId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiRegisterResponse (class)

- public string Raw;


## WechatMpShakeAroundApiRelationSearchRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiRelationSearchRequest()

- WechatMpShakeAroundApiRelationSearchRequest DeviceId(long fieldValue)

- WechatMpShakeAroundApiRelationSearchRequest Uuid(string fieldValue)

- WechatMpShakeAroundApiRelationSearchRequest Major(long fieldValue)

- WechatMpShakeAroundApiRelationSearchRequest Minor(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiRelationSearchResponse (class)

- public string Raw;


## WechatMpShakeAroundApiSearchDeviceByApplyIdRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiSearchDeviceByApplyIdRequest()

- WechatMpShakeAroundApiSearchDeviceByApplyIdRequest ApplyId(long fieldValue)

- WechatMpShakeAroundApiSearchDeviceByApplyIdRequest Begin(int fieldValue)

- WechatMpShakeAroundApiSearchDeviceByApplyIdRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiSearchDeviceByApplyIdResponse (class)

- public string Raw;


## WechatMpShakeAroundApiSearchDeviceByIdRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiSearchDeviceByIdRequest()

- WechatMpShakeAroundApiSearchDeviceByIdRequest DeviceIdentifiers(List<WechatMpDeviceApplyDataDeviceIdentifiers> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiSearchDeviceByIdResponse (class)

- public string Raw;


## WechatMpShakeAroundApiSearchDeviceByRangeRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiSearchDeviceByRangeRequest()

- WechatMpShakeAroundApiSearchDeviceByRangeRequest Begin(int fieldValue)

- WechatMpShakeAroundApiSearchDeviceByRangeRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiSearchDeviceByRangeResponse (class)

- public string Raw;


## WechatMpShakeAroundApiSearchPagesByPageIdRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiSearchPagesByPageIdRequest()

- WechatMpShakeAroundApiSearchPagesByPageIdRequest PageIds(List<long> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiSearchPagesByPageIdResponse (class)

- public string Raw;


## WechatMpShakeAroundApiSearchPagesByRangeRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiSearchPagesByRangeRequest()

- WechatMpShakeAroundApiSearchPagesByRangeRequest LastSeen(long fieldValue)

- WechatMpShakeAroundApiSearchPagesByRangeRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiSearchPagesByRangeResponse (class)

- public string Raw;


## WechatMpShakeAroundApiSetLotterySwitchRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiSetLotterySwitchRequest()

- WechatMpShakeAroundApiSetLotterySwitchRequest LotteryId(string fieldValue)

- WechatMpShakeAroundApiSetLotterySwitchRequest OnOff(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiSetLotterySwitchResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpShakeAroundApiSetPrizeBucketRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiSetPrizeBucketRequest()

- WechatMpShakeAroundApiSetPrizeBucketRequest Ticket(string fieldValue)

- WechatMpShakeAroundApiSetPrizeBucketRequest LotteryId(string fieldValue)

- WechatMpShakeAroundApiSetPrizeBucketRequest Mchid(string fieldValue)

- WechatMpShakeAroundApiSetPrizeBucketRequest SponsorAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiSetPrizeBucketResponse (class)

- public string Raw;


## WechatMpShakeAroundApiStatisticsByDeviceRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiStatisticsByDeviceRequest()

- WechatMpShakeAroundApiStatisticsByDeviceRequest DeviceId(long fieldValue)

- WechatMpShakeAroundApiStatisticsByDeviceRequest Uuid(string fieldValue)

- WechatMpShakeAroundApiStatisticsByDeviceRequest Major(long fieldValue)

- WechatMpShakeAroundApiStatisticsByDeviceRequest Minor(long fieldValue)

- WechatMpShakeAroundApiStatisticsByDeviceRequest BeginDate(long fieldValue)

- WechatMpShakeAroundApiStatisticsByDeviceRequest EndDate(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiStatisticsByDeviceResponse (class)

- public string Raw;


## WechatMpShakeAroundApiStatisticsByPageRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiStatisticsByPageRequest()

- WechatMpShakeAroundApiStatisticsByPageRequest PageId(long fieldValue)

- WechatMpShakeAroundApiStatisticsByPageRequest BeginDate(long fieldValue)

- WechatMpShakeAroundApiStatisticsByPageRequest EndDate(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiStatisticsByPageResponse (class)

- public string Raw;


## WechatMpShakeAroundApiUpdatePageRequest (class)

- WechatTypedRequest request;

- public WechatMpShakeAroundApiUpdatePageRequest()

- WechatMpShakeAroundApiUpdatePageRequest PageId(long fieldValue)

- WechatMpShakeAroundApiUpdatePageRequest Title(string fieldValue)

- WechatMpShakeAroundApiUpdatePageRequest Description(string fieldValue)

- WechatMpShakeAroundApiUpdatePageRequest PageUrl(string fieldValue)

- WechatMpShakeAroundApiUpdatePageRequest Comment(string fieldValue)

- WechatMpShakeAroundApiUpdatePageRequest IconUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpShakeAroundApiUpdatePageResponse (class)

- public string Raw;


## WechatMpShakeAroundSpecialApi (class)

摇一摇周边素材上传。

- WechatClient client;

- public WechatMpShakeAroundSpecialApi(WechatClient client)

- async WechatMpSpecialShakeUploadImageResponse UploadImageAsync(string fileName, string contentType, string fileBytes)


## WechatMpSpecialAiCropResponse (class)

- public string Raw;


## WechatMpSpecialDrivingLicenseResponse (class)

- public string Raw;


## WechatMpSpecialNonTaxBillResponse (class)

- public string Raw;


## WechatMpSpecialOperationResponse (class)

- public int errcode;

- public string errmsg;

- public string Raw;


## WechatMpSpecialPictureResponse (class)

- public string Raw;


## WechatMpSpecialPoiUploadImageResponse (class)

- public string Raw;


## WechatMpSpecialProtocol (class)

- static string MediaType(WechatMpMediaType mediaType)

- static bool LooksLikeJson(string body)

- static string VideoDescription(string title, string introduction)


## WechatMpSpecialQrCodeResponse (class)

- public string Raw;


## WechatMpSpecialSetPdfResponse (class)

- public string Raw;


## WechatMpSpecialShakeUploadImageResponse (class)

- public string Raw;


## WechatMpSpecialUploadForeverMediaResponse (class)

- public string Raw;


## WechatMpSpecialUploadImgResponse (class)

- public string Raw;


## WechatMpSpecialUploadTemporaryMediaResponse (class)

- public string Raw;


## WechatMpTemplateMessageTemplateApi (class)

TemplateMessage/TemplateApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpTemplateMessageTemplateApi(WechatClient client)

- async WechatMpTemplateMessageTemplateApiSendTemplateMessageResponse SendTemplateMessageAsync(WechatMpTemplateMessageTemplateApiSendTemplateMessageRequest request)
  - POST /cgi-bin/message/template/send

- async WechatResponse SendTemplateMessageRawAsync(string query, string jsonBody)

- async WechatMpTemplateMessageTemplateApiSetIndustryResponse SetIndustryAsync(WechatMpTemplateMessageTemplateApiSetIndustryRequest request)
  - POST /cgi-bin/template/api_set_industry

- async WechatResponse SetIndustryRawAsync(string query, string jsonBody)

- async WechatMpTemplateMessageTemplateApiGetIndustryResponse GetIndustryAsync()
  - GET /cgi-bin/template/get_industry

- async WechatResponse GetIndustryRawAsync(string query)

- async WechatMpTemplateMessageTemplateApiAddTemplateResponse AddTemplateAsync(WechatMpTemplateMessageTemplateApiAddTemplateRequest request)
  - POST /cgi-bin/template/api_add_template

- async WechatResponse AddTemplateRawAsync(string query, string jsonBody)

- async WechatMpTemplateMessageTemplateApiGetPrivateTemplateResponse GetPrivateTemplateAsync()
  - GET /cgi-bin/template/get_all_private_template

- async WechatResponse GetPrivateTemplateRawAsync(string query)

- async WechatMpTemplateMessageTemplateApiDelPrivateTemplateResponse DelPrivateTemplateAsync(WechatMpTemplateMessageTemplateApiDelPrivateTemplateRequest request)
  - POST /cgi-bin/template/del_private_template

- async WechatResponse DelPrivateTemplateRawAsync(string query, string jsonBody)

- async WechatMpTemplateMessageTemplateApiSubscribeResponse SubscribeAsync(WechatMpTemplateMessageTemplateApiSubscribeRequest request)
  - POST /cgi-bin/message/template/subscribe

- async WechatResponse SubscribeRawAsync(string query, string jsonBody)

- async WechatMpTemplateMessageTemplateApiQueryBlockTemplateMessageResponse QueryBlockTemplateMessageAsync(WechatMpTemplateMessageTemplateApiQueryBlockTemplateMessageRequest request)
  - POST /wxa/sec/queryblocktmplmsg

- async WechatResponse QueryBlockTemplateMessageRawAsync(string query, string jsonBody)


## WechatMpTemplateMessageTemplateApiAddTemplateRequest (class)

- WechatTypedRequest request;

- public WechatMpTemplateMessageTemplateApiAddTemplateRequest()

- WechatMpTemplateMessageTemplateApiAddTemplateRequest TemplateIdShort(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpTemplateMessageTemplateApiAddTemplateResponse (class)

- public string Raw;


## WechatMpTemplateMessageTemplateApiDelPrivateTemplateRequest (class)

- WechatTypedRequest request;

- public WechatMpTemplateMessageTemplateApiDelPrivateTemplateRequest()

- WechatMpTemplateMessageTemplateApiDelPrivateTemplateRequest TemplateId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpTemplateMessageTemplateApiDelPrivateTemplateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpTemplateMessageTemplateApiGetIndustryResponse (class)

- public string Raw;


## WechatMpTemplateMessageTemplateApiGetPrivateTemplateResponse (class)

- public string Raw;


## WechatMpTemplateMessageTemplateApiQueryBlockTemplateMessageRequest (class)

- WechatTypedRequest request;

- public WechatMpTemplateMessageTemplateApiQueryBlockTemplateMessageRequest()

- WechatMpTemplateMessageTemplateApiQueryBlockTemplateMessageRequest TemplateMessageId(string fieldValue)

- WechatMpTemplateMessageTemplateApiQueryBlockTemplateMessageRequest LargestId(long fieldValue)

- WechatMpTemplateMessageTemplateApiQueryBlockTemplateMessageRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpTemplateMessageTemplateApiQueryBlockTemplateMessageResponse (class)

- public string Raw;


## WechatMpTemplateMessageTemplateApiSendTemplateMessageRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpTemplateMessageTemplateApiSendTemplateMessageRequest()

- WechatMpTemplateMessageTemplateApiSendTemplateMessageRequest Appid(string fieldValue)

- WechatMpTemplateMessageTemplateApiSendTemplateMessageRequest Pagepath(string fieldValue)

- WechatMpTemplateMessageTemplateApiSendTemplateMessageRequest OpenId(string fieldValue)

- WechatMpTemplateMessageTemplateApiSendTemplateMessageRequest TemplateId(string fieldValue)

- WechatMpTemplateMessageTemplateApiSendTemplateMessageRequest Url(string fieldValue)

- WechatMpTemplateMessageTemplateApiSendTemplateMessageRequest Data(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpTemplateMessageTemplateApiSendTemplateMessageResponse (class)

- public string Raw;


## WechatMpTemplateMessageTemplateApiSetIndustryRequest (class)

- WechatTypedRequest request;

- public WechatMpTemplateMessageTemplateApiSetIndustryRequest()

- WechatMpTemplateMessageTemplateApiSetIndustryRequest IndustryId1(int fieldValue)

- WechatMpTemplateMessageTemplateApiSetIndustryRequest IndustryId2(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpTemplateMessageTemplateApiSetIndustryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpTemplateMessageTemplateApiSubscribeRequest (class)

- WechatTypedRequest request;

- public WechatMpTemplateMessageTemplateApiSubscribeRequest()

- WechatMpTemplateMessageTemplateApiSubscribeRequest ToUserOpenId(string fieldValue)

- WechatMpTemplateMessageTemplateApiSubscribeRequest TemplateId(string fieldValue)

- WechatMpTemplateMessageTemplateApiSubscribeRequest Scene(string fieldValue)

- WechatMpTemplateMessageTemplateApiSubscribeRequest Title(string fieldValue)

- WechatMpTemplateMessageTemplateApiSubscribeRequest Data(JsonValue fieldValue)

- WechatMpTemplateMessageTemplateApiSubscribeRequest Url(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpTemplateMessageTemplateApiSubscribeResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpUrlApi (class)

Url/UrlApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpUrlApi(WechatClient client)

- async WechatMpUrlApiShortUrlResponse ShortUrlAsync(WechatMpUrlApiShortUrlRequest request)
  - POST /cgi-bin/shorturl

- async WechatResponse ShortUrlRawAsync(string query, string jsonBody)

- async WechatMpUrlApiGenerateShortenResponse GenerateShortenAsync(WechatMpUrlApiGenerateShortenRequest request)
  - POST /cgi-bin/shorten/gen

- async WechatResponse GenerateShortenRawAsync(string query, string jsonBody)

- async WechatMpUrlApiFetchShortenResponse FetchShortenAsync(WechatMpUrlApiFetchShortenRequest request)
  - POST /cgi-bin/shorten/fetch

- async WechatResponse FetchShortenRawAsync(string query, string jsonBody)


## WechatMpUrlApiFetchShortenRequest (class)

- WechatTypedRequest request;

- public WechatMpUrlApiFetchShortenRequest()

- WechatMpUrlApiFetchShortenRequest ShortKey(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUrlApiFetchShortenResponse (class)

- public string Raw;


## WechatMpUrlApiGenerateShortenRequest (class)

- WechatTypedRequest request;

- public WechatMpUrlApiGenerateShortenRequest()

- WechatMpUrlApiGenerateShortenRequest LongData(string fieldValue)

- WechatMpUrlApiGenerateShortenRequest ExpireSeconds(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUrlApiGenerateShortenResponse (class)

- public string Raw;


## WechatMpUrlApiShortUrlRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpUrlApiShortUrlRequest()

- WechatMpUrlApiShortUrlRequest Action(string fieldValue)

- WechatMpUrlApiShortUrlRequest LongUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUrlApiShortUrlResponse (class)

- public string Raw;


## WechatMpUserApi (class)

User/UserApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpUserApi(WechatClient client)

- async WechatMpUserApiInfoResponse InfoAsync(WechatMpUserApiInfoRequest request)
  - GET /cgi-bin/user/info

- async WechatResponse InfoRawAsync(string query)

- async WechatMpUserApiGetResponse GetAsync(WechatMpUserApiGetRequest request)
  - GET /cgi-bin/user/get

- async WechatResponse GetRawAsync(string query)

- async WechatMpUserApiUpdateRemarkResponse UpdateRemarkAsync(WechatMpUserApiUpdateRemarkRequest request)
  - POST /cgi-bin/user/info/updateremark

- async WechatResponse UpdateRemarkRawAsync(string query, string jsonBody)

- async WechatMpUserApiBatchGetUserInfoResponse BatchGetUserInfoAsync(WechatMpUserApiBatchGetUserInfoRequest request)
  - POST /cgi-bin/user/info/batchget

- async WechatResponse BatchGetUserInfoRawAsync(string query, string jsonBody)

- async WechatMpUserApiChangeOpenIdResponse ChangeOpenIdAsync(WechatMpUserApiChangeOpenIdRequest request)
  - POST /cgi-bin/changeopenid

- async WechatResponse ChangeOpenIdRawAsync(string query, string jsonBody)

- async WechatMpUserApiGetBlackListResponse GetBlackListAsync(WechatMpUserApiGetBlackListRequest request)
  - POST /cgi-bin/tags/members/getblacklist

- async WechatResponse GetBlackListRawAsync(string query, string jsonBody)

- async WechatMpUserApiBatchUnBlackListResponse BatchUnBlackListAsync(WechatMpUserApiBatchUnBlackListRequest request)
  - POST /cgi-bin/tags/members/batchunblacklist

- async WechatResponse BatchUnBlackListRawAsync(string query, string jsonBody)

- async WechatMpUserApiBatchBlackListResponse BatchBlackListAsync(WechatMpUserApiBatchBlackListRequest request)
  - POST /cgi-bin/tags/members/batchblacklist

- async WechatResponse BatchBlackListRawAsync(string query, string jsonBody)


## WechatMpUserApiBatchBlackListRequest (class)

- WechatTypedRequest request;

- public WechatMpUserApiBatchBlackListRequest()

- WechatMpUserApiBatchBlackListRequest OpenidList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserApiBatchBlackListResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpUserApiBatchGetUserInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpUserApiBatchGetUserInfoRequest()

- WechatMpUserApiBatchGetUserInfoRequest UserList(List<WechatMpBatchGetUserInfoData> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserApiBatchGetUserInfoResponse (class)

- public string Raw;


## WechatMpUserApiBatchUnBlackListRequest (class)

- WechatTypedRequest request;

- public WechatMpUserApiBatchUnBlackListRequest()

- WechatMpUserApiBatchUnBlackListRequest OpenidList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserApiBatchUnBlackListResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpUserApiChangeOpenIdRequest (class)

- WechatTypedRequest request;

- public WechatMpUserApiChangeOpenIdRequest()

- WechatMpUserApiChangeOpenIdRequest FromAppId(string fieldValue)

- WechatMpUserApiChangeOpenIdRequest OpenIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserApiChangeOpenIdResponse (class)

- public string Raw;


## WechatMpUserApiGetBlackListRequest (class)

- WechatTypedRequest request;

- public WechatMpUserApiGetBlackListRequest()

- WechatMpUserApiGetBlackListRequest BeginOpenId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserApiGetBlackListResponse (class)

- public string Raw;


## WechatMpUserApiGetRequest (class)

- WechatTypedRequest request;

- public WechatMpUserApiGetRequest()

- WechatMpUserApiGetRequest NextOpenId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserApiGetResponse (class)

- public string Raw;


## WechatMpUserApiInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpUserApiInfoRequest()

- WechatMpUserApiInfoRequest OpenId(string fieldValue)

- WechatMpUserApiInfoRequest Lang(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserApiInfoResponse (class)

- public string Raw;


## WechatMpUserApiUpdateRemarkRequest (class)

- WechatTypedRequest request;

- public WechatMpUserApiUpdateRemarkRequest()

- WechatMpUserApiUpdateRemarkRequest OpenId(string fieldValue)

- WechatMpUserApiUpdateRemarkRequest Remark(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserApiUpdateRemarkResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpUserTagApi (class)

UserTag/UserTagApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpUserTagApi(WechatClient client)

- async WechatMpUserTagApiCreateResponse CreateAsync(WechatMpUserTagApiCreateRequest request)
  - POST /cgi-bin/tags/create

- async WechatResponse CreateRawAsync(string query, string jsonBody)

- async WechatMpUserTagApiGetResponse GetAsync()
  - GET /cgi-bin/tags/get

- async WechatResponse GetRawAsync(string query)

- async WechatMpUserTagApiUpdateResponse UpdateAsync(WechatMpUserTagApiUpdateRequest request)
  - POST /cgi-bin/tags/update

- async WechatResponse UpdateRawAsync(string query, string jsonBody)

- async WechatMpUserTagApiDeleteResponse DeleteAsync(WechatMpUserTagApiDeleteRequest request)
  - POST /cgi-bin/tags/delete

- async WechatResponse DeleteRawAsync(string query, string jsonBody)

- async WechatMpUserTagApiBatchTaggingResponse BatchTaggingAsync(WechatMpUserTagApiBatchTaggingRequest request)
  - POST /cgi-bin/tags/members/batchtagging

- async WechatResponse BatchTaggingRawAsync(string query, string jsonBody)

- async WechatMpUserTagApiBatchUntaggingResponse BatchUntaggingAsync(WechatMpUserTagApiBatchUntaggingRequest request)
  - POST /cgi-bin/tags/members/batchuntagging

- async WechatResponse BatchUntaggingRawAsync(string query, string jsonBody)

- async WechatMpUserTagApiUserTagListResponse UserTagListAsync(WechatMpUserTagApiUserTagListRequest request)
  - POST /cgi-bin/tags/getidlist

- async WechatResponse UserTagListRawAsync(string query, string jsonBody)


## WechatMpUserTagApiBatchTaggingRequest (class)

- WechatTypedRequest request;

- public WechatMpUserTagApiBatchTaggingRequest()

- WechatMpUserTagApiBatchTaggingRequest OpenidList(List<string> fieldValue)

- WechatMpUserTagApiBatchTaggingRequest Tagid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserTagApiBatchTaggingResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpUserTagApiBatchUntaggingRequest (class)

- WechatTypedRequest request;

- public WechatMpUserTagApiBatchUntaggingRequest()

- WechatMpUserTagApiBatchUntaggingRequest OpenidList(List<string> fieldValue)

- WechatMpUserTagApiBatchUntaggingRequest Tagid(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserTagApiBatchUntaggingResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpUserTagApiCreateRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpUserTagApiCreateRequest()

- WechatMpUserTagApiCreateRequest Name(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserTagApiCreateResponse (class)

- public string Raw;


## WechatMpUserTagApiDeleteRequest (class)

- WechatTypedRequest request;

- public WechatMpUserTagApiDeleteRequest()

- WechatMpUserTagApiDeleteRequest Id(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserTagApiDeleteResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpUserTagApiGetResponse (class)

- public string Raw;


## WechatMpUserTagApiUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpUserTagApiUpdateRequest()

- WechatMpUserTagApiUpdateRequest Id(int fieldValue)

- WechatMpUserTagApiUpdateRequest Name(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserTagApiUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpUserTagApiUserTagListRequest (class)

- WechatTypedRequest request;

- public WechatMpUserTagApiUserTagListRequest()

- WechatMpUserTagApiUserTagListRequest Openid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpUserTagApiUserTagListResponse (class)

- public string Raw;


## WechatMpWiFiApi (class)

WiFi/WiFiApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpWiFiApi(WechatClient client)

- async WechatMpWiFiApiShopListResponse ShopListAsync(WechatMpWiFiApiShopListRequest request)
  - POST /bizwifi/shop/list

- async WechatResponse ShopListRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiShopGetResponse ShopGetAsync(WechatMpWiFiApiShopGetRequest request)
  - POST /bizwifi/shop/get

- async WechatResponse ShopGetRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiShopUpdateResponse ShopUpdateAsync(WechatMpWiFiApiShopUpdateRequest request)
  - POST /bizwifi/shop/update

- async WechatResponse ShopUpdateRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiShopCleanResponse ShopCleanAsync(WechatMpWiFiApiShopCleanRequest request)
  - POST /bizwifi/shop/clean

- async WechatResponse ShopCleanRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiAddDeviceResponse AddDeviceAsync(WechatMpWiFiApiAddDeviceRequest request)
  - POST /bizwifi/device/add

- async WechatResponse AddDeviceRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiWifeRegisterResponse WifeRegisterAsync(WechatMpWiFiApiWifeRegisterRequest request)
  - POST /bizwifi/apportal/register

- async WechatResponse WifeRegisterRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiGetDeviceListResponse GetDeviceListAsync(WechatMpWiFiApiGetDeviceListRequest request)
  - POST /bizwifi/device/list

- async WechatResponse GetDeviceListRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiDeleteDeviceResponse DeleteDeviceAsync(WechatMpWiFiApiDeleteDeviceRequest request)
  - POST /bizwifi/device/delete

- async WechatResponse DeleteDeviceRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiGetQrcodeResponse GetQrcodeAsync(WechatMpWiFiApiGetQrcodeRequest request)
  - POST /bizwifi/qrcode/get

- async WechatResponse GetQrcodeRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiSetHomePageResponse SetHomePageAsync(WechatMpWiFiApiSetHomePageRequest request)
  - POST /bizwifi/homepage/set

- async WechatResponse SetHomePageRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiGetHomePageaResponse GetHomePageaAsync(WechatMpWiFiApiGetHomePageaRequest request)
  - POST /bizwifi/homepage/get

- async WechatResponse GetHomePageaRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiSetBarResponse SetBarAsync(WechatMpWiFiApiSetBarRequest request)
  - POST /bizwifi/bar/set

- async WechatResponse SetBarRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiSetFinishpageResponse SetFinishpageAsync(WechatMpWiFiApiSetFinishpageRequest request)
  - POST /bizwifi/finishpage/set

- async WechatResponse SetFinishpageRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiGetStatisticsResponse GetStatisticsAsync(WechatMpWiFiApiGetStatisticsRequest request)
  - POST /bizwifi/statistics/list

- async WechatResponse GetStatisticsRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiSetCouponPutResponse SetCouponPutAsync(WechatMpWiFiApiSetCouponPutRequest request)
  - POST /bizwifi/couponput/set

- async WechatResponse SetCouponPutRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiGetCouponPutResponse GetCouponPutAsync(WechatMpWiFiApiGetCouponPutRequest request)
  - POST /bizwifi/couponput/get

- async WechatResponse GetCouponPutRawAsync(string query, string jsonBody)

- async WechatMpWiFiApiGetConnectUrlResponse GetConnectUrlAsync()
  - GET /bizwifi/account/get_connecturl

- async WechatResponse GetConnectUrlRawAsync(string query)

- async WechatMpWiFiApiOpenPluginTokenResponse OpenPluginTokenAsync(WechatMpWiFiApiOpenPluginTokenRequest request)
  - GET /bizwifi/openplugin/token

- async WechatResponse OpenPluginTokenRawAsync(string query)


## WechatMpWiFiApiAddDeviceRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiAddDeviceRequest()

- WechatMpWiFiApiAddDeviceRequest ShopId(long fieldValue)

- WechatMpWiFiApiAddDeviceRequest Ssid(string fieldValue)

- WechatMpWiFiApiAddDeviceRequest Password(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiAddDeviceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWiFiApiDeleteDeviceRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiDeleteDeviceRequest()

- WechatMpWiFiApiDeleteDeviceRequest Bssid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiDeleteDeviceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWiFiApiGetConnectUrlResponse (class)

- public string Raw;


## WechatMpWiFiApiGetCouponPutRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiGetCouponPutRequest()

- WechatMpWiFiApiGetCouponPutRequest ShopId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiGetCouponPutResponse (class)

- public string Raw;


## WechatMpWiFiApiGetDeviceListRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiGetDeviceListRequest()

- WechatMpWiFiApiGetDeviceListRequest PageIndex(int fieldValue)

- WechatMpWiFiApiGetDeviceListRequest PageSize(int fieldValue)

- WechatMpWiFiApiGetDeviceListRequest ShopId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiGetDeviceListResponse (class)

- public string Raw;


## WechatMpWiFiApiGetHomePageaRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiGetHomePageaRequest()

- WechatMpWiFiApiGetHomePageaRequest ShopId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiGetHomePageaResponse (class)

- public string Raw;


## WechatMpWiFiApiGetQrcodeRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiGetQrcodeRequest()

- WechatMpWiFiApiGetQrcodeRequest ShopId(long fieldValue)

- WechatMpWiFiApiGetQrcodeRequest ImgId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiGetQrcodeResponse (class)

- public string Raw;


## WechatMpWiFiApiGetStatisticsRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiGetStatisticsRequest()

- WechatMpWiFiApiGetStatisticsRequest BeginDate(string fieldValue)

- WechatMpWiFiApiGetStatisticsRequest EndDate(string fieldValue)

- WechatMpWiFiApiGetStatisticsRequest ShopId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiGetStatisticsResponse (class)

- public string Raw;


## WechatMpWiFiApiOpenPluginTokenRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiOpenPluginTokenRequest()

- WechatMpWiFiApiOpenPluginTokenRequest CallBackUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiOpenPluginTokenResponse (class)

- public string Raw;


## WechatMpWiFiApiSetBarRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiSetBarRequest()

- WechatMpWiFiApiSetBarRequest ShopId(long fieldValue)

- WechatMpWiFiApiSetBarRequest BarType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiSetBarResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWiFiApiSetCouponPutRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiSetCouponPutRequest()

- WechatMpWiFiApiSetCouponPutRequest ShopId(long fieldValue)

- WechatMpWiFiApiSetCouponPutRequest CardId(string fieldValue)

- WechatMpWiFiApiSetCouponPutRequest CardDescribe(string fieldValue)

- WechatMpWiFiApiSetCouponPutRequest StarTime(string fieldValue)

- WechatMpWiFiApiSetCouponPutRequest EndTime(string fieldValue)

- WechatMpWiFiApiSetCouponPutRequest CardQuantity(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiSetCouponPutResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWiFiApiSetFinishpageRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiSetFinishpageRequest()

- WechatMpWiFiApiSetFinishpageRequest ShopId(long fieldValue)

- WechatMpWiFiApiSetFinishpageRequest FinishPageUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiSetFinishpageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWiFiApiSetHomePageRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiSetHomePageRequest()

- WechatMpWiFiApiSetHomePageRequest ShopId(long fieldValue)

- WechatMpWiFiApiSetHomePageRequest Url(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiSetHomePageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWiFiApiShopCleanRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiShopCleanRequest()

- WechatMpWiFiApiShopCleanRequest ShopId(long fieldValue)

- WechatMpWiFiApiShopCleanRequest Ssid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiShopCleanResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWiFiApiShopGetRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiShopGetRequest()

- WechatMpWiFiApiShopGetRequest ShopId(long fieldValue)

- WechatMpWiFiApiShopGetRequest Pageindex(int fieldValue)

- WechatMpWiFiApiShopGetRequest Pagesize(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiShopGetResponse (class)

- public string Raw;


## WechatMpWiFiApiShopListRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Mp。

- WechatTypedRequest request;

- public WechatMpWiFiApiShopListRequest()

- WechatMpWiFiApiShopListRequest PageIndex(int fieldValue)

- WechatMpWiFiApiShopListRequest PageSize(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiShopListResponse (class)

- public string Raw;


## WechatMpWiFiApiShopUpdateRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiShopUpdateRequest()

- WechatMpWiFiApiShopUpdateRequest ShopId(long fieldValue)

- WechatMpWiFiApiShopUpdateRequest OldSsid(string fieldValue)

- WechatMpWiFiApiShopUpdateRequest Ssid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiShopUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWiFiApiWifeRegisterRequest (class)

- WechatTypedRequest request;

- public WechatMpWiFiApiWifeRegisterRequest()

- WechatMpWiFiApiWifeRegisterRequest ShopId(long fieldValue)

- WechatMpWiFiApiWifeRegisterRequest Ssid(string fieldValue)

- WechatMpWiFiApiWifeRegisterRequest Reset(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWiFiApiWifeRegisterResponse (class)

- public string Raw;


## WechatMpWxaApi (class)

Wxa/WxaApi.cs 的 Zan 强类型 JSON 接口。
从 Senparc.Weixin.MP.AdvancedAPIs 生成；请勿在此手抄 HTTP 逻辑。

- WechatClient client;

- public WechatMpWxaApi(WechatClient client)

- async WechatMpWxaApiGetMerchantCategoryResultResponse GetMerchantCategoryResultAsync()
  - GET /wxa/get_merchant_category

- async WechatResponse GetMerchantCategoryResultRawAsync(string query)

- async WechatMpWxaApiApplyMerchantGetMerchantCategoryResultResponse ApplyMerchantGetMerchantCategoryResultAsync(WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest request)
  - POST /wxa/apply_merchant

- async WechatResponse ApplyMerchantGetMerchantCategoryResultRawAsync(string query, string jsonBody)

- async WechatMpWxaApiGetMerchantAuditInfoResponse GetMerchantAuditInfoAsync()
  - GET /wxa/get_merchant_audit_info

- async WechatResponse GetMerchantAuditInfoRawAsync(string query)

- async WechatMpWxaApiModifyMerchantResponse ModifyMerchantAsync(WechatMpWxaApiModifyMerchantRequest request)
  - POST /wxa/modify_merchant

- async WechatResponse ModifyMerchantRawAsync(string query, string jsonBody)

- async WechatMpWxaApiGetDistrictResponse GetDistrictAsync()
  - GET /wxa/get_district

- async WechatResponse GetDistrictRawAsync(string query)

- async WechatMpWxaApiSearchMapPoiResponse SearchMapPoiAsync(WechatMpWxaApiSearchMapPoiRequest request)
  - GET /wxa/search_map_poi

- async WechatResponse SearchMapPoiRawAsync(string query)

- async WechatMpWxaApiAddstoreResponse AddstoreAsync(WechatMpWxaApiAddstoreRequest request)
  - POST /wxa/add_store

- async WechatResponse AddstoreRawAsync(string query, string jsonBody)

- async WechatMpWxaApiCreateMapPoiResponse CreateMapPoiAsync(WechatMpWxaApiCreateMapPoiRequest request)
  - POST /wxa/create_map_poi

- async WechatResponse CreateMapPoiRawAsync(string query, string jsonBody)

- async WechatMpWxaApiUpdateStoreResponse UpdateStoreAsync(WechatMpWxaApiUpdateStoreRequest request)
  - POST /wxa/update_store

- async WechatResponse UpdateStoreRawAsync(string query, string jsonBody)

- async WechatMpWxaApiGetStoreInfoResponse GetStoreInfoAsync(WechatMpWxaApiGetStoreInfoRequest request)
  - POST /wxa/get_store_info

- async WechatResponse GetStoreInfoRawAsync(string query, string jsonBody)

- async WechatMpWxaApiDeleteStoreResponse DeleteStoreAsync(WechatMpWxaApiDeleteStoreRequest request)
  - POST /wxa/del_store

- async WechatResponse DeleteStoreRawAsync(string query, string jsonBody)


## WechatMpWxaApiAddstoreRequest (class)

- WechatTypedRequest request;

- public WechatMpWxaApiAddstoreRequest()

- WechatMpWxaApiAddstoreRequest PoiId(string fieldValue)

- WechatMpWxaApiAddstoreRequest MapPoiId(string fieldValue)

- WechatMpWxaApiAddstoreRequest PicList(string fieldValue)

- WechatMpWxaApiAddstoreRequest ContractPhone(string fieldValue)

- WechatMpWxaApiAddstoreRequest Hour(string fieldValue)

- WechatMpWxaApiAddstoreRequest Credential(string fieldValue)

- WechatMpWxaApiAddstoreRequest CompanyName(string fieldValue)

- WechatMpWxaApiAddstoreRequest QualificationList(string fieldValue)

- WechatMpWxaApiAddstoreRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWxaApiAddstoreResponse (class)

- public string Raw;


## WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest (class)

- WechatTypedRequest request;

- public WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest()

- WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest FirstCatid(int fieldValue)

- WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest SecondCatid(int fieldValue)

- WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest QualificationList(string fieldValue)

- WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest HeadimgMediaid(string fieldValue)

- WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest Nickname(string fieldValue)

- WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest Intro(string fieldValue)

- WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest OrgCode(string fieldValue)

- WechatMpWxaApiApplyMerchantGetMerchantCategoryResultRequest OtherFiles(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWxaApiApplyMerchantGetMerchantCategoryResultResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWxaApiCreateMapPoiRequest (class)

- WechatTypedRequest request;

- public WechatMpWxaApiCreateMapPoiRequest()

- WechatMpWxaApiCreateMapPoiRequest Name(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest Longitude(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest Latitude(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest Province(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest City(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest District(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest Address(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest Category(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest Telephone(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest Photo(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest License(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest Introduct(string fieldValue)

- WechatMpWxaApiCreateMapPoiRequest Districtid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWxaApiCreateMapPoiResponse (class)

- public string Raw;


## WechatMpWxaApiDeleteStoreRequest (class)

- WechatTypedRequest request;

- public WechatMpWxaApiDeleteStoreRequest()

- WechatMpWxaApiDeleteStoreRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWxaApiDeleteStoreResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWxaApiGetDistrictResponse (class)

- public string Raw;


## WechatMpWxaApiGetMerchantAuditInfoResponse (class)

- public string Raw;


## WechatMpWxaApiGetMerchantCategoryResultResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.Mp。

- public string Raw;


## WechatMpWxaApiGetStoreInfoRequest (class)

- WechatTypedRequest request;

- public WechatMpWxaApiGetStoreInfoRequest()

- WechatMpWxaApiGetStoreInfoRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWxaApiGetStoreInfoResponse (class)

- public string Raw;


## WechatMpWxaApiModifyMerchantRequest (class)

- WechatTypedRequest request;

- public WechatMpWxaApiModifyMerchantRequest()

- WechatMpWxaApiModifyMerchantRequest HeadimgMediaid(string fieldValue)

- WechatMpWxaApiModifyMerchantRequest Intro(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWxaApiModifyMerchantResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatMpWxaApiSearchMapPoiRequest (class)

- WechatTypedRequest request;

- public WechatMpWxaApiSearchMapPoiRequest()

- WechatMpWxaApiSearchMapPoiRequest Districtid(int fieldValue)

- WechatMpWxaApiSearchMapPoiRequest Keyword(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWxaApiSearchMapPoiResponse (class)

- public string Raw;


## WechatMpWxaApiUpdateStoreRequest (class)

- WechatTypedRequest request;

- public WechatMpWxaApiUpdateStoreRequest()

- WechatMpWxaApiUpdateStoreRequest PoiId(string fieldValue)

- WechatMpWxaApiUpdateStoreRequest Hour(string fieldValue)

- WechatMpWxaApiUpdateStoreRequest PicList(string fieldValue)

- WechatMpWxaApiUpdateStoreRequest ContractPhone(string fieldValue)

- WechatMpWxaApiUpdateStoreRequest CardId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatMpWxaApiUpdateStoreResponse (class)

- public string Raw;


## WechatMpMediaType (enum)

公众号临时/永久素材类型。使用枚举避免调用方手写协议字符串。

- Image

- Voice

- Video

- Thumb
