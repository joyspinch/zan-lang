# Sdk.Jd.Api.GetThirdPdfByOrderIdForVender

> 源码: `stdlib/Sdk/Jd/Api/GetThirdPdfByOrderIdForVender/GetThirdPdfByOrderIdForVenderRequest.zan`, `stdlib/Sdk/Jd/Api/GetThirdPdfByOrderIdForVender/JdGetThirdPdfByOrderIdForVenderApi.zan`


## GetThirdPdfByOrderIdForVenderRequest (class)

<c>jingdong.getThirdPdfByOrderIdForVender</c> 的请求。

- JdRequest req;

- public GetThirdPdfByOrderIdForVenderRequest()

- GetThirdPdfByOrderIdForVenderRequest OrderId(long orderId)
  - 设置 <c>orderId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## GetThirdPdfByOrderIdForVenderResponse (class)

<c>jingdong.getThirdPdfByOrderIdForVender</c> 的响应。

- public PoPdfDto poPdfDto;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdGetThirdPdfByOrderIdForVenderApi (class)

jingdong.getThirdPdfByOrderIdForVender.* 的强类型客户端。

- JdClient client;

- public JdGetThirdPdfByOrderIdForVenderApi(JdClient client)

- async GetThirdPdfByOrderIdForVenderResponse Async(GetThirdPdfByOrderIdForVenderRequest request)
  - 执行 <c>jingdong.getThirdPdfByOrderIdForVender</c>。
