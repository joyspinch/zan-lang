# Sdk.Jd.Api.Acty

> 源码: `stdlib/Sdk/Jd/Api/Acty/ActyQueryRegistrationDataCountRequest.zan`, `stdlib/Sdk/Jd/Api/Acty/JdActyApi.zan`


## ActyQueryRegistrationDataCountRequest (class)

<c>jingdong.acty.queryRegistrationDataCount</c> 的请求。

- JdRequest req;

- public ActyQueryRegistrationDataCountRequest()

- ActyQueryRegistrationDataCountRequest SkuId(long skuId)
  - 设置 <c>skuId</c> 参数。

- ActyQueryRegistrationDataCountRequest OrderId(long orderId)
  - 设置 <c>orderId</c> 参数。

- ActyQueryRegistrationDataCountRequest BeginDate(string beginDate)
  - 设置 <c>beginDate</c> 参数。

- ActyQueryRegistrationDataCountRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ActyQueryRegistrationDataCountResponse (class)

<c>jingdong.acty.queryRegistrationDataCount</c> 的响应。

- public ActyResult queryregistrationdatacount_result;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## JdActyApi (class)

jingdong.acty.* 的强类型客户端。

- JdClient client;

- public JdActyApi(JdClient client)

- async ActyQueryRegistrationDataCountResponse QueryRegistrationDataCountAsync(ActyQueryRegistrationDataCountRequest request)
  - 执行 <c>jingdong.acty.queryRegistrationDataCount</c>。
