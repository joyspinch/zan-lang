# Sdk.Wechat.WxOpen

> 源码: `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenB2BApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenB2BMerchantApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenB2BOrderApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenB2BProfitSharingApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenChargeApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenCityServiceApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenClient.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenCustomApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenCustomServiceApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenCustomServiceCustomerServiceBusinessApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenCustomServiceKfWorkApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenDataCubeApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenDeliveryApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenDeliveryProviderApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenExpressApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenFaceApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenHardwareDeviceApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenImmediateDeliveryApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenImmediateDeliveryProviderApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenLiveBroadcastApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenLiveBroadcastGoodsApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenLiveBroadcastRoleSubscriptionApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenMessageApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenMessageServiceCardApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenMessageUpdatableMessageApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenMiniDramaApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenMiniDramaAuditApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenMiniDramaAuthorizationApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenMiniDramaPlayerApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenNovelApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenNovelAuthorizationApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenNovelReaderApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenOperationApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenQrCodeJumpApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenRedPacketCoverApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenSecOrderApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenSecOrderIncrementApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenServiceMarketApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenSnsApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenSoterApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenSpecialApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenStudentApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenTcbApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenTcbIncrementApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenTemplateApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenTransactionGuaranteeApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWeixinExpressApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWeixinExpressInsuranceApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWeixinExpressIntracityApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWeixinExpressProviderApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWeixinExpressReturnApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWxAppApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWxAppBusinessApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWxAppGenerateSchemeApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWxAppSearchApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWxAppShortLinkApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWxAppUrlLinkApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenWxAppUrlSchemeApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenXPayApi.zan`, `stdlib/Sdk/Wechat/WxOpen/WechatWxOpenXPayIncrementApi.zan`


## WechatWxOpenB2BApi (class)

B2B/B2BApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenB2BApi(WechatWxOpenClient client)

- async WechatWxOpenB2BApiApplyRetailBusinessResponse ApplyRetailBusinessAsync(WechatWxOpenB2BApiApplyRetailBusinessRequest request)
  - POST /wxa/business/retailbusinessapply

- async WechatResponse ApplyRetailBusinessRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BApiBatchCreateRetailResponse BatchCreateRetailAsync(WechatWxOpenB2BApiBatchCreateRetailRequest request)
  - POST /wxa/business/batchcreateretail

- async WechatResponse BatchCreateRetailRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BApiGetRetailInfoResponse GetRetailInfoAsync(WechatWxOpenB2BApiGetRetailInfoRequest request)
  - POST /wxa/business/getretailinfo

- async WechatResponse GetRetailInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BApiGetRetailOpenIdListResponse GetRetailOpenIdListAsync(WechatWxOpenB2BApiGetRetailOpenIdListRequest request)
  - POST /wxa/business/getretailopenidlist

- async WechatResponse GetRetailOpenIdListRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BApiSendRetailNotificationResponse SendRetailNotificationAsync(WechatWxOpenB2BApiSendRetailNotificationRequest request)
  - POST /wxa/business/retailnotifybusiness

- async WechatResponse SendRetailNotificationRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BApiGetRetailMessageListResponse GetRetailMessageListAsync(WechatWxOpenB2BApiGetRetailMessageListRequest request)
  - POST /wxa/business/getretailmessagelist

- async WechatResponse GetRetailMessageListRawAsync(string query, string jsonBody)


## WechatWxOpenB2BApiApplyRetailBusinessRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenB2BApiApplyRetailBusinessRequest()

- WechatWxOpenB2BApiApplyRetailBusinessRequest GoodsTypeList(List<string> fieldValue)

- WechatWxOpenB2BApiApplyRetailBusinessRequest GoodsSaleList(List<string> fieldValue)

- WechatWxOpenB2BApiApplyRetailBusinessRequest CoverNum(string fieldValue)

- WechatWxOpenB2BApiApplyRetailBusinessRequest ServiceList(List<string> fieldValue)

- WechatWxOpenB2BApiApplyRetailBusinessRequest Description(string fieldValue)

- WechatWxOpenB2BApiApplyRetailBusinessRequest ContactName(string fieldValue)

- WechatWxOpenB2BApiApplyRetailBusinessRequest ContactPhone(string fieldValue)

