# Sdk.Jd.Api.Isv

> 源码: `stdlib/Sdk/Jd/Api/Isv/IsvAddisvlogRequest.zan`, `stdlib/Sdk/Jd/Api/Isv/IsvUploadBatchLogRequest.zan`, `stdlib/Sdk/Jd/Api/Isv/IsvUploadDBOperationLogRequest.zan`, `stdlib/Sdk/Jd/Api/Isv/IsvUploadLoginLogRequest.zan`, `stdlib/Sdk/Jd/Api/Isv/IsvUploadOrderInfoLogRequest.zan`, `stdlib/Sdk/Jd/Api/Isv/IsvUploadThirdAppTransmitOrderInfoLogRequest.zan`, `stdlib/Sdk/Jd/Api/Isv/JdIsvApi.zan`


## IsvAddisvlogRequest (class)

<c>jingdong.isv.addisvlog</c> 的请求。

- JdRequest req;

- public IsvAddisvlogRequest()

- IsvAddisvlogRequest Account(string account)
  - 设置 <c>account</c> 参数。

- IsvAddisvlogRequest ClientIp(string clientIp)
  - 设置 <c>clientIp</c> 参数。

- IsvAddisvlogRequest OperationTime(string operationTime)
  - 设置 <c>operationTime</c> 参数。

- IsvAddisvlogRequest OperationContent(string operationContent)
  - 设置 <c>operationContent</c> 参数。

- IsvAddisvlogRequest UseIsvAppkey(string useIsvAppkey)
  - 设置 <c>useIsvAppkey</c> 参数。

- IsvAddisvlogRequest ReqjosUrl(string reqjosUrl)
  - 设置 <c>reqjosUrl</c> 参数。

- IsvAddisvlogRequest TouchNumber(string touchNumber)
  - 设置 <c>touchNumber</c> 参数。

- IsvAddisvlogRequest TouchFiles(string touchFiles)
  - 设置 <c>touchFiles</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## IsvAddisvlogResponse (class)

<c>jingdong.isv.addisvlog</c> 的响应。

- public string success;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## IsvUploadBatchLogRequest (class)

<c>jingdong.isv.uploadBatchLog</c> 的请求。

- JdRequest req;

- public IsvUploadBatchLogRequest()

- IsvUploadBatchLogRequest JosAppKey(string josAppKey)
  - 设置 <c>josAppKey</c> 参数。

- IsvUploadBatchLogRequest Data(string data)
  - 设置 <c>data</c> 参数。

- IsvUploadBatchLogRequest TimeStamp(string timeStamp)
  - 设置 <c>time_stamp</c> 参数。

- IsvUploadBatchLogRequest Type(string type)
  - 设置 <c>type</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## IsvUploadBatchLogResponse (class)

<c>jingdong.isv.uploadBatchLog</c> 的响应。

- public int c;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## IsvUploadDBOperationLogRequest (class)

<c>jingdong.isv.uploadDBOperationLog</c> 的请求。

- JdRequest req;

- public IsvUploadDBOperationLogRequest()

- IsvUploadDBOperationLogRequest UserIp(string userIp)
  - 设置 <c>user_ip</c> 参数。

- IsvUploadDBOperationLogRequest AppName(string appName)
  - 设置 <c>app_name</c> 参数。

- IsvUploadDBOperationLogRequest JosAppKey(string josAppKey)
  - 设置 <c>josAppKey</c> 参数。

- IsvUploadDBOperationLogRequest DeviceId(string deviceId)
  - 设置 <c>device_id</c> 参数。

- IsvUploadDBOperationLogRequest UserId(string userId)
  - 设置 <c>user_id</c> 参数。

- IsvUploadDBOperationLogRequest Url(string url)
  - 设置 <c>url</c> 参数。

- IsvUploadDBOperationLogRequest Db(string db)
  - 设置 <c>db</c> 参数。

- IsvUploadDBOperationLogRequest Sql(string sql)
  - 设置 <c>sql</c> 参数。

- IsvUploadDBOperationLogRequest TimeStamp(string timeStamp)
  - 设置 <c>time_stamp</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## IsvUploadDBOperationLogResponse (class)

<c>jingdong.isv.uploadDBOperationLog</c> 的响应。

- public int c;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## IsvUploadLoginLogRequest (class)

<c>jingdong.isv.uploadLoginLog</c> 的请求。

- JdRequest req;

- public IsvUploadLoginLogRequest()

- IsvUploadLoginLogRequest Result(string result)
  - 设置 <c>result</c> 参数。

- IsvUploadLoginLogRequest UserIp(string userIp)
  - 设置 <c>user_ip</c> 参数。

- IsvUploadLoginLogRequest AppName(string appName)
  - 设置 <c>app_name</c> 参数。

- IsvUploadLoginLogRequest JosAppKey(string josAppKey)
  - 设置 <c>josAppKey</c> 参数。

- IsvUploadLoginLogRequest JdId(string jdId)
  - 设置 <c>jd_id</c> 参数。

- IsvUploadLoginLogRequest DeviceId(string deviceId)
  - 设置 <c>device_id</c> 参数。

- IsvUploadLoginLogRequest UserId(string userId)
  - 设置 <c>user_id</c> 参数。

