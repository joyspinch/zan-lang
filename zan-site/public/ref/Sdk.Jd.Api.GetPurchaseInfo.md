# Sdk.Jd.Api.GetPurchaseInfo

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/GetPurchaseInfo/GetPurchaseInfoRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/GetPurchaseInfo/JdGetPurchaseInfoApi.zan`


## GetPurchaseInfoRequest (class)

- JdRequest req;

- public GetPurchaseInfoRequest()

- JdRequest Raw()


## GetPurchaseInfoResponse (class)

- public JmServiceResult returnType;

- public string Raw;


## JdGetPurchaseInfoApi (class)

- JdClient client;

- public JdGetPurchaseInfoApi(JdClient client)

- async GetPurchaseInfoResponse Async(GetPurchaseInfoRequest request)
