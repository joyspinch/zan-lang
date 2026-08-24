# Sdk.Jd.Api.Area

> 源码: `stdlib/Sdk/Jd/Api/Area/AreaProvinceGetRequest.zan`, `stdlib/Sdk/Jd/Api/Area/JdAreaApi.zan`


## AreaProvinceGetRequest (class)

<c>jingdong.area.province.get</c> 的请求。

- JdRequest req;

- public AreaProvinceGetRequest()

- JdRequest Raw()
  - 底层协议请求。


## AreaProvinceGetResponse (class)

<c>jingdong.area.province.get</c> 的响应。

- public List<AreaListBeanVO> province_areas;

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdAreaApi (class)

jingdong.area.* 的强类型客户端。

- JdClient client;

- public JdAreaApi(JdClient client)

- async AreaProvinceGetResponse ProvinceGetAsync(AreaProvinceGetRequest request)
  - 执行 <c>jingdong.area.province.get</c>。
