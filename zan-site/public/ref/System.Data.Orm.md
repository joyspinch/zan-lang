# System.Data.Orm

> 源码: `packages/Zan.Data/src/System/Data/Orm/DbSchema.zan`, `packages/Zan.Data/src/System/Data/Orm/DbTable.zan`, `packages/Zan.Data/src/System/Data/Orm/ExprSql.zan`, `packages/Zan.Data/src/System/Data/Orm/IOrmRows.zan`, `packages/Zan.Data/src/System/Data/Orm/Model.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmCond.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmDialect.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmInsert.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmMeta.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmSelect.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmSync.zan`, `packages/Zan.Data/src/System/Data/Orm/OrmWrite.zan`, `packages/Zan.Data/src/System/Data/Orm/QueryBuilder.zan`


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

- DbTableQuery WhereIn(string column, List<int> values)
  - column IN (整型列表)，逐个占位符；空列表按恒假处理。

- DbTableQuery WhereIn(string column, List<long> values)
  - column IN (64 位整数列表)，逐个占位符；空列表按恒假处理。

- DbTableQuery OrderBy(string column)
  - 升序排序。

- DbTableQuery OrderByDesc(string column)
  - 降序排序。

- DbTableQuery OrderBySafe(string column, List<string> allowed)
  - 白名单版排序（升序）：allowed 之外的列一律抛异常。
    排序列无法参数化，可能来自用户输入（?sort=）时必须持单。

- DbTableQuery OrderBySafeDesc(string column, List<string> allowed)
  - 白名单版排序（降序）。

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

- async int ExecuteUpdateAsync(DbValues row)
  - 按 DbValues 更新符合条件的行，返回更新行数。SET 值排在
    WHERE 值之前，与 BuildUpdateParams 的占位符顺序一致。

- async DbResult run(string columns)
  - 执行 SELECT：套用已设置的 Limit/Offset 后按给定列清单查询。


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
  - 保存迁移名与 UP/DOWN SQL；外部经 `New` 构造。

- static Migration New(string name, string up, string down)
  - 创建一条迁移。

- static int Exec(IDbExecutor db, string sql)
  - 执行一条迁移语句；执行器以负数报告失败时抛出
    `DbException`——迁移绝不能悄悄落空，
    否则记录表照样登记，失败被永久掩盖（内置驱动语句失败
    本就直接抛，这里给不抛异常的自定义执行器兜底）。

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
  - 以已校验的表名初始化空模型；外部入口是 `Define`。

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
  - 忽略 ASCII 大小写比较两个字符串。

- static int ColIndex(DbResult r, string name)
  - 按列名（忽略大小写）在查询结果中找下标；未找到返回 -1。

- static void FillFromSqlite(Model m, IDbExecutor db, string t)
  - 用 PRAGMA table_info 把 SQLite 表内省进模型。

- static void FillFromMysql(Model m, IDbExecutor db, string t)
  - 用 SHOW COLUMNS 把 MySQL/MariaDB 表内省进模型。

- static void FillFromInformationSchema(Model m, IDbExecutor db, string t)
  - 用 information_schema.columns 内省表（PG / SQL Server 等 ANSI 目录），
    主键尽力从目录补取。

- static void FillFromTdengine(Model m, IDbExecutor db, string t)
  - 用 DESCRIBE 内省 TDengine 表；首行 timestamp 列视作主键。

- static string PrimaryKeyFromCatalog(IDbExecutor db, string t)
  - 从 ANSI 目录查表的主键列名；无主键或查询失败时返回空串。

- void AddIntrospected(string name, string type, bool isPk, bool notNull)
  - 追加内省得到的列，按需附加 PRIMARY KEY / NOT NULL 后缀。

- static Model FromTable(IDbExecutor db, string tableName)
  - 通过反射现有表结构来构建 Model（即
    “数据库优先”方向）。Provider 自动检测：SQLite 使用
    PRAGMA table_info，MySQL/MariaDB 使用 SHOW COLUMNS，TDengine 使用
    DESCRIBE，其余情况回退到 ANSI information_schema
    目录。原始 DB 列类型会被保留，以便后续
    CreateTable() 能够往返重建。

- static string StripMods(string type)
  - 去掉类型文本尾部的 PRIMARY KEY / NOT NULL / UNIQUE 修饰。

- static int IndexOfSub(string s, string sub)
  - 朴素子串查找，返回首个下标；未找到返回 -1。

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
  - 构造一个键值单元格。


## ModelColumn (class)

单个列定义：列名及完整 SQL 类型文本（含
NOT NULL / PRIMARY KEY / DEFAULT 等后缀）。用一个实体代替并行的
名称/类型列表。

- string name;

- string type;

- ModelColumn(string name, string type)
  - 保存列名与完整类型文本（类型文本经 RequireDdlType 校验）。


