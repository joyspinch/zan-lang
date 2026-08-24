# Sdk.Wechat.TenPay

> 源码: `stdlib/Sdk/Wechat/TenPay/WechatPayConfig.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayLegacyMpClient.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayLegacySpecialApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayLegacyTenPayApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayLegacyTenPayRightsApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayLegacyTenPayV3Api.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayLegacyTenpayV3PayBankApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV2.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3Apply4SubApply4SubApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3Apply4SubApply4SubApisCurrentApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3Apply4SubjectApply4SubjectApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3Apply4SubjectApply4SubjectApisCurrentApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3BankComponentBankComponentApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3BasePayBasePayApisAbnormalRefundApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3BasePayBasePayApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3BrandApplymentBrandApplymentApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3BrandCardBrandCardApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3BrandMemberCardBrandMemberCardApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3BrandStoreBrandStoreApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3BusinessCircleBusinessCircleApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3Client.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3ComplaintComplaintApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3ComplaintComplaintApisP1Api.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3DeliveryPlanDeliveryPlanApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3EcommerceEcommerceApisAccountFundsApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3EcommerceEcommerceApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3EcommerceEcommerceApisBillsApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3EcommerceEcommerceApisCrossBorderApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3EcommerceEcommerceApisMerchantCancellationApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3EcommerceEcommerceApisRefundsApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3FaPiaoFaPiaoApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3FundAppFundAppApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MarketingMarketingApisBusifavorApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MarketingMarketingApisCardApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MarketingMarketingApisFavorApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MarketingMarketingApisImageApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MarketingMarketingApisPartnershipsApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MarketingMarketingApisPaygiftApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MedicalInsuranceMedicalInsuranceApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MerchantGovernanceMerchantLimitationApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3MerchantRiskMerchantRiskApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3ParkingReminderParkingReminderApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3PayScorePayScoreApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3ProductCouponProductCouponApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3ProfitsharingProfitsharingApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3SmartGuideSmartGuideApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3Support.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3TransferTransferApisApi.zan`, `stdlib/Sdk/Wechat/TenPay/WechatPayV3VehicleParkingVehicleParkingApisApi.zan`


## WechatPayConfigRegistry (class)

按名称保存多商户配置，对应原 SDK 的 TenPayV3InfoCollection。

- List<string> v3Names;

- List<WechatPayV3Config> v3Values;

- List<string> v2Names;

- List<WechatPayV2Config> v2Values;

- public WechatPayConfigRegistry()

- WechatPayConfigRegistry AddV3(string name, WechatPayV3Config config)

- WechatPayConfigRegistry AddV2(string name, WechatPayV2Config config)

- WechatPayV3Config GetV3(string name)

- WechatPayV2Config GetV2(string name)

- int IndexOf(List<string> names, string name)


## WechatPayLegacyMpClient (class)

早期公众号微信支付客户端。AppId/AppSecret 是配置，短期 access_token 可在刷新后设置。

- WechatPayLegacyMpConfig config;

- WechatApiTransport transport;

- public WechatPayLegacyMpClient(WechatPayLegacyMpConfig config)

- WechatPayLegacyMpClient Server(string host, int port)

- WechatPayLegacyMpClient Timeout(int ms)

- WechatPayLegacyMpClient SetAccessToken(string accessToken)

- async WechatRawResponse RequestJsonAsync(string method, string path, string query, string jsonBody)

- async WechatRawResponse RequestMultipartAsync(string method, string path, string query, WechatMultipart multipart)


## WechatPayLegacyMpConfig (class)

早期公众号支付 /pay 与 /payfeedback 接口配置。

- string AppId;

- string AppSecret;

- string AccessToken;

- public WechatPayLegacyMpConfig(string appId, string appSecret)

- WechatPayLegacyMpConfig SetAccessToken(string accessToken)


## WechatPayLegacyProfitSharingApi (class)

旧版 XML 分账接口。

- WechatPayV2Client client;

- public WechatPayLegacyProfitSharingApi(WechatPayV2Client client)

- async WechatRawResponse ProfitSharingAsync(WechatPayV2Request request)

- async WechatRawResponse MultiProfitSharingAsync(WechatPayV2Request request)

- async WechatRawResponse ProfitSharingFinishAsync(WechatPayV2Request request)

- async WechatRawResponse ProfitSharingAddReceiverAsync(WechatPayV2Request request)

- async WechatRawResponse ProfitSharingRemoveReceiverAsync(WechatPayV2Request request)

- async WechatRawResponse ProfitSharingQueryAsync(WechatPayV2Request request)


## WechatPayLegacyRedPackApi (class)

现金红包、企业红包等仅在原 SDK 中提供同步方法的 V2/mTLS 接口。

- WechatPayV2Client client;

- public WechatPayLegacyRedPackApi(WechatPayV2Client client)

- async WechatRawResponse SendNormalRedPackAsync(WechatPayV2Request request)

- async WechatRawResponse SendGroupRedPackAsync(WechatPayV2Request request)

- async WechatRawResponse SearchRedPackAsync(WechatPayV2Request request)

- async WechatRawResponse SendMiniAppRedPackAsync(WechatPayV2Request request)

- async WechatRawResponse SendNormalRedPackViaProviderAsync(WechatPayV2Request request)

- async WechatRawResponse HbPreOrderAsync(WechatPayV2Request request)

- async WechatRawResponse SendWorkRedPackAsync(WechatPayV2Request request)

- async WechatRawResponse QueryWorkRedPackAsync(WechatPayV2Request request)


## WechatPayLegacyTenPayApi (class)

- WechatPayLegacyMpClient client;

- public WechatPayLegacyTenPayApi(WechatPayLegacyMpClient client)

- async WechatPayLegacyTenPayApiDelivernotifyResponse DelivernotifyAsync(WechatPayLegacyTenPayApiDelivernotifyRequest request)
  - POST /pay/delivernotify?access_token={0}; C# 参数：string appId, string accessToken,string openId, string transId, string out_Trade_No, string deliver_TimesTamp, string deliver_Status, string deliver_Msg, string app_Signature, string sign_Method = "sha1"

- async WechatRawResponse DelivernotifyRawAsync(string path, string query, string jsonBody)

- async WechatPayLegacyTenPayApiOrderqueryResponse OrderqueryAsync(WechatPayLegacyTenPayApiOrderqueryRequest request)
  - POST /pay/orderquery?access_token={0}; C# 参数：string appId, string accessToken, string package, string timesTamp, string app_Signature, string sign_Method

- async WechatRawResponse OrderqueryRawAsync(string path, string query, string jsonBody)


## WechatPayLegacyTenPayApiDelivernotifyRequest (class)

TenPay.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayLegacyTenPayApiDelivernotifyRequest()

- WechatPayLegacyTenPayApiDelivernotifyRequest AppId(string fieldValue)

- WechatPayLegacyTenPayApiDelivernotifyRequest OpenId(string fieldValue)

- WechatPayLegacyTenPayApiDelivernotifyRequest TransId(string fieldValue)

- WechatPayLegacyTenPayApiDelivernotifyRequest OutTradeNo(string fieldValue)

- WechatPayLegacyTenPayApiDelivernotifyRequest DeliverTimesTamp(string fieldValue)

- WechatPayLegacyTenPayApiDelivernotifyRequest DeliverStatus(string fieldValue)

- WechatPayLegacyTenPayApiDelivernotifyRequest DeliverMsg(string fieldValue)

- WechatPayLegacyTenPayApiDelivernotifyRequest AppSignature(string fieldValue)

- WechatPayLegacyTenPayApiDelivernotifyRequest SignMethod(string fieldValue)

- WechatPayLegacyTenPayApiDelivernotifyRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayLegacyTenPayApiDelivernotifyResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayApiOrderqueryRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayLegacyTenPayApiOrderqueryRequest()

- WechatPayLegacyTenPayApiOrderqueryRequest AppId(string fieldValue)

- WechatPayLegacyTenPayApiOrderqueryRequest Package(string fieldValue)

- WechatPayLegacyTenPayApiOrderqueryRequest TimesTamp(string fieldValue)

- WechatPayLegacyTenPayApiOrderqueryRequest AppSignature(string fieldValue)

- WechatPayLegacyTenPayApiOrderqueryRequest SignMethod(string fieldValue)

- WechatPayLegacyTenPayApiOrderqueryRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayLegacyTenPayApiOrderqueryResponse (class)

- public string Raw;


## WechatPayLegacyTenPayRightsApi (class)

- WechatPayLegacyMpClient client;

- public WechatPayLegacyTenPayRightsApi(WechatPayLegacyMpClient client)

- async WechatPayLegacyTenPayRightsApiUpDateFeedBackResponse UpDateFeedBackAsync(WechatPayLegacyTenPayRightsApiUpDateFeedBackRequest request)
  - GET /payfeedback/更新?access_token={0}&openid={1}&feedbackid={2}; C# 参数：string accessToken, string openId, string feedBackId

- async WechatRawResponse UpDateFeedBackRawAsync(string path, string query)


## WechatPayLegacyTenPayRightsApiUpDateFeedBackRequest (class)

TenPayRights.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayLegacyTenPayRightsApiUpDateFeedBackRequest()

- WechatPayLegacyTenPayRightsApiUpDateFeedBackRequest OpenId(string fieldValue)

- WechatPayLegacyTenPayRightsApiUpDateFeedBackRequest FeedBackId(string fieldValue)

- WechatPayLegacyTenPayRightsApiUpDateFeedBackRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayLegacyTenPayRightsApiUpDateFeedBackResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3Api (class)

- WechatPayV2Client client;

- public WechatPayLegacyTenPayV3Api(WechatPayV2Client client)

- async WechatPayLegacyTenPayV3ApiGetSignKeyResponse GetSignKeyAsync(WechatPayLegacyTenPayV3ApiGetSignKeyRequest request)
  - POST /sandboxnew/pay/getsignkey; C# 参数：TenPayV3GetSignKeyRequestData dataInfo, int timeOut = Config.TIME_OUT

- async WechatRawResponse GetSignKeyRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiUnifiedorderResponse UnifiedorderAsync(WechatPayLegacyTenPayV3ApiUnifiedorderRequest request)
  - POST /pay/unifiedorder; C# 参数：string 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UnifiedorderRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiHtml5OrderResponse Html5OrderAsync(WechatPayLegacyTenPayV3ApiHtml5OrderRequest request)
  - POST /pay/unifiedorder; C# 参数：TenPayV3UnifiedorderRequestData dataInfo, int timeOut = Config.TIME_OUT

- async WechatRawResponse Html5OrderRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiRefundResponse RefundAsync(WechatPayLegacyTenPayV3ApiRefundRequest request)
  - POST /secapi/pay/refund; C# 参数：IServiceProvider serviceProvider, TenPayV3RefundRequestData dataInfo, #if NET462 string cert, string certPassword, #endif int timeOut = Config.TIME_OUT

- async WechatRawResponse RefundRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiOrderQueryResponse OrderQueryAsync(WechatPayLegacyTenPayV3ApiOrderQueryRequest request)
  - POST /pay/orderquery; C# 参数：string 数据

- async WechatRawResponse OrderQueryRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiCloseOrderResponse CloseOrderAsync(WechatPayLegacyTenPayV3ApiCloseOrderRequest request)
  - POST /pay/closeorder; C# 参数：string 数据

- async WechatRawResponse CloseOrderRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiReverseResponse ReverseAsync(WechatPayLegacyTenPayV3ApiReverseRequest request)
  - POST /secapi/pay/reverse; C# 参数：string 数据

- async WechatRawResponse ReverseRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiRefundQueryResponse RefundQueryAsync(WechatPayLegacyTenPayV3ApiRefundQueryRequest request)
  - POST /pay/refundquery; C# 参数：string 数据

- async WechatRawResponse RefundQueryRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiDownloadBillResponse DownloadBillAsync(WechatPayLegacyTenPayV3ApiDownloadBillRequest request)
  - POST /pay/downloadbill; C# 参数：string 数据

- async WechatRawResponse DownloadBillRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiShortUrlResponse ShortUrlAsync(WechatPayLegacyTenPayV3ApiShortUrlRequest request)
  - POST /tools/shorturl; C# 参数：string 数据

- async WechatRawResponse ShortUrlRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiMicroPayResponse MicroPayAsync(WechatPayLegacyTenPayV3ApiMicroPayRequest request)
  - POST /pay/micropay; C# 参数：string 数据

- async WechatRawResponse MicroPayRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiTransfersResponse TransfersAsync(WechatPayLegacyTenPayV3ApiTransfersRequest request)
  - POST /mmpaymkttransfers/promotion/transfers; C# 参数：string 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse TransfersRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiPayToWorkerResponse PayToWorkerAsync(WechatPayLegacyTenPayV3ApiPayToWorkerRequest request)
  - POST /mmpaymkttransfers/promotion/paywwsptrans2pocket; C# 参数：IServiceProvider serviceProvider, TenPayV3PayToWorkerRequestData dataInfo, #if NET462 string cert, string certPassword, #endif int timeOut = Config.TIME_OUT

- async WechatRawResponse PayToWorkerRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiGetTransferInfoResponse GetTransferInfoAsync(WechatPayLegacyTenPayV3ApiGetTransferInfoRequest request)
  - POST /mmpaymkttransfers/gettransferinfo; C# 参数：string 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GetTransferInfoRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenPayV3ApiQueryPayLogResponse QueryPayLogAsync(WechatPayLegacyTenPayV3ApiQueryPayLogRequest request)
  - POST /mmpaymkttransfers/promotion/querywwsptrans2pocket; C# 参数：IServiceProvider serviceProvider, TenPayV3GetTransferInfoRequestData dataInfo, #if NET462 string cert, string certPassword, #endif int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPayLogRawAsync(WechatPayV2Request request)


## WechatPayLegacyTenPayV3ApiCloseOrderRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiCloseOrderRequest()

- WechatPayLegacyTenPayV3ApiCloseOrderRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiCloseOrderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3ApiDownloadBillRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiDownloadBillRequest()

- WechatPayLegacyTenPayV3ApiDownloadBillRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiDownloadBillResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3ApiGetSignKeyRequest (class)

TenPayV3.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiGetSignKeyRequest()

- WechatPayLegacyTenPayV3ApiGetSignKeyRequest MchId(string fieldValue)

- WechatPayLegacyTenPayV3ApiGetSignKeyRequest OutTradeNo(string fieldValue)

- WechatPayLegacyTenPayV3ApiGetSignKeyRequest SignType(string fieldValue)

