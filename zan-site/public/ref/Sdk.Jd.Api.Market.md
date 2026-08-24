# Sdk.Jd.Api.Market

> 源码: `stdlib/Sdk/Jd/Api/Market/JdMarketApi.zan`, `stdlib/Sdk/Jd/Api/Market/MarketBdpCartGetPinsBySkuIdRequest.zan`, `stdlib/Sdk/Jd/Api/Market/MarketChargeListGetRequest.zan`, `stdlib/Sdk/Jd/Api/Market/MarketDbpCartCartDataReadServiceGetCarSkuCountRequest.zan`, `stdlib/Sdk/Jd/Api/Market/MarketServiceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Market/MarketServiceListGetRequest.zan`


## JdMarketApi (class)

jingdong.market.* 的强类型客户端。

- JdClient client;

- public JdMarketApi(JdClient client)

- async MarketBdpCartGetPinsBySkuIdResponse BdpCartGetPinsBySkuIdAsync(MarketBdpCartGetPinsBySkuIdRequest request)
  - 执行 <c>jingdong.market.bdp.cart.getPinsBySkuId</c>。

- async MarketChargeListGetResponse ChargeListGetAsync(MarketChargeListGetRequest request)
  - 执行 <c>jingdong.market.charge.list.get</c>。

- async MarketDbpCartCartDataReadServiceGetCarSkuCountResponse DbpCartCartDataReadServiceGetCarSkuCountAsync(MarketDbpCartCartDataReadServiceGetCarSkuCountRequest request)
  - 执行 <c>jingdong.market.dbp.cart.CartDataReadService.getCarSkuCount</c>。

- async MarketServiceGetResponse ServiceGetAsync(MarketServiceGetRequest request)
  - 执行 <c>jingdong.market.service.get</c>。

- async MarketServiceListGetResponse ServiceListGetAsync(MarketServiceListGetRequest request)
  - 执行 <c>jingdong.market.service.list.get</c>。


## MarketBdpCartGetPinsBySkuIdRequest (class)

<c>jingdong.market.bdp.cart.getPinsBySkuId</c> 的请求。

- JdRequest req;

- public MarketBdpCartGetPinsBySkuIdRequest()

- MarketBdpCartGetPinsBySkuIdRequest SkuId(long skuId)
  - 设置 <c>skuId</c> 参数。

- MarketBdpCartGetPinsBySkuIdRequest Days(string days)
  - 设置 <c>days</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MarketBdpCartGetPinsBySkuIdResponse (class)

<c>jingdong.market.bdp.cart.getPinsBySkuId</c> 的响应。

- public List<string> returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MarketChargeListGetRequest (class)

<c>jingdong.market.charge.list.get</c> 的请求。

- JdRequest req;

- public MarketChargeListGetRequest()

- MarketChargeListGetRequest ServiceCode(string serviceCode)
  - 设置 <c>service_code</c> 参数。

- MarketChargeListGetRequest ServiceId(long serviceId)
  - 设置 <c>service_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MarketChargeListGetResponse (class)

<c>jingdong.market.charge.list.get</c> 的响应。

- public PublicResult PublicResult;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MarketDbpCartCartDataReadServiceGetCarSkuCountRequest (class)

<c>jingdong.market.dbp.cart.CartDataReadService.getCarSkuCount</c> 的请求。

- JdRequest req;

- public MarketDbpCartCartDataReadServiceGetCarSkuCountRequest()

- JdRequest Raw()
  - 底层协议请求。


## MarketDbpCartCartDataReadServiceGetCarSkuCountResponse (class)

<c>jingdong.market.dbp.cart.CartDataReadService.getCarSkuCount</c> 的响应。

- public long skuCount;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MarketServiceGetRequest (class)

<c>jingdong.market.service.get</c> 的请求。

- JdRequest req;

- public MarketServiceGetRequest()

- MarketServiceGetRequest ServiceCode(string serviceCode)
  - 设置 <c>service_code</c> 参数。

- MarketServiceGetRequest Id(long id)
  - 设置 <c>id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MarketServiceGetResponse (class)

<c>jingdong.market.service.get</c> 的响应。

- public ServiceResult service_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MarketServiceListGetRequest (class)

<c>jingdong.market.service.list.get</c> 的请求。

- JdRequest req;

- public MarketServiceListGetRequest()

- MarketServiceListGetRequest PageSize(int pageSize)
  - 设置 <c>page_size</c> 参数。

- MarketServiceListGetRequest Page(int page)
  - 设置 <c>page</c> 参数。

- MarketServiceListGetRequest ServiceStatus(int serviceStatus)
  - 设置 <c>service_status</c> 参数。

- MarketServiceListGetRequest StartDate(string startDate)
  - 设置 <c>start_date</c> 参数。

- MarketServiceListGetRequest EndDate(string endDate)
  - 设置 <c>end_date</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MarketServiceListGetResponse (class)

<c>jingdong.market.service.list.get</c> 的响应。

- public ServicesResult services_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
