# Zan.Web — ZanWeb 企业级 Web 框架包

`Zan.Web` 是 server-mvc 模板的框架底座：分层 MVC、RBAC 鉴权、ORM/缓存
上下文、指标、任务宿主、代码生成器。**框架以包引用，业务在应用侧**——
框架升级（拉新包版本）不动应用一行代码。

包内代码与 server-mvc 模板同源（真迁移），经模板 e2e（121 断言）全量验证。

## 引用方式

应用声明 `using ZanWeb;` 即自动拉入（包发现顺序：项目 `packages/` →
项目 `.zan-packages/` → exe 旁 `../packages/` / `packages/` → 全局库）。
`zan.pkg` 是包清单，版本随包一起演进。

```zan
using System;
using System.Web;
using ZanWeb;

class Program {
    static void Main() {
        Boot.OnRoutes(Program.RegisterRoutes);
        int _r = await Boot.Run();
    }
    static void RegisterRoutes(WebApp app) {
        __AttrRoutes.Register(app);   // 属性路由（编译期生成）
    }
}
```

## 包/应用边界（谁拥有什么）

| 归属 | 内容 |
|------|------|
| **包**（本目录） | 框架核心 `ZanWeb.*`、系统模型 `ZanWeb.Model.Sys`（19 实体）、系统 DAO `ZanWeb.Dao.Sys`（19 表）、三个控制器基类（App/Api/Admin） |
| **应用** | 业务控制器、业务模型/DAO（如 Blog）、`views/`、`wwwroot/`、`config/app.json`、`main.zan` 组合根、业务种子（经 `Schema.OnSeed` 挂点注册） |

原则：模型与 DAO 都在应用里，框架不预设任何业务；框架只提供挂点。

## 应用侧三个挂点

- **`Boot.OnRoutes(RouteFn)`** — 路由注册：组合根里调
  `__AttrRoutes.Register(app)`（GenRoute 编译期从控制器属性合成）。
- **`Schema.OnSeed(SeedFn)`** — 业务种子：框架启动应用 schema 后回调
  `async bool fn(IDbConnection db)`，业务表的初始化数据在这里写。
- **`MenuBuilder.Section(...)`** — 后台菜单分区：菜单由路由属性
  （`[AdminModule]`/`IsMenu`/`Icon`）声明式生成，组合根只声明分区顺序，
  无硬编码菜单。

`{{NAME}}` 等模板占位符只存在于模板侧；包内代码不含任何占位符
（`Cfg.Server.name` 默认 `"app"`，由外部 `config/app.json` 覆盖）。

## 命名空间地图

```
ZanWeb                 框架核心：Boot/Cfg/Db/DbContext/CacheContext/AppServices/
                       Schema/Auth/Perm/DataScope/Settings/Keys/Lang/Fmt/Metrics/
                       Mailer/VerifyCode/Prose/Gen/JobHost/ClusterBus/Presence/Ai…
ZanWeb.Model.Sys       系统实体（sys_user / sys_role / sys_operation_log / …）
ZanWeb.Dao.Sys         系统 DAO（每表一个，全部查询与写入口）
ZanWeb.Blog 等业务命名空间  在应用侧，不在包内
```

## 分层约定（上下文自动处理，业务不传连接）

- **请求连接由框架供给**：`AppController` 在动作运行前借出请求租约，
  `this.<Entity>` 表访问器（`this.User`、`this.Post`…）经编译期改写自动
  运行在该租约上——简单查询零仪式感，字段名编译期校验。
- **DAO 收请求作用域，不收连接**：`new UserDao(this)`——DAO 经
  `host.__Conn()` 解析请求租约，事务与只读从库口径与访问器完全一致；
  错传连接在编译期即被拒绝（`DbContext` 不是 `AppController`）。
- **非请求场景显式给连接**：种子（`Schema.OnSeed`）、后台任务自己借还，
  `new UserDao(db)`——借还协议只在框架层出现。
