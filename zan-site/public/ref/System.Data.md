# System.Data

> 源码: `stdlib/System/Data/DbConnection.zan`, `stdlib/System/Data/DbException.zan`, `stdlib/System/Data/DbParams.zan`, `stdlib/System/Data/DbPool.zan`, `stdlib/System/Data/DbResult.zan`, `stdlib/System/Data/DbTrace.zan`, `stdlib/System/Data/DbValues.zan`, `stdlib/System/Data/IDbConnection.zan`, `stdlib/System/Data/IDbConnector.zan`, `stdlib/System/Data/IDbExecutor.zan`, `stdlib/System/Data/OdbcConnector.zan`, `stdlib/System/Data/PoolCore.zan`, `stdlib/System/Data/TracedDbConnection.zan`


## DbConnection (class)

基于平台 ODBC 驱动管理器的通用数据库连接
（Windows 为 odbc32，Linux/macOS 为 unixODBC）。

任何提供 ODBC 驱动的引擎都可用：SQL Server、Oracle、
MySQL/MariaDB/TiDB/OceanBase、PostgreSQL、ClickHouse、QuestDB、Firebird 等。
DuckDB, TDengine, 达梦DM, SQLite, ... The convenience factories 下方 build
只需正确的连接字符串；OpenODBC 接受任意 DSN 或驱动字符串。

所有操作均支持协程，便于异步访问数据库。

用法：
DbConnection db = DbConnection.OpenSqlServer("127.0.0.1", 1433,
"app", "sa", "secret");
db.Execute("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(64))");
db.Execute("INSERT INTO users (id, name) VALUES (1, 'Alice')");
DbResult result = db.Query("SELECT * FROM users");
db.Close();

DbConnection any = DbConnection.OpenODBC("DSN=mydsn;UID=u;PWD=p;");

- nint handle;

- nint env;

- int provider;

- string connectionString;

- bool connected;

- string lastError;

- [DllImport("odbc32")]static extern int SQLAllocHandle(int handleType, nint inputHandle, string outputHandle);

- [DllImport("odbc32")]static extern int SQLSetEnvAttr(nint env, int attr, nint valuePtr, int strLen);

- [DllImport("odbc32")]static extern int SQLDriverConnect(nint dbc, nint hwnd, string inConn, int inLen, string outConn, int outBufLen, string outLenPtr, int completion);

- [DllImport("odbc32")]static extern int SQLExecDirect(nint stmt, string sql, int textLength);

- [DllImport("odbc32")]static extern int SQLNumResultCols(nint stmt, string colCountPtr);

- [DllImport("odbc32")]static extern int SQLDescribeCol(nint stmt, int colNum, string colName, int bufLen, string nameLenPtr, string typePtr, string sizePtr, string decPtr, string nullPtr);

- [DllImport("odbc32")]static extern int SQLFetch(nint stmt);

- [DllImport("odbc32")]static extern int SQLGetData(nint stmt, int colNum, int targetType, string buf, int bufLen, string strLenOrIndPtr);

- [DllImport("odbc32")]static extern int SQLRowCount(nint stmt, string rowCountPtr);

- [DllImport("odbc32")]static extern int SQLFreeHandle(int handleType, nint handle);

- [DllImport("odbc32")]static extern int SQLDisconnect(nint dbc);

- [DllImport("odbc32")]static extern int SQLFreeStmt(nint stmt, int option);

- [DllImport("odbc")]static extern int SQLAllocHandle(int handleType, nint inputHandle, string outputHandle);

- [DllImport("odbc")]static extern int SQLSetEnvAttr(nint env, int attr, nint valuePtr, int strLen);

- [DllImport("odbc")]static extern int SQLDriverConnect(nint dbc, nint hwnd, string inConn, int inLen, string outConn, int outBufLen, string outLenPtr, int completion);

- [DllImport("odbc")]static extern int SQLExecDirect(nint stmt, string sql, int textLength);

- [DllImport("odbc")]static extern int SQLNumResultCols(nint stmt, string colCountPtr);

- [DllImport("odbc")]static extern int SQLDescribeCol(nint stmt, int colNum, string colName, int bufLen, string nameLenPtr, string typePtr, string sizePtr, string decPtr, string nullPtr);

- [DllImport("odbc")]static extern int SQLFetch(nint stmt);

- [DllImport("odbc")]static extern int SQLGetData(nint stmt, int colNum, int targetType, string buf, int bufLen, string strLenOrIndPtr);

