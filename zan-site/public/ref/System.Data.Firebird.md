# System.Data.Firebird

> 源码: `stdlib/System/Data/Firebird/FbSql.zan`, `stdlib/System/Data/Firebird/FbSrp.zan`, `stdlib/System/Data/Firebird/FbWire.zan`, `stdlib/System/Data/Firebird/FirebirdConnection.zan`, `stdlib/System/Data/Firebird/FirebirdPool.zan`


## FbArc4 (class)

ARC4 密钥流，即每个 Firebird 3+ 服务器自带的线路加密插件。
按现代标准它很弱，但这是无 TLS 隧道时协议
能提供的选择；注重安全的部署应把连接放进 TLS 隧道。
发送与接收方向各需独立实例，因为
密钥流是有状态的。

- List<int> s;

- int x;

- int y;

- FbArc4(FbBytes key)

- void Translate(string buf, int off, int len)
  - 就地异或缓冲区中的 <paramref name="len"/> 字节；
    加密与解密是同一操作。


## FbBlobRef (class)

载荷仍需另行取回的 BLOB 列。

- FbRow row;

- int col;

- FbBytes id;

- int subtype;

- FbBlobRef(FbRow row, int col, FbBytes id, int subtype)

- static FbBlobRef Of(FbRow row, int col, FbBytes id, int subtype)


## FbBuf (class)

协议类 XDR 帧格式的构建器：每个整数占 4 字节
大端，每个变长块为长度后跟数据，并
补齐到 4 字节边界。

- List<int> b;

- static byte[]Alloc(int n)

- FbBuf()

- int Count()

- int At(int i)

- FbBuf U8(int v)

- FbBuf Int(int v)
  - 4 字节大端整数。

- FbBuf Raw(string s)
  - Zan 字符串的原始字节，至 NUL 终止符为止。

- FbBuf RawBlock(FbBytes src)
  - 带长度块的原始字节。

- void pad()

- FbBuf Block(FbBytes src)
  - 带长度前缀、4 字节对齐的块。

- FbBuf Str(string s)
  - 带长度前缀、4 字节对齐的文本。

- FbBytes ToBytes()
  - 将缓冲区物化为带长度的块。


## FbBytes (class)

带长度的字节块。Zan 字符串在首个 NUL 处截断，而线缆
协议中充满零字节（每个整数都是 4 字节大端），因此
二进制数据必须携带显式长度。

- byte[]data;

- int len;

- FbBytes(byte[]data, int len)

- static FbBytes Own(byte[]data, int len)
  - 将现有缓冲区连同其长度封装起来。

- static FbBytes CopyOf(string src, int len)
  - 从调用方仍持有的缓冲区中复制 <paramref name="len"/> 字节
    （例如托管 <c>byte[]</c> 摘要）。

- static FbBytes Of(string s)
  - 将 Zan 字符串的字节复制到带长度的块中。

- static FbBytes Alloc(int n)

- static FbBytes Empty()

- byte[]Data()

- int Len()

- int At(int i)

- void SetAt(int i, int v)

- string Text()
  - 字节作为文本，止于首个 NUL。

- FbBytes Slice(int off, int n)
  - 从 <paramref name="off"/> 起复制
    <paramref name="n"/> 字节。

- string Hex()
  - 整块的小写十六进制。

- static FbBytes FromHex(string hex)
  - 将十六进制字符串解码为带长度的块。

- static int nibble(int c)

- void Release()
  - 丢弃缓冲区以便立即回收。


## FbColumn (class)

预编译语句输出行中的一列。

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
  - 已知形状的列，用于在缺少 describe 缓冲区时
    解码某个值。

- int SqlType()

- int SubType()

- int Scale()

- int Length()

- bool NullOk()

- string Field()

- string Relation()

- string Name()
  - 结果集应展示的列名：有 SELECT 别名时
    用别名，否则用底层字段名。

- int IoLength()
  - 该列在取回的行中占用的字节数，
    带长度前缀（VARCHAR）时为 -1。


## FbError (class)

解码后的状态向量：gds 错误码、SQL 码和可读
消息（由服务器的参数列表拼装）。

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
  - 向量包含给定 gds 码时为 true。

- static string Text(int code)
  - 常见 gds 码的文本及 <c>@n</c> 占位符。
    服务器发送的是数字而非文本，而携带整张消息表
    （约两千条）并不划算；未列出的
    以其数字码报告，便于查询。


## FbOp (class)

Firebird 线缆协议的操作码（服务器 <c>remote/protocol.h</c> 中的
<c>op_</c> 常量），以及本驱动
所讲的协议版本。

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
  - 连接握手版本（CONNECT_VERSION3）。

