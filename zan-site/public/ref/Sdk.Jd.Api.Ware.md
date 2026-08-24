# Sdk.Jd.Api.Ware

> 源码: `stdlib/Sdk/Jd/Api/Ware/JdWareApi.zan`, `stdlib/Sdk/Jd/Api/Ware/WareProductbigfieldGetRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareProductimageGetRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareProductsortGetRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareReadFindOpReasonRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareReadFindWareByIdRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareReadSearchWare4RecycledRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareReadSearchWare4ValidRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareWriteDeleteRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareWriteUpOrDownRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareWriteUpdateWareStatusByTimerRequest.zan`, `stdlib/Sdk/Jd/Api/Ware/WareWriteUpdateWareTitleRequest.zan`


## JdWareApi (class)

jingdong.ware.* 的强类型客户端。

- JdClient client;

- public JdWareApi(JdClient client)

- async WareProductbigfieldGetResponse ProductbigfieldGetAsync(WareProductbigfieldGetRequest request)
  - 执行 <c>jingdong.ware.productbigfield.get</c>。

- async WareProductimageGetResponse ProductimageGetAsync(WareProductimageGetRequest request)
  - 执行 <c>jingdong.ware.productimage.get</c>。

- async WareProductsortGetResponse ProductsortGetAsync(WareProductsortGetRequest request)
  - 执行 <c>jingdong.ware.productsort.get</c>。

- async WareReadFindOpReasonResponse ReadFindOpReasonAsync(WareReadFindOpReasonRequest request)
  - 执行 <c>jingdong.ware.read.findOpReason</c>。

- async WareReadFindWareByIdResponse ReadFindWareByIdAsync(WareReadFindWareByIdRequest request)
  - 执行 <c>jingdong.ware.read.findWareById</c>。

- async WareReadSearchWare4RecycledResponse ReadSearchWare4RecycledAsync(WareReadSearchWare4RecycledRequest request)
  - 执行 <c>jingdong.ware.read.searchWare4Recycled</c>。

- async WareReadSearchWare4ValidResponse ReadSearchWare4ValidAsync(WareReadSearchWare4ValidRequest request)
  - 执行 <c>jingdong.ware.read.searchWare4Valid</c>。

- async WareWriteDeleteResponse WriteDeleteAsync(WareWriteDeleteRequest request)
  - 执行 <c>jingdong.ware.write.delete</c>。

- async WareWriteUpOrDownResponse WriteUpOrDownAsync(WareWriteUpOrDownRequest request)
  - 执行 <c>jingdong.ware.write.upOrDown</c>。

- async WareWriteUpdateWareStatusByTimerResponse WriteUpdateWareStatusByTimerAsync(WareWriteUpdateWareStatusByTimerRequest request)
  - 执行 <c>jingdong.ware.write.updateWareStatusByTimer</c>。

- async WareWriteUpdateWareTitleResponse WriteUpdateWareTitleAsync(WareWriteUpdateWareTitleRequest request)
  - 执行 <c>jingdong.ware.write.updateWareTitle</c>。


## WareProductbigfieldGetRequest (class)

<c>jingdong.ware.productbigfield.get</c> 的请求。

- JdRequest req;

- public WareProductbigfieldGetRequest()

- WareProductbigfieldGetRequest SkuId(string skuId)
  - 设置 <c>sku_id</c> 参数。

- WareProductbigfieldGetRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareProductbigfieldGetResponse (class)

<c>jingdong.ware.productbigfield.get</c> 的响应。

- public string shou_hou;

- public string wdis;

- public string prop_code;

- public string ware_qd;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareProductimageGetRequest (class)

<c>jingdong.ware.productimage.get</c> 的请求。

- JdRequest req;

- public WareProductimageGetRequest()

- WareProductimageGetRequest SkuId(string skuId)
  - 设置 <c>sku_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareProductimageGetResponse (class)

<c>jingdong.ware.productimage.get</c> 的响应。

- public List<string> image_path_list;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareProductsortGetRequest (class)

<c>jingdong.ware.productsort.get</c> 的请求。

- JdRequest req;

- public WareProductsortGetRequest()

- WareProductsortGetRequest ProductSortIds(string productSortIds)
  - 设置 <c>product_sort_ids</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareProductsortGetResponse (class)

<c>jingdong.ware.productsort.get</c> 的响应。

- public List<string> product_sorts;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareReadFindOpReasonRequest (class)

<c>jingdong.ware.read.findOpReason</c> 的请求。

