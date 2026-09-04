# {{NAME}} — Zan 企业协同平台

多租户、模块化单体的企业协同平台模板，构建在 Zan 的协程运行时与 ZanWeb
MVC 框架之上。**零 Redis、零外部服务依赖**：默认 SQLite（WAL）+ 进程内
缓存即可完整运行，切 MySQL/PostgreSQL 与 Redis 只改 `config/app.json`。

功能面（管线 T1~T16 交付）：

| 领域 | 能力 |
|---|---|
| 平台基座（T1） | 多租户隔离（全业务表 tenant_id 过滤、跨租户读写 404）、RBAC 权限位（View/Create/Update/Delete/Export）、部门数据范围、登录日志/操作日志、站点设置 |
| 系统管理 | 用户 / 角色 / 部门 / 数据字典（含字典联动表单）、站点配置、注册与找回密码 |
| 通知中心（T2/T3） | sys_notify 单一事实源、WS 优先 + SSE 回落事件流、铃铛未读数、免打扰偏好 |
| 附件（T4） | 流式上传落盘（64KB 分块）、租户闸下载/删除、按模块挂载 |
| 待办与工作台（T5） | Flow 任务 / CRM 到期 / 日程聚合待办，一键跳转 |
| 导入导出（T6/T7） | CSV/XLSX 导出、CSV 导入（分批事务 + 逐行错误报告）、各业务模块端点 |
| 全文搜索（T8） | 顶栏聚合搜索，逐源 MaskOf 权限闸（无权限源零泄漏）、租户内检索 |
| 文档中心（T9/T10） | 空间-树形文档、11 种块类型渲染、块编辑器（2s 防抖自动保存、乐观锁 409）、版本历史、XSS 白名单 |
| 日历（T11） | 月/周视图、到期触发通知 + 待办各一条（多 worker 原子抢占防重发） |
| 站内信 + 公告（T12） | 私信（会话 + 已读回执）、全员公告 + 登录弹窗一次 |
| IM 实时（T13） | 统一事件流（ChatHub + MessageRelay 水位扫描）、跨 worker 1s 拍内送达、离线上线补拉 |
| 表单引擎（T14/T15） | 8 种字段 schema 定义 + 预览、字典联动（`dict:`）、服务端校验白名单、填报自动发起流程、审批状态推导回显、CSV 导出 |
| 备份恢复（T16） | 管理页一键备份（VACUUM INTO 快照 + uploads.zip + manifest）、保留 7 份轮转、跨 worker 锁、zip 下载 |

框架本身（路由、请求上下文、钩子、视图、验证、会话、限流、请求锁、
服务器引导）在标准库 `System.Web`（`WebApp`、`Router`、`HttpContext`、
`Controller`、`View`、`WebServer`、`ApiDocs`…）。模板只保留属于本应用的
部分：配置、DB、缓存、各域控制器、模型与视图。

## Layout

```
config/app.json         runtime config (host/port/limits/db/cache) — NOT compiled in
src/main.zan            bootstrap only — routes come from controller attributes
src/Controller/         平台域控制器
  Account/                Login / Register（登录域，Bypass）
  Admin/Dashboard.zan     工作台（聚合待办 + 公告卡片）
  Admin/Profile.zan       个人中心
  Admin/System/           Users / Roles / Departments / Dicts / Logs /
                          SiteConfig / Backups / Docs（接口文档页）
  Admin/Dev/Assistant.zan AI 助手（[Ai] 白名单策略）
  Api/                    Auth（令牌登录）、Data、Index
src/Modules/            业务域（模型 + 控制器同目录内聚）
  Oa/                     通知、附件、待办、日历、文档中心、块编辑器、
                          站内信、公告、表单引擎 + 填报、备份页、全局搜索页
  Crm/                    客户 / 联系人 / 商机
  Wms/                    商品 / 仓库 / 库存 / 入库
  Ecbi/                   电商 BI：店铺 / 商品 / 广告 / 报表 / 平台
  Flow/                   流程定义 / 实例 / 任务 + 引擎（Engine.zan）+ 通知兼容层
src/Feature/            横切能力：Attachment、TodoCenter、ExcelIo、Blocks、
                        FormSchema、Backup、Search、CalendarRemind、
                        MessageRelay、Mailer、Metrics、Ai…
src/Framework/          应用接线：Cfg、Db、DbContext、Schema（CodeFirst DDL +
                        种子）、Auth、Tenant、Notify(Hub)、DataScope、Perm(Table)、
                        ExcelIo、Search、MessageRelay、AppServices
src/Model/、src/Dao/    sys_* 平台实体与查询
views/                  templates, in the module structure of the controllers
  layout.html             the site-wide page wrapper (global {{content}} layout)
  <Module>/*.html         that module's views; a module's own layout.html
                          overrides the global one for that module only
wwwroot/                the ONLY web-reachable directory, served at /static
docs/deploy-collab.md   部署、备份恢复演练、升级指引
```

