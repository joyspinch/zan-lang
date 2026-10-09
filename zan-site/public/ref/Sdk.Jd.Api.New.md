# Sdk.Jd.Api.New

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/New/JdNewApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/New/NewWareAttributeGroupsQueryRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/New/NewWareAttributeValuesQueryRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/New/NewWareAttributesQueryRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/New/NewWareBaseproductGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/New/NewWareMobilebigfieldGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/New/NewWareProductsortattGetRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/New/NewWareSameproductskuidsQueryRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/New/NewWareVenderSkusQueryRequest.zan`


## JdNewApi (class)

- JdClient client;

- public JdNewApi(JdClient client)

- async NewWareAttributeGroupsQueryResponse WareAttributeGroupsQueryAsync(NewWareAttributeGroupsQueryRequest request)

- async NewWareAttributeValuesQueryResponse WareAttributeValuesQueryAsync(NewWareAttributeValuesQueryRequest request)

- async NewWareAttributesQueryResponse WareAttributesQueryAsync(NewWareAttributesQueryRequest request)

- async NewWareBaseproductGetResponse WareBaseproductGetAsync(NewWareBaseproductGetRequest request)

- async NewWareMobilebigfieldGetResponse WareMobilebigfieldGetAsync(NewWareMobilebigfieldGetRequest request)

- async NewWareProductsortattGetResponse WareProductsortattGetAsync(NewWareProductsortattGetRequest request)

- async NewWareSameproductskuidsQueryResponse WareSameproductskuidsQueryAsync(NewWareSameproductskuidsQueryRequest request)

- async NewWareVenderSkusQueryResponse WareVenderSkusQueryAsync(NewWareVenderSkusQueryRequest request)


## NewWareAttributeGroupsQueryRequest (class)

- JdRequest req;

- public NewWareAttributeGroupsQueryRequest()

- NewWareAttributeGroupsQueryRequest Id(string id)

- JdRequest Raw()


## NewWareAttributeGroupsQueryResponse (class)

- public List<string> resultset;

- public string Raw;


## NewWareAttributeValuesQueryRequest (class)

- JdRequest req;

- public NewWareAttributeValuesQueryRequest()

- NewWareAttributeValuesQueryRequest Id(string id)

- JdRequest Raw()


## NewWareAttributeValuesQueryResponse (class)

- public List<string> resultset;

- public string Raw;


## NewWareAttributesQueryRequest (class)

- JdRequest req;

- public NewWareAttributesQueryRequest()

- NewWareAttributesQueryRequest Id(string id)

- JdRequest Raw()


## NewWareAttributesQueryResponse (class)

- public List<string> resultset;

- public string Raw;


## NewWareBaseproductGetRequest (class)

- JdRequest req;

- public NewWareBaseproductGetRequest()

- NewWareBaseproductGetRequest Ids(string ids)

- NewWareBaseproductGetRequest Basefields(string basefields)

- JdRequest Raw()


## NewWareBaseproductGetResponse (class)

- public List<string> listproductbase_result;

- public string Raw;


## NewWareMobilebigfieldGetRequest (class)

- JdRequest req;

- public NewWareMobilebigfieldGetRequest()

- NewWareMobilebigfieldGetRequest Skuid(long skuid)

- JdRequest Raw()


## NewWareMobilebigfieldGetResponse (class)

- public string result;

- public string Raw;


## NewWareProductsortattGetRequest (class)

- JdRequest req;

- public NewWareProductsortattGetRequest()

- NewWareProductsortattGetRequest Skuid(long skuid)

- JdRequest Raw()


## NewWareProductsortattGetResponse (class)

- public List<string> resultset;

- public string Raw;


## NewWareSameproductskuidsQueryRequest (class)

- JdRequest req;

- public NewWareSameproductskuidsQueryRequest()

- NewWareSameproductskuidsQueryRequest Id(string id)

- JdRequest Raw()


## NewWareSameproductskuidsQueryResponse (class)

- public List<string> result;

- public string Raw;


## NewWareVenderSkusQueryRequest (class)

- JdRequest req;

- public NewWareVenderSkusQueryRequest()

- NewWareVenderSkusQueryRequest Index(string index)

- JdRequest Raw()


## NewWareVenderSkusQueryResponse (class)

- public SearchResult search_result;

- public string Raw;
