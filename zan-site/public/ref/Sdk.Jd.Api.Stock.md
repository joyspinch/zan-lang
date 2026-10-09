# Sdk.Jd.Api.Stock

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Stock/JdStockApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Stock/StockReadFindSkuStockRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Stock/StockWriteUpdateSkuStockRequest.zan`


## JdStockApi (class)

- JdClient client;

- public JdStockApi(JdClient client)

- async StockReadFindSkuStockResponse ReadFindSkuStockAsync(StockReadFindSkuStockRequest request)

- async StockWriteUpdateSkuStockResponse WriteUpdateSkuStockAsync(StockWriteUpdateSkuStockRequest request)


## StockReadFindSkuStockRequest (class)

- JdRequest req;

- public StockReadFindSkuStockRequest()

- StockReadFindSkuStockRequest SkuId(long skuId)

- StockReadFindSkuStockRequest Field(string field)

- JdRequest Raw()


## StockReadFindSkuStockResponse (class)

- public List<string> skuStocks;

- public string Raw;


## StockWriteUpdateSkuStockRequest (class)

- JdRequest req;

- public StockWriteUpdateSkuStockRequest()

- StockWriteUpdateSkuStockRequest SkuId(long skuId)

- StockWriteUpdateSkuStockRequest StockNum(long stockNum)

- StockWriteUpdateSkuStockRequest StoreId(long storeId)

- JdRequest Raw()


## StockWriteUpdateSkuStockResponse (class)

- public bool success;

- public string Raw;
