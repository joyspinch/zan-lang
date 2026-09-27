# Zan.Mvc — ZanWeb Web MVC Framework

Enterprise web application skeleton modeled on a production swoole (ZxPHP)
framework, rebuilt on Zan's coroutine runtime — a layered controller/model
structure, an external config file, and the ORM and cache wired in by default.

本包以**包（基座）**形态存在，不再是一个项目：应用按层 `using ZanWeb.*;`
引用包源（zanc 自动发现拉入），框架随包升级，应用代码零改动——非侵入。

分层两件事：

- **`Zan.Mvc` 包（本目录）** — 应用框架层与管理台：引导（`ZanWeb.Boot`）、
  配置/DB/缓存上下文、鉴权 RBAC、设置、Schema + 种子挂点、指标、任务宿主、
  sys 实体与 DAO、三个控制器基类（`AppController` / `ApiController` /
  `AdminController`），以及管理台与前台控制器、随包视图与静态资产。
- **`System.Web` 标准库** — Web 内核：`WebApp`、`Router`、`HttpContext`、
  `Controller`、`View`、`WebServer`，与 `RouteTable` / `RouteStats` /
  `PermTable`。

应用侧只保留业务：组合根（main.zan）、业务控制器/模型/DAO、种子、应用自有
配置。框架修复落在包里——拉新包版本即完成更新。

## Layout

```
zan.pkg                 包清单（版本随包演进）
src/ZanWeb/             包源码。目录是工程组织（Framework/Modules 两块），
                        命名空间保持扁平、与 URL 同构，不随目录加深
  Framework/              框架层：所有模块共享的件；业务模块只依赖它
    Core/                   基建：Boot 引导 · Cfg 配置 · Db/DbContext · Schema · Gen 代码生成器
    Services/               业务支撑：JobHost 任务宿主 · Cache/CacheContext · Metrics ·
                            Mailer · ClusterBus/Presence · Settings
    Security/               横切·安全：Auth · Perm/PermTable · DataScope · Keys ·
                            LoginThrottle · VerifyCode
    Ai/                     横切·AI：AI 助手注册与端点策略
    *.zan                   接入层基座：App/Api/AdminController 三基类 · AppServices ·
                            CrudOps 写网关 · ListPage/FormPage 屏基座 · Fmt/Lang/Prose 渲染件
  Modules/                业务模块，一个目录一个模块（垂直切片：Controller+Model+Dao）
    Sys/
      Controller/           接入层：目录=URL 族（Account/ Admin/ Api/ Blog/
                            Health/ Index/ User/），一类一文件
      Model/  Dao/          数据层：sys_* 实体与 DAO（DAO 只放跨表 JOIN/聚合/
                            复杂动态条件，单表读写走实体链，见「代码规范」）；
                            Model/Blog/、Dao/Blog/ 为示例模块
      Services/             模块内非控制器业务件（业务种子 BlogSeed 等），
                            不进 Controller/，见「Controller/ 纯净与端点纪律」
    Crud/                   配置驱动管理屏引擎
      Controller/Admin/       CrudScreenController.zan——管理屏基座（ns
                              ZanWeb.Admin，与 Dashboard/Profile 同族；
                              继承 AppController 即控制器，无论是否
                              自带路由）
      Model/                  CrudConf.zan——屏面声明模型（ns ZanWeb.Model，
                              与 Model/ 目录镜像；描述字段/表单/校验，
                              非表实体）
views/                  页面模板，按控制器模块分目录（随包资产）
  layout.html             全站布局；模块自有 layout.html 仅覆盖本模块
wwwroot/                唯一 Web 可达目录，挂载在 /static（css/js/vendor/i18n）
```

两条不变式：

1. **命名同构（扁平）**：URL ↔ Controller ↔ Model/Dao ↔ 表名一一对齐
   （`/admin/system/*` ↔ `ZanWeb.Admin.System` ↔ `SysUsers` ↔
   `sys_user`；`/health` ↔ `Health`）。目录负责工程分层，命名空间负责
   URL 语义——两者解耦后，挪文件不引起改名，改名不引起挪文件。
2. **依赖单向**：`Modules/* → Framework/*`；Framework 不引用任何模块，
   控制器基类、屏幕原语、基础设施全部住在 Framework，新模块照 Sys 的
   形状即可接入，不产生模块间依赖。
3. **`Controller/` 的成员资格机械可查**：继承 `AppController` 的类
   （路由控制器与控制器基座）进 `Controller/`，其余类型（声明、实体、
   DAO、视图助手）不进——判定看类声明的继承链，不看是否自带路由属性。

视图键由 `View.LoadRec` 按目录路径推导（`views/Admin/System/SysUsers.Index.html`
→ `Admin.System.SysUsers.Index`），与控制器命名空间同构——看到路径即知键名。

## 代码规范

目录、namespace、命名、DB 四件事全部机械可查；每条规则带"为什么"。

### 目录 ↔ namespace ↔ URL 三者同构

