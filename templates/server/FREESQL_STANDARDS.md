# Zan 服务端 FreeSql 现代规范指南

Zan 现已完全对齐 C# FreeSql 的现代 ORM 架构与最佳实践，彻底淘汰早期的弱类型字符串拼表模型（如 `Model.Define` / `ModelRow`）和短生命周期裸连接管理。

---

## 一、核心设计哲学与规范

1. **统一长生命周期门面 `IFreeSql`**
   - 框架层（如 `AppServices`）由连接池统一构建并持有单例 `IFreeSql`；
   - 控制器与服务层统一通过 `Orm()` / `AppServices.Current().Orm()` 获取门面；
   - 杜绝在业务代码中频繁传递、开启裸连接 `IDbConnection`。

2. **强类型通用泛型仓储 `BaseRepository<T>`**
   - 所有业务 DAO 必须继承 `BaseRepository<T>`（如 `class SysUserDao : BaseRepository<SysUser>`）；
   - 开箱即用标准 CRUD：`ById` / `Insert` / `InsertIdentity` / `InsertBatch` / `Update` / `Delete` / `SelectAll` / `Count`；
   - DAO 仅编写特定领域查询与业务过滤逻辑，统一使用 `this.GetExecutor().Select<T>()` / `Insert<T>()` / `Update<T>()` / `Delete<T>()`。

3. **双构造器支持**
   ```zan
   class SysUserDao : BaseRepository<SysUser> {
       SysUserDao(IFreeSql fsql) : base(fsql) {}
       SysUserDao(IDbConnection db) : base(FreeSql.FromConnection(db)) {}
   }
   ```
   同时兼容依赖注入现代 `IFreeSql` 与遗留单元调用。

4. **CodeFirst 结构同步**
   - 使用 `fsql.CodeFirst().SyncStructure<T>()` 进行表结构幂等同步，替代旧版硬编码 DDL。

5. **事务与工作单元 `IUnitOfWork`**
   - 涉及多表事务操作时，统一通过 `fsql.CreateUnitOfWork()`：
   ```zan
   using (IUnitOfWork uow = fsql.CreateUnitOfWork()) {
       userRepo.Attach(uow);
       roleRepo.Attach(uow);
       // 事务操作...
       uow.Commit();
   }
   userRepo.Detach();
   roleRepo.Detach();
   ```

---

## 二、示范代码

完整强类型实战范式请参考：
- `examples/db/orm_model.zan`：FreeSql 链式查询、更新、增删与事务范例。
- `examples/db/postgres_crud.zan`：PostgreSQL 引擎上的强类型 FreeSql 实践。
- `tests/conformance/orm_freesql.zan`：全功能覆盖与回归测试。
