# System.Data.Orm

> 源码: `stdlib/System/Data/Orm/DbSchema.zan`, `stdlib/System/Data/Orm/DbTable.zan`, `stdlib/System/Data/Orm/ExprSql.zan`, `stdlib/System/Data/Orm/Model.zan`, `stdlib/System/Data/Orm/QueryBuilder.zan`


## DbColumnDef (class)

一列的逻辑定义：列名、逻辑类型、宽度、可空性、默认值，
以及它是不是自增主键。逻辑类型是引擎无关的词汇
（`int` / `long` / `bool` / `datetime` / `decimal` / `string` / `text`），
具体落到哪个物理类型由 `DbSchema` 按 provider 决定 ——
运行期才知道形状的表（表设计器、数据字典）这样建表，业务代码
就不必知道自己跑在谁上面，也不必拼 DDL。

- string name;

- string kind;

- int size;

- bool nullable;

- bool identity;

- string defText;

- DbColumnDef(string name, string kind)

- static DbColumnDef Of(string name, string kind)
  - 一个普通列。

- static DbColumnDef Identity(string name)
  - 自增整型主键列（每家方言写法不同，这里只表达意图）。

- DbColumnDef WithSize(int n)
  - 字符串列的宽度（其余类型忽略；<= 0 表示按默认 255）。

- DbColumnDef WithNull(bool ok)
  - 可空性。

- DbColumnDef NotNull()
  - NOT NULL。

- DbColumnDef WithDefault(string v)
  - 默认值，按逻辑类型校验后写成字面量：数值类型只接受
    数字，其余按文本加引号（引号翻倍）。无法用作字面量时忽略。
    DDL 里没有参数占位，默认值只能内联，所以这一步的校验就是
    这里唯一的防线。

- string Name()
  - 列名（构造时已通过标识符校验）。


## DbSchema (class)

库表层面的内省与运维动作：一张表在不在、有哪些列、改个名、丢掉、
以及 SQLite 的写前日志。CodeFirst（`SyncStructure<T>()`）负责
让实体对应的表长成实体的样子，这里补的是它管不到的那一半：一个
更早版本留下的表要不要让路、一个诊断库要不要开 WAL。

为什么在标准库而不是业务里：这些语句每家方言都不一样（SQLite 问
`sqlite_master` 和 `PRAGMA`，别家问 `information_schema`），业务
代码不该知道自己跑在谁上面，也不该自己拼语句。表名一律按标识符
校验后才进语句，其余全部走参数。

- static async List<string> ColumnsAsync(IDbExecutor db, string table)
  - 表的列名，顺序同表定义；表不存在时是空列表。

- static async bool HasColumnAsync(IDbExecutor db, string table, string column)
  - 该表是否有这一列。

- static async bool HasAnyColumnAsync(IDbExecutor db, string table, List<string> columns)
  - 该表是否有其中任意一列。一个换过形状的旧表就是这么认出
    来的：它带着新实体里已经没有的列。

- static async bool TableExistsAsync(IDbExecutor db, string table)
  - 表是否存在。

- static string SqlType(string kind, int size, int provider)
  - 逻辑类型在该 provider 上的物理类型。

- static string DefaultLiteral(DbColumnDef c)
  - 默认值的字面量，无法安全内联时是空串。

- static string ColumnSql(DbColumnDef c, int provider)
  - 一列的 DDL 片段。

- static string CreateTableSql(string table, List<DbColumnDef> cols, int provider)
  - 建表语句（列顺序即给定顺序；表已存在时不报错）。

- static async void CreateTableAsync(IDbExecutor db, string table, List<DbColumnDef> cols)
  - 按逻辑列定义建表。形状在运行期才知道的表（表设计器、
    数据字典）走这里，业务代码不必拼 DDL，也不必分方言。

- static async void AddColumnAsync(IDbExecutor db, string table, DbColumnDef col)
  - 给已有表加一列（不改类型、不删列 —— 那会丢数据）。

- static async int MigrateAsync(IDbExecutor db, string table, List<DbColumnDef> cols)
  - 把设计里有、表里没有的列补上，返回补了几列；表还不
    存在时先建表，返回列数。列永不删除、永不改类型。