`views/` and `wwwroot/` sit next to `src/`, not inside it, because both are read
at run time: a release is a copy of the executable plus `config/`, `views/` and
`wwwroot/` — `src/` is a build input and never ships. The view keys derive from
the directory path (`View.LoadRec`), so `views/Admin/Oa/Docs.Index.html` is
`Admin.Oa.Docs.Index`, matching the controller namespaces.

## Attribute-driven routes

Controllers declare routing with **attributes** instead of hand-wiring in
`main.zan` (the Zan equivalent of the PHP `@title/@auth/@rank` docblocks). A
compiler pass scans the controllers and synthesizes `__AttrRoutes.Register(app)`,
which `main.zan` calls once — no generated file to maintain, no reflection.

```zan
class ApiController {
    [Post]                 // HTTP verb: [Get] [Post] [Put] [Delete] [Patch]
    [Title("收集数据")]     // human label (admin menu / docs)
    [Auth]                 // permission check required (implies login)
    [Rank(3)]              // minimum principal rank
    [Lock("user")]         // per-user request lock (double-submit guard)
    static void Gather(HttpContext ctx) { ... }   // -> POST /api/gather
}
```

| Attribute | Effect |
|---|---|
| `[Get] [Post] [Put] [Delete] [Patch]` | HTTP verb (required to mark a method as a route) |
| `[Route("/custom")]` | override the convention path |
| `[Title("...")]` | label for the admin menu / docs |
| `[Login]` | a logged-in principal is required (401 otherwise) |
| `[Auth]` | RBAC permission check (implies `[Login]`, 403 on failure) |
| `[Rank(n)]` | minimum principal rank |
| `[Lock("user"\|"global")]` | request lock; second concurrent call gets 429 |
| `[Limit(n)]` | per-route rate limit (requests/sec) |
| `[Upload]` | stream the request body to disk |
| `[Menu]` | surface this route in the admin menu (`/admin/menu`) |

**Shorthand: one combined `[Api(...)]` attribute.** Instead of stacking many
lines you can declare everything in one, mixing bare flags and `key=value`:

```zan
[Api(post, route="/api/gather", title="收集数据", auth, rank=3, lock="user")]
static void Gather(HttpContext ctx) { ... }
```

Keys: `get/post/put/delete/patch` (or `method="POST"`), `route="/x"`,
`title="…"`, `login`, `auth`, `rank=n`, `lock="user"|"global"`, `limit=n`,
`upload`, `menu` — booleans may be bare (`menu`) or explicit (`menu=true`). The
single attributes above still work and can be mixed with `[Api(...)]`.

**Routes follow the directory structure.** The path is
`<folders under src/controller> + controller name (minus "Controller") + action`,
lower-cased — `ApiController.Status` → `/api/status`, `Admin/UserController.List`
→ `/admin/user/list`. `Index` maps to the folder root. `[Route("...")]` overrides
the convention when you need an exception.

Custom rank/RBAC logic plugs in without touching the framework:
`app.RankResolver(fn)` where `fn(uid) -> int` (default: uid "1" is rank 9).

