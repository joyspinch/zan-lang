# Sdk.Jd.Api.Jm

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Jm/JdJmApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Jm/JmOrderGetPayUrlRequest.zan`


## JdJmApi (class)

- JdClient client;

- public JdJmApi(JdClient client)

- async JmOrderGetPayUrlResponse OrderGetPayUrlAsync(JmOrderGetPayUrlRequest request)


## JmOrderGetPayUrlRequest (class)

- JdRequest req;

- public JmOrderGetPayUrlRequest()

- JmOrderGetPayUrlRequest ServiceCode(string serviceCode)

- JmOrderGetPayUrlRequest AccessCode(string accessCode)

- JmOrderGetPayUrlRequest OrderNum(int orderNum)

- JmOrderGetPayUrlRequest SkuId(long skuId)

- JmOrderGetPayUrlRequest ClientIp(string clientIp)

- JdRequest Raw()


## JmOrderGetPayUrlResponse (class)

- public JmServiceResult returnType;

- public string Raw;
