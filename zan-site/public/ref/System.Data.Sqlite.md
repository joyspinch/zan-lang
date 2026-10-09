# System.Data.Sqlite

> 源码: `packages/Zan.Data/src/System/Data/Sqlite/SqliteConnection.zan`, `packages/Zan.Data/src/System/Data/Sqlite/SqliteConnector.zan`, `packages/Zan.Data/src/System/Data/Sqlite/SqlitePool.zan`


## SqliteConnection (class)

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

- [DllImport("sqlite3")]static extern long sqlite3_column_int64(nint stmt, int col);

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

- SqliteConnection()

- static string CopyText(string s)

- static nint HandleFromBuf(string p)

- static SqliteConnection Open(string filename)

- static int BindAll(nint stmt, DbParams prms)

- void Fail(int rc)

- int Execute(string sql)

- int Execute(string sql, DbParams prms)

- DbResult Query(string sql)

- DbResult Query(string sql, DbParams prms)

- string ExecuteScalar(string sql)

- string ExecuteScalar(string sql, DbParams prms)

- long LastInsertId()

- string GetError()

- async DbResult QueryAsync(string sql)

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql)

- async int ExecuteAsync(string sql, DbParams prms)

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- async bool BeginTransactionAsync()

- async bool CommitAsync()

- async bool RollbackAsync()

- void BeginTransaction()

- void Commit()

- void Rollback()

- void ReleaseStmt(string sql, nint stmt, bool cached)

- void Close()

- bool IsConnected()

- void Dispose()

- int GetProvider()


## SqliteConnector (class)

- string filename;

- SqliteConnector(string filename)

- static DbPool Pool(string filename, int maxSize)

- async IDbConnection ConnectAsync()

- int GetProvider()


## SqlitePool (class)

- string filename;

- PoolCore<SqliteConnection> core;

- SqlitePool(string filename, int maxSize)

- async SqliteConnection OpenOne()

- async SqliteConnection AcquireAsync()

- void Release(SqliteConnection c)

- int IdleCount()

- int LiveCount()

- void Close()
