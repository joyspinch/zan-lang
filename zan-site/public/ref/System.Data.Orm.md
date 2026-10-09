# System.Data.Orm

> 源码: `packages/Zan.Data/src/System/Data/Orm/BaseRepository.zan`, `packages/Zan.Data/src/System/Data/Orm/DbSchema.zan`, `packages/Zan.Data/src/System/Data/Orm/DbTable.zan`, `packages/Zan.Data/src/System/Data/Orm/EntityAccess.zan`, `packages/Zan.Data/src/System/Data/Orm/ExprSql.zan`, `packages/Zan.Data/src/System/Data/Orm/FreeSqlBuilder.zan`, `packages/Zan.Data/src/System/Data/Orm/IAop.zan`, `packages/Zan.Data/src/System/Data/Orm/ICodeFirst.zan`, `packages/Zan.Data/src/System/Data/Orm/IFreeSql.zan`, `packages/Zan.Data/src/System/Data/Orm/IOrmRows.zan`, `packages/Zan.Data/src/System/Data/Orm/IUnitOfWork.zan`, `packages/Zan.Data/src/System/Data/Orm/Model.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmCond.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmDialect.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmInsert.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmMeta.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmSelect.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmSync.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmWrite.zan`, `packages/Zan.Data/src/System/Data/Orm/QueryBuilder.zan`


## AopProvider (class)

- Action<AuditValueEventArgs> handler;

- AopProvider()

- void SetAuditValueHandler(Action<AuditValueEventArgs> handler)

- void Audit(AuditValueEventArgs e)


## AuditValueEventArgs (class)

- string table;

- string column;

- object val;

- bool isInsert;

- AuditValueEventArgs(string table, string column, object val, bool isInsert)

- string Table { get }

- string Column { get }

- object Value{ get set}

- bool IsInsert { get }


## BaseRepository (class)

- IFreeSql fsql;

- IUnitOfWork uow;

- BaseRepository(IFreeSql fsql)

- BaseRepository(IFreeSql fsql, IUnitOfWork uow)

- IFreeSql Orm { get }

- IUnitOfWork UnitOfWork{ get set}

- IDbExecutor GetExecutor()

- void Attach(IUnitOfWork uow)

- void Detach()

- int Insert(T entity)

- async int InsertAsync(T entity)

- int InsertIdentity(T entity)

- async int InsertIdentityAsync(T entity)

- int InsertBatch(List<T> entities)

- async int InsertBatchAsync(List<T> entities)

- int Update(T entity)

- async int UpdateAsync(T entity)

- List<T> SelectAll()

- async List<T> SelectAllAsync()

- int Count()

- async int CountAsync()


## CodeFirst (class)

- IDbExecutor db;

- CodeFirst(IDbExecutor db)

- void SetExecutor(IDbExecutor db)

- void SyncStructure(OrmMeta meta)

- async void SyncStructureAsync(OrmMeta meta)

- DbResult Query(string sql, DbParams prms)

- int Execute(string sql, DbParams prms)

- DbResult Query(string sql)

- int Execute(string sql)

- string ExecuteScalar(string sql)

- string ExecuteScalar(string sql, DbParams prms)

- int GetProvider()

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql, DbParams prms)

- async DbResult QueryAsync(string sql)

- async int ExecuteAsync(string sql)

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)


## DbColumnDef (class)

- string name;

- string kind;

- int size;

- bool nullable;

- bool identity;

- string defText;

- DbColumnDef(string name, string kind)

- static DbColumnDef Of(string name, string kind)

- static DbColumnDef Identity(string name)

- DbColumnDef WithSize(int n)

- DbColumnDef WithNull(bool ok)

- DbColumnDef NotNull()

- DbColumnDef WithDefault(string v)

- string Name()


## DbSchema (class)

- static async List<string> ColumnsAsync(IDbExecutor db, string table)

- static async bool HasColumnAsync(IDbExecutor db, string table, string column)

- static async bool HasAnyColumnAsync(IDbExecutor db, string table, List<string> columns)

- static async bool TableExistsAsync(IDbExecutor db, string table)

- static string SqlType(string kind, int size, int provider)

- static string DefaultLiteral(DbColumnDef c)

