# Sdk.Jd.Api.Adwords

> 源码: `stdlib/Sdk/Jd/Api/Adwords/AdwordsReadFindAdWordsByWareIdRequest.zan`, `stdlib/Sdk/Jd/Api/Adwords/AdwordsWriteUpdateWareAdWordsRequest.zan`, `stdlib/Sdk/Jd/Api/Adwords/JdAdwordsApi.zan`


## AdwordsReadFindAdWordsByWareIdRequest (class)

<c>jingdong.adwords.read.findAdWordsByWareId</c> 的请求。

- JdRequest req;

- public AdwordsReadFindAdWordsByWareIdRequest()

- AdwordsReadFindAdWordsByWareIdRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdwordsReadFindAdWordsByWareIdResponse (class)

<c>jingdong.adwords.read.findAdWordsByWareId</c> 的响应。

- public AdWords adWords;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## AdwordsWriteUpdateWareAdWordsRequest (class)

<c>jingdong.adwords.write.updateWareAdWords</c> 的请求。

- JdRequest req;

- public AdwordsWriteUpdateWareAdWordsRequest()

- AdwordsWriteUpdateWareAdWordsRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- AdwordsWriteUpdateWareAdWordsRequest Url(string url)
  - 设置 <c>url</c> 参数。

- AdwordsWriteUpdateWareAdWordsRequest UrlWords(string urlWords)
  - 设置 <c>urlWords</c> 参数。

- AdwordsWriteUpdateWareAdWordsRequest Words(string words)
  - 设置 <c>words</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## AdwordsWriteUpdateWareAdWordsResponse (class)

<c>jingdong.adwords.write.updateWareAdWords</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdAdwordsApi (class)

jingdong.adwords.* 的强类型客户端。

- JdClient client;

- public JdAdwordsApi(JdClient client)

- async AdwordsReadFindAdWordsByWareIdResponse ReadFindAdWordsByWareIdAsync(AdwordsReadFindAdWordsByWareIdRequest request)
  - 执行 <c>jingdong.adwords.read.findAdWordsByWareId</c>。

- async AdwordsWriteUpdateWareAdWordsResponse WriteUpdateWareAdWordsAsync(AdwordsWriteUpdateWareAdWordsRequest request)
  - 执行 <c>jingdong.adwords.write.updateWareAdWords</c>。
