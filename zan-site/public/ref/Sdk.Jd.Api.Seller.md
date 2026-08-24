# Sdk.Jd.Api.Seller

> 源码: `stdlib/Sdk/Jd/Api/Seller/JdSellerApi.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerCouponReadGetCouponCountRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerCouponReadGetCouponListRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionActivitymodeAddRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionActivitymodeGetRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionAddRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionCheckRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionCommitRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionDeleteRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionGetRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionListRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionOrdermodeListRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionSkuAddRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionV2CountRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionV2GetRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionV2ListRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionV2SkuListRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerPromotionV2UnitFullCreateRequest.zan`, `stdlib/Sdk/Jd/Api/Seller/SellerVenderInfoGetRequest.zan`


## JdSellerApi (class)

jingdong.seller.* 的强类型客户端。

- JdClient client;

- public JdSellerApi(JdClient client)

- async SellerCouponReadGetCouponCountResponse CouponReadGetCouponCountAsync(SellerCouponReadGetCouponCountRequest request)
  - 执行 <c>jingdong.seller.coupon.read.getCouponCount</c>。

- async SellerCouponReadGetCouponListResponse CouponReadGetCouponListAsync(SellerCouponReadGetCouponListRequest request)
  - 执行 <c>jingdong.seller.coupon.read.getCouponList</c>。

- async SellerPromotionActivitymodeAddResponse PromotionActivitymodeAddAsync(SellerPromotionActivitymodeAddRequest request)
  - 执行 <c>jingdong.seller.promotion.activitymode.add</c>。

- async SellerPromotionActivitymodeGetResponse PromotionActivitymodeGetAsync(SellerPromotionActivitymodeGetRequest request)
  - 执行 <c>jingdong.seller.promotion.activitymode.get</c>。

- async SellerPromotionAddResponse PromotionAddAsync(SellerPromotionAddRequest request)
  - 执行 <c>jingdong.seller.promotion.add</c>。

- async SellerPromotionCheckResponse PromotionCheckAsync(SellerPromotionCheckRequest request)
  - 执行 <c>jingdong.seller.promotion.check</c>。

- async SellerPromotionCommitResponse PromotionCommitAsync(SellerPromotionCommitRequest request)
  - 执行 <c>jingdong.seller.promotion.commit</c>。

- async SellerPromotionDeleteResponse PromotionDeleteAsync(SellerPromotionDeleteRequest request)
  - 执行 <c>jingdong.seller.promotion.delete</c>。

- async SellerPromotionGetResponse PromotionGetAsync(SellerPromotionGetRequest request)
  - 执行 <c>jingdong.seller.promotion.get</c>。

- async SellerPromotionListResponse PromotionListAsync(SellerPromotionListRequest request)
  - 执行 <c>jingdong.seller.promotion.list</c>。

- async SellerPromotionOrdermodeListResponse PromotionOrdermodeListAsync(SellerPromotionOrdermodeListRequest request)
  - 执行 <c>jingdong.seller.promotion.ordermode.list</c>。

- async SellerPromotionSkuAddResponse PromotionSkuAddAsync(SellerPromotionSkuAddRequest request)
  - 执行 <c>jingdong.seller.promotion.sku.add</c>。

- async SellerPromotionV2CountResponse PromotionV2CountAsync(SellerPromotionV2CountRequest request)
  - 执行 <c>jingdong.seller.promotion.v2.count</c>。

- async SellerPromotionV2GetResponse PromotionV2GetAsync(SellerPromotionV2GetRequest request)
  - 执行 <c>jingdong.seller.promotion.v2.get</c>。

- async SellerPromotionV2ListResponse PromotionV2ListAsync(SellerPromotionV2ListRequest request)
  - 执行 <c>jingdong.seller.promotion.v2.list</c>。

- async SellerPromotionV2SkuListResponse PromotionV2SkuListAsync(SellerPromotionV2SkuListRequest request)
  - 执行 <c>jingdong.seller.promotion.v2.sku.list</c>。

