# Sdk.Jd.Api.Price

> 源码: `stdlib/Sdk/Jd/Api/Price/JdPriceApi.zan`, `stdlib/Sdk/Jd/Api/Price/PriceWriteUpdateSkuJdPriceRequest.zan`, `stdlib/Sdk/Jd/Api/Price/PriceWriteUpdateWareMarketPriceRequest.zan`


## JdPriceApi (class)

jingdong.price.* 的强类型客户端。

- JdClient client;

- public JdPriceApi(JdClient client)

- async PriceWriteUpdateSkuJdPriceResponse WriteUpdateSkuJdPriceAsync(PriceWriteUpdateSkuJdPriceRequest request)
  - 执行 <c>jingdong.price.write.updateSkuJdPrice</c>。

- async PriceWriteUpdateWareMarketPriceResponse WriteUpdateWareMarketPriceAsync(PriceWriteUpdateWareMarketPriceRequest request)
  - 执行 <c>jingdong.price.write.updateWareMarketPrice</c>。


## PriceWriteUpdateSkuJdPriceRequest (class)

<c>jingdong.price.write.updateSkuJdPrice</c> 的请求。

- JdRequest req;

- public PriceWriteUpdateSkuJdPriceRequest()

- PriceWriteUpdateSkuJdPriceRequest JdPrice(string jdPrice)
  - 设置 <c>jdPrice</c> 参数。

- PriceWriteUpdateSkuJdPriceRequest SkuId(long skuId)
  - 设置 <c>skuId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PriceWriteUpdateSkuJdPriceResponse (class)

<c>jingdong.price.write.updateSkuJdPrice</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PriceWriteUpdateWareMarketPriceRequest (class)

<c>jingdong.price.write.updateWareMarketPrice</c> 的请求。

- JdRequest req;

- public PriceWriteUpdateWareMarketPriceRequest()

- PriceWriteUpdateWareMarketPriceRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- PriceWriteUpdateWareMarketPriceRequest MarketPrice(string marketPrice)
  - 设置 <c>marketPrice</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PriceWriteUpdateWareMarketPriceResponse (class)

<c>jingdong.price.write.updateWareMarketPrice</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