- [DllImport("odbc")]static extern int SQLRowCount(nint stmt, string rowCountPtr);

- [DllImport("odbc")]static extern int SQLFreeHandle(int handleType, nint handle);

- [DllImport("odbc")]static extern int SQLDisconnect(nint dbc);

- [DllImport("odbc")]static extern int SQLFreeStmt(nint stmt, int option);

- DbConnection()

- static nint HandleFromBuf(string p)
  - 重建写入 out 缓冲区中的 64 位句柄/指针。
    64 位平台上句柄为指针大小（8 字节），因此必须读回全部八个
    小端字节。

- static int Int16FromBuf(string p)
  - 从 out 缓冲区读取 16 位小端值。

- static int ScanNul(string buf, int max)
  - 驱动填充的文本缓冲区中第一个 NUL 字节的索引。

- static string BufToString(string buf, int max)
  - 将 NUL 结尾的 C 缓冲区复制为 Zan 字符串。

- static bool OdbcSuccess(int rc)

- static DbConnection OpenODBC(string connectionString)
  - 用完整的 ODBC 连接字符串 / DSN 打开任意数据源。

- static DbConnection OpenSqlServer(string host, int port, string database, string user, string password)
  - 打开 Microsoft SQL Server。

- static IDbConnection OpenMySQL(string host, int port, string database, string user, string password)
  - 打开 MySQL / MariaDB / TiDB / OceanBase。优先使用原生
    线协议客户端（System.Data.MySql）——无需安装 ODBC 驱动，
    不可用时回退到 ODBC 驱动。

- static IDbConnection OpenMariaDB(string host, int port, string database, string user, string password)
  - 打开 MariaDB（默认使用原生 MySQL 协议）。

- static DbConnection OpenPostgreSQL(string host, int port, string database, string user, string password)
  - 通过 psqlODBC 驱动打开 PostgreSQL。

- static DbConnection OpenOpenGauss(string host, int port, string database, string user, string password)
  - 通过 PostgreSQL ODBC 驱动打开 openGauss / GaussDB。

- static DbConnection OpenKingbase(string host, int port, string database, string user, string password)
  - Opens KingbaseES / 人大金仓 via the PostgreSQL ODBC driver.

- static DbConnection OpenQuestDB(string host, int port, string user, string password)
  - 通过 PostgreSQL ODBC 驱动打开 QuestDB。

- static DbConnection OpenOracle(string host, int port, string service, string user, string password)
  - 打开 Oracle（dbq = host:port/service）。

- static DbConnection OpenFirebird(string host, int port, string dbPath, string user, string password)
  - 打开 Firebird（嵌入式或服务器模式）。

- static DbConnection OpenClickHouse(string host, int port, string database, string user, string password)
  - 打开 ClickHouse。

- static DbConnection OpenTDengine(string host, int port, string database, string user, string password)
  - 打开 TDengine。

- static DbConnection OpenDuckDB(string path)
  - 打开 DuckDB 数据库（嵌入式 OLAP）。

- static DbConnection OpenDM(string host, int port, string user, string password)
  - Opens 达梦 DM.

- static DbConnection OpenSQLite(string path)
  - 通过 SQLite ODBC 驱动打开 SQLite 文件。
    如需免依赖的嵌入式访问，请使用 System.Data.Sqlite.SqliteConnection。

- static DbConnection OpenWith(string connectionString, int provider)

- nint AllocStmt()
  - 分配语句句柄（SQL_HANDLE_STMT = 3）；失败时返回 0。

- int Execute(string sql)
  - 执行非查询语句（INSERT/UPDATE/DELETE/DDL）。

- async int ExecuteAsync(string sql)
  - 执行非查询语句（包装方法：ODBC 为同步操作）。

- async int ExecuteAsync(string sql, DbParams prms)

- DbResult Query(string sql)
  - 执行查询并返回结果集。

- async DbResult QueryAsync(string sql)
  - 执行查询（包装方法：ODBC 为同步操作）。

- async DbResult QueryAsync(string sql, DbParams prms)

- string ExecuteScalar(string sql)
  - 执行标量查询（以字符串形式返回第一行第一列）。

- string ExecuteScalar(string sql, DbParams prms)

- async string ExecuteScalarAsync(string sql)
  - 执行标量查询（包装方法：ODBC 为同步操作）。

- async string ExecuteScalarAsync(string sql, DbParams prms)

