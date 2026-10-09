# Sdk.Jd.Api.UserRelatedRpcI18nService

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/UserRelatedRpcI18nService/JdUserRelatedRpcI18nServiceApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/UserRelatedRpcI18nService/UserRelatedRpcI18nServiceGetOpenIdRequest.zan`


## JdUserRelatedRpcI18nServiceApi (class)

- JdClient client;

- public JdUserRelatedRpcI18nServiceApi(JdClient client)

- async UserRelatedRpcI18nServiceGetOpenIdResponse GetOpenIdAsync(UserRelatedRpcI18nServiceGetOpenIdRequest request)


## UserRelatedRpcI18nServiceGetOpenIdRequest (class)

- JdRequest req;

- public UserRelatedRpcI18nServiceGetOpenIdRequest()

- UserRelatedRpcI18nServiceGetOpenIdRequest Pin(string pin)

- JdRequest Raw()


## UserRelatedRpcI18nServiceGetOpenIdResponse (class)

- public Result result;

- public string Raw;
