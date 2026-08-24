# Sdk.Jd.Api.Test

> 源码: `stdlib/Sdk/Jd/Api/Test/JdTestApi.zan`, `stdlib/Sdk/Jd/Api/Test/TestDemo1Request.zan`


## JdTestApi (class)

jingdong.test.* 的强类型客户端。

- JdClient client;

- public JdTestApi(JdClient client)

- async TestDemo1Response Demo1Async(TestDemo1Request request)
  - 执行 <c>shangling.test.demo1</c>。


## TestDemo1Request (class)

<c>shangling.test.demo1</c> 的请求。

- JdRequest req;

- public TestDemo1Request()

- TestDemo1Request BizTenantCode(string bizTenantCode)
  - 设置 <c>bizTenantCode</c> 参数。

- TestDemo1Request Name(string name)
  - 设置 <c>name</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## TestDemo1Response (class)

<c>shangling.test.demo1</c> 的响应。

- public BizTenantCodeBean returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
