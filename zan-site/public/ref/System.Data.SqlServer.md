# System.Data.SqlServer

> 源码: `stdlib/System/Data/SqlServer/SqlServerConnection.zan`, `stdlib/System/Data/SqlServer/SqlServerPool.zan`, `stdlib/System/Data/SqlServer/TdsCodec.zan`, `stdlib/System/Data/SqlServer/TdsMessage.zan`, `stdlib/System/Data/SqlServer/TdsTypes.zan`


## SqlServerConnection (class)

原生、协程化的 SQL Server 客户端，线协议为 TDS 7.4——
无需 ODBC 驱动、SNI、原生 DLL。PRELOGIN、LOGIN7、SQL 批处理及
sp_executesql RPC 都由 `TdsMessage` 构建，每步网络
操作都在 IO reactor 上挂起（<c>Socket.ConnectAsync</c> /
<c>Socket.SendAsync</c> / <c>Socket.RecvOv</c>），因此查询从不阻塞
工作线程，多个连接可在单线程上并行推进。

登录包以明文发送：TDS 会对密码字段做加扰，但
那只是混淆而非加密，因此开启了 “force encryption” 的服务器
会拒绝登录（其 PRELOGIN 应答 ENCRYPT_REQ，表现为
连接错误而非静默降级）。

用法：
SqlServerConnection db = await SqlServerConnection.OpenAsync(
"127.0.0.1", 1433, "master", "sa", "secret");
DbResult r = await db.QueryAsync("SELECT id, name FROM t WHERE id > ?",
new DbParams().AddInt(3));
db.Close();

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

- SqlServerConnection()

- async int recvExact(byte[]dst, int off, int need)
  - 恰好读取 <paramref name="need"/> 字节，部分读取之间
    会挂起。返回实际读到的字节数。

- async bool sendMessage(int type, TdsBytes payload)
  - 发送一条消息，按协商的包大小拆分成
    所需数量的包；只有最后一个包带 EOM 位。

- async TdsBytes recvMessage()
  - 读取一整条响应消息：不断拼接数据包，直到
    服务器标记消息结束，因为令牌可能跨越
    两个数据包的边界。

- static async SqlServerConnection OpenAsync(string host, int port, string database, string user, string password)
  - 打开连接并使用 SQL Server 认证方式登录。

- async bool handshake(string host, int port, string db, string user, string pw)

- async TdsResponse roundTrip(int type, TdsBytes payload)

- async DbResult QueryAsync(string sql)
  - 运行语句批处理并返回结果集。当服务器拒绝批处理时抛出
    `DbException`。因此空
    结果总表示“无行”；`GetError` 仍会携带
    错误消息，供偏好自行检查的调用方查看。

- async DbResult QueryAsync(string sql, DbParams prms)
  - 通过 sp_executesql 运行参数化查询。占位符
    为 `?`，会被改写为服务器期望的 @P1.. 名称；值
    带外传输，因此无需向语句文本中做任何转义。

- async int ExecuteAsync(string sql)
  - 执行一条语句并返回受影响的行数。服务器拒绝时抛出
    `DbException`。

- async int ExecuteAsync(string sql, DbParams prms)
  - 带参数的语句。

- void fail()
  - 抛出已记录的错误，使服务器自身的消息
    能到达调用方，而不是返回空结果。

- async string ExecuteScalarAsync(string sql)
  - 第一行第一列的值，无行时返回 ""。

- static string Renumber(string sql, int count)
  - 将 `?` 占位符重写为 @P1、@P2 ……，
    字符串字面量、带引号的标识符或括号内的内容原样保留。数量不匹配时返回 ""
    。

- int AffectedRows()
  - 上一条语句影响的行数。

- string GetError()
  - 上一次失败的错误描述，无则为 ""。

- string ServerVersion()
  - LOGINACK 报告的服务器版本，如 "16.0.1000"。

- string Database()
  - 会话所绑定的数据库。

- bool IsConnected()

- int GetProvider()

- void Close()


## SqlServerPool (class)

适用于 `SqlServerConnection` 的协程感知连接池。