- static int ARCH_GENERIC=1;
  - 通用架构（arch_generic）。

- static int PROTOCOL_V13=4294934541;
  - 线缆上传输的 PROTOCOL_VERSION13：版本字
    带有 FB_PROTOCOL_FLAG (0xFFFF8000) 位标记。版本 13
    是首个支持 SRP 认证、线路加密和
    基于位图的 NULL 编码的版本，也是本驱动唯一支持的版本。

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

按 <c>op_execute</c> 所需方式编码的输入参数：
一条描述取值的 BLR 消息，以及取值本身。

- FbBytes blr;

- FbBytes values;

- FbParamBlock(FbBytes blr, FbBytes values)

- static FbParamBlock Of(FbBytes blr, FbBytes values)

- FbBytes Blr()

- FbBytes Values()

- void Release()


## FbReader (class)

接收块的游标。

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
  - 2 字节小端整数：info 缓冲区和 clumplets
    在响应内为小端序，尽管帧格式本身不是。

- int UIntLE(int n)
  - <paramref name="n"/> 字节的无符号小端整数。

- int IntLE(int n)
  - <paramref name="n"/> 字节的有符号小端整数。

- int Int()
  - 4 字节大端整数。

- int IntBE(int n)
  - <paramref name="n"/> 字节的有符号大端整数。

- long LongBE(int n)
  - <paramref name="n"/> 字节的有符号大端整数，以
    64 位 long 返回（INT64/BIGINT 和 IEEE double 放不进 32 位 int）。

- FbBytes Raw(int n)
  - <paramref name="n"/> 个原始字节。

- string Text(int n)
  - <paramref name="n"/> 字节的文本。


## FbResponse (class)

一个 <c>op_response</c>：对象句柄、其 id、info 缓冲区
以及随附的状态向量。

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

构建中的行：单元格文本加 SQL NULL 标志。

- List<string> values;

- List<bool> nulls;

- FbRow(int n)

- void Set(int i, string v)

- List<string> Values()

- List<bool> Nulls()


## FbSrp (class)

Firebird 3+ 用于认证的 SRP-6a 握手（插件 <c>Srp</c> 与
<c>Srp256</c> 仅最终证明所用的哈希不同）。

client -> server:  A = g^a
server -> client:  salt, B = k*v + g^b
both:              u = H(A, B), x = H(salt, H(user ":" password))
client:            S = (B - k*g^x)^(a + u*x),  K = SHA1(S)
client -> server:  M = H(H(N)^H(g) mod N, H(user), salt, A, B, K)

所有整数均以其最短的大端形式参与哈希，这与
服务器做法一致；只有该细节一致时双方证明才能匹配。
密码不会离开本进程：只有 A 和 M 会被发送。

- static string PRIME_HEX="e67d2e994b2f900c3f41f08f5bb2627ed0d49ee1fe767a52efcd565cd6e768812c3e1e9ce8f0a8bea6cb13cd29ddebf7a96d4a93b55d488df099a15c89dcb0640738eb2cbdd9a8f7bab561ab1b0dc1c6cdabf303264a08d1bca932d1f1ee428b619d970f342aba9a65793b8b2f041ae5364350c16f735f56ecbca87bd57b29e7";
  - Firebird SRP 插件使用的 1024 位素数。

- static string MULT_HEX="dfc212b4bd69674855cfceb30002b5c306ac60b5";
  - SRP-6a 乘数 k = H(N, g)，为插件常量。

- static int PRIVATE_BYTES=32;
  - 每次握手抽取的秘密指数的字节数。

- static int WIDTH=128;
  - 涉及的最宽整数（素数占 128 字节）。

- BigInt priv;

- FbBytes pub;

- FbSrp(BigInt priv, FbBytes pub)

- static FbSrp Create()
  - 以新的秘密指数开始握手。当
    操作系统随机源不可用时返回 null——可猜测的指数会
    把会话拱手让人，因此不做回退。

- static FbSrp FromPrivateHex(string hex)
  - 使用调用方指定指数的握手。仅测试应
    使用它：可使握手可复现。

- static FbSrp FromPrivate(BigInt a)

- static BigInt Prime()

- static BigInt Multiplier()

- static BigInt Generator()

- FbBytes Public()
  - 客户端公钥 A。

- string PublicHex()
  - 连接包携带的 A 的十六进制文本。

- static FbBytes Minimal(BigInt v)
  - 整数的最短大端编码：协议
    对每个整数都这样哈希和传输。

- static BigInt ToInt(FbBytes b)

