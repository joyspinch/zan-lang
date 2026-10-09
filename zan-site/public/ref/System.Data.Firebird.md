# System.Data.Firebird

> 源码: `packages/Zan.Data/src/System/Data/Firebird/FbSql.zan`, `packages/Zan.Data/src/System/Data/Firebird/FbSrp.zan`, `packages/Zan.Data/src/System/Data/Firebird/FbWire.zan`, `packages/Zan.Data/src/System/Data/Firebird/FirebirdConnection.zan`, `packages/Zan.Data/src/System/Data/Firebird/FirebirdPool.zan`


## FbArc4 (class)

- List<int> s;

- int x;

- int y;

- FbArc4(FbBytes key)

- void Translate(string buf, int off, int len)


## FbBlobRef (class)

- FbRow row;

- int col;

- FbBytes id;

- int subtype;

- FbBlobRef(FbRow row, int col, FbBytes id, int subtype)

- static FbBlobRef Of(FbRow row, int col, FbBytes id, int subtype)


## FbBuf (class)

- List<int> b;

- static byte[]Alloc(int n)

- FbBuf()

- int Count()

- int At(int i)

- FbBuf U8(int v)

- FbBuf Int(int v)

- FbBuf Raw(string s)

- FbBuf RawBlock(FbBytes src)

- void pad()

- FbBuf Block(FbBytes src)

- FbBuf Str(string s)

- FbBytes ToBytes()


## FbBytes (class)

- byte[]data;

- int len;

- FbBytes(byte[]data, int len)

- static FbBytes Own(byte[]data, int len)

- static FbBytes CopyOf(string src, int len)

- static FbBytes Of(string s)

- static FbBytes Alloc(int n)

- static FbBytes Empty()

- byte[]Data()

- int Len()

- int At(int i)

- void SetAt(int i, int v)

- string Text()

- FbBytes Slice(int off, int n)

- string Hex()

- static FbBytes FromHex(string hex)

- static int nibble(int c)

- void Release()


## FbCell (class)

- public string value;

- public bool isNull;

- FbCell(string value, bool isNull)


## FbColumn (class)

- int sqltype;

- int subtype;

- int scale;

- int sqllen;

- bool nullOk;

- string field;

- string relation;

- string owner;

- string alias;

- FbColumn()

- static FbColumn Of(int sqltype, int scale, int sqllen)

- int SqlType()

- int SubType()

- int Scale()

- int Length()

- bool NullOk()

- string Field()

- string Relation()

- string Name()

- int IoLength()


## FbError (class)

- List<int> codes;

- int sqlCode;

- string message;

- FbError()

- void AddCode(int c)

- void SetSqlCode(int c)

- void SetMessage(string m)

- bool Failed()

- int SqlCode()

- string Message()

- int CodeCount()

- int CodeAt(int i)

- bool Has(int code)

- static string Text(int code)


## FbOp (class)

- static int CONNECT=1;

- static int EXIT=2;

- static int ACCEPT=3;

- static int REJECT=4;

- static int DISCONNECT=6;

- static int RESPONSE=9;

- static int ATTACH=19;

- static int CREATE=20;

- static int DETACH=21;

- static int TRANSACTION=29;

- static int COMMIT=30;

- static int ROLLBACK=31;

- static int OPEN_BLOB2=56;

- static int GET_SEGMENT=36;

- static int CLOSE_BLOB=39;

- static int ALLOCATE_STATEMENT=62;

- static int EXECUTE=63;

- static int FETCH=65;

- static int FETCH_RESPONSE=66;

- static int FREE_STATEMENT=67;

- static int PREPARE_STATEMENT=68;

- static int INFO_SQL=70;

- static int DUMMY=71;

- static int SQL_RESPONSE=78;

- static int COMMIT_RETAINING=50;

- static int CONT_AUTH=92;

- static int PING=93;

- static int ACCEPT_DATA=94;

