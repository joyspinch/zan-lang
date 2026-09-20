# examples/licensed_app — 商业软件授权接入示例

演示 C 端成品软件接入官方授权体系的最小完整程序：启动校验 →
激活码/账号密码激活 → 心跳占座 → 登出释放。SDK 是
[packages/Zan.Commercial/src/System/Commercial/LicenseClient.zan](../../packages/Zan.Commercial/src/System/Commercial/LicenseClient.zan)，
服务端是 [templates/server/server-licensing](../../templates/server/server-licensing)
（授权服务端模板，`--publish` 后 `licensing_server.exe` 即可部署）。

## 运行

1. 起服务端（模板编译产物；种子产品 `demo-app`，演示激活码
   `ZPQ-D1QJ-M8RR-K3SA`（30 天）/ `ETJ-3FGW-HG62-UU1D`（终身），
   管理后台 `/admin`（admin/admin1234）可发放更多）。
2. 编译并运行本示例：

```
build/zanc.exe examples/licensed_app/licensed_app.zan --auto-stdlib -o licensed_app.exe
./licensed_app.exe                                   # 默认 http://127.0.0.1:8096 + demo-app
./licensed_app.exe http://lic.example.com demo-app   # 或指定 [服务端地址] [产品标识]
```

> `Environment.ArgAt(i)` 下标从第一个命令行参数起算（arg[0] 即第一个参数）。

按提示输入 `A` + 激活码（或 `L` + 账号密码）完成激活，进入演示主功能
（展示有效期 → 心跳 → 登出）。状态落在 `~/.zan-license/ZanSoft/demo-app/license.json`，
删除该文件即回到未激活。

## 接入要点（成品软件照抄即可）

- `LicenseClient.Shared()` 拿共享客户端，`Server()/Product()` 两行配置；
- 启动时 `await lic.Check()`：本地授权在宽限期内**直接放行**（离线可用），
  服务端只管签发与吊销；
- 激活码 `Activate(code)`、账号密码 `Login(account, password)`；
- 运行期 `await lic.Heartbeat()` 占在线座位（account/online 模式按 seat 限终端）；
- 授权结果永远是 `LicenseCheckResult`（`ok/reason/message`），**不抛异常**，
  宿主程序不会因授权探测崩溃；
- 本机指纹只上送用户名+机器名的 SHA-256 前 16 hex，原始主机信息不出机器。

## 契约

客户端↔服务端的四个端点（activate/login/heartbeat/logout）与响应封套
`{"code":"0000","msg","data":{mode,subject,session,expires_at,grace_seconds}}`
在服务端模板的 [README.md](../../templates/server/server-licensing/README.md#客户端契约apilicense)
有完整字段表；改契约必须两边同步。
