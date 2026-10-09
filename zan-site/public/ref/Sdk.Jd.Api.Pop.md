# Sdk.Jd.Api.Pop

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/JdPopApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopAfsPriceprotectDetailRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopCrmGetShopRuleTypeRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopCrmMembertypeGetTypeRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopFwOrderListwithpageRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopJmCenterUserGetOpenIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopOrderEnGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopOrderEnSearchRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopOrderEncryptMobileNumRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopOrderFbpSearchRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopOrderGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopOrderGetmobilelistRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopOrderModifyVenderRemarkRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopOrderSearchRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopPopCommentJsfServiceGetVenderCommentsForJosRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Pop/PopVenderCenerVenderBrandQueryRequest.zan`


## JdPopApi (class)

- JdClient client;

- public JdPopApi(JdClient client)

- async PopAfsPriceprotectDetailResponse AfsPriceprotectDetailAsync(PopAfsPriceprotectDetailRequest request)

- async PopCrmGetShopRuleTypeResponse CrmGetShopRuleTypeAsync(PopCrmGetShopRuleTypeRequest request)

- async PopCrmMembertypeGetTypeResponse CrmMembertypeGetTypeAsync(PopCrmMembertypeGetTypeRequest request)

- async PopFwOrderListwithpageResponse FwOrderListwithpageAsync(PopFwOrderListwithpageRequest request)

- async PopJmCenterUserGetOpenIdResponse JmCenterUserGetOpenIdAsync(PopJmCenterUserGetOpenIdRequest request)

- async PopOrderEnGetResponse OrderEnGetAsync(PopOrderEnGetRequest request)

- async PopOrderEnSearchResponse OrderEnSearchAsync(PopOrderEnSearchRequest request)

- async PopOrderEncryptMobileNumResponse OrderEncryptMobileNumAsync(PopOrderEncryptMobileNumRequest request)

- async PopOrderFbpSearchResponse OrderFbpSearchAsync(PopOrderFbpSearchRequest request)

- async PopOrderGetResponse OrderGetAsync(PopOrderGetRequest request)

- async PopOrderGetmobilelistResponse OrderGetmobilelistAsync(PopOrderGetmobilelistRequest request)

- async PopOrderModifyVenderRemarkResponse OrderModifyVenderRemarkAsync(PopOrderModifyVenderRemarkRequest request)

- async PopOrderSearchResponse OrderSearchAsync(PopOrderSearchRequest request)

- async PopPopCommentJsfServiceGetVenderCommentsForJosResponse PopCommentJsfServiceGetVenderCommentsForJosAsync(PopPopCommentJsfServiceGetVenderCommentsForJosRequest request)

- async PopVenderCenerVenderBrandQueryResponse VenderCenerVenderBrandQueryAsync(PopVenderCenerVenderBrandQueryRequest request)


## PopAfsPriceprotectDetailRequest (class)

- JdRequest req;

- public PopAfsPriceprotectDetailRequest()

- PopAfsPriceprotectDetailRequest PricePrtctType(int pricePrtctType)

- PopAfsPriceprotectDetailRequest Uuid(long uuid)

- JdRequest Raw()


## PopAfsPriceprotectDetailResponse (class)

- public PublicResult response;

- public string Raw;


## PopCrmGetShopRuleTypeRequest (class)

- JdRequest req;

- public PopCrmGetShopRuleTypeRequest()

- JdRequest Raw()


## PopCrmGetShopRuleTypeResponse (class)

- public ReturnResult returnResult;

- public string Raw;


## PopCrmMembertypeGetTypeRequest (class)

- JdRequest req;

- public PopCrmMembertypeGetTypeRequest()

- JdRequest Raw()


## PopCrmMembertypeGetTypeResponse (class)

- public ReturnResult returnResult;

- public string Raw;


## PopFwOrderListwithpageRequest (class)

- JdRequest req;

- public PopFwOrderListwithpageRequest()

- PopFwOrderListwithpageRequest PageSize(int pageSize)

- PopFwOrderListwithpageRequest FwsPin(string fwsPin)

- PopFwOrderListwithpageRequest CurrentPage(int currentPage)

- PopFwOrderListwithpageRequest ServiceCode(string serviceCode)

- JdRequest Raw()


## PopFwOrderListwithpageResponse (class)

- public PageResult returnType;

- public string Raw;


## PopJmCenterUserGetOpenIdRequest (class)

- JdRequest req;

- public PopJmCenterUserGetOpenIdRequest()

- PopJmCenterUserGetOpenIdRequest Source(string source)

- PopJmCenterUserGetOpenIdRequest Token(string token)

- JdRequest Raw()


## PopJmCenterUserGetOpenIdResponse (class)

- public Result returnType;

- public string Raw;


## PopOrderEnGetRequest (class)

- JdRequest req;

- public PopOrderEnGetRequest()

- PopOrderEnGetRequest OrderState(string orderState)

- PopOrderEnGetRequest OptionalFields(string optionalFields)

- PopOrderEnGetRequest OrderId(string orderId)

- JdRequest Raw()


## PopOrderEnGetResponse (class)

- public OrderResult orderDetailInfo;

- public string Raw;


## PopOrderEnSearchRequest (class)

- JdRequest req;

- public PopOrderEnSearchRequest()

- PopOrderEnSearchRequest StartDate(string startDate)

- PopOrderEnSearchRequest EndDate(string endDate)

- PopOrderEnSearchRequest OrderState(string orderState)

- PopOrderEnSearchRequest OptionalFields(string optionalFields)

- PopOrderEnSearchRequest Page(string page)

- PopOrderEnSearchRequest PageSize(string pageSize)

- PopOrderEnSearchRequest SortType(string sortType)

- PopOrderEnSearchRequest DateType(string dateType)

- JdRequest Raw()


## PopOrderEnSearchResponse (class)

- public OrderListResult searchorderinfo_result;

- public string Raw;


## PopOrderEncryptMobileNumRequest (class)

- JdRequest req;

- public PopOrderEncryptMobileNumRequest()

- PopOrderEncryptMobileNumRequest Mobile(string mobile)

- JdRequest Raw()


## PopOrderEncryptMobileNumResponse (class)

- public ResponseData result;

- public string Raw;


## PopOrderFbpSearchRequest (class)

- JdRequest req;

- public PopOrderFbpSearchRequest()

- PopOrderFbpSearchRequest StartDate(string startDate)

- PopOrderFbpSearchRequest EndDate(string endDate)

- PopOrderFbpSearchRequest OrderState(string orderState)

- PopOrderFbpSearchRequest Page(string page)

- PopOrderFbpSearchRequest PageSize(string pageSize)

- PopOrderFbpSearchRequest ColType(string colType)

- PopOrderFbpSearchRequest OptionalFields(string optionalFields)

- PopOrderFbpSearchRequest OrderId(string orderId)

- PopOrderFbpSearchRequest SortType(string sortType)

- PopOrderFbpSearchRequest DateType(string dateType)

- PopOrderFbpSearchRequest StoreId(string storeId)

- PopOrderFbpSearchRequest Cky2(string cky2)

- JdRequest Raw()


## PopOrderFbpSearchResponse (class)

- public OrderInfoResult searchfbporderinfo_result;

- public string Raw;


## PopOrderGetRequest (class)

- JdRequest req;

- public PopOrderGetRequest()

- PopOrderGetRequest OrderState(string orderState)

- PopOrderGetRequest OptionalFields(string optionalFields)

- PopOrderGetRequest OrderId(string orderId)

- PopOrderGetRequest RealPin(string realPin)

- PopOrderGetRequest OpenIdBuyer(string openIdBuyer)

- PopOrderGetRequest XidBuyer(string xidBuyer)

- JdRequest Raw()


## PopOrderGetResponse (class)

- public OrderResult orderDetailInfo;

- public string Raw;


## PopOrderGetmobilelistRequest (class)

- JdRequest req;

- public PopOrderGetmobilelistRequest()

- PopOrderGetmobilelistRequest AppName(string appName)

- PopOrderGetmobilelistRequest Region(string region)

- PopOrderGetmobilelistRequest OrderId(string orderId)

- PopOrderGetmobilelistRequest Expiration(int expiration)

- PopOrderGetmobilelistRequest OrderType(string orderType)

- JdRequest Raw()


## PopOrderGetmobilelistResponse (class)

- public ResponseData result;

- public string Raw;


## PopOrderModifyVenderRemarkRequest (class)

- JdRequest req;

- public PopOrderModifyVenderRemarkRequest()

- PopOrderModifyVenderRemarkRequest OrderId(long orderId)

- PopOrderModifyVenderRemarkRequest Flag(string flag)

- PopOrderModifyVenderRemarkRequest Remark(string remark)

- JdRequest Raw()


## PopOrderModifyVenderRemarkResponse (class)

- public OperatorResult modifyvenderremark_result;

- public string Raw;


## PopOrderSearchRequest (class)

- JdRequest req;

- public PopOrderSearchRequest()

- PopOrderSearchRequest StartDate(string startDate)

- PopOrderSearchRequest EndDate(string endDate)

- PopOrderSearchRequest OrderState(string orderState)

- PopOrderSearchRequest OptionalFields(string optionalFields)

- PopOrderSearchRequest Page(string page)

- PopOrderSearchRequest PageSize(string pageSize)

- PopOrderSearchRequest SortType(string sortType)

- PopOrderSearchRequest DateType(string dateType)

- PopOrderSearchRequest RealPin(string realPin)

- PopOrderSearchRequest OpenIdBuyer(string openIdBuyer)

- PopOrderSearchRequest XidBuyer(string xidBuyer)

- JdRequest Raw()


## PopOrderSearchResponse (class)

- public OrderListResult searchorderinfo_result;

- public string Raw;


## PopPopCommentJsfServiceGetVenderCommentsForJosRequest (class)

- JdRequest req;

- public PopPopCommentJsfServiceGetVenderCommentsForJosRequest()

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Skuids(string skuids)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest WareName(string wareName)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest BeginTime(string beginTime)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest EndTime(string endTime)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Score(string score)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Content(string content)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Pin(string pin)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest IsVenderReply(bool isVenderReply)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Cid(string cid)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest OrderIds(string orderIds)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Page(string page)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest PageSize(string pageSize)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest OpenIdBuyer(string openIdBuyer)

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest XidBuyer(string xidBuyer)

- JdRequest Raw()


## PopPopCommentJsfServiceGetVenderCommentsForJosResponse (class)

- public List<string> comments;

- public int totalItem;

- public int page;

- public string resultCode;

- public string resultMsg;

- public string Raw;


## PopVenderCenerVenderBrandQueryRequest (class)

- JdRequest req;

- public PopVenderCenerVenderBrandQueryRequest()

- PopVenderCenerVenderBrandQueryRequest Name(string name)

- JdRequest Raw()


## PopVenderCenerVenderBrandQueryResponse (class)

- public List<string> brandList;

- public string Raw;