- async SellerPromotionV2UnitFullCreateResponse PromotionV2UnitFullCreateAsync(SellerPromotionV2UnitFullCreateRequest request)
  - 执行 <c>jingdong.seller.promotion.v2.unit.full.create</c>。

- async SellerVenderInfoGetResponse VenderInfoGetAsync(SellerVenderInfoGetRequest request)
  - 执行 <c>jingdong.seller.vender.info.get</c>。


## SellerCouponReadGetCouponCountRequest (class)

<c>jingdong.seller.coupon.read.getCouponCount</c> 的请求。

- JdRequest req;

- public SellerCouponReadGetCouponCountRequest()

- SellerCouponReadGetCouponCountRequest Ip(string ip)
  - 设置 <c>ip</c> 参数。

- SellerCouponReadGetCouponCountRequest Port(string port)
  - 设置 <c>port</c> 参数。

- SellerCouponReadGetCouponCountRequest CouponId(long couponId)
  - 设置 <c>couponId</c> 参数。

- SellerCouponReadGetCouponCountRequest Type(int type)
  - 设置 <c>type</c> 参数。

- SellerCouponReadGetCouponCountRequest GrantType(int grantType)
  - 设置 <c>grantType</c> 参数。

- SellerCouponReadGetCouponCountRequest BindType(int bindType)
  - 设置 <c>bindType</c> 参数。

- SellerCouponReadGetCouponCountRequest GrantWay(int grantWay)
  - 设置 <c>grantWay</c> 参数。

- SellerCouponReadGetCouponCountRequest Name(string name)
  - 设置 <c>name</c> 参数。

- SellerCouponReadGetCouponCountRequest CreateMonth(string createMonth)
  - 设置 <c>createMonth</c> 参数。

- SellerCouponReadGetCouponCountRequest CreatorType(int creatorType)
  - 设置 <c>creatorType</c> 参数。

- SellerCouponReadGetCouponCountRequest Closed(int closed)
  - 设置 <c>closed</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerCouponReadGetCouponCountResponse (class)

<c>jingdong.seller.coupon.read.getCouponCount</c> 的响应。

- public int getcouponcount_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerCouponReadGetCouponListRequest (class)

<c>jingdong.seller.coupon.read.getCouponList</c> 的请求。

- JdRequest req;

- public SellerCouponReadGetCouponListRequest()

- SellerCouponReadGetCouponListRequest Ip(string ip)
  - 设置 <c>ip</c> 参数。

- SellerCouponReadGetCouponListRequest Port(string port)
  - 设置 <c>port</c> 参数。

- SellerCouponReadGetCouponListRequest CouponId(long couponId)
  - 设置 <c>couponId</c> 参数。

- SellerCouponReadGetCouponListRequest Type(int type)
  - 设置 <c>type</c> 参数。

- SellerCouponReadGetCouponListRequest GrantType(int grantType)
  - 设置 <c>grantType</c> 参数。

- SellerCouponReadGetCouponListRequest BindType(int bindType)
  - 设置 <c>bindType</c> 参数。

- SellerCouponReadGetCouponListRequest GrantWay(int grantWay)
  - 设置 <c>grantWay</c> 参数。

- SellerCouponReadGetCouponListRequest Name(string name)
  - 设置 <c>name</c> 参数。

- SellerCouponReadGetCouponListRequest CreateMonth(string createMonth)
  - 设置 <c>createMonth</c> 参数。

- SellerCouponReadGetCouponListRequest CreatorType(int creatorType)
  - 设置 <c>creatorType</c> 参数。

- SellerCouponReadGetCouponListRequest Closed(int closed)
  - 设置 <c>closed</c> 参数。

- SellerCouponReadGetCouponListRequest Page(int page)
  - 设置 <c>page</c> 参数。

- SellerCouponReadGetCouponListRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerCouponReadGetCouponListResponse (class)

<c>jingdong.seller.coupon.read.getCouponList</c> 的响应。

- public List<string> couponList;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionActivitymodeAddRequest (class)

<c>jingdong.seller.promotion.activitymode.add</c> 的请求。

- JdRequest req;

- public SellerPromotionActivitymodeAddRequest()

- SellerPromotionActivitymodeAddRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- SellerPromotionActivitymodeAddRequest NumBound(int numBound)
  - 设置 <c>num_bound</c> 参数。

- SellerPromotionActivitymodeAddRequest FreqBound(int freqBound)
  - 设置 <c>freq_bound</c> 参数。

- SellerPromotionActivitymodeAddRequest PerMaxNum(int perMaxNum)
  - 设置 <c>per_max_num</c> 参数。

- SellerPromotionActivitymodeAddRequest PerMinNum(int perMinNum)
  - 设置 <c>per_min_num</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionActivitymodeAddResponse (class)

<c>jingdong.seller.promotion.activitymode.add</c> 的响应。

- public long id;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionActivitymodeGetRequest (class)

<c>jingdong.seller.promotion.activitymode.get</c> 的请求。

- JdRequest req;

- public SellerPromotionActivitymodeGetRequest()

- SellerPromotionActivitymodeGetRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionActivitymodeGetResponse (class)

<c>jingdong.seller.promotion.activitymode.get</c> 的响应。

- public ActivityModeVO activity_mode;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionAddRequest (class)

<c>jingdong.seller.promotion.add</c> 的请求。

- JdRequest req;

- public SellerPromotionAddRequest()

- SellerPromotionAddRequest Name(string name)
  - 设置 <c>name</c> 参数。

- SellerPromotionAddRequest Type(int type)
  - 设置 <c>type</c> 参数。

- SellerPromotionAddRequest BeginTime(string beginTime)
  - 设置 <c>begin_time</c> 参数。

- SellerPromotionAddRequest EndTime(string endTime)
  - 设置 <c>end_time</c> 参数。

- SellerPromotionAddRequest Bound(int bound)
  - 设置 <c>bound</c> 参数。

- SellerPromotionAddRequest Member(int member)
  - 设置 <c>member</c> 参数。

- SellerPromotionAddRequest Slogan(string slogan)
  - 设置 <c>slogan</c> 参数。

- SellerPromotionAddRequest Comment(string comment)
  - 设置 <c>comment</c> 参数。

- SellerPromotionAddRequest FavorMode(int favorMode)
  - 设置 <c>favor_mode</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionAddResponse (class)

<c>jingdong.seller.promotion.add</c> 的响应。

- public long promo_id;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionCheckRequest (class)

<c>jingdong.seller.promotion.check</c> 的请求。

- JdRequest req;

- public SellerPromotionCheckRequest()

- SellerPromotionCheckRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- SellerPromotionCheckRequest Status(int status)
  - 设置 <c>status</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionCheckResponse (class)

<c>jingdong.seller.promotion.check</c> 的响应。

- public int count;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionCommitRequest (class)

<c>jingdong.seller.promotion.commit</c> 的请求。

- JdRequest req;

- public SellerPromotionCommitRequest()

- SellerPromotionCommitRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionCommitResponse (class)

<c>jingdong.seller.promotion.commit</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionDeleteRequest (class)

<c>jingdong.seller.promotion.delete</c> 的请求。

- JdRequest req;

- public SellerPromotionDeleteRequest()

- SellerPromotionDeleteRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionDeleteResponse (class)

<c>jingdong.seller.promotion.delete</c> 的响应。

- public int count;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionGetRequest (class)

<c>jingdong.seller.promotion.get</c> 的请求。

- JdRequest req;

- public SellerPromotionGetRequest()

- SellerPromotionGetRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionGetResponse (class)

<c>jingdong.seller.promotion.get</c> 的响应。

- public PromotionVO promotion_v_o;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionListRequest (class)

<c>jingdong.seller.promotion.list</c> 的请求。

- JdRequest req;

- public SellerPromotionListRequest()

- SellerPromotionListRequest Type(int type)
  - 设置 <c>type</c> 参数。