- static async void RenameTableAsync(IDbExecutor db, string from, string to)
  - 把一张表改名（目标名已被占用时先丢掉目标）。

- static async void DropTableAsync(IDbExecutor db, string table)
  - 丢掉一张表（不存在也不报错）。

- static async void UseWriteAheadLogAsync(IDbExecutor db)
  - SQLite 的写前日志：写的人不再挡住读的人。别的 provider
    上是空操作 —— 日志形态是它们自己的服务端配置，不是连接属性。


## DbTable (class)

运行期成形的表网关：表名与列名在编译期还不知道（代码生成器
设计出来的表、导入的电子表格、外部库），因此无法用
<c>Select<T></c> 那样的实体链，但业务代码同样不该去拼 SQL。

DbTable t = DbTable.Of(db, "sys_user");
long total = await t.Query().WhereContains("name", kw).CountAsync();
DbResult rows = await t.Query().WhereContains("name", kw)
.OrderByDesc("id").Page(20, 40).ToResultAsync(cols);
await t.InsertAsync(form);                  // form 是 DbValues
await t.UpdateAsync(form, "id", id);
await t.DeleteAsync("id", id);

表名与每个列名都按标识符规则校验（`QueryBuilder`
的 RequireIdent），值一律作为绑定参数，绝不进入 SQL 文本。
SQL 文本由 `QueryBuilder` 生成 —— 调用方只描述
要读写什么。

- IDbConnection conn;

- string table;

- DbTable(IDbConnection conn, string table)

- static DbTable Of(IDbConnection conn, string table)
  - 某个连接上的一张运行期表。

- string Name()
  - 表名（已校验）。

- DbTableQuery Query()
  - 这张表上的一次读取：条件、排序、分页。

- async DbResult ReadAsync(string keyColumn, long key)
  - 按主键读取一行的全部列，没有该行时返回 null。

- async int InsertAsync(DbValues row)
  - 写入一行动态列，返回写入行数。

- async int UpdateAsync(DbValues row, string keyColumn, long key)
  - 按主键更新一行的动态列，返回更新行数。

- async int DeleteAsync(string keyColumn, long key)
  - 按主键删除一行，返回删除行数。


## DbTableQuery (class)

`DbTable` 上的一次读取：条件、排序、分页都用列名
描述，执行时才由 `QueryBuilder` 变成参数化 SQL。

- IDbConnection conn;

- QueryBuilder qb;

- int limitVal;

- int offsetVal;

- DbTableQuery(IDbConnection conn, string table)

- DbTableQuery WhereEq(string column, string val)
  - column = 文本值。

- DbTableQuery WhereEq(string column, long val)
  - column = 整数值。

- DbTableQuery WhereGe(string column, long val)
  - column >= 整数值。

- DbTableQuery WhereLe(string column, long val)
  - column <= 整数值。

- DbTableQuery WhereContains(string column, string val)
  - column LIKE %val%（子串匹配）。

- DbTableQuery WhereDict(DbValues cond)
  - DbValues 里的每一列都按等值匹配（提交上来的
    筛选表单原样交给 ORM）。

- DbTableQuery OrderBy(string column)
  - 升序排序。

- DbTableQuery OrderByDesc(string column)
  - 降序排序。

- DbTableQuery Limit(int count)
  - 最多读多少行。

- DbTableQuery Offset(int count)
  - 跳过多少行。

- DbTableQuery Page(int take, int skip)
  - 一页：take 行，跳过 skip 行。

- async long CountAsync()
  - 符合条件的行数（分页不参与计数）。

- async DbResult ToResultAsync()
  - 读取全部列。

- async DbResult ToResultAsync(List<string> columns)
  - 只读取列出的列（每个列名都会校验）。

- async int ExecuteDeleteAsync()
  - 删除符合条件的行，返回删除行数。

- async DbResult run(string columns)


## ExprSql (class)

