# Zan.Sdk.OceanEngine

巨量引擎中国商业开放平台全量强类型 SDK，按业务平台拆分独立命名空间与客户端，避免跨平台同路径契约冲突。

- **巨量营销 (`Sdk.OceanEngine.Marketing`)**：`OceanEngineMarketingClient` + 166 个强类型业务模块（564 个接口）
- **巨量千川 (`Sdk.OceanEngine.Qianchuan`)**：`OceanEngineQianchuanClient` + 63 个强类型业务模块（181 个接口）
- **巨量本地推 (`Sdk.OceanEngine.LocalPush`)**：`OceanEngineLocalPushClient` + 41 个强类型业务模块（123 个接口）
- **巨量星图 (`Sdk.OceanEngine.StarMap`)**：`OceanEngineStarMapClient` + 12 个强类型业务模块（24 个接口）
- **群峰服务市场 (`Sdk.OceanEngine.ServeMarket`)**：`OceanEngineServeMarketClient` + 3 个强类型业务模块（3 个接口）
- **SPI 消息回调 (`Sdk.OceanEngine.Spi`)**：`OceanEngineSpi` 验签/Challenge 工具 + 25 个强类型事件模型

底层共享 `Sdk.OceanEngine` 传输层（`OceanEngineTransport`、`OceanEngineResponse`、`OceanEngineException`、`OceanEngineMultipart`、`OceanEngineJson`），依赖 `Zan.Net`、`Zan.Json` 与 `Zan.Security`。每次调用通过客户端派生（`WithAccessToken` / `WithAppAccessToken`）或单次调用传入对应令牌，令牌存储与刷新调度由接入方负责。

## 接入示例

```csharp
using System;
using System.Json;
using Sdk.OceanEngine;
using Sdk.OceanEngine.Marketing;
using Sdk.OceanEngine.Marketing.Api.Project;
using Sdk.OceanEngine.Marketing.Models.Project;

class Example {
    public static async MarketingProjectListResponse LoadProjectsAsync(string accessToken, long advertiserId) {
        OceanEngineMarketingClient client = new OceanEngineMarketingClient()
            .WithAccessToken(accessToken)
            .MaxRetries(2);
        OceanEngineMarketingProjectApi projectApi = new OceanEngineMarketingProjectApi(client);
        List<string> fields = new List<string>();
        fields.Add("name");
        MarketingProjectListRequest req = new MarketingProjectListRequest()
            .AdvertiserId(advertiserId)
            .Fields(fields)
            .Page(1L)
            .PageSize(20L);
        return await projectApi.ListAsync(req);
    }
}
```

## 设计与契约保证

- **跨平台同路径隔离**：如 `/open_api/2/file/image/ad/` 在营销中走 `api.oceanengine.com`（`OceanEngineMarketingFileImageApi`），在千川中走 `ad.oceanengine.com`（`OceanEngineQianchuanFileApi`）；两套模型与网关完全隔离，互不冲突。
- **64 位整数零精度损失**：`OceanEngineJson` 与生成的 `*Response.FromResponse` / `FromJson` 直接通过 `JsonValue.Long` / `AsLong` 读取 `int64` 字段，构造参数使用 `JsonValue.PutInt` / `AppendInt`，不经由 `double` 截断 19 位 ID。
- **字节安全 Multipart 上传**：文件上传使用 `byte[]` 构造请求体并调用 `HttpClient.SendBytesAsync`，当请求包含 `*_signature` 字段且未手工传入时自动计算原始字节的小写 MD5。
- **多租户派生与限流退避**：各平台客户端支持 `WithAccessToken` / `WithAppAccessToken` 派生独立会话（共享连接策略而不互相污染），支持 `MaxRetries(n)` 对 HTTP `429` / `503` 自动指数退避。
- **Raw 兜底**：每个模块方法均同时提供强类型 `*Async` 与原始 `*RawAsync`；各平台客户端另提供 `CallAsync` 通用兜底入口。
- **错误模型**：`OceanEngineException` 提供 `Code`、`StatusCode` 与 `RequestId`。HTTP 非 2xx 为 `http.<状态码>`，传输失败为 `client.networkError`，无效 JSON 或缺失 `code` 为 `client.badResponse`，非零业务码保留平台原始 `code` 与消息。默认不跟随重定向，本地明文测试需配置 `ExternalCallPolicy.Default().AllowLocalHttp()`。

## 契约重新生成

从巨量引擎契约库重新生成全量平台模块与 SPI 模型：

```bash
python3 scripts/generate_oceanengine_sdk.py
```

## 验证

在 Zan 仓库根目录运行本地仿真网关与全模块编译验证（无需真实巨量账号或凭证）：

```powershell
pwsh -NoProfile -File scripts/test.ps1 -Match '^case_sdk_oceanengine$'
pwsh -NoProfile -File scripts/build_gate.ps1 -Command '& ./build/zanc.exe tests/conformance/sdk_oceanengine_modules.zan --auto-stdlib --check-leaks -o build/cases/case_sdk_oceanengine_modules.exe; if ($LASTEXITCODE -eq 0) { & ./build/cases/case_sdk_oceanengine_modules.exe }'
```
