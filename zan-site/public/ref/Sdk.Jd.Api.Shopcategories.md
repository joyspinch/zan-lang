# Sdk.Jd.Api.Shopcategories

> 源码: `stdlib/Sdk/Jd/Api/Shopcategories/JdShopcategoriesApi.zan`, `stdlib/Sdk/Jd/Api/Shopcategories/ShopcategoriesReadFindShopCategoriesByWareIdRequest.zan`, `stdlib/Sdk/Jd/Api/Shopcategories/ShopcategoriesWriteSaveWareShopCategoriesRequest.zan`


## JdShopcategoriesApi (class)

jingdong.shopcategories.* 的强类型客户端。

- JdClient client;

- public JdShopcategoriesApi(JdClient client)

- async ShopcategoriesReadFindShopCategoriesByWareIdResponse ReadFindShopCategoriesByWareIdAsync(ShopcategoriesReadFindShopCategoriesByWareIdRequest request)
  - 执行 <c>jingdong.shopcategories.read.findShopCategoriesByWareId</c>。

- async ShopcategoriesWriteSaveWareShopCategoriesResponse WriteSaveWareShopCategoriesAsync(ShopcategoriesWriteSaveWareShopCategoriesRequest request)
  - 执行 <c>jingdong.shopcategories.write.saveWareShopCategories</c>。


## ShopcategoriesReadFindShopCategoriesByWareIdRequest (class)

<c>jingdong.shopcategories.read.findShopCategoriesByWareId</c> 的请求。

- JdRequest req;

- public ShopcategoriesReadFindShopCategoriesByWareIdRequest()

- ShopcategoriesReadFindShopCategoriesByWareIdRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ShopcategoriesReadFindShopCategoriesByWareIdResponse (class)

<c>jingdong.shopcategories.read.findShopCategoriesByWareId</c> 的响应。

- public List<string> shopCategories;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## ShopcategoriesWriteSaveWareShopCategoriesRequest (class)

<c>jingdong.shopcategories.write.saveWareShopCategories</c> 的请求。

- JdRequest req;

- public ShopcategoriesWriteSaveWareShopCategoriesRequest()

- ShopcategoriesWriteSaveWareShopCategoriesRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- ShopcategoriesWriteSaveWareShopCategoriesRequest ShopCategory(string shopCategory)
  - 设置 <c>shopCategory</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ShopcategoriesWriteSaveWareShopCategoriesResponse (class)

<c>jingdong.shopcategories.write.saveWareShopCategories</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
