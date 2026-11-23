# Sdk.Jushuitan —— 聚水潭开放平台客户端

`Zan.Sdk.Jushuitan` 是聚水潭开放平台（POP）的 Zan SDK，覆盖 20 个常规业务分类（177 个业务接口强类型 Request/Response 契约）、3 种授权令牌流程与新版消息推送回调验签及强类型载荷。

## 快速开始

按实际使用的业务域引入对应子命名空间：

```zan
using System;
using Sdk.Jushuitan;
using Sdk.Jushuitan.Api.Order;

class Program {
    static async void Main() {
        string appKey = Environment.GetEnvironmentVariable("JST_APP_KEY");
        string appSecret = Environment.GetEnvironmentVariable("JST_APP_SECRET");
        string accessToken = Environment.GetEnvironmentVariable("JST_ACCESS_TOKEN");

        JushuitanClient client = new JushuitanClient(appKey, appSecret)
            .SetAccessToken(accessToken);

        OrderApi orderApi = new OrderApi(client);
        QueryOrdersRequest req = new QueryOrdersRequest()
            .StartTs(1)
            .IsGetTotal(false)
            .PageIndex(1)
            .PageSize(50);

        QueryOrdersResponse resp = await orderApi.QueryOrdersAsync(req);
        Console.WriteLine("orders=" + Convert.ToString(resp.orders.Count));
    }
}
```

仅 `using` 你实际使用的模块（如 `Sdk.Jushuitan.Api.Order`），编译器不会把其余 19 个模块拉入编译闭包。

## 多租户与链式配置

服务商或管理多店铺的场景无需重复重建客户端，可用 `WithAccessToken` 派生轻量会话，或在模块方法上直接传显式 `accessToken`：

```zan
JushuitanClient baseClient = new JushuitanClient(appKey, appSecret)
    .Timeout(15000)      // 请求超时，默认 30000ms
    .MaxRetries(2);      // 遇到 199/200 限流时的退避重试次数，默认 2（0 关闭）

// 方式 A：为不同商家派生独立会话
JushuitanClient tenantA = baseClient.WithAccessToken(tokenA);
OrderApi orderA = new OrderApi(tenantA);
QueryOrdersResponse resA = await orderA.QueryOrdersAsync(req);

// 方式 B：复用同一个 OrderApi，显式传入商家 access_token
OrderApi sharedOrderApi = new OrderApi(baseClient);
QueryOrdersResponse resB = await sharedOrderApi.QueryOrdersAsync(tokenB, req);
```

其他链式配置与兜底调用：

- `.Server("https://...")`：指向自定义网关或本地测试服务端；
- `.UseHangzhouGateway()`：全局覆盖为杭州网关 `https://openapi-hz.jushuitan.com`（注意：`BasicApi.QueryDistributorsAsync` 等杭州接口内部已直接指向杭州网关，常规调用无需手工切换）；
- `.PlainHttpMode()` 与 `.WithCallPolicy(ExternalCallPolicy.Default().AllowLocalHttp())`：放行本地 HTTP 测试假网关；
- `client.CallAsync("/open/...", jsonValue)`：未收录接口或自定义参数原始调用逃生舱；强类型请求同样提供 `.SetRaw(key, JsonValue)`，所有强类型响应均保留 `Raw`（`JushuitanResponse`）。

## 包结构与设计特点

由 `scripts/generate_catalog.py` 从 `docs/jushuitan/api/` 确定性生成：

```text
src/Sdk/Jushuitan/                         核心客户端、签名、响应解包、令牌与授权
src/Sdk/Jushuitan/Messages/                新版消息回调验签与 4 种强类型消息载荷
src/Sdk/Jushuitan/Api/<Module>/            20 个按需模块客户端（Sdk.Jushuitan.Api.<Module>）
src/Sdk/Jushuitan/Api/<Module>/Requests/   链式 Request Builder（直接操作 JsonValue，无多余代理层）
src/Sdk/Jushuitan/Api/<Module>/Models/     强类型 Response DTO 与嵌套实体
```

