# 微信其他产品线迁移覆盖

本页记录企业微信 Work、小程序 WxOpen、开放平台 Open 和微信支付 TenPay 的迁移状态。
普通 JSON API 由 `scripts/generate_wechat_product_apis.py` 从仓库内固定的 C# SDK 快照生成，统一复用 `WechatApiTransport`、`HttpClient`、`WechatResponse` 和 `System.Json`。

## 汇总

| 产品线 | Zan 模块 | 已映射普通 JSON 方法 | 明确跳过 | 状态 |
|---|---:|---:|---:|---|
| 企业微信 Work | 34 | 249 | 7 | 普通 JSON 已迁移；WebSocket 机器人另有 5 个特殊方法 |
| 小程序 WxOpen | 58 | 421 | 18 | 普通 JSON 已迁移 |
| 开放平台 Open | 24 | 147 | 12 | 普通 JSON 已迁移 |
| 微信支付 TenPay | 0 | 0 | 357 个唯一异步方法 | 全部依赖商户签名、证书、APIv3 密钥、XML 或加解密，按“特殊接口稍后迁移”暂缓 |

统一签名：

```zan
// GET / DELETE
WechatResponse result = await api.MethodAsync(query);

// POST / PUT / PATCH
WechatResponse result = await api.MethodAsync(query, jsonBody);
```

`query` 负责原 C# 强类型参数对应的 query 字段；产品客户端自动补充自身管理的 token。返回业务字段继续通过 `WechatResponse` / `System.Json` 读取。

## Work 模块

| Zan 类 | C# 来源 | JSON 方法数 |
|---|---|---:|
| `WechatWorkAppApi` | `App/AppApi.cs` | 3 |
| `WechatWorkAsynchronousApi` | `Asynchronous/AsynchronousApi.cs` | 5 |
| `WechatWorkCalendarApi` | `Calendar/CalendarApi.cs` | 4 |
| `WechatWorkChatApi` | `Chat/ChatApi.cs` | 11 |
| `WechatWorkConcernApi` | `Concern/ConcernApi.cs` | 1 |
| `WechatWorkContactP1Api` | `Contact/ContactP1Api.cs` | 1 |
| `WechatWorkCorpgroupApi` | `Corpgroup/CorpgroupApi.cs` | 19 |
| `WechatWorkCustomerAcquisitionApi` | `CustomerAcquisition/CustomerAcquisitionApi.cs` | 5 |
| `WechatWorkCustomerTagApi` | `CustomerTag/CustomerTagApi.cs` | 5 |
| `WechatWorkDataIntelligenceApi` | `DataIntelligence/DataIntelligenceApi.cs` | 2 |
| `WechatWorkExternalApi` | `External/ExternalApi.cs` | 40 |
| `WechatWorkIdConvertApi` | `IdConvert/IdConvertApi.cs` | 1 |
| `WechatWorkInvoiceApi` | `Invoice/InvoiceApi.cs` | 4 |
| `WechatWorkKFApi` | `KF/KFApi.cs` | 5 |
| `WechatWorkLinkedCorpApi` | `LinkedCorp/LinkedCorpApi.cs` | 5 |
| `WechatWorkLivingApi` | `Living/LivingApi.cs` | 9 |
| `WechatWorkLoginAuthApi` | `LoginAuth/LoginAuthApi.cs` | 1 |
| `WechatWorkMailListApi` | `MailList/MailListApi.cs` | 25 |
| `WechatWorkMailListCurrentApi` | `MailList/MailListCurrentApi.cs` | 2 |
| `WechatWorkMassLinkerCorpApi` | `Mass/LinkerCorp/LinkerCorpApi.cs` | 9 |
| `WechatWorkMassApi` | `Mass/MassApi.cs` | 15 |
| `WechatWorkMediaApi` | `Media/MediaApi.cs` | 6 |
| `WechatWorkMiniProgramMiniApi` | `MiniProgram/MiniApi.cs` | 3 |
| `WechatWorkMobileApi` | `Mobile/MobileApi.cs` | 2 |
| `WechatWorkOaApi` | `OA/OaApi.cs` | 10 |
| `WechatWorkOaDataOpenCheckinP2Api` | `OaDataOpen/OaDataOpenApi.CheckinP2.cs` | 11 |
| `WechatWorkOaDataOpenApi` | `OaDataOpen/OaDataOpenApi.cs` | 7 |
| `WechatWorkOAuth2Api` | `OAuth2/OAuth2Api.cs` | 2 |
| `WechatWorkScheduleApi` | `Schedule/ScheduleApi.cs` | 8 |
| `WechatWorkShakeAroundApi` | `ShakeAround/ShakeAroundApi.cs` | 1 |
| `WechatWorkSsoApi` | `SSO/SsoApi.cs` | 2 |
| `WechatWorkThirdPartyAuthApi` | `ThirdPartyAuth/ThirdPartyAuthApi.cs` | 15 |
| `WechatWorkWebhookApi` | `Webhook/WebhookApi.cs` | 7 |
| `WechatWorkWorkBenchApi` | `WorkBench/WorkBenchApi.cs` | 3 |

