# Sdk.Jd.Api.Fw

> 源码: `stdlib/Sdk/Jd/Api/Fw/FwMarketPaymentoutRequest.zan`, `stdlib/Sdk/Jd/Api/Fw/JdFwApi.zan`


## FwMarketPaymentoutRequest (class)

<c>jingdong.fw.market.paymentout</c> 的请求。

- JdRequest req;

- public FwMarketPaymentoutRequest()

- FwMarketPaymentoutRequest RequestNo(string requestNo)
  - 设置 <c>requestNo</c> 参数。

- FwMarketPaymentoutRequest ActivityId(long activityId)
  - 设置 <c>activityId</c> 参数。

- FwMarketPaymentoutRequest AppId(string appId)
  - 设置 <c>appId</c> 参数。

- FwMarketPaymentoutRequest Price(long price)
  - 设置 <c>price</c> 参数。

- FwMarketPaymentoutRequest IsMainService(bool isMainService)
  - 设置 <c>isMainService</c> 参数。

- FwMarketPaymentoutRequest ServiceCycle(int serviceCycle)
  - 设置 <c>serviceCycle</c> 参数。

- FwMarketPaymentoutRequest SkuId(long skuId)
  - 设置 <c>skuId</c> 参数。

- FwMarketPaymentoutRequest ServiceCode(string serviceCode)
  - 设置 <c>serviceCode</c> 参数。

- FwMarketPaymentoutRequest OrderNum(int orderNum)
  - 设置 <c>orderNum</c> 参数。

- FwMarketPaymentoutRequest ItemCode(string itemCode)
  - 设置 <c>itemCode</c> 参数。

- FwMarketPaymentoutRequest OutOrderId(long outOrderId)
  - 设置 <c>outOrderId</c> 参数。

- FwMarketPaymentoutRequest Value1(JsonValue value1)
  - 设置 <c>value1</c> 参数。

- FwMarketPaymentoutRequest ResultPageType(int resultPageType)
  - 设置 <c>resultPageType</c> 参数。

- FwMarketPaymentoutRequest SuccessUrl(string successUrl)
  - 设置 <c>successUrl</c> 参数。

- FwMarketPaymentoutRequest Ip(string ip)
  - 设置 <c>ip</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## FwMarketPaymentoutResponse (class)

<c>jingdong.fw.market.paymentout</c> 的响应。

- public JmServiceResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdFwApi (class)

jingdong.fw.* 的强类型客户端。

- JdClient client;

- public JdFwApi(JdClient client)

- async FwMarketPaymentoutResponse MarketPaymentoutAsync(FwMarketPaymentoutRequest request)
  - 执行 <c>jingdong.fw.market.paymentout</c>。
