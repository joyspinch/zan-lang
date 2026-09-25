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
5. **多语言（I18n）**：原生请求级多语言检测（Query / Cookie / Accept-Language）与字典降级。
6. **三级缓存基线**：
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
            │   └── WebController.zan      # Web 控制器基类（JSON 序列化、参数解析、多语言）
            ├── Generator/                 # 代码生成引擎 (Zan.Web.Generator)
            │   ├── Model/SysCurd.zan      # sys_curd 生成器配置实体
            │   ├── Dao/SysCurdDao.zan     # sys_curd 原生数据网关
            │   ├── CodeGenerator.zan      # 1:1:1 代码产出引擎
            │   └── CurdController.zan     # 对接 adminUI CurdForm.vue 的生成器控制器
            └── Cache/                     # 缓存层 (Zan.Web.Cache)
                └── Cache.zan              # MemoryCache + 单实例 RedisCache
```
