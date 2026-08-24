# Sdk.Jd.Api.Data

> 源码: `stdlib/Sdk/Jd/Api/Data/DataVenderCommonQueryRequest.zan`, `stdlib/Sdk/Jd/Api/Data/JdDataApi.zan`


## DataVenderCommonQueryRequest (class)

<c>jingdong.data.vender.common.query</c> 的请求。

- JdRequest req;

- public DataVenderCommonQueryRequest()

- DataVenderCommonQueryRequest Method(string method)
  - 设置 <c>method</c> 参数。

- DataVenderCommonQueryRequest InputPara(string inputPara)
  - 设置 <c>input_para</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DataVenderCommonQueryResponse (class)

<c>jingdong.data.vender.common.query</c> 的响应。

- public ServiceResponse response;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdDataApi (class)

jingdong.data.* 的强类型客户端。

- JdClient client;

- public JdDataApi(JdClient client)

- async DataVenderCommonQueryResponse VenderCommonQueryAsync(DataVenderCommonQueryRequest request)
  - 执行 <c>jingdong.data.vender.common.query</c>。
