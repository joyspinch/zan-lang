# Sdk.Jd.Api.Sku

> 源码: `stdlib/Sdk/Jd/Api/Sku/JdSkuApi.zan`, `stdlib/Sdk/Jd/Api/Sku/SkuReadFindSkuByIdRequest.zan`, `stdlib/Sdk/Jd/Api/Sku/SkuReadSearchSkuListRequest.zan`


## JdSkuApi (class)

jingdong.sku.* 的强类型客户端。

- JdClient client;

- public JdSkuApi(JdClient client)

- async SkuReadFindSkuByIdResponse ReadFindSkuByIdAsync(SkuReadFindSkuByIdRequest request)
  - 执行 <c>jingdong.sku.read.findSkuById</c>。

- async SkuReadSearchSkuListResponse ReadSearchSkuListAsync(SkuReadSearchSkuListRequest request)
  - 执行 <c>jingdong.sku.read.searchSkuList</c>。


## SkuReadFindSkuByIdRequest (class)

<c>jingdong.sku.read.findSkuById</c> 的请求。

- JdRequest req;

- public SkuReadFindSkuByIdRequest()

- SkuReadFindSkuByIdRequest SkuId(long skuId)
  - 设置 <c>skuId</c> 参数。

- SkuReadFindSkuByIdRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SkuReadFindSkuByIdResponse (class)

<c>jingdong.sku.read.findSkuById</c> 的响应。

- public Sku sku;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SkuReadSearchSkuListRequest (class)

<c>jingdong.sku.read.searchSkuList</c> 的请求。

- JdRequest req;

- public SkuReadSearchSkuListRequest()

- SkuReadSearchSkuListRequest WareId(string wareId)
  - 设置 <c>wareId</c> 参数。

- SkuReadSearchSkuListRequest SkuId(string skuId)
  - 设置 <c>skuId</c> 参数。

- SkuReadSearchSkuListRequest SkuStatuValue(string skuStatuValue)
  - 设置 <c>skuStatuValue</c> 参数。

- SkuReadSearchSkuListRequest MaxStockNum(long maxStockNum)
  - 设置 <c>maxStockNum</c> 参数。

- SkuReadSearchSkuListRequest MinStockNum(long minStockNum)
  - 设置 <c>minStockNum</c> 参数。

- SkuReadSearchSkuListRequest EndCreatedTime(string endCreatedTime)
  - 设置 <c>endCreatedTime</c> 参数。

- SkuReadSearchSkuListRequest EndModifiedTime(string endModifiedTime)
  - 设置 <c>endModifiedTime</c> 参数。

- SkuReadSearchSkuListRequest StartCreatedTime(string startCreatedTime)
  - 设置 <c>startCreatedTime</c> 参数。

- SkuReadSearchSkuListRequest StartModifiedTime(string startModifiedTime)
  - 设置 <c>startModifiedTime</c> 参数。

- SkuReadSearchSkuListRequest OutId(string outId)
  - 设置 <c>outId</c> 参数。

- SkuReadSearchSkuListRequest ColType(int colType)
  - 设置 <c>colType</c> 参数。

- SkuReadSearchSkuListRequest ItemNum(string itemNum)
  - 设置 <c>itemNum</c> 参数。

- SkuReadSearchSkuListRequest WareTitle(string wareTitle)
  - 设置 <c>wareTitle</c> 参数。

- SkuReadSearchSkuListRequest OrderFiled(string orderFiled)
  - 设置 <c>orderFiled</c> 参数。

- SkuReadSearchSkuListRequest OrderType(string orderType)
  - 设置 <c>orderType</c> 参数。

- SkuReadSearchSkuListRequest PageNo(int pageNo)
  - 设置 <c>pageNo</c> 参数。

- SkuReadSearchSkuListRequest PageSize(int pageSize)
  - 设置 <c>page_size</c> 参数。

- SkuReadSearchSkuListRequest Valid(string valid)
  - 设置 <c>valid</c> 参数。

- SkuReadSearchSkuListRequest Key(string key)
  - 设置 <c>key</c> 参数。

- SkuReadSearchSkuListRequest Value(string value_)
  - 设置 <c>value</c> 参数。

- SkuReadSearchSkuListRequest Cn(string cn)
  - 设置 <c>cn</c> 参数。

- SkuReadSearchSkuListRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SkuReadSearchSkuListResponse (class)

<c>jingdong.sku.read.searchSkuList</c> 的响应。

- public Page page;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
