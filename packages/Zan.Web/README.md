# Zan.Web

企业级 Web 与后台管理系统开发框架（Zan 原生驱动）。

---

## 核心架构原则

1. **全模块化架构（收拢至 `Modules/`）**：
   - 基础设施层独立于 `Core/`、`Protocol/`、`Cache/`。
   - 业务与系统功能全模块化收拢至 `Modules/`：`Modules/System`、`Modules/Monitor`、`Modules/Dev`。
   - 目录 = 命名空间 = 层，严格遵守命名同构（如 `Zan.Web.Modules.System.Model` 严格对应 `Modules/System/Model/`）。
2. **严格 1:1:1 原生结构**：
   - 1 物理表 = 1 原生 Model 实体 = 1 领域 Dao 网关 = 1 领域 Controller。
   - 表名与实体类名严格同构（如 `sys_user` -> `SysUser` -> `SysUserDao` -> `SysUserController`）。
3. **零手拼 SQL**：全链路采用 Zan 原生强类型 ORM 流式查询（`Select<T>()` / `Insert<T>()` / `Update<T>()` / `Delete<T>()`），严禁手拼 SQL 字符串。
4. **代码生成驱动（Modules/Dev）**：
   - 依托 `CurdController` 与 `CodeGenerator` 读取表结构与元数据配置，一键产出 1:1:1 的 Model、Dao、Controller 原生代码，完美对齐 `adminUI`（`<ZanTable>` 与 `CurdForm.vue`）。
5. **完善的服务端统计基座（Modules/Monitor）**：
   - 全链路自动性能打点（`WebController.OnBefore/OnAfter` + `Stopwatch.GetMilliseconds`）。
   - 实时内存聚合器（`ServerStats`）：QPS、平均耗时、成功率、路由级指标、最近慢请求采样队列（>300ms）。
   - 持久化时序统计（`sys_api_stat` 表 + `SysApiStatDao`）。
   - 统计端点：`/api/stat/summary`、`/api/stat/apis`、`/api/stat/slow`、`/api/stat/daily`。
6. **完整系统管理基座（Modules/System）**：
   - **用户体系**：`SysUser`、`SysUserDao`（MD5+加盐防彩虹表哈希）、`SysUserController`。
   - **RBAC 权限与动态路由**：`SysRole`、`SysMenu`、`SysRoleMenu`、`SysRoleController`、`SysMenuController` 与 `AuthManager`，支持超级管理员免检与动态组装 `adminUI` 路由树（`RouteNode`）。
   - **数据字典**：`SysDictType`、`SysDictItem`、`SysDictTypeDao`、`SysDictItemDao`、`DictController`。
   - **系统配置**：`SysConfig`、`SysConfigDao`、`ConfigController`。
   - **操作与审计日志**：`SysLog`、`SysLogDao`、`LogController`。
7. **多语言（I18n）**：原生请求级多语言检测（Query / Cookie / Accept-Language）与字典降级。
8. **三级缓存基线（Cache）**：
   - 瞬时状态与高频字典：进程内 `MemoryCache`。
   - 分布式热缓存：单实例轻量 `RedisCache`，杜绝无谓的集群开销。
   - 统一门面：`Cache`。

---

## 模块化包目录结构