- JdRequest req;

- public WareReadFindOpReasonRequest()

- WareReadFindOpReasonRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- WareReadFindOpReasonRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareReadFindOpReasonResponse (class)

<c>jingdong.ware.read.findOpReason</c> 的响应。

- public OpReason opReason;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareReadFindWareByIdRequest (class)

<c>jingdong.ware.read.findWareById</c> 的请求。

- JdRequest req;

- public WareReadFindWareByIdRequest()

- WareReadFindWareByIdRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- WareReadFindWareByIdRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareReadFindWareByIdResponse (class)

<c>jingdong.ware.read.findWareById</c> 的响应。

- public Ware ware;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareReadSearchWare4RecycledRequest (class)

<c>jingdong.ware.read.searchWare4Recycled</c> 的请求。

- JdRequest req;

- public WareReadSearchWare4RecycledRequest()

- WareReadSearchWare4RecycledRequest WareId(string wareId)
  - 设置 <c>wareId</c> 参数。

- WareReadSearchWare4RecycledRequest SearchKey(string searchKey)
  - 设置 <c>searchKey</c> 参数。

- WareReadSearchWare4RecycledRequest SearchField(string searchField)
  - 设置 <c>searchField</c> 参数。

- WareReadSearchWare4RecycledRequest CategoryId(long categoryId)
  - 设置 <c>categoryId</c> 参数。

- WareReadSearchWare4RecycledRequest ShopCategoryIdLevel1(long shopCategoryIdLevel1)
  - 设置 <c>shopCategoryIdLevel1</c> 参数。

- WareReadSearchWare4RecycledRequest ShopCategoryIdLevel2(long shopCategoryIdLevel2)
  - 设置 <c>shopCategoryIdLevel2</c> 参数。

- WareReadSearchWare4RecycledRequest TemplateId(long templateId)
  - 设置 <c>templateId</c> 参数。

- WareReadSearchWare4RecycledRequest PromiseId(long promiseId)
  - 设置 <c>promiseId</c> 参数。

- WareReadSearchWare4RecycledRequest BrandId(long brandId)
  - 设置 <c>brandId</c> 参数。

- WareReadSearchWare4RecycledRequest FeatureKey(string featureKey)
  - 设置 <c>featureKey</c> 参数。

- WareReadSearchWare4RecycledRequest FeatureValue(string featureValue)
  - 设置 <c>featureValue</c> 参数。

- WareReadSearchWare4RecycledRequest WareStatusValue(string wareStatusValue)
  - 设置 <c>wareStatusValue</c> 参数。

- WareReadSearchWare4RecycledRequest ItemNum(string itemNum)
  - 设置 <c>itemNum</c> 参数。

- WareReadSearchWare4RecycledRequest BarCode(string barCode)
  - 设置 <c>barCode</c> 参数。

- WareReadSearchWare4RecycledRequest ColType(int colType)
  - 设置 <c>colType</c> 参数。

- WareReadSearchWare4RecycledRequest StartCreatedTime(string startCreatedTime)
  - 设置 <c>startCreatedTime</c> 参数。

- WareReadSearchWare4RecycledRequest EndCreatedTime(string endCreatedTime)
  - 设置 <c>endCreatedTime</c> 参数。

- WareReadSearchWare4RecycledRequest StartJdPrice(string startJdPrice)
  - 设置 <c>startJdPrice</c> 参数。

- WareReadSearchWare4RecycledRequest EndJdPrice(string endJdPrice)
  - 设置 <c>endJdPrice</c> 参数。

- WareReadSearchWare4RecycledRequest StartOnlineTime(string startOnlineTime)
  - 设置 <c>startOnlineTime</c> 参数。

- WareReadSearchWare4RecycledRequest EndOnlineTime(string endOnlineTime)
  - 设置 <c>endOnlineTime</c> 参数。

- WareReadSearchWare4RecycledRequest StartModifiedTime(string startModifiedTime)
  - 设置 <c>startModifiedTime</c> 参数。

- WareReadSearchWare4RecycledRequest EndModifiedTime(string endModifiedTime)
  - 设置 <c>endModifiedTime</c> 参数。

- WareReadSearchWare4RecycledRequest StartOfflineTime(string startOfflineTime)
  - 设置 <c>startOfflineTime</c> 参数。

- WareReadSearchWare4RecycledRequest EndOfflineTime(string endOfflineTime)
  - 设置 <c>endOfflineTime</c> 参数。