- static string ColumnSql(DbColumnDef c, int provider)

- static string CreateTableSql(string table, List<DbColumnDef> cols, int provider)

- static async void CreateTableAsync(IDbExecutor db, string table, List<DbColumnDef> cols)

- static async void AddColumnAsync(IDbExecutor db, string table, DbColumnDef col)

- static async int MigrateAsync(IDbExecutor db, string table, List<DbColumnDef> cols)

- static async void RenameTableAsync(IDbExecutor db, string from, string to)

- static async void DropTableAsync(IDbExecutor db, string table)

- static async void UseWriteAheadLogAsync(IDbExecutor db)


## DbTable (class)

- IDbConnection conn;

- string table;

- DbTable(IDbConnection conn, string table)

- static DbTable Of(IDbConnection conn, string table)

- string Name()

- DbTableQuery Query()

- async DbResult ReadAsync(string keyColumn, long key)

- async int InsertAsync(DbValues row)

- async int UpdateAsync(DbValues row, string keyColumn, long key)

- async int DeleteAsync(string keyColumn, long key)


## DbTableQuery (class)

- IDbConnection conn;

- QueryBuilder qb;

- int limitVal;

- int offsetVal;

- DbTableQuery(IDbConnection conn, string table)

- DbTableQuery WhereEq(string column, string val)

- DbTableQuery WhereEq(string column, long val)

- DbTableQuery WhereGe(string column, long val)

- DbTableQuery WhereLe(string column, long val)

- DbTableQuery WhereContains(string column, string val)

- DbTableQuery WhereDict(DbValues cond)

- DbTableQuery WhereIn(string column, List<int> values)

- DbTableQuery WhereIn(string column, List<long> values)

- DbTableQuery OrderBy(string column)

- DbTableQuery OrderByDesc(string column)

- DbTableQuery OrderBySafe(string column, List<string> allowed)

- DbTableQuery OrderBySafeDesc(string column, List<string> allowed)

- DbTableQuery Limit(int count)

- DbTableQuery Offset(int count)

- DbTableQuery Page(int take, int skip)

- async long CountAsync()

- async DbResult ToResultAsync()

- async DbResult ToResultAsync(List<string> columns)

- async int ExecuteDeleteAsync()

- async int ExecuteUpdateAsync(DbValues row)

- async DbResult run(string columns)


## EntityAccess (class)

- static IDbConnection __Conn(this IDbConnection db)


## ExprSql (class)

- static string EvalText(ExprNode n)

- static string SqlOp(string op)

- static void Build(ExprNode n, StringBuilder sb, DbParams ps)


## FreeSql (class)

- IDbConnection conn;

- DbPool pool;

- AopProvider aop;

- FreeSql(IDbConnection conn)

- FreeSql(DbPool pool)

- static IFreeSql FromConnection(IDbConnection conn)

- static IFreeSql FromPool(DbPool pool)

- ICodeFirst CodeFirst()

- IAop Aop()

- CodeFirst CodeFirstProperty { get }

- IUnitOfWork CreateUnitOfWork()

- IDbConnection OpenConnection()

- IDbConnection __Conn()

- int GetProvider()

- DbResult Query(string sql, DbParams prms)

- int Execute(string sql, DbParams prms)

- DbResult Query(string sql)

- int Execute(string sql)

- string ExecuteScalar(string sql)

- string ExecuteScalar(string sql, DbParams prms)

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql, DbParams prms)

- async DbResult QueryAsync(string sql)

- async int ExecuteAsync(string sql)

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- void Dispose()


## FreeSqlBuilder (class)

- IDbConnection conn;

- DbPool pool;

- int provider;

- string connectionString;

- bool autoSyncStructure;

- FreeSqlBuilder()

- FreeSqlBuilder UseConnection(IDbConnection conn)

- FreeSqlBuilder UsePool(DbPool pool)

- FreeSqlBuilder UseConnectionString(int provider, string connectionString)

- FreeSqlBuilder UseAutoSyncStructure(bool autoSync)

- IFreeSql Build()


## Migration (class)

- string name;

- string upSql;

- string downSql;

- Migration(string name, string up, string down)

- static Migration New(string name, string up, string down)

- static int Exec(IDbExecutor db, string sql)

