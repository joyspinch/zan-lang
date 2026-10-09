# Sdk.Jd.Api.Sku

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Sku/JdSkuApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Sku/SkuReadFindSkuByIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Sku/SkuReadSearchSkuListRequest.zan`


## JdSkuApi (class)

- JdClient client;

- public JdSkuApi(JdClient client)

- async SkuReadFindSkuByIdResponse ReadFindSkuByIdAsync(SkuReadFindSkuByIdRequest request)

- async SkuReadSearchSkuListResponse ReadSearchSkuListAsync(SkuReadSearchSkuListRequest request)


## SkuReadFindSkuByIdRequest (class)

- JdRequest req;

- public SkuReadFindSkuByIdRequest()

- SkuReadFindSkuByIdRequest SkuId(long skuId)

- SkuReadFindSkuByIdRequest Field(string field)

- JdRequest Raw()


## SkuReadFindSkuByIdResponse (class)

- public Sku sku;

- public string Raw;


## SkuReadSearchSkuListRequest (class)

- JdRequest req;

- public SkuReadSearchSkuListRequest()

- SkuReadSearchSkuListRequest WareId(string wareId)

- SkuReadSearchSkuListRequest SkuId(string skuId)

- SkuReadSearchSkuListRequest SkuStatuValue(string skuStatuValue)

- SkuReadSearchSkuListRequest MaxStockNum(long maxStockNum)

- SkuReadSearchSkuListRequest MinStockNum(long minStockNum)

- SkuReadSearchSkuListRequest EndCreatedTime(string endCreatedTime)

- SkuReadSearchSkuListRequest EndModifiedTime(string endModifiedTime)

- SkuReadSearchSkuListRequest StartCreatedTime(string startCreatedTime)

- SkuReadSearchSkuListRequest StartModifiedTime(string startModifiedTime)

- SkuReadSearchSkuListRequest OutId(string outId)

- SkuReadSearchSkuListRequest ColType(int colType)

- SkuReadSearchSkuListRequest ItemNum(string itemNum)

- SkuReadSearchSkuListRequest WareTitle(string wareTitle)

- SkuReadSearchSkuListRequest OrderFiled(string orderFiled)

- SkuReadSearchSkuListRequest OrderType(string orderType)

- SkuReadSearchSkuListRequest PageNo(int pageNo)

- SkuReadSearchSkuListRequest PageSize(int pageSize)

- SkuReadSearchSkuListRequest Valid(string valid)

- SkuReadSearchSkuListRequest Key(string key)

- SkuReadSearchSkuListRequest Value(string value_)

- SkuReadSearchSkuListRequest Cn(string cn)

- SkuReadSearchSkuListRequest Field(string field)

- JdRequest Raw()


## SkuReadSearchSkuListResponse (class)

- public Page page;

- public string Raw;
