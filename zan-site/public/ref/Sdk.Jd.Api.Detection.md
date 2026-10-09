# Sdk.Jd.Api.Detection

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Detection/DetectionImagesRedLineDetectBatchRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Detection/JdDetectionApi.zan`


## DetectionImagesRedLineDetectBatchRequest (class)

- JdRequest req;

- public DetectionImagesRedLineDetectBatchRequest()

- DetectionImagesRedLineDetectBatchRequest TimeZone(string timeZone)

- DetectionImagesRedLineDetectBatchRequest Key(string key)

- DetectionImagesRedLineDetectBatchRequest Value(JsonValue value_)

- DetectionImagesRedLineDetectBatchRequest DetectItem(string detectItem)

- DetectionImagesRedLineDetectBatchRequest ImageUrl(string imageUrl)

- JdRequest Raw()


## DetectionImagesRedLineDetectBatchResponse (class)

- public ImageBatchDetectResult returnType;

- public string Raw;


## JdDetectionApi (class)

- JdClient client;

- public JdDetectionApi(JdClient client)

- async DetectionImagesRedLineDetectBatchResponse ImagesRedLineDetectBatchAsync(DetectionImagesRedLineDetectBatchRequest request)
