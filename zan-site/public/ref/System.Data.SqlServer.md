# System.Data.SqlServer

> 源码: `stdlib/System/Data/SqlServer/SqlServerConnection.zan`, `stdlib/System/Data/SqlServer/SqlServerPool.zan`, `stdlib/System/Data/SqlServer/TdsCodec.zan`, `stdlib/System/Data/SqlServer/TdsMessage.zan`, `stdlib/System/Data/SqlServer/TdsTypes.zan`


## SqlServerConnection (class)

原生、协程化的 SQL Server 客户端，线协议为 TDS 7.4——
无需 ODBC 驱动、SNI、原生 DLL。PRELOGIN、LOGIN7、SQL 批处理及
sp_executesql RPC 都由 `TdsMessage` 构建，每步网络
操作都在 IO reactor 上挂起（<c>Socket.ConnectAsync</c> /
<c>Socket.SendAsync</c> / <c>Socket.RecvOv</c>），因此查询从不阻塞
工作线程，多个连接可在单线程上并行推进。

明文路径（OpenAsync）的登录包以明文发送：TDS 会对密码字段做
加扰，但那只是混淆而非加密，因此开启了 "force encryption" 的
服务器会拒绝登录（其 PRELOGIN 应答 ENCRYPT_REQ，表现为
连接错误而非静默降级）。跨网使用请走
`OpenSecureAsync`（A287②：整条流含口令走 TLS）。

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

- TlsStream tls;

- bool busy;

- List<AsyncGate> waiters;

- async bool AcquireLock()

- void ReleaseLock()

- static bool verifyTls=true;

- static void SetTlsVerify(bool verify)
  - 切换后续 OpenSecureAsync 的证书与主机名校验。
    仅测试/开发自签场景关闭；生产保持默认开启。

- SqlServerConnection()
  - 私有构造；统一经 `OpenAsync` 创建。

- async int recvExact(byte[]dst, int off, int need)
  - 恰好读取 <paramref name="need"/> 字节，部分读取之间
    会挂起。返回实际读到的字节数。TLS 建立后从解密流读取
    （TDS 报文层整体运行在隧道之内）。

- async int recvExactRaw(byte[]dst, int off, int need)
  - 握手阶段专用的原话路收发（0x12 封装的 TLS 密文），
    绝不经 recvExact——那里此时还走不了隧道。

- async bool sendMessage(int type, TdsBytes payload)
  - 发送一条消息，按协商的包大小拆分成
    所需数量的包；只有最后一个包带 EOM 位。

- async TdsBytes recvMessage()
  - 读取一整条响应消息：不断拼接数据包，直到
    服务器标记消息结束，因为令牌可能跨越
    两个数据包的边界。

- static async SqlServerConnection OpenAsync(string host, int port, string database, string user, string password)
  - 打开连接并使用 SQL Server 认证方式登录。

- static async SqlServerConnection OpenSecureAsync(string host, int port, string database, string user, string password)
  - 与 `OpenAsync` 相同，但 PRELOGIN 协商
    ENCRYPT_ON，握手响应起整条 TDS 流（含 LOGIN7 与口令）走 TLS。
    MS-TDS 的特例：握手期间的 TLS 记录封装在 PRELOGIN(0x12) 报文
    里，握手完成后裸 TLS 记录直达套接字、TDS 报文层整体进入隧道。
    服务器拒绝加密（应答 NOT_SUP）时显式失败——绝不静默降级
    明文（A287②）。

- async bool handshake(string host, int port, string db, string user, string pw, bool secure)
  - PRELOGIN + LOGIN7 握手。明文路径声明 ENCRYPT_NOT_SUP，
    服务器要求 TLS（ENCRYPT_REQ）时在这里失败；安全路径声明
    ENCRYPT_ON 并在 PRELOGIN 应答后把通道升级为 TLS。登录应答
    带出服务器版本与实际绑定的数据库。

