# Sdk.Jd.Api.Pop

> 源码: `stdlib/Sdk/Jd/Api/Pop/JdPopApi.zan`, `stdlib/Sdk/Jd/Api/Pop/PopAfsPriceprotectDetailRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopCrmGetShopRuleTypeRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopCrmMembertypeGetTypeRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopFwOrderListwithpageRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopJmCenterUserGetOpenIdRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopOrderEnGetRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopOrderEnSearchRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopOrderEncryptMobileNumRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopOrderFbpSearchRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopOrderGetRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopOrderGetmobilelistRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopOrderModifyVenderRemarkRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopOrderSearchRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopPopCommentJsfServiceGetVenderCommentsForJosRequest.zan`, `stdlib/Sdk/Jd/Api/Pop/PopVenderCenerVenderBrandQueryRequest.zan`


## JdPopApi (class)

jingdong.pop.* 的强类型客户端。

- JdClient client;

- public JdPopApi(JdClient client)

- async PopAfsPriceprotectDetailResponse AfsPriceprotectDetailAsync(PopAfsPriceprotectDetailRequest request)
  - 执行 <c>jingdong.pop.afs.priceprotect.detail</c>。

- async PopCrmGetShopRuleTypeResponse CrmGetShopRuleTypeAsync(PopCrmGetShopRuleTypeRequest request)
  - 执行 <c>jingdong.pop.crm.getShopRuleType</c>。

- async PopCrmMembertypeGetTypeResponse CrmMembertypeGetTypeAsync(PopCrmMembertypeGetTypeRequest request)
  - 执行 <c>jingdong.pop.crm.membertype.getType</c>。

- async PopFwOrderListwithpageResponse FwOrderListwithpageAsync(PopFwOrderListwithpageRequest request)
  - 执行 <c>jingdong.pop.fw.order.listwithpage</c>。

- async PopJmCenterUserGetOpenIdResponse JmCenterUserGetOpenIdAsync(PopJmCenterUserGetOpenIdRequest request)
  - 执行 <c>jingdong.pop.jm.center.user.getOpenId</c>。

- async PopOrderEnGetResponse OrderEnGetAsync(PopOrderEnGetRequest request)
  - 执行 <c>jingdong.pop.order.enGet</c>。

- async PopOrderEnSearchResponse OrderEnSearchAsync(PopOrderEnSearchRequest request)
  - 执行 <c>jingdong.pop.order.enSearch</c>。

- async PopOrderEncryptMobileNumResponse OrderEncryptMobileNumAsync(PopOrderEncryptMobileNumRequest request)
  - 执行 <c>jingdong.pop.order.encryptMobileNum</c>。

- async PopOrderFbpSearchResponse OrderFbpSearchAsync(PopOrderFbpSearchRequest request)
  - 执行 <c>jingdong.pop.order.fbp.search</c>。

- async PopOrderGetResponse OrderGetAsync(PopOrderGetRequest request)
  - 执行 <c>jingdong.pop.order.get</c>。

- async PopOrderGetmobilelistResponse OrderGetmobilelistAsync(PopOrderGetmobilelistRequest request)
  - 执行 <c>jingdong.pop.order.getmobilelist</c>。

- async PopOrderModifyVenderRemarkResponse OrderModifyVenderRemarkAsync(PopOrderModifyVenderRemarkRequest request)
  - 执行 <c>jingdong.pop.order.modifyVenderRemark</c>。

- async PopOrderSearchResponse OrderSearchAsync(PopOrderSearchRequest request)
  - 执行 <c>jingdong.pop.order.search</c>。

- async PopPopCommentJsfServiceGetVenderCommentsForJosResponse PopCommentJsfServiceGetVenderCommentsForJosAsync(PopPopCommentJsfServiceGetVenderCommentsForJosRequest request)
  - 执行 <c>jingdong.pop.PopCommentJsfService.getVenderCommentsForJos</c>。

- async PopVenderCenerVenderBrandQueryResponse VenderCenerVenderBrandQueryAsync(PopVenderCenerVenderBrandQueryRequest request)
  - 执行 <c>jingdong.pop.vender.cener.venderBrand.query</c>。


## PopAfsPriceprotectDetailRequest (class)

<c>jingdong.pop.afs.priceprotect.detail</c> 的请求。

- JdRequest req;

