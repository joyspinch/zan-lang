# Sdk.Wechat —— 微信 SDK

`stdlib/Sdk/Wechat` 是 [Senparc 微信 SDK](https://github.com/JeffreySu/WeiXinMPSDK)
的 Zan 原生移植。实现复用 Zan 标准库的 HTTP/TLS/WebSocket、JSON 和密码学能力，
不复制 C# 版的 ASP.NET、DI、缓存容器或底层网络实现。

## 覆盖范围

| 产品线 | 覆盖 |
|---|---|
| 通用公众号能力 | URL 签名、token 缓存、客服消息、菜单、用户、二维码、模板消息、OAuth、消息 XML、安全模式加解密 |
| MP Advanced API | 37 个模块、383 个强类型普通接口 |
| 企业微信 Work | 34 个模块、249 个强类型普通接口、7 个特殊 HTTP 接口、智能机器人 WSS |
| 小程序 WxOpen | 58 个模块、421 个强类型普通接口、18 个特殊接口 |
| 开放平台 Open | 24 个模块、147 个强类型普通接口、12 个特殊接口 |
| 微信支付 TenPay | 45 个生成模块、344 个强类型异步接口、14 个同步旧接口的异步封装及支付安全核心 |

MP 清单见 `Mp/COVERAGE.md`；Work、WxOpen、Open 与 TenPay 清单见
`PRODUCT_COVERAGE.md`。

## 凭据与 `access_token`

调用方只配置 `appId/appSecret`，或通过客户端的 `Set*Token()` 注入已经取得的 token。
业务 API 不接收 `"access_token"` 字符串，也不要求调用方拼接 token query。
SDK 内部使用 `WechatCredentialKind` 描述接口需要哪一种凭据，并由客户端自动取缓存、刷新和
追加微信协议要求的 query 名。协议字段名集中在 `WechatCredential.QueryName()` 一处。

```zan
WechatClient client = new WechatClient("your-app-id", "your-app-secret");
string token = await client.GetAccessTokenAsync(); // 实例内缓存
```

## 强类型请求、响应与共享实体

接口层只保留方法专属的 Request/Response **契约**：Request 负责把字段放到正确的
path/query/body，Response 负责保留原始响应并继承实际返回实体。真正的业务 DTO 不按方法复制，
而是按 C# 的“命名空间 + 外层类型链 + 类型名”识别并复用，独立存放在：

| 产品线 | 共享实体命名空间 | 目录 | 实体数 |
|---|---|---|---:|
| MP | `Sdk.Wechat.Models.Mp` | `Models/Mp/` | 484 |
| Work | `Sdk.Wechat.Models.Work` | `Models/Work/` | 516 |
| WxOpen | `Sdk.Wechat.Models.WxOpen` | `Models/WxOpen/` | 525 |
| Open | `Sdk.Wechat.Models.Open` | `Models/Open/` | 190 |
| TenPay | `Sdk.Wechat.Models.TenPay` | `Models/TenPay/` | 631 |

因此同一个 C# 实体只生成一个 Zan 类；即使它被多个接口、多个请求字段或多个响应引用，
IDE/LSP 看到的也始终是同一类型。嵌套同名类则按其 C# 外层类型区分，不会把支付、退款等
语义不同的 `Amount` 错误合并。

```zan
using Sdk.Wechat;
using Sdk.Wechat.Mp;
using Sdk.Wechat.Models.Mp;

WechatClient client = new WechatClient("app-id", "app-secret");
WechatMpUserApi users = new WechatMpUserApi(client);
WechatMpUserApiInfoResponse user = await users.InfoAsync(
    new WechatMpUserApiInfoRequest().OpenId("openid").Lang(null));
Console.WriteLine(user.nickname);

// Cell 是共享实体；新增和更新礼品卡页面复用同一个类型。
WechatMpCell cell = new WechatMpCell();
cell.title = "领取礼品卡";
cell.url = "pages/gift/index";
WechatMpCardApiAddGiftCardPageRequest addPage =
    new WechatMpCardApiAddGiftCardPageRequest().Cell1(cell);
WechatMpCardApiUpdateGiftCardPageRequest updatePage =
    new WechatMpCardApiUpdateGiftCardPageRequest().Cell1(cell);
```

只有 C# 模型本身就是动态字典或任意 JSON 的字段才保留 `JsonValue`。每个普通方法另外保留
显式命名的 `*RawAsync(...)` 作为协议刚新增字段时的高级逃生口；正常业务代码不需要自行
处理 query、JSON 请求体或 `WechatResponse.Data`。

## Work、WxOpen、Open

```zan
using Sdk.Wechat.Work;
using Sdk.Wechat.WxOpen;
using Sdk.Wechat.Open;

WechatWorkClient work = new WechatWorkClient("corp-id", "corp-secret");
WechatWorkAppApi workApp = new WechatWorkAppApi(work);
WechatWorkAppApiGetAppInfoResponse appInfo = await workApp.GetAppInfoAsync(
    new WechatWorkAppApiGetAppInfoRequest().AgentId(1000002));
Console.WriteLine(appInfo.name);

WechatWxOpenClient mini = new WechatWxOpenClient("app-id", "app-secret");
WechatWxOpenDataCubeApi cube = new WechatWxOpenDataCubeApi(mini);
WechatWxOpenDataCubeApiGetWeAnalysisAppidDailySummaryTrendResponse trend =
    await cube.GetWeAnalysisAppidDailySummaryTrendAsync(
        new WechatWxOpenDataCubeApiGetWeAnalysisAppidDailySummaryTrendRequest()
            .BeginDate("20260727")
            .EndDate("20260727"));

WechatOpenClient open = new WechatOpenClient();
open.SetComponentAccessToken(componentToken);
WechatOpenComponentApi component = new WechatOpenComponentApi(open);
WechatOpenComponentApiGetPreAuthCodeResponse auth = await component.GetPreAuthCodeAsync(
    new WechatOpenComponentApiGetPreAuthCodeRequest()
        .ComponentAppId("component-app-id"));
Console.WriteLine(auth.pre_auth_code);
```

## 特殊传输接口

上传、下载、二维码和账单等接口复用 `WechatMultipart` 与 `WechatRawResponse`。JSON 参数仍用
强类型请求；二进制响应保留 `WechatRawResponse`，避免把图片、PDF、CSV 当 JSON 解码。

```zan
WechatWorkMediaSpecialApi media = new WechatWorkMediaSpecialApi(work);
WechatResponse uploaded = await media.UploadAsync(
    "image", "logo.png", "image/png", fileBytes);

WechatWxOpenSpecialApi miniSpecial = new WechatWxOpenSpecialApi(mini);
WechatWxOpenSpecialWxaCodeRequest codeRequest = new WechatWxOpenSpecialWxaCodeRequest()
    .Path("pages/index/index")
    .Width(430)
    .IsHyaline(false);
WechatRawResponse code = await miniSpecial.GetWxaCodeAsync(codeRequest);
code.SaveBody("mini-code.png");
```

企业微信智能机器人使用标准库 TLS WebSocket：

```zan
WechatWorkSmartRobotClient robot = new WechatWorkSmartRobotClient();
await robot.ConnectAndSubscribeAsync(botId, botSecret);
string message = await robot.ReceiveAsync();
await robot.RespondAsync(requestId, replyJson);
```

## 微信支付

商户号、密钥、私钥、证书、通知地址和服务商子商户信息属于长期部署配置，放在
`WechatPayV3Config` / `WechatPayV2Config`。每笔订单、退款、营销等数据放在对应的强类型
Request，不再传裸 JSON/XML，也不把业务数据误塞进配置对象。

```zan
using System.IO;
using Sdk.Wechat.TenPay;
using Sdk.Wechat.Models.TenPay;

WechatPayV3Config payConfig = new WechatPayV3Config(
    "wx-app-id", "merchant-id", "merchant-serial",
    File.ReadAllText("apiclient_key.pem"),
    "0123456789abcdef0123456789abcdef");
payConfig.SetNotifyUrl("https://merchant/pay-notify")
         .SetPlatformPublicKey("wechatpay-public-key-id",
             File.ReadAllText("wechatpay_public_key.pem"));

WechatPayV3Client pay = new WechatPayV3Client(payConfig);
WechatPayV3BasePayBasePayApisApi basePay =
    new WechatPayV3BasePayBasePayApisApi(pay);

WechatPayBasePayTransactionsRequestDataAmount amount =
    new WechatPayBasePayTransactionsRequestDataAmount();
amount.total = 100;
WechatPayBasePayTransactionsRequestDataPayer payer =
    new WechatPayBasePayTransactionsRequestDataPayer();
payer.openid = "payer-openid";

WechatPayV3BasePayBasePayApisApiJsApiRequest orderRequest =
    new WechatPayV3BasePayBasePayApisApiJsApiRequest()
        .Appid("wx-app-id")
        .Mchid("merchant-id")
        .Description("Zan SDK order")
        .OutTradeNo("ORDER-20260728-001")
        .NotifyUrl("https://merchant/pay-notify")
        .Amount(amount)
        .Payer(payer);
WechatPayV3BasePayBasePayApisApiJsApiResponse order =
    await basePay.JsApiAsync(orderRequest);
Console.WriteLine(order.prepay_id);
```

V2/XML 使用 `WechatPayV2Config`；生成的 V2 Request 会构造、补全配置字段并签名，生成的
Response 会把 XML 映射为字段。证书接口自动走 mTLS。API v3 负责 WECHATPAY2 RSA-SHA256、
平台响应/通知验签、证书轮换、AES-GCM、RSA-OAEP 与支付调起签名。

## 错误处理

微信 JSON 错误统一抛 `WechatException`；原始下载可检查 `WechatRawResponse.StatusCode`：

```zan
try {
    await client.GetAccessTokenAsync();
} catch (WechatException e) {
    Console.WriteLine(e.Code + " / " + e.Message);
    Console.WriteLine(e.Body);
}
```

## 设计取舍

- 一个客户端实例管理一个应用/企业/开放平台凭据及 token 缓存；
- 不复制 C# 的 DI、MVC、中间件、Redis/Memcached 抽象；跨进程 token 可由调用方注入；
- 不自造 HTTP、TLS、WebSocket、SHA、AES、RSA、JSON，直接复用标准库；
- 业务实体按 C# 类型身份集中在 `Sdk.Wechat.Models.*`，不按接口方法重复生成；
- 方法专属 Request/Response 只是传输契约，普通接口默认强类型，`*RawAsync` 只用于微信新增字段尚未进入快照时的兼容；
- 二进制接口返回 `WechatRawResponse`，不会错误地二次 JSON 解析；
- 失败即抛，不返回带错误码但看似成功的空业务对象。

## 测试

离线一致性用例：

- `tests/conformance/sdk_wechat.zan`：签名、token/JSON 响应、错误路径和基础请求体；
- `tests/conformance/sdk_wechat_crypt.zan`：官方密文向量、加解密往返、消息 XML、OAuth URL；
- `tests/conformance/sdk_wechat_mp_modules.zan`：37 个 MP 模块、383 个强类型方法编译面；
- `tests/conformance/sdk_wechat_product_modules.zan`：Work/WxOpen/Open 共 116 个模块、817 个强类型方法编译面；
- `tests/conformance/sdk_wechat_special_modules.zan`：特殊 HTTP 接口与企业微信智能机器人 WSS 编译面；
- `tests/conformance/sdk_wechat_tenpay.zan`：45 个 TenPay 模块、344 个强类型方法及支付密码学/配置/通知；
- `tests/conformance/json_entity_mapping.zan`：生成 DTO 所需的对象、列表和嵌套列表 JSON 映射。

测试不访问微信网关，可在 conformance / determinism / leakcheck 模式运行。
