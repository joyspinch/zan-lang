# System.Data.Postgres

> 源码: `stdlib/System/Data/Postgres/PgConnector.zan`, `stdlib/System/Data/Postgres/PgPool.zan`, `stdlib/System/Data/Postgres/PostgresConnection.zan`


## PgConnection (class)

通过 libpq 的原生 PostgreSQL 连接，同样适用于 PG wire 兼容的
engines: openGauss / GaussDB, KingbaseES (人大金仓), Vastbase and QuestDB.

libpq 提供按单元格的文本取值器（PQgetvalue -> char*），可干净地映射
到 Zan 字符串，无需手动做缓冲区编组。需要
在构建/运行时提供 libpq 共享库。

用法：
PostgresConnection db = PostgresConnection.Open(
"host=127.0.0.1 port=5432 dbname=app user=postgres password=secret");
db.Execute("CREATE TABLE users (id SERIAL PRIMARY KEY, name TEXT)");
DbResult r = db.Query("SELECT * FROM users");
db.Close();

参数化（推荐做法——值从不进入 SQL 文本；`?`
占位符会转换为 libpq 的 $1..$n 形式）：
db.Execute("INSERT INTO users (name) VALUES (?)", new DbParams().Add(name));
DbResult r = db.Query("SELECT * FROM users WHERE name = ?",
new DbParams().Add(name));

- nint handle;

- bool connected;

- string lastError;

- [DllImport("libpq")]static extern nint PQconnectdb(string conninfo);

- [DllImport("libpq")]static extern int PQstatus(nint conn);

- [DllImport("libpq")]static extern string PQerrorMessage(nint conn);

- [DllImport("libpq")]static extern void PQfinish(nint conn);

- [DllImport("libpq")]static extern nint PQexec(nint conn, string query);

- [DllImport("libpq")]static extern int PQresultStatus(nint res);

- [DllImport("libpq")]static extern int PQntuples(nint res);

- [DllImport("libpq")]static extern int PQnfields(nint res);

- [DllImport("libpq")]static extern string PQfname(nint res, int col);

- [DllImport("libpq")]static extern string PQgetvalue(nint res, int row, int col);

- [DllImport("libpq")]static extern int PQgetisnull(nint res, int row, int col);

- [DllImport("libpq")]static extern string PQcmdTuples(nint res);

- [DllImport("libpq")]static extern void PQclear(nint res);

- [DllImport("libpq")]static extern nint PQexecParams(nint conn, string command, int nParams, nint paramTypes, nint paramValues, nint paramLengths, nint paramFormats, int resultFormat);

- [DllImport("libpq")]static extern int PQsendQueryParams(nint conn, string command, int nParams, nint paramTypes, nint paramValues, nint paramLengths, nint paramFormats, int resultFormat);

- [DllImport("crt")]static extern nint memcpy(string dest, string src, long n);

- [DllImport("libpq")]static extern nint PQconnectStart(string conninfo);

- [DllImport("libpq")]static extern int PQconnectPoll(nint conn);

- [DllImport("libpq")]static extern int PQsocket(nint conn);

- [DllImport("libpq")]static extern int PQsetnonblocking(nint conn, int arg);

- [DllImport("libpq")]static extern int PQsendQuery(nint conn, string query);

- [DllImport("libpq")]static extern int PQflush(nint conn);

- [DllImport("libpq")]static extern int PQconsumeInput(nint conn);

- [DllImport("libpq")]static extern int PQisBusy(nint conn);

- [DllImport("libpq")]static extern nint PQgetResult(nint conn);

- static string CopyText(string s)
  - 将 libpq 返回的 C 字符串复制成由 ARC 管理的自有 Zan
    字符串。PQgetvalue/PQfname/PQerrorMessage 返回指向
    PGresult/PGconn 缓冲区的指针，PQclear()（或下一次 libpq 调用）会
    使其失效；直接保存会让结果集或
    lastError 指向已释放的内存。与 "" 拼接可在源指针仍有效时
    分配一份新的托管副本。

- static string ToDollarParams(string sql)
  - 将 `?` 占位符（单引号字面量之外）改写为
    libpq 的位置参数 $1..$n 形式，使所有驱动统一使用 `?` 语法。

- static byte[]BuildParamBlock(DbParams prms, List <byte[]> keepAlive)
  - 为 PQexecParams 构建 char** paramValues 块：一个
    指向各值 NUL 结尾副本的指针数组（SQL NULL 用 0）。
    值缓冲区会追加到 keepAlive 中，调用方须保证其在
    libpq 调用返回前一直存活。

- static DbResult ReadTuples(nint res)
  - 将 TUPLES_OK 的 PGresult 复制为 DbResult（NULL 单元格变为 ""）。

- PgConnection()

- static PgConnection Open(string conninfo)
  - 从 libpq conninfo 字符串打开连接。

- static PgConnection OpenParams(string host, int port, string database, string user, string password)
  - 从分散参数打开连接。

- int Execute(string sql)
  - 执行非查询语句；返回受影响行数或 -1。

- DbResult Query(string sql)
  - 执行查询并返回结果集。

- int Execute(string sql, DbParams prms)
  - 通过 PQexecParams 执行参数化非查询语句：
    值带外传输，绝不拼入 SQL 文本。

- DbResult Query(string sql, DbParams prms)
  - 通过 PQexecParams 执行参数化查询。

