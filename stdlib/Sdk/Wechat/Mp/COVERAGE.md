# 微信公众号 MP 接口覆盖清单

本目录迁移自 `WeiXinMPSDK-master/src/Senparc.Weixin.MP/.../AdvancedAPIs`。
映射代码由 `scripts/generate_wechat_mp.py` 从仓库内固定的 C# 源码快照生成。
这里保留公开 API 名称、方法专属请求/响应契约和微信 HTTP 端点；484 个可复用业务实体按原 C# 类型身份集中到 `../Models/Mp/WechatMpModels.zan`，统一复用 Zan 标准库 HTTP / JSON / 加密能力，不复制 C# 的 DI、MVC 和反射基础设施。

## 当前覆盖

- C# MP 项目：463 个 `.cs` 文件；
- Advanced API 入口：39 个 `*Api.cs`；
- 扫描到 415 个公开异步声明；去除 OAuth 的 4 个声明后有 411 个，其中 8 个是同名重载；
- 非 OAuth 的 403 个唯一异步方法名中，383 个普通 JSON API 已映射为 Zan 方法；
- 383 个方法分布在 37 个 `Sdk.Wechat.Mp` 模块类；
- 其余 20 个方法因 multipart、文件流、二进制或 CSV 特殊响应暂不伪装成 JSON 接口；
- OAuth 授权 URL、token、刷新和用户信息由上层 `WechatClient.zan` 的便捷方法提供，不重复生成。

统一调用形式：

```zan
using Sdk.Wechat;
using Sdk.Wechat.Mp;

WechatClient client = new WechatClient("app-id", "app-secret");
WechatMpUserApi users = new WechatMpUserApi(client);
WechatMpUserApiInfoResponse result = await users.InfoAsync(
    new WechatMpUserApiInfoRequest().OpenId("openid"));
Console.WriteLine(result.nickname);
```

- 每个方法生成独立 Request/Response 契约，IDE 可补全请求 setter 和响应字段；
- Request 自动区分 path/query/body，调用方不再拼 query 或 JSON；
- 业务 DTO 按 C# 命名空间、外层类型链和类型名去重，集中在 `Sdk.Wechat.Models.Mp`；
- Response 继承共享实体，嵌套对象、列表和嵌套列表也引用共享类型，动态字典才保留 `JsonValue`；
- token 缓存、HTTPS、超时、JSON 错误码处理都由 `WechatClient` 负责；
- `*RawAsync(...)` 是明确的高级兼容入口，不是普通调用方式。

## 已迁移模块

| Zan 类 | C# 来源 | JSON 方法数 |
|---|---|---:|
| `WechatMpAnalysisApi` | `Analysis/AnalysisApi.cs` | 21 |
| `WechatMpAutoReplyApi` | `AutoReply/AutoReplyApi.cs` | 1 |
| `WechatMpCardApi` | `Card/CardAPI.cs` | 79 |
| `WechatMpCardStoreApi` | `Card/Store/StoreApi.cs` | 14 |
| `WechatMpCommentApi` | `Comment/CommentApi.cs` | 8 |
| `WechatMpCustomApi` | `Custom/CustomApi.cs` | 11 |
| `WechatMpCustomServiceApi` | `CustomService/CustomServiceApi.cs` | 13 |
| `WechatMpCVImageApi` | `CV/Image/ImageApi.cs` | 1 |
| `WechatMpCVOCRApi` | `CV/OCR/OCRApi.cs` | 8 |
| `WechatMpDraftApi` | `Draft/DraftApi.cs` | 7 |
| `WechatMpDraftProductCardApi` | `Draft/ProductCardApi.cs` | 1 |
| `WechatMpFreePublishApi` | `FreePublish/FreePublishApi.cs` | 5 |
| `WechatMpGroupMessageApi` | `GroupMessage/GroupMessageApi.cs` | 11 |
| `WechatMpGroupsApi` | `Groups/GroupsApi.cs` | 7 |
| `WechatMpInvoiceApi` | `Invoice/InvoiceApi.cs` | 27 |
| `WechatMpMediaApi` | `Media/MediaApi.cs` | 11 |
| `WechatMpMedicalAssistantApi` | `MedicalAssistant/MedicalAssistantApi.cs` | 1 |
| `WechatMpMerchantExpressApi` | `MerChant/Express/ExpressApi.cs` | 5 |
| `WechatMpMerchantGroupApi` | `MerChant/Group/GroupApi.cs` | 6 |
| `WechatMpMerchantOrderApi` | `MerChant/Order/OrderApi.cs` | 4 |
| `WechatMpMerchantProductApi` | `MerChant/Product/ProductApi.cs` | 9 |
| `WechatMpMerchantShelfApi` | `MerChant/Shelf/ShelfApi.cs` | 5 |
| `WechatMpMerchantStockApi` | `MerChant/Stock/StockApi.cs` | 2 |
| `WechatMpNewTmplApi` | `NewTmpl/NewTmplApi.cs` | 7 |
| `WechatMpNonTaxPayApi` | `NonTaxPay/NonTaxPayApi.cs` | 9 |
| `WechatMpOneCodeApi` | `OneCode/OneCodeApi.cs` | 6 |
| `WechatMpPoiApi` | `Poi/PoiApi.cs` | 6 |
| `WechatMpQrCodeApi` | `QrCode/QrCodeApi.cs` | 1 |
| `WechatMpScanApi` | `Scan/ScanApi.cs` | 9 |
| `WechatMpSemanticApi` | `Semantic/SemanticApi.cs` | 1 |
| `WechatMpShakeAroundApi` | `ShakeAround/ShakeAroundApi.cs` | 32 |
| `WechatMpTemplateMessageTemplateApi` | `TemplateMessage/TemplateApi.cs` | 8 |
| `WechatMpUrlApi` | `Url/UrlApi.cs` | 3 |
| `WechatMpUserApi` | `User/UserApi.cs` | 8 |
| `WechatMpUserTagApi` | `UserTag/UserTagApi.cs` | 7 |
| `WechatMpWiFiApi` | `WiFi/WiFiApi.cs` | 18 |
| `WechatMpWxaApi` | `Wxa/WxaApi.cs` | 11 |

