# Sdk.Taobao —— 淘宝开放平台 TOP 客户端

`Zan.Sdk.Taobao` 是淘宝开放平台（TOP）的 Zan 原生异步 SDK，合并去重官方 `Topsdk`（.NET 6）与 `taobao-sdk-net-auto`（.NET 2.0），覆盖 **18 个业务域模块、392 个 TOP API** 的强类型 Request Builder 与 Response 模型，支持多租户会话派生、自动限流退避重试、文件上传与 `/router/batch` 批量网关。

## 快速开始

按实际使用的业务域引入对应子命名空间：

```zan
using System;
using Sdk.Taobao;
using Sdk.Taobao.Api.Fuwu;

class Program {
    static async void Main() {
        string appKey = Environment.GetEnvironmentVariable("TAOBAO_APP_KEY");
        string appSecret = Environment.GetEnvironmentVariable("TAOBAO_APP_SECRET");
        string session = Environment.GetEnvironmentVariable("TAOBAO_SESSION");

        TaobaoClient client = new TaobaoClient(appKey, appSecret)
            .SetSession(session);

        FuwuApi fuwuApi = new FuwuApi(client);
        TaobaoAppstoreSubscribeGetRequest req = new TaobaoAppstoreSubscribeGetRequest()
            .Nick("tb_nick")
            .LeaseId(1001L);

        TaobaoAppstoreSubscribeGetResponse resp = await fuwuApi.TaobaoAppstoreSubscribeGetAsync(req);
        if (resp.UserSubscribe != null) {
            Console.WriteLine("status=" + resp.UserSubscribe.Status);
        }
    }
}
```

仅 `using` 你实际使用的模块（如 `Sdk.Taobao.Api.Fuwu`），编译器不会把其余 17 个模块拉入编译闭包。

## 多租户、签名模式与逃生舱

服务商多店铺场景可用 `WithSession` 派生轻量会话，或在模块方法上显式传入 `session`：

```zan
TaobaoClient baseClient = new TaobaoClient(appKey, appSecret)
    .Timeout(15000)      // 请求超时，默认 30000ms
    .MaxRetries(2);      // 遇到 isp.* / 限流错误时的退避重试次数，默认 2（0 关闭）

// 方式 A：为不同授权商家派生独立会话
TaobaoClient tenantA = baseClient.WithSession(sessionA);
FuwuApi fuwuA = new FuwuApi(tenantA);
TaobaoAppstoreSubscribeGetResponse resA = await fuwuA.TaobaoAppstoreSubscribeGetAsync(req);

// 方式 B：复用同一个 Facade，显式传入商家 session
FuwuApi sharedFuwu = new FuwuApi(baseClient);
TaobaoAppstoreSubscribeGetResponse resB = await sharedFuwu.TaobaoAppstoreSubscribeGetAsync(sessionB, req);
```

其他链式配置与能力：

- `.Server(url)` / `.Sandbox()`：切换正式网关（`https://eco.taobao.com/router/rest`）、沙箱网关（`https://gw.api.tbsandbox.com/router/rest`）或本地测试网关；
- `.SignMethod(TaobaoSign.SignMethodHmacSha256 | SignMethodHmac | SignMethodMd5)`：默认 `hmac-sha256`；
- `.Simplify(true | false)`：默认 `true`（精简响应），切换 `false` 时自动兼容 `<api>_response` 包装层及 `{"item": [...]}` 列表结构；
- `.TargetAppKey(key)`：兼容老版奇门/目标 `target_app_key`；
- `.PlainHttpMode()` 与 `.WithCallPolicy(ExternalCallPolicy.Default().AllowLocalHttp())`：放行本地 HTTP 测试假网关；
- `client.CallAsync(method, params, files, session)`：未收录 API 或自定义参数原始调用逃生舱；所有请求均支持 `.SetRaw(key, val)` / `.SetRawJson(key, JsonValue)`，所有强类型响应均保留 `Raw`（`TaobaoResponse`）。

## 批量请求（Batch）与文件上传

```zan
// 文件上传（字节流或本地文件）
TaobaoFileItem img = TaobaoFileItem.FromBytes("demo.png", bytes, "image/png");
Dictionary<string, TaobaoFileItem> files = new Dictionary<string, TaobaoFileItem>();
files.Add("image", img);
TaobaoResponse upResp = await tenantA.CallAsync("taobao.picture.upload", paramsMap, files, "");

// 批量调用 /router/batch
TaobaoBatchRequest batch = new TaobaoBatchRequest()
    .Add("taobao.item.seller.get", p1, sessionA)
    .Add("taobao.item.seller.get", p2, sessionB);
TaobaoBatchResponse br = await baseClient.ExecuteBatchAsync(batch);
```

## 包结构与模块列表

由 `scripts/generate_catalog.py` 确定性生成（`python -I packages/Zan.Sdk.Taobao/scripts/generate_catalog.py --check`）：

```text
src/Sdk/Taobao/                         核心客户端、签名、响应解包、异常处理、Multipart、Batch
src/Sdk/Taobao/Api/<Module>/            18 个按需模块客户端（Sdk.Taobao.Api.<Module>）
src/Sdk/Taobao/Api/<Module>/Requests/   链式 Request Builder 与请求嵌套参数（*Param）
src/Sdk/Taobao/Api/<Module>/Models/     强类型 Response DTO 与嵌套实体
```

18 个业务模块：`Channel`、`Fenxiao`、`FenxiaoDealer`、`Fuwu`、`Inventory`、`Item`、`Miniapp`、`Promotion`、`Refund`、`Shop`、`Subuser`、`Tmc`、`UniversalbpAccount`、`UniversalbpAdgroup`、`UniversalbpCampaign`、`UniversalbpCreative`、`UniversalbpKeyword`、`UniversalbpReport`。