## ModelDelete (class)

模型级 DELETE 查询构建器。

- IDbExecutor db;

- QueryBuilder qb;

- ModelDelete(IDbExecutor db, string tableName)
  - 记录执行器，条件构建复用 `QueryBuilder`。

- static ModelDelete New(IDbExecutor db, string tableName)
  - 为表开启 DELETE 构建。

- ModelDelete Where(string condition)
  - 追加原始条件（AND 连接）。

- ModelDelete WhereEq(string column, string val)
  - 追加 column = ?（绑定值）。

- int Execute()
  - 执行 DELETE 并返回受影响的行数。

- int ExecuteAsync()
  - `Execute` 的包装方法。


## ModelQuery (class)

模型级 SELECT 查询构建器。

- IDbExecutor db;

- string tableName;

- QueryBuilder qb;

- ModelQuery(IDbExecutor db, string tableName)
  - 记录执行器与表名，条件构建复用 `QueryBuilder`。

- static ModelQuery NewSelect(IDbExecutor db, string tableName)
  - 为表开启 SELECT 构建。

- ModelQuery Where(string condition)
  - 追加原始条件（AND 连接）。

- ModelQuery WhereEq(string column, string val)
  - 追加 column = ?（绑定值）。

- ModelQuery WhereEqInt(string column, int val)
  - 追加 column = ?（绑定整数）。

- ModelQuery OrderBy(string column)
  - 按列升序排序。

- ModelQuery OrderByDesc(string column)
  - 按列降序排序。

- ModelQuery OrderByDescending(string column)
  - `OrderByDesc` 的别名。

- ModelQuery OrderBySafe(string column, List<string> allowed)
  - 白名单版排序（升序）：allowed 之外的列一律抛异常。
    排序列无法参数化，可能来自用户输入（?sort=）时必须持单。

- ModelQuery OrderBySafeDesc(string column, List<string> allowed)
  - 白名单版排序（降序）。

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
  - 设置 LIMIT。

- ModelQuery Offset(int count)
  - 设置 OFFSET。

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
  - 构造空行。

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
  - 记录执行器，条件与赋值构建复用 `QueryBuilder`。

- static ModelUpdate New(IDbExecutor db, string tableName)
  - 为表开启 UPDATE 构建。

- ModelUpdate Set(string column, string val)
  - 设置 column = ?（绑定值）。

- ModelUpdate SetInt(string column, int val)
  - 设置 column = ?（绑定整数）。

- ModelUpdate Where(string condition)
  - 追加原始条件（AND 连接）。

- ModelUpdate WhereEq(string column, string val)
  - 追加 column = ?（绑定值）。

- int Execute()
  - 执行 UPDATE 并返回受影响的行数。

- int ExecuteAsync()
  - `Execute` 的包装方法。


## OrmCol (class)

一个实体列的运行期描述。编译期只发射这些描述（表名、列名、
字段名、类型码、主键/自增/长度），SQL 文本、参数绑定与行映射
全部由本目录下的共享实现按描述完成 —— 实体和列增加时只多几行
元数据，不再多出一份 SQL 拼装代码。

Kind 与编译期 <c>DbField.Kind</c> 一致：0 int、1 double、
2 bool、3 string、4 enum；<c>I64</c> 区分 64 位整数列。

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
  - DDL 类型码（`OrmDialect.Ty`）：
    0 int、1 double、2 bool、3 string、4 bigint。

- int PKind()
  - 动态值按列声明类型转换时用的 DbParams 类型码。


## OrmCond (class)

一条声明式查询条件：声明式列表页（ListPage.Collect 一类）
的产出，经 `OrmSelect.WhereConds` 变成参数化 WHERE。

col/kind/value 都是声明侧给定的自由文本，但各自的出口都被 ORM 收口：
col 在 `OrmSelect` 里按实体元数据校验（未知列抛异常），
kind 只认 LIKE/EQ/GE/LE，value 永远走绑定参数——用户输入进不了
SQL 文本。name 是来源参数名（回填分页链接用，可空）。

- string name;

- string col;

- string kind;

- string value;

- static long Num(string s)
  - 宽松数字解析：可选正负号 + 纯数字，其余形态一律 0
    （与 Web 层 Filter.ToInt 的容错一致）。EQ/GE/LE 的 value 在
    WhereConds 里经它落到整型绑定。


## OrmDelete (class)

类型化 DELETE 的运行期实现：条件片段 + 绑定值。
没有条件时不执行（不生成全表删除）。

- IDbExecutor db;

- OrmMeta meta;

- string tbl;

- string w;

- DbParams ps;

- bool on;

- OrmDelete(IDbExecutor db, OrmMeta meta)
  - 私有构造；统一经 `Create` 创建。

