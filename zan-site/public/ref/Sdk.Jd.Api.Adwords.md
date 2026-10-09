# Sdk.Jd.Api.Adwords

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Adwords/AdwordsReadFindAdWordsByWareIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Adwords/AdwordsWriteUpdateWareAdWordsRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Adwords/JdAdwordsApi.zan`


## AdwordsReadFindAdWordsByWareIdRequest (class)

- JdRequest req;

- public AdwordsReadFindAdWordsByWareIdRequest()

- AdwordsReadFindAdWordsByWareIdRequest WareId(long wareId)

- JdRequest Raw()


## AdwordsReadFindAdWordsByWareIdResponse (class)

- public AdWords adWords;

- public string Raw;


## AdwordsWriteUpdateWareAdWordsRequest (class)

- JdRequest req;

- public AdwordsWriteUpdateWareAdWordsRequest()

- AdwordsWriteUpdateWareAdWordsRequest WareId(long wareId)

- AdwordsWriteUpdateWareAdWordsRequest Url(string url)

- AdwordsWriteUpdateWareAdWordsRequest UrlWords(string urlWords)

- AdwordsWriteUpdateWareAdWordsRequest Words(string words)

- JdRequest Raw()


## AdwordsWriteUpdateWareAdWordsResponse (class)

- public bool success;

- public string Raw;


## JdAdwordsApi (class)

- JdClient client;

- public JdAdwordsApi(JdClient client)

- async AdwordsReadFindAdWordsByWareIdResponse ReadFindAdWordsByWareIdAsync(AdwordsReadFindAdWordsByWareIdRequest request)

- async AdwordsWriteUpdateWareAdWordsResponse WriteUpdateWareAdWordsAsync(AdwordsWriteUpdateWareAdWordsRequest request)
