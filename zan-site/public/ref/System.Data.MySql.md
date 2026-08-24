# System.Data.MySql

> 源码: `stdlib/System/Data/MySql/MySqlConnection.zan`, `stdlib/System/Data/MySql/MySqlConnector.zan`, `stdlib/System/Data/MySql/MySqlPool.zan`, `stdlib/System/Data/MySql/MySqlSyncConnection.zan`


## MySqlAsyncConnector (class)

返回协程化
`MySqlConnection` 的 `IDbConnector`，使 MySQL 连接的 `DbPool`
端到端非阻塞：等待空闲连接时
挂起（连接池），等待服务器响应同样挂起（驱动）。而
使用阻塞驱动时只有前者成立，这会把数据库并发
限制在 worker 数量上，无论连接池多大。

这些连接只实现
`IDbExecutor` 的异步部分；同步部分会抛错并指明要用的 *Async
终端。因此请将此连接池与异步 ORM 链搭配使用：

DbPool pool = MySqlAsyncConnector.Pool("127.0.0.1", 3306, "app", "root", "", 8);
IDbConnection db = await pool.AcquireAsync();
try { List<Post> rows = await db.Select<Post>().ToListAsync(); }
finally { pool.Release(db); }

- string host;

- int port;

- string database;

- string user;

- string password;

- MySqlAsyncConnector(string host, int port, string database, string user, string password)

- static DbPool Pool(string host, int port, string database, string user, string password, int maxSize)
  - 基于 host/port/database/user/password 的非阻塞连接池。

- async IDbConnection ConnectAsync()

- int GetProvider()


## MySqlConnection (class)

原生、协程化的 MySQL / MariaDB 客户端，直接用 Zan 的异步套接字
讲 MySQL 线协议 —— 不依赖 libmariadb、ODBC
或任何原生数据库 DLL。每个网络步骤（连接、握手、认证、查询、
结果）都通过 <c>Socket.RecvOv</c> /
<c>Socket.SendAsync</c> 在 IO reactor 上挂起，因此查询不会阻塞工作线程，
多个连接可在同一线程上并行推进。

认证实现了 <c>caching_sha2_password</c> 快速认证（MySQL
8.x 默认方式），用纯 Zan 实现的 `Sha256` 计算：
token = SHA256(pw) XOR SHA256( SHA256(SHA256(pw)) . nonce )
成功登录后服务器会预热凭据缓存，因此
之后的每次连接都能走快速认证。缓存为空时，
服务器要求完整认证：客户端获取服务器的 RSA
公钥，将 NUL 结尾的密码与握手 nonce 异或，再
以 RSA-OAEP(SHA-1) 加密发送 —— 全部用纯 Zan 实现。

数据包按原始字节缓冲区构建与解析（Zan 字符串用作字节
数组），对含内嵌 NUL 字节的二进制数据安全。

用法：
MySqlConnection db = await MySqlConnection.OpenParamsAsync(
"127.0.0.1", 3306, "test", "root", "secret");
int n = await db.ExecuteAsync("INSERT INTO t VALUES (1,'a')");
DbResult r = await db.QueryAsync("SELECT id, name FROM t");
db.Close();

- nint sock;

- bool connected;

- string lastError;

- int lastNumber;

- int lastLen;

- int lastSeq;

- int lastAffected;

- bool hadError;

- List<PreparedStmt> stmtCache;

- MySqlConnection()

- async int recvExact(byte[]dst, int off, int need)
  - 恰好读取 <paramref name="need"/> 个字节到
    <paramref name="dst"/> 的 <paramref name="off"/> 处，在 IO
    reactor 上分段读取间挂起。返回实际读到的字节数（对端关闭时 < need，
    不足）。

- async byte[]readPacket()
  - 读取一个协议数据包，返回其负载，并将长度写入
    <c>lastLen</c>（负载是二进制，从不当作
    NUL 结尾的字符串读取）。同时设置 <c>lastSeq</c>。连接关闭时返回 null。

- async int writePacket(string payload, int len, int seq)
  - 以给定序列号写入一个协议数据包（4 字节头 + 负载）
    ，二进制安全（按长度发送）。

- static string bytesToStr(string buf, int off, int len)
  - 将缓冲区中 <paramref name="len"/> 个字节复制到新的
    Zan 字符串。

- static long readLenenc(string buf, List<int> cur)
  - 在游标 <c>cur[0]</c> 处解码长度编码整数，
    并推进游标。NULL 标记（0xFB）返回 -1；八字节形式
    携带 64 位计数，因此返回 <c>long</c>。