- static OrmDelete Create(IDbExecutor db, OrmMeta meta)
  - 为实体创建一个空 DELETE 构造器。

- void AsTable(string t)
  - 改写到另一张表（标识符校验，防注入）。

- void CondBegin(bool c)
  - 条件开关：false 期间 W/P* 收集的片段全部失效（ORM 内部
    按条件组合调用，应用层一般不直接用）。

- void CondEnd()
  - 结束条件区间，恢复片段收集。

- void W(string f)
  - WHERE 片段：多个片段以 AND 连接。

- void WhereDict(DbValues v)
  - 字典键值批量进 WHERE（等值条件，列名经元数据校验）。

- string RequireCol(string c)
  - 校验列名属于本实体（剥掉可选的 "t." 前缀）；未知列抛异常。

- void WhereEq(string c, string v)
  - column = 文本值。

- void WhereEq(string c, int v)
  - column = 整数值。

- void WhereEq(string c, long v)
  - column = 64 位整数值。

- void WhereIn(string c, List<int> vs)
  - column IN (逐个占位符)；空列表按恒假处理，不生成 `IN ()`。

- void WhereIn(string c, List<long> vs)
  - `WhereIn` 的 long 版本。

- void WhereNone()
  - 恒假条件：数据边界为空集时用它表达"一行都不许删"。

- void P(string v)
  - WHERE 参数：与 W 片段中占位符的出现顺序一致。

- void Pi(int v)
  - WHERE 整数参数。

- void Pl(long v)
  - WHERE 64 位整数参数。

- void Pd(double v)
  - WHERE 浮点参数。

- void InI(List<int> vs)
  - IN 列表参数（配合 W 片段里的 "col IN (?)"）。

- void InL(List<long> vs)
  - `InI` 的 long 版本。

- void InS(List<string> vs)
  - `InI` 的 string 版本。

- void InD(List<double> vs)
  - `InI` 的 double 版本。

- int ExecuteAffrows()
  - 执行 DELETE 并返回受影响行数；无 WHERE 时不执行（不生成
    全表删除）。

- async int ExecuteAffrowsAsync()
  - 异步执行 DELETE。


## OrmDialect (class)

方言与目录探测：列类型、upsert 写法、表/列/索引存在性、自增值。
以前这些文本由代码生成器抄进每个用户产物，现在只有这一份实现，
改 SQL 规则只改这里。

- static string Marks(int n)
  - `?, ?, ?` —— n 个占位符（n 为 0 时是 NULL）。

- static bool Has(List<string> l, string v)
  - 列表是否包含某值。

- static string Unalias(string f)
  - WHERE 片段里的 `t.` 别名在 UPDATE/DELETE 上不成立。

- static DbParams Cat(DbParams a, DbParams b)
  - 两段参数按顺序合成一段（SET 在前、WHERE 在后）。

- static void CopyInto(DbParams src, DbParams dst)
  - 按各自的类型码把参数逐个复制到 dst。

- static string ConflictHead(int p, List<string> keys, bool nop)
  - upsert 的冲突头：各家写法不同（ON CONFLICT /
    ON DUPLICATE KEY / MERGE 前的 ON ...）。

- static string Excl(int p, string c, int md)
  - 冲突时一列的新值：直接覆盖（0）、累加（1）、
    取大（2）、取小（3）。

- static int Mode(List<string> acc, List<string> mx, List<string> mn, string c)
  - 列 c 的冲突更新模式码：累加 1、取大 2、取小 3、覆盖 0
    （与 `Excl` 的 md 对应）。

- static string LastIdSql(IDbExecutor db)
  - 刚插入行的自增值。没有会话级「最后自增 id」的
    引擎（Firebird 用 INSERT … RETURNING 读生成器，TDengine
    没有自增列）直接抛错，而不是发出别的方言的 SQL 让
    服务器报一条费解的错。

- static int LastId(IDbExecutor db)
  - 查询刚插入行的自增值；无行时为 0。

- static async int LastIdAsync(IDbExecutor db)
  - `LastId` 的异步版本。

- static string OwnScope(int p)
  - 目录探测的库/schema 作用域过滤子句（限定在连接自己的库，按 provider 给出）。

- static bool HasInformationSchema(int p)
  - 该 provider 是否提供 information_schema 目录。

- static string HasTableSql(IDbExecutor db)
  - 表存在性探测 SQL（SQLite 查 sqlite_master、Firebird 查
    RDB$RELATIONS、其余查 information_schema，均带参数占位符）；
    不支持目录探测的 provider 直接抛错。

- static bool HasTable(IDbExecutor db, string t)
  - 表是否存在。

- static async bool HasTableAsync(IDbExecutor db, string t)
  - `HasTable` 的异步版本。

