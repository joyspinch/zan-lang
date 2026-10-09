# Sdk.Jd.Api.GetThirdPdfByOrderIdForVender

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/GetThirdPdfByOrderIdForVender/GetThirdPdfByOrderIdForVenderRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/GetThirdPdfByOrderIdForVender/JdGetThirdPdfByOrderIdForVenderApi.zan`


## GetThirdPdfByOrderIdForVenderRequest (class)

- JdRequest req;

- public GetThirdPdfByOrderIdForVenderRequest()

- GetThirdPdfByOrderIdForVenderRequest OrderId(long orderId)

- JdRequest Raw()


## GetThirdPdfByOrderIdForVenderResponse (class)

- public PoPdfDto poPdfDto;

- public string Raw;


## JdGetThirdPdfByOrderIdForVenderApi (class)

- JdClient client;

- public JdGetThirdPdfByOrderIdForVenderApi(JdClient client)

- async GetThirdPdfByOrderIdForVenderResponse Async(GetThirdPdfByOrderIdForVenderRequest request)
