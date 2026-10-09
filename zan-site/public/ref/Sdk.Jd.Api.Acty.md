# Sdk.Jd.Api.Acty

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Acty/ActyQueryRegistrationDataCountRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Acty/JdActyApi.zan`


## ActyQueryRegistrationDataCountRequest (class)

- JdRequest req;

- public ActyQueryRegistrationDataCountRequest()

- ActyQueryRegistrationDataCountRequest SkuId(long skuId)

- ActyQueryRegistrationDataCountRequest OrderId(long orderId)

- ActyQueryRegistrationDataCountRequest BeginDate(string beginDate)

- ActyQueryRegistrationDataCountRequest EndDate(string endDate)

- JdRequest Raw()


## ActyQueryRegistrationDataCountResponse (class)

- public ActyResult queryregistrationdatacount_result;

- public string Raw;


## JdActyApi (class)

- JdClient client;

- public JdActyApi(JdClient client)

- async ActyQueryRegistrationDataCountResponse QueryRegistrationDataCountAsync(ActyQueryRegistrationDataCountRequest request)