### Work 暂缓方法

| C# 来源 | 方法 | 原因 |
|---|---|---|
| `Media/MediaApi.cs` | `UploadAsync` | multipart / 专用传输 |
| `Media/MediaApi.cs` | `GetAsync` | 流或二进制响应 |
| `Media/MediaApi.cs` | `AddMaterialAsync` | multipart / 专用传输 |
| `Media/MediaApi.cs` | `GetForeverMaterialAsync` | 流或二进制响应 |
| `Media/MediaApi.cs` | `UploadimgMediaAsync` | multipart / 专用传输 |
| `Media/MediaAttachmentApi.cs` | `UploadAttachmentAsync` | multipart / 专用传输 |
| `Webhook/WebhookApi.cs` | `UploadMediaAsync` | multipart / 专用传输 |

## WxOpen 模块

| Zan 类 | C# 来源 | JSON 方法数 |
|---|---|---:|
| `WechatWxOpenB2BApi` | `B2B/B2BApi.cs` | 6 |
| `WechatWxOpenB2BMerchantApi` | `B2B/B2BMerchantApi.cs` | 9 |
| `WechatWxOpenB2BOrderApi` | `B2B/B2BOrderApi.cs` | 10 |
| `WechatWxOpenB2BProfitSharingApi` | `B2B/B2BProfitSharingApi.cs` | 9 |
| `WechatWxOpenChargeApi` | `Charge/ChargeApi.cs` | 2 |
| `WechatWxOpenCityServiceApi` | `CityService/CityServiceApi.cs` | 11 |
| `WechatWxOpenCustomApi` | `Custom/CustomApi.cs` | 3 |
| `WechatWxOpenCustomServiceCustomerServiceBusinessApi` | `CustomService/CustomerServiceBusinessApi.cs` | 4 |
| `WechatWxOpenCustomServiceApi` | `CustomService/CustomServiceApi.cs` | 6 |
| `WechatWxOpenCustomServiceKfWorkApi` | `CustomService/KfWorkApi.cs` | 3 |
| `WechatWxOpenDataCubeApi` | `DataCube/DataCubeApi.cs` | 11 |
| `WechatWxOpenDeliveryApi` | `Delivery/DeliveryApi.cs` | 12 |
| `WechatWxOpenDeliveryProviderApi` | `Delivery/DeliveryProviderApi.cs` | 9 |
| `WechatWxOpenExpressApi` | `Express/ExpressApi.cs` | 14 |
| `WechatWxOpenFaceApi` | `Face/FaceApi.cs` | 2 |
| `WechatWxOpenHardwareDeviceApi` | `HardwareDevice/HardwareDeviceApi.cs` | 1 |
| `WechatWxOpenImmediateDeliveryApi` | `ImmediateDelivery/ImmediateDeliveryApi.cs` | 14 |
| `WechatWxOpenImmediateDeliveryProviderApi` | `ImmediateDelivery/ImmediateDeliveryProviderApi.cs` | 1 |
| `WechatWxOpenLiveBroadcastApi` | `LiveBroadcast/LiveBroadcastApi.cs` | 26 |
| `WechatWxOpenLiveBroadcastGoodsApi` | `LiveBroadcast/LiveBroadcastGoodsApi.cs` | 7 |
| `WechatWxOpenLiveBroadcastRoleSubscriptionApi` | `LiveBroadcast/LiveBroadcastRoleSubscriptionApi.cs` | 5 |
| `WechatWxOpenMessageApi` | `Message/MessageApi.cs` | 2 |
| `WechatWxOpenMessageServiceCardApi` | `Message/ServiceCardApi.cs` | 3 |
| `WechatWxOpenMessageUpdatableMessageApi` | `Message/UpdatableMessageApi.cs` | 3 |
| `WechatWxOpenMiniDramaApi` | `MiniDrama/MiniDramaApi.cs` | 8 |
| `WechatWxOpenMiniDramaAuditApi` | `MiniDrama/MiniDramaAuditApi.cs` | 10 |
| `WechatWxOpenMiniDramaAuthorizationApi` | `MiniDrama/MiniDramaAuthorizationApi.cs` | 11 |
| `WechatWxOpenMiniDramaPlayerApi` | `MiniDrama/MiniDramaPlayerApi.cs` | 9 |
| `WechatWxOpenNovelApi` | `Novel/NovelApi.cs` | 14 |
| `WechatWxOpenNovelAuthorizationApi` | `Novel/NovelAuthorizationApi.cs` | 6 |
| `WechatWxOpenNovelReaderApi` | `Novel/NovelReaderApi.cs` | 3 |
| `WechatWxOpenOperationApi` | `Operation/OperationApi.cs` | 8 |
| `WechatWxOpenQrCodeJumpApi` | `QrCodeJump/QrCodeJumpApi.cs` | 5 |
| `WechatWxOpenRedPacketCoverApi` | `RedPacketCover/RedPacketCoverApi.cs` | 1 |
| `WechatWxOpenSecOrderApi` | `Sec/Order.cs` | 8 |
| `WechatWxOpenSecOrderIncrementApi` | `Sec/OrderIncrementApi.cs` | 4 |
| `WechatWxOpenServiceMarketApi` | `ServiceMarket/ServiceMarketApi.cs` | 2 |
| `WechatWxOpenSnsApi` | `Sns/SnsApi.cs` | 1 |
| `WechatWxOpenSoterApi` | `Soter/SoterApi.cs` | 1 |
| `WechatWxOpenStudentApi` | `Student/StudentApi.cs` | 1 |
| `WechatWxOpenTcbApi` | `Tcb/TcbApi.cs` | 18 |
| `WechatWxOpenTcbIncrementApi` | `Tcb/TcbIncrementApi.cs` | 1 |
| `WechatWxOpenTemplateApi` | `Template/TemplateApi.cs` | 7 |
| `WechatWxOpenTransactionGuaranteeApi` | `TransactionGuarantee/TransactionGuaranteeApi.cs` | 16 |
| `WechatWxOpenWeixinExpressApi` | `WeixinExpress/WeixinExpressApi.cs` | 7 |
| `WechatWxOpenWeixinExpressInsuranceApi` | `WeixinExpress/WeixinExpressInsuranceApi.cs` | 11 |
| `WechatWxOpenWeixinExpressIntracityApi` | `WeixinExpress/WeixinExpressIntracityApi.cs` | 16 |
| `WechatWxOpenWeixinExpressProviderApi` | `WeixinExpress/WeixinExpressProviderApi.cs` | 2 |
| `WechatWxOpenWeixinExpressReturnApi` | `WeixinExpress/WeixinExpressReturnApi.cs` | 3 |
| `WechatWxOpenWxAppBusinessApi` | `WxApp/Business/BusinessApi.cs` | 6 |
| `WechatWxOpenWxAppGenerateSchemeApi` | `WxApp/GenerateSchemeApi.cs` | 1 |
| `WechatWxOpenWxAppSearchApi` | `WxApp/SearchApi.cs` | 1 |
| `WechatWxOpenWxAppShortLinkApi` | `WxApp/ShortLinkApi.cs` | 1 |
| `WechatWxOpenWxAppUrlLinkApi` | `WxApp/UrlLinkApi.cs` | 2 |
| `WechatWxOpenWxAppUrlSchemeApi` | `WxApp/UrlScheme/UrlSchemeApi.cs` | 3 |
| `WechatWxOpenWxAppApi` | `WxApp/WxAppApi.cs` | 23 |
| `WechatWxOpenXPayApi` | `XPay/XPayApi.cs` | 35 |
| `WechatWxOpenXPayIncrementApi` | `XPay/XPayIncrementApi.cs` | 3 |