纯 Zan 的表达式树转 SQL 构建器。遍历 dbgen 阶段生成的 `ExprNode` 树
（即将 lambda 降级为可检查的节点），
并将参数化 SQL 片段追加到 StringBuilder，同时把
常量绑定到 DbParams 列表。类型化 ORM 的所有 WHERE 子句 SQL 构造都在这里——
编译器只把 `Where(o => ...)` lambda 降级为
Expr 树；C 通道不会生成任何 SQL 形式的内容。

列引用将成员名映射为 `t.<name>`（列 = 字段名，
ORM 默认约定）。支持：
member / const（int、double、string、bool）
binary: == != < > <= >= + - * / %  && (AND)  || (OR)
not: !expr
调用：StartsWith / EndsWith / Contains → 转换为带绑定模式的 LIKE

- static string EvalText(ExprNode n)
  - 将常量节点渲染为文本（用于组装 LIKE 模式）。

- static string SqlOp(string op)
  - Zan 比较运算符对应的 SQL 写法。SQLite 恰好
    接受 `==` 和 `!=`，但标准 SQL（MySQL、PostgreSQL、SQL Server）
    不接受——未转换的运算符在那里是语法错误，因此
    运算符必须做映射，不能直接透传。

- static void Build(ExprNode n, StringBuilder sb, DbParams ps)
  - 为以 n 为根的树构建 WHERE 片段。


## Migration (class)

用于管理数据库 schema 演进的迁移辅助工具。

- string name;

- string upSql;

- string downSql;

- Migration(string name, string up, string down)

- void Up(IDbExecutor db)
  - 执行迁移（运行 UP SQL）。

- void Down(IDbExecutor db)
  - 回退迁移（运行 DOWN SQL）。

- static void EnsureTable(IDbExecutor db)
  - 确保迁移记录表存在。SQLite 可以以
    TEXT 为键；MySQL/MariaDB 禁止无长度前缀的 TEXT 作键，因此它们
    使用 VARCHAR(255)。

- static bool IsApplied(IDbExecutor db, string name)
  - 检查某个迁移是否已执行。

- static void RunAll(IDbExecutor db, List<Migration> migrations)
  - 按顺序运行迁移列表。


## Model (class)

仿 FreeSQL 的 ORM 模型基类。
提供自动生成 SQL 的 CRUD 操作。
模型通过列到字段的映射关联数据库表。

用法：
// 定义模型
Model userModel = Model.Define("users")
.Column("id", "INTEGER", true)
.Column("name", "TEXT")
.Column("email", "TEXT")
.Column("age", "INTEGER")
.Column("created_at", "TEXT");

// 建表
userModel.CreateTable(db);

// 插入
ModelRow row = new ModelRow()
.Set("name", "Alice").Set("email", "alice@example.com").Set("age", "30");
userModel.Insert(db, row);

// 查询
List<ModelRow> users = userModel.Select(db).Where("age > 18").Execute();

// 更新
userModel.Update(db).Set("name", "Bob").Where("id = 1").Execute();

// 删除
userModel.Delete(db).Where("id = 1").Execute();

- string tableName;

- List<ModelColumn> columns;

- string primaryKey;

- IDbExecutor db;

- Model(string tableName)

- static Model Define(string tableName)
  - 以表名定义一个新模型。

- Model Column(string name, string type)
  - 添加一个列定义。

- Model Column(string name, string type, bool isPrimaryKey)
  - 将一列设为主键。

- Model ColumnNotNull(string name, string type)
  - 添加一个 NOT NULL 列。

- Model ColumnDefault(string name, string type, string defaultVal)
  - 添加带默认值的列。

- Model ColumnUnique(string name, string type)
  - 添加一个 UNIQUE 列。

- void CreateTable(IDbExecutor db)
  - 在数据库中创建表。

- void CreateTableAsync(IDbExecutor db)
  - 创建表（包装方法）。

- void DropTable(IDbExecutor db)
  - 从数据库中删除表。

- bool TableExists(IDbExecutor db)
  - 检查表是否存在。（tableName 在 Define 时已通过标识符校验，
    因此这里直接内联是安全的。）

- int Insert(IDbExecutor db, ModelRow row)
  - 向表中插入一行。

- int InsertAsync(IDbExecutor db, ModelRow row)
  - 插入一行（包装方法）。

