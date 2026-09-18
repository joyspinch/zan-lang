# 微信其他产品线迁移覆盖

本页记录企业微信 Work、小程序 WxOpen、开放平台 Open 和微信支付 TenPay 的迁移状态。
普通 JSON API 由 `scripts/generate_wechat_product_apis.py` 生成；微信支付由
`scripts/generate_wechat_tenpay_apis.py` 生成。特殊传输统一复用
`WechatApiTransport`、`WechatMultipart`、`WechatRawResponse`、标准库 HTTP/TLS/WebSocket
以及 `System.Security.Cryptography`，没有复制 C# 的 HTTP、JSON 或密码学实现。

## 汇总

| 产品线 | 普通生成模块 / 方法 | 特殊接口 | 状态 |
|---|---:|---:|---|
| 企业微信 Work | 34 / 249 | 7 个 HTTP 特殊接口 + 智能机器人 WSS | 已迁移 |
| 小程序 WxOpen | 58 / 421 | 18 | 已迁移 |
| 开放平台 Open | 24 / 147 | 12 | 已迁移（含 5 个 obsolete 兼容入口） |
| 微信支付 TenPay | 45 / 344 | 14 个同步旧接口的异步封装 + 支付核心 | 已迁移 |

TenPay 的 344 个生成方法对应 C# 源码中 384 个公开异步声明；同一源文件内的
重载折叠为一个 Zan 方法。另补齐原 SDK 只有同步版本的红包和旧版分账网络接口。
商户号、密钥、私钥、证书、通知地址、服务商子商户信息进入配置对象；订单、退款、
营销等逐笔业务数据进入方法专属 Request。

普通接口统一为强类型签名：

```zan
WechatWorkAppApiGetAppInfoResponse result = await api.GetAppInfoAsync(
    new WechatWorkAppApiGetAppInfoRequest().AgentId(1000002));
```

Request setter 自动映射 path/query/body，Response 包含强类型字段、嵌套对象和列表。
可复用 DTO 不跟随方法重复生成，而是按 C# 命名空间、外层类型链和类型名去重，分别位于
`Models/Work`（516）、`Models/WxOpen`（525）、`Models/Open`（190）和
`Models/TenPay`（631），命名空间为 `Sdk.Wechat.Models.*`。客户端依据
`WechatCredentialKind` 自动补充自身管理的 token，调用方不传 token 名称或拼接 token query。
每个生成方法保留显式 `*RawAsync(...)` 逃生口，用于协议新增字段尚未进入 vendored C# 快照的场景。

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

### Work 特殊方法（已迁移）

| C# 来源 | 方法 | 迁移方式 |
|---|---|---|
| `Media/MediaApi.cs` | `UploadAsync` | `WechatMultipart` 二进制安全上传 |
| `Media/MediaApi.cs` | `GetAsync` | `WechatRawResponse` 二进制响应 |
| `Media/MediaApi.cs` | `AddMaterialAsync` | `WechatMultipart` 二进制安全上传 |
| `Media/MediaApi.cs` | `GetForeverMaterialAsync` | `WechatRawResponse` 二进制响应 |
| `Media/MediaApi.cs` | `UploadimgMediaAsync` | `WechatMultipart` 二进制安全上传 |
| `Media/MediaAttachmentApi.cs` | `UploadAttachmentAsync` | `WechatMultipart` 二进制安全上传 |
| `Webhook/WebhookApi.cs` | `UploadMediaAsync` | `WechatMultipart` 二进制安全上传 |

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

### WxOpen 特殊方法（已迁移）

| C# 来源 | 方法 | 迁移方式 |
|---|---|---|
| `Custom/CustomApi.cs` | `SendTextAsync` | 动态路径由特殊 API 选择 |
| `Custom/CustomApi.cs` | `GetTypingStatusAsync` | 动态路径由特殊 API 选择 |
| `CV/VisualProcessingApi.cs` | `AiCropAsync` | `WechatMultipart` 二进制安全上传 |
| `CV/VisualProcessingApi.cs` | `AiCropByFileAsync` | `WechatMultipart` 二进制安全上传 |
| `CV/VisualProcessingApi.cs` | `QrCodeAsync` | `WechatMultipart` 二进制安全上传 |
| `CV/VisualProcessingApi.cs` | `QrCodeByFileAsync` | `WechatMultipart` 二进制安全上传 |
| `CV/VisualProcessingApi.cs` | `SuperResolutionAsync` | `WechatMultipart` 二进制安全上传 |
| `CV/VisualProcessingApi.cs` | `SuperResolutionByFileAsync` | `WechatMultipart` 二进制安全上传 |
| `CV/VisualProcessingApi.cs` | `DrivingLicenseAsync` | `WechatMultipart` 二进制安全上传 |
| `CV/VisualProcessingApi.cs` | `DrivingLicenseByFileAsync` | `WechatMultipart` 二进制安全上传 |
| `Delivery/DeliveryProviderApi.cs` | `GetBillAsync` | `WechatRawResponse` 二进制响应 |
| `MiniDrama/MiniDramaApi.cs` | `SingleFileUploadAsync` | `WechatMultipart` 二进制安全上传 |
| `MiniDrama/MiniDramaApi.cs` | `UploadPartAsync` | `WechatMultipart` 二进制安全上传 |
| `Operation/OperationApi.cs` | `GetFeedbackMediaAsync` | `WechatRawResponse` 二进制响应 |
| `WxApp/WxAppApi.cs` | `GetWxaCodeAsync` | `WechatRawResponse` 二进制响应 |
| `WxApp/WxAppApi.cs` | `GetWxaCodeUnlimitAsync` | `WechatRawResponse` 二进制响应 |
| `WxApp/WxAppApi.cs` | `CreateWxQrCodeAsync` | `WechatRawResponse` 二进制响应 |
| `WxApp/WxAppApi.cs` | `ImgSecCheckAsync` | `WechatMultipart` 二进制安全上传 |

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

