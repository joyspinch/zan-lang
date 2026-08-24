# System.Data.TDengine

> 源码: `stdlib/System/Data/TDengine/TDengineConnection.zan`, `stdlib/System/Data/TDengine/TDengineConnector.zan`, `stdlib/System/Data/TDengine/TDenginePool.zan`


## TDengineConnection (class)

原生、无依赖的 TDengine 连接，通过 REST API 通信，
taosAdapter 在 6041 端口提供服务（`POST /rest/sql`）。

与 ODBC 方式不同，目标机器无需安装任何东西：
整个协议就是 HTTP + JSON，任何能访问到 adapter 的机器都能用，
包括没有 taosc 客户端库的交叉编译目标。

TDengine 的 REST 没有客户端语句协议，因此 `?`
占位符在这里被渲染进 SQL 文本——文本值会被加引号
并转义，数字按格式输出，NULL 输出为关键字。
渲染器只替换字符串字面量之外的占位符，
因此 SQL 中的字面量问号不会被处理。

主机必须是点分 IPv4 地址（socket 层不解析主机名）。
TDengine 没有事务，因此这里没有 Begin/Commit
等操作可用。

用法：
TDengineConnection db = TDengineConnection.Open("127.0.0.1", 6041,
"root", "taosdata", "demo");
db.Execute("CREATE TABLE IF NOT EXISTS t (ts TIMESTAMP, v INT)");
db.Execute("INSERT INTO t VALUES (NOW, ?)", new DbParams().AddInt(7));
DbResult r = db.Query("SELECT * FROM t WHERE v > ?",
new DbParams().AddInt(3));
db.Close();

- string host;

- int port;

- string auth;

- string database;

- bool connected;

- string lastError;

- int affected;

- TDengineConnection()

- static TDengineConnection Open(string host, int port, string user, string password)
  - 使用 HTTP Basic 凭据打开连接，不
    选择数据库。

- static TDengineConnection Open(string host, int port, string user, string password, string database)
  - 使用 HTTP Basic 凭据打开连接，并指定默认
    数据库（语句随后可直接使用不带库名的表名）。
    连接时会先与服务器通信一次以校验凭据，可检查 IsConnected()。

- static TDengineConnection OpenToken(string host, int port, string token, string database)
  - 使用 taosAdapter 登录 token 打开连接
    （`Authorization: Taosd <token>`），由
    `GET /rest/login/<user>/<password>` 返回。

- static async TDengineConnection OpenAsync(string host, int port, string user, string password, string database)
  - 使用 HTTP Basic 凭据打开连接且不阻塞 worker：
    校验请求在 IO reactor 上执行。

- static async TDengineConnection OpenTokenAsync(string host, int port, string token, string database)
  - OpenAsync 的 token 版本。

- bool Ping()
  - 执行一条简单语句，检查 adapter 是否响应且
    凭据是否被接受。

- async bool PingAsync()
  - 在 reactor 上 Ping，挂起而非阻塞。

- string ServerVersion()
  - 服务器版本字符串，查询失败时返回 ""。

- void Use(string database)
  - 选择后续语句使用的默认数据库。

- static string EscapeText(string val)
  - 对 TDengine 单引号字符串字面量中的值进行转义：
    解析器只特殊处理反斜杠和引号
    这两个字符。

- static string Literal(DbParams prms, int i)
  - 将单个参数渲染为 SQL 字面量。

- static string RenderSql(string sql, DbParams prms)
  - 将 `?` 占位符替换为转义后的参数字面量。
    只有字符串字面量之外的占位符才有效，所以 `WHERE note = 'a?b'`
    中的问号会保留；字面量内的反斜杠会转义下一个字符，
    与服务器解析器行为一致。
    
    当 SQL 与参数列表的占位符数量不一致时返回 ""，
    避免不匹配时发送半绑定的语句。

- static string Dechunk(string body)
  - 解除 `Transfer-Encoding: chunked` 分块。taosAdapter 对
    事先不知道大小的结果集使用它，
    而响应解析器交出的 body 仍带分块。

- static int HexOf(string text)
  - 解析 chunk-size 行（十六进制，可带 ";extension"，以 CR 结尾）。

- static bool IsChunked(HttpResponse resp)
  - 响应声明使用分块传输时返回 true。

- string SqlPath()
  - 当前默认数据库的 REST 端点。

- string Request(string sql)
  - 携带单条语句的 HTTP/1.1 请求。

- string BodyOf(string raw)
  - 从原始 HTTP 交换中提取响应 body，
    解除分块，并记录非 200 状态码。

- string Unreachable()

- string Post(string sql)
  - 发送一条语句并返回响应 body，请求无法完成时
    返回 "" 并设置 lastError。

- async string PostAsync(string sql)
  - 在 IO reactor 上执行 Post：连接、发送、接收都挂起
    协程而不是阻塞 worker 线程，因此这样的连接组成池后
    可以同时有多条语句在途。

- bool Failed(JsonValue root)
  - 响应报告服务端错误时返回 true；
    错误描述保存在 lastError 中。3.x 的（`code` != 0）
    和 2.x 的（`status` == "error"）都会被识别。

- DbResult Decode(string body)
  - 将一个 `/rest/sql` 响应文档转换为结果集，
    列名取自 `column_meta`（3.x）或 `head`（2.x），
    并区分 JSON null 与空字符串。

