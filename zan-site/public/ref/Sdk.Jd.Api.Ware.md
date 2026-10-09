# Sdk.Jd.Api.Ware

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/JdWareApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareProductbigfieldGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareProductimageGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareProductsortGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareReadFindOpReasonRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareReadFindWareByIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareReadSearchWare4RecycledRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareReadSearchWare4ValidRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareWriteDeleteRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareWriteUpOrDownRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareWriteUpdateWareStatusByTimerRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Ware/WareWriteUpdateWareTitleRequest.zan`


## JdWareApi (class)

- JdClient client;

- public JdWareApi(JdClient client)

- async WareProductbigfieldGetResponse ProductbigfieldGetAsync(WareProductbigfieldGetRequest request)

- async WareProductimageGetResponse ProductimageGetAsync(WareProductimageGetRequest request)

- async WareProductsortGetResponse ProductsortGetAsync(WareProductsortGetRequest request)

- async WareReadFindOpReasonResponse ReadFindOpReasonAsync(WareReadFindOpReasonRequest request)

- async WareReadFindWareByIdResponse ReadFindWareByIdAsync(WareReadFindWareByIdRequest request)

- async WareReadSearchWare4RecycledResponse ReadSearchWare4RecycledAsync(WareReadSearchWare4RecycledRequest request)

- async WareReadSearchWare4ValidResponse ReadSearchWare4ValidAsync(WareReadSearchWare4ValidRequest request)

- async WareWriteDeleteResponse WriteDeleteAsync(WareWriteDeleteRequest request)

- async WareWriteUpOrDownResponse WriteUpOrDownAsync(WareWriteUpOrDownRequest request)

- async WareWriteUpdateWareStatusByTimerResponse WriteUpdateWareStatusByTimerAsync(WareWriteUpdateWareStatusByTimerRequest request)

- async WareWriteUpdateWareTitleResponse WriteUpdateWareTitleAsync(WareWriteUpdateWareTitleRequest request)


## WareProductbigfieldGetRequest (class)

- JdRequest req;

- public WareProductbigfieldGetRequest()

- WareProductbigfieldGetRequest SkuId(string skuId)

- WareProductbigfieldGetRequest Field(string field)

- JdRequest Raw()


## WareProductbigfieldGetResponse (class)

- public string shou_hou;

- public string wdis;

- public string prop_code;

- public string ware_qd;

- public string Raw;


## WareProductimageGetRequest (class)

- JdRequest req;

- public WareProductimageGetRequest()

- WareProductimageGetRequest SkuId(string skuId)

- JdRequest Raw()


## WareProductimageGetResponse (class)

- public List<string> image_path_list;

- public string Raw;


## WareProductsortGetRequest (class)

- JdRequest req;

- public WareProductsortGetRequest()

- WareProductsortGetRequest ProductSortIds(string productSortIds)

- JdRequest Raw()


## WareProductsortGetResponse (class)

- public List<string> product_sorts;

- public string Raw;


## WareReadFindOpReasonRequest (class)

- JdRequest req;

- public WareReadFindOpReasonRequest()

- WareReadFindOpReasonRequest WareId(long wareId)

- WareReadFindOpReasonRequest Field(string field)

- JdRequest Raw()


## WareReadFindOpReasonResponse (class)

- public OpReason opReason;

- public string Raw;


## WareReadFindWareByIdRequest (class)

- JdRequest req;

- public WareReadFindWareByIdRequest()

- WareReadFindWareByIdRequest WareId(long wareId)

- WareReadFindWareByIdRequest Field(string field)

- JdRequest Raw()


## WareReadFindWareByIdResponse (class)

- public Ware ware;

- public string Raw;


## WareReadSearchWare4RecycledRequest (class)

- JdRequest req;

- public WareReadSearchWare4RecycledRequest()

- WareReadSearchWare4RecycledRequest WareId(string wareId)

- WareReadSearchWare4RecycledRequest SearchKey(string searchKey)

- WareReadSearchWare4RecycledRequest SearchField(string searchField)

- WareReadSearchWare4RecycledRequest CategoryId(long categoryId)

- WareReadSearchWare4RecycledRequest ShopCategoryIdLevel1(long shopCategoryIdLevel1)

- WareReadSearchWare4RecycledRequest ShopCategoryIdLevel2(long shopCategoryIdLevel2)

- WareReadSearchWare4RecycledRequest TemplateId(long templateId)

- WareReadSearchWare4RecycledRequest PromiseId(long promiseId)

- WareReadSearchWare4RecycledRequest BrandId(long brandId)

- WareReadSearchWare4RecycledRequest FeatureKey(string featureKey)

- WareReadSearchWare4RecycledRequest FeatureValue(string featureValue)

- WareReadSearchWare4RecycledRequest WareStatusValue(string wareStatusValue)

- WareReadSearchWare4RecycledRequest ItemNum(string itemNum)

- WareReadSearchWare4RecycledRequest BarCode(string barCode)

- WareReadSearchWare4RecycledRequest ColType(int colType)

- WareReadSearchWare4RecycledRequest StartCreatedTime(string startCreatedTime)

- WareReadSearchWare4RecycledRequest EndCreatedTime(string endCreatedTime)

- WareReadSearchWare4RecycledRequest StartJdPrice(string startJdPrice)

- WareReadSearchWare4RecycledRequest EndJdPrice(string endJdPrice)

- WareReadSearchWare4RecycledRequest StartOnlineTime(string startOnlineTime)

- WareReadSearchWare4RecycledRequest EndOnlineTime(string endOnlineTime)

- WareReadSearchWare4RecycledRequest StartModifiedTime(string startModifiedTime)

- WareReadSearchWare4RecycledRequest EndModifiedTime(string endModifiedTime)

- WareReadSearchWare4RecycledRequest StartOfflineTime(string startOfflineTime)

- WareReadSearchWare4RecycledRequest EndOfflineTime(string endOfflineTime)

- WareReadSearchWare4RecycledRequest StartStockNum(long startStockNum)

- WareReadSearchWare4RecycledRequest EndStockNum(long endStockNum)

- WareReadSearchWare4RecycledRequest OrderField(string orderField)

- WareReadSearchWare4RecycledRequest OrderType(string orderType)

- WareReadSearchWare4RecycledRequest PageNo(int pageNo)

- WareReadSearchWare4RecycledRequest PageSize(int pageSize)

- WareReadSearchWare4RecycledRequest TransportId(long transportId)

- WareReadSearchWare4RecycledRequest Claim(int claim)

- WareReadSearchWare4RecycledRequest GroupId(long groupId)

- WareReadSearchWare4RecycledRequest MultiCategoryId(long multiCategoryId)

- WareReadSearchWare4RecycledRequest WarePropKey(string warePropKey)

- WareReadSearchWare4RecycledRequest WarePropValue(string warePropValue)

- WareReadSearchWare4RecycledRequest Field(string field)

- JdRequest Raw()


## WareReadSearchWare4RecycledResponse (class)

- public Page page;

- public string Raw;


## WareReadSearchWare4ValidRequest (class)

- JdRequest req;

- public WareReadSearchWare4ValidRequest()

- WareReadSearchWare4ValidRequest WareId(string wareId)

- WareReadSearchWare4ValidRequest SearchKey(string searchKey)

- WareReadSearchWare4ValidRequest SearchField(string searchField)

- WareReadSearchWare4ValidRequest CategoryId(long categoryId)

- WareReadSearchWare4ValidRequest ShopCategoryIdLevel1(long shopCategoryIdLevel1)

- WareReadSearchWare4ValidRequest ShopCategoryIdLevel2(long shopCategoryIdLevel2)

- WareReadSearchWare4ValidRequest TemplateId(long templateId)

- WareReadSearchWare4ValidRequest PromiseId(long promiseId)

- WareReadSearchWare4ValidRequest BrandId(long brandId)

- WareReadSearchWare4ValidRequest FeatureKey(string featureKey)

- WareReadSearchWare4ValidRequest FeatureValue(string featureValue)

- WareReadSearchWare4ValidRequest WareStatusValue(string wareStatusValue)

- WareReadSearchWare4ValidRequest ItemNum(string itemNum)

- WareReadSearchWare4ValidRequest BarCode(string barCode)

- WareReadSearchWare4ValidRequest ColType(int colType)

- WareReadSearchWare4ValidRequest StartCreatedTime(string startCreatedTime)

- WareReadSearchWare4ValidRequest EndCreatedTime(string endCreatedTime)

- WareReadSearchWare4ValidRequest StartJdPrice(string startJdPrice)

- WareReadSearchWare4ValidRequest EndJdPrice(string endJdPrice)

- WareReadSearchWare4ValidRequest StartOnlineTime(string startOnlineTime)

- WareReadSearchWare4ValidRequest EndOnlineTime(string endOnlineTime)

- WareReadSearchWare4ValidRequest StartModifiedTime(string startModifiedTime)

- WareReadSearchWare4ValidRequest EndModifiedTime(string endModifiedTime)

- WareReadSearchWare4ValidRequest StartOfflineTime(string startOfflineTime)

- WareReadSearchWare4ValidRequest EndOfflineTime(string endOfflineTime)

- WareReadSearchWare4ValidRequest StartStockNum(long startStockNum)

- WareReadSearchWare4ValidRequest EndStockNum(long endStockNum)

- WareReadSearchWare4ValidRequest OrderField(string orderField)

- WareReadSearchWare4ValidRequest OrderType(string orderType)

- WareReadSearchWare4ValidRequest PageNo(int pageNo)

- WareReadSearchWare4ValidRequest PageSize(int pageSize)

- WareReadSearchWare4ValidRequest TransportId(long transportId)

- WareReadSearchWare4ValidRequest Claim(int claim)

- WareReadSearchWare4ValidRequest GroupId(long groupId)

- WareReadSearchWare4ValidRequest MultiCategoryId(long multiCategoryId)

- WareReadSearchWare4ValidRequest WarePropKey(string warePropKey)

- WareReadSearchWare4ValidRequest WarePropValue(string warePropValue)

- WareReadSearchWare4ValidRequest Field(string field)

- JdRequest Raw()


## WareReadSearchWare4ValidResponse (class)

- public Page page;

- public string Raw;


## WareWriteDeleteRequest (class)

- JdRequest req;

- public WareWriteDeleteRequest()

- WareWriteDeleteRequest WareId(long wareId)

- JdRequest Raw()


## WareWriteDeleteResponse (class)

- public bool success;

- public string Raw;


## WareWriteUpOrDownRequest (class)

- JdRequest req;

- public WareWriteUpOrDownRequest()

- WareWriteUpOrDownRequest Note(string note)

- WareWriteUpOrDownRequest WareId(long wareId)

- WareWriteUpOrDownRequest OpType(int opType)

- JdRequest Raw()


## WareWriteUpOrDownResponse (class)

- public bool success;

- public string Raw;


## WareWriteUpdateWareStatusByTimerRequest (class)

- JdRequest req;

- public WareWriteUpdateWareStatusByTimerRequest()

- WareWriteUpdateWareStatusByTimerRequest WareId(long wareId)

- WareWriteUpdateWareStatusByTimerRequest UpTime(long upTime)

- WareWriteUpdateWareStatusByTimerRequest DownTime(long downTime)

- JdRequest Raw()


## WareWriteUpdateWareStatusByTimerResponse (class)

- public bool success;

- public string Raw;


## WareWriteUpdateWareTitleRequest (class)

- JdRequest req;

- public WareWriteUpdateWareTitleRequest()

- WareWriteUpdateWareTitleRequest WareId(long wareId)

- WareWriteUpdateWareTitleRequest Title(string title)

- JdRequest Raw()


## WareWriteUpdateWareTitleResponse (class)

- public bool success;

- public string Raw;
