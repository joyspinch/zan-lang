# Sdk.Jd.Api.User

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/User/JdUserApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/User/UserGetUserInfoByOpenIdRequest.zan`


## JdUserApi (class)

- JdClient client;

- public JdUserApi(JdClient client)

- async UserGetUserInfoByOpenIdResponse GetUserInfoByOpenIdAsync(UserGetUserInfoByOpenIdRequest request)


## UserGetUserInfoByOpenIdRequest (class)

- JdRequest req;

- public UserGetUserInfoByOpenIdRequest()

- UserGetUserInfoByOpenIdRequest OpenId(string openId)

- JdRequest Raw()


## UserGetUserInfoByOpenIdResponse (class)

- public Result getuserinfobyappidandopenid_result;

- public string Raw;
