# Sdk.Jd.Api.Dmp

> 源码: `stdlib/Sdk/Jd/Api/Dmp/DmpCommonCrowdQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpCommonSmartcrowdGetRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewCrowdAddRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewCrowdDelRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewCrowdDetailRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewCrowdEstimateCrowdNumByCrowdIdRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewResourceListRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewTagDetailRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewTagInfoSkusRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewTagListV1Request.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewTagRecommendCategoryRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/DmpNewTagSetDetailRequest.zan`, `stdlib/Sdk/Jd/Api/Dmp/JdDmpApi.zan`


## DmpCommonCrowdQueryRequest (class)

<c>jingdong.dmp.common.crowd.query</c> 的请求。

- JdRequest req;

- public DmpCommonCrowdQueryRequest()

- DmpCommonCrowdQueryRequest AdGroupAdType(int adGroupAdType)
  - 设置 <c>adGroupAdType</c> 参数。

- DmpCommonCrowdQueryRequest AdGroupBidPrice(long adGroupBidPrice)
  - 设置 <c>adGroupBidPrice</c> 参数。

- DmpCommonCrowdQueryRequest AdGroupBillingType(int adGroupBillingType)
  - 设置 <c>adGroupBillingType</c> 参数。

- DmpCommonCrowdQueryRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- DmpCommonCrowdQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- DmpCommonCrowdQueryRequest CrowdName(string crowdName)
  - 设置 <c>crowdName</c> 参数。

- DmpCommonCrowdQueryRequest CrowdTabType(int crowdTabType)
  - 设置 <c>crowdTabType</c> 参数。

- DmpCommonCrowdQueryRequest FirstSenceCategory(string firstSenceCategory)
  - 设置 <c>firstSenceCategory</c> 参数。

- DmpCommonCrowdQueryRequest PageIndex(int pageIndex)
  - 设置 <c>pageIndex</c> 参数。

- DmpCommonCrowdQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- DmpCommonCrowdQueryRequest SecondSenceCategory(string secondSenceCategory)
  - 设置 <c>secondSenceCategory</c> 参数。

- DmpCommonCrowdQueryRequest ResourcesList(string resourcesList)
  - 设置 <c>resourcesList</c> 参数。

- DmpCommonCrowdQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpCommonCrowdQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpCommonCrowdQueryResponse (class)

<c>jingdong.dmp.common.crowd.query</c> 的响应。

- public JsonCommonResponseCommonCrowdQuery data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpCommonSmartcrowdGetRequest (class)

<c>jingdong.dmp.common.smartcrowd.get</c> 的请求。

- JdRequest req;

- public DmpCommonSmartcrowdGetRequest()

- DmpCommonSmartcrowdGetRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpCommonSmartcrowdGetRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpCommonSmartcrowdGetResponse (class)

<c>jingdong.dmp.common.smartcrowd.get</c> 的响应。

- public JsonCommonResponseCommonSmartcrowdGet data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewCrowdAddRequest (class)

<c>jingdong.dmp.new.crowd.add</c> 的请求。

- JdRequest req;

- public DmpNewCrowdAddRequest()

- DmpNewCrowdAddRequest BoardIdsList(string boardIdsList)
  - 设置 <c>boardIdsList</c> 参数。

- DmpNewCrowdAddRequest CrowdId(long crowdId)
  - 设置 <c>crowdId</c> 参数。

- DmpNewCrowdAddRequest CrowdName(string crowdName)
  - 设置 <c>crowdName</c> 参数。

- DmpNewCrowdAddRequest ExpiredTime(string expiredTime)
  - 设置 <c>expiredTime</c> 参数。

- DmpNewCrowdAddRequest GroupIds(string groupIds)
  - 设置 <c>groupIds</c> 参数。

- DmpNewCrowdAddRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewCrowdAddRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- DmpNewCrowdAddRequest FrontendDimensionGroupsList(string frontendDimensionGroupsList)
  - 设置 <c>frontendDimensionGroupsList</c> 参数。

- DmpNewCrowdAddRequest IsNewTagCompose(int isNewTagCompose)
  - 设置 <c>isNewTagCompose</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewCrowdAddResponse (class)

<c>jingdong.dmp.new.crowd.add</c> 的响应。

- public JsonCommonResponseCrowdAdd data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewCrowdDelRequest (class)

<c>jingdong.dmp.new.crowd.del</c> 的请求。