## Safe request input

Read every frontend parameter through the unified, filtered accessors on
`HttpContext` — resolved from the same place (form → query → route param) and
passed through the central `Filter` so sanitising is uniform:

```zan
string name = this.In("name");        // trimmed, CR/LF/TAB control chars stripped
string html = this.InText("bio");     // In + HTML-entity escaped (safe to render)
int    page = this.InInt("page", 1);  // tolerant integer parse with a fallback
long   since= this.InLong("since", 0);
double rate = this.InDouble("rate", 1.0);
bool   draft= this.InBool("draft", false);   // 1/true/on/yes vs 0/false/off/no
string raw  = this.InRaw("blob");      // UNFILTERED — only when you need raw bytes
bool   has  = this.HasIn("name");
```

**Required parameters read as one expression.** `Need*` is `In*` for a value the
action cannot proceed without: it aborts with the uniform
`{"code":"0003","msg":"标题不能为空"}` answer instead of repeating the same guard
in every handler. The label is the human name used in that message *and* in the
generated API docs.

```zan
string title = this.Need("title", "标题");      // 400/0003 when absent
string body  = this.NeedText("content", "内容"); // + HTML escaped
int    id    = this.NeedInt("article_id", "文章"); // 400 when absent or not an int
this.Abort(403, "1004", "不是作者本人");          // same uniform answer, on demand
```

## Built-in API documentation

`main.zan` calls `ApiDocs.Mount(app)`; nothing else is written or generated by
hand:

- `GET /api/docs` — offline reference UI (no CDN, no bundler, dark mode)
- `GET /api/docs.json` — OpenAPI 3.0 document

Both are built from what the compiler already knows: the route, verb,
`[Description]` title and auth/rank/menu flags come from the attributes that
*enforce* them, and the parameter list comes from the `In*`/`Need*` calls in the
action body. The read **is** the declaration — there is no parameter schema to
keep in sync, so an endpoint and its documentation cannot disagree:

```zan
[HttpPost]
[Description("保存文章")]
async void Save() {
    int    id    = this.InInt("article_id", 0);       // int, optional, default 0
    string title = this.Need("title", "标题");         // string, required, 标题
    string body  = this.NeedText("content", "内容");   // string, required, 内容
    ...
}
```

`{id}` segments in the route are documented as path parameters automatically.

`In*` centralises input handling; it is **not** a universal security layer.
Output/context escaping (HTML via `InText`, JSON via `JsonStr.Escape`),
parameterised SQL (the ORM binds values), and authorisation remain separate
concerns — filtering input does not replace them.

## Configuration (no recompile)

`config/app.json` is read at startup and is **not** compiled into the binary —
edit it and restart to change settings. Ship it next to the executable.

```json
{
  "server":   { "name": "{{NAME}}", "host": "127.0.0.1", "port": 8090,
                "maxBodyMB": 2, "uploadBodyMB": 20, "globalLimitPerSec": 0 },
  "database": { "driver": "sqlite", "sqlitePath": "data/app.db",
                "host": "127.0.0.1", "port": 3306, "name": "app",
                "user": "root", "password": "", "poolSize": 8 },
  "auth":     { "secret": "32+ random chars — or env ZAN_AUTH_SECRET" },
  "worker":   { "count": 4, "daemon": false },
  "log":      { "access": false, "slowMs": 1000, "slowSqlMs": 200,
                "dir": "logs", "pattern": "{yyyy}{MM}/{dd}.log" },
  "metrics":  { "enabled": true, "path": "data/metrics.db",
                "flushSeconds": 60, "flushRows": 2000, "keepDays": 30 },
  "cache":    { "driver": "memory", "redisHost": "127.0.0.1",
                "redisPort": 6379, "poolSize": 8, "connectTimeoutMs": 3000 }
}
```

### 配置项说明

