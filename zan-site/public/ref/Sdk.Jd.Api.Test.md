# Sdk.Jd.Api.Test

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Test/JdTestApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Test/TestDemo1Request.zan`


## JdTestApi (class)

- JdClient client;

- public JdTestApi(JdClient client)

- async TestDemo1Response Demo1Async(TestDemo1Request request)


## TestDemo1Request (class)

- JdRequest req;

- public TestDemo1Request()

- TestDemo1Request BizTenantCode(string bizTenantCode)

- TestDemo1Request Name(string name)

- JdRequest Raw()


## TestDemo1Response (class)

- public BizTenantCodeBean returnType;

- public string Raw;
