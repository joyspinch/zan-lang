# Sdk.Jd.Api.Arealimit

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Arealimit/ArealimitReadFindAreaLimitsByWareIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Arealimit/JdArealimitApi.zan`


## ArealimitReadFindAreaLimitsByWareIdRequest (class)

- JdRequest req;

- public ArealimitReadFindAreaLimitsByWareIdRequest()

- ArealimitReadFindAreaLimitsByWareIdRequest WareId(long wareId)

- ArealimitReadFindAreaLimitsByWareIdRequest Field(string field)

- JdRequest Raw()


## ArealimitReadFindAreaLimitsByWareIdResponse (class)

- public List<string> wareAreaLimitList;

- public string Raw;


## JdArealimitApi (class)

- JdClient client;

- public JdArealimitApi(JdClient client)

- async ArealimitReadFindAreaLimitsByWareIdResponse ReadFindAreaLimitsByWareIdAsync(ArealimitReadFindAreaLimitsByWareIdRequest request)
