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
using Zan.Mvc.Admin.Attributes;
using Zan.Mvc.Admin.Security;
using Zan.Mvc.Admin.Data;
using Zan.Mvc.Admin.Web;
using Zan.Mvc.Admin.UI;
```

### 2. 声明式编写控制器
```zan
namespace App.Controller.Admin.Erp;

using Zan.Mvc.Admin.Attributes;
using Zan.Mvc.Admin.Web;

[AdminModule(Key = "erp", Title = "供应链管理", Icon = "truck", Order = 10)]
public class PurchaseController : BaseAdminController
{
    [AdminAction(Key = "purchase.list", Title = "采购单列表", Route = "/admin/erp/purchase", IsMenu = true)]
    public IActionResult Index()
    {
        // 业务逻辑
        return View();
    }
}
```