`Modules/<模块>/Controller/` 以下的目录 == namespace（`ZanWeb.` 以下）==
URL 族：`Controller/Admin/Content/Categories.zan` ↔ `ZanWeb.Admin.Content` ↔
`/admin/content/*`。`Model/`、`Dao/` 层段保留在 namespace
（`ZanWeb.Model[.子族]`、`ZanWeb.Dao[.子族]`）。模块目录名（Sys、Crud）
是纯物理分组——不入 namespace、不入 URL。

为什么：RBAC 权限码就是 `namespace.Class.Action`
（`Admin.Content.Categories.Def`），多级菜单分组就是 URL 第一段
（`MenuBuilder.Section("content", "内容管理")` 注册段名，`ForUser` 从
路由表推导、可见性与放行走同一个权限解析器）。namespace 或目录一旦
偏离 URL，权限码与菜单就失去锚点——不存在第二份要维护的映射表。

**视图键同理**：`View.LoadRec` 按目录路径推导
（`views/Admin/System/SysUsers.Index.html` → `Admin.System.SysUsers.Index`）。
显式 `ViewOf/FragmentOf` 的键必须等于某个文件的真实键——查不到时
HTTP 仍是 200，页面渲染成 `<!-- view not found -->` 注释，冒烟只看
状态码是假绿（本轮踩过：key 改了、views 资产目录没跟着挪）。

### Controller/ 纯净与端点纪律

`Controller/` 路径下**只允许控制器类**（成员资格看继承链，不变式 3），
且控制器文件里**不得混入非控制器类型**——数据形状进 `Model/`（如
`Model/Dev/AiAgentDoc.zan`），业务种子等非控制器业务件进模块的
`Services/`（如 `Services/BlogSeed.zan`）。

**非端点方法必须显式 `private`/`protected`。** Zan 成员默认公有，而类级
`[Route(".../[action]")]` 的约定展开会把**每一个公有方法**变成可达端点
并计入权限码空间——`Def()`/`OnDeleting()`/`Bust()` 这类钩子与内部助手
曾因此生成 21 个假 action。启动日志 `[rbac] ... 未标注 Perm，已拒绝授权`
一旦出现，就是在提示有方法收错了可见性：清零为准，不留假端点。

**权限三档**，新动作先选档再写码：`Grant`（管理屏，授权位驱动）/
`Login`（登录即可：个人中心、菜单 JSON）/`None`（登录注册页、健康检查、
前台博客、API 文档与演示口）。匿名面必须逐动作复核——公开控制器里
新增动作不标档即匿名。

### 呈现格式化一律 Fmt.*

状态→文案/徽章样式只住在 `Framework/Fmt`（`Published*` 发布态、
`Review*` 审核态、`Enabled*` 启停态、`Ok*` 成败、`LoginText` 登录成败，
各一对 Text/Class），控制器**禁止手写**同名 helper——此前 8 个文件各写
一遍，文案漂移（"停用"vs"禁用"）与样式漂移（ok/off vs ok/bad）随之
而来。新状态族先进 Fmt 再使用。

### 命名词表

**取参族：基名 + `Any` 变体。** 基名只读 route/query/form（表单编码）；
`*Any` 变体在基语义上追加收 JSON 体顶层字段——自研壳（表单编码）与
adminUI（JSON 体）两套前端同动作双兼容靠它。成员按所在层住：

| 层                          | 基名                  | Any 变体            |
|-----------------------------|-----------------------|---------------------|
| stdlib `Controller`         | `In` `InInt` `Paged`  | —                   |
| Framework `AppController`   | —                     | `InAny` `InAnyInt`  |
| Framework `AdminController` | `Ids(name)`           | —                   |
| Modules `CrudScreenController` | —                  | `IdsAny` `PagedAny` |

新取参一律进这个族：基语义在哪个层就加在哪层，不发明第三个后缀。
（为什么：Any 后缀在接 adminUI 时没成文，看着像随手起名——实际四对
`In/InAny`、`InInt/InAnyInt`、`Ids/IdsAny`、`Paged/PagedAny` 全对齐；
写下来之后"乱"变"规则"。）

**动作词表。** 自有动作与前端合同动作分列；合同词由前端源码钉死，改词
即断前端，改动前必须对照 adminUI：

- 自有：`index` `form` `save` `delete` `batch` `field` `options`
- adminUI 合同：`conf`(PUT 配置下发) · `list`(POST JSON 筛选列表) ·
  `edit`(POST 单行回显——合同词，读语义) · `formconf`(PUT 表单配置)

**`Crud*` 语义前缀**只有三类角色，新增类型先对号入座：

| 类型                  | 位置                                        | 角色                         |
|-----------------------|---------------------------------------------|------------------------------|
| `CrudConf`            | Modules/Crud/Model/（ns `ZanWeb.Model`）    | 屏面声明（描述字段/表单/校验）|
| `CrudScreenController`| Modules/Crud/Controller/Admin/（ns `ZanWeb.Admin`） | 屏引擎基座            |
| `CrudOps`             | Framework/（ns `ZanWeb.Web`）               | 声明驱动的通用写动作与 conf 投影网关 |

（为什么：曾有静态网关 `class Crud` 与模块命名空间 `ZanWeb.Crud` 撞名，
看名字分不清角色——已更名 `CrudOps`。）

