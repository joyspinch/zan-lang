# ZanWeb.Dao

> 源码: `packages/Zan.Mvc/src/ZanWeb/Modules/Crud/Dao/SysGenColumnDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Crud/Dao/SysGenTableDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysDepartmentDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysDepartmentRoleDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysDictItemDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysDictTypeDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysJobDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysJobLogDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysLoginLogDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysMediaFileDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysOperationLogDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysRoleDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysRoleGrantDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysSiteSettingDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysUserDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysUserRoleDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysWikiDocDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysWikiRevisionDao.zan`, `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Dao/SysWikiSpaceDao.zan`


## SysDepartmentDao (class)

- SysDepartmentDao(IFreeSql fsql):base(fsql)

- SysDepartmentDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysDepartment ById(int id)

- async List<SysDepartment> All()

- async List<SysDepartment> Scoped(bool byDept, List<int> scopedDepts, bool mine)

- async List<SysDepartment> ByParent(int parentId)

- async int Count()

- async int Insert(SysDepartment dept)

- async int UpdateBasic(int id, string name, string code, int parentId, int sortOrder, int status)

- async int Update(SysDepartment dept)

- async int Delete(int id)


## SysDepartmentRoleDao (class)

- SysDepartmentRoleDao(IFreeSql fsql):base(fsql)

- SysDepartmentRoleDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysDepartmentRole ById(int id)

- async List<SysDepartmentRole> ByDepartmentId(int deptId)

- async List<SysDepartmentRole> ByRoleId(int roleId)

- async int Insert(SysDepartmentRole dr)

- async int Delete(int id)

- async int DeleteByDepartmentId(int deptId)


## SysDictItemDao (class)

- SysDictItemDao(IFreeSql fsql):base(fsql)

- SysDictItemDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysDictItem ById(int id)

- async List<SysDictItem> ByTypeCode(string typeCode)

- async List<SysDictItem> ByTypeCodeActive(string typeCode)

- async int Insert(SysDictItem item)

- async int Update(SysDictItem item)

- async int UpdateFields(int id, string typeCode, string label, string value, int sortOrder, int status, long updatedAt)

- async int UpdateLabel(int id, string label, long updatedAt)

- async int UpdateSortOrder(int id, int sortOrder, long updatedAt)

- async int UpdateStatus(int id, int status, long updatedAt)

- async int Delete(int id)

- async int DeleteByTypeCode(string typeCode)


## SysDictTypeDao (class)

- SysDictTypeDao(IFreeSql fsql):base(fsql)

- SysDictTypeDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysDictType ById(int id)

- async SysDictType ByCode(string code)

- async List<SysDictType> All()

- async List<SysDictType> AllActive()

- async int UpdateFields(int id, string code, string name, string description, int status, long updatedAt)

- async int Insert(SysDictType dt)

- async int Update(SysDictType dt)

- async int Delete(int id)


## SysGenColumnDao (class)

- SysGenColumnDao(IFreeSql fsql):base(fsql)

- SysGenColumnDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async List<SysGenColumn> ByTableId(int tableId)

- async int Insert(SysGenColumn col)

- async int DeleteByTableId(int tableId)


## SysGenTableDao (class)

- SysGenTableDao(IFreeSql fsql):base(fsql)

- SysGenTableDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysGenTable ById(int id)

- async SysGenTable ByTableName(string tableName)

- async List<SysGenTable> All()

- async List<SysGenTable> AllMigrated()

- async int Insert(SysGenTable table)

- async int Update(SysGenTable table)

- async int Delete(int id)


## SysJobDao (class)

- SysJobDao(IFreeSql fsql):base(fsql)

- SysJobDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysJob ById(int id)

- async List<SysJob> All()

- async int Insert(SysJob job)

- async int Update(SysJob job)

- async int SetEnabled(int id, int enabled)

- async int Delete(int id)


## SysJobLogDao (class)

- SysJobLogDao(IFreeSql fsql):base(fsql)

- SysJobLogDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async List<SysJobLog> ByJobId(int jobId, int limit)

- async int Insert(SysJobLog log)


## SysLoginLogDao (class)

- SysLoginLogDao(IFreeSql fsql):base(fsql)

- SysLoginLogDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysLoginLog ById(int id)

- async List<SysLoginLog> Latest(int limit)

- async int Count()

- async int Insert(SysLoginLog log)

- async int CountScoped(string kw, bool byActor, List<string> actors, bool nothing)

- async List<SysLoginLog> PagedScoped(string kw, bool byActor, List<string> actors, bool nothing, int skip, int limit)

- async int ClearAll()


## SysMediaFileDao (class)

- SysMediaFileDao(IFreeSql fsql):base(fsql)

- SysMediaFileDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysMediaFile ById(int id)

- async List<SysMediaFile> All()

- async int Count()

- async int Insert(SysMediaFile file)

- async int Delete(int id)


## SysOperationLogDao (class)

- SysOperationLogDao(IFreeSql fsql):base(fsql)

- SysOperationLogDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysOperationLog ById(int id)

- async List<SysOperationLog> Latest(int limit)

- async int Count()

- async int Insert(SysOperationLog log)

- async int CountScoped(string kw, bool byActor, List<int> actors)

- async List<SysOperationLog> PagedScoped(string kw, bool byActor, List<int> actors, int skip, int limit)

- async int CountBy(List<OrmCond> conds, bool byActor, List<int> actors)

- async List<SysOperationLog> PagedListBy(List<OrmCond> conds, bool byActor, List<int> actors, int skip, int limit)

- async int ClearAll()


## SysRoleDao (class)

- SysRoleDao(IFreeSql fsql):base(fsql)

- SysRoleDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysRole ById(int id)

- async SysRole ByCode(string code)

- async List<SysRole> All()

- async int Count()

- async int Insert(SysRole role)

- async int UpdateBasic(int id, string name, string code, int rank, int status, string description, int dataScope, string scopeDepts, long updatedAt)

- async int Update(SysRole role)

- async int Delete(int id)


## SysRoleGrantDao (class)

- SysRoleGrantDao(IFreeSql fsql):base(fsql)

- SysRoleGrantDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysRoleGrant ById(int id)

- async List<SysRoleGrant> ByRoleId(int roleId)

- async int Insert(SysRoleGrant rg)

- async int Delete(int id)

- async int DeleteByRoleId(int roleId)


## SysSiteSettingDao (class)

- SysSiteSettingDao(IFreeSql fsql):base(fsql)

- SysSiteSettingDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysSiteSetting ById(int id)

- async SysSiteSetting ByName(string name)

- async List<SysSiteSetting> All()

- async List<SysSiteSetting> ByGroupName(string groupName)

- async int SetValue(string name, string value)

- async int UpdateValueById(int id, string value, long updatedAt)

- async int Insert(SysSiteSetting s)


## SysUserDao (class)

- SysUserDao(IFreeSql fsql):base(fsql)

- SysUserDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysUser ByUsername(string username)

- async SysUser ByEmail(string email)

- async int CountByUsername(string username)

- async int CountByEmail(string email)

- async SysUser ById(int id)

- async List<SysUser> All()

- async int Count()

- async int Insert(SysUser user)

- async int SetStatus(int id, int status)

- async int UpdateProfile(int id, string username, string nickname, string email, string mobile, int departmentId, int status, long updatedAt)

- async int UpdatePersonal(int id, string nickname, string email, string mobile, long updatedAt)

- async int Delete(int id)

- async int ResetPassword(int id, string salt, string hash, long updatedAt)

- async int BumpTokenVersion(int id)

- async int UpgradeHash(int id, string hash, long updatedAt)

- async int CountScoped(bool byDept, List<int> depts, bool mine, int myId)

- async int CountPaged(string kw, int status, bool byDept, List<int> scoped, bool mine, int myId)

- async List<SysUser> PagedList(string kw, int status, bool byDept, List<int> scoped, bool mine, int myId, int skip, int limit)

- async List<SysUser> ByDeptIds(List<int> depts)

- async List<SysUser> ByIds(List<int> ids)

- async int SetStatusByIds(List<int> ids, int status)

- async int DeleteByIds(List<int> ids)

- async int CountBy(List<OrmCond> conds, bool byDept, List<int> scoped, bool mine, int myId)

- async List<SysUser> PagedListBy(List<OrmCond> conds, bool byDept, List<int> scoped, bool mine, int myId, int skip, int limit)


## SysUserRoleDao (class)

- SysUserRoleDao(IFreeSql fsql):base(fsql)

- SysUserRoleDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysUserRole ById(int id)

- async List<SysUserRole> All()

- async List<SysUserRole> ByUserId(int userId)

- async List<SysUserRole> ByRoleId(int roleId)

- async int Insert(SysUserRole ur)

- async int Delete(int id)

- async int DeleteByUserId(int userId)

- async List<SysUserRole> ByRoleIds(List<int> roleIds)


## SysWikiDocDao (class)

- SysWikiDocDao(IFreeSql fsql):base(fsql)

- SysWikiDocDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysWikiDoc ById(int id)

- async List<SysWikiDoc> BySpace(int spaceId)

- async int Insert(SysWikiDoc doc)

- async int Update(SysWikiDoc doc)

- async int Delete(int id)


## SysWikiRevisionDao (class)

- SysWikiRevisionDao(IFreeSql fsql):base(fsql)

- SysWikiRevisionDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async List<SysWikiRevision> ByDocId(int docId)

- async SysWikiRevision ByDocAndRev(int docId, int rev)

- async int Insert(SysWikiRevision rev)


## SysWikiSpaceDao (class)

- SysWikiSpaceDao(IFreeSql fsql):base(fsql)

- SysWikiSpaceDao(IDbConnection db):base(FreeSql.FromConnection(db))

- async SysWikiSpace ById(int id)

- async List<SysWikiSpace> All()

- async int Insert(SysWikiSpace space)

- async int Update(SysWikiSpace space)

- async int Delete(int id)
