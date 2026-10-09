# Sdk.Jd.Api.Seller

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/JdSellerApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerCouponReadGetCouponCountRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerCouponReadGetCouponListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionActivitymodeAddRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionActivitymodeGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionAddRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionCheckRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionCommitRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionDeleteRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionOrdermodeListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionSkuAddRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionV2CountRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionV2GetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionV2ListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionV2SkuListRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerPromotionV2UnitFullCreateRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Seller/SellerVenderInfoGetRequest.zan`


## JdSellerApi (class)

- JdClient client;

- public JdSellerApi(JdClient client)

- async SellerCouponReadGetCouponCountResponse CouponReadGetCouponCountAsync(SellerCouponReadGetCouponCountRequest request)

- async SellerCouponReadGetCouponListResponse CouponReadGetCouponListAsync(SellerCouponReadGetCouponListRequest request)

- async SellerPromotionActivitymodeAddResponse PromotionActivitymodeAddAsync(SellerPromotionActivitymodeAddRequest request)

- async SellerPromotionActivitymodeGetResponse PromotionActivitymodeGetAsync(SellerPromotionActivitymodeGetRequest request)

- async SellerPromotionAddResponse PromotionAddAsync(SellerPromotionAddRequest request)

- async SellerPromotionCheckResponse PromotionCheckAsync(SellerPromotionCheckRequest request)

- async SellerPromotionCommitResponse PromotionCommitAsync(SellerPromotionCommitRequest request)

- async SellerPromotionDeleteResponse PromotionDeleteAsync(SellerPromotionDeleteRequest request)

- async SellerPromotionGetResponse PromotionGetAsync(SellerPromotionGetRequest request)

- async SellerPromotionListResponse PromotionListAsync(SellerPromotionListRequest request)

- async SellerPromotionOrdermodeListResponse PromotionOrdermodeListAsync(SellerPromotionOrdermodeListRequest request)

- async SellerPromotionSkuAddResponse PromotionSkuAddAsync(SellerPromotionSkuAddRequest request)

- async SellerPromotionV2CountResponse PromotionV2CountAsync(SellerPromotionV2CountRequest request)

- async SellerPromotionV2GetResponse PromotionV2GetAsync(SellerPromotionV2GetRequest request)

- async SellerPromotionV2ListResponse PromotionV2ListAsync(SellerPromotionV2ListRequest request)

- async SellerPromotionV2SkuListResponse PromotionV2SkuListAsync(SellerPromotionV2SkuListRequest request)

- async SellerPromotionV2UnitFullCreateResponse PromotionV2UnitFullCreateAsync(SellerPromotionV2UnitFullCreateRequest request)

- async SellerVenderInfoGetResponse VenderInfoGetAsync(SellerVenderInfoGetRequest request)


## SellerCouponReadGetCouponCountRequest (class)

- JdRequest req;

- public SellerCouponReadGetCouponCountRequest()

- SellerCouponReadGetCouponCountRequest Ip(string ip)

- SellerCouponReadGetCouponCountRequest Port(string port)

- SellerCouponReadGetCouponCountRequest CouponId(long couponId)

- SellerCouponReadGetCouponCountRequest Type(int type)

- SellerCouponReadGetCouponCountRequest GrantType(int grantType)

- SellerCouponReadGetCouponCountRequest BindType(int bindType)

- SellerCouponReadGetCouponCountRequest GrantWay(int grantWay)

- SellerCouponReadGetCouponCountRequest Name(string name)

- SellerCouponReadGetCouponCountRequest CreateMonth(string createMonth)

- SellerCouponReadGetCouponCountRequest CreatorType(int creatorType)

- SellerCouponReadGetCouponCountRequest Closed(int closed)

- JdRequest Raw()


## SellerCouponReadGetCouponCountResponse (class)

- public int getcouponcount_result;

- public string Raw;


## SellerCouponReadGetCouponListRequest (class)

- JdRequest req;

- public SellerCouponReadGetCouponListRequest()

- SellerCouponReadGetCouponListRequest Ip(string ip)

- SellerCouponReadGetCouponListRequest Port(string port)

- SellerCouponReadGetCouponListRequest CouponId(long couponId)

- SellerCouponReadGetCouponListRequest Type(int type)

- SellerCouponReadGetCouponListRequest GrantType(int grantType)

- SellerCouponReadGetCouponListRequest BindType(int bindType)

- SellerCouponReadGetCouponListRequest GrantWay(int grantWay)

- SellerCouponReadGetCouponListRequest Name(string name)

- SellerCouponReadGetCouponListRequest CreateMonth(string createMonth)

- SellerCouponReadGetCouponListRequest CreatorType(int creatorType)

- SellerCouponReadGetCouponListRequest Closed(int closed)

- SellerCouponReadGetCouponListRequest Page(int page)

- SellerCouponReadGetCouponListRequest PageSize(int pageSize)

- JdRequest Raw()


## SellerCouponReadGetCouponListResponse (class)

- public List<string> couponList;

- public string Raw;


## SellerPromotionActivitymodeAddRequest (class)

- JdRequest req;

- public SellerPromotionActivitymodeAddRequest()

- SellerPromotionActivitymodeAddRequest PromoId(long promoId)

- SellerPromotionActivitymodeAddRequest NumBound(int numBound)

- SellerPromotionActivitymodeAddRequest FreqBound(int freqBound)

- SellerPromotionActivitymodeAddRequest PerMaxNum(int perMaxNum)

- SellerPromotionActivitymodeAddRequest PerMinNum(int perMinNum)

- JdRequest Raw()


## SellerPromotionActivitymodeAddResponse (class)

- public long id;

- public string Raw;


## SellerPromotionActivitymodeGetRequest (class)

- JdRequest req;

- public SellerPromotionActivitymodeGetRequest()

- SellerPromotionActivitymodeGetRequest PromoId(long promoId)

- JdRequest Raw()


## SellerPromotionActivitymodeGetResponse (class)

- public ActivityModeVO activity_mode;

- public string Raw;


## SellerPromotionAddRequest (class)

- JdRequest req;

- public SellerPromotionAddRequest()

- SellerPromotionAddRequest Name(string name)

- SellerPromotionAddRequest Type(int type)

- SellerPromotionAddRequest BeginTime(string beginTime)

- SellerPromotionAddRequest EndTime(string endTime)

- SellerPromotionAddRequest Bound(int bound)

- SellerPromotionAddRequest Member(int member)

- SellerPromotionAddRequest Slogan(string slogan)

- SellerPromotionAddRequest Comment(string comment)

- SellerPromotionAddRequest FavorMode(int favorMode)

- JdRequest Raw()


## SellerPromotionAddResponse (class)

- public long promo_id;

- public string Raw;


## SellerPromotionCheckRequest (class)

- JdRequest req;

- public SellerPromotionCheckRequest()

- SellerPromotionCheckRequest PromoId(long promoId)

- SellerPromotionCheckRequest Status(int status)

- JdRequest Raw()


## SellerPromotionCheckResponse (class)

- public int count;

- public string Raw;


## SellerPromotionCommitRequest (class)

- JdRequest req;

- public SellerPromotionCommitRequest()

- SellerPromotionCommitRequest PromoId(long promoId)

- JdRequest Raw()


## SellerPromotionCommitResponse (class)

- public bool success;

- public string Raw;


## SellerPromotionDeleteRequest (class)

- JdRequest req;

- public SellerPromotionDeleteRequest()

- SellerPromotionDeleteRequest PromoId(long promoId)

- JdRequest Raw()


## SellerPromotionDeleteResponse (class)

- public int count;

- public string Raw;


## SellerPromotionGetRequest (class)

- JdRequest req;

- public SellerPromotionGetRequest()

- SellerPromotionGetRequest PromoId(long promoId)

- JdRequest Raw()


## SellerPromotionGetResponse (class)

- public PromotionVO promotion_v_o;

- public string Raw;


## SellerPromotionListRequest (class)

- JdRequest req;

- public SellerPromotionListRequest()

- SellerPromotionListRequest Type(int type)

- SellerPromotionListRequest Status(int status)

- SellerPromotionListRequest BeginTime(string beginTime)

- SellerPromotionListRequest EndTime(string endTime)

- SellerPromotionListRequest SkuId(long skuId)

- SellerPromotionListRequest FavorMode(int favorMode)

- SellerPromotionListRequest Page(string page)

- SellerPromotionListRequest Size(string size)

- JdRequest Raw()


## SellerPromotionListResponse (class)

- public int total_count;

- public List<string> promotion_v_o_s;

- public string Raw;


## SellerPromotionOrdermodeListRequest (class)

- JdRequest req;

- public SellerPromotionOrdermodeListRequest()

- SellerPromotionOrdermodeListRequest PromoId(long promoId)

- JdRequest Raw()


## SellerPromotionOrdermodeListResponse (class)

- public List<string> promo_order_mode_v_os;

- public string Raw;


## SellerPromotionSkuAddRequest (class)

- JdRequest req;

- public SellerPromotionSkuAddRequest()

- SellerPromotionSkuAddRequest PromoId(long promoId)

- SellerPromotionSkuAddRequest SkuIds(string skuIds)

- SellerPromotionSkuAddRequest JdPrices(string jdPrices)

- SellerPromotionSkuAddRequest PromoPrices(string promoPrices)

- SellerPromotionSkuAddRequest Seq(string seq)

- SellerPromotionSkuAddRequest Num(string num)

- SellerPromotionSkuAddRequest BindType(string bindType)

- JdRequest Raw()


## SellerPromotionSkuAddResponse (class)

- public List<string> ids;

- public string Raw;


## SellerPromotionV2CountRequest (class)

- JdRequest req;

- public SellerPromotionV2CountRequest()

- SellerPromotionV2CountRequest Ip(string ip)

- SellerPromotionV2CountRequest Port(string port)

- SellerPromotionV2CountRequest PromoId(long promoId)

- SellerPromotionV2CountRequest Name(string name)

- SellerPromotionV2CountRequest Type(int type)

- SellerPromotionV2CountRequest FavorMode(int favorMode)

- SellerPromotionV2CountRequest BeginTime(string beginTime)

- SellerPromotionV2CountRequest EndTime(string endTime)

- SellerPromotionV2CountRequest PromoStatus(int promoStatus)

- SellerPromotionV2CountRequest WareId(long wareId)

- SellerPromotionV2CountRequest SkuId(long skuId)

- SellerPromotionV2CountRequest SrcType(int srcType)

- JdRequest Raw()


## SellerPromotionV2CountResponse (class)

- public int promotion_count;

- public string Raw;


## SellerPromotionV2GetRequest (class)

- JdRequest req;

- public SellerPromotionV2GetRequest()

- SellerPromotionV2GetRequest Ip(string ip)

- SellerPromotionV2GetRequest Port(string port)

- SellerPromotionV2GetRequest PromoId(long promoId)

- SellerPromotionV2GetRequest PromoType(int promoType)

- JdRequest Raw()


## SellerPromotionV2GetResponse (class)

- public JosPromotion jos_promotion;

- public string Raw;


## SellerPromotionV2ListRequest (class)

- JdRequest req;

- public SellerPromotionV2ListRequest()

- SellerPromotionV2ListRequest Ip(string ip)

- SellerPromotionV2ListRequest Port(string port)

- SellerPromotionV2ListRequest PromoId(long promoId)

- SellerPromotionV2ListRequest Name(string name)

- SellerPromotionV2ListRequest Type(int type)

- SellerPromotionV2ListRequest FavorMode(int favorMode)

- SellerPromotionV2ListRequest BeginTime(string beginTime)

- SellerPromotionV2ListRequest EndTime(string endTime)

- SellerPromotionV2ListRequest PromoStatus(int promoStatus)

- SellerPromotionV2ListRequest WareId(long wareId)

- SellerPromotionV2ListRequest SkuId(long skuId)

- SellerPromotionV2ListRequest Page(string page)

- SellerPromotionV2ListRequest PageSSize(string pageSSize)

- SellerPromotionV2ListRequest SrcType(int srcType)

- SellerPromotionV2ListRequest StartId(long startId)

- JdRequest Raw()


## SellerPromotionV2ListResponse (class)

- public List<string> promotion_list;

- public string Raw;


## SellerPromotionV2SkuListRequest (class)

- JdRequest req;

- public SellerPromotionV2SkuListRequest()

- SellerPromotionV2SkuListRequest Ip(string ip)

- SellerPromotionV2SkuListRequest Port(string port)

- SellerPromotionV2SkuListRequest PromoId(long promoId)

- SellerPromotionV2SkuListRequest WareId(long wareId)

- SellerPromotionV2SkuListRequest SkuId(long skuId)

- SellerPromotionV2SkuListRequest BindType(int bindType)

- SellerPromotionV2SkuListRequest PromoType(int promoType)

- SellerPromotionV2SkuListRequest Page(string page)

- SellerPromotionV2SkuListRequest PageSSize(string pageSSize)

- JdRequest Raw()


## SellerPromotionV2SkuListResponse (class)

- public List<string> promotion_sku_list;

- public string Raw;


## SellerPromotionV2UnitFullCreateRequest (class)

- JdRequest req;

- public SellerPromotionV2UnitFullCreateRequest()

- SellerPromotionV2UnitFullCreateRequest Ip(string ip)

- SellerPromotionV2UnitFullCreateRequest Port(string port)

- SellerPromotionV2UnitFullCreateRequest RequestId(string requestId)

- SellerPromotionV2UnitFullCreateRequest PromoName(string promoName)

- SellerPromotionV2UnitFullCreateRequest BeginTime(string beginTime)

- SellerPromotionV2UnitFullCreateRequest EndTime(string endTime)

- SellerPromotionV2UnitFullCreateRequest Slogan(string slogan)

- SellerPromotionV2UnitFullCreateRequest Comment(string comment)

- SellerPromotionV2UnitFullCreateRequest Link(string link)

- SellerPromotionV2UnitFullCreateRequest PlusMember(int plusMember)

- SellerPromotionV2UnitFullCreateRequest AllowOthersOperate(string allowOthersOperate)

- SellerPromotionV2UnitFullCreateRequest AllowOthersCheck(string allowOthersCheck)

- SellerPromotionV2UnitFullCreateRequest AllowOtherUserOperate(string allowOtherUserOperate)

- SellerPromotionV2UnitFullCreateRequest AllowOtherUserCheck(string allowOtherUserCheck)

- SellerPromotionV2UnitFullCreateRequest NeedManualCheck(string needManualCheck)

- SellerPromotionV2UnitFullCreateRequest FreqBound(int freqBound)

- SellerPromotionV2UnitFullCreateRequest PerMaxNum(int perMaxNum)

- SellerPromotionV2UnitFullCreateRequest PerMinNum(int perMinNum)

- SellerPromotionV2UnitFullCreateRequest PropType(int propType)

- SellerPromotionV2UnitFullCreateRequest PropNum(int propNum)

- SellerPromotionV2UnitFullCreateRequest PropUsedWay(int propUsedWay)

- SellerPromotionV2UnitFullCreateRequest CouponValidDays(int couponValidDays)

- SellerPromotionV2UnitFullCreateRequest TokenUseNum(int tokenUseNum)

- SellerPromotionV2UnitFullCreateRequest UserPins(string userPins)

- SellerPromotionV2UnitFullCreateRequest PromoAreaType(int promoAreaType)

- SellerPromotionV2UnitFullCreateRequest PromoAreas(string promoAreas)

- SellerPromotionV2UnitFullCreateRequest SkuId(string skuId)

- SellerPromotionV2UnitFullCreateRequest PromoPrice(string promoPrice)

- SellerPromotionV2UnitFullCreateRequest LimitNum(string limitNum)

- JdRequest Raw()


## SellerPromotionV2UnitFullCreateResponse (class)

- public long promo_id;

- public string Raw;


## SellerVenderInfoGetRequest (class)

- JdRequest req;

- public SellerVenderInfoGetRequest()

- SellerVenderInfoGetRequest ExtJsonParam(string extJsonParam)

- JdRequest Raw()


## SellerVenderInfoGetResponse (class)

- public VenderInfoResult vender_info_result;

- public string Raw;