- ModelQuery Select(IDbExecutor db)
  - 为该模型开启 SELECT 查询构建器。

- ModelUpdate UpdateQuery(IDbExecutor db)
  - 为该模型开启 UPDATE 查询构建器。

- ModelDelete DeleteQuery(IDbExecutor db)
  - 为该模型开启 DELETE 查询构建器。

- ModelRow FindById(IDbExecutor db, int id)
  - 按主键查找一行。

- ModelRow FindByIdAsync(IDbExecutor db, int id)
  - 按主键查找一行（包装方法）。

- int Count(IDbExecutor db)
  - 统计表中的总行数。

- int CountWhere(IDbExecutor db, string condition)
  - 统计满足条件的行数。

- void DeleteById(IDbExecutor db, int id)
  - 按主键删除一行。

- List<ModelRow> All(IDbExecutor db)
  - 返回所有行。

- string GetTableName()
  - 返回表名。

- string GetPrimaryKey()
  - 返回主键列名。

- int ColumnCount()
  - 该模型定义的列数。

- string ColumnNameAt(int i)
  - 索引 i 处的列名。

- string ColumnTypeAt(int i)
  - 索引 i 处的列类型（含 NOT NULL / PRIMARY KEY 后缀）。

- static bool EqIgnoreCase(string a, string b)

- static int ColIndex(DbResult r, string name)

- static void FillFromSqlite(Model m, IDbExecutor db, string t)

- static void FillFromMysql(Model m, IDbExecutor db, string t)

- static void FillFromInformationSchema(Model m, IDbExecutor db, string t)

- static void FillFromTdengine(Model m, IDbExecutor db, string t)

- static string PrimaryKeyFromCatalog(IDbExecutor db, string t)

- void AddIntrospected(string name, string type, bool isPk, bool notNull)

- static Model FromTable(IDbExecutor db, string tableName)
  - 通过反射现有表结构来构建 Model（即
    “数据库优先”方向）。Provider 自动检测：SQLite 使用
    PRAGMA table_info，MySQL/MariaDB 使用 SHOW COLUMNS，TDengine 使用
    DESCRIBE，其余情况回退到 ANSI information_schema
    目录。原始 DB 列类型会被保留，以便后续
    CreateTable() 能够往返重建。

- static string StripMods(string type)

- static int IndexOfSub(string s, string sub)

- string ToDefineCode()
  - 生成重建该模型的 Zan 源码，形式为
    Model.Define(...) 链式调用——方便从现有表
    生成模型文件骨架（`FromTable(...).ToDefineCode()`）。


## ModelCell (class)

行上的单个列值：键和字符串值，组合成一个
实体，避免两者不同步。

- string key;

- string val;

- ModelCell(string key, string val)


## ModelColumn (class)

单个列定义：列名及完整 SQL 类型文本（含
NOT NULL / PRIMARY KEY / DEFAULT 等后缀）。用一个实体代替并行的
名称/类型列表。

- string name;

- string type;

- ModelColumn(string name, string type)


## ModelDelete (class)

模型级 DELETE 查询构建器。

- IDbExecutor db;

- QueryBuilder qb;

- ModelDelete(IDbExecutor db, string tableName)

- static ModelDelete New(IDbExecutor db, string tableName)

- ModelDelete Where(string condition)

- ModelDelete WhereEq(string column, string val)

- int Execute()
  - 执行 DELETE 并返回受影响的行数。

- int ExecuteAsync()


## ModelQuery (class)

模型级 SELECT 查询构建器。

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

- ModelQuery WhereLike(string column, string pattern)
  - 添加 `column LIKE ?`，使用绑定模式。

- ModelQuery WhereContains(string column, string val)
  - 添加 `column LIKE %value%`（子串匹配，已绑定）。

- ModelQuery WhereAnyLike(List<string> columns, string val)
  - 添加 `(col1 LIKE ? OR col2 LIKE ? OR ...)` — 一个关键字
    在多个列中搜索，全部绑定。这是典型的列表页
    关键字搜索；通过外层查询与 AND 组合。

