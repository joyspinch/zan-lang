# Zan MVC Admin 业务演示项目 (ERP 订单供应链)

本示例演示基于企业级独立核心包 `packages/Zan.Mvc.Admin` 构建非侵入式业务后台管理系统。

---

## 架构亮点

1. **零侵入包依赖**：
   - 业务代码仅需 `using Zan.Mvc.Admin;` 与命名空间，完全不修改底层包源码；
   - 底层包升级时业务无感知，实现业务中台与框架的彻底物理隔离。

2. **泛型仓储零样板 CRUD (`BaseDao<T>`)**：
   - 业务实体 DAO 仅需继承 `BaseDao<ErpOrder>`：
     ```zan
     public class ErpOrderDao : BaseDao<ErpOrder> {
         public ErpOrderDao(IDbConnection db) : base(db) {}
         public static ErpOrderDao Of(IDbConnection db) { return new ErpOrderDao(db); }
     }
     ```
   - 自动获得 22+ 项标准同步与异步 CRUD 方法（`FindById`、`FindAll`、`Page`、`Count`、`Insert`、`Update`、`DeleteById` 等），无需重复编写样板代码。

3. **5 级 DataScope 部门与人员数据权限隔离**：
   - 控制器直接通过 `this.CurrentDataScope.Covers(deptId, ownerId)` 实施多租户/多部门数据隔离过滤；
   - 支持全部、本部门、本部门及下属部门树、仅本人、自定义授权等 5 级灵活配置。

4. **声明式自动化注册**：
   - 使用 `[AdminModule]` 与 `[AdminAction]` 声明业务模块与菜单，启动期自动扫描并注入权限中枢；
   - 告别手写菜单列表、硬编码路由的繁琐操作。

5. **开箱即用初始化向导**：
   - 系统首次启动自动引导至 `/install` 完成探针检测、SQLite 库表迁移与超级管理员初始化；
   - 安装完成后自动生成 `install.lock` 物理锁，实施永久熔断防护。

---

## 编译与运行

```bash
# 编译业务演示程序
build/zanc packages/Zan.Mvc.Admin/src/Zan/Mvc/Admin/Attributes/*.zan \
           packages/Zan.Mvc.Admin/src/Zan/Mvc/Admin/Model/*.zan \
           packages/Zan.Mvc.Admin/src/Zan/Mvc/Admin/Security/*.zan \
           packages/Zan.Mvc.Admin/src/Zan/Mvc/Admin/Data/*.zan \
           packages/Zan.Mvc.Admin/src/Zan/Mvc/Admin/Dao/*.zan \
           packages/Zan.Mvc.Admin/src/Zan/Mvc/Admin/Installer/*.zan \
           packages/Zan.Mvc.Admin/src/Zan/Mvc/Admin/UI/*.zan \
           packages/Zan.Mvc.Admin/src/Zan/Mvc/Admin/Web/*.zan \
           packages/Zan.Mvc.Admin/src/Zan/Mvc/Admin/AdminApp.zan \
           examples/mvc_admin_demo/src/Model/*.zan \
           examples/mvc_admin_demo/src/Dao/*.zan \
           examples/mvc_admin_demo/src/Controller/*.zan \
           examples/mvc_admin_demo/src/App.zan \
           --auto-stdlib -o _scratch/mvc_admin_demo.exe

# 启动运行
_scratch/mvc_admin_demo.exe
```

打开浏览器访问：
- 首次安装：[http://127.0.0.1:8088/install](http://127.0.0.1:8088/install)
- 后台登录：[http://127.0.0.1:8088/admin/login](http://127.0.0.1:8088/admin/login)
- 订单管理：[http://127.0.0.1:8088/admin/erp/order](http://127.0.0.1:8088/admin/erp/order)