- static string ColsSql(int p)
  - information_schema 列名查询（限自身库/schema）。

- static List<string> Cols(IDbExecutor db, string t)
  - 表现有的列名清单：SQLite 走 PRAGMA table_info，Firebird 走
    RDB$RELATION_FIELDS（并截掉 CHAR(31) 的尾部空格），
    其余走 information_schema；不支持的 provider 抛错。

- static async List<string> ColsAsync(IDbExecutor db, string t)
  - `Cols` 的异步版本。

- static string HasIndexSql(int p)
  - 二级索引存在性探测 SQL（按方言给出，带参数占位符）。

- static DbParams IndexProbeParams(int p, string nm, string t)
  - `HasIndexSql` 对应的探测参数：
    按表名一起比对的方言为 (索引名, 表名)，其余为 (索引名)。

- static string IndexDdl(int p, string nm, string t, string cols, bool uniq)
  - CREATE [UNIQUE] INDEX 语句文本。

- static void EnsureIndex(IDbExecutor db, string nm, string t, string cols, bool uniq)
  - 索引不存在时创建；DDL 失败（负返回值）抛
    `DbException`。

- static async void EnsureIndexAsync(IDbExecutor db, string nm, string t, string cols, bool uniq)
  - `EnsureIndex` 的异步版本。

- static string Ty(int p, int kind, bool pk, bool ident, int len)
  - 列的 DDL 类型。kind：0 int/enum、1 double、2 bool、
    3 string、4 64 位整数。


## OrmIndex (class)

实体上声明的二级索引。

- string Name;

- string Cols;

- bool Unique;

- OrmIndex(string name, string cols, bool uniq)


## OrmInsert (class)

类型化 INSERT 的运行期实现：实体行与动态列（DbValues）两条来源，
Only/Skip 决定的列集合在运行期成形，upsert 子句按方言拼装。

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
  - 私有构造；统一经 `Create` 创建。

- static OrmInsert Create(IDbExecutor db, OrmMeta meta)
  - 为实体创建一个空的 INSERT 构造器。

- void AsTable(string t)
  - 改写到另一张表（标识符校验，防注入）。

- void SetRows(IOrmRows rows)
  - 实体行来源：由编译期为该实体发的取值口提供。

- int RowCount()
  - 实体行数。

- void AddDict(DbValues v)
  - 追加一行动态列（列名经元数据校验，未知列抛异常）。

- void Only(string c)
  - 只插入该列（可多次调用，与 Skip 互斥语义）。

- void Skip(string c)
  - 跳过该列（可多次调用）。

- void OC(string c)
  - upsert：冲突时把该列更新为新值；键列缺省取主键。

- void ACC(string c)
  - upsert：冲突时该列累加（counter 语义）。

- void GMX(string c)
  - upsert：冲突时该列取更大值（GREATEST/max）。

- void GMN(string c)
  - upsert：冲突时该列取更小值。

- void NOP()
  - upsert：冲突时什么都不改（DO NOTHING）。

- int DictCount()
  - 动态列行数。

- bool Use(string c, bool identity)
  - 这一列是否进语句：Only 优先，其次自增列不写，
    最后看 Skip。

- List<OrmCol> used()
  - 依 Only / 自增列 / Skip 规则选出参与语句的列集合。

- string colList(List<OrmCol> cols)
  - 把列集合拼成逗号分隔的列名清单。

- string Conflict()
  - 冲突子句。键列缺省取实体主键；方言差异
    （excluded 对 VALUES()、GREATEST 对 max）在 OrmDialect 里解决。

- int BuildDicts()
  - 动态列写入：所有行按第一行的列形状对齐，缺的列写
    NULL，值按列声明类型绑定。

- int BuildRows()
  - 实体行写入：列集合成形一次，每行按同一集合绑定。

- int ExecuteDicts()
  - 执行动态列 INSERT，返回受影响行数。

- async int ExecuteDictsAsync()
  - 异步执行动态列 INSERT。

- int ExecuteAffrows()
  - 执行插入（动态列优先，否则实体行），返回受影响行数。

- async int ExecuteAffrowsAsync()
  - 异步执行插入。

- int ExecuteIdentity()
  - 插入单行并取自增主键值（0 = 插入失败或无自增列）。

- async int ExecuteIdentityAsync()
  - 异步插入并取自增主键值。


## OrmMeta (class)

一个实体的全部运行期元数据。生成的代码为每个实体建一次并缓存，
之后所有 SELECT/INSERT/UPDATE/DELETE/CodeFirst 都读它。

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
  - 新建一个实体的元数据。

- OrmMeta Seal()
  - 封存元数据：`Seal` 之后的构建调用一律
    抛异常。生成的代码在元数据建成并发布到静态缓存前调用它，
    保证运行期描述不会被后续代码悄悄改形。