- string ExecuteScalar(string sql)
  - 执行标量查询（第一行第一列）。

- string ExecuteScalar(string sql, DbParams prms)
  - 执行参数化标量查询。

- static async PgConnection OpenAsync(string conninfo)
  - 打开连接而不阻塞线程：驱动 libpq 的
    非阻塞握手（PQconnectStart / PQconnectPoll），并在
    各轮询步骤之间于 IO reactor 上挂起等待套接字就绪。

- static async PgConnection OpenParamsAsync(string host, int port, string database, string user, string password)
  - 从分散参数异步打开连接。

- async int flushOutput()
  - 将缓冲输出刷新到服务器，在 libpq 仍有待发数据时
    挂起等待可写就绪。成功返回 0。

- async bool waitReady()
  - 等待 libpq 能无阻塞地产出下一条结果，
    在可读就绪时消费输入。出错返回 false。

- async DbResult QueryAsync(string sql)
  - 发送查询并异步收完所有结果，保留
    最后一个返回行的结果集。在 reactor 上挂起等待 IO 就绪，
    使工作线程可供其他协程使用。

- async DbResult QueryParamsAsync(string sql, DbParams prms)
  - 异步发送参数化查询（PQsendQueryParams）
    并在 IO reactor 上收完结果。语句失败时抛出
    `DbException`。

- async int ExecuteParamsAsync(string sql, DbParams prms)
  - 异步发送参数化非查询语句并返回
    受影响行数。语句失败时抛出 `DbException`。

- async int ExecuteAsync(string sql)
  - 异步发送非查询语句并返回
    受影响行数。语句失败时抛出 `DbException`。

- void fail()
  - 抛出记录的失败。libpq 的消息来自服务器本身，
    因此能原样到达调用方，而不会被
    空结果或 -1 吞掉。

- string GetError()
  - 获取最后一条错误消息。

- async DbResult QueryAsync(string sql, DbParams prms)
  - 开始一个事务。

- async int ExecuteAsync(string sql, DbParams prms)

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- async bool BeginTransactionAsync()

- async bool CommitAsync()

- async bool RollbackAsync()

- void BeginTransaction()

- void Commit()
  - 提交当前事务。

- void Rollback()
  - 回滚当前事务。

- void Close()
  - 关闭数据库连接。

- bool IsConnected()
  - 返回连接是否已打开。

- int GetProvider()
  - 返回提供程序 ID（DbProvider.PostgreSQL）。


## PgConnector (class)

`IDbConnector` 的原生 PostgreSQL 驱动实现。
使用非阻塞握手建立连接（<c>PQconnectStart</c> 由
IO reactor 驱动），因此扩容连接池从不阻塞工作线程。

用法：
DbPool pool = PgConnector.Pool("127.0.0.1", 5432, "app", "postgres", "secret", 8);
IDbConnection db = await pool.AcquireAsync();
DbResult r = db.Query("SELECT 1");
pool.Release(db);

`PgPool` 是对应的类型化连接池；当查询
本身需要以协程方式运行时使用（<c>QueryAsync</c>/<c>ExecuteAsync</c>）。

- string conninfo;

- PgConnector(string conninfo)

- static PgConnector CreateParams(string host, int port, string database, string user, string password)
  - 基于 host/port/database/user/password 的连接器。

- static DbPool Pool(string conninfo, int maxSize)
  - 基于 libpq 连接字符串的连接池。

- static DbPool Pool(string host, int port, string database, string user, string password, int maxSize)
  - 基于 host/port/database/user/password 的连接池。

- static string BuildConninfo(string host, int port, string database, string user, string password)

- async IDbConnection ConnectAsync()

- int GetProvider()


## PgPool (class)

面向 `PgConnection` 的协程化连接池。

契约与 `DbPool` 相同——惰性增长至 <c>maxSize</c>，
用事件驱动 `Gate` 等待而非轮询，驱逐失效
连接——但它是类型化的，协程查询 API（<c>QueryAsync</c>、
<c>ExecuteAsync</c>、<c>QueryParamsAsync</c>）仍然可用。连接
使用非阻塞 libpq 握手建立，因此无论扩容
连接池还是查询都不会阻塞工作线程。

用法：
PgPool pool = new PgPool("127.0.0.1", 5432, "app", "postgres", "secret", 8);
PgConnection db = await pool.AcquireAsync();
DbResult r = await db.QueryAsync("SELECT 1");
pool.Release(db);
pool.Close();

- string conninfo;

- PoolCore<PgConnection> core;

- PgPool(string conninfo, int maxSize)

- PgPool(string host, int port, string database, string user, string password, int maxSize):this("host="+host+" port="+Convert.ToString(port)+" dbname="+database+" user="+user+" password="+password, maxSize)

- async PgConnection OpenOne()

- async PgConnection AcquireAsync()
  - 借出连接：优先复用空闲连接，
    未达上限则新建，否则挂起协程直到有连接被归还。
    连接池关闭后返回 null。

- void Release(PgConnection c)
  - 将连接归还连接池。已损坏或 Close() 之后的
    连接会被关闭销毁而不入池。

- int IdleCount()
  - 空闲（已入池、立即可用）连接数。

- int LiveCount()
  - 存活连接总数（空闲 + 借出）。

- int WaitingCount()
  - 当前停泊等待连接的协程数。

- void Close()
  - 关闭所有空闲连接并标记连接池已关闭。仍
    被借出的连接在归还时关闭。
