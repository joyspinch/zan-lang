# Sdk.Jd.Api.Ads

> 源码: `stdlib/Sdk/Jd/Api/Ads/AdsDspMaterialOpenapiDeleteByMaterialIdRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspMaterialOpenapiFindMaterialByIdRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspMaterialOpenapiGetMaterialListRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspMaterialOpenapiGetMaterialResourceRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedAddCampaignRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedBindCrowdRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedBindMaterialRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedDeleteCampaignRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedGetPosPackageListRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedPosPackageListRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedUpdateCampaignNameRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedUpdateCampaignStatusRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedUpdateDateRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbFeaturedUpdateTimeRangePriceCoefRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbHtListCampaignsByPinRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbKeywordFindKeyWordHistoryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbKuaicheQueryPriceSuggestRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbShopkeywordListRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbTpBatchDeleteDmpRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbTpBatchUpdateAdGroupStatusRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbTpBatchUpdateDmpPriceRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbTpBatchUpdatePosPackageCoefRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbTpUpdateAdGroupNameRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbTpUpdateDateRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspRtbTpUpdateTimeRangePriceCoefRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsDspTraceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceAccountQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceAdQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceAgentActivityV1Request.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceAgentDetailV1Request.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceCampaignQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceDmpDetailV1Request.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceDmpListV1Request.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceGroupQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceKeywordQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceLocationQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceOrderQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServicePackageQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceSearchWordQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceSkuDeliveryQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceSkuQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/AdsIbgUniversalJosServiceTrendQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Ads/JdAdsApi.zan`


## AdsDspMaterialOpenapiDeleteByMaterialIdRequest (class)

<c>jingdong.ads.dsp.material.openapi.deleteByMaterialId</c> 的请求。

- JdRequest req;

- public AdsDspMaterialOpenapiDeleteByMaterialIdRequest()

- AdsDspMaterialOpenapiDeleteByMaterialIdRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspMaterialOpenapiDeleteByMaterialIdRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspMaterialOpenapiDeleteByMaterialIdRequest MaterialId(long materialId)
  - 设置 <c>materialId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspMaterialOpenapiDeleteByMaterialIdResponse (class)

<c>jingdong.ads.dsp.material.openapi.deleteByMaterialId</c> 的响应。

- public CreativeResult returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspMaterialOpenapiFindMaterialByIdRequest (class)

<c>jingdong.ads.dsp.material.openapi.findMaterialById</c> 的请求。

- JdRequest req;

- public AdsDspMaterialOpenapiFindMaterialByIdRequest()

- AdsDspMaterialOpenapiFindMaterialByIdRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspMaterialOpenapiFindMaterialByIdRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspMaterialOpenapiFindMaterialByIdRequest MaterialId(long materialId)
  - 设置 <c>materialId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspMaterialOpenapiFindMaterialByIdResponse (class)

<c>jingdong.ads.dsp.material.openapi.findMaterialById</c> 的响应。

- public CreativeResult returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspMaterialOpenapiGetMaterialListRequest (class)

<c>jingdong.ads.dsp.material.openapi.getMaterialList</c> 的请求。

- JdRequest req;

- public AdsDspMaterialOpenapiGetMaterialListRequest()

- AdsDspMaterialOpenapiGetMaterialListRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspMaterialOpenapiGetMaterialListRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspMaterialOpenapiGetMaterialListRequest Effective(int effective)
  - 设置 <c>effective</c> 参数。

- AdsDspMaterialOpenapiGetMaterialListRequest PageIndex(int pageIndex)
  - 设置 <c>pageIndex</c> 参数。

- AdsDspMaterialOpenapiGetMaterialListRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsDspMaterialOpenapiGetMaterialListRequest BusinessType(long businessType)
  - 设置 <c>businessType</c> 参数。

- AdsDspMaterialOpenapiGetMaterialListRequest Status(int status)
  - 设置 <c>status</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspMaterialOpenapiGetMaterialListResponse (class)

<c>jingdong.ads.dsp.material.openapi.getMaterialList</c> 的响应。

- public CreativeResult returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspMaterialOpenapiGetMaterialResourceRequest (class)

<c>jingdong.ads.dsp.material.openapi.getMaterialResource</c> 的请求。

- JdRequest req;

- public AdsDspMaterialOpenapiGetMaterialResourceRequest()

- AdsDspMaterialOpenapiGetMaterialResourceRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspMaterialOpenapiGetMaterialResourceRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspMaterialOpenapiGetMaterialResourceResponse (class)

<c>jingdong.ads.dsp.material.openapi.getMaterialResource</c> 的响应。

- public CreativeResult returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedAddCampaignRequest (class)

<c>jingdong.ads.dsp.rtb.featured.addCampaign</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedAddCampaignRequest()

- AdsDspRtbFeaturedAddCampaignRequest StartTime(string startTime)
  - 设置 <c>startTime</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest EndTime(string endTime)
  - 设置 <c>endTime</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest DayBudget(long dayBudget)
  - 设置 <c>dayBudget</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest CampaignType(int campaignType)
  - 设置 <c>campaignType</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest Name(string name)
  - 设置 <c>name</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest Pin(string pin)
  - 设置 <c>pin</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest VenderId(string venderId)
  - 设置 <c>venderId</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest JosRemoteIp(string josRemoteIp)
  - 设置 <c>josRemoteIp</c> 参数。

- AdsDspRtbFeaturedAddCampaignRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedAddCampaignResponse (class)

