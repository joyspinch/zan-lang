# Sdk.Jd.Api.Areas

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Areas/AreasCityGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Areas/AreasProvinceGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Areas/JdAreasApi.zan`


## AreasCityGetRequest (class)

- JdRequest req;

- public AreasCityGetRequest()

- AreasCityGetRequest ParentId(int parentId)

- JdRequest Raw()


## AreasCityGetResponse (class)

- public BaseAreaServiceResponse baseAreaServiceResponse;

- public string Raw;


## AreasProvinceGetRequest (class)

- JdRequest req;

- public AreasProvinceGetRequest()

- JdRequest Raw()


## AreasProvinceGetResponse (class)

- public BaseAreaServiceResponse baseAreaServiceResponse;

- public string Raw;


## JdAreasApi (class)

- JdClient client;

- public JdAreasApi(JdClient client)

- async AreasCityGetResponse CityGetAsync(AreasCityGetRequest request)

- async AreasProvinceGetResponse ProvinceGetAsync(AreasProvinceGetRequest request)