与 `DbPool` 契约相同——惰性增长至 <c>maxSize</c>，
用 `Gate` 驱动等待而非轮询，驱逐失效的
连接——但保留类型，因此协程查询 API（<c>QueryAsync</c>、
<c>ExecuteAsync</c>）仍然可用。一次 TDS 登录需要多轮往返，
因此应复用会话，而非每条语句都新建一个。

用法：
SqlServerPool pool = new SqlServerPool("127.0.0.1", 1433, "master", "sa", "secret", 8);
SqlServerConnection db = await pool.AcquireAsync();
DbResult r = await db.QueryAsync("SELECT TOP 10 * FROM sys.objects");
pool.Release(db);
pool.Close();

- string host;

- int port;

- string database;

- string user;

- string password;

- PoolCore<SqlServerConnection> core;

- SqlServerPool(string host, int port, string database, string user, string password, int maxSize)

- async SqlServerConnection OpenOne()

- async SqlServerConnection AcquireAsync()
  - 借出一条连接：复用空闲连接，未达到上限则新建，
    否则挂起协程直到有连接被释放。
    连接池关闭后返回 null。

- void Release(SqlServerConnection c)
  - 将连接归还连接池。已损坏或 Close() 之后的
    连接将被关闭并丢弃，而不会入池。

- int IdleCount()
  - 空闲（已入池、可直接使用）连接数。

- int LiveCount()
  - 活跃连接总数（空闲 + 已借出）。

- int WaitingCount()
  - 当前挂起等待连接的协程数。

- int MaxSize()
  - 并发活跃连接数上限。

- void Close()
  - 关闭所有空闲连接并将连接池标记为已关闭。
    仍被借出的连接在归还时关闭。


## TdsBuf (class)

用于构建 TDS 消息的字节缓冲区。字节先收集到列表，
最后一次性生成，编码器无需进行指针运算；
生成的 Zan 字符串是纯字节数组，保持二进制安全（包括
内嵌的 NUL 字节）。

- List<int> b;

- static byte[]Alloc(int n)
  - 分配 <paramref name="n"/> 字节的零填充块，
    供逐字节拼接文本的解码器使用。

- TdsBuf()

- int Count()

- int At(int i)

- void SetAt(int i, int v)

- TdsBuf U8(int v)

- TdsBuf U16LE(int v)

- TdsBuf U16BE(int v)

- TdsBuf U32LE(int v)

- TdsBuf U32BE(int v)

- TdsBuf U64LE(long v)
  - 八字节小端整数（行数、事务
    描述符、money）。

- TdsBuf Bytes(string s)
  - Zan 字符串的原始字节，直到其 NUL 终止符。

- TdsBuf Block(TdsBytes src)
  - 追加一段带长度的字节块。

- TdsBuf Ucs2(string s)
  - UCS-2LE 文本，TDS 中所有字符串使用的编码。

- TdsBuf Password(string s)
  - 密码字段经过混淆（半字节交换，再 XOR 0xA5），
    而非加密——这只是混淆，因此正式
    部署时应通过 TLS 进行登录。

- TdsBytes ToBytes()
  - 生成最终缓冲区。结果自带长度，
    因为 Zan 字符串以第一个 NUL 结尾，而 TDS 消息中满是
    零字节。


## TdsBytes (class)

带长度的字节块。Zan 字符串以 NUL 结尾，因此任何二进制数据
都必须与长度一起传递；此类将两者绑定并持有
内存分配。

- byte[]data;

- int len;

- TdsBytes(byte[]data, int len)

- static TdsBytes Own(byte[]data, int len)
  - 包装现有缓冲区及其长度。

- static TdsBytes Of(string s)
  - 将 Zan 字符串的字节复制为带长度的块。

- static TdsBytes Alloc(int n)

- byte[]Data()

- int Len()

- int At(int i)

- void SetAt(int i, int v)

- void Release()
  - 释放缓冲区，以便立即回收。


## TdsCell (class)

一个解码后的单元格：NULL 与空字符串不同，因此
仅凭文本无法表达这一区别。

