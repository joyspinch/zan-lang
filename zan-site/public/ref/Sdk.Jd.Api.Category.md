# Sdk.Jd.Api.Category

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindAttrByIdJosRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindAttrByIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindAttrByIdUnlimitCateRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindAttrsByCategoryIdJosRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindAttrsByCategoryIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindAttrsByCategoryIdUnlimitCateRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindByIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindByPIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindValuesByAttrIdJosRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindValuesByAttrIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindValuesByAttrIdUnlimitRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindValuesByIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/CategoryReadFindValuesByIdUnlimitRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Category/JdCategoryApi.zan`


## CategoryReadFindAttrByIdJosRequest (class)

- JdRequest req;

- public CategoryReadFindAttrByIdJosRequest()

- CategoryReadFindAttrByIdJosRequest AttrId(long attrId)

- CategoryReadFindAttrByIdJosRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindAttrByIdJosResponse (class)

- public CategoryAttrJos categoryAttr;

- public string Raw;


## CategoryReadFindAttrByIdRequest (class)

- JdRequest req;

- public CategoryReadFindAttrByIdRequest()

- CategoryReadFindAttrByIdRequest AttrId(long attrId)

- CategoryReadFindAttrByIdRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindAttrByIdResponse (class)

- public CategoryAttr categoryAttr;

- public string Raw;


## CategoryReadFindAttrByIdUnlimitCateRequest (class)

- JdRequest req;

- public CategoryReadFindAttrByIdUnlimitCateRequest()

- CategoryReadFindAttrByIdUnlimitCateRequest AttrId(long attrId)

- CategoryReadFindAttrByIdUnlimitCateRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindAttrByIdUnlimitCateResponse (class)

- public CategoryAttrUnlimit findattrbyidunlimitcate_result;

- public string Raw;


## CategoryReadFindAttrsByCategoryIdJosRequest (class)

- JdRequest req;

- public CategoryReadFindAttrsByCategoryIdJosRequest()

- CategoryReadFindAttrsByCategoryIdJosRequest Cid(long cid)

- CategoryReadFindAttrsByCategoryIdJosRequest AttributeType(int attributeType)

- CategoryReadFindAttrsByCategoryIdJosRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindAttrsByCategoryIdJosResponse (class)

- public List<string> categoryAttrs;

- public string Raw;


## CategoryReadFindAttrsByCategoryIdRequest (class)

- JdRequest req;

- public CategoryReadFindAttrsByCategoryIdRequest()

- CategoryReadFindAttrsByCategoryIdRequest Cid(long cid)

- CategoryReadFindAttrsByCategoryIdRequest AttributeType(int attributeType)

- CategoryReadFindAttrsByCategoryIdRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindAttrsByCategoryIdResponse (class)

- public List<string> categoryAttrs;

- public string Raw;


## CategoryReadFindAttrsByCategoryIdUnlimitCateRequest (class)

- JdRequest req;

- public CategoryReadFindAttrsByCategoryIdUnlimitCateRequest()

- CategoryReadFindAttrsByCategoryIdUnlimitCateRequest Cid(long cid)

- CategoryReadFindAttrsByCategoryIdUnlimitCateRequest AttributeType(int attributeType)

- CategoryReadFindAttrsByCategoryIdUnlimitCateRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindAttrsByCategoryIdUnlimitCateResponse (class)

- public List<string> findattrsbycategoryidunlimitcate_result;

- public string Raw;


## CategoryReadFindByIdRequest (class)

- JdRequest req;

- public CategoryReadFindByIdRequest()

- CategoryReadFindByIdRequest Cid(long cid)

- CategoryReadFindByIdRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindByIdResponse (class)

- public Category category;

- public string Raw;


## CategoryReadFindByPIdRequest (class)

- JdRequest req;

- public CategoryReadFindByPIdRequest()

- CategoryReadFindByPIdRequest ParentCid(long parentCid)

- CategoryReadFindByPIdRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindByPIdResponse (class)

- public List<string> categories;

- public string Raw;


## CategoryReadFindValuesByAttrIdJosRequest (class)

- JdRequest req;

- public CategoryReadFindValuesByAttrIdJosRequest()

- CategoryReadFindValuesByAttrIdJosRequest CategoryAttrId(long categoryAttrId)

- CategoryReadFindValuesByAttrIdJosRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindValuesByAttrIdJosResponse (class)

- public List<string> categoryAttrValues;

- public string Raw;


## CategoryReadFindValuesByAttrIdRequest (class)

- JdRequest req;

- public CategoryReadFindValuesByAttrIdRequest()

- CategoryReadFindValuesByAttrIdRequest CategoryAttrId(long categoryAttrId)

- CategoryReadFindValuesByAttrIdRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindValuesByAttrIdResponse (class)

- public List<string> categoryAttrValues;

- public string Raw;


## CategoryReadFindValuesByAttrIdUnlimitRequest (class)

- JdRequest req;

- public CategoryReadFindValuesByAttrIdUnlimitRequest()

- CategoryReadFindValuesByAttrIdUnlimitRequest CategoryAttrId(long categoryAttrId)

- CategoryReadFindValuesByAttrIdUnlimitRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindValuesByAttrIdUnlimitResponse (class)

- public List<string> findvaluesbyattridunlimit_result;

- public string Raw;


## CategoryReadFindValuesByIdRequest (class)

- JdRequest req;

- public CategoryReadFindValuesByIdRequest()

- CategoryReadFindValuesByIdRequest Id(long id)

- CategoryReadFindValuesByIdRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindValuesByIdResponse (class)

- public CategoryAttrValue categoryAttrValue;

- public string Raw;


## CategoryReadFindValuesByIdUnlimitRequest (class)

- JdRequest req;

- public CategoryReadFindValuesByIdUnlimitRequest()

- CategoryReadFindValuesByIdUnlimitRequest Id(long id)

- CategoryReadFindValuesByIdUnlimitRequest Field(string field)

- JdRequest Raw()


## CategoryReadFindValuesByIdUnlimitResponse (class)

- public CategoryAttrValueUnlimit findvaluesbyidunlimit_result;

- public string Raw;


## JdCategoryApi (class)

- JdClient client;

- public JdCategoryApi(JdClient client)

- async CategoryReadFindAttrByIdJosResponse ReadFindAttrByIdJosAsync(CategoryReadFindAttrByIdJosRequest request)

- async CategoryReadFindAttrByIdResponse ReadFindAttrByIdAsync(CategoryReadFindAttrByIdRequest request)

- async CategoryReadFindAttrByIdUnlimitCateResponse ReadFindAttrByIdUnlimitCateAsync(CategoryReadFindAttrByIdUnlimitCateRequest request)

- async CategoryReadFindAttrsByCategoryIdJosResponse ReadFindAttrsByCategoryIdJosAsync(CategoryReadFindAttrsByCategoryIdJosRequest request)

- async CategoryReadFindAttrsByCategoryIdResponse ReadFindAttrsByCategoryIdAsync(CategoryReadFindAttrsByCategoryIdRequest request)

- async CategoryReadFindAttrsByCategoryIdUnlimitCateResponse ReadFindAttrsByCategoryIdUnlimitCateAsync(CategoryReadFindAttrsByCategoryIdUnlimitCateRequest request)

- async CategoryReadFindByIdResponse ReadFindByIdAsync(CategoryReadFindByIdRequest request)

- async CategoryReadFindByPIdResponse ReadFindByPIdAsync(CategoryReadFindByPIdRequest request)

- async CategoryReadFindValuesByAttrIdJosResponse ReadFindValuesByAttrIdJosAsync(CategoryReadFindValuesByAttrIdJosRequest request)

- async CategoryReadFindValuesByAttrIdResponse ReadFindValuesByAttrIdAsync(CategoryReadFindValuesByAttrIdRequest request)

- async CategoryReadFindValuesByAttrIdUnlimitResponse ReadFindValuesByAttrIdUnlimitAsync(CategoryReadFindValuesByAttrIdUnlimitRequest request)

- async CategoryReadFindValuesByIdResponse ReadFindValuesByIdAsync(CategoryReadFindValuesByIdRequest request)

- async CategoryReadFindValuesByIdUnlimitResponse ReadFindValuesByIdUnlimitAsync(CategoryReadFindValuesByIdUnlimitRequest request)
