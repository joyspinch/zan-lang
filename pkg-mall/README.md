# pkg-mall — Zan 扩展与程序商城

Zan 语言生态的**扩展（extension / 库）与程序（app / 成品）**一站式分发平台，
基于 `templates/server/server-mvc`（ZanWeb MVC）开发。默认对接生产地址
**https://ide.zanlang.top**（IDE 侧 `ZAN_MALL_URL` 环境变量可覆盖，本机开发时
指向 `http://127.0.0.1:8090`）。

## 两类商品

| 类型 | 受众 | 交付物 | 渠道 |
|---|---|---|---|
| `extension`（库） | 程序员 | stdlib 布局包（`.zpkg`） | IDE 内商城：浏览、安全安装、缺库自动定位并重试 |
| `app`（成品） | C 端用户 | 可执行文件 + 许可证 | Web 商城：购买后签发许可证，授权 SDK 校验 |

官方免费扩展示例：`System.Scripting.Lua 1.0.0`（seed 内置，once、0 积分、approved）。

## Web 页面（面向 C 端用户与开发者）

| 路径 | 说明 |
|---|---|
| `/` | 商城首页：扩展与程序双目录（公开） |
| `/mall/product?id=N` | 商品详情：方案/版本/签名状态，购买表单 |
| `/login` `/register` `/logout` | 认证（与 API 同一 cookie 机制） |
| `/account` | 账户中心：积分、卡密兑换、我的授权、许可证签发、下载 |
| `/account/developer` | 开发者工作台：申请、提交商品/版本/方案、提现 |
| `/admin` | 管理后台：四类审核队列、卡密生成、频道发布/回滚/灰度 |

浏览器页面未登录访问受保护路径时 302 到 `/login?next=…`（API 同闸答 401）。
POST 动作失败重渲同页面，理由显示在用户正看的页面上。

## 运行

```bash
cd pkg-mall
zanc src/main.zan src/Controller/*.zan src/Controller/Api/*.zan \
     src/Controller/Index/*.zan src/Framework/*.zan src/Feature/*.zan \
     src/Dao/Mall/*.zan src/Model/Mall/*.zan src/Model/Sys/SiteSetting.zan \
     --auto-stdlib -o mall.exe
./mall.exe            # 默认 127.0.0.1:8090，SQLite（data/mall.db）+ WAL
```

- 首次启动 CodeFirst 建表并种子：管理员 `admin / admin1234`（部署后立即改密）、
  3 个分类、官方免费扩展、4 条版本频道初始行。
- 数据库：默认 SQLite；改 `config/app.json` 的 `[database].driver` 即切 MySQL/PostgreSQL。
- 包签名：`ZAN_MALL_SIGNING_KEY_FILE` 指向 PEM；私钥签发、公钥进 IDE 验签。
- 包体存放：`wwwroot/packages/*.zpkg`（artifact_url 记 `packages/…`，
  下载经授权闸后 302 到 `/static/packages/…` 由既有静态挂载服务；
  静态资源启动时扫描缓存，**先放包再启动/重启**）。

## API 一览

### 商城（`/api/mall/*`）
`catalog` 目录 / `product` 详情 / `redeem` 兑换卡密 / `buy` 购买（原子事务）/
`entitlements` 我的授权 / `download` 下载（授权闸）/
`license_issue` / `license_check` 许可证 / `review` 评价。

### 认证（`/api/auth/*`）
`register` / `login`（AES-GCM cookie，30 天）/ `me`。角色 `user / developer / admin`。

### 开发者（`/api/developer/*`）
`apply` 申请 / `product` 提交商品（含 `product_type`）/ `version` 提交版本
（含 `artifact_sha256` + `artifact_signature`）/ `plan` 价格方案 / `withdraw` 提现。

### 管理（`/api/malladmin/*`、`/api/channeladmin/*`，需 admin）
`codes` 生成卡密（明文只出现一次）/ `developer|product|version|withdrawal` 四类审核 /
`entitlement` 停用授权；频道 `publish`（含灰度与安全下限）/ `rollback` 回滚。