- void CheckNotFrozen()
  - 封存后的构建调用统一经此拒绝。

- OrmMeta Col(string col, string field, int kind, bool i64, bool pk, bool ident, bool notNull, int len)
  - 追加一列（按实体字段声明顺序）。

- OrmNav Nav(string field, string table)
  - 追加一个导航字段，返回它以便继续追加它的列。

- OrmMeta Index(string name, string cols, bool uniq)
  - 追加一条索引声明。

- OrmCol Find(string c)
  - 列描述，不存在时为 null。

- bool Has(string c)
  - 列是否属于本实体。

- int KindOf(string c)
  - 列的声明类型（DbParams.Kind*），不存在时 KindNull。

- string Require(string c)
  - 校验列名属于本实体，动态列集合的唯一入口。

- string SelectList()
  - `t.a, t.b` —— 实体自身列的 SELECT 片段（缓存）。

- List<string> PkCols()
  - 主键列名（声明顺序）。

- OrmCol PkCol()
  - UPDATE ... WHERE 用的定位列：最后一个主键列，
    没有主键时为 null（与实体链的既有语义一致）。

- string PkList()
  - 复合主键的表级约束列清单（两列及以上才有；
    identity 列自带 PRIMARY KEY，不参与）。


## OrmNav (class)

导航字段（`[Column]` 之外的实体引用）的连接描述：
连接 `LEFT JOIN <Table> j_<Field> ON t.<Field>Id = j_<Field>.id`，
被包含时它的列跟在实体自身列之后。

- string Field;

- string Table;

- List<OrmCol> Cols;

- int PkAt;

- OrmNav(string field, string table)

- OrmNav Col(string col, string field, int kind, bool i64)
  - 追加一列被连接实体的列。

- string SelectList()
  - `j_<Field>.a, j_<Field>.b` —— 连接列的 SELECT 片段。

- string Join()
  - 连接子句。


## OrmSelect (class)

类型化 SELECT 链的运行期实现：条件片段、绑定值、分组、排序、
分页、连接与终端 SQL 都在这里，一份实现服务所有实体。生成的
`__DbQ_<E>` 只是它的类型化门面（转发 + 用实体类型收口结果）。

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
  - 私有构造；统一经 `Create` 创建。

- static OrmSelect Create(IDbExecutor db, OrmMeta meta)
  - 为实体创建一个空 SELECT 链。

- OrmMeta Meta()
  - 实体元数据（类型化门面读取列信息用）。

- DbParams Params()
  - 当前积累的 WHERE 参数（类型化门面直接执行时复用）。

- string GroupKey()
  - 分组键（类型化门面按分组取值用）。

- bool NavOn(int i)
  - 第 i 个导航字段是否被包含。

- void W(string f)
  - WHERE 片段：多个片段以 AND 连接（列名一般带 "t." 别名）。

- void WExpr(ExprNode root)
  - lambda 降级出的表达式树：SQL 片段与绑定值都由
    `ExprSql` 产出。

- void WhereDict(DbValues v)
  - 动态列集合等值条件：列名按实体校验，值按列声明类型
    绑定。

- void CondBegin(bool c)
  - 条件开关：false 期间 W/P* 收集的片段全部失效（ORM 内部
    按条件组合调用）。

- void CondEnd()
  - 结束条件区间，恢复片段收集。

- void AsTable(string t)
  - 查另一张表（标识符校验，防注入）。

- void P(string v)
  - WHERE 参数：与 W 片段中占位符的出现顺序一致。

- void Pi(int v)
  - WHERE 整数参数。

- void Pl(long v)
  - WHERE 64 位整数参数。

- void Pd(double v)
  - WHERE 浮点参数。

- void InI(List<int> vs)
  - IN 列表参数（配合 W 片段里的 "t.col IN (?)"）。

- void InL(List<long> vs)
  - `InI` 的 long 版本。

- void InS(List<string> vs)
  - `InI` 的 string 版本。

- void InD(List<double> vs)
  - `InI` 的 double 版本。

- string RequireCol(string c)
  - 校验列名属于本实体并补全 "t." 别名；未知列抛异常。

- void WhereEq(string c, string v)
  - column = 文本值。

- void WhereEq(string c, int v)
  - column = 整数值。

- void WhereEq(string c, long v)
  - column = 64 位整数值。

- void WhereGe(string c, long v)
  - column >= 整数值。

- void WhereLe(string c, long v)
  - column <= 整数值。

- void WhereIn(string c, List<int> vs)
  - column IN (逐个占位符)；空列表按恒假处理，不生成 `IN ()`。

- void WhereIn(string c, List<long> vs)
  - `WhereIn` 的 long 版本。

- void WhereIn(string c, List<string> vs)
  - `WhereIn` 的 string 版本。

