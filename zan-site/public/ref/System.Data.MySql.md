# System.Data.MySql

> 源码: `packages/Zan.Data/src/System/Data/MySql/MySqlConnection.zan`, `packages/Zan.Data/src/System/Data/MySql/MySqlConnector.zan`, `packages/Zan.Data/src/System/Data/MySql/MySqlPool.zan`, `packages/Zan.Data/src/System/Data/MySql/MySqlSyncConnection.zan`, `packages/Zan.Data/src/System/Data/MySql/MySqlWire.zan`


## MySqlAsyncConnector (class)

- string host;

- int port;

- string database;

- string user;

- string password;

- MySqlAsyncConnector(string host, int port, string database, string user, string password)

- static DbPool Pool(string host, int port, string database, string user, string password, int maxSize)

- async IDbConnection ConnectAsync()

- int GetProvider()


## MySqlConnection (class)

- nint sock;

- bool connected;

- string lastError;

- int lastNumber;

- int lastLen;

- int lastSeq;

- int lastAffected;

- bool hadError;

- List<PreparedStmt> stmtCache;

- TlsStream tls;

- bool busy;

- List<AsyncGate> waiters;

- async bool AcquireLock()

- void ReleaseLock()

- MySqlConnection()

- async int recvExact(byte[]dst, int off, int need)

- internal async byte[]readPacket()

- internal async int writePacket(string payload, int len, int seq)

- bool isEof(byte[]pkt)

- static byte[]scramble41(string pw, string nonce20)

- static byte[]scrambleNative(string pw, string nonce20)

- static byte[]authToken(string plugin, string pw, string nonce20, List<int> outLen)

- static byte[]genSeed(int n)

- static byte[]pemToDer(string pem, int pemLen, List<int> outLen)

- static byte[]fullAuthCipher(string der, int derLen, string obf, int obfLen, string seed, List<int> kOut)

- static async MySqlConnection OpenParamsAsync(string host, int port, string database, string user, string password)

- static bool verifyTls=true;

- static void SetTlsVerify(bool verify)

- static async MySqlConnection OpenSecureParamsAsync(string host, int port, string database, string user, string password)

- static async MySqlConnection OpenSecureParamsAsync(string host, int port, string database, string user, string password, string sslCa)

- internal async bool doConnect(string host, int port, string db, string user, string pw, bool secure, string sslCa)

- internal async int comQuery(string sql)

- async DbResult QueryAsync(string sql)

- async int ExecuteAsync(string sql)

- void fail()

- async string ExecuteScalarAsync(string sql)

- static long readBinLong(string buf, List<int> cur, int type, int flags)

- static string readBinValue(string buf, List<int> cur, int type, int flags)

- internal async bool stmtPrepare(string sql, List<int> outIds)

- internal async int stmtExecute(int stmtId, DbParams prms)

- internal async int stmtClose(int stmtId)

- int stmtCacheFind(string sql)

- async int stmtCacheAdd(string sql, int stmtId)

- void stmtCacheRemove(int stmtId)

- async DbResult QueryParamsAsync(string sql, DbParams prms)

- async int ExecuteParamsAsync(string sql, DbParams prms)

- async string ExecuteScalarParamsAsync(string sql, DbParams prms)

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql, DbParams prms)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- async bool BeginTransactionAsync()

- async bool CommitAsync()

- async bool RollbackAsync()

- int GetProvider()

- static DbException Sync()

- DbResult Query(string sql)

- DbResult Query(string sql, DbParams prms)

- int Execute(string sql)

- int Execute(string sql, DbParams prms)

- string ExecuteScalar(string sql)

- string ExecuteScalar(string sql, DbParams prms)

- void BeginTransaction()

- void Commit()

- void Rollback()

- string GetError()

- bool IsConnected()

- void Close()

- void Dispose()


## MySqlConnector (class)

- string host;

- int port;

- string database;

- string user;

- string password;

- MySqlConnector(string host, int port, string database, string user, string password)

- static DbPool Pool(string host, int port, string database, string user, string password, int maxSize)

- async IDbConnection ConnectAsync()

- int GetProvider()


## MySqlPool (class)

- string host;

- int port;

- string database;

- string user;

- string password;

- PoolCore<MySqlConnection> core;

- string lastOpenError;

- MySqlPool(string host, int port, string database, string user, string password, int maxSize)

- async MySqlConnection OpenOne()

- async MySqlConnection AcquireAsync()

- void Release(MySqlConnection c)

- int IdleCount()

- int LiveCount()

- void Close()


## MySqlSyncConnection (class)

- nint sock;

- bool connected;

- string lastError;

- int lastNumber;

- int lastLen;

- int lastSeq;

- int lastAffected;

- bool hadError;

- List<PreparedStmt> stmtCache;

- MySqlSyncConnection()

- int recvExact(byte[]dst, int off, int need)

- byte[]readPacket()

- int writePacket(string payload, int len, int seq)

- bool isEof(byte[]pkt)

- static byte[]scramble41(string pw, string nonce20)

- static byte[]scrambleNative(string pw, string nonce20)

- static byte[]authToken(string plugin, string pw, string nonce20, List<int> outLen)

- static byte[]genSeed(int n)

- static byte[]pemToDer(string pem, int pemLen, List<int> outLen)

- static byte[]fullAuthCipher(string der, int derLen, string obf, int obfLen, string seed, List<int> kOut)

- static MySqlSyncConnection OpenParams(string host, int port, string database, string user, string password)

- bool doConnect(string host, int port, string db, string user, string pw)

- int comQuery(string sql)

- DbResult querySync(string sql)

- static long readBinLong(string buf, List<int> cur, int type, int flags)

- static string readBinValue(string buf, List<int> cur, int type, int flags)

- bool stmtPrepare(string sql, List<int> outIds)

- int stmtExecute(int stmtId, DbParams prms)

- int stmtClose(int stmtId)

- int stmtCacheFind(string sql)

- void stmtCacheAdd(string sql, int stmtId)

- void stmtCacheRemove(int stmtId)

- DbResult queryParams(string sql, DbParams prms)

- DbResult Query(string sql, DbParams prms)

- int Execute(string sql, DbParams prms)

- DbResult Query(string sql)

- int Execute(string sql)

- string ExecuteScalar(string sql)

- string ExecuteScalar(string sql, DbParams prms)

- int GetProvider()

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

- bool IsConnected()

- void Close()

- void Dispose()


## MySqlWire (class)

- static string bytesToStr(string buf, int off, int len)

- static long readLenenc(string buf, List<int> cur)

- static string readLenencStr(string buf, List<int> cur)

- static int findSub(string h, int hlen, string ndl, int nlen, int from)

- static int readDerLen(string der, List<int> c)

- static int writeLenenc(string buf, int pos, int v)

- static double pow2(int e)

- static string pad2(int v)

- static string ieeeToText(int sign, int exp, long mant, int expBias, int mantBits)


## PreparedStmt (class)

- string sql;

- int id;

- PreparedStmt(string sql, int id)