### DB 访问规范

三种形态各管一摊，不混用：

1. **实体链（默认）**：`this.SysUser.Select<...>`——编译期表访问器，
   字段名编译期校验。单表读写一律走它。
2. **DAO（跨表/聚合才建）**：`XxxDao(AppController host)` 收宿主解析
   连接；只放跨表 JOIN、聚合统计、复杂动态条件。**禁止**新增与实体链
   逐字重复的方法（现存 `SysUserDao.ById` 等属历史债务：不扩散、不改
   依赖它的调用点，但改到相关文件时顺手收敛到实体链）。
3. **`DbTable` 运行期网关（仅配置驱动场景）**：表名/列名运行期才确定
   的（Crud 引擎、代码生成器）走 `DbTable.Of`；标识符过 `Gen.Safe`+
   `RequireIdent` 双校验，值一律 `?` 占位符。业务代码**禁手拼 SQL**
   （现状为零，保持为零）。

**连接获取 2×2 矩阵 + 协作者口**（共五个口，各有唯一语义）：

| 取法                         | 写/默认              | 只读（从库，事务中强制主库） |
|------------------------------|----------------------|------------------------------|
| 异步借出（动作内首次取数）   | `await Conn()`       | `await ReadConn()`           |
| 同步取已借（辅助函数）       | `Held()`             | `HeldRead()`                 |
| 跨类协作者（静态网关/DAO 收宿主） | `__Conn()`（公开，唯此一处） |                    |

规则：动作里第一次取数用异步口；`Held*` 只准在"同动作已 `await` 过对应
异步口"之后使用；`__Conn` 供够不着 protected 口的协作者（静态网关、
DAO）解析请求连接，控制器内部不用它。（为什么：五个口曾无成文语义，
借还与只读回退全靠注释撑；矩阵写死后，用错口变成可 review 出来的事。）

## 配置驱动的管理屏（Modules/Crud）

大多数管理屏只有"一张表的增删改查"：`Modules/Crud` 把这一层做成引擎——
子类写一份 `CrudConf` 声明 + 约三十行属性壳，即得到列表、表单、保存、
删除、批量启停/删除、行内修改、表格配置（adminUI conf 契约）、远程选项
（data-pick 契约）八个端点，共享一份视图。声明即白名单：搜索、保存、
行内修改只收声明过的列名，值一律占位符参数，审计与权限位与手写屏同一
机制。`ZanWeb.Admin.Content.Categories` 是完整的示范屏（配置 + 三个钩子）。

```zan
[Route("admin/shop/goods/[action]")]
[Custom(Authorization = CustomAuthorization.Grant)]
[Description("商品管理")]
class Goods : CrudScreenController {
    override CrudConf Def() {
        return CrudConf.Of("shop_goods", "商品管理", "admin/shop/goods")
            .Col("id", "ID", "num", "70px")
            .Col("name", "名称", "text", "")
            .Tag("status", "状态", "1=上架|ok,0=下架|off", "90px")
            .Text("名称", "kw", "name", "搜索名称")
            .TbarAdd("新增商品").TbarEnable().TbarDisable()
            .OpsEdit().OpsDel("确认删除该商品吗？")
            .EditFields("name:text,price:int,stock:int,status:int")
            .Inline("stock:int,status:int")          /* 行内只放行库存与状态 */
            .Unique("name", "商品名已存在")
            .FormText("名称", "name").Req().Max(64)
            .FormText("价格", "price").Req()
            .Created("createdAt").Updated("updatedAt")
            .Label("name").Toggle("status")
            .Order("id", true);
    }
    [HttpGet] [Route("/admin/shop/goods")]
    [Custom(IsMenu = true, Icon = "mdi:package", Perm = PermBit.View)]
    [Description("商品管理")]
    async void Index() { await this.DoList(); }
    /* ...其余属性壳同样只转发：Conf/Form/Save/Delete/Batch/Field/Options */
}
```

超出声明的业务语义经四个钩子注入（按需覆写，不必全写）：

| 钩子 | 时机 | 典型用途 |
|---|---|---|
| `OnSaving(row, isNew)` | 保存前 | 派生字段、规整化，返回错误文案可拒绝 |
| `OnDeleting(id)` | 删除前 | 占用检查（如分类下还有文章则拒绝） |
| `OnRows(rows)` | 列表取数后 | 投影计数列、关联名、衍生徽章 |
| `OnSaved(isNew)` | 保存后 | 缓存失效、联动重算 |

边界约定：`EditFields` 是保存可写的全集，`Inline` 收窄行内/批量启停的
子集；唯一约束用 `Unique()` 声明（写前检查、排除自身行）；**有子记录的
表不要声明批量删除**——批量删不过行级占用钩子（Categories 即如此：只有
行删带占用检查）。

### 与 OneAdmin/adminUI 的对照（借鉴结论）

