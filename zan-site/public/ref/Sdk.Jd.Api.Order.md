# Sdk.Jd.Api.Order

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Order/JdOrderApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Order/OrderVenderRemarkQueryByOrderIdRequest.zan`


## JdOrderApi (class)

- JdClient client;

- public JdOrderApi(JdClient client)

- async OrderVenderRemarkQueryByOrderIdResponse VenderRemarkQueryByOrderIdAsync(OrderVenderRemarkQueryByOrderIdRequest request)


## OrderVenderRemarkQueryByOrderIdRequest (class)

- JdRequest req;

- public OrderVenderRemarkQueryByOrderIdRequest()

- OrderVenderRemarkQueryByOrderIdRequest OrderId(string orderId)

- JdRequest Raw()


## OrderVenderRemarkQueryByOrderIdResponse (class)

- public VenderRemarkQueryResult venderRemarkQueryResult;

- public string Raw;
