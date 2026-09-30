# System.Data.Redis

> 源码: `stdlib/System/Data/Redis/RedisClient.zan`, `stdlib/System/Data/Redis/RedisPool.zan`, `stdlib/System/Data/Redis/RedisReply.zan`


## RedisClient (class)

基于异步 IO reactor、使用 RESP（RESP2）协议的协程化 Redis 客户端。

所有网络操作都通过异步
`Socket` 层（非阻塞 connect / send / recv）挂起到无栈 reactor，因此
单个工作线程上可同时有数千个 Redis 调用在途，而不阻塞线程
——与 HTTP/WebSocket 栈采用相同模型。这是刻意
的无依赖设计：RESP 是行协议，因此无需任何原生客户端库
。

用法：
RedisClient r = await RedisClient.ConnectAsync("127.0.0.1", 6379);
await r.SetAsync("name", "Alice");
string v = await r.GetAsync("name");     // "Alice"
int n = await r.DelAsync("name");        // 1
RedisReply pong = await r.CommandAsync2("PING", "");   // +PONG
r.Close();

二进制安全：bulk *回复* 按显式字节长度解析，因此含
NUL 字节的值可原样返回（使用 RedisReply.len）。命令
*参数* 用 strlen 度量，因此参数值不应包含
内嵌的 NUL 字节。

- nint sock;

- string host;

- int port;

- bool connected;

- string lastError;

- bool busy;

- List<AsyncGate> waiters;

- byte[]inbuf;

- int incap;

- int inhead;

- int intail;

- byte[]tmp;

- int tmpcap;

- TlsStream tls;

- static bool verifyTls=true;

- static void SetTlsVerify(bool verify)
  - 切换后续 ConnectSecureAsync 的证书与主机名校验。
    仅测试/开发自签场景关闭；生产保持默认开启。

- [DllImport("crt")]static extern long strlen(string str);

- RedisClient()
  - 私有构造；统一经 `ConnectAsync` 创建。

- static async RedisClient ConnectAsync(string host, int port)
  - 连接 Redis 服务器，在 IO reactor 上挂起直到
    （非阻塞）连接建立完成。

- static async RedisClient ConnectAsync(string host, int port, int timeoutMs)
  - 带连接超时（毫秒，0 = 不限时）的连接；失败返回 null。

- static async RedisClient ConnectSecureAsync(string host, int port, int timeoutMs)
  - 与 `ConnectAsync` 相同，但 TCP 建连后立即把
    通道升级为 TLS（Redis 的 TLS 监听从首字节就是加密流，无需
    协商握手）。认证（AUTH）与数据全部走加密信道。
    握手按 <paramref name="host"/> 做证书主机名校验，信任系统根。
    A287③：口令不再明文过网。

- static async RedisClient doConnect(string host, int port, int timeoutMs, bool secure)

- async bool AcquireLock()

- void ReleaseLock()

- void Close()
  - 关闭连接并释放缓冲区。

- string sliceBytes(int start, int len)
  - 从接收缓冲区中复制 <paramref name="len"/> 字节
    （从 <paramref name="start"/> 起）到新的 NUL 结尾字符串中。
    二进制安全：按索引复制，内嵌 NUL 字节也能保留。

- void ensureRoom(int extra)
  - 确保接收缓冲区在尾位置之后还能容纳 <paramref name="extra"/> 字节，
    必要时先压缩再扩容。带上限与溢出防护。

- async int fillMore()
  - 从套接字接收一块数据存入缓冲区。返回
    字节数（对端关闭时为 0）。在 reactor 上挂起直到可读。

- async bool ensureAvailable(int need)
  - 确保缓冲区中至少有 <paramref name="need"/> 字节，
    需要时从套接字补读。对端关闭时返回 false。

- async string readLine()
  - 从头游标处读取一行以 CRLF 结尾的行（不含 CRLF），
    借助 AVX2 向量化 IndexOf 快速跳过普通字节，需要时补读更多数据。对端
    关闭时返回 null。

- async string readBulk(int n)
  - 读取恰好 <paramref name="n"/> 字节的 bulk 负载，其后
    跟一个结尾 CRLF，并将头游标越过这两部分。

- async RedisReply readReply()
  - 从流中解析一条完整的 RESP 回复（数组递归解析），
    需要更多字节时在 reactor 上挂起。

- async int sendArgs(List<string> args)
  - 将 <paramref name="args"/> 编码为 RESP 数组（bulk 字符串），
    然后发送。参数长度用 strlen 度量。

- async RedisReply CommandAsync(List<string> args)
  - 发送命令（参数向量）并等待其回复。内部使用 AsyncGate 门控保证单连接事务互斥，杜绝并发调用数据串号。

- async RedisReply CommandInternalAsync(List<string> args)

- async RedisReply CommandBytesAsync(List<string> prefixArgs, byte[]binaryArg)
  - 发送带二进制参数的命令（如 SET key <byte[]>）并等待回复。
    前缀参数以字符串形式编码，最后一个二进制参数按原生字节流发送，
    完全二进制安全，支持任意包含 NUL 字节的大数据量载荷。

- async RedisReply CommandBytesInternalAsync(List<string> prefixArgs, byte[]binaryArg)

- async RedisReply CommandAsync1(string a0)
  - 便捷方法：单参数命令（例如 PING）。

- async RedisReply CommandAsync2(string a0, string a1)
  - 便捷方法：双参数命令（例如 GET key）。

