# Sdk.Jd.Api.Category

> 源码: `stdlib/Sdk/Jd/Api/Category/CategoryReadFindAttrByIdJosRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindAttrByIdRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindAttrByIdUnlimitCateRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindAttrsByCategoryIdJosRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindAttrsByCategoryIdRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindAttrsByCategoryIdUnlimitCateRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindByIdRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindByPIdRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindValuesByAttrIdJosRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindValuesByAttrIdRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindValuesByAttrIdUnlimitRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindValuesByIdRequest.zan`, `stdlib/Sdk/Jd/Api/Category/CategoryReadFindValuesByIdUnlimitRequest.zan`, `stdlib/Sdk/Jd/Api/Category/JdCategoryApi.zan`


## CategoryReadFindAttrByIdJosRequest (class)

<c>jingdong.category.read.findAttrByIdJos</c> 的请求。

- JdRequest req;

- public CategoryReadFindAttrByIdJosRequest()

- CategoryReadFindAttrByIdJosRequest AttrId(long attrId)
  - 设置 <c>attrId</c> 参数。

- CategoryReadFindAttrByIdJosRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindAttrByIdJosResponse (class)

<c>jingdong.category.read.findAttrByIdJos</c> 的响应。

- public CategoryAttrJos categoryAttr;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindAttrByIdRequest (class)

<c>jingdong.category.read.findAttrById</c> 的请求。

- JdRequest req;

- public CategoryReadFindAttrByIdRequest()

- CategoryReadFindAttrByIdRequest AttrId(long attrId)
  - 设置 <c>attrId</c> 参数。

- CategoryReadFindAttrByIdRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindAttrByIdResponse (class)

<c>jingdong.category.read.findAttrById</c> 的响应。

- public CategoryAttr categoryAttr;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindAttrByIdUnlimitCateRequest (class)

<c>jingdong.category.read.findAttrByIdUnlimitCate</c> 的请求。

- JdRequest req;

- public CategoryReadFindAttrByIdUnlimitCateRequest()

- CategoryReadFindAttrByIdUnlimitCateRequest AttrId(long attrId)
  - 设置 <c>attrId</c> 参数。

- CategoryReadFindAttrByIdUnlimitCateRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindAttrByIdUnlimitCateResponse (class)

<c>jingdong.category.read.findAttrByIdUnlimitCate</c> 的响应。

- public CategoryAttrUnlimit findattrbyidunlimitcate_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindAttrsByCategoryIdJosRequest (class)

<c>jingdong.category.read.findAttrsByCategoryIdJos</c> 的请求。

- JdRequest req;

- public CategoryReadFindAttrsByCategoryIdJosRequest()

- CategoryReadFindAttrsByCategoryIdJosRequest Cid(long cid)
  - 设置 <c>cid</c> 参数。

- CategoryReadFindAttrsByCategoryIdJosRequest AttributeType(int attributeType)
  - 设置 <c>attributeType</c> 参数。

- CategoryReadFindAttrsByCategoryIdJosRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindAttrsByCategoryIdJosResponse (class)

<c>jingdong.category.read.findAttrsByCategoryIdJos</c> 的响应。

- public List<string> categoryAttrs;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindAttrsByCategoryIdRequest (class)

<c>jingdong.category.read.findAttrsByCategoryId</c> 的请求。

- JdRequest req;

- public CategoryReadFindAttrsByCategoryIdRequest()

- CategoryReadFindAttrsByCategoryIdRequest Cid(long cid)
  - 设置 <c>cid</c> 参数。

- CategoryReadFindAttrsByCategoryIdRequest AttributeType(int attributeType)
  - 设置 <c>attributeType</c> 参数。

- CategoryReadFindAttrsByCategoryIdRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindAttrsByCategoryIdResponse (class)

<c>jingdong.category.read.findAttrsByCategoryId</c> 的响应。

- public List<string> categoryAttrs;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindAttrsByCategoryIdUnlimitCateRequest (class)

<c>jingdong.category.read.findAttrsByCategoryIdUnlimitCate</c> 的请求。