本引擎与 `D:\project\admin\OneAdmin`（后端）+ `adminUI`（前端）那套
TableConfig/FormConfig 体系架构同构：约定端点（conf/list/save/field/
batch/delete/options）、服务端下发表格配置、列即表单。借鉴落地的：
字段单点声明（对方 buildFallbackFormConfig 的"从列推导表单"在这里是
构造保证——可写列天然就是表单）、控件类型收敛（对方 29 种收敛到 10 种
的教训，这里只留 text/area/int/opts/pass/time 六种）、`Pick(url)` 远程
选项筛选（对方 OptionsUrl + `{api}/options` 约定）、`Pattern()` 校验
（对方收敛后的 {required,min,max,pattern} 规则集）。明确不搬的：字段
联动四件套（visibleWhen 等）——需要前端求值器，管理对话框规模下暂无
必要；字典驱动 Opts（sys_dict 接入留待首个真实需求）；用户级列配置
持久化（属前端 zan-table 职责）；弹窗 LayerConfig 式配置爆炸（对方自
认的历史包袱）。

### adminUI 前端契约（无需改前端即可接）

每个配置驱动屏同时供两类客户端：自研壳（SSR HTML，`/admin/...` 直接
出页面）与 adminUI 系前端（axios + JSON 信封）。信封统一为
`{resp_code:"0000",status,msg,url,dynamicToken,t,data}`（`resp_code`
"0000" 为成功；错误走既有 `{code,msg}` 形状并带 HTTP 状态码）：

| 端点（{api} = 屏路径） | 动词 | 请求 | data |
|---|---|---|---|
| `{api}/conf` | PUT | — | ZanTable ListConfData：{conf,columns,filters,tools} |
| `{api}/list` | POST | JSON `{page,pageSize,...筛选}` 或表单 | `{data:[行],total}` |
| `{api}/edit` | POST | JSON/表单 `{id}` | 整行对象（列名即键，值全部文本） |
| `{api}/formconf` | PUT | — | legacy FormConfig `{title,width,labelWidth,size,cols,formItems}`，formItems 含 component/span/rules（{required,min,max,pattern}） |
| `{api}/form` | GET | `?id=` | 表单对话框 HTML |
| `{api}/save` | POST | 表单或 JSON `{字段...}` | 无（toast） |
| `{api}/delete` | POST | `{id}` | 无 |
| `{api}/batch` | POST | `ids` CSV 或 JSON 数组；`value` 空为批量删 | 无 |
| `{api}/field` | POST | `{id,field,value}`（adminUI fieldAsync 的 `{data:{id},field,value}` 亦收） | 无 |
| `{api}/options` | GET | `?kw=` / `?id=` | `[{id,label}]` |

取参族 `InAny/InAnyInt` 对表单编码与 JSON 体双兼容：route/query/form 未
命中时读 application/json 顶层标量字段，同一动作两类客户端共用。筛选
条件仍由 `CrudConf` 声明收集（JSON 筛选与表单筛选同一入口）。

已知边界（实测踩坑后的保守写法）：async 动作里不要从"helper 返回的
`List<JsonValue>` 提取元素再跨静态调用传参"来构建应答体——该形状在
服务端 async 延续语境下原生崩溃（最小同步探针无法复现，根因未定论，
疑 zanc 对该形态的 ARC 处理有漏）；应答对象请在动作内就地
`NewObject`+`Put` 构建（DoEdit/DoFormConf 即此写法）。

## Attribute-driven routes

Controllers declare routing with **attributes** instead of hand-wiring in the
composition root (the Zan equivalent of the PHP `@title/@auth/@rank` docblocks).
A compiler pass scans the compile unit's controllers (package-pulled ones
included) and synthesizes `__AttrRoutes.Register(app)`, which the composition
root calls once — no generated file to maintain, no reflection.

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

