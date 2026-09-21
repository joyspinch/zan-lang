# Zan.Sdk.Jd - 京东开放平台（JOS）官方 SDK

`Zan.Sdk.Jd` 是京东官方 `jos-net-open-api-sdk-2.0`（C#）的 100% 强类型 Zan 版本。

覆盖全部 212 个 Domain 实体、211 个 Request、211 个 Response 和 44 个核心 API 模块（商品、订单、物流仓储、售后、营销、结算等）。

---

## 快速开始

```zan
using System;
using Sdk.Jd;
using Sdk.Jd.Api.Ware;

class Program {
    static async void Main() {
        JdClient client = new JdClient("your-app-key", "your-app-secret");
        client.SetAccessToken("oauth-access-token");
        JdWareApi wareApi = new JdWareApi(client);

        WareReadSearchWare4ValidRequest request = new WareReadSearchWare4ValidRequest();
        request.SearchKey("钢笔").PageNo(1).PageSize(50);

        try {
            WareReadSearchWare4ValidResponse result = await wareApi.ReadSearchWare4ValidAsync(request);
            Console.WriteLine("商品总数: " + result.page.totalItem.ToString());
        } catch (JdException e) {
            Console.WriteLine("调用失败: " + e.Code + " / " + e.Message);
        }
    }
}
```

更多 API 与模块细节请查阅：[`src/Sdk/Jd/README.md`](src/Sdk/Jd/README.md)。