- void Up(IDbExecutor db)

- void Down(IDbExecutor db)

- static void EnsureTable(IDbExecutor db)

- static bool IsApplied(IDbExecutor db, string name)

- static void RunAll(IDbExecutor db, List<Migration> migrations)


## Model (class)

- string tableName;

- List<ModelColumn> columns;

- string primaryKey;

- IDbExecutor db;

- Model(string tableName)

- static Model Define(string tableName)

- Model Column(string name, string type)

- Model Column(string name, string type, bool isPrimaryKey)

- Model ColumnNotNull(string name, string type)

- Model ColumnDefault(string name, string type, string defaultVal)

- Model ColumnUnique(string name, string type)

- void CreateTable(IDbExecutor db)

- void CreateTableAsync(IDbExecutor db)

- void DropTable(IDbExecutor db)

- bool TableExists(IDbExecutor db)

- int Insert(IDbExecutor db, ModelRow row)

- int InsertAsync(IDbExecutor db, ModelRow row)

- ModelQuery Select(IDbExecutor db)

- ModelUpdate UpdateQuery(IDbExecutor db)

- ModelDelete DeleteQuery(IDbExecutor db)

- ModelRow FindById(IDbExecutor db, int id)

- ModelRow FindByIdAsync(IDbExecutor db, int id)

- int Count(IDbExecutor db)

- int CountWhere(IDbExecutor db, string condition)

- void DeleteById(IDbExecutor db, int id)

- List<ModelRow> All(IDbExecutor db)

- string GetTableName()

- string GetPrimaryKey()

- int ColumnCount()

- string ColumnNameAt(int i)

- string ColumnTypeAt(int i)

- static bool EqIgnoreCase(string a, string b)

- static int ColIndex(DbResult r, string name)

- static void FillFromSqlite(Model m, IDbExecutor db, string t)

- static void FillFromMysql(Model m, IDbExecutor db, string t)

- static void FillFromInformationSchema(Model m, IDbExecutor db, string t)

- static void FillFromTdengine(Model m, IDbExecutor db, string t)

- static string PrimaryKeyFromCatalog(IDbExecutor db, string t)

- void AddIntrospected(string name, string type, bool isPk, bool notNull)

- static Model FromTable(IDbExecutor db, string tableName)

- static string StripMods(string type)

- static int IndexOfSub(string s, string sub)

- string ToDefineCode()


## ModelCell (class)

- string key;

- string val;

- ModelCell(string key, string val)


## ModelColumn (class)

- string name;

- string type;

- ModelColumn(string name, string type)


## ModelDelete (class)

- IDbExecutor db;

- QueryBuilder qb;

- ModelDelete(IDbExecutor db, string tableName)

- static ModelDelete New(IDbExecutor db, string tableName)

- ModelDelete Where(string condition)

- ModelDelete WhereEq(string column, string val)

- int Execute()

- int ExecuteAsync()


## ModelQuery (class)

- IDbExecutor db;

- string tableName;

- QueryBuilder qb;

- ModelQuery(IDbExecutor db, string tableName)

- static ModelQuery NewSelect(IDbExecutor db, string tableName)

- ModelQuery Where(string condition)

- ModelQuery WhereEq(string column, string val)

- ModelQuery WhereEqInt(string column, int val)

- ModelQuery OrderBy(string column)

- ModelQuery OrderByDesc(string column)

- ModelQuery OrderByDescending(string column)

- ModelQuery OrderBySafe(string column, List<string> allowed)

- ModelQuery OrderBySafeDesc(string column, List<string> allowed)

- ModelQuery WhereLike(string column, string pattern)

- ModelQuery WhereContains(string column, string val)

- ModelQuery WhereAnyLike(List<string> columns, string val)

- ModelQuery WhereNe(string column, string val)

- ModelQuery WhereGtInt(string column, int val)

- ModelQuery WhereGeInt(string column, int val)

- ModelQuery WhereLtInt(string column, int val)

- ModelQuery WhereLeInt(string column, int val)

- ModelQuery WhereBetween(string column, string low, string high)

- ModelQuery WhereNull(string column)

- ModelQuery WhereNotNull(string column)

- ModelQuery WhereIn(string column, List<string> values)

