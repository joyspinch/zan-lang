# Sdk.Jd.Api.New

> 源码: `stdlib/Sdk/Jd/Api/New/JdNewApi.zan`, `stdlib/Sdk/Jd/Api/New/NewWareAttributeGroupsQueryRequest.zan`, `stdlib/Sdk/Jd/Api/New/NewWareAttributeValuesQueryRequest.zan`, `stdlib/Sdk/Jd/Api/New/NewWareAttributesQueryRequest.zan`, `stdlib/Sdk/Jd/Api/New/NewWareBaseproductGetRequest.zan`, `stdlib/Sdk/Jd/Api/New/NewWareMobilebigfieldGetRequest.zan`, `stdlib/Sdk/Jd/Api/New/NewWareProductsortattGetRequest.zan`, `stdlib/Sdk/Jd/Api/New/NewWareSameproductskuidsQueryRequest.zan`, `stdlib/Sdk/Jd/Api/New/NewWareVenderSkusQueryRequest.zan`


## JdNewApi (class)

jingdong.new.* 的强类型客户端。

- JdClient client;

- public JdNewApi(JdClient client)

- async NewWareAttributeGroupsQueryResponse WareAttributeGroupsQueryAsync(NewWareAttributeGroupsQueryRequest request)
  - 执行 <c>jingdong.new.ware.AttributeGroups.query</c>。

- async NewWareAttributeValuesQueryResponse WareAttributeValuesQueryAsync(NewWareAttributeValuesQueryRequest request)
  - 执行 <c>jingdong.new.ware.AttributeValues.query</c>。

- async NewWareAttributesQueryResponse WareAttributesQueryAsync(NewWareAttributesQueryRequest request)
  - 执行 <c>jingdong.new.ware.Attributes.query</c>。

- async NewWareBaseproductGetResponse WareBaseproductGetAsync(NewWareBaseproductGetRequest request)
  - 执行 <c>jingdong.new.ware.baseproduct.get</c>。

- async NewWareMobilebigfieldGetResponse WareMobilebigfieldGetAsync(NewWareMobilebigfieldGetRequest request)
  - 执行 <c>jingdong.new.ware.mobilebigfield.get</c>。

- async NewWareProductsortattGetResponse WareProductsortattGetAsync(NewWareProductsortattGetRequest request)
  - 执行 <c>jingdong.new.ware.productsortatt.get</c>。

- async NewWareSameproductskuidsQueryResponse WareSameproductskuidsQueryAsync(NewWareSameproductskuidsQueryRequest request)
  - 执行 <c>jingdong.new.ware.sameproductskuids.query</c>。

- async NewWareVenderSkusQueryResponse WareVenderSkusQueryAsync(NewWareVenderSkusQueryRequest request)
  - 执行 <c>jingdong.new.ware.vender.skus.query</c>。


## NewWareAttributeGroupsQueryRequest (class)

<c>jingdong.new.ware.AttributeGroups.query</c> 的请求。

- JdRequest req;

- public NewWareAttributeGroupsQueryRequest()

- NewWareAttributeGroupsQueryRequest Id(string id)
  - 设置 <c>id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## NewWareAttributeGroupsQueryResponse (class)

<c>jingdong.new.ware.AttributeGroups.query</c> 的响应。

- public List<string> resultset;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## NewWareAttributeValuesQueryRequest (class)

<c>jingdong.new.ware.AttributeValues.query</c> 的请求。

- JdRequest req;

- public NewWareAttributeValuesQueryRequest()

- NewWareAttributeValuesQueryRequest Id(string id)
  - 设置 <c>id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## NewWareAttributeValuesQueryResponse (class)

<c>jingdong.new.ware.AttributeValues.query</c> 的响应。

- public List<string> resultset;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## NewWareAttributesQueryRequest (class)

<c>jingdong.new.ware.Attributes.query</c> 的请求。

- JdRequest req;

- public NewWareAttributesQueryRequest()

- NewWareAttributesQueryRequest Id(string id)
  - 设置 <c>id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## NewWareAttributesQueryResponse (class)

<c>jingdong.new.ware.Attributes.query</c> 的响应。

- public List<string> resultset;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## NewWareBaseproductGetRequest (class)

<c>jingdong.new.ware.baseproduct.get</c> 的请求。

- JdRequest req;

- public NewWareBaseproductGetRequest()

- NewWareBaseproductGetRequest Ids(string ids)
  - 设置 <c>ids</c> 参数。

- NewWareBaseproductGetRequest Basefields(string basefields)
  - 设置 <c>basefields</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## NewWareBaseproductGetResponse (class)

<c>jingdong.new.ware.baseproduct.get</c> 的响应。

- public List<string> listproductbase_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## NewWareMobilebigfieldGetRequest (class)

<c>jingdong.new.ware.mobilebigfield.get</c> 的请求。

- JdRequest req;

- public NewWareMobilebigfieldGetRequest()

- NewWareMobilebigfieldGetRequest Skuid(long skuid)
  - 设置 <c>skuid</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## NewWareMobilebigfieldGetResponse (class)

<c>jingdong.new.ware.mobilebigfield.get</c> 的响应。

- public string result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## NewWareProductsortattGetRequest (class)

<c>jingdong.new.ware.productsortatt.get</c> 的请求。

- JdRequest req;

- public NewWareProductsortattGetRequest()

- NewWareProductsortattGetRequest Skuid(long skuid)
  - 设置 <c>skuid</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## NewWareProductsortattGetResponse (class)

<c>jingdong.new.ware.productsortatt.get</c> 的响应。

- public List<string> resultset;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## NewWareSameproductskuidsQueryRequest (class)

<c>jingdong.new.ware.sameproductskuids.query</c> 的请求。

- JdRequest req;

- public NewWareSameproductskuidsQueryRequest()

- NewWareSameproductskuidsQueryRequest Id(string id)
  - 设置 <c>id</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## NewWareSameproductskuidsQueryResponse (class)

<c>jingdong.new.ware.sameproductskuids.query</c> 的响应。

- public List<string> result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## NewWareVenderSkusQueryRequest (class)

<c>jingdong.new.ware.vender.skus.query</c> 的请求。

- JdRequest req;

- public NewWareVenderSkusQueryRequest()

- NewWareVenderSkusQueryRequest Index(string index)
  - 设置 <c>index</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## NewWareVenderSkusQueryResponse (class)

<c>jingdong.new.ware.vender.skus.query</c> 的响应。

- public SearchResult search_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