- async bool tlsUpgrade(string host)
  - 把通道升级为 TLS：握手期间的 TLS 记录按 MS-TDS 规则
    封装在 PRELOGIN(0x12) 报文里收发；握手完成后调用方照常经
    sendMessage/recvMessage 走隧道（裸 TLS 记录，TDS 报文层在
    解密侧）。失败时已记录 lastError。

- async TdsResponse roundTrip(int type, TdsBytes payload)
  - 发送一条消息并解码响应：网络失败会把连接标记为断开并返回
    空响应，语句级失败只记录（由调用方决定是否抛出）。内部通过 AsyncGate 门控保证单连接事务互斥，杜绝并发调用数据串号。

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
  - 登录成功且连接未被网络错误关闭。

- int GetProvider()
  - 恒为 `DbProvider.SqlServer`。

- void Close()
  - 关闭套接字；重复调用安全。TLS 先发 close_notify（fd 仍归
    调用方）再关套接字。安全唤醒所有门控等待者。


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
  - 新建一条 TDS 连接（池的建连回调）。

- async SqlServerConnection AcquireAsync()
  - 借出一条连接：复用空闲连接，未达到上限则新建，
    否则挂起协程直到有连接被释放。饱和等待有硬上限
    （见 `PoolWait`）：超时返回 null，绝不定死。
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
  - 私有构造；缓冲区经 `Alloc` 风格的写入方法累积。

- int Count()
  - 当前字节数。

- int At(int i)
  - 下标 i 处的字节。

- internal void SetAt(int i, int v)
  - 覆写下标 i 处的字节（回填长度/状态位用）。

- TdsBuf U8(int v)
  - 追加单字节，返回自身以链式写入。

- TdsBuf U16LE(int v)
  - 追加两字节小端整数。

- TdsBuf U16BE(int v)
  - 追加两字节大端整数（BLOB 块头等大端字段用）。

- TdsBuf U32LE(int v)
  - 追加四字节小端整数。

- TdsBuf U32BE(int v)
  - 追加四字节大端整数。

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
  - 私有构造；统一经 `Own`/`Of`/`Alloc` 创建。

- static TdsBytes Own(byte[]data, int len)
  - 包装现有缓冲区及其长度。

- static TdsBytes Of(string s)
  - 将 Zan 字符串的字节复制为带长度的块。

- static TdsBytes Alloc(int n)
  - 分配 n 字节的零填充块。

- byte[]Data()
  - 底层缓冲区（自带 NUL 终止，内容按长度解读）。

- int Len()
  - 有效字节数。

- int At(int i)
  - 下标 i 处的字节。

- void SetAt(int i, int v)
  - 覆写下标 i 处的字节。

- void Release()
  - 释放缓冲区，以便立即回收。


## TdsCell (class)

一个解码后的单元格：NULL 与空字符串不同，因此
仅凭文本无法表达这一区别。

- string text;

- bool isNull;

- TdsCell(string text, bool isNull)
  - 私有构造；经 `Of`/`Null` 创建。

- static TdsCell Of(string text)
  - 非空单元格。

- static TdsCell Null()
  - NULL 单元格（文本为空串但 `IsNull` 为 true）。

- string Text()
  - 单元格文本（NULL 时为空串）。

- bool IsNull()
  - 是否为 SQL NULL。


## TdsColumn (class)

COLMETADATA 描述的结果集的一列。

- string name;

- int type;

- int size;
  - 声明长度；0xFFFF 表示 MAX (PLP) 列。

- int precision;

- int scale;

- TdsColumn()
  - 空列描述，经 `TdsValue.ReadTypeInfo` 填充。

- string Name()
  - 列名。

- int Type()
  - TDS 类型 ID。

- int Size()
  - 声明长度；0xFFFF 表示 MAX (PLP) 列。

- int Scale()
  - 小数位数（时间/数值类型）。

- int Precision()
  - 精度（DECIMAL/NUMERIC）。


## TdsMessage (class)

TDS 7.4 客户端消息的构建器，以及返回的
token 流的解码器。不涉及 IO，
无需服务器即可测试线格式。

