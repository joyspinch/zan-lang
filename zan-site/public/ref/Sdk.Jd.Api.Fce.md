# Sdk.Jd.Api.Fce

> 源码: `stdlib/Sdk/Jd/Api/Fce/FceAlphaGetVenderCarrierRequest.zan`, `stdlib/Sdk/Jd/Api/Fce/JdFceApi.zan`


## FceAlphaGetVenderCarrierRequest (class)

<c>jingdong.fce.alpha.getVenderCarrier</c> 的请求。

- JdRequest req;

- public FceAlphaGetVenderCarrierRequest()

- JdRequest Raw()
  - 底层协议请求。


## FceAlphaGetVenderCarrierResponse (class)

<c>jingdong.fce.alpha.getVenderCarrier</c> 的响应。

- public StandardGenericResponse StandardGenericResponse;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdFceApi (class)

jingdong.fce.* 的强类型客户端。

- JdClient client;

- public JdFceApi(JdClient client)

- async FceAlphaGetVenderCarrierResponse AlphaGetVenderCarrierAsync(FceAlphaGetVenderCarrierRequest request)
  - 执行 <c>jingdong.fce.alpha.getVenderCarrier</c>。
