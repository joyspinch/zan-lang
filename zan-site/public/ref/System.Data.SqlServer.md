# System.Data.SqlServer

> 源码: `packages/Zan.Data/src/System/Data/SqlServer/SqlServerConnection.zan`, `packages/Zan.Data/src/System/Data/SqlServer/SqlServerPool.zan`, `packages/Zan.Data/src/System/Data/SqlServer/TdsCodec.zan`, `packages/Zan.Data/src/System/Data/SqlServer/TdsMessage.zan`, `packages/Zan.Data/src/System/Data/SqlServer/TdsTypes.zan`


## SqlServerConnection (class)

- nint sock;

- bool connected;

- string lastError;

- int lastNumber;

- int lastAffected;

- string serverVersion;

- string database;

- string appName;

- int packetSize;

- int packetId;

- TlsStream tls;

- bool busy;

- List<AsyncGate> waiters;

- async bool AcquireLock()

- void ReleaseLock()

- static bool verifyTls=true;

- static void SetTlsVerify(bool verify)

- SqlServerConnection()

- async int recvExact(byte[]dst, int off, int need)

- async int recvExactRaw(byte[]dst, int off, int need)

- async bool sendMessage(int type, TdsBytes payload)

- async TdsBytes recvMessage()

- static async SqlServerConnection OpenAsync(string host, int port, string database, string user, string password)

- static async SqlServerConnection OpenSecureAsync(string host, int port, string database, string user, string password)

- async bool handshake(string host, int port, string db, string user, string pw, bool secure)

- async bool tlsUpgrade(string host)

- async TdsResponse roundTrip(int type, TdsBytes payload)

- async DbResult QueryAsync(string sql)

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql)

- async int ExecuteAsync(string sql, DbParams prms)

- void fail()

- async string ExecuteScalarAsync(string sql)

- static string Renumber(string sql, int count)

- int AffectedRows()

- string GetError()

- string ServerVersion()

- string Database()

- bool IsConnected()

- int GetProvider()

- void Close()


## SqlServerPool (class)

- string host;

- int port;

- string database;

- string user;

- string password;

- PoolCore<SqlServerConnection> core;

- string lastOpenError;

- SqlServerPool(string host, int port, string database, string user, string password, int maxSize)

- async SqlServerConnection OpenOne()

- async SqlServerConnection AcquireAsync()

- void Release(SqlServerConnection c)

- int IdleCount()

- int LiveCount()

- int WaitingCount()

- int MaxSize()

- void Close()


## TdsBuf (class)

- List<int> b;

- static byte[]Alloc(int n)

- TdsBuf()

- int Count()

- int At(int i)

- internal void SetAt(int i, int v)

- TdsBuf U8(int v)

- TdsBuf U16LE(int v)

- TdsBuf U16BE(int v)

- TdsBuf U32LE(int v)

- TdsBuf U32BE(int v)

- TdsBuf U64LE(long v)

- TdsBuf Bytes(string s)

- TdsBuf Block(TdsBytes src)

- TdsBuf Ucs2(string s)

- TdsBuf Password(string s)

- TdsBytes ToBytes()


## TdsBytes (class)

- byte[]data;

- int len;

- TdsBytes(byte[]data, int len)

- static TdsBytes Own(byte[]data, int len)

- static TdsBytes Of(string s)

- static TdsBytes Alloc(int n)

- byte[]Data()

- int Len()

- int At(int i)

- void SetAt(int i, int v)

- void Release()


## TdsCell (class)

- string text;

- bool isNull;

- long ival;

- int intKind;

- TdsCell(string text, bool isNull)

- static TdsCell Of(string text)

- static TdsCell Null()

- static TdsCell OfInt(long v)

- string Text()

- bool IsNull()

- int IntKind()

- long IVal()


## TdsColumn (class)

- string name;

- int type;

- int size;

- int precision;

- int scale;

- TdsColumn()

- string Name()

- int Type()

- int Size()

- int Scale()

- int Precision()


## TdsMessage (class)

- static int TDS74=1946157060;

- static TdsBytes Prelogin(int encrypt)

- static int PreloginEncryption(TdsBytes payload)

- internal static TdsBytes Login7(string host, string user, string password, string appName, string server, string database, int packetSize)

- static TdsBuf AllHeaders(TdsBuf b)

- internal static TdsBytes SqlBatch(string sql)

- internal static TdsBytes RpcExecuteSql(string sql, DbParams prms)

- static string Declaration(DbParams prms)

- static void ParamHeader(TdsBuf b, string name)

- static void NVarcharParam(TdsBuf b, string name, string val)

- static void IntParam(TdsBuf b, string name, long val)

- static void FloatParam(TdsBuf b, string name, double val)

- static void NullParam(TdsBuf b, string name)

- static long DoubleBits(double v)

- static int TOK_RETURNSTATUS=121;

- static int TOK_COLMETADATA=129;

- static int TOK_TABNAME=164;

- static int TOK_COLINFO=165;

- static int TOK_ORDER=169;

- static int TOK_ERROR=170;

- static int TOK_INFO=171;

