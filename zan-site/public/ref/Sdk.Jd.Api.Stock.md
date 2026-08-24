# Sdk.Jd.Api.Stock

> 源码: `stdlib/Sdk/Jd/Api/Stock/JdStockApi.zan`, `stdlib/Sdk/Jd/Api/Stock/StockReadFindSkuStockRequest.zan`, `stdlib/Sdk/Jd/Api/Stock/StockWriteUpdateSkuStockRequest.zan`


## JdStockApi (class)

jingdong.stock.* 的强类型客户端。

- JdClient client;

- public JdStockApi(JdClient client)

- async StockReadFindSkuStockResponse ReadFindSkuStockAsync(StockReadFindSkuStockRequest request)
  - 执行 <c>jingdong.stock.read.findSkuStock</c>。

- async StockWriteUpdateSkuStockResponse WriteUpdateSkuStockAsync(StockWriteUpdateSkuStockRequest request)
  - 执行 <c>jingdong.stock.write.updateSkuStock</c>。


## StockReadFindSkuStockRequest (class)

<c>jingdong.stock.read.findSkuStock</c> 的请求。

- JdRequest req;

- public StockReadFindSkuStockRequest()

- StockReadFindSkuStockRequest SkuId(long skuId)
  - 设置 <c>skuId</c> 参数。

- StockReadFindSkuStockRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## StockReadFindSkuStockResponse (class)

<c>jingdong.stock.read.findSkuStock</c> 的响应。

- public List<string> skuStocks;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## StockWriteUpdateSkuStockRequest (class)

<c>jingdong.stock.write.updateSkuStock</c> 的请求。

- JdRequest req;

- public StockWriteUpdateSkuStockRequest()

- StockWriteUpdateSkuStockRequest SkuId(long skuId)
  - 设置 <c>skuId</c> 参数。

- StockWriteUpdateSkuStockRequest StockNum(long stockNum)
  - 设置 <c>stockNum</c> 参数。

- StockWriteUpdateSkuStockRequest StoreId(long storeId)
  - 设置 <c>storeId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## StockWriteUpdateSkuStockResponse (class)

<c>jingdong.stock.write.updateSkuStock</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
