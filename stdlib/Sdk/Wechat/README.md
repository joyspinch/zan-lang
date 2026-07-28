# Sdk.Wechat —— 微信公众号（MP）客户端

`stdlib/Sdk/Wechat` 是 [Senparc.Weixin.MP](https://github.com/JeffreySu/WeiXinMPSDK)
的 Zan 精简移植。第一期只覆盖公众号最核心的能力：

| 能力 | 对应文件 | 微信接口 |
|------|----------|----------|
| 服务器 URL 签名校验 | `CheckSignature.zan` | 接入校验 SHA-1 |
| access_token 获取与缓存 | `WechatClient.zan` | `GET /cgi-bin/token` |
| 客服文本消息 | `WechatClient.zan` | `POST /cgi-bin/message/custom/send` |
| 响应解析 / 错误抛出 | `WechatResponse.zan` / `WechatException.zan` | 通用 errcode/errmsg |

后续模块（菜单、素材、OAuth、模板消息、小程序、企业微信、支付）按同样模式追加，
**不**再复制一份 HTTP / 摘要 / JSON——一律用 `System.Net.Http.Client.HttpClient`、
`System.Security.Cryptography.*`、`System.Json.*`。

## 快速开始

```csharp
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

失败一律抛 `WechatException`：

```csharp
try {
    await client.GetAccessTokenAsync();
} catch (WechatException e) {
    Console.WriteLine(e.Code + " / " + e.Message);
    Console.WriteLine(e.Body);
}
```

服务器 URL 接入校验：

```csharp
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
- **不自造** HTTP / SHA-1 / AES / JSON——直接 `using` 标准库。

## 测试

离线一致性用例：`tests/conformance/sdk_wechat.zan`（签名、响应解析、错误路径，
不打网关）。
