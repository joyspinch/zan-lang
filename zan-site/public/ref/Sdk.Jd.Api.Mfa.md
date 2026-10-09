# Sdk.Jd.Api.Mfa

> 源码: `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mfa/JdMfaApi.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mfa/MfaInnerEliminateRiskRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mfa/MfaInnerSendCodeToMobileRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mfa/MfaInnerUserUnifiedAuthenticationRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mfa/MfaInnerValidateMsgCodeRequest.zan`, `packages/Zan.Sdk.Jd/src/Sdk/Jd/Api/Mfa/MfaUserUnifiedAuthenticationRequest.zan`


## JdMfaApi (class)

- JdClient client;

- public JdMfaApi(JdClient client)

- async MfaInnerEliminateRiskResponse InnerEliminateRiskAsync(MfaInnerEliminateRiskRequest request)

- async MfaInnerSendCodeToMobileResponse InnerSendCodeToMobileAsync(MfaInnerSendCodeToMobileRequest request)

- async MfaInnerUserUnifiedAuthenticationResponse InnerUserUnifiedAuthenticationAsync(MfaInnerUserUnifiedAuthenticationRequest request)

- async MfaInnerValidateMsgCodeResponse InnerValidateMsgCodeAsync(MfaInnerValidateMsgCodeRequest request)

- async MfaUserUnifiedAuthenticationResponse UserUnifiedAuthenticationAsync(MfaUserUnifiedAuthenticationRequest request)


## MfaInnerEliminateRiskRequest (class)

- JdRequest req;

- public MfaInnerEliminateRiskRequest()

- MfaInnerEliminateRiskRequest RKey(string rKey)

- MfaInnerEliminateRiskRequest ValidateType(int validateType)

- JdRequest Raw()


## MfaInnerEliminateRiskResponse (class)

- public SafeCResult returnType;

- public string Raw;


## MfaInnerSendCodeToMobileRequest (class)

- JdRequest req;

- public MfaInnerSendCodeToMobileRequest()

- MfaInnerSendCodeToMobileRequest RKey(string rKey)

- MfaInnerSendCodeToMobileRequest ValidateType(int validateType)

- JdRequest Raw()


## MfaInnerSendCodeToMobileResponse (class)

- public SafeCResult returnType;

- public string Raw;


## MfaInnerUserUnifiedAuthenticationRequest (class)

- JdRequest req;

- public MfaInnerUserUnifiedAuthenticationRequest()

- MfaInnerUserUnifiedAuthenticationRequest DeviceOSType(string deviceOSType)

- MfaInnerUserUnifiedAuthenticationRequest AppId(string appId)

- MfaInnerUserUnifiedAuthenticationRequest BusinessType(int businessType)

- MfaInnerUserUnifiedAuthenticationRequest Eid(string eid)

- MfaInnerUserUnifiedAuthenticationRequest OpenUDID(string openUDID)

- MfaInnerUserUnifiedAuthenticationRequest Source(string source)

- MfaInnerUserUnifiedAuthenticationRequest DeviceName(string deviceName)

- MfaInnerUserUnifiedAuthenticationRequest Email(string email)

- MfaInnerUserUnifiedAuthenticationRequest DeviceOSVersion(string deviceOSVersion)

- MfaInnerUserUnifiedAuthenticationRequest Pin(string pin)

- MfaInnerUserUnifiedAuthenticationRequest AppVersion(string appVersion)

- MfaInnerUserUnifiedAuthenticationRequest LoginChannel(string loginChannel)

- MfaInnerUserUnifiedAuthenticationRequest AuthType(string authType)

- MfaInnerUserUnifiedAuthenticationRequest ClientIp(string clientIp)

- MfaInnerUserUnifiedAuthenticationRequest Uuid(string uuid)

- MfaInnerUserUnifiedAuthenticationRequest Mobile(string mobile)

- MfaInnerUserUnifiedAuthenticationRequest OpenIdBuyer(string openIdBuyer)

- MfaInnerUserUnifiedAuthenticationRequest XidBuyer(string xidBuyer)

- JdRequest Raw()


## MfaInnerUserUnifiedAuthenticationResponse (class)

- public SafeCResult returnType;

- public string Raw;


## MfaInnerValidateMsgCodeRequest (class)

- JdRequest req;

- public MfaInnerValidateMsgCodeRequest()

- MfaInnerValidateMsgCodeRequest MsgCode(string msgCode)

- MfaInnerValidateMsgCodeRequest RKey(string rKey)

- MfaInnerValidateMsgCodeRequest ValidateType(int validateType)

- JdRequest Raw()


## MfaInnerValidateMsgCodeResponse (class)

- public SafeCResult returnType;

- public string Raw;


## MfaUserUnifiedAuthenticationRequest (class)

- JdRequest req;

- public MfaUserUnifiedAuthenticationRequest()

- MfaUserUnifiedAuthenticationRequest ReturnUrl(string returnUrl)

- MfaUserUnifiedAuthenticationRequest DeviceOSType(string deviceOSType)

- MfaUserUnifiedAuthenticationRequest AppId(string appId)

- MfaUserUnifiedAuthenticationRequest BusinessType(int businessType)

- MfaUserUnifiedAuthenticationRequest Eid(string eid)

- MfaUserUnifiedAuthenticationRequest OpenUDID(string openUDID)

- MfaUserUnifiedAuthenticationRequest Source(string source)

- MfaUserUnifiedAuthenticationRequest DeviceName(string deviceName)

- MfaUserUnifiedAuthenticationRequest Email(string email)

- MfaUserUnifiedAuthenticationRequest DeviceOSVersion(string deviceOSVersion)

- MfaUserUnifiedAuthenticationRequest Pin(string pin)

- MfaUserUnifiedAuthenticationRequest AppVersion(string appVersion)

- MfaUserUnifiedAuthenticationRequest LoginChannel(string loginChannel)

- MfaUserUnifiedAuthenticationRequest AuthType(string authType)

- MfaUserUnifiedAuthenticationRequest ClientIp(string clientIp)

- MfaUserUnifiedAuthenticationRequest Uuid(string uuid)

- MfaUserUnifiedAuthenticationRequest Mobile(string mobile)

- MfaUserUnifiedAuthenticationRequest OpenIdBuyer(string openIdBuyer)

- MfaUserUnifiedAuthenticationRequest XidBuyer(string xidBuyer)

- JdRequest Raw()


## MfaUserUnifiedAuthenticationResponse (class)

- public SafeCResult returnType;

- public string Raw;
