# Sdk.Jd.Api.Vas

> 源码: `stdlib/Sdk/Jd/Api/Vas/JdVasApi.zan`, `stdlib/Sdk/Jd/Api/Vas/VasSubscribeGetByCodeRequest.zan`, `stdlib/Sdk/Jd/Api/Vas/VasSubscribeGetRequest.zan`


## JdVasApi (class)

jingdong.vas.* 的强类型客户端。

- JdClient client;

- public JdVasApi(JdClient client)

- async VasSubscribeGetByCodeResponse SubscribeGetByCodeAsync(VasSubscribeGetByCodeRequest request)
  - 执行 <c>jingdong.vas.subscribe.getByCode</c>。

- async VasSubscribeGetResponse SubscribeGetAsync(VasSubscribeGetRequest request)
  - 执行 <c>jingdong.vas.subscribe.get</c>。


## VasSubscribeGetByCodeRequest (class)

<c>jingdong.vas.subscribe.getByCode</c> 的请求。

- JdRequest req;

- public VasSubscribeGetByCodeRequest()

- VasSubscribeGetByCodeRequest ItemCode(string itemCode)
  - 设置 <c>item_code</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## VasSubscribeGetByCodeResponse (class)

<c>jingdong.vas.subscribe.getByCode</c> 的响应。

- public string item_code;

- public string end_date;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## VasSubscribeGetRequest (class)

<c>jingdong.vas.subscribe.get</c> 的请求。

- JdRequest req;

- public VasSubscribeGetRequest()

- VasSubscribeGetRequest UserName(string userName)
  - 设置 <c>user_name</c> 参数。

- VasSubscribeGetRequest ItemCode(string itemCode)
  - 设置 <c>item_code</c> 参数。

- VasSubscribeGetRequest OpenIdBuyer(string openIdBuyer)
  - 设置 <c>open_id_buyer</c> 参数。

- VasSubscribeGetRequest XidBuyer(string xidBuyer)
  - 设置 <c>xid_buyer</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## VasSubscribeGetResponse (class)

<c>jingdong.vas.subscribe.get</c> 的响应。

- public string item_code;

- public string end_date;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
