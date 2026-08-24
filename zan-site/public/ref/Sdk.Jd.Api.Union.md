# Sdk.Jd.Api.Union

> 源码: `stdlib/Sdk/Jd/Api/Union/JdUnionApi.zan`, `stdlib/Sdk/Jd/Api/Union/UnionPopOpenApiDetailOrdersV1Request.zan`


## JdUnionApi (class)

jingdong.union.* 的强类型客户端。

- JdClient client;

- public JdUnionApi(JdClient client)

- async UnionPopOpenApiDetailOrdersV1Response PopOpenApiDetailOrdersV1Async(UnionPopOpenApiDetailOrdersV1Request request)
  - 执行 <c>jingdong.union.pop.open.api.detail.orders.v1</c>。


## UnionPopOpenApiDetailOrdersV1Request (class)

<c>jingdong.union.pop.open.api.detail.orders.v1</c> 的请求。

- JdRequest req;

- public UnionPopOpenApiDetailOrdersV1Request()

- UnionPopOpenApiDetailOrdersV1Request JosRemoteIp(string josRemoteIp)
  - 设置 <c>josRemoteIp</c> 参数。

- UnionPopOpenApiDetailOrdersV1Request AccessPin(string accessPin)
  - 设置 <c>accessPin</c> 参数。

- UnionPopOpenApiDetailOrdersV1Request AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- UnionPopOpenApiDetailOrdersV1Request QueryJsonString(string queryJsonString)
  - 设置 <c>queryJsonString</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## UnionPopOpenApiDetailOrdersV1Response (class)

<c>jingdong.union.pop.open.api.detail.orders.v1</c> 的响应。

- public OpenApiRes data;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
