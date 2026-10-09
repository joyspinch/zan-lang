# System.Data.TDengine

> 源码: `packages/Zan.Data/src/System/Data/TDengine/TDengineConnection.zan`, `packages/Zan.Data/src/System/Data/TDengine/TDengineConnector.zan`, `packages/Zan.Data/src/System/Data/TDengine/TDenginePool.zan`


## TDengineConnection (class)

- string host;

- int port;

- string auth;

- string database;

- bool connected;

- string lastError;

- int lastCode;

- int affected;

- bool busy;

- List<AsyncGate> waiters;

- async bool AcquireLock()

- void ReleaseLock()

- TDengineConnection()

- static TDengineConnection Open(string host, int port, string user, string password)

- static TDengineConnection Open(string host, int port, string user, string password, string database)

- static TDengineConnection OpenToken(string host, int port, string token, string database)

- static async TDengineConnection OpenAsync(string host, int port, string user, string password, string database)

- static async TDengineConnection OpenTokenAsync(string host, int port, string token, string database)

- bool Ping()

- async bool PingAsync()

- string ServerVersion()

- void Use(string database)

- static string EscapeText(string val)

- static string Literal(DbParams prms, int i)

- static string RenderSql(string sql, DbParams prms)

- static string Dechunk(string body)

- static int HexOf(string text)

- static bool IsChunked(HttpResponse resp)

- string SqlPath()

- string Request(string sql)

- string BodyOf(string raw)

- string Unreachable()

- string Post(string sql)

- async string PostAsync(string sql)

- bool Failed(JsonValue root)

- void Fail()

- DbResult Decode(string body)

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

- async bool BeginTransactionAsync()

- async bool CommitAsync()

- async bool RollbackAsync()

- int AffectedRows()

- string GetError()

- void BeginTransaction()

- void Commit()

- void Rollback()

- bool IsConnected()

- void Close()

- int GetProvider()

- void Dispose()


## TDengineConnector (class)

- string host;

- int port;

- string user;

- string password;

- string token;

- string database;

- TDengineConnector(string host, int port, string user, string password, string token, string database)

- static TDengineConnector CreateToken(string host, int port, string token, string database)

- static DbPool Pool(string host, int port, string user, string password, string database, int maxSize)

- static DbPool PoolToken(string host, int port, string token, string database, int maxSize)

- async IDbConnection ConnectAsync()

- int GetProvider()


## TDenginePool (class)

- string host;

- int port;

- string user;

- string password;

- string token;

- string database;

- PoolCore<TDengineConnection> core;

- string lastOpenError;

- TDenginePool(string host, int port, string user, string password, string token, string database, int maxSize)

- TDenginePool(string host, int port, string token, string database, int maxSize):this(host, port, "", "", token, database, maxSize)

- async TDengineConnection OpenOne()

- async TDengineConnection AcquireAsync()

- void Release(TDengineConnection c)

- int IdleCount()

- int LiveCount()

- int WaitingCount()

- int MaxSize()

- void Close()