- ModelQuery WhereNotIn(string column, List<string> values)

- ModelQuery Page(int index, int size)

- bool Any()

- ModelQuery Limit(int count)

- ModelQuery Offset(int count)

- List<ModelRow> Execute()

- List<ModelRow> ExecuteAsync()

- ModelRow First()

- int Count()


## ModelRow (class)

- List<ModelCell> cells;

- ModelRow()

- ModelRow Set(string key, string val)

- ModelRow SetInt(string key, int val)

- string Get(string key)

- int GetInt(string key)

- bool Has(string key)

- List<string> GetKeys()

- List<string> GetValues()

- static ModelRow FromDbResult(DbResult result, int rowIndex)

- static List<ModelRow> FromDbResultAll(DbResult result)

- string ToJson()


## ModelUpdate (class)

- IDbExecutor db;

- QueryBuilder qb;

- ModelUpdate(IDbExecutor db, string tableName)

- static ModelUpdate New(IDbExecutor db, string tableName)

- ModelUpdate Set(string column, string val)

- ModelUpdate SetInt(string column, int val)

- ModelUpdate Where(string condition)

- ModelUpdate WhereEq(string column, string val)

- int Execute()

- int ExecuteAsync()


## OrmCol (class)

- string Col;

- string Field;

- int Kind;

- bool I64;

- bool IsPk;

- bool IsIdent;

- bool NotNull;

- int StrLen;

- OrmCol(string col, string field, int kind, bool i64, bool pk, bool ident, bool notNull, int len)

- int TyCode()

- int PKind()


## OrmCond (class)

- string name;

- string col;

- string kind;

- string value;

- static long Num(string s)


## OrmDelete (class)

- IDbExecutor db;

- OrmMeta meta;

- string tbl;

- string w;

- DbParams ps;

- bool on;

- OrmDelete(IDbExecutor db, OrmMeta meta)

- static OrmDelete Create(IDbExecutor db, OrmMeta meta)

- void AsTable(string t)

- void CondBegin(bool c)

- void CondEnd()

- void W(string f)

- void WhereDict(DbValues v)

- string RequireCol(string c)

- void WhereEq(string c, string v)

- void WhereEq(string c, int v)

- void WhereEq(string c, long v)

- void WhereIn(string c, List<int> vs)

- void WhereIn(string c, List<long> vs)

- void WhereIn(string c, List<string> vs)

- void WhereNone()

- void P(string v)

- void Pi(int v)

- void Pl(long v)

- void Pd(double v)

- void InI(List<int> vs)

- void InL(List<long> vs)

- void InS(List<string> vs)

- void InD(List<double> vs)

- int ExecuteAffrows()

- async int ExecuteAffrowsAsync()


## OrmDialect (class)

- static string Marks(int n)

- static bool Has(List<string> l, string v)

- static string Unalias(string f)

- static DbParams Cat(DbParams a, DbParams b)

- static void CopyInto(DbParams src, DbParams dst)

- static string ConflictHead(int p, List<string> keys, bool nop)

- static string Excl(int p, string c, int md)

- static int Mode(List<string> acc, List<string> mx, List<string> mn, string c)

- static string LastIdSql(IDbExecutor db)

- static int LastId(IDbExecutor db)

- static async int LastIdAsync(IDbExecutor db)

- static string OwnScope(int p)

- static bool HasInformationSchema(int p)

- static string HasTableSql(IDbExecutor db)

- static bool HasTable(IDbExecutor db, string t)

- static async bool HasTableAsync(IDbExecutor db, string t)

- static string ColsSql(int p)

- static List<string> Cols(IDbExecutor db, string t)

- static async List<string> ColsAsync(IDbExecutor db, string t)

- static string HasIndexSql(int p)

- static DbParams IndexProbeParams(int p, string nm, string t)

- static string IndexDdl(int p, string nm, string t, string cols, bool uniq)

- static void EnsureIndex(IDbExecutor db, string nm, string t, string cols, bool uniq)

- static async void EnsureIndexAsync(IDbExecutor db, string nm, string t, string cols, bool uniq)

- static string Ty(int p, int kind, bool pk, bool ident, int len)


## OrmIndex (class)

- string Name;

- string Cols;

- bool Unique;