- static string readLenencStr(string buf, List<int> cur)
  - 在游标 <c>cur[0]</c> 处解码长度编码字符串，
    并推进游标。NULL（0xFB）返回 ""。

- bool isEof(byte[]pkt)

- static byte[]scramble41(string pw, string nonce20)
  - caching_sha2_password 快速认证加扰：
    SHA256(pw) XOR SHA256( SHA256(SHA256(pw)) . nonce )，返回 32 字节
    缓冲区。

- static byte[]scrambleNative(string pw, string nonce20)
  - mysql_native_password 加扰：
    SHA1(pw) XOR SHA1( nonce . SHA1(SHA1(pw)) )，返回 20 字节
    缓冲区。

- static byte[]authToken(string plugin, string pw, string nonce20, List<int> outLen)

- static byte[]genSeed(int n)

- static int findSub(string h, int hlen, string ndl, int nlen, int from)
  - 从 <paramref name="from"/> 起在 haystack 中查找 needle；
    返回其索引或 -1。

- static byte[]pemToDer(string pem, int pemLen, List<int> outLen)
  - 从 PEM 公钥中提取 DER SubjectPublicKeyInfo。
    outLen[0] 接收 DER 长度；找不到 PEM 主体则返回 null。

- static int readDerLen(string der, List<int> c)
  - 在游标 c[0] 处读取 DER 长度，并将 c[0] 推进过它。

- static byte[]fullAuthCipher(string der, int derLen, string obf, int obfLen, string seed, List<int> kOut)
  - 解析 RSA SubjectPublicKeyInfo，并用 RSA OAEP-SHA1
    （MySQL 完整认证填充）加密 <paramref name="obf"/>。kOut[0] 接收模数
    字节长度（即密文长度）。

- static async MySqlConnection OpenParamsAsync(string host, int port, string database, string user, string password)
  - 使用原生线协议打开协程化的 MySQL/MariaDB 连接
    。

- async bool doConnect(string host, int port, string db, string user, string pw)

- async int comQuery(string sql)

- async DbResult QueryAsync(string sql)
  - 执行查询并返回完整结果集。非 SELECT
    语句的结果为空，受影响行数会被记录。当服务器返回错误包或
    会话已断开时抛出 `DbException`，
    因此空结果总意味着「无行」；
    错误信息仍可通过 `GetError` 获取。

- async int ExecuteAsync(string sql)
  - 执行非查询语句并返回受影响行数。语句失败时
    抛出 `DbException`。

- void fail()
  - 抛出记录的失败，携带服务器自身的错误
    号，处理方无需解析错误消息。

- async string ExecuteScalarAsync(string sql)
  - 第一行第一列以字符串返回（无行时返回 ""
    ）。

- static int writeLenenc(string buf, int pos, int v)
  - 在 pos 处写入长度编码整数；返回新的 pos。

- static double pow2(int e)
  - 以 double 返回 2^e（e 可为负）。

- static string ieeeToText(int sign, int exp, long mant, int expBias, int mantBits)
  - 从尾数/指数/符号字段解码 IEEE-754 值。

- static string pad2(int v)

- static string readBinValue(string buf, List<int> cur, int type, int flags)
  - 在游标 cur[0] 处读取给定列类型的一个二进制协议值，
    读取后推进游标，返回其文本形式。

- async bool stmtPrepare(string sql, List<int> outIds)
  - 预编译 sql；成功后填写 out[0]=stmtId、out[1]=numCols、
    out[2]=numParams 并返回 true。跳过 prepare 响应中
    的参数/列定义包。

- async int stmtExecute(int stmtId, DbParams prms)
  - 发送带类型化二进制参数的 COM_STMT_EXECUTE：
    整数用 LONGLONG，文本/浮点用 VAR_STRING（长度编码，
    服务器将文本转换到列类型），NULL 通过 null bitmap 标记。

- async int stmtClose(int stmtId)
  - 关闭服务端预编译语句（发完即忘）。

- int stmtCacheFind(string sql)
  - sql 在语句缓存中的索引，找不到则为 -1。

- async int stmtCacheAdd(string sql, int stmtId)
  - 缓存预编译语句 id，缓存满时
    关闭最旧的条目（每连接 32 条）。

- void stmtCacheRemove(int stmtId)
  - 从缓存中移除一条语句（服务器出错后调用）。