### WxOpen 暂缓方法

| C# 来源 | 方法 | 原因 |
|---|---|---|
| `Custom/CustomApi.cs` | `SendTextAsync` | 一个方法按参数切换多个端点 |
| `Custom/CustomApi.cs` | `GetTypingStatusAsync` | 一个方法按参数切换多个端点 |
| `CV/VisualProcessingApi.cs` | `AiCropAsync` | multipart / 专用传输 |
| `CV/VisualProcessingApi.cs` | `AiCropByFileAsync` | multipart / 专用传输 |
| `CV/VisualProcessingApi.cs` | `QrCodeAsync` | multipart / 专用传输 |
| `CV/VisualProcessingApi.cs` | `QrCodeByFileAsync` | multipart / 专用传输 |
| `CV/VisualProcessingApi.cs` | `SuperResolutionAsync` | multipart / 专用传输 |
| `CV/VisualProcessingApi.cs` | `SuperResolutionByFileAsync` | multipart / 专用传输 |
| `CV/VisualProcessingApi.cs` | `DrivingLicenseAsync` | multipart / 专用传输 |
| `CV/VisualProcessingApi.cs` | `DrivingLicenseByFileAsync` | multipart / 专用传输 |
| `Delivery/DeliveryProviderApi.cs` | `GetBillAsync` | 流或二进制响应 |
| `MiniDrama/MiniDramaApi.cs` | `SingleFileUploadAsync` | multipart / 专用传输 |
| `MiniDrama/MiniDramaApi.cs` | `UploadPartAsync` | multipart / 专用传输 |
| `Operation/OperationApi.cs` | `GetFeedbackMediaAsync` | 流或二进制响应 |
| `WxApp/WxAppApi.cs` | `GetWxaCodeAsync` | 流或二进制响应 |
| `WxApp/WxAppApi.cs` | `GetWxaCodeUnlimitAsync` | 流或二进制响应 |
| `WxApp/WxAppApi.cs` | `CreateWxQrCodeAsync` | 流或二进制响应 |
| `WxApp/WxAppApi.cs` | `ImgSecCheckAsync` | multipart / 专用传输 |