- async bool BeginTransactionAsync()

- async bool CommitAsync()

- async bool RollbackAsync()

- string GetError()
  - 获取此连接记录的最后一条错误信息。

- void BeginTransaction()
  - 开始事务。

- void Commit()
  - 提交当前事务。

- void Rollback()
  - 回滚当前事务。

- void Close()
  - 关闭数据库连接。

- bool IsConnected()
  - 返回连接是否打开。

- int GetProvider()
  - 返回 provider 类型。

- int Execute(string sql, DbParams prms)
  - 执行带参数的非查询语句。ODBC 路径没有
    统一的参数绑定接口，因此 `?` 占位符在执行前经加引号和转义
    后内联进 SQL。

- DbResult Query(string sql, DbParams prms)
  - 执行带参数的查询（ODBC 路径上参数的绑定方式
    参见 Execute 重载）。

- static string InlineParams(string sql, DbParams prms)
  - 将 `?` 占位符（引号字面量之外的）替换为
    加引号并转义后的参数值。

- static string EscapeSqlString(string val)
  - 转义字符串中的单引号以保证 SQL 安全。


## DbException (class)

语句执行失败时抛出：服务端拒绝执行、会话未
连接，或驱动无法完成请求。过去驱动通常以空结果
回应此类失败，并把原因留在
<c>GetError()</c> 中，使得忘记检查的调用方继续
处理根本不存在的行。

`Code` 是驱动自身的错误号（Firebird SQL 代码、
MySQL 错误号、SQL Server 错误号、TDengine 代码）；当失败
未到达服务器时为 0。`Provider` 是
`DbProvider` 的 id，处理方无需解析消息即可知道
是哪个驱动抛出的异常。

- public int Code;

- public int Provider;

- public DbException(string message, int code, int provider)

- static DbException Of(string message)
  - 从未到达服务器的失败，因此不带错误号
    也没有 provider。


## DbParam (class)

一个带类型的 SQL 参数：类型加上 text/int/double 载荷
（仅与类型匹配的字段有意义）。用一个实体代替四个
索引对齐的平行列表。整数载荷是 64 位的：KindInt 在各驱动上
都按 64 位整数绑定（sqlite3_bind_int64、MYSQL_TYPE_LONGLONG…），
窄一点的载荷会把毫秒/微秒累计这类值截断。

- int kind;

- string text;

- long ival;

- double dval;

- DbParam(int kind, string text, long ival, double dval)


## DbParams (class)

有序、带类型的 SQL 参数列表，所有数据库驱动共用。

值携带自身类型（int / double / text / null），驱动可
原生绑定它们（sqlite3_bind_*、COM_STMT_EXECUTE、PQexecParams、
SQLBindParameter），而不是把转义字符串内联进 SQL。

用法：
DbParams p = new DbParams().Add("Alice").AddInt(30);
DbResult r = db.Query("SELECT * FROM users WHERE name = ? AND age > ?", p);

- static int KindNull=0;

- static int KindInt=1;

- static int KindDouble=2;

- static int KindText=3;

- List<DbParam> items;

- DbParams()

- void push(int kind, string t, long i, double d)

- DbParams Add(string val)
  - 追加一个文本参数。null 字符串绑定为 SQL NULL——
    未设置的实体字段必须以 NULL 写入数据库，绝不能是
    解引用后的空字符串。

- DbParams AddInt(int val)
  - 追加一个整数参数。

- DbParams AddLong(long val)
  - 追加一个 64 位整数参数。

- DbParams AddDouble(double val)
  - 追加一个浮点参数。

- DbParams AddNull()
  - 追加一个 NULL 参数。

- int Count()
  - 参数数量。

- int KindAt(int i)
  - 索引 i 处参数的类型（参见 Kind* 常量）。

- string TextAt(int i)
  - 索引 i 处的文本值（KindAt(i) == KindText 时有效）。

- long IntAt(int i)
  - 索引 i 处的整数值（KindAt(i) == KindInt 时有效）。

- double DoubleAt(int i)
  - 索引 i 处的双精度值（KindAt(i) == KindDouble 时有效）。

- string AsText(int i)
  - 将索引 i 处的参数转为文本（供把所有值都绑定为文本的驱动使用，
    如 PQexecParams）。

- bool IsNull(int i)
  - 索引 i 处的参数是否为 NULL。


## DbPool (class)

与驱动无关、支持协程的连接池。