| 键 | 默认 | 说明 |
|---|---|---|
| `server.name` | `{{NAME}}` | 站点名；进 API 文档标题与管理页 |
| `server.host` / `server.port` | `127.0.0.1` / `8090` | 监听地址与端口 |
| `server.maxBodyMB` | `2` | 普通（内存）请求体上限，MB |
| `server.uploadBodyMB` | `20` | 流式上传上限（附件 T4），MB；`main.zan` 用它设 `MaxBody`/`MaxUpload` |
| `server.globalLimitPerSec` | `0`（不限） | 全局每秒请求上限；0 关闭全局限流 |
| `database.driver` | `sqlite` | `sqlite` / `mysql` / `mariadb` / `postgres`；连接不可达时服务器照常启动，DB 路由返回 503 |
| `database.sqlitePath` | `data/app.db` | SQLite 文件相对路径（相对运行目录；绝对路径会落到盘根而失败） |
| `database.host/port/name/user/password` | — | driver 为 mysql/postgres 时使用；`poolSize` 连接池大小 |
| `auth.secret` | — | 会话令牌签名密钥，32+ 字符；被环境变量 `ZAN_AUTH_SECRET` 覆盖；缺省/过短登录报配置错误 |
| `worker.count` | `4` | worker 进程数；Linux/macOS 走 `SO_REUSEPORT` 多进程，Windows 恒为单进程 |
| `worker.daemon` | `false` | `true` 时 Linux 下 `setsid` 后台化；Windows 忽略 |
| `log.access` / `log.slowMs` / `log.slowSqlMs` | `false` / `1000` / `200` | 访问日志开关、慢请求（ms）与慢 SQL（ms）阈值 |
| `log.dir` / `log.pattern` | `logs` / `{yyyy}{MM}/{dd}.log` | 日志目录与按日分文件模板 |
| `metrics.enabled` / `metrics.path` | `true` / `data/metrics.db` | 指标历史独立库存放（与业务库分离） |
| `metrics.flushSeconds` / `flushRows` / `keepDays` | `60` / `2000` / `30` | 刷盘间隔、批量行数、保留天数 |
| `cache.driver` | `memory` | `memory`（worker 本地，仅 `worker.count=1` 时语义正确）或 `redis`（跨 worker 共享，IR/轮询/会话共享的前提） |
| `cache.redisHost/port/poolSize/connectTimeoutMs` | `127.0.0.1` / `6379` / `8` / `3000` | driver 为 redis 时使用；Redis 不可达时读未命中、写丢弃、回落数据库 |

DB `driver` accepts `sqlite` (default), `mysql`/`mariadb`, `postgres`. The DB
connection is opened lazily and tolerantly: if it is not configured/reachable,
the server still runs and DB-backed routes return a clear 503 instead of
crashing.

## Features

| Capability | Where | Notes |
|---|---|---|
| Layered controllers | `src/controller/` | thin `main.zan`, one class per resource |
| Default ORM | `src/model/`, `framework/Db.zan` | `System.Data.Orm` models, config-driven engine |
| Default cache | `framework/Cache.zan` | in-memory TTL; Redis via `System.Data.Redis` on async path |
| External config | `config/app.json`, `framework/Cfg.zan` | runtime-loaded, not compiled in |
| High-performance routing | `System.Web.Router` | static-first match + `{param}`, 404/405 |
| Rate limiting | `System.Web.Hooks` | global + per-route fixed windows, 429 |
| Auth / sessions | `WebApp.AuthUser`, `Sessions` | Bearer token or session cookie, `.Auth()` guard |
| Lifecycle hooks | `System.Web.Hooks` | typed delegates, run in order |
| In-memory views | `System.Web.View` | templates loaded ONCE at startup |
| Streaming uploads | `.Upload()` routes | body streamed to disk in 64KB chunks |
| Validation | `System.Web.Validate` | Require/MaxLen/IsInt/OneOf + SafeFileName |
| Shared-memory tables | `System.Web.RouteTable` / `RouteStats` / `PermTable` | route attributes, per-route timings, role×route rights across workers |

## Workers & scaling

Set `worker.count` in `config/app.json`:

