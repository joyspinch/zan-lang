# Sdk.Jd.Api.Detection

> 源码: `stdlib/Sdk/Jd/Api/Detection/DetectionImagesRedLineDetectBatchRequest.zan`, `stdlib/Sdk/Jd/Api/Detection/JdDetectionApi.zan`


## DetectionImagesRedLineDetectBatchRequest (class)

<c>jingdong.detection.imagesRedLineDetectBatch</c> 的请求。

- JdRequest req;

- public DetectionImagesRedLineDetectBatchRequest()

- DetectionImagesRedLineDetectBatchRequest TimeZone(string timeZone)
  - 设置 <c>timeZone</c> 参数。

- DetectionImagesRedLineDetectBatchRequest Key(string key)
  - 设置 <c>key</c> 参数。

- DetectionImagesRedLineDetectBatchRequest Value(JsonValue value_)
  - 设置 <c>value</c> 参数。

- DetectionImagesRedLineDetectBatchRequest DetectItem(string detectItem)
  - 设置 <c>detectItem</c> 参数。

- DetectionImagesRedLineDetectBatchRequest ImageUrl(string imageUrl)
  - 设置 <c>imageUrl</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## DetectionImagesRedLineDetectBatchResponse (class)

<c>jingdong.detection.imagesRedLineDetectBatch</c> 的响应。

- public ImageBatchDetectResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## JdDetectionApi (class)

jingdong.detection.* 的强类型客户端。

- JdClient client;

- public JdDetectionApi(JdClient client)

- async DetectionImagesRedLineDetectBatchResponse ImagesRedLineDetectBatchAsync(DetectionImagesRedLineDetectBatchRequest request)
  - 执行 <c>jingdong.detection.imagesRedLineDetectBatch</c>。