- JdRequest req;

- public DmpNewCrowdDelRequest()

- DmpNewCrowdDelRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewCrowdDelRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- DmpNewCrowdDelRequest CrowdIds(string crowdIds)
  - 设置 <c>crowdIds</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewCrowdDelResponse (class)

<c>jingdong.dmp.new.crowd.del</c> 的响应。

- public JsonCommonResponseCrowdDel data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewCrowdDetailRequest (class)

<c>jingdong.dmp.new.crowd.detail</c> 的请求。

- JdRequest req;

- public DmpNewCrowdDetailRequest()

- DmpNewCrowdDetailRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewCrowdDetailRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- DmpNewCrowdDetailRequest CrowdId(long crowdId)
  - 设置 <c>crowdId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewCrowdDetailResponse (class)

<c>jingdong.dmp.new.crowd.detail</c> 的响应。

- public JsonCommonResponseCrowdDetail data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewCrowdEstimateCrowdNumByCrowdIdRequest (class)

<c>jingdong.dmp.new.crowd.estimateCrowdNumByCrowdId</c> 的请求。

- JdRequest req;

- public DmpNewCrowdEstimateCrowdNumByCrowdIdRequest()

- DmpNewCrowdEstimateCrowdNumByCrowdIdRequest CrowdId(long crowdId)
  - 设置 <c>crowdId</c> 参数。

- DmpNewCrowdEstimateCrowdNumByCrowdIdRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewCrowdEstimateCrowdNumByCrowdIdRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewCrowdEstimateCrowdNumByCrowdIdResponse (class)

<c>jingdong.dmp.new.crowd.estimateCrowdNumByCrowdId</c> 的响应。

- public JsonCommonResponse data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewResourceListRequest (class)

<c>jingdong.dmp.new.resource.list</c> 的请求。

- JdRequest req;

- public DmpNewResourceListRequest()

- DmpNewResourceListRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewResourceListRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- DmpNewResourceListRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- DmpNewResourceListRequest PageIndex(int pageIndex)
  - 设置 <c>pageIndex</c> 参数。

- DmpNewResourceListRequest SeedStatus(int seedStatus)
  - 设置 <c>seedStatus</c> 参数。

- DmpNewResourceListRequest SeedName(string seedName)
  - 设置 <c>seedName</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewResourceListResponse (class)

<c>jingdong.dmp.new.resource.list</c> 的响应。

- public JsonCommonResponseResourceList data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewTagDetailRequest (class)

<c>jingdong.dmp.new.tag.detail</c> 的请求。

- JdRequest req;

- public DmpNewTagDetailRequest()

- DmpNewTagDetailRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewTagDetailRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- DmpNewTagDetailRequest TagId(long tagId)
  - 设置 <c>tagId</c> 参数。

- DmpNewTagDetailRequest CrowdId(long crowdId)
  - 设置 <c>crowdId</c> 参数。

- DmpNewTagDetailRequest IndustryHot(string industryHot)
  - 设置 <c>industryHot</c> 参数。

- DmpNewTagDetailRequest CoverageRate(string coverageRate)
  - 设置 <c>coverageRate</c> 参数。

- DmpNewTagDetailRequest BoardId(long boardId)
  - 设置 <c>boardId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewTagDetailResponse (class)

<c>jingdong.dmp.new.tag.detail</c> 的响应。

- public JsonCommonResponseTagDetail data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewTagInfoSkusRequest (class)

<c>jingdong.dmp.new.tag.info.skus</c> 的请求。

- JdRequest req;

- public DmpNewTagInfoSkusRequest()

- DmpNewTagInfoSkusRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewTagInfoSkusRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- DmpNewTagInfoSkusRequest SkuIds(long skuIds)
  - 设置 <c>skuIds</c> 参数。

- DmpNewTagInfoSkusRequest Type(int type)
  - 设置 <c>type</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewTagInfoSkusResponse (class)

<c>jingdong.dmp.new.tag.info.skus</c> 的响应。

- public JsonCommonResponse data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewTagListV1Request (class)

<c>jingdong.dmp.new.tag.list.v1</c> 的请求。

- JdRequest req;

- public DmpNewTagListV1Request()

- DmpNewTagListV1Request AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewTagListV1Request AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- DmpNewTagListV1Request CategoryId(string categoryId)
  - 设置 <c>categoryId</c> 参数。