- **`count: 1` (default)** — one process running the coroutine event loop. Each
  connection is handled in its own coroutine, so a single worker already serves
  thousands of concurrent connections. Best for development: logs stream into
  the IDE terminal.
- **`count: N` (Linux/macOS)** — the process binds with `SO_REUSEPORT` and
  re-launches itself as `N-1` extra worker processes bound to the **same** port;
  the kernel load-balances accepted connections across all workers, using every
  CPU core (this is how Workerman/swoole scale). Windows has no `SO_REUSEPORT`
  load-balancing, so it runs a single process there.
- **`daemon: true` (Linux)** — detaches from the terminal (`setsid`) and runs in
  the background.

Restart-on-crash is delegated to the process supervisor (systemd
`Restart=always`, Docker `restart: unless-stopped`, Kubernetes), the standard
way to supervise horizontally scaled services. `main.zan` boots via
`WebServer.RunCommand(app, count, daemon)` from the standard library, so the
binary is a service that answers `start`, `start -d`, `stop`, `restart`,
`reload` (rolling worker replacement) and `status` on the command line; the
running instance is addressed through its control port (the HTTP port +
10000, or `--ctl-port N`). `WebServer.Run(app, count, daemon)` is the plain
variant that only ever starts in the foreground. `-d` / `daemon: true` detach
on Linux only -- on Windows the process stays in the foreground (use NSSM or a
Windows service to run it in the background). Listener handoff,
respawn-on-crash, daemonization and the control port live in
`System.Net.Worker` / `System.Diagnostics.ProcessHost`, so any server gets
them, not just this template.

## Observability (`GET /admin/stats`)

The request lifecycle and database queries are instrumented into the shared
`System.Diagnostics.ServerMetrics` singleton, exposed as JSON at
`/admin/stats`:

```json
{
  "uptime_ms": 2015, "pid": 31084, "worker_id": 0,
  "cpu_ms": 31, "cpu_percent": 1, "mem_rss_bytes": 6852608,
  "requests": {"count":4,"errors":2,"total_us":13120,"avg_us":3280,
               "max_us":15400,"slow_threshold_us":500000,"slow_count":0},
  "queries":  {"count":2,"total_us":12800,"avg_us":6400,"max_us":8100,
               "slow_threshold_us":200000,"slow_count":0},
  "slow_requests": [{"req":"GET /slow -> 200","us":750000}],
  "slow_queries":  [{"sql":"SELECT * FROM huge_join","us":320000}]
}
```

- Every duration is MICROSECONDS (`*_us`). A request this server serves in
  200us is not measurable in milliseconds -- the Windows millisecond clock steps
  ~15.6ms -- so a millisecond average of a fast endpoint read as a flat `0`.
  The slow thresholds are still *configured* in ms (`SlowRequestMs`,
  `[log].slowMs`) and reported here converted.

- `cpu_ms` / `cpu_percent` / `mem_rss_bytes` are read from the OS
  (Windows `GetProcessTimes`/`GetProcessMemoryInfo`, Linux `/proc/self`). On a
  platform where a probe is unavailable the field reports `-1` (not a fake
  zero). `cpu_percent` is a delta between successive polls — poll on a fixed
  interval for a meaningful value.
- Request throughput/latency/errors and the last 32 **slow requests** are
  recorded for every request; database query throughput/latency and the last
  32 **slow queries** are recorded around model DB calls. Adjust thresholds via
  `ServerMetrics.Global().SlowRequestMs(...)` / `.SlowQueryMs(...)`.
- **Security:** `/admin/stats` leaks internal timing/paths. Guard it with
  `.Auth()`, an IP allow-list, or a separate admin bind before exposing it.

## Run

Build & run from the IDE (output streams into the terminal panel), or from a
shell — **build into `build/`, run from the project root**:

```
zanc src/main.zan src/**/*.zan --auto-stdlib -o build/app.exe
build/app.exe          # cwd = project root, so ./config/app.json and ./wwwroot resolve
```

