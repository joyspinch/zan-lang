# System.Data.Odbc

> 源码: `packages/Zan.Data/src/System/Data/Odbc/DbConnection.zan`, `packages/Zan.Data/src/System/Data/Odbc/OdbcConnector.zan`


## DbConnection (class)

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

- [DllImport("odbc32")]static extern int SQLPrepare(nint stmt, string sql, int textLength);

- [DllImport("odbc32")]static extern int SQLBindParameter(nint stmt, int ipar, int ptype, int ctype, int sqltype, long colDef, int scale, string valuePtr, long valueMax, byte[]indPtr);

- [DllImport("odbc32")]static extern int SQLExecute(nint stmt);

- [DllImport("odbc32")]static extern int SQLNumResultCols(nint stmt, string colCountPtr);

- [DllImport("odbc32")]static extern int SQLDescribeCol(nint stmt, int colNum, string colName, int bufLen, string nameLenPtr, string typePtr, string sizePtr, string decPtr, string nullPtr);

- [DllImport("odbc32")]static extern int SQLFetch(nint stmt);

- [DllImport("odbc32")]static extern int SQLGetData(nint stmt, int colNum, int targetType, string buf, int bufLen, string strLenOrIndPtr);

- [DllImport("odbc32")]static extern int SQLRowCount(nint stmt, string rowCountPtr);

- [DllImport("odbc32")]static extern int SQLFreeHandle(int handleType, nint handle);

- [DllImport("odbc32")]static extern int SQLDisconnect(nint dbc);

- [DllImport("odbc32")]static extern int SQLFreeStmt(nint stmt, int option);

- [DllImport("odbc32")]static extern int SQLGetDiagRec(int handleType, nint handle, int recNum, string sqlState, string nativeErrPtr, string msgBuf, int msgBufLen, string msgLenPtr);

- [DllImport("odbc")]static extern int SQLAllocHandle(int handleType, nint inputHandle, string outputHandle);

- [DllImport("odbc")]static extern int SQLSetEnvAttr(nint env, int attr, nint valuePtr, int strLen);

- [DllImport("odbc")]static extern int SQLDriverConnect(nint dbc, nint hwnd, string inConn, int inLen, string outConn, int outBufLen, string outLenPtr, int completion);

- [DllImport("odbc")]static extern int SQLExecDirect(nint stmt, string sql, int textLength);

- [DllImport("odbc")]static extern int SQLPrepare(nint stmt, string sql, int textLength);

- [DllImport("odbc")]static extern int SQLBindParameter(nint stmt, int ipar, int ptype, int ctype, int sqltype, long colDef, int scale, string valuePtr, long valueMax, byte[]indPtr);

- [DllImport("odbc")]static extern int SQLExecute(nint stmt);

- [DllImport("odbc")]static extern int SQLNumResultCols(nint stmt, string colCountPtr);

- [DllImport("odbc")]static extern int SQLDescribeCol(nint stmt, int colNum, string colName, int bufLen, string nameLenPtr, string typePtr, string sizePtr, string decPtr, string nullPtr);

- [DllImport("odbc")]static extern int SQLFetch(nint stmt);

- [DllImport("odbc")]static extern int SQLGetData(nint stmt, int colNum, int targetType, string buf, int bufLen, string strLenOrIndPtr);

- [DllImport("odbc")]static extern int SQLRowCount(nint stmt, string rowCountPtr);

- [DllImport("odbc")]static extern int SQLFreeHandle(int handleType, nint handle);

- [DllImport("odbc")]static extern int SQLDisconnect(nint dbc);

- [DllImport("odbc")]static extern int SQLFreeStmt(nint stmt, int option);

- [DllImport("odbc")]static extern int SQLGetDiagRec(int handleType, nint handle, int recNum, string sqlState, string nativeErrPtr, string msgBuf, int msgBufLen, string msgLenPtr);

- DbConnection()

- static nint HandleFromBuf(string p)

- static int Int16FromBuf(string p)

- static int ScanNul(string buf, int max)

- static string BufToString(string buf, int max)

- static bool OdbcSuccess(int rc)

- static DbConnection OpenODBC(string connectionString)

- static DbConnection OpenSqlServer(string host, int port, string database, string user, string password)

- static IDbConnection OpenMySQL(string host, int port, string database, string user, string password)

- static IDbConnection OpenMariaDB(string host, int port, string database, string user, string password)

- static DbConnection OpenPostgreSQL(string host, int port, string database, string user, string password)

- static DbConnection OpenOpenGauss(string host, int port, string database, string user, string password)

- static DbConnection OpenKingbase(string host, int port, string database, string user, string password)

- static DbConnection OpenQuestDB(string host, int port, string user, string password)

- static DbConnection OpenOracle(string host, int port, string service, string user, string password)

- static DbConnection OpenFirebird(string host, int port, string dbPath, string user, string password)

- static DbConnection OpenClickHouse(string host, int port, string database, string user, string password)

- static DbConnection OpenTDengine(string host, int port, string database, string user, string password)

- static DbConnection OpenDuckDB(string path)

- static DbConnection OpenDM(string host, int port, string user, string password)

- static DbConnection OpenSQLite(string path)

- static DbConnection OpenWith(string connectionString, int provider)

- nint AllocStmt()

- void Fail()

- void Diagnose(int handleType, nint h)

- int Execute(string sql)

- int BindParams(nint stmt, DbParams prms, List<string> keepValues, List <byte[]> keepInds)

- async int ExecuteAsync(string sql)

- async int ExecuteAsync(string sql, DbParams prms)

- async int ExecuteCoreAsync(string sql, DbParams prms)

- async DbResult QueryAsync(string sql)

- async DbResult QueryAsync(string sql, DbParams prms)

- async DbResult QueryCoreAsync(string sql, DbParams prms)

- DbResult Query(string sql)

- string ExecuteScalar(string sql)

- string ExecuteScalar(string sql, DbParams prms)

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- async bool BeginTransactionAsync()

- async bool CommitAsync()

- async bool RollbackAsync()

- string GetError()

- void BeginTransaction()

- void Commit()

- void Rollback()

- void Close()

- bool IsConnected()

- int GetProvider()

- int Execute(string sql, DbParams prms)

- DbResult Query(string sql, DbParams prms)

- void Dispose()


## OdbcConnector (class)

- string connectionString;

- int provider;

- OdbcConnector(string connectionString, int provider)

- static DbPool Pool(string connectionString, int provider, int maxSize)

- async IDbConnection ConnectAsync()

- int GetProvider()