- DmpNewTagListV1Request PageIndex(int pageIndex)
  - 设置 <c>pageIndex</c> 参数。

- DmpNewTagListV1Request PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- DmpNewTagListV1Request TagName(string tagName)
  - 设置 <c>tagName</c> 参数。

- DmpNewTagListV1Request SortType(int sortType)
  - 设置 <c>sortType</c> 参数。

- DmpNewTagListV1Request IsFavorite(int isFavorite)
  - 设置 <c>isFavorite</c> 参数。

- DmpNewTagListV1Request Level(int level)
  - 设置 <c>level</c> 参数。

- DmpNewTagListV1Request TagCategoryType(int tagCategoryType)
  - 设置 <c>tagCategoryType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewTagListV1Response (class)

<c>jingdong.dmp.new.tag.list.v1</c> 的响应。

- public JsonCommonResponse data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewTagRecommendCategoryRequest (class)

<c>jingdong.dmp.new.tag.recommend.category</c> 的请求。

- JdRequest req;

- public DmpNewTagRecommendCategoryRequest()

- DmpNewTagRecommendCategoryRequest Skus(string skus)
  - 设置 <c>skus</c> 参数。

- DmpNewTagRecommendCategoryRequest ResourceType(int resourceType)
  - 设置 <c>resourceType</c> 参数。

- DmpNewTagRecommendCategoryRequest CategoryLevel(int categoryLevel)
  - 设置 <c>categoryLevel</c> 参数。

- DmpNewTagRecommendCategoryRequest CategoryMatchType(int categoryMatchType)
  - 设置 <c>categoryMatchType</c> 参数。

- DmpNewTagRecommendCategoryRequest RelevantDegreeType(int relevantDegreeType)
  - 设置 <c>relevantDegreeType</c> 参数。

- DmpNewTagRecommendCategoryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewTagRecommendCategoryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewTagRecommendCategoryResponse (class)

<c>jingdong.dmp.new.tag.recommend.category</c> 的响应。

- public JsonCommonResponse data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## DmpNewTagSetDetailRequest (class)

<c>jingdong.dmp.new.tag.set.detail</c> 的请求。

- JdRequest req;

- public DmpNewTagSetDetailRequest()

- DmpNewTagSetDetailRequest TagId(long tagId)
  - 设置 <c>tagId</c> 参数。

- DmpNewTagSetDetailRequest CrowdId(long crowdId)
  - 设置 <c>crowdId</c> 参数。

- DmpNewTagSetDetailRequest CommitAttributeList(string commitAttributeList)
  - 设置 <c>commitAttributeList</c> 参数。

- DmpNewTagSetDetailRequest CustomerDefineList(string customerDefineList)
  - 设置 <c>customerDefineList</c> 参数。

- DmpNewTagSetDetailRequest ShopIdsList(string shopIdsList)
  - 设置 <c>shopIdsList</c> 参数。

- DmpNewTagSetDetailRequest CategoryIdsList(string categoryIdsList)
  - 设置 <c>categoryIdsList</c> 参数。

- DmpNewTagSetDetailRequest SkusList(string skusList)
  - 设置 <c>skusList</c> 参数。

- DmpNewTagSetDetailRequest KeywordsList(string keywordsList)
  - 设置 <c>keywordsList</c> 参数。

- DmpNewTagSetDetailRequest KeywordsDescList(string keywordsDescList)
  - 设置 <c>keywordsDescList</c> 参数。

- DmpNewTagSetDetailRequest SeedIdsList(string seedIdsList)
  - 设置 <c>seedIdsList</c> 参数。

- DmpNewTagSetDetailRequest BrandIdsList(string brandIdsList)
  - 设置 <c>brandIdsList</c> 参数。

- DmpNewTagSetDetailRequest CidAndBrandList(string cidAndBrandList)
  - 设置 <c>cidAndBrandList</c> 参数。

- DmpNewTagSetDetailRequest PriceAttributesList(string priceAttributesList)
  - 设置 <c>priceAttributesList</c> 参数。

- DmpNewTagSetDetailRequest CampaignIdsList(string campaignIdsList)
  - 设置 <c>campaignIdsList</c> 参数。

- DmpNewTagSetDetailRequest CampaignDescsList(string campaignDescsList)
  - 设置 <c>campaignDescsList</c> 参数。

- DmpNewTagSetDetailRequest FrequencyBeginValue(long frequencyBeginValue)
  - 设置 <c>frequencyBeginValue</c> 参数。