The composition root calls `ApiDocs.Mount(app)`; nothing else is written or generated by
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
  "server":   { "host": "127.0.0.1", "port": 8080, "maxBodyMB": 2, "globalLimitPerSec": 0 },
  "database": { "driver": "sqlite", "sqlitePath": "data/app.db",
                "host": "127.0.0.1", "port": 3306, "name": "app", "user": "root", "password": "" },
  "worker":   { "count": 1, "daemon": false },
  "cache":    { "driver": "memory", "redisHost": "127.0.0.1", "redisPort": 6379 }
}
```

`driver` accepts `sqlite` (default), `mysql`/`mariadb`, `postgres`. The DB
connection is opened lazily and tolerantly: if it is not configured/reachable,
the server still runs and DB-backed routes return a clear 503 instead of
crashing.

## Features

| Capability | Where | Notes |
|---|---|---|
| Layered controllers | `src/ZanWeb/` | thin composition root, one class per resource |
| Config-driven CRUD screens | `Modules/Crud` | one CrudConf declaration → list/form/save/delete/batch/inline/conf/options; hooks for the rest |
| Default ORM | `Modules/Sys/Model`, `Framework/Core/Db.zan` | `System.Data.Orm` models, config-driven engine |
| Default cache | `Framework/Services/Cache.zan` | in-memory TTL; Redis via `System.Data.Redis` on async path |
| External config | `config/app.json`, `Framework/Core/Cfg.zan` | runtime-loaded, not compiled in |
| High-performance routing | `System.Web.Router` | static-first match + `{param}`, 404/405 |
| Rate limiting | `System.Web.Hooks` | global + per-route fixed windows, 429 |
| Auth / sessions | `WebApp.AuthUser`, `Sessions` | Bearer token or session cookie, `.Auth()` guard |
| Lifecycle hooks | `System.Web.Hooks` | typed delegates, run in order |
| In-memory views | `System.Web.View` | templates loaded ONCE at startup |
| Streaming uploads | `.Upload()` routes | body streamed to disk in 64KB chunks |
| Validation | `System.Web.Validate` | Require/MaxLen/IsInt/OneOf + SafeFileName |
| Scheduled jobs | `src/Feature/JobHost.zan`, `/admin/system/jobs` | in-process scheduler: second-granularity intervals, DB optimistic-lock claim (multi-worker safe), built-in kinds (`ping`, `log.cleanup`), run history with pruning |
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
way to supervise horizontally scaled services. 组合根 boots via
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
them, not just this package.

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

## Monitoring screens (`/admin/monitor/*`)

Four screens read the persisted history (`metrics.db`, `[metrics]` config):
**运行监控** (live SSE snapshot + today's totals + per-route leaderboard),
**历史统计** (time-bucketed windows, per-route ranking, CSV/JSON export),
**错误日志** (persisted, sanitized error events), **SQL 统计**
(`/admin/monitor/sql` — per-day per-normalized-statement aggregates from
`metrics_sql_day`: calls, slow count, avg/max/total time; rank by calls,
total time or slow count; CSV export). Slow SQL and slow requests are also
logged at the `[log].slowSqlMs` / `[log].slowMs` thresholds.

- **P95 is persisted.** `metrics_minute` carries `p95_us` per route per
  minute, computed at flush time from a fixed-bin latency histogram (the same
  bins as the live series, so live and stored P95 read the same scale).
  Percentiles are not additive: multiple flushes/workers merge with MAX, so a
  stored P95 is an upper bound of the true one — good enough to see "this
  route's tail got slower" across hours and restarts, which avg/max cannot.
- **Health probe** `GET /health` — no authentication (LB/watchdog/supervisor
  probes cannot log in). Shallow answer is process facts only (status,
  uptime_ms, requests, pid, worker). `GET /health?deep=1` also runs `SELECT 1`
  on the main pool on the request's existing lease and answers **503** when
  the database is unreachable.
- **Alert bell** — the admin top bar polls `/admin/monitor/alerts` every 30s
  (visible only to accounts granted the monitor screen) and shows a red badge
  when something is wrong. Three signals, evaluated statelessly per poll over
  a 5-minute window: error rate (`[metrics].alertErrPct` %, only once
  `[metrics].alertErrCalls` calls have accumulated — avoids 1-of-2 = 100%
  false alarms at night), worst per-minute P95
  (`[metrics].alertP95Ms`), and workers below `[worker].count`. Set any
  threshold to 0 to disable its check. There is deliberately no alert
  history: history is the error log's and the metrics screens' job; the bell
  only answers "is anything wrong right now".

## 在应用中使用本包（组合根）

应用不复制本包代码：源文件按层 `using`，zanc 自动发现并拉入包源（发现顺序：
应用 `packages/` → 应用 `.zan-packages/` → 编译器旁 `packages/` → 全局包库）。
组合根只声明菜单分组、路由挂点与业务种子：

```zan
using System;
using System.Web;
using ZanWeb;
using ZanWeb.Blog;

class Program {
    static void Main() {
        /* 侧栏分组：URL 首段 → 显示名（管理台外壳与 /api/admin/menu 同读） */
        MenuBuilder.Section("content", "内容管理");
        MenuBuilder.Section("monitor", "运行监控");
        MenuBuilder.Section("system", "系统管理");

        Boot.OnRoutes(Program.RegisterRoutes);
        BlogSeed.Register();   /* 业务种子：内置管理员与演示文章（幂等） */
        int _r = await Boot.Run();
    }
    static void RegisterRoutes(WebApp app) {
        __AttrRoutes.Register(app);   // 属性路由（编译期按单元元数据生成）
    }
}
```

框架升级 = 拉取新包版本，应用代码零改动（非侵入）。

**构建必须把包源与应用源一起显式列出**。属性路由与代码生成器只扫描显式
列出的源文件；`using` 纯拉入路径下，包文件若从未被代码按名引用（控制器恰
是靠属性发现、从不被引用）不会进入编译单元元数据——程序能编译、能启动、
静态资产正常，但控制器路由静默缺失（404）。此编译器缺陷已定位（pull-in
live-name 闭包所致，修复后纯 `using` 引用即可），当前消费配方：

```sh
zanc src/main.zan src/**/*.zan \
     packages/Zan.Mvc/src/ZanWeb/**/*.zan \
     --auto-stdlib -o build/app.exe