- SellerPromotionListRequest Status(int status)
  - 设置 <c>status</c> 参数。

- SellerPromotionListRequest BeginTime(string beginTime)
  - 设置 <c>begin_time</c> 参数。

- SellerPromotionListRequest EndTime(string endTime)
  - 设置 <c>end_time</c> 参数。

- SellerPromotionListRequest SkuId(long skuId)
  - 设置 <c>sku_id</c> 参数。

- SellerPromotionListRequest FavorMode(int favorMode)
  - 设置 <c>favor_mode</c> 参数。

- SellerPromotionListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- SellerPromotionListRequest Size(string size)
  - 设置 <c>size</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionListResponse (class)

<c>jingdong.seller.promotion.list</c> 的响应。

- public int total_count;

- public List<string> promotion_v_o_s;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionOrdermodeListRequest (class)

<c>jingdong.seller.promotion.ordermode.list</c> 的请求。

- JdRequest req;

- public SellerPromotionOrdermodeListRequest()

- SellerPromotionOrdermodeListRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionOrdermodeListResponse (class)

<c>jingdong.seller.promotion.ordermode.list</c> 的响应。

- public List<string> promo_order_mode_v_os;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionSkuAddRequest (class)

<c>jingdong.seller.promotion.sku.add</c> 的请求。

- JdRequest req;

- public SellerPromotionSkuAddRequest()

- SellerPromotionSkuAddRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- SellerPromotionSkuAddRequest SkuIds(string skuIds)
  - 设置 <c>sku_ids</c> 参数。

- SellerPromotionSkuAddRequest JdPrices(string jdPrices)
  - 设置 <c>jd_prices</c> 参数。

- SellerPromotionSkuAddRequest PromoPrices(string promoPrices)
  - 设置 <c>promo_prices</c> 参数。

- SellerPromotionSkuAddRequest Seq(string seq)
  - 设置 <c>seq</c> 参数。

- SellerPromotionSkuAddRequest Num(string num)
  - 设置 <c>num</c> 参数。

- SellerPromotionSkuAddRequest BindType(string bindType)
  - 设置 <c>bind_type</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionSkuAddResponse (class)

<c>jingdong.seller.promotion.sku.add</c> 的响应。

- public List<string> ids;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionV2CountRequest (class)

<c>jingdong.seller.promotion.v2.count</c> 的请求。

- JdRequest req;

- public SellerPromotionV2CountRequest()

- SellerPromotionV2CountRequest Ip(string ip)
  - 设置 <c>ip</c> 参数。

- SellerPromotionV2CountRequest Port(string port)
  - 设置 <c>port</c> 参数。

- SellerPromotionV2CountRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- SellerPromotionV2CountRequest Name(string name)
  - 设置 <c>name</c> 参数。

- SellerPromotionV2CountRequest Type(int type)
  - 设置 <c>type</c> 参数。

- SellerPromotionV2CountRequest FavorMode(int favorMode)
  - 设置 <c>favor_mode</c> 参数。

- SellerPromotionV2CountRequest BeginTime(string beginTime)
  - 设置 <c>begin_time</c> 参数。

- SellerPromotionV2CountRequest EndTime(string endTime)
  - 设置 <c>end_time</c> 参数。

- SellerPromotionV2CountRequest PromoStatus(int promoStatus)
  - 设置 <c>promo_status</c> 参数。

- SellerPromotionV2CountRequest WareId(long wareId)
  - 设置 <c>ware_id</c> 参数。

- SellerPromotionV2CountRequest SkuId(long skuId)
  - 设置 <c>sku_id</c> 参数。

- SellerPromotionV2CountRequest SrcType(int srcType)
  - 设置 <c>src_type</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionV2CountResponse (class)

<c>jingdong.seller.promotion.v2.count</c> 的响应。

- public int promotion_count;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionV2GetRequest (class)

<c>jingdong.seller.promotion.v2.get</c> 的请求。

- JdRequest req;

- public SellerPromotionV2GetRequest()

- SellerPromotionV2GetRequest Ip(string ip)
  - 设置 <c>ip</c> 参数。

