# {{NAME}} — Zan 授权服务端

C 端成品软件的官方授权服务模板（从 server-mvc 复制裁剪而来）：发布后的
软件通过**激活码**或**账号密码**在本服务激活，支持按月/季/年/终身的有效期、
限制或不限制设备、限制并发在线终端数；管理后台发放授权、解绑设备、
封停授权。客户端侧的接入 SDK 是
[stdlib/System/Commercial/LicenseClient.zan](../../../stdlib/System/Commercial/LicenseClient.zan)
（约五行接入），配套示例见
[examples/licensed_app](../../../examples/licensed_app)。

框架本身不在模板里：路由/请求上下文/钩子/视图/会话/限流等都在标准库
`System.Web`；模板只保留授权业务自己的东西——配置、库表、授权流程、
管理界面。

## 授权模型

| 概念 | 落点 | 说明 |
|---|---|---|
| 产品 | `lic_product` | productId（如 `demo-app`）+ 产品密钥；未开放的产品拒绝一切授权 |
| 激活码 | `lic_code` | 生成时即算好的有效期档位（30/90/365/0=终身天）、可绑设备数；库里只存 SHA-256 散列 + 尾号，明文只在生成时展示一次 |
| 授权 | `lic_grant` | 一条授权 = 某产品上某主体（激活码首次激活生成，或后台按账号发放）的到期时间/设备数/座位数；`disabled` 即封停 |
| 会话 | `lic_session` | 一次在线占用（座位）：库里只存会话令牌散列；心跳刷新 last_seen，超过 5 分钟无心跳的座位不算占用 |
| 设备 | `lic_device` | 激活时绑定的设备指纹（客户端只上送 用户名@机器名 的 SHA-256 前 16 hex，原始主机信息不出机器） |

有效期：月/季/年/终身只是服务端签发时算好的 `expiresAt`（epoch 秒，
0=终身），客户端不区分档位。设备数 `maxDevices`、座位数 `maxSeats`
为 0 都表示不限制。同一设备重新激活**顶掉自己的旧座位**，不占新座。

## 客户端契约（/api/license/*）

请求与响应均为 JSON；除 logout 外都要求 `product`。业务码：`0000` 成功，
`2001` 产品不存在、`2002` 激活码无效、`2003` 账号或密码不正确（或无该产品
授权）、`2004` 已过期、`2005` 设备绑定已满、`2006` 在线座位已满、`2007`
授权已封停、`2008` 会话无效需重新激活。

```
POST /api/license/activate   {"product","code","device_fp"}
POST /api/license/login      {"product","account","password","device_fp"}
POST /api/license/heartbeat  {"product","device_fp","session"}
POST /api/license/logout     {"product","session"}
```

成功响应（封套统一，`data` 即 SDK 持久化进 license.json 的字段）：

```json
{"code":"0000","msg":"ok","data":{
  "mode":"code",              // code | account
  "subject":"ZPQ-…-K3SA",     // 激活码尾号或账号名
  "session":"LS-…",           // 会话令牌（心跳/登出凭据）
  "expires_at":1791345100,    // epoch 秒；0=终身
  "grace_seconds":86400,      // 掉线宽限秒（[license.grace.seconds] 可调）
  "heartbeat":300,            // 建议心跳秒（展示用）
  "now":1788753100
}}
```

失败响应 `{"code":"2002","msg":"激活码无效","data":null}`——SDK 把 `msg`
原样给宿主程序展示。改契约必须与 LicenseClient.zan 两边同步。

## 布局

```
config/app.json         运行时配置（端口/库/缓存/授权策略），不编译进二进制
src/main.zan            引导；路由来自控制器属性
src/Controller/Api/License.zan    四个授权端点（上面契约）
src/Controller/Admin/Lic/         管理后台：产品/激活码/账号授权/在线与设备
src/Controller/Account/Login.zan  管理后台登录（seed: admin/admin1234）
src/Feature/LicenseFlow.zan       授权核心域：激活/登录/心跳/登出/解绑
src/Model/Lic/                    lic_* 五张表实体
src/Framework/Schema.zan          建表 + 种子（示例产品、演示码、内置角色）
views/Admin/Lic/                  管理界面视图
```

内置角色：`admin`（全部）与 `operator`（授权运营，只见 /admin/lic/*，
发放激活码与账号授权、处理解绑）。首次启动用 admin/admin1234 登录。

## 演示种子

- 产品 `demo-app`（示例桌面软件）
- 激活码 `ZPQ-D1QJ-M8RR-K3SA`（30 天，限 1 台设备）
- 激活码 `ETJ-3FGW-HG62-UU1D`（终身）

生产部署前删掉演示码、修改 admin 密码、在 `config/app.json` 设置
`[auth].secret`（32+ 字符）。

## 构建与运行

```
zanc src/main.zan --auto-stdlib -o build/licensing_server.exe   # A90 在案时需显式附带 stdlib Orm/Linq 源文件
build/licensing_server.exe        # 从项目根运行，config/views/wwwroot 按相对路径解析
```

管理后台 `http://127.0.0.1:8096/admin`；客户端 SDK 指向
`http://127.0.0.1:8096` 即可。其余部署细节（worker 数、守护、SQLite/MySQL、
发布目录四件套）与 server-mvc 模板一致，见
[server-mvc README](../server-mvc/README.md)。

**探活**：`GET /health` 免鉴权答进程事实（uptime/requests/pid/worker），
`?deep=1` 追加 `SELECT 1` 探主库、不可达答 503——给负载均衡与进程监督用，
认证墙内没有可用性观测。指标采集与 server-mvc 同源（p95 直方图随
`metrics_minute` 落库）。
