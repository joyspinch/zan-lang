# Sdk.Jd.Api.Transport

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Transport/JdTransportApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Transport/TransportWriteUpdateWareTransportIdRequest.zan`


## JdTransportApi (class)

- JdClient client;

- public JdTransportApi(JdClient client)

- async TransportWriteUpdateWareTransportIdResponse WriteUpdateWareTransportIdAsync(TransportWriteUpdateWareTransportIdRequest request)


## TransportWriteUpdateWareTransportIdRequest (class)

- JdRequest req;

- public TransportWriteUpdateWareTransportIdRequest()

- TransportWriteUpdateWareTransportIdRequest WareId(long wareId)

- TransportWriteUpdateWareTransportIdRequest TransportId(long transportId)

- JdRequest Raw()


## TransportWriteUpdateWareTransportIdResponse (class)

- public bool success;

- public string Raw;