- WareReadSearchWare4RecycledRequest StartStockNum(long startStockNum)
  - 设置 <c>startStockNum</c> 参数。

- WareReadSearchWare4RecycledRequest EndStockNum(long endStockNum)
  - 设置 <c>endStockNum</c> 参数。

- WareReadSearchWare4RecycledRequest OrderField(string orderField)
  - 设置 <c>orderField</c> 参数。

- WareReadSearchWare4RecycledRequest OrderType(string orderType)
  - 设置 <c>orderType</c> 参数。

- WareReadSearchWare4RecycledRequest PageNo(int pageNo)
  - 设置 <c>pageNo</c> 参数。

- WareReadSearchWare4RecycledRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- WareReadSearchWare4RecycledRequest TransportId(long transportId)
  - 设置 <c>transportId</c> 参数。

- WareReadSearchWare4RecycledRequest Claim(int claim)
  - 设置 <c>claim</c> 参数。

- WareReadSearchWare4RecycledRequest GroupId(long groupId)
  - 设置 <c>groupId</c> 参数。

- WareReadSearchWare4RecycledRequest MultiCategoryId(long multiCategoryId)
  - 设置 <c>multiCategoryId</c> 参数。

- WareReadSearchWare4RecycledRequest WarePropKey(string warePropKey)
  - 设置 <c>warePropKey</c> 参数。

- WareReadSearchWare4RecycledRequest WarePropValue(string warePropValue)
  - 设置 <c>warePropValue</c> 参数。

- WareReadSearchWare4RecycledRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareReadSearchWare4RecycledResponse (class)

<c>jingdong.ware.read.searchWare4Recycled</c> 的响应。

- public Page page;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareReadSearchWare4ValidRequest (class)

<c>jingdong.ware.read.searchWare4Valid</c> 的请求。

- JdRequest req;

- public WareReadSearchWare4ValidRequest()

- WareReadSearchWare4ValidRequest WareId(string wareId)
  - 设置 <c>wareId</c> 参数。

- WareReadSearchWare4ValidRequest SearchKey(string searchKey)
  - 设置 <c>searchKey</c> 参数。

- WareReadSearchWare4ValidRequest SearchField(string searchField)
  - 设置 <c>searchField</c> 参数。

- WareReadSearchWare4ValidRequest CategoryId(long categoryId)
  - 设置 <c>categoryId</c> 参数。

- WareReadSearchWare4ValidRequest ShopCategoryIdLevel1(long shopCategoryIdLevel1)
  - 设置 <c>shopCategoryIdLevel1</c> 参数。

- WareReadSearchWare4ValidRequest ShopCategoryIdLevel2(long shopCategoryIdLevel2)
  - 设置 <c>shopCategoryIdLevel2</c> 参数。

- WareReadSearchWare4ValidRequest TemplateId(long templateId)
  - 设置 <c>templateId</c> 参数。

- WareReadSearchWare4ValidRequest PromiseId(long promiseId)
  - 设置 <c>promiseId</c> 参数。

- WareReadSearchWare4ValidRequest BrandId(long brandId)
  - 设置 <c>brandId</c> 参数。

- WareReadSearchWare4ValidRequest FeatureKey(string featureKey)
  - 设置 <c>featureKey</c> 参数。

- WareReadSearchWare4ValidRequest FeatureValue(string featureValue)
  - 设置 <c>featureValue</c> 参数。

- WareReadSearchWare4ValidRequest WareStatusValue(string wareStatusValue)
  - 设置 <c>wareStatusValue</c> 参数。

- WareReadSearchWare4ValidRequest ItemNum(string itemNum)
  - 设置 <c>itemNum</c> 参数。

- WareReadSearchWare4ValidRequest BarCode(string barCode)
  - 设置 <c>barCode</c> 参数。

- WareReadSearchWare4ValidRequest ColType(int colType)
  - 设置 <c>colType</c> 参数。

- WareReadSearchWare4ValidRequest StartCreatedTime(string startCreatedTime)
  - 设置 <c>startCreatedTime</c> 参数。

- WareReadSearchWare4ValidRequest EndCreatedTime(string endCreatedTime)
  - 设置 <c>endCreatedTime</c> 参数。

- WareReadSearchWare4ValidRequest StartJdPrice(string startJdPrice)
  - 设置 <c>startJdPrice</c> 参数。

- WareReadSearchWare4ValidRequest EndJdPrice(string endJdPrice)
  - 设置 <c>endJdPrice</c> 参数。