- DmpNewTagSetDetailRequest FrequencyEndValue(long frequencyEndValue)
  - 设置 <c>frequencyEndValue</c> 参数。

- DmpNewTagSetDetailRequest SkuAttributeList(string skuAttributeList)
  - 设置 <c>skuAttributeList</c> 参数。

- DmpNewTagSetDetailRequest ExternalDescList(string externalDescList)
  - 设置 <c>externalDescList</c> 参数。

- DmpNewTagSetDetailRequest ExternalValueList(string externalValueList)
  - 设置 <c>externalValueList</c> 参数。

- DmpNewTagSetDetailRequest ResourceType(int resourceType)
  - 设置 <c>resourceType</c> 参数。

- DmpNewTagSetDetailRequest CategoryLevel(int categoryLevel)
  - 设置 <c>categoryLevel</c> 参数。

- DmpNewTagSetDetailRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- DmpNewTagSetDetailRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- DmpNewTagSetDetailRequest AttributeList(string attributeList)
  - 设置 <c>attributeList</c> 参数。

- DmpNewTagSetDetailRequest ShopIdsDescList(string shopIdsDescList)
  - 设置 <c>shopIdsDescList</c> 参数。

- DmpNewTagSetDetailRequest BrandIdsDescList(string brandIdsDescList)
  - 设置 <c>brandIdsDescList</c> 参数。

- DmpNewTagSetDetailRequest DynamicRule(int dynamicRule)
  - 设置 <c>dynamicRule</c> 参数。

- DmpNewTagSetDetailRequest CompeteGoodsType(int competeGoodsType)
  - 设置 <c>competeGoodsType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DmpNewTagSetDetailResponse (class)

<c>jingdong.dmp.new.tag.set.detail</c> 的响应。

- public JsonCommonResponseTagSetDetail data;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdDmpApi (class)

jingdong.dmp.* 的强类型客户端。

- JdClient client;

- public JdDmpApi(JdClient client)

- async DmpCommonCrowdQueryResponse CommonCrowdQueryAsync(DmpCommonCrowdQueryRequest request)
  - 执行 <c>jingdong.dmp.common.crowd.query</c>。

- async DmpCommonSmartcrowdGetResponse CommonSmartcrowdGetAsync(DmpCommonSmartcrowdGetRequest request)
  - 执行 <c>jingdong.dmp.common.smartcrowd.get</c>。

- async DmpNewCrowdAddResponse NewCrowdAddAsync(DmpNewCrowdAddRequest request)
  - 执行 <c>jingdong.dmp.new.crowd.add</c>。

- async DmpNewCrowdDelResponse NewCrowdDelAsync(DmpNewCrowdDelRequest request)
  - 执行 <c>jingdong.dmp.new.crowd.del</c>。

- async DmpNewCrowdDetailResponse NewCrowdDetailAsync(DmpNewCrowdDetailRequest request)
  - 执行 <c>jingdong.dmp.new.crowd.detail</c>。

- async DmpNewCrowdEstimateCrowdNumByCrowdIdResponse NewCrowdEstimateCrowdNumByCrowdIdAsync(DmpNewCrowdEstimateCrowdNumByCrowdIdRequest request)
  - 执行 <c>jingdong.dmp.new.crowd.estimateCrowdNumByCrowdId</c>。

- async DmpNewResourceListResponse NewResourceListAsync(DmpNewResourceListRequest request)
  - 执行 <c>jingdong.dmp.new.resource.list</c>。

- async DmpNewTagDetailResponse NewTagDetailAsync(DmpNewTagDetailRequest request)
  - 执行 <c>jingdong.dmp.new.tag.detail</c>。

- async DmpNewTagInfoSkusResponse NewTagInfoSkusAsync(DmpNewTagInfoSkusRequest request)
  - 执行 <c>jingdong.dmp.new.tag.info.skus</c>。

- async DmpNewTagListV1Response NewTagListV1Async(DmpNewTagListV1Request request)
  - 执行 <c>jingdong.dmp.new.tag.list.v1</c>。

- async DmpNewTagRecommendCategoryResponse NewTagRecommendCategoryAsync(DmpNewTagRecommendCategoryRequest request)
  - 执行 <c>jingdong.dmp.new.tag.recommend.category</c>。

- async DmpNewTagSetDetailResponse NewTagSetDetailAsync(DmpNewTagSetDetailRequest request)
  - 执行 <c>jingdong.dmp.new.tag.set.detail</c>。