- ModelQuery WhereNe(string column, string val)
  - 添加 `column <> ?`，使用绑定值。

- ModelQuery WhereGtInt(string column, int val)
  - 添加 `column > ?`，使用绑定整数。

- ModelQuery WhereGeInt(string column, int val)
  - 添加 `column >= ?`，使用绑定整数。

- ModelQuery WhereLtInt(string column, int val)
  - 添加 `column < ?`，使用绑定整数。

- ModelQuery WhereLeInt(string column, int val)
  - 添加 `column <= ?`，使用绑定整数。

- ModelQuery WhereBetween(string column, string low, string high)
  - 添加 `column BETWEEN ? AND ?`，使用绑定值。

- ModelQuery WhereNull(string column)
  - 添加 `column IS NULL`。

- ModelQuery WhereNotNull(string column)
  - 添加 `column IS NOT NULL`。

- ModelQuery WhereIn(string column, List<string> values)
  - 添加 `column IN (?, ...)`，使用绑定值。

- ModelQuery WhereNotIn(string column, List<string> values)
  - 添加 `column NOT IN (?, ...)`，使用绑定值。

- ModelQuery Page(int index, int size)
  - 一次调用设置 LIMIT n OFFSET (index-1)*size（index 从 1 开始）。

- bool Any()
  - 至少有一行匹配时返回 true。

- ModelQuery Limit(int count)

- ModelQuery Offset(int count)

- List<ModelRow> Execute()
  - 执行查询并返回 ModelRow 列表。

- List<ModelRow> ExecuteAsync()
  - 执行查询（包装方法）。

- ModelRow First()
  - 返回第一条结果，无结果时返回空行。

- int Count()
  - 返回满足查询条件的数量。


## ModelRow (class)

表示模型数据的单行（键值对）。

- List<ModelCell> cells;

- ModelRow()

- ModelRow Set(string key, string val)
  - 设置行上的一个值。

- ModelRow SetInt(string key, int val)
  - 设置一个整数值。

- string Get(string key)
  - 按键获取字符串值。

- int GetInt(string key)
  - 按键获取整数值。

- bool Has(string key)
  - 检查行中是否存在某个键。

- List<string> GetKeys()
  - 按插入顺序返回所有键。

- List<string> GetValues()
  - 按插入顺序返回所有值。

- static ModelRow FromDbResult(DbResult result, int rowIndex)
  - 从 DbResult 的一行创建 ModelRow。

- static List<ModelRow> FromDbResultAll(DbResult result)
  - 从 DbResult 的所有行创建 ModelRow 列表。

- string ToJson()
  - 将行转换为 JSON 字符串。


## ModelUpdate (class)

模型级 UPDATE 查询构建器。

- IDbExecutor db;

- QueryBuilder qb;

- ModelUpdate(IDbExecutor db, string tableName)

- static ModelUpdate New(IDbExecutor db, string tableName)

- ModelUpdate Set(string column, string val)

- ModelUpdate SetInt(string column, int val)

- ModelUpdate Where(string condition)

- ModelUpdate WhereEq(string column, string val)

- int Execute()
  - 执行 UPDATE 并返回受影响的行数。

- int ExecuteAsync()


## QueryBuilder (class)

仿 FreeSQL 的流式 SQL 查询构建器。
支持通过方法链执行 SELECT、INSERT、UPDATE、DELETE。

用法：
QueryBuilder qb = QueryBuilder.From("users");
string sql = qb.Where("age > 18").OrderBy("name").Limit(10).BuildSelect("*");

string insertSql = QueryBuilder.InsertInto("users")
.Set("name", "Alice").Set("age", "30").BuildInsert();

string updateSql = QueryBuilder.Update("users")
.Set("name", "Bob").Where("id = 1").BuildUpdate();

参数化模式（推荐——值永远不会进入 SQL 文本）：
QueryBuilder qb = QueryBuilder.From("users").WhereEq("name", name);
DbResult r = db.Query(qb.BuildSelectParams("*"), qb.SelectParams());