### 版本频道（`/api/channel/*`，公开）
- `GET /api/channel/check?channel=ide|stdlib|compiler|sdk&platform=win-x64&current=0.2.4&client_id=<GUID>`
- `GET /api/channel/list`

## 版本同步更新机制（设计）

服务端把工具链拆成四条**发布线**（`version_channels` 表）：`ide` / `stdlib` /
`compiler` / `sdk`，各自独立演进、互不绑定。每条线一行"当前指针" +
一组"历史发布"（`version_releases`）。

```
客户端(IDE 启动)
  └─ GET /api/channel/check?channel=ide&platform=win-x64&current=<本地>&client_id=<GUID>
       ├─ 灰度：rollout_percent<100 时按 client_id FNV 散列落桶 0-99，
       │   桶值 < rollout 才算"灰度内"；灰度外客户端拿到旧版本
       ├─ update_available = 灰度内 && latest 比本地新
       ├─ update_required = 本地 < required_version（安全下限，管理员可强制）
       └─ 响应携带 installer_url + sha256 + signature（RSA-SHA256，
           内容 = channel + "\\n" + version + "\\n" + sha256）供下载与验签
```

- **发布**：`POST /api/channeladmin/publish` 写历史发布行并推进频道指针，
  `rollout_percent` 从小到大逐级放量。
- **回滚**：`POST /api/channeladmin/rollback?version=<old>` 把指针挪回历史行——
  客户端下一次 check 自然回到旧版本，无需重新分发。
- **IDE 侧**（`src/ide_zan/src/services/IdeUpdate.zan`）：启动时后台线程依次检查
  `ide / stdlib / compiler` 三条线（`ZAN_MALL_URL` 覆盖默认地址，HTTPS-only 策略），
  窗口标题显示 `[update available]` / `[!] update required` 徽标。
- **包体校验与商城商品同一套**：SHA-256 摘要 + RSA-SHA256 签名，防篡改/防伪包。

## 增量更新（文件级 md5 差异同步）

版本频道的包体不必整包下载——发布按**文件清单**走增量：

```
发布（管理员）:
  1. 把新版文件树放进 packages/<channel>[-<platform>]/（如 packages/ide-win-x64/）
  2. POST /api/channeladmin/publish  → 频道指针指向新版本
  3. POST /api/manifestadmin/publish → 扫描目录生成清单快照
     （{path, md5, size} 逐文件，写入 version_releases.manifest 冻结）

客户端同步:
  1. POST /api/channel/sync  body: {"channel","platform","version",
       "client": {"path": "本地md5", ...}}     （不带 client = 全量）
     → {"match": 一致数, "download": [{path, md5, size}...]}
  2. 对 download 里每个文件 GET /api/channel/file?...&path=...
     （响应 ETag = 内容 md5，可自行复验；md5 一致的文件根本不会进
       download 列表——改动多少传多少）
  3. 本地有而清单没有的文件（服务端已移除）客户端对照 download 全集
     自行删除
```

IDE 侧（`IdeUpdate.BeginSync`）把本地安装目录的平行 `path/md5` 清单
发给 sync，得到 `IdeSyncPlan`（要下载哪些文件、合计字节），状态面据此
展示"这次更新要下多少"。真正的下载落盘由安装管线消费该计划。

## 数据层约定

- **Database First 与 CodeFirst 混合**：实体用 `[Table]` 声明，启动时
  `SyncStructureAllAsync` 建表（幂等）；种子带计数保护。
- 17 张表：`users / developer_profiles / categories / products / product_versions /
  pricing_plans / orders / entitlements / point_ledger / developer_ledger /
  recharge_codes / reviews / withdrawals / licenses / version_channels /
  version_releases`。
- 分层边界：Repository（Dao）执行参数化 SQL、Service（Feature）只做业务流程
  不拼 SQL、Controller 只做请求响应。事务用 `[Tx]` 蹦床（`Redeem/Buy/Review` 等）。

详细需求与验收标准见 [REQUIREMENTS.md](REQUIREMENTS.md)。
