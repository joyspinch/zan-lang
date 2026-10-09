# Sdk.Jd.Api.Union

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Union/JdUnionApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Union/UnionPopOpenApiDetailOrdersV1Request.zan`


## JdUnionApi (class)

- JdClient client;

- public JdUnionApi(JdClient client)

- async UnionPopOpenApiDetailOrdersV1Response PopOpenApiDetailOrdersV1Async(UnionPopOpenApiDetailOrdersV1Request request)


## UnionPopOpenApiDetailOrdersV1Request (class)

- JdRequest req;

- public UnionPopOpenApiDetailOrdersV1Request()

- UnionPopOpenApiDetailOrdersV1Request JosRemoteIp(string josRemoteIp)

- UnionPopOpenApiDetailOrdersV1Request AccessPin(string accessPin)

- UnionPopOpenApiDetailOrdersV1Request AuthType(string authType)

- UnionPopOpenApiDetailOrdersV1Request QueryJsonString(string queryJsonString)

- JdRequest Raw()


## UnionPopOpenApiDetailOrdersV1Response (class)

- public OpenApiRes data;

- public string Raw;
