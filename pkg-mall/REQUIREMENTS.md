# pkg-mall 需求文档

> 本文档是从历史会话上下文重建的**需求依据**，是代码实现的唯一需求来源。
> 需求与界面描述分离：本文档只描述"要做什么、验收标准是什么"。
> 状态标记：`[完成]` 已实现并验证；`[部分]` 已实现未闭环；`[未开始]` 未实现。

## 1. 项目定位

Zan 语言生态的**扩展与程序商城**（package mall）：为 Zan 语言提供**扩展（库）**与**程序（应用）**两大类商品的一站式分发平台。两类商品**受众不同、展示渠道不同**：

- **扩展（extension / 库）——面向程序员**：
  - 主要展示渠道是 **IDE 内商城**：程序员在 IDE 中浏览、安装扩展，编译器缺库时自动定位并重试。
  - 按 `stdlib/` 布局打包（官方示例 `System.Scripting.Python`），走编译器安全安装接口。
- **程序（app / 成品）——面向 C 端用户**：
  - 交付物为**可执行文件 + 许可证**，不安装进项目 stdlib。
  - 购买后签发许可证，第三方程序通过授权 SDK 校验（online/device_offline/account_offline）。
  - 展示/购买以面向 C 端用户的渠道为主（Web 商城页面/IDE 内浏览），IDE 侧重于展示与引导。

- 开发者注册并提交商品、版本与价格方案；
- 管理员审核开发者/商品/版本/提现；
- 用户用积分购买或兑换卡密获得授权与许可证；
- IDE 内置商城：**以扩展（面向程序员）为主**，浏览、安装确认、构建缺库自动定位并重试；程序（成品）在 IDE 中可浏览展示、购买与下载引导；
- 包下载经过 SHA-256 摘要 + RSA-SHA256 签名双重校验，安装走编译器安全接口。

> 注意：`System.Scripting.Python` 仅是"扩展"类商品的官方免费示例种子，商城本身面向所有 Zan 扩展与程序，不是 Python 商城。

> 注意：`System.Scripting.Python` 仅是"扩展"类商品的官方免费示例种子，商城本身面向所有 Zan 扩展与程序，不是 Python 商城。

## 2. 技术栈与架构约束

1. 基于 `templates/server/server-mvc` 模板（ZanWeb 分层 Web 应用），目录：`controller / model / framework`。
2. 外部配置 `config/app.json`（server/database/worker/log/cache），启动时解析为类型化 `Cfg` 实体，不编译进二进制。
3. 认证：**非 JWT 单段 AES-GCM 加密 Cookie**（`zsession`），内容 opaque（version+uid+issue_time+nonce+password_hash_prefix），**改密后旧 token 失效**；Attribute 驱动（None/Login/ApiAuth），action 前统一 401/403 并注入 `uid`；服务端密钥来自环境变量 `ZAN_AUTH_SECRET`。
4. 数据库：**Database First** —— `db/schema.sql` 是唯一真源，启动时 `Migration.RunAll` 应用、`Model.FromTable` 反射生成模型，应用内不携带手写重复 Schema。
5. 数据访问边界（用户明确要求）：
   - `db/schema.sql`：完整数据库结构；`db/migrations/*.sql`：旧库升级；
   - Repository：执行参数化 SQL、映射实体；Model：实体与领域状态；Service/Workflow：业务流程，**不拼 SQL**；Controller：请求与响应。
6. 集合建模（全局约束）：多字段业务记录必须 `List<实体>`；禁止平行 `List` 或分隔符/JSON 字符串压缩；`List<string>` 仅用于日志、路径、名称、命令参数等一维字符串。
7. 驱动：标准库**原生同步 MySQL 驱动**（无需 ODBC），支持 `CLIENT_FOUND_ROWS`（UPDATE 返回匹配行数而非变更行数）；SQLite 亦可运行。

## 3. 功能需求

### 3.1 身份与账号（Auth）`[完成]`

| 接口 | 说明 | 约束 |
|---|---|---|
| `POST api/auth/register` | 注册 | username 3-64；password 8-128；重名拒绝；PBKDF2 风格 12000 轮迭代哈希 + 随机 salt |
| `POST api/auth/login` | 登录 | 校验密码与 `status='active'`；签发 AES-GCM cookie（30 天）；失败 401 |
| `GET api/auth/me` | 当前用户 | 需 Login；返回 id/username/role |

- 角色：`user` / `developer` / `admin`；开发者审核通过后 role 升为 `developer`。

### 3.2 开发者与商品（Developer + MallAdmin）`[完成]`