- static FbBytes Sha1Of(FbBytes b)

- static FbBytes Sha256Of(FbBytes b)

- static string NormalizeUser(string user)
  - 参与证明计算的登录名：带引号的名称保留
    原大小写（双引号折叠），不带引号的
    则按服务器存储方式转大写。

- static BigInt UserHash(string user, string password, FbBytes salt)
  - x = SHA1(salt, SHA1(user ":" password))。

- static BigInt Scramble(FbBytes a, FbBytes b)
  - u = SHA1(A, B)，这个混淆参数将证明
    绑定到这组公钥对上。

- FbSrpProof Prove(string user, string password, FbBytes salt, FbBytes serverPub, bool sha256)
  - 完成握手：由服务器的 salt 和公钥
    计算会话密钥及向服务器证明密码匹配的证明；
    服务器密钥退化（B mod N == 0）时返回 null，
    SRP 规定这种情况应中止。


## FbSrpProof (class)

SRP 握手的客户端证明及双方协商出的会话密钥
（该密钥同时用作线路加密密钥）。

- FbBytes proof;

- FbBytes key;

- FbSrpProof(FbBytes proof, FbBytes key)

- static FbSrpProof Of(FbBytes proof, FbBytes key)

- FbBytes Proof()

- FbBytes Key()


## FbType (class)

Firebird SQL 类型码（表示“可空”的奇数变体
在解析元数据时并入对应的偶数基础类型）。

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

语句取值的编码器与解码器。

- static string HEX="0123456789abcdef";

- static long DoubleBits(double v)
  - double 的 IEEE-754 位模式：语言没有
    重新解释转换（reinterpret cast），故需手工规范化。

- static double Pow2(int e)
  - 以 double 表示的 2^e（e 可为负）。

- static string Ieee(int sign, int exp, long mant, int bias, int mantBits)

- static string Pad2(int v)

- static string Pad4(int v)

- static string CivilDate(int z)
  - 由相对于 1970-01-01 的天数得到公历日期。

- static string Date(int nday)
  - Firebird 以自 1858-11-17 起的天数存储日期（修正的
    儒略历纪元）；其中有 40587 天在 1970-01-01 之前。

- static string Time(int n)
  - 一天内的时间，单位为万分之一秒。

- static string Scaled(long units, int scale)
  - 渲染带有 <paramref name="scale"/> 个隐含小数的整数
    （Firebird 元数据中 scale 为负数）。

- static string WideDecimal(FbBytes raw, int scale)
  - 任意宽度的二进制补码大端整数的十进制文本
    （INT128 放不进机器字）。

- static string Hex(FbBytes raw)

- static string Decode(FbColumn col, FbBytes raw)
  - 把取回的一个值渲染为文本。BLOB 列不在此解码——
    其载荷需要另外几轮往返，连接层
    会在组装行之前先取回它们。

- static FbParamBlock Params(DbParams prms)
  - 把参数列表编码成 BLR 消息加上 <c>op_execute</c> 携带的取值。
    协议 13 在取值前放置 NULL 位图，所以 NULL 参数
    只贡献自己的一个位。

- static void appendBE(FbBuf b, long v, int n)


## FbXsqlda (class)

预编译语句输出行的布局；
该布局从服务器的 describe 缓冲区解码而来。

- List<FbColumn> cols;

- int stmtType;

- FbXsqlda()

- int Count()

- FbColumn At(int i)

- int StmtType()

- void SetStmtType(int t)

- bool IsSelect()
  - 当语句需要逐行取回结果时为 true。

- void Resize(int n)

- int Parse(FbBytes buf)
  - 将 describe 缓冲区读入本布局。返回服务器截断处的 1 起始索引，
    以便调用方用 <c>isc_info_sql_sqlda_start</c> 请求剩余部分；
    描述完整时返回 -1，
    缓冲区根本不是 describe 应答时返回 -2。

- int ParseItems(FbReader r)
  - 读取逐列条目；服务器缓冲区耗尽时返回续读索引，
    列表读完时返回 -1。

- FbBytes CalcBlr()
  - 服务器返回取回行时使用的行格式。


## FirebirdConnection (class)

原生 Firebird 客户端，使用服务器自身的线缆协议（版本 13，
即 Firebird 3 及以上）——无需 fbclient，无需 ODBC。它以 SRP 登录，
可选开启服务器的 ARC4 线路加密，并直接驱动
语句生命周期（allocate / prepare / execute / fetch / free）。