- OrmIndex(string name, string cols, bool uniq)


## OrmInsert (class)

- IDbExecutor db;

- OrmMeta meta;

- string tbl;

- IOrmRows rows;

- List<DbValues> dicts;

- List<string> only;

- List<string> skip;

- List<string> conf;

- List<string> acc;

- List<string> gmx;

- List<string> gmn;

- bool upsert;

- bool nop;

- string sql;

- DbParams bound;

- OrmInsert(IDbExecutor db, OrmMeta meta)

- static OrmInsert Create(IDbExecutor db, OrmMeta meta)

- void AsTable(string t)

- void SetRows(IOrmRows rows)

- int RowCount()

- void AddDict(DbValues v)

- void Only(string c)

- void Skip(string c)

- void OC(string c)

- void ACC(string c)

- void GMX(string c)

- void GMN(string c)

- void NOP()

- int DictCount()

- bool Use(string c, bool identity)

- List<OrmCol> used()

- string colList(List<OrmCol> cols)

- string Conflict()

- int BuildDictsRange(int start, int count)

- int BuildDicts()

- int BuildRowsRange(int start, int count)

- int BuildRows()

- int safeBatchRows(int colCount)

- int ExecuteDicts()

- async int ExecuteDictsAsync()

- int ExecuteRows()

- async int ExecuteRowsAsync()

- int ExecuteAffrows()

- async int ExecuteAffrowsAsync()

- int ExecuteIdentity()

- async int ExecuteIdentityAsync()


## OrmMeta (class)

- string Entity;

- string Table;

- List<OrmCol> Cols;

- List<OrmNav> Navs;

- List<OrmIndex> Idx;

- Dict <string, OrmCol> byCol;

- string selList;

- bool frozen;

- OrmMeta(string entity, string table)

- static OrmMeta Of(string entity, string table)

- OrmMeta Seal()

- void CheckNotFrozen()

- OrmMeta Col(string col, string field, int kind, bool i64, bool pk, bool ident, bool notNull, int len)

- OrmNav Nav(string field, string table)

- OrmMeta Index(string name, string cols, bool uniq)

- OrmCol Find(string c)

- bool Has(string c)

- int KindOf(string c)

- string Require(string c)

- string SelectList()

- List<string> PkCols()

- OrmCol PkCol()

- string PkList()


## OrmNav (class)

- string Field;

- string Table;

- List<OrmCol> Cols;

- int PkAt;

- OrmNav(string field, string table)

- OrmNav Col(string col, string field, int kind, bool i64)

- string SelectList()

- string Join()


## OrmSelect (class)

- IDbExecutor db;

- OrmMeta meta;

- string tbl;

- string w;

- DbParams ps;

- string ob;

- string gb;

- string h;

- bool distinct;

- int limN;

- int offN;

- bool on;

- List<bool> navOn;

- OrmSelect(IDbExecutor db, OrmMeta meta)

- static OrmSelect Create(IDbExecutor db, OrmMeta meta)

- OrmMeta Meta()

- DbParams Params()

- string GroupKey()

- bool NavOn(int i)

- void W(string f)

- void WExpr(ExprNode root)

- void WhereDict(DbValues v)

- void CondBegin(bool c)

- void CondEnd()

- void AsTable(string t)

- void P(string v)

- void Pi(int v)

- void Pl(long v)

- void Pd(double v)

- void InI(List<int> vs)

- void InL(List<long> vs)

- void InS(List<string> vs)

- void InD(List<double> vs)

- string RequireCol(string c)

- void WhereEq(string c, string v)

- void WhereEq(string c, int v)

- void WhereEq(string c, long v)

- void WhereGe(string c, long v)

- void WhereLe(string c, long v)

- void WhereIn(string c, List<int> vs)

- void WhereIn(string c, List<long> vs)

- void WhereIn(string c, List<string> vs)

- void WhereLikeAny(string cols, string v)

- void WhereNone()

- void WhereConds(List<OrmCond> cs)

- void OB(string col)

- void OrderBy(string col)

- void OBD(string col)

- void OrderBySafe(string col)

- void OrderBySafeDesc(string col)

- void GB(string col)

- void OBK()

- void OBDK()

- void H(string f)

- void DISTINCT()

