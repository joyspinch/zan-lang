# Sdk.Jd.Api.Fce

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Fce/FceAlphaGetVenderCarrierRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Fce/JdFceApi.zan`


## FceAlphaGetVenderCarrierRequest (class)

- JdRequest req;

- public FceAlphaGetVenderCarrierRequest()

- JdRequest Raw()


## FceAlphaGetVenderCarrierResponse (class)

- public StandardGenericResponse StandardGenericResponse;

- public string Raw;


## JdFceApi (class)

- JdClient client;

- public JdFceApi(JdClient client)

- async FceAlphaGetVenderCarrierResponse AlphaGetVenderCarrierAsync(FceAlphaGetVenderCarrierRequest request)
