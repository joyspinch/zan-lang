# Sdk.Jd.Api.UserRelatedRpcI18nService

> 源码: `stdlib/Sdk/Jd/Api/UserRelatedRpcI18nService/JdUserRelatedRpcI18nServiceApi.zan`, `stdlib/Sdk/Jd/Api/UserRelatedRpcI18nService/UserRelatedRpcI18nServiceGetOpenIdRequest.zan`


## JdUserRelatedRpcI18nServiceApi (class)

jingdong.UserRelatedRpcI18nService.* 的强类型客户端。

- JdClient client;

- public JdUserRelatedRpcI18nServiceApi(JdClient client)

- async UserRelatedRpcI18nServiceGetOpenIdResponse GetOpenIdAsync(UserRelatedRpcI18nServiceGetOpenIdRequest request)
  - 执行 <c>jingdong.UserRelatedRpcI18nService.getOpenId</c>。


## UserRelatedRpcI18nServiceGetOpenIdRequest (class)

<c>jingdong.UserRelatedRpcI18nService.getOpenId</c> 的请求。

- JdRequest req;

- public UserRelatedRpcI18nServiceGetOpenIdRequest()

- UserRelatedRpcI18nServiceGetOpenIdRequest Pin(string pin)
  - 设置 <c>pin</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## UserRelatedRpcI18nServiceGetOpenIdResponse (class)

<c>jingdong.UserRelatedRpcI18nService.getOpenId</c> 的响应。

- public Result result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