- JdRequest req;

- public CategoryReadFindAttrsByCategoryIdUnlimitCateRequest()

- CategoryReadFindAttrsByCategoryIdUnlimitCateRequest Cid(long cid)
  - 设置 <c>cid</c> 参数。

- CategoryReadFindAttrsByCategoryIdUnlimitCateRequest AttributeType(int attributeType)
  - 设置 <c>attributeType</c> 参数。

- CategoryReadFindAttrsByCategoryIdUnlimitCateRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindAttrsByCategoryIdUnlimitCateResponse (class)

<c>jingdong.category.read.findAttrsByCategoryIdUnlimitCate</c> 的响应。

- public List<string> findattrsbycategoryidunlimitcate_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindByIdRequest (class)

<c>jingdong.category.read.findById</c> 的请求。

- JdRequest req;

- public CategoryReadFindByIdRequest()

- CategoryReadFindByIdRequest Cid(long cid)
  - 设置 <c>cid</c> 参数。

- CategoryReadFindByIdRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindByIdResponse (class)

<c>jingdong.category.read.findById</c> 的响应。

- public Category category;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindByPIdRequest (class)

<c>jingdong.category.read.findByPId</c> 的请求。

- JdRequest req;

- public CategoryReadFindByPIdRequest()

- CategoryReadFindByPIdRequest ParentCid(long parentCid)
  - 设置 <c>parentCid</c> 参数。

- CategoryReadFindByPIdRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindByPIdResponse (class)

<c>jingdong.category.read.findByPId</c> 的响应。

- public List<string> categories;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindValuesByAttrIdJosRequest (class)

<c>jingdong.category.read.findValuesByAttrIdJos</c> 的请求。

- JdRequest req;

- public CategoryReadFindValuesByAttrIdJosRequest()

- CategoryReadFindValuesByAttrIdJosRequest CategoryAttrId(long categoryAttrId)
  - 设置 <c>categoryAttrId</c> 参数。

- CategoryReadFindValuesByAttrIdJosRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindValuesByAttrIdJosResponse (class)

<c>jingdong.category.read.findValuesByAttrIdJos</c> 的响应。

- public List<string> categoryAttrValues;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindValuesByAttrIdRequest (class)

<c>jingdong.category.read.findValuesByAttrId</c> 的请求。

- JdRequest req;

- public CategoryReadFindValuesByAttrIdRequest()

- CategoryReadFindValuesByAttrIdRequest CategoryAttrId(long categoryAttrId)
  - 设置 <c>categoryAttrId</c> 参数。

- CategoryReadFindValuesByAttrIdRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindValuesByAttrIdResponse (class)

<c>jingdong.category.read.findValuesByAttrId</c> 的响应。

- public List<string> categoryAttrValues;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindValuesByAttrIdUnlimitRequest (class)

<c>jingdong.category.read.findValuesByAttrIdUnlimit</c> 的请求。

- JdRequest req;

- public CategoryReadFindValuesByAttrIdUnlimitRequest()

- CategoryReadFindValuesByAttrIdUnlimitRequest CategoryAttrId(long categoryAttrId)
  - 设置 <c>categoryAttrId</c> 参数。

- CategoryReadFindValuesByAttrIdUnlimitRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindValuesByAttrIdUnlimitResponse (class)

<c>jingdong.category.read.findValuesByAttrIdUnlimit</c> 的响应。

- public List<string> findvaluesbyattridunlimit_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindValuesByIdRequest (class)

<c>jingdong.category.read.findValuesById</c> 的请求。

- JdRequest req;

- public CategoryReadFindValuesByIdRequest()

- CategoryReadFindValuesByIdRequest Id(long id)
  - 设置 <c>id</c> 参数。

- CategoryReadFindValuesByIdRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindValuesByIdResponse (class)

<c>jingdong.category.read.findValuesById</c> 的响应。

- public CategoryAttrValue categoryAttrValue;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## CategoryReadFindValuesByIdUnlimitRequest (class)