- WareReadSearchWare4ValidRequest StartOnlineTime(string startOnlineTime)
  - 设置 <c>startOnlineTime</c> 参数。

- WareReadSearchWare4ValidRequest EndOnlineTime(string endOnlineTime)
  - 设置 <c>endOnlineTime</c> 参数。

- WareReadSearchWare4ValidRequest StartModifiedTime(string startModifiedTime)
  - 设置 <c>startModifiedTime</c> 参数。

- WareReadSearchWare4ValidRequest EndModifiedTime(string endModifiedTime)
  - 设置 <c>endModifiedTime</c> 参数。

- WareReadSearchWare4ValidRequest StartOfflineTime(string startOfflineTime)
  - 设置 <c>startOfflineTime</c> 参数。

- WareReadSearchWare4ValidRequest EndOfflineTime(string endOfflineTime)
  - 设置 <c>endOfflineTime</c> 参数。

- WareReadSearchWare4ValidRequest StartStockNum(long startStockNum)
  - 设置 <c>startStockNum</c> 参数。

- WareReadSearchWare4ValidRequest EndStockNum(long endStockNum)
  - 设置 <c>endStockNum</c> 参数。

- WareReadSearchWare4ValidRequest OrderField(string orderField)
  - 设置 <c>orderField</c> 参数。

- WareReadSearchWare4ValidRequest OrderType(string orderType)
  - 设置 <c>orderType</c> 参数。

- WareReadSearchWare4ValidRequest PageNo(int pageNo)
  - 设置 <c>pageNo</c> 参数。

- WareReadSearchWare4ValidRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- WareReadSearchWare4ValidRequest TransportId(long transportId)
  - 设置 <c>transportId</c> 参数。

- WareReadSearchWare4ValidRequest Claim(int claim)
  - 设置 <c>claim</c> 参数。

- WareReadSearchWare4ValidRequest GroupId(long groupId)
  - 设置 <c>groupId</c> 参数。

- WareReadSearchWare4ValidRequest MultiCategoryId(long multiCategoryId)
  - 设置 <c>multiCategoryId</c> 参数。

- WareReadSearchWare4ValidRequest WarePropKey(string warePropKey)
  - 设置 <c>warePropKey</c> 参数。

- WareReadSearchWare4ValidRequest WarePropValue(string warePropValue)
  - 设置 <c>warePropValue</c> 参数。

- WareReadSearchWare4ValidRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareReadSearchWare4ValidResponse (class)

<c>jingdong.ware.read.searchWare4Valid</c> 的响应。

- public Page page;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareWriteDeleteRequest (class)

<c>jingdong.ware.write.delete</c> 的请求。

- JdRequest req;

- public WareWriteDeleteRequest()

- WareWriteDeleteRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareWriteDeleteResponse (class)

<c>jingdong.ware.write.delete</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareWriteUpOrDownRequest (class)

<c>jingdong.ware.write.upOrDown</c> 的请求。

- JdRequest req;

- public WareWriteUpOrDownRequest()

- WareWriteUpOrDownRequest Note(string note)
  - 设置 <c>note</c> 参数。

- WareWriteUpOrDownRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- WareWriteUpOrDownRequest OpType(int opType)
  - 设置 <c>opType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareWriteUpOrDownResponse (class)

<c>jingdong.ware.write.upOrDown</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareWriteUpdateWareStatusByTimerRequest (class)

<c>jingdong.ware.write.updateWareStatusByTimer</c> 的请求。

- JdRequest req;

- public WareWriteUpdateWareStatusByTimerRequest()

- WareWriteUpdateWareStatusByTimerRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- WareWriteUpdateWareStatusByTimerRequest UpTime(long upTime)
  - 设置 <c>upTime</c> 参数。

- WareWriteUpdateWareStatusByTimerRequest DownTime(long downTime)
  - 设置 <c>downTime</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareWriteUpdateWareStatusByTimerResponse (class)

<c>jingdong.ware.write.updateWareStatusByTimer</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## WareWriteUpdateWareTitleRequest (class)

<c>jingdong.ware.write.updateWareTitle</c> 的请求。

- JdRequest req;

- public WareWriteUpdateWareTitleRequest()

- WareWriteUpdateWareTitleRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- WareWriteUpdateWareTitleRequest Title(string title)
  - 设置 <c>title</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## WareWriteUpdateWareTitleResponse (class)

<c>jingdong.ware.write.updateWareTitle</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