- void Take(int n)

- void SkipN(int n)

- void Page(int index, int size)

- void Include(int i)

- int NavAt(int i)

- void tail(StringBuilder sb)

- void appendLimitOffset(StringBuilder sb)

- string BuildSelect()

- DbResult Rows()

- async DbResult RowsAsync()

- string BuildCol(string col)

- string BuildDto(string cols)

- List<string> MapCol(DbResult r)

- List<string> ToListCol(string col)

- async List<string> ToListColAsync(string col)

- DbResult Dto(string cols)

- async DbResult DtoAsync(string cols)

- void One()

- string BuildCount()

- string BuildAgg(string expr)

- DbResult AggR(string expr)

- async DbResult AggRAsync(string expr)

- int AggI(string expr)

- long AggL(string expr)

- double AggD(string expr)

- async int AggIAsync(string expr)

- async long AggLAsync(string expr)

- async double AggDAsync(string expr)

- int Count()

- async int CountAsync()

- bool Any()

- async bool AnyAsync()


## OrmSync (class)

- static string CreateDdl(int p, OrmMeta m, string t)

- static string AddColDdl(int p, OrmCol c, string t)

- static void ExecDdl(IDbExecutor db, string ddl)

- static async void ExecDdlAsync(IDbExecutor db, string ddl)

- static void Sync(IDbExecutor db, OrmMeta m)

- static async void SyncAsync(IDbExecutor db, OrmMeta m)

- static void Indexes(IDbExecutor db, OrmMeta m, string t)

- static async void IndexesAsync(IDbExecutor db, OrmMeta m, string t)


## OrmUpdate (class)

- IDbExecutor db;

- OrmMeta meta;

- string tbl;

- IOrmRows src;

- List<string> only;

- List<string> skip;

- string sets;

- DbParams sp;

- string w;

- DbParams wp;

- bool on;

- OrmUpdate(IDbExecutor db, OrmMeta meta)

- static OrmUpdate Create(IDbExecutor db, OrmMeta meta)

- void AsTable(string t)

- void Only(string c)

- void Skip(string c)

- void SetSource(IOrmRows rows)

- void CondBegin(bool c)

- void CondEnd()

- bool Use(string c)

- void Frag(string f)

- void SetI(string c, int v)

- void SetL(string c, long v)

- void SetD(string c, double v)

- void SetS(string c, string v)

- void SetB(string c, bool v)

- void SetIncrI(string c, int v)

- void SetIncrL(string c, long v)

- void SetIncrD(string c, double v)

- void SetDict(DbValues v)

- void W(string f)

- void WhereDict(DbValues v)

- string RequireCol(string c)

- void WhereEq(string c, string v)

- void WhereEq(string c, int v)

- void WhereEq(string c, long v)

- void WhereIn(string c, List<int> vs)

- void WhereIn(string c, List<long> vs)

- void WhereIn(string c, List<string> vs)

- void WhereNone()

- void P(string v)

- void Pi(int v)

- void Pl(long v)

- void Pd(double v)

- void InI(List<int> vs)

- void InL(List<long> vs)

- void InS(List<string> vs)

- void InD(List<double> vs)

- void ApplySource()

- string BuildUpdate()

- int ExecuteAffrows()

- async int ExecuteAffrowsAsync()


## QueryBuilder (class)

- string tableName;

- string whereClause;

- string orderByClause;

- string groupByClause;

- string havingClause;

- string joinClause;

- int limitVal;

- int offsetVal;

- int dialect;

- List<SetClause> sets;

- bool distinct;

- DbParams whereParams;

- QueryBuilder(string table)

- static bool IsIdent(string s)

- static string RequireIdent(string s)

- static string RequireDdlType(string s)

- static QueryBuilder From(string table)

- static QueryBuilder InsertInto(string table)

- static QueryBuilder Update(string table)

- static QueryBuilder DeleteFrom(string table)

- QueryBuilder Where(string condition)

- QueryBuilder OrWhere(string condition)

- QueryBuilder WhereEq(string column, string val)

- QueryBuilder WhereEqInt(string column, int val)

- QueryBuilder WhereEqLong(string column, long val)

- QueryBuilder WhereGeLong(string column, long val)