- static int CRYPT=96;

- static int COND_ACCEPT=98;

- static int CONNECT_VERSION=3;

- static int ARCH_GENERIC=1;

- static int PROTOCOL_V13=0xFFFF800D;

- static int PTYPE_RPC=2;

- static int PTYPE_BATCH_SEND=3;

- static int DSQL_CLOSE=1;

- static int DSQL_DROP=2;

- static int ARG_END=0;

- static int ARG_GDS=1;

- static int ARG_STRING=2;

- static int ARG_CSTRING=3;

- static int ARG_NUMBER=4;

- static int ARG_INTERPRETED=5;

- static int ARG_SQL_STATE=19;

- static int CNCT_USER=1;

- static int CNCT_HOST=4;

- static int CNCT_USER_VERIFICATION=6;

- static int CNCT_SPECIFIC_DATA=7;

- static int CNCT_PLUGIN_NAME=8;

- static int CNCT_LOGIN=9;

- static int CNCT_PLUGIN_LIST=10;

- static int CNCT_CLIENT_CRYPT=11;

- static int DPB_VERSION1=1;

- static int DPB_USER_NAME=28;

- static int DPB_SQL_ROLE_NAME=60;

- static int DPB_LC_CTYPE=48;

- static int DPB_SQL_DIALECT=63;

- static int DPB_PROCESS_NAME=74;

- static int DPB_PROCESS_ID=71;

- static int DPB_SPECIFIC_AUTH_DATA=84;

- static int DPB_UTF8_FILENAME=77;

- static int TPB_VERSION3=3;

- static int TPB_WAIT=6;

- static int TPB_WRITE=9;

- static int TPB_READ_COMMITTED=15;

- static int TPB_REC_VERSION=17;


## FbParamBlock (class)

- FbBytes blr;

- FbBytes values;

- FbParamBlock(FbBytes blr, FbBytes values)

- static FbParamBlock Of(FbBytes blr, FbBytes values)

- FbBytes Blr()

- FbBytes Values()

- void Release()


## FbReader (class)

- string buf;

- int pos;

- int len;

- FbReader(string buf, int len)

- static FbReader Over(FbBytes b)

- static FbReader Over(string buf, int len)

- int Pos()

- int Left()

- bool Eof()

- void Skip(int n)

- int U8()

- int U16LE()

- int UIntLE(int n)

- int IntLE(int n)

- int Int()

- int IntBE(int n)

- long LongBE(int n)

- FbBytes Raw(int n)

- string Text(int n)


## FbResponse (class)

- int handle;

- FbBytes oid;

- FbBytes buf;

- FbError err;

- FbResponse(int handle, FbBytes oid, FbBytes buf, FbError err)

- static FbResponse Of(int handle, FbBytes oid, FbBytes buf, FbError err)

- int Handle()

- FbBytes Oid()

- FbBytes Buf()

- FbError Err()

- bool Failed()

- void Release()


## FbRow (class)

- List<FbCell> cells;

- FbRow(int n)

- void Set(int i, string v)

- bool IsNull(int i)

- string ValueAt(int i)

- List<string> Values()

- List<bool> Nulls()


## FbSrp (class)

- static string PRIME_HEX="e67d2e994b2f900c3f41f08f5bb2627ed0d49ee1fe767a52efcd565cd6e768812c3e1e9ce8f0a8bea6cb13cd29ddebf7a96d4a93b55d488df099a15c89dcb0640738eb2cbdd9a8f7bab561ab1b0dc1c6cdabf303264a08d1bca932d1f1ee428b619d970f342aba9a65793b8b2f041ae5364350c16f735f56ecbca87bd57b29e7";

- static string MULT_HEX="dfc212b4bd69674855cfceb30002b5c306ac60b5";

- static int PRIVATE_BYTES=32;

- static int WIDTH=128;

- BigInt priv;

- FbBytes pub;

