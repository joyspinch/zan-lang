# Sdk.Jd.Api.Transport

> 源码: `stdlib/Sdk/Jd/Api/Transport/JdTransportApi.zan`, `stdlib/Sdk/Jd/Api/Transport/TransportWriteUpdateWareTransportIdRequest.zan`


## JdTransportApi (class)

jingdong.transport.* 的强类型客户端。

- JdClient client;

- public JdTransportApi(JdClient client)

- async TransportWriteUpdateWareTransportIdResponse WriteUpdateWareTransportIdAsync(TransportWriteUpdateWareTransportIdRequest request)
  - 执行 <c>jingdong.transport.write.updateWareTransportId</c>。


## TransportWriteUpdateWareTransportIdRequest (class)

<c>jingdong.transport.write.updateWareTransportId</c> 的请求。

- JdRequest req;

- public TransportWriteUpdateWareTransportIdRequest()

- TransportWriteUpdateWareTransportIdRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- TransportWriteUpdateWareTransportIdRequest TransportId(long transportId)
  - 设置 <c>transportId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## TransportWriteUpdateWareTransportIdResponse (class)

<c>jingdong.transport.write.updateWareTransportId</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