每个网络步骤都在 IO 反应器上挂起（<c>Socket.ConnectAsync</c>、
<c>Socket.SendAsync</c>、<c>Socket.RecvOv</c>），语句不会阻塞
工作线程，多连接可在单线程上推进。这正是
该类只暴露协程 API 而非阻塞式
`IDbExecutor` 接口的原因：连接池见 `FirebirdPool`。

用法：
FirebirdConnection db = await FirebirdConnection.OpenAsync(
"127.0.0.1", 3050, "/var/lib/firebird/data/test.fdb", "SYSDBA", "secret");
DbResult r = await db.QueryAsync("SELECT id, name FROM t1 WHERE id > ?",
new DbParams().AddInt(0));
await db.CloseAsync();

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

- static int FETCH_ROWS=400;
  - 每次 fetch 往返请求服务器发送的行数。

- static int INFO_BUFFER=32768;
  - 描述语句时请求的 info 缓冲区大小。

- static int MAX_BLOCK=33554432;
  - 可接受的单个协议块上限，用于防范
    损坏或恶意的长度前缀。

- FirebirdConnection()

- async int recvExact(byte[]dst, int off, int need)
  - 精确读取 <paramref name="need"/> 字节，会话加密时
    对其解密。返回实际读取的字节数。

- async bool sendPacket(FbBytes pkt)
  - 发送整个数据包并释放它。字节在
    加密前复制，确保密钥流不会改写调用方的缓冲区。

- async int recvInt()

- async int recvSigned()
  - 读取一个协议视为有符号的 32 位值。

- async FbBytes recvAligned(int n)
  - <paramref name="n"/> 字节加上将其补至
    4 字节边界的填充。

- async FbBytes recvBlock()
  - 带长度前缀、4 字节对齐的块。

- async FbError recvStatus()
  - 读取状态向量并将其转为可读错误。

- static void fill(List<string> segs, int argNum, string text)
  - 将第 n 个参数代入正在等待它的
    消息段。

- async FbResponse recvResponse()
  - 读取一个 <c>op_response</c>，跳过服务器可能
    穿插发送的 keep-alive 包。

- async FbResponse roundTrip(FbBytes pkt)
  - 发送一个数据包并读取对应的响应，记录
    任何服务器错误。

- static async FirebirdConnection OpenAsync(string host, int port, string database, string user, string password)
  - 打开连接，以 SRP 认证并附加到
    数据库。服务器支持时启用线路加密。

- static async FirebirdConnection OpenAsync(string host, int port, string database, string user, string password, bool wireCrypt)
  - 可拒绝线路加密的打开变体（适用于
    经由已加密隧道访问的服务器，或配置了
    <c>WireCrypt = Disabled</c> 的服务器）。

- async bool handshake(string host, int port, string db, string user, string pw)

- static FbBytes ConnectPacket(string database, string user, string pubHex, bool wireCrypt)
  - 构建 <c>op_connect</c>：客户端提供协议 13 及
    SRP 公钥，服务器以 salt 和自身公钥应答。

- static void cnct(FbBuf b, int tag, string v)

- static void cnctChunked(FbBuf b, int tag, string v)
  - specific-data clumplet 带有序号字节，按
    254 字节分片，SRP 公钥总是需要这样发送。

- async bool authenticate(FbSrp srp, string user, string password)
  - 执行 SRP 续接流程，并在服务器要求时
    开启线路加密。

- async FbResponse recvResponse4()
  - 读取一个操作码已从线缆上取走的响应的
    正文。

- static FbBytes ContAuth(FbBytes authData, string plugin, string pluginList)

- static string GuessWireCrypt(FbBytes buf)
  - 从服务器提供的列表中选择线路加密插件。
    缓冲区是 clumplet 列表：标签 1 保存以空格分隔的插件名。

- async bool startCrypt(FbBytes sessionKey)

- static FbBytes AttachPacket(string database, string user, FbBytes authData)
  - 构建带数据库参数块的 <c>op_attach</c>。

- static void dpbStr(FbBuf b, int tag, string v)

- async bool beginTransaction()

- async bool commitRetaining()
  - 提交但不放弃事务句柄：在显式事务之外执行的语句
    都以此方式提交，这正是
    连接像自动提交一样工作的原因。

- async bool CommitAsync()
  - 提交当前事务并开启新事务。

- async bool RollbackAsync()
  - 回滚当前事务并开启新事务。

- async int allocStatement()

- static FbBytes describeItems()
  - prepare 请求的 describe 条目：先是语句类型，
    再是每个输出列的布局。

- async FbXsqlda prepare(int stmt, string sql)

- async bool execute(int stmt, DbParams prms)

- async bool freeStatement(int stmt)

- async int affectedRows(int stmt, bool select)
  - 读取上一条语句产生的行计数器。

