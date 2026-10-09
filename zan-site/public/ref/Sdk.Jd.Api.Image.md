# Sdk.Jd.Api.Image

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Image/ImageReadFindFirstImageRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Image/ImageReadFindImagesByColorRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Image/ImageReadFindImagesByWareIdRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Image/ImageWriteDeleteRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Image/ImageWriteUpdateRectangleRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Image/ImageWriteUpdateRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Image/JdImageApi.zan`


## ImageReadFindFirstImageRequest (class)

- JdRequest req;

- public ImageReadFindFirstImageRequest()

- ImageReadFindFirstImageRequest WareId(long wareId)

- ImageReadFindFirstImageRequest ColorId(string colorId)

- JdRequest Raw()


## ImageReadFindFirstImageResponse (class)

- public Image image;

- public string Raw;


## ImageReadFindImagesByColorRequest (class)

- JdRequest req;

- public ImageReadFindImagesByColorRequest()

- ImageReadFindImagesByColorRequest WareId(long wareId)

- ImageReadFindImagesByColorRequest ColorId(string colorId)

- JdRequest Raw()


## ImageReadFindImagesByColorResponse (class)

- public List<string> images;

- public string Raw;


## ImageReadFindImagesByWareIdRequest (class)

- JdRequest req;

- public ImageReadFindImagesByWareIdRequest()

- ImageReadFindImagesByWareIdRequest WareId(long wareId)

- JdRequest Raw()


## ImageReadFindImagesByWareIdResponse (class)

- public List<string> images;

- public string Raw;


## ImageWriteDeleteRequest (class)

- JdRequest req;

- public ImageWriteDeleteRequest()

- ImageWriteDeleteRequest WareId(long wareId)

- ImageWriteDeleteRequest ColorIds(string colorIds)

- ImageWriteDeleteRequest ImgIndexes(string imgIndexes)

- JdRequest Raw()


## ImageWriteDeleteResponse (class)

- public bool success;

- public string Raw;


## ImageWriteUpdateRectangleRequest (class)

- JdRequest req;

- public ImageWriteUpdateRectangleRequest()

- ImageWriteUpdateRectangleRequest WareId(long wareId)

- ImageWriteUpdateRectangleRequest ColorId(string colorId)

- ImageWriteUpdateRectangleRequest ImgId(string imgId)

- ImageWriteUpdateRectangleRequest ImgRectangleUrl(string imgRectangleUrl)

- ImageWriteUpdateRectangleRequest ImgIndex(string imgIndex)

- ImageWriteUpdateRectangleRequest IsGgt(string isGgt)

- JdRequest Raw()


## ImageWriteUpdateRectangleResponse (class)

- public bool success;

- public string Raw;


## ImageWriteUpdateRequest (class)

- JdRequest req;

- public ImageWriteUpdateRequest()

- ImageWriteUpdateRequest WareId(long wareId)

- ImageWriteUpdateRequest ColorId(string colorId)

- ImageWriteUpdateRequest ImgId(string imgId)

- ImageWriteUpdateRequest ImgIndex(string imgIndex)

- ImageWriteUpdateRequest ImgUrl(string imgUrl)

- ImageWriteUpdateRequest ImgZoneId(string imgZoneId)

- ImageWriteUpdateRequest IsGgt(string isGgt)

- JdRequest Raw()


## ImageWriteUpdateResponse (class)

- public bool success;

- public string Raw;


## JdImageApi (class)

- JdClient client;

- public JdImageApi(JdClient client)

- async ImageReadFindFirstImageResponse ReadFindFirstImageAsync(ImageReadFindFirstImageRequest request)

- async ImageReadFindImagesByColorResponse ReadFindImagesByColorAsync(ImageReadFindImagesByColorRequest request)

- async ImageReadFindImagesByWareIdResponse ReadFindImagesByWareIdAsync(ImageReadFindImagesByWareIdRequest request)

- async ImageWriteDeleteResponse WriteDeleteAsync(ImageWriteDeleteRequest request)

- async ImageWriteUpdateRectangleResponse WriteUpdateRectangleAsync(ImageWriteUpdateRectangleRequest request)

- async ImageWriteUpdateResponse WriteUpdateAsync(ImageWriteUpdateRequest request)