连接通过 `IDbConnector` 惰性打开，最多
<c>maxSize</c> 个，并在协程间复用。当所有连接
都被借出且达到上限时，`AcquireAsync` 会挂起
调用协程，等待基于事件的 `Gate`（无轮询、无
定时器），而不是阻塞工作线程；一旦有连接
通过 `Release` 归还便立即恢复。已失效的连接
会被逐出而非回收，把名额让给等待者。

连接池依赖每个调度器单线程的协作模型：
空闲列表只在 await 之间访问，因此无需加锁。

用法：
DbPool pool = SqliteConnector.Pool("app.sqlite3", 8);
IDbConnection db = await pool.AcquireAsync();
DbResult r = db.Query("SELECT * FROM users");
pool.Release(db);
pool.Close();

查询本身即为协程的驱动（MySQL、PostgreSQL）还提供
类型化连接池——<c>MySqlPool</c>、<c>PgPool</c>——返回
具体连接类型，使 <c>QueryAsync</c> 仍可调用。

- IDbConnector connector;

- PoolCore<IDbConnection> core;

- DbPool(IDbConnector connector, int maxSize)

- async IDbConnection OpenOne()

- async IDbConnection AcquireAsync()
  - 借出一个连接：复用空闲连接，未达上限时新建，
    否则挂起协程直到有连接被释放。
    连接池关闭后返回 null。

- void Release(IDbConnection c)
  - 将连接归还连接池。已损坏或 Close() 之后
    的连接会被关闭丢弃，不再入池。

- IDbConnection TryAcquire()
  - 借出已空闲的连接而不挂起；没有空闲连接时
    返回 null。适用于同步调用点（非协程的钩子或
    解析器），且驱动 IO 本身是同步的；它从不新建连接，因为
    打开连接是协程操作，所以应在启动时用
    `WarmupAsync` 预热连接池，并把 null 视为
    "容量已耗尽"。归还方式与借出的连接相同。

- async int WarmupAsync(int count)
  - 最多打开 <paramref name="count"/> 个连接（上限为
    maxSize）并停放，使首批请求——以及每次
    `TryAcquire` 的调用方——都能拿到活跃连接。返回
    当前空闲的数量；数据库不可达时返回 0。

- int IdleCount()
  - 空闲（已入池、可即用）连接数。

- int LiveCount()
  - 活跃连接总数（空闲 + 已借出）。

- int WaitingCount()
  - 当前挂起等待连接的协程数。

- int MaxSize()
  - 并发活跃连接的最大数量。

- bool IsClosed()
  - `Close` 调用过后为 true。

- int GetProvider()
  - 此连接池服务的 `DbProvider` id。

- void Close()
  - 关闭所有空闲连接并将连接池标记为已关闭。仍
    被借出的连接会在归还时关闭。


## DbProvider (class)

FreeSQL 风格 ORM 支持的数据库 provider 类型。

默认的 `DbConnection` 即可连接其中大多数引擎
通过 its ODBC driver (the "对应的驱动"), so a single, dependency-light
其连接类型服务于整个 ORM。另有原生、更高性能的驱动
可供选择：
- System.Data.MySql：原生 MySQL/MariaDB 线协议（无需 ODBC；
OpenMySQL 自动选用它，因为无需
外部驱动）。
- System.Data.Sqlite：通过 sqlite3 C 库使用嵌入式 SQLite。
- System.Data.Postgres：PostgreSQL 及兼容 PG 线协议的引擎（openGauss、
KingbaseES/人大金仓, Vastbase, GaussDB, QuestDB) via libpq.
- System.Data.TDengine：通过 taosAdapter REST API 访问 TDengine（无需任何客户端
库）。

- static int Unknown=0-1;

- static int ODBC=0;

- static int SQLite=1;

- static int MySQL=2;

- static int PostgreSQL=3;

- static int SqlServer=4;

- static int Oracle=5;

- static int ClickHouse=7;

- static int QuestDB=8;

- static int Firebird=9;

- static int DuckDB=10;

- static int TDengine=11;

- static int MariaDB=12;

- static int Kingbase=13;

- static int OpenGauss=14;

- static int DM=15;


## DbResult (class)

表示一次数据库查询的结果集。
以字符串值列表存储各行，并附带列名元数据。

- List<DbRow> rows;

- List<string> columnNames;

- int columnCount;

- DbResult()

- static DbResult Empty()
  - 创建空结果（无行）。