全量编译约 295 个文件（129 业务源 + stdlib 缓存）；编译器从控制器属性与
`In*`/`Need*` 调用派生路由表与 API 文档数据，无生成文件需要维护。

**种子账号**（首次启动 `Schema.SeedAdmin` 写入，仅当 sys_user 空表时）：

- 超级管理员：`admin` / `admin1234`（`isSuper=1`，绕过角色表）
- 登录页：`/admin/login`；首个动作建议改密（个人中心）并改 `auth.secret`

数据目录（运行时生成，git-ignored）：

```
data/app.db          SQLite 主库（WAL）
data/backups/{ts}/   备份（app.db + uploads.zip + manifest.txt，保留 7 份）
data/metrics.db      指标历史（独立库）
data/uploads/        附件流式落盘（{year-month}/ 结构）
logs/{yyyyMM}/dd.log 运行日志
```

The working directory matters more than where the binary sits: the server reads
`config/app.json`, loads `views/`, serves `wwwroot/` and opens the SQLite file by
RELATIVE path, so run it from the project root (or from a deploy directory that
has those next to the executable) — never `cd build && ./app.exe`.

### Deploying

Only `.zan` code is compiled into the executable. Views, config and assets are
read at run time, so a deploy directory is four things — and `src/` is not one
of them:

```
app.exe                 + the driver DLLs the linker put beside it in build/
config/app.json
views/                  every .html, in the module structure it has in the repo
wwwroot/
```

Of the DLLs, only the driver for the database in use is needed: `libsqlite3-0.dll`
for SQLite, `libpq.dll` + `libssl-3-x64.dll` + `libcrypto-3-x64.dll` +
`libiconv-2.dll` + `libintl-8.dll` for PostgreSQL (MySQL speaks its protocol
without a client library). `data/app.db` is not copied — the directory and the
database are created on first start, and `Schema` fills in the tables and the
seed account. Set the session key — `[auth].secret` in `config/app.json`, or the
`ZAN_AUTH_SECRET` environment variable which overrides it — to 32+ characters,
or sign-in fails with a configuration error.

生产部署、MySQL 迁移、备份恢复**演练**步骤与旧库升级路径见
`docs/deploy-collab.md`。

**Keep generated files out of the source tree.** `-o build/app.exe` exists so the
executable and the driver DLLs the linker copies beside it land in one throwaway
directory. Everything the running app produces is likewise disposable and
git-ignored, but it does land in the working directory:

| Path | What | Keep? |
|---|---|---|
| `build/` | compiler output: `app.exe` + driver DLLs | no — delete freely |
| `publish/<platform>/` | the IDE's Publish output | no |
| `data/` | SQLite database + metrics history (`database.sqlitePath`, `metrics.path`) | it *is* your data in dev |
| `uploads/` | streamed upload target | runtime state |
| `*.log`, `*.err` | whatever you redirected stdout/stderr to | no |

Point `database.sqlitePath` anywhere else (e.g. `var/app.db`) if `data/` does not
suit you: the directory is created for you, but keep the path RELATIVE — an
absolute `/data/app.db` is the root of the drive, where the file cannot be
created, and the server then runs with no database at all (schema unapplied,
every sign-in "wrong password").

## Endpoints

入口与平台域（完整清单以 `/admin/system/docs` 页与 `/api/docs.json` 为准，
由编译器从路由属性自动派生，T2~T16 新端点已全部收录）：

- `GET /` — HTML 落地页
- `GET|POST /admin/login`, `GET /admin/logout` — 后台登录（种子 `admin` / `admin1234`）
- `POST /api/auth/login`, `GET /api/auth/me` — 令牌登录 / 当前身份
- `GET /admin` — 工作台（聚合待办 + 公告卡片 + 运行指标）
- `GET /admin/system/users|roles|departments|dicts|logs|settings` — 系统管理
- `GET /admin/system/docs` — 接口文档页（分组/检索/参数/应答）
- `GET /admin/system/backup`, `POST /admin/system/backup/create`,
  `GET /admin/system/backup/download?id=` — 备份页 / 立即备份 / 下载 zip
