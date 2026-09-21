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

---

## 快速上手

```zan
using System;
using Sdk.Wechat;
using Sdk.Wechat.Mp.Custom;

class Program {
    static async void Main() {
        // 1. 初始化客户端（配置凭据，内部自动处理 access_token 缓存与刷新）
        WechatClient client = new WechatClient("wx_your_app_id", "your_app_secret");

        // 2. 发送客服文本消息
        CustomApi customApi = new CustomApi(client);
        SendTextCustomRequest req = new SendTextCustomRequest();
        req.ToUser("OPENID_123456").Content("欢迎体验 Zan 微信 SDK！");

        CustomResponse resp = await customApi.SendTextAsync(req);
        Console.WriteLine("发送结果: " + resp.ErrorCode.ToString() + " / " + resp.ErrorMessage);
    }
}
```

各产品线全量接口清单与架构细则请查阅：
- 📘 详细开发手册：[`src/Sdk/Wechat/README.md`](src/Sdk/Wechat/README.md)
- 📊 全产品线覆盖表：[`src/Sdk/Wechat/PRODUCT_COVERAGE.md`](src/Sdk/Wechat/PRODUCT_COVERAGE.md)
- 📗 公众号高级接口清单：[`src/Sdk/Wechat/Mp/COVERAGE.md`](src/Sdk/Wechat/Mp/COVERAGE.md)
