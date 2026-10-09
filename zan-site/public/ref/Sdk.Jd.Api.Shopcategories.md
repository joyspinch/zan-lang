# Sdk.Jd.Api.Shopcategories

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Shopcategories/JdShopcategoriesApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Shopcategories/ShopcategoriesReadFindShopCategoriesByWareIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Shopcategories/ShopcategoriesWriteSaveWareShopCategoriesRequest.zan`


## JdShopcategoriesApi (class)

- JdClient client;

- public JdShopcategoriesApi(JdClient client)

- async ShopcategoriesReadFindShopCategoriesByWareIdResponse ReadFindShopCategoriesByWareIdAsync(ShopcategoriesReadFindShopCategoriesByWareIdRequest request)

- async ShopcategoriesWriteSaveWareShopCategoriesResponse WriteSaveWareShopCategoriesAsync(ShopcategoriesWriteSaveWareShopCategoriesRequest request)


## ShopcategoriesReadFindShopCategoriesByWareIdRequest (class)

- JdRequest req;

- public ShopcategoriesReadFindShopCategoriesByWareIdRequest()

- ShopcategoriesReadFindShopCategoriesByWareIdRequest WareId(long wareId)

- JdRequest Raw()


## ShopcategoriesReadFindShopCategoriesByWareIdResponse (class)

- public List<string> shopCategories;

- public string Raw;


## ShopcategoriesWriteSaveWareShopCategoriesRequest (class)

- JdRequest req;

- public ShopcategoriesWriteSaveWareShopCategoriesRequest()

- ShopcategoriesWriteSaveWareShopCategoriesRequest WareId(long wareId)

- ShopcategoriesWriteSaveWareShopCategoriesRequest ShopCategory(string shopCategory)

- JdRequest Raw()


## ShopcategoriesWriteSaveWareShopCategoriesResponse (class)

- public bool success;

- public string Raw;