- async DbResult QueryParamsAsync(string sql, DbParams prms)
  - 通过二进制预编译语句协议执行参数化查询
    （COM_STMT_PREPARE + COM_STMT_EXECUTE）。参数值带外传输，
    绝不拼入 SQL 文本。预编译语句按连接缓存，
    重复查询可省去 prepare 往返。

- async int ExecuteParamsAsync(string sql, DbParams prms)
  - 执行参数化非查询语句；返回受影响行数。
    语句失败时抛出 `DbException`，
    失败的写入绝不会被误判为「匹配 0 行」。

- async string ExecuteScalarParamsAsync(string sql, DbParams prms)
  - 参数化标量查询（第一行第一列）。

- async DbResult QueryAsync(string sql, DbParams prms)
  - 参数化协程查询的接口形式，
    让生成的 ORM 终端在所有驱动上调用同一方法名。

- async int ExecuteAsync(string sql, DbParams prms)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- async bool BeginTransactionAsync()
  - MySQL 的事务控制，与其他语句一样在 reactor 上执行
    。

- async bool CommitAsync()

- async bool RollbackAsync()

- int GetProvider()

- static DbException Sync()
  - 该契约的同步部分在此无法兑现：此驱动的每一步
    都是挂在 IO reactor 上的协程，
    非协程代码要等它完成，只能阻塞 reactor 所在的
    工作线程 —— 那会造成死锁，而不只是
    停顿。因此这里直接抛错而非假装可用，错误信息指明了解决办法：
    使用 *Async 终端（ToListAsync / ExecuteAffrowsAsync 等），
    或在调用点无法改为协程时，用连接池容纳阻塞驱动（MySqlConnector）
    。

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
  - 此连接上记录的最后一条错误信息。

- bool IsConnected()
  - 连接是否已打开。

- void Close()
  - 关闭连接。


## MySqlConnector (class)

面向 MySQL / MariaDB 的 `IDbConnector`，让 MySQL 与 SQLite、
PostgreSQL、ODBC 一起接入与驱动无关的 `DbPool`，
应用可用一种类型池化所有引擎。

它返回的连接是同步线协议客户端
（`DbConnection.OpenMySQL`）：语句执行时阻塞工作线程
等待服务器响应，因此连接池提供容量控制与复用，而非
非阻塞 IO。它面向无法改成协程的调用点。
优先使用 `MySqlAsyncConnector`，其连接会在
reactor 上挂起，除非你需要同步 ORM 终端。

用法：
DbPool pool = MySqlConnector.Pool("127.0.0.1", 3306, "app", "root", "", 8);
IDbConnection db = await pool.AcquireAsync();
try { List<Post> rows = db.Select<Post>().ToList(); }
finally { pool.Release(db); }

- string host;

- int port;

- string database;

- string user;

- string password;

- MySqlConnector(string host, int port, string database, string user, string password)

- static DbPool Pool(string host, int port, string database, string user, string password, int maxSize)
  - 基于 host/port/database/user/password 的连接池。

- async IDbConnection ConnectAsync()

- int GetProvider()


## MySqlPool (class)

面向 `MySqlConnection` 的协程感知连接池。

连接按需懒打开，最多 <c>maxSize</c> 条，并在协程间
复用。当所有连接都被借出且达到上限时，
`AcquireAsync` 会让调用协程挂起在事件驱动的
`Gate` 上（无轮询、无定时器），而非阻塞
工作线程；一旦有连接通过 `Release`
归还，它便立即恢复。由于打开连接
与执行查询都已协程化于 IO reactor 上，
整个 获取/查询/释放 周期保持非阻塞。

连接池依赖协作式「每调度器单线程」模型：
空闲列表只在 await 之间被访问，因此无需加锁。

用法：
MySqlPool pool = new MySqlPool("127.0.0.1", 3306, "test", "root", "secret", 8);
MySqlConnection db = await pool.AcquireAsync();
DbResult r = await db.QueryAsync("SELECT 1");
pool.Release(db);
pool.Close();

- string host;

- int port;

- string database;

- string user;

- string password;

- PoolCore<MySqlConnection> core;

- MySqlPool(string host, int port, string database, string user, string password, int maxSize)

- async MySqlConnection OpenOne()

- async MySqlConnection AcquireAsync()
  - 借出一个连接：复用空闲的，未达上限则新建，
    否则挂起协程直到有连接被释放。
    连接池关闭后返回 null。

- void Release(MySqlConnection c)
  - 将连接归还连接池。损坏的或 Close() 之后的
    连接会被关闭丢弃，不再入池。

- int IdleCount()
  - 空闲（已入池、可直接使用）连接数。