- public PopAfsPriceprotectDetailRequest()

- PopAfsPriceprotectDetailRequest PricePrtctType(int pricePrtctType)
  - 设置 <c>pricePrtctType</c> 参数。

- PopAfsPriceprotectDetailRequest Uuid(long uuid)
  - 设置 <c>uuid</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopAfsPriceprotectDetailResponse (class)

<c>jingdong.pop.afs.priceprotect.detail</c> 的响应。

- public PublicResult response;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopCrmGetShopRuleTypeRequest (class)

<c>jingdong.pop.crm.getShopRuleType</c> 的请求。

- JdRequest req;

- public PopCrmGetShopRuleTypeRequest()

- JdRequest Raw()
  - 底层协议请求。


## PopCrmGetShopRuleTypeResponse (class)

<c>jingdong.pop.crm.getShopRuleType</c> 的响应。

- public ReturnResult returnResult;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopCrmMembertypeGetTypeRequest (class)

<c>jingdong.pop.crm.membertype.getType</c> 的请求。

- JdRequest req;

- public PopCrmMembertypeGetTypeRequest()

- JdRequest Raw()
  - 底层协议请求。


## PopCrmMembertypeGetTypeResponse (class)

<c>jingdong.pop.crm.membertype.getType</c> 的响应。

- public ReturnResult returnResult;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopFwOrderListwithpageRequest (class)

<c>jingdong.pop.fw.order.listwithpage</c> 的请求。

- JdRequest req;

- public PopFwOrderListwithpageRequest()

- PopFwOrderListwithpageRequest PageSize(int pageSize)
  - 设置 <c>pageSize</c> 参数。

- PopFwOrderListwithpageRequest FwsPin(string fwsPin)
  - 设置 <c>fwsPin</c> 参数。

- PopFwOrderListwithpageRequest CurrentPage(int currentPage)
  - 设置 <c>currentPage</c> 参数。

- PopFwOrderListwithpageRequest ServiceCode(string serviceCode)
  - 设置 <c>serviceCode</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopFwOrderListwithpageResponse (class)

<c>jingdong.pop.fw.order.listwithpage</c> 的响应。

- public PageResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopJmCenterUserGetOpenIdRequest (class)

<c>jingdong.pop.jm.center.user.getOpenId</c> 的请求。

- JdRequest req;

- public PopJmCenterUserGetOpenIdRequest()

- PopJmCenterUserGetOpenIdRequest Source(string source)
  - 设置 <c>source</c> 参数。

- PopJmCenterUserGetOpenIdRequest Token(string token)
  - 设置 <c>token</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopJmCenterUserGetOpenIdResponse (class)

<c>jingdong.pop.jm.center.user.getOpenId</c> 的响应。

- public Result returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopOrderEnGetRequest (class)

<c>jingdong.pop.order.enGet</c> 的请求。

- JdRequest req;

- public PopOrderEnGetRequest()

- PopOrderEnGetRequest OrderState(string orderState)
  - 设置 <c>order_state</c> 参数。

- PopOrderEnGetRequest OptionalFields(string optionalFields)
  - 设置 <c>optional_fields</c> 参数。

- PopOrderEnGetRequest OrderId(string orderId)
  - 设置 <c>order_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopOrderEnGetResponse (class)

<c>jingdong.pop.order.enGet</c> 的响应。

- public OrderResult orderDetailInfo;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopOrderEnSearchRequest (class)

<c>jingdong.pop.order.enSearch</c> 的请求。

- JdRequest req;

- public PopOrderEnSearchRequest()

- PopOrderEnSearchRequest StartDate(string startDate)
  - 设置 <c>start_date</c> 参数。

- PopOrderEnSearchRequest EndDate(string endDate)
  - 设置 <c>end_date</c> 参数。

- PopOrderEnSearchRequest OrderState(string orderState)
  - 设置 <c>order_state</c> 参数。

- PopOrderEnSearchRequest OptionalFields(string optionalFields)
  - 设置 <c>optional_fields</c> 参数。

- PopOrderEnSearchRequest Page(string page)
  - 设置 <c>page</c> 参数。

- PopOrderEnSearchRequest PageSize(string pageSize)
  - 设置 <c>page_size</c> 参数。

- PopOrderEnSearchRequest SortType(string sortType)
  - 设置 <c>sortType</c> 参数。

