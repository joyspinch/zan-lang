# Sdk.Jd.Api.Data

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Data/DataVenderCommonQueryRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Data/JdDataApi.zan`


## DataVenderCommonQueryRequest (class)

- JdRequest req;

- public DataVenderCommonQueryRequest()

- DataVenderCommonQueryRequest Method(string method)

- DataVenderCommonQueryRequest InputPara(string inputPara)

- JdRequest Raw()


## DataVenderCommonQueryResponse (class)

- public ServiceResponse response;

- public string Raw;


## JdDataApi (class)

- JdClient client;

- public JdDataApi(JdClient client)

- async DataVenderCommonQueryResponse VenderCommonQueryAsync(DataVenderCommonQueryRequest request)
