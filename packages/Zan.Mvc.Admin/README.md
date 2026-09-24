# Zan.Mvc.Admin

企业级非侵入式后台管理基座包（Zan MVC Platform Kit）

## 核心设计特性

1. **零业务侵入（Zero-Intrusion）**：
   - 业务控制器无需手动注册菜单或编写胶水代码，使用声明式特性标注；
   - 实体纯粹 POCO 规范，主表窄定长，扩展字段采用 1:1 外键 Profile 模式；
   - 升级基座只需更新包版本重新编译，业务代码零冲突。
2. **O(1) 权限中枢与 5 级数据范围（RBAC + DataScope）**：
   - 紧凑 4 字节对齐 `PermNode`，角色权限位图（Bitmask）高速单指令判权；
   - 杜绝低效的并行 List 与字符串线性循环；
   - 5 级数据范围管控：全部数据、本部门、本部门及下级、仅本人、自定义部门；
   - DAO 自动织入数据隔离切面，业务查询零心智负担。
3. **首运行开箱安装向导（First-run Installer）**：
   - 环境探针自检（扩展、目录写权限）；
   - 数据库连通性测试与事务级 Schema 自动迁移；
   - 初始化超级管理员密码（安全哈希）；
   - `install.lock` 物理锁文件熔断机制，安装后永久封禁安装路由。
4. **统一四层架构（Unified 4-Layer Architecture）**：
   - 遵循 `URL = 目录 = 类名 = 表名` 命名同构；
   - `Model/` 实体统一收拢；`Dao/` 严格 1:1 镜像同构；`Controller/` 统一接入；`Framework/` 基座中枢。
5. **前端微内核模块化与布局原语**：
   - 肢解单体巨石 JS 为 `tabs.js`、`layer.js`、`form.js`、`event-bus.js`；
   - 统一声明式语义化指令（`data-action="dialog"`、`data-action="post"`）；
   - 沉淀 `DataGrid` 与 `FormView` 布局原语，消灭每个业务页面拷贝 HTML 的低级重复。

## 快速使用

### 1. 业务工程引入依赖
在工程中直接使用命名空间：
```zan
using Zan.Mvc.Admin;
using Zan.Mvc.Admin.Attributes;
using Zan.Mvc.Admin.Security;
using Zan.Mvc.Admin.Data;
using Zan.Mvc.Admin.Web;
using Zan.Mvc.Admin.UI;
```

### 2. 启动与一键装配
在 Web 入口程序中调用 `AdminApp.UseAdmin(app, db)` 即可一键挂载全量管理中台基座设施：
```zan
public class Program {
    static void Main() {
        WebApp app = new WebApp();
        IDbConnection db = AdminApp.OpenDb("data/app.db");
        AdminApp.UseAdmin(app, db);
        app.Listen(8080);
    }
}
```

### 3. 基于 CRUD 微内核快速开发业务管理模块
继承通用 `CrudController<TEntity>`，声明表格规格 `BuildTableConfig` 与表单规格 `BuildFormView`，即可自动获得完整的增删改查、即时修改与安全导出功能：
```zan
[Description("客户管理")]
[AdminAction(Icon = "team", Order = 10)]
public class CustomerController : CrudController<Customer> {
    public CustomerController(IDbConnection db = null) : base(db) {}

    protected override TableConfig BuildTableConfig() {
        TableConfig cfg = new TableConfig();
        cfg.Conf.SetKey("id", "客户管理");
        cfg.AddColumn("id", "ID", "number", 80, true, false)
           .AddColumn("name", "客户姓名", "input", 160, true, true)
           .AddColumn("phone", "联系电话", "text", 140, false, false)
           .AddFilter("name", "客户姓名", "Input", "请输入姓名搜索")
           .AddTool("添加客户", "Add", "客户", "Primary", false, "600px", "400px")
           .AddTool("批量删除", "Delete", "客户", "Error", true, "400px", "260px")
           .AddTool("导出数据", "Export", "客户", "Default", false, "", "");
        return cfg;
    }

    protected override FormView BuildFormView() {
        FormView form = new FormView("客户信息");
        form.AddField("name", "客户姓名", "input", true, "请输入姓名", 12)
            .AddField("phone", "联系电话", "input", true, "请输入电话", 12);
        return form;
    }

    // 重写 OnExportRows 钩子提供导出数据源
    protected override List<Dict<string, string>> OnExportRows() {
        // ... 返回待导出数据
        return new List<Dict<string, string>>();
    }
}
```

## 企业级安全防御架构

1. **防刷与高频限流 (`AdminRateLimiter`)**：
   - 纯内存滑动窗口限流器，单请求零磁盘 IO；
   - 登录接口、批量删除、敏感字段行内修改全量切入，超限返回 `4029`。
2. **CSRF 防御防线 (`CsrfGuard`)**：
   - Double Submit Cookie 与 Header（`X-CSRF-Token`）结合；
   - 恒定时间（Constant-Time）比对函数，防御侧信道时序攻击；
   - 所有非幂等写操作（POST / PUT / DELETE）强制校验，非法请求返回 `4032`。
3. **行内编辑保护 (`FieldSecurityGuard`)**：
   - 严格白名单机制，禁止即时越权修改密码、金额、主键、组织部门等高危列。
4. **5 级数据权限隔离 (`DataScope`)**：
   - 紧凑 4 字节位掩码与预扁平化树路径计算（`tree_path`），无缝实现跨部门与人员数据隔离。

## 生产级 APM 性能监控与运行看板

- 微秒级 SQL 耗时埋点 (`DbTrace`) 与慢查询聚合分析；
- 共享内存滑动窗口高频访问统计；
- 全局异常自动入库持久化与实时健康看板（`/admin/dashboard`）。
