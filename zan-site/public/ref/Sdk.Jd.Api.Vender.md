# Sdk.Jd.Api.Vender

> 源码: `stdlib/Sdk/Jd/Api/Vender/JdVenderApi.zan`, `stdlib/Sdk/Jd/Api/Vender/VenderAuthFindUserRequest.zan`, `stdlib/Sdk/Jd/Api/Vender/VenderCategoryGetFullValidCategoryResultByVenderIdRequest.zan`, `stdlib/Sdk/Jd/Api/Vender/VenderCategoryGetValidCategoryResultByVenderIdRequest.zan`, `stdlib/Sdk/Jd/Api/Vender/VenderInfoQueryByPinRequest.zan`, `stdlib/Sdk/Jd/Api/Vender/VenderShipaddressQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Vender/VenderShopQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Vender/VenderShopcategoryGetShopCategorysByVenderIdRequest.zan`, `stdlib/Sdk/Jd/Api/Vender/VenderVbinfoGetBasicVenderInfoByVenderIdRequest.zan`


## JdVenderApi (class)

jingdong.vender.* 的强类型客户端。

- JdClient client;

- public JdVenderApi(JdClient client)

- async VenderAuthFindUserResponse AuthFindUserAsync(VenderAuthFindUserRequest request)
  - 执行 <c>jingdong.vender.auth.findUser</c>。

- async VenderCategoryGetFullValidCategoryResultByVenderIdResponse CategoryGetFullValidCategoryResultByVenderIdAsync(VenderCategoryGetFullValidCategoryResultByVenderIdRequest request)
  - 执行 <c>jingdong.vender.category.getFullValidCategoryResultByVenderId</c>。

- async VenderCategoryGetValidCategoryResultByVenderIdResponse CategoryGetValidCategoryResultByVenderIdAsync(VenderCategoryGetValidCategoryResultByVenderIdRequest request)
  - 执行 <c>jingdong.vender.category.getValidCategoryResultByVenderId</c>。

- async VenderInfoQueryByPinResponse InfoQueryByPinAsync(VenderInfoQueryByPinRequest request)
  - 执行 <c>jingdong.vender.info.queryByPin</c>。

- async VenderShipaddressQueryResponse ShipaddressQueryAsync(VenderShipaddressQueryRequest request)
  - 执行 <c>jingdong.vender.shipaddress.query</c>。

- async VenderShopQueryResponse ShopQueryAsync(VenderShopQueryRequest request)
  - 执行 <c>jingdong.vender.shop.query</c>。

- async VenderShopcategoryGetShopCategorysByVenderIdResponse ShopcategoryGetShopCategorysByVenderIdAsync(VenderShopcategoryGetShopCategorysByVenderIdRequest request)
  - 执行 <c>jingdong.vender.shopcategory.getShopCategorysByVenderId</c>。

- async VenderVbinfoGetBasicVenderInfoByVenderIdResponse VbinfoGetBasicVenderInfoByVenderIdAsync(VenderVbinfoGetBasicVenderInfoByVenderIdRequest request)
  - 执行 <c>jingdong.vender.vbinfo.getBasicVenderInfoByVenderId</c>。


## VenderAuthFindUserRequest (class)

<c>jingdong.vender.auth.findUser</c> 的请求。

- JdRequest req;

- public VenderAuthFindUserRequest()

- VenderAuthFindUserRequest Pin(string pin)
  - 设置 <c>pin</c> 参数。

- VenderAuthFindUserRequest OpenIdSeller(string openIdSeller)
  - 设置 <c>open_id_seller</c> 参数。

- VenderAuthFindUserRequest XidSeller(string xidSeller)
  - 设置 <c>xid_seller</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## VenderAuthFindUserResponse (class)

<c>jingdong.vender.auth.findUser</c> 的响应。

- public AuthLoginResult result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## VenderCategoryGetFullValidCategoryResultByVenderIdRequest (class)

<c>jingdong.vender.category.getFullValidCategoryResultByVenderId</c> 的请求。

- JdRequest req;

- public VenderCategoryGetFullValidCategoryResultByVenderIdRequest()

- JdRequest Raw()
  - 底层协议请求。


## VenderCategoryGetFullValidCategoryResultByVenderIdResponse (class)

<c>jingdong.vender.category.getFullValidCategoryResultByVenderId</c> 的响应。

- public CategoryResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## VenderCategoryGetValidCategoryResultByVenderIdRequest (class)

<c>jingdong.vender.category.getValidCategoryResultByVenderId</c> 的请求。

- JdRequest req;

- public VenderCategoryGetValidCategoryResultByVenderIdRequest()

- JdRequest Raw()
  - 底层协议请求。


## VenderCategoryGetValidCategoryResultByVenderIdResponse (class)

<c>jingdong.vender.category.getValidCategoryResultByVenderId</c> 的响应。

- public CategoryResult getvalidcategoryresultbyvenderid_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## VenderInfoQueryByPinRequest (class)

<c>jingdong.vender.info.queryByPin</c> 的请求。

- JdRequest req;

- public VenderInfoQueryByPinRequest()

- VenderInfoQueryByPinRequest ExtJsonParam(string extJsonParam)
  - 设置 <c>ext_json_param</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## VenderInfoQueryByPinResponse (class)

<c>jingdong.vender.info.queryByPin</c> 的响应。

- public VenderInfoResult vender_info_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## VenderShipaddressQueryRequest (class)

<c>jingdong.vender.shipaddress.query</c> 的请求。

- JdRequest req;

- public VenderShipaddressQueryRequest()

- JdRequest Raw()
  - 底层协议请求。


## VenderShipaddressQueryResponse (class)

<c>jingdong.vender.shipaddress.query</c> 的响应。

- public ShipAddressResult returnAddressResult;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## VenderShopQueryRequest (class)

<c>jingdong.vender.shop.query</c> 的请求。

- JdRequest req;

- public VenderShopQueryRequest()

- JdRequest Raw()
  - 底层协议请求。


## VenderShopQueryResponse (class)

<c>jingdong.vender.shop.query</c> 的响应。

- public ShopJosResult shop_jos_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## VenderShopcategoryGetShopCategorysByVenderIdRequest (class)

<c>jingdong.vender.shopcategory.getShopCategorysByVenderId</c> 的请求。

- JdRequest req;

- public VenderShopcategoryGetShopCategorysByVenderIdRequest()

- JdRequest Raw()
  - 底层协议请求。


## VenderShopcategoryGetShopCategorysByVenderIdResponse (class)

<c>jingdong.vender.shopcategory.getShopCategorysByVenderId</c> 的响应。

- public ShopCategoryResult getshopcategorysbyvenderid_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## VenderVbinfoGetBasicVenderInfoByVenderIdRequest (class)

<c>jingdong.vender.vbinfo.getBasicVenderInfoByVenderId</c> 的请求。

- JdRequest req;

- public VenderVbinfoGetBasicVenderInfoByVenderIdRequest()

- VenderVbinfoGetBasicVenderInfoByVenderIdRequest ColNames(string colNames)
  - 设置 <c>colNames</c> 参数。

- VenderVbinfoGetBasicVenderInfoByVenderIdRequest Source(string source)
  - 设置 <c>source</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## VenderVbinfoGetBasicVenderInfoByVenderIdResponse (class)

<c>jingdong.vender.vbinfo.getBasicVenderInfoByVenderId</c> 的响应。

- public VenderBasicResult getbasicvenderinfobyvenderid_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