<c>jingdong.ads.dsp.rtb.featured.addCampaign</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedBindCrowdRequest (class)

<c>jingdong.ads.dsp.rtb.featured.bindCrowd</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedBindCrowdRequest()

- AdsDspRtbFeaturedBindCrowdRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- AdsDspRtbFeaturedBindCrowdRequest CrowdId(string crowdId)
  - 设置 <c>crowdId</c> 参数。

- AdsDspRtbFeaturedBindCrowdRequest AdGroupPrice(string adGroupPrice)
  - 设置 <c>adGroupPrice</c> 参数。

- AdsDspRtbFeaturedBindCrowdRequest IsUsed(string isUsed)
  - 设置 <c>isUsed</c> 参数。

- AdsDspRtbFeaturedBindCrowdRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedBindCrowdRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedBindCrowdRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedBindCrowdResponse (class)

<c>jingdong.ads.dsp.rtb.featured.bindCrowd</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedBindMaterialRequest (class)

<c>jingdong.ads.dsp.rtb.featured.bindMaterial</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedBindMaterialRequest()

- AdsDspRtbFeaturedBindMaterialRequest MaterialIds(string materialIds)
  - 设置 <c>materialIds</c> 参数。

- AdsDspRtbFeaturedBindMaterialRequest AdGroupIds(string adGroupIds)
  - 设置 <c>adGroupIds</c> 参数。

- AdsDspRtbFeaturedBindMaterialRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedBindMaterialRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedBindMaterialRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedBindMaterialResponse (class)

<c>jingdong.ads.dsp.rtb.featured.bindMaterial</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedDeleteCampaignRequest (class)

<c>jingdong.ads.dsp.rtb.featured.deleteCampaign</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedDeleteCampaignRequest()

- AdsDspRtbFeaturedDeleteCampaignRequest Ids(string ids)
  - 设置 <c>ids</c> 参数。

- AdsDspRtbFeaturedDeleteCampaignRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedDeleteCampaignRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedDeleteCampaignRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedDeleteCampaignResponse (class)

<c>jingdong.ads.dsp.rtb.featured.deleteCampaign</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedGetPosPackageListRequest (class)

<c>jingdong.ads.dsp.rtb.featured.getPosPackageList</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedGetPosPackageListRequest()

- AdsDspRtbFeaturedGetPosPackageListRequest CampaignType(int campaignType)
  - 设置 <c>campaignType</c> 参数。

- AdsDspRtbFeaturedGetPosPackageListRequest Device(int device)
  - 设置 <c>device</c> 参数。

- AdsDspRtbFeaturedGetPosPackageListRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedGetPosPackageListRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedGetPosPackageListRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedGetPosPackageListResponse (class)

<c>jingdong.ads.dsp.rtb.featured.getPosPackageList</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedPosPackageListRequest (class)

<c>jingdong.ads.dsp.rtb.featured.posPackageList</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedPosPackageListRequest()

- AdsDspRtbFeaturedPosPackageListRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest CampaignType(int campaignType)
  - 设置 <c>campaignType</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest ImpressionOrClickEffect(int impressionOrClickEffect)
  - 设置 <c>impressionOrClickEffect</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest Status(int status)
  - 设置 <c>status</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest Platform(string platform)
  - 设置 <c>platform</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedPosPackageListRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedPosPackageListResponse (class)

<c>jingdong.ads.dsp.rtb.featured.posPackageList</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedUpdateCampaignNameRequest (class)

<c>jingdong.ads.dsp.rtb.featured.updateCampaignName</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedUpdateCampaignNameRequest()

- AdsDspRtbFeaturedUpdateCampaignNameRequest Id(long id)
  - 设置 <c>id</c> 参数。

- AdsDspRtbFeaturedUpdateCampaignNameRequest Name(string name)
  - 设置 <c>name</c> 参数。

- AdsDspRtbFeaturedUpdateCampaignNameRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedUpdateCampaignNameRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedUpdateCampaignNameRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedUpdateCampaignNameResponse (class)

<c>jingdong.ads.dsp.rtb.featured.updateCampaignName</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedUpdateCampaignStatusRequest (class)

<c>jingdong.ads.dsp.rtb.featured.updateCampaignStatus</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedUpdateCampaignStatusRequest()

- AdsDspRtbFeaturedUpdateCampaignStatusRequest Ids(string ids)
  - 设置 <c>ids</c> 参数。

- AdsDspRtbFeaturedUpdateCampaignStatusRequest OperateType(int operateType)
  - 设置 <c>operateType</c> 参数。

- AdsDspRtbFeaturedUpdateCampaignStatusRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedUpdateCampaignStatusRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedUpdateCampaignStatusRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedUpdateCampaignStatusResponse (class)

<c>jingdong.ads.dsp.rtb.featured.updateCampaignStatus</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedUpdateDateRequest (class)

<c>jingdong.ads.dsp.rtb.featured.updateDate</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedUpdateDateRequest()

- AdsDspRtbFeaturedUpdateDateRequest Id(long id)
  - 设置 <c>id</c> 参数。

- AdsDspRtbFeaturedUpdateDateRequest StartTime(string startTime)
  - 设置 <c>startTime</c> 参数。

- AdsDspRtbFeaturedUpdateDateRequest EndTime(string endTime)
  - 设置 <c>endTime</c> 参数。

- AdsDspRtbFeaturedUpdateDateRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedUpdateDateRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedUpdateDateRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedUpdateDateResponse (class)

<c>jingdong.ads.dsp.rtb.featured.updateDate</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbFeaturedUpdateTimeRangePriceCoefRequest (class)