- WechatPayLegacyTenPayV3ApiGetSignKeyRequest Key(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiGetSignKeyResponse (class)

- public string Raw;

- public string mch_id;

- public string sandbox_signkey;

- public string return_code;

- public string return_msg;


## WechatPayLegacyTenPayV3ApiGetTransferInfoRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiGetTransferInfoRequest()

- WechatPayLegacyTenPayV3ApiGetTransferInfoRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiGetTransferInfoResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3ApiHtml5OrderRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiHtml5OrderRequest()

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest AppId(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest MchId(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest SubAppId(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest SubMchId(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest DeviceInfo(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest SignType(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest Body(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest Detail(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest Attach(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest FeeType(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest OutTradeNo(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest TotalFee(int fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest SpbillCreateIP(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest TimeStart(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest TimeExpire(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest GoodsTag(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest NotifyUrl(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest TradeType(int fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest ProductId(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest LimitPay(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest OpenId(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest SceneInfo(WechatPayTenPayV3UnifiedorderRequestDataSceneInfo fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest SubOpenid(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest ProfitSharing(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest Version(string fieldValue)

- WechatPayLegacyTenPayV3ApiHtml5OrderRequest Key(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiHtml5OrderResponse (class)

- public string Raw;

- public string device_info;

- public string trade_type;

- public string prepay_id;

- public string code_url;

- public string mweb_url;

- public string appid;

- public string mch_id;

- public string sub_appid;

- public string sub_mch_id;

- public string nonce_str;

- public string sign;

- public string result_code;

- public string err_code;

- public string err_code_des;

- public string return_code;

- public string return_msg;


## WechatPayLegacyTenPayV3ApiMicroPayRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiMicroPayRequest()

- WechatPayLegacyTenPayV3ApiMicroPayRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiMicroPayResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3ApiOrderQueryRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiOrderQueryRequest()

- WechatPayLegacyTenPayV3ApiOrderQueryRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiOrderQueryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3ApiPayToWorkerRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiPayToWorkerRequest()

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest AppId(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest SubAppId(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest MchId(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest SubMchId(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest DeviceInfo(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest PartnerTradeNo(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest OpenId(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest CheckName(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest ReUserName(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest Amount(double fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest Desc(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest SpbillCreateIP(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest WwMsgType(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest ApprovalNumber(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest ApprovalType(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest ActName(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest AgentId(long fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest Key(string fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest ServiceProvider(JsonValue fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest Cert(JsonValue fieldValue)

- WechatPayLegacyTenPayV3ApiPayToWorkerRequest CertPassword(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiPayToWorkerResponse (class)

- public string Raw;

- public string mch_appid;

- public string mchid;

- public string device_info;

- public string nonce_str;

- public string result_code;

- public string err_code;

- public string err_code_des;

- public string partner_trade_no;

- public string payment_no;

- public string payment_time;

- public string return_code;

- public string return_msg;


## WechatPayLegacyTenPayV3ApiQueryPayLogRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiQueryPayLogRequest()

- WechatPayLegacyTenPayV3ApiQueryPayLogRequest AppId(string fieldValue)

- WechatPayLegacyTenPayV3ApiQueryPayLogRequest SubAppId(string fieldValue)

- WechatPayLegacyTenPayV3ApiQueryPayLogRequest MchId(string fieldValue)

- WechatPayLegacyTenPayV3ApiQueryPayLogRequest SubMchId(string fieldValue)

- WechatPayLegacyTenPayV3ApiQueryPayLogRequest PartnerTradeNo(string fieldValue)

- WechatPayLegacyTenPayV3ApiQueryPayLogRequest Key(string fieldValue)

- WechatPayLegacyTenPayV3ApiQueryPayLogRequest ServiceProvider(JsonValue fieldValue)

- WechatPayLegacyTenPayV3ApiQueryPayLogRequest Cert(JsonValue fieldValue)

- WechatPayLegacyTenPayV3ApiQueryPayLogRequest CertPassword(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiQueryPayLogResponse (class)

- public string Raw;

- public string result_code;

- public string err_code;

- public string err_code_des;

- public string partner_trade_no;

- public string mch_id;

- public string detail_id;

- public string status;

- public string reason;

- public string openid;

- public string transfer_name;

- public string payment_amount;

- public string transfer_time;

- public string desc;

- public string payment_time;

- public string return_code;

- public string return_msg;


## WechatPayLegacyTenPayV3ApiRefundQueryRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiRefundQueryRequest()

- WechatPayLegacyTenPayV3ApiRefundQueryRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiRefundQueryResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3ApiRefundRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiRefundRequest()

- WechatPayLegacyTenPayV3ApiRefundRequest AppId(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest MchId(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest DeviceInfo(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest TransactionId(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest OutTradeNo(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest OutRefundNo(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest TotalFee(int fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest RefundFee(int fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest RefundFeeType(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest OpUserId(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest RefundAccount(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest SignType(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest Key(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest SubAppId(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest SubMchId(string fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest ServiceProvider(JsonValue fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest Cert(JsonValue fieldValue)

- WechatPayLegacyTenPayV3ApiRefundRequest CertPassword(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiRefundResponse (class)

- public string Raw;

- public string device_info;

- public string transaction_id;

- public string out_trade_no;

- public string out_refund_no;

- public string refund_id;

- public string refund_fee;

- public string settlement_refund_fee;

- public string total_fee;

- public string settlement_total_fee;

- public string fee_type;

- public string cash_fee;

- public string cash_fee_type;

- public string cash_refund_fee;

- public string coupon_refund_fee;

- public string coupon_refund_count;

- public string coupon_type_n;

- public string coupon_refund_fee_n;

- public string coupon_refund_id_n;

- public string appid;

- public string mch_id;

- public string sub_appid;

- public string sub_mch_id;

- public string nonce_str;

- public string sign;

- public string result_code;

- public string err_code;

- public string err_code_des;

- public string return_code;

- public string return_msg;


## WechatPayLegacyTenPayV3ApiReverseRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiReverseRequest()

- WechatPayLegacyTenPayV3ApiReverseRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiReverseResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3ApiShortUrlRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiShortUrlRequest()

- WechatPayLegacyTenPayV3ApiShortUrlRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiShortUrlResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3ApiTransfersRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiTransfersRequest()

- WechatPayLegacyTenPayV3ApiTransfersRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiTransfersResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenPayV3ApiUnifiedorderRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenPayV3ApiUnifiedorderRequest()

- WechatPayLegacyTenPayV3ApiUnifiedorderRequest Data(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenPayV3ApiUnifiedorderResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayLegacyTenpayV3PayBankApi (class)

- WechatPayV2Client client;

- public WechatPayLegacyTenpayV3PayBankApi(WechatPayV2Client client)

- async WechatPayLegacyTenpayV3PayBankApiPayBankResponse PayBankAsync(WechatPayLegacyTenpayV3PayBankApiPayBankRequest request)
  - POST /mmpaysptrans/pay_bank; C# 参数：IServiceProvider serviceProvider, TenPayV3PayBankRequestData dataInfo, #if NET462 string cert, string certPassword, #endif int timeOut = Config.TIME_OUT

- async WechatRawResponse PayBankRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenpayV3PayBankApiQueryBankResponse QueryBankAsync(WechatPayLegacyTenpayV3PayBankApiQueryBankRequest request)
  - POST /mmpaysptrans/query_bank; C# 参数：IServiceProvider serviceProvider, TenPayV3QueryBankRequestData dataInfo, #if NET462 string cert, string certPassword, #endif int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBankRawAsync(WechatPayV2Request request)

- async WechatPayLegacyTenpayV3PayBankApiGetPublicKeyResponse GetPublicKeyAsync(WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest request)
  - POST /risk/getpublickey; C# 参数：IServiceProvider serviceProvider, TenPayV3QueryBankRequestData dataInfo, #if NET462 string cert, string certPassword, #endif int timeOut = Config.TIME_OUT

- async WechatRawResponse GetPublicKeyRawAsync(WechatPayV2Request request)


## WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest()

- WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest MchId(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest SubMchId(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest PartnerTradeNumber(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest Key(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest ServiceProvider(JsonValue fieldValue)

- WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest Cert(JsonValue fieldValue)

- WechatPayLegacyTenpayV3PayBankApiGetPublicKeyRequest CertPassword(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenpayV3PayBankApiGetPublicKeyResponse (class)

- public string Raw;

- public string result_code;

- public string err_code;

- public string err_code_des;

- public string mch_id;

- public string pub_key;

- public string return_code;

- public string return_msg;


## WechatPayLegacyTenpayV3PayBankApiPayBankRequest (class)

TenpayV3.PayBank.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatPayV2Request request;

- public WechatPayLegacyTenpayV3PayBankApiPayBankRequest()

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest MchId(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest SubMchId(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest PartnerTradeNumber(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest EncBankNumber(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest EncTrueName(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest BankCode(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest Amount(int fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest Desc(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest OutTradeNo(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest SignType(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest Key(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest ServiceProvider(JsonValue fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest Cert(JsonValue fieldValue)

- WechatPayLegacyTenpayV3PayBankApiPayBankRequest CertPassword(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenpayV3PayBankApiPayBankResponse (class)

- public string Raw;

- public string result_code;

- public string err_code;

- public string err_code_des;

- public string mch_id;

- public string partner_trade_no;

- public string amount;

- public string nonce_str;

- public string sign;

- public string payment_no;

- public string cmms_amt;

- public string return_code;

- public string return_msg;


## WechatPayLegacyTenpayV3PayBankApiQueryBankRequest (class)

- WechatPayV2Request request;

- public WechatPayLegacyTenpayV3PayBankApiQueryBankRequest()

- WechatPayLegacyTenpayV3PayBankApiQueryBankRequest MchId(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiQueryBankRequest SubMchId(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiQueryBankRequest PartnerTradeNumber(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiQueryBankRequest Key(string fieldValue)

- WechatPayLegacyTenpayV3PayBankApiQueryBankRequest ServiceProvider(JsonValue fieldValue)

- WechatPayLegacyTenpayV3PayBankApiQueryBankRequest Cert(JsonValue fieldValue)

- WechatPayLegacyTenpayV3PayBankApiQueryBankRequest CertPassword(string fieldValue)

- WechatPayV2Request Inner()


## WechatPayLegacyTenpayV3PayBankApiQueryBankResponse (class)

- public string Raw;

- public string result_code;

- public string err_code;

- public string err_code_des;

- public string mch_id;

- public string partner_trade_no;

- public string payment_no;

- public string bank_no_md5;

- public string true_name_md5;

- public string amount;

- public string status;

- public string cmms_amt;

- public string create_time;

- public string pay_succ_time;

- public string reason;

- public string return_code;

- public string return_msg;


## WechatPayLegacyUtilities (class)

旧版微信支付中没有异步端点对应项的工具函数。

- static string NativePayV1(string sign, string appId, string timestamp, string nonce, string productId)

- static string NativePay(string appId, string timestamp, string merchantId, string nonce, string productId, string sign)

- static string GetJsPaySign(string appId, string timestamp, string nonce, string packageValue, string merchantKey, string signType)


## WechatPayNotification (class)

微信支付 API v3 通知。原始报文、通知 ID、验签与 AES-GCM 资源解密均保留，
业务方可将 DecryptResource() 的 JSON 再映射为自己的数据类型。

- string Id;

- string CreateTime;

- string EventType;

- string ResourceType;

- string Summary;

- string Algorithm;

- string Ciphertext;

- string Nonce;

- string AssociatedData;

- string RawBody;

- public WechatPayNotification()

- static WechatPayNotification Parse(string body)

- bool Verify(WechatPayV3Client client, string timestamp, string nonce, string signature, string serial)

- bool VerifyWithPublicKey(string timestamp, string nonce, string signature, string serial, string expectedSerial, string publicKeyPem)

- string DecryptResourceWithKey(string apiKey)

- string DecryptResource(WechatPayV3Client client)


## WechatPayPlatformKeyStore (class)

微信支付平台证书/公钥缓存，按序列号支持证书轮换。

- List<string> serials;

- List<string> pems;

- List<RsaKey> keys;

- public WechatPayPlatformKeyStore()

- int Count()

- WechatPayPlatformKeyStore Add(string serial, string publicKeyOrCertificatePem)

- bool Contains(string serial)

- RsaKey Get(string serial)

- string Pem(string serial)

- string SerialAt(int index)

- RsaKey First()

- int IndexOf(string serial)


## WechatPaySpecialBuilders (class)

现代特殊接口中的客户端参数构造辅助。

- static string VehicleParkingRepaymentMiniProgramAppId()

- static string VehicleParkingRepaymentMiniProgramUserName()

- static string CreateVehicleParkingRepaymentData(string appId, string merchantId, string openId, string nonce)

- static string CreateVehicleParkingRepaymentPath(string appId, string merchantId, string openId, string nonce)

- static string CreateMedicalInsurancePayPackage(WechatPayV3Client client, string mixTradeNo, string prepayId, string appId, bool includeAppId)


## WechatPayV2Client (class)

微信支付 V2 XML 客户端，支持普通 HTTPS 与退款/红包所需 mTLS。

- WechatPayV2Config config;

- WechatApiTransport transport;

- WechatApiTransport mutualTransport;

- public WechatPayV2Client(WechatPayV2Config config)

- WechatPayV2Client Server(string host, int port)

- WechatPayV2Client Timeout(int ms)

- async WechatRawResponse RequestAsync(string path, WechatPayV2Request request, bool requireClientCertificate)

- async WechatRawResponse RequestXmlAsync(string path, string signedXml, bool requireClientCertificate)

- bool VerifyResponse(string xml)

- string DecodeRefundReqInfo(string encryptedBase64)


## WechatPayV2Config (class)

微信支付 V2/XML 商户配置。

- string AppId;

- string AppSecret;

- string MerchantId;

- string MerchantKey;

- string NotifyUrl;

- string WxOpenNotifyUrl;

- string SignType;

- string ClientCertificateFile;

- string ClientPrivateKeyFile;

- string SubAppId;

- string SubAppSecret;

- string SubMerchantId;

- public WechatPayV2Config(string appId, string merchantId, string merchantKey)

- WechatPayV2Config SetAppSecret(string appSecret)

- WechatPayV2Config SetNotifyUrl(string url)

- WechatPayV2Config SetNotifyUrls(string payUrl, string wxOpenUrl)

- WechatPayV2Config UseHmacSha256()

- WechatPayV2Config SetClientCertificate(string certFile, string keyFile)

- WechatPayV2Config SetServiceProviderCredentials(string subAppId, string subAppSecret, string subMerchantId)


## WechatPayV2Field (class)

微信支付 V2/XML 单个参数。

- string name;

- string value;

- WechatPayV2Field(string fieldName, string fieldValue)


## WechatPayV2Request (class)

微信支付 V2/XML 参数集合、排序签名与 XML 编解码。

- List<WechatPayV2Field> fields;

- public WechatPayV2Request()

- WechatPayV2Request Set(string name, string fieldValue)

- bool Has(string name)

- string Get(string name, string dflt)

- int Count()

- string Canonical(string merchantKey)

- string Sign(string merchantKey, string signType)

- bool Verify(string merchantKey)

- string ToSignedXml(WechatPayV2Config config)

- string ToXml()

- static WechatPayV2Request ParseXml(string xml)

- int Index(string name)

- static int Compare(string a, string b)

- static string Upper(string text)


## WechatPayV3Apply4SubApply4SubApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3Apply4SubApply4SubApisApi(WechatPayV3Client client)

- async WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentResponse Apply4SubApplymentAsync(WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest request)
  - POST /v3/apply4sub/applyment/; C# 参数：Apply4SubApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse Apply4SubApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByIdResponse QueryApply4SubApplymentByIdAsync(WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByIdRequest request)
  - GET /v3/apply4sub/applyment/applyment_id/{数据.applyment_id}; C# 参数：QueryApply4SubApplymentByIdRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApply4SubApplymentByIdRawAsync(string path, string query)

- async WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByOutRequestNoResponse QueryApply4SubApplymentByOutRequestNoAsync(WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByOutRequestNoRequest request)
  - GET /v3/apply4sub/applyment/out_request_no/{数据.out_request_no}; C# 参数：QueryApply4SubApplymentByOutRequestNoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApply4SubApplymentByOutRequestNoRawAsync(string path, string query)

- async WechatPayV3Apply4SubApply4SubApisApiModifyApply4SubSettlementResponse ModifyApply4SubSettlementAsync(WechatPayV3Apply4SubApply4SubApisApiModifyApply4SubSettlementRequest request)
  - POST /v3/apply4sub/sub_merchants/{数据.sub_mchid}/modify-settlement; C# 参数：ModifyApply4SubSettlementRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifyApply4SubSettlementRawAsync(string path, string query, string jsonBody)

- async WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubSettlementResponse QueryApply4SubSettlementAsync(WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubSettlementRequest request)
  - GET /v3/apply4sub/sub_merchants/{数据.sub_mchid}/settlement; C# 参数：QueryApply4SubSettlementRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApply4SubSettlementRawAsync(string path, string query)


## WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest (class)

Apply4Sub/Apply4SubApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest()

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest OutRequestNo(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest OrganizationType(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest BusinessLicenseInfo(WechatPayApply4SubBusinessLicenseInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest IdDocInfo(WechatPayApply4SubIdDocInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest UboInfo(WechatPayApply4SubUboInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest AccountInfo(WechatPayApply4SubAccountInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest ContactInfo(WechatPayApply4SubContactInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest SalesSceneInfo(WechatPayApply4SubSalesSceneInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest MerchantShortname(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest Qualifications(List<string> fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest BusinessAdditionPics(List<string> fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest BusinessAdditionMsg(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisApiApply4SubApplymentResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisApiModifyApply4SubSettlementRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisApiModifyApply4SubSettlementRequest()

- WechatPayV3Apply4SubApply4SubApisApiModifyApply4SubSettlementRequest SubMchid(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiModifyApply4SubSettlementRequest AccountInfo(WechatPayApply4SubAccountInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiModifyApply4SubSettlementRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisApiModifyApply4SubSettlementResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByIdRequest()

- WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByIdRequest ApplymentId(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByIdResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByOutRequestNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByOutRequestNoRequest()

- WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByOutRequestNoRequest OutRequestNo(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByOutRequestNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubApplymentByOutRequestNoResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubSettlementRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubSettlementRequest()

- WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubSettlementRequest SubMchid(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubSettlementRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisApiQueryApply4SubSettlementResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisCurrentApi (class)

- WechatPayV3Client client;

- public WechatPayV3Apply4SubApply4SubApisCurrentApi(WechatPayV3Client client)

- async WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentResponse SubmitApplymentAsync(WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest request)
  - POST /v3/applyment4sub/applyment/; C# 参数：Apply4SubCurrentApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByIdResponse QueryApplymentByIdAsync(WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByIdRequest request)
  - GET /v3/applyment4sub/applyment/applyment_id/{applymentId}; C# 参数：long applymentId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApplymentByIdRawAsync(string path, string query)

- async WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByBusinessCodeResponse QueryApplymentByBusinessCodeAsync(WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByBusinessCodeRequest request)
  - GET /v3/applyment4sub/applyment/business_code/{EscapeCurrent(businessCode)}; C# 参数：string businessCode, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApplymentByBusinessCodeRawAsync(string path, string query)

- async WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementResponse ModifySettlementAsync(WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest request)
  - POST /v3/apply4sub/sub_merchants/{EscapeCurrent(subMchId)}/modify-settlement; C# 参数：string subMchId, Apply4SubModifySettlementRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifySettlementRawAsync(string path, string query, string jsonBody)

- async WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementResponse QuerySettlementAsync(WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementRequest request)
  - GET /v3/apply4sub/sub_merchants/{EscapeCurrent(subMchId)}/settlement; C# 参数：string subMchId, string accountNumberRule = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySettlementRawAsync(string path, string query)

- async WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementModificationResponse QuerySettlementModificationAsync(WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementModificationRequest request)
  - GET /v3/apply4sub/sub_merchants/{EscapeCurrent(subMchId)}/application/{EscapeCurrent(applicationNo)}; C# 参数：string subMchId, string applicationNo, string accountNumberRule = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySettlementModificationRawAsync(string path, string query)

- async WechatPayV3Apply4SubApply4SubApisCurrentApiUploadFileResponse UploadFileAsync(WechatPayV3Apply4SubApply4SubApisCurrentApiUploadFileRequest request, WechatMultipart multipart)
  - POST /v3/merchant/media/upload; C# 参数：string fileName, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadFileRawAsync(string path, string query, WechatMultipart multipart)

- async WechatPayV3Apply4SubApply4SubApisCurrentApiUploadVideoResponse UploadVideoAsync(WechatPayV3Apply4SubApply4SubApisCurrentApiUploadVideoRequest request, WechatMultipart multipart)
  - POST /v3/merchant/media/video_upload; C# 参数：string fileName, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadVideoRawAsync(string path, string query, WechatMultipart multipart)


## WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest()

- WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest AccountType(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest AccountBank(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest BankName(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest BankBranchId(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest AccountNumber(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest AccountName(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest SubMchId(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisCurrentApiModifySettlementResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByBusinessCodeRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByBusinessCodeRequest()

- WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByBusinessCodeRequest BusinessCode(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByBusinessCodeRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByBusinessCodeResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByIdRequest()

- WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByIdRequest ApplymentId(long fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisCurrentApiQueryApplymentByIdResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementModificationRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementModificationRequest()

- WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementModificationRequest SubMchId(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementModificationRequest ApplicationNo(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementModificationRequest AccountNumberRule(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementModificationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementModificationResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementRequest()

- WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementRequest SubMchId(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementRequest AccountNumberRule(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisCurrentApiQuerySettlementResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest (class)

Apply4Sub/Apply4SubApis.Current.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest()

- WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest BusinessCode(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest ContactInfo(WechatPayApply4SubCurrentContactInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest SubjectInfo(WechatPayApply4SubCurrentSubjectInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest BusinessInfo(WechatPayApply4SubCurrentBusinessInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest SettlementInfo(WechatPayApply4SubCurrentSettlementInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest BankAccountInfo(WechatPayApply4SubCurrentBankAccountInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest AdditionInfo(WechatPayApply4SubCurrentAdditionInfo fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisCurrentApiSubmitApplymentResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisCurrentApiUploadFileRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisCurrentApiUploadFileRequest()

- WechatPayV3Apply4SubApply4SubApisCurrentApiUploadFileRequest FileName(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiUploadFileRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisCurrentApiUploadFileResponse (class)

- public string Raw;


## WechatPayV3Apply4SubApply4SubApisCurrentApiUploadVideoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubApply4SubApisCurrentApiUploadVideoRequest()

- WechatPayV3Apply4SubApply4SubApisCurrentApiUploadVideoRequest FileName(string fieldValue)

- WechatPayV3Apply4SubApply4SubApisCurrentApiUploadVideoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubApply4SubApisCurrentApiUploadVideoResponse (class)

- public string Raw;


## WechatPayV3Apply4SubjectApply4SubjectApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3Apply4SubjectApply4SubjectApisApi(WechatPayV3Client client)

- async WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentResponse Apply4SubjectApplymentAsync(WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentRequest request)
  - POST /v3/apply4subject/applyment; C# 参数：Apply4SubjectApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse Apply4SubjectApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3Apply4SubjectApply4SubjectApisApiCancelApply4SubjectApplymentResponse CancelApply4SubjectApplymentAsync(WechatPayV3Apply4SubjectApply4SubjectApisApiCancelApply4SubjectApplymentRequest request)
  - POST /v3/apply4subject/applyment/{数据.applyment_id}/cancel; C# 参数：CancelApply4SubjectApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CancelApply4SubjectApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByIdResponse QueryApply4SubjectApplymentByIdAsync(WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByIdRequest request)
  - GET /v3/apply4subject/applyment/applyment_id/{数据.applyment_id}; C# 参数：QueryApply4SubjectApplymentByIdRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApply4SubjectApplymentByIdRawAsync(string path, string query)

- async WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByOutRequestNoResponse QueryApply4SubjectApplymentByOutRequestNoAsync(WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByOutRequestNoRequest request)
  - GET /v3/apply4subject/applyment/out_request_no/{数据.out_request_no}; C# 参数：QueryApply4SubjectApplymentByOutRequestNoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApply4SubjectApplymentByOutRequestNoRawAsync(string path, string query)


## WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentRequest (class)

Apply4Subject/Apply4SubjectApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentRequest()

- WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentRequest OutRequestNo(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentRequest SubjectInfo(WechatPayApply4SubjectInfo fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentRequest IdentityInfo(WechatPayApply4SubjectIdentityInfo fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentRequest ContactInfo(WechatPayApply4SubjectContactInfo fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubjectApply4SubjectApisApiApply4SubjectApplymentResponse (class)

- public string Raw;


## WechatPayV3Apply4SubjectApply4SubjectApisApiCancelApply4SubjectApplymentRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubjectApply4SubjectApisApiCancelApply4SubjectApplymentRequest()

- WechatPayV3Apply4SubjectApply4SubjectApisApiCancelApply4SubjectApplymentRequest ApplymentId(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisApiCancelApply4SubjectApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubjectApply4SubjectApisApiCancelApply4SubjectApplymentResponse (class)

- public string Raw;


## WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByIdRequest()

- WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByIdRequest ApplymentId(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByIdResponse (class)

- public string Raw;


## WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByOutRequestNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByOutRequestNoRequest()

- WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByOutRequestNoRequest OutRequestNo(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByOutRequestNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubjectApply4SubjectApisApiQueryApply4SubjectApplymentByOutRequestNoResponse (class)

- public string Raw;


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApi (class)

- WechatPayV3Client client;

- public WechatPayV3Apply4SubjectApply4SubjectApisCurrentApi(WechatPayV3Client client)

- async WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentResponse SubmitApplymentAsync(WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest request)
  - POST /v3/apply4subject/applyment/; C# 参数：Apply4SubjectApplicationRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiCancelApplymentResponse CancelApplymentAsync(WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiCancelApplymentRequest request)
  - POST /v3/apply4subject/applyment/{Escape(identifier)}/cancel; C# 参数：string businessCode = null, long? applymentId = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse CancelApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryApplymentAuditResultResponse QueryApplymentAuditResultAsync(WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryApplymentAuditResultRequest request)
  - GET /v3/apply4subject/applyment; C# 参数：long? applymentId = null, string businessCode = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApplymentAuditResultRawAsync(string path, string query)

- async WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryMerchantAuthorizationStateResponse QueryMerchantAuthorizationStateAsync(WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryMerchantAuthorizationStateRequest request)
  - GET /v3/apply4subject/applyment/merchants/{Escape(subMchId)}/state; C# 参数：string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryMerchantAuthorizationStateRawAsync(string path, string query)

- async WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiUploadImageResponse UploadImageAsync(WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiUploadImageRequest request, WechatMultipart multipart)
  - POST /v3/merchant/media/upload; C# 参数：string fileName, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadImageRawAsync(string path, string query, WechatMultipart multipart)


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiCancelApplymentRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiCancelApplymentRequest()

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiCancelApplymentRequest BusinessCode(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiCancelApplymentRequest ApplymentId(long fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiCancelApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiCancelApplymentResponse (class)

- public string Raw;


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryApplymentAuditResultRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryApplymentAuditResultRequest()

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryApplymentAuditResultRequest ApplymentId(long fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryApplymentAuditResultRequest BusinessCode(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryApplymentAuditResultRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryApplymentAuditResultResponse (class)

- public string Raw;


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryMerchantAuthorizationStateRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryMerchantAuthorizationStateRequest()

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryMerchantAuthorizationStateRequest SubMchId(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryMerchantAuthorizationStateRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiQueryMerchantAuthorizationStateResponse (class)

- public string Raw;


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest (class)

Apply4Subject/Apply4SubjectApis.Current.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest()

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest ChannelId(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest BusinessCode(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest ContactInfo(WechatPayApply4SubjectApplicationContactInfo fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest SubjectInfo(WechatPayApply4SubjectApplicationSubjectInfo fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest IdentificationInfo(WechatPayApply4SubjectApplicationIdentificationInfo fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest AdditionInfo(WechatPayApply4SubjectApplicationAdditionInfo fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest UboInfoList(List<WechatPayApply4SubjectApplicationUboInfo> fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiSubmitApplymentResponse (class)

- public string Raw;


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiUploadImageRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiUploadImageRequest()

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiUploadImageRequest FileName(string fieldValue)

- WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiUploadImageRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3Apply4SubjectApply4SubjectApisCurrentApiUploadImageResponse (class)

- public string Raw;


## WechatPayV3BankComponentBankComponentApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3BankComponentBankComponentApisApi(WechatPayV3Client client)

- async WechatPayV3BankComponentBankComponentApisApiQueryBankResponse QueryBankAsync(WechatPayV3BankComponentBankComponentApisApiQueryBankRequest request)
  - POST /v3/capital/capitallhh/banks/personal-banking; C# 参数：QueryBankRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBankRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BankComponentBankComponentApisApiQueryBankListResponse QueryBankListAsync(WechatPayV3BankComponentBankComponentApisApiQueryBankListRequest request)
  - GET /v3/capital/capitallhh/banks/personal-banking/banks; C# 参数：QueryBankListRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBankListRawAsync(string path, string query)

- async WechatPayV3BankComponentBankComponentApisApiQueryProvinceListResponse QueryProvinceListAsync(WechatPayV3BankComponentBankComponentApisApiQueryProvinceListRequest request)
  - GET /v3/capital/capitallhh/areas/provinces; C# 参数：int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryProvinceListRawAsync(string path, string query)

- async WechatPayV3BankComponentBankComponentApisApiQueryCityListResponse QueryCityListAsync(WechatPayV3BankComponentBankComponentApisApiQueryCityListRequest request)
  - GET /v3/capital/capitallhh/areas/provinces/{数据.province_code}/cities; C# 参数：QueryCityListRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCityListRawAsync(string path, string query)

- async WechatPayV3BankComponentBankComponentApisApiQueryBranchListResponse QueryBranchListAsync(WechatPayV3BankComponentBankComponentApisApiQueryBranchListRequest request)
  - POST /v3/capital/capitallhh/banks/{数据.bank_alias_code}/branches; C# 参数：QueryBranchListRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBranchListRawAsync(string path, string query, string jsonBody)


## WechatPayV3BankComponentBankComponentApisApiQueryBankListRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BankComponentBankComponentApisApiQueryBankListRequest()

- WechatPayV3BankComponentBankComponentApisApiQueryBankListRequest Offset(int fieldValue)

- WechatPayV3BankComponentBankComponentApisApiQueryBankListRequest Limit(int fieldValue)

- WechatPayV3BankComponentBankComponentApisApiQueryBankListRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BankComponentBankComponentApisApiQueryBankListResponse (class)

- public string Raw;


## WechatPayV3BankComponentBankComponentApisApiQueryBankRequest (class)

BankComponent/BankComponentApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BankComponentBankComponentApisApiQueryBankRequest()

- WechatPayV3BankComponentBankComponentApisApiQueryBankRequest AccountNumber(string fieldValue)

- WechatPayV3BankComponentBankComponentApisApiQueryBankRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BankComponentBankComponentApisApiQueryBankResponse (class)

- public string Raw;


## WechatPayV3BankComponentBankComponentApisApiQueryBranchListRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BankComponentBankComponentApisApiQueryBranchListRequest()

- WechatPayV3BankComponentBankComponentApisApiQueryBranchListRequest BankAliasCode(string fieldValue)

- WechatPayV3BankComponentBankComponentApisApiQueryBranchListRequest CityCode(string fieldValue)

- WechatPayV3BankComponentBankComponentApisApiQueryBranchListRequest Offset(int fieldValue)

- WechatPayV3BankComponentBankComponentApisApiQueryBranchListRequest Limit(int fieldValue)

- WechatPayV3BankComponentBankComponentApisApiQueryBranchListRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BankComponentBankComponentApisApiQueryBranchListResponse (class)

- public string Raw;


## WechatPayV3BankComponentBankComponentApisApiQueryCityListRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BankComponentBankComponentApisApiQueryCityListRequest()

- WechatPayV3BankComponentBankComponentApisApiQueryCityListRequest ProvinceCode(string fieldValue)

- WechatPayV3BankComponentBankComponentApisApiQueryCityListRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BankComponentBankComponentApisApiQueryCityListResponse (class)

- public string Raw;


## WechatPayV3BankComponentBankComponentApisApiQueryProvinceListRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BankComponentBankComponentApisApiQueryProvinceListRequest()

- WechatPayV3BankComponentBankComponentApisApiQueryProvinceListRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BankComponentBankComponentApisApiQueryProvinceListResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisAbnormalRefundApi (class)

- WechatPayV3Client client;

- public WechatPayV3BasePayBasePayApisAbnormalRefundApi(WechatPayV3Client client)

- async WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundResponse ApplyAbnormalRefundAsync(WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest request)
  - POST /v3/refund/domestic/refunds/{escapedRefundId}/apply-abnormal-refund; C# 参数：string refundId, AbnormalRefundRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyAbnormalRefundRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundResponse ApplyCombineAbnormalRefundAsync(WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest request)
  - POST /v3/refund/domestic/refunds/{escapedRefundId}/apply-abnormal-refund; C# 参数：string refundId, AbnormalRefundRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyCombineAbnormalRefundRawAsync(string path, string query, string jsonBody)


## WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest (class)

BasePay/BasePayApis.AbnormalRefund.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest()

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest SubMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest OutRefundNo(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest Type(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest BankType(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest BankAccount(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest RealName(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest RefundId(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyAbnormalRefundResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest()

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest SubMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest OutRefundNo(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest Type(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest BankType(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest BankAccount(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest RealName(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest RefundId(string fieldValue)

- WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisAbnormalRefundApiApplyCombineAbnormalRefundResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3BasePayBasePayApisApi(WechatPayV3Client client)

- async WechatPayV3BasePayBasePayApisApiCertificatesResponse CertificatesAsync(WechatPayV3BasePayBasePayApisApiCertificatesRequest request)
  - GET /v3/certificates; C# 参数：CertType algorithmType, int timeOut = Config.TIME_OUT

- async WechatRawResponse CertificatesRawAsync(string path, string query)

- async WechatPayV3BasePayBasePayApisApiGetPublicKeysResponse GetPublicKeysAsync(WechatPayV3BasePayBasePayApisApiGetPublicKeysRequest request)
  - GET /v3/pub_key; C# 参数：

- async WechatRawResponse GetPublicKeysRawAsync(string path, string query)

- async WechatPayV3BasePayBasePayApisApiJsApiResponse JsApiAsync(WechatPayV3BasePayBasePayApisApiJsApiRequest request)
  - POST /v3/pay/{1}transactions/jsapi; C# 参数：TransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse JsApiRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiJsApiCombineResponse JsApiCombineAsync(WechatPayV3BasePayBasePayApisApiJsApiCombineRequest request)
  - POST /v3/combine-transactions/jsapi; C# 参数：CombineTransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse JsApiCombineRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiAppResponse AppAsync(WechatPayV3BasePayBasePayApisApiAppRequest request)
  - POST /v3/pay/{1}transactions/app; C# 参数：TransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AppRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiAppCombineResponse AppCombineAsync(WechatPayV3BasePayBasePayApisApiAppCombineRequest request)
  - POST /v3/combine-transactions/app; C# 参数：CombineTransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AppCombineRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiH5Response H5Async(WechatPayV3BasePayBasePayApisApiH5Request request)
  - POST /v3/pay/{1}transactions/h5; C# 参数：TransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse H5RawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiH5CombineResponse H5CombineAsync(WechatPayV3BasePayBasePayApisApiH5CombineRequest request)
  - POST /v3/combine-transactions/h5; C# 参数：CombineTransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse H5CombineRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiNativeResponse NativeAsync(WechatPayV3BasePayBasePayApisApiNativeRequest request)
  - POST /v3/pay/{1}transactions/native; C# 参数：TransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse NativeRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiNativeCombineResponse NativeCombineAsync(WechatPayV3BasePayBasePayApisApiNativeCombineRequest request)
  - POST /v3/combine-transactions/native; C# 参数：CombineTransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse NativeCombineRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiOrderQueryByTransactionIdResponse OrderQueryByTransactionIdAsync(WechatPayV3BasePayBasePayApisApiOrderQueryByTransactionIdRequest request)
  - GET /v3/pay/transactions/id/{transaction_id}?mchid={mchid}; C# 参数：string transaction_id, string mchid, int timeOut = Config.TIME_OUT

- async WechatRawResponse OrderQueryByTransactionIdRawAsync(string path, string query)

- async WechatPayV3BasePayBasePayApisApiOrderQueryByOutTradeNoResponse OrderQueryByOutTradeNoAsync(WechatPayV3BasePayBasePayApisApiOrderQueryByOutTradeNoRequest request)
  - GET /v3/pay/transactions/out-trade-no/{out_trade_no}?mchid={mchid}; C# 参数：string out_trade_no, string mchid, int timeOut = Config.TIME_OUT

- async WechatRawResponse OrderQueryByOutTradeNoRawAsync(string path, string query)

- async WechatPayV3BasePayBasePayApisApiCombineOrderQueryResponse CombineOrderQueryAsync(WechatPayV3BasePayBasePayApisApiCombineOrderQueryRequest request)
  - GET /v3/combine-transactions/out-trade-no/{combine_out_trade_no}; C# 参数：string combine_out_trade_no, int timeOut = Config.TIME_OUT

- async WechatRawResponse CombineOrderQueryRawAsync(string path, string query)

- async WechatPayV3BasePayBasePayApisApiCloseOrderResponse CloseOrderAsync(WechatPayV3BasePayBasePayApisApiCloseOrderRequest request)
  - POST /v3/pay/transactions/out-trade-no/{out_trade_no}/close; C# 参数：string out_trade_no, string mchid, int timeOut = Config.TIME_OUT

- async WechatRawResponse CloseOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiCloseCombineOrderResponse CloseCombineOrderAsync(WechatPayV3BasePayBasePayApisApiCloseCombineOrderRequest request)
  - POST /v3/combine-transactions/out-trade-no/{combine_out_trade_no}/close; C# 参数：string combine_out_trade_no, CloseCombineOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CloseCombineOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiRefundResponse RefundAsync(WechatPayV3BasePayBasePayApisApiRefundRequest request)
  - POST /v3/refund/domestic/refunds; C# 参数：RefundRequsetData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse RefundRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BasePayBasePayApisApiRefundQueryResponse RefundQueryAsync(WechatPayV3BasePayBasePayApisApiRefundQueryRequest request)
  - GET /v3/refund/domestic/refunds/{out_refund_no}; C# 参数：string out_refund_no, int timeOut = Config.TIME_OUT

- async WechatRawResponse RefundQueryRawAsync(string path, string query)

- async WechatRawResponse TradeBillQueryAsync(WechatPayV3BasePayBasePayApisApiTradeBillQueryRequest request)
  - GET /v3/bill/tradebill?bill_date={bill_date}&bill_type={bill_type}; C# 参数：string bill_date, Stream fileStream, string bill_type = "ALL", string tar_type = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse TradeBillQueryRawAsync(string path, string query)

- async WechatRawResponse FundflowBillQueryAsync(WechatPayV3BasePayBasePayApisApiFundflowBillQueryRequest request)
  - GET /v3/bill/fundflowbill?bill_date={bill_date}&account_type={account_type}; C# 参数：string bill_date, Stream fileStream, string account_type = "BASIC", string tar_type = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse FundflowBillQueryRawAsync(string path, string query)

- async WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryResponse SubmerchantFundflowBillQueryAsync(WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest request)
  - GET /v3/bill/sub-merchant-fundflowbill{UrlQueryHelper.ToParams(数据)}; C# 参数：FundflowBillQueryRequestData 数据, List<Stream> fileStreams, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmerchantFundflowBillQueryRawAsync(string path, string query)


## WechatPayV3BasePayBasePayApisApiAppCombineRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiAppCombineRequest()

- WechatPayV3BasePayBasePayApisApiAppCombineRequest CombineAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppCombineRequest CombineMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppCombineRequest CombineOutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppCombineRequest SceneInfo(WechatPayBasePayCombineTransactionsRequestDataSceneInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiAppCombineRequest SubOrders(List<WechatPayBasePayCombineTransactionsRequestDataSubOrder> fieldValue)

- WechatPayV3BasePayBasePayApisApiAppCombineRequest CombinePayerInfo(WechatPayBasePayCombineTransactionsRequestDataCombinePayerInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiAppCombineRequest TimeStart(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppCombineRequest TimeExpire(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppCombineRequest NotifyUrl(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppCombineRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiAppCombineResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiAppRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiAppRequest()

- WechatPayV3BasePayBasePayApisApiAppRequest Appid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest Mchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest SpAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest SpMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest SubAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest SubMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest Description(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest OutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest TimeExpire(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest Attach(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest NotifyUrl(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest GoodsTag(string fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest SupportFapiao(bool fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest Amount(WechatPayBasePayTransactionsRequestDataAmount fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest Detail(WechatPayBasePayTransactionsRequestDataDetail fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest SceneInfo(WechatPayBasePayTransactionsRequestDataSceneInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest Payer(WechatPayBasePayTransactionsRequestDataPayer fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest SettleInfo(WechatPayBasePayTransactionsRequestDataSettleInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiAppRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiAppResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiCertificatesRequest (class)

BasePay/BasePayApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiCertificatesRequest()

- WechatPayV3BasePayBasePayApisApiCertificatesRequest AlgorithmType(JsonValue fieldValue)

- WechatPayV3BasePayBasePayApisApiCertificatesRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiCertificatesResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiCloseCombineOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiCloseCombineOrderRequest()

- WechatPayV3BasePayBasePayApisApiCloseCombineOrderRequest CombineAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiCloseCombineOrderRequest SubOrders(List<WechatPayBasePayCloseCombineOrderRequestDataSubOrder> fieldValue)

- WechatPayV3BasePayBasePayApisApiCloseCombineOrderRequest CombineOutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiCloseCombineOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiCloseCombineOrderResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiCloseOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiCloseOrderRequest()

- WechatPayV3BasePayBasePayApisApiCloseOrderRequest Mchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiCloseOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiCloseOrderResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiCombineOrderQueryRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiCombineOrderQueryRequest()

- WechatPayV3BasePayBasePayApisApiCombineOrderQueryRequest CombineOutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiCombineOrderQueryRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiCombineOrderQueryResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiFundflowBillQueryRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiFundflowBillQueryRequest()

- WechatPayV3BasePayBasePayApisApiFundflowBillQueryRequest BillDate(string fieldValue)

- WechatPayV3BasePayBasePayApisApiFundflowBillQueryRequest AccountType(string fieldValue)

- WechatPayV3BasePayBasePayApisApiFundflowBillQueryRequest TarType(string fieldValue)

- WechatPayV3BasePayBasePayApisApiFundflowBillQueryRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiFundflowBillQueryResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiGetPublicKeysRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiGetPublicKeysRequest()

- WechatPayV3BasePayBasePayApisApiGetPublicKeysRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiGetPublicKeysResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiH5CombineRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiH5CombineRequest()

- WechatPayV3BasePayBasePayApisApiH5CombineRequest CombineAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5CombineRequest CombineMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5CombineRequest CombineOutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5CombineRequest SceneInfo(WechatPayBasePayCombineTransactionsRequestDataSceneInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiH5CombineRequest SubOrders(List<WechatPayBasePayCombineTransactionsRequestDataSubOrder> fieldValue)

- WechatPayV3BasePayBasePayApisApiH5CombineRequest CombinePayerInfo(WechatPayBasePayCombineTransactionsRequestDataCombinePayerInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiH5CombineRequest TimeStart(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5CombineRequest TimeExpire(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5CombineRequest NotifyUrl(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5CombineRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiH5CombineResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiH5Request (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiH5Request()

- WechatPayV3BasePayBasePayApisApiH5Request Appid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request Mchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request SpAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request SpMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request SubAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request SubMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request Description(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request OutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request TimeExpire(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request Attach(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request NotifyUrl(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request GoodsTag(string fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request SupportFapiao(bool fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request Amount(WechatPayBasePayTransactionsRequestDataAmount fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request Detail(WechatPayBasePayTransactionsRequestDataDetail fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request SceneInfo(WechatPayBasePayTransactionsRequestDataSceneInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request Payer(WechatPayBasePayTransactionsRequestDataPayer fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request SettleInfo(WechatPayBasePayTransactionsRequestDataSettleInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiH5Request RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiH5Response (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiJsApiCombineRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiJsApiCombineRequest()

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest CombineAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest CombineMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest CombineOutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest SceneInfo(WechatPayBasePayCombineTransactionsRequestDataSceneInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest SubOrders(List<WechatPayBasePayCombineTransactionsRequestDataSubOrder> fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest CombinePayerInfo(WechatPayBasePayCombineTransactionsRequestDataCombinePayerInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest TimeStart(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest TimeExpire(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest NotifyUrl(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiCombineRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiJsApiCombineResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiJsApiRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiJsApiRequest()

- WechatPayV3BasePayBasePayApisApiJsApiRequest Appid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest Mchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest SpAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest SpMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest SubAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest SubMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest Description(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest OutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest TimeExpire(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest Attach(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest NotifyUrl(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest GoodsTag(string fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest SupportFapiao(bool fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest Amount(WechatPayBasePayTransactionsRequestDataAmount fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest Detail(WechatPayBasePayTransactionsRequestDataDetail fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest SceneInfo(WechatPayBasePayTransactionsRequestDataSceneInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest Payer(WechatPayBasePayTransactionsRequestDataPayer fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest SettleInfo(WechatPayBasePayTransactionsRequestDataSettleInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiJsApiRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiJsApiResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiNativeCombineRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiNativeCombineRequest()

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest CombineAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest CombineMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest CombineOutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest SceneInfo(WechatPayBasePayCombineTransactionsRequestDataSceneInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest SubOrders(List<WechatPayBasePayCombineTransactionsRequestDataSubOrder> fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest CombinePayerInfo(WechatPayBasePayCombineTransactionsRequestDataCombinePayerInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest TimeStart(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest TimeExpire(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest NotifyUrl(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeCombineRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiNativeCombineResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiNativeRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiNativeRequest()

- WechatPayV3BasePayBasePayApisApiNativeRequest Appid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest Mchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest SpAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest SpMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest SubAppid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest SubMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest Description(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest OutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest TimeExpire(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest Attach(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest NotifyUrl(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest GoodsTag(string fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest SupportFapiao(bool fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest Amount(WechatPayBasePayTransactionsRequestDataAmount fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest Detail(WechatPayBasePayTransactionsRequestDataDetail fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest SceneInfo(WechatPayBasePayTransactionsRequestDataSceneInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest Payer(WechatPayBasePayTransactionsRequestDataPayer fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest SettleInfo(WechatPayBasePayTransactionsRequestDataSettleInfo fieldValue)

- WechatPayV3BasePayBasePayApisApiNativeRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiNativeResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiOrderQueryByOutTradeNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiOrderQueryByOutTradeNoRequest()

- WechatPayV3BasePayBasePayApisApiOrderQueryByOutTradeNoRequest OutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiOrderQueryByOutTradeNoRequest Mchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiOrderQueryByOutTradeNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiOrderQueryByOutTradeNoResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiOrderQueryByTransactionIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiOrderQueryByTransactionIdRequest()

- WechatPayV3BasePayBasePayApisApiOrderQueryByTransactionIdRequest TransactionId(string fieldValue)

- WechatPayV3BasePayBasePayApisApiOrderQueryByTransactionIdRequest Mchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiOrderQueryByTransactionIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiOrderQueryByTransactionIdResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiRefundQueryRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiRefundQueryRequest()

- WechatPayV3BasePayBasePayApisApiRefundQueryRequest OutRefundNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiRefundQueryRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiRefundQueryResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiRefundRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiRefundRequest()

- WechatPayV3BasePayBasePayApisApiRefundRequest TransactionId(string fieldValue)

- WechatPayV3BasePayBasePayApisApiRefundRequest OutTradeNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiRefundRequest OutRefundNo(string fieldValue)

- WechatPayV3BasePayBasePayApisApiRefundRequest Reason(string fieldValue)

- WechatPayV3BasePayBasePayApisApiRefundRequest NotifyUrl(string fieldValue)

- WechatPayV3BasePayBasePayApisApiRefundRequest FundsAccount(string fieldValue)

- WechatPayV3BasePayBasePayApisApiRefundRequest Amount(WechatPayBasePayRefundRequsetDataAmount fieldValue)

- WechatPayV3BasePayBasePayApisApiRefundRequest GoodsDetail(List<WechatPayBasePayRefundRequsetDataGoodsDetail> fieldValue)

- WechatPayV3BasePayBasePayApisApiRefundRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiRefundResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest()

- WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest SubMchid(string fieldValue)

- WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest BillDate(string fieldValue)

- WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest AccountType(string fieldValue)

- WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest Algorithm(string fieldValue)

- WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest TarType(string fieldValue)

- WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest FileStreams(JsonValue fieldValue)

- WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiSubmerchantFundflowBillQueryResponse (class)

- public string Raw;


## WechatPayV3BasePayBasePayApisApiTradeBillQueryRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BasePayBasePayApisApiTradeBillQueryRequest()

- WechatPayV3BasePayBasePayApisApiTradeBillQueryRequest BillDate(string fieldValue)

- WechatPayV3BasePayBasePayApisApiTradeBillQueryRequest BillType(string fieldValue)

- WechatPayV3BasePayBasePayApisApiTradeBillQueryRequest TarType(string fieldValue)

- WechatPayV3BasePayBasePayApisApiTradeBillQueryRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BasePayBasePayApisApiTradeBillQueryResponse (class)

- public string Raw;


## WechatPayV3BrandApplymentBrandApplymentApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3BrandApplymentBrandApplymentApisApi(WechatPayV3Client client)

- async WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentResponse SubmitApplymentAsync(WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentRequest request)
  - POST /v3/brand/applyments; C# 参数：BrandApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandApplymentBrandApplymentApisApiQueryByBusinessCodeResponse QueryByBusinessCodeAsync(WechatPayV3BrandApplymentBrandApplymentApisApiQueryByBusinessCodeRequest request)
  - GET /v3/brand/applyments/business-code/{Escape(businessCode)}; C# 参数：string businessCode, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryByBusinessCodeRawAsync(string path, string query)

- async WechatPayV3BrandApplymentBrandApplymentApisApiQueryByApplymentIdResponse QueryByApplymentIdAsync(WechatPayV3BrandApplymentBrandApplymentApisApiQueryByApplymentIdRequest request)
  - GET /v3/brand/applyments/applyment-id/{Escape(applymentId)}; C# 参数：string applymentId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryByApplymentIdRawAsync(string path, string query)

- async WechatPayV3BrandApplymentBrandApplymentApisApiCancelApplymentResponse CancelApplymentAsync(WechatPayV3BrandApplymentBrandApplymentApisApiCancelApplymentRequest request)
  - POST /v3/brand/applyments/cancel-applyment; C# 参数：BrandApplymentCancelRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CancelApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandApplymentBrandApplymentApisApiUploadImageResponse UploadImageAsync(WechatPayV3BrandApplymentBrandApplymentApisApiUploadImageRequest request, WechatMultipart multipart)
  - POST /v3/merchant/media/upload; C# 参数：string fileName, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadImageRawAsync(string path, string query, WechatMultipart multipart)


## WechatPayV3BrandApplymentBrandApplymentApisApiCancelApplymentRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandApplymentBrandApplymentApisApiCancelApplymentRequest()

- WechatPayV3BrandApplymentBrandApplymentApisApiCancelApplymentRequest BusinessCode(string fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiCancelApplymentRequest ApplymentId(string fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiCancelApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandApplymentBrandApplymentApisApiCancelApplymentResponse (class)

- public string Raw;


## WechatPayV3BrandApplymentBrandApplymentApisApiQueryByApplymentIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandApplymentBrandApplymentApisApiQueryByApplymentIdRequest()

- WechatPayV3BrandApplymentBrandApplymentApisApiQueryByApplymentIdRequest ApplymentId(string fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiQueryByApplymentIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandApplymentBrandApplymentApisApiQueryByApplymentIdResponse (class)

- public string Raw;


## WechatPayV3BrandApplymentBrandApplymentApisApiQueryByBusinessCodeRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandApplymentBrandApplymentApisApiQueryByBusinessCodeRequest()

- WechatPayV3BrandApplymentBrandApplymentApisApiQueryByBusinessCodeRequest BusinessCode(string fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiQueryByBusinessCodeRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandApplymentBrandApplymentApisApiQueryByBusinessCodeResponse (class)

- public string Raw;


## WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentRequest (class)

BrandApplyment/BrandApplymentApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentRequest()

- WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentRequest BusinessCode(string fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentRequest AdminInfo(WechatPayBrandApplymentAdminInfo fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentRequest SubjectInfo(WechatPayBrandApplymentSubjectInfo fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentRequest BrandBasicInfo(WechatPayBrandApplymentBasicInfo fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentRequest Trademark(WechatPayBrandApplymentTrademarkInfo fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandApplymentBrandApplymentApisApiSubmitApplymentResponse (class)

- public string Raw;


## WechatPayV3BrandApplymentBrandApplymentApisApiUploadImageRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandApplymentBrandApplymentApisApiUploadImageRequest()

- WechatPayV3BrandApplymentBrandApplymentApisApiUploadImageRequest FileName(string fieldValue)

- WechatPayV3BrandApplymentBrandApplymentApisApiUploadImageRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandApplymentBrandApplymentApisApiUploadImageResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3BrandCardBrandCardApisApi(WechatPayV3Client client)

- async WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigResponse SubmitCardConfigAsync(WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigRequest request)
  - POST /v3/brand/card/card-configs; C# 参数：BrandCardConfigRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitCardConfigRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandCardBrandCardApisApiPublishCardConfigResponse PublishCardConfigAsync(WechatPayV3BrandCardBrandCardApisApiPublishCardConfigRequest request)
  - POST /v3/brand/card/card-configs/publish; C# 参数：BrandCardConfigPublishRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse PublishCardConfigRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandCardBrandCardApisApiCancelCardConfigApplymentResponse CancelCardConfigApplymentAsync(WechatPayV3BrandCardBrandCardApisApiCancelCardConfigApplymentRequest request)
  - POST /v3/brand/card/card-configs/cancel-applyment; C# 参数：BrandCardConfigApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CancelCardConfigApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandCardBrandCardApisApiQueryCardConfigApplymentResponse QueryCardConfigApplymentAsync(WechatPayV3BrandCardBrandCardApisApiQueryCardConfigApplymentRequest request)
  - GET /v3/brand/card/card-configs; C# 参数：BrandCardConfigApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCardConfigApplymentRawAsync(string path, string query)

- async WechatPayV3BrandCardBrandCardApisApiGetCardPreviewUrlResponse GetCardPreviewUrlAsync(WechatPayV3BrandCardBrandCardApisApiGetCardPreviewUrlRequest request)
  - GET /v3/brand/card/card-configs/preview-url; C# 参数：BrandCardConfigApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GetCardPreviewUrlRawAsync(string path, string query)

- async WechatPayV3BrandCardBrandCardApisApiAddCardLinkResponse AddCardLinkAsync(WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest request)
  - POST /v3/brand/card/card-links; C# 参数：BrandCardLinkRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AddCardLinkRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkResponse UnbindCardLinkAsync(WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkRequest request)
  - POST /v3/brand/card/card-links/unbind-card-link; C# 参数：BrandCardLinkUnbindRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UnbindCardLinkRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandCardBrandCardApisApiCancelCardLinkApplymentResponse CancelCardLinkApplymentAsync(WechatPayV3BrandCardBrandCardApisApiCancelCardLinkApplymentRequest request)
  - POST /v3/brand/card/card-links/cancel-applyment; C# 参数：BrandCardLinkCancelRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CancelCardLinkApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksResponse QueryActiveCardLinksAsync(WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksRequest request)
  - GET /v3/brand/card/card-links; C# 参数：BrandCardActiveLinksQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryActiveCardLinksRawAsync(string path, string query)

- async WechatPayV3BrandCardBrandCardApisApiQueryCardLinkApplymentByBusinessCodeResponse QueryCardLinkApplymentByBusinessCodeAsync(WechatPayV3BrandCardBrandCardApisApiQueryCardLinkApplymentByBusinessCodeRequest request)
  - GET /v3/brand/card/card-links/business-code/{EscapeBrandCardValue(businessCode)}; C# 参数：string businessCode, string brandId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCardLinkApplymentByBusinessCodeRawAsync(string path, string query)


## WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest()

- WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest BusinessCode(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest PaymentScene(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest Appid(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest CardLinkMchid(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest ServiceId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiAddCardLinkRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiAddCardLinkResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApiCancelCardConfigApplymentRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiCancelCardConfigApplymentRequest()

- WechatPayV3BrandCardBrandCardApisApiCancelCardConfigApplymentRequest BusinessCode(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiCancelCardConfigApplymentRequest ApplymentId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiCancelCardConfigApplymentRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiCancelCardConfigApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiCancelCardConfigApplymentResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApiCancelCardLinkApplymentRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiCancelCardLinkApplymentRequest()

- WechatPayV3BrandCardBrandCardApisApiCancelCardLinkApplymentRequest BusinessCode(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiCancelCardLinkApplymentRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiCancelCardLinkApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiCancelCardLinkApplymentResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApiGetCardPreviewUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiGetCardPreviewUrlRequest()

- WechatPayV3BrandCardBrandCardApisApiGetCardPreviewUrlRequest BusinessCode(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiGetCardPreviewUrlRequest ApplymentId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiGetCardPreviewUrlRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiGetCardPreviewUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiGetCardPreviewUrlResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApiPublishCardConfigRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiPublishCardConfigRequest()

- WechatPayV3BrandCardBrandCardApisApiPublishCardConfigRequest BusinessCode(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiPublishCardConfigRequest ApplymentId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiPublishCardConfigRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiPublishCardConfigRequest PublishType(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiPublishCardConfigRequest ScheduledPublishTime(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiPublishCardConfigRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiPublishCardConfigResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksRequest()

- WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksRequest PaymentScene(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksRequest PageIndex(int fieldValue)

- WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksRequest PageSize(int fieldValue)

- WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiQueryActiveCardLinksResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApiQueryCardConfigApplymentRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiQueryCardConfigApplymentRequest()

- WechatPayV3BrandCardBrandCardApisApiQueryCardConfigApplymentRequest BusinessCode(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiQueryCardConfigApplymentRequest ApplymentId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiQueryCardConfigApplymentRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiQueryCardConfigApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiQueryCardConfigApplymentResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApiQueryCardLinkApplymentByBusinessCodeRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiQueryCardLinkApplymentByBusinessCodeRequest()

- WechatPayV3BrandCardBrandCardApisApiQueryCardLinkApplymentByBusinessCodeRequest BusinessCode(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiQueryCardLinkApplymentByBusinessCodeRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiQueryCardLinkApplymentByBusinessCodeRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiQueryCardLinkApplymentByBusinessCodeResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigRequest (class)

BrandCard/BrandCardApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigRequest()

- WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigRequest BusinessCode(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigRequest BrandMiniProgramInfo(WechatPayBrandCardMiniProgramInfo fieldValue)

- WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigRequest BrandCustomerService(WechatPayBrandCardCustomerServiceInfo fieldValue)

- WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigRequest ServiceList(List<WechatPayBrandCardServiceInfo> fieldValue)

- WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiSubmitCardConfigResponse (class)

- public string Raw;


## WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkRequest()

- WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkRequest BrandId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkRequest PaymentScene(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkRequest Appid(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkRequest CardLinkMchid(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkRequest ServiceId(string fieldValue)

- WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandCardBrandCardApisApiUnbindCardLinkResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApi(WechatPayV3Client client)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardResponse CreateCardAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest request)
  - POST /v3/card-member/cards; C# 参数：BrandMemberCardCreateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateCardRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardsResponse QueryCardsAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardsRequest request)
  - GET /v3/card-member/cards; C# 参数：BrandMemberCardListQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCardsRawAsync(string path, string query)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardResponse QueryCardAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardRequest request)
  - GET /v3/card-member/cards/{值}; C# 参数：string cardId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCardRawAsync(string path, string query)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardResponse UpdateCardAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest request)
  - PATCH /v3/card-member/cards/{值}; C# 参数：string cardId, BrandMemberCardUpdateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateCardRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateCardResponse InvalidateCardAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateCardRequest request)
  - POST /v3/card-member/cards/{值}; C# 参数：string cardId, int timeOut = Config.TIME_OUT

- async WechatRawResponse InvalidateCardRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardResponse QueryUserCardAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardRequest request)
  - GET /v3/card-member/user-cards/{值}; C# 参数：string userCardCode, BrandMemberCardUserCardQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryUserCardRawAsync(string path, string query)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsResponse QueryUserCardsAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsRequest request)
  - GET /v3/card-member/user-cards; C# 参数：BrandMemberCardUserCardListQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryUserCardsRawAsync(string path, string query)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardResponse UpdateUserCardAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest request)
  - PATCH /v3/card-member/user-cards/{值}; C# 参数：string userCardCode, BrandMemberCardUserCardUpdateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateUserCardRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardResponse InvalidateUserCardAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardRequest request)
  - POST /v3/card-member/user-cards/{值}; C# 参数：string userCardCode, BrandMemberCardUserCardInvalidateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse InvalidateUserCardRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiCreatePreAuthTokenResponse CreatePreAuthTokenAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiCreatePreAuthTokenRequest request)
  - POST /v3/card-member/pre-auth-tokens; C# 参数：BrandMemberCardPreAuthTokenRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreatePreAuthTokenRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdResponse ImportUserCardByOpenIdAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest request)
  - POST /v3/card-member/user-cards/import-by-openid; C# 参数：BrandMemberCardUserCardImportRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ImportUserCardByOpenIdRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardResponse ConfirmUserCardAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest request)
  - POST /v3/card-member/user-cards/{值}; C# 参数：string userCardCode, BrandMemberCardUserCardConfirmRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ConfirmUserCardRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedResponse CreateUserFeedAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedRequest request)
  - POST /v3/card-member/user-feeds; C# 参数：BrandMemberCardUserFeedRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateUserFeedRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsResponse SyncUserPointsAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsRequest request)
  - POST /v3/card-member/user-points/sync; C# 参数：BrandMemberCardPointBalanceRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SyncUserPointsRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponResponse ConfirmPointExchangeCouponAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest request)
  - POST /v3/card-member/user-points/exchange-coupon/confirm; C# 参数：BrandMemberCardPointExchangeRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ConfirmPointExchangeCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandMemberCardBrandMemberCardApisApiUploadMemberImageResponse UploadMemberImageAsync(WechatPayV3BrandMemberCardBrandMemberCardApisApiUploadMemberImageRequest request, WechatMultipart multipart)
  - POST /v3/card-member/media/图片-upload; C# 参数：string fileName, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadMemberImageRawAsync(string path, string query, WechatMultipart multipart)


## WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest RecordId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest ExchangeCouponTemplateId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest UserCardCode(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest Result(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest RejectReason(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmPointExchangeCouponResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest UserCardConfirmState(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest CardColor(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest CardPictureUrl(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest PhoneNumber(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest Level(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest ValidDateInformation(WechatPayBrandMemberCardValidDateInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest UserInformation(WechatPayBrandMemberCardUserProfileInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest Attach(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest UserCardCode(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiConfirmUserCardResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest (class)

BrandMemberCard/BrandMemberCardApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest OutRequestNo(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest Appid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest CardType(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest CardTitle(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest CardColor(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest CardPictureUrl(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest CodeMode(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest CodeType(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest CodeJumpInformation(WechatPayBrandMemberCardJumpInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest Benefits(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest NotifyUrl(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest NeedPinned(bool fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest NeedDisplayLevel(bool fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest InitLevel(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest ServicePhone(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest LegalAgreement(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest ValidDateInformation(WechatPayBrandMemberCardValidDateInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest MemberInformation(WechatPayBrandMemberCardJumpInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest PointsInformation(WechatPayBrandMemberCardJumpInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest BalanceInformation(WechatPayBrandMemberCardJumpInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest PurchaseInformation(WechatPayBrandMemberCardPurchaseInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest UserInformation(WechatPayBrandMemberCardUserInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateCardResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiCreatePreAuthTokenRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiCreatePreAuthTokenRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreatePreAuthTokenRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreatePreAuthTokenRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreatePreAuthTokenRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiCreatePreAuthTokenResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedRequest UserCardCode(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedRequest OutRequestNo(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedRequest Cell(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiCreateUserFeedResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest UserCardCode(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest PhoneNumber(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest CardColor(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest CardPictureUrl(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest Level(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest UserInformation(WechatPayBrandMemberCardUserProfileInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest ValidDateInformation(WechatPayBrandMemberCardValidDateInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest PickupTime(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiImportUserCardByOpenIdResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateCardRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateCardRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateCardRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateCardRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateCardResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardRequest InvalidReason(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardRequest UserCardCode(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiInvalidateUserCardResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardsRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardsRequest State(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardsRequest Offset(int fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardsRequest Limit(int fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryCardsResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardRequest UserCardCode(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsRequest UserCardState(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsRequest Offset(int fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsRequest Limit(int fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiQueryUserCardsResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsRequest OutRequestNo(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsRequest UserCardCode(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsRequest PointBalance(long fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiSyncUserPointsResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest CardTitle(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest CardColor(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest CardPictureUrl(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest CodeJumpInformation(WechatPayBrandMemberCardJumpInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest Benefits(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest NotifyUrl(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest NeedPinned(bool fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest NeedDisplayLevel(bool fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest ServicePhone(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest ValidDateInformation(WechatPayBrandMemberCardValidDateInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest MemberInformation(WechatPayBrandMemberCardJumpInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest PointsInformation(WechatPayBrandMemberCardJumpInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest BalanceInformation(WechatPayBrandMemberCardJumpInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest PurchaseInformation(WechatPayBrandMemberCardPurchaseInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest UserInformation(WechatPayBrandMemberCardUserInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateCardResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest CardId(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest Openid(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest CardColor(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest CardPictureUrl(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest PhoneNumber(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest Level(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest ValidDateInformation(WechatPayBrandMemberCardValidDateInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest UserInformation(WechatPayBrandMemberCardUserProfileInformation fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest Attach(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest UserCardCode(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiUpdateUserCardResponse (class)

- public string Raw;


## WechatPayV3BrandMemberCardBrandMemberCardApisApiUploadMemberImageRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandMemberCardBrandMemberCardApisApiUploadMemberImageRequest()

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUploadMemberImageRequest FileName(string fieldValue)

- WechatPayV3BrandMemberCardBrandMemberCardApisApiUploadMemberImageRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandMemberCardBrandMemberCardApisApiUploadMemberImageResponse (class)

- public string Raw;


## WechatPayV3BrandStoreBrandStoreApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3BrandStoreBrandStoreApisApi(WechatPayV3Client client)

- async WechatPayV3BrandStoreBrandStoreApisApiCreateBrandStoreResponse CreateBrandStoreAsync(WechatPayV3BrandStoreBrandStoreApisApiCreateBrandStoreRequest request)
  - POST /v3/store/brandstores; C# 参数：BrandStoreCreateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateBrandStoreRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoreResponse QueryBrandStoreAsync(WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoreRequest request)
  - GET /v3/store/brandstores/{值}; C# 参数：string storeId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBrandStoreRawAsync(string path, string query)

- async WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoresResponse QueryBrandStoresAsync(WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoresRequest request)
  - GET /v3/store/brandstores; C# 参数：BrandStoreListQueryRequestData 数据 = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBrandStoresRawAsync(string path, string query)

- async WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreResponse UpdateBrandStoreAsync(WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreRequest request)
  - PATCH /v3/store/brandstores/{值}; C# 参数：string storeId, BrandStoreUpdateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateBrandStoreRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandStoreBrandStoreApisApiDeleteBrandStoreResponse DeleteBrandStoreAsync(WechatPayV3BrandStoreBrandStoreApisApiDeleteBrandStoreRequest request)
  - DELETE /v3/store/brandstores/{值}; C# 参数：string storeId, int timeOut = Config.TIME_OUT

- async WechatRawResponse DeleteBrandStoreRawAsync(string path, string query)

- async WechatPayV3BrandStoreBrandStoreApisApiCloseBrandStoreResponse CloseBrandStoreAsync(WechatPayV3BrandStoreBrandStoreApisApiCloseBrandStoreRequest request)
  - POST /v3/store/brandstores/{值}; C# 参数：string storeId, int timeOut = Config.TIME_OUT

- async WechatRawResponse CloseBrandStoreRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandStoreBrandStoreApisApiResumeBrandStoreResponse ResumeBrandStoreAsync(WechatPayV3BrandStoreBrandStoreApisApiResumeBrandStoreRequest request)
  - POST /v3/store/brandstores/{值}; C# 参数：string storeId, int timeOut = Config.TIME_OUT

- async WechatRawResponse ResumeBrandStoreRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandStoreBrandStoreApisApiBindRecipientResponse BindRecipientAsync(WechatPayV3BrandStoreBrandStoreApisApiBindRecipientRequest request)
  - POST /v3/store/brandstores/{值}; C# 参数：string storeId, BrandStoreBindRecipientRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse BindRecipientRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BrandStoreBrandStoreApisApiUnbindRecipientResponse UnbindRecipientAsync(WechatPayV3BrandStoreBrandStoreApisApiUnbindRecipientRequest request)
  - POST /v3/store/brandstores/{值}; C# 参数：string storeId, BrandStoreUnbindRecipientRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UnbindRecipientRawAsync(string path, string query, string jsonBody)


## WechatPayV3BrandStoreBrandStoreApisApiBindRecipientRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandStoreBrandStoreApisApiBindRecipientRequest()

- WechatPayV3BrandStoreBrandStoreApisApiBindRecipientRequest Mchid(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiBindRecipientRequest CompanyName(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiBindRecipientRequest StoreId(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiBindRecipientRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandStoreBrandStoreApisApiBindRecipientResponse (class)

- public string Raw;


## WechatPayV3BrandStoreBrandStoreApisApiCloseBrandStoreRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandStoreBrandStoreApisApiCloseBrandStoreRequest()

- WechatPayV3BrandStoreBrandStoreApisApiCloseBrandStoreRequest StoreId(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiCloseBrandStoreRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandStoreBrandStoreApisApiCloseBrandStoreResponse (class)

- public string Raw;


## WechatPayV3BrandStoreBrandStoreApisApiCreateBrandStoreRequest (class)

BrandStore/BrandStoreApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandStoreBrandStoreApisApiCreateBrandStoreRequest()

- WechatPayV3BrandStoreBrandStoreApisApiCreateBrandStoreRequest StoreBasics(WechatPayBrandStoreBasics fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiCreateBrandStoreRequest StoreAddress(WechatPayBrandStoreAddress fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiCreateBrandStoreRequest StoreBusiness(WechatPayBrandStoreBusiness fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiCreateBrandStoreRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandStoreBrandStoreApisApiCreateBrandStoreResponse (class)

- public string Raw;


## WechatPayV3BrandStoreBrandStoreApisApiDeleteBrandStoreRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandStoreBrandStoreApisApiDeleteBrandStoreRequest()

- WechatPayV3BrandStoreBrandStoreApisApiDeleteBrandStoreRequest StoreId(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiDeleteBrandStoreRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandStoreBrandStoreApisApiDeleteBrandStoreResponse (class)

- public string Raw;


## WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoreRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoreRequest()

- WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoreRequest StoreId(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoreRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoreResponse (class)

- public string Raw;


## WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoresRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoresRequest()

- WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoresRequest StoreState(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoresRequest Offset(int fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoresRequest Limit(int fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoresRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandStoreBrandStoreApisApiQueryBrandStoresResponse (class)

- public string Raw;


## WechatPayV3BrandStoreBrandStoreApisApiResumeBrandStoreRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandStoreBrandStoreApisApiResumeBrandStoreRequest()

- WechatPayV3BrandStoreBrandStoreApisApiResumeBrandStoreRequest StoreId(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiResumeBrandStoreRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandStoreBrandStoreApisApiResumeBrandStoreResponse (class)

- public string Raw;


## WechatPayV3BrandStoreBrandStoreApisApiUnbindRecipientRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandStoreBrandStoreApisApiUnbindRecipientRequest()

- WechatPayV3BrandStoreBrandStoreApisApiUnbindRecipientRequest Mchid(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiUnbindRecipientRequest StoreId(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiUnbindRecipientRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandStoreBrandStoreApisApiUnbindRecipientResponse (class)

- public string Raw;


## WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreRequest()

- WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreRequest StoreBasics(WechatPayBrandStoreBasics fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreRequest StoreAddress(WechatPayBrandStoreAddress fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreRequest StoreBusiness(WechatPayBrandStoreBusiness fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreRequest StoreId(string fieldValue)

- WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BrandStoreBrandStoreApisApiUpdateBrandStoreResponse (class)

- public string Raw;


## WechatPayV3BusinessCircleBusinessCircleApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3BusinessCircleBusinessCircleApisApi(WechatPayV3Client client)

- async WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsResponse NotifyBusinessCirclePointsAsync(WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest request)
  - POST /v3/businesscircle/points/notify; C# 参数：NotifyBusinessCirclePointsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse NotifyBusinessCirclePointsRawAsync(string path, string query, string jsonBody)

- async WechatPayV3BusinessCircleBusinessCircleApisApiQueryUserAuthorizationResponse QueryUserAuthorizationAsync(WechatPayV3BusinessCircleBusinessCircleApisApiQueryUserAuthorizationRequest request)
  - GET /v3/businesscircle/user-authorizations/{openid}?appid={appid}; C# 参数：string appid, string openid, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryUserAuthorizationRawAsync(string path, string query)


## WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest (class)

BusinessCircle/BusinessCircleApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest()

- WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest TransactionId(string fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest Appid(string fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest Openid(string fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest EarnPoints(bool fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest IncreasedPoints(int fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest PointsUpdateTime(string fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest NoPointsRemarks(string fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest TotalPoints(int fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BusinessCircleBusinessCircleApisApiNotifyBusinessCirclePointsResponse (class)

- public string Raw;


## WechatPayV3BusinessCircleBusinessCircleApisApiQueryUserAuthorizationRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3BusinessCircleBusinessCircleApisApiQueryUserAuthorizationRequest()

- WechatPayV3BusinessCircleBusinessCircleApisApiQueryUserAuthorizationRequest Appid(string fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiQueryUserAuthorizationRequest Openid(string fieldValue)

- WechatPayV3BusinessCircleBusinessCircleApisApiQueryUserAuthorizationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3BusinessCircleBusinessCircleApisApiQueryUserAuthorizationResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApi(WechatPayV3Client client)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderResponse CreateOrderAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest request)
  - POST /v3/brand/profitsharing/orders; C# 参数：ChainBrandProfitsharingCreateOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryOrderResponse QueryOrderAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryOrderRequest request)
  - GET /v3/brand/profitsharing/orders; C# 参数：ChainBrandProfitsharingOrderQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryOrderRawAsync(string path, string query)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderResponse CreateReturnOrderAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest request)
  - POST /v3/brand/profitsharing/returnorders; C# 参数：ChainBrandProfitsharingReturnOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateReturnOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderResponse QueryReturnOrderAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderRequest request)
  - GET /v3/brand/profitsharing/returnorders; C# 参数：ChainBrandProfitsharingReturnOrderQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryReturnOrderRawAsync(string path, string query)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderResponse FinishOrderAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderRequest request)
  - POST /v3/brand/profitsharing/finish-顺序; C# 参数：ChainBrandProfitsharingFinishOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse FinishOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryAmountsResponse QueryAmountsAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryAmountsRequest request)
  - GET /v3/brand/profitsharing/orders/; C# 参数：string transactionId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryAmountsRawAsync(string path, string query)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryBrandConfigResponse QueryBrandConfigAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryBrandConfigRequest request)
  - GET /v3/brand/profitsharing/brand-configs/; C# 参数：string brandMchid, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBrandConfigRawAsync(string path, string query)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverResponse AddReceiverAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest request)
  - POST /v3/brand/profitsharing/receivers/添加; C# 参数：ChainBrandProfitsharingAddReceiverRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AddReceiverRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverResponse DeleteReceiverAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverRequest request)
  - POST /v3/brand/profitsharing/receivers/删除; C# 参数：ChainBrandProfitsharingDeleteReceiverRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DeleteReceiverRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiApplyBillResponse ApplyBillAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiApplyBillRequest request)
  - GET /v3/profitsharing/bills; C# 参数：ChainBrandProfitsharingBillRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyBillRawAsync(string path, string query)

- async WechatRawResponse DownloadBillAsync(WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillRequest request)
  - POST (请求.RequestPath 指定); C# 参数：ChainBrandProfitsharingBillResultJson bill, Stream destination, int timeOut = Config.TIME_OUT

- async WechatRawResponse DownloadBillRawAsync(string path, string query, string jsonBody)


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest BrandMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest Appid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest SubAppid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest Type(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest Account(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest Name(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest RelationType(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiAddReceiverResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiApplyBillRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiApplyBillRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiApplyBillRequest SubMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiApplyBillRequest BillDate(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiApplyBillRequest TarType(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiApplyBillRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiApplyBillResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest (class)

ChainBrandProfitsharing/ChainBrandProfitsharingApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest BrandMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest SubMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest Appid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest SubAppid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest TransactionId(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest Receivers(List<WechatPayChainBrandProfitsharingReceiverRequestData> fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest Finish(bool fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateOrderResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest SubMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest OrderId(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest OutReturnNo(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest ReturnMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest Amount(long fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest Description(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiCreateReturnOrderResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverRequest BrandMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverRequest Appid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverRequest SubAppid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverRequest Type(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverRequest Account(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDeleteReceiverResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillRequest HashType(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillRequest HashValue(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillRequest DownloadUrl(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillRequest ResultCode(WechatPayTenPayApiResultCode fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillRequest VerifySignSuccess(bool fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiDownloadBillResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderRequest SubMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderRequest TransactionId(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderRequest Description(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiFinishOrderResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryAmountsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryAmountsRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryAmountsRequest TransactionId(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryAmountsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryAmountsResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryBrandConfigRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryBrandConfigRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryBrandConfigRequest BrandMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryBrandConfigRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryBrandConfigResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryOrderRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryOrderRequest SubMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryOrderRequest TransactionId(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryOrderResponse (class)

- public string Raw;


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderRequest()

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderRequest SubMchid(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderRequest OutReturnNo(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderRequest OrderId(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ChainBrandProfitsharingChainBrandProfitsharingApisApiQueryReturnOrderResponse (class)

- public string Raw;


## WechatPayV3Client (class)

微信支付 API v3 的 WECHATPAY2 签名、验签、平台证书轮换、敏感字段和通知解密客户端。

- WechatPayV3Config config;

- WechatApiTransport transport;

- RsaKey merchantKey;

- WechatPayPlatformKeyStore platformKeys;

- public WechatPayV3Client(WechatPayV3Config config)

- WechatPayV3Client Server(string host, int port)

- WechatPayV3Client Timeout(int ms)

- WechatPayV3Client SetPlatformPublicKey(string serial, string publicKeyOrCertificatePem)

- bool HasPlatformPublicKey(string serial)

- string GetPlatformPublicKeyPem(string serial)

- string BuildAuthorization(string method, string target, string body, long timestamp, string nonce)

- async WechatRawResponse RequestAsync(string method, string path, string query, string body, string contentType)

- async WechatRawResponse RequestJsonAsync(string method, string path, string query, string jsonBody)

- async WechatRawResponse RequestMultipartAsync(string method, string path, string query, WechatMultipart multipart)

- async WechatRawResponse DownloadAsync(string url)
  - 下载微信返回的短期签名 URL；此请求不附加商户 Authorization。

- bool VerifyResponse(WechatRawResponse response)

- bool VerifyNotification(string timestamp, string nonce, string body, string signature, string serial)

- static bool VerifyNotificationWithPublicKey(string timestamp, string nonce, string body, string signature, string serial, string expectedSerial, string publicKeyPem)

- RsaKey PlatformKey(string serial)

- string DecryptResource(string ciphertextBase64, string nonce, string associatedData)

- static string DecryptResourceWithKey(string apiKey, string ciphertextBase64, string nonce, string associatedData)

- string EncryptSensitive(string plaintext)

- string EncryptSensitiveWithSerial(string plaintext, string serial)

- string SignAppMessage(string appId, string packageValue, long timestamp, string nonce)

- async WechatRawResponse DownloadCertificatesAsync(string algorithmType)
  - 下载微信支付平台证书；algorithmType 为 RSA、SM2 或 ALL。

- async int RefreshPlatformCertificatesAsync(string algorithmType)
  - 下载并解密 RSA 平台证书，加入按序列号缓存。

- int LoadCertificatesResponse(string json)
  - 装载 /v3/certificates 的 JSON 响应并返回新增/更新数量。

- int LoadPublicKeysResponse(string json)
  - 装载直接返回 public_key/public_key_id 的平台公钥 JSON。

- int LoadPublicKeyItem(JsonValue item)


## WechatPayV3ComplaintComplaintApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3ComplaintComplaintApisApi(WechatPayV3Client client)

- async WechatPayV3ComplaintComplaintApisApiQueryComplaintsResponse QueryComplaintsAsync(WechatPayV3ComplaintComplaintApisApiQueryComplaintsRequest request)
  - GET /v3/merchant-service/complaints-v2?limit={limit}&偏移={偏移}&begin_date={begin_date?.ToString()}&end_date={end_date?.ToString()}; C# 参数：TenpayDateTime begin_date, TenpayDateTime end_date, string complainted_mchid, int limit = 10, int 偏移 = 0, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryComplaintsRawAsync(string path, string query)

- async WechatPayV3ComplaintComplaintApisApiQueryComplaintResponse QueryComplaintAsync(WechatPayV3ComplaintComplaintApisApiQueryComplaintRequest request)
  - GET /v3/merchant-service/complaints-v2/{complaint_id}; C# 参数：string complaint_id, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryComplaintRawAsync(string path, string query)

- async WechatPayV3ComplaintComplaintApisApiQueryNegotiationHistorysResponse QueryNegotiationHistorysAsync(WechatPayV3ComplaintComplaintApisApiQueryNegotiationHistorysRequest request)
  - GET /v3/merchant-service/complaints-v2/{complaint_id}/negotiation-historys?limit={limit}&偏移={偏移}; C# 参数：string complaint_id, int limit = 10, int 偏移 = 0, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryNegotiationHistorysRawAsync(string path, string query)

- async WechatPayV3ComplaintComplaintApisApiCreateComplaintNotifyUrlResponse CreateComplaintNotifyUrlAsync(WechatPayV3ComplaintComplaintApisApiCreateComplaintNotifyUrlRequest request)
  - POST /v3/merchant-service/complaint-notifications; C# 参数：CreateComplaintNotifyUrlRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateComplaintNotifyUrlRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ComplaintComplaintApisApiQueryComplaintNotifyUrlResponse QueryComplaintNotifyUrlAsync(WechatPayV3ComplaintComplaintApisApiQueryComplaintNotifyUrlRequest request)
  - GET /v3/merchant-service/complaint-notifications; C# 参数：int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryComplaintNotifyUrlRawAsync(string path, string query)

- async WechatPayV3ComplaintComplaintApisApiModifyComplaintNotifyUrlResponse ModifyComplaintNotifyUrlAsync(WechatPayV3ComplaintComplaintApisApiModifyComplaintNotifyUrlRequest request)
  - POST /v3/merchant-service/complaint-notifications; C# 参数：ModifyComplaintNotifyUrlRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifyComplaintNotifyUrlRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ComplaintComplaintApisApiDeleteComplaintNotifyUrlResponse DeleteComplaintNotifyUrlAsync(WechatPayV3ComplaintComplaintApisApiDeleteComplaintNotifyUrlRequest request)
  - DELETE /v3/merchant-service/complaint-notifications; C# 参数：int timeOut = Config.TIME_OUT

- async WechatRawResponse DeleteComplaintNotifyUrlRawAsync(string path, string query)

- async WechatPayV3ComplaintComplaintApisApiResponseResponse ResponseAsync(WechatPayV3ComplaintComplaintApisApiResponseRequest request)
  - POST /v3/merchant-service/complaints-v2/{数据.complaint_id}/响应; C# 参数：ResponseRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ResponseRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ComplaintComplaintApisApiCompleteComplaintResponse CompleteComplaintAsync(WechatPayV3ComplaintComplaintApisApiCompleteComplaintRequest request)
  - POST /v3/merchant-service/complaints-v2/{数据.complaint_id}/complete; C# 参数：CompleteComplaintRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CompleteComplaintRawAsync(string path, string query, string jsonBody)


## WechatPayV3ComplaintComplaintApisApiCompleteComplaintRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisApiCompleteComplaintRequest()

- WechatPayV3ComplaintComplaintApisApiCompleteComplaintRequest ComplaintId(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiCompleteComplaintRequest ComplaintedMchid(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiCompleteComplaintRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisApiCompleteComplaintResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisApiCreateComplaintNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisApiCreateComplaintNotifyUrlRequest()

- WechatPayV3ComplaintComplaintApisApiCreateComplaintNotifyUrlRequest Url(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiCreateComplaintNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisApiCreateComplaintNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisApiDeleteComplaintNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisApiDeleteComplaintNotifyUrlRequest()

- WechatPayV3ComplaintComplaintApisApiDeleteComplaintNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisApiDeleteComplaintNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisApiModifyComplaintNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisApiModifyComplaintNotifyUrlRequest()

- WechatPayV3ComplaintComplaintApisApiModifyComplaintNotifyUrlRequest Url(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiModifyComplaintNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisApiModifyComplaintNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisApiQueryComplaintNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisApiQueryComplaintNotifyUrlRequest()

- WechatPayV3ComplaintComplaintApisApiQueryComplaintNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisApiQueryComplaintNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisApiQueryComplaintRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisApiQueryComplaintRequest()

- WechatPayV3ComplaintComplaintApisApiQueryComplaintRequest ComplaintId(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiQueryComplaintRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisApiQueryComplaintResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisApiQueryComplaintsRequest (class)

Complaint/ComplaintApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisApiQueryComplaintsRequest()

- WechatPayV3ComplaintComplaintApisApiQueryComplaintsRequest BeginDate(WechatPayTenpayDateTime fieldValue)

- WechatPayV3ComplaintComplaintApisApiQueryComplaintsRequest EndDate(WechatPayTenpayDateTime fieldValue)

- WechatPayV3ComplaintComplaintApisApiQueryComplaintsRequest ComplaintedMchid(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiQueryComplaintsRequest Limit(int fieldValue)

- WechatPayV3ComplaintComplaintApisApiQueryComplaintsRequest Offset(int fieldValue)

- WechatPayV3ComplaintComplaintApisApiQueryComplaintsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisApiQueryComplaintsResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisApiQueryNegotiationHistorysRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisApiQueryNegotiationHistorysRequest()

- WechatPayV3ComplaintComplaintApisApiQueryNegotiationHistorysRequest ComplaintId(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiQueryNegotiationHistorysRequest Limit(int fieldValue)

- WechatPayV3ComplaintComplaintApisApiQueryNegotiationHistorysRequest Offset(int fieldValue)

- WechatPayV3ComplaintComplaintApisApiQueryNegotiationHistorysRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisApiQueryNegotiationHistorysResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisApiResponseRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisApiResponseRequest()

- WechatPayV3ComplaintComplaintApisApiResponseRequest ComplaintId(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiResponseRequest ComplaintedMchid(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiResponseRequest ResponseContent(string fieldValue)

- WechatPayV3ComplaintComplaintApisApiResponseRequest ResponseImages(List<string> fieldValue)

- WechatPayV3ComplaintComplaintApisApiResponseRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisApiResponseResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisP1Api (class)

- WechatPayV3Client client;

- public WechatPayV3ComplaintComplaintApisP1Api(WechatPayV3Client client)

- async WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressResponse UpdateRefundProgressAsync(WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest request)
  - POST /v3/merchant-service/complaints-v2/{escapedComplaintId}/更新-refund-progress; C# 参数：string complaintId, UpdateRefundProgressRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateRefundProgressRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceResponse ResponseImmediateServiceAsync(WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceRequest request)
  - POST /v3/merchant-service/complaints-v2/{escapedComplaintId}/响应-immediate-service; C# 参数：string complaintId, ImmediateServiceRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ResponseImmediateServiceRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ComplaintComplaintApisP1ApiUploadImageResponse UploadImageAsync(WechatPayV3ComplaintComplaintApisP1ApiUploadImageRequest request, WechatMultipart multipart)
  - POST /v3/merchant-service/图片/upload; C# 参数：string fileName, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadImageRawAsync(string path, string query, WechatMultipart multipart)

- async WechatRawResponse DownloadImageAsync(WechatPayV3ComplaintComplaintApisP1ApiDownloadImageRequest request)
  - POST /v3/merchant-service/图片/{escapedMediaId}; C# 参数：string mediaId, Stream destination, int timeOut = Config.TIME_OUT

- async WechatRawResponse DownloadImageRawAsync(string path, string query, string jsonBody)


## WechatPayV3ComplaintComplaintApisP1ApiDownloadImageRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisP1ApiDownloadImageRequest()

- WechatPayV3ComplaintComplaintApisP1ApiDownloadImageRequest MediaId(string fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiDownloadImageRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisP1ApiDownloadImageResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceRequest()

- WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceRequest ComplaintedMchid(string fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceRequest Message(WechatPayImmediateServiceMessage fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceRequest IdempotentId(string fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceRequest ComplaintId(string fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisP1ApiResponseImmediateServiceResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest (class)

Complaint/ComplaintApis.P1.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest()

- WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest Action(string fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest LaunchRefundDay(int fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest RejectReason(string fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest RejectMediaList(List<string> fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest Remark(string fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest ComplaintId(string fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisP1ApiUpdateRefundProgressResponse (class)

- public string Raw;


## WechatPayV3ComplaintComplaintApisP1ApiUploadImageRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ComplaintComplaintApisP1ApiUploadImageRequest()

- WechatPayV3ComplaintComplaintApisP1ApiUploadImageRequest FileName(string fieldValue)

- WechatPayV3ComplaintComplaintApisP1ApiUploadImageRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ComplaintComplaintApisP1ApiUploadImageResponse (class)

- public string Raw;


## WechatPayV3Config (class)

微信支付 API v3 商户配置。这里保存商户部署时提供的长期配置；
每笔订单、退款等业务数据由具体 API 的强类型 Request 提供。

- string AppId;

- string AppSecret;

- string MerchantId;

- string MerchantKey;

- string MerchantSerialNumber;

- string MerchantPrivateKeyPem;

- string ApiV3Key;

- string ClientCertificateFile;

- string ClientPrivateKeyFile;

- string NotifyUrl;

- string WxOpenNotifyUrl;

- string SubAppId;

- string SubAppSecret;

- string SubMerchantId;

- string PlatformSerialNumber;

- string PlatformPublicKeyPem;

- bool RequireResponseSignature;

- public WechatPayV3Config(string appId, string merchantId, string merchantSerialNumber, string merchantPrivateKeyPem, string apiV3Key)

- WechatPayV3Config SetAppSecret(string appSecret)

- WechatPayV3Config SetLegacyMerchantKey(string merchantKey)

- WechatPayV3Config SetLegacyCertificate(string certFile, string keyFile)

- WechatPayV3Config SetNotifyUrl(string url)

- WechatPayV3Config SetNotifyUrls(string payUrl, string wxOpenUrl)

- WechatPayV3Config SetServiceProvider(string subAppId, string subMerchantId)

- WechatPayV3Config SetServiceProviderCredentials(string subAppId, string subAppSecret, string subMerchantId)

- WechatPayV3Config SetPlatformPublicKey(string serial, string publicKeyPem)

- WechatPayV3Config AllowUnsignedResponse()


## WechatPayV3DeliveryPlanDeliveryPlanApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3DeliveryPlanDeliveryPlanApisApi(WechatPayV3Client client)

- async WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanResponse CreateDeliveryPlanAsync(WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest request)
  - POST /v3/marketing/partner/delivery-plan/delivery-plans; C# 参数：DeliveryPlanCreateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateDeliveryPlanRawAsync(string path, string query, string jsonBody)

- async WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansResponse QueryDeliveryPlansAsync(WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest request)
  - GET /v3/marketing/partner/delivery-plan/delivery-plans/{EscapeDeliveryPlanValue(brandId)}/delivery-plans; C# 参数：string brandId, DeliveryPlanQueryRequestData 数据 = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryDeliveryPlansRawAsync(string path, string query)

- async WechatPayV3DeliveryPlanDeliveryPlanApisApiUpdateDeliveryPlanResponse UpdateDeliveryPlanAsync(WechatPayV3DeliveryPlanDeliveryPlanApisApiUpdateDeliveryPlanRequest request)
  - PATCH /v3/marketing/partner/delivery-plan/delivery-plans/{EscapeDeliveryPlanValue(planId)}; C# 参数：string planId, DeliveryPlanUpdateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateDeliveryPlanRawAsync(string path, string query, string jsonBody)

- async WechatPayV3DeliveryPlanDeliveryPlanApisApiTerminateDeliveryPlanResponse TerminateDeliveryPlanAsync(WechatPayV3DeliveryPlanDeliveryPlanApisApiTerminateDeliveryPlanRequest request)
  - POST /v3/marketing/partner/delivery-plan/delivery-plans/{EscapeDeliveryPlanValue(planId)}/terminate; C# 参数：string planId, int timeOut = Config.TIME_OUT

- async WechatRawResponse TerminateDeliveryPlanRawAsync(string path, string query, string jsonBody)

- async WechatPayV3DeliveryPlanDeliveryPlanApisApiSetDeliveryPlanNotifyUrlResponse SetDeliveryPlanNotifyUrlAsync(WechatPayV3DeliveryPlanDeliveryPlanApisApiSetDeliveryPlanNotifyUrlRequest request)
  - POST /v3/marketing/partner/delivery-plan/{EscapeDeliveryPlanValue(serviceProviderMchId)}/notify-url; C# 参数：string serviceProviderMchId, DeliveryPlanNotifyUrlRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SetDeliveryPlanNotifyUrlRawAsync(string path, string query, string jsonBody)


## WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest (class)

DeliveryPlan/DeliveryPlanApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest()

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest OutRequestNo(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest BrandId(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest ProductCouponId(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest StockId(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest ReuseCouponConfig(bool fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest PlanName(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest TotalCount(long fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest UserLimit(long fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest DailyLimit(long fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest DeliveryStartTime(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest DeliveryEndTime(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest RecommendWord(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest UsageMode(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest StockBundleId(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3DeliveryPlanDeliveryPlanApisApiCreateDeliveryPlanResponse (class)

- public string Raw;


## WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest()

- WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest PageSize(int fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest Offset(int fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest PlanState(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest AuditState(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest PlanId(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest BrandId(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3DeliveryPlanDeliveryPlanApisApiQueryDeliveryPlansResponse (class)

- public string Raw;


## WechatPayV3DeliveryPlanDeliveryPlanApisApiSetDeliveryPlanNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3DeliveryPlanDeliveryPlanApisApiSetDeliveryPlanNotifyUrlRequest()

- WechatPayV3DeliveryPlanDeliveryPlanApisApiSetDeliveryPlanNotifyUrlRequest NotifyUrl(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiSetDeliveryPlanNotifyUrlRequest ServiceProviderMchId(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiSetDeliveryPlanNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3DeliveryPlanDeliveryPlanApisApiSetDeliveryPlanNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3DeliveryPlanDeliveryPlanApisApiTerminateDeliveryPlanRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3DeliveryPlanDeliveryPlanApisApiTerminateDeliveryPlanRequest()

- WechatPayV3DeliveryPlanDeliveryPlanApisApiTerminateDeliveryPlanRequest PlanId(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiTerminateDeliveryPlanRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3DeliveryPlanDeliveryPlanApisApiTerminateDeliveryPlanResponse (class)

- public string Raw;


## WechatPayV3DeliveryPlanDeliveryPlanApisApiUpdateDeliveryPlanRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3DeliveryPlanDeliveryPlanApisApiUpdateDeliveryPlanRequest()

- WechatPayV3DeliveryPlanDeliveryPlanApisApiUpdateDeliveryPlanRequest ModifyContent(WechatPayDeliveryPlanModifyContent fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiUpdateDeliveryPlanRequest OutRequestNo(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiUpdateDeliveryPlanRequest PlanId(string fieldValue)

- WechatPayV3DeliveryPlanDeliveryPlanApisApiUpdateDeliveryPlanRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3DeliveryPlanDeliveryPlanApisApiUpdateDeliveryPlanResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApi (class)

- WechatPayV3Client client;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApi(WechatPayV3Client client)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantBalanceResponse QuerySubMerchantBalanceAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantBalanceRequest request)
  - GET /v3/ecommerce/fund/balance/{EscapeAccountFundsValue(subMchId)}; C# 参数：string subMchId, string accountType = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySubMerchantBalanceRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndBalanceResponse QuerySubMerchantDayEndBalanceAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndBalanceRequest request)
  - GET /v3/ecommerce/fund/enddaybalance/{EscapeAccountFundsValue(subMchId)}; C# 参数：string subMchId, string date, string accountType = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySubMerchantDayEndBalanceRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformBalanceResponse QueryPlatformBalanceAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformBalanceRequest request)
  - GET /v3/merchant/fund/balance/{EscapeAccountFundsValue(accountType)}; C# 参数：string accountType, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPlatformBalanceRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformDayEndBalanceResponse QueryPlatformDayEndBalanceAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformDayEndBalanceRequest request)
  - GET /v3/merchant/fund/dayendbalance/{EscapeAccountFundsValue(accountType)}; C# 参数：string accountType, string date = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPlatformDayEndBalanceRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalResponse SubmitSubMerchantWithdrawalAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest request)
  - POST /v3/ecommerce/fund/withdraw; C# 参数：EcommerceSubMerchantWithdrawalRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitSubMerchantWithdrawalRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByOutRequestNoResponse QuerySubMerchantWithdrawalByOutRequestNoAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByOutRequestNoRequest request)
  - GET /v3/ecommerce/fund/withdraw/out-请求-no/{EscapeAccountFundsValue(outRequestNo)}; C# 参数：string outRequestNo, string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySubMerchantWithdrawalByOutRequestNoRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByWithdrawIdResponse QuerySubMerchantWithdrawalByWithdrawIdAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByWithdrawIdRequest request)
  - GET /v3/ecommerce/fund/withdraw/{EscapeAccountFundsValue(withdrawId)}; C# 参数：string withdrawId, string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySubMerchantWithdrawalByWithdrawIdRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalResponse SubmitPlatformWithdrawalAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest request)
  - POST /v3/merchant/fund/withdraw; C# 参数：EcommercePlatformWithdrawalRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitPlatformWithdrawalRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByOutRequestNoResponse QueryPlatformWithdrawalByOutRequestNoAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByOutRequestNoRequest request)
  - GET /v3/merchant/fund/withdraw/out-请求-no/{EscapeAccountFundsValue(outRequestNo)}; C# 参数：string outRequestNo, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPlatformWithdrawalByOutRequestNoRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByWithdrawIdResponse QueryPlatformWithdrawalByWithdrawIdAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByWithdrawIdRequest request)
  - GET /v3/merchant/fund/withdraw/withdraw-id/{EscapeAccountFundsValue(withdrawId)}; C# 参数：string withdrawId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPlatformWithdrawalByWithdrawIdRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalResponse SubmitSubMerchantDayEndWithdrawalAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest request)
  - POST /v3/platsolution/ecommerce/withdraw/day-end-balance-withdraw; C# 参数：EcommerceSubMerchantDayEndWithdrawalRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitSubMerchantDayEndWithdrawalRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndWithdrawalResponse QuerySubMerchantDayEndWithdrawalAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndWithdrawalRequest request)
  - GET /v3/platsolution/ecommerce/withdraw/day-end-balance-withdraw/out-请求-no/{EscapeAccountFundsValue(outRequestNo)}; C# 参数：string outRequestNo, string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySubMerchantDayEndWithdrawalRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryWithdrawalAbnormalBillResponse QueryWithdrawalAbnormalBillAsync(WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryWithdrawalAbnormalBillRequest request)
  - GET /v3/merchant/fund/withdraw/bill-type/{EscapeAccountFundsValue(billType)}; C# 参数：string billType, string billDate, string tarType = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryWithdrawalAbnormalBillRawAsync(string path, string query)


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformBalanceRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformBalanceRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformBalanceRequest AccountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformBalanceRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformBalanceResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformDayEndBalanceRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformDayEndBalanceRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformDayEndBalanceRequest AccountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformDayEndBalanceRequest Date(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformDayEndBalanceRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformDayEndBalanceResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByOutRequestNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByOutRequestNoRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByOutRequestNoRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByOutRequestNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByOutRequestNoResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByWithdrawIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByWithdrawIdRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByWithdrawIdRequest WithdrawId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByWithdrawIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryPlatformWithdrawalByWithdrawIdResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantBalanceRequest (class)

Ecommerce/EcommerceApis.AccountFunds.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantBalanceRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantBalanceRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantBalanceRequest AccountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantBalanceRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantBalanceResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndBalanceRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndBalanceRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndBalanceRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndBalanceRequest Date(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndBalanceRequest AccountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndBalanceRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndBalanceResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndWithdrawalRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndWithdrawalRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndWithdrawalRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndWithdrawalRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndWithdrawalRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantDayEndWithdrawalResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByOutRequestNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByOutRequestNoRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByOutRequestNoRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByOutRequestNoRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByOutRequestNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByOutRequestNoResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByWithdrawIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByWithdrawIdRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByWithdrawIdRequest WithdrawId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByWithdrawIdRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByWithdrawIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQuerySubMerchantWithdrawalByWithdrawIdResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryWithdrawalAbnormalBillRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryWithdrawalAbnormalBillRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryWithdrawalAbnormalBillRequest BillType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryWithdrawalAbnormalBillRequest BillDate(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryWithdrawalAbnormalBillRequest TarType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryWithdrawalAbnormalBillRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiQueryWithdrawalAbnormalBillResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest Amount(int fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest Remark(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest BankMemo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest AccountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest NotifyUrl(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitPlatformWithdrawalResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest CalculateAmountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest Remark(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest BankMemo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest NotifyUrl(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest ReserveAmount(int fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantDayEndWithdrawalResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest()

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest Amount(int fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest Remark(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest BankMemo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest AccountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest NotifyUrl(string fieldValue)

- WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisAccountFundsApiSubmitSubMerchantWithdrawalResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3EcommerceEcommerceApisApi(WechatPayV3Client client)

- async WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentResponse SubMerchantApplymentAsync(WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest request)
  - POST /v3/ecommerce/applyments/; C# 参数：SubMerchantApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubMerchantApplymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentResponse QuerySubMerchantApplymentAsync(WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentRequest request)
  - GET /v3/ecommerce/applyments/{数据.applyment_id}; C# 参数：QuerySubMerchantApplymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySubMerchantApplymentRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentByOutRequestNoResponse QuerySubMerchantApplymentByOutRequestNoAsync(WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentByOutRequestNoRequest request)
  - GET /v3/ecommerce/applyments/out-请求-no/{数据.out_request_no}; C# 参数：QuerySubMerchantApplymentByOutRequestNoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySubMerchantApplymentByOutRequestNoRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisApiCombineTransactionsResponse CombineTransactionsAsync(WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest request)
  - POST /v3/ecommerce/combine/transactions/jsapi; C# 参数：CombineTransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CombineTransactionsRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisApiQueryCombineTransactionsResponse QueryCombineTransactionsAsync(WechatPayV3EcommerceEcommerceApisApiQueryCombineTransactionsRequest request)
  - GET /v3/ecommerce/combine/transactions/out-trade-no/{数据.combine_out_trade_no}; C# 参数：QueryCombineTransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCombineTransactionsRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisApiCloseCombineTransactionsResponse CloseCombineTransactionsAsync(WechatPayV3EcommerceEcommerceApisApiCloseCombineTransactionsRequest request)
  - POST /v3/ecommerce/combine/transactions/out-trade-no/{数据.combine_out_trade_no}/close; C# 参数：CloseCombineTransactionsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CloseCombineTransactionsRawAsync(string path, string query, string jsonBody)


## WechatPayV3EcommerceEcommerceApisApiCloseCombineTransactionsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisApiCloseCombineTransactionsRequest()

- WechatPayV3EcommerceEcommerceApisApiCloseCombineTransactionsRequest CombineOutTradeNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCloseCombineTransactionsRequest SubOrders(List<WechatPayCombineSubOrderInfo> fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCloseCombineTransactionsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisApiCloseCombineTransactionsResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest()

- WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest CombineAppid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest CombineMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest CombineOutTradeNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest SceneInfo(WechatPayEcommerceEcommerceCombineRequestDataCombineSceneInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest SubOrders(List<WechatPayCombineSubOrder> fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest CombinePayerInfo(WechatPayCombinePayer fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest TimeExpire(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest NotifyUrl(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiCombineTransactionsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisApiCombineTransactionsResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisApiQueryCombineTransactionsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisApiQueryCombineTransactionsRequest()

- WechatPayV3EcommerceEcommerceApisApiQueryCombineTransactionsRequest CombineOutTradeNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiQueryCombineTransactionsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisApiQueryCombineTransactionsResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentByOutRequestNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentByOutRequestNoRequest()

- WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentByOutRequestNoRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentByOutRequestNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentByOutRequestNoResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentRequest()

- WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentRequest ApplymentId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisApiQuerySubMerchantApplymentResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest (class)

Ecommerce/EcommerceApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest()

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest OrganizationType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest BusinessLicenseInfo(WechatPayBusinessLicenseInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest IdDocInfo(WechatPayIdDocInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest UboInfo(WechatPayUboInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest AccountInfo(WechatPayAccountInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest ContactInfo(WechatPayContactInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest SalesSceneInfo(WechatPaySalesSceneInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest MerchantShortname(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest Qualifications(List<string> fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest BusinessAdditionPics(List<string> fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest BusinessAdditionMsg(string fieldValue)

- WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisApiSubMerchantApplymentResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisBillsApi (class)

- WechatPayV3Client client;

- public WechatPayV3EcommerceEcommerceApisBillsApi(WechatPayV3Client client)

- async WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillResponse ApplyAllSubMerchantFundflowBillAsync(WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillRequest request)
  - GET /v3/ecommerce/bill/fundflowbill; C# 参数：EcommerceAllSubMerchantFundflowBillRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyAllSubMerchantFundflowBillRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillResponse ApplySingleSubMerchantFundflowBillAsync(WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillRequest request)
  - GET /v3/bill/sub-merchant-fundflowbill; C# 参数：EcommerceSingleSubMerchantFundflowBillRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplySingleSubMerchantFundflowBillRawAsync(string path, string query)

- async WechatRawResponse DownloadEcommerceBillAsync(WechatPayV3EcommerceEcommerceApisBillsApiDownloadEcommerceBillRequest request)
  - POST (请求.RequestPath 指定); C# 参数：string downloadUrl, Stream destination, int timeOut = Config.TIME_OUT

- async WechatRawResponse DownloadEcommerceBillRawAsync(string path, string query, string jsonBody)


## WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillRequest (class)

Ecommerce/EcommerceApis.Bills.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillRequest()

- WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillRequest BillDate(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillRequest AccountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillRequest TarType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillRequest Algorithm(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisBillsApiApplyAllSubMerchantFundflowBillResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillRequest()

- WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillRequest BillDate(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillRequest AccountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillRequest Algorithm(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillRequest TarType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisBillsApiApplySingleSubMerchantFundflowBillResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisBillsApiDownloadEcommerceBillRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisBillsApiDownloadEcommerceBillRequest()

- WechatPayV3EcommerceEcommerceApisBillsApiDownloadEcommerceBillRequest DownloadUrl(string fieldValue)

- WechatPayV3EcommerceEcommerceApisBillsApiDownloadEcommerceBillRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisBillsApiDownloadEcommerceBillResponse (class)

- public string Raw;

- public JsonValue Value;


## WechatPayV3EcommerceEcommerceApisCrossBorderApi (class)

- WechatPayV3Client client;

- public WechatPayV3EcommerceEcommerceApisCrossBorderApi(WechatPayV3Client client)

- async WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaAvailableAmountResponse QueryFundsToOverseaAvailableAmountAsync(WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaAvailableAmountRequest request)
  - GET /v3/funds-to-oversea/transactions/{EscapeFundsToOverseaValue(transactionId)}/available_abroad_amounts; C# 参数：string transactionId, string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryFundsToOverseaAvailableAmountRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaResponse ApplyFundsToOverseaAsync(WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest request)
  - POST /v3/funds-to-oversea/orders; C# 参数：EcommerceFundsToOverseaRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyFundsToOverseaRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaOrderResponse QueryFundsToOverseaOrderAsync(WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaOrderRequest request)
  - GET /v3/funds-to-oversea/orders/{EscapeFundsToOverseaValue(outOrderId)}; C# 参数：string outOrderId, string subMchId, string transactionId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryFundsToOverseaOrderRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaBillDownloadUrlResponse QueryFundsToOverseaBillDownloadUrlAsync(WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaBillDownloadUrlRequest request)
  - GET /v3/funds-to-oversea/bill-download-url; C# 参数：EcommerceFundsToOverseaBillRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryFundsToOverseaBillDownloadUrlRawAsync(string path, string query)


## WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest()

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest OutOrderId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest TransactionId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest Amount(long fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest ForeignCurrency(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest GoodsInfo(List<WechatPayEcommerceFundsToOverseaGoodsInfo> fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest SellerInfo(WechatPayEcommerceFundsToOverseaSellerInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest ExpressInfo(WechatPayEcommerceFundsToOverseaExpressInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest PayeeInfo(WechatPayEcommerceFundsToOverseaPayeeInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest PresaleInfo(WechatPayEcommerceFundsToOverseaPresaleInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisCrossBorderApiApplyFundsToOverseaResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaAvailableAmountRequest (class)

Ecommerce/EcommerceApis.CrossBorder.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaAvailableAmountRequest()

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaAvailableAmountRequest TransactionId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaAvailableAmountRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaAvailableAmountRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaAvailableAmountResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaBillDownloadUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaBillDownloadUrlRequest()

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaBillDownloadUrlRequest BillDate(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaBillDownloadUrlRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaBillDownloadUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaBillDownloadUrlResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaOrderRequest()

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaOrderRequest OutOrderId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaOrderRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaOrderRequest TransactionId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisCrossBorderApiQueryFundsToOverseaOrderResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApi (class)

- WechatPayV3Client client;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApi(WechatPayV3Client client)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiValidateMerchantCancellationResponse ValidateMerchantCancellationAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiValidateMerchantCancellationRequest request)
  - GET /v3/ecommerce/account/apply-cancel-withdraw/validate-cancel/{EscapeMerchantCancellationPath(subMchId)}; C# 参数：string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse ValidateMerchantCancellationRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawResponse ApplyCancelWithdrawAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest request)
  - POST /v3/ecommerce/account/apply-cancel-withdraw; C# 参数：EcommerceApplyCancelWithdrawRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyCancelWithdrawRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByOutRequestNoResponse QueryCancelWithdrawByOutRequestNoAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByOutRequestNoRequest request)
  - GET /v3/ecommerce/account/apply-cancel-withdraw/out-请求-no/{EscapeMerchantCancellationPath(outRequestNo)}; C# 参数：string outRequestNo, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCancelWithdrawByOutRequestNoRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByApplymentIdResponse QueryCancelWithdrawByApplymentIdAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByApplymentIdRequest request)
  - GET /v3/ecommerce/account/apply-cancel-withdraw/applyment-id/{EscapeMerchantCancellationPath(applymentId)}; C# 参数：string applymentId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCancelWithdrawByApplymentIdRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelApplicationResponse SubmitLegacyCancelApplicationAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelApplicationRequest request)
  - POST /v3/ecommerce/account/cancel-applications; C# 参数：EcommerceLegacyCancelApplicationRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitLegacyCancelApplicationRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelApplicationResponse QueryLegacyCancelApplicationAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelApplicationRequest request)
  - GET /v3/ecommerce/account/cancel-applications/out-apply-no/{EscapeMerchantCancellationPath(outApplyNo)}; C# 参数：string outApplyNo, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryLegacyCancelApplicationRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiUploadCancelApplicationImageResponse UploadCancelApplicationImageAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiUploadCancelApplicationImageRequest request, WechatMultipart multipart)
  - POST /v3/ecommerce/account/cancel-applications/media; C# 参数：string fileName, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadCancelApplicationImageRawAsync(string path, string query, WechatMultipart multipart)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawResponse SubmitLegacyCancelWithdrawAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest request)
  - POST /v3/mch_operate/risk/withdrawl-apply; C# 参数：EcommerceLegacyCancelWithdrawRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitLegacyCancelWithdrawRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByOutRequestNoResponse QueryLegacyCancelWithdrawByOutRequestNoAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByOutRequestNoRequest request)
  - GET /v3/mch_operate/risk/withdrawl-apply/out-请求-no/{EscapeMerchantCancellationPath(outRequestNo)}; C# 参数：string outRequestNo, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryLegacyCancelWithdrawByOutRequestNoRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByApplymentIdResponse QueryLegacyCancelWithdrawByApplymentIdAsync(WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByApplymentIdRequest request)
  - GET /v3/mch_operate/risk/withdrawl-apply/applyment-id/{EscapeMerchantCancellationPath(applymentId)}; C# 参数：string applymentId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryLegacyCancelWithdrawByApplymentIdRawAsync(string path, string query)


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest Withdraw(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest PayeeInfo(WechatPayEcommerceCancelWithdrawPayeeInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest ProofMedias(List<WechatPayEcommerceCancelWithdrawProofMedia> fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest AdditionalMaterials(List<string> fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest Remark(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiApplyCancelWithdrawResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByApplymentIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByApplymentIdRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByApplymentIdRequest ApplymentId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByApplymentIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByApplymentIdResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByOutRequestNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByOutRequestNoRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByOutRequestNoRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByOutRequestNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryCancelWithdrawByOutRequestNoResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelApplicationRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelApplicationRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelApplicationRequest OutApplyNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelApplicationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelApplicationResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByApplymentIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByApplymentIdRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByApplymentIdRequest ApplymentId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByApplymentIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByApplymentIdResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByOutRequestNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByOutRequestNoRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByOutRequestNoRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByOutRequestNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiQueryLegacyCancelWithdrawByOutRequestNoResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelApplicationRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelApplicationRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelApplicationRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelApplicationRequest OutApplyNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelApplicationRequest ApplicationInfo(List<WechatPayEcommerceLegacyCancelApplicationMaterial> fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelApplicationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelApplicationResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest OutAccountType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest Amount(int fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest OutRequestNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest PayeeType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest PayeeMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest PayeeInfo(WechatPayEcommerceLegacyCancelWithdrawPayeeInfo fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest ProofMediaList(WechatPayEcommerceLegacyCancelWithdrawProofMediaList fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest AdditionalMaterials(WechatPayEcommerceLegacyCancelWithdrawAdditionalMaterials fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest Remark(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiSubmitLegacyCancelWithdrawResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiUploadCancelApplicationImageRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiUploadCancelApplicationImageRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiUploadCancelApplicationImageRequest FileName(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiUploadCancelApplicationImageRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiUploadCancelApplicationImageResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiValidateMerchantCancellationRequest (class)

Ecommerce/EcommerceApis.MerchantCancellation.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisMerchantCancellationApiValidateMerchantCancellationRequest()

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiValidateMerchantCancellationRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisMerchantCancellationApiValidateMerchantCancellationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisMerchantCancellationApiValidateMerchantCancellationResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisRefundsApi (class)

- WechatPayV3Client client;

- public WechatPayV3EcommerceEcommerceApisRefundsApi(WechatPayV3Client client)

- async WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundResponse ApplyEcommerceRefundAsync(WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest request)
  - POST /v3/ecommerce/refunds/apply; C# 参数：EcommerceRefundRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyEcommerceRefundRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByRefundIdResponse QueryEcommerceRefundByRefundIdAsync(WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByRefundIdRequest request)
  - GET /v3/ecommerce/refunds/id/{EscapeEcommerceRefundValue(refundId)}; C# 参数：string refundId, string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryEcommerceRefundByRefundIdRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByOutRefundNoResponse QueryEcommerceRefundByOutRefundNoAsync(WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByOutRefundNoRequest request)
  - GET /v3/ecommerce/refunds/out-refund-no/{EscapeEcommerceRefundValue(outRefundNo)}; C# 参数：string outRefundNo, string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryEcommerceRefundByOutRefundNoRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundAdvanceReturnResponse QueryEcommerceRefundAdvanceReturnAsync(WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundAdvanceReturnRequest request)
  - GET /v3/ecommerce/refunds/{EscapeEcommerceRefundValue(refundId)}/return-advance; C# 参数：string refundId, string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryEcommerceRefundAdvanceReturnRawAsync(string path, string query)

- async WechatPayV3EcommerceEcommerceApisRefundsApiReturnEcommerceRefundAdvanceResponse ReturnEcommerceRefundAdvanceAsync(WechatPayV3EcommerceEcommerceApisRefundsApiReturnEcommerceRefundAdvanceRequest request)
  - POST /v3/ecommerce/refunds/{EscapeEcommerceRefundValue(refundId)}/return-advance; C# 参数：string refundId, EcommerceRefundAdvanceReturnRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ReturnEcommerceRefundAdvanceRawAsync(string path, string query, string jsonBody)

- async WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundResponse ApplyEcommerceAbnormalRefundAsync(WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest request)
  - POST /v3/ecommerce/refunds/{EscapeEcommerceRefundValue(refundId)}/apply-abnormal-refund; C# 参数：string refundId, AbnormalRefundRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyEcommerceAbnormalRefundRawAsync(string path, string query, string jsonBody)


## WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest()

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest OutRefundNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest Type(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest BankType(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest BankAccount(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest RealName(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest RefundId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceAbnormalRefundResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest (class)

Ecommerce/EcommerceApis.Refunds.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest()

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest SpAppid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest SubAppid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest TransactionId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest OutTradeNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest OutRefundNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest Reason(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest Amount(WechatPayEcommerceRefundRequestAmount fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest NotifyUrl(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest RefundAccount(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest FundsAccount(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisRefundsApiApplyEcommerceRefundResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundAdvanceReturnRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundAdvanceReturnRequest()

- WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundAdvanceReturnRequest RefundId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundAdvanceReturnRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundAdvanceReturnRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundAdvanceReturnResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByOutRefundNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByOutRefundNoRequest()

- WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByOutRefundNoRequest OutRefundNo(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByOutRefundNoRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByOutRefundNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByOutRefundNoResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByRefundIdRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByRefundIdRequest()

- WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByRefundIdRequest RefundId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByRefundIdRequest SubMchId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByRefundIdRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisRefundsApiQueryEcommerceRefundByRefundIdResponse (class)

- public string Raw;


## WechatPayV3EcommerceEcommerceApisRefundsApiReturnEcommerceRefundAdvanceRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3EcommerceEcommerceApisRefundsApiReturnEcommerceRefundAdvanceRequest()

- WechatPayV3EcommerceEcommerceApisRefundsApiReturnEcommerceRefundAdvanceRequest SubMchid(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiReturnEcommerceRefundAdvanceRequest RefundId(string fieldValue)

- WechatPayV3EcommerceEcommerceApisRefundsApiReturnEcommerceRefundAdvanceRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3EcommerceEcommerceApisRefundsApiReturnEcommerceRefundAdvanceResponse (class)

- public string Raw;


## WechatPayV3FaPiaoFaPiaoApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3FaPiaoFaPiaoApisApi(WechatPayV3Client client)

- async WechatPayV3FaPiaoFaPiaoApisApiCheckSubMerchantFapiaoStatusResponse CheckSubMerchantFapiaoStatusAsync(WechatPayV3FaPiaoFaPiaoApisApiCheckSubMerchantFapiaoStatusRequest request)
  - GET /v3/new-tax-控件-fapiao/merchant/{数据.sub_mchid}/检查; C# 参数：CheckFapiaoStatusRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CheckSubMerchantFapiaoStatusRawAsync(string path, string query)

- async WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateResponse CreateFapiaoCardTemplateAsync(WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateRequest request)
  - POST /v3/new-tax-控件-fapiao/card-template; C# 参数：CreateFapiaoCardTemplateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateFapiaoCardTemplateRawAsync(string path, string query, string jsonBody)

- async WechatPayV3FaPiaoFaPiaoApisApiQueryFapiaoResponse QueryFapiaoAsync(WechatPayV3FaPiaoFaPiaoApisApiQueryFapiaoRequest request)
  - GET /v3/new-tax-控件-fapiao/fapiao-applications/{数据.fapiao_apply_id}; C# 参数：QueryFapiaoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryFapiaoRawAsync(string path, string query)

- async WechatPayV3FaPiaoFaPiaoApisApiGetTitleUrlResponse GetTitleUrlAsync(WechatPayV3FaPiaoFaPiaoApisApiGetTitleUrlRequest request)
  - POST /v3/new-tax-控件-fapiao/user-title/title-url; C# 参数：GetTitleUrlRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GetTitleUrlRawAsync(string path, string query, string jsonBody)

- async WechatPayV3FaPiaoFaPiaoApisApiGetUserTitleResponse GetUserTitleAsync(WechatPayV3FaPiaoFaPiaoApisApiGetUserTitleRequest request)
  - POST /v3/new-tax-控件-fapiao/user-title; C# 参数：GetUserTitleRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GetUserTitleRawAsync(string path, string query, string jsonBody)

- async WechatPayV3FaPiaoFaPiaoApisApiGetMerchantInfoResponse GetMerchantInfoAsync(WechatPayV3FaPiaoFaPiaoApisApiGetMerchantInfoRequest request)
  - GET /v3/new-tax-控件-fapiao/merchant/{数据.sub_mchid}; C# 参数：GetMerchantInfoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GetMerchantInfoRawAsync(string path, string query)

- async WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoResponse CreateFapiaoAsync(WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest request)
  - POST /v3/new-tax-控件-fapiao/fapiao-applications; C# 参数：CreateFapiaoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateFapiaoRawAsync(string path, string query, string jsonBody)

- async WechatPayV3FaPiaoFaPiaoApisApiReverseFapiaoResponse ReverseFapiaoAsync(WechatPayV3FaPiaoFaPiaoApisApiReverseFapiaoRequest request)
  - POST /v3/new-tax-控件-fapiao/fapiao-applications/{数据.fapiao_apply_id}/reverse; C# 参数：ReverseFapiaoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ReverseFapiaoRawAsync(string path, string query, string jsonBody)

- async WechatPayV3FaPiaoFaPiaoApisApiGetFapiaoFileResponse GetFapiaoFileAsync(WechatPayV3FaPiaoFaPiaoApisApiGetFapiaoFileRequest request)
  - GET /v3/new-tax-控件-fapiao/fapiao-applications/{数据.fapiao_apply_id}/fapiao-文件; C# 参数：GetFapiaoFileRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GetFapiaoFileRawAsync(string path, string query)


## WechatPayV3FaPiaoFaPiaoApisApiCheckSubMerchantFapiaoStatusRequest (class)

FaPiao/FaPiaoApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FaPiaoFaPiaoApisApiCheckSubMerchantFapiaoStatusRequest()

- WechatPayV3FaPiaoFaPiaoApisApiCheckSubMerchantFapiaoStatusRequest SubMchid(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCheckSubMerchantFapiaoStatusRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FaPiaoFaPiaoApisApiCheckSubMerchantFapiaoStatusResponse (class)

- public string Raw;


## WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateRequest()

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateRequest Mchid(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateRequest TemplateName(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateRequest TemplateDescription(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateRequest InvoiceType(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoCardTemplateResponse (class)

- public string Raw;


## WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest()

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest Mchid(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest SubMchid(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest TransactionId(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest OutTradeNo(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest FapiaoApplyTime(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest FapiaoInfo(WechatPayFapiaoInfo fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest BuyerInfo(WechatPayBuyerInfo fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FaPiaoFaPiaoApisApiCreateFapiaoResponse (class)

- public string Raw;


## WechatPayV3FaPiaoFaPiaoApisApiGetFapiaoFileRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FaPiaoFaPiaoApisApiGetFapiaoFileRequest()

- WechatPayV3FaPiaoFaPiaoApisApiGetFapiaoFileRequest FapiaoApplyId(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiGetFapiaoFileRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FaPiaoFaPiaoApisApiGetFapiaoFileResponse (class)

- public string Raw;


## WechatPayV3FaPiaoFaPiaoApisApiGetMerchantInfoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FaPiaoFaPiaoApisApiGetMerchantInfoRequest()

- WechatPayV3FaPiaoFaPiaoApisApiGetMerchantInfoRequest SubMchid(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiGetMerchantInfoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FaPiaoFaPiaoApisApiGetMerchantInfoResponse (class)

- public string Raw;


## WechatPayV3FaPiaoFaPiaoApisApiGetTitleUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FaPiaoFaPiaoApisApiGetTitleUrlRequest()

- WechatPayV3FaPiaoFaPiaoApisApiGetTitleUrlRequest Mchid(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiGetTitleUrlRequest Openid(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiGetTitleUrlRequest RedirectUrl(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiGetTitleUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FaPiaoFaPiaoApisApiGetTitleUrlResponse (class)

- public string Raw;


## WechatPayV3FaPiaoFaPiaoApisApiGetUserTitleRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FaPiaoFaPiaoApisApiGetUserTitleRequest()

- WechatPayV3FaPiaoFaPiaoApisApiGetUserTitleRequest Openid(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiGetUserTitleRequest Scene(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiGetUserTitleRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FaPiaoFaPiaoApisApiGetUserTitleResponse (class)

- public string Raw;


## WechatPayV3FaPiaoFaPiaoApisApiQueryFapiaoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FaPiaoFaPiaoApisApiQueryFapiaoRequest()

- WechatPayV3FaPiaoFaPiaoApisApiQueryFapiaoRequest FapiaoApplyId(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiQueryFapiaoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FaPiaoFaPiaoApisApiQueryFapiaoResponse (class)

- public string Raw;


## WechatPayV3FaPiaoFaPiaoApisApiReverseFapiaoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FaPiaoFaPiaoApisApiReverseFapiaoRequest()

- WechatPayV3FaPiaoFaPiaoApisApiReverseFapiaoRequest FapiaoApplyId(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiReverseFapiaoRequest ReverseReason(string fieldValue)

- WechatPayV3FaPiaoFaPiaoApisApiReverseFapiaoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FaPiaoFaPiaoApisApiReverseFapiaoResponse (class)

- public string Raw;


## WechatPayV3FundAppFundAppApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3FundAppFundAppApisApi(WechatPayV3Client client)

- async WechatPayV3FundAppFundAppApisApiTransferBillResponse TransferBillAsync(WechatPayV3FundAppFundAppApisApiTransferBillRequest request)
  - POST /v3/fund-app/mch-transfer/transfer-bills; C# 参数：TransferBillRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse TransferBillRawAsync(string path, string query, string jsonBody)

- async WechatPayV3FundAppFundAppApisApiCancelTransferResponse CancelTransferAsync(WechatPayV3FundAppFundAppApisApiCancelTransferRequest request)
  - POST /v3/fund-app/mch-transfer/transfer-bills/out-bill-no/{数据.out_bill_no}/cancel; C# 参数：CancelTransferRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CancelTransferRawAsync(string path, string query, string jsonBody)

- async WechatPayV3FundAppFundAppApisApiQueryTransferByOutBillNoResponse QueryTransferByOutBillNoAsync(WechatPayV3FundAppFundAppApisApiQueryTransferByOutBillNoRequest request)
  - GET /v3/fund-app/mch-transfer/transfer-bills/out-bill-no/{数据.out_bill_no}; C# 参数：QueryTransferByOutBillNoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryTransferByOutBillNoRawAsync(string path, string query)

- async WechatPayV3FundAppFundAppApisApiQueryTransferByBillNoResponse QueryTransferByBillNoAsync(WechatPayV3FundAppFundAppApisApiQueryTransferByBillNoRequest request)
  - GET /v3/fund-app/mch-transfer/transfer-bills/transfer-bill-no/{数据.transfer_bill_no}; C# 参数：QueryTransferByBillNoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryTransferByBillNoRawAsync(string path, string query)

- async WechatPayV3FundAppFundAppApisApiApplyElecsignByOutBillNoResponse ApplyElecsignByOutBillNoAsync(WechatPayV3FundAppFundAppApisApiApplyElecsignByOutBillNoRequest request)
  - POST /v3/fund-app/mch-transfer/elecsign/out-bill-no; C# 参数：ApplyElecsignByOutBillNoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyElecsignByOutBillNoRawAsync(string path, string query, string jsonBody)

- async WechatPayV3FundAppFundAppApisApiQueryElecsignByOutBillNoResponse QueryElecsignByOutBillNoAsync(WechatPayV3FundAppFundAppApisApiQueryElecsignByOutBillNoRequest request)
  - GET /v3/fund-app/mch-transfer/elecsign/out-bill-no/{数据.out_bill_no}; C# 参数：QueryElecsignByOutBillNoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryElecsignByOutBillNoRawAsync(string path, string query)

- async WechatPayV3FundAppFundAppApisApiApplyElecsignByBillNoResponse ApplyElecsignByBillNoAsync(WechatPayV3FundAppFundAppApisApiApplyElecsignByBillNoRequest request)
  - POST /v3/fund-app/mch-transfer/elecsign/transfer-bill-no; C# 参数：ApplyElecsignByBillNoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ApplyElecsignByBillNoRawAsync(string path, string query, string jsonBody)

- async WechatPayV3FundAppFundAppApisApiQueryElecsignByBillNoResponse QueryElecsignByBillNoAsync(WechatPayV3FundAppFundAppApisApiQueryElecsignByBillNoRequest request)
  - GET /v3/fund-app/mch-transfer/elecsign/transfer-bill-no/{数据.transfer_bill_no}; C# 参数：QueryElecsignByBillNoRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryElecsignByBillNoRawAsync(string path, string query)


## WechatPayV3FundAppFundAppApisApiApplyElecsignByBillNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FundAppFundAppApisApiApplyElecsignByBillNoRequest()

- WechatPayV3FundAppFundAppApisApiApplyElecsignByBillNoRequest TransferBillNo(string fieldValue)

- WechatPayV3FundAppFundAppApisApiApplyElecsignByBillNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FundAppFundAppApisApiApplyElecsignByBillNoResponse (class)

- public string Raw;


## WechatPayV3FundAppFundAppApisApiApplyElecsignByOutBillNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FundAppFundAppApisApiApplyElecsignByOutBillNoRequest()

- WechatPayV3FundAppFundAppApisApiApplyElecsignByOutBillNoRequest OutBillNo(string fieldValue)

- WechatPayV3FundAppFundAppApisApiApplyElecsignByOutBillNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FundAppFundAppApisApiApplyElecsignByOutBillNoResponse (class)

- public string Raw;


## WechatPayV3FundAppFundAppApisApiCancelTransferRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FundAppFundAppApisApiCancelTransferRequest()

- WechatPayV3FundAppFundAppApisApiCancelTransferRequest OutBillNo(string fieldValue)

- WechatPayV3FundAppFundAppApisApiCancelTransferRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FundAppFundAppApisApiCancelTransferResponse (class)

- public string Raw;


## WechatPayV3FundAppFundAppApisApiQueryElecsignByBillNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FundAppFundAppApisApiQueryElecsignByBillNoRequest()

- WechatPayV3FundAppFundAppApisApiQueryElecsignByBillNoRequest TransferBillNo(string fieldValue)

- WechatPayV3FundAppFundAppApisApiQueryElecsignByBillNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FundAppFundAppApisApiQueryElecsignByBillNoResponse (class)

- public string Raw;


## WechatPayV3FundAppFundAppApisApiQueryElecsignByOutBillNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FundAppFundAppApisApiQueryElecsignByOutBillNoRequest()

- WechatPayV3FundAppFundAppApisApiQueryElecsignByOutBillNoRequest OutBillNo(string fieldValue)

- WechatPayV3FundAppFundAppApisApiQueryElecsignByOutBillNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FundAppFundAppApisApiQueryElecsignByOutBillNoResponse (class)

- public string Raw;


## WechatPayV3FundAppFundAppApisApiQueryTransferByBillNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FundAppFundAppApisApiQueryTransferByBillNoRequest()

- WechatPayV3FundAppFundAppApisApiQueryTransferByBillNoRequest TransferBillNo(string fieldValue)

- WechatPayV3FundAppFundAppApisApiQueryTransferByBillNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FundAppFundAppApisApiQueryTransferByBillNoResponse (class)

- public string Raw;


## WechatPayV3FundAppFundAppApisApiQueryTransferByOutBillNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FundAppFundAppApisApiQueryTransferByOutBillNoRequest()

- WechatPayV3FundAppFundAppApisApiQueryTransferByOutBillNoRequest OutBillNo(string fieldValue)

- WechatPayV3FundAppFundAppApisApiQueryTransferByOutBillNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FundAppFundAppApisApiQueryTransferByOutBillNoResponse (class)

- public string Raw;


## WechatPayV3FundAppFundAppApisApiTransferBillRequest (class)

FundApp/FundAppApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3FundAppFundAppApisApiTransferBillRequest()

- WechatPayV3FundAppFundAppApisApiTransferBillRequest Appid(string fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest OutBillNo(string fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest TransferSceneId(string fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest Openid(string fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest UserName(string fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest TransferAmount(int fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest TransferRemark(string fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest NotifyUrl(string fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest UserRecvPerception(string fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest TransferSceneReportInfos(List<WechatPayTransferSceneReportInfo> fieldValue)

- WechatPayV3FundAppFundAppApisApiTransferBillRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3FundAppFundAppApisApiTransferBillResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApi (class)

- WechatPayV3Client client;

- public WechatPayV3MarketingMarketingApisBusifavorApi(WechatPayV3Client client)

- async WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataResponse CreateBusifavorStockRequestDataAsync(WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest request)
  - POST /v3/marketing/busifavor/stocks; C# 参数：CreateBusifavorStockRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateBusifavorStockRequestDataRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorStockResponse QueryBusifavorStockAsync(WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorStockRequest request)
  - GET /v3/marketing/busifavor/stocks/{stock_id}; C# 参数：string stock_id, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBusifavorStockRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponResponse UseBusifavorCouponAsync(WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest request)
  - POST /v3/marketing/busifavor/coupons/use; C# 参数：UseBusifavorCouponRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UseBusifavorCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsResponse QueryBusifavorCouponsAsync(WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest request)
  - GET /v3/marketing/busifavor/users/{openid}/coupons?appid={appid}&偏移={偏移}&limit={limit}; C# 参数：string openid, string appid, string stock_id, string coupon_state, string creator_merchant, string belong_merchant, string sender_merchant, int 偏移 = 0, int limit = 20, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBusifavorCouponsRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponResponse QueryBusifavorCouponAsync(WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponRequest request)
  - GET /v3/marketing/busifavor/users/{openid}/coupons/{coupon_code}/appids/{appid}; C# 参数：string coupon_code, string appid, string openid, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBusifavorCouponRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorCouponCodesResponse SetBusifavorCouponCodesAsync(WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorCouponCodesRequest request)
  - POST /v3/marketing/busifavor/stocks/{数据.stock_id}/couponcodes; C# 参数：SetBusifavorCouponCodesRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SetBusifavorCouponCodesRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorSetNotifyUrlResponse SetBusifavorSetNotifyUrlAsync(WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorSetNotifyUrlRequest request)
  - POST /v3/marketing/busifavor/callbacks; C# 参数：SetBusifavorSetNotifyUrlRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SetBusifavorSetNotifyUrlRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorNotifyUrlResponse QueryBusifavorNotifyUrlAsync(WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorNotifyUrlRequest request)
  - GET /v3/marketing/busifavor/callbacks?mchid={mchid}; C# 参数：string mchid, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBusifavorNotifyUrlRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorResponse AssociateBusifavorAsync(WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorRequest request)
  - POST /v3/marketing/busifavor/coupons/associate; C# 参数：AssociateBusifavorRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AssociateBusifavorRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorResponse DisassociateBusifavorAsync(WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorRequest request)
  - POST /v3/marketing/busifavor/coupons/disassociate; C# 参数：DisassociateBusifavorRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DisassociateBusifavorRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetResponse ModifyBusifavorStockBudgetAsync(WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest request)
  - PATCH /v3/marketing/busifavor/stocks/{stock_id}/budget; C# 参数：string stock_id, ModifyBusifavorStockBudgetRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifyBusifavorStockBudgetRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationResponse ModifyBusifavorStockInformationAsync(WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest request)
  - PATCH /v3/marketing/busifavor/stocks/{stock_id}; C# 参数：string stock_id, ModifyBusifavorStockInformationRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifyBusifavorStockInformationRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiReturnBusifavorCouponResponse ReturnBusifavorCouponAsync(WechatPayV3MarketingMarketingApisBusifavorApiReturnBusifavorCouponRequest request)
  - POST /v3/marketing/busifavor/coupons/return; C# 参数：ReturnBusifavorCouponRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ReturnBusifavorCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponResponse DeactivateBusifavorCouponAsync(WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponRequest request)
  - POST /v3/marketing/busifavor/coupons/deactivate; C# 参数：DeactivateBusifavorCouponRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DeactivateBusifavorCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsResponse PayBusifavorReceiptsAsync(WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest request)
  - POST /v3/marketing/busifavor/subsidy/pay-receipts; C# 参数：PayBusifavorReceiptsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse PayBusifavorReceiptsRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorPayReceiptsResponse QueryBusifavorPayReceiptsAsync(WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorPayReceiptsRequest request)
  - GET /v3/marketing/busifavor/subsidy/pay-receipts/{subsidy_receipt_id}; C# 参数：string subsidy_receipt_id, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryBusifavorPayReceiptsRawAsync(string path, string query)


## WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorRequest CouponCode(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorRequest OutTradeNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorRequest OutRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiAssociateBusifavorResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest (class)

Marketing/MarketingApis.Busifavor.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest StockName(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest BelongMerchant(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest Comment(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest GoodsName(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest StockType(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest CouponUseRule(WechatPayMarketingCreateBusifavorStockRequestDataCouponUseRule fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest StockSendRule(WechatPayMarketingCreateBusifavorStockRequestDataStockSendRule fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest OutRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest CustomEntrance(WechatPayMarketingCreateBusifavorStockRequestDataCustomEntrance fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest DisplayPatternInfo(WechatPayMarketingCreateBusifavorStockRequestDataDisplayPatternInfo fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest CouponCodeMode(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest NotifyConfig(WechatPayMarketingCreateBusifavorStockRequestDataNotifyConfig fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest Subsidy(bool fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiCreateBusifavorStockRequestDataResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponRequest CouponCode(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponRequest DeactivateRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponRequest DeactivateReason(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiDeactivateBusifavorCouponResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorRequest CouponCode(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorRequest OutTradeNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorRequest OutRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiDisassociateBusifavorResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest TargetMaxCoupons(int fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest TargetMaxCouponsByDay(int fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest CurrentMaxCoupons(int fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest CurrentMaxCouponsByDay(int fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest ModifyBudgetRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockBudgetResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest CustomEntrance(WechatPayMarketingModifyBusifavorStockInformationRequestDataCustomEntrance fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest StockName(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest Comment(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest GoodsName(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest OutRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest DisplayPatternInfo(WechatPayMarketingModifyBusifavorStockInformationRequestDataDisplayPatternInfo fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest CouponUseRule(WechatPayMarketingModifyBusifavorStockInformationRequestDataCouponUseRule fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest StockSendRule(WechatPayMarketingModifyBusifavorStockInformationRequestDataStockSendRule fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest NotifyConfig(WechatPayMarketingModifyBusifavorStockInformationRequestDataNotifyConfig fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiModifyBusifavorStockInformationResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest CouponCode(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest TransactionId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest PayerMerchant(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest PayeeMerchant(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest Amount(int fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest Description(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest OutSubsidyNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiPayBusifavorReceiptsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponRequest CouponCode(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponRequest Appid(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponRequest Openid(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest Openid(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest Appid(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest CouponState(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest CreatorMerchant(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest BelongMerchant(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest SenderMerchant(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest Offset(int fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest Limit(int fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorCouponsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorNotifyUrlRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorNotifyUrlRequest Mchid(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorPayReceiptsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorPayReceiptsRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorPayReceiptsRequest SubsidyReceiptId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorPayReceiptsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorPayReceiptsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorStockRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorStockRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiQueryBusifavorStockResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiReturnBusifavorCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiReturnBusifavorCouponRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiReturnBusifavorCouponRequest CouponCode(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiReturnBusifavorCouponRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiReturnBusifavorCouponRequest ReturnRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiReturnBusifavorCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiReturnBusifavorCouponResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorCouponCodesRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorCouponCodesRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorCouponCodesRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorCouponCodesRequest CouponCodeList(List<string> fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorCouponCodesRequest UploadRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorCouponCodesRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorCouponCodesResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorSetNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorSetNotifyUrlRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorSetNotifyUrlRequest Mchid(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorSetNotifyUrlRequest NotifyUrl(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorSetNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiSetBusifavorSetNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest()

- WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest CouponCode(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest Appid(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest UseTime(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest UseRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest Openid(string fieldValue)

- WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisBusifavorApiUseBusifavorCouponResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisCardApi (class)

- WechatPayV3Client client;

- public WechatPayV3MarketingMarketingApisCardApi(WechatPayV3Client client)

- async WechatPayV3MarketingMarketingApisCardApiSendCardResponse SendCardAsync(WechatPayV3MarketingMarketingApisCardApiSendCardRequest request)
  - POST /v3/marketing/busifavor/coupons/{数据.card_id}/发送; C# 参数：SendCardRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SendCardRawAsync(string path, string query, string jsonBody)


## WechatPayV3MarketingMarketingApisCardApiSendCardRequest (class)

Marketing/MarketingApis.Card.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisCardApiSendCardRequest()

- WechatPayV3MarketingMarketingApisCardApiSendCardRequest CardId(string fieldValue)

- WechatPayV3MarketingMarketingApisCardApiSendCardRequest Appid(string fieldValue)

- WechatPayV3MarketingMarketingApisCardApiSendCardRequest Openid(string fieldValue)

- WechatPayV3MarketingMarketingApisCardApiSendCardRequest OutRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisCardApiSendCardRequest SendTime(string fieldValue)

- WechatPayV3MarketingMarketingApisCardApiSendCardRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisCardApiSendCardResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApi (class)

- WechatPayV3Client client;

- public WechatPayV3MarketingMarketingApisFavorApi(WechatPayV3Client client)

- async WechatPayV3MarketingMarketingApisFavorApiCreateStockResponse CreateStockAsync(WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest request)
  - POST /v3/marketing/favor/coupon-stocks; C# 参数：CreateStockRequsetData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateStockRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisFavorApiStartStockResponse StartStockAsync(WechatPayV3MarketingMarketingApisFavorApiStartStockRequest request)
  - POST /v3/marketing/favor/stocks/{stock_id}/start; C# 参数：string stock_id, StartStockRequsetData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse StartStockRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisFavorApiDistributeStockResponse DistributeStockAsync(WechatPayV3MarketingMarketingApisFavorApiDistributeStockRequest request)
  - POST /v3/marketing/favor/users/{openid}/coupons; C# 参数：string openid, DistributeStockRequsetData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DistributeStockRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisFavorApiPauseStockResponse PauseStockAsync(WechatPayV3MarketingMarketingApisFavorApiPauseStockRequest request)
  - POST /v3/marketing/favor/stocks/{stock_id}/pause; C# 参数：string stock_id, string stock_creator_mchid, int timeOut = Config.TIME_OUT

- async WechatRawResponse PauseStockRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisFavorApiRestartStockResponse RestartStockAsync(WechatPayV3MarketingMarketingApisFavorApiRestartStockRequest request)
  - POST /v3/marketing/favor/stocks/{stock_id}/restart; C# 参数：string stock_id, string stock_creator_mchid, int timeOut = Config.TIME_OUT

- async WechatRawResponse RestartStockRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisFavorApiQueryStocksResponse QueryStocksAsync(WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest request)
  - GET /v3/marketing/favor/stocks?偏移={偏移}&limit={limit}&stock_creator_mchid={stock_creator_mchid}; C# 参数：uint 偏移, uint limit, string stock_creator_mchid, TenpayDateTime create_start_time = null, TenpayDateTime create_end_time = null, string status = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryStocksRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisFavorApiQueryStockResponse QueryStockAsync(WechatPayV3MarketingMarketingApisFavorApiQueryStockRequest request)
  - GET /v3/marketing/favor/stocks/{stock_id}?stock_creator_mchid={stock_creator_mchid}; C# 参数：string stock_id, string stock_creator_mchid, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryStockRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockResponse QueryMerchantsStockAsync(WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockRequest request)
  - GET /v3/marketing/favor/stocks/{stock_id}/merchants?偏移={偏移}&limit={limit}&stock_creator_mchid={stock_creator_mchid}; C# 参数：uint 偏移, uint limit, string stock_creator_mchid, string stock_id, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryMerchantsStockRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisFavorApiQueryItemsResponse QueryItemsAsync(WechatPayV3MarketingMarketingApisFavorApiQueryItemsRequest request)
  - GET /v3/marketing/favor/stocks/{stock_id}/项目?偏移={偏移}&limit={limit}&stock_creator_mchid={stock_creator_mchid}; C# 参数：uint 偏移, uint limit, string stock_creator_mchid, string stock_id, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryItemsRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisFavorApiQueryCouponsResponse QueryCouponsAsync(WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest request)
  - GET /v3/marketing/favor/users/{openid}/coupons?appid={appid}&偏移={偏移}&limit={limit}; C# 参数：string openid, string appid, string stock_id, string status, string creator_mchid, string sender_mchid, string available_mchid, uint 偏移 = 0, uint limit = 20, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCouponsRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisFavorApiQueryCouponResponse QueryCouponAsync(WechatPayV3MarketingMarketingApisFavorApiQueryCouponRequest request)
  - GET /v3/marketing/favor/users/{openid}/coupons/{coupon_id}?appid={appid}; C# 参数：string coupon_id, string appid, string openid, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryCouponRawAsync(string path, string query)

- async WechatRawResponse DownloadStockUseFlowAsync(WechatPayV3MarketingMarketingApisFavorApiDownloadStockUseFlowRequest request)
  - GET /v3/marketing/favor/stocks/{stock_id}/use-flow; C# 参数：string stock_id, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse DownloadStockUseFlowRawAsync(string path, string query)

- async WechatRawResponse DownloadStockRefundFlowAsync(WechatPayV3MarketingMarketingApisFavorApiDownloadStockRefundFlowRequest request)
  - GET /v3/marketing/favor/stocks/{stock_id}/refund-flow; C# 参数：string stock_id, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse DownloadStockRefundFlowRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisFavorApiSetNotifyUrlResponse SetNotifyUrlAsync(WechatPayV3MarketingMarketingApisFavorApiSetNotifyUrlRequest request)
  - POST /v3/marketing/favor/callbacks; C# 参数：SetNotifyUrlRequsetData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SetNotifyUrlRawAsync(string path, string query, string jsonBody)


## WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest (class)

Marketing/MarketingApis.Favor.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest()

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest StockName(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest Comment(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest BelongMerchant(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest AvailableBeginTime(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest AvailableEndTime(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest StockUseRule(WechatPayMarketingCreateStockRequsetDataStockUseRule fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest PatternInfo(WechatPayPatternInfo fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest CouponUseRule(WechatPayMarketingCreateStockRequsetDataCouponUseRule fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest NoCash(bool fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest StockType(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest OutRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiCreateStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiCreateStockResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiDistributeStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiDistributeStockRequest()

- WechatPayV3MarketingMarketingApisFavorApiDistributeStockRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiDistributeStockRequest OutRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiDistributeStockRequest Appid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiDistributeStockRequest StockCreatorMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiDistributeStockRequest Openid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiDistributeStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiDistributeStockResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiDownloadStockRefundFlowRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiDownloadStockRefundFlowRequest()

- WechatPayV3MarketingMarketingApisFavorApiDownloadStockRefundFlowRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiDownloadStockRefundFlowRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiDownloadStockRefundFlowResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiDownloadStockUseFlowRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiDownloadStockUseFlowRequest()

- WechatPayV3MarketingMarketingApisFavorApiDownloadStockUseFlowRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiDownloadStockUseFlowRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiDownloadStockUseFlowResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiPauseStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiPauseStockRequest()

- WechatPayV3MarketingMarketingApisFavorApiPauseStockRequest StockCreatorMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiPauseStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiPauseStockResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiQueryCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiQueryCouponRequest()

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponRequest CouponId(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponRequest Appid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponRequest Openid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiQueryCouponResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest()

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest Openid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest Appid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest Status(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest CreatorMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest SenderMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest AvailableMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest Offset(long fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest Limit(long fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryCouponsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiQueryCouponsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiQueryItemsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiQueryItemsRequest()

- WechatPayV3MarketingMarketingApisFavorApiQueryItemsRequest Offset(long fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryItemsRequest Limit(long fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryItemsRequest StockCreatorMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryItemsRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryItemsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiQueryItemsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockRequest()

- WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockRequest Offset(long fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockRequest Limit(long fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockRequest StockCreatorMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiQueryMerchantsStockResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiQueryStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiQueryStockRequest()

- WechatPayV3MarketingMarketingApisFavorApiQueryStockRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryStockRequest StockCreatorMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiQueryStockResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest()

- WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest Offset(long fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest Limit(long fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest StockCreatorMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest CreateStartTime(WechatPayTenpayDateTime fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest CreateEndTime(WechatPayTenpayDateTime fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest Status(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiQueryStocksRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiQueryStocksResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiRestartStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiRestartStockRequest()

- WechatPayV3MarketingMarketingApisFavorApiRestartStockRequest StockCreatorMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiRestartStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiRestartStockResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiSetNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiSetNotifyUrlRequest()

- WechatPayV3MarketingMarketingApisFavorApiSetNotifyUrlRequest Mchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiSetNotifyUrlRequest NotifyUrl(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiSetNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiSetNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisFavorApiStartStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisFavorApiStartStockRequest()

- WechatPayV3MarketingMarketingApisFavorApiStartStockRequest StockCreatorMchid(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiStartStockRequest StockId(string fieldValue)

- WechatPayV3MarketingMarketingApisFavorApiStartStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisFavorApiStartStockResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisImageApi (class)

- WechatPayV3Client client;

- public WechatPayV3MarketingMarketingApisImageApi(WechatPayV3Client client)

- async WechatPayV3MarketingMarketingApisImageApiUploadImageResponse UploadImage(WechatPayV3MarketingMarketingApisImageApiUploadImageRequest request)
  - POST /v3/marketing/favor/media/图片-upload; C# 参数：UploadImageRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadImage(string path, string query, string jsonBody)


## WechatPayV3MarketingMarketingApisImageApiUploadImageRequest (class)

Marketing/MarketingApis.Image.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisImageApiUploadImageRequest()

- WechatPayV3MarketingMarketingApisImageApiUploadImageRequest File(string fieldValue)

- WechatPayV3MarketingMarketingApisImageApiUploadImageRequest Meta(WechatPayMeta fieldValue)

- WechatPayV3MarketingMarketingApisImageApiUploadImageRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisImageApiUploadImageResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPartnershipsApi (class)

- WechatPayV3Client client;

- public WechatPayV3MarketingMarketingApisPartnershipsApi(WechatPayV3Client client)

- async WechatPayV3MarketingMarketingApisPartnershipsApiBuildPartnershipsResponse BuildPartnershipsAsync(WechatPayV3MarketingMarketingApisPartnershipsApiBuildPartnershipsRequest request)
  - POST /v3/marketing/partnerships/build; C# 参数：BuildPartnershipsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse BuildPartnershipsRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisPartnershipsApiTerminatePartnershipsResponse TerminatePartnershipsAsync(WechatPayV3MarketingMarketingApisPartnershipsApiTerminatePartnershipsRequest request)
  - POST /v3/marketing/partnerships/terminate; C# 参数：TerminatePartnershipsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse TerminatePartnershipsRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsResponse QueryPartnershipsAsync(WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsRequest request)
  - GET /v3/marketing/partnerships?authorized_data={数据.authorized_data.ToJson()}&partner={数据.partner.ToJson()}&偏移={偏移}&limit={limit}; C# 参数：QueryPartnershipsRequestData 数据, ulong limit = 20, ulong 偏移 = 0, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPartnershipsRawAsync(string path, string query)


## WechatPayV3MarketingMarketingApisPartnershipsApiBuildPartnershipsRequest (class)

Marketing/MarketingApis.Partnerships.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPartnershipsApiBuildPartnershipsRequest()

- WechatPayV3MarketingMarketingApisPartnershipsApiBuildPartnershipsRequest Partner(WechatPayMarketingBuildPartnershipsRequestDataPartner fieldValue)

- WechatPayV3MarketingMarketingApisPartnershipsApiBuildPartnershipsRequest AuthorizedData(WechatPayMarketingBuildPartnershipsRequestDataAuthorizedData fieldValue)

- WechatPayV3MarketingMarketingApisPartnershipsApiBuildPartnershipsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPartnershipsApiBuildPartnershipsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsRequest()

- WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsRequest Partner(WechatPayMarketingQueryPartnershipsRequestDataPartner fieldValue)

- WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsRequest AuthorizedData(WechatPayMarketingQueryPartnershipsRequestDataAuthorizedData fieldValue)

- WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsRequest Limit(long fieldValue)

- WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsRequest Offset(long fieldValue)

- WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPartnershipsApiQueryPartnershipsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPartnershipsApiTerminatePartnershipsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPartnershipsApiTerminatePartnershipsRequest()

- WechatPayV3MarketingMarketingApisPartnershipsApiTerminatePartnershipsRequest Partner(WechatPayMarketingTerminatePartnershipsRequestDataPartner fieldValue)

- WechatPayV3MarketingMarketingApisPartnershipsApiTerminatePartnershipsRequest AuthorizedData(WechatPayMarketingTerminatePartnershipsRequestDataAuthorizedData fieldValue)

- WechatPayV3MarketingMarketingApisPartnershipsApiTerminatePartnershipsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPartnershipsApiTerminatePartnershipsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPaygiftApi (class)

- WechatPayV3Client client;

- public WechatPayV3MarketingMarketingApisPaygiftApi(WechatPayV3Client client)

- async WechatPayV3MarketingMarketingApisPaygiftApiCreateUniqueThresholdActivityResponse CreateUniqueThresholdActivityAsync(WechatPayV3MarketingMarketingApisPaygiftApiCreateUniqueThresholdActivityRequest request)
  - POST /v3/marketing/paygiftactivity/unique-threshold-activity; C# 参数：CreateUniqueThresholdActivityRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateUniqueThresholdActivityRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityResponse QueryPaygiftActivityAsync(WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityRequest request)
  - GET /v3/marketing/paygiftactivity/activities/{activity_id}; C# 参数：string activity_id, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPaygiftActivityRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityMerchantsResponse QueryPaygiftActivityMerchantsAsync(WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityMerchantsRequest request)
  - GET /v3/marketing/paygiftactivity/activities/{activity_id}/merchants?偏移={偏移}&limit={limit}; C# 参数：string activity_id, ulong limit = 10, ulong 偏移 = 0, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPaygiftActivityMerchantsRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityGoodsResponse QueryPaygiftActivityGoodsAsync(WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityGoodsRequest request)
  - GET /v3/marketing/paygiftactivity/activities/{activity_id}/goods?偏移={偏移}&limit={limit}; C# 参数：string activity_id, ulong limit = 10, ulong 偏移 = 0, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPaygiftActivityGoodsRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisPaygiftApiTerminatePaygiftActivityResponse TerminatePaygiftActivityAsync(WechatPayV3MarketingMarketingApisPaygiftApiTerminatePaygiftActivityRequest request)
  - POST /v3/marketing/paygiftactivity/activities/{activity_id}/terminate; C# 参数：string activity_id, int timeOut = Config.TIME_OUT

- async WechatRawResponse TerminatePaygiftActivityRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisPaygiftApiAddPaygiftActivityMerchantsResponse AddPaygiftActivityMerchantsAsync(WechatPayV3MarketingMarketingApisPaygiftApiAddPaygiftActivityMerchantsRequest request)
  - POST /v3/marketing/paygiftactivity/activities/{数据.activity_id}/merchants/添加; C# 参数：AddPaygiftActivityMerchantsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AddPaygiftActivityMerchantsRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesResponse QueryPaygiftActivitiesAsync(WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesRequest request)
  - GET /v3/marketing/paygiftactivity/activities?偏移={偏移}&limit={limit}; C# 参数：string activity_name, string activity_status, string award_type, int limit = 10, int 偏移 = 0, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPaygiftActivitiesRawAsync(string path, string query)

- async WechatPayV3MarketingMarketingApisPaygiftApiDeletePaygiftActivitiyMerchantsResponse DeletePaygiftActivitiyMerchantsAsync(WechatPayV3MarketingMarketingApisPaygiftApiDeletePaygiftActivitiyMerchantsRequest request)
  - POST /v3/marketing/paygiftactivity/activities/{数据.activity_id}/merchants/删除; C# 参数：DeletePaygiftActivitiyMerchantsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DeletePaygiftActivitiyMerchantsRawAsync(string path, string query, string jsonBody)


## WechatPayV3MarketingMarketingApisPaygiftApiAddPaygiftActivityMerchantsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPaygiftApiAddPaygiftActivityMerchantsRequest()

- WechatPayV3MarketingMarketingApisPaygiftApiAddPaygiftActivityMerchantsRequest ActivityId(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiAddPaygiftActivityMerchantsRequest MerchantIdList(List<string> fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiAddPaygiftActivityMerchantsRequest AddRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiAddPaygiftActivityMerchantsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPaygiftApiAddPaygiftActivityMerchantsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPaygiftApiCreateUniqueThresholdActivityRequest (class)

Marketing/MarketingApis.Paygift.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPaygiftApiCreateUniqueThresholdActivityRequest()

- WechatPayV3MarketingMarketingApisPaygiftApiCreateUniqueThresholdActivityRequest ActivityBaseInfo(WechatPayMarketingCreateUniqueThresholdActivityRequestDataActivityBaseInfo fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiCreateUniqueThresholdActivityRequest AwardSendRule(WechatPayMarketingCreateUniqueThresholdActivityRequestDataAwardSendRule fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiCreateUniqueThresholdActivityRequest AdvancedSetting(WechatPayMarketingCreateUniqueThresholdActivityRequestDataAdvancedSetting fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiCreateUniqueThresholdActivityRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPaygiftApiCreateUniqueThresholdActivityResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPaygiftApiDeletePaygiftActivitiyMerchantsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPaygiftApiDeletePaygiftActivitiyMerchantsRequest()

- WechatPayV3MarketingMarketingApisPaygiftApiDeletePaygiftActivitiyMerchantsRequest ActivityId(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiDeletePaygiftActivitiyMerchantsRequest MerchantIdList(List<string> fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiDeletePaygiftActivitiyMerchantsRequest DeleteRequestNo(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiDeletePaygiftActivitiyMerchantsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPaygiftApiDeletePaygiftActivitiyMerchantsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesRequest()

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesRequest ActivityName(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesRequest ActivityStatus(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesRequest AwardType(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesRequest Limit(int fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesRequest Offset(int fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivitiesResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityGoodsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityGoodsRequest()

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityGoodsRequest ActivityId(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityGoodsRequest Limit(long fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityGoodsRequest Offset(long fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityGoodsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityGoodsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityMerchantsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityMerchantsRequest()

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityMerchantsRequest ActivityId(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityMerchantsRequest Limit(long fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityMerchantsRequest Offset(long fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityMerchantsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityMerchantsResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityRequest()

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityRequest ActivityId(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPaygiftApiQueryPaygiftActivityResponse (class)

- public string Raw;


## WechatPayV3MarketingMarketingApisPaygiftApiTerminatePaygiftActivityRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MarketingMarketingApisPaygiftApiTerminatePaygiftActivityRequest()

- WechatPayV3MarketingMarketingApisPaygiftApiTerminatePaygiftActivityRequest ActivityId(string fieldValue)

- WechatPayV3MarketingMarketingApisPaygiftApiTerminatePaygiftActivityRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MarketingMarketingApisPaygiftApiTerminatePaygiftActivityResponse (class)

- public string Raw;


## WechatPayV3MedicalInsuranceMedicalInsuranceApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3MedicalInsuranceMedicalInsuranceApisApi(WechatPayV3Client client)

- async WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderResponse CreateOrderAsync(WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest request)
  - POST /v3/med-ins/orders; C# 参数：MedicalInsuranceOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByMixTradeNoResponse QueryOrderByMixTradeNoAsync(WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByMixTradeNoRequest request)
  - GET /v3/med-ins/orders/mix-trade-no/{Escape(mixTradeNo)}; C# 参数：string mixTradeNo, string subMchId = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryOrderByMixTradeNoRawAsync(string path, string query)

- async WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByOutTradeNoResponse QueryOrderByOutTradeNoAsync(WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByOutTradeNoRequest request)
  - GET /v3/med-ins/orders/out-trade-no/{Escape(outTradeNo)}; C# 参数：string outTradeNo, string subMchId = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryOrderByOutTradeNoRawAsync(string path, string query)

- async WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessResponse NotifyRefundSuccessAsync(WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest request)
  - POST /v3/med-ins/refunds/notify{query}; C# 参数：string mixTradeNo, MedicalInsuranceRefundNotifyRequestData 数据, string subMchId = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse NotifyRefundSuccessRawAsync(string path, string query, string jsonBody)


## WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest (class)

MedicalInsurance/MedicalInsuranceApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest()

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest MixPayType(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest OrderType(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest Appid(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest SubAppid(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest SubMchid(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest Openid(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest SubOpenid(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest Payer(WechatPayMedicalInsuranceIdentity fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest PayForRelatives(bool fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest Relative(WechatPayMedicalInsuranceIdentity fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest OutTradeNo(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest SerialNo(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest PayOrderId(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest PayAuthNo(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest GeoLocation(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest CityId(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest MedInstName(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest MedInstNo(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest MedInsOrderCreateTime(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest TotalFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest MedInsGovFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest MedInsSelfFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest MedInsOtherFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest MedInsCashFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest WechatPayCashFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest CashAddDetail(List<WechatPayMedicalInsuranceCashAddDetail> fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest CashReduceDetail(List<WechatPayMedicalInsuranceCashReduceDetail> fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest CallbackUrl(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest PrepayId(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest PassthroughRequestContent(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest Extends(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest Attach(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest ChannelNo(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest MedInsTestEnv(bool fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MedicalInsuranceMedicalInsuranceApisApiCreateOrderResponse (class)

- public string Raw;


## WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest()

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest MedRefundTotalFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest MedRefundGovFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest MedRefundSelfFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest MedRefundOtherFee(long fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest RefundTime(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest OutRefundNo(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest MixTradeNo(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest SubMchId(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MedicalInsuranceMedicalInsuranceApisApiNotifyRefundSuccessResponse (class)

- public string Raw;


## WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByMixTradeNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByMixTradeNoRequest()

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByMixTradeNoRequest MixTradeNo(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByMixTradeNoRequest SubMchId(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByMixTradeNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByMixTradeNoResponse (class)

- public string Raw;


## WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByOutTradeNoRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByOutTradeNoRequest()

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByOutTradeNoRequest OutTradeNo(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByOutTradeNoRequest SubMchId(string fieldValue)

- WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByOutTradeNoRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MedicalInsuranceMedicalInsuranceApisApiQueryOrderByOutTradeNoResponse (class)

- public string Raw;


## WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApi(WechatPayV3Client client)

- async WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiStartVerificationResponse StartVerificationAsync(WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiStartVerificationRequest request)
  - POST /v3/compliance/inactive-merchant-identity-verification/merchants; C# 参数：InactiveMerchantVerificationRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse StartVerificationRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiQueryVerificationResponse QueryVerificationAsync(WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiQueryVerificationRequest request)
  - GET /v3/compliance/inactive-merchant-identity-verification/merchants/{Escape(subMchId)}/verifications/{Escape(verificationId)}; C# 参数：string subMchId, string verificationId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryVerificationRawAsync(string path, string query)


## WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiQueryVerificationRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiQueryVerificationRequest()

- WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiQueryVerificationRequest SubMchId(string fieldValue)

- WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiQueryVerificationRequest VerificationId(string fieldValue)

- WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiQueryVerificationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiQueryVerificationResponse (class)

- public string Raw;


## WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiStartVerificationRequest (class)

MerchantGovernance/InactiveMerchantVerificationApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiStartVerificationRequest()

- WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiStartVerificationRequest SubMchid(string fieldValue)

- WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiStartVerificationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MerchantGovernanceInactiveMerchantVerificationApisApiStartVerificationResponse (class)

- public string Raw;


## WechatPayV3MerchantGovernanceMerchantLimitationApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3MerchantGovernanceMerchantLimitationApisApi(WechatPayV3Client client)

- async WechatPayV3MerchantGovernanceMerchantLimitationApisApiQuerySubMerchantLimitationResponse QuerySubMerchantLimitationAsync(WechatPayV3MerchantGovernanceMerchantLimitationApisApiQuerySubMerchantLimitationRequest request)
  - GET /v3/mch-operation-manage/merchant-limitations/sub-mchid/{Escape(subMchId)}; C# 参数：string subMchId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySubMerchantLimitationRawAsync(string path, string query)


## WechatPayV3MerchantGovernanceMerchantLimitationApisApiQuerySubMerchantLimitationRequest (class)

MerchantGovernance/MerchantLimitationApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MerchantGovernanceMerchantLimitationApisApiQuerySubMerchantLimitationRequest()

- WechatPayV3MerchantGovernanceMerchantLimitationApisApiQuerySubMerchantLimitationRequest SubMchId(string fieldValue)

- WechatPayV3MerchantGovernanceMerchantLimitationApisApiQuerySubMerchantLimitationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MerchantGovernanceMerchantLimitationApisApiQuerySubMerchantLimitationResponse (class)

- public string Raw;


## WechatPayV3MerchantRiskMerchantRiskApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3MerchantRiskMerchantRiskApisApi(WechatPayV3Client client)

- async WechatPayV3MerchantRiskMerchantRiskApisApiCreateMerchantRiskNotifyUrlResponse CreateMerchantRiskNotifyUrlAsync(WechatPayV3MerchantRiskMerchantRiskApisApiCreateMerchantRiskNotifyUrlRequest request)
  - POST /v3/merchant-risk-manage/violation-notifications; C# 参数：CreateMerchantRiskNotifyUrlRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateMerchantRiskNotifyUrlRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MerchantRiskMerchantRiskApisApiQueryMerchantRiskNotifyUrlResponse QueryMerchantRiskNotifyUrlAsync(WechatPayV3MerchantRiskMerchantRiskApisApiQueryMerchantRiskNotifyUrlRequest request)
  - GET /v3/merchant-risk-manage/violation-notifications; C# 参数：int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryMerchantRiskNotifyUrlRawAsync(string path, string query)

- async WechatPayV3MerchantRiskMerchantRiskApisApiUpdateMerchantRiskNotifyUrlResponse UpdateMerchantRiskNotifyUrlAsync(WechatPayV3MerchantRiskMerchantRiskApisApiUpdateMerchantRiskNotifyUrlRequest request)
  - PUT /v3/merchant-risk-manage/violation-notifications; C# 参数：UpdateMerchantRiskNotifyUrlRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateMerchantRiskNotifyUrlRawAsync(string path, string query, string jsonBody)

- async WechatPayV3MerchantRiskMerchantRiskApisApiDeleteMerchantRiskNotifyUrlResponse DeleteMerchantRiskNotifyUrlAsync(WechatPayV3MerchantRiskMerchantRiskApisApiDeleteMerchantRiskNotifyUrlRequest request)
  - DELETE /v3/merchant-risk-manage/violation-notifications; C# 参数：int timeOut = Config.TIME_OUT

- async WechatRawResponse DeleteMerchantRiskNotifyUrlRawAsync(string path, string query)


## WechatPayV3MerchantRiskMerchantRiskApisApiCreateMerchantRiskNotifyUrlRequest (class)

MerchantRisk/MerchantRiskApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MerchantRiskMerchantRiskApisApiCreateMerchantRiskNotifyUrlRequest()

- WechatPayV3MerchantRiskMerchantRiskApisApiCreateMerchantRiskNotifyUrlRequest NotifyUrl(string fieldValue)

- WechatPayV3MerchantRiskMerchantRiskApisApiCreateMerchantRiskNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MerchantRiskMerchantRiskApisApiCreateMerchantRiskNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3MerchantRiskMerchantRiskApisApiDeleteMerchantRiskNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MerchantRiskMerchantRiskApisApiDeleteMerchantRiskNotifyUrlRequest()

- WechatPayV3MerchantRiskMerchantRiskApisApiDeleteMerchantRiskNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MerchantRiskMerchantRiskApisApiDeleteMerchantRiskNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3MerchantRiskMerchantRiskApisApiQueryMerchantRiskNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MerchantRiskMerchantRiskApisApiQueryMerchantRiskNotifyUrlRequest()

- WechatPayV3MerchantRiskMerchantRiskApisApiQueryMerchantRiskNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MerchantRiskMerchantRiskApisApiQueryMerchantRiskNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3MerchantRiskMerchantRiskApisApiUpdateMerchantRiskNotifyUrlRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3MerchantRiskMerchantRiskApisApiUpdateMerchantRiskNotifyUrlRequest()

- WechatPayV3MerchantRiskMerchantRiskApisApiUpdateMerchantRiskNotifyUrlRequest NotifyUrl(string fieldValue)

- WechatPayV3MerchantRiskMerchantRiskApisApiUpdateMerchantRiskNotifyUrlRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3MerchantRiskMerchantRiskApisApiUpdateMerchantRiskNotifyUrlResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3ParkingReminderParkingReminderApisApi(WechatPayV3Client client)

- async WechatPayV3ParkingReminderParkingReminderApisApiSubmitApplicationResponse SubmitApplicationAsync(WechatPayV3ParkingReminderParkingReminderApisApiSubmitApplicationRequest request)
  - POST /v3/parking/reminders/application; C# 参数：ParkingLotApplicationRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SubmitApplicationRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationResponse QueryApplicationAsync(WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationRequest request)
  - GET /v3/parking/reminders/application/query; C# 参数：string parkingLotAuditNo, string outParkingLotId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApplicationRawAsync(string path, string query)

- async WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationListResponse QueryApplicationListAsync(WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationListRequest request)
  - GET /v3/parking/reminders/applications; C# 参数：string outParkingLotId, int? 偏移 = null, int? limit = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryApplicationListRawAsync(string path, string query)

- async WechatPayV3ParkingReminderParkingReminderApisApiWithdrawApplicationResponse WithdrawApplicationAsync(WechatPayV3ParkingReminderParkingReminderApisApiWithdrawApplicationRequest request)
  - POST /v3/parking/reminders/application/withdraw; C# 参数：string parkingLotAuditNo, string outParkingLotId, int timeOut = Config.TIME_OUT

- async WechatRawResponse WithdrawApplicationRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryResponse SyncEntryAsync(WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest request)
  - POST /v3/parking/reminders/条目; C# 参数：ParkingEntryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SyncEntryRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ParkingReminderParkingReminderApisApiSyncExitResponse SyncExitAsync(WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest request)
  - POST /v3/parking/reminders/exit; C# 参数：ParkingExitRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SyncExitRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentResponse SyncPaymentAsync(WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest request)
  - POST /v3/parking/reminders/payment; C# 参数：ParkingPaymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SyncPaymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentResponse SyncExtensionPaymentAsync(WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentRequest request)
  - POST /v3/parking/reminders/ext-payment; C# 参数：ParkingExtensionPaymentRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SyncExtensionPaymentRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingLotResponse QueryParkingLotAsync(WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingLotRequest request)
  - GET /v3/parking/reminders/parking-lot; C# 参数：string outParkingLotId = null, string wxParkingLotId = null, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryParkingLotRawAsync(string path, string query)

- async WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingFeeResponse QueryParkingFeeAsync(WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingFeeRequest request)
  - POST /v3/parking/reminders/parking-fee; C# 参数：ParkingFeeRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryParkingFeeRawAsync(string path, string query, string jsonBody)


## WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationListRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationListRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationListRequest OutParkingLotId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationListRequest Offset(int fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationListRequest Limit(int fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationListRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationListResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationRequest ParkingLotAuditNo(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationRequest OutParkingLotId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiQueryApplicationResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingFeeRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingFeeRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingFeeRequest OutSerialNumber(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingFeeRequest PlateNumber(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingFeeRequest OutParkingId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingFeeRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingFeeResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingLotRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingLotRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingLotRequest OutParkingLotId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingLotRequest WxParkingLotId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingLotRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiQueryParkingLotResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApiSubmitApplicationRequest (class)

ParkingReminder/ParkingReminderApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiSubmitApplicationRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiSubmitApplicationRequest ParkingLot(WechatPayParkingLotApplicationData fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSubmitApplicationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiSubmitApplicationResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest OutSerialNumber(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest ParkingId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest PlateNumber(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest EnterTimestamp(long fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest PlateColor(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest CarType(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest VehicleType(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest EntranceNumber(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest EntranceName(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest DiscountTemplateId(List<long> fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiSyncEntryResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest OutSerialNumber(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest ExitTimestamp(long fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest PlateNumber(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest PlateColor(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest ParkingId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest PayState(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest PayType(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest OutTradeNo(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest TotalAmount(long fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest PaidAmount(long fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest Openid(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest PayChannel(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest PayTime(long fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest Token(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest WxTradeNo(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExitRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiSyncExitResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentRequest OutTradeNo(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentRequest FeeItems(List<WechatPayParkingExtensionFeeItem> fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentRequest PayTime(long fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentRequest WxParkingLotId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentRequest WxTradeNo(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiSyncExtensionPaymentResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest OutSerialNumber(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest PlateNumber(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest ParkingId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest ParkingState(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest PayType(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest Openid(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest SubMchid(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest TotalAmount(long fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest PaidAmount(long fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest OutTradeNo(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest PayChannel(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest PayTime(long fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest Token(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest WxTradeNo(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiSyncPaymentResponse (class)

- public string Raw;


## WechatPayV3ParkingReminderParkingReminderApisApiWithdrawApplicationRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ParkingReminderParkingReminderApisApiWithdrawApplicationRequest()

- WechatPayV3ParkingReminderParkingReminderApisApiWithdrawApplicationRequest ParkingLotAuditNo(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiWithdrawApplicationRequest OutParkingLotId(string fieldValue)

- WechatPayV3ParkingReminderParkingReminderApisApiWithdrawApplicationRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ParkingReminderParkingReminderApisApiWithdrawApplicationResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3PayScorePayScoreApisApi(WechatPayV3Client client)

- async WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderResponse CreateDirectCompleteServiceOrderAsync(WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest request)
  - POST /v3/serviceorder/direct-complete; C# 参数：CreateDirectCompleteServiceOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateDirectCompleteServiceOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiGivePermissionResponse GivePermissionAsync(WechatPayV3PayScorePayScoreApisApiGivePermissionRequest request)
  - POST /v3/payscore/permissions; C# 参数：GivePermissionRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GivePermissionRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiQueryPermissionByAuthorizationCodeResponse QueryPermissionByAuthorizationCodeAsync(WechatPayV3PayScorePayScoreApisApiQueryPermissionByAuthorizationCodeRequest request)
  - GET /v3/payscore/permissions/authorization-code/{authorization_code}&service_id={service_id}; C# 参数：string service_id, string authorization_code, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPermissionByAuthorizationCodeRawAsync(string path, string query)

- async WechatPayV3PayScorePayScoreApisApiTerminatePermissionByAuthorizationCodeResponse TerminatePermissionByAuthorizationCodeAsync(WechatPayV3PayScorePayScoreApisApiTerminatePermissionByAuthorizationCodeRequest request)
  - POST /v3/payscore/permissions/authorization-code/{数据.authorization_code}/terminate; C# 参数：TerminatePermissionByAuthorizationCodeRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse TerminatePermissionByAuthorizationCodeRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiQueryPermissionByOpenidResponse QueryPermissionByOpenidAsync(WechatPayV3PayScorePayScoreApisApiQueryPermissionByOpenidRequest request)
  - GET /v3/payscore/permissions/openid/{openid}?appid={appid}&service_id={service_id}; C# 参数：string service_id, string appid, string openid, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryPermissionByOpenidRawAsync(string path, string query)

- async WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidResponse TerminatePermissionByOpenidAsync(WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidRequest request)
  - POST /v3/payscore/permissions/openid/{数据.openid}/terminate; C# 参数：TerminatePermissionByOpenidRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse TerminatePermissionByOpenidRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiCreateServiceOrderResponse CreateServiceOrderAsync(WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest request)
  - POST /v3/payscore/serviceorder; C# 参数：CreateServiceOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateServiceOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiQueryServiceOrderResponse QueryServiceOrderAsync(WechatPayV3PayScorePayScoreApisApiQueryServiceOrderRequest request)
  - GET /v3/payscore/serviceorder?service_id={service_id}&appid={appid}; C# 参数：string out_order_no, string query_id, string service_id, string appid, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryServiceOrderRawAsync(string path, string query)

- async WechatPayV3PayScorePayScoreApisApiCancelServiceOrderResponse CancelServiceOrderAsync(WechatPayV3PayScorePayScoreApisApiCancelServiceOrderRequest request)
  - POST /v3/payscore/serviceorder/{数据.out_order_no}/cancel; C# 参数：CancelServiceOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CancelServiceOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiModifyServiceOrderResponse ModifyServiceOrderAsync(WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest request)
  - POST /v3/payscore/serviceorder/{数据.out_order_no}/modify; C# 参数：ModifyServiceOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifyServiceOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderResponse CompleteServiceOrderAsync(WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest request)
  - POST /v3/payscore/serviceorder/{数据.out_order_no}/complete; C# 参数：CompleteServiceOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CompleteServiceOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiPayServiceOrderResponse PayServiceOrderAsync(WechatPayV3PayScorePayScoreApisApiPayServiceOrderRequest request)
  - POST /v3/payscore/serviceorder/{数据.out_order_no}/pay; C# 参数：PayServiceOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse PayServiceOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderResponse SyncPayServiceOrderAsync(WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderRequest request)
  - POST /v3/payscore/serviceorder/{数据.out_order_no}/sync; C# 参数：SyncPayServiceOrderRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SyncPayServiceOrderRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiRegisterGuideResponse RegisterGuideAsync(WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest request)
  - POST /v3/smartguide/guides; C# 参数：RegisterGuideRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse RegisterGuideRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiAssignGuideResponse AssignGuideAsync(WechatPayV3PayScorePayScoreApisApiAssignGuideRequest request)
  - POST /v3/smartguide/guides/{数据.guide_id}/assign; C# 参数：AssignGuideRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AssignGuideRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiQueryGuideResponse QueryGuideAsync(WechatPayV3PayScorePayScoreApisApiQueryGuideRequest request)
  - GET /v3/smartguide/guides?store_id={UrlQueryHelper.ToParams(数据)}; C# 参数：QueryGuideRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryGuideRawAsync(string path, string query)

- async WechatPayV3PayScorePayScoreApisApiModifyGuideResponse ModifyGuideAsync(WechatPayV3PayScorePayScoreApisApiModifyGuideRequest request)
  - PATCH /v3/smartguide/guides/{数据.guide_id}; C# 参数：ModifyGuideRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifyGuideRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiGoldplanChangeGoldplanStatusResponse GoldplanChangeGoldplanStatusAsync(WechatPayV3PayScorePayScoreApisApiGoldplanChangeGoldplanStatusRequest request)
  - POST /v3/goldplan/merchants/changegoldplanstatus; C# 参数：GoldplanChangeGoldplanStatusRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GoldplanChangeGoldplanStatusRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiGoldplanChangeCustomPageStatusResponse GoldplanChangeCustomPageStatusAsync(WechatPayV3PayScorePayScoreApisApiGoldplanChangeCustomPageStatusRequest request)
  - POST /v3/goldplan/merchants/changecustompagestatus; C# 参数：GoldplanChangeCustomPageStatusRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GoldplanChangeCustomPageStatusRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiGoldplanSetAdvertisingIndustryFilterResponse GoldplanSetAdvertisingIndustryFilterAsync(WechatPayV3PayScorePayScoreApisApiGoldplanSetAdvertisingIndustryFilterRequest request)
  - POST /v3/goldplan/merchants/设置-advertising-industry-filter; C# 参数：GoldplanSetAdvertisingIndustryFilterRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GoldplanSetAdvertisingIndustryFilterRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiGoldplanOpenAdvertisingShowResponse GoldplanOpenAdvertisingShowAsync(WechatPayV3PayScorePayScoreApisApiGoldplanOpenAdvertisingShowRequest request)
  - POST /v3/goldplan/merchants/open-advertising-show; C# 参数：GoldplanOpenAdvertisingShowRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GoldplanOpenAdvertisingShowRawAsync(string path, string query, string jsonBody)

- async WechatPayV3PayScorePayScoreApisApiGoldplanCloseAdvertisingShowResponse GoldplanCloseAdvertisingShowAsync(WechatPayV3PayScorePayScoreApisApiGoldplanCloseAdvertisingShowRequest request)
  - POST /v3/goldplan/merchants/close-advertising-show; C# 参数：GoldplanCloseAdvertisingShowRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse GoldplanCloseAdvertisingShowRawAsync(string path, string query, string jsonBody)


## WechatPayV3PayScorePayScoreApisApiAssignGuideRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiAssignGuideRequest()

- WechatPayV3PayScorePayScoreApisApiAssignGuideRequest SubMchid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiAssignGuideRequest GuideId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiAssignGuideRequest OutTradeNo(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiAssignGuideRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiAssignGuideResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiCancelServiceOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiCancelServiceOrderRequest()

- WechatPayV3PayScorePayScoreApisApiCancelServiceOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCancelServiceOrderRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCancelServiceOrderRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCancelServiceOrderRequest Reason(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCancelServiceOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiCancelServiceOrderResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest()

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest PostPayments(List<WechatPayPayScoreCompleteServiceOrderRequestDataPostPayment> fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest PostDiscounts(List<WechatPayPayScoreCompleteServiceOrderRequestDataPostDiscount> fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest TotalAmount(long fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest TimeRange(WechatPayPayScoreCompleteServiceOrderRequestDataTimeRange fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest Location(WechatPayPayScoreCompleteServiceOrderRequestDataLocation fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest ProfitSharing(bool fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest GoodsTag(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest Device(WechatPayServiceOrderRequestDevice fieldValue)

- WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiCompleteServiceOrderResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest (class)

PayScore/PayScoreApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest()

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest Openid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest ServiceIntroduction(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest PostPayments(List<WechatPayPayScoreCreateDirectCompleteServiceOrderRequestDataPostPayment> fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest PostDiscounts(List<WechatPayPayScoreCreateDirectCompleteServiceOrderRequestDataPostDiscount> fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest TimeRange(WechatPayPayScoreCreateDirectCompleteServiceOrderRequestDataTimeRange fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest Location(WechatPayPayScoreCreateDirectCompleteServiceOrderRequestDataLocation fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest TotalAmount(long fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest ProfitSharing(bool fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest GoodsTag(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest Attach(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest NotifyUrl(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiCreateDirectCompleteServiceOrderResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest()

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest ServiceIntroduction(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest PostPayments(List<WechatPayPayScoreCreateServiceOrderRequestDataPostPayment> fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest PostDiscounts(List<WechatPayPayScoreCreateServiceOrderRequestDataPostDiscount> fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest TimeRange(WechatPayPayScoreCreateServiceOrderRequestDataTimeRange fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest Location(WechatPayPayScoreCreateServiceOrderRequestDataLocation fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest RiskFund(WechatPayPayScoreCreateServiceOrderRequestDataRiskFund fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest Attach(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest NotifyUrl(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest Openid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest NeedUserConfirm(bool fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest Device(WechatPayServiceOrderRequestDevice fieldValue)

- WechatPayV3PayScorePayScoreApisApiCreateServiceOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiCreateServiceOrderResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiGivePermissionRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiGivePermissionRequest()

- WechatPayV3PayScorePayScoreApisApiGivePermissionRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGivePermissionRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGivePermissionRequest AuthorizationCode(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGivePermissionRequest NotifyUrl(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGivePermissionRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiGivePermissionResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiGoldplanChangeCustomPageStatusRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiGoldplanChangeCustomPageStatusRequest()

- WechatPayV3PayScorePayScoreApisApiGoldplanChangeCustomPageStatusRequest SubMchid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGoldplanChangeCustomPageStatusRequest OperationType(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGoldplanChangeCustomPageStatusRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiGoldplanChangeCustomPageStatusResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiGoldplanChangeGoldplanStatusRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiGoldplanChangeGoldplanStatusRequest()

- WechatPayV3PayScorePayScoreApisApiGoldplanChangeGoldplanStatusRequest SubMchid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGoldplanChangeGoldplanStatusRequest OperationType(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGoldplanChangeGoldplanStatusRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiGoldplanChangeGoldplanStatusResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiGoldplanCloseAdvertisingShowRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiGoldplanCloseAdvertisingShowRequest()

- WechatPayV3PayScorePayScoreApisApiGoldplanCloseAdvertisingShowRequest SubMchid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGoldplanCloseAdvertisingShowRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiGoldplanCloseAdvertisingShowResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiGoldplanOpenAdvertisingShowRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiGoldplanOpenAdvertisingShowRequest()

- WechatPayV3PayScorePayScoreApisApiGoldplanOpenAdvertisingShowRequest SubMchid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGoldplanOpenAdvertisingShowRequest AdvertisingIndustryFilters(List<string> fieldValue)

- WechatPayV3PayScorePayScoreApisApiGoldplanOpenAdvertisingShowRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiGoldplanOpenAdvertisingShowResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiGoldplanSetAdvertisingIndustryFilterRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiGoldplanSetAdvertisingIndustryFilterRequest()

- WechatPayV3PayScorePayScoreApisApiGoldplanSetAdvertisingIndustryFilterRequest SubMchid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiGoldplanSetAdvertisingIndustryFilterRequest AdvertisingIndustryFilters(List<string> fieldValue)

- WechatPayV3PayScorePayScoreApisApiGoldplanSetAdvertisingIndustryFilterRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiGoldplanSetAdvertisingIndustryFilterResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiModifyGuideRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiModifyGuideRequest()

- WechatPayV3PayScorePayScoreApisApiModifyGuideRequest GuideId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyGuideRequest Name(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyGuideRequest Mobile(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyGuideRequest QrCode(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyGuideRequest Avatar(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyGuideRequest GroupQrcode(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyGuideRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiModifyGuideResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest()

- WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest PostPayments(List<WechatPayPayScoreModifyServiceOrderRequestDataPostPayment> fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest PostDiscounts(List<WechatPayPayScoreModifyServiceOrderRequestDataPostDiscount> fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest TotalAmount(long fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest Reason(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest Device(WechatPayServiceOrderRequestDevice fieldValue)

- WechatPayV3PayScorePayScoreApisApiModifyServiceOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiModifyServiceOrderResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiPayServiceOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiPayServiceOrderRequest()

- WechatPayV3PayScorePayScoreApisApiPayServiceOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiPayServiceOrderRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiPayServiceOrderRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiPayServiceOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiPayServiceOrderResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiQueryGuideRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiQueryGuideRequest()

- WechatPayV3PayScorePayScoreApisApiQueryGuideRequest SubMchid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryGuideRequest StoreId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryGuideRequest Userid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryGuideRequest Mobile(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryGuideRequest WorkId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryGuideRequest Limit(int fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryGuideRequest Offset(int fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryGuideRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiQueryGuideResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiQueryPermissionByAuthorizationCodeRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiQueryPermissionByAuthorizationCodeRequest()

- WechatPayV3PayScorePayScoreApisApiQueryPermissionByAuthorizationCodeRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryPermissionByAuthorizationCodeRequest AuthorizationCode(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryPermissionByAuthorizationCodeRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiQueryPermissionByAuthorizationCodeResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiQueryPermissionByOpenidRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiQueryPermissionByOpenidRequest()

- WechatPayV3PayScorePayScoreApisApiQueryPermissionByOpenidRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryPermissionByOpenidRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryPermissionByOpenidRequest Openid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryPermissionByOpenidRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiQueryPermissionByOpenidResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiQueryServiceOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiQueryServiceOrderRequest()

- WechatPayV3PayScorePayScoreApisApiQueryServiceOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryServiceOrderRequest QueryId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryServiceOrderRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryServiceOrderRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiQueryServiceOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiQueryServiceOrderResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest()

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest SubMchid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest Corpid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest StoreId(int fieldValue)

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest Userid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest Name(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest Mobile(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest QrCode(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest Avatar(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest GroupQrcode(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiRegisterGuideRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiRegisterGuideResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderRequest()

- WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderRequest OutOrderNo(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderRequest Type(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderRequest Detail(WechatPayPayScoreSyncPayServiceOrderRequestDataDetail fieldValue)

- WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiSyncPayServiceOrderResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiTerminatePermissionByAuthorizationCodeRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiTerminatePermissionByAuthorizationCodeRequest()

- WechatPayV3PayScorePayScoreApisApiTerminatePermissionByAuthorizationCodeRequest AuthorizationCode(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiTerminatePermissionByAuthorizationCodeRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiTerminatePermissionByAuthorizationCodeRequest Reason(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiTerminatePermissionByAuthorizationCodeRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiTerminatePermissionByAuthorizationCodeResponse (class)

- public string Raw;


## WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidRequest()

- WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidRequest Openid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidRequest ServiceId(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidRequest Appid(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidRequest Reason(string fieldValue)

- WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3PayScorePayScoreApisApiTerminatePermissionByOpenidResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3ProductCouponProductCouponApisApi(WechatPayV3Client client)

- async WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponResponse CreateProductCouponAsync(WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons; C# 参数：ProductCouponCreateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateProductCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponResponse UpdateProductCouponAsync(WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponRequest request)
  - PATCH /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, ProductCouponModifyRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateProductCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponResponse ModifyProductCouponAsync(WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponRequest request)
  - PATCH /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, ProductCouponModifyRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifyProductCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiQueryProductCouponResponse QueryProductCouponAsync(WechatPayV3ProductCouponProductCouponApisApiQueryProductCouponRequest request)
  - GET /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string brandId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryProductCouponRawAsync(string path, string query)

- async WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponResponse DeactivateProductCouponAsync(WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, ProductCouponDeactivateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DeactivateProductCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiCreateStockResponse CreateStockAsync(WechatPayV3ProductCouponProductCouponApisApiCreateStockRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, ProductCouponStockCreateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateStockRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleResponse CreateStockBundleAsync(WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, ProductCouponStockBundleCreateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateStockBundleRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiQueryStocksResponse QueryStocksAsync(WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest request)
  - GET /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, ProductCouponStockListQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryStocksRawAsync(string path, string query)

- async WechatPayV3ProductCouponProductCouponApisApiQueryStockResponse QueryStockAsync(WechatPayV3ProductCouponProductCouponApisApiQueryStockRequest request)
  - GET /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockId, string brandId, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryStockRawAsync(string path, string query)

- async WechatPayV3ProductCouponProductCouponApisApiUpdateStockResponse UpdateStockAsync(WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest request)
  - PATCH /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockId, ProductCouponStockModifyRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateStockRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiModifyStockResponse ModifyStockAsync(WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest request)
  - PATCH /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockId, ProductCouponStockModifyRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifyStockRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleResponse ModifyStockBundleAsync(WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest request)
  - PATCH /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockBundleId, ProductCouponStockModifyRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ModifyStockBundleRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetResponse UpdateStockBudgetAsync(WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockId, ProductCouponStockBudgetRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateStockBudgetRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetResponse UpdateStockBundleBudgetAsync(WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockBundleId, ProductCouponStockBudgetRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateStockBundleBudgetRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiDeactivateStockResponse DeactivateStockAsync(WechatPayV3ProductCouponProductCouponApisApiDeactivateStockRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockId, ProductCouponDeactivateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DeactivateStockRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiAssociateStoresResponse AssociateStoresAsync(WechatPayV3ProductCouponProductCouponApisApiAssociateStoresRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockId, ProductCouponStoresRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AssociateStoresRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresResponse AssociateStockBundleStoresAsync(WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockBundleId, ProductCouponStoresRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AssociateStockBundleStoresRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresResponse QueryAssociatedStoresAsync(WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresRequest request)
  - GET /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockId, ProductCouponStoreListQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryAssociatedStoresRawAsync(string path, string query)

- async WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresResponse DisassociateStoresAsync(WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockId, ProductCouponStoresRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DisassociateStoresRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresResponse DisassociateStockBundleStoresAsync(WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockBundleId, ProductCouponStoresRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DisassociateStockBundleStoresRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesResponse UploadCouponCodesAsync(WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesRequest request)
  - POST /v3/marketing/partner/product-coupon/product-coupons/; C# 参数：string productCouponId, string stockId, ProductCouponCodeUploadRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadCouponCodesRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiSendCouponResponse SendCouponAsync(WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, ProductCouponSendRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SendCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleResponse SendCouponBundleAsync(WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, ProductCouponSendBundleRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SendCouponBundleRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiConfirmCouponResponse ConfirmCouponAsync(WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, string couponCode, ProductCouponConfirmRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ConfirmCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiPreSendCouponResponse PreSendCouponAsync(WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, ProductCouponPreSendRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse PreSendCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleResponse PreSendCouponBundleAsync(WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, ProductCouponPreSendBundleRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse PreSendCouponBundleRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiUseCouponResponse UseCouponAsync(WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, string couponCode, ProductCouponUseRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UseCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponResponse QueryUserCouponAsync(WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest request)
  - GET /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, string couponCode, ProductCouponUserCouponQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryUserCouponRawAsync(string path, string query)

- async WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsResponse QueryUserCouponsAsync(WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest request)
  - GET /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, ProductCouponUserCouponListQueryRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryUserCouponsRawAsync(string path, string query)

- async WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponResponse DeactivateUserCouponAsync(WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, string couponCode, ProductCouponUserCouponDeactivateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DeactivateUserCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleResponse DeactivateUserCouponBundleAsync(WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, string userCouponBundleId, ProductCouponUserCouponBundleDeactivateRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DeactivateUserCouponBundleRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiReturnCouponResponse ReturnCouponAsync(WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, string couponCode, ProductCouponUserCouponReturnRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ReturnCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponResponse ReturnUserCouponAsync(WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest request)
  - POST /v3/marketing/partner/product-coupon/users/; C# 参数：string openid, string couponCode, ProductCouponUserCouponReturnRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ReturnUserCouponRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiQueryNotifyConfigResponse QueryNotifyConfigAsync(WechatPayV3ProductCouponProductCouponApisApiQueryNotifyConfigRequest request)
  - GET /v3/marketing/partner/product-coupon/notify-configs; C# 参数：int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryNotifyConfigRawAsync(string path, string query)

- async WechatPayV3ProductCouponProductCouponApisApiSetNotifyConfigResponse SetNotifyConfigAsync(WechatPayV3ProductCouponProductCouponApisApiSetNotifyConfigRequest request)
  - POST /v3/marketing/partner/product-coupon/notify-configs; C# 参数：ProductCouponNotifyConfigRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse SetNotifyConfigRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskResponse CreateImageGenerationTaskAsync(WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskRequest request)
  - POST /v3/marketing/partner/product-coupon/图片-generation-tasks; C# 参数：ProductCouponImageGenerationTaskRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateImageGenerationTaskRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProductCouponProductCouponApisApiQueryImageGenerationTaskResponse QueryImageGenerationTaskAsync(WechatPayV3ProductCouponProductCouponApisApiQueryImageGenerationTaskRequest request)
  - GET /v3/marketing/partner/product-coupon/; C# 参数：string taskId, string brandId, string imageGenerationType, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryImageGenerationTaskRawAsync(string path, string query)

- async WechatPayV3ProductCouponProductCouponApisApiUploadImageResponse UploadImageAsync(WechatPayV3ProductCouponProductCouponApisApiUploadImageRequest request, WechatMultipart multipart)
  - POST /v3/marketing/partner/product-coupon/media/upload-图片; C# 参数：string fileName, Stream fileStream, int timeOut = Config.TIME_OUT

- async WechatRawResponse UploadImageRawAsync(string path, string query, WechatMultipart multipart)


## WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresRequest()

- WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresRequest StoreList(List<WechatPayProductCouponStoreInfo> fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresRequest StockBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiAssociateStockBundleStoresResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiAssociateStoresRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiAssociateStoresRequest()

- WechatPayV3ProductCouponProductCouponApisApiAssociateStoresRequest StoreList(List<WechatPayProductCouponStoreInfo> fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiAssociateStoresRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiAssociateStoresRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiAssociateStoresRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiAssociateStoresRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiAssociateStoresResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest CouponCode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiConfirmCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiConfirmCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskRequest()

- WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskRequest TaskId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskRequest ImageGenerationType(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskRequest CombineImage(WechatPayProductCouponCombineImageRequestData fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskRequest CutOut(WechatPayProductCouponCutOutRequestData fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiCreateImageGenerationTaskResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest (class)

ProductCoupon/ProductCouponApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest Scope(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest Type(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest UsageMode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest SingleUsageInfo(WechatPayProductCouponSingleUsageInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest ProgressiveBundleUsageInfo(WechatPayProductCouponProgressiveBundleUsageInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest DisplayInfo(WechatPayProductCouponDisplayInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest OutProductNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest Stock(WechatPayProductCouponStockRequestData fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest StockBundle(WechatPayProductCouponStockBundleCreateInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiCreateProductCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleRequest()

- WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleRequest StockBundle(WechatPayProductCouponStockBundleCreateInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiCreateStockBundleResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiCreateStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiCreateStockRequest()

- WechatPayV3ProductCouponProductCouponApisApiCreateStockRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateStockRequest Stock(WechatPayProductCouponStockRequestData fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateStockRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateStockRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiCreateStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiCreateStockResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponRequest DeactivateReason(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiDeactivateProductCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiDeactivateStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiDeactivateStockRequest()

- WechatPayV3ProductCouponProductCouponApisApiDeactivateStockRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateStockRequest DeactivateReason(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateStockRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateStockRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateStockRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiDeactivateStockResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest()

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest StockBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest DeactivateReason(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest UserCouponBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponBundleResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest DeactivateReason(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest CouponCode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiDeactivateUserCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresRequest()

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresRequest StoreList(List<WechatPayProductCouponStoreInfo> fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresRequest StockBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiDisassociateStockBundleStoresResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresRequest()

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresRequest StoreList(List<WechatPayProductCouponStoreInfo> fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiDisassociateStoresResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponRequest DisplayInfo(WechatPayProductCouponDisplayInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiModifyProductCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest()

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest Remark(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest UsageRuleDisplayInfo(WechatPayProductCouponUsageRuleDisplayInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest CouponDisplayInfo(WechatPayProductCouponCouponDisplayInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest NotifyConfig(WechatPayProductCouponStockNotifyConfig fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest StoreScope(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest StockBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiModifyStockBundleResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest()

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest Remark(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest UsageRuleDisplayInfo(WechatPayProductCouponUsageRuleDisplayInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest CouponDisplayInfo(WechatPayProductCouponCouponDisplayInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest NotifyConfig(WechatPayProductCouponStockNotifyConfig fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest StoreScope(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiModifyStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiModifyStockResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest()

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest StockBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest SendRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest Attach(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiPreSendCouponBundleResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest CouponCode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest SendRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest Attach(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest CouponTagInfo(WechatPayProductCouponTagInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest MemberTagInfo(WechatPayProductCouponMemberTagInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiPreSendCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiPreSendCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresRequest()

- WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresRequest PageSize(int fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresRequest PageToken(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiQueryAssociatedStoresResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiQueryImageGenerationTaskRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiQueryImageGenerationTaskRequest()

- WechatPayV3ProductCouponProductCouponApisApiQueryImageGenerationTaskRequest TaskId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryImageGenerationTaskRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryImageGenerationTaskRequest ImageGenerationType(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryImageGenerationTaskRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiQueryImageGenerationTaskResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiQueryNotifyConfigRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiQueryNotifyConfigRequest()

- WechatPayV3ProductCouponProductCouponApisApiQueryNotifyConfigRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiQueryNotifyConfigResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiQueryProductCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiQueryProductCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiQueryProductCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryProductCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryProductCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiQueryProductCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiQueryStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiQueryStockRequest()

- WechatPayV3ProductCouponProductCouponApisApiQueryStockRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryStockRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryStockRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiQueryStockResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest()

- WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest State(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest PageSize(int fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest PageToken(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest StockBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryStocksRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiQueryStocksResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest CouponCode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest()

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest CouponState(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest UserCouponBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest PageSize(int fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest PageToken(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiQueryUserCouponsResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest CouponCode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiReturnCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest CouponCode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiReturnUserCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest()

- WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest StockBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest SendRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest Attach(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest CouponTagInfo(WechatPayProductCouponTagInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiSendCouponBundleResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest CouponCode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest SendRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest Attach(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest CouponTagInfo(WechatPayProductCouponTagInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest MemberTagInfo(WechatPayProductCouponMemberTagInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSendCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiSendCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiSetNotifyConfigRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiSetNotifyConfigRequest()

- WechatPayV3ProductCouponProductCouponApisApiSetNotifyConfigRequest NotifyUrl(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiSetNotifyConfigRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiSetNotifyConfigResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponRequest DisplayInfo(WechatPayProductCouponDisplayInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiUpdateProductCouponResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest()

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest UpdateMode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest CurrentMaxCount(long fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest TargetMaxCount(long fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest CurrentMaxCountPerDay(long fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest TargetMaxCountPerDay(long fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiUpdateStockBudgetResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest()

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest UpdateMode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest CurrentMaxCount(long fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest TargetMaxCount(long fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest CurrentMaxCountPerDay(long fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest TargetMaxCountPerDay(long fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest StockBundleId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiUpdateStockBundleBudgetResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest()

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest Remark(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest UsageRuleDisplayInfo(WechatPayProductCouponUsageRuleDisplayInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest CouponDisplayInfo(WechatPayProductCouponCouponDisplayInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest NotifyConfig(WechatPayProductCouponStockNotifyConfig fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest StoreScope(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUpdateStockRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiUpdateStockResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesRequest()

- WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesRequest CodeList(List<string> fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiUploadCouponCodesResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiUploadImageRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiUploadImageRequest()

- WechatPayV3ProductCouponProductCouponApisApiUploadImageRequest FileName(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUploadImageRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiUploadImageResponse (class)

- public string Raw;


## WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest()

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest UseTime(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest OutRequestNo(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest StoreId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest AssociatedOrderInfo(WechatPayProductCouponAssociatedOrderInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest AssociatedPayScoreOrderInfo(WechatPayProductCouponAssociatedPayScoreOrderInfo fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest SavedAmount(long fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest ProductCouponId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest StockId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest Appid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest BrandId(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest Openid(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest CouponCode(string fieldValue)

- WechatPayV3ProductCouponProductCouponApisApiUseCouponRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProductCouponProductCouponApisApiUseCouponResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3ProfitsharingProfitsharingApisApi(WechatPayV3Client client)

- async WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingResponse CreateProfitsharingAsync(WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest request)
  - POST /v3/{1}profitsharing/orders; C# 参数：CreateProfitsharingRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateProfitsharingRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingResponse QueryProfitsharingAsync(WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingRequest request)
  - GET /v3/profitsharing/orders/{out_order_no}?&transaction_id={transaction_id}; C# 参数：string transaction_id, string out_order_no, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryProfitsharingRawAsync(string path, string query)

- async WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingResponse ReturnProfitsharingAsync(WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest request)
  - POST /v3/{1}profitsharing/return-orders; C# 参数：ReturnProfitsharingRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse ReturnProfitsharingRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProfitsharingProfitsharingApisApiQueryReturnProfitsharingResponse QueryReturnProfitsharingAsync(WechatPayV3ProfitsharingProfitsharingApisApiQueryReturnProfitsharingRequest request)
  - GET /v3/profitsharing/return-orders/{out_return_no}?&out_order_no={out_return_no}; C# 参数：string out_return_no, string out_order_no, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryReturnProfitsharingRawAsync(string path, string query)

- async WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingResponse UnfreezeProfitsharingAsync(WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingRequest request)
  - POST /v3/profitsharing/orders/unfreeze; C# 参数：UnfreezeProfitsharingRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UnfreezeProfitsharingRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingAmountsResponse QueryProfitsharingAmountsAsync(WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingAmountsRequest request)
  - GET /v3/profitsharing/transactions/{transaction_id}/amounts; C# 参数：string transaction_id, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryProfitsharingAmountsRawAsync(string path, string query)

- async WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingConfigsResponse QueryProfitsharingConfigsAsync(WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingConfigsRequest request)
  - GET /v3/brand/profitsharing/brand-configs/{数据.brand_mchid}; C# 参数：QueryProfitsharingConfigsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryProfitsharingConfigsRawAsync(string path, string query)

- async WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingResponse FinishProfitsharingAsync(WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingRequest request)
  - POST /v3/brand/profitsharing/finish-顺序; C# 参数：FinishProfitsharingRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse FinishProfitsharingRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverResponse AddProfitsharingReceiverAsync(WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest request)
  - POST /v3/{1}profitsharing/receivers/添加; C# 参数：AddProfitsharingReceiverRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AddProfitsharingReceiverRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingResponse DeleteProfitsharingAsync(WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest request)
  - POST /v3/{1}profitsharing/receivers/删除; C# 参数：DeleteProfitsharingReceiverRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse DeleteProfitsharingRawAsync(string path, string query, string jsonBody)

- async WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingBillsResponse QueryProfitsharingBillsAsync(WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingBillsRequest request)
  - GET /v3/profitsharing/bills{UrlQueryHelper.ToParams(数据)}; C# 参数：QueryProfitsharingBillsRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryProfitsharingBillsRawAsync(string path, string query)


## WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest BrandMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest SubMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest SubAppid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest Appid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest Type(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest Account(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest Name(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest RelationType(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest CustomRelation(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiAddProfitsharingReceiverResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest (class)

Profitsharing/ProfitsharingApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest BrandMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest SubMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest SubAppid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest Appid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest TransactionId(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest OutOrderNo(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest Receivers(List<WechatPayProfitsharingCreateProfitsharingRequestDataReceiver> fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest UnfreezeUnsplit(bool fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiCreateProfitsharingResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest BrandMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest SubMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest SubAppid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest Appid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest Type(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest Account(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiDeleteProfitsharingResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingRequest SubMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingRequest TransactionId(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingRequest OutOrderNo(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingRequest Description(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiFinishProfitsharingResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingAmountsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingAmountsRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingAmountsRequest TransactionId(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingAmountsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingAmountsResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingBillsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingBillsRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingBillsRequest SubMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingBillsRequest BillDate(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingBillsRequest TarType(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingBillsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingBillsResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingConfigsRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingConfigsRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingConfigsRequest BrandMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingConfigsRequest SubMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingConfigsRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingConfigsResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingRequest TransactionId(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingRequest OutOrderNo(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiQueryProfitsharingResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiQueryReturnProfitsharingRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiQueryReturnProfitsharingRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiQueryReturnProfitsharingRequest OutReturnNo(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryReturnProfitsharingRequest OutOrderNo(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiQueryReturnProfitsharingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiQueryReturnProfitsharingResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest BrandMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest SubMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest OrderId(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest OutOrderNo(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest OutReturnNo(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest ReturnMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest Amount(int fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest Description(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiReturnProfitsharingResponse (class)

- public string Raw;


## WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingRequest()

- WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingRequest SubMchid(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingRequest TransactionId(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingRequest OutOrderNo(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingRequest Description(string fieldValue)

- WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3ProfitsharingProfitsharingApisApiUnfreezeProfitsharingResponse (class)

- public string Raw;


## WechatPayV3SmartGuideSmartGuideApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3SmartGuideSmartGuideApisApi(WechatPayV3Client client)

- async WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideResponse RegisterSmartGuideAsync(WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest request)
  - POST /v3/smartguide/guides; C# 参数：RegisterSmartGuideRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse RegisterSmartGuideRawAsync(string path, string query, string jsonBody)

- async WechatPayV3SmartGuideSmartGuideApisApiAssignSmartGuideResponse AssignSmartGuideAsync(WechatPayV3SmartGuideSmartGuideApisApiAssignSmartGuideRequest request)
  - POST /v3/smartguide/guides/{数据.guide_id}/assign; C# 参数：AssignSmartGuideRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse AssignSmartGuideRawAsync(string path, string query, string jsonBody)

- async WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideResponse QuerySmartGuideAsync(WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest request)
  - GET /v3/smartguide/guides; C# 参数：QuerySmartGuideRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse QuerySmartGuideRawAsync(string path, string query)

- async WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideResponse UpdateSmartGuideAsync(WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest request)
  - PATCH /v3/smartguide/guides/{数据.guide_id}; C# 参数：UpdateSmartGuideRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse UpdateSmartGuideRawAsync(string path, string query, string jsonBody)


## WechatPayV3SmartGuideSmartGuideApisApiAssignSmartGuideRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3SmartGuideSmartGuideApisApiAssignSmartGuideRequest()

- WechatPayV3SmartGuideSmartGuideApisApiAssignSmartGuideRequest GuideId(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiAssignSmartGuideRequest OutTradeNo(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiAssignSmartGuideRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3SmartGuideSmartGuideApisApiAssignSmartGuideResponse (class)

- public string Raw;


## WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest()

- WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest StoreId(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest Userid(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest Mobile(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest WorkId(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest Limit(int fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest Offset(int fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3SmartGuideSmartGuideApisApiQuerySmartGuideResponse (class)

- public string Raw;


## WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest (class)

SmartGuide/SmartGuideApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest()

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest CorpId(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest StoreId(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest Userid(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest Mobile(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest WorkId(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest Name(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest QrCode(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest Avatar(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest GroupQrcode(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3SmartGuideSmartGuideApisApiRegisterSmartGuideResponse (class)

- public string Raw;


## WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest()

- WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest GuideId(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest Name(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest Mobile(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest QrCode(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest Avatar(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest GroupQrcode(string fieldValue)

- WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3SmartGuideSmartGuideApisApiUpdateSmartGuideResponse (class)

- public string Raw;


## WechatPayV3TransferTransferApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3TransferTransferApisApi(WechatPayV3Client client)

- async WechatPayV3TransferTransferApisApiBatchesResponse BatchesAsync(WechatPayV3TransferTransferApisApiBatchesRequest request)
  - POST /v3/transfer/batches; C# 参数：BatchesRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse BatchesRawAsync(string path, string query, string jsonBody)


## WechatPayV3TransferTransferApisApiBatchesRequest (class)

Transfer/TransferApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3TransferTransferApisApiBatchesRequest()

- WechatPayV3TransferTransferApisApiBatchesRequest Appid(string fieldValue)

- WechatPayV3TransferTransferApisApiBatchesRequest OutBatchNo(string fieldValue)

- WechatPayV3TransferTransferApisApiBatchesRequest BatchName(string fieldValue)

- WechatPayV3TransferTransferApisApiBatchesRequest BatchRemark(string fieldValue)

- WechatPayV3TransferTransferApisApiBatchesRequest TotalAmount(int fieldValue)

- WechatPayV3TransferTransferApisApiBatchesRequest TotalNum(int fieldValue)

- WechatPayV3TransferTransferApisApiBatchesRequest TransferDetailList(List<WechatPayTransferDetailList> fieldValue)

- WechatPayV3TransferTransferApisApiBatchesRequest TransferSceneId(string fieldValue)

- WechatPayV3TransferTransferApisApiBatchesRequest NotifyUrl(string fieldValue)

- WechatPayV3TransferTransferApisApiBatchesRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3TransferTransferApisApiBatchesResponse (class)

- public string Raw;


## WechatPayV3VehicleParkingVehicleParkingApisApi (class)

- WechatPayV3Client client;

- public WechatPayV3VehicleParkingVehicleParkingApisApi(WechatPayV3Client client)

- async WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceResponse QueryServiceAsync(WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceRequest request)
  - GET /v3/vehicle/parking/services/find?appid={appid}&plate_number={plate_number}&plate_color={plate_color}&openid={openid}; C# 参数：string appid, string plate_number, string plate_color, string openid, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryServiceRawAsync(string path, string query)

- async WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingResponse CreateParkingAsync(WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest request)
  - POST /v3/vehicle/parking/parkings; C# 参数：CreateParkingRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse CreateParkingRawAsync(string path, string query, string jsonBody)

- async WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingResponse PayParkingAsync(WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest request)
  - POST /v3/vehicle/transactions/parking; C# 参数：PayParkingRequestData 数据, int timeOut = Config.TIME_OUT

- async WechatRawResponse PayParkingRawAsync(string path, string query, string jsonBody)

- async WechatPayV3VehicleParkingVehicleParkingApisApiQueryParkingResponse QueryParkingAsync(WechatPayV3VehicleParkingVehicleParkingApisApiQueryParkingRequest request)
  - GET /v3/vehicle/transactions/out-trade-no/{out_trade_no}; C# 参数：string out_trade_no, int timeOut = Config.TIME_OUT

- async WechatRawResponse QueryParkingRawAsync(string path, string query)


## WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest()

- WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest OutParkingNo(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest PlateNumber(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest PlateColor(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest NotifyUrl(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest StartTime(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest ParkingName(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest FreeDuration(int fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3VehicleParkingVehicleParkingApisApiCreateParkingResponse (class)

- public string Raw;


## WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest()

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest Appid(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest Description(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest Attach(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest OutTradeNo(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest TradeScene(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest GoodsTag(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest NotifyUrl(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest ProfitSharing(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest Amount(WechatPayVehicleParkingPayParkingRequestDataAmount fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest ParkingInfo(WechatPayVehicleParkingPayParkingRequestDataParkingInfo fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3VehicleParkingVehicleParkingApisApiPayParkingResponse (class)

- public string Raw;


## WechatPayV3VehicleParkingVehicleParkingApisApiQueryParkingRequest (class)

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3VehicleParkingVehicleParkingApisApiQueryParkingRequest()

- WechatPayV3VehicleParkingVehicleParkingApisApiQueryParkingRequest OutTradeNo(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiQueryParkingRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3VehicleParkingVehicleParkingApisApiQueryParkingResponse (class)

- public string Raw;


## WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceRequest (class)

VehicleParking/VehicleParkingApis.cs 的 Zan 微信支付强类型接口。
商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。
由 scripts/generate_wechat_tenpay_apis.py 从 Senparc 微信支付 API 生成。

- WechatTypedRequest request;

- string overridePath;

- public WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceRequest()

- WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceRequest Appid(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceRequest PlateNumber(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceRequest PlateColor(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceRequest Openid(string fieldValue)

- WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceRequest RequestPath(string fieldValue)

- string Path()

- string QueryText()

- string JsonBody()


## WechatPayV3VehicleParkingVehicleParkingApisApiQueryServiceResponse (class)

- public string Raw;


## WxPayRepayData (class)

- string appid;

- string mchid;

- string nonce_str;

- string openid;
