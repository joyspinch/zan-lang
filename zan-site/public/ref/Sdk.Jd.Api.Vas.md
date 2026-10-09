# Sdk.Jd.Api.Vas

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vas/JdVasApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vas/VasSubscribeGetByCodeRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Vas/VasSubscribeGetRequest.zan`


## JdVasApi (class)

- JdClient client;

- public JdVasApi(JdClient client)

- async VasSubscribeGetByCodeResponse SubscribeGetByCodeAsync(VasSubscribeGetByCodeRequest request)

- async VasSubscribeGetResponse SubscribeGetAsync(VasSubscribeGetRequest request)


## VasSubscribeGetByCodeRequest (class)

- JdRequest req;

- public VasSubscribeGetByCodeRequest()

- VasSubscribeGetByCodeRequest ItemCode(string itemCode)

- JdRequest Raw()


## VasSubscribeGetByCodeResponse (class)

- public string item_code;

- public string end_date;

- public string Raw;


## VasSubscribeGetRequest (class)

- JdRequest req;

- public VasSubscribeGetRequest()

- VasSubscribeGetRequest UserName(string userName)

- VasSubscribeGetRequest ItemCode(string itemCode)

- VasSubscribeGetRequest OpenIdBuyer(string openIdBuyer)

- VasSubscribeGetRequest XidBuyer(string xidBuyer)

- JdRequest Raw()


## VasSubscribeGetResponse (class)

- public string item_code;

- public string end_date;

- public string Raw;