- `GET /admin/oa/notifies`（`unread` / `stream` / `read`）— 通知中心 + 铃铛 API
- `GET|POST /admin/oa/attachments/upload|download|delete` — 附件（流式上传，
  query 传 `module/relatedId/name/mime`）
- `GET /admin/oa/todocenter`（`add` / `done`）— 待办中心
- `GET /admin/oa/calendar` — 日历（月/周）
- `GET /admin/oa/docs`, `/admin/oa/spaces`, `docs/edit|save|revisions` — 文档中心 + 块编辑器
- `GET|POST /admin/oa/messages`（`chat?peer=` / `send`）— 站内私信
- `GET|POST /admin/oa/announcements` — 公告（全员弹窗一次）
- `GET /admin/oa/forms`, `/admin/oa/formfill` — 表单定义 / 填报（提交可自动起流程）
- `GET /admin/oa/search?kw=` — 全局搜索聚合
- `/admin/crm/*`（customers/contacts/opportunities）、`/admin/wms/*`、
  `/admin/ecbi/*` — 业务模块 CRUD + `export|import|template` 导入导出端点
- `/admin/flow/flows|tasks` — 流程定义/发起/审批（`flows/kill?id=`）
- `GET /admin/flow/notifies` — 旧通知页（301 → `/admin/oa/notifies`；铃铛 API 原路径保留）
- `GET /api/docs.json` — OpenAPI 3.0（`/api/docs` UI 已收进管理后台 docs 屏）

## 已知行为说明（T17 回归观察项，非缺陷）

- **私信已读双层语义（O-1）**：打开私信会话只清 `OaMessage.isRead`（会话层
  已读回执）；对应的通知行 `SysNotify` 独立计数，需在通知中心显式标记已读。
  两层 API 分明是有意设计（Messages 与 Notifies 各自闭环），"点开即全消"
  与否留产品定夺。
- **私信发送参数是 `peer`（O-2）**：`POST /admin/oa/messages/send` 用
  `peer` 指定对方用户 id，不是 `toId`；参数错名会得到 404"对方不存在或已
  停用"（伪造 `peer=9999` 同样 404，租户闸生效）。
- **`Flow.Kill` 幂等 200（O-3）**：对不在进行中（status≠1）的流程实例，
  `POST /admin/flow/flows/kill?id=` 返回 200"实例不在进行中"而非 404；
  Update 条件含 `status==1`，语义无害且不越租户（T17 隔离矩阵 N 已验证）。

## Database & ORM (FreeSQL-style, bidirectional)

`System.Data.Orm.Model` maps both directions:

- **Model -> table** (code first): `Model.Define("users").Column(...).CreateTable(db)`
  (see `src/model/User.zan`).
- **Table -> model** (database first): `Model.FromTable(db, "users")` reflects an
  existing table's schema (SQLite `PRAGMA table_info`, MySQL/MariaDB
  `SHOW COLUMNS`, otherwise the ANSI `information_schema` catalog) into a Model,
  auto-detecting the provider. `Model.FromTable(db, "users").ToDefineCode()`
  emits the `Model.Define(...)` source to scaffold a model file from a table.

```
Model users = Model.FromTable(Db.Conn(), "users");   // reflect schema
Console.WriteLine(users.ToDefineCode());               // print model source
```

## Static assets

`main.zan` mounts `wwwroot/` at `/static` before auth and routing:

```zan
StaticFiles.Mount(app, "/static", "wwwroot");   // GET /static/css/app.css
```

**`wwwroot/` is the whole public surface.** One mounted directory, nothing else:
`src/`, `config/app.json`, `data/` and every log sit outside it, so no path
trick reaches them even if a check were wrong — the files simply are not under
the served root. A file becomes downloadable by being moved into `wwwroot/`,
which makes "is this public?" a question about the directory rather than about
the path parser.