- SellerPromotionV2GetRequest Port(string port)
  - 设置 <c>port</c> 参数。

- SellerPromotionV2GetRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- SellerPromotionV2GetRequest PromoType(int promoType)
  - 设置 <c>promo_type</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionV2GetResponse (class)

<c>jingdong.seller.promotion.v2.get</c> 的响应。

- public JosPromotion jos_promotion;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionV2ListRequest (class)

<c>jingdong.seller.promotion.v2.list</c> 的请求。

- JdRequest req;

- public SellerPromotionV2ListRequest()

- SellerPromotionV2ListRequest Ip(string ip)
  - 设置 <c>ip</c> 参数。

- SellerPromotionV2ListRequest Port(string port)
  - 设置 <c>port</c> 参数。

- SellerPromotionV2ListRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- SellerPromotionV2ListRequest Name(string name)
  - 设置 <c>name</c> 参数。

- SellerPromotionV2ListRequest Type(int type)
  - 设置 <c>type</c> 参数。

- SellerPromotionV2ListRequest FavorMode(int favorMode)
  - 设置 <c>favor_mode</c> 参数。

- SellerPromotionV2ListRequest BeginTime(string beginTime)
  - 设置 <c>begin_time</c> 参数。

- SellerPromotionV2ListRequest EndTime(string endTime)
  - 设置 <c>end_time</c> 参数。

- SellerPromotionV2ListRequest PromoStatus(int promoStatus)
  - 设置 <c>promo_status</c> 参数。

- SellerPromotionV2ListRequest WareId(long wareId)
  - 设置 <c>ware_id</c> 参数。

- SellerPromotionV2ListRequest SkuId(long skuId)
  - 设置 <c>sku_id</c> 参数。

- SellerPromotionV2ListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- SellerPromotionV2ListRequest PageSSize(string pageSSize)
  - 设置 <c>pageS_size</c> 参数。

- SellerPromotionV2ListRequest SrcType(int srcType)
  - 设置 <c>src_type</c> 参数。

- SellerPromotionV2ListRequest StartId(long startId)
  - 设置 <c>start_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionV2ListResponse (class)

<c>jingdong.seller.promotion.v2.list</c> 的响应。

- public List<string> promotion_list;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionV2SkuListRequest (class)

<c>jingdong.seller.promotion.v2.sku.list</c> 的请求。

- JdRequest req;

- public SellerPromotionV2SkuListRequest()

- SellerPromotionV2SkuListRequest Ip(string ip)
  - 设置 <c>ip</c> 参数。

- SellerPromotionV2SkuListRequest Port(string port)
  - 设置 <c>port</c> 参数。

- SellerPromotionV2SkuListRequest PromoId(long promoId)
  - 设置 <c>promo_id</c> 参数。

- SellerPromotionV2SkuListRequest WareId(long wareId)
  - 设置 <c>ware_id</c> 参数。

- SellerPromotionV2SkuListRequest SkuId(long skuId)
  - 设置 <c>sku_id</c> 参数。

- SellerPromotionV2SkuListRequest BindType(int bindType)
  - 设置 <c>bind_type</c> 参数。

- SellerPromotionV2SkuListRequest PromoType(int promoType)
  - 设置 <c>promo_type</c> 参数。

- SellerPromotionV2SkuListRequest Page(string page)
  - 设置 <c>page</c> 参数。

- SellerPromotionV2SkuListRequest PageSSize(string pageSSize)
  - 设置 <c>pageS_size</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionV2SkuListResponse (class)

<c>jingdong.seller.promotion.v2.sku.list</c> 的响应。

- public List<string> promotion_sku_list;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerPromotionV2UnitFullCreateRequest (class)

<c>jingdong.seller.promotion.v2.unit.full.create</c> 的请求。

- JdRequest req;

- public SellerPromotionV2UnitFullCreateRequest()

- SellerPromotionV2UnitFullCreateRequest Ip(string ip)
  - 设置 <c>ip</c> 参数。

