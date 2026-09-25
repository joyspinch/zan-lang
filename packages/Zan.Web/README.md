# Zan.Web

企业级 Web 与后台管理系统开发框架（Zan 原生驱动）。

---

## 核心架构原则

1. **严格 1:1:1 结构**：1 物理表 = 1 原生 Model 实体 = 1 领域 Dao 网关。
2. **零手拼 SQL**：全链路采用 Zan 原生强类型 ORM 流式查询（`Select<T>()` / `Insert<T>()` / `Update<T>()` / `Delete<T>()`），严禁拼接 SQL 字符串。
3. **极简控制器与零连接样板**：控制器直接通过 `this.dao` 访问领域数据网关，连接生命周期与请求上下文底层全托管，杜绝 C# 繁琐的注入与连接租约胶水代码。
4. **代码生成驱动（CurdController）**：
   - 彻底摒弃运行期动态反射或厚重的 C# 泛型仓储。
   - 依托 `CurdController`（代码生成器）读取表结构与元数据配置，一键产出 1:1:1 的 Model、Dao、Controller 原生代码，完美对齐 `adminUI`（`<ZanTable>` 与 `CurdForm.vue`）。
5. **完善的服务端统计基座**：
   - 全链路自动性能打点（`WebController.OnBefore/OnAfter` + `Stopwatch.GetMilliseconds`）。
   - 实时内存聚合器（`ServerStats`）：QPS、平均耗时、成功率、路由级指标、最近慢请求采样队列（>300ms）。
   - 持久化时序统计（`sys_api_stat` 表 + `SysApiStatDao`）。
   - 统计端点：`/api/stat/summary`、`/api/stat/apis`、`/api/stat/slow`、`/api/stat/daily`。
6. **用户与 RBAC 权限基座**：
   - 用户体系：`SysUser`、`SysUserDao`（MD5+加盐防彩虹表哈希）、`LoginUser` 与 `TokenService`。
   - 权限与路由：`SysRole`、`SysMenu`、`SysRoleMenu` 与 `AuthManager`，支持超级管理员免检与动态组装 `adminUI` 路由树（`RouteNode`）。
7. **多语言（I18n）**：原生请求级多语言检测（Query / Cookie / Accept-Language）与字典降级。
8. **三级缓存基线**：
   - 瞬时状态与高频字典：进程内 `MemoryCache` / `SharedTable`。
   - 分布式热缓存：单实例轻量 `RedisCache`，杜绝无谓的集群开销。

---

## 包目录结构

```
packages/Zan.Web/
├── zan.pkg                        # 包元数据声明
├── README.md                      # 框架架构与使用规范
└── src/
    └── Zan/
        └── Web/
            ├── Protocol/                  # Web 与管理端通信协议契约 (Zan.Web.Protocol)
            │   ├── TableConfig.zan        # TableConfig / TableColumn / TableFilter / Tools
            │   ├── PostData.zan           # PostData / FieldPostData / ResponseList
            │   ├── Response.zan           # 统一 Response<T> 外壳
            │   └── I18n.zan               # 国际化与多语言引擎
            ├── Core/                      # 基础核心 (Zan.Web.Core)
            │   ├── Db.zan                 # 全局连接与数据库上下文网关
            │   └── WebController.zan      # Web 控制器基类（JSON 序列化、参数解析、耗时打点）
            ├── User/                      # 用户与认证体系 (Zan.Web.User)
            │   ├── Model/SysUser.zan      # sys_user 用户实体
            │   ├── Dao/SysUserDao.zan     # 用户数据访问与密码哈希校验
            │   ├── LoginUser.zan          # 会话用户信息载荷（对齐 adminUI）
            │   ├── TokenService.zan       # AccessToken 签发、提取与缓存网关
            │   └── Controller/AuthController.zan # 登录、注销、个人信息与动态菜单端点
            ├── Auth/                      # RBAC 权限与动态路由 (Zan.Web.Auth)
            │   ├── Model/SysRole.zan      # sys_role 角色实体
            │   ├── Dao/SysRoleDao.zan     # 角色数据访问
            │   ├── Model/SysUserRole.zan  # sys_user_role 用户-角色关联实体
            │   ├── Dao/SysUserRoleDao.zan # 用户-角色映射网关
            │   ├── Model/SysMenu.zan      # sys_menu 菜单路由实体
            │   ├── Dao/SysMenuDao.zan     # 菜单与权限节点数据访问
            │   ├── Model/SysRoleMenu.zan  # sys_role_menu 角色-菜单关联实体
            │   ├── Dao/SysRoleMenuDao.zan # 角色-菜单关联网关
            │   ├── RouteNode.zan          # 动态路由树节点（契合 adminUI）
            │   └── AuthManager.zan        # 鉴权决策与路由树生成器
            ├── Stat/                      # 服务端统计与链路性能监控 (Zan.Web.Stat)
            │   ├── Model/SysApiStat.zan   # sys_api_stat 接口耗时与调用量实体
            │   ├── Dao/SysApiStatDao.zan  # 统计时序聚合与查询网关
            │   ├── ServerStats.zan        # 实时内存指标聚合器与慢请求采样队列
            │   └── Controller/StatController.zan # /api/stat/summary、apis、slow 监控端点
            ├── Generator/                 # 代码生成引擎 (Zan.Web.Generator)
            │   ├── Model/SysCurd.zan      # sys_curd 生成器配置实体
            │   ├── Dao/SysCurdDao.zan     # sys_curd 原生数据网关
            │   ├── CodeGenerator.zan      # 1:1:1 代码产出引擎
            │   └── CurdController.zan     # 对接 adminUI CurdForm.vue 的生成器控制器
            └── Cache/                     # 缓存层 (Zan.Web.Cache)
                └── Cache.zan              # MemoryCache + 单实例 RedisCache
```