- DbResult Query(string sql)
  - 执行查询并返回结果集。

- DbResult Query(string sql, DbParams prms)
  - 执行查询，其中 `?` 占位符从
    <paramref name="prms"/> 填充。

- int Execute(string sql)
  - 执行语句并返回受影响的行数，失败返回 -1
    。

- int Execute(string sql, DbParams prms)
  - 执行带参数的语句。

- string ExecuteScalar(string sql)
  - 第一行第一列的值，否则返回 ""。

- string ExecuteScalar(string sql, DbParams prms)
  - 参数化查询第一行第一列的值。

- async DbResult QueryAsync(string sql)
  - 查询，不阻塞 worker 线程。

- async DbResult QueryAsync(string sql, DbParams prms)
  - 参数化查询，不阻塞 worker 线程。

- async int ExecuteAsync(string sql)
  - 执行语句，不阻塞 worker 线程。

- async int ExecuteAsync(string sql, DbParams prms)
  - 执行参数化语句，不阻塞 worker 线程。

- async string ExecuteScalarAsync(string sql)
  - 第一行第一列的值，不阻塞。

- async string ExecuteScalarAsync(string sql, DbParams prms)
  - 参数化语句第一行第一列的值。

- async bool BeginTransactionAsync()
  - 空操作：TDengine 没有事务。参见 BeginTransaction。

- async bool CommitAsync()

- async bool RollbackAsync()

- int AffectedRows()
  - 上一条语句影响的行数。

- string GetError()
  - 最近一次失败的描述，否则返回 ""。

- void BeginTransaction()
  - 连接打开时 adapter 是否正常响应。
    空操作：TDengine 没有事务。仅用于满足
    `IDbConnection` 接口；任何需要原子性的操作都不应
    在 TDengine 上执行。

- void Commit()
  - 空操作：TDengine 没有事务（写入在
    Execute 返回时即已持久化）。

- void Rollback()
  - 空操作：TDengine 没有事务，因此无法撤销任何操作。

- bool IsConnected()

- void Close()
  - 释放连接状态。REST 协议是无状态的，因此
    不会向服务器发送任何内容。

- int GetProvider()
  - 返回 provider id（DbProvider.TDengine）。


## TDengineConnector (class)

TDengine REST 驱动，见 `IDbConnector`。这里的"连接"
就是端点加 Authorization 请求头——taosAdapter 无状态——
因此池化限制了同时在途的语句数，并把
准备好的凭据交给每个协程。

用法：
DbPool pool = TDengineConnector.Pool("127.0.0.1", 6041, "root", "taosdata", "power", 8);
IDbConnection db = await pool.AcquireAsync();
DbResult r = db.Query("SELECT * FROM meters LIMIT 10");
pool.Release(db);

当需要协程查询 API 时请改用 `TDenginePool`
（QueryAsync/ExecuteAsync）：它会返回驱动类型。

- string host;

- int port;

- string user;

- string password;

- string token;

- string database;

- TDengineConnector(string host, int port, string user, string password, string token, string database)

- static TDengineConnector CreateToken(string host, int port, string token, string database)
  - 使用预先签发的 taosAdapter token 的连接器。

- static DbPool Pool(string host, int port, string user, string password, string database, int maxSize)
  - 使用用户名/密码认证的连接池。

- static DbPool PoolToken(string host, int port, string token, string database, int maxSize)
  - 使用 token 认证的连接池。

- async IDbConnection ConnectAsync()

- int GetProvider()


## TDenginePool (class)

为 `TDengineConnection` 设计的协程感知连接池。

契约与 `DbPool` 相同——惰性增长到 <c>maxSize</c>，
等待由 `Gate` 驱动而非轮询，逐出失效连接——
但它是类型化的，协程查询 API（<c>QueryAsync</c>、
<c>ExecuteAsync</c>）仍可用。打开连接和每条语句都运行在
IO reactor 上，因此限制并发的是池的大小，
而不是 worker 线程数。

用法：
TDenginePool pool = new TDenginePool("127.0.0.1", 6041, "root", "taosdata", "", "power", 8);
TDengineConnection db = await pool.AcquireAsync();
DbResult r = await db.QueryAsync("SELECT * FROM meters LIMIT 10");
pool.Release(db);
pool.Close();

- string host;

- int port;

- string user;

- string password;

- string token;

- string database;

- PoolCore<TDengineConnection> core;

- TDenginePool(string host, int port, string user, string password, string token, string database, int maxSize)

- TDenginePool(string host, int port, string token, string database, int maxSize):this(host, port, "", "", token, database, maxSize)

- async TDengineConnection OpenOne()

- async TDengineConnection AcquireAsync()
  - 借出一个连接：复用空闲连接，未达上限则新建，
    否则挂起协程，直到有连接被归还。
    池已关闭后返回 null。

- void Release(TDengineConnection c)
  - 将连接归还给池。已损坏或 Close() 之后的
    连接会被直接关闭丢弃，不再入池。

- int IdleCount()
  - 空闲（已入池、可用）连接数。

- int LiveCount()
  - 存活连接总数（空闲 + 已借出）。

- int WaitingCount()
  - 当前挂起等待连接的协程数。

- int MaxSize()
  - 并发存活连接数的上限。

- void Close()
  - 关闭所有空闲连接并将池标记为已关闭。
    仍借出的连接会在归还时被关闭。
