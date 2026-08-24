# Sdk.Jd.Api.Media

> 源码: `stdlib/Sdk/Jd/Api/Media/JdMediaApi.zan`, `stdlib/Sdk/Jd/Api/Media/MediaGetMaterialBySkuIdsRequest.zan`


## JdMediaApi (class)

jingdong.media.* 的强类型客户端。

- JdClient client;

- public JdMediaApi(JdClient client)

- async MediaGetMaterialBySkuIdsResponse GetMaterialBySkuIdsAsync(MediaGetMaterialBySkuIdsRequest request)
  - 执行 <c>jingdong.media.getMaterialBySkuIds</c>。


## MediaGetMaterialBySkuIdsRequest (class)

<c>jingdong.media.getMaterialBySkuIds</c> 的请求。

- JdRequest req;

- public MediaGetMaterialBySkuIdsRequest()

- MediaGetMaterialBySkuIdsRequest VenderId(long venderId)
  - 设置 <c>venderId</c> 参数。

- MediaGetMaterialBySkuIdsRequest CallEnd(int callEnd)
  - 设置 <c>callEnd</c> 参数。

- MediaGetMaterialBySkuIdsRequest SkuId(string skuId)
  - 设置 <c>skuId</c> 参数。

- MediaGetMaterialBySkuIdsRequest VideoType(string videoType)
  - 设置 <c>videoType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MediaGetMaterialBySkuIdsResponse (class)

<c>jingdong.media.getMaterialBySkuIds</c> 的响应。

- public List<string> returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
