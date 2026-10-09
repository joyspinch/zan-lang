# System.Data.Postgres

> 源码: `packages/Zan.Data/src/System/Data/Postgres/PgConnector.zan`, `packages/Zan.Data/src/System/Data/Postgres/PgPool.zan`, `packages/Zan.Data/src/System/Data/Postgres/PgWire.zan`, `packages/Zan.Data/src/System/Data/Postgres/PostgresConnection.zan`


## PgConnection (class)

- nint sock;

- TlsStream tls;

- TlsEngine syncTls;

- byte[]tlsIn;

- bool connected;

- string lastError;

- int lastAffected;

- bool hadError;

- int msgType;

- int msgLen;

- string host;

- int port;

- string database;

- string user;

- string password;

- int sslMode;

- string sslRootCert;

- int connectTimeout;

- int backendPid;

- int backendSecret;

- byte[]bb;

- int bp;

- bool busy;

- List<AsyncGate> waiters;

- async bool AcquireLock()

- void ReleaseLock()

- static bool verifyTls=true;

- static void SetTlsVerify(bool verify)

- PgConnection()

- void begin(int cap)

- void put8(int v)

- void put32(int v)

- void put16(int v)

- void putRaw(string s)

- void putCstr(string s)

- void putBytes(byte[]b, int len)

- int tlsFlushSync()

- int tlsRecvSync(byte[]dst, int want)

- int tlsSendSync(byte[]data, int len)

- int recvExactSync(byte[]dst, int off, int need)

- int sendRawSync(byte[]buf, int len)

- int sendMessageSync(int type, byte[]pay, int len)

- byte[]recvMessageSync()

- int sendPasswordSync(string text)

- async int recvExactAsync(byte[]dst, int off, int need)

- async int sendRawAsync(byte[]buf, int len)

- async int sendMessageAsync(int type, byte[]pay, int len)

- async byte[]recvMessageAsync()

- async int sendPasswordAsync(string text)

- int applyConninfo(string conninfo)

- int sslNegotiateSync()

- async int sslNegotiateAsync()

- bool tlsUpgradeSync()

- byte[]startupPayload()

- bool doConnectSync()

- async bool doConnectAsync()

- void recordParamStatus(byte[]pay)

- bool authLoopSync()

- async bool authLoopAsync()

- static PgConnection Open(string conninfo)

- static PgConnection OpenParams(string host, int port, string database, string user, string password)

- static async PgConnection OpenAsync(string conninfo)

- static async PgConnection OpenParamsAsync(string host, int port, string database, string user, string password)

- DbResult readResultsSync()

- async DbResult readResultsAsync()

- int Execute(string sql)

- DbResult Query(string sql)

- string ExecuteScalar(string sql)

- int Execute(string sql, DbParams prms)

- DbResult Query(string sql, DbParams prms)

- string ExecuteScalar(string sql, DbParams prms)

- bool sendExtendedSync(string dollarSql, DbParams prms)

- void die(string why)

- async DbResult QueryAsync(string sql)

- async int ExecuteAsync(string sql)

- async DbResult QueryParamsAsync(string sql, DbParams prms)

- async int ExecuteParamsAsync(string sql, DbParams prms)

- async bool sendExtendedAsync(string dollarSql, DbParams prms)

- void fail()

- string GetError()

- string serverVersion;

- string clientEncoding;

- void BeginTransaction()

- void Commit()

- void Rollback()

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql, DbParams prms)

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- async bool BeginTransactionAsync()

- async bool CommitAsync()

- async bool RollbackAsync()

- void Close()

- bool IsConnected()

- int GetProvider()

- void Dispose()


## PgConnector (class)

- string conninfo;

- PgConnector(string conninfo)

- static PgConnector CreateParams(string host, int port, string database, string user, string password)

- static DbPool Pool(string conninfo, int maxSize)

- static DbPool Pool(string host, int port, string database, string user, string password, int maxSize)

- static string BuildConninfo(string host, int port, string database, string user, string password)

- async IDbConnection ConnectAsync()

- int GetProvider()


## PgPool (class)

- string conninfo;

- PoolCore<PgConnection> core;

- string lastOpenError;

- PgPool(string conninfo, int maxSize)

- PgPool(string host, int port, string database, string user, string password, int maxSize):this("host="+host+" port="+Convert.ToString(port)+" dbname="+database+" user="+user+" password="+password, maxSize)

- async PgConnection OpenOne()

- async PgConnection AcquireAsync()

- void Release(PgConnection c)

- int IdleCount()

- int LiveCount()

- int WaitingCount()

- void Close()


## PgWire (class)

- static string bytesToStr(byte[]buf, int off, int len)

- static byte[]strToBytes(string s, int len)

- static byte[]slice4(byte[]b, int off)

- static bool hasPrefix(string b, string pre)

- static int be32(byte[]b, int off)

- static int parseConninfo(string s, List<string> outs)

- static bool isSpace(int c)

- static void readRowDescription(DbResult result, byte[]pay, int len, List<int> colOids)

- static void readDataRow(DbResult result, byte[]pay, int len, List<int> colOids)

- static string formatError(byte[]pay, int len)

- static int parseCommandTag(string tag)

- static string ToDollarParams(string sql)

- static string md5PasswordToken(string password, string user, byte[]salt)

- static string clientNonce()

- static string scramField(string msg, string key)

- static byte[]pbkdf2Sha256(string password, byte[]salt, int saltLen, int iters)

- static byte[]xor32(byte[]a, byte[]b)