- void SetColumnCount(int count)
  - 设置列数。

- void AddColumnName(string name)
  - 添加一个列名。

- void AddRow(List<string> row)
  - 添加一行值（不含 SQL NULL）。

- void AddRowNulls(List<string> row, List<bool> nullFlags)
  - 添加一行，并附逐列的 SQL NULL 标记，调用方
    可通过 IsNull() 区分 NULL 与空字符串。

- bool IsNull(int row, int col)
  - 单元格为 SQL NULL 时为 true（空字符串则相反，
    GetString 对空字符串同样返回 ""）。

- bool IsNullByName(int row, string columnName)
  - 按列名检查是否为 NULL。

- int RowCount()
  - 返回行数。

- int ColumnCount()
  - 返回列数。

- string GetString(int row, int col)
  - 获取指定行列索引处的字符串值。

- int GetInt(int row, int col)
  - 获取指定行列索引处的整数值。

- long GetLong(int row, int col)
  - 获取指定行列索引处的 64 位整数值。

- double GetDouble(int row, int col)
  - 获取指定行列索引处的双精度值。

- bool GetBool(int row, int col)
  - 获取指定行列索引处的布尔值。

- string GetByName(int row, string columnName)
  - 按行索引和列名取值。

- int GetColumnIndex(string name)
  - 按列名获取列索引。

- string GetColumnName(int index)
  - 获取指定索引处的列名。

- List<string> GetColumnNames()
  - 返回所有列名。

- bool HasRows()
  - 检查结果集是否包含行。

- List<string> GetRow(int index)
  - 将整行作为字符串列表返回。

- List<string> First()
  - 获取第一行（便捷方法）。

- void Print()
  - 将结果集打印到控制台（用于调试）。


## DbRow (class)

一行结果：各单元格的字符串值及逐格的 SQL NULL 标记。
用单一实体承载，避免值与 NULL 标记脱节。

- List<string> values;

- List<bool> nulls;

- DbRow(List<string> values, List<bool> nulls)


## DbTrace (class)

Where timed statements are reported, set once at startup by the application
that wants them. Nothing here knows what a metric or a log file is: the sink
is a delegate, so the data layer stays free of the monitoring it feeds.

Without a sink the whole thing costs one null check and no clock read --
timing is not something a library should charge for when nobody is reading.


DbTrace.Sink(Program.SqlTrace);                 // once, at startup
IDbConnection db = TracedDbConnection.Wrap(await pool.AcquireAsync());

- static SqlTraceSink sink=null;

- static void Sink(SqlTraceSink fn)
  - Sends every timed statement to `fn`; null stops tracing.
    The sink runs on the coroutine that ran the statement, right after it
    finished, so it must not itself go to the database that is being traced
    -- that is a statement reporting a statement, without end.

- static bool Enabled()
  - Whether anything is listening. Call sites use it to skip the
    wrapper and the clock entirely.

- static long NowUs()
  - Monotonic microseconds: the reading a duration is measured from.
    Monotonic, because a statement timed across an NTP correction would
    otherwise be reported as hours long (or negative).

- static void Report(string sql, long startUs)
  - Reports the statement that started at `startUs` (a
    `NowUs` reading) as having just finished.


## DbValue (class)

一个动态列值：列名、类型（使用
`DbParams` 的 Kind* 代码）及匹配该类型的载荷。

- string name;

- int kind;

- string text;

- long ival;

- double dval;

- DbValue(string name, int kind, string text, long ival, double dval)


## DbValues (class)

用于仅存在于运行期数据的有序、带类型列集合——如
提交的表单、解析出的 JSON 主体、电子表格行——使动态写入
无需手写 SQL：

DbValues form = new DbValues()
.Set(BlogCols.title, "hello")
.Set(BlogCols.clicks, 5);

db.Insert<Blog>().AppendDict(form).ExecuteAffrows();
db.Update<Blog>().SetDict(form).Where(a => a.id == id).ExecuteAffrows();

生成的代码会校验键是否属于实体的列，且
值始终作为参数绑定，绝不内联进 SQL 文本。
ORM 生成的 `<Entity>Cols` 类为列名提供
自动补全，代替裸字符串字面量。

- List<DbValue> items;

- DbValues()

- DbValues Set(string name, string val)
  - 设置文本列（覆盖之前的值）。

- DbValues Set(string name, int val)
  - 设置整数列。

