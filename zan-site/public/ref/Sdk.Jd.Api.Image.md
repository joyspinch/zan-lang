# Sdk.Jd.Api.Image

> 源码: `stdlib/Sdk/Jd/Api/Image/ImageReadFindFirstImageRequest.zan`, `stdlib/Sdk/Jd/Api/Image/ImageReadFindImagesByColorRequest.zan`, `stdlib/Sdk/Jd/Api/Image/ImageReadFindImagesByWareIdRequest.zan`, `stdlib/Sdk/Jd/Api/Image/ImageWriteDeleteRequest.zan`, `stdlib/Sdk/Jd/Api/Image/ImageWriteUpdateRectangleRequest.zan`, `stdlib/Sdk/Jd/Api/Image/ImageWriteUpdateRequest.zan`, `stdlib/Sdk/Jd/Api/Image/JdImageApi.zan`


## ImageReadFindFirstImageRequest (class)

<c>jingdong.image.read.findFirstImage</c> 的请求。

- JdRequest req;

- public ImageReadFindFirstImageRequest()

- ImageReadFindFirstImageRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- ImageReadFindFirstImageRequest ColorId(string colorId)
  - 设置 <c>colorId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ImageReadFindFirstImageResponse (class)

<c>jingdong.image.read.findFirstImage</c> 的响应。

- public Image image;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## ImageReadFindImagesByColorRequest (class)

<c>jingdong.image.read.findImagesByColor</c> 的请求。

- JdRequest req;

- public ImageReadFindImagesByColorRequest()

- ImageReadFindImagesByColorRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- ImageReadFindImagesByColorRequest ColorId(string colorId)
  - 设置 <c>colorId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ImageReadFindImagesByColorResponse (class)

<c>jingdong.image.read.findImagesByColor</c> 的响应。

- public List<string> images;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## ImageReadFindImagesByWareIdRequest (class)

<c>jingdong.image.read.findImagesByWareId</c> 的请求。

- JdRequest req;

- public ImageReadFindImagesByWareIdRequest()

- ImageReadFindImagesByWareIdRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ImageReadFindImagesByWareIdResponse (class)

<c>jingdong.image.read.findImagesByWareId</c> 的响应。

- public List<string> images;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## ImageWriteDeleteRequest (class)

<c>jingdong.image.write.delete</c> 的请求。

- JdRequest req;

- public ImageWriteDeleteRequest()

- ImageWriteDeleteRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- ImageWriteDeleteRequest ColorIds(string colorIds)
  - 设置 <c>colorIds</c> 参数。

- ImageWriteDeleteRequest ImgIndexes(string imgIndexes)
  - 设置 <c>imgIndexes</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ImageWriteDeleteResponse (class)

<c>jingdong.image.write.delete</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## ImageWriteUpdateRectangleRequest (class)

<c>jingdong.image.write.updateRectangle</c> 的请求。

- JdRequest req;

- public ImageWriteUpdateRectangleRequest()

- ImageWriteUpdateRectangleRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- ImageWriteUpdateRectangleRequest ColorId(string colorId)
  - 设置 <c>colorId</c> 参数。

- ImageWriteUpdateRectangleRequest ImgId(string imgId)
  - 设置 <c>imgId</c> 参数。

- ImageWriteUpdateRectangleRequest ImgRectangleUrl(string imgRectangleUrl)
  - 设置 <c>imgRectangleUrl</c> 参数。

- ImageWriteUpdateRectangleRequest ImgIndex(string imgIndex)
  - 设置 <c>imgIndex</c> 参数。

- ImageWriteUpdateRectangleRequest IsGgt(string isGgt)
  - 设置 <c>isGgt</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ImageWriteUpdateRectangleResponse (class)

<c>jingdong.image.write.updateRectangle</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应信封。


## ImageWriteUpdateRequest (class)

<c>jingdong.image.write.update</c> 的请求。

- JdRequest req;

- public ImageWriteUpdateRequest()

- ImageWriteUpdateRequest WareId(long wareId)
  - 设置 <c>wareId</c> 参数。

- ImageWriteUpdateRequest ColorId(string colorId)
  - 设置 <c>colorId</c> 参数。

- ImageWriteUpdateRequest ImgId(string imgId)
  - 设置 <c>imgId</c> 参数。

- ImageWriteUpdateRequest ImgIndex(string imgIndex)
  - 设置 <c>imgIndex</c> 参数。

- ImageWriteUpdateRequest ImgUrl(string imgUrl)
  - 设置 <c>imgUrl</c> 参数。

- ImageWriteUpdateRequest ImgZoneId(string imgZoneId)
  - 设置 <c>imgZoneId</c> 参数。

- ImageWriteUpdateRequest IsGgt(string isGgt)
  - 设置 <c>isGgt</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## ImageWriteUpdateResponse (class)

<c>jingdong.image.write.update</c> 的响应。

- public bool success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## JdImageApi (class)

jingdong.image.* 的强类型客户端。

- JdClient client;

- public JdImageApi(JdClient client)

- async ImageReadFindFirstImageResponse ReadFindFirstImageAsync(ImageReadFindFirstImageRequest request)
  - 执行 <c>jingdong.image.read.findFirstImage</c>。

- async ImageReadFindImagesByColorResponse ReadFindImagesByColorAsync(ImageReadFindImagesByColorRequest request)
  - 执行 <c>jingdong.image.read.findImagesByColor</c>。

- async ImageReadFindImagesByWareIdResponse ReadFindImagesByWareIdAsync(ImageReadFindImagesByWareIdRequest request)
  - 执行 <c>jingdong.image.read.findImagesByWareId</c>。

- async ImageWriteDeleteResponse WriteDeleteAsync(ImageWriteDeleteRequest request)
  - 执行 <c>jingdong.image.write.delete</c>。

- async ImageWriteUpdateRectangleResponse WriteUpdateRectangleAsync(ImageWriteUpdateRectangleRequest request)
  - 执行 <c>jingdong.image.write.updateRectangle</c>。

- async ImageWriteUpdateResponse WriteUpdateAsync(ImageWriteUpdateRequest request)
  - 执行 <c>jingdong.image.write.update</c>。