- PopOrderEnSearchRequest DateType(string dateType)
  - 设置 <c>dateType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopOrderEnSearchResponse (class)

<c>jingdong.pop.order.enSearch</c> 的响应。

- public OrderListResult searchorderinfo_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopOrderEncryptMobileNumRequest (class)

<c>jingdong.pop.order.encryptMobileNum</c> 的请求。

- JdRequest req;

- public PopOrderEncryptMobileNumRequest()

- PopOrderEncryptMobileNumRequest Mobile(string mobile)
  - 设置 <c>mobile</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopOrderEncryptMobileNumResponse (class)

<c>jingdong.pop.order.encryptMobileNum</c> 的响应。

- public ResponseData result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopOrderFbpSearchRequest (class)

<c>jingdong.pop.order.fbp.search</c> 的请求。

- JdRequest req;

- public PopOrderFbpSearchRequest()

- PopOrderFbpSearchRequest StartDate(string startDate)
  - 设置 <c>startDate</c> 参数。

- PopOrderFbpSearchRequest EndDate(string endDate)
  - 设置 <c>endDate</c> 参数。

- PopOrderFbpSearchRequest OrderState(string orderState)
  - 设置 <c>orderState</c> 参数。

- PopOrderFbpSearchRequest Page(string page)
  - 设置 <c>page</c> 参数。

- PopOrderFbpSearchRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- PopOrderFbpSearchRequest ColType(string colType)
  - 设置 <c>colType</c> 参数。

- PopOrderFbpSearchRequest OptionalFields(string optionalFields)
  - 设置 <c>optionalFields</c> 参数。

- PopOrderFbpSearchRequest OrderId(string orderId)
  - 设置 <c>orderId</c> 参数。

- PopOrderFbpSearchRequest SortType(string sortType)
  - 设置 <c>sortType</c> 参数。

- PopOrderFbpSearchRequest DateType(string dateType)
  - 设置 <c>dateType</c> 参数。

- PopOrderFbpSearchRequest StoreId(string storeId)
  - 设置 <c>storeId</c> 参数。

- PopOrderFbpSearchRequest Cky2(string cky2)
  - 设置 <c>cky2</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopOrderFbpSearchResponse (class)

<c>jingdong.pop.order.fbp.search</c> 的响应。

- public OrderInfoResult searchfbporderinfo_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopOrderGetRequest (class)

<c>jingdong.pop.order.get</c> 的请求。

- JdRequest req;

- public PopOrderGetRequest()

- PopOrderGetRequest OrderState(string orderState)
  - 设置 <c>order_state</c> 参数。

- PopOrderGetRequest OptionalFields(string optionalFields)
  - 设置 <c>optional_fields</c> 参数。

- PopOrderGetRequest OrderId(string orderId)
  - 设置 <c>order_id</c> 参数。

- PopOrderGetRequest RealPin(string realPin)
  - 设置 <c>realPin</c> 参数。

- PopOrderGetRequest OpenIdBuyer(string openIdBuyer)
  - 设置 <c>open_id_buyer</c> 参数。

- PopOrderGetRequest XidBuyer(string xidBuyer)
  - 设置 <c>xid_buyer</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopOrderGetResponse (class)

<c>jingdong.pop.order.get</c> 的响应。

- public OrderResult orderDetailInfo;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopOrderGetmobilelistRequest (class)

<c>jingdong.pop.order.getmobilelist</c> 的请求。

- JdRequest req;

- public PopOrderGetmobilelistRequest()

- PopOrderGetmobilelistRequest AppName(string appName)
  - 设置 <c>appName</c> 参数。

- PopOrderGetmobilelistRequest Region(string region)
  - 设置 <c>region</c> 参数。

- PopOrderGetmobilelistRequest OrderId(string orderId)
  - 设置 <c>orderId</c> 参数。

- PopOrderGetmobilelistRequest Expiration(int expiration)
  - 设置 <c>expiration</c> 参数。

- PopOrderGetmobilelistRequest OrderType(string orderType)
  - 设置 <c>orderType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopOrderGetmobilelistResponse (class)

<c>jingdong.pop.order.getmobilelist</c> 的响应。

- public ResponseData result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopOrderModifyVenderRemarkRequest (class)

<c>jingdong.pop.order.modifyVenderRemark</c> 的请求。

- JdRequest req;

- public PopOrderModifyVenderRemarkRequest()

- PopOrderModifyVenderRemarkRequest OrderId(long orderId)
  - 设置 <c>order_id</c> 参数。

- PopOrderModifyVenderRemarkRequest Flag(string flag)
  - 设置 <c>flag</c> 参数。

- PopOrderModifyVenderRemarkRequest Remark(string remark)
  - 设置 <c>remark</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopOrderModifyVenderRemarkResponse (class)

<c>jingdong.pop.order.modifyVenderRemark</c> 的响应。

- public OperatorResult modifyvenderremark_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopOrderSearchRequest (class)

<c>jingdong.pop.order.search</c> 的请求。

- JdRequest req;

- public PopOrderSearchRequest()

- PopOrderSearchRequest StartDate(string startDate)
  - 设置 <c>start_date</c> 参数。

- PopOrderSearchRequest EndDate(string endDate)
  - 设置 <c>end_date</c> 参数。

- PopOrderSearchRequest OrderState(string orderState)
  - 设置 <c>order_state</c> 参数。

- PopOrderSearchRequest OptionalFields(string optionalFields)
  - 设置 <c>optional_fields</c> 参数。

- PopOrderSearchRequest Page(string page)
  - 设置 <c>page</c> 参数。

- PopOrderSearchRequest PageSize(string pageSize)
  - 设置 <c>page_size</c> 参数。

- PopOrderSearchRequest SortType(string sortType)
  - 设置 <c>sortType</c> 参数。

- PopOrderSearchRequest DateType(string dateType)
  - 设置 <c>dateType</c> 参数。

- PopOrderSearchRequest RealPin(string realPin)
  - 设置 <c>realPin</c> 参数。

- PopOrderSearchRequest OpenIdBuyer(string openIdBuyer)
  - 设置 <c>open_id_buyer</c> 参数。

- PopOrderSearchRequest XidBuyer(string xidBuyer)
  - 设置 <c>xid_buyer</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopOrderSearchResponse (class)

<c>jingdong.pop.order.search</c> 的响应。

- public OrderListResult searchorderinfo_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopPopCommentJsfServiceGetVenderCommentsForJosRequest (class)

<c>jingdong.pop.PopCommentJsfService.getVenderCommentsForJos</c> 的请求。

- JdRequest req;

- public PopPopCommentJsfServiceGetVenderCommentsForJosRequest()

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Skuids(string skuids)
  - 设置 <c>skuids</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest WareName(string wareName)
  - 设置 <c>wareName</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest BeginTime(string beginTime)
  - 设置 <c>beginTime</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest EndTime(string endTime)
  - 设置 <c>endTime</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Score(string score)
  - 设置 <c>score</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Content(string content)
  - 设置 <c>content</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Pin(string pin)
  - 设置 <c>pin</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest IsVenderReply(bool isVenderReply)
  - 设置 <c>isVenderReply</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Cid(string cid)
  - 设置 <c>cid</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest OrderIds(string orderIds)
  - 设置 <c>orderIds</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest Page(string page)
  - 设置 <c>page</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest PageSize(string pageSize)
  - 设置 <c>pageSize</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest OpenIdBuyer(string openIdBuyer)
  - 设置 <c>open_id_buyer</c> 参数。

- PopPopCommentJsfServiceGetVenderCommentsForJosRequest XidBuyer(string xidBuyer)
  - 设置 <c>xid_buyer</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopPopCommentJsfServiceGetVenderCommentsForJosResponse (class)

<c>jingdong.pop.PopCommentJsfService.getVenderCommentsForJos</c> 的响应。

- public List<string> comments;

- public int totalItem;

- public int page;

- public string resultCode;

- public string resultMsg;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## PopVenderCenerVenderBrandQueryRequest (class)

<c>jingdong.pop.vender.cener.venderBrand.query</c> 的请求。

- JdRequest req;

- public PopVenderCenerVenderBrandQueryRequest()

- PopVenderCenerVenderBrandQueryRequest Name(string name)
  - 设置 <c>name</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## PopVenderCenerVenderBrandQueryResponse (class)

<c>jingdong.pop.vender.cener.venderBrand.query</c> 的响应。

- public List<string> brandList;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
