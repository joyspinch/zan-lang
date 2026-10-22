# ZanWeb (Zan.Mvc) 生产级企业通用管理中台演示工程

本示例演示了如何基于统一架构包 `packages/Zan.Mvc`（命名空间 `ZanWeb.*`），在业务工程中以**声明式配置驱动、编译期属性路由、全自动数据迁移与安全守门**的方式快速落地企业级后台管理系统。

---

## 核心架构与特性

1. **统一架构包与依赖注入 (`AppServices` / `DbContext`)**
   - 业务项目引用 `packages/Zan.Mvc`，通过 `AppServices.Use` 注入连接池与缓存上下文；
   - 框架统一收口：数据库连接池管理、Redis/本地内存缓存、全链路指标 APM、自动化 WAL 模式与防并发锁表机制。

2. **声明式 CRUD 管理屏 (`CrudScreenController` + `CrudConf`)**
   - 示例包含「电商商城 -> 商品管理 (`ProductController`)」；
   - 继承自现代通用 `CrudScreenController`，重写 `Def()` 给出 `CrudConf` 声明；
   - 一行代码完成列宽、必填、行内即时编辑、文本/下拉字典、搜索栏筛选、工具栏与唯一性校验白名单派生；
   - 自动获得：分页列表、行级数据回显、弹窗表单编辑、批量启停与删除、行内即时修改。

3. **编译期属性路由与全自动安全守门 (`__AttrRoutes` + `Boot.AdminGate`)**
   - 编译期零反射扫描控制器上的 `[Route]`、`[HttpGet]`、`[HttpPost]`、`[Custom]` 与 `[Description]` 属性并自动合成路由注册；
   - 运行时 `Boot.AdminGate` 强制守门，彻底封死匿名越权访问，仅放行明确声明的登录认证端点。

4. **自动化数据迁移与业务种子注入 (`Schema.Ensure` + `Schema.OnSeed`)**
   - 启动时自动同步管理基座物理表与业务实体表 (`SyncStructureAllAsync`)；
   - 通过 `Schema.OnSeed` 注册业务演示数据种子，重复启动自动幂等保护。

5. **资源级数据域（DataGrant）**
   - 商品按店铺归属（`products.shopId`），商品屏只声明 `.Scope("shop", "shopId")`；
   - 运营账号被授予 1~N 个店铺后，列表、计数、详情、保存、批量、关联选项
     六处自动按可见店铺过滤，屏体零改动；
   - 演示账号 `ops_a`（店铺 1、3）看到 4 条商品，`ops_b`（店铺 2）看到 2 条；
     打开或写入其他店铺的数据分别答 404 / 403；
   - 授权入口：账号管理 →「数据范围」（按资源域勾选，空钩即收回）。

---

## 快速编译与运行

在仓库根目录下执行编译命令：

```bash
build/zanc.exe examples/admin_demo/src/main.zan $(find examples/admin_demo/src packages/Zan.Mvc/src -name "*.zan") --auto-stdlib -o _scratch/admin_demo.exe
```

启动演示后台服务：

```bash
_scratch/admin_demo.exe
```

服务就绪后，在浏览器访问：
- **后台地址**：[http://127.0.0.1:8888/admin](http://127.0.0.1:8888/admin)
- **初始超级管理员账号**：`admin`
- **初始超级管理员密码**：`admin12345678`
- **运营账号**：`ops_a`（店铺 1、3）/ `ops_b`（店铺 2），密码 `ops12345678`
- **业务演示模块**：电商商城 -> 商品管理 (`/admin/shop/product`)、店铺管理 (`/admin/shop/shops`)

用两个运营账号分别登录商品管理，对比可见行数与新建表单里的店铺下拉——
这就是数据域生效的样子。
