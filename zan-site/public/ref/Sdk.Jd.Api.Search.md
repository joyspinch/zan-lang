# Sdk.Jd.Api.Search

> 源码: `stdlib/Sdk/Jd/Api/Search/JdSearchApi.zan`, `stdlib/Sdk/Jd/Api/Search/SearchWareRequest.zan`


## JdSearchApi (class)

jingdong.search.* 的强类型客户端。

- JdClient client;

- public JdSearchApi(JdClient client)

- async SearchWareResponse WareAsync(SearchWareRequest request)
  - 执行 <c>jingdong.search.ware</c>。


## SearchWareRequest (class)

<c>jingdong.search.ware</c> 的请求。

- JdRequest req;

- public SearchWareRequest()

- SearchWareRequest Key(string key)
  - 设置 <c>key</c> 参数。

- SearchWareRequest FiltType(string filtType)
  - 设置 <c>filt_type</c> 参数。

- SearchWareRequest AreaIds(string areaIds)
  - 设置 <c>area_ids</c> 参数。

- SearchWareRequest SortType(string sortType)
  - 设置 <c>sort_type</c> 参数。

- SearchWareRequest Page(string page)
  - 设置 <c>page</c> 参数。

- SearchWareRequest Charset(string charset)
  - 设置 <c>charset</c> 参数。

- SearchWareRequest Urlencode(string urlencode)
  - 设置 <c>urlencode</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## SearchWareResponse (class)

<c>jingdong.search.ware</c> 的响应。

- public Summary Summary;

- public List<string> ObjA_Price;

- public List<string> ObjExtAttrCollection;

- public List<string> Paragraph;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
