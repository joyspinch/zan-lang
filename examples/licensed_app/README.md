# examples/licensed_app — 商城授权 SDK 接入示例

入口是 [LicensedAppProgram.zan](LicensedAppProgram.zan)，实际使用
[Security.Commercial.LicenseClient](../../packages/Zan.Commercial/src/Security/Commercial/LicenseClient.zan)。
它连接已部署的商城授权接口，使用 SDK 的 `Activate` / `Login` / `Heartbeat`，
或通过 `InstallCertificate` 安装商城签发的离线证书，再以 `await Check()`
验证签名、产品、设备与有效期。所有异步调用均等待完成。

程序没有默认端点、产品、公钥、激活码或演示账号。缺少配置或校验失败时，
主功能不会启动。当前修改尚未编译运行；编译和真实商城互通验证应在协调后的构建窗口执行。

## 配置

通过启动进程的环境传入配置；凭据不要放进命令行、版本库或日志。

| 环境变量 | 用途 |
|---|---|
| `ZAN_LICENSE_PRODUCT` | 必填，商城返回的精确产品标识，例如 `your-app@major-1`。主版本为正整数，无前导零；升级主版本时必须明确更改。 |
| `ZAN_LICENSE_PUBLIC_KEY_PATH` | 必填，可信发行方的 RSA 公钥 PEM 文件，至少 2048 位。应由可信安装包或部署配置提供并保护其完整性。 |
| `ZAN_LICENSE_SERVER` | 在线激活/登录使用的商城根地址，例如隔离测试商城的 `http://127.0.0.1:<port>`；生产使用 HTTPS。离线证书模式可留空。 |
| `ZAN_LICENSE_ACTIVATION_CODE` | 可选，尚无有效本地状态时交给 `await Activate(code)`；优先于账号登录。 |
| `ZAN_LICENSE_ACCOUNT` / `ZAN_LICENSE_PASSWORD` | 可选，尚无有效状态且未提供激活码时交给 `await Login(account, password)`。 |
| `ZAN_LICENSE_CERTIFICATE_PATH` | 可选，商城离线授权响应中 `certificate` 字段的完整 JSON 文件。配置后先安装再校验；失败时退出。 |
| `ZAN_LICENSE_LOGOUT` | 可选，值为 `1` 时在线演示结束后调用 `await LogoutOnline()`，清除本地状态。默认保留状态供下次启动校验。 |

`Product` 使用 Windows 安全的 `<slug>@major-<N>`，例如 `your-app@major-1`，
须与商城签发结果完全一致。申请授权的商城账户接口使用 `major_version` 字段；
SDK 请求中的 `product` 已包含该主版本。不能将裸 slug、其他主版本或旧的冒号格式替代它。

公钥通过 `VendorPublicKeyPemBundle` 加载可信本地 PEM。可选 `ZAN_LICENSE_PUBLIC_KEYSET_PATH`
优先于原 `ZAN_LICENSE_PUBLIC_KEY_PATH`，内容为最多五把不同公钥的 PEM bundle；配置集合非法
即拒绝，不回落旧文件。重叠期放当前与前任公钥，撤销时移除前任。旧 `VendorPublicKey` API
仍可配置 singleton。公钥不能从同一份待验证证书或未经信任的 HTTP 响应中获取；
正式发行时也可把可信公钥随程序嵌入。安装证书成功仅表示已持久化，
**必须再等待 `Check()` 成功后才进入业务功能**。

## 编译与使用

在仓库根目录、获得协调构建窗口后编译，产物只放 `build/`。

```powershell
build/zanc.exe examples/licensed_app/LicensedAppProgram.zan --auto-stdlib --package-project . -o build/licensed_app.exe
```

在私有启动环境配置上述变量后运行 `build/licensed_app.exe`。示例不回显激活码、
密码、session、证书或原始错误响应。退出码 `0` 表示本次校验与主功能完成，
`1` 表示授权或操作失败，`2` 表示产品/公钥配置不完整。

在线路径：配置 Product、公钥、商城地址及真实激活码或账号；首次启动激活后，
显式执行实际 HTTP 心跳，再进入主功能。再次启动会加载已有状态。
SDK 的 `Check()` 可在本地宽限期内成功，因此需要证明网络互通时应显式调用 `Heartbeat()`。
`LogoutOnline().ok` 只表示 SDK 收到非空成功 HTTP 响应，不能单独证明服务端认可退出；
严格验收还应确认原 session 的后续心跳被拒绝。

离线路径：配置同一 Product、公钥和证书文件，商城地址留空。
程序调用 `InstallCertificate` 后本地验签，通过后直接进入主功能，不发在线心跳。
首次安装后可移除 `ZAN_LICENSE_CERTIFICATE_PATH`，下次启动仍会校验持久化证书。
设备绑定证书必须在绑定的 SDK 指纹设备上使用；便携证书是否允许由商城签发策略决定。

状态由 SDK 保存到用户主目录的
`.zan-license/LicensedAppExample/<slug>@major-<N>/license.json`。
独立验收必须给子进程设置私有 `USERPROFILE`（Windows）/ `HOME`（POSIX），
以隔离真实用户状态。商城的 `tests/sdk_client_contract.zan` 与 `tests/e2e.ps1`
覆盖真实 SDK HTTP、跨进程状态重载、错误主版本/设备、离线签名与篡改拒绝。