- int LiveCount()
  - 活跃连接总数（空闲 + 已借出）。

- void Close()
  - 关闭所有空闲连接并将连接池标记为已关闭。
    仍被借出的连接在归还时关闭。


## MySqlSyncConnection (class)

同步的原生 MySQL / MariaDB 客户端，直接实现 MySQL wire
协议（阻塞套接字），无需 ODBC 驱动、libmariadb 或原生
数据库 DLL。`DbConnection.OpenMySQL` 默认使用此驱动，
因此 ORM（Model / QueryBuilder / Migration）在只装有
MySQL server（而无 ODBC 驱动）的机器上即可开箱即用，
无需任何额外原生驱动。

认证实现了 <c>caching_sha2_password</c> 快速认证（MySQL
8.x 的默认方式），并提供完整认证（使用服务器公钥的 RSA-OAEP）以应对
凭据缓存冷启动的情况，以及 <c>mysql_native_password</c> 认证切换——全部
用纯 Zan 实现（System.Security.Cryptography 中的 Sha256/Sha1/Rsa）。

参数化语句使用 COM_STMT_PREPARE / COM_STMT_EXECUTE，值
通过二进制协议带外传输，因此通过值进行注入
是不可能的。

用法（推荐使用标准工厂）：
IDbConnection db = DbConnection.OpenMySQL("127.0.0.1", 3306,
"app", "root", "secret");
db.Execute("CREATE TABLE t (id BIGINT PRIMARY KEY, name VARCHAR(64))");
DbResult r = db.Query("SELECT * FROM t");
db.Close();

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
  - 将 <paramref name="need"/> 字节读入
    <paramref name="dst"/> 的 <paramref name="off"/> 偏移处。返回实际
    读到的字节数（对端关闭时 < need）。

- byte[]readPacket()
  - 读取一个协议数据包，返回其有效载荷，
    长度记录在 <c>lastLen</c>；连接关闭时返回 null。

- int writePacket(string payload, int len, int seq)
  - 写入一个协议数据包（4 字节包头 + 有效载荷）。

- static string bytesToStr(string buf, int off, int len)
  - 将缓冲区中的 <paramref name="len"/> 字节复制到新的
    Zan 字符串。

- static long readLenenc(string buf, List<int> cur)
  - 在游标 <c>cur[0]</c> 处解码长度编码整数，
    并推进游标；NULL 标记（0xFB）返回 -1。

- static string readLenencStr(string buf, List<int> cur)
  - 在游标 <c>cur[0]</c> 处解码长度编码字符串。
    NULL（0xFB）返回空字符串。

- bool isEof(byte[]pkt)

- static byte[]scramble41(string pw, string nonce20)
  - caching_sha2_password 快速认证混淆：
    SHA256(pw) XOR SHA256( SHA256(SHA256(pw)) . nonce )，32 字节。

- static byte[]scrambleNative(string pw, string nonce20)
  - mysql_native_password 混淆：
    SHA1(pw) XOR SHA1( nonce . SHA1(SHA1(pw)) )，20 字节。

- static byte[]authToken(string plugin, string pw, string nonce20, List<int> outLen)

- static byte[]genSeed(int n)

- static int findSub(string h, int hlen, string ndl, int nlen, int from)

- static byte[]pemToDer(string pem, int pemLen, List<int> outLen)

- static int readDerLen(string der, List<int> c)

- static byte[]fullAuthCipher(string der, int derLen, string obf, int obfLen, string seed, List<int> kOut)

- static MySqlSyncConnection OpenParams(string host, int port, string database, string user, string password)
  - 使用 wire 协议在阻塞套接字上打开原生 MySQL/MariaDB 连接。
    失败时返回 null；`GetError` 携带
    失败原因。

- bool doConnect(string host, int port, string db, string user, string pw)

- int comQuery(string sql)

- DbResult querySync(string sql)

- static int writeLenenc(string buf, int pos, int v)

- static double pow2(int e)

- static string ieeeToText(int sign, int exp, long mant, int expBias, int mantBits)

- static string pad2(int v)

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
  - 此连接上记录的最后一条错误消息。

- async DbResult QueryAsync(string sql)
  - 在当前会话上开启事务。

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
  - 提交当前事务。

- void Rollback()
  - 回滚当前事务。

- bool IsConnected()

- void Close()


## PreparedStmt (class)

一条缓存的预编译语句：其 SQL 文本及对应的
服务端语句 id。用一个实体，而非并行的 sql/id 列表。

- string sql;

- int id;

- PreparedStmt(string sql, int id)
