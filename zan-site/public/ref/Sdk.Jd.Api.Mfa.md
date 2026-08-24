# Sdk.Jd.Api.Mfa

> 源码: `stdlib/Sdk/Jd/Api/Mfa/JdMfaApi.zan`, `stdlib/Sdk/Jd/Api/Mfa/MfaInnerEliminateRiskRequest.zan`, `stdlib/Sdk/Jd/Api/Mfa/MfaInnerSendCodeToMobileRequest.zan`, `stdlib/Sdk/Jd/Api/Mfa/MfaInnerUserUnifiedAuthenticationRequest.zan`, `stdlib/Sdk/Jd/Api/Mfa/MfaInnerValidateMsgCodeRequest.zan`, `stdlib/Sdk/Jd/Api/Mfa/MfaUserUnifiedAuthenticationRequest.zan`


## JdMfaApi (class)

jingdong.mfa.* 的强类型客户端。

- JdClient client;

- public JdMfaApi(JdClient client)

- async MfaInnerEliminateRiskResponse InnerEliminateRiskAsync(MfaInnerEliminateRiskRequest request)
  - 执行 <c>jingdong.mfa.inner.eliminateRisk</c>。

- async MfaInnerSendCodeToMobileResponse InnerSendCodeToMobileAsync(MfaInnerSendCodeToMobileRequest request)
  - 执行 <c>jingdong.mfa.inner.sendCodeToMobile</c>。

- async MfaInnerUserUnifiedAuthenticationResponse InnerUserUnifiedAuthenticationAsync(MfaInnerUserUnifiedAuthenticationRequest request)
  - 执行 <c>jingdong.mfa.inner.userUnifiedAuthentication</c>。

- async MfaInnerValidateMsgCodeResponse InnerValidateMsgCodeAsync(MfaInnerValidateMsgCodeRequest request)
  - 执行 <c>jingdong.mfa.inner.validateMsgCode</c>。

- async MfaUserUnifiedAuthenticationResponse UserUnifiedAuthenticationAsync(MfaUserUnifiedAuthenticationRequest request)
  - 执行 <c>jingdong.mfa.userUnifiedAuthentication</c>。


## MfaInnerEliminateRiskRequest (class)

<c>jingdong.mfa.inner.eliminateRisk</c> 的请求。

- JdRequest req;

- public MfaInnerEliminateRiskRequest()

- MfaInnerEliminateRiskRequest RKey(string rKey)
  - 设置 <c>rKey</c> 参数。

- MfaInnerEliminateRiskRequest ValidateType(int validateType)
  - 设置 <c>validateType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MfaInnerEliminateRiskResponse (class)

<c>jingdong.mfa.inner.eliminateRisk</c> 的响应。

- public SafeCResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MfaInnerSendCodeToMobileRequest (class)

<c>jingdong.mfa.inner.sendCodeToMobile</c> 的请求。

- JdRequest req;

- public MfaInnerSendCodeToMobileRequest()

- MfaInnerSendCodeToMobileRequest RKey(string rKey)
  - 设置 <c>rKey</c> 参数。

- MfaInnerSendCodeToMobileRequest ValidateType(int validateType)
  - 设置 <c>validateType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MfaInnerSendCodeToMobileResponse (class)

<c>jingdong.mfa.inner.sendCodeToMobile</c> 的响应。

- public SafeCResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MfaInnerUserUnifiedAuthenticationRequest (class)

<c>jingdong.mfa.inner.userUnifiedAuthentication</c> 的请求。

- JdRequest req;

- public MfaInnerUserUnifiedAuthenticationRequest()

- MfaInnerUserUnifiedAuthenticationRequest DeviceOSType(string deviceOSType)
  - 设置 <c>deviceOSType</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest AppId(string appId)
  - 设置 <c>appId</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest Eid(string eid)
  - 设置 <c>eid</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest OpenUDID(string openUDID)
  - 设置 <c>openUDID</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest Source(string source)
  - 设置 <c>source</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest DeviceName(string deviceName)
  - 设置 <c>deviceName</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest Email(string email)
  - 设置 <c>email</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest DeviceOSVersion(string deviceOSVersion)
  - 设置 <c>deviceOSVersion</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest Pin(string pin)
  - 设置 <c>pin</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest AppVersion(string appVersion)
  - 设置 <c>appVersion</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest LoginChannel(string loginChannel)
  - 设置 <c>loginChannel</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest ClientIp(string clientIp)
  - 设置 <c>clientIp</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest Uuid(string uuid)
  - 设置 <c>uuid</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest Mobile(string mobile)
  - 设置 <c>mobile</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest OpenIdBuyer(string openIdBuyer)
  - 设置 <c>open_id_buyer</c> 参数。