- void WhereLikeAny(string cols, string v)
  - 多列 OR LIKE："username|email" 一并子串匹配同一个值，每列绑定
    自己的参数，列名逐一校验。

- void WhereNone()
  - 恒假条件：数据边界为空集时用它表达"一行都不许看"。

- void WhereConds(List<OrmCond> cs)
  - 声明式条件（`OrmCond` 列表）落成参数化 WHERE：
    LIKE 走多列 OR 匹配，EQ/GE/LE 按宽松数字解析后绑定。

- void OB(string col)
  - ORDER BY 追加一列（升序）。

- void OrderBy(string col)
  - ORDER BY 别名（支持带 DESC/ASC 的原生表达式片段）。

- void OBD(string col)
  - ORDER BY 追加一列（降序）。

- void OrderBySafe(string col)
  - ORDER BY 白名单版（升序）：列名必须属于本实体
    （`RequireCol` 同款校验，未知列抛异常）。ORDER BY
    无法参数化——排序列进的是 SQL 文本，可能来自用户输入（?sort=）
    的列一律走这里，让实体元数据做白名单。

- void OrderBySafeDesc(string col)
  - ORDER BY 白名单版（降序）。

- void GB(string col)
  - GROUP BY 单列。

- void OBK()
  - 便捷：按分组键排序（升序）。

- void OBDK()
  - 便捷：按分组键排序（降序）。

- void H(string f)
  - HAVING 片段：多个片段以 AND 连接。

- void DISTINCT()
  - SELECT DISTINCT。

- void Take(int n)
  - LIMIT n。

- void SkipN(int n)
  - OFFSET n。

- void Page(int index, int size)
  - 1 起始的分页（换算成 LIMIT/OFFSET，方言在 BuildSelect 处理）。

- void Include(int i)
  - 包含第 i 个导航字段（LEFT JOIN + 它的列）。

- int NavAt(int i)
  - 被包含的导航字段在结果里的起始列号。

- void tail(StringBuilder sb)
  - 把 WHERE/GROUP BY/HAVING/ORDER BY 片段追加到 SELECT 文本之后。

- string BuildSelect()
  - 实体行的 SELECT。分页写法按方言：SQL Server/Oracle 用
    OFFSET ... FETCH（且必须有 ORDER BY）。

- DbResult Rows()
  - 执行实体行查询。

- async DbResult RowsAsync()
  - 异步执行实体行查询。

- string BuildCol(string col)
  - 单列/投影的 SELECT（分页在 DTO 版里按 LIMIT/OFFSET）。

- string BuildDto(string cols)
  - 投影 SELECT 的语句文本（固定 LIMIT/OFFSET 分页写法）。

- List<string> MapCol(DbResult r)
  - 把结果集第一列抽成字符串列表。

- List<string> ToListCol(string col)
  - 执行单列查询并返回字符串列表。

- async List<string> ToListColAsync(string col)
  - `ToListCol` 的异步版本。

- DbResult Dto(string cols)
  - 执行投影查询（DTO 列表，LIMIT/OFFSET 分页）。

- async DbResult DtoAsync(string cols)
  - 异步执行投影查询。

- void One()
  - 只取一行（LIMIT 1）。

- string BuildCount()
  - 分组后的行数：分组键上的 COUNT 要套一层子查询。
    派生表必须带别名——MySQL/PostgreSQL/SQL Server 对无别名
    的子查询直接报语法错。

- string BuildAgg(string expr)
  - 聚合 SELECT 的语句文本（表达式如 "COUNT(*)"、"SUM(t.n)"），
    复用 WHERE/GROUP BY/HAVING，忽略排序与分页。

- DbResult AggR(string expr)
  - 执行聚合查询，返回原始结果集。

- async DbResult AggRAsync(string expr)
  - `AggR` 的异步版本。

- int AggI(string expr)
  - 聚合结果取 int；无行时为 0。

- long AggL(string expr)
  - 聚合结果取 long；无行时为 0。

- double AggD(string expr)
  - 聚合结果取 double；无行时为 0.0。

- async int AggIAsync(string expr)
  - `AggI` 的异步版本。

- async long AggLAsync(string expr)
  - `AggL` 的异步版本。

- async double AggDAsync(string expr)
  - `AggD` 的异步版本。

- int Count()
  - 匹配的行数（分组查询时数分组数）。

- async int CountAsync()
  - 异步行数。

- bool Any()
  - 是否存在匹配行。

- async bool AnyAsync()
  - 异步判断是否存在匹配行。


## OrmSync (class)

CodeFirst：缺表建表、缺列加列、缺索引补索引。DDL 从实体元数据
直接算出来，同步与异步两条路径共用同一段文本构造。

