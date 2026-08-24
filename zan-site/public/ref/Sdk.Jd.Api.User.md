# Sdk.Jd.Api.User

> 源码: `stdlib/Sdk/Jd/Api/User/JdUserApi.zan`, `stdlib/Sdk/Jd/Api/User/UserGetUserInfoByOpenIdRequest.zan`


## JdUserApi (class)

jingdong.user.* 的强类型客户端。

- JdClient client;

- public JdUserApi(JdClient client)

- async UserGetUserInfoByOpenIdResponse GetUserInfoByOpenIdAsync(UserGetUserInfoByOpenIdRequest request)
  - 执行 <c>jingdong.user.getUserInfoByOpenId</c>。


## UserGetUserInfoByOpenIdRequest (class)

<c>jingdong.user.getUserInfoByOpenId</c> 的请求。

- JdRequest req;

- public UserGetUserInfoByOpenIdRequest()

- UserGetUserInfoByOpenIdRequest OpenId(string openId)
  - 设置 <c>openId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## UserGetUserInfoByOpenIdResponse (class)

<c>jingdong.user.getUserInfoByOpenId</c> 的响应。

- public Result getuserinfobyappidandopenid_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
