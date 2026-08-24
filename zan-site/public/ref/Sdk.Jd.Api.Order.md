# Sdk.Jd.Api.Order

> 源码: `stdlib/Sdk/Jd/Api/Order/JdOrderApi.zan`, `stdlib/Sdk/Jd/Api/Order/OrderVenderRemarkQueryByOrderIdRequest.zan`


## JdOrderApi (class)

jingdong.order.* 的强类型客户端。

- JdClient client;

- public JdOrderApi(JdClient client)

- async OrderVenderRemarkQueryByOrderIdResponse VenderRemarkQueryByOrderIdAsync(OrderVenderRemarkQueryByOrderIdRequest request)
  - 执行 <c>jingdong.order.venderRemark.queryByOrderId</c>。


## OrderVenderRemarkQueryByOrderIdRequest (class)

<c>jingdong.order.venderRemark.queryByOrderId</c> 的请求。

- JdRequest req;

- public OrderVenderRemarkQueryByOrderIdRequest()

- OrderVenderRemarkQueryByOrderIdRequest OrderId(string orderId)
  - 设置 <c>order_id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## OrderVenderRemarkQueryByOrderIdResponse (class)

<c>jingdong.order.venderRemark.queryByOrderId</c> 的响应。

- public VenderRemarkQueryResult venderRemarkQueryResult;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