标识符（表、列、ORDER BY / GROUP BY 目标）会按
[A-Za-z_][A-Za-z0-9_.]* 校验；非法标识符抛出 Exception。
原始字符串子句（Where/OrWhere/Having/Join）不做参数化——
绝不要在其中放入用户输入；请改用 WhereEq/WhereIn/...。

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
  - 当 s 是安全的 SQL 标识符时返回 true：[A-Za-z_][A-Za-z0-9_.]*
    （点号允许用于 schema.table 这类限定名）。

- static string RequireIdent(string s)
  - s 是安全标识符时原样返回；否则抛出异常。

- static QueryBuilder From(string table)
  - 创建从某表 SELECT 的查询构建器。

- static QueryBuilder InsertInto(string table)
  - 创建向某表 INSERT INTO 的查询构建器。

- static QueryBuilder Update(string table)
  - 创建更新某表的查询构建器。

- static QueryBuilder DeleteFrom(string table)
  - 创建从某表 DELETE 的查询构建器。

- QueryBuilder Where(string condition)
  - 添加 WHERE 子句。

- QueryBuilder OrWhere(string condition)
  - 添加 OR WHERE 子句。

- QueryBuilder WhereEq(string column, string val)
  - 添加 `column = ?`，值作为绑定参数传递
    （防 SQL 注入：值永远不会进入 SQL 文本）。

- QueryBuilder WhereEqInt(string column, int val)
  - 添加 `column = ?`，使用整数参数。

- QueryBuilder WhereEqLong(string column, long val)
  - 添加 `column = ?`，使用 64 位整数参数。

- QueryBuilder WhereGeLong(string column, long val)
  - 添加 `column >= ?`，使用 64 位整数参数。

- QueryBuilder WhereLeLong(string column, long val)
  - 添加 `column <= ?`，使用 64 位整数参数。

- QueryBuilder WhereValueAt(DbValues vals, int i)
  - 添加 `column = ?`，值按 DbValues 里声明的类型绑定
    （运行期成形的条件：提交的筛选表单、解析出的 JSON）。

- QueryBuilder WhereIn(string column, List<string> values)
  - 添加 WHERE column IN (?, ?, ...)，使用绑定参数。

- QueryBuilder WhereNotIn(string column, List<string> values)
  - 添加 WHERE column NOT IN (?, ?, ...)，使用绑定参数。

- QueryBuilder WhereLike(string column, string pattern)
  - 添加 WHERE column LIKE ?，使用绑定参数。

- QueryBuilder WhereNotLike(string column, string pattern)
  - 添加 WHERE column NOT LIKE ?，使用绑定参数。

- QueryBuilder WhereContains(string column, string val)
  - 添加 `column LIKE %value%`（子串匹配，已绑定）。

- QueryBuilder WhereStartsWith(string column, string val)
  - 添加 `column LIKE value%`（前缀匹配，已绑定）。

- QueryBuilder WhereEndsWith(string column, string val)
  - 添加 `column LIKE %value`（后缀匹配，已绑定）。

- QueryBuilder WhereAnyLike(List<string> columns, string val)
  - 添加 `(col1 LIKE ? OR col2 LIKE ? OR ...)`，单个关键字
    绑定到每一列（子串匹配）。整个组是一个 WHERE
    操作数，因此可与 AND 组合为 `... AND (a LIKE ? OR b LIKE ?)`。
    空列列表时不做任何操作；值始终绑定，绝不内联。

- QueryBuilder WhereNe(string column, string val)
  - 添加 `column <> ?`，使用绑定参数。

- QueryBuilder WhereNeInt(string column, int val)
  - 添加 `column <> ?`，使用整数参数。

- QueryBuilder WhereGtInt(string column, int val)
  - 添加 `column > ?`，使用整数参数。

- QueryBuilder WhereGeInt(string column, int val)
  - 添加 `column >= ?`，使用整数参数。

- QueryBuilder WhereLtInt(string column, int val)
  - 添加 `column < ?`，使用整数参数。

- QueryBuilder WhereLeInt(string column, int val)
  - 添加 `column <= ?`，使用整数参数。

- QueryBuilder WhereBetween(string column, string low, string high)
  - 添加 WHERE column BETWEEN ? AND ?，使用绑定参数。