- SellerPromotionV2UnitFullCreateRequest Port(string port)
  - 设置 <c>port</c> 参数。

- SellerPromotionV2UnitFullCreateRequest RequestId(string requestId)
  - 设置 <c>request_id</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PromoName(string promoName)
  - 设置 <c>promo_name</c> 参数。

- SellerPromotionV2UnitFullCreateRequest BeginTime(string beginTime)
  - 设置 <c>begin_time</c> 参数。

- SellerPromotionV2UnitFullCreateRequest EndTime(string endTime)
  - 设置 <c>end_time</c> 参数。

- SellerPromotionV2UnitFullCreateRequest Slogan(string slogan)
  - 设置 <c>slogan</c> 参数。

- SellerPromotionV2UnitFullCreateRequest Comment(string comment)
  - 设置 <c>comment</c> 参数。

- SellerPromotionV2UnitFullCreateRequest Link(string link)
  - 设置 <c>link</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PlusMember(int plusMember)
  - 设置 <c>plusMember</c> 参数。

- SellerPromotionV2UnitFullCreateRequest AllowOthersOperate(string allowOthersOperate)
  - 设置 <c>allow_others_operate</c> 参数。

- SellerPromotionV2UnitFullCreateRequest AllowOthersCheck(string allowOthersCheck)
  - 设置 <c>allow_others_check</c> 参数。

- SellerPromotionV2UnitFullCreateRequest AllowOtherUserOperate(string allowOtherUserOperate)
  - 设置 <c>allow_other_user_operate</c> 参数。

- SellerPromotionV2UnitFullCreateRequest AllowOtherUserCheck(string allowOtherUserCheck)
  - 设置 <c>allow_other_user_check</c> 参数。

- SellerPromotionV2UnitFullCreateRequest NeedManualCheck(string needManualCheck)
  - 设置 <c>need_manual_check</c> 参数。

- SellerPromotionV2UnitFullCreateRequest FreqBound(int freqBound)
  - 设置 <c>freq_bound</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PerMaxNum(int perMaxNum)
  - 设置 <c>per_max_num</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PerMinNum(int perMinNum)
  - 设置 <c>per_min_num</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PropType(int propType)
  - 设置 <c>prop_type</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PropNum(int propNum)
  - 设置 <c>prop_num</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PropUsedWay(int propUsedWay)
  - 设置 <c>prop_used_way</c> 参数。

- SellerPromotionV2UnitFullCreateRequest CouponValidDays(int couponValidDays)
  - 设置 <c>coupon_valid_days</c> 参数。

- SellerPromotionV2UnitFullCreateRequest TokenUseNum(int tokenUseNum)
  - 设置 <c>token_use_num</c> 参数。

- SellerPromotionV2UnitFullCreateRequest UserPins(string userPins)
  - 设置 <c>user_pins</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PromoAreaType(int promoAreaType)
  - 设置 <c>promo_area_type</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PromoAreas(string promoAreas)
  - 设置 <c>promo_areas</c> 参数。

- SellerPromotionV2UnitFullCreateRequest SkuId(string skuId)
  - 设置 <c>sku_id</c> 参数。

- SellerPromotionV2UnitFullCreateRequest PromoPrice(string promoPrice)
  - 设置 <c>promo_price</c> 参数。

- SellerPromotionV2UnitFullCreateRequest LimitNum(string limitNum)
  - 设置 <c>limit_num</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerPromotionV2UnitFullCreateResponse (class)

<c>jingdong.seller.promotion.v2.unit.full.create</c> 的响应。

- public long promo_id;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## SellerVenderInfoGetRequest (class)

<c>jingdong.seller.vender.info.get</c> 的请求。

- JdRequest req;

- public SellerVenderInfoGetRequest()

- SellerVenderInfoGetRequest ExtJsonParam(string extJsonParam)
  - 设置 <c>ext_json_param</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SellerVenderInfoGetResponse (class)

<c>jingdong.seller.vender.info.get</c> 的响应。

- public VenderInfoResult vender_info_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