| 接口 | 说明 | 约束 |
|---|---|---|
| `POST api/developer/apply` | 申请开发者 | 需 Login；display_name 2-128、bio；status=pending；不可重复申请 |
| `POST api/developer/product` | 提交商品 | 需 ApiAuth+开发者；category_id/slug(≥3)/name(≥2)；status=pending |
| `POST api/developer/version` | 提交版本 | 需归属商品；version、major_version≥1、artifact_url 必填；status=pending |
| `POST api/developer/plan` | 添加价格方案 | license_mode ∈ {once, periodic}；price_points≥0；periodic 需 duration_days≥1；佣金默认 10% |
| `POST api/developer/withdraw` | 申请提现 | 余额=Σ(developer_ledger)−Σ(pending 提现)；amount≥1、account≥3 |
| `POST api/malladmin/codes` | 生成卡密 | count 1-100、points≥1；返回明文（仅此一次），库中只存 SHA-256 digest |
| `POST api/malladmin/developer` | 审核开发者 | pending→approved（升 role）/rejected，记 review_note |
| `POST api/malladmin/product` | 审核商品 | pending→approved/rejected |
| `POST api/malladmin/version` | 审核版本 | pending→approved/rejected，记 review_note；**批准时无私钥/签名失败则拒绝**（见 §3.6） |
| `POST api/malladmin/withdrawal` | 审核提现 | 批准后 developer_ledger 记 `withdrawal` 负 delta |
| `POST api/malladmin/entitlement` | 停用授权 | entitlements.disabled 置位 |

### 3.3 商城交易（Mall）`[完成]`

| 接口 | 说明 | 约束 |
|---|---|---|
| `GET api/mall/catalog` | 商品目录 | 仅 approved；含分类与 MIN 起售价 |
| `GET api/mall/product` | 商品详情 | approved 商品 + enabled 方案 + approved 版本（含 artifact_url） |
| `POST api/mall/redeem` | 兑换卡密 | 事务；`FOR UPDATE` + `redeemed_by IS NULL` 防重；积分入账 + point_ledger(recharge) |
| `POST api/mall/buy` | 购买 | **原子事务**：锁定用户与方案；余额≥price；once 模式同产品同 major_version 不重复；扣款+订单(paid)+授权+point_ledger(purchase)+开发者佣金；periodic 的 expires_at=now+duration_days |
| `GET api/mall/entitlements` | 我的授权 | 含 license_mode/major_version/expires_at/disabled |
| `GET api/mall/download` | 下载 | 需授权（once: major 匹配；periodic: 未过期）且版本 approved；返回 artifact_url |
| `POST api/mall/license_issue` | 签发许可证 | mode ∈ {online, device_offline, account_offline}；device_offline 需 device_id；token 随机，库中只存 digest；periodic 授权不超 entitlement 到期 |
| `POST api/mall/license_check` | 检查许可证 | token_digest + revoked=0 + expires_at>now；可选 device_id |
| `POST api/mall/review` | 评价 | 需已购买；rating 1-5；content ≤2000；(user, product) 唯一 |

### 3.4 数据表（`db/schema.sql`，16 表）`[完成]`

`users` / `developer_profiles` / `categories` / `products` / `product_versions` /
`pricing_plans` / `orders` / `entitlements` / `point_ledger` / `developer_ledger` /
`recharge_codes` / `reviews` / `withdrawals` / `licenses`（+ `_migrations`）。

种子数据：3 个分类（system/scripting/developer-tools）；官方免费包
`System.Scripting.Python 1.0.0`（once、0 积分、approved）。

### 3.5 编译器缺库诊断与安全安装（Task 7）`[完成]`

- `ZANPKG_MISSING` **不误报工程内命名空间**：扫描 `zan.proj` 项目树声明，而非仅输入文件。
- 包搜索路径闭环：IDE 安装的包可被编译器识别。
- 安全安装接口 `zan_pkg_install_local`：拒绝无版本 manifest；强制 `stdlib/` 布局；
  输出 `ZANPKG_STATUS action=install ...` 供上层解析。

### 3.6 包摘要/签名校验（Task 9）`[部分]`

安全模型（RSA-SHA256）：

