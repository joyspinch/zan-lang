# Sdk.Jd.Api.Mkt

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mkt/JdMktApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mkt/MktSmartstrategyIntelligentisvGetCouponBatchRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mkt/MktSmartstrategyIntelligentisvGetDeliveryChannelRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mkt/MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mkt/MktSmartstrategyIntelligentisvSubmitISVPlanRequest.zan`


## JdMktApi (class)

- JdClient client;

- public JdMktApi(JdClient client)

- async MktSmartstrategyIntelligentisvGetCouponBatchResponse SmartstrategyIntelligentisvGetCouponBatchAsync(MktSmartstrategyIntelligentisvGetCouponBatchRequest request)

- async MktSmartstrategyIntelligentisvGetDeliveryChannelResponse SmartstrategyIntelligentisvGetDeliveryChannelAsync(MktSmartstrategyIntelligentisvGetDeliveryChannelRequest request)

- async MktSmartstrategyIntelligentisvGetISVPlanEffectListResponse SmartstrategyIntelligentisvGetISVPlanEffectListAsync(MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest request)

- async MktSmartstrategyIntelligentisvSubmitISVPlanResponse SmartstrategyIntelligentisvSubmitISVPlanAsync(MktSmartstrategyIntelligentisvSubmitISVPlanRequest request)


## MktSmartstrategyIntelligentisvGetCouponBatchRequest (class)

- JdRequest req;

- public MktSmartstrategyIntelligentisvGetCouponBatchRequest()

- MktSmartstrategyIntelligentisvGetCouponBatchRequest PutKey(string putKey)

- JdRequest Raw()


## MktSmartstrategyIntelligentisvGetCouponBatchResponse (class)

- public JsfResult returnType;

- public string Raw;


## MktSmartstrategyIntelligentisvGetDeliveryChannelRequest (class)

- JdRequest req;

- public MktSmartstrategyIntelligentisvGetDeliveryChannelRequest()

- MktSmartstrategyIntelligentisvGetDeliveryChannelRequest Value(string value_)

- JdRequest Raw()


## MktSmartstrategyIntelligentisvGetDeliveryChannelResponse (class)

- public JsfResult returnType;

- public string Raw;


## MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest (class)

- JdRequest req;

- public MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest()

- MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest PageNo(int pageNo)

- MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest PageSize(int pageSize)

- JdRequest Raw()


## MktSmartstrategyIntelligentisvGetISVPlanEffectListResponse (class)

- public JsfResult returnType;

- public string Raw;


## MktSmartstrategyIntelligentisvSubmitISVPlanRequest (class)

- JdRequest req;

- public MktSmartstrategyIntelligentisvSubmitISVPlanRequest()

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest TargetThirdCateIds(string targetThirdCateIds)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PlanName(string planName)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest TargetSecondCateIds(string targetSecondCateIds)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest TargetFirstCateIds(string targetFirstCateIds)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest MultiChannels(string multiChannels)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest Pin(string pin)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PullNewer(int pullNewer)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PlanBeginTime(long planBeginTime)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest Repurchase(int repurchase)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PutKey(string putKey)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest CatePopStrategy(int catePopStrategy)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PlanEndTime(long planEndTime)

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest Campus(int campus)

- JdRequest Raw()


## MktSmartstrategyIntelligentisvSubmitISVPlanResponse (class)

- public JsfResult returnType;

- public string Raw;