- string text;

- bool isNull;

- TdsCell(string text, bool isNull)

- static TdsCell Of(string text)

- static TdsCell Null()

- string Text()

- bool IsNull()


## TdsColumn (class)

COLMETADATA 描述的结果集的一列。

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

TDS 7.4 客户端消息的构建器，以及返回的
token 流的解码器。不涉及 IO，
无需服务器即可测试线格式。

- static int TDS74=1946157060;

- static TdsBytes Prelogin(int encrypt)
  - PRELOGIN：一张 (token, offset, length) 条目表，后跟
    各条目载荷。<paramref name="encrypt"/> 是 ENCRYPTION 字节——0x02
    (NOT_SUP) 表示客户端不支持 TLS，配置了
    "force encryption" 的服务器会拒绝。

- static int PreloginEncryption(TdsBytes payload)
  - 服务器对 PRELOGIN 的应答，只取其中的 ENCRYPTION 字节
    （0 关闭、1 开启、2 不支持、3 必须）。

- static TdsBytes Login7(string host, string user, string password, string appName, string server, string database, int packetSize)
  - LOGIN7：固定 94 字节的 offset/length 对头部，后跟
    其指向的 UCS-2 字符串。

- static TdsBuf AllHeaders(TdsBuf b)
  - TDS 7.2 及以上要求在批次或 RPC 之前
    的 ALL_HEADERS 块：只需要事务描述符。

- static TdsBytes SqlBatch(string sql)
  - SQLBatch 载荷：头部加 UCS-2 编码的语句。

- static TdsBytes RpcExecuteSql(string sql, DbParams prms)
  - 调用 sp_executesql 的 RPC 载荷，参数正是借此
    带外传输：值从不拼进语句文本，因此无需转义，
    服务器也能复用执行计划。

- static string Declaration(DbParams prms)
  - sp_executesql 所需的 "@P1 bigint, @P2 float" 声明串。

- static void ParamHeader(TdsBuf b, string name)

- static void NVarcharParam(TdsBuf b, string name, string val)
  - NVARCHAR 参数。超过 4000 字符的文本无法放入
    定长格式，改为以 PLP 分块按 NVARCHAR(MAX) 发送。

- static void IntParam(TdsBuf b, string name, long val)

- static void FloatParam(TdsBuf b, string name, double val)

- static void NullParam(TdsBuf b, string name)

- static long DoubleBits(double v)
  - double 的 IEEE-754 位模式：对数值做归一化来构造，
    因为语言没有 reinterpret 转换。

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
  - 解码完整的响应消息。

- static List<TdsColumn> ReadColumns(TdsReader r, TdsResponse resp)

- static void ReadRow(TdsReader r, List<TdsColumn> cols, TdsResponse resp, bool nbc)
  - ROW，或 NBCROW——前导位图标记 NULL 列，
    这些列的值完全省略。

- static void ReadError(TdsReader r, TdsResponse resp)

- static void ReadLoginAck(TdsReader r, TdsResponse resp)

- static void ReadEnvChange(TdsReader r, TdsResponse resp)
  - ENVCHANGE 报告登录带来的环境变化；只有数据库名
    值得保留，其余只需跳过。

- static void SkipFeatureAck(TdsReader r)

- static void SkipReturnValue(TdsReader r)


## TdsPacket (class)

TDS 报文类型及报文头的状态位。

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
  - 将载荷包进八字节报文头。
    长度字段包含报文头本身且为大端序，与协议中其他
    整数不同。


## TdsReader (class)

接收到的字节缓冲区上的游标。

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
  - 1..8 字节的无符号小端整数。结果用
    <c>long</c> 表示：八字节字段（bigint、MONEY、PLP 长度）以及完整
    的无符号四字节范围都超出 <c>int</c>。

- long IntLE(int n)
  - 1..8 字节的有符号小端整数。

- void Skip(int n)

- string Narrow(int n)
  - 读取 <paramref name="n"/> 字节的单字节文本。其中的任何 NUL
    都会截断生成的 Zan 字符串，这与这类文本的用法一致。

