# Sdk.Wechat —— 微信 SDK

`stdlib/Sdk/Wechat` 是 [Senparc 微信 SDK](https://github.com/JeffreySu/WeiXinMPSDK)
的 Zan 原生移植。当前覆盖公众号 MP、企业微信 Work、小程序 WxOpen、开放平台 Open 和微信支付 TenPay：

| 能力 | 对应文件 | 微信接口 |
|------|----------|----------|
| 服务器 URL 签名校验 | `CheckSignature.zan` | 接入校验 SHA-1 |
| access_token 获取与缓存 | `WechatClient.zan` | `GET /cgi-bin/token` |
| 客服文本消息 | `WechatClient.zan` | `POST /cgi-bin/message/custom/send` |
| 菜单管理 | `WechatClient.zan` | 创建 / 查询 / 删除自定义菜单 |
| 用户与关注者列表 | `WechatClient.zan` | 用户信息 / OpenID 列表 |
| 二维码与 JSAPI ticket | `WechatClient.zan` | 临时/永久二维码、ticket |
| 模板消息 | `WechatClient.zan` | 发送模板消息 |
| 网页 OAuth | `WechatClient.zan` | 授权 URL、token、刷新、用户信息 |
| 明文消息与被动回复 XML | `Message.zan` / `XmlUtil.zan` | 文本、图片、事件、图文回复 |
| 安全模式消息加解密 | `WXBizMsgCrypt.zan` | SHA-1 + AES-256-CBC + PKCS#7 |
| 响应解析 / 错误抛出 | `WechatResponse.zan` / `WechatException.zan` | 通用 errcode/errmsg |
| MP Advanced JSON API | `Mp/WechatMp*Api.zan` | 37 个模块、383 个方法映射 |
| 企业微信 Work | `Work/WechatWork*Api.zan` | 34 个普通模块、249 个 JSON 方法、7 个特殊 HTTP 接口、智能机器人 WSS |
| 小程序 WxOpen | `WxOpen/WechatWxOpen*Api.zan` | 58 个普通模块、421 个 JSON 方法、18 个特殊接口 |
| 开放平台 Open | `Open/WechatOpen*Api.zan` | 24 个普通模块、147 个 JSON 方法、12 个特殊接口 |
| 微信支付 TenPay | `TenPay/WechatPay*.zan` | 45 个生成模块、344 个异步方法入口、14 个同步旧接口封装及支付安全核心 |

MP 模块见 `Mp/COVERAGE.md`；Work、WxOpen、Open 特殊接口和 TenPay 完整清单见
`PRODUCT_COVERAGE.md`。
**不**再复制一份 HTTP / 摘要 / JSON——一律用 `System.Net.Http.Client.HttpClient`、
`System.Security.Cryptography.*`、`System.Json.*`。

## 快速开始

```zan
using System;
using Sdk.Wechat;

class Program {
    static async void Main() {
        WechatClient client = new WechatClient("your-app-id", "your-app-secret");

        // AccessToken 实例内缓存，未过期不会重复打网关。
        string token = await client.GetAccessTokenAsync();
        Console.WriteLine(token);

        WechatResponse r = await client.SendTextAsync("openid", "hello from Zan");
        Console.WriteLine(r.Data);
    }
}
```

更多 MP Advanced API 使用 `Sdk.Wechat.Mp`：

```zan
using Sdk.Wechat;
using Sdk.Wechat.Mp;

WechatClient client = new WechatClient("your-app-id", "your-app-secret");
WechatMpDraftApi draft = new WechatMpDraftApi(client);
WechatResponse result = await draft.AddDraftAsync("", draftJson);
```

生成模块采用原始 query / JSON 请求体，避免把 C# SDK 的 ASP.NET、DI 和纯数据 POCO 整套复制到 Zan；返回字段通过 `WechatResponse` / `System.Json` 读取。

其他产品线使用各自客户端：

```zan
using Sdk.Wechat.Work;
using Sdk.Wechat.WxOpen;
using Sdk.Wechat.Open;

WechatWorkClient work = new WechatWorkClient("corp-id", "corp-secret");
WechatWorkAppApi workApp = new WechatWorkAppApi(work);
WechatResponse appInfo = await workApp.GetAppInfoAsync("agentid=1000002");

WechatWxOpenClient mini = new WechatWxOpenClient("app-id", "app-secret");
WechatWxOpenDataCubeApi cube = new WechatWxOpenDataCubeApi(mini);
WechatResponse trend = await cube.GetWeAnalysisAppidDailySummaryTrendAsync("", dateJson);

WechatOpenClient open = new WechatOpenClient();
open.SetComponentAccessToken(componentToken);
WechatOpenComponentApi component = new WechatOpenComponentApi(open);
WechatResponse auth = await component.GetPreAuthCodeAsync("", componentJson);
```

文件上传、账单/二维码下载等特殊接口使用二进制安全的 `WechatMultipart` 和
`WechatRawResponse`：

```zan
WechatWorkMediaSpecialApi media = new WechatWorkMediaSpecialApi(work);
WechatResponse uploaded = await media.UploadAsync("image", "logo.png",
    "image/png", fileBytes);

WechatWxOpenSpecialApi miniSpecial = new WechatWxOpenSpecialApi(mini);
WechatRawResponse code = await miniSpecial.GetWxaCodeAsync(codeJson);
code.SaveBody("mini-code.png");
```

企业微信智能机器人使用标准库 TLS WebSocket：

```zan
WechatWorkSmartRobotClient robot = new WechatWorkSmartRobotClient();
await robot.ConnectAndSubscribeAsync(botId, botSecret);
string message = await robot.ReceiveAsync();
await robot.RespondAsync(requestId, replyJson);
```

微信支付的长期商户数据全部进入配置对象。每笔订单、退款或营销业务数据仍以
JSON/XML 请求体传给对应 API：

```zan
using System.IO;
using Sdk.Wechat.TenPay;

WechatPayV3Config payConfig = new WechatPayV3Config(
    "wx-app-id", "merchant-id", "merchant-serial",
    File.ReadAllText("apiclient_key.pem"), "0123456789abcdef0123456789abcdef");
payConfig.SetNotifyUrls("https://merchant/pay-notify",
                        "https://merchant/mini-notify")
         .SetServiceProviderCredentials("sub-app", "sub-secret", "sub-mch")
         .SetPlatformPublicKey("wechatpay-public-key-id",
                               File.ReadAllText("wechatpay_public_key.pem"));

WechatPayV3Client pay = new WechatPayV3Client(payConfig);
WechatPayV3BasePayBasePayApisApi basePay =
    new WechatPayV3BasePayBasePayApisApi(pay);
WechatRawResponse order = await basePay.JsApiAsync(
    "/v3/pay/transactions/jsapi", "", orderJson);
```

V2/XML 和需要商户证书的接口使用 `WechatPayV2Config`；证书配置为 PEM 证书和
独立 PEM 私钥，红包、退款、旧版分账会自动走 mTLS：

```zan
WechatPayV2Config v2Config = new WechatPayV2Config(
    "wx-app-id", "merchant-id", "merchant-v2-key");
v2Config.SetClientCertificate("apiclient_cert.pem", "apiclient_key.pem")
        .SetNotifyUrl("https://merchant/v2-notify");
WechatPayV2Client v2 = new WechatPayV2Client(v2Config);
```

失败一律抛 `WechatException`：

```zan
try {
    await client.GetAccessTokenAsync();
} catch (WechatException e) {
    Console.WriteLine(e.Code + " / " + e.Message);
    Console.WriteLine(e.Body);
}
```

服务器 URL 接入校验：

```zan
if (CheckSignature.Check(signature, timestamp, nonce, "your-token")) {
    // 回 echostr
}
```

## 相对 C# 原版的取舍

- **删掉** AccessTokenContainer / DI / 中间件 / MVC 扩展——Zan 侧一个
  `WechatClient` 实例对应一个公众号，token 缓存在实例字段。
- **删掉** 对 Redis/Memcached 缓存策略的抽象——需要跨进程缓存时由调用方
  用 `SetAccessToken` 注入。
- **失败即抛**：不返回带 `errcode` 的空结果对象。
- **不自造** HTTP / TLS / WebSocket / SHA / AES / RSA / JSON——直接 `using` 标准库；
- **支付配置集中化**：商户号、密钥、私钥、证书、通知地址和服务商子商户信息只保存在配置对象；
- **原始业务载荷**：不复制数百个 C# POCO，调用方传 query/JSON/XML，返回原始或 `System.Json` 结果。

## 测试

离线一致性用例：

- `tests/conformance/sdk_wechat.zan`：签名、JSON 响应、错误路径和请求体；
- `tests/conformance/sdk_wechat_crypt.zan`：官方密文向量、加解密往返、消息 XML、OAuth URL；
- `tests/conformance/sdk_wechat_mp_modules.zan`：37 个 MP 模块自动发现、构造及代表性 GET/POST 接口编译；
- `tests/conformance/sdk_wechat_product_modules.zan`：Work/WxOpen/Open 共 116 个普通模块和 817 个方法的自动发现、构造与代表性调用编译；
- `tests/conformance/sdk_wechat_special_modules.zan`：37 个特殊 HTTP 接口及企业微信智能机器人 WSS 编译面；
- `tests/conformance/sdk_wechat_tenpay.zan`：45 个 TenPay 生成模块、V2 MD5/HMAC、V3 RSA/X.509、配置注册、通知与旧版特殊接口。

测试不访问微信网关，可在 conformance / determinism / leakcheck 三种模式运行。