- **浅层直白调用链**：`OrderApi.QueryOrdersAsync` → `JushuitanClient.CallAsync` → `PostFormWithRetryAsync`（内置限流退避重算 `timestamp`/`sign` 与 `using` 资源释放）→ `JushuitanResponse.Parse`。
- **仅序列化显式设置的字段**：未调用 setter 的可选字段不会出现在发送的 `biz` JSON 中，避免默认 `0`/`false` 触发网关参数冲突。
- **支持顶层数组 `biz`**：如 `BatchRegisterExpressShipmentsRequest.AddItem(...)` 直接构建顶层 JSON Array（`[{...}]`）。
- **统一响应信封解包**：自动适配 `{code, data}` 与非标准根/无 `code` 响应（`54/532/625/1246/1247/1414/1437/156/52/193/802/24/42` 及确认态响应）。

## 授权与续期

```zan
// 1. 服务商生成授权链接，并保存一次性 state
string authUrl = client.BuildAuthorizationUrl(state);

// 2. 回调验签（GET 参数字典）并返回 {"code":0}
string authCode = client.VerifyAuthorizationCallback(queryFields, expectedState);
string ackJson = JushuitanClient.AuthorizationCallbackSuccessResponse(); // {"code":0}
JushuitanToken token = await client.ExchangeAccessTokenAsync(authCode);

// 3. 商家自研应用：首次授权（6 位随机 code）与刷新
JushuitanToken selfToken = await client.GetInitTokenAsync("a1b2c3");
if (selfToken.IsExpiringSoon(86400)) {
    selfToken = await client.RefreshTokenAsync(selfToken.RefreshToken);
}
```

> 服务商应用自 2023-08-15 起官方已禁用 `refreshToken`，到期前需重新生成授权链接请商家确认授权。

## 消息推送接收（文档 28）

`Sdk.Jushuitan.Messages.JushuitanMessageCallback` 支持在验签后直接绑定为 4 种官方强类型消息：

- `sync_shipped` → `msg.AsShipment()`（`ShipmentPushMessage`）
- `business_sku_syn` → `msg.AsInventory()`（`InventoryPushMessage`）
- `trade_after_sale` → `msg.AsAfterSale()`（`AfterSalePushMessage`）
- `receipt_action` → `msg.AsReceipt()`（`ReceiptPushMessage`，兼顾蓝票与红票冲红字段）

```zan
using Sdk.Jushuitan.Messages;

// 可传第 4 个参数 maxAgeSeconds（如 600）开启重放时间窗校验
JushuitanMessage msg = JushuitanMessageCallback.Parse(httpRequest, appKey, appSecret, 600);
if (msg.IsSubscriptionCheck) {
    return JushuitanMessageCallback.SuccessResponse(); // {"code":"0","msg":"执行成功"}
}
if (msg.ActionCode == JushuitanMessageCallback.Shipment) {
    ShipmentPushMessage ship = msg.AsShipment();
    Console.WriteLine(ship.l_id);
}
```

## 异常与已知边界

- 所有本地校验、网络错误、HTTP 非 2xx 及网关业务错误均抛 `JushuitanException`（含 `Code`、`Message`、`StatusCode`、`Body`、`IsRateLimited`、`IsTokenExpired`）。
- **奇门开票接口（`docId=1047`）**：需经淘宝奇门网关 `http://37zf6iaq2d.api.taobao.com/router/qm?`（`method=jushuitan.shengsuan.invoiceresult.query`）调用，不走聚水潭业务网关。
- **清单独有条目（`docId=800`）**：官方公开文档接口未提供可用契约，不生成代码入口。
- **未进行真实商家线上调用**：官方测试环境已关闭且未配置真实商家密钥，当前全部由离线向量及本地 `HttpServer` Mock 网关验证。

重新生成与一致性检查：

```bash
python -I packages/Zan.Sdk.Jushuitan/scripts/generate_catalog.py <path/to/docs/jushuitan>
python -I packages/Zan.Sdk.Jushuitan/scripts/generate_catalog.py --check <path/to/docs/jushuitan>
```
