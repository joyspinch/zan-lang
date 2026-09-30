# System.Commercial

> 源码: `packages/Zan.Commercial/src/System/Commercial/LicenseClient.zan`


## LicenseCheckResult (class)

授权校验结果（在线模式）。

- public bool ok;
  - true = 已授权可用。

- public string reason;
  - 不可用的原因（供应用显示）：no_license / expired / device_mismatch
    / seat_limit / network_fail / server_reject / bad_state。

- public int graceSeconds;
  - 服务端下发的续期窗口（秒）——该窗口内离线仍可用（online 模式）。

- public long expiresAt;
  - 授权到期（epoch 秒；0 = 不限/终身）。

- public string message;
  - 可读一句话（ok=false 时的建议动作）。

- static LicenseCheckResult Fail(string why, string msg)


## LicenseClient (class)

Zan 商业软件授权客户端（官方基础模板，C 端成品软件接
入用）。与授权服务端模板（templates/server/server-licensing）配对：

授权模式 licenseMode（服务端签发决定，客户端只认结果）：
account  —— 账号密码登录授权（按用户，不限设备或按 seat 数限终端）
code     —— 激活码激活（按码授权，可绑设备）
signed   —— 离线证书（RSA 验签，发布前签好，全程无网可用）
online   —— 在线会话：登录/激活后由服务端发放会话令牌，
客户端静默心跳续期；掉线进入宽限期，超期回落

有效期 period：服务端在授权里下发 expiresAt（epoch 秒，0=终身）；
月/季/年只是服务端签发时算好的到期时间，客户端不区分档位。

终端数：account/online 模式服务端按 seatLimit 管理并发在线座位
（心跳即座位占用；超限拒绝并提示）；code 模式可绑定 deviceFp。

用法（约五行接入）：
LicenseClient lic = LicenseClient.Shared();
lic.Server("http://127.0.0.1:8095", "ZanSoft");
lic.Product("my-app");
LicenseCheckResult r = await lic.Check();
if (!r.ok) { Console.WriteLine(r.message); return; }

状态持久化在用户目录下（默认 ~/.zan-license/<product>/license.json），
剥离即回到未激活。SDK 从不通过网络传输任何明文密码——账号模式
只上送服务端校验后的会话令牌。所有网络失败都返回结果对象而非
抛异常，宿主程序绝不因授权探测崩溃。

- string serverUrl;
  - 授权服务端地址（http(s)://host[:port]）。

- string vendor;
  - 厂商名（状态目录隔离 + 展示用）。

- string product;
  - 产品标识（授权服务端按此签发）。

- LicenseState state;
  - 已加载的持久状态（null = 未加载）。

- static LicenseClient shared;

- static LicenseClient Shared()
  - 进程级共享客户端（绝大多数成品软件只用一个授权）。

- void Server(string url, string vendorName)
  - 授权服务端地址与厂商名（须与授权服务端部署一致）。

- void Product(string productId)
  - 产品标识。

- string DeviceFp()
  - 本机指纹：用户名 + 机器名的 SHA-256 前 16 hex。服务端只见这个
    稳定哈希，拿不到原始主机信息；重装系统/换用户名才会变。

- public string Fingerprint()
  - 本机指纹（宿主程序展示"绑定设备"信息用）。

- public string StateFile()
  - 本地授权状态文件路径（复位说明/诊断展示用）。

- string StateDir()
  - 状态文件目录：~/.zan-license/<vendor>/<product>/（可整体删除复位）。

- string StatePath()

- void EnsureLoaded()
  - 载入持久状态；文件缺失/损坏返回空状态（调用方读 .ok 判断）。

- void Persist()
  - 把当前状态写回磁盘；失败静默（最坏情况是重新激活一次）。

- void Forget()
  - 清除本地状态（登出/注销用）。

- async LicenseCheckResult Activate(string code)
  - 激活码激活。成功后本地持有签发的会话，之后 Check()
    走 online 心跳；服务端拒绝（错码/用尽/终端超限）返回原因。

- async LicenseCheckResult Login(string account, string password)
  - 账号密码登录授权。服务端校验密码（本方法不传原密码——
    改用 /api/license/login 在服务端换会话），按账号可用授权签发。
    本客户端仅上送账号名+密码给服务端专用端点，服务端返回会话。

- async LicenseCheckResult Check()
  - 总校验入口：启动时与需要时调用。本地有效即放行
    （online 模式带最后心跳+宽限期），否则联网复检。绝不抛异常。

- async LicenseCheckResult Heartbeat()
  - 立即向服务端心跳一次（在线座位占用）。返回服务端结论。

- void Logout()
  - 登出并清除本地状态（account/online 模式用）。释放座位的
    通知是尽力而为：Zan 的 HTTP 客户端是 async 通道，同步方法里无法
    等待其完成——通知失败只是座位等心跳过期，不影响本地清理。

- LicenseCheckResult OkResult(long now)

- async LicenseCheckResult Heartbeat(bool quiet)
  - 会话心跳：带 session 调 /api/license/heartbeat，服务端校验座位
    与有效期并回发续期窗口；返回的 JSON 即服务端模板的响应契约。

- async LicenseCheckResult PostActivate(string code, string unused)

- async LicenseCheckResult PostLogin(string account, string password)

- async string PostJson(string path, string json)
  - POST JSON 的公共通道：设置 Content-Type、超时预算，返回响应
    正文；连接失败/非 2xx 返回空串（调用方按 network_fail 处理）。

- async LicenseCheckResult LogoutOnline()
  - 在线登出：通知服务端释放座位后清本地状态。UI 侧有
    协程上下文时用这个；控制台一次性脚本用同步 Logout() 即可。


## LicenseState (class)

本地持久化的授权状态（license.json 的字段）。字符串字段
构造时初始化为 ""——Zan 的引用类型字段默认是 null，长度探测即
运行时错误，绝不能让授权状态带着 null 出门。

- public LicenseState()

- public string mode;
  - account / code / signed / online（登录/激活时服务端下发）。

- public string product;
  - 产品标识（防状态文件拷给别的产品）。

- public string subject;
  - 授权主体（账号名或激活码尾号，展示用）。

- public string deviceFp;
  - 激活时绑定的本机指纹（重绑由服务端解绑）。

- public string sessionToken;
  - 会话令牌（服务端可吊销；本地只存，不签发）。

- public long expiresAt;
  - 授权到期 epoch 秒；0 = 终身。

- public int graceSeconds;
  - 掉线宽限秒数（服务端策略下发）。

- public long lastHeartbeat;
  - 最后一次成功心跳的 epoch 秒。