- QueryBuilder WhereLeLong(string column, long val)

- QueryBuilder WhereValueAt(DbValues vals, int i)

- QueryBuilder WhereIn(string column, List<string> values)

- QueryBuilder WhereInInts(string column, List<int> values)

- QueryBuilder WhereInLongs(string column, List<long> values)

- QueryBuilder WhereNotIn(string column, List<string> values)

- QueryBuilder WhereLike(string column, string pattern)

- QueryBuilder WhereNotLike(string column, string pattern)

- QueryBuilder WhereContains(string column, string val)

- QueryBuilder WhereStartsWith(string column, string val)

- QueryBuilder WhereEndsWith(string column, string val)

- QueryBuilder WhereAnyLike(List<string> columns, string val)

- QueryBuilder WhereNe(string column, string val)

- QueryBuilder WhereNeInt(string column, int val)

- QueryBuilder WhereGtInt(string column, int val)

- QueryBuilder WhereGeInt(string column, int val)

- QueryBuilder WhereLtInt(string column, int val)

- QueryBuilder WhereLeInt(string column, int val)

- QueryBuilder WhereBetween(string column, string low, string high)

- QueryBuilder WhereNull(string column)

- QueryBuilder WhereNotNull(string column)

- QueryBuilder OrderBy(string column)

- QueryBuilder OrderByDesc(string column)

- static void CheckAllowed(string column, List<string> allowed)

- QueryBuilder OrderBySafe(string column, List<string> allowed)

- QueryBuilder OrderBySafeDesc(string column, List<string> allowed)

- QueryBuilder GroupBy(string column)

- QueryBuilder Having(string condition)

- QueryBuilder Join(string table, string on)

- QueryBuilder LeftJoin(string table, string on)

- QueryBuilder RightJoin(string table, string on)

- QueryBuilder Limit(int count)

- QueryBuilder Offset(int count)

- QueryBuilder Dialect(int d)

- QueryBuilder Distinct()

- QueryBuilder Set(string column, string val)

- QueryBuilder SetParam(string column)

- QueryBuilder SetInt(string column, int val)

- void AppendClauses(StringBuilder sb)

- string BuildSelectParams(string columns)

- DbParams SelectParams()

- string BuildInsertParams()

- DbParams InsertParams()

- string BuildUpdateParams()

- DbParams UpdateParams()

- string BuildDeleteParams()

- static string EscapeDdlLiteral(string val)


## SetClause (class)

- string column;

- string val;

- SetClause(string column, string val)


## SqlDialect (class)

- static int Standard=0;

- static int SqlServer=1;

- static int Oracle=2;


## UnitOfWork (class)

- IDbConnection conn;

- bool committed;

- bool rolledBack;

- bool closeOnDispose;

- UnitOfWork(IDbConnection conn, bool closeOnDispose)

- static UnitOfWork Create(IDbConnection conn, bool closeOnDispose)

- IDbConnection GetConnection()

- void Commit()

- void Rollback()

- async bool CommitAsync()

- async bool RollbackAsync()

- void Dispose()

- DbResult Query(string sql, DbParams prms)

- int Execute(string sql, DbParams prms)

- DbResult Query(string sql)

- int Execute(string sql)

- string ExecuteScalar(string sql)

- string ExecuteScalar(string sql, DbParams prms)

- int GetProvider()

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql, DbParams prms)

- async DbResult QueryAsync(string sql)

- async int ExecuteAsync(string sql)

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)


## IAop (interface)

- void SetAuditValueHandler(Action<AuditValueEventArgs> handler);

- void Audit(AuditValueEventArgs e);


## ICodeFirst (interface)

- void SyncStructure(OrmMeta meta);

- async void SyncStructureAsync(OrmMeta meta);


## IFreeSql (interface)

- ICodeFirst CodeFirst();

- IAop Aop();

- IUnitOfWork CreateUnitOfWork();

- IDbConnection OpenConnection();

- void Dispose();


## IOrmRows (interface)

- int RowCount();

- void BindCol(int row, OrmCol c, DbParams ps);


## IUnitOfWork (interface)

- IDbConnection GetConnection();

- void Commit();

- void Rollback();

- async bool CommitAsync();

- async bool RollbackAsync();

- void Dispose();