## Open 模块

| Zan 类 | C# 来源 | JSON 方法数 |
|---|---|---:|
| `WechatOpenAccountApi` | `AccountAPIs/AccountApi.cs` | 5 |
| `WechatOpenComponentApi` | `ComponentAPIs/ComponentApi.cs` | 21 |
| `WechatOpenComponentCurrentApi` | `ComponentAPIs/ComponentApi.Current.cs` | 2 |
| `WechatOpenComponentOpenApi` | `ComponentAPIs/ComponentOpenApi.cs` | 2 |
| `WechatOpenOpenApi` | `MpAPIs/Open/OpenApi.cs` | 4 |
| `WechatOpenOAuthApi` | `OAuthAPIs/OAuthAPI.cs` | 3 |
| `WechatOpenQRConnectApi` | `QRConnect/QRConnectAPI.cs` | 4 |
| `WechatOpenCodeApi` | `WxaAPIs/Code/CodeApi.cs` | 17 |
| `WechatOpenCodeTemplateApi` | `WxaAPIs/CodeTemplate/CodeTemplateApi.cs` | 4 |
| `WechatOpenDomainApi` | `WxaAPIs/Domain/DomainApi.cs` | 9 |
| `WechatOpenIcpApi` | `WxaAPIs/Icp/IcpApi.cs` | 12 |
| `WechatOpenModifyDomainApi` | `WxaAPIs/ModifyDomain/ModifyDomainApi.cs` | 1 |
| `WechatOpenNewTmplApi` | `WxaAPIs/NewTmpl/NewTmplApi.cs` | 6 |
| `WechatOpenNickNameApi` | `WxaAPIs/NickName/NickNameApi.cs` | 3 |
| `WechatOpenP1Api` | `WxaAPIs/P1/P1Api.cs` | 11 |
| `WechatOpenSearchStatusApi` | `WxaAPIs/SearchStatus/SearchStatusApi.cs` | 2 |
| `WechatOpenSecApi` | `WxaAPIs/Sec/SecApi.cs` | 4 |
| `WechatOpenSecOrderApi` | `WxaAPIs/SecOrder/SecOrderApi.cs` | 8 |
| `WechatOpenSetWebViewDomainApi` | `WxaAPIs/SetWebViewDomain/SetWebViewDomainApi.cs` | 1 |
| `WechatOpenSnsApi` | `WxaAPIs/Sns/SnsApi.cs` | 1 |
| `WechatOpenWxaApi` | `WxaAPIs/WxaApi.cs` | 10 |
| `WechatOpenWxaEmbeddedApi` | `WxaAPIs/WxaEmbedded/WxaEmbeddedApi.cs` | 6 |
| `WechatOpenWxOpenManagedOfficialAccountApi` | `WxOpenAPIs/ManagedOfficialAccountApi.cs` | 5 |
| `WechatOpenWxOpenApi` | `WxOpenAPIs/WxOpenApi.cs` | 6 |