- async RedisReply CommandAsync3(string a0, string a1, string a2)
  - 便捷方法：三参数命令（例如 SET key value）。

- async bool PingAsync()
  - PING；服务器回复 PONG 时返回 true。

- async bool AuthAsync(string password)
  - 使用密码进行认证。收到 +OK 返回 true。

- async bool SelectAsync(int index)
  - 选择逻辑数据库编号。收到 +OK 返回 true。

- async bool SetAsync(string key, string val)
  - SET key value。收到 +OK 返回 true。

- async bool SetBytesAsync(string key, byte[]val)
  - 二进制安全的 SET：将原始字节数组作为值存储。支持包含 NUL 字节的任意二进制载荷与大数据量。

- async string GetAsync(string key)
  - GET key。返回对应的值；键不存在（nil）时
    返回空字符串。如需区分 nil 与
    空值，请使用 `GetReplyAsync`。

- async byte[]GetBytesAsync(string key)
  - 二进制安全的 GET：返回原始字节数组。支持包含 NUL 字节的任意二进制载荷。
    键不存在或出错时返回 null。

- async RedisReply GetReplyAsync(string key)
  - GET key，返回原始回复，使调用方可区分
    空字符串值和不存在的键（nil）。

- async int DelAsync(string key)
  - DEL key。返回被删除的键数（0 或 1）。

- async int ExistsAsync(string key)
  - EXISTS key。键存在返回 1，否则返回 0。

- async int IncrAsync(string key)
  - INCR key。返回自增后的值。

- async int ExpireAsync(string key, int seconds)
  - EXPIRE key seconds。设置成功返回 1。

- string GetLastError()
  - 返回最近记录到的服务器错误消息（如有）。

- bool IsConnected()
  - 已连接则返回 true。


## RedisPool (class)

Redis 客户端连接池：复用空闲连接、限制并发连接数，饱和时
以协程挂起等待归还（有界，超时返回 null）。建连失败的路径
带退避，避免后端不可达时每次获取都重试建连。

- string host;

- int port;

- int connectTimeoutMs;

- PoolCore<RedisClient> core;

- RedisPool(string host, int port, int maxSize):this(host, port, maxSize, 3000)
  - 以默认 3000ms 连接超时构建池。

- RedisPool(string host, int port, int maxSize, int connectTimeoutMs)
  - 以显式连接超时（毫秒）构建池。

- async RedisClient OpenOne()
  - 新建一个客户端并用 PING 验证可用性；任何失败都关闭连接并
    返回 null（不抛出，由调用方记录建连失败）。

- async RedisClient AcquireAsync()
  - 借出一个客户端：复用空闲的，未达上限则新建，
    否则挂起协程直到有客户端被归还。饱和等待有硬上限
    （见 `PoolWait`）：超时返回 null，绝不定死。
    连接池关闭后返回 null。

- void Release(RedisClient client)
  - 归还一个客户端：池已关闭或连接已死时无条件 Close 并剔除
    （否则套接字泄漏），否则放回空闲列表。null 安全。

- int IdleCount()
  - 空闲连接数。

- int LiveCount()
  - 存活连接数（空闲 + 已借出）。

- int WaitingCount()
  - 正在等待归还的协程数。

- int MaxSize()
  - 并发连接上限。

- bool IsClosed()
  - 池是否已被关闭。

- void Close()
  - 关闭池：关闭所有空闲连接并唤醒全部等待者
    （它们从 AcquireAsync 得到 null）。已借出的连接在
    Release 时关闭。


## RedisReply (class)

一条已解析的 RESP（REdis Serialization Protocol）回复。

Redis 回复自带类型信息；`kind` 指明值在哪个字段中：
携带值：
- <c>STR</c>   : simple string (<c>+OK</c>)        -> `str`
- <c>ERROR</c> : error string (<c>-ERR ...</c>)    -> `str`
- <c>INT</c>   : integer (<c>:42</c>)              -> `integer`
- <c>BULK</c>  : bulk string (<c>$3\r\nfoo</c>)    -> `str` / `len`
- <c>NIL</c>   : null bulk / null array (<c>$-1</c>)
- <c>ARRAY</c> : array (<c>*2 ...</c>)             -> `items`

`len` 是 BULK 负载的真实字节长度；当值可能含
NUL 字节时，用它代替 <c>str.Length</c>（二进制安全）。

- static int STR=1;

- static int ERROR=2;

- static int INT=3;

- static int BULK=4;

- static int NIL=5;

- static int ARRAY=6;

- int kind;

- string str;

- int len;

- int integer;

- List<RedisReply> items;

- RedisReply()

- bool IsNil()
  - 当回复为 null bulk 字符串或 null 数组时为 true。

- bool IsError()
  - 当回复为服务器错误（<c>-ERR ...</c>）时为 true。

- bool IsString()
  - 当回复为简单字符串或 bulk 字符串时为 true。

- byte[]AsBytes()
  - 返回原始字节数组。二进制安全，即使包含 NUL 字节也不会被截断。

- string AsString()
  - 返回字符串负载（nil 或非字符串类型返回空）。

- int AsInt()
  - 返回整数负载（非整数类型返回 0）。

- int Count()
  - ARRAY 回复的元素数量（否则为 0）。

- RedisReply At(int i)
  - ARRAY 回复中下标为 <paramref name="i"/> 的元素。