- DbValues Set(string name, long val)
  - 设置 64 位整数列。

- DbValues Set(string name, double val)
  - 设置浮点列。

- DbValues Set(string name, bool val)
  - 设置布尔列（以 0/1 存储）。

- DbValues SetNull(string name)
  - 将列设置为 SQL NULL。

- DbValues push(string name, int kind, string t, long i, double d)

- int Count()
  - 现有列数。

- string NameAt(int i)
  - 索引 i 处的列名。

- int KindAt(int i)
  - 索引 i 处值的类型（参见 DbParams.Kind*）。

- int IndexOf(string name)
  - 列的索引，不存在时为 -1。

- bool Has(string name)
  - 列是否存在。

- DbValues Remove(string name)
  - 移除某列，使写入时不再处理它。

- void BindTo(DbParams ps, int i)
  - 按原样把索引 i 处的值追加到参数列表。

- void BindAs(DbParams ps, int i, int want)
  - 把索引 i 处的值转换为列声明的
    类型（DbParams.Kind* 代码），使所有字段以文本形式携带的表单
    其 INTEGER 列仍能按数字绑定。无法转换的值
    会抛出指明列名的 Exception，而不是
    到达数据库时以类型错误的形式暴露。

- long parseInt(DbValue v)

- double parseDouble(DbValue v)

- void reject(DbValue v, string want)


## OdbcConnector (class)

基于通用 ODBC `DbConnection` 实现的 `IDbConnector`，
因此凡是通过 DSN/驱动可访问的引擎都能获得与原生驱动相同的
协程感知连接池。

ODBC 调用是同步的，所以连接池只提供生命周期管理——
连接复用与并发连接上限，等待者以协程挂起
而非阻塞线程——并非非阻塞 IO。

用法：
DbPool pool = OdbcConnector.Pool("DSN=mydb;UID=u;PWD=p", DbProvider.ODBC, 8);
IDbConnection db = await pool.AcquireAsync();
DbResult r = db.Query("SELECT 1");
pool.Release(db);

- string connectionString;

- int provider;

- OdbcConnector(string connectionString, int provider)

- static DbPool Pool(string connectionString, int provider, int maxSize)
  - 根据连接字符串构建的 ODBC 连接池。

- async IDbConnection ConnectAsync()

- int GetProvider()


## PoolCore (class)

所有驱动连接池共享的簿记数据：空闲列表、
存活/等待计数器、唤醒闸门和关闭标志。驱动连接池
只保留端点参数和自身的建连方式
（通常为协程），其余都委托到这里。

核心本身从不直接接触连接——既不做活性探测，也
不关闭任何连接——因此适用于任意连接类型，包括
接口类型 `DbPool`。驱动连接池负责判断
从空闲列表取出的连接是否仍可用，并报告结论
——通过 `Pool` 或 `Drop`。

簿记状态被多个 worker 线程共享：协程在 per-worker
就绪队列上运行（<c>--async-workers</c> / <c>ZAN_CO_WORKERS</c>），
同一个池会被不同线程上的协程同时获取和归还。因此每一处
状态访问都在 <c>lock (this)</c> 里完成——没有它，`TakeIdle`
的"先查 Count 再取下标"就会在两个线程间被撕开
（实测崩溃：list index out of bounds）。监视器是可重入的，
所以内部辅助方法照常调用。

临界区里绝不 await：所有方法都是同步的，锁只覆盖几条
列表/计数操作，因此持锁期间协程不会被挂起，也不会
把锁带过调度点。停泊逻辑留在驱动连接池中——<c>EnterWait</c>、
<c>await core.Waiter().Wait()</c>、<c>LeaveWait</c>——因此挂起的协程
正是调用方所等待的那个协程，等待的 await 也发生在锁之外。

- int maxSize;

- List<T> idle;

- List<T> borrowed;

- int liveCount;

- int waiting;

- Gate gate;

- bool closed;

- bool openFailed;

- int skipped;

- int retryEvery;

- PoolCore(int maxSize)
  - 创建核心，存活连接数上限为 <paramref name="maxSize"/>
    （至少一个）。

- T TakeIdle()
  - 从空闲列表取出最近归还的连接，
    列表为空时返回 null。调用方检查其是否仍可用，
    对停泊期间已失效的连接调用 `Drop`
    （服务器重启、空闲超时）。

- void Pool(T c)
  - 将可用连接放入空闲列表并唤醒一个等待者。
    仅接受当前处于借出状态的连接，重复归还会被忽略。

