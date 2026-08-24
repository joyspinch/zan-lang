# Sdk.Jd.Api.Arealimit

> 源码: `stdlib/Sdk/Jd/Api/Arealimit/ArealimitReadFindAreaLimitsByWareIdRequest.zan`, `stdlib/Sdk/Jd/Api/Arealimit/JdArealimitApi.zan`


## ArealimitReadFindAreaLimitsByWareIdRequest (class)

<c>jingdong.arealimit.read.findAreaLimitsByWareId</c> 的请求。

- JdRequest req;

- public ArealimitReadFindAreaLimitsByWareIdRequest()

- ArealimitReadFindAreaLimitsByWareIdRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- ArealimitReadFindAreaLimitsByWareIdRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ArealimitReadFindAreaLimitsByWareIdResponse (class)

<c>jingdong.arealimit.read.findAreaLimitsByWareId</c> 的响应。

- public List<string> wareAreaLimitList;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdArealimitApi (class)

jingdong.arealimit.* 的强类型客户端。

- JdClient client;

- public JdArealimitApi(JdClient client)

- async ArealimitReadFindAreaLimitsByWareIdResponse ReadFindAreaLimitsByWareIdAsync(ArealimitReadFindAreaLimitsByWareIdRequest request)
  - 执行 <c>jingdong.arealimit.read.findAreaLimitsByWareId</c>。
