# Sdk.Jd.Api.SkuFareTemplateService

> 源码: `stdlib/Sdk/Jd/Api/SkuFareTemplateService/JdSkuFareTemplateServiceApi.zan`, `stdlib/Sdk/Jd/Api/SkuFareTemplateService/SkuFareTemplateServiceGetTemplatesRequest.zan`


## JdSkuFareTemplateServiceApi (class)

jingdong.SkuFareTemplateService.* 的强类型客户端。

- JdClient client;

- public JdSkuFareTemplateServiceApi(JdClient client)

- async SkuFareTemplateServiceGetTemplatesResponse GetTemplatesAsync(SkuFareTemplateServiceGetTemplatesRequest request)
  - 执行 <c>jingdong.SkuFareTemplateService.getTemplates</c>。


## SkuFareTemplateServiceGetTemplatesRequest (class)

<c>jingdong.SkuFareTemplateService.getTemplates</c> 的请求。

- JdRequest req;

- public SkuFareTemplateServiceGetTemplatesRequest()

- JdRequest Raw()
  - 底层协议请求。


## SkuFareTemplateServiceGetTemplatesResponse (class)

<c>jingdong.SkuFareTemplateService.getTemplates</c> 的响应。

- public SkuFareTemplateResult query_skuFareTemplate_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
