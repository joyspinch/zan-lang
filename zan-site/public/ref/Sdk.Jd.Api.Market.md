# Sdk.Jd.Api.Market

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Market/JdMarketApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Market/MarketBdpCartGetPinsBySkuIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Market/MarketChargeListGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Market/MarketDbpCartCartDataReadServiceGetCarSkuCountRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Market/MarketServiceGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Market/MarketServiceListGetRequest.zan`


## JdMarketApi (class)

- JdClient client;

- public JdMarketApi(JdClient client)

- async MarketBdpCartGetPinsBySkuIdResponse BdpCartGetPinsBySkuIdAsync(MarketBdpCartGetPinsBySkuIdRequest request)

- async MarketChargeListGetResponse ChargeListGetAsync(MarketChargeListGetRequest request)

- async MarketDbpCartCartDataReadServiceGetCarSkuCountResponse DbpCartCartDataReadServiceGetCarSkuCountAsync(MarketDbpCartCartDataReadServiceGetCarSkuCountRequest request)

- async MarketServiceGetResponse ServiceGetAsync(MarketServiceGetRequest request)

- async MarketServiceListGetResponse ServiceListGetAsync(MarketServiceListGetRequest request)


## MarketBdpCartGetPinsBySkuIdRequest (class)

- JdRequest req;

- public MarketBdpCartGetPinsBySkuIdRequest()

- MarketBdpCartGetPinsBySkuIdRequest SkuId(long skuId)

- MarketBdpCartGetPinsBySkuIdRequest Days(string days)

- JdRequest Raw()


## MarketBdpCartGetPinsBySkuIdResponse (class)

- public List<string> returnType;

- public string Raw;


## MarketChargeListGetRequest (class)

- JdRequest req;

- public MarketChargeListGetRequest()

- MarketChargeListGetRequest ServiceCode(string serviceCode)

- MarketChargeListGetRequest ServiceId(long serviceId)

- JdRequest Raw()


## MarketChargeListGetResponse (class)

- public PublicResult PublicResult;

- public string Raw;


## MarketDbpCartCartDataReadServiceGetCarSkuCountRequest (class)

- JdRequest req;

- public MarketDbpCartCartDataReadServiceGetCarSkuCountRequest()

- JdRequest Raw()


## MarketDbpCartCartDataReadServiceGetCarSkuCountResponse (class)

- public long skuCount;

- public string Raw;


## MarketServiceGetRequest (class)

- JdRequest req;

- public MarketServiceGetRequest()

- MarketServiceGetRequest ServiceCode(string serviceCode)

- MarketServiceGetRequest Id(long id)

- JdRequest Raw()


## MarketServiceGetResponse (class)

- public ServiceResult service_result;

- public string Raw;


## MarketServiceListGetRequest (class)

- JdRequest req;

- public MarketServiceListGetRequest()

- MarketServiceListGetRequest PageSize(int pageSize)

- MarketServiceListGetRequest Page(int page)

- MarketServiceListGetRequest ServiceStatus(int serviceStatus)

- MarketServiceListGetRequest StartDate(string startDate)

- MarketServiceListGetRequest EndDate(string endDate)

- JdRequest Raw()


## MarketServiceListGetResponse (class)

- public ServicesResult services_result;

- public string Raw;