### Open 暂缓方法

| C# 来源 | 方法 | 原因 |
|---|---|---|
| `ComponentAPIs/ComponentApi.cs` | `UploadPrivacyExtFileAsync` | multipart / 专用传输 |
| `WxaAPIs/Code/CodeApi.cs` | `GetQRCodeAsync` | 流或二进制响应 |
| `WxaAPIs/Code/CodeApi.cs` | `UploadMediaAsync` | multipart / 专用传输 |
| `WxaAPIs/Icp/IcpApi.cs` | `UploadIcpMediaAsync` | multipart / 专用传输 |
| `WxaAPIs/P1/P1Api.cs` | `GetIcpMediaAsync` | 流或二进制响应 |
| `WxaAPIs/Sec/SecApi.cs` | `UploadAuthMaterialAsync` | multipart / 专用传输 |
| `WxaAPIs/Template/TemplateApi.cs` | `LibraryListAsync` | C# 已废弃，实际接口位于 WxOpen 模块 |
| `WxaAPIs/Template/TemplateApi.cs` | `LibraryGetAsync` | C# 已废弃，实际接口位于 WxOpen 模块 |
| `WxaAPIs/Template/TemplateApi.cs` | `AddAsync` | C# 已废弃，实际接口位于 WxOpen 模块 |
| `WxaAPIs/Template/TemplateApi.cs` | `ListAsync` | C# 已废弃，实际接口位于 WxOpen 模块 |
| `WxaAPIs/Template/TemplateApi.cs` | `DelAsync` | C# 已废弃，实际接口位于 WxOpen 模块 |
| `WxaAPIs/WxaApi.cs` | `UploadMediaAsync` | multipart / 专用传输 |

## TenPay 为什么本轮不生成

TenPay 源码中扫描到 405 个公开异步声明、357 个按文件去重的方法名。它们的 HTTP body 虽然部分是 JSON，但请求不能只靠普通 HTTP 客户端调用，普遍要求：

- 商户私钥签名和 `Authorization: WECHATPAY2-SHA256-RSA2048`；
- 平台证书、公钥轮换和响应验签；
- APIv3 Key / AES-GCM 敏感字段加解密；
- V2 XML 签名、退款证书和双向 TLS；
- 图片、视频、资质文件 multipart 上传。

因此这一批不能复用普通 token JSON 包装。后续应先建设共享的支付签名、证书和 multipart 基础层，再迁移 TenPay，避免生成“能编译但无法安全支付”的假接口。