- void Drop(T c)
  - 登记一条永久离开连接池的连接（关闭、
    损坏或被驱逐）：释放其槽位，等待者可新建
    连接顶替它。
    
    只有确实从借出列表里摘下来时才归还槽位：否则重复
    归还（同一条连接被 Release/Drop 两次）会把 liveCount
    减到实际连接数以下，池子就会开出超过 maxSize 的连接。

- void Borrow(T c)
  - 记录新打开的连接已借出。在 `TryReserve`
    预留槽位并成功打开后调用。

- bool TryReserve()
  - 为即将打开的连接预留槽位。预先
    预留可防止并发获取者同时判定
    有空位而超出上限。

- void UndoReserve()
  - 归还因打开失败而空出的槽位，并让等待者重试。

- bool RemoveBorrowed(T c)

- bool OpenBlocked()
  - 建连失败后，本次获取是否应跳过建连尝试。
    
    后端不可达时，池里永远没有空闲连接，于是每个获取者都会
    重新尝试建连；建连失败本身是有代价的（一次失败的
    sqlite3_open 约 125us，网络驱动还要等一个 RTT 或超时），
    代价会摊到每个请求上。失败后只放行每 <c>retryEvery</c>
    次获取中的一次去真正重试，其余立即失败，让后端宕机变成
    快速失败而不是吞吐塌方；重试一旦成功便恢复常态。
    不依赖时钟，因此各平台行为一致。

- void NoteOpenFailed()
  - 登记一次失败的建连，开启（或延续）退避。

- void NoteOpenOk()
  - 登记一次成功的建连，结束退避。

- bool OpenFailing()
  - 最近一次建连是否失败（即连接池是否处于退避中）。

- Gate Waiter()
  - 连接池饱和时停泊获取者的闸门。其计数
    语义加上调用方的复查循环，可保证不丢失唤醒。

- void EnterWait()
  - 在协程 await 闸门之前，将其登记为停泊状态，
    这样释放者就知道必须发信号唤醒。

- void LeaveWait()
  - 注销从闸门唤醒的协程。

- int IdleCount()
  - 空闲（已入池、立即可用）连接数。

- int LiveCount()
  - 存活连接总数（空闲 + 借出）。

- int WaitingCount()
  - 当前停泊等待连接的协程数。

- int MaxSize()
  - 最大并发存活连接数。

- bool IsClosed()
  - 连接池关闭后为 true。

- void MarkClosed()
  - 标记连接池已关闭：获取者返回 null，归还的
    连接将被销毁而不入池。

- void WakeAll()
  - 唤醒所有停泊的等待者；每个都会重新检查已关闭的池并
    返回 null。


## TracedDbConnection (class)

A connection that times what runs through it and reports it to
`DbTrace`, forwarding everything else to the connection it wraps.

Why a wrapper and not the drivers: a statement is executed in six different
implementations (SQLite, MySQL sync/async, PostgreSQL, ODBC, TDengine), each
with the six executor entry points. Timing them one by one would put the same
four lines in three dozen places and would still miss the next driver. The
wrapper is the interface, so a driver added tomorrow is traced without being
touched -- and a connection the application uses for its own bookkeeping (a
metrics database writing the trace down) simply is not wrapped, which is what
keeps a trace from tracing itself.

The pool never sees this object: whoever wraps a lease unwraps it before
giving it back (see `Unwrap`).

- IDbConnection inner;

- TracedDbConnection(IDbConnection inner)

- IDbConnection Inner()
  - The connection underneath.

- static IDbConnection Wrap(IDbConnection db)
  - `db` timed, or `db` itself when nothing is listening: an
    untraced server must not pay for a wrapper on every lease.

- static IDbConnection Unwrap(IDbConnection db)
  - The connection a pool handed out, however many times it was
    wrapped -- what has to go back to the pool is the pooled object, not a
    wrapper the pool has never seen.

- DbResult Query(string sql)

- DbResult Query(string sql, DbParams prms)

- int Execute(string sql)

- int Execute(string sql, DbParams prms)

- string ExecuteScalar(string sql)

- string ExecuteScalar(string sql, DbParams prms)

- async DbResult QueryAsync(string sql)

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql)

- async int ExecuteAsync(string sql, DbParams prms)

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- int GetProvider()

- bool IsConnected()

- void Close()

- void BeginTransaction()

- void Commit()

- void Rollback()

