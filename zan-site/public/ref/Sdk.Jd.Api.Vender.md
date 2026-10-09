# Sdk.Jd.Api.Vender

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vender/JdVenderApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vender/VenderAuthFindUserRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vender/VenderCategoryGetFullValidCategoryResultByVenderIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vender/VenderCategoryGetValidCategoryResultByVenderIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vender/VenderInfoQueryByPinRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vender/VenderShipaddressQueryRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vender/VenderShopQueryRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vender/VenderShopcategoryGetShopCategorysByVenderIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vender/VenderVbinfoGetBasicVenderInfoByVenderIdRequest.zan`


## JdVenderApi (class)

- JdClient client;

- public JdVenderApi(JdClient client)

- async VenderAuthFindUserResponse AuthFindUserAsync(VenderAuthFindUserRequest request)

- async VenderCategoryGetFullValidCategoryResultByVenderIdResponse CategoryGetFullValidCategoryResultByVenderIdAsync(VenderCategoryGetFullValidCategoryResultByVenderIdRequest request)

- async VenderCategoryGetValidCategoryResultByVenderIdResponse CategoryGetValidCategoryResultByVenderIdAsync(VenderCategoryGetValidCategoryResultByVenderIdRequest request)

- async VenderInfoQueryByPinResponse InfoQueryByPinAsync(VenderInfoQueryByPinRequest request)

- async VenderShipaddressQueryResponse ShipaddressQueryAsync(VenderShipaddressQueryRequest request)

- async VenderShopQueryResponse ShopQueryAsync(VenderShopQueryRequest request)

- async VenderShopcategoryGetShopCategorysByVenderIdResponse ShopcategoryGetShopCategorysByVenderIdAsync(VenderShopcategoryGetShopCategorysByVenderIdRequest request)

- async VenderVbinfoGetBasicVenderInfoByVenderIdResponse VbinfoGetBasicVenderInfoByVenderIdAsync(VenderVbinfoGetBasicVenderInfoByVenderIdRequest request)


## VenderAuthFindUserRequest (class)

- JdRequest req;

- public VenderAuthFindUserRequest()

- VenderAuthFindUserRequest Pin(string pin)

- VenderAuthFindUserRequest OpenIdSeller(string openIdSeller)

- VenderAuthFindUserRequest XidSeller(string xidSeller)

- JdRequest Raw()


## VenderAuthFindUserResponse (class)

- public AuthLoginResult result;

- public string Raw;


## VenderCategoryGetFullValidCategoryResultByVenderIdRequest (class)

- JdRequest req;

- public VenderCategoryGetFullValidCategoryResultByVenderIdRequest()

- JdRequest Raw()


## VenderCategoryGetFullValidCategoryResultByVenderIdResponse (class)

- public CategoryResult returnType;

- public string Raw;


## VenderCategoryGetValidCategoryResultByVenderIdRequest (class)

- JdRequest req;

- public VenderCategoryGetValidCategoryResultByVenderIdRequest()

- JdRequest Raw()


## VenderCategoryGetValidCategoryResultByVenderIdResponse (class)

- public CategoryResult getvalidcategoryresultbyvenderid_result;

- public string Raw;


## VenderInfoQueryByPinRequest (class)

- JdRequest req;

- public VenderInfoQueryByPinRequest()

- VenderInfoQueryByPinRequest ExtJsonParam(string extJsonParam)

- JdRequest Raw()


## VenderInfoQueryByPinResponse (class)

- public VenderInfoResult vender_info_result;

- public string Raw;


## VenderShipaddressQueryRequest (class)

- JdRequest req;

- public VenderShipaddressQueryRequest()

- JdRequest Raw()


## VenderShipaddressQueryResponse (class)

- public ShipAddressResult returnAddressResult;

- public string Raw;


## VenderShopQueryRequest (class)

- JdRequest req;

- public VenderShopQueryRequest()

- JdRequest Raw()


## VenderShopQueryResponse (class)

- public ShopJosResult shop_jos_result;

- public string Raw;


## VenderShopcategoryGetShopCategorysByVenderIdRequest (class)

- JdRequest req;

- public VenderShopcategoryGetShopCategorysByVenderIdRequest()

- JdRequest Raw()


## VenderShopcategoryGetShopCategorysByVenderIdResponse (class)

- public ShopCategoryResult getshopcategorysbyvenderid_result;

- public string Raw;


## VenderVbinfoGetBasicVenderInfoByVenderIdRequest (class)

- JdRequest req;

- public VenderVbinfoGetBasicVenderInfoByVenderIdRequest()

- VenderVbinfoGetBasicVenderInfoByVenderIdRequest ColNames(string colNames)

- VenderVbinfoGetBasicVenderInfoByVenderIdRequest Source(string source)

- JdRequest Raw()


## VenderVbinfoGetBasicVenderInfoByVenderIdResponse (class)

- public VenderBasicResult getbasicvenderinfobyvenderid_result;

- public string Raw;