- static string CreateDdl(int p, OrmMeta m, string t)
  - CREATE TABLE 语句。复合主键写成表级约束（列定义里
    不能各自带 PRIMARY KEY）。

- static string AddColDdl(int p, OrmCol c, string t)
  - ALTER TABLE ... ADD COLUMN 语句（新列不带主键/自增修饰）。

- static void ExecDdl(IDbExecutor db, string ddl)
  - 执行一条 DDL；执行器以负数报告失败（内置驱动
    语句失败会直接抛 DbException，这里给不抛异常的
    自定义执行器兜底）时同样抛出，绝不让建表/加列悄悄落空。

- static async void ExecDdlAsync(IDbExecutor db, string ddl)
  - `ExecDdl` 的协程版本，失败约定相同。

- static void Sync(IDbExecutor db, OrmMeta m)
  - 同步路径的 schema 同步：缺表则建表并建全部索引；表已存在时
    逐列比对、缺列加列，最后补齐索引（幂等，可重复调用）。

- static async void SyncAsync(IDbExecutor db, OrmMeta m)
  - `Sync` 的协程版本。

- static void Indexes(IDbExecutor db, OrmMeta m, string t)
  - 按元数据补齐实体声明的全部索引（EnsureIndex 幂等）。

- static async void IndexesAsync(IDbExecutor db, OrmMeta m, string t)
  - `Indexes` 的协程版本。


## OrmUpdate (class)

类型化 UPDATE 的运行期实现：SET 片段与 WHERE 片段各自带一段参数，
执行时按 SET→WHERE 的顺序合并。实体整体更新（SetSource）在执行
时按元数据展开：非主键列进 SET，主键列进 WHERE。

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
  - 私有构造；统一经 `Create` 创建。

- static OrmUpdate Create(IDbExecutor db, OrmMeta meta)
  - 为实体创建一个空 UPDATE 构造器。

- void AsTable(string t)
  - 改写到另一张表（标识符校验，防注入）。

- void Only(string c)
  - 只更新该列（与 Skip 互斥语义，可多次调用）。

- void Skip(string c)
  - 跳过该列（可多次调用）。

- void SetSource(IOrmRows rows)
  - 实体整体更新的值来源（取第一行）。

- void CondBegin(bool c)
  - 条件开关：false 期间 W/P* 收集的片段全部失效（ORM 内部
    按条件组合调用，应用层一般不直接用）。

- void CondEnd()
  - 结束条件区间，恢复片段收集。

- bool Use(string c)
  - 该列是否进 SET：Only 优先，其次非 Skip。

- void Frag(string f)
  - 追加一段 SET 片段（片段间以逗号连接）。

- void SetI(string c, int v)
  - SET 片段：追加一个 "col = ?" 并把值排入 SET 参数区。

- void SetL(string c, long v)
  - `SetI` 的 long 版本。

- void SetD(string c, double v)
  - `SetI` 的 double 版本。

- void SetS(string c, string v)
  - `SetI` 的 string 版本。

- void SetB(string c, bool v)
  - 布尔值写整型 0/1。

- void SetIncrI(string c, int v)
  - 自增片段："col = col + ?"（数据库侧原值累加）。

- void SetIncrL(string c, long v)
  - `SetIncrI` 的 long 版本。

- void SetIncrD(string c, double v)
  - `SetIncrI` 的 double 版本。

- void SetDict(DbValues v)
  - 字典键值批量进 SET（列名经元数据校验，按列类型绑定）。

- void W(string f)
  - WHERE 片段：多个片段以 AND 连接。

- void WhereDict(DbValues v)
  - 字典键值批量进 WHERE（等值条件）。

- string RequireCol(string c)
  - 校验列名属于本实体（剥掉可选的 "t." 前缀）；未知列抛异常。

- void WhereEq(string c, string v)
  - column = 文本值。

- void WhereEq(string c, int v)
  - column = 整数值。

- void WhereEq(string c, long v)
  - column = 64 位整数值。

- void WhereIn(string c, List<int> vs)
  - column IN (逐个占位符)；空列表按恒假处理，不生成 `IN ()`。

- void WhereIn(string c, List<long> vs)
  - `WhereIn` 的 long 版本。

- void WhereNone()
  - 恒假条件：数据边界为空集时用它表达"一行都不许动"。

- void P(string v)
  - WHERE 参数：与 W 片段中占位符的出现顺序一致。

- void Pi(int v)
  - WHERE 整数参数。

- void Pl(long v)
  - WHERE 64 位整数参数。

- void Pd(double v)
  - WHERE 浮点参数。

- void InI(List<int> vs)
  - IN 列表参数（配合 W 片段里的 "col IN (?)"）。

- void InL(List<long> vs)
  - `InI` 的 long 版本。

- void InS(List<string> vs)
  - `InI` 的 string 版本。

