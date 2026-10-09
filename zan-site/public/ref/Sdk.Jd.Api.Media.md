# Sdk.Jd.Api.Media

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Media/JdMediaApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Media/MediaGetMaterialBySkuIdsRequest.zan`


## JdMediaApi (class)

- JdClient client;

- public JdMediaApi(JdClient client)

- async MediaGetMaterialBySkuIdsResponse GetMaterialBySkuIdsAsync(MediaGetMaterialBySkuIdsRequest request)


## MediaGetMaterialBySkuIdsRequest (class)

- JdRequest req;

- public MediaGetMaterialBySkuIdsRequest()

- MediaGetMaterialBySkuIdsRequest VenderId(long venderId)

- MediaGetMaterialBySkuIdsRequest CallEnd(int callEnd)

- MediaGetMaterialBySkuIdsRequest SkuId(string skuId)

- MediaGetMaterialBySkuIdsRequest VideoType(string videoType)

- JdRequest Raw()


## MediaGetMaterialBySkuIdsResponse (class)

- public List<string> returnType;

- public string Raw;