## 暂未映射的特殊方法

| C# 来源 | 方法 | 原因 |
|---|---|---|
| `CustomService/CustomServiceApi.cs` | `UploadCustomHeadimgAsync` | multipart / 文件流 / 二进制 |
| `CV/Image/ImageApi.cs` | `AiCropAsync` | multipart / 文件流 / 二进制 |
| `CV/Image/ImageApi.cs` | `AiCropByFileAsync` | multipart / 文件流 / 二进制 |
| `CV/Image/ImageApi.cs` | `QrCodeByFileAsync` | multipart / 文件流 / 二进制 |
| `CV/OCR/OCRApi.cs` | `DrivingLicenseByFileAsync` | multipart / 文件流 / 二进制 |
| `Invoice/InvoiceApi.cs` | `SetPdfAsync` | multipart / 文件流 / 二进制 |
| `Media/MediaApi.cs` | `UploadTemporaryMediaAsync` | multipart / 文件流 / 二进制 |
| `Media/MediaApi.cs` | `GetAsync` | multipart / 文件流 / 二进制 |
| `Media/MediaApi.cs` | `GetJssdkAsync` | multipart / 文件流 / 二进制 |
| `Media/MediaApi.cs` | `UploadForeverMediaAsync` | multipart / 文件流 / 二进制 |
| `Media/MediaApi.cs` | `UploadForeverVideoAsync` | multipart / 文件流 / 二进制 |
| `Media/MediaApi.cs` | `GetForeverMediaAsync` | multipart / 文件流 / 二进制 |
| `Media/MediaApi.cs` | `GetForeverMediaWithContentTypeAsync` | multipart / 文件流 / 二进制 |
| `Media/MediaApi.cs` | `UploadImgAsync` | multipart / 文件流 / 二进制 |
| `Media/MediaApi.cs` | `AddVoiceAsync` | multipart / 文件流 / 二进制 |
| `MerChant/Picture/PictureApi.cs` | `UploadImgAsync` | 专用上传或非 JSON 响应 |
| `NonTaxPay/NonTaxPayApi.cs` | `DownloadBillAsync` | 专用上传或非 JSON 响应 |
| `Poi/PoiApi.cs` | `UploadImageAsync` | multipart / 文件流 / 二进制 |
| `QrCode/QrCodeApi.cs` | `ShowQrCodeAsync` | multipart / 文件流 / 二进制 |
| `ShakeAround/ShakeAroundApi.cs` | `UploadImageAsync` | multipart / 文件流 / 二进制 |

这些方法属于文件路径/流对象或二进制/CSV 返回形态，不能伪装成普通 JSON 方法；对应能力由共享 `WechatMultipart`、`WechatRawResponse` 和专用接口承载。

## 其他产品线

企业微信 Work、小程序 WxOpen、开放平台 Open 和微信支付 TenPay 已迁移，详见上级目录的 `PRODUCT_COVERAGE.md`。