- void InD(List<double> vs)
  - `InI` 的 double 版本。

- void ApplySource()
  - 实体整体更新：列在执行时展开，Only/Skip 的链顺序
    任意；定位列取实体主键（含复合主键），没有主键则无法定位、
    什么也不做。

- string BuildUpdate()
  - 生成 UPDATE 语句；无 SET 或无 WHERE 时返回 ""
    （绝无全表更新）。

- int ExecuteAffrows()
  - 执行并返回受影响行数。

- async int ExecuteAffrowsAsync()
  - 异步执行并返回受影响行数。


## QueryBuilder (class)

仿 FreeSQL 的流式 SQL 查询构建器。
支持通过方法链执行 SELECT、INSERT、UPDATE、DELETE。

用法（全部走 `?` 占位符 + DbParams——值永远不会进入 SQL 文本，
由 DbConnection 以驱动级参数绑定上线）：
QueryBuilder qb = QueryBuilder.From("users").WhereEq("age", 18);
DbResult r = db.Query(qb.BuildSelectParams("*"), qb.SelectParams());

string sql = QueryBuilder.InsertInto("users")
.Set("name", "Alice").BuildInsertParams();
db.Execute(sql, qb.InsertParams());

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

- static string RequireDdlType(string s)
  - s 是安全的 DDL 类型文本时原样返回；否则抛出异常。
    允许字母、数字、下划线、空格、括号和逗号——足以表达
    `INTEGER`、`VARCHAR(255)`、`NUMERIC(10,2)`、`DOUBLE PRECISION`
    这类类型，而引号、分号、注释符等注入通道一律拒绝。
    列类型文本会拼接进 CREATE TABLE，必须过这道检查。

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

- QueryBuilder WhereInInts(string column, List<int> values)
  - 添加 WHERE column IN (?, ?, ...)，使用整数参数；
    空列表按恒假处理（`IN (NULL)`），不生成 `IN ()`。

- QueryBuilder WhereInLongs(string column, List<long> values)
  - 添加 WHERE column IN (?, ?, ...)，使用 64 位整数参数；
    空列表按恒假处理（`IN (NULL)`），不生成 `IN ()`。

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

- static void CheckAllowed(string column, List<string> allowed)
  - 列名必须精确命中调用方提供的白名单；未命中抛异常。

- QueryBuilder OrderBySafe(string column, List<string> allowed)
  - 白名单版 ORDER BY：列名必须命中 allowed（精确匹配）
    且是安全标识符，否则抛异常。RequireIdent 挡住注入语法，但挡不住
    "合法但未授权"的列——排序既能被攻击者当布尔预言机（按保密列
    排序观察顺序差），也能放大误差泄密；ORDER BY 无法参数化，可能
    来自用户输入（?sort=）的排序列必须持单。

- QueryBuilder OrderBySafeDesc(string column, List<string> allowed)
  - 白名单版 ORDER BY DESC。

- QueryBuilder GroupBy(string column)
  - 设置 GROUP BY 子句（列名已做标识符校验）。

- QueryBuilder Having(string condition)
  - 设置 HAVING 子句。

- QueryBuilder Join(string table, string on)
  - 添加 JOIN 子句。表名经过标识符校验；
    `on` 是自由条件表达式，引用外部输入时须自行参数化。

- QueryBuilder LeftJoin(string table, string on)
  - 添加 LEFT JOIN 子句。表名经过标识符校验。

- QueryBuilder RightJoin(string table, string on)
  - 添加 RIGHT JOIN 子句。表名经过标识符校验。

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

- void AppendClauses(StringBuilder sb)
  - 把 JOIN/WHERE/GROUP BY/HAVING/ORDER BY/LIMIT/OFFSET
    子句追加到 sb（占位符原样保留，参数由调用方随 SQL 一起传）。

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

- static string EscapeDdlLiteral(string val)
  - DDL 字面量转义（单引号加倍）。只允许用于
    CREATE TABLE 的 DEFAULT 子句这类无法参数化的位置——
    所有 DML 一律走 `?` + DbParams 驱动级绑定，
    不要用本方法拼接查询值。


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


## IOrmRows (interface)

实体行的取值口：共享实现只按列元数据要值，不认识实体类型。

反射读写字段只在「静态类型就是实体」的地方成立（`object` 接收者拿不回
具体类型），所以按列元数据循环取值的那几行由编译期给每个实体发一份，
实现这个接口；代码量与字段数无关，加字段只多一行元数据。
参数绑定、SQL 构造、结果映射的规则仍只有共享实现这一份。

- int RowCount();
  - 待写入的行数。

- void BindCol(int row, OrmCol c, DbParams ps);
  - 把第 row 行的 c 列按声明类型追加为一个参数。
