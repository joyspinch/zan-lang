# examples/sdk — 第三方 SDK 示例

本目录提供可独立编译的真实 SDK 调用示例。示例不会把密钥写进源码，运行时从命令行参数读取。

## 京东 JOS

`jd_open_api.zan` 演示：

- `JdClient` 配置 appKey、appSecret 和 OAuth access token；
- `JdAreaApi` 模块客户端与强类型 Request/Response；
- SDK 自动完成参数 JSON、时间戳、签名、HTTP 请求、响应信封解包和实体反序列化；
- `JdException` 错误处理。

```powershell
build/zanc.exe examples/sdk/jd_open_api.zan --auto-stdlib -o build/jd_open_api.exe
build/jd_open_api.exe <appKey> <appSecret> <accessToken|->
```

## 微信公众号

`wechat_mp.zan` 演示：

- `WechatClient` 自动获取和缓存 access token；
- `WechatMpUserApiInfoRequest` 强类型请求；
- `WechatMpUserApiInfoResponse` 及共享用户实体字段；
- `WechatException` 错误处理。

```powershell
build/zanc.exe examples/sdk/wechat_mp.zan --auto-stdlib -o build/wechat_mp.exe
build/wechat_mp.exe <appId> <appSecret> <openId>
```

两个示例都会访问正式网关，请使用有效的测试应用凭据，不要把密钥提交到仓库。