- static int TDS74=1946157060;
  - 登录包头中的 TDS 版本常数 0x74000004（7.4）。

- static TdsBytes Prelogin(int encrypt)
  - PRELOGIN：一张 (token, offset, length) 条目表，后跟
    各条目载荷。<paramref name="encrypt"/> 是 ENCRYPTION 字节——0x02
    (NOT_SUP) 表示客户端不支持 TLS，配置了
    "force encryption" 的服务器会拒绝。

- static int PreloginEncryption(TdsBytes payload)
  - 服务器对 PRELOGIN 的应答，只取其中的 ENCRYPTION 字节
    （0 关闭、1 开启、2 不支持、3 必须）。

- internal static TdsBytes Login7(string host, string user, string password, string appName, string server, string database, int packetSize)
  - LOGIN7：固定 94 字节的 offset/length 对头部，后跟
    其指向的 UCS-2 字符串。

- static TdsBuf AllHeaders(TdsBuf b)
  - TDS 7.2 及以上要求在批次或 RPC 之前
    的 ALL_HEADERS 块：只需要事务描述符。

- internal static TdsBytes SqlBatch(string sql)
  - SQLBatch 载荷：头部加 UCS-2 编码的语句。

- internal static TdsBytes RpcExecuteSql(string sql, DbParams prms)
  - 调用 sp_executesql 的 RPC 载荷，参数正是借此
    带外传输：值从不拼进语句文本，因此无需转义，
    服务器也能复用执行计划。

- static string Declaration(DbParams prms)
  - sp_executesql 所需的 "@P1 bigint, @P2 float" 声明串。

- static void ParamHeader(TdsBuf b, string name)
  - RPC 参数公共头：参数名 + 输入参数状态字节。

- static void NVarcharParam(TdsBuf b, string name, string val)
  - NVARCHAR 参数。超过 4000 字符的文本无法放入
    定长格式，改为以 PLP 分块按 NVARCHAR(MAX) 发送。

- static void IntParam(TdsBuf b, string name, long val)
  - bigint 参数（INTN，八字节）。

- static void FloatParam(TdsBuf b, string name, double val)
  - float 参数（FLTN，八字节，按 IEEE-754 位型发送）。

- static void NullParam(TdsBuf b, string name)
  - NULL 参数（NVARCHAR 类型、长度 0xFFFF 表示 NULL）。

- static long DoubleBits(double v)
  - double 的 IEEE-754 位模式：对数值做归一化来构造，
    因为语言没有 reinterpret 转换。非有限值单独处理：±∞ 与 NaN
    会让下面的归一化循环永不终止（∞/2 还是 ∞）——一次
    AddDouble(Infinity) 就把协程永久钉死。

- static int TOK_RETURNSTATUS=121;
  - token 0x79：RPC 返回状态。

- static int TOK_COLMETADATA=129;
  - token 0x81：结果集列元数据。

- static int TOK_TABNAME=164;
  - token 0xA4：表名。

- static int TOK_COLINFO=165;
  - token 0xA5：列信息。

- static int TOK_ORDER=169;
  - token 0xA9：ORDER BY 列序号。

- static int TOK_ERROR=170;
  - token 0xAA：服务器错误。

- static int TOK_INFO=171;
  - token 0xAB：服务器提示消息。

- static int TOK_RETURNVALUE=172;
  - token 0xAC：输出参数值。

- static int TOK_LOGINACK=173;
  - token 0xAD：登录确认。

- static int TOK_FEATUREACK=174;
  - token 0xAE：服务器功能协商应答。

- static int TOK_ROW=209;
  - token 0xD1：普通行。

- static int TOK_NBCROW=210;
  - token 0xD2：NBC 行（位图标记 NULL 列）。

- static int TOK_SSPI=237;
  - token 0xED：SSPI 安全通道数据。

- static int TOK_ENVCHANGE=227;
  - token 0xE3：环境变化（数据库、语言、包大小…）。

