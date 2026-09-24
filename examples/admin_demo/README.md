# Zan.Mvc.Admin 生产级通用管理中台演示工程

本示例演示了如何基于独立的底层架构包 `packages/Zan.Mvc.Admin`，在业务工程中以**零样板代码、纯配置规格驱动、物理完全解耦**的方式快速开发一套企业级后台管理系统。

---

## 核心特性演示

1. **一键无缝挂载 (`AdminApp.UseAdmin`)**
   - 业务项目仅需在入口函数调用 `AdminApp.UseAdmin(app, db);`；
   - 自动挂载安装守卫网关、静态资产、全局 RBAC 权限中枢、5 级 DataScope 数据隔离及全链路 APM 监控。

2. **业务 CRUD 极速落地 (`CrudController<T>`)**
   - 示例中创建了「电商商城 -> 商品管理 (`ProductController`)」；
   - 业务控制器无需手写一行 HTML/JS 前端代码，只需声明 `TableConfig` 列规格与 `FormView` 表单规则；
   - 开箱即用获得：分页检索、动态搜索、行内单元格编辑、新增/编辑弹窗抽屉、批量删除、带 BOM 的 CSV 导出、原生 OpenXML `.xlsx` 流式导出。

3. **内置系统管理能力**
   - **工作台仪表盘**：实时展示服务器性能指标、QPS、慢查询日志及错误链路汇流；
   - **组织部门管理**：支持多级部门树形网格（Tree Grid），提供层级自动缩进、Caret Toggle 折叠展开与一键全部展开/折叠；
   - **系统用户管理 / 角色管理**：支持 5 级数据权限模型（全部/本部门/本部门及下级/仅本人/自定义）；
   - **代码生成器**：支持逆向工程跨方言（SQLite/MySQL/PostgreSQL）自动探测表结构并生成 Entity/DAO/Controller。

---

## 快速编译与运行

在仓库根目录下执行编译命令：

```bash
build/zanc.exe examples/admin_demo/src/main.zan $(find examples/admin_demo/src packages/Zan.Mvc.Admin/src -type f -name "*.zan") --auto-stdlib -o _scratch/admin_demo.exe
```

启动演示后台服务：

```bash
_scratch/admin_demo.exe
```

服务就绪后，在浏览器访问：
- **后台地址**：[http://127.0.0.1:8888/admin](http://127.0.0.1:8888/admin)
- **初始超级管理员账号**：`admin`
- **初始超级管理员密码**：`admin123`
