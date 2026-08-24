# Sdk.Jd.Api.GetPurchaseInfo

> 源码: `stdlib/Sdk/Jd/Api/GetPurchaseInfo/GetPurchaseInfoRequest.zan`, `stdlib/Sdk/Jd/Api/GetPurchaseInfo/JdGetPurchaseInfoApi.zan`


## GetPurchaseInfoRequest (class)

<c>jingdong.getPurchaseInfo</c> 的请求。

- JdRequest req;

- public GetPurchaseInfoRequest()

- JdRequest Raw()
  - 底层协议请求。


## GetPurchaseInfoResponse (class)

<c>jingdong.getPurchaseInfo</c> 的响应。

- public JmServiceResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdGetPurchaseInfoApi (class)

jingdong.getPurchaseInfo.* 的强类型客户端。

- JdClient client;

- public JdGetPurchaseInfoApi(JdClient client)

- async GetPurchaseInfoResponse Async(GetPurchaseInfoRequest request)
  - 执行 <c>jingdong.getPurchaseInfo</c>。