- static int TOK_DONE=253;
  - token 0xFD：整个请求的完成标记。

- static int TOK_DONEPROC=254;
  - token 0xFE：存储过程级完成标记。

- static int TOK_DONEINPROC=255;
  - token 0xFF：批内语句级完成标记。

- static TdsResponse Decode(TdsBytes buf)
  - 解码完整的响应消息。

- static List<TdsColumn> ReadColumns(TdsReader r, TdsResponse resp)
  - COLMETADATA：读出列类型与列名，并把结果集的列结构填进 resp。

- static void ReadRow(TdsReader r, List<TdsColumn> cols, TdsResponse resp, bool nbc)
  - ROW，或 NBCROW——前导位图标记 NULL 列，
    这些列的值完全省略。

- static void ReadError(TdsReader r, TdsResponse resp)
  - ERROR：记录第一条错误的消息文本与错误号（后续错误忽略）。

- static void ReadLoginAck(TdsReader r, TdsResponse resp)
  - LOGINACK：取服务器版本号，拼成 "major.minor.build"。

- static void ReadEnvChange(TdsReader r, TdsResponse resp)
  - ENVCHANGE 报告登录带来的环境变化；只有数据库名
    值得保留，其余只需跳过。

- static void SkipFeatureAck(TdsReader r)
  - 跳过 FEATUREACK 的 (id, len, data) 条目表，直至 0xFF 终止符。

- static void SkipReturnValue(TdsReader r)
  - 跳过 RETURNVALUE（输出参数）的完整结构。


## TdsPacket (class)

TDS 报文类型及报文头的状态位。

- static int SQLBATCH=1;
  - 报文类型 0x01：SQL 语句批处理。

- static int RPC=3;
  - 报文类型 0x03：RPC 调用（sp_executesql）。

- static int REPLY=4;
  - 报文类型 0x04：服务器响应。

- static int ATTENTION=6;
  - 报文类型 0x06：Attention（取消当前请求）。

- static int LOGIN7=16;
  - 报文类型 0x10：登录请求。

- static int PRELOGIN=18;
  - 报文类型 0x12：登录前协商。

- static int STATUS_NORMAL=0;
  - 状态位 0x00：消息未结束（后续还有包）。

- static int STATUS_EOM=1;
  - 状态位 0x01：EOM，本消息的最后一个包。

- static int HEADER_LEN=8;
  - 报文头长度（字节）。

- static int DEFAULT_SIZE=4096;
  - 登录时协商的默认包大小。

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
  - 私有构造；统一经 `Over` 创建。

- static TdsReader Over(string buf)
  - 游标覆盖整段缓冲区。

- static TdsReader Over(string buf, int len)
  - 游标只覆盖缓冲区前 len 字节（消息体短于缓冲区时用）。

- static TdsReader Over(TdsBytes b)
  - 游标覆盖一个带长度的字节块。

- int Pos()
  - 当前读取位置。

- int Left()
  - 距末尾剩余的字节数。

- bool Eof()
  - 是否已读到末尾。

- void Seek(int p)
  - 跳到绝对位置 p。

- int U8()
  - 读取单字节；越过末尾返回 0（各 U* 同此约定）。

- int U16LE()
  - 读取两字节小端整数。

- int U16BE()
  - 读取两字节大端整数。

- int U32LE()
  - 读取四字节小端整数。

- int U32BE()
  - 读取四字节大端整数。

- long UIntLE(int n)
  - 1..8 字节的无符号小端整数。结果用
    <c>long</c> 表示：八字节字段（bigint、MONEY、PLP 长度）以及完整
    的无符号四字节范围都超出 <c>int</c>。

- long IntLE(int n)
  - 1..8 字节的有符号小端整数。

- void Skip(int n)
  - 向前跳过 n 字节（截停在末尾，不报错）。

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
  - 私有构造；经 `TdsMessage.Decode` 填充。

- DbResult Rows()
  - 结果集（无结果集时为空结果）。