- async FbBytes fetchValue(FbColumn col)
  - 从线缆读取一个值，遵循每字段
    补齐到的 4 字节对齐。

- async DbResult fetchAll(int stmt, FbXsqlda xs)
  - 拉取打开游标的每一行。BLOB 列先收集
    稍后再解析，因为其载荷需要单独的往返，
    且不能与取数流交错。

- async string readBlob(FbBytes blobId, int subtype)
  - 完整读取 BLOB。子类型 1 为文本，按文本返回；
    其余为二进制，以十六进制渲染，而非在
    首个零字节处截断。

- async DbResult QueryAsync(string sql)
  - 运行语句并返回其行（不产生行的语句
    返回空）。语句失败时抛出 `DbException`，
    因此空结果总是表示“无行”而绝非“未执行”；
    `GetError` 和 `SqlCode` 仍携带
    上次失败信息，供调用方自行检查。

- async DbResult QueryAsync(string sql, DbParams prms)
  - 运行带参数的语句。占位符是协议
    自身的位置占位符，因此值采用绑定而非拼入
    SQL 文本。

- async int ExecuteAsync(string sql)
  - 运行语句并返回其影响的行数。语句
    失败时抛出 `DbException`。

- async int ExecuteAsync(string sql, DbParams prms)
  - ExecuteAsync 的带参数版本。

- void fail()
  - 抛出记录的失败，让调用方看到服务器的
    原始措辞和 SQL 码，而不是空结果。

- async string ExecuteScalarAsync(string sql)
  - 第一行第一列的值，无行时返回 ""。

- async string ExecuteScalarAsync(string sql, DbParams prms)
  - ExecuteScalarAsync 的带参数版本。

- async bool PingAsync()
  - 用于验证会话仍可用的往返。

- int AffectedRows()
  - 上一条语句影响的行数。

- string GetError()
  - 上次失败的描述，没有则为 ""。

- int SqlCode()
  - 上次失败的 Firebird SQL 码（无失败时为 0）。

- string AuthPlugin()
  - 服务器选择的认证插件。

- string WireCrypt()
  - 当前线路加密方式，会话未加密时为 ""。

- int ProtocolVersion()
  - 服务器接受的协议版本。

- string Database()
  - 本会话附加到的数据库。

- bool IsConnected()

- int GetProvider()

- async void CloseAsync()
  - 礼貌地分离（回滚打开的事务），然后
    关闭套接字。

- void Close()
  - 不经分离往返直接丢弃连接。


## FirebirdPool (class)

面向协程的 `FirebirdConnection` 连接池。

与 `DbPool` 契约一致——惰性增长至 <c>maxSize</c>，
由 `Gate` 驱动等待而非轮询，淘汰失效
连接——但类型化，因此协程查询 API（<c>QueryAsync</c>、
<c>ExecuteAsync</c>）仍可访问。一次 attach 需要一次 SRP 握手和
多轮往返，这就是要复用会话而非
每条语句开一个新会话的原因。

用法：
FirebirdPool pool = new FirebirdPool("127.0.0.1", 3050,
"/var/lib/firebird/data/test.fdb", "SYSDBA", "secret", 8);
FirebirdConnection db = await pool.AcquireAsync();
DbResult r = await db.QueryAsync("SELECT id, name FROM t1");
pool.Release(db);
pool.Close();

- string host;

- int port;

- string database;

- string user;

- string password;

- bool wireCrypt;

- PoolCore<FirebirdConnection> core;

- FirebirdPool(string host, int port, string database, string user, string password, bool wireCrypt, int maxSize)

- FirebirdPool(string host, int port, string database, string user, string password, int maxSize):this(host, port, database, user, password, true, maxSize)

- async FirebirdConnection OpenOne()

- async FirebirdConnection AcquireAsync()
  - 借出一个连接：复用空闲的，未达上限则新建，
    否则挂起协程，直到有连接被释放。
    连接池关闭后返回 null。

- void Release(FirebirdConnection c)
  - 将连接归还连接池。已损坏或 Close() 之后
    的连接会直接关闭丢弃，而非放回池中。

- int IdleCount()
  - 空闲（已入池、可立即使用）连接数。

- int LiveCount()
  - 存活连接总数（空闲 + 已借出）。

- int WaitingCount()
  - 当前挂起等待连接的协程数。

- int MaxSize()
  - 最大并发存活连接数。

- bool IsClosed()
  - `Close` 调用过后为 true。

- int GetProvider()

- void Close()
  - 关闭所有空闲连接并将连接池标记为已关闭。仍被
    借出的连接在归还时才会被关闭。