- `product_versions` 增加 `artifact_sha256`（64 位十六进制）与 `artifact_signature`（Base64 RSA-SHA256）。
- 签名内容固定为：`packageName + "\n" + version + "\n" + sha256`。
- 服务端审核批准时：**必须有私钥且签名成功**，否则拒绝批准——不允许存在"已批准但不可验证"的版本；私钥路径由环境变量 `ZAN_MALL_SIGNING_KEY_FILE` 指定；摘要必须是 64 位十六进制。
- 数据库升级采用**独立幂等迁移**（不动初始建表，旧库加列、新库不重复加列）。
- IDE 侧验证流程（`MarketplaceClient` 后台执行）：
  1. 下载包到临时文件；
  2. 比对 SHA-256 摘要；
  3. 用内置受信公钥（`RsaKey.FromPublicPem` + `VerifySha256`）验签；
  4. 校验压缩包内路径（防目录穿越）；
  5. 解压到 staging 目录；
  6. 调用编译器安全安装接口；
  7. 成功后触发**一次性构建重试**（不无限循环）。

### 3.7 IDE 商城（Task 8）`[部分]`

**IDE 商城以扩展（面向程序员）为主**：

- `MarketplaceClient.zan`：后台任务（目录/商品详情/包下载），`Thread.Start` + `SseSink` 线程安全回传；实体用 `MarketplacePackage/MarketplaceVersion/MarketplacePlan`（`List<实体>`）。
- `MarketplaceView`/`MarketplaceWindow`：目录展示、详情、**免费扩展安装前确认对话框**。
- 构建失败出现 `ZANPKG_MISSING` → 打开商城定位对应扩展 → 安装成功后自动重试原构建一次。
- **程序（成品）在 IDE 中侧重展示与引导**：可浏览商品与详情，安装/运行入口引导到购买与下载（面向 C 端用户的渠道）。
- 服务端 API 扩展：商品详情返回 `artifact_url` 与 `product_type` 供下载与类型区分。

### 3.8 授权 SDK 与商品发布（Task 12）`[未开始]`

商城商品分两类，**流程完全分开、受众不同**：

**A. 扩展（extension / 库）——面向程序员，IDE 中展示安装**
- 按 `stdlib/` 布局打包（`System.Scripting.Python` 为官方免费示例，seed 已就绪：1.0.0、免费 once、approved）。
- 交付方式：IDE 安全安装进项目（走编译器安全安装接口），构建缺库时自动定位并重试。
- 价格方案默认免费（price_points=0）也可付费。

**B. 程序（app / 成品）——面向 C 端用户**
- 完整可执行的 Zan 程序/工具，交付物为**可执行文件 + 许可证**，不安装进项目 stdlib。
- 购买后签发许可证；第三方程序通过**授权 SDK** 校验（online/device_offline/account_offline）。
- 下载流程：授权校验通过后返回可执行文件下载地址（`artifact_url` 指向 `.exe`/可执行包）。
- 展示/购买以面向 C 端用户的渠道为主；IDE 可浏览展示与购买引导。

**模型要求**：`products` 增加类型字段（`product_type`：`extension` / `app`），发布、目录、详情、价格方案、下载、许可证校验按类型区分；目录/详情返回类型供 IDE 区分"安装"（扩展）与"下载运行"（程序）。

### 3.9 Python 自然调用示例及端到端测试（Task 13）`[未开始]`

- `examples/python/`：Python 自然调用示例（README + python_embed.zan）——作为 Zan 扩展能力的演示。
- 商城端到端测试：注册→登录→/me→目录→购买→授权→许可证→下载→评价；覆盖**扩展类**与**程序类**商品各一例。

## 4. 验收标准

- 迁移幂等：重启不重复应用，旧库升级后列存在。
- 全 API 流程走通且事务原子（购买/兑换/提现审核）。
- 认证：action 前统一 401/403、注入 uid；改密后旧 cookie 失效。
- 编译器：工程内命名空间不误报缺库；安装包可被识别；无版本/无 stdlib 布局拒绝安装。
- 商城商品类型：扩展（库）与程序（应用）两类流程分开、受众不同（扩展→程序员/IDE 安装；程序→C 端用户/可执行文件+许可证），均能发布、审核、购买、授权、下载。
- 扩展类：IDE 安全安装进项目、缺库自动定位并重试；程序类：交付可执行文件 + 许可证、授权 SDK 校验。
- 签名：篡改包体/摘要/签名任一环节均安装失败；服务器不允许批准未签名版本。
- IDE：商城窗口以扩展为主（安装确认、缺库定位、安装后一次性重试），程序类可浏览展示与购买引导。
- 编译/ORM 回归无副作用（不跑全量测试，跑受影响子集）。

## 5. 非目标（明确不做）

- 真实货币支付网关（以积分模拟交易）。
- 邮件/短信/站内通知。
- 多语言 i18n。