<c>jingdong.ads.dsp.rtb.featured.updateTimeRangePriceCoef</c> 的请求。

- JdRequest req;

- public AdsDspRtbFeaturedUpdateTimeRangePriceCoefRequest()

- AdsDspRtbFeaturedUpdateTimeRangePriceCoefRequest Id(long id)
  - 设置 <c>id</c> 参数。

- AdsDspRtbFeaturedUpdateTimeRangePriceCoefRequest TimeRangePriceCoef(string timeRangePriceCoef)
  - 设置 <c>timeRangePriceCoef</c> 参数。

- AdsDspRtbFeaturedUpdateTimeRangePriceCoefRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbFeaturedUpdateTimeRangePriceCoefRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbFeaturedUpdateTimeRangePriceCoefRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbFeaturedUpdateTimeRangePriceCoefResponse (class)

<c>jingdong.ads.dsp.rtb.featured.updateTimeRangePriceCoef</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbHtListCampaignsByPinRequest (class)

<c>jingdong.ads.dsp.rtb.ht.listCampaignsByPin</c> 的请求。

- JdRequest req;

- public AdsDspRtbHtListCampaignsByPinRequest()

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbHtListCampaignsByPinResponse (class)

<c>jingdong.ads.dsp.rtb.ht.listCampaignsByPin</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbKeywordFindKeyWordHistoryRequest (class)

<c>jingdong.ads.dsp.rtb.keyword.findKeyWordHistory</c> 的请求。

- JdRequest req;

- public AdsDspRtbKeywordFindKeyWordHistoryRequest()

- AdsDspRtbKeywordFindKeyWordHistoryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbKeywordFindKeyWordHistoryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbKeywordFindKeyWordHistoryRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbKeywordFindKeyWordHistoryResponse (class)

<c>jingdong.ads.dsp.rtb.keyword.findKeyWordHistory</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefRequest (class)

<c>jingdong.ads.dsp.rtb.kuaiche.campaign.updateTimeRangePriceCoef</c> 的请求。

- JdRequest req;

- public AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefRequest()

- AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefRequest Id(long id)
  - 设置 <c>id</c> 参数。

- AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefRequest TimeRangePriceCoef(string timeRangePriceCoef)
  - 设置 <c>timeRangePriceCoef</c> 参数。

- AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefResponse (class)

<c>jingdong.ads.dsp.rtb.kuaiche.campaign.updateTimeRangePriceCoef</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbKuaicheQueryPriceSuggestRequest (class)

<c>jingdong.ads.dsp.rtb.kuaiche.queryPriceSuggest</c> 的请求。

- JdRequest req;

- public AdsDspRtbKuaicheQueryPriceSuggestRequest()

- AdsDspRtbKuaicheQueryPriceSuggestRequest Type(int type)
  - 设置 <c>type</c> 参数。

- AdsDspRtbKuaicheQueryPriceSuggestRequest MobileType(int mobileType)
  - 设置 <c>mobileType</c> 参数。

- AdsDspRtbKuaicheQueryPriceSuggestRequest Key(string key)
  - 设置 <c>key</c> 参数。

- AdsDspRtbKuaicheQueryPriceSuggestRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbKuaicheQueryPriceSuggestRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbKuaicheQueryPriceSuggestResponse (class)

<c>jingdong.ads.dsp.rtb.kuaiche.queryPriceSuggest</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbShopkeywordListRequest (class)

<c>jingdong.ads.dsp.rtb.shopkeyword.list</c> 的请求。

- JdRequest req;

- public AdsDspRtbShopkeywordListRequest()

- AdsDspRtbShopkeywordListRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsDspRtbShopkeywordListRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsDspRtbShopkeywordListRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsDspRtbShopkeywordListRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsDspRtbShopkeywordListRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsDspRtbShopkeywordListRequest GroupId(long groupId)
  - 设置 <c>groupId</c> 参数。

- AdsDspRtbShopkeywordListRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsDspRtbShopkeywordListRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsDspRtbShopkeywordListRequest Platform(string platform)
  - 设置 <c>platform</c> 参数。

- AdsDspRtbShopkeywordListRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsDspRtbShopkeywordListRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsDspRtbShopkeywordListRequest NameLike(string nameLike)
  - 设置 <c>nameLike</c> 参数。

- AdsDspRtbShopkeywordListRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbShopkeywordListRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbShopkeywordListRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbShopkeywordListResponse (class)

<c>jingdong.ads.dsp.rtb.shopkeyword.list</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbTpBatchDeleteDmpRequest (class)

<c>jingdong.ads.dsp.rtb.tp.batchDeleteDmp</c> 的请求。

- JdRequest req;

- public AdsDspRtbTpBatchDeleteDmpRequest()

- AdsDspRtbTpBatchDeleteDmpRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- AdsDspRtbTpBatchDeleteDmpRequest Ids(string ids)
  - 设置 <c>ids</c> 参数。

- AdsDspRtbTpBatchDeleteDmpRequest DmpType(int dmpType)
  - 设置 <c>dmpType</c> 参数。

- AdsDspRtbTpBatchDeleteDmpRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbTpBatchDeleteDmpRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbTpBatchDeleteDmpRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbTpBatchDeleteDmpResponse (class)

<c>jingdong.ads.dsp.rtb.tp.batchDeleteDmp</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbTpBatchUpdateAdGroupStatusRequest (class)

<c>jingdong.ads.dsp.rtb.tp.batchUpdateAdGroupStatus</c> 的请求。

- JdRequest req;

- public AdsDspRtbTpBatchUpdateAdGroupStatusRequest()

