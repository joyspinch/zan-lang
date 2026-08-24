# Sdk.Jd.Api.Mkt

> 源码: `stdlib/Sdk/Jd/Api/Mkt/JdMktApi.zan`, `stdlib/Sdk/Jd/Api/Mkt/MktSmartstrategyIntelligentisvGetCouponBatchRequest.zan`, `stdlib/Sdk/Jd/Api/Mkt/MktSmartstrategyIntelligentisvGetDeliveryChannelRequest.zan`, `stdlib/Sdk/Jd/Api/Mkt/MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest.zan`, `stdlib/Sdk/Jd/Api/Mkt/MktSmartstrategyIntelligentisvSubmitISVPlanRequest.zan`


## JdMktApi (class)

jingdong.mkt.* 的强类型客户端。

- JdClient client;

- public JdMktApi(JdClient client)

- async MktSmartstrategyIntelligentisvGetCouponBatchResponse SmartstrategyIntelligentisvGetCouponBatchAsync(MktSmartstrategyIntelligentisvGetCouponBatchRequest request)
  - 执行 <c>jingdong.mkt.smartstrategy.intelligentisv.getCouponBatch</c>。

- async MktSmartstrategyIntelligentisvGetDeliveryChannelResponse SmartstrategyIntelligentisvGetDeliveryChannelAsync(MktSmartstrategyIntelligentisvGetDeliveryChannelRequest request)
  - 执行 <c>jingdong.mkt.smartstrategy.intelligentisv.getDeliveryChannel</c>。

- async MktSmartstrategyIntelligentisvGetISVPlanEffectListResponse SmartstrategyIntelligentisvGetISVPlanEffectListAsync(MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest request)
  - 执行 <c>jingdong.mkt.smartstrategy.intelligentisv.getISVPlanEffectList</c>。

- async MktSmartstrategyIntelligentisvSubmitISVPlanResponse SmartstrategyIntelligentisvSubmitISVPlanAsync(MktSmartstrategyIntelligentisvSubmitISVPlanRequest request)
  - 执行 <c>jingdong.mkt.smartstrategy.intelligentisv.submitISVPlan</c>。


## MktSmartstrategyIntelligentisvGetCouponBatchRequest (class)

<c>jingdong.mkt.smartstrategy.intelligentisv.getCouponBatch</c> 的请求。

- JdRequest req;

- public MktSmartstrategyIntelligentisvGetCouponBatchRequest()

- MktSmartstrategyIntelligentisvGetCouponBatchRequest PutKey(string putKey)
  - 设置 <c>putKey</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MktSmartstrategyIntelligentisvGetCouponBatchResponse (class)

<c>jingdong.mkt.smartstrategy.intelligentisv.getCouponBatch</c> 的响应。

- public JsfResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MktSmartstrategyIntelligentisvGetDeliveryChannelRequest (class)

<c>jingdong.mkt.smartstrategy.intelligentisv.getDeliveryChannel</c> 的请求。

- JdRequest req;

- public MktSmartstrategyIntelligentisvGetDeliveryChannelRequest()

- MktSmartstrategyIntelligentisvGetDeliveryChannelRequest Value(string value_)
  - 设置 <c>value</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MktSmartstrategyIntelligentisvGetDeliveryChannelResponse (class)

<c>jingdong.mkt.smartstrategy.intelligentisv.getDeliveryChannel</c> 的响应。

- public JsfResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest (class)

<c>jingdong.mkt.smartstrategy.intelligentisv.getISVPlanEffectList</c> 的请求。

- JdRequest req;

- public MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest()

- MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest PageNo(int pageNo)
  - 设置 <c>pageNo</c> 参数。

- MktSmartstrategyIntelligentisvGetISVPlanEffectListRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MktSmartstrategyIntelligentisvGetISVPlanEffectListResponse (class)

<c>jingdong.mkt.smartstrategy.intelligentisv.getISVPlanEffectList</c> 的响应。

- public JsfResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MktSmartstrategyIntelligentisvSubmitISVPlanRequest (class)

<c>jingdong.mkt.smartstrategy.intelligentisv.submitISVPlan</c> 的请求。

- JdRequest req;

- public MktSmartstrategyIntelligentisvSubmitISVPlanRequest()

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest TargetThirdCateIds(string targetThirdCateIds)
  - 设置 <c>targetThirdCateIds</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PlanName(string planName)
  - 设置 <c>planName</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest TargetSecondCateIds(string targetSecondCateIds)
  - 设置 <c>targetSecondCateIds</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest TargetFirstCateIds(string targetFirstCateIds)
  - 设置 <c>targetFirstCateIds</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest MultiChannels(string multiChannels)
  - 设置 <c>multiChannels</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest Pin(string pin)
  - 设置 <c>pin</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PullNewer(int pullNewer)
  - 设置 <c>pullNewer</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PlanBeginTime(long planBeginTime)
  - 设置 <c>planBeginTime</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest Repurchase(int repurchase)
  - 设置 <c>repurchase</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PutKey(string putKey)
  - 设置 <c>putKey</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest CatePopStrategy(int catePopStrategy)
  - 设置 <c>catePopStrategy</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest PlanEndTime(long planEndTime)
  - 设置 <c>planEndTime</c> 参数。

- MktSmartstrategyIntelligentisvSubmitISVPlanRequest Campus(int campus)
  - 设置 <c>campus</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MktSmartstrategyIntelligentisvSubmitISVPlanResponse (class)

<c>jingdong.mkt.smartstrategy.intelligentisv.submitISVPlan</c> 的响应。

- public JsfResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
