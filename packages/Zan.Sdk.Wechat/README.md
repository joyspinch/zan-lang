# Zan.Sdk.Wechat - 微信全生态官方 SDK (公众号 / 企业微信 / 小程序 / 微信支付 / 开放平台)

`Zan.Sdk.Wechat` 是面向微信全产品线的工业级 Zan 原生 SDK（基于 Senparc 深度移植，全面对接 Zan 异步 HTTP/TLS/WebSocket 与密码学库）。

---

## 产品线覆盖

| 产品线 | 模块覆盖与特性 |
|---|---|
| **微信公众号 (MP)** | 37 个业务模块，覆盖自定义菜单、客服消息、模板消息、OAuth 授权、带参二维码、素材管理等 |
| **企业微信 (Work)** | 34 个模块，覆盖通讯录同步、应用消息推送、审批、打卡、智能客服与企微机器人长连接 |
| **微信小程序 (WxOpen)** | 58 个模块，覆盖用户手机号解密、订阅消息、统一服务消息、云开发、码生成等 |
| **微信开放平台 (Open)** | 24 个模块，覆盖第三方平台代公众号/小程序发起业务、开放平台账号绑定 |
| **微信支付 (TenPay)** | 45 个模块，覆盖 Native/JSAPI/H5/App 统一下单、退款、分账、合单支付及证书加验签 |

已基于 `Zan.Net` / `Zan.Security` 原生支持 TLS 1.2 / 1.3 客户端证书认证（mTLS）、SPKI 公钥钉扎（`PinPublicKey`）及证书策略回调（`SetCertPolicy`）。通过 `WechatPayV2Config.SetClientCertificate(certPemPath, keyPemPath)` 配置 PEM 客户端证书和私钥后，`WechatPayV2Client` 会对需要双向认证的退款、撤销、转账等接口自动启用 mTLS。

## 分层目录架构

源码按职责拆分在 `src/Sdk/Wechat/` 下（对外仍保持 `Sdk.Wechat.Models.<Product>` 等原命名空间兼容）：
- `Models/<Product>/`：按业务域拆分的纯 DTO 实体与枚举（零 Request/Response 契约）
- `Requests/<Product>/`：强类型 `*Request` 传输契约（按业务域与子域拆分）
- `Responses/<Product>/`：强类型 `*Response` 响应契约（按业务域拆分）
- `<Product>/`：`*Api` 业务门面、`*Client`、`*Config` 及运行时内部配置模型

---

## 快速上手

```zan
using System;
using Sdk.Wechat;
using Sdk.Wechat.Mp;
using Sdk.Wechat.Models.Mp;

class Program {
    static async void Main() {
        // 1. 初始化客户端（内部自动缓存并提前刷新 access_token）
        WechatClient client = new WechatClient("wx_your_app_id", "your_app_secret");

        // 2. 发起强类型业务调用（失败一律抛 WechatException）
        try {
            WechatMpCustomApi customApi = new WechatMpCustomApi(client);
            WechatMpCustomApiSendTextRequest req = new WechatMpCustomApiSendTextRequest()
                .OpenId("OPENID_123456")
                .Content("欢迎体验 Zan 微信 SDK！");
            WechatMpCustomApiSendTextResponse resp = await customApi.SendTextAsync(req);
            Console.WriteLine("发送成功: " + resp.Raw);
        } catch (WechatException e) {
            Console.WriteLine("调用失败: " + e.Code + " / " + e.Message);
        }
    }
}
```

各产品线全量接口清单与架构细则请查阅：
- 📘 详细开发手册：[`src/Sdk/Wechat/README.md`](src/Sdk/Wechat/README.md)
- 📊 全产品线覆盖表：[`src/Sdk/Wechat/PRODUCT_COVERAGE.md`](src/Sdk/Wechat/PRODUCT_COVERAGE.md)
- 📗 公众号高级接口清单：[`src/Sdk/Wechat/Mp/COVERAGE.md`](src/Sdk/Wechat/Mp/COVERAGE.md)