- FbSrp(BigInt priv, FbBytes pub)

- static FbSrp Create()

- static FbSrp FromPrivateHex(string hex)

- static FbSrp FromPrivate(BigInt a)

- static BigInt Prime()

- static BigInt Multiplier()

- static BigInt Generator()

- FbBytes Public()

- string PublicHex()

- static FbBytes Minimal(BigInt v)

- static BigInt ToInt(FbBytes b)

- static FbBytes Sha1Of(FbBytes b)

- static FbBytes Sha256Of(FbBytes b)

- static string NormalizeUser(string user)

- static BigInt UserHash(string user, string password, FbBytes salt)

- static BigInt Scramble(FbBytes a, FbBytes b)

- FbSrpProof Prove(string user, string password, FbBytes salt, FbBytes serverPub, bool sha256)


## FbSrpProof (class)

- FbBytes proof;

- FbBytes key;

- FbSrpProof(FbBytes proof, FbBytes key)

- static FbSrpProof Of(FbBytes proof, FbBytes key)

- FbBytes Proof()

- FbBytes Key()


## FbType (class)

- static int TEXT=452;

- static int VARYING=448;

- static int SHORT=500;

- static int LONG=496;

- static int FLOAT=482;

- static int DOUBLE=480;

- static int D_FLOAT=530;

- static int TIMESTAMP=510;

- static int BLOB=520;

- static int ARRAY=540;

- static int QUAD=550;

- static int TIME=560;

- static int DATE=570;

- static int INT64=580;

- static int INT128=32752;

- static int TIMESTAMP_TZ=32754;

- static int TIME_TZ=32756;

- static int DEC_FIXED=32758;

- static int DEC64=32760;

- static int DEC128=32762;

- static int BOOLEAN=32764;

- static int NULL=32766;

- static int INFO_END=1;

- static int INFO_TRUNCATED=2;

- static int INFO_SELECT=4;

- static int INFO_BIND=5;

- static int INFO_DESCRIBE_VARS=7;

- static int INFO_DESCRIBE_END=8;

- static int INFO_SQLDA_SEQ=9;

- static int INFO_TYPE=11;

- static int INFO_SUB_TYPE=12;

- static int INFO_SCALE=13;

- static int INFO_LENGTH=14;

- static int INFO_NULL_IND=15;

- static int INFO_FIELD=16;

- static int INFO_RELATION=17;

- static int INFO_OWNER=18;

- static int INFO_ALIAS=19;

- static int INFO_SQLDA_START=20;

- static int INFO_STMT_TYPE=21;

- static int INFO_RECORDS=23;

- static int REQ_SELECT_COUNT=13;

- static int REQ_INSERT_COUNT=14;

- static int REQ_UPDATE_COUNT=15;

- static int REQ_DELETE_COUNT=16;

- static int STMT_SELECT=1;

- static int STMT_INSERT=2;

- static int STMT_UPDATE=3;

- static int STMT_DELETE=4;

- static int STMT_DDL=5;

- static int STMT_EXEC_PROCEDURE=8;

- static int STMT_SELECT_FOR_UPD=12;


## FbValue (class)

- static string HEX="0123456789abcdef";

- static long DoubleBits(double v)

- static double Pow2(int e)

- static string Ieee(int sign, int exp, long mant, int bias, int mantBits)

- static string Pad2(int v)

- static string Pad4(int v)

- static string CivilDate(int z)

- static string Date(int nday)

- static string Time(int n)

- static string Scaled(long units, int scale)

- static string WideDecimal(FbBytes raw, int scale)

- static string Hex(FbBytes raw)

- static string Decode(FbColumn col, FbBytes raw)

- static FbParamBlock Params(DbParams prms)

- static void appendBE(FbBuf b, long v, int n)


## FbXsqlda (class)

- List<FbColumn> cols;

- int stmtType;

- FbXsqlda()

- int Count()