- AdsDspRtbTpBatchUpdateAdGroupStatusRequest Ids(string ids)
  - 设置 <c>ids</c> 参数。

- AdsDspRtbTpBatchUpdateAdGroupStatusRequest Status(int status)
  - 设置 <c>status</c> 参数。

- AdsDspRtbTpBatchUpdateAdGroupStatusRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbTpBatchUpdateAdGroupStatusRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbTpBatchUpdateAdGroupStatusRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbTpBatchUpdateAdGroupStatusResponse (class)

<c>jingdong.ads.dsp.rtb.tp.batchUpdateAdGroupStatus</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbTpBatchUpdateDmpPriceRequest (class)

<c>jingdong.ads.dsp.rtb.tp.batchUpdateDmpPrice</c> 的请求。

- JdRequest req;

- public AdsDspRtbTpBatchUpdateDmpPriceRequest()

- AdsDspRtbTpBatchUpdateDmpPriceRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- AdsDspRtbTpBatchUpdateDmpPriceRequest AdGroupPrice(int adGroupPrice)
  - 设置 <c>adGroupPrice</c> 参数。

- AdsDspRtbTpBatchUpdateDmpPriceRequest Ids(string ids)
  - 设置 <c>ids</c> 参数。

- AdsDspRtbTpBatchUpdateDmpPriceRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbTpBatchUpdateDmpPriceRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbTpBatchUpdateDmpPriceRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbTpBatchUpdateDmpPriceResponse (class)

<c>jingdong.ads.dsp.rtb.tp.batchUpdateDmpPrice</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbTpBatchUpdatePosPackageCoefRequest (class)

<c>jingdong.ads.dsp.rtb.tp.batchUpdatePosPackageCoef</c> 的请求。

- JdRequest req;

- public AdsDspRtbTpBatchUpdatePosPackageCoefRequest()

- AdsDspRtbTpBatchUpdatePosPackageCoefRequest Id(string id)
  - 设置 <c>id</c> 参数。

- AdsDspRtbTpBatchUpdatePosPackageCoefRequest Coef(int coef)
  - 设置 <c>coef</c> 参数。

- AdsDspRtbTpBatchUpdatePosPackageCoefRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- AdsDspRtbTpBatchUpdatePosPackageCoefRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbTpBatchUpdatePosPackageCoefRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbTpBatchUpdatePosPackageCoefRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbTpBatchUpdatePosPackageCoefResponse (class)

<c>jingdong.ads.dsp.rtb.tp.batchUpdatePosPackageCoef</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbTpUpdateAdGroupNameRequest (class)

<c>jingdong.ads.dsp.rtb.tp.updateAdGroupName</c> 的请求。

- JdRequest req;

- public AdsDspRtbTpUpdateAdGroupNameRequest()

- AdsDspRtbTpUpdateAdGroupNameRequest Id(long id)
  - 设置 <c>id</c> 参数。

- AdsDspRtbTpUpdateAdGroupNameRequest Name(string name)
  - 设置 <c>name</c> 参数。

- AdsDspRtbTpUpdateAdGroupNameRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbTpUpdateAdGroupNameRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbTpUpdateAdGroupNameRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbTpUpdateAdGroupNameResponse (class)

<c>jingdong.ads.dsp.rtb.tp.updateAdGroupName</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbTpUpdateDateRequest (class)

<c>jingdong.ads.dsp.rtb.tp.updateDate</c> 的请求。

- JdRequest req;

- public AdsDspRtbTpUpdateDateRequest()

- AdsDspRtbTpUpdateDateRequest Id(long id)
  - 设置 <c>id</c> 参数。

- AdsDspRtbTpUpdateDateRequest StartTime(string startTime)
  - 设置 <c>startTime</c> 参数。

- AdsDspRtbTpUpdateDateRequest EndTime(string endTime)
  - 设置 <c>endTime</c> 参数。

- AdsDspRtbTpUpdateDateRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbTpUpdateDateRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbTpUpdateDateRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbTpUpdateDateResponse (class)

<c>jingdong.ads.dsp.rtb.tp.updateDate</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspRtbTpUpdateTimeRangePriceCoefRequest (class)

<c>jingdong.ads.dsp.rtb.tp.updateTimeRangePriceCoef</c> 的请求。

- JdRequest req;

- public AdsDspRtbTpUpdateTimeRangePriceCoefRequest()

- AdsDspRtbTpUpdateTimeRangePriceCoefRequest Id(long id)
  - 设置 <c>id</c> 参数。

- AdsDspRtbTpUpdateTimeRangePriceCoefRequest TimeRangePriceCoef(string timeRangePriceCoef)
  - 设置 <c>timeRangePriceCoef</c> 参数。

- AdsDspRtbTpUpdateTimeRangePriceCoefRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsDspRtbTpUpdateTimeRangePriceCoefRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsDspRtbTpUpdateTimeRangePriceCoefRequest PlatformBusinessType(string platformBusinessType)
  - 设置 <c>platformBusinessType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsDspRtbTpUpdateTimeRangePriceCoefResponse (class)

<c>jingdong.ads.dsp.rtb.tp.updateTimeRangePriceCoef</c> 的响应。

- public Result returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsDspTraceGetRequest (class)

<c>jingdong.ads.dsp.trace.get</c> 的请求。

- JdRequest req;

- public AdsDspTraceGetRequest()

- JdRequest Raw()
  - 底层协议请求。


## AdsDspTraceGetResponse (class)

<c>jingdong.ads.dsp.trace.get</c> 的响应。