Bodies are read once per worker and then served from memory with
`Cache-Control: public, max-age=86400`; a path is rejected unless every segment
is plain `[A-Za-z0-9._-]`, so `..`, backslashes and dotfiles cannot escape the
directory. `StaticFiles.MaxAge(0)` while developing.

```
wwwroot/css/app.css          the whole design system (no framework, no build)
wwwroot/js/app.js            htmx glue: CSRF header, error/flash toasts
wwwroot/vendor/htmx.min.js   htmx 1.9.12    -- unpkg.com/htmx.org@1.9.12/dist/htmx.min.js
wwwroot/vendor/alpine.min.js Alpine 3.14.1  -- unpkg.com/alpinejs@3.14.1/dist/cdn.min.js
```

The two vendor files are committed on purpose: no CDN at runtime, versions
pinned, works offline. To upgrade, download the new file over the old one and
update the version in this list. There is no Node toolchain and nothing to
compile -- `app.css` is hand-written CSS (variables, grid, a 900px breakpoint
that turns the admin sidebar into a drawer, and tables that become cards under
720px), so a page needs no build step to look right on phone or desktop.

## Template syntax (views/*.html)

```
{{name}}                   escaped variable
{{{name}}}                 raw variable
{{#if name}}...{{/if}}     conditional
{{#if name}}...{{else}}...{{/if}}  conditional with else branch
{{#each rows}}...{{/each}} loop over ViewData.AddList("rows")
layout.html + {{content}}  page wrapper
```

## Backup & restore

Built-in backups live on the `Backup & restore` admin page (sqlite only,
super-admin only, so no role grants appear for it by default). One backup is a
timestamped directory under `data/backups/{YYYYMMDD}-{HHMMSS}/` containing:

```
app.db         consistent snapshot taken with VACUUM INTO (safe while the
               server keeps running; WAL readers/writers are not blocked)
uploads.zip    stored (not recompressed) copy of data/uploads/
manifest.txt   taken-at, driver, db size, attachment count / bytes
```

The last 7 directories are kept (older ones are rotated away). A `.lock` file
in `data/backups/` serializes concurrent runs across worker processes. Each
backup can be downloaded from the page as `backup-{timestamp}.zip`.

Manual restore, on or off the server:

1. Stop the app so nothing writes during the restore.
2. Get the files: download `backup-{timestamp}.zip` from the admin page, or
   read them straight from `data/backups/{timestamp}/` on the host.
3. Overwrite the main database file with the backup's `app.db`, then unpack
   `uploads.zip` over `data/uploads/` (keep the `{year-month}/` layout).
4. Restart the app and spot-check data and attachments.

### Cron backups without the admin page

The same VACUUM INTO trick works from a shell, no app changes needed:

```
# /etc/crontab -- daily 02:30, keep the same 7-slot layout
30 2 * * * root mkdir -p /srv/app/data/backups/$(date +\%Y\%m\%d-\%H\%M\%S) \
  && sqlite3 /srv/app/app.db "VACUUM INTO '/srv/app/data/backups/$(date +\%Y\%m\%d-\%H\%M\%S)/app.db'" \
  && cd /srv/app/data && zip -qr backups/$(date +\%Y\%m\%d-\%H\%M\%S)/uploads.zip uploads \
  && ls -1dt /srv/app/data/backups/* | tail -n +8 | xargs -r rm -rf
```

Adjust `/srv/app` to your install root. Use the admin page instead when you
can: it also stamps a manifest and is protected against concurrent runs.

### MySQL deployments

The built-in page degrades explicitly on `driver=mysql` (page notice and a
400 on create) rather than silently doing nothing. Back up with mysqldump
plus a file copy of the uploads directory:

```
mysqldump --single-transaction --routines --triggers dbname > backup.sql
tar -czf uploads.tar.gz data/uploads
```

Restore by piping the dump back into a fresh schema (`mysql dbname <
backup.sql`) and unpacking `uploads.tar.gz` over `data/uploads/`. Test the
full restore path on a staging box before you ever need it in production.