- `AppController.Db()` 是 **private**：`DbContext` 是池借还协议，控制器不
  触碰（历史上曾 protected 被当连接传进 DAO，运行时才炸）。
- DAO 每表一个、同 action 多 DAO 共享同一租约与事务；缓存键属于数据层，
  留在 DAO 里，不散落到控制器。

## 列表页声明式基座（ListPage / FormPage）

后台通用屏 = **列表**（ListPage）+ **表单**（FormPage），各一份声明驱动
渲染与取数/校验，UI 与 SQL 不再各写一遍：

```zan
ListPage lp = ListPage.Of()
    .Text("账号 / 姓名 / 邮箱", "kw", "t.username|t.nickname|t.email", "搜索…")
    .Select("状态", "status", "t.status", "1=启用,0=禁用")
    .Time("创建日期", "createdAt", "t.createdAt")
    .Pick("分类", "categoryId", "t.categoryId", "/admin/posts/cats")
    .TbarAdd("新增", "/admin/posts/form").TbarReload().TbarSep()
    .TbarEnable("/admin/posts/batchstatus").TbarDelete("/admin/posts/batchdelete");
List<ListCond> conds = lp.Collect(this);   /* 只收声明过的名字，空值跳过 */
lp.Search(this, d, "/admin/posts");        /* d.searchHtml：回显 + 渲染 */
lp.Toolbar(d);                             /* d.toolbarHtml：批量带确认 */
d.Set("prevUrl", lp.Query(conds, q.Page() - 1, q.Limit()));  /* 翻页保留全部筛选 */
```

```zan
/* 一份字段声明：Form 渲染与 Save 校验同源；新增/编辑共用。 */
static FormPage PostForm(List<StrMap> catRows) {
    return FormPage.Of("/admin/posts/save").Wide()
        .Text("标题", "title").Req().Max(120).Span()
        .SelectList("分类", "categoryId", catRows).Blank("未分类")
        .Select("状态", "published", "1=发布,0=草稿").Val("1")
        .Area("正文", "body").Req().Rows(14).Span();
}

async void Form() {            /* 新增/编辑：声明 → 渲染 → 共享壳对话框 */
    FormPage fp = PostForm(await LoadCatRows());
    if (id > 0) { fp.Id(...); fp.Field("title").Val(p.title); /* …回显 */ }
    fp.Render(d);
    this.FormDialog(d);        /* views/Admin/_FormDialog.html，零视图文件 */
}

async void Save() {            /* 校验从声明来：required/min/max */
    string invalid = fp.Validate(this);
    if (invalid.Length > 0) { this.Fail(400, "0004", invalid); }
    /* …In/InInt 取值落库（范围/唯一性等业务规则仍归动作） */
}
```

- **列名只来自声明**（`col` 是代码常量），用户输入永远走占位符——DAO 侧
  `ApplyConds(__DbQ_Post sel, List<ListCond> conds)` 把 LIKE/EQ/GE/LE 拼成
  参数化 WHERE（多列 LIKE 用 `|` 分组 OR），SQL 留在数据层。
- **批量端点零容忍**：`Ids()` 收 CSV 主键（上限 500），任一行越权
  （超管/自身停用）整批拒绝，不做部分成功。
- **远程搜索单选（Pick）**：服务端只渲染隐藏值输入 + 搜索框，选项由
  `?kw=` 端点即时返回（`{code:'0000',data:[{id,label}]}`），回显走
  `?id=N`；选项端点同时服务表单选择器。
- **表单条件可见性**：`OnlyNew`（初始密码）/`OnlyEdit`（编辑提示）按
  主键值判定，渲染与校验同步生效——编辑时初始密码既不渲染也不校验。
  密码类型恒不回显；值回填经 Esc 转义。

## 无侵入更新

框架缺陷修在包里、随包版本走；应用只在组合根（`main.zan`）对接挂点。
包内不出现应用命名空间，应用升级 = 换包 + 编译，业务代码零改动。