- int Affected()
  - 受影响行数（有结果集时等于行数）。

- string Error()
  - 第一条 ERROR token 的消息文本；无错为 ""。

- int ErrorNumber()
  - 服务器自身的错误号；无错为 0。

- string ServerVersion()
  - LOGINACK 报告的服务器版本。

- string Database()
  - ENVCHANGE 报告的会话数据库。

- bool Failed()
  - 服务器是否拒绝了请求（`Error` 非空）。


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
  - 十六进制渲染用的字符表。

- static void ReadTypeInfo(TdsReader r, TdsColumn col)
  - 读取 TYPE_INFO 描述符到 <paramref name="col"/>。

- static void SkipTableName(TdsReader r)
  - 跳过 TEXT/IMAGE 类型描述符尾部的表名（多段 UsVarchar）。

- static int FixedWidth(int t)
  - 定长类型的线上宽度（字节）；变长类型返回 0。

- static TdsCell ReadValue(TdsReader r, TdsColumn col)
  - 读取 <paramref name="col"/> 对应的一行值。

- static bool IsLarge(int t)
  - 是否为「大值类型」（两字节长度或 PLP 编码）。

- static TdsCell ReadPlp(TdsReader r, int t)
  - PLP：八字节总长度（或 unknown/NULL 标记），
    随后是分块，直到零长度块。分块先合并为一个
    带长度的块，因为一个值可能横跨多个分块。
    防御：U8() 读穿缓冲尾会返回 0 而非报错，因此损坏/恶意
    的流此前可以无限追加零字节把客户端喂到 OOM。这里按
    包剩余字节校验每个分块声明（分块不会横跨包边界），
    并给单值设 64 MiB 硬上限；违规一律按 NULL 收场。

- static TdsCell Text(TdsReader r, int n, int t)
  - 按列类型家族渲染接下来的 <paramref name="n"/> 个字节：
    二进制用十六进制，national 类型用 UCS-2，其余
    原样输出。

- static string Hex(TdsReader r, int n)
  - 把 n 字节渲染为 "0x…" 小写十六进制。

- static TdsCell Scalar(TdsReader r, int t, int n, TdsColumn col)
  - 数值、时间及 GUID 值，其含义取决于
    实际发送的字节数（INTN 4 是 int，INTN 8 是 bigint）。

- static string Float(TdsReader r, int n)
  - 读取 n(4 或 8) 字节小端位型并按 IEEE 754 渲染浮点文本。

- static double Pow2(int e)
  - 以 double 表示的 2^e（e 可为负）。

- static string Ieee(int sign, int exp, long mant, int bias, int mantBits)
  - 由 IEEE 754 位型（符号/指数/尾数/偏置/尾数位数）渲染浮点文本。

- static string Money(TdsReader r, int n)
  - MONEY 是缩放整数：每单位四万分之一，
    八字节形式先发送高半字。

- static string Numeric(TdsReader r, int n, int scale)
  - DECIMAL/NUMERIC：一个符号字节，后跟小端
    幅值，最多十六字节，因此通过反复
    对 32 位 limb 做除法来生成各位数字，而非直接整数运算。

- static string LimbsToDecimal(List<int> limbs)
  - 小端 32 位 limb 序列转十进制数字串（去前导零）。

- static string Digit(int v)
  - 把 0-15 的数值渲染为单个十六进制字符。

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
  - 渲染 UTC 偏移："+HH:MM" / "-HH:MM"。

- static string CivilDate(int z)
  - 相对于 1970-01-01 的天数对应的公历日期。

- static string Guid(TdsReader r, int n)
  - 规范 GUID 文本：前三个组在线上是小端，
    后两个是大端。

- static string HexAt(List<int> raw, int i)
  - 取 raw[i] 的两个十六进制字符。

- static string Pad2(int v)
  - 两位零填充十进制。

- static string Pad3(int v)
  - 三位零填充十进制。

- static string Pad4(int v)
  - 四位零填充十进制。
