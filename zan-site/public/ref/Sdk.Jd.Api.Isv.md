# Sdk.Jd.Api.Isv

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Isv/IsvAddisvlogRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Isv/IsvUploadBatchLogRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Isv/IsvUploadDBOperationLogRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Isv/IsvUploadLoginLogRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Isv/IsvUploadOrderInfoLogRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Isv/IsvUploadThirdAppTransmitOrderInfoLogRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Isv/JdIsvApi.zan`


## IsvAddisvlogRequest (class)

- JdRequest req;

- public IsvAddisvlogRequest()

- IsvAddisvlogRequest Account(string account)

- IsvAddisvlogRequest ClientIp(string clientIp)

- IsvAddisvlogRequest OperationTime(string operationTime)

- IsvAddisvlogRequest OperationContent(string operationContent)

- IsvAddisvlogRequest UseIsvAppkey(string useIsvAppkey)

- IsvAddisvlogRequest ReqjosUrl(string reqjosUrl)

- IsvAddisvlogRequest TouchNumber(string touchNumber)

- IsvAddisvlogRequest TouchFiles(string touchFiles)

- JdRequest Raw()


## IsvAddisvlogResponse (class)

- public string success;

- public string Raw;


## IsvUploadBatchLogRequest (class)

- JdRequest req;

- public IsvUploadBatchLogRequest()

- IsvUploadBatchLogRequest JosAppKey(string josAppKey)

- IsvUploadBatchLogRequest Data(string data)

- IsvUploadBatchLogRequest TimeStamp(string timeStamp)

- IsvUploadBatchLogRequest Type(string type)

- JdRequest Raw()


## IsvUploadBatchLogResponse (class)

- public int c;

- public string Raw;


## IsvUploadDBOperationLogRequest (class)

- JdRequest req;

- public IsvUploadDBOperationLogRequest()

- IsvUploadDBOperationLogRequest UserIp(string userIp)

- IsvUploadDBOperationLogRequest AppName(string appName)

- IsvUploadDBOperationLogRequest JosAppKey(string josAppKey)

- IsvUploadDBOperationLogRequest DeviceId(string deviceId)

- IsvUploadDBOperationLogRequest UserId(string userId)

- IsvUploadDBOperationLogRequest Url(string url)

- IsvUploadDBOperationLogRequest Db(string db)

- IsvUploadDBOperationLogRequest Sql(string sql)

- IsvUploadDBOperationLogRequest TimeStamp(string timeStamp)

- JdRequest Raw()


## IsvUploadDBOperationLogResponse (class)

- public int c;

- public string Raw;


## IsvUploadLoginLogRequest (class)

- JdRequest req;

- public IsvUploadLoginLogRequest()

- IsvUploadLoginLogRequest Result(string result)

- IsvUploadLoginLogRequest UserIp(string userIp)

- IsvUploadLoginLogRequest AppName(string appName)

- IsvUploadLoginLogRequest JosAppKey(string josAppKey)

- IsvUploadLoginLogRequest JdId(string jdId)

- IsvUploadLoginLogRequest DeviceId(string deviceId)

- IsvUploadLoginLogRequest UserId(string userId)

- IsvUploadLoginLogRequest Message(string message)

- IsvUploadLoginLogRequest TimeStamp(string timeStamp)

- JdRequest Raw()


## IsvUploadLoginLogResponse (class)

- public int c;

- public string Raw;


## IsvUploadOrderInfoLogRequest (class)

- JdRequest req;

- public IsvUploadOrderInfoLogRequest()

- IsvUploadOrderInfoLogRequest UserIp(string userIp)

- IsvUploadOrderInfoLogRequest AppName(string appName)

- IsvUploadOrderInfoLogRequest JosAppKey(string josAppKey)

- IsvUploadOrderInfoLogRequest JdId(string jdId)

- IsvUploadOrderInfoLogRequest DeviceId(string deviceId)

- IsvUploadOrderInfoLogRequest UserId(string userId)

- IsvUploadOrderInfoLogRequest FileMd5(string fileMd5)

- IsvUploadOrderInfoLogRequest OrderIds(string orderIds)

- IsvUploadOrderInfoLogRequest Operation(string operation)

- IsvUploadOrderInfoLogRequest Url(string url)

- IsvUploadOrderInfoLogRequest TimeStamp(string timeStamp)

- JdRequest Raw()


## IsvUploadOrderInfoLogResponse (class)

- public int c;

- public string Raw;


## IsvUploadThirdAppTransmitOrderInfoLogRequest (class)

- JdRequest req;

- public IsvUploadThirdAppTransmitOrderInfoLogRequest()

- IsvUploadThirdAppTransmitOrderInfoLogRequest AppName(string appName)

- IsvUploadThirdAppTransmitOrderInfoLogRequest UserIp(string userIp)

- IsvUploadThirdAppTransmitOrderInfoLogRequest JosAppKey(string josAppKey)

- IsvUploadThirdAppTransmitOrderInfoLogRequest DeviceId(string deviceId)

- IsvUploadThirdAppTransmitOrderInfoLogRequest UserId(string userId)

- IsvUploadThirdAppTransmitOrderInfoLogRequest OrderIds(string orderIds)

- IsvUploadThirdAppTransmitOrderInfoLogRequest SendtoUrl(string sendtoUrl)

- IsvUploadThirdAppTransmitOrderInfoLogRequest Url(string url)

- IsvUploadThirdAppTransmitOrderInfoLogRequest TimeStamp(string timeStamp)

- JdRequest Raw()


## IsvUploadThirdAppTransmitOrderInfoLogResponse (class)

- public int c;

- public string Raw;


## JdIsvApi (class)

- JdClient client;

- public JdIsvApi(JdClient client)

- async IsvAddisvlogResponse AddisvlogAsync(IsvAddisvlogRequest request)

- async IsvUploadBatchLogResponse UploadBatchLogAsync(IsvUploadBatchLogRequest request)

- async IsvUploadDBOperationLogResponse UploadDBOperationLogAsync(IsvUploadDBOperationLogRequest request)

- async IsvUploadLoginLogResponse UploadLoginLogAsync(IsvUploadLoginLogRequest request)

- async IsvUploadOrderInfoLogResponse UploadOrderInfoLogAsync(IsvUploadOrderInfoLogRequest request)

- async IsvUploadThirdAppTransmitOrderInfoLogResponse UploadThirdAppTransmitOrderInfoLogAsync(IsvUploadThirdAppTransmitOrderInfoLogRequest request)
