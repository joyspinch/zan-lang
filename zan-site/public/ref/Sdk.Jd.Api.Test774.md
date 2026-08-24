# Sdk.Jd.Api.Test774

> 源码: `stdlib/Sdk/Jd/Api/Test774/JdTest774Api.zan`, `stdlib/Sdk/Jd/Api/Test774/Test774Request.zan`


## JdTest774Api (class)

jingdong.test774.* 的强类型客户端。

- JdClient client;

- public JdTest774Api(JdClient client)

- async Test774Response Async(Test774Request request)
  - 执行 <c>jingdong.test774</c>。


## Test774Request (class)

<c>jingdong.test774</c> 的请求。

- JdRequest req;

- public Test774Request()

- Test774Request Param1(string param1)
  - 设置 <c>param1</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## Test774Response (class)

<c>jingdong.test774</c> 的响应。

- public TestResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
