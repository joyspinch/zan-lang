# Sdk.Jd.Api.Search

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Search/JdSearchApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Search/SearchWareRequest.zan`


## JdSearchApi (class)

- JdClient client;

- public JdSearchApi(JdClient client)

- async SearchWareResponse WareAsync(SearchWareRequest request)


## SearchWareRequest (class)

- JdRequest req;

- public SearchWareRequest()

- SearchWareRequest Key(string key)

- SearchWareRequest FiltType(string filtType)

- SearchWareRequest AreaIds(string areaIds)

- SearchWareRequest SortType(string sortType)

- SearchWareRequest Page(string page)

- SearchWareRequest Charset(string charset)

- SearchWareRequest Urlencode(string urlencode)

- JdRequest Raw()


## SearchWareResponse (class)

- public Summary Summary;

- public List<string> ObjA_Price;

- public List<string> ObjExtAttrCollection;

- public List<string> Paragraph;

- public string Raw;