```
packages/Zan.Web/
├── zan.pkg                        # 包元数据声明
├── README.md                      # 框架架构与使用规范
└── src/
    └── Zan/
        └── Web/
            ├── Core/                      # 基础核心 (Zan.Web.Core)
            │   ├── Db.zan                 # 全局连接与数据库上下文网关
            │   ├── DataScope.zan          # 多维度行级数据权限隔离 (All / ByDepts / Self)
            │   ├── WebBoot.zan            # 一键引导启动器（CORS/指标/RBAC/开箱即用装配）
            │   └── WebController.zan      # Web 控制器基类（租借事务、上下文注入、全链路打点）
            ├── Seed/                      # 种子与表结构迁移 (Zan.Web.Seed)
            │   └── SysSeed.zan            # 核心表 DDL 自动建表与超级管理员/默认数据播种
            ├── Protocol/                  # Web 与管理端通信协议契约 (Zan.Web.Protocol)
            │   ├── TableConfig.zan        # TableConfig / TableColumn / TableFilter / Tools
            │   ├── PostData.zan           # PostData / FieldPostData / ResponseList
            │   ├── Response.zan           # 统一 Response<T> 外壳
            │   └── I18n.zan               # 国际化与多语言引擎
            ├── Cache/                     # 缓存层 (Zan.Web.Cache)
            │   └── Cache.zan              # Cache / MemoryCache / RedisCache
            └── Modules/                   # 业务与系统功能全模块化收拢
                ├── System/                # 系统管理模块 (Zan.Web.Modules.System)
                │   ├── Model/             # 1:1 数据实体
                │   │   ├── SysUser.zan
                │   │   ├── SysRole.zan
                │   │   ├── SysUserRole.zan
                │   │   ├── SysDepartment.zan
                │   │   ├── SysMenu.zan
                │   │   ├── SysRoleMenu.zan
                │   │   ├── SysDictType.zan
                │   │   ├── SysDictItem.zan
                │   │   ├── SysConfig.zan
                │   │   └── SysLog.zan
                │   ├── Dao/               # 1:1 强类型数据访问网关
                │   │   ├── SysUserDao.zan
                │   │   ├── SysRoleDao.zan
                │   │   ├── SysUserRoleDao.zan
                │   │   ├── SysDepartmentDao.zan
                │   │   ├── SysMenuDao.zan
                │   │   ├── SysRoleMenuDao.zan
                │   │   ├── SysDictTypeDao.zan
                │   │   ├── SysDictItemDao.zan
                │   │   ├── SysConfigDao.zan
                │   │   └── SysLogDao.zan
	                │   ├── Service/           # 认证与路由业务服务
	                │   │   ├── LoginUser.zan
	                │   │   ├── TokenService.zan
	                │   │   ├── RouteNode.zan
	                │   │   ├── AuthManager.zan
	                │   │   └── SysMenuSync.zan    # 路由特性驱动的动态菜单同步器与构建器
	                │   └── Controller/        # 系统管理接口控制器（统配 Conf() 元数据对接 ZanTable）
                │       ├── AuthController.zan
                │       ├── SysUserController.zan
                │       ├── SysDeptController.zan
                │       ├── SysRoleController.zan
                │       ├── SysMenuController.zan
                │       ├── DictController.zan
                │       ├── ConfigController.zan
                │       └── LogController.zan
                ├── Monitor/               # 链路监控与性能度量模块 (Zan.Web.Modules.Monitor)
                │   ├── Model/SysApiStat.zan
                │   ├── Dao/SysApiStatDao.zan
                │   ├── ServerStats.zan    # 内存高频聚合器、慢SQL采样与分钟级时序
                │   └── Controller/StatController.zan # 统计接口与 SSE 实时推流 (/api/monitor/stat/stream)
                └── Dev/                   # 开发辅助与代码生成模块 (Zan.Web.Modules.Dev)
                    ├── Model/SysCurd.zan
                    ├── Dao/SysCurdDao.zan
                    ├── CodeGenerator.zan  # 1:1:1 原生代码产出引擎
                    └── Controller/CurdController.zan # 对接 adminUI 表格设计器
```

---

## 快速上手与开箱即用引导

通过 `WebBoot` 一行代码即可启动完整企业级后台服务：

```zan
using System;
using System.Data;
using Zan.Web.Core;
using Zan.Web.Seed;

class Program {
    static async void Main() {
        // 1. 初始化数据库连接（支持 SQLite / MySQL / PostgreSQL / SQL Server）
        IDbConnection db = ...;

        // 2. 一键引导启动（自动建表、播种默认管理员、挂载 CORS 与监控打点）
        await WebBoot.Run("0.0.0.0", 8080, db);
    }
}
```

默认内置超级管理员凭证：
- **用户名**：`admin`
- **初始密码**：`123456`
- **默认组织**：总公司 (HQ)

内置管理页面与 SPA 挂载：
- **即开即用 Admin 控制台**：浏览器访问 `http://localhost:8080/admin` 即可查看服务运行看板与数据管理。
- **对接 adminUI 静态资源**：通过 `WebBoot.MountAdmin("dist")` 挂载 Vue 3 管理工程。
- **纯配置驱动菜单与表格**：所有控制器通过 `[Custom(IsMenu = true)]` 与 `[Description]` 自动生成路由菜单，通过 `Conf()` 接口向 `<ZanTable>` 输出完整 `TableConfig` 列声明与搜索项，无需手写页面与 SQL。


