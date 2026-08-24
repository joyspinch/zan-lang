# Sdk.Jd.Api.Jzt

> 源码: `stdlib/Sdk/Jd/Api/Jzt/JdJztApi.zan`, `stdlib/Sdk/Jd/Api/Jzt/JztFindAllSubPinsRequest.zan`


## JdJztApi (class)

jingdong.jzt.* 的强类型客户端。

- JdClient client;

- public JdJztApi(JdClient client)

- async JztFindAllSubPinsResponse FindAllSubPinsAsync(JztFindAllSubPinsRequest request)
  - 执行 <c>jingdong.jzt.findAllSubPins</c>。


## JztFindAllSubPinsRequest (class)

<c>jingdong.jzt.findAllSubPins</c> 的请求。

- JdRequest req;

- public JztFindAllSubPinsRequest()

- JdRequest Raw()
  - 底层协议请求。


## JztFindAllSubPinsResponse (class)

<c>jingdong.jzt.findAllSubPins</c> 的响应。

- public DailiSoaResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