- TdsBytes Raw(int n)
  - 读取 <paramref name="n"/> 个原始字节，作为带长度的块返回。

- string Ucs2(int chars)
  - 读取 <paramref name="chars"/> 个代码单元的 UCS-2LE 文本。
    高于 U+00FF 的单元渲染为 '?'，解码器无需
    引入完整转码器即可保持完备；其余单元保留原字节值。

- string BVarchar()
  - B_VARCHAR：一个长度字节，后跟相应数量的 UCS-2 单元。

- string UsVarchar()
  - US_VARCHAR：两字节长度，后跟相应数量的 UCS-2 单元。


## TdsResponse (class)

解码一条 TDS 响应消息的结果。

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

TDS 数据类型 ID（TYPE_INFO），按其长度在
线上的传输方式分组。

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
  - 该类型的文本是否为 UCS-2 而非单字节。

- static bool IsBinary(int t)
  - 该类型的值是否为原始字节（以十六进制显示）。


## TdsValue (class)

TYPE_INFO 描述符及其后的行值的解码。
所有值都转为文本：SQL Server 发送十几种二进制
编码，而 `DbResult` 基于文本，因此转换
必须在此完成，并对所有调用方共享。

- static string HEX="0123456789abcdef";

- static void ReadTypeInfo(TdsReader r, TdsColumn col)
  - 读取 TYPE_INFO 描述符到 <paramref name="col"/>。

- static void SkipTableName(TdsReader r)

- static int FixedWidth(int t)

- static TdsCell ReadValue(TdsReader r, TdsColumn col)
  - 读取 <paramref name="col"/> 对应的一行值。

- static bool IsLarge(int t)

- static TdsCell ReadPlp(TdsReader r, int t)
  - PLP：八字节总长度（或 unknown/NULL 标记），
    随后是分块，直到零长度块。分块先合并为一个
    带长度的块，因为一个值可能横跨多个分块。

- static TdsCell Text(TdsReader r, int n, int t)
  - 按列类型家族渲染接下来的 <paramref name="n"/> 个字节：
    二进制用十六进制，national 类型用 UCS-2，其余
    原样输出。

- static string Hex(TdsReader r, int n)

- static TdsCell Scalar(TdsReader r, int t, int n, TdsColumn col)
  - 数值、时间及 GUID 值，其含义取决于
    实际发送的字节数（INTN 4 是 int，INTN 8 是 bigint）。

- static string Float(TdsReader r, int n)

- static double Pow2(int e)
  - 以 double 表示的 2^e（e 可为负）。

- static string Ieee(int sign, int exp, long mant, int bias, int mantBits)

- static string Money(TdsReader r, int n)
  - MONEY 是缩放整数：每单位四万分之一，
    八字节形式先发送高半字。

- static string Numeric(TdsReader r, int n, int scale)
  - DECIMAL/NUMERIC：一个符号字节，后跟小端
    幅值，最多十六字节，因此通过反复
    对 32 位 limb 做除法来生成各位数字，而非直接整数运算。

- static string LimbsToDecimal(List<int> limbs)

- static string Digit(int v)

- static string Scaled(long units, int scale)
  - 渲染带 <paramref name="scale"/> 位隐式
    小数的整数。

- static string LegacyDateTime(TdsReader r, int n)
  - DATETIME（8 字节：1900 年以来的天数加 1/300 秒）和
    SMALLDATETIME（4 字节：天数加整分钟）。

- static string TimeOfDay(long ticks, int scale)
  - TIME/DATETIME2 保存 10^-scale 秒的计数。scale 为 7 时，
    一天的量是 8.64e11，因此计数用 <c>long</c>。

- static string Offset(int minutes)

- static string CivilDate(int z)
  - 相对于 1970-01-01 的天数对应的公历日期。

- static string Guid(TdsReader r, int n)
  - 规范 GUID 文本：前三个组在线上是小端，
    后两个是大端。

- static string HexAt(List<int> raw, int i)

- static string Pad2(int v)

- static string Pad3(int v)

- static string Pad4(int v)