- FbColumn At(int i)

- int StmtType()

- void SetStmtType(int t)

- bool IsSelect()

- void Resize(int n)

- int Parse(FbBytes buf)

- int ParseItems(FbReader r)

- FbBytes CalcBlr()


## FirebirdConnection (class)

- nint sock;

- bool connected;

- string lastError;

- int lastAffected;

- string database;

- string user;

- string plugin;

- string cryptPlugin;

- int acceptVersion;

- int dbHandle;

- int transHandle;

- FbBytes authData;

- FbArc4 encIn;

- FbArc4 encOut;

- bool wantCrypt;

- FbError lastStatus;

- bool busy;

- List<AsyncGate> waiters;

- async bool AcquireLock()

- void ReleaseLock()

- static int FETCH_ROWS=400;

- static int INFO_BUFFER=32768;

- static int MAX_BLOCK=33554432;

- FirebirdConnection()

- async int recvExact(byte[]dst, int off, int need)

- async bool sendPacket(FbBytes pkt)

- async int recvInt()

- async int recvSigned()

- async FbBytes recvAligned(int n)

- async FbBytes recvBlock()

- async FbError recvStatus()

- static void fill(List<string> segs, int argNum, string text)

- async FbResponse recvResponse()

- async FbResponse roundTrip(FbBytes pkt)

- static async FirebirdConnection OpenAsync(string host, int port, string database, string user, string password)

- static async FirebirdConnection OpenAsync(string host, int port, string database, string user, string password, bool wireCrypt)

- async bool handshake(string host, int port, string db, string user, string pw)

- static FbBytes ConnectPacket(string database, string user, string pubHex, bool wireCrypt)

- static void cnct(FbBuf b, int tag, string v)

- static void cnctChunked(FbBuf b, int tag, string v)

- async bool authenticate(FbSrp srp, string user, string password)

- async FbResponse recvResponse4()

- static FbBytes ContAuth(FbBytes authData, string plugin, string pluginList)

- static string GuessWireCrypt(FbBytes buf)

- async bool startCrypt(FbBytes sessionKey)

- static FbBytes AttachPacket(string database, string user, FbBytes authData)

- static void dpbStr(FbBuf b, int tag, string v)

- async bool beginTransaction()

- async bool commitRetaining()

- async bool CommitAsync()

- async bool RollbackAsync()

- async int allocStatement()

- static FbBytes describeItems()

- async FbXsqlda prepare(int stmt, string sql)

- async bool execute(int stmt, DbParams prms)

- async bool freeStatement(int stmt)

- async int affectedRows(int stmt, bool select)

- async FbBytes fetchValue(FbColumn col)

- async DbResult fetchAll(int stmt, FbXsqlda xs)

- async string readBlob(FbBytes blobId, int subtype)

- async DbResult QueryAsync(string sql)

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql)

- async int ExecuteAsync(string sql, DbParams prms)

- void fail()

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- async bool PingAsync()

- int AffectedRows()

- string GetError()

- int SqlCode()

- string AuthPlugin()

- string WireCrypt()

- int ProtocolVersion()

- string Database()

- bool IsConnected()

- int GetProvider()

- async void CloseAsync()

- void Close()


## FirebirdPool (class)

- string host;

- int port;

- string database;

- string user;

- string password;

- bool wireCrypt;

- PoolCore<FirebirdConnection> core;

- string lastOpenError;

- FirebirdPool(string host, int port, string database, string user, string password, bool wireCrypt, int maxSize)

- FirebirdPool(string host, int port, string database, string user, string password, int maxSize):this(host, port, database, user, password, true, maxSize)

- async FirebirdConnection OpenOne()

- async FirebirdConnection AcquireAsync()

- void Release(FirebirdConnection c)

- int IdleCount()

- int LiveCount()

- int WaitingCount()

- int MaxSize()

- bool IsClosed()

- int GetProvider()

- void Close()
