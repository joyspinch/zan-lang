# Sdk.Jd.Api.Fw

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Fw/FwMarketPaymentoutRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Fw/JdFwApi.zan`


## FwMarketPaymentoutRequest (class)

- JdRequest req;

- public FwMarketPaymentoutRequest()

- FwMarketPaymentoutRequest RequestNo(string requestNo)

- FwMarketPaymentoutRequest ActivityId(long activityId)

- FwMarketPaymentoutRequest AppId(string appId)

- FwMarketPaymentoutRequest Price(long price)

- FwMarketPaymentoutRequest IsMainService(bool isMainService)

- FwMarketPaymentoutRequest ServiceCycle(int serviceCycle)

- FwMarketPaymentoutRequest SkuId(long skuId)

- FwMarketPaymentoutRequest ServiceCode(string serviceCode)

- FwMarketPaymentoutRequest OrderNum(int orderNum)

- FwMarketPaymentoutRequest ItemCode(string itemCode)

- FwMarketPaymentoutRequest OutOrderId(long outOrderId)

- FwMarketPaymentoutRequest Value1(JsonValue value1)

- FwMarketPaymentoutRequest ResultPageType(int resultPageType)

- FwMarketPaymentoutRequest SuccessUrl(string successUrl)

- FwMarketPaymentoutRequest Ip(string ip)

- JdRequest Raw()


## FwMarketPaymentoutResponse (class)

- public JmServiceResult returnType;

- public string Raw;


## JdFwApi (class)

- JdClient client;

- public JdFwApi(JdClient client)

- async FwMarketPaymentoutResponse MarketPaymentoutAsync(FwMarketPaymentoutRequest request)
