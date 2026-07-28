# 微信公众号 MP 接口覆盖清单

本目录迁移自 `WeiXinMPSDK-master/src/Senparc.Weixin.MP/.../AdvancedAPIs`。
映射代码由 `scripts/generate_wechat_mp.py` 从仓库内固定的 C# 源码快照生成。
这里的目标不是复制 C# 的 DI、MVC、反射序列化和数百个 JSON POCO，而是保留公开 API 名称与微信 HTTP 端点，统一复用 Zan 标准库 HTTP / JSON / 加密能力。

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
WechatMpDraftApi draft = new WechatMpDraftApi(client);
WechatResponse result = await draft.AddDraftAsync("", draftJson);
```

- GET：`MethodAsync(query)`；
- JSON POST：`MethodAsync(query, jsonBody)`；
- `query` 不含开头 `?`，没有 query 时传空串（传入 `?` 也会被兼容）；
- 返回值统一为 `WechatResponse`，继续使用 `System.Json.JsonValue` 读取业务字段；
- Token 缓存、HTTPS、超时、JSON 错误码处理都由 `WechatClient` 负责。

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

这些方法需要先在 `System.Net.Http.Client.HttpClient` 增加可复用的 multipart、字节响应和流式下载能力；在此之前不能用 JSON POST 冒充。

## 尚未迁移的产品线

| 产品线 | C# 文件数 |
|---|---:|
| 企业微信 `Senparc.Weixin.Work` | 608 |
| 微信支付 `Senparc.Weixin.TenPay` | 432 |
| 小程序 `Senparc.Weixin.WxOpen` | 343 |
| 开放平台 `Senparc.Weixin.Open` | 194 |
| 通用层 `Senparc.Weixin` | 81 |

因此当前准确状态是：**公众号 MP 的普通 JSON Advanced API 已大面积迁移；整个 Senparc 微信 SDK 尚未全部迁完。**