- MfaInnerUserUnifiedAuthenticationRequest XidBuyer(string xidBuyer)
  - 设置 <c>xid_buyer</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MfaInnerUserUnifiedAuthenticationResponse (class)

<c>jingdong.mfa.inner.userUnifiedAuthentication</c> 的响应。

- public SafeCResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MfaInnerValidateMsgCodeRequest (class)

<c>jingdong.mfa.inner.validateMsgCode</c> 的请求。

- JdRequest req;

- public MfaInnerValidateMsgCodeRequest()

- MfaInnerValidateMsgCodeRequest MsgCode(string msgCode)
  - 设置 <c>msgCode</c> 参数。

- MfaInnerValidateMsgCodeRequest RKey(string rKey)
  - 设置 <c>rKey</c> 参数。

- MfaInnerValidateMsgCodeRequest ValidateType(int validateType)
  - 设置 <c>validateType</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MfaInnerValidateMsgCodeResponse (class)

<c>jingdong.mfa.inner.validateMsgCode</c> 的响应。

- public SafeCResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。


## MfaUserUnifiedAuthenticationRequest (class)

<c>jingdong.mfa.userUnifiedAuthentication</c> 的请求。

- JdRequest req;

- public MfaUserUnifiedAuthenticationRequest()

- MfaUserUnifiedAuthenticationRequest ReturnUrl(string returnUrl)
  - 设置 <c>returnUrl</c> 参数。

- MfaUserUnifiedAuthenticationRequest DeviceOSType(string deviceOSType)
  - 设置 <c>deviceOSType</c> 参数。

- MfaUserUnifiedAuthenticationRequest AppId(string appId)
  - 设置 <c>appId</c> 参数。

- MfaUserUnifiedAuthenticationRequest BusinessType(int businessType)
  - 设置 <c>businessType</c> 参数。

- MfaUserUnifiedAuthenticationRequest Eid(string eid)
  - 设置 <c>eid</c> 参数。

- MfaUserUnifiedAuthenticationRequest OpenUDID(string openUDID)
  - 设置 <c>openUDID</c> 参数。

- MfaUserUnifiedAuthenticationRequest Source(string source)
  - 设置 <c>source</c> 参数。

- MfaUserUnifiedAuthenticationRequest DeviceName(string deviceName)
  - 设置 <c>deviceName</c> 参数。

- MfaUserUnifiedAuthenticationRequest Email(string email)
  - 设置 <c>email</c> 参数。

- MfaUserUnifiedAuthenticationRequest DeviceOSVersion(string deviceOSVersion)
  - 设置 <c>deviceOSVersion</c> 参数。

- MfaUserUnifiedAuthenticationRequest Pin(string pin)
  - 设置 <c>pin</c> 参数。

- MfaUserUnifiedAuthenticationRequest AppVersion(string appVersion)
  - 设置 <c>appVersion</c> 参数。

- MfaUserUnifiedAuthenticationRequest LoginChannel(string loginChannel)
  - 设置 <c>loginChannel</c> 参数。

- MfaUserUnifiedAuthenticationRequest AuthType(string authType)
  - 设置 <c>authType</c> 参数。

- MfaUserUnifiedAuthenticationRequest ClientIp(string clientIp)
  - 设置 <c>clientIp</c> 参数。

- MfaUserUnifiedAuthenticationRequest Uuid(string uuid)
  - 设置 <c>uuid</c> 参数。

- MfaUserUnifiedAuthenticationRequest Mobile(string mobile)
  - 设置 <c>mobile</c> 参数。

- MfaUserUnifiedAuthenticationRequest OpenIdBuyer(string openIdBuyer)
  - 设置 <c>open_id_buyer</c> 参数。

- MfaUserUnifiedAuthenticationRequest XidBuyer(string xidBuyer)
  - 设置 <c>xid_buyer</c> 参数。

- JdRequest Raw()
  - 底层协议请求。


## MfaUserUnifiedAuthenticationResponse (class)

<c>jingdong.mfa.userUnifiedAuthentication</c> 的响应。

- public SafeCResult returnType;

- public string Raw;
  - 用于诊断的完整 JOS 响应封装。
