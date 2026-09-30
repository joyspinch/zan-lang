# System.Data.Sqlite

> 源码: `packages/Zan.Data/src/System/Data/Sqlite/SqliteConnection.zan`, `packages/Zan.Data/src/System/Data/Sqlite/SqliteConnector.zan`, `packages/Zan.Data/src/System/Data/Sqlite/SqlitePool.zan`


## SqliteConnection (class)

原生、无依赖的嵌入式 SQLite 连接（sqlite3 C 库）。

在需要零外部配置且只需 SQLite 时，优先选择它而非 ODBC 路径。
构建/运行时需要 sqlite3 共享库。

用法：
SqliteConnection db = SqliteConnection.Open("app.sqlite3");
db.Execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");
DbResult r = db.Query("SELECT * FROM users");
db.Close();

语句失败（语法错、表/列不存在、约束冲突、库被锁超时……）时
抛出 `DbException`，携带驱动返回码与
sqlite3_errmsg 文本；因此空结果集总意味着「无行」。
失败不会中断连接，后续语句照常执行。

- nint handle;

- bool connected;

- Dict <string, nint> stmtCache;

- Dict <string, bool> stmtBusy;

- [DllImport("sqlite3")]static extern int sqlite3_open(string filename, string ppDb);

- [DllImport("sqlite3")]static extern int sqlite3_close(nint db);

- [DllImport("sqlite3")]static extern int sqlite3_exec(nint db, string sql, nint callback, nint arg, string errmsg);

- [DllImport("sqlite3")]static extern int sqlite3_prepare_v2(nint db, string sql, int nByte, string ppStmt, string pzTail);

- [DllImport("sqlite3")]static extern int sqlite3_step(nint stmt);

- [DllImport("sqlite3")]static extern int sqlite3_reset(nint stmt);

- [DllImport("sqlite3")]static extern int sqlite3_clear_bindings(nint stmt);

- [DllImport("sqlite3")]static extern int sqlite3_finalize(nint stmt);

- [DllImport("sqlite3")]static extern int sqlite3_column_count(nint stmt);

- [DllImport("sqlite3")]static extern string sqlite3_column_text(nint stmt, int col);

- [DllImport("sqlite3")]static extern string sqlite3_column_name(nint stmt, int col);

- [DllImport("sqlite3")]static extern int sqlite3_column_type(nint stmt, int col);

- [DllImport("sqlite3")]static extern string sqlite3_errmsg(nint db);

- [DllImport("sqlite3")]static extern int sqlite3_bind_int64(nint stmt, int index, long val);

- [DllImport("sqlite3")]static extern int sqlite3_bind_double(nint stmt, int index, double val);

- [DllImport("sqlite3")]static extern int sqlite3_bind_text(nint stmt, int index, string val, int nBytes, nint destructor);

- [DllImport("sqlite3")]static extern int sqlite3_bind_null(nint stmt, int index);

- [DllImport("sqlite3")]static extern int sqlite3_changes(nint db);

- [DllImport("sqlite3")]static extern long sqlite3_last_insert_rowid(nint db);

- [DllImport("sqlite3")]static extern int sqlite3_busy_timeout(nint db, int ms);

- [DllImport("sqlite3")]static extern void sqlite3_free(nint ptr);

- static int busyTimeoutMs=5000;
  - 一个连接遇到别人持锁时愿意等多久（毫秒，<=0 关闭等待）。
    SQLite 一个库同一时刻只容一个写者，默认行为是立刻返回 SQLITE_BUSY，
    于是并发写在应用层表现为随机失败而不是排队。等待是连接属性、只能在
    打开时设置，而池里的连接是惰性打开的、调用方拿不到那一刻，所以由
    Open 统一设置。

- SqliteConnection()
  - 私有构造；统一经 `Open` 创建。