- public DailiSoaResult result;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsIbgUniversalJosServiceAccountQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.account.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceAccountQueryRequest()

- AdsIbgUniversalJosServiceAccountQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest MediaType(int mediaType)
  - 设置 <c>mediaType</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest Platform(string platform)
  - 设置 <c>platform</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest Granularity(int granularity)
  - 设置 <c>granularity</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest InteractiveType(int interactiveType)
  - 设置 <c>interactiveType</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest DeliverySystemType(int deliverySystemType)
  - 设置 <c>deliverySystemType</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest JdMediaUserId(long jdMediaUserId)
  - 设置 <c>jdMediaUserId</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest JstGroupType(int jstGroupType)
  - 设置 <c>jstGroupType</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest Columns(string columns)
  - 设置 <c>columns</c> 参数。

- AdsIbgUniversalJosServiceAccountQueryRequest ScenarioType(string scenarioType)
  - 设置 <c>scenarioType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceAccountQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.account.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 完整 JOS 响应包装，用于诊断。


## AdsIbgUniversalJosServiceAdQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.ad.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceAdQueryRequest()

- AdsIbgUniversalJosServiceAdQueryRequest AdName(string adName)
  - 设置 <c>adName</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest CampaignType(int campaignType)
  - 设置 <c>campaignType</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest ScenarioType(int scenarioType)
  - 设置 <c>scenarioType</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest AdGroupName(string adGroupName)
  - 设置 <c>adGroupName</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest DeliveryType(int deliveryType)
  - 设置 <c>deliveryType</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest PackageId(long packageId)
  - 设置 <c>packageId</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest MediaType(int mediaType)
  - 设置 <c>mediaType</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest Platform(string platform)
  - 设置 <c>platform</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest AdId(long adId)
  - 设置 <c>adId</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest SiteId(long siteId)
  - 设置 <c>siteId</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest InteractiveType(int interactiveType)
  - 设置 <c>interactiveType</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest BusinessType(string businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest DeliverySystemType(int deliverySystemType)
  - 设置 <c>deliverySystemType</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest JdMediaUserId(long jdMediaUserId)
  - 设置 <c>jdMediaUserId</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest Granularity(int granularity)
  - 设置 <c>granularity</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest CreativeType(string creativeType)
  - 设置 <c>creativeType</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest Type(string type)
  - 设置 <c>type</c> 参数。

- AdsIbgUniversalJosServiceAdQueryRequest Columns(string columns)
  - 设置 <c>columns</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceAdQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.ad.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceAgentActivityV1Request (class)

<c>jingdong.ads.ibg.UniversalJosService.agentActivity.v1</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceAgentActivityV1Request()

- AdsIbgUniversalJosServiceAgentActivityV1Request AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceAgentActivityV1Request ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceAgentActivityV1Response (class)

<c>jingdong.ads.ibg.UniversalJosService.agentActivity.v1</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceAgentDetailV1Request (class)

<c>jingdong.ads.ibg.UniversalJosService.agentDetail.v1</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceAgentDetailV1Request()

- AdsIbgUniversalJosServiceAgentDetailV1Request AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request PullNewFlag(int pullNewFlag)
  - 设置 <c>pullNewFlag</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request Filters(string filters)
  - 设置 <c>filters</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request Dimensions(string dimensions)
  - 设置 <c>dimensions</c> 参数。

- AdsIbgUniversalJosServiceAgentDetailV1Request Obys(string obys)
  - 设置 <c>obys</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceAgentDetailV1Response (class)

<c>jingdong.ads.ibg.UniversalJosService.agentDetail.v1</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceCampaignQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.campaign.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceCampaignQueryRequest()

- AdsIbgUniversalJosServiceCampaignQueryRequest CampaignType(int campaignType)
  - 设置 <c>campaignType</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest ScenarioType(int scenarioType)
  - 设置 <c>scenarioType</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest MediaType(int mediaType)
  - 设置 <c>mediaType</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest Platform(string platform)
  - 设置 <c>platform</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest CampaignName(string campaignName)
  - 设置 <c>campaignName</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest DeliverySystemType(int deliverySystemType)
  - 设置 <c>deliverySystemType</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest JdMediaUserId(long jdMediaUserId)
  - 设置 <c>jdMediaUserId</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest Granularity(int granularity)
  - 设置 <c>granularity</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- AdsIbgUniversalJosServiceCampaignQueryRequest Columns(string columns)
  - 设置 <c>columns</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceCampaignQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.campaign.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceDmpDetailV1Request (class)

<c>jingdong.ads.ibg.UniversalJosService.dmpDetail.v1</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceDmpDetailV1Request()

- AdsIbgUniversalJosServiceDmpDetailV1Request ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request DisplayType(string displayType)
  - 设置 <c>displayType</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request DmpId(long dmpId)
  - 设置 <c>dmpId</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceDmpDetailV1Request ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceDmpDetailV1Response (class)

<c>jingdong.ads.ibg.UniversalJosService.dmpDetail.v1</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceDmpListV1Request (class)

<c>jingdong.ads.ibg.UniversalJosService.dmpList.v1</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceDmpListV1Request()

- AdsIbgUniversalJosServiceDmpListV1Request ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request DisplayType(string displayType)
  - 设置 <c>displayType</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceDmpListV1Request ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceDmpListV1Response (class)

<c>jingdong.ads.ibg.UniversalJosService.dmpList.v1</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceGroupQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.group.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceGroupQueryRequest()

- AdsIbgUniversalJosServiceGroupQueryRequest CampaignType(int campaignType)
  - 设置 <c>campaignType</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest ScenarioType(int scenarioType)
  - 设置 <c>scenarioType</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest AdGroupName(string adGroupName)
  - 设置 <c>adGroupName</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest DeliveryType(int deliveryType)
  - 设置 <c>deliveryType</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest PackageId(long packageId)
  - 设置 <c>packageId</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest MediaType(int mediaType)
  - 设置 <c>mediaType</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest Platform(string platform)
  - 设置 <c>platform</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest SiteId(long siteId)
  - 设置 <c>siteId</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest InteractiveType(int interactiveType)
  - 设置 <c>interactiveType</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest DeliverySystemType(int deliverySystemType)
  - 设置 <c>deliverySystemType</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest JdMediaUserId(long jdMediaUserId)
  - 设置 <c>jdMediaUserId</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest Granularity(int granularity)
  - 设置 <c>granularity</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- AdsIbgUniversalJosServiceGroupQueryRequest Columns(string columns)
  - 设置 <c>columns</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceGroupQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.group.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceKeywordQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.keyword.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceKeywordQueryRequest()

- AdsIbgUniversalJosServiceKeywordQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest Platform(string platform)
  - 设置 <c>platform</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest TargetingType(string targetingType)
  - 设置 <c>targetingType</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- AdsIbgUniversalJosServiceKeywordQueryRequest ContainSmart(bool containSmart)
  - 设置 <c>containSmart</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceKeywordQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.keyword.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceLocationQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.location.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceLocationQueryRequest()

- AdsIbgUniversalJosServiceLocationQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest InteractiveType(int interactiveType)
  - 设置 <c>interactiveType</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceLocationQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceLocationQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.location.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceOrderQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.order.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceOrderQueryRequest()

- AdsIbgUniversalJosServiceOrderQueryRequest ClickStartDay(string clickStartDay)
  - 设置 <c>clickStartDay</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest OrderStartDay(string orderStartDay)
  - 设置 <c>orderStartDay</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest OrderStatus(int orderStatus)
  - 设置 <c>orderStatus</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest PaymentType(int paymentType)
  - 设置 <c>paymentType</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest Province(string province)
  - 设置 <c>province</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest OrderEndDay(string orderEndDay)
  - 设置 <c>orderEndDay</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest ClickEndDay(string clickEndDay)
  - 设置 <c>clickEndDay</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest DeliveryType(int deliveryType)
  - 设置 <c>deliveryType</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest MediaType(int mediaType)
  - 设置 <c>mediaType</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest PosPackageId(long posPackageId)
  - 设置 <c>posPackageId</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest Myself(string myself)
  - 设置 <c>myself</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest DeliverySystemType(int deliverySystemType)
  - 设置 <c>deliverySystemType</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest JdMediaUserId(long jdMediaUserId)
  - 设置 <c>jdMediaUserId</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest SpuId(string spuId)
  - 设置 <c>spuId</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest SkuId(string skuId)
  - 设置 <c>skuId</c> 参数。

- AdsIbgUniversalJosServiceOrderQueryRequest CampaignTypes(string campaignTypes)
  - 设置 <c>campaignTypes</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceOrderQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.order.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServicePackageQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.package.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServicePackageQueryRequest()

- AdsIbgUniversalJosServicePackageQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- AdsIbgUniversalJosServicePackageQueryRequest ScenarioType(string scenarioType)
  - 设置 <c>scenarioType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServicePackageQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.package.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceSearchWordQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.searchWord.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceSearchWordQueryRequest()

- AdsIbgUniversalJosServiceSearchWordQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest Province(string province)
  - 设置 <c>province</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest Keyword(string keyword)
  - 设置 <c>keyword</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest GroupId(long groupId)
  - 设置 <c>groupId</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest AdId(long adId)
  - 设置 <c>adId</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceSearchWordQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceSearchWordQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.searchWord.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceSkuDeliveryQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.skuDelivery.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceSkuDeliveryQueryRequest()

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest Platform(string platform)
  - 设置 <c>platform</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest GroupId(long groupId)
  - 设置 <c>groupId</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest MatchingType(int matchingType)
  - 设置 <c>matchingType</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest Province(string province)
  - 设置 <c>province</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest QueryType(string queryType)
  - 设置 <c>queryType</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceSkuDeliveryQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceSkuDeliveryQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.skuDelivery.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceSkuQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.sku.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceSkuQueryRequest()

- AdsIbgUniversalJosServiceSkuQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest IsDaily(bool isDaily)
  - 设置 <c>isDaily</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest OrderStatusCategory(int orderStatusCategory)
  - 设置 <c>orderStatusCategory</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest ClickOrOrderCaliber(int clickOrOrderCaliber)
  - 设置 <c>clickOrOrderCaliber</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest ClickOrOrderDay(int clickOrOrderDay)
  - 设置 <c>clickOrOrderDay</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest Page(int page)
  - 设置 <c>page</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest Platform(string platform)
  - 设置 <c>platform</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest GiftFlag(int giftFlag)
  - 设置 <c>giftFlag</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest IsDownload(bool isDownload)
  - 设置 <c>isDownload</c> 参数。

- AdsIbgUniversalJosServiceSkuQueryRequest ReportName(string reportName)
  - 设置 <c>reportName</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceSkuQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.sku.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdsIbgUniversalJosServiceTrendQueryRequest (class)

<c>jingdong.ads.ibg.UniversalJosService.trend.query</c> 的请求。

- JdRequest req;

- public AdsIbgUniversalJosServiceTrendQueryRequest()

- AdsIbgUniversalJosServiceTrendQueryRequest AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- AdsIbgUniversalJosServiceTrendQueryRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- AdsIbgUniversalJosServiceTrendQueryRequest StartDay(string startDay)
  - 设置 <c>startDay</c> 参数。

- AdsIbgUniversalJosServiceTrendQueryRequest EndDay(string endDay)
  - 设置 <c>endDay</c> 参数。

- AdsIbgUniversalJosServiceTrendQueryRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- AdsIbgUniversalJosServiceTrendQueryRequest Granularity(int granularity)
  - 设置 <c>granularity</c> 参数。

- AdsIbgUniversalJosServiceTrendQueryRequest CampaignId(long campaignId)
  - 设置 <c>campaignId</c> 参数。

- AdsIbgUniversalJosServiceTrendQueryRequest AdGroupId(long adGroupId)
  - 设置 <c>adGroupId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdsIbgUniversalJosServiceTrendQueryResponse (class)

<c>jingdong.ads.ibg.UniversalJosService.trend.query</c> 的响应。

- public ResponseMessage returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdAdsApi (class)

jingdong.ads.* 的强类型客户端。

- JdClient client;

- public JdAdsApi(JdClient client)

- async AdsDspMaterialOpenapiDeleteByMaterialIdResponse DspMaterialOpenapiDeleteByMaterialIdAsync(AdsDspMaterialOpenapiDeleteByMaterialIdRequest request)
  - 执行 <c>jingdong.ads.dsp.material.openapi.deleteByMaterialId</c>。

- async AdsDspMaterialOpenapiFindMaterialByIdResponse DspMaterialOpenapiFindMaterialByIdAsync(AdsDspMaterialOpenapiFindMaterialByIdRequest request)
  - 执行 <c>jingdong.ads.dsp.material.openapi.findMaterialById</c>。

- async AdsDspMaterialOpenapiGetMaterialListResponse DspMaterialOpenapiGetMaterialListAsync(AdsDspMaterialOpenapiGetMaterialListRequest request)
  - 执行 <c>jingdong.ads.dsp.material.openapi.getMaterialList</c>。

- async AdsDspMaterialOpenapiGetMaterialResourceResponse DspMaterialOpenapiGetMaterialResourceAsync(AdsDspMaterialOpenapiGetMaterialResourceRequest request)
  - 执行 <c>jingdong.ads.dsp.material.openapi.getMaterialResource</c>。

- async AdsDspRtbFeaturedAddCampaignResponse DspRtbFeaturedAddCampaignAsync(AdsDspRtbFeaturedAddCampaignRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.addCampaign</c>。

- async AdsDspRtbFeaturedBindCrowdResponse DspRtbFeaturedBindCrowdAsync(AdsDspRtbFeaturedBindCrowdRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.bindCrowd</c>。

- async AdsDspRtbFeaturedBindMaterialResponse DspRtbFeaturedBindMaterialAsync(AdsDspRtbFeaturedBindMaterialRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.bindMaterial</c>。

- async AdsDspRtbFeaturedDeleteCampaignResponse DspRtbFeaturedDeleteCampaignAsync(AdsDspRtbFeaturedDeleteCampaignRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.deleteCampaign</c>。

- async AdsDspRtbFeaturedGetPosPackageListResponse DspRtbFeaturedGetPosPackageListAsync(AdsDspRtbFeaturedGetPosPackageListRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.getPosPackageList</c>。

- async AdsDspRtbFeaturedPosPackageListResponse DspRtbFeaturedPosPackageListAsync(AdsDspRtbFeaturedPosPackageListRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.posPackageList</c>。

- async AdsDspRtbFeaturedUpdateCampaignNameResponse DspRtbFeaturedUpdateCampaignNameAsync(AdsDspRtbFeaturedUpdateCampaignNameRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.updateCampaignName</c>。

- async AdsDspRtbFeaturedUpdateCampaignStatusResponse DspRtbFeaturedUpdateCampaignStatusAsync(AdsDspRtbFeaturedUpdateCampaignStatusRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.updateCampaignStatus</c>。

- async AdsDspRtbFeaturedUpdateDateResponse DspRtbFeaturedUpdateDateAsync(AdsDspRtbFeaturedUpdateDateRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.updateDate</c>。

- async AdsDspRtbFeaturedUpdateTimeRangePriceCoefResponse DspRtbFeaturedUpdateTimeRangePriceCoefAsync(AdsDspRtbFeaturedUpdateTimeRangePriceCoefRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.featured.updateTimeRangePriceCoef</c>。

- async AdsDspRtbHtListCampaignsByPinResponse DspRtbHtListCampaignsByPinAsync(AdsDspRtbHtListCampaignsByPinRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.ht.listCampaignsByPin</c>。

- async AdsDspRtbKeywordFindKeyWordHistoryResponse DspRtbKeywordFindKeyWordHistoryAsync(AdsDspRtbKeywordFindKeyWordHistoryRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.keyword.findKeyWordHistory</c>。

- async AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefResponse DspRtbKuaicheCampaignUpdateTimeRangePriceCoefAsync(AdsDspRtbKuaicheCampaignUpdateTimeRangePriceCoefRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.kuaiche.campaign.updateTimeRangePriceCoef</c>。

- async AdsDspRtbKuaicheQueryPriceSuggestResponse DspRtbKuaicheQueryPriceSuggestAsync(AdsDspRtbKuaicheQueryPriceSuggestRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.kuaiche.queryPriceSuggest</c>。

- async AdsDspRtbShopkeywordListResponse DspRtbShopkeywordListAsync(AdsDspRtbShopkeywordListRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.shopkeyword.list</c>。

- async AdsDspRtbTpBatchDeleteDmpResponse DspRtbTpBatchDeleteDmpAsync(AdsDspRtbTpBatchDeleteDmpRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.tp.batchDeleteDmp</c>。

- async AdsDspRtbTpBatchUpdateAdGroupStatusResponse DspRtbTpBatchUpdateAdGroupStatusAsync(AdsDspRtbTpBatchUpdateAdGroupStatusRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.tp.batchUpdateAdGroupStatus</c>。

- async AdsDspRtbTpBatchUpdateDmpPriceResponse DspRtbTpBatchUpdateDmpPriceAsync(AdsDspRtbTpBatchUpdateDmpPriceRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.tp.batchUpdateDmpPrice</c>。

- async AdsDspRtbTpBatchUpdatePosPackageCoefResponse DspRtbTpBatchUpdatePosPackageCoefAsync(AdsDspRtbTpBatchUpdatePosPackageCoefRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.tp.batchUpdatePosPackageCoef</c>。

- async AdsDspRtbTpUpdateAdGroupNameResponse DspRtbTpUpdateAdGroupNameAsync(AdsDspRtbTpUpdateAdGroupNameRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.tp.updateAdGroupName</c>。

- async AdsDspRtbTpUpdateDateResponse DspRtbTpUpdateDateAsync(AdsDspRtbTpUpdateDateRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.tp.updateDate</c>。

- async AdsDspRtbTpUpdateTimeRangePriceCoefResponse DspRtbTpUpdateTimeRangePriceCoefAsync(AdsDspRtbTpUpdateTimeRangePriceCoefRequest request)
  - 执行 <c>jingdong.ads.dsp.rtb.tp.updateTimeRangePriceCoef</c>。

- async AdsDspTraceGetResponse DspTraceGetAsync(AdsDspTraceGetRequest request)
  - 执行 <c>jingdong.ads.dsp.trace.get</c>。

- async AdsIbgUniversalJosServiceAccountQueryResponse IbgUniversalJosServiceAccountQueryAsync(AdsIbgUniversalJosServiceAccountQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.account.query</c>。

- async AdsIbgUniversalJosServiceAdQueryResponse IbgUniversalJosServiceAdQueryAsync(AdsIbgUniversalJosServiceAdQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.ad.query</c>。

- async AdsIbgUniversalJosServiceAgentActivityV1Response IbgUniversalJosServiceAgentActivityV1Async(AdsIbgUniversalJosServiceAgentActivityV1Request request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.agentActivity.v1</c>。

- async AdsIbgUniversalJosServiceAgentDetailV1Response IbgUniversalJosServiceAgentDetailV1Async(AdsIbgUniversalJosServiceAgentDetailV1Request request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.agentDetail.v1</c>。

- async AdsIbgUniversalJosServiceCampaignQueryResponse IbgUniversalJosServiceCampaignQueryAsync(AdsIbgUniversalJosServiceCampaignQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.campaign.query</c>。

- async AdsIbgUniversalJosServiceDmpDetailV1Response IbgUniversalJosServiceDmpDetailV1Async(AdsIbgUniversalJosServiceDmpDetailV1Request request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.dmpDetail.v1</c>。

- async AdsIbgUniversalJosServiceDmpListV1Response IbgUniversalJosServiceDmpListV1Async(AdsIbgUniversalJosServiceDmpListV1Request request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.dmpList.v1</c>。

- async AdsIbgUniversalJosServiceGroupQueryResponse IbgUniversalJosServiceGroupQueryAsync(AdsIbgUniversalJosServiceGroupQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.group.query</c>。

- async AdsIbgUniversalJosServiceKeywordQueryResponse IbgUniversalJosServiceKeywordQueryAsync(AdsIbgUniversalJosServiceKeywordQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.keyword.query</c>。

- async AdsIbgUniversalJosServiceLocationQueryResponse IbgUniversalJosServiceLocationQueryAsync(AdsIbgUniversalJosServiceLocationQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.location.query</c>。

- async AdsIbgUniversalJosServiceOrderQueryResponse IbgUniversalJosServiceOrderQueryAsync(AdsIbgUniversalJosServiceOrderQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.order.query</c>。

- async AdsIbgUniversalJosServicePackageQueryResponse IbgUniversalJosServicePackageQueryAsync(AdsIbgUniversalJosServicePackageQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.package.query</c>。

- async AdsIbgUniversalJosServiceSearchWordQueryResponse IbgUniversalJosServiceSearchWordQueryAsync(AdsIbgUniversalJosServiceSearchWordQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.searchWord.query</c>。

- async AdsIbgUniversalJosServiceSkuDeliveryQueryResponse IbgUniversalJosServiceSkuDeliveryQueryAsync(AdsIbgUniversalJosServiceSkuDeliveryQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.skuDelivery.query</c>。

- async AdsIbgUniversalJosServiceSkuQueryResponse IbgUniversalJosServiceSkuQueryAsync(AdsIbgUniversalJosServiceSkuQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.sku.query</c>。

- async AdsIbgUniversalJosServiceTrendQueryResponse IbgUniversalJosServiceTrendQueryAsync(AdsIbgUniversalJosServiceTrendQueryRequest request)
  - 执行 <c>jingdong.ads.ibg.UniversalJosService.trend.query</c>。
