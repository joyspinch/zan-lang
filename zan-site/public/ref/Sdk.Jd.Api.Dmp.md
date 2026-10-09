# Sdk.Jd.Api.Dmp

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpCommonCrowdQueryRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpCommonSmartcrowdGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewCrowdAddRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewCrowdDelRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewCrowdDetailRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewCrowdEstimateCrowdNumByCrowdIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewResourceListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewTagDetailRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewTagInfoSkusRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewTagListV1Request.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewTagRecommendCategoryRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/DmpNewTagSetDetailRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Dmp/JdDmpApi.zan`


## DmpCommonCrowdQueryRequest (class)

- JdRequest req;

- public DmpCommonCrowdQueryRequest()

- DmpCommonCrowdQueryRequest AdGroupAdType(int adGroupAdType)

- DmpCommonCrowdQueryRequest AdGroupBidPrice(long adGroupBidPrice)

- DmpCommonCrowdQueryRequest AdGroupBillingType(int adGroupBillingType)

- DmpCommonCrowdQueryRequest AdGroupId(long adGroupId)

- DmpCommonCrowdQueryRequest BusinessType(int businessType)

- DmpCommonCrowdQueryRequest CrowdName(string crowdName)

- DmpCommonCrowdQueryRequest CrowdTabType(int crowdTabType)

- DmpCommonCrowdQueryRequest FirstSenceCategory(string firstSenceCategory)

- DmpCommonCrowdQueryRequest PageIndex(int pageIndex)

- DmpCommonCrowdQueryRequest PageSize(int pageSize)

- DmpCommonCrowdQueryRequest SecondSenceCategory(string secondSenceCategory)

- DmpCommonCrowdQueryRequest ResourcesList(string resourcesList)

- DmpCommonCrowdQueryRequest AccessPin(string accessPin)

- DmpCommonCrowdQueryRequest AuthType(string authType)

- JdRequest Raw()


## DmpCommonCrowdQueryResponse (class)

- public JsonCommonResponseCommonCrowdQuery data;

- public string Raw;


## DmpCommonSmartcrowdGetRequest (class)

- JdRequest req;

- public DmpCommonSmartcrowdGetRequest()

- DmpCommonSmartcrowdGetRequest AccessPin(string accessPin)

- DmpCommonSmartcrowdGetRequest AuthType(string authType)

- JdRequest Raw()


## DmpCommonSmartcrowdGetResponse (class)

- public JsonCommonResponseCommonSmartcrowdGet data;

- public string Raw;


## DmpNewCrowdAddRequest (class)

- JdRequest req;

- public DmpNewCrowdAddRequest()

- DmpNewCrowdAddRequest BoardIdsList(string boardIdsList)

- DmpNewCrowdAddRequest CrowdId(long crowdId)

- DmpNewCrowdAddRequest CrowdName(string crowdName)

- DmpNewCrowdAddRequest ExpiredTime(string expiredTime)

- DmpNewCrowdAddRequest GroupIds(string groupIds)

- DmpNewCrowdAddRequest AccessPin(string accessPin)

- DmpNewCrowdAddRequest AuthType(string authType)

- DmpNewCrowdAddRequest FrontendDimensionGroupsList(string frontendDimensionGroupsList)

- DmpNewCrowdAddRequest IsNewTagCompose(int isNewTagCompose)

- JdRequest Raw()


## DmpNewCrowdAddResponse (class)

- public JsonCommonResponseCrowdAdd data;

- public string Raw;


## DmpNewCrowdDelRequest (class)

- JdRequest req;

- public DmpNewCrowdDelRequest()

- DmpNewCrowdDelRequest AccessPin(string accessPin)

- DmpNewCrowdDelRequest AuthType(string authType)

- DmpNewCrowdDelRequest CrowdIds(string crowdIds)

- JdRequest Raw()


## DmpNewCrowdDelResponse (class)

- public JsonCommonResponseCrowdDel data;

- public string Raw;


## DmpNewCrowdDetailRequest (class)

- JdRequest req;

- public DmpNewCrowdDetailRequest()

- DmpNewCrowdDetailRequest AccessPin(string accessPin)

- DmpNewCrowdDetailRequest AuthType(string authType)

- DmpNewCrowdDetailRequest CrowdId(long crowdId)

- JdRequest Raw()


## DmpNewCrowdDetailResponse (class)

- public JsonCommonResponseCrowdDetail data;

- public string Raw;


## DmpNewCrowdEstimateCrowdNumByCrowdIdRequest (class)

- JdRequest req;

- public DmpNewCrowdEstimateCrowdNumByCrowdIdRequest()

- DmpNewCrowdEstimateCrowdNumByCrowdIdRequest CrowdId(long crowdId)

- DmpNewCrowdEstimateCrowdNumByCrowdIdRequest AccessPin(string accessPin)

- DmpNewCrowdEstimateCrowdNumByCrowdIdRequest AuthType(string authType)

- JdRequest Raw()


## DmpNewCrowdEstimateCrowdNumByCrowdIdResponse (class)

- public JsonCommonResponse data;

- public string Raw;


## DmpNewResourceListRequest (class)

- JdRequest req;

- public DmpNewResourceListRequest()

- DmpNewResourceListRequest AccessPin(string accessPin)

- DmpNewResourceListRequest AuthType(string authType)

- DmpNewResourceListRequest PageSize(int pageSize)

- DmpNewResourceListRequest PageIndex(int pageIndex)

- DmpNewResourceListRequest SeedStatus(int seedStatus)

- DmpNewResourceListRequest SeedName(string seedName)

- JdRequest Raw()


## DmpNewResourceListResponse (class)

- public JsonCommonResponseResourceList data;

- public string Raw;


## DmpNewTagDetailRequest (class)

- JdRequest req;

- public DmpNewTagDetailRequest()

- DmpNewTagDetailRequest AccessPin(string accessPin)

- DmpNewTagDetailRequest AuthType(string authType)

- DmpNewTagDetailRequest TagId(long tagId)

- DmpNewTagDetailRequest CrowdId(long crowdId)

- DmpNewTagDetailRequest IndustryHot(string industryHot)

- DmpNewTagDetailRequest CoverageRate(string coverageRate)

- DmpNewTagDetailRequest BoardId(long boardId)

- JdRequest Raw()


## DmpNewTagDetailResponse (class)

- public JsonCommonResponseTagDetail data;

- public string Raw;


## DmpNewTagInfoSkusRequest (class)

- JdRequest req;

- public DmpNewTagInfoSkusRequest()

- DmpNewTagInfoSkusRequest AccessPin(string accessPin)

- DmpNewTagInfoSkusRequest AuthType(string authType)

- DmpNewTagInfoSkusRequest SkuIds(long skuIds)

- DmpNewTagInfoSkusRequest Type(int type)

- JdRequest Raw()


## DmpNewTagInfoSkusResponse (class)

- public JsonCommonResponse data;

- public string Raw;


## DmpNewTagListV1Request (class)

- JdRequest req;

- public DmpNewTagListV1Request()

- DmpNewTagListV1Request AccessPin(string accessPin)

- DmpNewTagListV1Request AuthType(string authType)

- DmpNewTagListV1Request CategoryId(string categoryId)

- DmpNewTagListV1Request PageIndex(int pageIndex)

- DmpNewTagListV1Request PageSize(int pageSize)

- DmpNewTagListV1Request TagName(string tagName)

- DmpNewTagListV1Request SortType(int sortType)

- DmpNewTagListV1Request IsFavorite(int isFavorite)

- DmpNewTagListV1Request Level(int level)

- DmpNewTagListV1Request TagCategoryType(int tagCategoryType)

- JdRequest Raw()


## DmpNewTagListV1Response (class)

- public JsonCommonResponse data;

- public string Raw;


## DmpNewTagRecommendCategoryRequest (class)

- JdRequest req;

- public DmpNewTagRecommendCategoryRequest()

- DmpNewTagRecommendCategoryRequest Skus(string skus)

- DmpNewTagRecommendCategoryRequest ResourceType(int resourceType)

- DmpNewTagRecommendCategoryRequest CategoryLevel(int categoryLevel)

- DmpNewTagRecommendCategoryRequest CategoryMatchType(int categoryMatchType)

- DmpNewTagRecommendCategoryRequest RelevantDegreeType(int relevantDegreeType)

- DmpNewTagRecommendCategoryRequest AccessPin(string accessPin)

- DmpNewTagRecommendCategoryRequest AuthType(string authType)

- JdRequest Raw()


## DmpNewTagRecommendCategoryResponse (class)

- public JsonCommonResponse data;

- public string Raw;


## DmpNewTagSetDetailRequest (class)

- JdRequest req;

- public DmpNewTagSetDetailRequest()

- DmpNewTagSetDetailRequest TagId(long tagId)

- DmpNewTagSetDetailRequest CrowdId(long crowdId)

- DmpNewTagSetDetailRequest CommitAttributeList(string commitAttributeList)

- DmpNewTagSetDetailRequest CustomerDefineList(string customerDefineList)

- DmpNewTagSetDetailRequest ShopIdsList(string shopIdsList)

- DmpNewTagSetDetailRequest CategoryIdsList(string categoryIdsList)

- DmpNewTagSetDetailRequest SkusList(string skusList)

- DmpNewTagSetDetailRequest KeywordsList(string keywordsList)

- DmpNewTagSetDetailRequest KeywordsDescList(string keywordsDescList)

- DmpNewTagSetDetailRequest SeedIdsList(string seedIdsList)

- DmpNewTagSetDetailRequest BrandIdsList(string brandIdsList)

- DmpNewTagSetDetailRequest CidAndBrandList(string cidAndBrandList)

- DmpNewTagSetDetailRequest PriceAttributesList(string priceAttributesList)

- DmpNewTagSetDetailRequest CampaignIdsList(string campaignIdsList)

- DmpNewTagSetDetailRequest CampaignDescsList(string campaignDescsList)

- DmpNewTagSetDetailRequest FrequencyBeginValue(long frequencyBeginValue)

- DmpNewTagSetDetailRequest FrequencyEndValue(long frequencyEndValue)

- DmpNewTagSetDetailRequest SkuAttributeList(string skuAttributeList)

- DmpNewTagSetDetailRequest ExternalDescList(string externalDescList)

- DmpNewTagSetDetailRequest ExternalValueList(string externalValueList)

- DmpNewTagSetDetailRequest ResourceType(int resourceType)

- DmpNewTagSetDetailRequest CategoryLevel(int categoryLevel)

- DmpNewTagSetDetailRequest AccessPin(string accessPin)

- DmpNewTagSetDetailRequest AuthType(string authType)

- DmpNewTagSetDetailRequest AttributeList(string attributeList)

- DmpNewTagSetDetailRequest ShopIdsDescList(string shopIdsDescList)

- DmpNewTagSetDetailRequest BrandIdsDescList(string brandIdsDescList)

- DmpNewTagSetDetailRequest DynamicRule(int dynamicRule)

- DmpNewTagSetDetailRequest CompeteGoodsType(int competeGoodsType)

- JdRequest Raw()


## DmpNewTagSetDetailResponse (class)

- public JsonCommonResponseTagSetDetail data;

- public string Raw;


## JdDmpApi (class)

- JdClient client;

- public JdDmpApi(JdClient client)

- async DmpCommonCrowdQueryResponse CommonCrowdQueryAsync(DmpCommonCrowdQueryRequest request)

- async DmpCommonSmartcrowdGetResponse CommonSmartcrowdGetAsync(DmpCommonSmartcrowdGetRequest request)

- async DmpNewCrowdAddResponse NewCrowdAddAsync(DmpNewCrowdAddRequest request)

- async DmpNewCrowdDelResponse NewCrowdDelAsync(DmpNewCrowdDelRequest request)

- async DmpNewCrowdDetailResponse NewCrowdDetailAsync(DmpNewCrowdDetailRequest request)

- async DmpNewCrowdEstimateCrowdNumByCrowdIdResponse NewCrowdEstimateCrowdNumByCrowdIdAsync(DmpNewCrowdEstimateCrowdNumByCrowdIdRequest request)

- async DmpNewResourceListResponse NewResourceListAsync(DmpNewResourceListRequest request)

- async DmpNewTagDetailResponse NewTagDetailAsync(DmpNewTagDetailRequest request)

- async DmpNewTagInfoSkusResponse NewTagInfoSkusAsync(DmpNewTagInfoSkusRequest request)

- async DmpNewTagListV1Response NewTagListV1Async(DmpNewTagListV1Request request)

- async DmpNewTagRecommendCategoryResponse NewTagRecommendCategoryAsync(DmpNewTagRecommendCategoryRequest request)

- async DmpNewTagSetDetailResponse NewTagSetDetailAsync(DmpNewTagSetDetailRequest request)