<c>jingdong.category.read.findValuesByIdUnlimit</c> 的请求。

- JdRequest req;

- public CategoryReadFindValuesByIdUnlimitRequest()

- CategoryReadFindValuesByIdUnlimitRequest Id(long id)
  - 设置 <c>id</c> 参数。

- CategoryReadFindValuesByIdUnlimitRequest Field(string field)
  - 设置 <c>field</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## CategoryReadFindValuesByIdUnlimitResponse (class)

<c>jingdong.category.read.findValuesByIdUnlimit</c> 的响应。

- public CategoryAttrValueUnlimit findvaluesbyidunlimit_result;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdCategoryApi (class)

jingdong.category.* 的强类型客户端。

- JdClient client;

- public JdCategoryApi(JdClient client)

- async CategoryReadFindAttrByIdJosResponse ReadFindAttrByIdJosAsync(CategoryReadFindAttrByIdJosRequest request)
  - 执行 <c>jingdong.category.read.findAttrByIdJos</c>。

- async CategoryReadFindAttrByIdResponse ReadFindAttrByIdAsync(CategoryReadFindAttrByIdRequest request)
  - 执行 <c>jingdong.category.read.findAttrById</c>。

- async CategoryReadFindAttrByIdUnlimitCateResponse ReadFindAttrByIdUnlimitCateAsync(CategoryReadFindAttrByIdUnlimitCateRequest request)
  - 执行 <c>jingdong.category.read.findAttrByIdUnlimitCate</c>。

- async CategoryReadFindAttrsByCategoryIdJosResponse ReadFindAttrsByCategoryIdJosAsync(CategoryReadFindAttrsByCategoryIdJosRequest request)
  - 执行 <c>jingdong.category.read.findAttrsByCategoryIdJos</c>。

- async CategoryReadFindAttrsByCategoryIdResponse ReadFindAttrsByCategoryIdAsync(CategoryReadFindAttrsByCategoryIdRequest request)
  - 执行 <c>jingdong.category.read.findAttrsByCategoryId</c>。

- async CategoryReadFindAttrsByCategoryIdUnlimitCateResponse ReadFindAttrsByCategoryIdUnlimitCateAsync(CategoryReadFindAttrsByCategoryIdUnlimitCateRequest request)
  - 执行 <c>jingdong.category.read.findAttrsByCategoryIdUnlimitCate</c>。

- async CategoryReadFindByIdResponse ReadFindByIdAsync(CategoryReadFindByIdRequest request)
  - 执行 <c>jingdong.category.read.findById</c>。

- async CategoryReadFindByPIdResponse ReadFindByPIdAsync(CategoryReadFindByPIdRequest request)
  - 执行 <c>jingdong.category.read.findByPId</c>。

- async CategoryReadFindValuesByAttrIdJosResponse ReadFindValuesByAttrIdJosAsync(CategoryReadFindValuesByAttrIdJosRequest request)
  - 执行 <c>jingdong.category.read.findValuesByAttrIdJos</c>。

- async CategoryReadFindValuesByAttrIdResponse ReadFindValuesByAttrIdAsync(CategoryReadFindValuesByAttrIdRequest request)
  - 执行 <c>jingdong.category.read.findValuesByAttrId</c>。

- async CategoryReadFindValuesByAttrIdUnlimitResponse ReadFindValuesByAttrIdUnlimitAsync(CategoryReadFindValuesByAttrIdUnlimitRequest request)
  - 执行 <c>jingdong.category.read.findValuesByAttrIdUnlimit</c>。

- async CategoryReadFindValuesByIdResponse ReadFindValuesByIdAsync(CategoryReadFindValuesByIdRequest request)
  - 执行 <c>jingdong.category.read.findValuesById</c>。

- async CategoryReadFindValuesByIdUnlimitResponse ReadFindValuesByIdUnlimitAsync(CategoryReadFindValuesByIdUnlimitRequest request)
  - 执行 <c>jingdong.category.read.findValuesByIdUnlimit</c>。
