# Sdk.Jd.Api.Areas

> 源码: `stdlib/Sdk/Jd/Api/Areas/AreasCityGetRequest.zan`, `stdlib/Sdk/Jd/Api/Areas/AreasProvinceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Areas/JdAreasApi.zan`


## AreasCityGetRequest (class)

<c>jingdong.areas.city.get</c> 的请求。

- JdRequest req;

- public AreasCityGetRequest()

- AreasCityGetRequest ParentId(int parentId)
  - 设置 <c>parent_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AreasCityGetResponse (class)

<c>jingdong.areas.city.get</c> 的响应。

- public BaseAreaServiceResponse baseAreaServiceResponse;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AreasProvinceGetRequest (class)

<c>jingdong.areas.province.get</c> 的请求。

- JdRequest req;

- public AreasProvinceGetRequest()

- JdRequest Raw()
  - 底层协议请求。


## AreasProvinceGetResponse (class)

<c>jingdong.areas.province.get</c> 的响应。

- public BaseAreaServiceResponse baseAreaServiceResponse;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdAreasApi (class)

jingdong.areas.* 的强类型客户端。

- JdClient client;

- public JdAreasApi(JdClient client)

- async AreasCityGetResponse CityGetAsync(AreasCityGetRequest request)
  - 执行 <c>jingdong.areas.city.get</c>。

- async AreasProvinceGetResponse ProvinceGetAsync(AreasProvinceGetRequest request)
  - 执行 <c>jingdong.areas.province.get</c>。