```

**随包资产**：`views/` 与 `wwwroot/` 随包分发，但视图引擎运行时只读一个根
（`View.LoadDir`），所以应用发布目录需携带这两个目录与应用自有的
`config/app.json`（配置永远是应用所有，不编译进二进制）——从包复制一次后
归应用所有，框架更新不覆盖。

### 回归验证

包此前以项目形态携带的 144 断言 e2e（`tools/e2e_mvc.py`）随项目形态移除，
作为应用侧回归套件保留在 git 历史中，需要时取回到消费本包的应用。

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

- `GET /` — HTML landing page from the in-memory template cache
- `GET /blog`, `GET /blog/{id}`, `POST /blog/create` — server-rendered blog (author = signed-in account)
- `GET /blog?tag=X` — tag archive (whole-tag match, not substring)
- `GET /rss.xml` — RSS 2.0 feed of the latest 20 published posts
- `GET /sitemap.xml` — home, blog index and every published post
- `GET /robots.txt` — allows the public site, denies `/admin/`, points at the sitemap
- `GET /admin/login`, `POST /admin/login`, `GET /admin/logout` — admin sign-in (seed: `admin` / `admin1234`)
- `GET /admin` — admin dashboard (uptime, requests, CPU/RSS, slow requests, pool/cache)
- `GET /admin/system/users`, `POST /admin/system/users/status`, `POST /admin/system/users/logout` — account administration
- `GET /admin/content/posts`, `POST /admin/content/posts/state` — content administration (drafts included)
- `GET /users` — ORM + cache demo (503 until a database is configured)
- `GET /user/{id}` — route parameter demo (JSON envelope)
- `GET /api/status` — request statistics
- `GET /admin/stats` — runtime diagnostics (CPU, memory, requests, slow requests/queries) — **protect before exposing**
- `POST /api/echo` — rate-limited (100 req/s) echo
- `POST /api/gather` — `[Auth] [Rank(3)] [Lock("user")]` permissioned + locked demo
- `POST /api/login` — `user=admin&pass=admin` issues a session token
- `GET /api/me` — requires `Authorization: Bearer <token>` or session cookie
- `GET /admin/menu` — admin menu built from `[Menu]` routes (`[Auth] [Rank(9)]`)
- `POST /upload` — streaming upload

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

The composition root mounts `wwwroot/` at `/static` before auth and routing:

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

The vendor files are committed on purpose: no CDN at runtime, versions
pinned, works offline. To upgrade, download the new file over the old one and
update the version in this list. There is no Node toolchain and nothing to
compile -- `app.css` is hand-written CSS (variables, grid, a 900px breakpoint
that turns the admin sidebar into a drawer, and tables that become cards under
720px), so a page needs no build step to look right on phone or desktop.

## Admin JSON API

The server exposes clean REST/JSON endpoints for administration:
- **Endpoints** (all `Accept: application/json`, cookie session):
  - `POST /api/auth/login`, `GET /api/auth/me` -- existing auth
  - `GET /api/admin/menu` -- the sidebar as JSON, filtered by the same
    permission resolver the dispatcher uses (MenuBuilder.JsonFor), so the
    client cannot render a link the API would refuse
  - `GET /api/admin/summary` -- dashboard counters
  - `GET /api/admin/posts/list|get`, `POST .../create|update|publish|delete`
    -- article CRUD as JSON (src/Controller/Api/Posts.zan)
- **Optimistic concurrency**: post rows carry `version` (= updatedAt). The
  form sends it back on save; when it no longer matches the stored row the
  update is refused with `409 {"code":"1005"}` instead of silently
  overwriting the other editor's changes. Two people editing the same article
  cannot clobber each other.
- **Auth shape**: a JSON request (Accept: application/json) that is not
  signed in gets `401 {"code":"401"}`, never the sign-in redirect -- the
  dispatcher picks the response shape once (WebApp.WantsPage).

## Template syntax (views/*.html)

```
{{name}}                   escaped variable
{{{name}}}                 raw variable
{{#if name}}...{{/if}}     conditional
{{#each rows}}...{{/each}} loop over ViewData.AddList("rows")
layout.html + {{content}}  page wrapper
```

## Table designer (`/admin/dev/coder`)

Three one-click steps with different costs: **save design** (touches only
`sys_gen_*`), **同步数据库** (DDL — creates the table or adds missing columns),
**生成代码** (writes `.zan` sources, needs a rebuild). The designer only adds;
nothing is dropped or rewritten. Every designed table is immediately usable in
the generic data manager (`/admin/monitor/data?t=...`) before any code
generation.

**生成代码 writes 8 files** — model (`src/Model/Gen/<Entity>.zan`), admin
controller (`src/Controller/Admin/<Entity>.zan`), Dao
(`src/Dao/Gen/<Entity>Dao.zan`), admin list + form views, and a public
front-facing trio: controller (`src/Controller/Front/<Entity>Front.zan`,
anonymous-readable list + detail shaped like `Blog.Posts` — writes stay in the
admin area) plus its list and detail views. Output root follows the
`gen.root` site setting (default `..`, i.e. the sources of the running
server); all directories are created recursively. Preview
(`/admin/dev/coder/preview`) renders the eight sources in tabs before
writing anything. Generated code follows the hand-written module shape
(attribute routing, `AppController` lease, view keys
`<Module>.<Controller>.<Action>`), so it composes with layout, i18n and the
permission index unchanged.

**AI 生成字段** — on the field-design screen, describe the business in one
sentence and the configured AI provider drafts the whole column list (names,
Chinese labels, kinds, widgets, dict/relation suggestions, list/filter/edit
**Inline editing** — on the field-design table the cheap knobs are edited in
place: the list/filter/create/edit/required checkboxes toggle on click, and the
label, kind, widget and sort order are cell-level inputs/selects. Each change
POSTs to `columnquick` (allow-listed fields only, one UPDATE per change) and a
failed save reverts the control and toasts; deep attributes (size, default,
dict, relation) still use the edit dialog. Read-only accounts get disabled
controls.

**AI 生成字段** — on the field-design screen, describe the business in one
operator's real dict-type codes and designed tables → model answers strict
JSON → **every column is re-validated server-side** (name legality, kind/widget
whitelists, relation tables must actually exist, duplicate/id rejection) →
human reviews a checklist and applies only the wanted rows → the browser
round-trip is re-validated by the same sanitizer before insert. AI output is a
draft, never trusted, never auto-saved. Requires `[站点设置 → AI]` enabled;
the endpoint must be one of the allow-listed providers (or local Ollama).

## Wiki knowledge base (`/admin/wiki`)

Department-scoped knowledge bases with Markdown documents, AI curation and
full revision history.

- **Spaces** (`wiki_space`) — one knowledge base per department (bound to
  `sys_department`, or generic with `departmentId = 0`); a space with docs
  refuses deletion.
- **Docs** (`wiki_doc`) — Markdown source stored verbatim, rendered to HTML
  at read time by a small server-side renderer (headings / lists / quotes /
  code fences / **bold** / `code`; everything is HTML-escaped before
  markup is applied, so stored source can never inject HTML). Reading bumps
  the view counter.
- **Revisions** (`wiki_revision`) — every save (human edit, AI organize,
  rollback) snapshots the whole doc and bumps `rev`. History page lists
  snapshots with the operator's note; any revision can be viewed read-only
  or rolled back — rollback itself creates a new revision, history is never
  rewritten.
- **AI 整理** — sends the current body through `Ai.CompleteRaw` with a
  curation system prompt and stores the result as a new revision
  (`note = AI 整理`); the same allow-list / ready-check rules as the table
  designer apply, and people can keep editing or roll the AI version back.

Markdown source must be read with `InRaw` — the default `In`/`InText`
accessors run `Filter.Clean`, which strips CR/LF (header-injection defense)
and would silently flatten multiline text into one line.

## Media library (`/admin/media`)

Streamed file uploads with a deny-by-default extension whitelist.

- **Upload** — the action is marked `[Custom(Upload = true)]`: the whole
  request body streams to disk in 64 KB chunks (memory stays flat regardless
  of size) into a server-named spool file under `uploads/`, so the client's
  file name never touches a path. The real name travels as `?name=`, is
  reduced through `Validator.SafeFileName`, its extension is checked against
  a whitelist (`png/jpg/pdf/docx/zip/mp4/...`; **svg is deliberately
  excluded** — an SVG served same-origin can carry script), and only then is
  the file moved into `wwwroot/media/` and recorded in the `media` table.
  Duplicate names get a timestamp prefix instead of overwriting; rejected
  uploads delete their spool file.
- **Serving** — `StaticFiles.Mount` has a single slot (a second call
  replaces the prefix), so media lives **inside** the static root:
  `wwwroot/media/<file>`, publicly served as `/static/media/<file>`. The
  `uploads/` spool directory is never mounted.
- **Cache note** — `StaticFiles` caches asset bodies per worker after the
  first hit, so a deleted file may still be served from worker memory until
  the next restart. Fine for an internal library; call it out if you build
  user-facing deletion on top.
- **Size cap** — `[server].uploadBodyMB` (default 64) feeds
  `app.MaxUpload()`; over-cap requests are refused with 413 before the body
  is read.
- **Delete** — removes the row and the disk file; the path is rebuilt from
  the stored server-generated file name, never trusted as a full path.
- The admin page's 上传文件 button posts the raw bytes with
  `?name=` (`data-upload` in `admin.js`) — no multipart machinery needed.

## Public site discoverability (RSS / sitemap / robots / tags)

Three machine-facing surfaces served by `Blog/Posts.zan` next to the human
pages, all built from published posts only:

- **`GET /rss.xml`** — RSS 2.0, latest 20 published posts (`application/rss+xml`).
  Item links and GUIDs are absolute; `<pubDate>`/`<lastBuildDate>` are RFC-822
  in the site timezone (`site.timezone`, the same offset the page views use).
  Every text is XML-escaped and control characters are folded to spaces.
- **`GET /sitemap.xml`** — the sitemap-protocol URL set: `/`, `/blog`, and one
  `<url>` per published post with `<lastmod>` from the row's update date.
- **`GET /robots.txt`** — `User-agent: *` / `Allow: /` / `Disallow: /admin/` /
  `Sitemap: <base>/sitemap.xml`. No database involved, so it answers even when
  the DB is down.
- **Absolute base** — `site.url` (`站点地址` in 站点设置) is the source of truth
  when configured (the only reliable choice behind a reverse proxy or custom
  domain); left empty the base falls back to the request's `Host` header,
  restricted to URL-safe characters. Saving the setting takes effect on the
  next request (the settings cache is dropped per worker).
- **Tag archive** — post tags are plain comma text, so a tag link is a
  whole-word match over the split list (`Prose.HasTag`), never a SQL `LIKE`:
  a search for `ar` must not surface the `arc` post. `/blog?tag=X` filters,
  paginates and feeds the pager; tag chips on a post page link into the
  archive.
- **Head metadata** — the public layout renders an `<link rel="alternate">`
  for the feed on every page; a post page overrides `<meta name="description">`
  with its summary (body excerpt as fallback) and emits Open Graph tags
  (`og:title` / `og:description` / `og:url` / `og:image` when a cover exists).

## Multi-language UI (`site.language` / `i18n`)

The admin shell is translatable through a lightweight, Chinese-as-key layer
(`src/Feature/Lang.zan`):

- The UI language is the `site.language` site setting (`zh-CN` default,
  `en-US` shipped). `zh-CN` renders the source strings directly — zero
  overhead, zero behavior change.
- Any other registered language loads a flat `{chinese: translated}` JSON
  pack from `wwwroot/i18n/{lang}.json` at first render; a missing entry
  falls back to the Chinese source, so a partially filled pack can never
  break the screen. Packs ship with `wwwroot` (so `--publish` carries them)
  and adding a language = one JSON file + one entry in `Lang.Known`.
- Covered today: sidebar groups, menu titles, screen headings/titles, and
  the common inline action labels. Views reference the common labels as
  `{{iEdit}}` / `{{iSave}}` / `{{iOps}}` … injected into every page and
  dialog by `AdminController.I18n`. Server messages (toasts, errors) and
  front-site pages stay Chinese in v1 — translate by wrapping the literal
  with `Lang.T(...)` or adding a key as needed.
- Switching the setting takes effect on the next request (settings cache is
  forgotten on save); unknown values fall back to `zh-CN`.

## Inline cell editing (generic, `data-quick`)

Any admin list can opt its table into in-place editing without page-specific
scripts: put `data-quick="<POST url>"` on the `<table>` and mark each editable
control with `data-quick-field="<field>"` and `data-id`. Controls are plain
checkboxes / text / number inputs / selects (select initial value via
`data-value`). `admin.js` (`applyFragmentWidgets`, so panels, fragments and
dialogs all pick it up) delegates `change` to one POST `{id, field, value}` per
edit; a failed save reverts the control and toasts, so the UI never shows a
state the server refused. The `.cell-edit` styles keep the table looking like a
table until a cell is hovered/focused.

What is editable is a **per-resource server allowlist** — each controller
implements a `Quick` action whose branches whitelist exactly the fields it
accepts and update exactly those typed columns (the column name never comes
from the request). Reference implementations: `Coder.ColumnQuick`
(designer columns), `Categories.Quick`, `Dicts.ItemQuick`. Keep out of the
allowlist: id, passwords/tokens, created/updated timestamps, computed columns
(counts), and identity codes that other data references (category `slug`, dict
item `value`) — those stay dialog-edited so the operator sees the warning.
Read-only accounts render plain text instead of controls (`{{#if canUpdate}}`).

## Table component (wwwroot/js/zan-table.js)


`zan-table.js` progressively enhances the admin tables: the server template
keeps emitting a plain `<table class="table">` with static rows, and the
component wires itself onto any table that carries a `data-table` attribute
(wired by `applyFragmentWidgets`, so fragments, dialogs and the first
server-rendered panel all pick it up). No DOM re-rendering: the rows stay the
same nodes, so `data-post`/`data-dialog` delegation and inline `<a>` keep
working untouched.

```html
<table class="table" data-table="sort filter select">   <!-- empty = all -->
  <thead><tr>
    <th>标题</th>                    <!-- sortable, type auto-detected -->
    <th class="num">阅读</th>         <!-- numeric sort -->
    <th class="ops" data-nosort>操作</th>  <!-- ops columns are excluded anyway -->
  </tr></thead>
```

What it adds, all client-side on the current page's data (zero requests):

- **Column sort** — click a header; numeric/date/text kind is detected from
  the first non-empty cell, arrows show the direction.
- **Instant filter** — a 在当前页内筛选 box in the status bar; row
  visibility toggles as you type, with a hit counter.
- **Row selection** — a checkbox column with select-all/indeterminate head;
  selected count, row highlight (`.on`), and a 取消选择 button.
- **Density toggle** — 紧凑/舒适 per table, remembered in localStorage.
- **Column show/hide** — a 列 menu in the status bar toggles each column;
  hidden columns still sort and filter normally. Persisted per table identity
  (`data-table-key` attribute, or `pathname#tableIndex`) in localStorage; the
  last visible column cannot be hidden.
- **Sticky header** — the header row pins to the top of the scroll port while
  the table scrolls (CSS only; an inset shadow replaces the collapsed border
  that would otherwise scroll away).

Cross-page sort/filter belongs to the server (the pager is a link, the query
is a form) — the component deliberately only manages the current page and
resets on panel reload. Column visibility survives reloads; sort/filter state
does not.