- static int TOK_RETURNVALUE=172;

- static int TOK_LOGINACK=173;

- static int TOK_FEATUREACK=174;

- static int TOK_ROW=209;

- static int TOK_NBCROW=210;

- static int TOK_SSPI=237;

- static int TOK_ENVCHANGE=227;

- static int TOK_DONE=253;

- static int TOK_DONEPROC=254;

- static int TOK_DONEINPROC=255;

- static TdsResponse Decode(TdsBytes buf)

- static List<TdsColumn> ReadColumns(TdsReader r, TdsResponse resp)

- static void ReadRow(TdsReader r, List<TdsColumn> cols, TdsResponse resp, bool nbc)

- static void ReadError(TdsReader r, TdsResponse resp)

- static void ReadLoginAck(TdsReader r, TdsResponse resp)

- static void ReadEnvChange(TdsReader r, TdsResponse resp)

- static void SkipFeatureAck(TdsReader r)

- static void SkipReturnValue(TdsReader r)


## TdsPacket (class)

- static int SQLBATCH=1;

- static int RPC=3;

- static int REPLY=4;

- static int ATTENTION=6;

- static int LOGIN7=16;

- static int PRELOGIN=18;

- static int STATUS_NORMAL=0;

- static int STATUS_EOM=1;

- static int HEADER_LEN=8;

- static int DEFAULT_SIZE=4096;

- static TdsBytes Frame(int type, int status, int packetId, TdsBytes payload, int off, int len)


## TdsReader (class)

- string buf;

- int pos;

- int len;

- TdsReader(string buf, int len)

- static TdsReader Over(string buf)

- static TdsReader Over(string buf, int len)

- static TdsReader Over(TdsBytes b)

- int Pos()

- int Left()

- bool Eof()

- void Seek(int p)

- int U8()

- int U16LE()

- int U16BE()

- int U32LE()

- int U32BE()

- long UIntLE(int n)

- long IntLE(int n)

- void Skip(int n)

- string Narrow(int n)

- TdsBytes Raw(int n)

- string Ucs2(int chars)

- string BVarchar()

- string UsVarchar()


## TdsResponse (class)

- DbResult rows;

- int affected;

- string error;

- int errorNumber;

- string serverVersion;

- string database;

- TdsResponse()

- DbResult Rows()

- int Affected()

- string Error()

- int ErrorNumber()

- string ServerVersion()

- string Database()

- bool Failed()


## TdsType (class)

- static int NULLTYPE=31;

- static int INT1=48;

- static int BIT=50;

- static int INT2=52;

- static int INT4=56;

- static int DATETIM4=58;

- static int FLT4=59;

- static int MONEY=60;

- static int DATETIME=61;

- static int FLT8=62;

- static int MONEY4=122;

- static int INT8=127;

- static int GUIDN=36;

- static int INTN=38;

- static int DECIMAL=55;

- static int NUMERIC=63;

- static int BITN=104;

- static int DECIMALN=106;

- static int NUMERICN=108;

- static int FLTN=109;

- static int MONEYN=110;

- static int DATETIMN=111;

- static int DATEN=40;

- static int TIMEN=41;

- static int DATETIME2N=42;

- static int DTOFFSETN=43;

- static int CHAR=47;

- static int VARCHAR=39;

- static int BINARY=45;

- static int VARBINARY=37;

- static int BIGVARBIN=165;

- static int BIGVARCHR=167;

- static int BIGBINARY=173;

- static int BIGCHAR=175;

- static int NVARCHAR=231;

- static int NCHAR=239;

- static int XML=241;

- static int UDT=240;

- static int TEXT=35;

- static int IMAGE=34;

- static int NTEXT=99;

- static bool IsWide(int t)

- static bool IsBinary(int t)


## TdsValue (class)

- static string HEX="0123456789abcdef";

- static void ReadTypeInfo(TdsReader r, TdsColumn col)

- static void SkipTableName(TdsReader r)

- static int FixedWidth(int t)

- static TdsCell ReadValue(TdsReader r, TdsColumn col)

- static bool IsLarge(int t)

- static TdsCell ReadPlp(TdsReader r, int t)

- static TdsCell Text(TdsReader r, int n, int t)

- static string Hex(TdsReader r, int n)

- static TdsCell Scalar(TdsReader r, int t, int n, TdsColumn col)

- static string Float(TdsReader r, int n)

- static double Pow2(int e)

- static string Ieee(int sign, int exp, long mant, int bias, int mantBits)

- static string Money(TdsReader r, int n)

- static string Numeric(TdsReader r, int n, int scale)

- static string LimbsToDecimal(List<int> limbs)

- static string Digit(int v)

- static string Scaled(long units, int scale)

- static string LegacyDateTime(TdsReader r, int n)

- static string TimeOfDay(long ticks, int scale)

- static string Offset(int minutes)

- static string CivilDate(int z)

- static string Guid(TdsReader r, int n)

- static string HexAt(List<int> raw, int i)

- static string Pad2(int v)

- static string Pad3(int v)

- static string Pad4(int v)