- static string CopyText(string s)
  - 将 sqlite3 库返回的 C 字符串复制为受 ARC 管理、
    自有的 Zan 字符串。sqlite3_column_text/_name 返回的指针指向
    libsqlite3 自己的缓冲区，会被下一次 step()/
    finalize() 失效；直接保存会使结果集指向
    已释放的内存（垃圾行）。与 "" 拼接会分配一份新的
    托管副本，而此时源指针仍有效。

- static nint HandleFromBuf(string p)
  - 从 8 字节缓冲区按小端重组出指针值（sqlite3_open/prepare_v2
    的句柄出参经此取回）。

- static SqliteConnection Open(string filename)
  - 打开（或创建）SQLite 数据库文件。
    
    失败时 sqlite3_open 依然会回填一个句柄（用于取错误信息），
    该句柄必须由调用方 close，否则每次失败的打开都会
    泄漏一整个 sqlite3 连接对象（约 1.5KB）。数据库不可达时
    连接池每个请求都会尝试打开一次，泄漏便随请求线性增长。

- static int BindAll(nint stmt, DbParams prms)
  - 将 <paramref name="prms"/> 中的每个参数按其原生类型绑定到
    预编译语句，序号从 1 开始。SQLITE_TRANSIENT (-1) 让
    sqlite3 立即复制文本，这样 Zan 字符串可随时被回收。
    返回 0，或首个绑定失败的驱动返回码。

- void Fail(int rc)
  - 抛出记录的失败：文本来自 sqlite3_errmsg（服务器
    自身的说明），错误号是驱动返回码（如 SQLITE_CONSTRAINT）。
    空结果从此总意味着「无行」，而不是掩盖了一次失败。

- int Execute(string sql)
  - 执行非查询语句（INSERT/UPDATE/DELETE/DDL）。
    失败时抛出 `DbException`。

- int Execute(string sql, DbParams prms)
  - 执行带参数的非查询语句。值按原生方式绑定
    （绝不内联进 SQL 文本），因此无需转义，
    也无法通过值进行注入。占位符为 `?`。
    失败时抛出 `DbException`。

- DbResult Query(string sql)
  - 执行查询并返回结果集。

- DbResult Query(string sql, DbParams prms)
  - 执行带参数的查询（`?` 占位符）并返回
    结果集。值通过 sqlite3_bind_* 按原生方式绑定。
    失败时抛出 `DbException`——包括取行
    中途的 step 错误，不再把截短的结果集当成功返回。

- string ExecuteScalar(string sql)
  - 执行标量查询（第一行第一列）。

- string ExecuteScalar(string sql, DbParams prms)
  - 执行带参数的标量查询。

- long LastInsertId()
  - 获取最后插入行的 ID。

- string GetError()
  - 获取最后一条错误消息。

- async DbResult QueryAsync(string sql)
  - `Query(string)` 的协程版本。

- async DbResult QueryAsync(string sql, DbParams prms)
  - `Query(string, DbParams)` 的协程版本：准备/绑定留在
    reactor 上，仅 sqlite3_step 跨阻塞卸载边界；失败抛
    `DbException`。

- async int ExecuteAsync(string sql)
  - `Execute(string)` 的协程版本。

- async int ExecuteAsync(string sql, DbParams prms)
  - 协程版多语句执行：按 `pzTail` 逐条 prepare/step，
    参数只绑定到第一条语句。失败时抛出
    `DbException`，停在首条失败的语句上。

- async string ExecuteScalarAsync(string sql)
  - `ExecuteScalar(string)` 的协程版本。

- async string ExecuteScalarAsync(string sql, DbParams prms)
  - `ExecuteScalar(string, DbParams)` 的协程版本。

- async bool BeginTransactionAsync()
  - 协程版事务：执行 BEGIN TRANSACTION，恒返回 true。

- async bool CommitAsync()
  - 协程版提交：执行 COMMIT，恒返回 true。

- async bool RollbackAsync()
  - 协程版回滚：执行 ROLLBACK，恒返回 true。