### Open 特殊方法（已迁移）

| C# 来源 | 方法 | 迁移方式 |
|---|---|---|
| `ComponentAPIs/ComponentApi.cs` | `UploadPrivacyExtFileAsync` | `WechatMultipart` 二进制安全上传 |
| `WxaAPIs/Code/CodeApi.cs` | `GetQRCodeAsync` | `WechatRawResponse` 二进制响应 |
| `WxaAPIs/Code/CodeApi.cs` | `UploadMediaAsync` | `WechatMultipart` 二进制安全上传 |
| `WxaAPIs/Icp/IcpApi.cs` | `UploadIcpMediaAsync` | `WechatMultipart` 二进制安全上传 |
| `WxaAPIs/P1/P1Api.cs` | `GetIcpMediaAsync` | `WechatRawResponse` 二进制响应 |
| `WxaAPIs/Sec/SecApi.cs` | `UploadAuthMaterialAsync` | `WechatMultipart` 二进制安全上传 |
| `WxaAPIs/Template/TemplateApi.cs` | `LibraryListAsync` | 兼容入口直接调用实际模板端点 |
| `WxaAPIs/Template/TemplateApi.cs` | `LibraryGetAsync` | 兼容入口直接调用实际模板端点 |
| `WxaAPIs/Template/TemplateApi.cs` | `AddAsync` | 兼容入口直接调用实际模板端点 |
| `WxaAPIs/Template/TemplateApi.cs` | `ListAsync` | 兼容入口直接调用实际模板端点 |
| `WxaAPIs/Template/TemplateApi.cs` | `DelAsync` | 兼容入口直接调用实际模板端点 |
| `WxaAPIs/WxaApi.cs` | `UploadMediaAsync` | `WechatMultipart` 二进制安全上传 |

## TenPay 完整迁移

### API 表面

- `WechatPayV3*Api.zan`：41 个现代 API v3 模块；
- `WechatPayLegacyTenPayV3Api` / `WechatPayLegacyTenpayV3PayBankApi`：旧版 XML 支付；
- `WechatPayLegacyTenPayApi` / `WechatPayLegacyTenPayRightsApi`：早期公众号支付和维权；
- 共 45 个生成模块、344 个按源文件去重的方法，覆盖 384 个公开异步声明；
- 631 个支付实体按原 C# 类型身份集中到 `Models/TenPay/WechatPayModels.zan`，多个支付接口共享同一实体；
- 20 个 multipart、7 个下载/二进制接口、GET/POST/PATCH/PUT/DELETE 均保留；
- 固定端点和模板变量从 Request 字段自动生成，普通调用不传 path；仅两个由微信账单响应返回的签名下载 URL 使用 `RequestPath(downloadUrl)`。

### 只有同步版本的旧接口

`WechatPayLegacyRedPackApi` 补齐普通红包、裂变红包、小程序红包、服务商红包、摇一摇红包、
企业微信红包及查询；`WechatPayLegacyProfitSharingApi` 补齐单次/多次分账、完结、接收方增删和查询，
共 14 个网络入口，统一提供异步 Zan 方法。

### 商户配置与安全能力

| Zan 类型 | 能力 |
|---|---|
| `WechatPayV3Config` | AppId/AppSecret、商户号、API Key、商户私钥/序列号、通知地址、服务商子商户、平台公钥 |
| `WechatPayV2Config` | AppId/AppSecret、商户号、V2 Key、通知地址、签名算法、PEM 客户端证书和私钥 |
| `WechatPayLegacyMpConfig` | 早期公众号支付 AppId/AppSecret 与短期 access_token |
| `WechatPayConfigRegistry` | 按名称保存多商户 V2/V3 配置 |
| `WechatPayV3Client` | WECHATPAY2 RSA-SHA256、响应/通知验签、平台证书轮换、AES-GCM、RSA-OAEP、JSAPI/App 签名 |
| `WechatPayV2Client` | XML 字典序签名、MD5/HMAC-SHA256、mTLS、退款通知 AES-ECB 解密 |
| `WechatPayNotification` | 原始通知、通知 ID、平台/品牌验签和资源解密 |
| `WechatPayPlatformKeyStore` | 按序列号缓存多个平台证书/公钥，支持轮换 |

`RsaKey` 复用标准库 RSA，支持 PKCS#1、PKCS#8、SubjectPublicKeyInfo 和 X.509
证书 PEM；mTLS 复用 `HttpClient` / `TlsContext`，不会在 SDK 中另写 native shim。

### 生成与审计

```powershell
python scripts/generate_wechat_tenpay_apis.py
```

生成清单写入 `_scratch/wechat_tenpay_generated_manifest.json`，当前结果为
`modules=45 methods=344 dynamic=163 multipart=20`。生成器只重写带生成标记的文件，
支付核心、配置、通知和手写特殊接口不会被覆盖。
