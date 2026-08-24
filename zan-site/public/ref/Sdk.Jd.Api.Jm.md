# Sdk.Jd.Api.Jm

> 源码: `stdlib/Sdk/Jd/Api/Jm/JdJmApi.zan`, `stdlib/Sdk/Jd/Api/Jm/JmOrderGetPayUrlRequest.zan`


## JdJmApi (class)

jingdong.jm.* 的强类型客户端。

- JdClient client;

- public JdJmApi(JdClient client)

- async JmOrderGetPayUrlResponse OrderGetPayUrlAsync(JmOrderGetPayUrlRequest request)
  - 执行 <c>jingdong.jm.order.getPayUrl</c>。


## JmOrderGetPayUrlRequest (class)

<c>jingdong.jm.order.getPayUrl</c> 的请求。

- JdRequest req;

- public JmOrderGetPayUrlRequest()

- JmOrderGetPayUrlRequest ServiceCode(string serviceCode)
  - 设置 <c>serviceCode</c> 参数。

- JmOrderGetPayUrlRequest AccessCode(string accessCode)
  - 设置 <c>accessCode</c> 参数。

- JmOrderGetPayUrlRequest OrderNum(int orderNum)
  - 设置 <c>orderNum</c> 参数。

- JmOrderGetPayUrlRequest SkuId(long skuId)
  - 设置 <c>skuId</c> 参数。

- JmOrderGetPayUrlRequest ClientIp(string clientIp)
  - 设置 <c>clientIp</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## JmOrderGetPayUrlResponse (class)

<c>jingdong.jm.order.getPayUrl</c> 的响应。

- public JmServiceResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
