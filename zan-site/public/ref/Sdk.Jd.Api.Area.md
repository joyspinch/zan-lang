# Sdk.Jd.Api.Area

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Area/AreaProvinceGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Area/JdAreaApi.zan`


## AreaProvinceGetRequest (class)

- JdRequest req;

- public AreaProvinceGetRequest()

- JdRequest Raw()


## AreaProvinceGetResponse (class)

- public List<AreaListBeanVO> province_areas;

- public bool success;

- public string Raw;


## JdAreaApi (class)

- JdClient client;

- public JdAreaApi(JdClient client)

- async AreaProvinceGetResponse ProvinceGetAsync(AreaProvinceGetRequest request)