- IsvUploadLoginLogRequest Message(string message)
  - 设置 <c>message</c> 参数。

- IsvUploadLoginLogRequest TimeStamp(string timeStamp)
  - 设置 <c>time_stamp</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## IsvUploadLoginLogResponse (class)

<c>jingdong.isv.uploadLoginLog</c> 的响应。

- public int c;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## IsvUploadOrderInfoLogRequest (class)

<c>jingdong.isv.uploadOrderInfoLog</c> 的请求。

- JdRequest req;

- public IsvUploadOrderInfoLogRequest()

- IsvUploadOrderInfoLogRequest UserIp(string userIp)
  - 设置 <c>user_ip</c> 参数。

- IsvUploadOrderInfoLogRequest AppName(string appName)
  - 设置 <c>app_name</c> 参数。

- IsvUploadOrderInfoLogRequest JosAppKey(string josAppKey)
  - 设置 <c>josAppKey</c> 参数。

- IsvUploadOrderInfoLogRequest JdId(string jdId)
  - 设置 <c>jd_id</c> 参数。

- IsvUploadOrderInfoLogRequest DeviceId(string deviceId)
  - 设置 <c>device_id</c> 参数。

- IsvUploadOrderInfoLogRequest UserId(string userId)
  - 设置 <c>user_id</c> 参数。

- IsvUploadOrderInfoLogRequest FileMd5(string fileMd5)
  - 设置 <c>file_md5</c> 参数。

- IsvUploadOrderInfoLogRequest OrderIds(string orderIds)
  - 设置 <c>order_ids</c> 参数。

- IsvUploadOrderInfoLogRequest Operation(string operation)
  - 设置 <c>operation</c> 参数。

- IsvUploadOrderInfoLogRequest Url(string url)
  - 设置 <c>url</c> 参数。

- IsvUploadOrderInfoLogRequest TimeStamp(string timeStamp)
  - 设置 <c>time_stamp</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## IsvUploadOrderInfoLogResponse (class)

<c>jingdong.isv.uploadOrderInfoLog</c> 的响应。

- public int c;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## IsvUploadThirdAppTransmitOrderInfoLogRequest (class)

<c>jingdong.isv.uploadThirdAppTransmitOrderInfoLog</c> 的请求。

- JdRequest req;

- public IsvUploadThirdAppTransmitOrderInfoLogRequest()

- IsvUploadThirdAppTransmitOrderInfoLogRequest AppName(string appName)
  - 设置 <c>app_name</c> 参数。

- IsvUploadThirdAppTransmitOrderInfoLogRequest UserIp(string userIp)
  - 设置 <c>user_ip</c> 参数。

- IsvUploadThirdAppTransmitOrderInfoLogRequest JosAppKey(string josAppKey)
  - 设置 <c>josAppKey</c> 参数。

- IsvUploadThirdAppTransmitOrderInfoLogRequest DeviceId(string deviceId)
  - 设置 <c>device_id</c> 参数。

- IsvUploadThirdAppTransmitOrderInfoLogRequest UserId(string userId)
  - 设置 <c>user_id</c> 参数。

- IsvUploadThirdAppTransmitOrderInfoLogRequest OrderIds(string orderIds)
  - 设置 <c>order_ids</c> 参数。

- IsvUploadThirdAppTransmitOrderInfoLogRequest SendtoUrl(string sendtoUrl)
  - 设置 <c>sendto_url</c> 参数。

- IsvUploadThirdAppTransmitOrderInfoLogRequest Url(string url)
  - 设置 <c>url</c> 参数。

- IsvUploadThirdAppTransmitOrderInfoLogRequest TimeStamp(string timeStamp)
  - 设置 <c>time_stamp</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## IsvUploadThirdAppTransmitOrderInfoLogResponse (class)

<c>jingdong.isv.uploadThirdAppTransmitOrderInfoLog</c> 的响应。

- public int c;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## JdIsvApi (class)

jingdong.isv.* 的强类型客户端。

- JdClient client;

- public JdIsvApi(JdClient client)

- async IsvAddisvlogResponse AddisvlogAsync(IsvAddisvlogRequest request)
  - 执行 <c>jingdong.isv.addisvlog</c>。

- async IsvUploadBatchLogResponse UploadBatchLogAsync(IsvUploadBatchLogRequest request)
  - 执行 <c>jingdong.isv.uploadBatchLog</c>。

- async IsvUploadDBOperationLogResponse UploadDBOperationLogAsync(IsvUploadDBOperationLogRequest request)
  - 执行 <c>jingdong.isv.uploadDBOperationLog</c>。

- async IsvUploadLoginLogResponse UploadLoginLogAsync(IsvUploadLoginLogRequest request)
  - 执行 <c>jingdong.isv.uploadLoginLog</c>。

- async IsvUploadOrderInfoLogResponse UploadOrderInfoLogAsync(IsvUploadOrderInfoLogRequest request)
  - 执行 <c>jingdong.isv.uploadOrderInfoLog</c>。

- async IsvUploadThirdAppTransmitOrderInfoLogResponse UploadThirdAppTransmitOrderInfoLogAsync(IsvUploadThirdAppTransmitOrderInfoLogRequest request)
  - 执行 <c>jingdong.isv.uploadThirdAppTransmitOrderInfoLog</c>。
