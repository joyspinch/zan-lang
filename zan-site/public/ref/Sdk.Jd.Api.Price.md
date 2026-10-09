# Sdk.Jd.Api.Price

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Price/JdPriceApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Price/PriceWriteUpdateSkuJdPriceRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Price/PriceWriteUpdateWareMarketPriceRequest.zan`


## JdPriceApi (class)

- JdClient client;

- public JdPriceApi(JdClient client)

- async PriceWriteUpdateSkuJdPriceResponse WriteUpdateSkuJdPriceAsync(PriceWriteUpdateSkuJdPriceRequest request)

- async PriceWriteUpdateWareMarketPriceResponse WriteUpdateWareMarketPriceAsync(PriceWriteUpdateWareMarketPriceRequest request)


## PriceWriteUpdateSkuJdPriceRequest (class)

- JdRequest req;

- public PriceWriteUpdateSkuJdPriceRequest()

- PriceWriteUpdateSkuJdPriceRequest JdPrice(string jdPrice)

- PriceWriteUpdateSkuJdPriceRequest SkuId(long skuId)

- JdRequest Raw()


## PriceWriteUpdateSkuJdPriceResponse (class)

- public bool success;

- public string Raw;


## PriceWriteUpdateWareMarketPriceRequest (class)

- JdRequest req;

- public PriceWriteUpdateWareMarketPriceRequest()

- PriceWriteUpdateWareMarketPriceRequest WareId(long wareId)

- PriceWriteUpdateWareMarketPriceRequest MarketPrice(string marketPrice)

- JdRequest Raw()


## PriceWriteUpdateWareMarketPriceResponse (class)

- public bool success;

- public string Raw;