- void BeginTransaction()
  - 开始一个事务。

- void Commit()
  - 提交当前事务。

- void Rollback()
  - 回滚当前事务。

- void ReleaseStmt(string sql, nint stmt, bool cached)
  - 关闭数据库连接。
    单语句执行的收尾：缓存语句 reset 后留待复用并清忙
    标志；一次性语句立即 finalize。所有退出路径（绑定失败、step
    失败、成功）都必须先走这里再抛错/返回。

- void Close()
  - 关闭连接：先 finalize 全部缓存语句（否则 sqlite3_close 返回
    SQLITE_BUSY），再关闭连接句柄。

- bool IsConnected()
  - 返回连接是否已打开。

- void Dispose()
  - 释放连接持有的资源（实现 IDisposable，等同于 Close）。

- int GetProvider()
  - 返回 provider id（DbProvider.SQLite）。


## SqliteConnector (class)

内置 SQLite 驱动，见 `IDbConnector`。

SQLite 是嵌入式数据库：打开数据库、执行语句都是库调用，
而非 reactor 上的 IO，因此这里的池化就是连接复用加
并发连接数上限，等待通过协程（而非线程）实现。

用法：
DbPool pool = SqliteConnector.Pool("app.sqlite3", 8);
IDbConnection db = await pool.AcquireAsync();
DbResult r = db.Query("SELECT * FROM users");
pool.Release(db);

`SqlitePool` 是对应的类型化池，返回的
`SqliteConnection` 带有 SQLite 专属成员
（LastInsertId、事务）。

- string filename;

- SqliteConnector(string filename)

- static DbPool Pool(string filename, int maxSize)
  - 到 <paramref name="filename"/> 的连接池。

- async IDbConnection ConnectAsync()
  - 打开一个新连接。SQLite 是库调用而非 reactor IO：
    打开在调用线程同步完成，返回的连接可立即使用。

- int GetProvider()
  - 池的 provider id（恒为 `DbProvider.SQLite`）。


## SqlitePool (class)

为 `SqliteConnection` 设计的协程感知连接池。

SQLite 是嵌入式的基于文件的引擎，没有网络 socket 可等待，
因此查询无法在 reactor 意义上"异步"。这个连接池提供的是
协程感知的生命周期管理：连接被复用，
而不是每次请求都新建/关闭，并发借出的
连接数也有上限。当池满时，
`AcquireAsync` 让调用协程挂起在
事件驱动的 `Gate` 上（不轮询），而不是阻塞 worker 线程，
所以其他协程可以继续运行，直到有连接通过
`Release` 归还。

该池依赖每个 scheduler 单线程的协作模型：
free-list 只在 await 点之间被访问，因此无需加锁。

用法：
SqlitePool pool = new SqlitePool("app.sqlite3", 8);
SqliteConnection db = await pool.AcquireAsync();
DbResult r = db.Query("SELECT * FROM users");
pool.Release(db);
pool.Close();

- string filename;

- PoolCore<SqliteConnection> core;

- SqlitePool(string filename, int maxSize)

- async SqliteConnection OpenOne()
  - 打开 SQLite 数据库是本地非阻塞调用；
    协程形态让获取连接的过程与网络驱动一致。

- async SqliteConnection AcquireAsync()
  - 借出一个连接：复用空闲连接，未达上限则新建，
    否则挂起协程，直到有连接被归还。饱和等待有硬上限
    （见 `PoolWait`）：超时返回 null，绝不定死。
    池已关闭后返回 null。

- void Release(SqliteConnection c)
  - 将连接归还给池。已损坏或 Close() 之后的
    连接会被直接关闭丢弃，不再入池。

- int IdleCount()
  - 空闲（已入池、可用）连接数。

- int LiveCount()
  - 存活连接总数（空闲 + 已借出）。

- void Close()
  - 关闭所有空闲连接并将池标记为已关闭。
    仍借出的连接会在归还时被关闭。