- async bool BeginTransactionAsync()

- async bool CommitAsync()

- async bool RollbackAsync()


## void (delegate)

One finished statement: the SQL as it was submitted and how long it
took in MICROSECONDS -- the same shape `ServerMetrics.RecordQuery`
takes, so a server can hand this straight to its metrics.

`delegate void SqlTraceSink(string sql, long us);`


## IDbConnection (interface)

可入池的数据库连接：一个 `IDbExecutor`，同时还能
报告自身是否仍可用、可被关闭。这正是
`DbPool` 所需的全部能力，因此一个连接池实现即可服务所有
同步执行 SQL 的驱动（ODBC、SQLite、PostgreSQL、TDengine 等），
而调用方仍持有可用的连接类型 —— <c>Query</c>/<c>Execute</c>
可直接通过接口调用，无需向下转型。

- bool IsConnected();
  - 连接关闭或损坏后为 false；此类连接会被
    剔除而非归还空闲列表。

- void Close();
  - 释放底层句柄/套接字。

- void BeginTransaction();
  - 在 THIS 连接上开启事务。事务属于
    会话的属性，参与事务的每条语句都必须在
    同一连接上执行 —— 这正是它们放在这里而非
    具体驱动里的原因：持有接口的代码（池化连接、DAO）
    否则无法开启事务。
    
    不支持事务的引擎（如 TDengine）按文档实现为
    空操作；不要在需要原子性的代码中使用它们。

- void Commit();
  - 提交事务的写入。

- void Rollback();
  - 回滚事务的写入。

- async bool BeginTransactionAsync();
  - 上述三个事务调用的协程版本。IO 为异步的驱动
    无法同步执行 BEGIN/COMMIT，因此必须在所有驱动上
    工作的代码（请求作用域的工作单元、DAO）
    应使用这些方法。

- async bool CommitAsync();

- async bool RollbackAsync();


## IDbConnector (interface)

为 `DbPool` 打开新连接。每个驱动自带一个
持有自身端点/凭据的 connector，这是连接池中
唯一与提供方相关的部分；连接池本身与驱动无关。

<c>ConnectAsync</c> 是协程：有真实网络 IO 的驱动（MySQL、
PostgreSQL、TDengine、SQL Server 等）必须在 reactor 上连接，
这样扩容中的连接池就不会阻塞工作线程。嵌入式引擎（SQLite、
DuckDB）则同步打开、直接返回 —— 连接池仍为它们提供
协程感知的容量控制。

- async IDbConnection ConnectAsync();
  - 打开一条新连接；若尝试失败，返回的连接其
    <c>IsConnected()</c> 为 false。

- int GetProvider();
  - 所产生连接的 `DbProvider` id。


## IDbExecutor (interface)

数据库连接共用的最小执行契约：同一组四个操作提供同步
与异步两种版本。任何实现该接口的
连接都能驱动 FreeSQL 风格的 ORM（System.Data.Orm.Model /
QueryBuilder），使 ORM 在 ODBC DbConnection 与
原生 SQLite / PostgreSQL / MySQL 驱动上表现一致。

之所以有同步/异步两套，是因为驱动类型不同而非 API 不同：
嵌入式引擎（SQLite）从本地文件返回结果，无需等待；
而网络引擎必须等待服务器 —— 若在
阻塞套接字上等待，会停掉整个 worker 的事件循环，将数据库
并发限制在 worker 数量上。IO 已协程化的驱动
（MySqlConnection）真正实现 *Async 方法，并让
同步版本直接报错；而同步驱动的 *Async 方法只是
简单转发。调用方（包括生成的 ORM
终端）可无条件使用异步链，并在驱动支持处获得
非阻塞 IO。

- DbResult Query(string sql, DbParams prms);

- int Execute(string sql, DbParams prms);

- DbResult Query(string sql);

- int Execute(string sql);

- string ExecuteScalar(string sql);

- string ExecuteScalar(string sql, DbParams prms);

- int GetProvider();

- async DbResult QueryAsync(string sql, DbParams prms);
  - `Query` 的协程版本：挂起而非
    阻塞 worker（针对带异步 IO 的驱动）。

- async int ExecuteAsync(string sql, DbParams prms);

- async DbResult QueryAsync(string sql);

- async int ExecuteAsync(string sql);

- async string ExecuteScalarAsync(string sql);

- async string ExecuteScalarAsync(string sql, DbParams prms);