- QueryBuilder WhereNull(string column)
  - 添加 WHERE IS NULL 子句。

- QueryBuilder WhereNotNull(string column)
  - 添加 WHERE IS NOT NULL 子句。

- QueryBuilder OrderBy(string column)
  - 设置 ORDER BY 子句（列名已做标识符校验）。

- QueryBuilder OrderByDesc(string column)
  - 设置 ORDER BY DESC 子句（列名已做标识符校验）。

- QueryBuilder GroupBy(string column)
  - 设置 GROUP BY 子句（列名已做标识符校验）。

- QueryBuilder Having(string condition)
  - 设置 HAVING 子句。

- QueryBuilder Join(string table, string on)
  - 添加 JOIN 子句。

- QueryBuilder LeftJoin(string table, string on)
  - 添加 LEFT JOIN 子句。

- QueryBuilder RightJoin(string table, string on)
  - 添加 RIGHT JOIN 子句。

- QueryBuilder Limit(int count)
  - 设置 LIMIT。

- QueryBuilder Offset(int count)
  - 设置 OFFSET。

- QueryBuilder Dialect(int d)
  - 选择 SQL 分页方言（参见 SqlDialect）。

- QueryBuilder Distinct()
  - 设置 DISTINCT 标志。

- QueryBuilder Set(string column, string val)
  - 为 INSERT/UPDATE 设置 column=value 对。

- QueryBuilder SetParam(string column)
  - 声明一个 column = ? 赋值，值由调用方自己绑定
    （运行期成形的列：DbValues 按它记录的类型绑定，值不必
    先变成字符串）。与 BuildInsertParams / BuildUpdateParams
    搭配使用，绑定顺序即声明顺序。

- QueryBuilder SetInt(string column, int val)
  - 设置整数形式的 column=value 对。

- string BuildSelectParams(string columns)
  - 构建带 `?` 占位符的 SELECT（参见 SelectParams()）。

- DbParams SelectParams()
  - BuildSelectParams / BuildDeleteParams 的参数。

- string BuildInsertParams()
  - 构建带 `?` 占位符的 INSERT（参见 InsertParams()）。

- DbParams InsertParams()
  - BuildInsertParams 的参数（按顺序排列的 Set 值）。

- string BuildUpdateParams()
  - 构建带 `?` 占位符的 UPDATE（参见 UpdateParams()）。

- DbParams UpdateParams()
  - BuildUpdateParams 的参数（先是 Set 值，再是 WHERE 值）。

- string BuildDeleteParams()
  - 构建带 `?` 占位符的 DELETE（参见 SelectParams()）。

- string BuildSelect(string columns)
  - 构建 SELECT 查询（旧版：参数内联）。

- string BuildSelect(string columns, bool keepPlaceholders)

- string BuildCount()
  - 构建 SELECT COUNT 查询。

- string BuildInsert()
  - 构建 INSERT 查询。

- string BuildUpdate()
  - 构建 UPDATE 查询。

- string BuildDelete()
  - 构建 DELETE 查询（旧版：参数内联）。

- static string InlineParams(string sql, DbParams prms)
  - 将 `?` 占位符（引号字面量之外）替换为
    带引号、已转义的参数值，供旧版纯字符串执行使用。

- void AppendClauses(StringBuilder sb)

- static string EscapeString(string val)
  - 转义字符串中的单引号以确保 SQL 安全。


## SetClause (class)

INSERT/UPDATE 的单个 column=value 赋值，合并为一个
实体，而非并行的列/值列表。

- string column;

- string val;

- SetClause(string column, string val)


## SqlDialect (class)

SQL 分页方言。不同引擎对 LIMIT/OFFSET 的写法不同。
- Standard：LIMIT n OFFSET m（SQLite、MySQL、PostgreSQL、ClickHouse、
QuestDB、DuckDB、Firebird 3+）
- SqlServer：OFFSET m ROWS FETCH NEXT n ROWS ONLY（需要 ORDER BY）
- Oracle：OFFSET m ROWS FETCH NEXT n ROWS ONLY（12c+）

- static int Standard=0;

- static int SqlServer=1;

- static int Oracle=2;