- WechatWxOpenB2BApiApplyRetailBusinessRequest ContactEmail(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BApiApplyRetailBusinessResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BApiBatchCreateRetailRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BApiBatchCreateRetailRequest()

- WechatWxOpenB2BApiBatchCreateRetailRequest RetailInfoList(List<WechatWxOpenB2BRetailPreEntry> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BApiBatchCreateRetailResponse (class)

- public string Raw;


## WechatWxOpenB2BApiGetRetailInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BApiGetRetailInfoRequest()

- WechatWxOpenB2BApiGetRetailInfoRequest Openid(string fieldValue)

- WechatWxOpenB2BApiGetRetailInfoRequest MobilePhone(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BApiGetRetailInfoResponse (class)

- public string Raw;


## WechatWxOpenB2BApiGetRetailMessageListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BApiGetRetailMessageListRequest()

- WechatWxOpenB2BApiGetRetailMessageListRequest Start(int fieldValue)

- WechatWxOpenB2BApiGetRetailMessageListRequest Offset(int fieldValue)

- WechatWxOpenB2BApiGetRetailMessageListRequest BeginDate(string fieldValue)

- WechatWxOpenB2BApiGetRetailMessageListRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BApiGetRetailMessageListResponse (class)

- public string Raw;


## WechatWxOpenB2BApiGetRetailOpenIdListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BApiGetRetailOpenIdListRequest()

- WechatWxOpenB2BApiGetRetailOpenIdListRequest Limit(int fieldValue)

- WechatWxOpenB2BApiGetRetailOpenIdListRequest PageContext(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BApiGetRetailOpenIdListResponse (class)

- public string Raw;


## WechatWxOpenB2BApiSendRetailNotificationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BApiSendRetailNotificationRequest()

- WechatWxOpenB2BApiSendRetailNotificationRequest Type(int fieldValue)

- WechatWxOpenB2BApiSendRetailNotificationRequest ToUserList(List<string> fieldValue)

- WechatWxOpenB2BApiSendRetailNotificationRequest Content(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BApiSendRetailNotificationResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BMerchantApi (class)

B2B/B2BMerchantApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenB2BMerchantApi(WechatWxOpenClient client)

- async WechatWxOpenB2BMerchantApiRegisterMerchantResponse RegisterMerchantAsync(WechatWxOpenB2BMerchantApiRegisterMerchantRequest request)
  - POST /retail/B2b/retailregistermch

- async WechatResponse RegisterMerchantRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BMerchantApiUploadMerchantFileResponse UploadMerchantFileAsync(WechatWxOpenB2BMerchantApiUploadMerchantFileRequest request)
  - POST /retail/B2b/retailuploadmchfile

- async WechatResponse UploadMerchantFileRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BMerchantApiGetMerchantApplicationResponse GetMerchantApplicationAsync(WechatWxOpenB2BMerchantApiGetMerchantApplicationRequest request)
  - POST /retail/B2b/retailgetmchorder

- async WechatResponse GetMerchantApplicationRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BMerchantApiApplyBankTransferResponse ApplyBankTransferAsync(WechatWxOpenB2BMerchantApiApplyBankTransferRequest request)
  - POST /retail/B2b/registeronlywqf

- async WechatResponse ApplyBankTransferRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BMerchantApiCreateBankTransferLinkResponse CreateBankTransferLinkAsync(WechatWxOpenB2BMerchantApiCreateBankTransferLinkRequest request)
  - POST /retail/B2b/createwqflink

- async WechatResponse CreateBankTransferLinkRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BMerchantApiGetMerchantInfoResponse GetMerchantInfoAsync()
  - POST /retail/B2b/getmchinfo

- async WechatResponse GetMerchantInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BMerchantApiSetMerchantProfitRateResponse SetMerchantProfitRateAsync(WechatWxOpenB2BMerchantApiSetMerchantProfitRateRequest request)
  - POST /retail/B2b/setmchprofitrate

- async WechatResponse SetMerchantProfitRateRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BMerchantApiUpdateBankTransferFeeResponse UpdateBankTransferFeeAsync(WechatWxOpenB2BMerchantApiUpdateBankTransferFeeRequest request)
  - POST /retail/B2b/updatewqfchargefee

- async WechatResponse UpdateBankTransferFeeRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BMerchantApiGetBankTransferFeeResponse GetBankTransferFeeAsync(WechatWxOpenB2BMerchantApiGetBankTransferFeeRequest request)
  - POST /retail/B2b/getwqfchargefee

- async WechatResponse GetBankTransferFeeRawAsync(string query, string jsonBody)


## WechatWxOpenB2BMerchantApiApplyBankTransferRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BMerchantApiApplyBankTransferRequest()

- WechatWxOpenB2BMerchantApiApplyBankTransferRequest OutRegistrationId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BMerchantApiApplyBankTransferResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BMerchantApiCreateBankTransferLinkRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BMerchantApiCreateBankTransferLinkRequest()

- WechatWxOpenB2BMerchantApiCreateBankTransferLinkRequest RequestNo(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BMerchantApiCreateBankTransferLinkResponse (class)

- public string Raw;


## WechatWxOpenB2BMerchantApiGetBankTransferFeeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BMerchantApiGetBankTransferFeeRequest()

- WechatWxOpenB2BMerchantApiGetBankTransferFeeRequest SubMchid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BMerchantApiGetBankTransferFeeResponse (class)

- public string Raw;


## WechatWxOpenB2BMerchantApiGetMerchantApplicationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BMerchantApiGetMerchantApplicationRequest()

- WechatWxOpenB2BMerchantApiGetMerchantApplicationRequest OutRegistrationId(string fieldValue)

- WechatWxOpenB2BMerchantApiGetMerchantApplicationRequest PageIndex(int fieldValue)

- WechatWxOpenB2BMerchantApiGetMerchantApplicationRequest PageSize(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BMerchantApiGetMerchantApplicationResponse (class)

- public string Raw;


## WechatWxOpenB2BMerchantApiGetMerchantInfoResponse (class)

- public string Raw;


## WechatWxOpenB2BMerchantApiRegisterMerchantRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenB2BMerchantApiRegisterMerchantRequest()

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest IdDocTypeNum(int fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest IdCardInfo(WechatWxOpenB2BIdCardInfo fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest IdDocInfo(WechatWxOpenB2BIdDocumentInfo fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest AccountInfo(WechatWxOpenB2BMerchantAccountInfo fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest ContactInfo(WechatWxOpenB2BMerchantContactInfo fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest BusinessLicense(WechatWxOpenB2BBusinessLicense fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest MerchantShortname(string fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest OrganizationType(int fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest Qualification(WechatWxOpenB2BMerchantQualification fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest BusinessAdditionDesc(string fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest BusinessAdditionPics(string fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest OpenType(int fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest ExtRegisterInfo(WechatWxOpenB2BMerchantExtendedRegisterInfo fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest ClientIp(string fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest IgnoreSameEntity(bool fieldValue)

- WechatWxOpenB2BMerchantApiRegisterMerchantRequest LaunchPollTask(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BMerchantApiRegisterMerchantResponse (class)

- public string Raw;


## WechatWxOpenB2BMerchantApiSetMerchantProfitRateRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BMerchantApiSetMerchantProfitRateRequest()

- WechatWxOpenB2BMerchantApiSetMerchantProfitRateRequest SubMchid(string fieldValue)

- WechatWxOpenB2BMerchantApiSetMerchantProfitRateRequest ProfitRate(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BMerchantApiSetMerchantProfitRateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BMerchantApiUpdateBankTransferFeeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BMerchantApiUpdateBankTransferFeeRequest()

- WechatWxOpenB2BMerchantApiUpdateBankTransferFeeRequest SubMchid(string fieldValue)

- WechatWxOpenB2BMerchantApiUpdateBankTransferFeeRequest CertifiedChargeFeeNumerator(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BMerchantApiUpdateBankTransferFeeResponse (class)

- public string Raw;


## WechatWxOpenB2BMerchantApiUploadMerchantFileRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BMerchantApiUploadMerchantFileRequest()

- WechatWxOpenB2BMerchantApiUploadMerchantFileRequest FileName(string fieldValue)

- WechatWxOpenB2BMerchantApiUploadMerchantFileRequest File(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BMerchantApiUploadMerchantFileResponse (class)

- public string Raw;


## WechatWxOpenB2BOrderApi (class)

B2B/B2BOrderApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenB2BOrderApi(WechatWxOpenClient client)

- async WechatWxOpenB2BOrderApiGetOrderResponse GetOrderAsync(WechatWxOpenB2BOrderApiGetOrderRequest request)
  - POST /retail/B2b/getorder

- async WechatResponse GetOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BOrderApiCloseOrderResponse CloseOrderAsync(WechatWxOpenB2BOrderApiCloseOrderRequest request)
  - POST /retail/B2b/closeb2border

- async WechatResponse CloseOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BOrderApiRefundOrderResponse RefundOrderAsync(WechatWxOpenB2BOrderApiRefundOrderRequest request)
  - POST /retail/B2b/refund

- async WechatResponse RefundOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BOrderApiGetRefundResponse GetRefundAsync(WechatWxOpenB2BOrderApiGetRefundRequest request)
  - POST /retail/B2b/getrefund

- async WechatResponse GetRefundRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BOrderApiGetAppKeyResponse GetAppKeyAsync(WechatWxOpenB2BOrderApiGetAppKeyRequest request)
  - POST /retail/B2b/getappkey

- async WechatResponse GetAppKeyRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BOrderApiDownloadBillResponse DownloadBillAsync(WechatWxOpenB2BOrderApiDownloadBillRequest request)
  - POST /retail/B2b/downloadbill

- async WechatResponse DownloadBillRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BOrderApiGetMerchantBalanceResponse GetMerchantBalanceAsync(WechatWxOpenB2BOrderApiGetMerchantBalanceRequest request)
  - POST /retail/B2b/getmchbalance

- async WechatResponse GetMerchantBalanceRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BOrderApiWithdrawResponse WithdrawAsync(WechatWxOpenB2BOrderApiWithdrawRequest request)
  - POST /retail/B2b/withdraw

- async WechatResponse WithdrawRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BOrderApiQueryWithdrawResponse QueryWithdrawAsync(WechatWxOpenB2BOrderApiQueryWithdrawRequest request)
  - POST /retail/B2b/querywithdraw

- async WechatResponse QueryWithdrawRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BOrderApiSetAutoWithdrawResponse SetAutoWithdrawAsync(WechatWxOpenB2BOrderApiSetAutoWithdrawRequest request)
  - POST /retail/B2b/setautowithdraw

- async WechatResponse SetAutoWithdrawRawAsync(string query, string jsonBody)


## WechatWxOpenB2BOrderApiCloseOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiCloseOrderRequest()

- WechatWxOpenB2BOrderApiCloseOrderRequest Mchid(string fieldValue)

- WechatWxOpenB2BOrderApiCloseOrderRequest OutTradeNo(string fieldValue)

- WechatWxOpenB2BOrderApiCloseOrderRequest OrderId(string fieldValue)

- WechatWxOpenB2BOrderApiCloseOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiCloseOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BOrderApiDownloadBillRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiDownloadBillRequest()

- WechatWxOpenB2BOrderApiDownloadBillRequest BillDate(string fieldValue)

- WechatWxOpenB2BOrderApiDownloadBillRequest Mchid(string fieldValue)

- WechatWxOpenB2BOrderApiDownloadBillRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiDownloadBillResponse (class)

- public string Raw;


## WechatWxOpenB2BOrderApiGetAppKeyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiGetAppKeyRequest()

- WechatWxOpenB2BOrderApiGetAppKeyRequest Mchid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiGetAppKeyResponse (class)

- public string Raw;


## WechatWxOpenB2BOrderApiGetMerchantBalanceRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiGetMerchantBalanceRequest()

- WechatWxOpenB2BOrderApiGetMerchantBalanceRequest Mchid(string fieldValue)

- WechatWxOpenB2BOrderApiGetMerchantBalanceRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiGetMerchantBalanceResponse (class)

- public string Raw;


## WechatWxOpenB2BOrderApiGetOrderRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiGetOrderRequest()

- WechatWxOpenB2BOrderApiGetOrderRequest Mchid(string fieldValue)

- WechatWxOpenB2BOrderApiGetOrderRequest OutTradeNo(string fieldValue)

- WechatWxOpenB2BOrderApiGetOrderRequest OrderId(string fieldValue)

- WechatWxOpenB2BOrderApiGetOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiGetOrderResponse (class)

- public string Raw;


## WechatWxOpenB2BOrderApiGetRefundRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiGetRefundRequest()

- WechatWxOpenB2BOrderApiGetRefundRequest Mchid(string fieldValue)

- WechatWxOpenB2BOrderApiGetRefundRequest OutRefundNo(string fieldValue)

- WechatWxOpenB2BOrderApiGetRefundRequest RefundId(string fieldValue)

- WechatWxOpenB2BOrderApiGetRefundRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiGetRefundResponse (class)

- public string Raw;


## WechatWxOpenB2BOrderApiQueryWithdrawRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiQueryWithdrawRequest()

- WechatWxOpenB2BOrderApiQueryWithdrawRequest OutWithdrawNo(string fieldValue)

- WechatWxOpenB2BOrderApiQueryWithdrawRequest Mchid(string fieldValue)

- WechatWxOpenB2BOrderApiQueryWithdrawRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiQueryWithdrawResponse (class)

- public string Raw;


## WechatWxOpenB2BOrderApiRefundOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiRefundOrderRequest()

- WechatWxOpenB2BOrderApiRefundOrderRequest OutRefundNo(string fieldValue)

- WechatWxOpenB2BOrderApiRefundOrderRequest RefundAmount(long fieldValue)

- WechatWxOpenB2BOrderApiRefundOrderRequest RefundFrom(int fieldValue)

- WechatWxOpenB2BOrderApiRefundOrderRequest RefundReason(int fieldValue)

- WechatWxOpenB2BOrderApiRefundOrderRequest Description(string fieldValue)

- WechatWxOpenB2BOrderApiRefundOrderRequest Mchid(string fieldValue)

- WechatWxOpenB2BOrderApiRefundOrderRequest OutTradeNo(string fieldValue)

- WechatWxOpenB2BOrderApiRefundOrderRequest OrderId(string fieldValue)

- WechatWxOpenB2BOrderApiRefundOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiRefundOrderResponse (class)

- public string Raw;


## WechatWxOpenB2BOrderApiSetAutoWithdrawRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiSetAutoWithdrawRequest()

- WechatWxOpenB2BOrderApiSetAutoWithdrawRequest Status(int fieldValue)

- WechatWxOpenB2BOrderApiSetAutoWithdrawRequest RetainAmt(long fieldValue)

- WechatWxOpenB2BOrderApiSetAutoWithdrawRequest Mchid(string fieldValue)

- WechatWxOpenB2BOrderApiSetAutoWithdrawRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiSetAutoWithdrawResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BOrderApiWithdrawRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BOrderApiWithdrawRequest()

- WechatWxOpenB2BOrderApiWithdrawRequest WithdrawAmount(long fieldValue)

- WechatWxOpenB2BOrderApiWithdrawRequest OutWithdrawNo(string fieldValue)

- WechatWxOpenB2BOrderApiWithdrawRequest Mchid(string fieldValue)

- WechatWxOpenB2BOrderApiWithdrawRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BOrderApiWithdrawResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BProfitSharingApi (class)

B2B/B2BProfitSharingApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenB2BProfitSharingApi(WechatWxOpenClient client)

- async WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountResponse AddProfitSharingAccountAsync(WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountRequest request)
  - POST /retail/B2b/addprofitsharingaccount

- async WechatResponse AddProfitSharingAccountRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BProfitSharingApiDeleteProfitSharingAccountResponse DeleteProfitSharingAccountAsync(WechatWxOpenB2BProfitSharingApiDeleteProfitSharingAccountRequest request)
  - POST /retail/B2b/delprofitsharingaccount

- async WechatResponse DeleteProfitSharingAccountRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BProfitSharingApiQueryProfitSharingAccountResponse QueryProfitSharingAccountAsync(WechatWxOpenB2BProfitSharingApiQueryProfitSharingAccountRequest request)
  - POST /retail/B2b/queryprofitsharingaccount

- async WechatResponse QueryProfitSharingAccountRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderResponse CreateProfitSharingOrderAsync(WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderRequest request)
  - POST /retail/B2b/createprofitsharingorder

- async WechatResponse CreateProfitSharingOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderResponse QueryProfitSharingOrderAsync(WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderRequest request)
  - POST /retail/B2b/queryprofitsharingorder

- async WechatResponse QueryProfitSharingOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BProfitSharingApiQueryProfitSharingRemainingAmountResponse QueryProfitSharingRemainingAmountAsync(WechatWxOpenB2BProfitSharingApiQueryProfitSharingRemainingAmountRequest request)
  - POST /retail/B2b/queryprofitsharingremainamt

- async WechatResponse QueryProfitSharingRemainingAmountRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BProfitSharingApiFinishProfitSharingOrderResponse FinishProfitSharingOrderAsync(WechatWxOpenB2BProfitSharingApiFinishProfitSharingOrderRequest request)
  - POST /retail/B2b/finishprofitsharingorder

- async WechatResponse FinishProfitSharingOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BProfitSharingApiRefundProfitSharingResponse RefundProfitSharingAsync(WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest request)
  - POST /retail/B2b/refundprofitsharing

- async WechatResponse RefundProfitSharingRawAsync(string query, string jsonBody)

- async WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderResponse QueryRefundProfitSharingOrderAsync(WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderRequest request)
  - POST /retail/B2b/queryrefundprofitsharingorder

- async WechatResponse QueryRefundProfitSharingOrderRawAsync(string query, string jsonBody)


## WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountRequest()

- WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountRequest ProfitSharingRelationType(string fieldValue)

- WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountRequest PayeeType(string fieldValue)

- WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountRequest PayeeId(string fieldValue)

- WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountRequest PayeeName(string fieldValue)

- WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BProfitSharingApiAddProfitSharingAccountResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderRequest()

- WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderRequest Mchid(string fieldValue)

- WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderRequest OutTradeNo(string fieldValue)

- WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderRequest ProfitFee(long fieldValue)

- WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderRequest ReceiverType(string fieldValue)

- WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderRequest ReceiverAccount(string fieldValue)

- WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BProfitSharingApiCreateProfitSharingOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BProfitSharingApiDeleteProfitSharingAccountRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BProfitSharingApiDeleteProfitSharingAccountRequest()

- WechatWxOpenB2BProfitSharingApiDeleteProfitSharingAccountRequest PayeeType(string fieldValue)

- WechatWxOpenB2BProfitSharingApiDeleteProfitSharingAccountRequest PayeeId(string fieldValue)

- WechatWxOpenB2BProfitSharingApiDeleteProfitSharingAccountRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BProfitSharingApiDeleteProfitSharingAccountResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BProfitSharingApiFinishProfitSharingOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BProfitSharingApiFinishProfitSharingOrderRequest()

- WechatWxOpenB2BProfitSharingApiFinishProfitSharingOrderRequest Mchid(string fieldValue)

- WechatWxOpenB2BProfitSharingApiFinishProfitSharingOrderRequest OutTradeNo(string fieldValue)

- WechatWxOpenB2BProfitSharingApiFinishProfitSharingOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BProfitSharingApiFinishProfitSharingOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenB2BProfitSharingApiQueryProfitSharingAccountRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BProfitSharingApiQueryProfitSharingAccountRequest()

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingAccountRequest Offset(int fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingAccountRequest Limit(int fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingAccountRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BProfitSharingApiQueryProfitSharingAccountResponse (class)

- public string Raw;


## WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderRequest()

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderRequest OutTradeNo(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderRequest ReceiverType(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderRequest ReceiverAccount(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderRequest Mchid(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BProfitSharingApiQueryProfitSharingOrderResponse (class)

- public string Raw;


## WechatWxOpenB2BProfitSharingApiQueryProfitSharingRemainingAmountRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BProfitSharingApiQueryProfitSharingRemainingAmountRequest()

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingRemainingAmountRequest Mchid(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingRemainingAmountRequest OutTradeNo(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryProfitSharingRemainingAmountRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BProfitSharingApiQueryProfitSharingRemainingAmountResponse (class)

- public string Raw;


## WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderRequest()

- WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderRequest OutTradeNo(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderRequest OutRefundNo(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderRequest Mchid(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderRequest PayeeType(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderRequest PayeeId(string fieldValue)

- WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BProfitSharingApiQueryRefundProfitSharingOrderResponse (class)

- public string Raw;


## WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest()

- WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest OutTradeNo(string fieldValue)

- WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest OutRefundNo(string fieldValue)

- WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest PayeeType(string fieldValue)

- WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest PayeeId(string fieldValue)

- WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest Mchid(string fieldValue)

- WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest RefundAmt(long fieldValue)

- WechatWxOpenB2BProfitSharingApiRefundProfitSharingRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenB2BProfitSharingApiRefundProfitSharingResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenChargeApi (class)

Charge/ChargeApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenChargeApi(WechatWxOpenClient client)

- async WechatWxOpenChargeApiUsageResponse UsageAsync(WechatWxOpenChargeApiUsageRequest request)
  - GET /wxa/charge/usage/get

- async WechatResponse UsageRawAsync(string query)

- async WechatWxOpenChargeApiGetRecentAverageResponse GetRecentAverageAsync(WechatWxOpenChargeApiGetRecentAverageRequest request)
  - GET /wxa/charge/usage/get_recent_average

- async WechatResponse GetRecentAverageRawAsync(string query)


## WechatWxOpenChargeApiGetRecentAverageRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenChargeApiGetRecentAverageRequest()

- WechatWxOpenChargeApiGetRecentAverageRequest SpuId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenChargeApiGetRecentAverageResponse (class)

- public string Raw;


## WechatWxOpenChargeApiUsageRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenChargeApiUsageRequest()

- WechatWxOpenChargeApiUsageRequest SpuId(string fieldValue)

- WechatWxOpenChargeApiUsageRequest Offset(int fieldValue)

- WechatWxOpenChargeApiUsageRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenChargeApiUsageResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApi (class)

CityService/CityServiceApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenCityServiceApi(WechatWxOpenClient client)

- async WechatWxOpenCityServiceApiGetServicePathResponse GetServicePathAsync(WechatWxOpenCityServiceApiGetServicePathRequest request)
  - POST /cityservice/getservicepath

- async WechatResponse GetServicePathRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiSendMessageDataResponse SendMessageDataAsync(WechatWxOpenCityServiceApiSendMessageDataRequest request)
  - POST /cityservice/sendmsgdata

- async WechatResponse SendMessageDataRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiCheckRealNameResponse CheckRealNameAsync(WechatWxOpenCityServiceApiCheckRealNameRequest request)
  - POST /intp/realname/checkrealnameinfo

- async WechatResponse CheckRealNameRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiGetBusinessViewResponse GetBusinessViewAsync(WechatWxOpenCityServiceApiGetBusinessViewRequest request)
  - POST /intp/transportcode/getbusinessview

- async WechatResponse GetBusinessViewRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiSendMedicalMessageResponse SendMedicalMessageAsync(WechatWxOpenCityServiceApiSendMedicalMessageRequest request)
  - POST /cityservice/sendchannelmsg

- async WechatResponse SendMedicalMessageRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiGetMedicalRealNameResponse GetMedicalRealNameAsync(WechatWxOpenCityServiceApiGetMedicalRealNameRequest request)
  - POST /cityservice/getmedrealname

- async WechatResponse GetMedicalRealNameRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiGetMessageRelationResponse GetMessageRelationAsync(WechatWxOpenCityServiceApiGetMessageRelationRequest request)
  - POST /cityservice/getmsgrelation

- async WechatResponse GetMessageRelationRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiGetHospitalNoticeListResponse GetHospitalNoticeListAsync(WechatWxOpenCityServiceApiGetHospitalNoticeListRequest request)
  - POST /intp/eldermedical/gethospnoticelist

- async WechatResponse GetHospitalNoticeListRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiSetHospitalNoticePreviewResponse SetHospitalNoticePreviewAsync(WechatWxOpenCityServiceApiSetHospitalNoticePreviewRequest request)
  - POST /intp/eldermedical/previewhopsnotice

- async WechatResponse SetHospitalNoticePreviewRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiPublishHospitalNoticeResponse PublishHospitalNoticeAsync(WechatWxOpenCityServiceApiPublishHospitalNoticeRequest request)
  - POST /intp/eldermedical/publichopsnotice

- async WechatResponse PublishHospitalNoticeRawAsync(string query, string jsonBody)

- async WechatWxOpenCityServiceApiSetHospitalNoticeResponse SetHospitalNoticeAsync(WechatWxOpenCityServiceApiSetHospitalNoticeRequest request)
  - POST /intp/eldermedical/sethopsnotice

- async WechatResponse SetHospitalNoticeRawAsync(string query, string jsonBody)


## WechatWxOpenCityServiceApiCheckRealNameRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiCheckRealNameRequest()

- WechatWxOpenCityServiceApiCheckRealNameRequest Openid(string fieldValue)

- WechatWxOpenCityServiceApiCheckRealNameRequest RealName(string fieldValue)

- WechatWxOpenCityServiceApiCheckRealNameRequest CredId(string fieldValue)

- WechatWxOpenCityServiceApiCheckRealNameRequest CredType(string fieldValue)

- WechatWxOpenCityServiceApiCheckRealNameRequest Code(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiCheckRealNameResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApiGetBusinessViewRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiGetBusinessViewRequest()

- WechatWxOpenCityServiceApiGetBusinessViewRequest PathType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiGetBusinessViewResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApiGetHospitalNoticeListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiGetHospitalNoticeListRequest()

- WechatWxOpenCityServiceApiGetHospitalNoticeListRequest AppId(string fieldValue)

- WechatWxOpenCityServiceApiGetHospitalNoticeListRequest NoticeType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiGetHospitalNoticeListResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApiGetMedicalRealNameRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiGetMedicalRealNameRequest()

- WechatWxOpenCityServiceApiGetMedicalRealNameRequest AppId(string fieldValue)

- WechatWxOpenCityServiceApiGetMedicalRealNameRequest OpenId(string fieldValue)

- WechatWxOpenCityServiceApiGetMedicalRealNameRequest WxmedAuthcode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiGetMedicalRealNameResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApiGetMessageRelationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiGetMessageRelationRequest()

- WechatWxOpenCityServiceApiGetMessageRelationRequest BusinessId(int fieldValue)

- WechatWxOpenCityServiceApiGetMessageRelationRequest OpenId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiGetMessageRelationResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApiGetServicePathRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiGetServicePathRequest()

- WechatWxOpenCityServiceApiGetServicePathRequest PageType(int fieldValue)

- WechatWxOpenCityServiceApiGetServicePathRequest SrcChannel(int fieldValue)

- WechatWxOpenCityServiceApiGetServicePathRequest NeedPathType(int fieldValue)

- WechatWxOpenCityServiceApiGetServicePathRequest DeviceType(int fieldValue)

- WechatWxOpenCityServiceApiGetServicePathRequest CityName(string fieldValue)

- WechatWxOpenCityServiceApiGetServicePathRequest ContentName(string fieldValue)

- WechatWxOpenCityServiceApiGetServicePathRequest ExtParams(List<WechatWxOpenCityServicePathExtraParameter> fieldValue)

- WechatWxOpenCityServiceApiGetServicePathRequest ServiceId(long fieldValue)

- WechatWxOpenCityServiceApiGetServicePathRequest CityId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiGetServicePathResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApiPublishHospitalNoticeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiPublishHospitalNoticeRequest()

- WechatWxOpenCityServiceApiPublishHospitalNoticeRequest AppId(string fieldValue)

- WechatWxOpenCityServiceApiPublishHospitalNoticeRequest NoticeType(int fieldValue)

- WechatWxOpenCityServiceApiPublishHospitalNoticeRequest NoticeId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiPublishHospitalNoticeResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApiSendMedicalMessageRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiSendMedicalMessageRequest()

- WechatWxOpenCityServiceApiSendMedicalMessageRequest Request(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiSendMedicalMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCityServiceApiSendMessageDataRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiSendMessageDataRequest()

- WechatWxOpenCityServiceApiSendMessageDataRequest Request(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiSendMessageDataResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApiSetHospitalNoticePreviewRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiSetHospitalNoticePreviewRequest()

- WechatWxOpenCityServiceApiSetHospitalNoticePreviewRequest AppId(string fieldValue)

- WechatWxOpenCityServiceApiSetHospitalNoticePreviewRequest NoticeType(int fieldValue)

- WechatWxOpenCityServiceApiSetHospitalNoticePreviewRequest NoticeId(long fieldValue)

- WechatWxOpenCityServiceApiSetHospitalNoticePreviewRequest PreviewUsername(string fieldValue)

- WechatWxOpenCityServiceApiSetHospitalNoticePreviewRequest Operation(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiSetHospitalNoticePreviewResponse (class)

- public string Raw;


## WechatWxOpenCityServiceApiSetHospitalNoticeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCityServiceApiSetHospitalNoticeRequest()

- WechatWxOpenCityServiceApiSetHospitalNoticeRequest AppId(string fieldValue)

- WechatWxOpenCityServiceApiSetHospitalNoticeRequest NoticeType(int fieldValue)

- WechatWxOpenCityServiceApiSetHospitalNoticeRequest NoticeContent(string fieldValue)

- WechatWxOpenCityServiceApiSetHospitalNoticeRequest NoticeId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCityServiceApiSetHospitalNoticeResponse (class)

- public string Raw;


## WechatWxOpenClient (class)

微信小程序 JSON API 客户端，自动缓存小程序 access_token。

- WechatApiTransport transport;

- string appId;

- string appSecret;

- string accessToken;

- long accessTokenExpireUnix;

- public WechatWxOpenClient(string appId, string appSecret)

- WechatWxOpenClient Server(string host, int port)

- WechatWxOpenClient Timeout(int ms)

- WechatWxOpenClient SetAccessToken(string token, int expiresIn)

- async string GetAccessTokenAsync()

- async WechatResponse RequestAsync(string method, string path, string query, string jsonBody, WechatCredentialKind credential)

- async WechatRawResponse RequestRawAsync(string method, string path, string query, string body, string contentType, WechatCredentialKind credential)

- async WechatRawResponse RequestMultipartAsync(string method, string path, string query, WechatMultipart multipart, WechatCredentialKind credential)


## WechatWxOpenCustomApi (class)

Custom/CustomApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenCustomApi(WechatWxOpenClient client)

- async WechatWxOpenCustomApiSendImageResponse SendImageAsync(WechatWxOpenCustomApiSendImageRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendImageRawAsync(string query, string jsonBody)

- async WechatWxOpenCustomApiSendLinkResponse SendLinkAsync(WechatWxOpenCustomApiSendLinkRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendLinkRawAsync(string query, string jsonBody)

- async WechatWxOpenCustomApiSendMiniProgramPageResponse SendMiniProgramPageAsync(WechatWxOpenCustomApiSendMiniProgramPageRequest request)
  - POST /cgi-bin/message/custom/send

- async WechatResponse SendMiniProgramPageRawAsync(string query, string jsonBody)


## WechatWxOpenCustomApiSendImageRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenCustomApiSendImageRequest()

- WechatWxOpenCustomApiSendImageRequest OpenId(string fieldValue)

- WechatWxOpenCustomApiSendImageRequest MediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomApiSendImageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCustomApiSendLinkRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomApiSendLinkRequest()

- WechatWxOpenCustomApiSendLinkRequest Title(string fieldValue)

- WechatWxOpenCustomApiSendLinkRequest Description(string fieldValue)

- WechatWxOpenCustomApiSendLinkRequest Url(string fieldValue)

- WechatWxOpenCustomApiSendLinkRequest ThumbUrl(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomApiSendLinkResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCustomApiSendMiniProgramPageRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomApiSendMiniProgramPageRequest()

- WechatWxOpenCustomApiSendMiniProgramPageRequest Title(string fieldValue)

- WechatWxOpenCustomApiSendMiniProgramPageRequest PagePath(string fieldValue)

- WechatWxOpenCustomApiSendMiniProgramPageRequest ThumbMediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomApiSendMiniProgramPageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCustomServiceApi (class)

CustomService/CustomServiceApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenCustomServiceApi(WechatWxOpenClient client)

- async WechatWxOpenCustomServiceApiGetKfListResponse GetKfListAsync()
  - GET /cgi-bin/customservice/getkflist

- async WechatResponse GetKfListRawAsync(string query)

- async WechatWxOpenCustomServiceApiGetOnlineKfListResponse GetOnlineKfListAsync()
  - GET /cgi-bin/customservice/getonlinekflist

- async WechatResponse GetOnlineKfListRawAsync(string query)

- async WechatWxOpenCustomServiceApiKfAccountAddResponse KfAccountAddAsync(WechatWxOpenCustomServiceApiKfAccountAddRequest request)
  - POST /customservice/kfaccount/add

- async WechatResponse KfAccountAddRawAsync(string query, string jsonBody)

- async WechatWxOpenCustomServiceApiKfAccountDelResponse KfAccountDelAsync(WechatWxOpenCustomServiceApiKfAccountDelRequest request)
  - POST /customservice/kfaccount/del

- async WechatResponse KfAccountDelRawAsync(string query, string jsonBody)

- async WechatWxOpenCustomServiceApiKfAccountSetAdminResponse KfAccountSetAdminAsync(WechatWxOpenCustomServiceApiKfAccountSetAdminRequest request)
  - POST /customservice/kfaccount/setadmin

- async WechatResponse KfAccountSetAdminRawAsync(string query, string jsonBody)

- async WechatWxOpenCustomServiceApiKfAccountCancelAdminResponse KfAccountCancelAdminAsync(WechatWxOpenCustomServiceApiKfAccountCancelAdminRequest request)
  - POST /customservice/kfaccount/canceladmin

- async WechatResponse KfAccountCancelAdminRawAsync(string query, string jsonBody)


## WechatWxOpenCustomServiceApiGetKfListResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- public string Raw;


## WechatWxOpenCustomServiceApiGetOnlineKfListResponse (class)

- public string Raw;


## WechatWxOpenCustomServiceApiKfAccountAddRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceApiKfAccountAddRequest()

- WechatWxOpenCustomServiceApiKfAccountAddRequest KfWx(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceApiKfAccountAddResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCustomServiceApiKfAccountCancelAdminRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceApiKfAccountCancelAdminRequest()

- WechatWxOpenCustomServiceApiKfAccountCancelAdminRequest KfOpenid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceApiKfAccountCancelAdminResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCustomServiceApiKfAccountDelRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceApiKfAccountDelRequest()

- WechatWxOpenCustomServiceApiKfAccountDelRequest KfOpenid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceApiKfAccountDelResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCustomServiceApiKfAccountSetAdminRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceApiKfAccountSetAdminRequest()

- WechatWxOpenCustomServiceApiKfAccountSetAdminRequest KfOpenid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceApiKfAccountSetAdminResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCustomServiceCustomerServiceBusinessApi (class)

CustomService/CustomerServiceBusinessApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenCustomServiceCustomerServiceBusinessApi(WechatWxOpenClient client)

- async WechatWxOpenCustomServiceCustomerServiceBusinessApiRegisterResponse RegisterAsync(WechatWxOpenCustomServiceCustomerServiceBusinessApiRegisterRequest request)
  - POST /cgi-bin/business/register

- async WechatResponse RegisterRawAsync(string query, string jsonBody)

- async WechatWxOpenCustomServiceCustomerServiceBusinessApiUpdateResponse UpdateAsync(WechatWxOpenCustomServiceCustomerServiceBusinessApiUpdateRequest request)
  - POST /cgi-bin/business/update

- async WechatResponse UpdateRawAsync(string query, string jsonBody)

- async WechatWxOpenCustomServiceCustomerServiceBusinessApiGetResponse GetAsync(WechatWxOpenCustomServiceCustomerServiceBusinessApiGetRequest request)
  - POST /cgi-bin/business/get

- async WechatResponse GetRawAsync(string query, string jsonBody)

- async WechatWxOpenCustomServiceCustomerServiceBusinessApiListResponse ListAsync(WechatWxOpenCustomServiceCustomerServiceBusinessApiListRequest request)
  - POST /cgi-bin/business/list

- async WechatResponse ListRawAsync(string query, string jsonBody)


## WechatWxOpenCustomServiceCustomerServiceBusinessApiGetRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceCustomerServiceBusinessApiGetRequest()

- WechatWxOpenCustomServiceCustomerServiceBusinessApiGetRequest BusinessId(string fieldValue)

- WechatWxOpenCustomServiceCustomerServiceBusinessApiGetRequest AccountName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceCustomerServiceBusinessApiGetResponse (class)

- public string Raw;


## WechatWxOpenCustomServiceCustomerServiceBusinessApiListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceCustomerServiceBusinessApiListRequest()

- WechatWxOpenCustomServiceCustomerServiceBusinessApiListRequest Offset(int fieldValue)

- WechatWxOpenCustomServiceCustomerServiceBusinessApiListRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceCustomerServiceBusinessApiListResponse (class)

- public string Raw;


## WechatWxOpenCustomServiceCustomerServiceBusinessApiRegisterRequest (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceCustomerServiceBusinessApiRegisterRequest()

- WechatWxOpenCustomServiceCustomerServiceBusinessApiRegisterRequest AccountName(string fieldValue)

- WechatWxOpenCustomServiceCustomerServiceBusinessApiRegisterRequest Nickname(string fieldValue)

- WechatWxOpenCustomServiceCustomerServiceBusinessApiRegisterRequest IconMediaId(string fieldValue)

- WechatWxOpenCustomServiceCustomerServiceBusinessApiRegisterRequest TransferToCommonKf(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceCustomerServiceBusinessApiRegisterResponse (class)

- public string Raw;


## WechatWxOpenCustomServiceCustomerServiceBusinessApiUpdateRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceCustomerServiceBusinessApiUpdateRequest()

- WechatWxOpenCustomServiceCustomerServiceBusinessApiUpdateRequest BusinessId(string fieldValue)

- WechatWxOpenCustomServiceCustomerServiceBusinessApiUpdateRequest Nickname(string fieldValue)

- WechatWxOpenCustomServiceCustomerServiceBusinessApiUpdateRequest IconMediaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceCustomerServiceBusinessApiUpdateResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCustomServiceKfWorkApi (class)

CustomService/KfWorkApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenCustomServiceKfWorkApi(WechatWxOpenClient client)

- async WechatWxOpenCustomServiceKfWorkApiGetBoundResponse GetBoundAsync()
  - GET /customservice/work/get

- async WechatResponse GetBoundRawAsync(string query)

- async WechatWxOpenCustomServiceKfWorkApiBindResponse BindAsync(WechatWxOpenCustomServiceKfWorkApiBindRequest request)
  - POST /customservice/work/bind

- async WechatResponse BindRawAsync(string query, string jsonBody)

- async WechatWxOpenCustomServiceKfWorkApiUnbindResponse UnbindAsync(WechatWxOpenCustomServiceKfWorkApiUnbindRequest request)
  - POST /customservice/work/unbind

- async WechatResponse UnbindRawAsync(string query, string jsonBody)


## WechatWxOpenCustomServiceKfWorkApiBindRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceKfWorkApiBindRequest()

- WechatWxOpenCustomServiceKfWorkApiBindRequest CorpId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceKfWorkApiBindResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenCustomServiceKfWorkApiGetBoundResponse (class)

方法请求/响应契约；可复用的 DTO 实体见 Sdk.Wechat.Models.WxOpen。

- public string Raw;


## WechatWxOpenCustomServiceKfWorkApiUnbindRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenCustomServiceKfWorkApiUnbindRequest()

- WechatWxOpenCustomServiceKfWorkApiUnbindRequest CorpId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenCustomServiceKfWorkApiUnbindResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenDataCubeApi (class)

DataCube/DataCubeApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenDataCubeApi(WechatWxOpenClient client)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidDailySummaryTrendResponse GetWeAnalysisAppidDailySummaryTrendAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidDailySummaryTrendRequest request)
  - POST /datacube/getweanalysisappiddailysummarytrend

- async WechatResponse GetWeAnalysisAppidDailySummaryTrendRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyVisitTrendResponse GetWeAnalysisAppidDailyVisitTrendAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyVisitTrendRequest request)
  - POST /datacube/getweanalysisappiddailyvisittrend

- async WechatResponse GetWeAnalysisAppidDailyVisitTrendRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyVisitTrendResponse GetWeAnalysisAppidWeeklyVisitTrendAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyVisitTrendRequest request)
  - POST /datacube/getweanalysisappidweeklyvisittrend

- async WechatResponse GetWeAnalysisAppidWeeklyVisitTrendRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyVisitTrendResponse GetWeAnalysisAppidMonthlyVisitTrendAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyVisitTrendRequest request)
  - POST /datacube/getweanalysisappidmonthlyvisittrend

- async WechatResponse GetWeAnalysisAppidMonthlyVisitTrendRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitDistributionResponse GetWeAnalysisAppidVisitDistributionAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitDistributionRequest request)
  - POST /datacube/getweanalysisappidvisitdistribution

- async WechatResponse GetWeAnalysisAppidVisitDistributionRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyRetainInfoResponse GetWeAnalysisAppidDailyRetainInfoAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyRetainInfoRequest request)
  - POST /datacube/getweanalysisappiddailyretaininfo

- async WechatResponse GetWeAnalysisAppidDailyRetainInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyRetainInfoResponse GetWeAnalysisAppidWeeklyRetainInfoAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyRetainInfoRequest request)
  - POST /datacube/getweanalysisappidweeklyretaininfo

- async WechatResponse GetWeAnalysisAppidWeeklyRetainInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyRetainInfoResponse GetWeAnalysisAppidMonthlyRetainInfoAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyRetainInfoRequest request)
  - POST /datacube/getweanalysisappidmonthlyretaininfo

- async WechatResponse GetWeAnalysisAppidMonthlyRetainInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitPageResponse GetWeAnalysisAppidVisitPageAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitPageRequest request)
  - POST /datacube/getweanalysisappidvisitpage

- async WechatResponse GetWeAnalysisAppidVisitPageRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetWeAnalysisAppidUserPortraitResponse GetWeAnalysisAppidUserPortraitAsync(WechatWxOpenDataCubeApiGetWeAnalysisAppidUserPortraitRequest request)
  - POST /datacube/getweanalysisappiduserportrait

- async WechatResponse GetWeAnalysisAppidUserPortraitRawAsync(string query, string jsonBody)

- async WechatWxOpenDataCubeApiGetPerformanceDataResponse GetPerformanceDataAsync(WechatWxOpenDataCubeApiGetPerformanceDataRequest request)
  - POST /wxa/business/performance/boot

- async WechatResponse GetPerformanceDataRawAsync(string query, string jsonBody)


## WechatWxOpenDataCubeApiGetPerformanceDataRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetPerformanceDataRequest()

- WechatWxOpenDataCubeApiGetPerformanceDataRequest BeginTimestamp(long fieldValue)

- WechatWxOpenDataCubeApiGetPerformanceDataRequest EndTimestamp(long fieldValue)

- WechatWxOpenDataCubeApiGetPerformanceDataRequest Module(int fieldValue)

- WechatWxOpenDataCubeApiGetPerformanceDataRequest Parameters(List<WechatWxOpenPerformanceDataQuery> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetPerformanceDataResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyRetainInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyRetainInfoRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyRetainInfoRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyRetainInfoRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyRetainInfoResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidDailySummaryTrendRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidDailySummaryTrendRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidDailySummaryTrendRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidDailySummaryTrendRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidDailySummaryTrendResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyVisitTrendRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyVisitTrendRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyVisitTrendRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyVisitTrendRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidDailyVisitTrendResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyRetainInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyRetainInfoRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyRetainInfoRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyRetainInfoRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyRetainInfoResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyVisitTrendRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyVisitTrendRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyVisitTrendRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyVisitTrendRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidMonthlyVisitTrendResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidUserPortraitRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidUserPortraitRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidUserPortraitRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidUserPortraitRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidUserPortraitResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitDistributionRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitDistributionRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitDistributionRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitDistributionRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitDistributionResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitPageRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitPageRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitPageRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitPageRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidVisitPageResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyRetainInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyRetainInfoRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyRetainInfoRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyRetainInfoRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyRetainInfoResponse (class)

- public string Raw;


## WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyVisitTrendRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyVisitTrendRequest()

- WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyVisitTrendRequest BeginDate(string fieldValue)

- WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyVisitTrendRequest EndDate(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDataCubeApiGetWeAnalysisAppidWeeklyVisitTrendResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApi (class)

Delivery/DeliveryApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenDeliveryApi(WechatWxOpenClient client)

- async WechatWxOpenDeliveryApiGetAllDeliveryResponse GetAllDeliveryAsync()
  - GET /cgi-bin/express/business/delivery/getall

- async WechatResponse GetAllDeliveryRawAsync(string query)

- async WechatWxOpenDeliveryApiBindAccountResponse BindAccountAsync(WechatWxOpenDeliveryApiBindAccountRequest request)
  - POST /cgi-bin/express/business/account/bind

- async WechatResponse BindAccountRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryApiGetAllAccountResponse GetAllAccountAsync()
  - GET /cgi-bin/express/business/account/getall

- async WechatResponse GetAllAccountRawAsync(string query)

- async WechatWxOpenDeliveryApiGetPrinterResponse GetPrinterAsync()
  - GET /cgi-bin/express/business/printer/getall

- async WechatResponse GetPrinterRawAsync(string query)

- async WechatWxOpenDeliveryApiUpdatePrinterResponse UpdatePrinterAsync(WechatWxOpenDeliveryApiUpdatePrinterRequest request)
  - POST /cgi-bin/express/business/printer/update

- async WechatResponse UpdatePrinterRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryApiGetQuotaResponse GetQuotaAsync(WechatWxOpenDeliveryApiGetQuotaRequest request)
  - POST /cgi-bin/express/business/quota/get

- async WechatResponse GetQuotaRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryApiAddOrderResponse AddOrderAsync(WechatWxOpenDeliveryApiAddOrderRequest request)
  - POST /cgi-bin/express/business/order/add

- async WechatResponse AddOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryApiCancelOrderResponse CancelOrderAsync(WechatWxOpenDeliveryApiCancelOrderRequest request)
  - POST /cgi-bin/express/business/order/cancel

- async WechatResponse CancelOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryApiGetOrderResponse GetOrderAsync(WechatWxOpenDeliveryApiGetOrderRequest request)
  - POST /cgi-bin/express/business/order/get

- async WechatResponse GetOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryApiBatchGetOrderResponse BatchGetOrderAsync(WechatWxOpenDeliveryApiBatchGetOrderRequest request)
  - POST /cgi-bin/express/business/order/batchget

- async WechatResponse BatchGetOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryApiGetPathResponse GetPathAsync(WechatWxOpenDeliveryApiGetPathRequest request)
  - POST /cgi-bin/express/business/path/get

- async WechatResponse GetPathRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryApiTestUpdateOrderResponse TestUpdateOrderAsync(WechatWxOpenDeliveryApiTestUpdateOrderRequest request)
  - POST /cgi-bin/express/business/test_update_order

- async WechatResponse TestUpdateOrderRawAsync(string query, string jsonBody)


## WechatWxOpenDeliveryApiAddOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryApiAddOrderRequest()

- WechatWxOpenDeliveryApiAddOrderRequest OrderId(string fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest Openid(string fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest BizId(string fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest CustomRemark(string fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest Tagid(int fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest AddSource(int fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest WxAppid(string fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest Sender(WechatWxOpenSenderOrReceiverModel fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest Receiver(WechatWxOpenSenderOrReceiverModel fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest Cargo(WechatWxOpenCargoModel fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest Shop(WechatWxOpenShopModel fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest Insured(WechatWxOpenInsuredModel fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest Service(WechatWxOpenServiceType fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest ExpectTime(long fieldValue)

- WechatWxOpenDeliveryApiAddOrderRequest TakeMode(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryApiAddOrderResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiBatchGetOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryApiBatchGetOrderRequest()

- WechatWxOpenDeliveryApiBatchGetOrderRequest Data(List<WechatWxOpenDeliveryDeliveryJsonGetOrderModel> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryApiBatchGetOrderResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiBindAccountRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryApiBindAccountRequest()

- WechatWxOpenDeliveryApiBindAccountRequest Type(string fieldValue)

- WechatWxOpenDeliveryApiBindAccountRequest BizId(string fieldValue)

- WechatWxOpenDeliveryApiBindAccountRequest DeliveryId(string fieldValue)

- WechatWxOpenDeliveryApiBindAccountRequest Password(string fieldValue)

- WechatWxOpenDeliveryApiBindAccountRequest RemarkContent(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryApiBindAccountResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiCancelOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryApiCancelOrderRequest()

- WechatWxOpenDeliveryApiCancelOrderRequest Openid(string fieldValue)

- WechatWxOpenDeliveryApiCancelOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenDeliveryApiCancelOrderRequest WaybillId(string fieldValue)

- WechatWxOpenDeliveryApiCancelOrderRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryApiCancelOrderResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiGetAllAccountResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiGetAllDeliveryResponse (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- public string Raw;


## WechatWxOpenDeliveryApiGetOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryApiGetOrderRequest()

- WechatWxOpenDeliveryApiGetOrderRequest OrderId(string fieldValue)

- WechatWxOpenDeliveryApiGetOrderRequest Openid(string fieldValue)

- WechatWxOpenDeliveryApiGetOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenDeliveryApiGetOrderRequest WaybillId(string fieldValue)

- WechatWxOpenDeliveryApiGetOrderRequest PrintType(int fieldValue)

- WechatWxOpenDeliveryApiGetOrderRequest CustomRemark(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryApiGetOrderResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiGetPathRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryApiGetPathRequest()

- WechatWxOpenDeliveryApiGetPathRequest Openid(string fieldValue)

- WechatWxOpenDeliveryApiGetPathRequest DeliveryId(string fieldValue)

- WechatWxOpenDeliveryApiGetPathRequest WaybillId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryApiGetPathResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiGetPrinterResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiGetQuotaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryApiGetQuotaRequest()

- WechatWxOpenDeliveryApiGetQuotaRequest DeliveryId(string fieldValue)

- WechatWxOpenDeliveryApiGetQuotaRequest BizId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryApiGetQuotaResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiTestUpdateOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryApiTestUpdateOrderRequest()

- WechatWxOpenDeliveryApiTestUpdateOrderRequest BizId(string fieldValue)

- WechatWxOpenDeliveryApiTestUpdateOrderRequest OrderId(string fieldValue)

- WechatWxOpenDeliveryApiTestUpdateOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenDeliveryApiTestUpdateOrderRequest WaybillId(string fieldValue)

- WechatWxOpenDeliveryApiTestUpdateOrderRequest ActionTime(long fieldValue)

- WechatWxOpenDeliveryApiTestUpdateOrderRequest ActionType(int fieldValue)

- WechatWxOpenDeliveryApiTestUpdateOrderRequest ActionMsg(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryApiTestUpdateOrderResponse (class)

- public string Raw;


## WechatWxOpenDeliveryApiUpdatePrinterRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryApiUpdatePrinterRequest()

- WechatWxOpenDeliveryApiUpdatePrinterRequest Openid(string fieldValue)

- WechatWxOpenDeliveryApiUpdatePrinterRequest UpdateType(string fieldValue)

- WechatWxOpenDeliveryApiUpdatePrinterRequest TagidList(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryApiUpdatePrinterResponse (class)

- public string Raw;


## WechatWxOpenDeliveryProviderApi (class)

Delivery/DeliveryProviderApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenDeliveryProviderApi(WechatWxOpenClient client)

- async WechatWxOpenDeliveryProviderApiUpdateBusinessResponse UpdateBusinessAsync(WechatWxOpenDeliveryProviderApiUpdateBusinessRequest request)
  - POST /cgi-bin/express/delivery/service/business/update

- async WechatResponse UpdateBusinessRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryProviderApiUpdatePathResponse UpdatePathAsync(WechatWxOpenDeliveryProviderApiUpdatePathRequest request)
  - POST /cgi-bin/express/delivery/path/update

- async WechatResponse UpdatePathRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryProviderApiPreviewTemplateResponse PreviewTemplateAsync(WechatWxOpenDeliveryProviderApiPreviewTemplateRequest request)
  - POST /cgi-bin/express/delivery/template/preview

- async WechatResponse PreviewTemplateRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryProviderApiGetContactResponse GetContactAsync(WechatWxOpenDeliveryProviderApiGetContactRequest request)
  - POST /cgi-bin/express/delivery/contact/get

- async WechatResponse GetContactRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryProviderApiCancelOrderResponse CancelOrderAsync(WechatWxOpenDeliveryProviderApiCancelOrderRequest request)
  - POST /cgi-bin/express/delivery/single_waybill/cancel_order

- async WechatResponse CancelOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryProviderApiUpdateOrderFeeResponse UpdateOrderFeeAsync(WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest request)
  - POST /cgi-bin/express/delivery/single_waybill/fee

- async WechatResponse UpdateOrderFeeRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryProviderApiRefundOrderResponse RefundOrderAsync(WechatWxOpenDeliveryProviderApiRefundOrderRequest request)
  - POST /cgi-bin/express/delivery/single_waybill/refund_order

- async WechatResponse RefundOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryProviderApiUpdateComplaintResultResponse UpdateComplaintResultAsync(WechatWxOpenDeliveryProviderApiUpdateComplaintResultRequest request)
  - POST /cgi-bin/express/delivery/scatter/update_complaint_result

- async WechatResponse UpdateComplaintResultRawAsync(string query, string jsonBody)

- async WechatWxOpenDeliveryProviderApiUpdateOrderStatusResponse UpdateOrderStatusAsync(WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest request)
  - POST /cgi-bin/express/delivery/single_waybill/update

- async WechatResponse UpdateOrderStatusRawAsync(string query, string jsonBody)


## WechatWxOpenDeliveryProviderApiCancelOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryProviderApiCancelOrderRequest()

- WechatWxOpenDeliveryProviderApiCancelOrderRequest Token(string fieldValue)

- WechatWxOpenDeliveryProviderApiCancelOrderRequest Reason(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryProviderApiCancelOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenDeliveryProviderApiGetContactRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryProviderApiGetContactRequest()

- WechatWxOpenDeliveryProviderApiGetContactRequest Token(string fieldValue)

- WechatWxOpenDeliveryProviderApiGetContactRequest WaybillId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryProviderApiGetContactResponse (class)

- public string Raw;


## WechatWxOpenDeliveryProviderApiPreviewTemplateRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryProviderApiPreviewTemplateRequest()

- WechatWxOpenDeliveryProviderApiPreviewTemplateRequest WaybillId(string fieldValue)

- WechatWxOpenDeliveryProviderApiPreviewTemplateRequest WaybillTemplate(string fieldValue)

- WechatWxOpenDeliveryProviderApiPreviewTemplateRequest WaybillData(string fieldValue)

- WechatWxOpenDeliveryProviderApiPreviewTemplateRequest Custom(WechatWxOpenDeliveryDeliveryJsonAddOrderModel fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryProviderApiPreviewTemplateResponse (class)

- public string Raw;


## WechatWxOpenDeliveryProviderApiRefundOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryProviderApiRefundOrderRequest()

- WechatWxOpenDeliveryProviderApiRefundOrderRequest Token(string fieldValue)

- WechatWxOpenDeliveryProviderApiRefundOrderRequest Fee(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryProviderApiRefundOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenDeliveryProviderApiUpdateBusinessRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenDeliveryProviderApiUpdateBusinessRequest()

- WechatWxOpenDeliveryProviderApiUpdateBusinessRequest ShopAppId(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateBusinessRequest BizId(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateBusinessRequest ResultCode(int fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateBusinessRequest ResultMsg(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryProviderApiUpdateBusinessResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenDeliveryProviderApiUpdateComplaintResultRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryProviderApiUpdateComplaintResultRequest()

- WechatWxOpenDeliveryProviderApiUpdateComplaintResultRequest Token(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateComplaintResultRequest WaybillId(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateComplaintResultRequest Result(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateComplaintResultRequest Desc(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryProviderApiUpdateComplaintResultResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest()

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest Token(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest WaybillId(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest NeedPay(int fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest Fee(long fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest OriginalFee(long fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest BaseFee(long fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest InsuredFee(long fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest OtherFee(long fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest Remark(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderFeeRequest PayGoodsName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryProviderApiUpdateOrderFeeResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest()

- WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest Token(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest WaybillId(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest ActionTime(long fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest ActionType(int fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest ActionMsg(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest PickupCourierName(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest PickupCourierPhone(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest DeliveryCourierName(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdateOrderStatusRequest DeliveryCourierPhone(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryProviderApiUpdateOrderStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenDeliveryProviderApiUpdatePathRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenDeliveryProviderApiUpdatePathRequest()

- WechatWxOpenDeliveryProviderApiUpdatePathRequest Token(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdatePathRequest WaybillId(string fieldValue)

- WechatWxOpenDeliveryProviderApiUpdatePathRequest ActionTime(long fieldValue)

- WechatWxOpenDeliveryProviderApiUpdatePathRequest ActionType(int fieldValue)

- WechatWxOpenDeliveryProviderApiUpdatePathRequest ActionMsg(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenDeliveryProviderApiUpdatePathResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenExpressApi (class)

Express/ExpressApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenExpressApi(WechatWxOpenClient client)

- async WechatWxOpenExpressApiGetAllImmeDeliveryResponse GetAllImmeDeliveryAsync()
  - POST /cgi-bin/express/local/business/delivery/getall

- async WechatResponse GetAllImmeDeliveryRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiGetBindAccountResponse GetBindAccountAsync()
  - POST /cgi-bin/express/local/business/shop/get

- async WechatResponse GetBindAccountRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiBindAccountResponse BindAccountAsync(WechatWxOpenExpressApiBindAccountRequest request)
  - POST /cgi-bin/express/local/business/shop/add

- async WechatResponse BindAccountRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiOpenDeliveryResponse OpenDeliveryAsync()
  - POST /cgi-bin/express/local/business/open

- async WechatResponse OpenDeliveryRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiPreAddOrderResponse PreAddOrderAsync(WechatWxOpenExpressApiPreAddOrderRequest request)
  - POST /cgi-bin/express/local/business/order/pre_add

- async WechatResponse PreAddOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiPreCancelOrderResponse PreCancelOrderAsync(WechatWxOpenExpressApiPreCancelOrderRequest request)
  - POST /cgi-bin/express/local/business/order/precancel

- async WechatResponse PreCancelOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiAddOrderResponse AddOrderAsync(WechatWxOpenExpressApiAddOrderRequest request)
  - POST /cgi-bin/express/local/business/order/add

- async WechatResponse AddOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiCancelOrderResponse CancelOrderAsync(WechatWxOpenExpressApiCancelOrderRequest request)
  - POST /cgi-bin/express/local/business/order/cancel

- async WechatResponse CancelOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiReOrderResponse ReOrderAsync(WechatWxOpenExpressApiReOrderRequest request)
  - POST /cgi-bin/express/local/business/order/readd

- async WechatResponse ReOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiGetOrderResponse GetOrderAsync(WechatWxOpenExpressApiGetOrderRequest request)
  - POST /cgi-bin/express/local/business/order/get

- async WechatResponse GetOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiAddTipResponse AddTipAsync(WechatWxOpenExpressApiAddTipRequest request)
  - POST /cgi-bin/express/local/business/order/addtips

- async WechatResponse AddTipRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiAbnormalConfirmResponse AbnormalConfirmAsync(WechatWxOpenExpressApiAbnormalConfirmRequest request)
  - POST /cgi-bin/express/local/business/order/confirm_return

- async WechatResponse AbnormalConfirmRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiRealMockUpdateOrderResponse RealMockUpdateOrderAsync(WechatWxOpenExpressApiRealMockUpdateOrderRequest request)
  - POST /cgi-bin/express/local/business/realmock_update_order

- async WechatResponse RealMockUpdateOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenExpressApiMockUpdateOrderResponse MockUpdateOrderAsync(WechatWxOpenExpressApiMockUpdateOrderRequest request)
  - POST /cgi-bin/express/local/business/test_update_order

- async WechatResponse MockUpdateOrderRawAsync(string query, string jsonBody)


## WechatWxOpenExpressApiAbnormalConfirmRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiAbnormalConfirmRequest()

- WechatWxOpenExpressApiAbnormalConfirmRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiAbnormalConfirmRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiAbnormalConfirmRequest ShopNo(string fieldValue)

- WechatWxOpenExpressApiAbnormalConfirmRequest DeliverySign(string fieldValue)

- WechatWxOpenExpressApiAbnormalConfirmRequest WaybillId(string fieldValue)

- WechatWxOpenExpressApiAbnormalConfirmRequest Remark(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiAbnormalConfirmResponse (class)

- public string Raw;


## WechatWxOpenExpressApiAddOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiAddOrderRequest()

- WechatWxOpenExpressApiAddOrderRequest DeliveryToken(string fieldValue)

- WechatWxOpenExpressApiAddOrderRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiAddOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiAddOrderRequest ShopNo(string fieldValue)

- WechatWxOpenExpressApiAddOrderRequest DeliverySign(string fieldValue)

- WechatWxOpenExpressApiAddOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenExpressApiAddOrderRequest Openid(string fieldValue)

- WechatWxOpenExpressApiAddOrderRequest Sender(WechatWxOpenPreAddOrderSender fieldValue)

- WechatWxOpenExpressApiAddOrderRequest Receiver(WechatWxOpenPreAddOrderReceiver fieldValue)

- WechatWxOpenExpressApiAddOrderRequest Cargo(WechatWxOpenPreAddOrderCargo fieldValue)

- WechatWxOpenExpressApiAddOrderRequest OrderInfo(WechatWxOpenPreAddOrderOrderInfo fieldValue)

- WechatWxOpenExpressApiAddOrderRequest Shop(WechatWxOpenPreAddOrderShop fieldValue)

- WechatWxOpenExpressApiAddOrderRequest SubBizId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiAddOrderResponse (class)

- public string Raw;


## WechatWxOpenExpressApiAddTipRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiAddTipRequest()

- WechatWxOpenExpressApiAddTipRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiAddTipRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiAddTipRequest ShopNo(string fieldValue)

- WechatWxOpenExpressApiAddTipRequest DeliverySign(string fieldValue)

- WechatWxOpenExpressApiAddTipRequest WaybillId(string fieldValue)

- WechatWxOpenExpressApiAddTipRequest Openid(string fieldValue)

- WechatWxOpenExpressApiAddTipRequest Tips(double fieldValue)

- WechatWxOpenExpressApiAddTipRequest Remark(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiAddTipResponse (class)

- public string Raw;


## WechatWxOpenExpressApiBindAccountRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiBindAccountRequest()

- WechatWxOpenExpressApiBindAccountRequest DeliveryId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiBindAccountResponse (class)

- public string Raw;


## WechatWxOpenExpressApiCancelOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiCancelOrderRequest()

- WechatWxOpenExpressApiCancelOrderRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiCancelOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiCancelOrderRequest ShopNo(string fieldValue)

- WechatWxOpenExpressApiCancelOrderRequest DeliverySign(string fieldValue)

- WechatWxOpenExpressApiCancelOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenExpressApiCancelOrderRequest WaybillId(string fieldValue)

- WechatWxOpenExpressApiCancelOrderRequest CancelReasonId(int fieldValue)

- WechatWxOpenExpressApiCancelOrderRequest CancelReason(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiCancelOrderResponse (class)

- public string Raw;


## WechatWxOpenExpressApiGetAllImmeDeliveryResponse (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- public string Raw;


## WechatWxOpenExpressApiGetBindAccountResponse (class)

- public string Raw;


## WechatWxOpenExpressApiGetOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiGetOrderRequest()

- WechatWxOpenExpressApiGetOrderRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiGetOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiGetOrderRequest ShopNo(string fieldValue)

- WechatWxOpenExpressApiGetOrderRequest DeliverySign(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiGetOrderResponse (class)

- public string Raw;


## WechatWxOpenExpressApiMockUpdateOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiMockUpdateOrderRequest()

- WechatWxOpenExpressApiMockUpdateOrderRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiMockUpdateOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiMockUpdateOrderRequest ActionTime(long fieldValue)

- WechatWxOpenExpressApiMockUpdateOrderRequest OrderStatus(int fieldValue)

- WechatWxOpenExpressApiMockUpdateOrderRequest ActionMsg(string fieldValue)

- WechatWxOpenExpressApiMockUpdateOrderRequest DeliverySign(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiMockUpdateOrderResponse (class)

- public string Raw;


## WechatWxOpenExpressApiOpenDeliveryResponse (class)

- public string Raw;


## WechatWxOpenExpressApiPreAddOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiPreAddOrderRequest()

- WechatWxOpenExpressApiPreAddOrderRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest ShopNo(string fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest DeliverySign(string fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest Openid(string fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest Sender(WechatWxOpenPreAddOrderSender fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest Receiver(WechatWxOpenPreAddOrderReceiver fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest Cargo(WechatWxOpenPreAddOrderCargo fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest OrderInfo(WechatWxOpenPreAddOrderOrderInfo fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest Shop(WechatWxOpenPreAddOrderShop fieldValue)

- WechatWxOpenExpressApiPreAddOrderRequest SubBizId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiPreAddOrderResponse (class)

- public string Raw;


## WechatWxOpenExpressApiPreCancelOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiPreCancelOrderRequest()

- WechatWxOpenExpressApiPreCancelOrderRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiPreCancelOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiPreCancelOrderRequest ShopNo(string fieldValue)

- WechatWxOpenExpressApiPreCancelOrderRequest DeliverySign(string fieldValue)

- WechatWxOpenExpressApiPreCancelOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenExpressApiPreCancelOrderRequest WaybillId(string fieldValue)

- WechatWxOpenExpressApiPreCancelOrderRequest CancelReasonId(int fieldValue)

- WechatWxOpenExpressApiPreCancelOrderRequest CancelReason(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiPreCancelOrderResponse (class)

- public string Raw;


## WechatWxOpenExpressApiReOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiReOrderRequest()

- WechatWxOpenExpressApiReOrderRequest DeliveryToken(string fieldValue)

- WechatWxOpenExpressApiReOrderRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiReOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiReOrderRequest ShopNo(string fieldValue)

- WechatWxOpenExpressApiReOrderRequest DeliverySign(string fieldValue)

- WechatWxOpenExpressApiReOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenExpressApiReOrderRequest Openid(string fieldValue)

- WechatWxOpenExpressApiReOrderRequest Sender(WechatWxOpenPreAddOrderSender fieldValue)

- WechatWxOpenExpressApiReOrderRequest Receiver(WechatWxOpenPreAddOrderReceiver fieldValue)

- WechatWxOpenExpressApiReOrderRequest Cargo(WechatWxOpenPreAddOrderCargo fieldValue)

- WechatWxOpenExpressApiReOrderRequest OrderInfo(WechatWxOpenPreAddOrderOrderInfo fieldValue)

- WechatWxOpenExpressApiReOrderRequest Shop(WechatWxOpenPreAddOrderShop fieldValue)

- WechatWxOpenExpressApiReOrderRequest SubBizId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiReOrderResponse (class)

- public string Raw;


## WechatWxOpenExpressApiRealMockUpdateOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenExpressApiRealMockUpdateOrderRequest()

- WechatWxOpenExpressApiRealMockUpdateOrderRequest Shopid(string fieldValue)

- WechatWxOpenExpressApiRealMockUpdateOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenExpressApiRealMockUpdateOrderRequest ActionTime(long fieldValue)

- WechatWxOpenExpressApiRealMockUpdateOrderRequest OrderStatus(int fieldValue)

- WechatWxOpenExpressApiRealMockUpdateOrderRequest ActionMsg(string fieldValue)

- WechatWxOpenExpressApiRealMockUpdateOrderRequest DeliverySign(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenExpressApiRealMockUpdateOrderResponse (class)

- public string Raw;


## WechatWxOpenFaceApi (class)

Face/FaceApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenFaceApi(WechatWxOpenClient client)

- async WechatWxOpenFaceApiGetVerifyIdResponse GetVerifyIdAsync(WechatWxOpenFaceApiGetVerifyIdRequest request)
  - POST /cityservice/face/identify/getverifyid

- async WechatResponse GetVerifyIdRawAsync(string query, string jsonBody)

- async WechatWxOpenFaceApiQueryVerifyInfoResponse QueryVerifyInfoAsync(WechatWxOpenFaceApiQueryVerifyInfoRequest request)
  - POST /cityservice/face/identify/queryverifyinfo

- async WechatResponse QueryVerifyInfoRawAsync(string query, string jsonBody)


## WechatWxOpenFaceApiGetVerifyIdRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenFaceApiGetVerifyIdRequest()

- WechatWxOpenFaceApiGetVerifyIdRequest OutSeqNo(string fieldValue)

- WechatWxOpenFaceApiGetVerifyIdRequest CertInfo(WechatWxOpenFaceCertificateInfo fieldValue)

- WechatWxOpenFaceApiGetVerifyIdRequest Openid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenFaceApiGetVerifyIdResponse (class)

- public string Raw;


## WechatWxOpenFaceApiQueryVerifyInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenFaceApiQueryVerifyInfoRequest()

- WechatWxOpenFaceApiQueryVerifyInfoRequest VerifyId(string fieldValue)

- WechatWxOpenFaceApiQueryVerifyInfoRequest OutSeqNo(string fieldValue)

- WechatWxOpenFaceApiQueryVerifyInfoRequest CertHash(string fieldValue)

- WechatWxOpenFaceApiQueryVerifyInfoRequest Openid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenFaceApiQueryVerifyInfoResponse (class)

- public string Raw;


## WechatWxOpenHardwareDeviceApi (class)

HardwareDevice/HardwareDeviceApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenHardwareDeviceApi(WechatWxOpenClient client)

- async WechatWxOpenHardwareDeviceApiSendDeviceMessageResponse SendDeviceMessageAsync(WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest request)
  - POST /cgi-bin/message/device/subscribe/send

- async WechatResponse SendDeviceMessageRawAsync(string query, string jsonBody)


## WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest()

- WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest TemplateId(string fieldValue)

- WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest Sn(string fieldValue)

- WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest ModelId(string fieldValue)

- WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest Page(string fieldValue)

- WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest ToOpenIdList(List<string> fieldValue)

- WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest MiniprogramState(string fieldValue)

- WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest Data(JsonValue fieldValue)

- WechatWxOpenHardwareDeviceApiSendDeviceMessageRequest Lang(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenHardwareDeviceApiSendDeviceMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenImmediateDeliveryApi (class)

ImmediateDelivery/ImmediateDeliveryApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenImmediateDeliveryApi(WechatWxOpenClient client)

- async WechatWxOpenImmediateDeliveryApiGetAllDeliveryCompaniesResponse GetAllDeliveryCompaniesAsync()
  - POST /cgi-bin/express/local/business/delivery/getall

- async WechatResponse GetAllDeliveryCompaniesRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiPreAddOrderResponse PreAddOrderAsync(WechatWxOpenImmediateDeliveryApiPreAddOrderRequest request)
  - POST /cgi-bin/express/local/business/order/pre_add

- async WechatResponse PreAddOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiGetBoundAccountsResponse GetBoundAccountsAsync()
  - POST /cgi-bin/express/local/business/shop/get

- async WechatResponse GetBoundAccountsRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiPreCancelOrderResponse PreCancelOrderAsync(WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest request)
  - POST /cgi-bin/express/local/business/order/precancel

- async WechatResponse PreCancelOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiOpenDeliveryResponse OpenDeliveryAsync()
  - POST /cgi-bin/express/local/business/open

- async WechatResponse OpenDeliveryRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiBindAccountResponse BindAccountAsync(WechatWxOpenImmediateDeliveryApiBindAccountRequest request)
  - POST /cgi-bin/express/local/business/shop/add

- async WechatResponse BindAccountRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiReAddOrderResponse ReAddOrderAsync(WechatWxOpenImmediateDeliveryApiReAddOrderRequest request)
  - POST /cgi-bin/express/local/business/order/readd

- async WechatResponse ReAddOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderResponse RealMockUpdateOrderAsync(WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderRequest request)
  - POST /cgi-bin/express/local/business/realmock_update_order

- async WechatResponse RealMockUpdateOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiMockUpdateOrderResponse MockUpdateOrderAsync(WechatWxOpenImmediateDeliveryApiMockUpdateOrderRequest request)
  - POST /cgi-bin/express/local/business/test_update_order

- async WechatResponse MockUpdateOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiGetOrderResponse GetOrderAsync(WechatWxOpenImmediateDeliveryApiGetOrderRequest request)
  - POST /cgi-bin/express/local/business/order/get

- async WechatResponse GetOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiConfirmReturnResponse ConfirmReturnAsync(WechatWxOpenImmediateDeliveryApiConfirmReturnRequest request)
  - POST /cgi-bin/express/local/business/order/confirm_return

- async WechatResponse ConfirmReturnRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiCancelOrderResponse CancelOrderAsync(WechatWxOpenImmediateDeliveryApiCancelOrderRequest request)
  - POST /cgi-bin/express/local/business/order/cancel

- async WechatResponse CancelOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiAddTipsResponse AddTipsAsync(WechatWxOpenImmediateDeliveryApiAddTipsRequest request)
  - POST /cgi-bin/express/local/business/order/addtips

- async WechatResponse AddTipsRawAsync(string query, string jsonBody)

- async WechatWxOpenImmediateDeliveryApiAddOrderResponse AddOrderAsync(WechatWxOpenImmediateDeliveryApiAddOrderRequest request)
  - POST /cgi-bin/express/local/business/order/add

- async WechatResponse AddOrderRawAsync(string query, string jsonBody)


## WechatWxOpenImmediateDeliveryApiAddOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiAddOrderRequest()

- WechatWxOpenImmediateDeliveryApiAddOrderRequest DeliveryToken(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest Openid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest Sender(WechatWxOpenImmediateDeliveryContact fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest Receiver(WechatWxOpenImmediateDeliveryContact fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest Cargo(WechatWxOpenImmediateDeliveryCargo fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest OrderInfo(WechatWxOpenImmediateDeliveryOrderInfo fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest Shop(WechatWxOpenImmediateDeliveryShop fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest DeliverySign(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest ShopNo(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddOrderRequest SubBizId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiAddOrderResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiAddTipsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiAddTipsRequest()

- WechatWxOpenImmediateDeliveryApiAddTipsRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddTipsRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddTipsRequest WaybillId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddTipsRequest Tips(double fieldValue)

- WechatWxOpenImmediateDeliveryApiAddTipsRequest Remark(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddTipsRequest DeliverySign(string fieldValue)

- WechatWxOpenImmediateDeliveryApiAddTipsRequest ShopNo(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiAddTipsResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiBindAccountRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiBindAccountRequest()

- WechatWxOpenImmediateDeliveryApiBindAccountRequest DeliveryId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiBindAccountResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiCancelOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiCancelOrderRequest()

- WechatWxOpenImmediateDeliveryApiCancelOrderRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiCancelOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiCancelOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiCancelOrderRequest WaybillId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiCancelOrderRequest CancelReasonId(int fieldValue)

- WechatWxOpenImmediateDeliveryApiCancelOrderRequest CancelReason(string fieldValue)

- WechatWxOpenImmediateDeliveryApiCancelOrderRequest ShopNo(string fieldValue)

- WechatWxOpenImmediateDeliveryApiCancelOrderRequest DeliverySign(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiCancelOrderResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiConfirmReturnRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiConfirmReturnRequest()

- WechatWxOpenImmediateDeliveryApiConfirmReturnRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiConfirmReturnRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiConfirmReturnRequest WaybillId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiConfirmReturnRequest DeliverySign(string fieldValue)

- WechatWxOpenImmediateDeliveryApiConfirmReturnRequest ShopNo(string fieldValue)

- WechatWxOpenImmediateDeliveryApiConfirmReturnRequest Remark(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiConfirmReturnResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiGetAllDeliveryCompaniesResponse (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- public string Raw;


## WechatWxOpenImmediateDeliveryApiGetBoundAccountsResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiGetOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiGetOrderRequest()

- WechatWxOpenImmediateDeliveryApiGetOrderRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiGetOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiGetOrderRequest ShopNo(string fieldValue)

- WechatWxOpenImmediateDeliveryApiGetOrderRequest DeliverySign(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiGetOrderResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiMockUpdateOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiMockUpdateOrderRequest()

- WechatWxOpenImmediateDeliveryApiMockUpdateOrderRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiMockUpdateOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiMockUpdateOrderRequest WaybillId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiMockUpdateOrderRequest OrderStatus(int fieldValue)

- WechatWxOpenImmediateDeliveryApiMockUpdateOrderRequest ActionTime(long fieldValue)

- WechatWxOpenImmediateDeliveryApiMockUpdateOrderRequest ActionMsg(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiMockUpdateOrderResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiOpenDeliveryResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiPreAddOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiPreAddOrderRequest()

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest Openid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest Sender(WechatWxOpenImmediateDeliveryContact fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest Receiver(WechatWxOpenImmediateDeliveryContact fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest Cargo(WechatWxOpenImmediateDeliveryCargo fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest OrderInfo(WechatWxOpenImmediateDeliveryOrderInfo fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest Shop(WechatWxOpenImmediateDeliveryShop fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest DeliverySign(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest ShopNo(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreAddOrderRequest SubBizId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiPreAddOrderResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest()

- WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest WaybillId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest CancelReasonId(int fieldValue)

- WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest CancelReason(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest ShopNo(string fieldValue)

- WechatWxOpenImmediateDeliveryApiPreCancelOrderRequest DeliverySign(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiPreCancelOrderResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiReAddOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiReAddOrderRequest()

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest DeliveryToken(string fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest DeliveryId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest Openid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest Sender(WechatWxOpenImmediateDeliveryContact fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest Receiver(WechatWxOpenImmediateDeliveryContact fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest Cargo(WechatWxOpenImmediateDeliveryCargo fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest OrderInfo(WechatWxOpenImmediateDeliveryOrderInfo fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest Shop(WechatWxOpenImmediateDeliveryShop fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest DeliverySign(string fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest ShopNo(string fieldValue)

- WechatWxOpenImmediateDeliveryApiReAddOrderRequest SubBizId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiReAddOrderResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderRequest()

- WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderRequest OrderStatus(int fieldValue)

- WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderRequest ActionTime(long fieldValue)

- WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderRequest ActionMsg(string fieldValue)

- WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderRequest DeliverySign(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryApiRealMockUpdateOrderResponse (class)

- public string Raw;


## WechatWxOpenImmediateDeliveryProviderApi (class)

ImmediateDelivery/ImmediateDeliveryProviderApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenImmediateDeliveryProviderApi(WechatWxOpenClient client)

- async WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusResponse UpdateOrderStatusAsync(WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest request)
  - POST /cgi-bin/express/local/delivery/update_order

- async WechatResponse UpdateOrderStatusRawAsync(string query, string jsonBody)


## WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest()

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest WxToken(string fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest OrderStatus(int fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest WaybillId(string fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest ActionMsg(string fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest ActionTime(long fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest Agent(WechatWxOpenImmediateDeliveryAgent fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest Shopid(string fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest ShopOrderId(string fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest ShopNo(string fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest WxaPath(string fieldValue)

- WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusRequest ExpectedDeliveryTime(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenImmediateDeliveryProviderApiUpdateOrderStatusResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastApi (class)

LiveBroadcast/LiveBroadcastApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenLiveBroadcastApi(WechatWxOpenClient client)

- async WechatWxOpenLiveBroadcastApiCreateRoomResponse CreateRoomAsync(WechatWxOpenLiveBroadcastApiCreateRoomRequest request)
  - POST /wxaapi/broadcast/room/create

- async WechatResponse CreateRoomRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiGetLiveInfoResponse GetLiveInfoAsync(WechatWxOpenLiveBroadcastApiGetLiveInfoRequest request)
  - POST /wxa/business/getliveinfo

- async WechatResponse GetLiveInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiDeleteRoomResponse DeleteRoomAsync(WechatWxOpenLiveBroadcastApiDeleteRoomRequest request)
  - POST /wxaapi/broadcast/room/deleteroom

- async WechatResponse DeleteRoomRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiImportGoodsResponse ImportGoodsAsync(WechatWxOpenLiveBroadcastApiImportGoodsRequest request)
  - POST /wxaapi/broadcast/room/addgoods

- async WechatResponse ImportGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiEditRoomResponse EditRoomAsync(WechatWxOpenLiveBroadcastApiEditRoomRequest request)
  - POST /wxaapi/broadcast/room/editroom

- async WechatResponse EditRoomRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiGetPushUrlResponse GetPushUrlAsync(WechatWxOpenLiveBroadcastApiGetPushUrlRequest request)
  - GET /wxaapi/broadcast/room/getpushurl

- async WechatResponse GetPushUrlRawAsync(string query)

- async WechatWxOpenLiveBroadcastApiGetSharedCodeResponse GetSharedCodeAsync(WechatWxOpenLiveBroadcastApiGetSharedCodeRequest request)
  - GET /wxaapi/broadcast/room/getsharedcode

- async WechatResponse GetSharedCodeRawAsync(string query)

- async WechatWxOpenLiveBroadcastApiGetSubAnchorResponse GetSubAnchorAsync(WechatWxOpenLiveBroadcastApiGetSubAnchorRequest request)
  - GET /wxaapi/broadcast/room/getsubanchor

- async WechatResponse GetSubAnchorRawAsync(string query)

- async WechatWxOpenLiveBroadcastApiModifySubAnchorResponse ModifySubAnchorAsync(WechatWxOpenLiveBroadcastApiModifySubAnchorRequest request)
  - POST /wxaapi/broadcast/room/modifysubanchor

- async WechatResponse ModifySubAnchorRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiDeleteSubAnchorResponse DeleteSubAnchorAsync(WechatWxOpenLiveBroadcastApiDeleteSubAnchorRequest request)
  - POST /wxaapi/broadcast/room/deletesubanchor

- async WechatResponse DeleteSubAnchorRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiAddSubAnchorResponse AddSubAnchorAsync(WechatWxOpenLiveBroadcastApiAddSubAnchorRequest request)
  - POST /wxaapi/broadcast/room/addsubanchor

- async WechatResponse AddSubAnchorRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiDeleteRoomGoodsResponse DeleteRoomGoodsAsync(WechatWxOpenLiveBroadcastApiDeleteRoomGoodsRequest request)
  - POST /wxaapi/broadcast/goods/deleteInRoom

- async WechatResponse DeleteRoomGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiPushGoodsResponse PushGoodsAsync(WechatWxOpenLiveBroadcastApiPushGoodsRequest request)
  - POST /wxaapi/broadcast/goods/push

- async WechatResponse PushGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiSetGoodsOnSaleResponse SetGoodsOnSaleAsync(WechatWxOpenLiveBroadcastApiSetGoodsOnSaleRequest request)
  - POST /wxaapi/broadcast/goods/onsale

- async WechatResponse SetGoodsOnSaleRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiSortRoomGoodsResponse SortRoomGoodsAsync(WechatWxOpenLiveBroadcastApiSortRoomGoodsRequest request)
  - POST /wxaapi/broadcast/goods/sort

- async WechatResponse SortRoomGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiModifyAssistantResponse ModifyAssistantAsync(WechatWxOpenLiveBroadcastApiModifyAssistantRequest request)
  - POST /wxaapi/broadcast/room/modifyassistant

- async WechatResponse ModifyAssistantRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiGetAssistantListResponse GetAssistantListAsync(WechatWxOpenLiveBroadcastApiGetAssistantListRequest request)
  - GET /wxaapi/broadcast/room/getassistantlist

- async WechatResponse GetAssistantListRawAsync(string query)

- async WechatWxOpenLiveBroadcastApiRemoveAssistantResponse RemoveAssistantAsync(WechatWxOpenLiveBroadcastApiRemoveAssistantRequest request)
  - POST /wxaapi/broadcast/room/removeassistant

- async WechatResponse RemoveAssistantRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiAddAssistantsResponse AddAssistantsAsync(WechatWxOpenLiveBroadcastApiAddAssistantsRequest request)
  - POST /wxaapi/broadcast/room/addassistant

- async WechatResponse AddAssistantsRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiUpdateCommentResponse UpdateCommentAsync(WechatWxOpenLiveBroadcastApiUpdateCommentRequest request)
  - POST /wxaapi/broadcast/room/updatecomment

- async WechatResponse UpdateCommentRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiUpdateFeedPublicResponse UpdateFeedPublicAsync(WechatWxOpenLiveBroadcastApiUpdateFeedPublicRequest request)
  - POST /wxaapi/broadcast/room/updatefeedpublic

- async WechatResponse UpdateFeedPublicRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiUpdateCustomerServiceResponse UpdateCustomerServiceAsync(WechatWxOpenLiveBroadcastApiUpdateCustomerServiceRequest request)
  - POST /wxaapi/broadcast/room/updatekf

- async WechatResponse UpdateCustomerServiceRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiUpdateReplayResponse UpdateReplayAsync(WechatWxOpenLiveBroadcastApiUpdateReplayRequest request)
  - POST /wxaapi/broadcast/room/updatereplay

- async WechatResponse UpdateReplayRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiGetGoodsVideoResponse GetGoodsVideoAsync(WechatWxOpenLiveBroadcastApiGetGoodsVideoRequest request)
  - POST /wxaapi/broadcast/goods/getVideo

- async WechatResponse GetGoodsVideoRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiSetDefaultGoodsKeyResponse SetDefaultGoodsKeyAsync(WechatWxOpenLiveBroadcastApiSetDefaultGoodsKeyRequest request)
  - POST /wxaapi/broadcast/goods/setkey

- async WechatResponse SetDefaultGoodsKeyRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastApiGetDefaultGoodsKeyResponse GetDefaultGoodsKeyAsync()
  - GET /wxaapi/broadcast/goods/getkey

- async WechatResponse GetDefaultGoodsKeyRawAsync(string query)


## WechatWxOpenLiveBroadcastApiAddAssistantsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiAddAssistantsRequest()

- WechatWxOpenLiveBroadcastApiAddAssistantsRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiAddAssistantsRequest Users(List<WechatWxOpenLiveBroadcastAssistantUser> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiAddAssistantsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiAddSubAnchorRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiAddSubAnchorRequest()

- WechatWxOpenLiveBroadcastApiAddSubAnchorRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiAddSubAnchorRequest Username(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiAddSubAnchorResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiCreateRoomRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiCreateRoomRequest()

- WechatWxOpenLiveBroadcastApiCreateRoomRequest Name(string fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest CoverImg(string fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest StartTime(long fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest EndTime(long fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest AnchorName(string fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest AnchorWechat(string fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest SubAnchorWechat(string fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest CreaterWechat(string fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest ShareImg(string fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest FeedsImg(string fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest IsFeedsPublic(int fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest Type(int fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest CloseLike(int fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest CloseGoods(int fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest CloseComment(int fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest CloseReplay(int fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest CloseShare(int fieldValue)

- WechatWxOpenLiveBroadcastApiCreateRoomRequest CloseKf(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiCreateRoomResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastApiDeleteRoomGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiDeleteRoomGoodsRequest()

- WechatWxOpenLiveBroadcastApiDeleteRoomGoodsRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiDeleteRoomGoodsRequest GoodsId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiDeleteRoomGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiDeleteRoomRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiDeleteRoomRequest()

- WechatWxOpenLiveBroadcastApiDeleteRoomRequest Id(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiDeleteRoomResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiDeleteSubAnchorRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiDeleteSubAnchorRequest()

- WechatWxOpenLiveBroadcastApiDeleteSubAnchorRequest RoomId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiDeleteSubAnchorResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiEditRoomRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiEditRoomRequest()

- WechatWxOpenLiveBroadcastApiEditRoomRequest Id(long fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest Name(string fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest CoverImg(string fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest StartTime(long fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest EndTime(long fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest AnchorName(string fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest AnchorWechat(string fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest ShareImg(string fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest FeedsImg(string fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest IsFeedsPublic(int fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest CloseLike(int fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest CloseGoods(int fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest CloseComment(int fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest CloseReplay(int fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest CloseShare(int fieldValue)

- WechatWxOpenLiveBroadcastApiEditRoomRequest CloseKf(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiEditRoomResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiGetAssistantListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiGetAssistantListRequest()

- WechatWxOpenLiveBroadcastApiGetAssistantListRequest RoomId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiGetAssistantListResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastApiGetDefaultGoodsKeyResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastApiGetGoodsVideoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiGetGoodsVideoRequest()

- WechatWxOpenLiveBroadcastApiGetGoodsVideoRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiGetGoodsVideoRequest GoodsId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiGetGoodsVideoResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastApiGetLiveInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiGetLiveInfoRequest()

- WechatWxOpenLiveBroadcastApiGetLiveInfoRequest Start(int fieldValue)

- WechatWxOpenLiveBroadcastApiGetLiveInfoRequest Limit(int fieldValue)

- WechatWxOpenLiveBroadcastApiGetLiveInfoRequest Action(string fieldValue)

- WechatWxOpenLiveBroadcastApiGetLiveInfoRequest RoomId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiGetLiveInfoResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastApiGetPushUrlRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiGetPushUrlRequest()

- WechatWxOpenLiveBroadcastApiGetPushUrlRequest RoomId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiGetPushUrlResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastApiGetSharedCodeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiGetSharedCodeRequest()

- WechatWxOpenLiveBroadcastApiGetSharedCodeRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiGetSharedCodeRequest CustomParams(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiGetSharedCodeResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastApiGetSubAnchorRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiGetSubAnchorRequest()

- WechatWxOpenLiveBroadcastApiGetSubAnchorRequest RoomId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiGetSubAnchorResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastApiImportGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiImportGoodsRequest()

- WechatWxOpenLiveBroadcastApiImportGoodsRequest Ids(List<long> fieldValue)

- WechatWxOpenLiveBroadcastApiImportGoodsRequest RoomId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiImportGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiModifyAssistantRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiModifyAssistantRequest()

- WechatWxOpenLiveBroadcastApiModifyAssistantRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiModifyAssistantRequest Username(string fieldValue)

- WechatWxOpenLiveBroadcastApiModifyAssistantRequest Nickname(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiModifyAssistantResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiModifySubAnchorRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiModifySubAnchorRequest()

- WechatWxOpenLiveBroadcastApiModifySubAnchorRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiModifySubAnchorRequest Username(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiModifySubAnchorResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiPushGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiPushGoodsRequest()

- WechatWxOpenLiveBroadcastApiPushGoodsRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiPushGoodsRequest GoodsId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiPushGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiRemoveAssistantRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiRemoveAssistantRequest()

- WechatWxOpenLiveBroadcastApiRemoveAssistantRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiRemoveAssistantRequest Username(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiRemoveAssistantResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiSetDefaultGoodsKeyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiSetDefaultGoodsKeyRequest()

- WechatWxOpenLiveBroadcastApiSetDefaultGoodsKeyRequest GoodsKey(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiSetDefaultGoodsKeyResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiSetGoodsOnSaleRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiSetGoodsOnSaleRequest()

- WechatWxOpenLiveBroadcastApiSetGoodsOnSaleRequest OnSale(int fieldValue)

- WechatWxOpenLiveBroadcastApiSetGoodsOnSaleRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiSetGoodsOnSaleRequest GoodsId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiSetGoodsOnSaleResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiSortRoomGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiSortRoomGoodsRequest()

- WechatWxOpenLiveBroadcastApiSortRoomGoodsRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiSortRoomGoodsRequest Goods(List<WechatWxOpenLiveBroadcastGoodsId> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiSortRoomGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiUpdateCommentRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiUpdateCommentRequest()

- WechatWxOpenLiveBroadcastApiUpdateCommentRequest Id(long fieldValue)

- WechatWxOpenLiveBroadcastApiUpdateCommentRequest BanComment(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiUpdateCommentResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiUpdateCustomerServiceRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiUpdateCustomerServiceRequest()

- WechatWxOpenLiveBroadcastApiUpdateCustomerServiceRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiUpdateCustomerServiceRequest CloseKf(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiUpdateCustomerServiceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiUpdateFeedPublicRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiUpdateFeedPublicRequest()

- WechatWxOpenLiveBroadcastApiUpdateFeedPublicRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiUpdateFeedPublicRequest IsFeedsPublic(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiUpdateFeedPublicResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastApiUpdateReplayRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastApiUpdateReplayRequest()

- WechatWxOpenLiveBroadcastApiUpdateReplayRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastApiUpdateReplayRequest CloseReplay(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastApiUpdateReplayResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastGoodsApi (class)

LiveBroadcast/LiveBroadcastGoodsApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenLiveBroadcastGoodsApi(WechatWxOpenClient client)

- async WechatWxOpenLiveBroadcastGoodsApiAddGoodsResponse AddGoodsAsync(WechatWxOpenLiveBroadcastGoodsApiAddGoodsRequest request)
  - POST /wxaapi/broadcast/goods/add

- async WechatResponse AddGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastGoodsApiResubmitGoodsAuditResponse ResubmitGoodsAuditAsync(WechatWxOpenLiveBroadcastGoodsApiResubmitGoodsAuditRequest request)
  - POST /wxaapi/broadcast/goods/audit

- async WechatResponse ResubmitGoodsAuditRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastGoodsApiGetGoodsWarehouseResponse GetGoodsWarehouseAsync(WechatWxOpenLiveBroadcastGoodsApiGetGoodsWarehouseRequest request)
  - POST /wxa/business/getgoodswarehouse

- async WechatResponse GetGoodsWarehouseRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastGoodsApiResetGoodsAuditResponse ResetGoodsAuditAsync(WechatWxOpenLiveBroadcastGoodsApiResetGoodsAuditRequest request)
  - POST /wxaapi/broadcast/goods/resetaudit

- async WechatResponse ResetGoodsAuditRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastGoodsApiUpdateGoodsResponse UpdateGoodsAsync(WechatWxOpenLiveBroadcastGoodsApiUpdateGoodsRequest request)
  - POST /wxaapi/broadcast/goods/update

- async WechatResponse UpdateGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastGoodsApiGetApprovedGoodsResponse GetApprovedGoodsAsync(WechatWxOpenLiveBroadcastGoodsApiGetApprovedGoodsRequest request)
  - GET /wxaapi/broadcast/goods/getapproved

- async WechatResponse GetApprovedGoodsRawAsync(string query)

- async WechatWxOpenLiveBroadcastGoodsApiDeleteGoodsResponse DeleteGoodsAsync(WechatWxOpenLiveBroadcastGoodsApiDeleteGoodsRequest request)
  - POST /wxaapi/broadcast/goods/delete

- async WechatResponse DeleteGoodsRawAsync(string query, string jsonBody)


## WechatWxOpenLiveBroadcastGoodsApiAddGoodsRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastGoodsApiAddGoodsRequest()

- WechatWxOpenLiveBroadcastGoodsApiAddGoodsRequest GoodsInfo(WechatWxOpenLiveBroadcastGoodsInfoRequest fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastGoodsApiAddGoodsResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastGoodsApiDeleteGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastGoodsApiDeleteGoodsRequest()

- WechatWxOpenLiveBroadcastGoodsApiDeleteGoodsRequest GoodsId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastGoodsApiDeleteGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastGoodsApiGetApprovedGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastGoodsApiGetApprovedGoodsRequest()

- WechatWxOpenLiveBroadcastGoodsApiGetApprovedGoodsRequest Status(int fieldValue)

- WechatWxOpenLiveBroadcastGoodsApiGetApprovedGoodsRequest Offset(int fieldValue)

- WechatWxOpenLiveBroadcastGoodsApiGetApprovedGoodsRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastGoodsApiGetApprovedGoodsResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastGoodsApiGetGoodsWarehouseRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastGoodsApiGetGoodsWarehouseRequest()

- WechatWxOpenLiveBroadcastGoodsApiGetGoodsWarehouseRequest GoodsIds(List<long> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastGoodsApiGetGoodsWarehouseResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastGoodsApiResetGoodsAuditRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastGoodsApiResetGoodsAuditRequest()

- WechatWxOpenLiveBroadcastGoodsApiResetGoodsAuditRequest GoodsId(long fieldValue)

- WechatWxOpenLiveBroadcastGoodsApiResetGoodsAuditRequest AuditId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastGoodsApiResetGoodsAuditResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastGoodsApiResubmitGoodsAuditRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastGoodsApiResubmitGoodsAuditRequest()

- WechatWxOpenLiveBroadcastGoodsApiResubmitGoodsAuditRequest GoodsId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastGoodsApiResubmitGoodsAuditResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastGoodsApiUpdateGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastGoodsApiUpdateGoodsRequest()

- WechatWxOpenLiveBroadcastGoodsApiUpdateGoodsRequest GoodsInfo(WechatWxOpenLiveBroadcastGoodsInfoRequest fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastGoodsApiUpdateGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastRoleSubscriptionApi (class)

LiveBroadcast/LiveBroadcastRoleSubscriptionApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenLiveBroadcastRoleSubscriptionApi(WechatWxOpenClient client)

- async WechatWxOpenLiveBroadcastRoleSubscriptionApiAddRoleResponse AddRoleAsync(WechatWxOpenLiveBroadcastRoleSubscriptionApiAddRoleRequest request)
  - POST /wxaapi/broadcast/role/addrole

- async WechatResponse AddRoleRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastRoleSubscriptionApiDeleteRoleResponse DeleteRoleAsync(WechatWxOpenLiveBroadcastRoleSubscriptionApiDeleteRoleRequest request)
  - POST /wxaapi/broadcast/role/deleterole

- async WechatResponse DeleteRoleRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastRoleSubscriptionApiGetRoleListResponse GetRoleListAsync(WechatWxOpenLiveBroadcastRoleSubscriptionApiGetRoleListRequest request)
  - GET /wxaapi/broadcast/role/getrolelist

- async WechatResponse GetRoleListRawAsync(string query)

- async WechatWxOpenLiveBroadcastRoleSubscriptionApiPushLiveStartMessageResponse PushLiveStartMessageAsync(WechatWxOpenLiveBroadcastRoleSubscriptionApiPushLiveStartMessageRequest request)
  - POST /wxa/business/push_message

- async WechatResponse PushLiveStartMessageRawAsync(string query, string jsonBody)

- async WechatWxOpenLiveBroadcastRoleSubscriptionApiGetFollowersResponse GetFollowersAsync(WechatWxOpenLiveBroadcastRoleSubscriptionApiGetFollowersRequest request)
  - POST /wxa/business/get_wxa_followers

- async WechatResponse GetFollowersRawAsync(string query, string jsonBody)


## WechatWxOpenLiveBroadcastRoleSubscriptionApiAddRoleRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastRoleSubscriptionApiAddRoleRequest()

- WechatWxOpenLiveBroadcastRoleSubscriptionApiAddRoleRequest Username(string fieldValue)

- WechatWxOpenLiveBroadcastRoleSubscriptionApiAddRoleRequest Role(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastRoleSubscriptionApiAddRoleResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastRoleSubscriptionApiDeleteRoleRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastRoleSubscriptionApiDeleteRoleRequest()

- WechatWxOpenLiveBroadcastRoleSubscriptionApiDeleteRoleRequest Username(string fieldValue)

- WechatWxOpenLiveBroadcastRoleSubscriptionApiDeleteRoleRequest Role(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastRoleSubscriptionApiDeleteRoleResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenLiveBroadcastRoleSubscriptionApiGetFollowersRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastRoleSubscriptionApiGetFollowersRequest()

- WechatWxOpenLiveBroadcastRoleSubscriptionApiGetFollowersRequest Limit(int fieldValue)

- WechatWxOpenLiveBroadcastRoleSubscriptionApiGetFollowersRequest PageBreak(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastRoleSubscriptionApiGetFollowersResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastRoleSubscriptionApiGetRoleListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastRoleSubscriptionApiGetRoleListRequest()

- WechatWxOpenLiveBroadcastRoleSubscriptionApiGetRoleListRequest Offset(int fieldValue)

- WechatWxOpenLiveBroadcastRoleSubscriptionApiGetRoleListRequest Limit(int fieldValue)

- WechatWxOpenLiveBroadcastRoleSubscriptionApiGetRoleListRequest Keyword(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastRoleSubscriptionApiGetRoleListResponse (class)

- public string Raw;


## WechatWxOpenLiveBroadcastRoleSubscriptionApiPushLiveStartMessageRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenLiveBroadcastRoleSubscriptionApiPushLiveStartMessageRequest()

- WechatWxOpenLiveBroadcastRoleSubscriptionApiPushLiveStartMessageRequest RoomId(long fieldValue)

- WechatWxOpenLiveBroadcastRoleSubscriptionApiPushLiveStartMessageRequest UserOpenid(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenLiveBroadcastRoleSubscriptionApiPushLiveStartMessageResponse (class)

- public string Raw;


## WechatWxOpenMessageApi (class)

Message/MessageApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenMessageApi(WechatWxOpenClient client)

- async WechatWxOpenMessageApiSendSubscribeResponse SendSubscribeAsync(WechatWxOpenMessageApiSendSubscribeRequest request)
  - POST /cgi-bin/message/subscribe/send

- async WechatResponse SendSubscribeRawAsync(string query, string jsonBody)

- async WechatWxOpenMessageApiSendEmployeeRelationMessageResponse SendEmployeeRelationMessageAsync(WechatWxOpenMessageApiSendEmployeeRelationMessageRequest request)
  - POST /cgi-bin/message/wxopen/employeerelationmsg/send

- async WechatResponse SendEmployeeRelationMessageRawAsync(string query, string jsonBody)


## WechatWxOpenMessageApiSendEmployeeRelationMessageRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMessageApiSendEmployeeRelationMessageRequest()

- WechatWxOpenMessageApiSendEmployeeRelationMessageRequest ToUser(string fieldValue)

- WechatWxOpenMessageApiSendEmployeeRelationMessageRequest TemplateId(string fieldValue)

- WechatWxOpenMessageApiSendEmployeeRelationMessageRequest Page(string fieldValue)

- WechatWxOpenMessageApiSendEmployeeRelationMessageRequest Data(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMessageApiSendEmployeeRelationMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMessageApiSendSubscribeRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenMessageApiSendSubscribeRequest()

- WechatWxOpenMessageApiSendSubscribeRequest ToUser(string fieldValue)

- WechatWxOpenMessageApiSendSubscribeRequest TemplateId(string fieldValue)

- WechatWxOpenMessageApiSendSubscribeRequest Page(string fieldValue)

- WechatWxOpenMessageApiSendSubscribeRequest Data(JsonValue fieldValue)

- WechatWxOpenMessageApiSendSubscribeRequest MiniprogramState(string fieldValue)

- WechatWxOpenMessageApiSendSubscribeRequest Lang(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMessageApiSendSubscribeResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMessageServiceCardApi (class)

Message/ServiceCardApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenMessageServiceCardApi(WechatWxOpenClient client)

- async WechatWxOpenMessageServiceCardApiSetUserNotifyResponse SetUserNotifyAsync(WechatWxOpenMessageServiceCardApiSetUserNotifyRequest request)
  - POST /wxa/set_user_notify

- async WechatResponse SetUserNotifyRawAsync(string query, string jsonBody)

- async WechatWxOpenMessageServiceCardApiSetUserNotifyExtResponse SetUserNotifyExtAsync(WechatWxOpenMessageServiceCardApiSetUserNotifyExtRequest request)
  - POST /wxa/set_user_notifyext

- async WechatResponse SetUserNotifyExtRawAsync(string query, string jsonBody)

- async WechatWxOpenMessageServiceCardApiGetUserNotifyResponse GetUserNotifyAsync(WechatWxOpenMessageServiceCardApiGetUserNotifyRequest request)
  - POST /wxa/get_user_notify

- async WechatResponse GetUserNotifyRawAsync(string query, string jsonBody)


## WechatWxOpenMessageServiceCardApiGetUserNotifyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMessageServiceCardApiGetUserNotifyRequest()

- WechatWxOpenMessageServiceCardApiGetUserNotifyRequest OpenId(string fieldValue)

- WechatWxOpenMessageServiceCardApiGetUserNotifyRequest NotifyType(int fieldValue)

- WechatWxOpenMessageServiceCardApiGetUserNotifyRequest NotifyCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMessageServiceCardApiGetUserNotifyResponse (class)

- public string Raw;


## WechatWxOpenMessageServiceCardApiSetUserNotifyExtRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMessageServiceCardApiSetUserNotifyExtRequest()

- WechatWxOpenMessageServiceCardApiSetUserNotifyExtRequest OpenId(string fieldValue)

- WechatWxOpenMessageServiceCardApiSetUserNotifyExtRequest NotifyType(int fieldValue)

- WechatWxOpenMessageServiceCardApiSetUserNotifyExtRequest NotifyCode(string fieldValue)

- WechatWxOpenMessageServiceCardApiSetUserNotifyExtRequest ExtJson(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMessageServiceCardApiSetUserNotifyExtResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMessageServiceCardApiSetUserNotifyRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenMessageServiceCardApiSetUserNotifyRequest()

- WechatWxOpenMessageServiceCardApiSetUserNotifyRequest OpenId(string fieldValue)

- WechatWxOpenMessageServiceCardApiSetUserNotifyRequest NotifyType(int fieldValue)

- WechatWxOpenMessageServiceCardApiSetUserNotifyRequest NotifyCode(string fieldValue)

- WechatWxOpenMessageServiceCardApiSetUserNotifyRequest ContentJson(string fieldValue)

- WechatWxOpenMessageServiceCardApiSetUserNotifyRequest CheckJson(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMessageServiceCardApiSetUserNotifyResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMessageUpdatableMessageApi (class)

Message/UpdatableMessageApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenMessageUpdatableMessageApi(WechatWxOpenClient client)

- async WechatWxOpenMessageUpdatableMessageApiCreateActivityIdResponse CreateActivityIdAsync(WechatWxOpenMessageUpdatableMessageApiCreateActivityIdRequest request)
  - GET /cgi-bin/message/wxopen/activityid/create

- async WechatResponse CreateActivityIdRawAsync(string query)

- async WechatWxOpenMessageUpdatableMessageApiSetUpdatableMessageResponse SetUpdatableMessageAsync(WechatWxOpenMessageUpdatableMessageApiSetUpdatableMessageRequest request)
  - POST /cgi-bin/message/wxopen/updatablemsg/send

- async WechatResponse SetUpdatableMessageRawAsync(string query, string jsonBody)

- async WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageResponse SetChatToolMessageAsync(WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageRequest request)
  - POST /cgi-bin/message/wxopen/chattoolmsg/send

- async WechatResponse SetChatToolMessageRawAsync(string query, string jsonBody)


## WechatWxOpenMessageUpdatableMessageApiCreateActivityIdRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenMessageUpdatableMessageApiCreateActivityIdRequest()

- WechatWxOpenMessageUpdatableMessageApiCreateActivityIdRequest UnionId(string fieldValue)

- WechatWxOpenMessageUpdatableMessageApiCreateActivityIdRequest OpenId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMessageUpdatableMessageApiCreateActivityIdResponse (class)

- public string Raw;


## WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageRequest()

- WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageRequest ActivityId(string fieldValue)

- WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageRequest TargetState(int fieldValue)

- WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageRequest TemplateId(string fieldValue)

- WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageRequest VersionType(int fieldValue)

- WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageRequest ParticipatorInfoList(List<WechatWxOpenChatToolParticipatorInfo> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMessageUpdatableMessageApiSetChatToolMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMessageUpdatableMessageApiSetUpdatableMessageRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMessageUpdatableMessageApiSetUpdatableMessageRequest()

- WechatWxOpenMessageUpdatableMessageApiSetUpdatableMessageRequest ParameterList(List<WechatWxOpenUpdatableMessageParameter> fieldValue)

- WechatWxOpenMessageUpdatableMessageApiSetUpdatableMessageRequest ActivityId(string fieldValue)

- WechatWxOpenMessageUpdatableMessageApiSetUpdatableMessageRequest TargetState(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMessageUpdatableMessageApiSetUpdatableMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaApi (class)

MiniDrama/MiniDramaApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenMiniDramaApi(WechatWxOpenClient client)

- async WechatWxOpenMiniDramaApiPullUploadResponse PullUploadAsync(WechatWxOpenMiniDramaApiPullUploadRequest request)
  - POST /wxa/sec/vod/pullupload

- async WechatResponse PullUploadRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaApiGetTaskResponse GetTaskAsync(WechatWxOpenMiniDramaApiGetTaskRequest request)
  - POST /wxa/sec/vod/gettask

- async WechatResponse GetTaskRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaApiApplyUploadResponse ApplyUploadAsync(WechatWxOpenMiniDramaApiApplyUploadRequest request)
  - POST /wxa/sec/vod/applyupload

- async WechatResponse ApplyUploadRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaApiCommitUploadResponse CommitUploadAsync(WechatWxOpenMiniDramaApiCommitUploadRequest request)
  - POST /wxa/sec/vod/commitupload

- async WechatResponse CommitUploadRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaApiListMediaResponse ListMediaAsync(WechatWxOpenMiniDramaApiListMediaRequest request)
  - POST /wxa/sec/vod/listmedia

- async WechatResponse ListMediaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaApiGetMediaResponse GetMediaAsync(WechatWxOpenMiniDramaApiGetMediaRequest request)
  - POST /wxa/sec/vod/getmedia

- async WechatResponse GetMediaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaApiGetMediaLinkResponse GetMediaLinkAsync(WechatWxOpenMiniDramaApiGetMediaLinkRequest request)
  - POST /wxa/sec/vod/getmedialink

- async WechatResponse GetMediaLinkRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaApiDeleteMediaResponse DeleteMediaAsync(WechatWxOpenMiniDramaApiDeleteMediaRequest request)
  - POST /wxa/sec/vod/deletemedia

- async WechatResponse DeleteMediaRawAsync(string query, string jsonBody)


## WechatWxOpenMiniDramaApiApplyUploadRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaApiApplyUploadRequest()

- WechatWxOpenMiniDramaApiApplyUploadRequest MediaName(string fieldValue)

- WechatWxOpenMiniDramaApiApplyUploadRequest MediaType(string fieldValue)

- WechatWxOpenMiniDramaApiApplyUploadRequest CoverType(string fieldValue)

- WechatWxOpenMiniDramaApiApplyUploadRequest SourceContext(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaApiApplyUploadResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaApiCommitUploadRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaApiCommitUploadRequest()

- WechatWxOpenMiniDramaApiCommitUploadRequest UploadId(string fieldValue)

- WechatWxOpenMiniDramaApiCommitUploadRequest MediaPartInfos(List<WechatWxOpenMiniDramaPartInfo> fieldValue)

- WechatWxOpenMiniDramaApiCommitUploadRequest CoverPartInfos(List<WechatWxOpenMiniDramaPartInfo> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaApiCommitUploadResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaApiDeleteMediaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaApiDeleteMediaRequest()

- WechatWxOpenMiniDramaApiDeleteMediaRequest MediaId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaApiDeleteMediaResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaApiGetMediaLinkRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaApiGetMediaLinkRequest()

- WechatWxOpenMiniDramaApiGetMediaLinkRequest T(long fieldValue)

- WechatWxOpenMiniDramaApiGetMediaLinkRequest Us(string fieldValue)

- WechatWxOpenMiniDramaApiGetMediaLinkRequest Exper(int fieldValue)

- WechatWxOpenMiniDramaApiGetMediaLinkRequest Rlimit(int fieldValue)

- WechatWxOpenMiniDramaApiGetMediaLinkRequest Whref(string fieldValue)

- WechatWxOpenMiniDramaApiGetMediaLinkRequest Bkref(string fieldValue)

- WechatWxOpenMiniDramaApiGetMediaLinkRequest MediaId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaApiGetMediaLinkResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaApiGetMediaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaApiGetMediaRequest()

- WechatWxOpenMiniDramaApiGetMediaRequest MediaId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaApiGetMediaResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaApiGetTaskRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaApiGetTaskRequest()

- WechatWxOpenMiniDramaApiGetTaskRequest TaskId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaApiGetTaskResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaApiListMediaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaApiListMediaRequest()

- WechatWxOpenMiniDramaApiListMediaRequest DramaId(long fieldValue)

- WechatWxOpenMiniDramaApiListMediaRequest MediaName(string fieldValue)

- WechatWxOpenMiniDramaApiListMediaRequest MediaNameFuzzy(string fieldValue)

- WechatWxOpenMiniDramaApiListMediaRequest StartTime(long fieldValue)

- WechatWxOpenMiniDramaApiListMediaRequest EndTime(long fieldValue)

- WechatWxOpenMiniDramaApiListMediaRequest Limit(int fieldValue)

- WechatWxOpenMiniDramaApiListMediaRequest Offset(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaApiListMediaResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaApiPullUploadRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaApiPullUploadRequest()

- WechatWxOpenMiniDramaApiPullUploadRequest MediaName(string fieldValue)

- WechatWxOpenMiniDramaApiPullUploadRequest MediaUrl(string fieldValue)

- WechatWxOpenMiniDramaApiPullUploadRequest CoverUrl(string fieldValue)

- WechatWxOpenMiniDramaApiPullUploadRequest SourceContext(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaApiPullUploadResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuditApi (class)

MiniDrama/MiniDramaAuditApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenMiniDramaAuditApi(WechatWxOpenClient client)

- async WechatWxOpenMiniDramaAuditApiAuditDramaResponse AuditDramaAsync(WechatWxOpenMiniDramaAuditApiAuditDramaRequest request)
  - POST /wxa/sec/vod/auditdrama

- async WechatResponse AuditDramaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuditApiListDramasResponse ListDramasAsync(WechatWxOpenMiniDramaAuditApiListDramasRequest request)
  - POST /wxa/sec/vod/listdramas

- async WechatResponse ListDramasRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuditApiGetDramaResponse GetDramaAsync(WechatWxOpenMiniDramaAuditApiGetDramaRequest request)
  - POST /wxa/sec/vod/getdrama

- async WechatResponse GetDramaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuditApiSubmitReplaceDramaMediasResponse SubmitReplaceDramaMediasAsync(WechatWxOpenMiniDramaAuditApiSubmitReplaceDramaMediasRequest request)
  - POST /wxa/sec/vod/submitreplacedramamedias

- async WechatResponse SubmitReplaceDramaMediasRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuditApiReplaceDramaMediaResponse ReplaceDramaMediaAsync(WechatWxOpenMiniDramaAuditApiReplaceDramaMediaRequest request)
  - POST /wxa/sec/vod/replacedramamedia

- async WechatResponse ReplaceDramaMediaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoResponse ModifyDramaBasicInfoAsync(WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest request)
  - POST /wxa/sec/vod/modifydramabasicinfo

- async WechatResponse ModifyDramaBasicInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuditApiGetDramaLatestAuditInfoResponse GetDramaLatestAuditInfoAsync(WechatWxOpenMiniDramaAuditApiGetDramaLatestAuditInfoRequest request)
  - POST /wxa/sec/vod/getdramalatestauditinfo

- async WechatResponse GetDramaLatestAuditInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuditApiGetCdnUsageDataResponse GetCdnUsageDataAsync(WechatWxOpenMiniDramaAuditApiGetCdnUsageDataRequest request)
  - POST /wxa/sec/vod/getcdnusagedata

- async WechatResponse GetCdnUsageDataRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuditApiGetCdnLogsResponse GetCdnLogsAsync(WechatWxOpenMiniDramaAuditApiGetCdnLogsRequest request)
  - POST /wxa/sec/vod/getcdnlogs

- async WechatResponse GetCdnLogsRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuditApiListPackagesResponse ListPackagesAsync(WechatWxOpenMiniDramaAuditApiListPackagesRequest request)
  - POST /wxa/sec/vod/listpackages

- async WechatResponse ListPackagesRawAsync(string query, string jsonBody)


## WechatWxOpenMiniDramaAuditApiAuditDramaRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiAuditDramaRequest()

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest DramaId(long fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest Name(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest MediaCount(int fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest MediaIdList(List<long> fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest Description(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest Recommendations(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest CoverMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest PromotionPosterMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest Producer(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest AuthorizedMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest QualificationType(int fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest RegistrationNumber(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest QualificationCertificateMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest CostCommitmentLetterMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest CostOfProduction(int fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest Expedited(int fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest ActorList(WechatWxOpenMiniDramaActorList fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest OtherMaterialMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest ReplaceMediaList(List<WechatWxOpenMiniDramaReplaceMediaItem> fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest Copyright(WechatWxOpenMiniDramaCopyrightInfo fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest DramaType(int fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest ContentDeclared(int fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest AiSoftwareOwnershipProof(string fieldValue)

- WechatWxOpenMiniDramaAuditApiAuditDramaRequest OtherPlatformPublicationProof(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiAuditDramaResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuditApiGetCdnLogsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiGetCdnLogsRequest()

- WechatWxOpenMiniDramaAuditApiGetCdnLogsRequest StartTime(long fieldValue)

- WechatWxOpenMiniDramaAuditApiGetCdnLogsRequest EndTime(long fieldValue)

- WechatWxOpenMiniDramaAuditApiGetCdnLogsRequest Limit(int fieldValue)

- WechatWxOpenMiniDramaAuditApiGetCdnLogsRequest Offset(int fieldValue)

- WechatWxOpenMiniDramaAuditApiGetCdnLogsRequest QueryType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiGetCdnLogsResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuditApiGetCdnUsageDataRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiGetCdnUsageDataRequest()

- WechatWxOpenMiniDramaAuditApiGetCdnUsageDataRequest StartTime(long fieldValue)

- WechatWxOpenMiniDramaAuditApiGetCdnUsageDataRequest EndTime(long fieldValue)

- WechatWxOpenMiniDramaAuditApiGetCdnUsageDataRequest DataInterval(string fieldValue)

- WechatWxOpenMiniDramaAuditApiGetCdnUsageDataRequest QueryType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiGetCdnUsageDataResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuditApiGetDramaLatestAuditInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiGetDramaLatestAuditInfoRequest()

- WechatWxOpenMiniDramaAuditApiGetDramaLatestAuditInfoRequest AuditType(int fieldValue)

- WechatWxOpenMiniDramaAuditApiGetDramaLatestAuditInfoRequest DramaId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiGetDramaLatestAuditInfoResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuditApiGetDramaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiGetDramaRequest()

- WechatWxOpenMiniDramaAuditApiGetDramaRequest DramaId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiGetDramaResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuditApiListDramasRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiListDramasRequest()

- WechatWxOpenMiniDramaAuditApiListDramasRequest Offset(int fieldValue)

- WechatWxOpenMiniDramaAuditApiListDramasRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiListDramasResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuditApiListPackagesRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiListPackagesRequest()

- WechatWxOpenMiniDramaAuditApiListPackagesRequest Status(int fieldValue)

- WechatWxOpenMiniDramaAuditApiListPackagesRequest Offset(int fieldValue)

- WechatWxOpenMiniDramaAuditApiListPackagesRequest Limit(int fieldValue)

- WechatWxOpenMiniDramaAuditApiListPackagesRequest QueryType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiListPackagesResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest()

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest Description(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest CoverMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest Recommendations(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest PromotionPosterMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest AlternateName(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest ActorList(WechatWxOpenMiniDramaActorList fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest QualificationType(int fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest RegistrationNumber(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest QualificationCertificateMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest CostOfProduction(int fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest CostCommitmentLetterMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest OtherMaterialMaterialId(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest Producer(string fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest Copyright(WechatWxOpenMiniDramaCopyrightInfo fieldValue)

- WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoRequest DramaId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiModifyDramaBasicInfoResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaAuditApiReplaceDramaMediaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiReplaceDramaMediaRequest()

- WechatWxOpenMiniDramaAuditApiReplaceDramaMediaRequest OldMediaId(long fieldValue)

- WechatWxOpenMiniDramaAuditApiReplaceDramaMediaRequest NewMediaId(long fieldValue)

- WechatWxOpenMiniDramaAuditApiReplaceDramaMediaRequest DramaId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiReplaceDramaMediaResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaAuditApiSubmitReplaceDramaMediasRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuditApiSubmitReplaceDramaMediasRequest()

- WechatWxOpenMiniDramaAuditApiSubmitReplaceDramaMediasRequest ReplaceMediaList(List<WechatWxOpenMiniDramaReplaceMediaItem> fieldValue)

- WechatWxOpenMiniDramaAuditApiSubmitReplaceDramaMediasRequest DramaId(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuditApiSubmitReplaceDramaMediasResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaAuthorizationApi (class)

MiniDrama/MiniDramaAuthorizationApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenMiniDramaAuthorizationApi(WechatWxOpenClient client)

- async WechatWxOpenMiniDramaAuthorizationApiGetAuthorizedObjectsResponse GetAuthorizedObjectsAsync(WechatWxOpenMiniDramaAuthorizationApiGetAuthorizedObjectsRequest request)
  - POST /wxa/sec/vod/getauthorizedobjects

- async WechatResponse GetAuthorizedObjectsRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiAuthorizeDramaResponse AuthorizeDramaAsync(WechatWxOpenMiniDramaAuthorizationApiAuthorizeDramaRequest request)
  - POST /wxa/sec/vod/authorizedrama

- async WechatResponse AuthorizeDramaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiDeauthorizeDramaResponse DeauthorizeDramaAsync(WechatWxOpenMiniDramaAuthorizationApiDeauthorizeDramaRequest request)
  - POST /wxa/sec/vod/deauthorizedrama

- async WechatResponse DeauthorizeDramaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeObjectsResponse GetAuthorizeObjectsAsync(WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeObjectsRequest request)
  - POST /wxa/sec/vod/getauthorizeobjects

- async WechatResponse GetAuthorizeObjectsRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiAuthorizeAppResponse AuthorizeAppAsync(WechatWxOpenMiniDramaAuthorizationApiAuthorizeAppRequest request)
  - POST /wxa/sec/vod/authorizeapp

- async WechatResponse AuthorizeAppRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiDeauthorizeAppResponse DeauthorizeAppAsync(WechatWxOpenMiniDramaAuthorizationApiDeauthorizeAppRequest request)
  - POST /wxa/sec/vod/deauthorizeapp

- async WechatResponse DeauthorizeAppRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeAppsResponse GetAuthorizeAppsAsync()
  - POST /wxa/sec/vod/getauthorizeapps

- async WechatResponse GetAuthorizeAppsRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightResponse AuthorizeCopyrightAsync(WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightRequest request)
  - POST /wxa/sec/vod/authorizecopyright

- async WechatResponse AuthorizeCopyrightRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiDeauthorizeCopyrightResponse DeauthorizeCopyrightAsync(WechatWxOpenMiniDramaAuthorizationApiDeauthorizeCopyrightRequest request)
  - POST /wxa/sec/vod/deauthorizecopyright

- async WechatResponse DeauthorizeCopyrightRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListResponse GetCopyrightAuthorizationListAsync(WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListRequest request)
  - POST /wxa/sec/vod/getcopyrightauthorizationlist

- async WechatResponse GetCopyrightAuthorizationListRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizedListResponse GetCopyrightAuthorizedListAsync(WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizedListRequest request)
  - POST /wxa/sec/vod/getcopyrightauthorizedlist

- async WechatResponse GetCopyrightAuthorizedListRawAsync(string query, string jsonBody)


## WechatWxOpenMiniDramaAuthorizationApiAuthorizeAppRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiAuthorizeAppRequest()

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeAppRequest AuthorizedAppid(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeAppRequest AuthzExpireTime(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiAuthorizeAppResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightRequest()

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightRequest AuthorizationType(int fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightRequest AuthorizedAppid(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightRequest AuthorizedSubjectCertNo(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightRequest DramaIds(List<long> fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightRequest ExpireTime(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiAuthorizeCopyrightResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuthorizationApiAuthorizeDramaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiAuthorizeDramaRequest()

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeDramaRequest DramaId(List<long> fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeDramaRequest AuthorizedAppid(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiAuthorizeDramaRequest AuthzExpireTime(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiAuthorizeDramaResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuthorizationApiDeauthorizeAppRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiDeauthorizeAppRequest()

- WechatWxOpenMiniDramaAuthorizationApiDeauthorizeAppRequest AuthorizedAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiDeauthorizeAppResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaAuthorizationApiDeauthorizeCopyrightRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiDeauthorizeCopyrightRequest()

- WechatWxOpenMiniDramaAuthorizationApiDeauthorizeCopyrightRequest AuthorizationType(int fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiDeauthorizeCopyrightRequest AuthorizedAppid(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiDeauthorizeCopyrightRequest AuthorizedSubjectCertNo(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiDeauthorizeCopyrightRequest DramaIds(List<long> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiDeauthorizeCopyrightResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuthorizationApiDeauthorizeDramaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiDeauthorizeDramaRequest()

- WechatWxOpenMiniDramaAuthorizationApiDeauthorizeDramaRequest DramaId(List<long> fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiDeauthorizeDramaRequest AuthorizedAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiDeauthorizeDramaResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeAppsResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeObjectsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeObjectsRequest()

- WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeObjectsRequest DramaId(long fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeObjectsRequest AuthorizedAppid(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeObjectsRequest Offset(int fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeObjectsRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiGetAuthorizeObjectsResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuthorizationApiGetAuthorizedObjectsRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiGetAuthorizedObjectsRequest()

- WechatWxOpenMiniDramaAuthorizationApiGetAuthorizedObjectsRequest AuthorizerAppid(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetAuthorizedObjectsRequest Offset(int fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetAuthorizedObjectsRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiGetAuthorizedObjectsResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListRequest()

- WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListRequest AuthorizationType(int fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListRequest AuthorizedAppid(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListRequest AuthorizedSubjectCertNo(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListRequest DramaId(long fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListRequest Offset(int fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizationListResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizedListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizedListRequest()

- WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizedListRequest AuthorizerAppid(string fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizedListRequest Offset(int fieldValue)

- WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizedListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaAuthorizationApiGetCopyrightAuthorizedListResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaPlayerApi (class)

MiniDrama/MiniDramaPlayerApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenMiniDramaPlayerApi(WechatWxOpenClient client)

- async WechatWxOpenMiniDramaPlayerApiSetPlayerDramaRecommendedSwitchResponse SetPlayerDramaRecommendedSwitchAsync(WechatWxOpenMiniDramaPlayerApiSetPlayerDramaRecommendedSwitchRequest request)
  - POST /wxadrama/setplayerdramarecmdswitch

- async WechatResponse SetPlayerDramaRecommendedSwitchRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaPlayerApiSetFlushDramaResponse SetFlushDramaAsync(WechatWxOpenMiniDramaPlayerApiSetFlushDramaRequest request)
  - POST /wxadrama/developersetflushdrama

- async WechatResponse SetFlushDramaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaPlayerApiSetRecommendedDramaResponse SetRecommendedDramaAsync(WechatWxOpenMiniDramaPlayerApiSetRecommendedDramaRequest request)
  - POST /wxadrama/developersetrecmddrama

- async WechatResponse SetRecommendedDramaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaPlayerApiPublishDramaResponse PublishDramaAsync(WechatWxOpenMiniDramaPlayerApiPublishDramaRequest request)
  - POST /wxadrama/developerpublishdrama

- async WechatResponse PublishDramaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaPlayerApiGetPublishedDramaResponse GetPublishedDramaAsync()
  - POST /wxadrama/developergetpublisheddrama

- async WechatResponse GetPublishedDramaRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaPlayerApiSetMonetizationResponse SetMonetizationAsync(WechatWxOpenMiniDramaPlayerApiSetMonetizationRequest request)
  - POST /wxadrama/developersetiaadrama

- async WechatResponse SetMonetizationRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaPlayerApiGetMonetizationResponse GetMonetizationAsync(WechatWxOpenMiniDramaPlayerApiGetMonetizationRequest request)
  - POST /wxadrama/developergetiaadrama

- async WechatResponse GetMonetizationRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaPlayerApiBatchProcessPromotionResponse BatchProcessPromotionAsync(WechatWxOpenMiniDramaPlayerApiBatchProcessPromotionRequest request)
  - POST /wxadrama/batchprocessdramapromotion

- async WechatResponse BatchProcessPromotionRawAsync(string query, string jsonBody)

- async WechatWxOpenMiniDramaPlayerApiGetFinderEventResponse GetFinderEventAsync(WechatWxOpenMiniDramaPlayerApiGetFinderEventRequest request)
  - POST /wxadrama/getfinderevent

- async WechatResponse GetFinderEventRawAsync(string query, string jsonBody)


## WechatWxOpenMiniDramaPlayerApiBatchProcessPromotionRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaPlayerApiBatchProcessPromotionRequest()

- WechatWxOpenMiniDramaPlayerApiBatchProcessPromotionRequest ActionType(int fieldValue)

- WechatWxOpenMiniDramaPlayerApiBatchProcessPromotionRequest List(List<WechatWxOpenMiniDramaPlayerDramaIdentity> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaPlayerApiBatchProcessPromotionResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaPlayerApiGetFinderEventRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaPlayerApiGetFinderEventRequest()

- WechatWxOpenMiniDramaPlayerApiGetFinderEventRequest EventIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaPlayerApiGetFinderEventResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaPlayerApiGetMonetizationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaPlayerApiGetMonetizationRequest()

- WechatWxOpenMiniDramaPlayerApiGetMonetizationRequest List(List<WechatWxOpenMiniDramaPlayerDramaIdentity> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaPlayerApiGetMonetizationResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaPlayerApiGetPublishedDramaResponse (class)

- public string Raw;


## WechatWxOpenMiniDramaPlayerApiPublishDramaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaPlayerApiPublishDramaRequest()

- WechatWxOpenMiniDramaPlayerApiPublishDramaRequest List(List<WechatWxOpenMiniDramaPublishDramaItem> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaPlayerApiPublishDramaResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaPlayerApiSetFlushDramaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaPlayerApiSetFlushDramaRequest()

- WechatWxOpenMiniDramaPlayerApiSetFlushDramaRequest List(List<WechatWxOpenMiniDramaFlushDramaItem> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaPlayerApiSetFlushDramaResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaPlayerApiSetMonetizationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaPlayerApiSetMonetizationRequest()

- WechatWxOpenMiniDramaPlayerApiSetMonetizationRequest List(List<WechatWxOpenMiniDramaMonetizationItem> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaPlayerApiSetMonetizationResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaPlayerApiSetPlayerDramaRecommendedSwitchRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaPlayerApiSetPlayerDramaRecommendedSwitchRequest()

- WechatWxOpenMiniDramaPlayerApiSetPlayerDramaRecommendedSwitchRequest EntryType(int fieldValue)

- WechatWxOpenMiniDramaPlayerApiSetPlayerDramaRecommendedSwitchRequest SwitchStatus(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaPlayerApiSetPlayerDramaRecommendedSwitchResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenMiniDramaPlayerApiSetRecommendedDramaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenMiniDramaPlayerApiSetRecommendedDramaRequest()

- WechatWxOpenMiniDramaPlayerApiSetRecommendedDramaRequest EntryType(int fieldValue)

- WechatWxOpenMiniDramaPlayerApiSetRecommendedDramaRequest List(List<WechatWxOpenMiniDramaPlayerDramaIdentity> fieldValue)

- WechatWxOpenMiniDramaPlayerApiSetRecommendedDramaRequest SrcAppid(string fieldValue)

- WechatWxOpenMiniDramaPlayerApiSetRecommendedDramaRequest DramaId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenMiniDramaPlayerApiSetRecommendedDramaResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelApi (class)

Novel/NovelApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenNovelApi(WechatWxOpenClient client)

- async WechatWxOpenNovelApiCreateBookResponse CreateBookAsync(WechatWxOpenNovelApiCreateBookRequest request)
  - POST /wxa/book/createbook

- async WechatResponse CreateBookRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiUpdateBookResponse UpdateBookAsync(WechatWxOpenNovelApiUpdateBookRequest request)
  - POST /wxa/book/updatebook

- async WechatResponse UpdateBookRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiDeleteBookResponse DeleteBookAsync(WechatWxOpenNovelApiDeleteBookRequest request)
  - POST /wxa/book/deletebook

- async WechatResponse DeleteBookRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiListBooksResponse ListBooksAsync(WechatWxOpenNovelApiListBooksRequest request)
  - POST /wxa/book/listbook

- async WechatResponse ListBooksRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiGetBookResponse GetBookAsync(WechatWxOpenNovelApiGetBookRequest request)
  - POST /wxa/book/getbook

- async WechatResponse GetBookRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiCreateChapterResponse CreateChapterAsync(WechatWxOpenNovelApiCreateChapterRequest request)
  - POST /wxa/book/createchapter

- async WechatResponse CreateChapterRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiBatchCreateChaptersResponse BatchCreateChaptersAsync(WechatWxOpenNovelApiBatchCreateChaptersRequest request)
  - POST /wxa/book/batchcreatechapter

- async WechatResponse BatchCreateChaptersRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiDeleteChapterResponse DeleteChapterAsync(WechatWxOpenNovelApiDeleteChapterRequest request)
  - POST /wxa/book/deletechapter

- async WechatResponse DeleteChapterRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiReplaceChapterResponse ReplaceChapterAsync(WechatWxOpenNovelApiReplaceChapterRequest request)
  - POST /wxa/book/replacechapter

- async WechatResponse ReplaceChapterRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiListChaptersResponse ListChaptersAsync(WechatWxOpenNovelApiListChaptersRequest request)
  - POST /wxa/book/listchapter

- async WechatResponse ListChaptersRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiGetChapterResponse GetChapterAsync(WechatWxOpenNovelApiGetChapterRequest request)
  - POST /wxa/book/getchapter

- async WechatResponse GetChapterRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiReorderChapterResponse ReorderChapterAsync(WechatWxOpenNovelApiReorderChapterRequest request)
  - POST /wxa/book/reorderchapter

- async WechatResponse ReorderChapterRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiUpdateChapterSequenceResponse UpdateChapterSequenceAsync(WechatWxOpenNovelApiUpdateChapterSequenceRequest request)
  - POST /wxa/book/updatechapterseq

- async WechatResponse UpdateChapterSequenceRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelApiAuditBookResponse AuditBookAsync(WechatWxOpenNovelApiAuditBookRequest request)
  - POST /wxa/book/auditbook

- async WechatResponse AuditBookRawAsync(string query, string jsonBody)


## WechatWxOpenNovelApiAuditBookRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiAuditBookRequest()

- WechatWxOpenNovelApiAuditBookRequest BookId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiAuditBookResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelApiBatchCreateChaptersRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiBatchCreateChaptersRequest()

- WechatWxOpenNovelApiBatchCreateChaptersRequest BookId(string fieldValue)

- WechatWxOpenNovelApiBatchCreateChaptersRequest ChapterList(List<WechatWxOpenNovelChapterInput> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiBatchCreateChaptersResponse (class)

- public string Raw;


## WechatWxOpenNovelApiCreateBookRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenNovelApiCreateBookRequest()

- WechatWxOpenNovelApiCreateBookRequest Title(string fieldValue)

- WechatWxOpenNovelApiCreateBookRequest Intro(string fieldValue)

- WechatWxOpenNovelApiCreateBookRequest CoverMediaId(string fieldValue)

- WechatWxOpenNovelApiCreateBookRequest Author(string fieldValue)

- WechatWxOpenNovelApiCreateBookRequest FirstCategoryId(int fieldValue)

- WechatWxOpenNovelApiCreateBookRequest SecondCategoryId(int fieldValue)

- WechatWxOpenNovelApiCreateBookRequest ThirdCategoryId(int fieldValue)

- WechatWxOpenNovelApiCreateBookRequest CompleteStatus(int fieldValue)

- WechatWxOpenNovelApiCreateBookRequest OriginalId(string fieldValue)

- WechatWxOpenNovelApiCreateBookRequest ChapterOrderMethod(int fieldValue)

- WechatWxOpenNovelApiCreateBookRequest CustomInfo(string fieldValue)

- WechatWxOpenNovelApiCreateBookRequest KeywordList(List<string> fieldValue)

- WechatWxOpenNovelApiCreateBookRequest AwesomeParagraph(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiCreateBookResponse (class)

- public string Raw;


## WechatWxOpenNovelApiCreateChapterRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiCreateChapterRequest()

- WechatWxOpenNovelApiCreateChapterRequest BookId(string fieldValue)

- WechatWxOpenNovelApiCreateChapterRequest Chapter(WechatWxOpenNovelChapterInput fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiCreateChapterResponse (class)

- public string Raw;


## WechatWxOpenNovelApiDeleteBookRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiDeleteBookRequest()

- WechatWxOpenNovelApiDeleteBookRequest BookId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiDeleteBookResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelApiDeleteChapterRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiDeleteChapterRequest()

- WechatWxOpenNovelApiDeleteChapterRequest BookId(string fieldValue)

- WechatWxOpenNovelApiDeleteChapterRequest ChapterId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiDeleteChapterResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelApiGetBookRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiGetBookRequest()

- WechatWxOpenNovelApiGetBookRequest BookId(string fieldValue)

- WechatWxOpenNovelApiGetBookRequest NeedEditedData(bool fieldValue)

- WechatWxOpenNovelApiGetBookRequest OriginalId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiGetBookResponse (class)

- public string Raw;


## WechatWxOpenNovelApiGetChapterRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiGetChapterRequest()

- WechatWxOpenNovelApiGetChapterRequest BookId(string fieldValue)

- WechatWxOpenNovelApiGetChapterRequest ChapterId(string fieldValue)

- WechatWxOpenNovelApiGetChapterRequest NeedEditedData(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiGetChapterResponse (class)

- public string Raw;


## WechatWxOpenNovelApiListBooksRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiListBooksRequest()

- WechatWxOpenNovelApiListBooksRequest Limit(int fieldValue)

- WechatWxOpenNovelApiListBooksRequest Offset(int fieldValue)

- WechatWxOpenNovelApiListBooksRequest LastId(long fieldValue)

- WechatWxOpenNovelApiListBooksRequest NeedEditedData(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiListBooksResponse (class)

- public string Raw;


## WechatWxOpenNovelApiListChaptersRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiListChaptersRequest()

- WechatWxOpenNovelApiListChaptersRequest BookId(string fieldValue)

- WechatWxOpenNovelApiListChaptersRequest NeedEditedData(bool fieldValue)

- WechatWxOpenNovelApiListChaptersRequest Limit(int fieldValue)

- WechatWxOpenNovelApiListChaptersRequest Offset(int fieldValue)

- WechatWxOpenNovelApiListChaptersRequest VolumeIndex(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiListChaptersResponse (class)

- public string Raw;


## WechatWxOpenNovelApiReorderChapterRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiReorderChapterRequest()

- WechatWxOpenNovelApiReorderChapterRequest BookId(string fieldValue)

- WechatWxOpenNovelApiReorderChapterRequest ChapterId(string fieldValue)

- WechatWxOpenNovelApiReorderChapterRequest TargetChapterId(string fieldValue)

- WechatWxOpenNovelApiReorderChapterRequest Operation(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiReorderChapterResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelApiReplaceChapterRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiReplaceChapterRequest()

- WechatWxOpenNovelApiReplaceChapterRequest BookId(string fieldValue)

- WechatWxOpenNovelApiReplaceChapterRequest ChapterId(string fieldValue)

- WechatWxOpenNovelApiReplaceChapterRequest NewChapterTitle(string fieldValue)

- WechatWxOpenNovelApiReplaceChapterRequest NewContent(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiReplaceChapterResponse (class)

- public string Raw;


## WechatWxOpenNovelApiUpdateBookRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiUpdateBookRequest()

- WechatWxOpenNovelApiUpdateBookRequest BookId(string fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest Title(string fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest Intro(string fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest CoverMediaId(string fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest Author(string fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest FirstCategoryId(int fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest SecondCategoryId(int fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest ThirdCategoryId(int fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest CompleteStatus(int fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest ChapterIdList(List<string> fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest NeedVolume(bool fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest VolumeList(List<WechatWxOpenNovelVolumeInfo> fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest ChapterOrderMethod(int fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest CustomInfo(string fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest UpdateKeyword(bool fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest KeywordList(List<string> fieldValue)

- WechatWxOpenNovelApiUpdateBookRequest AwesomeParagraph(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiUpdateBookResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelApiUpdateChapterSequenceRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelApiUpdateChapterSequenceRequest()

- WechatWxOpenNovelApiUpdateChapterSequenceRequest BookId(string fieldValue)

- WechatWxOpenNovelApiUpdateChapterSequenceRequest ChapterSeqList(List<WechatWxOpenNovelChapterSequence> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelApiUpdateChapterSequenceResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelAuthorizationApi (class)

Novel/NovelAuthorizationApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenNovelAuthorizationApi(WechatWxOpenClient client)

- async WechatWxOpenNovelAuthorizationApiAddBookAuthorizationResponse AddBookAuthorizationAsync(WechatWxOpenNovelAuthorizationApiAddBookAuthorizationRequest request)
  - POST /wxa/book/addbookauth

- async WechatResponse AddBookAuthorizationRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationResponse QueryBookAuthorizationAsync(WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationRequest request)
  - POST /wxa/book/querybookauth

- async WechatResponse QueryBookAuthorizationRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelAuthorizationApiDeleteBookAuthorizationResponse DeleteBookAuthorizationAsync(WechatWxOpenNovelAuthorizationApiDeleteBookAuthorizationRequest request)
  - POST /wxa/book/delbookauth

- async WechatResponse DeleteBookAuthorizationRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelAuthorizationApiAddAppAuthorizationResponse AddAppAuthorizationAsync(WechatWxOpenNovelAuthorizationApiAddAppAuthorizationRequest request)
  - POST /wxa/book/addbookauthbyappid

- async WechatResponse AddAppAuthorizationRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationResponse QueryAppAuthorizationAsync(WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationRequest request)
  - POST /wxa/book/querybookauthv2

- async WechatResponse QueryAppAuthorizationRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelAuthorizationApiDeleteAppAuthorizationResponse DeleteAppAuthorizationAsync(WechatWxOpenNovelAuthorizationApiDeleteAppAuthorizationRequest request)
  - POST /wxa/book/delbookauthbyappid

- async WechatResponse DeleteAppAuthorizationRawAsync(string query, string jsonBody)


## WechatWxOpenNovelAuthorizationApiAddAppAuthorizationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelAuthorizationApiAddAppAuthorizationRequest()

- WechatWxOpenNovelAuthorizationApiAddAppAuthorizationRequest Infos(List<WechatWxOpenNovelAppAuthorizationInput> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelAuthorizationApiAddAppAuthorizationResponse (class)

- public string Raw;


## WechatWxOpenNovelAuthorizationApiAddBookAuthorizationRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenNovelAuthorizationApiAddBookAuthorizationRequest()

- WechatWxOpenNovelAuthorizationApiAddBookAuthorizationRequest Books(List<WechatWxOpenNovelBookAuthorizationInput> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelAuthorizationApiAddBookAuthorizationResponse (class)

- public string Raw;


## WechatWxOpenNovelAuthorizationApiDeleteAppAuthorizationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelAuthorizationApiDeleteAppAuthorizationRequest()

- WechatWxOpenNovelAuthorizationApiDeleteAppAuthorizationRequest GranteeAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelAuthorizationApiDeleteAppAuthorizationResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelAuthorizationApiDeleteBookAuthorizationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelAuthorizationApiDeleteBookAuthorizationRequest()

- WechatWxOpenNovelAuthorizationApiDeleteBookAuthorizationRequest BookId(string fieldValue)

- WechatWxOpenNovelAuthorizationApiDeleteBookAuthorizationRequest GranteeAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelAuthorizationApiDeleteBookAuthorizationResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationRequest()

- WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationRequest Type(int fieldValue)

- WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationRequest Count(int fieldValue)

- WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationRequest Cursor(string fieldValue)

- WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationRequest GrantorAppid(string fieldValue)

- WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationRequest BookIds(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelAuthorizationApiQueryAppAuthorizationResponse (class)

- public string Raw;


## WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationRequest()

- WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationRequest Type(int fieldValue)

- WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationRequest Offset(int fieldValue)

- WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationRequest Count(int fieldValue)

- WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationRequest IsSum(bool fieldValue)

- WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationRequest BookId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelAuthorizationApiQueryBookAuthorizationResponse (class)

- public string Raw;


## WechatWxOpenNovelReaderApi (class)

Novel/NovelReaderApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenNovelReaderApi(WechatWxOpenClient client)

- async WechatWxOpenNovelReaderApiSetPreviewSettingResponse SetPreviewSettingAsync(WechatWxOpenNovelReaderApiSetPreviewSettingRequest request)
  - POST /wxa/business/novelreader/setpreviewsetting

- async WechatResponse SetPreviewSettingRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelReaderApiGetPreviewSettingResponse GetPreviewSettingAsync(WechatWxOpenNovelReaderApiGetPreviewSettingRequest request)
  - POST /wxa/business/novelreader/getpreviewsetting

- async WechatResponse GetPreviewSettingRawAsync(string query, string jsonBody)

- async WechatWxOpenNovelReaderApiSetRecommendedNovelsResponse SetRecommendedNovelsAsync(WechatWxOpenNovelReaderApiSetRecommendedNovelsRequest request)
  - POST /wxa/business/novelreader/setrecmdnovel

- async WechatResponse SetRecommendedNovelsRawAsync(string query, string jsonBody)


## WechatWxOpenNovelReaderApiGetPreviewSettingRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelReaderApiGetPreviewSettingRequest()

- WechatWxOpenNovelReaderApiGetPreviewSettingRequest BookId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelReaderApiGetPreviewSettingResponse (class)

- public string Raw;


## WechatWxOpenNovelReaderApiSetPreviewSettingRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenNovelReaderApiSetPreviewSettingRequest()

- WechatWxOpenNovelReaderApiSetPreviewSettingRequest Setting(WechatWxOpenNovelPreviewSetting fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelReaderApiSetPreviewSettingResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenNovelReaderApiSetRecommendedNovelsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenNovelReaderApiSetRecommendedNovelsRequest()

- WechatWxOpenNovelReaderApiSetRecommendedNovelsRequest RecmdType(int fieldValue)

- WechatWxOpenNovelReaderApiSetRecommendedNovelsRequest BookIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenNovelReaderApiSetRecommendedNovelsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenOperationApi (class)

Operation/OperationApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenOperationApi(WechatWxOpenClient client)

- async WechatWxOpenOperationApiGetDomainInfoResponse GetDomainInfoAsync(WechatWxOpenOperationApiGetDomainInfoRequest request)
  - POST /wxa/getwxadevinfo

- async WechatResponse GetDomainInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenOperationApiGetPerformanceResponse GetPerformanceAsync(WechatWxOpenOperationApiGetPerformanceRequest request)
  - POST /wxaapi/log/get_performance

- async WechatResponse GetPerformanceRawAsync(string query, string jsonBody)

- async WechatWxOpenOperationApiGetSceneListResponse GetSceneListAsync()
  - GET /wxaapi/log/get_scene

- async WechatResponse GetSceneListRawAsync(string query)

- async WechatWxOpenOperationApiGetVersionListResponse GetVersionListAsync()
  - GET /wxaapi/log/get_client_version

- async WechatResponse GetVersionListRawAsync(string query)

- async WechatWxOpenOperationApiRealTimeLogSearchResponse RealTimeLogSearchAsync(WechatWxOpenOperationApiRealTimeLogSearchRequest request)
  - GET /wxaapi/userlog/userlog_search

- async WechatResponse RealTimeLogSearchRawAsync(string query)

- async WechatWxOpenOperationApiGetFeedbackResponse GetFeedbackAsync(WechatWxOpenOperationApiGetFeedbackRequest request)
  - GET /wxaapi/feedback/list

- async WechatResponse GetFeedbackRawAsync(string query)

- async WechatWxOpenOperationApiGetJsErrDetailResponse GetJsErrDetailAsync(WechatWxOpenOperationApiGetJsErrDetailRequest request)
  - POST /wxaapi/log/jserr_detail

- async WechatResponse GetJsErrDetailRawAsync(string query, string jsonBody)

- async WechatWxOpenOperationApiGetJsErrListResponse GetJsErrListAsync(WechatWxOpenOperationApiGetJsErrListRequest request)
  - POST /wxaapi/log/jserr_list

- async WechatResponse GetJsErrListRawAsync(string query, string jsonBody)


## WechatWxOpenOperationApiGetDomainInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenOperationApiGetDomainInfoRequest()

- WechatWxOpenOperationApiGetDomainInfoRequest Action(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenOperationApiGetDomainInfoResponse (class)

- public string Raw;


## WechatWxOpenOperationApiGetFeedbackRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenOperationApiGetFeedbackRequest()

- WechatWxOpenOperationApiGetFeedbackRequest Page(int fieldValue)

- WechatWxOpenOperationApiGetFeedbackRequest Num(int fieldValue)

- WechatWxOpenOperationApiGetFeedbackRequest Type(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenOperationApiGetFeedbackResponse (class)

- public string Raw;


## WechatWxOpenOperationApiGetJsErrDetailRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenOperationApiGetJsErrDetailRequest()

- WechatWxOpenOperationApiGetJsErrDetailRequest StartTime(string fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest EndTime(string fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest ErrorMsgMd5(string fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest ErrorStackMd5(string fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest AppVersion(string fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest SdkVersion(string fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest OsName(string fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest ClientVersion(string fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest Openid(string fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest Offset(int fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest Limit(int fieldValue)

- WechatWxOpenOperationApiGetJsErrDetailRequest Desc(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenOperationApiGetJsErrDetailResponse (class)

- public string Raw;


## WechatWxOpenOperationApiGetJsErrListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenOperationApiGetJsErrListRequest()

- WechatWxOpenOperationApiGetJsErrListRequest AppVersion(string fieldValue)

- WechatWxOpenOperationApiGetJsErrListRequest ErrType(string fieldValue)

- WechatWxOpenOperationApiGetJsErrListRequest StartTime(string fieldValue)

- WechatWxOpenOperationApiGetJsErrListRequest EndTime(string fieldValue)

- WechatWxOpenOperationApiGetJsErrListRequest Keyword(string fieldValue)

- WechatWxOpenOperationApiGetJsErrListRequest Openid(string fieldValue)

- WechatWxOpenOperationApiGetJsErrListRequest Orderby(string fieldValue)

- WechatWxOpenOperationApiGetJsErrListRequest Desc(string fieldValue)

- WechatWxOpenOperationApiGetJsErrListRequest Offset(int fieldValue)

- WechatWxOpenOperationApiGetJsErrListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenOperationApiGetJsErrListResponse (class)

- public string Raw;


## WechatWxOpenOperationApiGetPerformanceRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenOperationApiGetPerformanceRequest()

- WechatWxOpenOperationApiGetPerformanceRequest CostTimeType(int fieldValue)

- WechatWxOpenOperationApiGetPerformanceRequest DefaultStartTime(long fieldValue)

- WechatWxOpenOperationApiGetPerformanceRequest DefaultEndTime(long fieldValue)

- WechatWxOpenOperationApiGetPerformanceRequest Device(string fieldValue)

- WechatWxOpenOperationApiGetPerformanceRequest IsDownloadCode(string fieldValue)

- WechatWxOpenOperationApiGetPerformanceRequest Scene(string fieldValue)

- WechatWxOpenOperationApiGetPerformanceRequest NetworkType(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenOperationApiGetPerformanceResponse (class)

- public string Raw;


## WechatWxOpenOperationApiGetSceneListResponse (class)

- public string Raw;


## WechatWxOpenOperationApiGetVersionListResponse (class)

- public string Raw;


## WechatWxOpenOperationApiRealTimeLogSearchRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenOperationApiRealTimeLogSearchRequest()

- WechatWxOpenOperationApiRealTimeLogSearchRequest Date(string fieldValue)

- WechatWxOpenOperationApiRealTimeLogSearchRequest BeginTime(long fieldValue)

- WechatWxOpenOperationApiRealTimeLogSearchRequest EndTime(long fieldValue)

- WechatWxOpenOperationApiRealTimeLogSearchRequest Start(int fieldValue)

- WechatWxOpenOperationApiRealTimeLogSearchRequest Limit(int fieldValue)

- WechatWxOpenOperationApiRealTimeLogSearchRequest TraceId(string fieldValue)

- WechatWxOpenOperationApiRealTimeLogSearchRequest Url(string fieldValue)

- WechatWxOpenOperationApiRealTimeLogSearchRequest Id(string fieldValue)

- WechatWxOpenOperationApiRealTimeLogSearchRequest FilterMsg(string fieldValue)

- WechatWxOpenOperationApiRealTimeLogSearchRequest Level(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenOperationApiRealTimeLogSearchResponse (class)

- public string Raw;


## WechatWxOpenQrCodeJumpApi (class)

QrCodeJump/QrCodeJumpApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenQrCodeJumpApi(WechatWxOpenClient client)

- async WechatWxOpenQrCodeJumpApiGetResponse GetAsync()
  - POST /cgi-bin/wxopen/qrcodejumpget

- async WechatResponse GetRawAsync(string query, string jsonBody)

- async WechatWxOpenQrCodeJumpApiDownloadResponse DownloadAsync()
  - POST /cgi-bin/wxopen/qrcodejumpdownload

- async WechatResponse DownloadRawAsync(string query, string jsonBody)

- async WechatWxOpenQrCodeJumpApiAddResponse AddAsync(WechatWxOpenQrCodeJumpApiAddRequest request)
  - POST /cgi-bin/wxopen/qrcodejumpadd

- async WechatResponse AddRawAsync(string query, string jsonBody)

- async WechatWxOpenQrCodeJumpApiPublishResponse PublishAsync(WechatWxOpenQrCodeJumpApiPublishRequest request)
  - POST /cgi-bin/wxopen/qrcodejumppublish

- async WechatResponse PublishRawAsync(string query, string jsonBody)

- async WechatWxOpenQrCodeJumpApiDeleteResponse DeleteAsync(WechatWxOpenQrCodeJumpApiDeleteRequest request)
  - POST /cgi-bin/wxopen/qrcodejumpdelete

- async WechatResponse DeleteRawAsync(string query, string jsonBody)


## WechatWxOpenQrCodeJumpApiAddRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenQrCodeJumpApiAddRequest()

- WechatWxOpenQrCodeJumpApiAddRequest Prefix(string fieldValue)

- WechatWxOpenQrCodeJumpApiAddRequest PermitSubRule(int fieldValue)

- WechatWxOpenQrCodeJumpApiAddRequest Path(string fieldValue)

- WechatWxOpenQrCodeJumpApiAddRequest OpenVersion(int fieldValue)

- WechatWxOpenQrCodeJumpApiAddRequest DebugUrl(List<string> fieldValue)

- WechatWxOpenQrCodeJumpApiAddRequest IsEdit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenQrCodeJumpApiAddResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenQrCodeJumpApiDeleteRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenQrCodeJumpApiDeleteRequest()

- WechatWxOpenQrCodeJumpApiDeleteRequest Prefix(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenQrCodeJumpApiDeleteResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenQrCodeJumpApiDownloadResponse (class)

- public string Raw;


## WechatWxOpenQrCodeJumpApiGetResponse (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- public string Raw;


## WechatWxOpenQrCodeJumpApiPublishRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenQrCodeJumpApiPublishRequest()

- WechatWxOpenQrCodeJumpApiPublishRequest Prefix(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenQrCodeJumpApiPublishResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenRedPacketCoverApi (class)

RedPacketCover/RedPacketCoverApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenRedPacketCoverApi(WechatWxOpenClient client)

- async WechatWxOpenRedPacketCoverApiGetCoverUrlResponse GetCoverUrlAsync(WechatWxOpenRedPacketCoverApiGetCoverUrlRequest request)
  - POST /redpacketcover/wxapp/cover_url/get_by_token

- async WechatResponse GetCoverUrlRawAsync(string query, string jsonBody)


## WechatWxOpenRedPacketCoverApiGetCoverUrlRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenRedPacketCoverApiGetCoverUrlRequest()

- WechatWxOpenRedPacketCoverApiGetCoverUrlRequest Openid(string fieldValue)

- WechatWxOpenRedPacketCoverApiGetCoverUrlRequest Ctoken(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenRedPacketCoverApiGetCoverUrlResponse (class)

- public string Raw;


## WechatWxOpenSecOrderApi (class)

Sec/Order.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenSecOrderApi(WechatWxOpenClient client)

- async WechatWxOpenSecOrderApiUploadShippingInfoResponse UploadShippingInfoAsync(WechatWxOpenSecOrderApiUploadShippingInfoRequest request)
  - POST /wxa/sec/order/upload_shipping_info

- async WechatResponse UploadShippingInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderApiUploadCombinedShippingInfoResponse UploadCombinedShippingInfoAsync(WechatWxOpenSecOrderApiUploadCombinedShippingInfoRequest request)
  - POST /wxa/sec/order/upload_combined_shipping_info

- async WechatResponse UploadCombinedShippingInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderApiGetOrderResponse GetOrderAsync(WechatWxOpenSecOrderApiGetOrderRequest request)
  - POST /wxa/sec/order/get_order

- async WechatResponse GetOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderApiGetOrderListResponse GetOrderListAsync(WechatWxOpenSecOrderApiGetOrderListRequest request)
  - POST /wxa/sec/order/get_order_list

- async WechatResponse GetOrderListRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderApiNotifyConfirmReceiveResponse NotifyConfirmReceiveAsync(WechatWxOpenSecOrderApiNotifyConfirmReceiveRequest request)
  - POST /wxa/sec/order/notify_confirm_receive

- async WechatResponse NotifyConfirmReceiveRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderApiSetMsgJumpPathResponse SetMsgJumpPathAsync(WechatWxOpenSecOrderApiSetMsgJumpPathRequest request)
  - POST /wxa/sec/order/set_msg_jump_path

- async WechatResponse SetMsgJumpPathRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderApiIsTradeManagedResponse IsTradeManagedAsync(WechatWxOpenSecOrderApiIsTradeManagedRequest request)
  - POST /wxa/sec/order/is_trade_managed

- async WechatResponse IsTradeManagedRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderApiIsTradeManagementConfirmationCompletedResponse IsTradeManagementConfirmationCompletedAsync(WechatWxOpenSecOrderApiIsTradeManagementConfirmationCompletedRequest request)
  - POST /wxa/sec/order/is_trade_management_confirmation_completed

- async WechatResponse IsTradeManagementConfirmationCompletedRawAsync(string query, string jsonBody)


## WechatWxOpenSecOrderApiGetOrderListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSecOrderApiGetOrderListRequest()

- WechatWxOpenSecOrderApiGetOrderListRequest PayTimeRange(WechatWxOpenPayTimeRangeModel fieldValue)

- WechatWxOpenSecOrderApiGetOrderListRequest OrderState(int fieldValue)

- WechatWxOpenSecOrderApiGetOrderListRequest Openid(string fieldValue)

- WechatWxOpenSecOrderApiGetOrderListRequest LastIndex(string fieldValue)

- WechatWxOpenSecOrderApiGetOrderListRequest PageSize(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderApiGetOrderListResponse (class)

- public string Raw;


## WechatWxOpenSecOrderApiGetOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSecOrderApiGetOrderRequest()

- WechatWxOpenSecOrderApiGetOrderRequest TransactionId(string fieldValue)

- WechatWxOpenSecOrderApiGetOrderRequest MerchantId(string fieldValue)

- WechatWxOpenSecOrderApiGetOrderRequest SubMerchantId(string fieldValue)

- WechatWxOpenSecOrderApiGetOrderRequest MerchantTradeNo(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderApiGetOrderResponse (class)

- public string Raw;


## WechatWxOpenSecOrderApiIsTradeManagedRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSecOrderApiIsTradeManagedRequest()

- WechatWxOpenSecOrderApiIsTradeManagedRequest Appid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderApiIsTradeManagedResponse (class)

- public string Raw;


## WechatWxOpenSecOrderApiIsTradeManagementConfirmationCompletedRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSecOrderApiIsTradeManagementConfirmationCompletedRequest()

- WechatWxOpenSecOrderApiIsTradeManagementConfirmationCompletedRequest Appid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderApiIsTradeManagementConfirmationCompletedResponse (class)

- public string Raw;


## WechatWxOpenSecOrderApiNotifyConfirmReceiveRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSecOrderApiNotifyConfirmReceiveRequest()

- WechatWxOpenSecOrderApiNotifyConfirmReceiveRequest ReceiveTime(long fieldValue)

- WechatWxOpenSecOrderApiNotifyConfirmReceiveRequest TransactionId(string fieldValue)

- WechatWxOpenSecOrderApiNotifyConfirmReceiveRequest MerchantId(string fieldValue)

- WechatWxOpenSecOrderApiNotifyConfirmReceiveRequest SubMerchantId(string fieldValue)

- WechatWxOpenSecOrderApiNotifyConfirmReceiveRequest MerchantTradeNo(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderApiNotifyConfirmReceiveResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenSecOrderApiSetMsgJumpPathRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSecOrderApiSetMsgJumpPathRequest()

- WechatWxOpenSecOrderApiSetMsgJumpPathRequest Path(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderApiSetMsgJumpPathResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenSecOrderApiUploadCombinedShippingInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSecOrderApiUploadCombinedShippingInfoRequest()

- WechatWxOpenSecOrderApiUploadCombinedShippingInfoRequest OrderKey(WechatWxOpenOrderKeyModel fieldValue)

- WechatWxOpenSecOrderApiUploadCombinedShippingInfoRequest SubOrders(List<WechatWxOpenSubOrdersModel> fieldValue)

- WechatWxOpenSecOrderApiUploadCombinedShippingInfoRequest UploadTime(string fieldValue)

- WechatWxOpenSecOrderApiUploadCombinedShippingInfoRequest Payer(WechatWxOpenPayerModel fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderApiUploadCombinedShippingInfoResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenSecOrderApiUploadShippingInfoRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenSecOrderApiUploadShippingInfoRequest()

- WechatWxOpenSecOrderApiUploadShippingInfoRequest OrderKey(WechatWxOpenOrderKeyModel fieldValue)

- WechatWxOpenSecOrderApiUploadShippingInfoRequest LogisticsType(int fieldValue)

- WechatWxOpenSecOrderApiUploadShippingInfoRequest DeliveryMode(int fieldValue)

- WechatWxOpenSecOrderApiUploadShippingInfoRequest IsAllDelivered(bool fieldValue)

- WechatWxOpenSecOrderApiUploadShippingInfoRequest ShippingList(List<WechatWxOpenShippingListModel> fieldValue)

- WechatWxOpenSecOrderApiUploadShippingInfoRequest UploadTime(string fieldValue)

- WechatWxOpenSecOrderApiUploadShippingInfoRequest Payer(WechatWxOpenPayerModel fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderApiUploadShippingInfoResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenSecOrderIncrementApi (class)

Sec/OrderIncrementApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenSecOrderIncrementApi(WechatWxOpenClient client)

- async WechatWxOpenSecOrderIncrementApiReportSpecialOrderResponse ReportSpecialOrderAsync(WechatWxOpenSecOrderIncrementApiReportSpecialOrderRequest request)
  - POST /wxa/sec/order/opspecialorder

- async WechatResponse ReportSpecialOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderIncrementApiApplyFamousBrandResponse ApplyFamousBrandAsync(WechatWxOpenSecOrderIncrementApiApplyFamousBrandRequest request)
  - POST /wxa/sec/famousbrand/apply

- async WechatResponse ApplyFamousBrandRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderIncrementApiGetFamousBrandApplyStatusResponse GetFamousBrandApplyStatusAsync()
  - POST /wxa/sec/famousbrand/get_status

- async WechatResponse GetFamousBrandApplyStatusRawAsync(string query, string jsonBody)

- async WechatWxOpenSecOrderIncrementApiApplyTradeTypeChangeResponse ApplyTradeTypeChangeAsync(WechatWxOpenSecOrderIncrementApiApplyTradeTypeChangeRequest request)
  - POST /wxa/sec/order/setwxatradetypecgi

- async WechatResponse ApplyTradeTypeChangeRawAsync(string query, string jsonBody)


## WechatWxOpenSecOrderIncrementApiApplyFamousBrandRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSecOrderIncrementApiApplyFamousBrandRequest()

- WechatWxOpenSecOrderIncrementApiApplyFamousBrandRequest Application(WechatWxOpenFamousBrandApplication fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderIncrementApiApplyFamousBrandResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenSecOrderIncrementApiApplyTradeTypeChangeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSecOrderIncrementApiApplyTradeTypeChangeRequest()

- WechatWxOpenSecOrderIncrementApiApplyTradeTypeChangeRequest TradeType(int fieldValue)

- WechatWxOpenSecOrderIncrementApiApplyTradeTypeChangeRequest MaterialList(List<WechatWxOpenTradeTypeMaterial> fieldValue)

- WechatWxOpenSecOrderIncrementApiApplyTradeTypeChangeRequest Reason(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderIncrementApiApplyTradeTypeChangeResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenSecOrderIncrementApiGetFamousBrandApplyStatusResponse (class)

- public string Raw;


## WechatWxOpenSecOrderIncrementApiReportSpecialOrderRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenSecOrderIncrementApiReportSpecialOrderRequest()

- WechatWxOpenSecOrderIncrementApiReportSpecialOrderRequest OrderId(string fieldValue)

- WechatWxOpenSecOrderIncrementApiReportSpecialOrderRequest Type(int fieldValue)

- WechatWxOpenSecOrderIncrementApiReportSpecialOrderRequest DelayTo(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSecOrderIncrementApiReportSpecialOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenServiceMarketApi (class)

ServiceMarket/ServiceMarketApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenServiceMarketApi(WechatWxOpenClient client)

- async WechatWxOpenServiceMarketApiInvokeServiceResponse InvokeServiceAsync(WechatWxOpenServiceMarketApiInvokeServiceRequest request)
  - POST /wxa/servicemarket

- async WechatResponse InvokeServiceRawAsync(string query, string jsonBody)

- async WechatWxOpenServiceMarketApiRetrieveResultResponse RetrieveResultAsync(WechatWxOpenServiceMarketApiRetrieveResultRequest request)
  - POST /wxa/servicemarketretrieve

- async WechatResponse RetrieveResultRawAsync(string query, string jsonBody)


## WechatWxOpenServiceMarketApiInvokeServiceRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenServiceMarketApiInvokeServiceRequest()

- WechatWxOpenServiceMarketApiInvokeServiceRequest Request(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenServiceMarketApiInvokeServiceResponse (class)

- public string Raw;


## WechatWxOpenServiceMarketApiRetrieveResultRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenServiceMarketApiRetrieveResultRequest()

- WechatWxOpenServiceMarketApiRetrieveResultRequest RequestId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenServiceMarketApiRetrieveResultResponse (class)

- public string Raw;


## WechatWxOpenSnsApi (class)

Sns/SnsApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenSnsApi(WechatWxOpenClient client)

- async WechatWxOpenSnsApiJsCode2JsonResponse JsCode2JsonAsync(WechatWxOpenSnsApiJsCode2JsonRequest request)
  - GET /sns/jscode2session

- async WechatResponse JsCode2JsonRawAsync(string query)


## WechatWxOpenSnsApiJsCode2JsonRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenSnsApiJsCode2JsonRequest()

- WechatWxOpenSnsApiJsCode2JsonRequest AppId(string fieldValue)

- WechatWxOpenSnsApiJsCode2JsonRequest Secret(string fieldValue)

- WechatWxOpenSnsApiJsCode2JsonRequest JsCode(string fieldValue)

- WechatWxOpenSnsApiJsCode2JsonRequest GrantType(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSnsApiJsCode2JsonResponse (class)

- public string Raw;


## WechatWxOpenSoterApi (class)

Soter/SoterApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenSoterApi(WechatWxOpenClient client)

- async WechatWxOpenSoterApiVerifySignatureResponse VerifySignatureAsync(WechatWxOpenSoterApiVerifySignatureRequest request)
  - POST /cgi-bin/soter/verify_signature

- async WechatResponse VerifySignatureRawAsync(string query, string jsonBody)


## WechatWxOpenSoterApiVerifySignatureRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenSoterApiVerifySignatureRequest()

- WechatWxOpenSoterApiVerifySignatureRequest Openid(string fieldValue)

- WechatWxOpenSoterApiVerifySignatureRequest JsonString(string fieldValue)

- WechatWxOpenSoterApiVerifySignatureRequest JsonSignature(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenSoterApiVerifySignatureResponse (class)

- public string Raw;


## WechatWxOpenSpecialApi (class)

小程序客服动态路径、文件上传、媒体下载和二维码二进制接口。

- WechatWxOpenClient client;

- public WechatWxOpenSpecialApi(WechatWxOpenClient client)

- async WechatWxOpenSpecialOperationResponse SendTextAsync(WechatWxOpenSpecialSendTextRequest request)

- async WechatResponse SendTextRawAsync(string businessId, string jsonBody)

- async WechatWxOpenSpecialOperationResponse GetTypingStatusAsync(WechatWxOpenSpecialTypingRequest request)

- async WechatResponse GetTypingStatusRawAsync(string businessId, string jsonBody)

- async WechatResponse AiCropAsync(string imgUrl, string ratios)

- async WechatResponse AiCropByFileAsync(string ratios, string fileName, string contentType, string fileBytes)

- async WechatResponse QrCodeAsync(string imgUrl)

- async WechatResponse QrCodeByFileAsync(string fileName, string contentType, string fileBytes)

- async WechatResponse SuperResolutionAsync(string imgUrl)

- async WechatResponse SuperResolutionByFileAsync(string fileName, string contentType, string fileBytes)

- async WechatResponse DrivingLicenseAsync(string imgUrl)

- async WechatResponse DrivingLicenseByFileAsync(string fileName, string contentType, string fileBytes)

- async WechatResponse SendCvFileAsync(string path, string fileName, string contentType, string fileBytes)

- async WechatRawResponse GetBillAsync(WechatWxOpenSpecialBillRequest request)

- async WechatRawResponse GetBillRawAsync(string jsonBody)

- async WechatResponse SingleFileUploadAsync(string mediaName, string mediaType, string mediaFileName, string mediaContentType, string mediaBytes, string coverType, string coverFileName, string coverContentType, string coverBytes, string sourceContext)

- async WechatResponse UploadPartAsync(string uploadId, int partNumber, int resourceType, string fileName, string contentType, string fileBytes)

- async WechatRawResponse GetFeedbackMediaAsync(long recordId, string mediaId)

- async WechatRawResponse GetWxaCodeAsync(WechatWxOpenSpecialWxaCodeRequest request)

- async WechatRawResponse GetWxaCodeRawAsync(string jsonBody)

- async WechatRawResponse GetWxaCodeUnlimitAsync(WechatWxOpenSpecialWxaCodeUnlimitRequest request)

- async WechatRawResponse GetWxaCodeUnlimitRawAsync(string jsonBody)

- async WechatRawResponse CreateWxQrCodeAsync(WechatWxOpenSpecialQrCodeRequest request)

- async WechatRawResponse CreateWxQrCodeRawAsync(string jsonBody)

- async WechatResponse ImgSecCheckAsync(string fileName, string contentType, string fileBytes)


## WechatWxOpenSpecialBillRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSpecialBillRequest()

- WechatWxOpenSpecialBillRequest Date(string fieldValue)

- WechatWxOpenSpecialBillRequest Type(string fieldValue)

- string JsonBody()


## WechatWxOpenSpecialOperationResponse (class)

- public string Raw;


## WechatWxOpenSpecialQrCodeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSpecialQrCodeRequest()

- WechatWxOpenSpecialQrCodeRequest Path(string fieldValue)

- WechatWxOpenSpecialQrCodeRequest Width(int fieldValue)

- string JsonBody()


## WechatWxOpenSpecialSendTextRequest (class)

- WechatTypedRequest request;

- string businessId;

- public WechatWxOpenSpecialSendTextRequest()

- WechatWxOpenSpecialSendTextRequest OpenId(string fieldValue)

- WechatWxOpenSpecialSendTextRequest Content(string fieldValue)

- WechatWxOpenSpecialSendTextRequest BusinessId(string fieldValue)

- string Path()

- string JsonBody()


## WechatWxOpenSpecialTypingRequest (class)

- WechatTypedRequest request;

- string businessId;

- public WechatWxOpenSpecialTypingRequest()

- WechatWxOpenSpecialTypingRequest OpenId(string fieldValue)

- WechatWxOpenSpecialTypingRequest Command(string fieldValue)

- WechatWxOpenSpecialTypingRequest BusinessId(string fieldValue)

- string Path()

- string JsonBody()


## WechatWxOpenSpecialWxaCodeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSpecialWxaCodeRequest()

- WechatWxOpenSpecialWxaCodeRequest Path(string fieldValue)

- WechatWxOpenSpecialWxaCodeRequest Width(int fieldValue)

- WechatWxOpenSpecialWxaCodeRequest AutoColor(bool fieldValue)

- WechatWxOpenSpecialWxaCodeRequest LineColor(JsonValue fieldValue)

- WechatWxOpenSpecialWxaCodeRequest IsHyaline(bool fieldValue)

- string JsonBody()


## WechatWxOpenSpecialWxaCodeUnlimitRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenSpecialWxaCodeUnlimitRequest()

- WechatWxOpenSpecialWxaCodeUnlimitRequest Scene(string fieldValue)

- WechatWxOpenSpecialWxaCodeUnlimitRequest Page(string fieldValue)

- WechatWxOpenSpecialWxaCodeUnlimitRequest CheckPath(bool fieldValue)

- WechatWxOpenSpecialWxaCodeUnlimitRequest EnvVersion(string fieldValue)

- WechatWxOpenSpecialWxaCodeUnlimitRequest Width(int fieldValue)

- WechatWxOpenSpecialWxaCodeUnlimitRequest AutoColor(bool fieldValue)

- WechatWxOpenSpecialWxaCodeUnlimitRequest LineColor(JsonValue fieldValue)

- WechatWxOpenSpecialWxaCodeUnlimitRequest IsHyaline(bool fieldValue)

- string JsonBody()


## WechatWxOpenStudentApi (class)

Student/StudentApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenStudentApi(WechatWxOpenClient client)

- async WechatWxOpenStudentApiQuickCheckStudentIdentityResponse QuickCheckStudentIdentityAsync(WechatWxOpenStudentApiQuickCheckStudentIdentityRequest request)
  - POST /intp/quickcheckstudentidentity

- async WechatResponse QuickCheckStudentIdentityRawAsync(string query, string jsonBody)


## WechatWxOpenStudentApiQuickCheckStudentIdentityRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenStudentApiQuickCheckStudentIdentityRequest()

- WechatWxOpenStudentApiQuickCheckStudentIdentityRequest Openid(string fieldValue)

- WechatWxOpenStudentApiQuickCheckStudentIdentityRequest WxStudentcheckCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenStudentApiQuickCheckStudentIdentityResponse (class)

- public string Raw;


## WechatWxOpenTcbApi (class)

Tcb/TcbApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenTcbApi(WechatWxOpenClient client)

- async WechatWxOpenTcbApiSendTemplateMessageResponse SendTemplateMessageAsync(WechatWxOpenTcbApiSendTemplateMessageRequest request)
  - POST /tcb/invokecloudfunction

- async WechatResponse SendTemplateMessageRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseMigrateImportResponse DatabaseMigrateImportAsync(WechatWxOpenTcbApiDatabaseMigrateImportRequest request)
  - POST /tcb/databasemigrateimport

- async WechatResponse DatabaseMigrateImportRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseMigrateExportResponse DatabaseMigrateExportAsync(WechatWxOpenTcbApiDatabaseMigrateExportRequest request)
  - POST /tcb/databasemigrateexport

- async WechatResponse DatabaseMigrateExportRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseMigrateQueryInfoResponse DatabaseMigrateQueryInfoAsync(WechatWxOpenTcbApiDatabaseMigrateQueryInfoRequest request)
  - POST /tcb/databasemigratequeryinfo

- async WechatResponse DatabaseMigrateQueryInfoRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiUpdateIndexResponse UpdateIndexAsync(WechatWxOpenTcbApiUpdateIndexRequest request)
  - POST /tcb/updateindex

- async WechatResponse UpdateIndexRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseCollectionAddResponse DatabaseCollectionAddAsync(WechatWxOpenTcbApiDatabaseCollectionAddRequest request)
  - POST /tcb/databasecollectionadd

- async WechatResponse DatabaseCollectionAddRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseCollectionDeleteResponse DatabaseCollectionDeleteAsync(WechatWxOpenTcbApiDatabaseCollectionDeleteRequest request)
  - POST /tcb/databasecollectiondelete

- async WechatResponse DatabaseCollectionDeleteRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseCollectionGetResponse DatabaseCollectionGetAsync(WechatWxOpenTcbApiDatabaseCollectionGetRequest request)
  - POST /tcb/databasecollectionget

- async WechatResponse DatabaseCollectionGetRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseAddResponse DatabaseAddAsync(WechatWxOpenTcbApiDatabaseAddRequest request)
  - POST /tcb/databaseadd

- async WechatResponse DatabaseAddRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseDeleteResponse DatabaseDeleteAsync(WechatWxOpenTcbApiDatabaseDeleteRequest request)
  - POST /tcb/databasedelete

- async WechatResponse DatabaseDeleteRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseUpdateResponse DatabaseUpdateAsync(WechatWxOpenTcbApiDatabaseUpdateRequest request)
  - POST /tcb/databaseupdate

- async WechatResponse DatabaseUpdateRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseQueryResponse DatabaseQueryAsync(WechatWxOpenTcbApiDatabaseQueryRequest request)
  - POST /tcb/databasequery

- async WechatResponse DatabaseQueryRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseAggregateResponse DatabaseAggregateAsync(WechatWxOpenTcbApiDatabaseAggregateRequest request)
  - POST /tcb/databaseaggregate

- async WechatResponse DatabaseAggregateRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiDatabaseCountResponse DatabaseCountAsync(WechatWxOpenTcbApiDatabaseCountRequest request)
  - POST /tcb/databasecount

- async WechatResponse DatabaseCountRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiUploadFileResponse UploadFileAsync(WechatWxOpenTcbApiUploadFileRequest request)
  - POST /tcb/uploadfile

- async WechatResponse UploadFileRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiBatchDownloadFileResponse BatchDownloadFileAsync(WechatWxOpenTcbApiBatchDownloadFileRequest request)
  - POST /tcb/batchdownloadfile

- async WechatResponse BatchDownloadFileRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiBatchDeleteFileResponse BatchDeleteFileAsync(WechatWxOpenTcbApiBatchDeleteFileRequest request)
  - POST /tcb/batchdeletefile

- async WechatResponse BatchDeleteFileRawAsync(string query, string jsonBody)

- async WechatWxOpenTcbApiGetQcloudTokenResponse GetQcloudTokenAsync(WechatWxOpenTcbApiGetQcloudTokenRequest request)
  - POST /tcb/getqcloudtoken

- async WechatResponse GetQcloudTokenRawAsync(string query, string jsonBody)


## WechatWxOpenTcbApiBatchDeleteFileRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiBatchDeleteFileRequest()

- WechatWxOpenTcbApiBatchDeleteFileRequest Env(string fieldValue)

- WechatWxOpenTcbApiBatchDeleteFileRequest FileidList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiBatchDeleteFileResponse (class)

- public string Raw;


## WechatWxOpenTcbApiBatchDownloadFileRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiBatchDownloadFileRequest()

- WechatWxOpenTcbApiBatchDownloadFileRequest Env(string fieldValue)

- WechatWxOpenTcbApiBatchDownloadFileRequest FileList(List<WechatWxOpenFileItem> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiBatchDownloadFileResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseAddRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseAddRequest()

- WechatWxOpenTcbApiDatabaseAddRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseAddRequest Query(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseAddResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseAggregateRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseAggregateRequest()

- WechatWxOpenTcbApiDatabaseAggregateRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseAggregateRequest Query(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseAggregateResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseCollectionAddRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseCollectionAddRequest()

- WechatWxOpenTcbApiDatabaseCollectionAddRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseCollectionAddRequest CollectionName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseCollectionAddResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenTcbApiDatabaseCollectionDeleteRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseCollectionDeleteRequest()

- WechatWxOpenTcbApiDatabaseCollectionDeleteRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseCollectionDeleteRequest CollectionName(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseCollectionDeleteResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenTcbApiDatabaseCollectionGetRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseCollectionGetRequest()

- WechatWxOpenTcbApiDatabaseCollectionGetRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseCollectionGetRequest Limit(int fieldValue)

- WechatWxOpenTcbApiDatabaseCollectionGetRequest Offset(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseCollectionGetResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseCountRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseCountRequest()

- WechatWxOpenTcbApiDatabaseCountRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseCountRequest Query(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseCountResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseDeleteRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseDeleteRequest()

- WechatWxOpenTcbApiDatabaseDeleteRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseDeleteRequest Query(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseDeleteResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseMigrateExportRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseMigrateExportRequest()

- WechatWxOpenTcbApiDatabaseMigrateExportRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseMigrateExportRequest FilePath(string fieldValue)

- WechatWxOpenTcbApiDatabaseMigrateExportRequest FileType(int fieldValue)

- WechatWxOpenTcbApiDatabaseMigrateExportRequest Query(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseMigrateExportResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseMigrateImportRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseMigrateImportRequest()

- WechatWxOpenTcbApiDatabaseMigrateImportRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseMigrateImportRequest Collection(string fieldValue)

- WechatWxOpenTcbApiDatabaseMigrateImportRequest FilePath(string fieldValue)

- WechatWxOpenTcbApiDatabaseMigrateImportRequest FileType(int fieldValue)

- WechatWxOpenTcbApiDatabaseMigrateImportRequest StopOnError(bool fieldValue)

- WechatWxOpenTcbApiDatabaseMigrateImportRequest ConflictMode(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseMigrateImportResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseMigrateQueryInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseMigrateQueryInfoRequest()

- WechatWxOpenTcbApiDatabaseMigrateQueryInfoRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseMigrateQueryInfoRequest JobId(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseMigrateQueryInfoResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseQueryRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseQueryRequest()

- WechatWxOpenTcbApiDatabaseQueryRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseQueryRequest Query(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseQueryResponse (class)

- public string Raw;


## WechatWxOpenTcbApiDatabaseUpdateRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiDatabaseUpdateRequest()

- WechatWxOpenTcbApiDatabaseUpdateRequest Env(string fieldValue)

- WechatWxOpenTcbApiDatabaseUpdateRequest Query(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiDatabaseUpdateResponse (class)

- public string Raw;


## WechatWxOpenTcbApiGetQcloudTokenRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiGetQcloudTokenRequest()

- WechatWxOpenTcbApiGetQcloudTokenRequest Lifespan(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiGetQcloudTokenResponse (class)

- public string Raw;


## WechatWxOpenTcbApiSendTemplateMessageRequest (class)

方法请求/响应契约；可复用的 DTO 实体定义在 Sdk.Wechat.Models.WxOpen 中。

- WechatTypedRequest request;

- public WechatWxOpenTcbApiSendTemplateMessageRequest()

- WechatWxOpenTcbApiSendTemplateMessageRequest Env(string fieldValue)

- WechatWxOpenTcbApiSendTemplateMessageRequest Name(string fieldValue)

- WechatWxOpenTcbApiSendTemplateMessageRequest PostBody(JsonValue fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiSendTemplateMessageResponse (class)

- public string Raw;


## WechatWxOpenTcbApiUpdateIndexRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiUpdateIndexRequest()

- WechatWxOpenTcbApiUpdateIndexRequest Env(string fieldValue)

- WechatWxOpenTcbApiUpdateIndexRequest CollectionName(string fieldValue)

- WechatWxOpenTcbApiUpdateIndexRequest CreateIndexes(List<WechatWxOpenCreateIndex> fieldValue)

- WechatWxOpenTcbApiUpdateIndexRequest DropIndexes(List<WechatWxOpenDropIndex> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiUpdateIndexResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenTcbApiUploadFileRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTcbApiUploadFileRequest()

- WechatWxOpenTcbApiUploadFileRequest Env(string fieldValue)

- WechatWxOpenTcbApiUploadFileRequest Path(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbApiUploadFileResponse (class)

- public string Raw;


## WechatWxOpenTcbIncrementApi (class)

Tcb/TcbIncrementApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenTcbIncrementApi(WechatWxOpenClient client)

- async WechatWxOpenTcbIncrementApiGetOpenDataResponse GetOpenDataAsync(WechatWxOpenTcbIncrementApiGetOpenDataRequest request)
  - POST /wxa/getopendata

- async WechatResponse GetOpenDataRawAsync(string query, string jsonBody)


## WechatWxOpenTcbIncrementApiGetOpenDataRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenTcbIncrementApiGetOpenDataRequest()

- WechatWxOpenTcbIncrementApiGetOpenDataRequest CloudIds(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTcbIncrementApiGetOpenDataResponse (class)

- public string Raw;


## WechatWxOpenTemplateApi (class)

Template/TemplateApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenTemplateApi(WechatWxOpenClient client)

- async WechatWxOpenTemplateApiSendTemplateMessageResponse SendTemplateMessageAsync(WechatWxOpenTemplateApiSendTemplateMessageRequest request)
  - POST /cgi-bin/message/wxopen/template/send

- async WechatResponse SendTemplateMessageRawAsync(string query, string jsonBody)

- async WechatWxOpenTemplateApiUniformSendResponse UniformSendAsync(WechatWxOpenTemplateApiUniformSendRequest request)
  - POST /cgi-bin/message/wxopen/template/uniform_send

- async WechatResponse UniformSendRawAsync(string query, string jsonBody)

- async WechatWxOpenTemplateApiLibraryListResponse LibraryListAsync(WechatWxOpenTemplateApiLibraryListRequest request)
  - POST /cgi-bin/wxopen/template/library/list

- async WechatResponse LibraryListRawAsync(string query, string jsonBody)

- async WechatWxOpenTemplateApiLibraryGetResponse LibraryGetAsync(WechatWxOpenTemplateApiLibraryGetRequest request)
  - POST /cgi-bin/wxopen/template/library/get

- async WechatResponse LibraryGetRawAsync(string query, string jsonBody)

- async WechatWxOpenTemplateApiAddResponse AddAsync(WechatWxOpenTemplateApiAddRequest request)
  - POST /cgi-bin/wxopen/template/add

- async WechatResponse AddRawAsync(string query, string jsonBody)

- async WechatWxOpenTemplateApiListResponse ListAsync(WechatWxOpenTemplateApiListRequest request)
  - POST /cgi-bin/wxopen/template/list

- async WechatResponse ListRawAsync(string query, string jsonBody)

- async WechatWxOpenTemplateApiDelResponse DelAsync(WechatWxOpenTemplateApiDelRequest request)
  - POST /cgi-bin/wxopen/template/del

- async WechatResponse DelRawAsync(string query, string jsonBody)


## WechatWxOpenTemplateApiAddRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTemplateApiAddRequest()

- WechatWxOpenTemplateApiAddRequest Id(string fieldValue)

- WechatWxOpenTemplateApiAddRequest KeywordIdList(List<int> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTemplateApiAddResponse (class)

- public string Raw;


## WechatWxOpenTemplateApiDelRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTemplateApiDelRequest()

- WechatWxOpenTemplateApiDelRequest TemplateId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTemplateApiDelResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenTemplateApiLibraryGetRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTemplateApiLibraryGetRequest()

- WechatWxOpenTemplateApiLibraryGetRequest Id(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTemplateApiLibraryGetResponse (class)

- public string Raw;


## WechatWxOpenTemplateApiLibraryListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTemplateApiLibraryListRequest()

- WechatWxOpenTemplateApiLibraryListRequest Offset(int fieldValue)

- WechatWxOpenTemplateApiLibraryListRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTemplateApiLibraryListResponse (class)

- public string Raw;


## WechatWxOpenTemplateApiListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTemplateApiListRequest()

- WechatWxOpenTemplateApiListRequest Offset(int fieldValue)

- WechatWxOpenTemplateApiListRequest Count(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTemplateApiListResponse (class)

- public string Raw;


## WechatWxOpenTemplateApiSendTemplateMessageRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenTemplateApiSendTemplateMessageRequest()

- WechatWxOpenTemplateApiSendTemplateMessageRequest OpenId(string fieldValue)

- WechatWxOpenTemplateApiSendTemplateMessageRequest TemplateId(string fieldValue)

- WechatWxOpenTemplateApiSendTemplateMessageRequest Data(JsonValue fieldValue)

- WechatWxOpenTemplateApiSendTemplateMessageRequest FormId(string fieldValue)

- WechatWxOpenTemplateApiSendTemplateMessageRequest Page(string fieldValue)

- WechatWxOpenTemplateApiSendTemplateMessageRequest EmphasisKeyword(string fieldValue)

- WechatWxOpenTemplateApiSendTemplateMessageRequest Color(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTemplateApiSendTemplateMessageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenTemplateApiUniformSendRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTemplateApiUniformSendRequest()

- WechatWxOpenTemplateApiUniformSendRequest Touser(string fieldValue)

- WechatWxOpenTemplateApiUniformSendRequest WeappTemplateMsg(WechatWxOpenWeappTemplateMsg fieldValue)

- WechatWxOpenTemplateApiUniformSendRequest MpTemplateMsg(WechatWxOpenMpTemplateMsg fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTemplateApiUniformSendResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenTransactionGuaranteeApi (class)

TransactionGuarantee/TransactionGuaranteeApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenTransactionGuaranteeApi(WechatWxOpenClient client)

- async WechatWxOpenTransactionGuaranteeApiGetPenaltyListResponse GetPenaltyListAsync(WechatWxOpenTransactionGuaranteeApiGetPenaltyListRequest request)
  - GET /wxaapi/wxamptrade/get_penalty_list

- async WechatResponse GetPenaltyListRawAsync(string query)

- async WechatWxOpenTransactionGuaranteeApiGetGuaranteeStatusResponse GetGuaranteeStatusAsync()
  - GET /wxaapi/wxamptrade/get_guarantee_status

- async WechatResponse GetGuaranteeStatusRawAsync(string query)

- async WechatWxOpenTransactionGuaranteeApiGetCommentListResponse GetCommentListAsync(WechatWxOpenTransactionGuaranteeApiGetCommentListRequest request)
  - GET /wxaapi/comment/mpcommentlist/get

- async WechatResponse GetCommentListRawAsync(string query)

- async WechatWxOpenTransactionGuaranteeApiGetCommentReplyListResponse GetCommentReplyListAsync(WechatWxOpenTransactionGuaranteeApiGetCommentReplyListRequest request)
  - GET /wxaapi/comment/replyandcommentreplylist/get

- async WechatResponse GetCommentReplyListRawAsync(string query)

- async WechatWxOpenTransactionGuaranteeApiGetCommentInfoResponse GetCommentInfoAsync(WechatWxOpenTransactionGuaranteeApiGetCommentInfoRequest request)
  - GET /wxaapi/comment/commentinfo/get

- async WechatResponse GetCommentInfoRawAsync(string query)

- async WechatWxOpenTransactionGuaranteeApiAddReplyResponse AddReplyAsync(WechatWxOpenTransactionGuaranteeApiAddReplyRequest request)
  - POST /wxaapi/comment/reply/add

- async WechatResponse AddReplyRawAsync(string query, string jsonBody)

- async WechatWxOpenTransactionGuaranteeApiDeleteReplyResponse DeleteReplyAsync(WechatWxOpenTransactionGuaranteeApiDeleteReplyRequest request)
  - POST /wxaapi/comment/reply/delete

- async WechatResponse DeleteReplyRawAsync(string query, string jsonBody)

- async WechatWxOpenTransactionGuaranteeApiAddCommentReplyResponse AddCommentReplyAsync(WechatWxOpenTransactionGuaranteeApiAddCommentReplyRequest request)
  - POST /wxaapi/comment/commentreply/add

- async WechatResponse AddCommentReplyRawAsync(string query, string jsonBody)

- async WechatWxOpenTransactionGuaranteeApiDeleteCommentReplyResponse DeleteCommentReplyAsync(WechatWxOpenTransactionGuaranteeApiDeleteCommentReplyRequest request)
  - POST /wxaapi/comment/commentreply/delete

- async WechatResponse DeleteCommentReplyRawAsync(string query, string jsonBody)

- async WechatWxOpenTransactionGuaranteeApiResetApiCustomerServiceQuotaResponse ResetApiCustomerServiceQuotaAsync(WechatWxOpenTransactionGuaranteeApiResetApiCustomerServiceQuotaRequest request)
  - POST /wxaapi/comment/apikfquota/reset

- async WechatResponse ResetApiCustomerServiceQuotaRawAsync(string query, string jsonBody)

- async WechatWxOpenTransactionGuaranteeApiConfirmCompromiseResponse ConfirmCompromiseAsync(WechatWxOpenTransactionGuaranteeApiConfirmCompromiseRequest request)
  - POST /wxaapi/comment/confirmcompromise

- async WechatResponse ConfirmCompromiseRawAsync(string query, string jsonBody)

- async WechatWxOpenTransactionGuaranteeApiRespondComplaintResponse RespondComplaintAsync(WechatWxOpenTransactionGuaranteeApiRespondComplaintRequest request)
  - POST /wxaapi/minishop/bussiRespondComplaint

- async WechatResponse RespondComplaintRawAsync(string query, string jsonBody)

- async WechatWxOpenTransactionGuaranteeApiSupplyComplaintProofResponse SupplyComplaintProofAsync(WechatWxOpenTransactionGuaranteeApiSupplyComplaintProofRequest request)
  - POST /wxaapi/minishop/bussiSupplyProof

- async WechatResponse SupplyComplaintProofRawAsync(string query, string jsonBody)

- async WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundResponse SubmitComplaintRefundAsync(WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundRequest request)
  - POST /wxaapi/minishop/bussiSupplyRefund

- async WechatResponse SubmitComplaintRefundRawAsync(string query, string jsonBody)

- async WechatWxOpenTransactionGuaranteeApiGetComplaintOrderDetailResponse GetComplaintOrderDetailAsync(WechatWxOpenTransactionGuaranteeApiGetComplaintOrderDetailRequest request)
  - GET /wxaapi/minishop/complaintOrderDetail

- async WechatResponse GetComplaintOrderDetailRawAsync(string query)

- async WechatWxOpenTransactionGuaranteeApiSubmitComplaintAppealResponse SubmitComplaintAppealAsync(WechatWxOpenTransactionGuaranteeApiSubmitComplaintAppealRequest request)
  - POST /wxaapi/minishop/busiAppeal

- async WechatResponse SubmitComplaintAppealRawAsync(string query, string jsonBody)


## WechatWxOpenTransactionGuaranteeApiAddCommentReplyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiAddCommentReplyRequest()

- WechatWxOpenTransactionGuaranteeApiAddCommentReplyRequest Content(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiAddCommentReplyRequest ReplyId(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiAddCommentReplyRequest CommentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiAddCommentReplyResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiAddReplyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiAddReplyRequest()

- WechatWxOpenTransactionGuaranteeApiAddReplyRequest Content(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiAddReplyRequest CommentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiAddReplyResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiConfirmCompromiseRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiConfirmCompromiseRequest()

- WechatWxOpenTransactionGuaranteeApiConfirmCompromiseRequest PicList(List<string> fieldValue)

- WechatWxOpenTransactionGuaranteeApiConfirmCompromiseRequest Content(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiConfirmCompromiseRequest CommentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiConfirmCompromiseResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiDeleteCommentReplyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiDeleteCommentReplyRequest()

- WechatWxOpenTransactionGuaranteeApiDeleteCommentReplyRequest CommentReplyId(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiDeleteCommentReplyRequest ReplyId(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiDeleteCommentReplyRequest CommentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiDeleteCommentReplyResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiDeleteReplyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiDeleteReplyRequest()

- WechatWxOpenTransactionGuaranteeApiDeleteReplyRequest ReplyId(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiDeleteReplyRequest CommentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiDeleteReplyResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiGetCommentInfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiGetCommentInfoRequest()

- WechatWxOpenTransactionGuaranteeApiGetCommentInfoRequest CommentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiGetCommentInfoResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiGetCommentListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiGetCommentListRequest()

- WechatWxOpenTransactionGuaranteeApiGetCommentListRequest StartTime(long fieldValue)

- WechatWxOpenTransactionGuaranteeApiGetCommentListRequest EndTime(long fieldValue)

- WechatWxOpenTransactionGuaranteeApiGetCommentListRequest FilterType(int fieldValue)

- WechatWxOpenTransactionGuaranteeApiGetCommentListRequest Offset(int fieldValue)

- WechatWxOpenTransactionGuaranteeApiGetCommentListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiGetCommentListResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiGetCommentReplyListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiGetCommentReplyListRequest()

- WechatWxOpenTransactionGuaranteeApiGetCommentReplyListRequest CommentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiGetCommentReplyListResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiGetComplaintOrderDetailRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiGetComplaintOrderDetailRequest()

- WechatWxOpenTransactionGuaranteeApiGetComplaintOrderDetailRequest ComplaintOrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiGetComplaintOrderDetailResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiGetGuaranteeStatusResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiGetPenaltyListRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiGetPenaltyListRequest()

- WechatWxOpenTransactionGuaranteeApiGetPenaltyListRequest Offset(int fieldValue)

- WechatWxOpenTransactionGuaranteeApiGetPenaltyListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiGetPenaltyListResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiResetApiCustomerServiceQuotaRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiResetApiCustomerServiceQuotaRequest()

- WechatWxOpenTransactionGuaranteeApiResetApiCustomerServiceQuotaRequest CommentId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiResetApiCustomerServiceQuotaResponse (class)

- public string Raw;


## WechatWxOpenTransactionGuaranteeApiRespondComplaintRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiRespondComplaintRequest()

- WechatWxOpenTransactionGuaranteeApiRespondComplaintRequest Content(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiRespondComplaintRequest ComplaintOrderId(long fieldValue)

- WechatWxOpenTransactionGuaranteeApiRespondComplaintRequest MediaIdList(List<string> fieldValue)

- WechatWxOpenTransactionGuaranteeApiRespondComplaintRequest BussiHandle(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiRespondComplaintResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenTransactionGuaranteeApiSubmitComplaintAppealRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiSubmitComplaintAppealRequest()

- WechatWxOpenTransactionGuaranteeApiSubmitComplaintAppealRequest Content(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiSubmitComplaintAppealRequest ComplaintOrderId(long fieldValue)

- WechatWxOpenTransactionGuaranteeApiSubmitComplaintAppealRequest MediaIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiSubmitComplaintAppealResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundRequest()

- WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundRequest AcceptReturn(int fieldValue)

- WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundRequest ReturnId(long fieldValue)

- WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundRequest Content(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundRequest ComplaintOrderId(long fieldValue)

- WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundRequest MediaIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiSubmitComplaintRefundResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenTransactionGuaranteeApiSupplyComplaintProofRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenTransactionGuaranteeApiSupplyComplaintProofRequest()

- WechatWxOpenTransactionGuaranteeApiSupplyComplaintProofRequest Content(string fieldValue)

- WechatWxOpenTransactionGuaranteeApiSupplyComplaintProofRequest ComplaintOrderId(long fieldValue)

- WechatWxOpenTransactionGuaranteeApiSupplyComplaintProofRequest MediaIdList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenTransactionGuaranteeApiSupplyComplaintProofResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressApi (class)

WeixinExpress/WeixinExpressApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWeixinExpressApi(WechatWxOpenClient client)

- async WechatWxOpenWeixinExpressApiQueryTraceResponse QueryTraceAsync(WechatWxOpenWeixinExpressApiQueryTraceRequest request)
  - POST /cgi-bin/express/delivery/open_msg/query_trace

- async WechatResponse QueryTraceRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressApiGetDeliveryListResponse GetDeliveryListAsync()
  - POST /cgi-bin/express/delivery/open_msg/get_delivery_list

- async WechatResponse GetDeliveryListRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressApiTraceWaybillResponse TraceWaybillAsync(WechatWxOpenWeixinExpressApiTraceWaybillRequest request)
  - POST /cgi-bin/express/delivery/open_msg/trace_waybill

- async WechatResponse TraceWaybillRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressApiUpdateWaybillGoodsResponse UpdateWaybillGoodsAsync(WechatWxOpenWeixinExpressApiUpdateWaybillGoodsRequest request)
  - POST /cgi-bin/express/delivery/open_msg/update_waybill_goods

- async WechatResponse UpdateWaybillGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressApiUpdateFollowWaybillGoodsResponse UpdateFollowWaybillGoodsAsync(WechatWxOpenWeixinExpressApiUpdateFollowWaybillGoodsRequest request)
  - POST /cgi-bin/express/delivery/open_msg/update_follow_waybill_goods

- async WechatResponse UpdateFollowWaybillGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressApiQueryFollowTraceResponse QueryFollowTraceAsync(WechatWxOpenWeixinExpressApiQueryFollowTraceRequest request)
  - POST /cgi-bin/express/delivery/open_msg/query_follow_trace

- async WechatResponse QueryFollowTraceRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressApiFollowWaybillResponse FollowWaybillAsync(WechatWxOpenWeixinExpressApiFollowWaybillRequest request)
  - POST /cgi-bin/express/delivery/open_msg/follow_waybill

- async WechatResponse FollowWaybillRawAsync(string query, string jsonBody)


## WechatWxOpenWeixinExpressApiFollowWaybillRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressApiFollowWaybillRequest()

- WechatWxOpenWeixinExpressApiFollowWaybillRequest Openid(string fieldValue)

- WechatWxOpenWeixinExpressApiFollowWaybillRequest SenderPhone(string fieldValue)

- WechatWxOpenWeixinExpressApiFollowWaybillRequest ReceiverPhone(string fieldValue)

- WechatWxOpenWeixinExpressApiFollowWaybillRequest WaybillId(string fieldValue)

- WechatWxOpenWeixinExpressApiFollowWaybillRequest GoodsInfo(WechatWxOpenWeixinExpressGoodsInfo fieldValue)

- WechatWxOpenWeixinExpressApiFollowWaybillRequest TransId(string fieldValue)

- WechatWxOpenWeixinExpressApiFollowWaybillRequest OrderDetailPath(string fieldValue)

- WechatWxOpenWeixinExpressApiFollowWaybillRequest DeliveryId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressApiFollowWaybillResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressApiGetDeliveryListResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressApiQueryFollowTraceRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressApiQueryFollowTraceRequest()

- WechatWxOpenWeixinExpressApiQueryFollowTraceRequest WaybillToken(string fieldValue)

- WechatWxOpenWeixinExpressApiQueryFollowTraceRequest Openid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressApiQueryFollowTraceResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressApiQueryTraceRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressApiQueryTraceRequest()

- WechatWxOpenWeixinExpressApiQueryTraceRequest WaybillToken(string fieldValue)

- WechatWxOpenWeixinExpressApiQueryTraceRequest Openid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressApiQueryTraceResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressApiTraceWaybillRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressApiTraceWaybillRequest()

- WechatWxOpenWeixinExpressApiTraceWaybillRequest Openid(string fieldValue)

- WechatWxOpenWeixinExpressApiTraceWaybillRequest SenderPhone(string fieldValue)

- WechatWxOpenWeixinExpressApiTraceWaybillRequest ReceiverPhone(string fieldValue)

- WechatWxOpenWeixinExpressApiTraceWaybillRequest WaybillId(string fieldValue)

- WechatWxOpenWeixinExpressApiTraceWaybillRequest GoodsInfo(WechatWxOpenWeixinExpressGoodsInfo fieldValue)

- WechatWxOpenWeixinExpressApiTraceWaybillRequest TransId(string fieldValue)

- WechatWxOpenWeixinExpressApiTraceWaybillRequest OrderDetailPath(string fieldValue)

- WechatWxOpenWeixinExpressApiTraceWaybillRequest DeliveryId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressApiTraceWaybillResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressApiUpdateFollowWaybillGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressApiUpdateFollowWaybillGoodsRequest()

- WechatWxOpenWeixinExpressApiUpdateFollowWaybillGoodsRequest WaybillToken(string fieldValue)

- WechatWxOpenWeixinExpressApiUpdateFollowWaybillGoodsRequest Openid(string fieldValue)

- WechatWxOpenWeixinExpressApiUpdateFollowWaybillGoodsRequest GoodsInfo(WechatWxOpenWeixinExpressGoodsInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressApiUpdateFollowWaybillGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressApiUpdateWaybillGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressApiUpdateWaybillGoodsRequest()

- WechatWxOpenWeixinExpressApiUpdateWaybillGoodsRequest WaybillToken(string fieldValue)

- WechatWxOpenWeixinExpressApiUpdateWaybillGoodsRequest Openid(string fieldValue)

- WechatWxOpenWeixinExpressApiUpdateWaybillGoodsRequest GoodsInfo(WechatWxOpenWeixinExpressGoodsInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressApiUpdateWaybillGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressInsuranceApi (class)

WeixinExpress/WeixinExpressInsuranceApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWeixinExpressInsuranceApi(WechatWxOpenClient client)

- async WechatWxOpenWeixinExpressInsuranceApiOpenInsuranceFreightResponse OpenInsuranceFreightAsync()
  - POST /wxa/business/insurance_freight/open

- async WechatResponse OpenInsuranceFreightRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiQueryInsuranceFreightOpenStatusResponse QueryInsuranceFreightOpenStatusAsync()
  - POST /wxa/business/insurance_freight/query_open

- async WechatResponse QueryInsuranceFreightOpenStatusRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderResponse CreateInsuranceFreightOrderAsync(WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest request)
  - POST /wxa/business/insurance_freight/createorder

- async WechatResponse CreateInsuranceFreightOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiClaimInsuranceFreightResponse ClaimInsuranceFreightAsync(WechatWxOpenWeixinExpressInsuranceApiClaimInsuranceFreightRequest request)
  - POST /wxa/business/insurance_freight/claim

- async WechatResponse ClaimInsuranceFreightRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceChargeIdResponse CreateInsuranceChargeIdAsync(WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceChargeIdRequest request)
  - POST /wxa/business/insurance_freight/createchargeid

- async WechatResponse CreateInsuranceChargeIdRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiApplyInsurancePayResponse ApplyInsurancePayAsync(WechatWxOpenWeixinExpressInsuranceApiApplyInsurancePayRequest request)
  - POST /wxa/business/insurance_freight/applypay

- async WechatResponse ApplyInsurancePayRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiGetInsurancePayOrderListResponse GetInsurancePayOrderListAsync(WechatWxOpenWeixinExpressInsuranceApiGetInsurancePayOrderListRequest request)
  - POST /wxa/business/insurance_freight/getpayorderlist

- async WechatResponse GetInsurancePayOrderListRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiRefundInsurancePremiumResponse RefundInsurancePremiumAsync()
  - POST /wxa/business/insurance_freight/refund

- async WechatResponse RefundInsurancePremiumRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiGetInsuranceSummaryResponse GetInsuranceSummaryAsync(WechatWxOpenWeixinExpressInsuranceApiGetInsuranceSummaryRequest request)
  - POST /wxa/business/insurance_freight/getsummary

- async WechatResponse GetInsuranceSummaryRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListResponse GetInsuranceOrderListAsync(WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest request)
  - POST /wxa/business/insurance_freight/getorderlist

- async WechatResponse GetInsuranceOrderListRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressInsuranceApiUpdateInsuranceNotifyFundsResponse UpdateInsuranceNotifyFundsAsync(WechatWxOpenWeixinExpressInsuranceApiUpdateInsuranceNotifyFundsRequest request)
  - POST /wxa/business/insurance_freight/update_notify_funds

- async WechatResponse UpdateInsuranceNotifyFundsRawAsync(string query, string jsonBody)


## WechatWxOpenWeixinExpressInsuranceApiApplyInsurancePayRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressInsuranceApiApplyInsurancePayRequest()

- WechatWxOpenWeixinExpressInsuranceApiApplyInsurancePayRequest OrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressInsuranceApiApplyInsurancePayResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressInsuranceApiClaimInsuranceFreightRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressInsuranceApiClaimInsuranceFreightRequest()

- WechatWxOpenWeixinExpressInsuranceApiClaimInsuranceFreightRequest Openid(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiClaimInsuranceFreightRequest OrderNo(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiClaimInsuranceFreightRequest RefundDeliveryNo(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiClaimInsuranceFreightRequest RefundCompany(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressInsuranceApiClaimInsuranceFreightResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceChargeIdRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceChargeIdRequest()

- WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceChargeIdRequest Quota(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceChargeIdResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest()

- WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest Openid(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest OrderNo(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest PayTime(long fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest PayAmount(long fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest DeliveryNo(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest DeliveryPlace(WechatWxOpenWeixinExpressInsurancePlace fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest ReceiptPlace(WechatWxOpenWeixinExpressInsurancePlace fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderRequest ProductInfo(WechatWxOpenWeixinExpressInsuranceProductInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressInsuranceApiCreateInsuranceFreightOrderResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest()

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest Openid(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest OrderNo(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest PolicyNo(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest ReportNo(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest DeliveryNo(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest RefundDeliveryNo(string fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest BeginTime(long fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest EndTime(long fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest StatusList(List<int> fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest Offset(int fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest Limit(int fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListRequest SortDirect(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressInsuranceApiGetInsuranceOrderListResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressInsuranceApiGetInsurancePayOrderListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressInsuranceApiGetInsurancePayOrderListRequest()

- WechatWxOpenWeixinExpressInsuranceApiGetInsurancePayOrderListRequest StatusList(List<int> fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsurancePayOrderListRequest Offset(int fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsurancePayOrderListRequest Limit(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressInsuranceApiGetInsurancePayOrderListResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressInsuranceApiGetInsuranceSummaryRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressInsuranceApiGetInsuranceSummaryRequest()

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceSummaryRequest BeginTime(long fieldValue)

- WechatWxOpenWeixinExpressInsuranceApiGetInsuranceSummaryRequest EndTime(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressInsuranceApiGetInsuranceSummaryResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressInsuranceApiOpenInsuranceFreightResponse (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressInsuranceApiQueryInsuranceFreightOpenStatusResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressInsuranceApiRefundInsurancePremiumResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressInsuranceApiUpdateInsuranceNotifyFundsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressInsuranceApiUpdateInsuranceNotifyFundsRequest()

- WechatWxOpenWeixinExpressInsuranceApiUpdateInsuranceNotifyFundsRequest NotifyFunds(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressInsuranceApiUpdateInsuranceNotifyFundsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressIntracityApi (class)

WeixinExpress/WeixinExpressIntracityApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWeixinExpressIntracityApi(WechatWxOpenClient client)

- async WechatWxOpenWeixinExpressIntracityApiIntracityApplyResponse IntracityApplyAsync()
  - POST /cgi-bin/express/intracity/apply

- async WechatResponse IntracityApplyRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreResponse IntracityCreateStoreAsync(WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreRequest request)
  - POST /cgi-bin/express/intracity/createstore

- async WechatResponse IntracityCreateStoreRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityQueryStoreResponse IntracityQueryStoreAsync(WechatWxOpenWeixinExpressIntracityApiIntracityQueryStoreRequest request)
  - POST /cgi-bin/express/intracity/querystore

- async WechatResponse IntracityQueryStoreRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityUpdateStoreResponse IntracityUpdateStoreAsync(WechatWxOpenWeixinExpressIntracityApiIntracityUpdateStoreRequest request)
  - POST /cgi-bin/express/intracity/updatestore

- async WechatResponse IntracityUpdateStoreRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityStoreChargeResponse IntracityStoreChargeAsync(WechatWxOpenWeixinExpressIntracityApiIntracityStoreChargeRequest request)
  - POST /cgi-bin/express/intracity/storecharge

- async WechatResponse IntracityStoreChargeRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityStoreRefundResponse IntracityStoreRefundAsync(WechatWxOpenWeixinExpressIntracityApiIntracityStoreRefundRequest request)
  - POST /cgi-bin/express/intracity/storerefund

- async WechatResponse IntracityStoreRefundRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowResponse IntracityQueryFlowAsync(WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowRequest request)
  - POST /cgi-bin/express/intracity/queryflow

- async WechatResponse IntracityQueryFlowRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityBalanceQueryResponse IntracityBalanceQueryAsync(WechatWxOpenWeixinExpressIntracityApiIntracityBalanceQueryRequest request)
  - POST /cgi-bin/express/intracity/balancequery

- async WechatResponse IntracityBalanceQueryRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderResponse IntracityPreAddOrderAsync(WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest request)
  - POST /cgi-bin/express/intracity/preaddorder

- async WechatResponse IntracityPreAddOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderResponse IntracityAddOrderAsync(WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest request)
  - POST /cgi-bin/express/intracity/addorder

- async WechatResponse IntracityAddOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityQueryOrderResponse IntracityQueryOrderAsync(WechatWxOpenWeixinExpressIntracityApiIntracityQueryOrderRequest request)
  - POST /cgi-bin/express/intracity/queryorder

- async WechatResponse IntracityQueryOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderResponse IntracityCancelOrderAsync(WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderRequest request)
  - POST /cgi-bin/express/intracity/cancelorder

- async WechatResponse IntracityCancelOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracitySetPayModeResponse IntracitySetPayModeAsync(WechatWxOpenWeixinExpressIntracityApiIntracitySetPayModeRequest request)
  - POST /cgi-bin/express/intracity/setpaymode

- async WechatResponse IntracitySetPayModeRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityGetPayModeResponse IntracityGetPayModeAsync(WechatWxOpenWeixinExpressIntracityApiIntracityGetPayModeRequest request)
  - POST /cgi-bin/express/intracity/getpaymode

- async WechatResponse IntracityGetPayModeRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityGetCityResponse IntracityGetCityAsync(WechatWxOpenWeixinExpressIntracityApiIntracityGetCityRequest request)
  - POST /cgi-bin/express/intracity/getcity

- async WechatResponse IntracityGetCityRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressIntracityApiIntracityMockNotifyResponse IntracityMockNotifyAsync(WechatWxOpenWeixinExpressIntracityApiIntracityMockNotifyRequest request)
  - POST /cgi-bin/express/intracity/mocknotify

- async WechatResponse IntracityMockNotifyRawAsync(string query, string jsonBody)


## WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest StoreOrderId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest UserOpenid(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest UserLng(double fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest UserLat(double fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest UserAddress(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest UserName(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest UserPhone(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest OrderSeq(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest VerifyCodeType(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest OrderDetailPath(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest CallbackUrl(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest UseSandbox(int fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderRequest Cargo(WechatWxOpenWeixinExpressIntracityCargo fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityAddOrderResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityApplyResponse (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressIntracityApiIntracityBalanceQueryRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityBalanceQueryRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityBalanceQueryRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityBalanceQueryRequest ServiceTransId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityBalanceQueryRequest PayMode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityBalanceQueryResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderRequest CancelReasonId(int fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderRequest CancelReason(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderRequest StoreOrderId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderRequest WxOrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityCancelOrderResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreRequest OutStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreRequest StoreName(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreRequest OrderPattern(int fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreRequest ServiceTransPrefer(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreRequest AddressInfo(WechatWxOpenWeixinExpressIntracityAddressInfo fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityCreateStoreResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityGetCityRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityGetCityRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityGetCityRequest ServiceTransId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityGetCityResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityGetPayModeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityGetPayModeRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityGetPayModeRequest Appid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityGetPayModeResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityMockNotifyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityMockNotifyRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityMockNotifyRequest WxOrderId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityMockNotifyRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityMockNotifyRequest StoreOrderId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityMockNotifyRequest OrderStatus(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityMockNotifyResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest UserName(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest UserPhone(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest UserLng(double fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest UserLat(double fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest UserAddress(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest CargoName(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest Cargo(WechatWxOpenWeixinExpressIntracityCargo fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderRequest UseSandbox(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityPreAddOrderResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowRequest FlowType(int fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowRequest ServiceTransId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowRequest BeginTime(long fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowRequest EndTime(long fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowRequest PayMode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityQueryFlowResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityQueryOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityQueryOrderRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryOrderRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryOrderRequest StoreOrderId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryOrderRequest WxOrderId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityQueryOrderResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityQueryStoreRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityQueryStoreRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryStoreRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityQueryStoreRequest OutStoreId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityQueryStoreResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracitySetPayModeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracitySetPayModeRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracitySetPayModeRequest Appid(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracitySetPayModeRequest PayMode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracitySetPayModeResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressIntracityApiIntracityStoreChargeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityStoreChargeRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityStoreChargeRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityStoreChargeRequest ServiceTransId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityStoreChargeRequest Amount(long fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityStoreChargeRequest PayMode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityStoreChargeResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityStoreRefundRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityStoreRefundRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityStoreRefundRequest WxStoreId(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityStoreRefundRequest PayMode(string fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityStoreRefundRequest ServiceTransId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityStoreRefundResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressIntracityApiIntracityUpdateStoreRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressIntracityApiIntracityUpdateStoreRequest()

- WechatWxOpenWeixinExpressIntracityApiIntracityUpdateStoreRequest Keys(WechatWxOpenWeixinExpressIntracityStoreKey fieldValue)

- WechatWxOpenWeixinExpressIntracityApiIntracityUpdateStoreRequest Content(WechatWxOpenWeixinExpressIntracityStoreUpdateContent fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressIntracityApiIntracityUpdateStoreResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWeixinExpressProviderApi (class)

WeixinExpress/WeixinExpressProviderApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWeixinExpressProviderApi(WechatWxOpenClient client)

- async WechatWxOpenWeixinExpressProviderApiQueryUserBindingResponse QueryUserBindingAsync(WechatWxOpenWeixinExpressProviderApiQueryUserBindingRequest request)
  - POST /cgi-bin/express/delivery/userquery

- async WechatResponse QueryUserBindingRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressProviderApiNotifyPathResponse NotifyPathAsync(WechatWxOpenWeixinExpressProviderApiNotifyPathRequest request)
  - POST /cgi-bin/express/delivery/pathnotify

- async WechatResponse NotifyPathRawAsync(string query, string jsonBody)


## WechatWxOpenWeixinExpressProviderApiNotifyPathRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressProviderApiNotifyPathRequest()

- WechatWxOpenWeixinExpressProviderApiNotifyPathRequest Sender(WechatWxOpenWeixinExpressPathContact fieldValue)

- WechatWxOpenWeixinExpressProviderApiNotifyPathRequest Receiver(WechatWxOpenWeixinExpressPathContact fieldValue)

- WechatWxOpenWeixinExpressProviderApiNotifyPathRequest WaybillId(string fieldValue)

- WechatWxOpenWeixinExpressProviderApiNotifyPathRequest Path(WechatWxOpenWeixinExpressPathNode fieldValue)

- WechatWxOpenWeixinExpressProviderApiNotifyPathRequest CreateTime(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressProviderApiNotifyPathResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressProviderApiQueryUserBindingRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressProviderApiQueryUserBindingRequest()

- WechatWxOpenWeixinExpressProviderApiQueryUserBindingRequest Phone(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressProviderApiQueryUserBindingResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressReturnApi (class)

WeixinExpress/WeixinExpressReturnApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWeixinExpressReturnApi(WechatWxOpenClient client)

- async WechatWxOpenWeixinExpressReturnApiUnbindReturnIdResponse UnbindReturnIdAsync(WechatWxOpenWeixinExpressReturnApiUnbindReturnIdRequest request)
  - POST /cgi-bin/express/delivery/no_worry_return/unbind

- async WechatResponse UnbindReturnIdRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressReturnApiGetReturnIdResponse GetReturnIdAsync(WechatWxOpenWeixinExpressReturnApiGetReturnIdRequest request)
  - POST /cgi-bin/express/delivery/no_worry_return/get

- async WechatResponse GetReturnIdRawAsync(string query, string jsonBody)

- async WechatWxOpenWeixinExpressReturnApiAddReturnIdResponse AddReturnIdAsync(WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest request)
  - POST /cgi-bin/express/delivery/no_worry_return/add

- async WechatResponse AddReturnIdRawAsync(string query, string jsonBody)


## WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest()

- WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest ShopOrderId(string fieldValue)

- WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest BizAddr(WechatWxOpenWeixinExpressReturnAddress fieldValue)

- WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest UserAddr(WechatWxOpenWeixinExpressReturnAddress fieldValue)

- WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest Openid(string fieldValue)

- WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest OrderPath(string fieldValue)

- WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest GoodsList(List<WechatWxOpenWeixinExpressReturnGoodsItem> fieldValue)

- WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest OrderPrice(double fieldValue)

- WechatWxOpenWeixinExpressReturnApiAddReturnIdRequest WxPayId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressReturnApiAddReturnIdResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressReturnApiGetReturnIdRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressReturnApiGetReturnIdRequest()

- WechatWxOpenWeixinExpressReturnApiGetReturnIdRequest ReturnId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressReturnApiGetReturnIdResponse (class)

- public string Raw;


## WechatWxOpenWeixinExpressReturnApiUnbindReturnIdRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWeixinExpressReturnApiUnbindReturnIdRequest()

- WechatWxOpenWeixinExpressReturnApiUnbindReturnIdRequest ReturnId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWeixinExpressReturnApiUnbindReturnIdResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppApi (class)

WxApp/WxAppApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWxAppApi(WechatWxOpenClient client)

- async WechatWxOpenWxAppApiMediaCheckResponse MediaCheckAsync(WechatWxOpenWxAppApiMediaCheckRequest request)
  - POST /wxa/media_check_async

- async WechatResponse MediaCheckRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiCheckSessionResponse CheckSessionAsync(WechatWxOpenWxAppApiCheckSessionRequest request)
  - GET /wxa/checksession

- async WechatResponse CheckSessionRawAsync(string query)

- async WechatWxOpenWxAppApiGetMerchantCategoryResponse GetMerchantCategoryAsync()
  - GET /wxa/get_merchant_category

- async WechatResponse GetMerchantCategoryRawAsync(string query)

- async WechatWxOpenWxAppApiNearbyapplycategoryResponse NearbyapplycategoryAsync(WechatWxOpenWxAppApiNearbyapplycategoryRequest request)
  - POST /wxa/nearbyapplycategory

- async WechatResponse NearbyapplycategoryRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiGetStoreWxaAttrResponse GetStoreWxaAttrAsync()
  - GET /wxa/getstorewxaattr

- async WechatResponse GetStoreWxaAttrRawAsync(string query)

- async WechatWxOpenWxAppApiGetNearbyOfficialServiceInfoResponse GetNearbyOfficialServiceInfoAsync()
  - GET /wxa/getnearbyofficialserviceinfo

- async WechatResponse GetNearbyOfficialServiceInfoRawAsync(string query)

- async WechatWxOpenWxAppApiGetDistrictResponse GetDistrictAsync()
  - GET /wxa/get_district

- async WechatResponse GetDistrictRawAsync(string query)

- async WechatWxOpenWxAppApiSearchMapPoiResponse SearchMapPoiAsync(WechatWxOpenWxAppApiSearchMapPoiRequest request)
  - POST /wxa/search_map_poi

- async WechatResponse SearchMapPoiRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiCreateMapPoiResponse CreateMapPoiAsync(WechatWxOpenWxAppApiCreateMapPoiRequest request)
  - POST /wxa/create_map_poi

- async WechatResponse CreateMapPoiRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiAddNearbyPoiResponse AddNearbyPoiAsync(WechatWxOpenWxAppApiAddNearbyPoiRequest request)
  - POST /wxa/addnearbypoi

- async WechatResponse AddNearbyPoiRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiGetNearbyPoiListResponse GetNearbyPoiListAsync(WechatWxOpenWxAppApiGetNearbyPoiListRequest request)
  - GET /wxa/getnearbypoilist

- async WechatResponse GetNearbyPoiListRawAsync(string query)

- async WechatWxOpenWxAppApiDelNearbyPoiResponse DelNearbyPoiAsync(WechatWxOpenWxAppApiDelNearbyPoiRequest request)
  - POST /wxa/delnearbypoi

- async WechatResponse DelNearbyPoiRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiSetNearbyPoiListResponse SetNearbyPoiListAsync(WechatWxOpenWxAppApiSetNearbyPoiListRequest request)
  - POST /wxa/setnearbypoishowstatus

- async WechatResponse SetNearbyPoiListRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiGetNearbyDetailPageResponse GetNearbyDetailPageAsync(WechatWxOpenWxAppApiGetNearbyDetailPageRequest request)
  - GET /wxa/getnearbydetailpage

- async WechatResponse GetNearbyDetailPageRawAsync(string query)

- async WechatWxOpenWxAppApiMediaCheckAsyncResponse MediaCheckAsyncAsync(WechatWxOpenWxAppApiMediaCheckAsyncRequest request)
  - POST /wxa/media_check_async

- async WechatResponse MediaCheckAsyncRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiMsgSecCheckResponse MsgSecCheckAsync(WechatWxOpenWxAppApiMsgSecCheckRequest request)
  - POST /wxa/msg_sec_check

- async WechatResponse MsgSecCheckRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiApplyPluginResponse ApplyPluginAsync(WechatWxOpenWxAppApiApplyPluginRequest request)
  - POST /wxa/plugin

- async WechatResponse ApplyPluginRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiDevPluginResponse DevPluginAsync(WechatWxOpenWxAppApiDevPluginRequest request)
  - POST /wxa/devplugin

- async WechatResponse DevPluginRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiSetDevPluginApplyStatusResponse SetDevPluginApplyStatusAsync(WechatWxOpenWxAppApiSetDevPluginApplyStatusRequest request)
  - POST /wxa/devplugin

- async WechatResponse SetDevPluginApplyStatusRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiGetPluginListResponse GetPluginListAsync()
  - POST /wxa/plugin

- async WechatResponse GetPluginListRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiUnbindPluginResponse UnbindPluginAsync(WechatWxOpenWxAppApiUnbindPluginRequest request)
  - POST /wxa/plugin

- async WechatResponse UnbindPluginRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppApiGetPaidUnionidResponse GetPaidUnionidAsync(WechatWxOpenWxAppApiGetPaidUnionidRequest request)
  - GET /wxa/getpaidunionid

- async WechatResponse GetPaidUnionidRawAsync(string query)

- async WechatWxOpenWxAppApiGetUserRiskResponse GetUserRiskAsync(WechatWxOpenWxAppApiGetUserRiskRequest request)
  - POST /wxa/getuserriskrank

- async WechatResponse GetUserRiskRawAsync(string query, string jsonBody)


## WechatWxOpenWxAppApiAddNearbyPoiRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiAddNearbyPoiRequest()

- WechatWxOpenWxAppApiAddNearbyPoiRequest PicList(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest ServiceInfos(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest StoreName(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest Hour(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest Credential(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest Address(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest CompanyName(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest ContractPhone(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest QualificationList(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest KfInfo(string fieldValue)

- WechatWxOpenWxAppApiAddNearbyPoiRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiAddNearbyPoiResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiApplyPluginRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiApplyPluginRequest()

- WechatWxOpenWxAppApiApplyPluginRequest PluginAppid(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiApplyPluginResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppApiCheckSessionRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiCheckSessionRequest()

- WechatWxOpenWxAppApiCheckSessionRequest OpenId(string fieldValue)

- WechatWxOpenWxAppApiCheckSessionRequest SessionKey(string fieldValue)

- WechatWxOpenWxAppApiCheckSessionRequest Buffer(string fieldValue)

- WechatWxOpenWxAppApiCheckSessionRequest SigMethod(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiCheckSessionResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppApiCreateMapPoiRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiCreateMapPoiRequest()

- WechatWxOpenWxAppApiCreateMapPoiRequest Name(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest Longitude(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest Latitude(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest Province(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest City(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest District(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest Address(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest Category(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest Telephone(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest Photo(List<string> fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest License(List<string> fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest Introduct(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest Districtid(string fieldValue)

- WechatWxOpenWxAppApiCreateMapPoiRequest MpId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiCreateMapPoiResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiDelNearbyPoiRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiDelNearbyPoiRequest()

- WechatWxOpenWxAppApiDelNearbyPoiRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiDelNearbyPoiResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppApiDevPluginRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiDevPluginRequest()

- WechatWxOpenWxAppApiDevPluginRequest Page(int fieldValue)

- WechatWxOpenWxAppApiDevPluginRequest Num(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiDevPluginResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiGetDistrictResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiGetMerchantCategoryResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiGetNearbyDetailPageRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiGetNearbyDetailPageRequest()

- WechatWxOpenWxAppApiGetNearbyDetailPageRequest PoiId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiGetNearbyDetailPageResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiGetNearbyOfficialServiceInfoResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiGetNearbyPoiListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiGetNearbyPoiListRequest()

- WechatWxOpenWxAppApiGetNearbyPoiListRequest Page(int fieldValue)

- WechatWxOpenWxAppApiGetNearbyPoiListRequest PageRows(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiGetNearbyPoiListResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiGetPaidUnionidRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiGetPaidUnionidRequest()

- WechatWxOpenWxAppApiGetPaidUnionidRequest OpenId(string fieldValue)

- WechatWxOpenWxAppApiGetPaidUnionidRequest TransactionId(string fieldValue)

- WechatWxOpenWxAppApiGetPaidUnionidRequest MchId(string fieldValue)

- WechatWxOpenWxAppApiGetPaidUnionidRequest OutTradeNo(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiGetPaidUnionidResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppApiGetPluginListResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiGetStoreWxaAttrResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiGetUserRiskRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiGetUserRiskRequest()

- WechatWxOpenWxAppApiGetUserRiskRequest Appid(string fieldValue)

- WechatWxOpenWxAppApiGetUserRiskRequest Openid(string fieldValue)

- WechatWxOpenWxAppApiGetUserRiskRequest Scene(int fieldValue)

- WechatWxOpenWxAppApiGetUserRiskRequest MobileNo(string fieldValue)

- WechatWxOpenWxAppApiGetUserRiskRequest ClientIp(string fieldValue)

- WechatWxOpenWxAppApiGetUserRiskRequest EmailAddress(string fieldValue)

- WechatWxOpenWxAppApiGetUserRiskRequest ExtendedInfo(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiGetUserRiskResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiMediaCheckAsyncRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiMediaCheckAsyncRequest()

- WechatWxOpenWxAppApiMediaCheckAsyncRequest MediaUrl(string fieldValue)

- WechatWxOpenWxAppApiMediaCheckAsyncRequest MediaType(string fieldValue)

- WechatWxOpenWxAppApiMediaCheckAsyncRequest Version(int fieldValue)

- WechatWxOpenWxAppApiMediaCheckAsyncRequest OpenId(string fieldValue)

- WechatWxOpenWxAppApiMediaCheckAsyncRequest Scene(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiMediaCheckAsyncResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiMediaCheckRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiMediaCheckRequest()

- WechatWxOpenWxAppApiMediaCheckRequest MediaUrl(string fieldValue)

- WechatWxOpenWxAppApiMediaCheckRequest MediaType(string fieldValue)

- WechatWxOpenWxAppApiMediaCheckRequest Version(int fieldValue)

- WechatWxOpenWxAppApiMediaCheckRequest OpenId(string fieldValue)

- WechatWxOpenWxAppApiMediaCheckRequest Scene(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiMediaCheckResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiMsgSecCheckRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiMsgSecCheckRequest()

- WechatWxOpenWxAppApiMsgSecCheckRequest Content(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiMsgSecCheckResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppApiNearbyapplycategoryRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiNearbyapplycategoryRequest()

- WechatWxOpenWxAppApiNearbyapplycategoryRequest CategoryFirstId(int fieldValue)

- WechatWxOpenWxAppApiNearbyapplycategoryRequest CategorySecondId(int fieldValue)

- WechatWxOpenWxAppApiNearbyapplycategoryRequest MediaList(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiNearbyapplycategoryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppApiSearchMapPoiRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiSearchMapPoiRequest()

- WechatWxOpenWxAppApiSearchMapPoiRequest Districtid(long fieldValue)

- WechatWxOpenWxAppApiSearchMapPoiRequest Keyword(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiSearchMapPoiResponse (class)

- public string Raw;


## WechatWxOpenWxAppApiSetDevPluginApplyStatusRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiSetDevPluginApplyStatusRequest()

- WechatWxOpenWxAppApiSetDevPluginApplyStatusRequest Action(string fieldValue)

- WechatWxOpenWxAppApiSetDevPluginApplyStatusRequest AppId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiSetDevPluginApplyStatusResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppApiSetNearbyPoiListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiSetNearbyPoiListRequest()

- WechatWxOpenWxAppApiSetNearbyPoiListRequest PoiId(string fieldValue)

- WechatWxOpenWxAppApiSetNearbyPoiListRequest Status(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiSetNearbyPoiListResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppApiUnbindPluginRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppApiUnbindPluginRequest()

- WechatWxOpenWxAppApiUnbindPluginRequest AppId(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppApiUnbindPluginResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppBusinessApi (class)

WxApp/Business/BusinessApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWxAppBusinessApi(WechatWxOpenClient client)

- async WechatWxOpenWxAppBusinessApiGetUserPhoneNumberResponse GetUserPhoneNumberAsync(WechatWxOpenWxAppBusinessApiGetUserPhoneNumberRequest request)
  - POST /wxa/business/getuserphonenumber

- async WechatResponse GetUserPhoneNumberRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppBusinessApiUnBindUserAuthinfoResponse UnBindUserAuthinfoAsync(WechatWxOpenWxAppBusinessApiUnBindUserAuthinfoRequest request)
  - POST /wxa/business/unbinduserb2cauthinfo

- async WechatResponse UnBindUserAuthinfoRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppBusinessApiGetPluginOpenPidResponse GetPluginOpenPidAsync(WechatWxOpenWxAppBusinessApiGetPluginOpenPidRequest request)
  - POST /wxa/getpluginopenpid

- async WechatResponse GetPluginOpenPidRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppBusinessApiCheckEncryptedDataResponse CheckEncryptedDataAsync(WechatWxOpenWxAppBusinessApiCheckEncryptedDataRequest request)
  - POST /wxa/business/checkencryptedmsg

- async WechatResponse CheckEncryptedDataRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppBusinessApiGetUserEncryptKeyResponse GetUserEncryptKeyAsync(WechatWxOpenWxAppBusinessApiGetUserEncryptKeyRequest request)
  - GET /wxa/business/getuserencryptkey

- async WechatResponse GetUserEncryptKeyRawAsync(string query)

- async WechatWxOpenWxAppBusinessApiResetUserSessionKeyResponse ResetUserSessionKeyAsync(WechatWxOpenWxAppBusinessApiResetUserSessionKeyRequest request)
  - GET /wxa/resetusersessionkey

- async WechatResponse ResetUserSessionKeyRawAsync(string query)


## WechatWxOpenWxAppBusinessApiCheckEncryptedDataRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppBusinessApiCheckEncryptedDataRequest()

- WechatWxOpenWxAppBusinessApiCheckEncryptedDataRequest EncryptedMsgHash(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppBusinessApiCheckEncryptedDataResponse (class)

- public string Raw;


## WechatWxOpenWxAppBusinessApiGetPluginOpenPidRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppBusinessApiGetPluginOpenPidRequest()

- WechatWxOpenWxAppBusinessApiGetPluginOpenPidRequest Code(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppBusinessApiGetPluginOpenPidResponse (class)

- public string Raw;


## WechatWxOpenWxAppBusinessApiGetUserEncryptKeyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppBusinessApiGetUserEncryptKeyRequest()

- WechatWxOpenWxAppBusinessApiGetUserEncryptKeyRequest OpenId(string fieldValue)

- WechatWxOpenWxAppBusinessApiGetUserEncryptKeyRequest Signature(string fieldValue)

- WechatWxOpenWxAppBusinessApiGetUserEncryptKeyRequest SigMethod(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppBusinessApiGetUserEncryptKeyResponse (class)

- public string Raw;


## WechatWxOpenWxAppBusinessApiGetUserPhoneNumberRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWxAppBusinessApiGetUserPhoneNumberRequest()

- WechatWxOpenWxAppBusinessApiGetUserPhoneNumberRequest Code(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppBusinessApiGetUserPhoneNumberResponse (class)

- public string Raw;


## WechatWxOpenWxAppBusinessApiResetUserSessionKeyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppBusinessApiResetUserSessionKeyRequest()

- WechatWxOpenWxAppBusinessApiResetUserSessionKeyRequest OpenId(string fieldValue)

- WechatWxOpenWxAppBusinessApiResetUserSessionKeyRequest Signature(string fieldValue)

- WechatWxOpenWxAppBusinessApiResetUserSessionKeyRequest SigMethod(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppBusinessApiResetUserSessionKeyResponse (class)

- public string Raw;


## WechatWxOpenWxAppBusinessApiUnBindUserAuthinfoRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppBusinessApiUnBindUserAuthinfoRequest()

- WechatWxOpenWxAppBusinessApiUnBindUserAuthinfoRequest OpenidList(List<string> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppBusinessApiUnBindUserAuthinfoResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppGenerateSchemeApi (class)

WxApp/GenerateSchemeApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWxAppGenerateSchemeApi(WechatWxOpenClient client)

- async WechatWxOpenWxAppGenerateSchemeApiSubmitPagesResponse SubmitPagesAsync(WechatWxOpenWxAppGenerateSchemeApiSubmitPagesRequest request)
  - POST /wxa/search/wxaapi_submitpages

- async WechatResponse SubmitPagesRawAsync(string query, string jsonBody)


## WechatWxOpenWxAppGenerateSchemeApiSubmitPagesRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWxAppGenerateSchemeApiSubmitPagesRequest()

- WechatWxOpenWxAppGenerateSchemeApiSubmitPagesRequest Pages(List<WechatWxOpenPage> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppGenerateSchemeApiSubmitPagesResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppSearchApi (class)

WxApp/SearchApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWxAppSearchApi(WechatWxOpenClient client)

- async WechatWxOpenWxAppSearchApiSubmitPagesResponse SubmitPagesAsync(WechatWxOpenWxAppSearchApiSubmitPagesRequest request)
  - POST /wxa/search/wxaapi_submitpages

- async WechatResponse SubmitPagesRawAsync(string query, string jsonBody)


## WechatWxOpenWxAppSearchApiSubmitPagesRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWxAppSearchApiSubmitPagesRequest()

- WechatWxOpenWxAppSearchApiSubmitPagesRequest Pages(List<WechatWxOpenPage> fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppSearchApiSubmitPagesResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenWxAppShortLinkApi (class)

WxApp/ShortLinkApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWxAppShortLinkApi(WechatWxOpenClient client)

- async WechatWxOpenWxAppShortLinkApiGenerateResponse GenerateAsync(WechatWxOpenWxAppShortLinkApiGenerateRequest request)
  - POST /wxa/genwxashortlink

- async WechatResponse GenerateRawAsync(string query, string jsonBody)


## WechatWxOpenWxAppShortLinkApiGenerateRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWxAppShortLinkApiGenerateRequest()

- WechatWxOpenWxAppShortLinkApiGenerateRequest PageUrl(string fieldValue)

- WechatWxOpenWxAppShortLinkApiGenerateRequest PageTitle(string fieldValue)

- WechatWxOpenWxAppShortLinkApiGenerateRequest IsPermanent(bool fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppShortLinkApiGenerateResponse (class)

- public string Raw;


## WechatWxOpenWxAppUrlLinkApi (class)

WxApp/UrlLinkApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWxAppUrlLinkApi(WechatWxOpenClient client)

- async WechatWxOpenWxAppUrlLinkApiGenerateResponse GenerateAsync(WechatWxOpenWxAppUrlLinkApiGenerateRequest request)
  - POST /wxa/generate_urllink

- async WechatResponse GenerateRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppUrlLinkApiQueryResponse QueryAsync(WechatWxOpenWxAppUrlLinkApiQueryRequest request)
  - POST /wxa/query_urllink

- async WechatResponse QueryRawAsync(string query, string jsonBody)


## WechatWxOpenWxAppUrlLinkApiGenerateRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWxAppUrlLinkApiGenerateRequest()

- WechatWxOpenWxAppUrlLinkApiGenerateRequest Env(string fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest Domain(string fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest Path(string fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest Query(string fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest ResourceAppid(string fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest IsShowSafetyNotice(bool fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest SafeUrl(string fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest EnvVersion(string fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest IsExpire(bool fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest ExpireType(int fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest ExpireTime(long fieldValue)

- WechatWxOpenWxAppUrlLinkApiGenerateRequest ExpireInterval(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppUrlLinkApiGenerateResponse (class)

- public string Raw;


## WechatWxOpenWxAppUrlLinkApiQueryRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppUrlLinkApiQueryRequest()

- WechatWxOpenWxAppUrlLinkApiQueryRequest UrlLink(string fieldValue)

- WechatWxOpenWxAppUrlLinkApiQueryRequest QueryType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppUrlLinkApiQueryResponse (class)

- public string Raw;


## WechatWxOpenWxAppUrlSchemeApi (class)

WxApp/UrlScheme/UrlSchemeApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenWxAppUrlSchemeApi(WechatWxOpenClient client)

- async WechatWxOpenWxAppUrlSchemeApiGenerateSchemeResponse GenerateSchemeAsync(WechatWxOpenWxAppUrlSchemeApiGenerateSchemeRequest request)
  - POST /wxa/generatescheme

- async WechatResponse GenerateSchemeRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeResponse GenerateNFCSchemeAsync(WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeRequest request)
  - POST /wxa/generatenfcscheme

- async WechatResponse GenerateNFCSchemeRawAsync(string query, string jsonBody)

- async WechatWxOpenWxAppUrlSchemeApiQuerySchemeResponse QuerySchemeAsync(WechatWxOpenWxAppUrlSchemeApiQuerySchemeRequest request)
  - POST /wxa/queryscheme

- async WechatResponse QuerySchemeRawAsync(string query, string jsonBody)


## WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeRequest()

- WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeRequest Path(string fieldValue)

- WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeRequest Query(string fieldValue)

- WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeRequest EnvVersion(string fieldValue)

- WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeRequest ModelId(string fieldValue)

- WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeRequest Sn(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppUrlSchemeApiGenerateNFCSchemeResponse (class)

- public string Raw;


## WechatWxOpenWxAppUrlSchemeApiGenerateSchemeRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenWxAppUrlSchemeApiGenerateSchemeRequest()

- WechatWxOpenWxAppUrlSchemeApiGenerateSchemeRequest Path(string fieldValue)

- WechatWxOpenWxAppUrlSchemeApiGenerateSchemeRequest Query(string fieldValue)

- WechatWxOpenWxAppUrlSchemeApiGenerateSchemeRequest EnvVersion(string fieldValue)

- WechatWxOpenWxAppUrlSchemeApiGenerateSchemeRequest IsExpire(bool fieldValue)

- WechatWxOpenWxAppUrlSchemeApiGenerateSchemeRequest ExpireTime(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppUrlSchemeApiGenerateSchemeResponse (class)

- public string Raw;


## WechatWxOpenWxAppUrlSchemeApiQuerySchemeRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenWxAppUrlSchemeApiQuerySchemeRequest()

- WechatWxOpenWxAppUrlSchemeApiQuerySchemeRequest Scheme(string fieldValue)

- WechatWxOpenWxAppUrlSchemeApiQuerySchemeRequest QueryType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenWxAppUrlSchemeApiQuerySchemeResponse (class)

- public string Raw;


## WechatWxOpenXPayApi (class)

XPay/XPayApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenXPayApi(WechatWxOpenClient client)

- async WechatWxOpenXPayApiQueryUserBalanceResponse QueryUserBalanceAsync(WechatWxOpenXPayApiQueryUserBalanceRequest request)
  - POST /xpay/query_user_balance

- async WechatResponse QueryUserBalanceRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiCurrencyPayResponse CurrencyPayAsync(WechatWxOpenXPayApiCurrencyPayRequest request)
  - POST /xpay/currency_pay

- async WechatResponse CurrencyPayRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryOrderResponse QueryOrderAsync(WechatWxOpenXPayApiQueryOrderRequest request)
  - POST /xpay/query_order

- async WechatResponse QueryOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiCancelCurrencyPayResponse CancelCurrencyPayAsync(WechatWxOpenXPayApiCancelCurrencyPayRequest request)
  - POST /xpay/cancel_currency_pay

- async WechatResponse CancelCurrencyPayRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiNotifyProvideGoodsResponse NotifyProvideGoodsAsync(WechatWxOpenXPayApiNotifyProvideGoodsRequest request)
  - POST /xpay/notify_provide_goods

- async WechatResponse NotifyProvideGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiPresentCurrencyResponse PresentCurrencyAsync(WechatWxOpenXPayApiPresentCurrencyRequest request)
  - POST /xpay/present_currency

- async WechatResponse PresentCurrencyRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiDownloadBillResponse DownloadBillAsync(WechatWxOpenXPayApiDownloadBillRequest request)
  - POST /xpay/download_bill

- async WechatResponse DownloadBillRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiRefundOrderResponse RefundOrderAsync(WechatWxOpenXPayApiRefundOrderRequest request)
  - POST /xpay/refund_order

- async WechatResponse RefundOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiCreateWithdrawOrderResponse CreateWithdrawOrderAsync(WechatWxOpenXPayApiCreateWithdrawOrderRequest request)
  - POST /xpay/create_withdraw_order

- async WechatResponse CreateWithdrawOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryWithdrawOrderResponse QueryWithdrawOrderAsync(WechatWxOpenXPayApiQueryWithdrawOrderRequest request)
  - POST /xpay/query_withdraw_order

- async WechatResponse QueryWithdrawOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiStartUploadGoodsResponse StartUploadGoodsAsync(WechatWxOpenXPayApiStartUploadGoodsRequest request)
  - POST /xpay/start_upload_goods

- async WechatResponse StartUploadGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryUploadGoodsResponse QueryUploadGoodsAsync(WechatWxOpenXPayApiQueryUploadGoodsRequest request)
  - POST /xpay/query_upload_goods

- async WechatResponse QueryUploadGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiStartPublishGoodsResponse StartPublishGoodsAsync(WechatWxOpenXPayApiStartPublishGoodsRequest request)
  - POST /xpay/start_publish_goods

- async WechatResponse StartPublishGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryPublishGoodsResponse QueryPublishGoodsAsync(WechatWxOpenXPayApiQueryPublishGoodsRequest request)
  - POST /xpay/query_publish_goods

- async WechatResponse QueryPublishGoodsRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryBizBalanceResponse QueryBizBalanceAsync(WechatWxOpenXPayApiQueryBizBalanceRequest request)
  - POST /xpay/query_biz_balance

- async WechatResponse QueryBizBalanceRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryTransferAccountResponse QueryTransferAccountAsync(WechatWxOpenXPayApiQueryTransferAccountRequest request)
  - POST /xpay/query_transfer_account

- async WechatResponse QueryTransferAccountRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryAdverFundsResponse QueryAdverFundsAsync(WechatWxOpenXPayApiQueryAdverFundsRequest request)
  - POST /xpay/query_adver_funds

- async WechatResponse QueryAdverFundsRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiCreateFundsBillResponse CreateFundsBillAsync(WechatWxOpenXPayApiCreateFundsBillRequest request)
  - POST /xpay/create_funds_bill

- async WechatResponse CreateFundsBillRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiBindTransferAccoutResponse BindTransferAccoutAsync(WechatWxOpenXPayApiBindTransferAccoutRequest request)
  - POST /xpay/bind_transfer_accout

- async WechatResponse BindTransferAccoutRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryFundsBillResponse QueryFundsBillAsync(WechatWxOpenXPayApiQueryFundsBillRequest request)
  - POST /xpay/query_funds_bill

- async WechatResponse QueryFundsBillRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryRecoverBillResponse QueryRecoverBillAsync(WechatWxOpenXPayApiQueryRecoverBillRequest request)
  - POST /xpay/query_recover_bill

- async WechatResponse QueryRecoverBillRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiGetComplaintListResponse GetComplaintListAsync(WechatWxOpenXPayApiGetComplaintListRequest request)
  - POST /xpay/get_complaint_list

- async WechatResponse GetComplaintListRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiGetComplaintDetailResponse GetComplaintDetailAsync(WechatWxOpenXPayApiGetComplaintDetailRequest request)
  - POST /xpay/get_complaint_detail

- async WechatResponse GetComplaintDetailRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiGetNegotiationHistoryResponse GetNegotiationHistoryAsync(WechatWxOpenXPayApiGetNegotiationHistoryRequest request)
  - POST /xpay/get_negotiation_history

- async WechatResponse GetNegotiationHistoryRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiResponseComplaintResponse ResponseComplaintAsync(WechatWxOpenXPayApiResponseComplaintRequest request)
  - POST /xpay/response_complaint

- async WechatResponse ResponseComplaintRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiCompleteComplaintResponse CompleteComplaintAsync(WechatWxOpenXPayApiCompleteComplaintRequest request)
  - POST /xpay/complete_complaint

- async WechatResponse CompleteComplaintRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiUploadVpFileResponse UploadVpFileAsync(WechatWxOpenXPayApiUploadVpFileRequest request)
  - POST /xpay/upload_vp_file

- async WechatResponse UploadVpFileRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiGetUploadFileSignResponse GetUploadFileSignAsync(WechatWxOpenXPayApiGetUploadFileSignRequest request)
  - POST /xpay/get_upload_file_sign

- async WechatResponse GetUploadFileSignRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiDownloadAdverfundsOrderResponse DownloadAdverfundsOrderAsync(WechatWxOpenXPayApiDownloadAdverfundsOrderRequest request)
  - POST /xpay/download_adverfunds_order

- async WechatResponse DownloadAdverfundsOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQuerySubscribeContractResponse QuerySubscribeContractAsync(WechatWxOpenXPayApiQuerySubscribeContractRequest request)
  - POST /xpay/query_subscribe_contract

- async WechatResponse QuerySubscribeContractRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiSendSubscribePrePaymentResponse SendSubscribePrePaymentAsync(WechatWxOpenXPayApiSendSubscribePrePaymentRequest request)
  - POST /xpay/send_subscribe_pre_payment

- async WechatResponse SendSubscribePrePaymentRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiSubmitSubscribePayOrderResponse SubmitSubscribePayOrderAsync(WechatWxOpenXPayApiSubmitSubscribePayOrderRequest request)
  - POST /xpay/submit_subscribe_pay_order

- async WechatResponse SubmitSubscribePayOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiCancelSubscribeContractResponse CancelSubscribeContractAsync(WechatWxOpenXPayApiCancelSubscribeContractRequest request)
  - POST /xpay/cancel_subscribe_contract

- async WechatResponse CancelSubscribeContractRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiStartDownloadOrderResponse StartDownloadOrderAsync(WechatWxOpenXPayApiStartDownloadOrderRequest request)
  - POST /xpay/start_download_order

- async WechatResponse StartDownloadOrderRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayApiQueryDownloadOrderResponse QueryDownloadOrderAsync(WechatWxOpenXPayApiQueryDownloadOrderRequest request)
  - POST /xpay/query_download_order

- async WechatResponse QueryDownloadOrderRawAsync(string query, string jsonBody)


## WechatWxOpenXPayApiBindTransferAccoutRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiBindTransferAccoutRequest()

- WechatWxOpenXPayApiBindTransferAccoutRequest TransferAccountUid(long fieldValue)

- WechatWxOpenXPayApiBindTransferAccoutRequest TransferAccountOrgName(string fieldValue)

- WechatWxOpenXPayApiBindTransferAccoutRequest Env(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiBindTransferAccoutResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenXPayApiCancelCurrencyPayRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiCancelCurrencyPayRequest()

- WechatWxOpenXPayApiCancelCurrencyPayRequest Openid(string fieldValue)

- WechatWxOpenXPayApiCancelCurrencyPayRequest Env(int fieldValue)

- WechatWxOpenXPayApiCancelCurrencyPayRequest UserIp(string fieldValue)

- WechatWxOpenXPayApiCancelCurrencyPayRequest PayOrderId(string fieldValue)

- WechatWxOpenXPayApiCancelCurrencyPayRequest OrderId(string fieldValue)

- WechatWxOpenXPayApiCancelCurrencyPayRequest Amount(int fieldValue)

- WechatWxOpenXPayApiCancelCurrencyPayRequest PaySig(string fieldValue)

- WechatWxOpenXPayApiCancelCurrencyPayRequest Signature(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiCancelCurrencyPayResponse (class)

- public string Raw;


## WechatWxOpenXPayApiCancelSubscribeContractRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiCancelSubscribeContractRequest()

- WechatWxOpenXPayApiCancelSubscribeContractRequest Openid(string fieldValue)

- WechatWxOpenXPayApiCancelSubscribeContractRequest TerminationReason(string fieldValue)

- WechatWxOpenXPayApiCancelSubscribeContractRequest ProductId(string fieldValue)

- WechatWxOpenXPayApiCancelSubscribeContractRequest OutContractCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiCancelSubscribeContractResponse (class)

- public string Raw;


## WechatWxOpenXPayApiCompleteComplaintRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiCompleteComplaintRequest()

- WechatWxOpenXPayApiCompleteComplaintRequest ComplaintId(string fieldValue)

- WechatWxOpenXPayApiCompleteComplaintRequest Env(int fieldValue)

- WechatWxOpenXPayApiCompleteComplaintRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiCompleteComplaintResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenXPayApiCreateFundsBillRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiCreateFundsBillRequest()

- WechatWxOpenXPayApiCreateFundsBillRequest TransferAmount(int fieldValue)

- WechatWxOpenXPayApiCreateFundsBillRequest TransferAccountUid(long fieldValue)

- WechatWxOpenXPayApiCreateFundsBillRequest TransferAccountName(string fieldValue)

- WechatWxOpenXPayApiCreateFundsBillRequest TransferAccountAgencyId(long fieldValue)

- WechatWxOpenXPayApiCreateFundsBillRequest RequestId(string fieldValue)

- WechatWxOpenXPayApiCreateFundsBillRequest SettleBegin(long fieldValue)

- WechatWxOpenXPayApiCreateFundsBillRequest SettleEnd(long fieldValue)

- WechatWxOpenXPayApiCreateFundsBillRequest Env(int fieldValue)

- WechatWxOpenXPayApiCreateFundsBillRequest AuthorizeAdvertise(int fieldValue)

- WechatWxOpenXPayApiCreateFundsBillRequest FundType(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiCreateFundsBillResponse (class)

- public string Raw;


## WechatWxOpenXPayApiCreateWithdrawOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiCreateWithdrawOrderRequest()

- WechatWxOpenXPayApiCreateWithdrawOrderRequest WithdrawNo(string fieldValue)

- WechatWxOpenXPayApiCreateWithdrawOrderRequest WithdrawAmount(string fieldValue)

- WechatWxOpenXPayApiCreateWithdrawOrderRequest Env(int fieldValue)

- WechatWxOpenXPayApiCreateWithdrawOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiCreateWithdrawOrderResponse (class)

- public string Raw;


## WechatWxOpenXPayApiCurrencyPayRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiCurrencyPayRequest()

- WechatWxOpenXPayApiCurrencyPayRequest Openid(string fieldValue)

- WechatWxOpenXPayApiCurrencyPayRequest Env(int fieldValue)

- WechatWxOpenXPayApiCurrencyPayRequest UserIp(string fieldValue)

- WechatWxOpenXPayApiCurrencyPayRequest Amount(int fieldValue)

- WechatWxOpenXPayApiCurrencyPayRequest OrderId(string fieldValue)

- WechatWxOpenXPayApiCurrencyPayRequest Payitem(string fieldValue)

- WechatWxOpenXPayApiCurrencyPayRequest Remark(string fieldValue)

- WechatWxOpenXPayApiCurrencyPayRequest PaySig(string fieldValue)

- WechatWxOpenXPayApiCurrencyPayRequest Signature(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiCurrencyPayResponse (class)

- public string Raw;


## WechatWxOpenXPayApiDownloadAdverfundsOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiDownloadAdverfundsOrderRequest()

- WechatWxOpenXPayApiDownloadAdverfundsOrderRequest FundId(string fieldValue)

- WechatWxOpenXPayApiDownloadAdverfundsOrderRequest Env(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiDownloadAdverfundsOrderResponse (class)

- public string Raw;


## WechatWxOpenXPayApiDownloadBillRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiDownloadBillRequest()

- WechatWxOpenXPayApiDownloadBillRequest BeginDs(long fieldValue)

- WechatWxOpenXPayApiDownloadBillRequest EndDs(long fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiDownloadBillResponse (class)

- public string Raw;


## WechatWxOpenXPayApiGetComplaintDetailRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiGetComplaintDetailRequest()

- WechatWxOpenXPayApiGetComplaintDetailRequest ComplaintId(string fieldValue)

- WechatWxOpenXPayApiGetComplaintDetailRequest Env(int fieldValue)

- WechatWxOpenXPayApiGetComplaintDetailRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiGetComplaintDetailResponse (class)

- public string Raw;


## WechatWxOpenXPayApiGetComplaintListRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiGetComplaintListRequest()

- WechatWxOpenXPayApiGetComplaintListRequest Offset(int fieldValue)

- WechatWxOpenXPayApiGetComplaintListRequest Limit(int fieldValue)

- WechatWxOpenXPayApiGetComplaintListRequest BeginDate(string fieldValue)

- WechatWxOpenXPayApiGetComplaintListRequest EndDate(string fieldValue)

- WechatWxOpenXPayApiGetComplaintListRequest Env(int fieldValue)

- WechatWxOpenXPayApiGetComplaintListRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiGetComplaintListResponse (class)

- public string Raw;


## WechatWxOpenXPayApiGetNegotiationHistoryRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiGetNegotiationHistoryRequest()

- WechatWxOpenXPayApiGetNegotiationHistoryRequest Offset(int fieldValue)

- WechatWxOpenXPayApiGetNegotiationHistoryRequest Limit(int fieldValue)

- WechatWxOpenXPayApiGetNegotiationHistoryRequest ComplaintId(string fieldValue)

- WechatWxOpenXPayApiGetNegotiationHistoryRequest Env(int fieldValue)

- WechatWxOpenXPayApiGetNegotiationHistoryRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiGetNegotiationHistoryResponse (class)

- public string Raw;


## WechatWxOpenXPayApiGetUploadFileSignRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiGetUploadFileSignRequest()

- WechatWxOpenXPayApiGetUploadFileSignRequest WxpayUrl(string fieldValue)

- WechatWxOpenXPayApiGetUploadFileSignRequest ConvertCos(bool fieldValue)

- WechatWxOpenXPayApiGetUploadFileSignRequest ComplaintId(string fieldValue)

- WechatWxOpenXPayApiGetUploadFileSignRequest Env(int fieldValue)

- WechatWxOpenXPayApiGetUploadFileSignRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiGetUploadFileSignResponse (class)

- public string Raw;


## WechatWxOpenXPayApiNotifyProvideGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiNotifyProvideGoodsRequest()

- WechatWxOpenXPayApiNotifyProvideGoodsRequest OrderId(string fieldValue)

- WechatWxOpenXPayApiNotifyProvideGoodsRequest WxOrderId(string fieldValue)

- WechatWxOpenXPayApiNotifyProvideGoodsRequest Env(int fieldValue)

- WechatWxOpenXPayApiNotifyProvideGoodsRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiNotifyProvideGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenXPayApiPresentCurrencyRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiPresentCurrencyRequest()

- WechatWxOpenXPayApiPresentCurrencyRequest Openid(string fieldValue)

- WechatWxOpenXPayApiPresentCurrencyRequest Env(int fieldValue)

- WechatWxOpenXPayApiPresentCurrencyRequest OrderId(string fieldValue)

- WechatWxOpenXPayApiPresentCurrencyRequest Amount(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiPresentCurrencyResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryAdverFundsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryAdverFundsRequest()

- WechatWxOpenXPayApiQueryAdverFundsRequest Page(int fieldValue)

- WechatWxOpenXPayApiQueryAdverFundsRequest PageSize(int fieldValue)

- WechatWxOpenXPayApiQueryAdverFundsRequest Filter(WechatWxOpenQueryAdverFundsFilter fieldValue)

- WechatWxOpenXPayApiQueryAdverFundsRequest Env(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryAdverFundsResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryBizBalanceRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryBizBalanceRequest()

- WechatWxOpenXPayApiQueryBizBalanceRequest Env(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryBizBalanceResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryDownloadOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryDownloadOrderRequest()

- WechatWxOpenXPayApiQueryDownloadOrderRequest TaskId(string fieldValue)

- WechatWxOpenXPayApiQueryDownloadOrderRequest Env(int fieldValue)

- WechatWxOpenXPayApiQueryDownloadOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryDownloadOrderResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryFundsBillRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryFundsBillRequest()

- WechatWxOpenXPayApiQueryFundsBillRequest Page(int fieldValue)

- WechatWxOpenXPayApiQueryFundsBillRequest PageSize(int fieldValue)

- WechatWxOpenXPayApiQueryFundsBillRequest Filter(WechatWxOpenQueryAdverFundsFilter fieldValue)

- WechatWxOpenXPayApiQueryFundsBillRequest Env(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryFundsBillResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryOrderRequest()

- WechatWxOpenXPayApiQueryOrderRequest Openid(string fieldValue)

- WechatWxOpenXPayApiQueryOrderRequest Env(int fieldValue)

- WechatWxOpenXPayApiQueryOrderRequest OrderId(string fieldValue)

- WechatWxOpenXPayApiQueryOrderRequest WxOrderId(string fieldValue)

- WechatWxOpenXPayApiQueryOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryOrderResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryPublishGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryPublishGoodsRequest()

- WechatWxOpenXPayApiQueryPublishGoodsRequest Env(int fieldValue)

- WechatWxOpenXPayApiQueryPublishGoodsRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryPublishGoodsResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryRecoverBillRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryRecoverBillRequest()

- WechatWxOpenXPayApiQueryRecoverBillRequest Page(int fieldValue)

- WechatWxOpenXPayApiQueryRecoverBillRequest PageSize(int fieldValue)

- WechatWxOpenXPayApiQueryRecoverBillRequest Filter(WechatWxOpenQueryRecoverBillFilter fieldValue)

- WechatWxOpenXPayApiQueryRecoverBillRequest Env(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryRecoverBillResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQuerySubscribeContractRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQuerySubscribeContractRequest()

- WechatWxOpenXPayApiQuerySubscribeContractRequest Openid(string fieldValue)

- WechatWxOpenXPayApiQuerySubscribeContractRequest ProductId(string fieldValue)

- WechatWxOpenXPayApiQuerySubscribeContractRequest OutContractCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQuerySubscribeContractResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryTransferAccountRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryTransferAccountRequest()

- WechatWxOpenXPayApiQueryTransferAccountRequest Env(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryTransferAccountResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryUploadGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryUploadGoodsRequest()

- WechatWxOpenXPayApiQueryUploadGoodsRequest Env(int fieldValue)

- WechatWxOpenXPayApiQueryUploadGoodsRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryUploadGoodsResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryUserBalanceRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryUserBalanceRequest()

- WechatWxOpenXPayApiQueryUserBalanceRequest Openid(string fieldValue)

- WechatWxOpenXPayApiQueryUserBalanceRequest Env(int fieldValue)

- WechatWxOpenXPayApiQueryUserBalanceRequest UserIp(string fieldValue)

- WechatWxOpenXPayApiQueryUserBalanceRequest PaySig(string fieldValue)

- WechatWxOpenXPayApiQueryUserBalanceRequest Signature(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryUserBalanceResponse (class)

- public string Raw;


## WechatWxOpenXPayApiQueryWithdrawOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiQueryWithdrawOrderRequest()

- WechatWxOpenXPayApiQueryWithdrawOrderRequest WithdrawNo(string fieldValue)

- WechatWxOpenXPayApiQueryWithdrawOrderRequest Env(int fieldValue)

- WechatWxOpenXPayApiQueryWithdrawOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiQueryWithdrawOrderResponse (class)

- public string Raw;


## WechatWxOpenXPayApiRefundOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiRefundOrderRequest()

- WechatWxOpenXPayApiRefundOrderRequest Openid(string fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest OrderId(string fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest WxOrderId(string fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest RefundOrderId(string fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest LeftFee(int fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest RefundFee(int fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest BizMeta(string fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest RefundReason(string fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest ReqFrom(string fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest Env(int fieldValue)

- WechatWxOpenXPayApiRefundOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiRefundOrderResponse (class)

- public string Raw;


## WechatWxOpenXPayApiResponseComplaintRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiResponseComplaintRequest()

- WechatWxOpenXPayApiResponseComplaintRequest ComplaintId(string fieldValue)

- WechatWxOpenXPayApiResponseComplaintRequest ResponseContent(string fieldValue)

- WechatWxOpenXPayApiResponseComplaintRequest ResponseImages(List<string> fieldValue)

- WechatWxOpenXPayApiResponseComplaintRequest Env(int fieldValue)

- WechatWxOpenXPayApiResponseComplaintRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiResponseComplaintResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenXPayApiSendSubscribePrePaymentRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiSendSubscribePrePaymentRequest()

- WechatWxOpenXPayApiSendSubscribePrePaymentRequest Openid(string fieldValue)

- WechatWxOpenXPayApiSendSubscribePrePaymentRequest DeductPrice(int fieldValue)

- WechatWxOpenXPayApiSendSubscribePrePaymentRequest ProductId(string fieldValue)

- WechatWxOpenXPayApiSendSubscribePrePaymentRequest OutContractCode(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiSendSubscribePrePaymentResponse (class)

- public string Raw;


## WechatWxOpenXPayApiStartDownloadOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiStartDownloadOrderRequest()

- WechatWxOpenXPayApiStartDownloadOrderRequest BeginDs(long fieldValue)

- WechatWxOpenXPayApiStartDownloadOrderRequest EndDs(long fieldValue)

- WechatWxOpenXPayApiStartDownloadOrderRequest OrderType(int fieldValue)

- WechatWxOpenXPayApiStartDownloadOrderRequest OrderInfo(string fieldValue)

- WechatWxOpenXPayApiStartDownloadOrderRequest IsProvided(bool fieldValue)

- WechatWxOpenXPayApiStartDownloadOrderRequest RefundStatus(int fieldValue)

- WechatWxOpenXPayApiStartDownloadOrderRequest Env(int fieldValue)

- WechatWxOpenXPayApiStartDownloadOrderRequest PayChannel(int fieldValue)

- WechatWxOpenXPayApiStartDownloadOrderRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiStartDownloadOrderResponse (class)

- public string Raw;


## WechatWxOpenXPayApiStartPublishGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiStartPublishGoodsRequest()

- WechatWxOpenXPayApiStartPublishGoodsRequest PublishItem(List<WechatWxOpenStartPublishGoodsItem> fieldValue)

- WechatWxOpenXPayApiStartPublishGoodsRequest Env(int fieldValue)

- WechatWxOpenXPayApiStartPublishGoodsRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiStartPublishGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenXPayApiStartUploadGoodsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiStartUploadGoodsRequest()

- WechatWxOpenXPayApiStartUploadGoodsRequest UploadItem(List<WechatWxOpenStartUploadGoodsItem> fieldValue)

- WechatWxOpenXPayApiStartUploadGoodsRequest Env(int fieldValue)

- WechatWxOpenXPayApiStartUploadGoodsRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiStartUploadGoodsResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenXPayApiSubmitSubscribePayOrderRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiSubmitSubscribePayOrderRequest()

- WechatWxOpenXPayApiSubmitSubscribePayOrderRequest Openid(string fieldValue)

- WechatWxOpenXPayApiSubmitSubscribePayOrderRequest OfferId(string fieldValue)

- WechatWxOpenXPayApiSubmitSubscribePayOrderRequest BuyQuantity(string fieldValue)

- WechatWxOpenXPayApiSubmitSubscribePayOrderRequest Env(int fieldValue)

- WechatWxOpenXPayApiSubmitSubscribePayOrderRequest CurrencyType(string fieldValue)

- WechatWxOpenXPayApiSubmitSubscribePayOrderRequest ProductId(string fieldValue)

- WechatWxOpenXPayApiSubmitSubscribePayOrderRequest DeductPrice(int fieldValue)

- WechatWxOpenXPayApiSubmitSubscribePayOrderRequest OrderId(string fieldValue)

- WechatWxOpenXPayApiSubmitSubscribePayOrderRequest Attach(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiSubmitSubscribePayOrderResponse (class)

- public string Raw;


## WechatWxOpenXPayApiUploadVpFileRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayApiUploadVpFileRequest()

- WechatWxOpenXPayApiUploadVpFileRequest Base64Img(string fieldValue)

- WechatWxOpenXPayApiUploadVpFileRequest ImgUrl(string fieldValue)

- WechatWxOpenXPayApiUploadVpFileRequest FileName(string fieldValue)

- WechatWxOpenXPayApiUploadVpFileRequest Env(int fieldValue)

- WechatWxOpenXPayApiUploadVpFileRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayApiUploadVpFileResponse (class)

- public string Raw;


## WechatWxOpenXPayIncrementApi (class)

XPay/XPayIncrementApi.cs 的 Zan 强类型接口。
业务参数由请求对象自动序列化，凭证由客户端配置和缓存自动补充。
由 scripts/generate_wechat_product_apis.py 从 Senparc 微信产品 API 生成。

- WechatWxOpenClient client;

- public WechatWxOpenXPayIncrementApi(WechatWxOpenClient client)

- async WechatWxOpenXPayIncrementApiBindTransferAccountResponse BindTransferAccountAsync(WechatWxOpenXPayIncrementApiBindTransferAccountRequest request)
  - POST /xpay/bind_transfer_accout

- async WechatResponse BindTransferAccountRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayIncrementApiDownloadIosSettlementBillResponse DownloadIosSettlementBillAsync(WechatWxOpenXPayIncrementApiDownloadIosSettlementBillRequest request)
  - POST /xpay/download_ios_settlement_bill

- async WechatResponse DownloadIosSettlementBillRawAsync(string query, string jsonBody)

- async WechatWxOpenXPayIncrementApiQueryPunishmentReasonsResponse QueryPunishmentReasonsAsync(WechatWxOpenXPayIncrementApiQueryPunishmentReasonsRequest request)
  - POST /xpay/query_punishment_reasons

- async WechatResponse QueryPunishmentReasonsRawAsync(string query, string jsonBody)


## WechatWxOpenXPayIncrementApiBindTransferAccountRequest (class)

方法请求/响应契约；可复用的 DTO 实体位于 Sdk.Wechat.Models.WxOpen。

- WechatTypedRequest request;

- public WechatWxOpenXPayIncrementApiBindTransferAccountRequest()

- WechatWxOpenXPayIncrementApiBindTransferAccountRequest TransferAccountUid(long fieldValue)

- WechatWxOpenXPayIncrementApiBindTransferAccountRequest TransferAccountOrgName(string fieldValue)

- WechatWxOpenXPayIncrementApiBindTransferAccountRequest Env(int fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayIncrementApiBindTransferAccountResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatWxOpenXPayIncrementApiDownloadIosSettlementBillRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayIncrementApiDownloadIosSettlementBillRequest()

- WechatWxOpenXPayIncrementApiDownloadIosSettlementBillRequest StartMonth(string fieldValue)

- WechatWxOpenXPayIncrementApiDownloadIosSettlementBillRequest EndMonth(string fieldValue)

- WechatWxOpenXPayIncrementApiDownloadIosSettlementBillRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayIncrementApiDownloadIosSettlementBillResponse (class)

- public string Raw;


## WechatWxOpenXPayIncrementApiQueryPunishmentReasonsRequest (class)

- WechatTypedRequest request;

- public WechatWxOpenXPayIncrementApiQueryPunishmentReasonsRequest()

- WechatWxOpenXPayIncrementApiQueryPunishmentReasonsRequest PaySig(string fieldValue)

- string QueryText()

- string JsonBody()


## WechatWxOpenXPayIncrementApiQueryPunishmentReasonsResponse (class)

- public string Raw;
