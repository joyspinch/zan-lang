# Zan 标准库设计：功能清单 + 用 zan 特性原生实现

> 原则（按你的要求）：**不借鉴任何现成库的封装**。只回答"一个现代 stdlib 应该有哪些功能"，然后**用 zan 自己的语言特性**（泛型、lambda/闭包、`async/await`、ARC、接口、迭代器、操作符重载、`T?` 可空、特性 attribute、扩展方法）设计出符合 zan 风格的 API。C#/.NET 仅作为"能力标杆"，不照抄其实现。
>
> 承接 `docs/STDLIB_ANALYSIS.md`。（原 `STDLIB_DB_AARDIO_REF.md` 已废弃。）

---

## 0'. 全局硬性约定：**跨平台 + 协程化**
zan 是 M:N 协程（`Task`）+ channel + 跨平台异步 I/O（Linux `io_uring/epoll`、Windows `IOCP`、macOS `kqueue`，见 `docs/CONCURRENCY.md`）。因此整个 stdlib 遵守两条铁律：

1. **一切 I/O-bound API 一律协程化**，返回 `Task<T>`（单值）或 `IAsyncEnumerable<T>`（流式），绝不提供阻塞版占位：
   - 文件、网络、数据库、进程、定时器、DNS、Socket…全部 `async`；调用点 `await`，自动挂起让出调度器，不占用 OS 线程。
   - 流式读取用 `await foreach`（`IAsyncEnumerable<T>`）。
   - CPU-bound（内存内 LINQ、字符串、数学、编解码）保持同步；但凡数据源是 I/O，就提供 `IAsyncEnumerable<T>` 版本的 LINQ。
2. **一切平台差异用 `#if PLATFORM_*` 封在 `native/{win,linux,macos}` 内**，对外 API 各平台完全一致。底层异步 I/O 直接对接运行时的 io_uring/IOCP/kqueue 事件循环，**不能用同步 DLL 调用假装异步**（那会阻塞 worker 线程，毁掉 M:N 调度）。

> 换句话说：本文下面所有涉及读写/网络/DB 的签名都带 `async Task<...>`；同步 IO API（如现有 `File.ReadAllText`）只作为对 `await File.ReadAllTextAsync` 的便捷包装或干脆废弃。

并发原语（`Channel<T>` / `Mutex.Lock()`(返回可 `using` 的锁) / `RWLock` / `Semaphore` / `AtomicInt` / `TaskGroup` / `CancellationToken`）已在 `CONCURRENCY.md` 定义，stdlib 直接复用，不再自造。所有耗时 API 都接受可选 `CancellationToken`。

---

## 0. 先决：需要补的语言/编译器能力
这些是让 stdlib "能写得优雅"的地基，缺了就只能写丑 API：
1. **迭代器 / `foreach`**：`IEnumerator<T>` + `yield`（LINQ 延迟求值的前提）。
2. **扩展方法**：`static T Method(this X x, ...)`（LINQ、字符串扩展都靠它挂到类型上）。
3. **反射 / `TypeInfo` + 读取特性**：ORM、JSON 自动映射、DI 的前提。
4. **异步迭代器 `IAsyncEnumerable<T>` + `await foreach`**：流式 I/O（读文件行、DB 结果集、channel）的协程化前提（`CONCURRENCY.md` §6.2 已列为目标）。
5. **表达式树（可选、进阶）**：把 `u => u.Age > 18` 捕获成可内省的 AST，供 ORM 翻译成 SQL。没有它就走"强类型列引用"方案（见 §5）。

---

## 1. LINQ —— 用 `IEnumerable<T>` + 迭代器 + 扩展方法
不做成 `List` 的成员方法（那样每步都要物化一份 List），而是**延迟求值的序列管道**：

```csharp
public interface IEnumerable<T> { IEnumerator<T> GetEnumerator(); }
public interface IEnumerator<T> { bool MoveNext(); T Current { get; } }

public static class Linq {
    public static IEnumerable<T> Where<T>(this IEnumerable<T> src, Func<T,bool> pred) {
        foreach (var x in src) if (pred(x)) yield return x;      // 延迟求值
    }
    public static IEnumerable<R> Select<T,R>(this IEnumerable<T> src, Func<T,R> f) {
        foreach (var x in src) yield return f(x);
    }
    public static IEnumerable<T> OrderBy<T,K>(this IEnumerable<T> src, Func<T,K> key) where K: IComparable<K>;
    public static Dict<K, List<T>> GroupBy<T,K>(this IEnumerable<T> src, Func<T,K> key);
    public static T First<T>(this IEnumerable<T> src, Func<T,bool> pred);
    public static bool Any<T>(...); public static bool All<T>(...);
    public static int Count<T>(...); public static int Sum<T>(this IEnumerable<T> s, Func<T,int> f);
    public static List<T> ToList<T>(this IEnumerable<T> src);
    public static Dict<K,V> ToDict<T,K,V>(...);
    public static IEnumerable<T> Skip<T>(...); public static IEnumerable<T> Take<T>(...);
    public static IEnumerable<T> Distinct<T>(...);
}
```
用起来就是标准 LINQ 体验，但实现全是 zan 的 `yield` + 扩展方法：
```csharp
var names = users.Where(u => u.Age >= 18).OrderBy(u => u.Name).Select(u => u.Name).ToList();
```
**异步版（数据源是 I/O 时）**：对 `IAsyncEnumerable<T>` 提供同名扩展，用 `await foreach` + `yield`，末端 `await ToListAsync()`：
```csharp
var lines = await File.ReadLinesAsync("big.log")      // IAsyncEnumerable<string>
                      .Where(l => l.Contains("ERROR"))
                      .Take(100)
                      .ToListAsync();
```

---

## 2. 集合家族（泛型 + ARC + 迭代器）
补齐并统一实现 `IEnumerable<T>`：`Set<T>`、`Queue<T>`、`Stack<T>`、`SortedDict<K,V>`、`LinkedList<T>`、只读视图。全部走 zan 泛型单态化 + ARC，无需手工释放。

---

## 3. 字符串 & 文本（挂在 `string` 上的扩展方法）
`Split / Join / Trim / TrimStart / TrimEnd / Replace / StartsWith / EndsWith / Contains / ToUpper / ToLower / PadLeft / PadRight / Repeat / IndexOf(string) / Format`。
- `Format` 结合已有的 `$"..."` 插值统一走 `StringBuilder`。
- `Regex`：用 zan 写一个 NFA/回溯引擎（纯 zan，不依赖外部 DLL），接口 `Regex.Match / Matches / Replace / IsMatch`。
- `Encoding`：UTF-8 ↔ UTF-16、Base64、Hex——用 zan 的字节数组实现。

---

## 4. 基础类型与错误模型
- **`DateTime` / `TimeSpan`**：用 `struct` + 值语义实现，内部存 i64 tick；`Now/UtcNow/Parse/Format/AddDays/运算符重载(+ - < >)`。
- **`Guid` / `Random`**：`struct`。
- **统一错误模型**：在已实现的 `try/catch` 上定义异常层级（`Exception → IOException / FormatException / SqlException …`）；对"可预期失败"提供 `Result<T>`（用**标签联合 enum**，zan 已支持代数类型 + 模式匹配）：
  ```csharp
  enum Result<T,E> { Ok(T value), Err(E error) }
  switch (File.TryReadAllText(path)) {
      case Ok(var text): ...;
      case Err(var e):   ...;
  }
  ```

---

## 5. 数据库 / ORM —— 用 zan 特性原生做
分层，每层都能独立用：

### 5.1 驱动层 `System.Data`（接口 + SQLite 后端，**全异步**）
用 zan 接口定义与后端无关的抽象，句柄包进不透明 `class`（ARC 析构自动 `close`，不裸传 `int`）。数据库是 I/O，**所以全部 `async`**，结果集用异步迭代器流式读（不一次性物化）；后端对接运行时事件循环，不阻塞 worker：
```csharp
public interface IDbConnection {
    Task<IDbCommand> CreateCommand(string sql);
    Task<IDbTransaction> BeginTransaction();
    Task Close();
}
public interface IDbCommand {
    IDbCommand Bind(string name, object value);      // 参数化，防注入
    Task<int> Execute(CancellationToken ct = null);  // 影响行数
    IAsyncEnumerable<IDataRow> Query();              // 异步流式读取
}
// 跨平台：native/{win,linux,macos} 下各自接 sqlite3 并桥接运行时异步 I/O
```

### 5.2 映射层（反射 + 特性）
实体用特性标注，反射把行映射到对象：
```csharp
[Table("t_user")]
class User {
    [Key, AutoIncrement] public int Id;
    [Column("user_name")] public string Name;
    public int Age;
}
List<User> all = await db.Query<User>("select * from t_user where age >= @age", new { age = 18 })
                         .ToListAsync();
```

### 5.3 查询层——**zan 风格的类型安全查询，不靠表达式树**
C# 的 `Where(u => u.Age>18)` 依赖表达式树；zan 若暂不做表达式树，用**强类型列引用**（编译期安全、无字符串），这是更"zan"的做法：
```csharp
var q = db.Select<User>();
List<User> list = await q.Where(User.Age >= 18)     // User.Age 是强类型列 Column<int>
                         .OrderBy(User.Name)
                         .Take(20)
                         .ToListAsync();             // 执行才发 SQL（异步）
// 也可流式：await foreach (var u in q.Where(User.Age >= 18)) { ... }
```
`Column<T>` 重载 `>= == < > &&` 等运算符，返回一个 `SqlExpr` 节点树 → 直接生成 SQL。**这套完全用 zan 的操作符重载 + 泛型实现，类型安全且能翻译成 SQL，避开了表达式树的编译器改造。**

> 进阶（可选）：等编译器支持表达式树后，再额外提供 `Where(u => u.Age >= 18)` 的 lambda 语法糖，与 §5.3 共存。

### 5.4 上层便利
`await Insert(entity) / await Update(entity) / await Delete<T>().Where(...)`、`await SaveChanges()` 批量、事务用 `using` + ARC 自动提交/回滚、CodeFirst `await SyncStructure<T>()` 自动建表。连接池本身也是协程友好的（`await pool.Rent()`）。

---

## 6. 网络 / 序列化 / 其它常用（I/O 均协程化，后端跨平台）
- **`HttpClient`**：`async Task<HttpResponse> Get/Post/...`，身体流式 `IAsyncEnumerable<byte[]>`；跨平台后端直接基于非阻塞 socket + 运行时事件循环（而非同步 WinHTTP/libcurl 阻塞调用），用 `#if PLATFORM_*` 隔离。
- **`Socket` / `TcpListener` / `Dns`**：`await Socket.Connect`、`await listener.Accept`、`await sock.Read/Write`——对接 io_uring/IOCP/kqueue（参 `CONCURRENCY.md` §9.3 的 echo server）。
- **文件 / Stream 家族**：`Stream / FileStream / MemoryStream / TextReader / TextWriter / BinaryReader / BinaryWriter`，读写均 `async`（`await stream.ReadAsync`）；`File.ReadLinesAsync` 返回 `IAsyncEnumerable<string>`。
- **JSON**：接通运行时值树，`Json.Parse → JsonValue`（索引器 `v["a"][0]`，同步——纯计算）；大流解析提供 `Json.ParseStreamAsync`；反射做对象↔JSON 映射 `Json.Deserialize<T>()`。
- **并发**：直接用 `CONCURRENCY.md` 的 `Task/Channel<T>/TaskGroup/Mutex/RWLock/Semaphore/AtomicInt`；lock 用 `using (await mutex.Lock())`。
- **安全**：`SHA256 / MD5 / HMAC / AES / Base64`（纯计算，同步）。
- **`Environment` / `Process`（跨平台，`await proc.WaitForExit` / stdout 用 `IAsyncEnumerable`）/ `Stopwatch`（高精度）/ `Timer`（`await Task.Delay`）**。

---

## 6.5 协议服务端 / 客户端框架（`System.Net.Sockets` + `System.Net.Protocols`）
对标 workerman / swoole / netty / Go net：**统一连接事件模型 + 可插拔协议编解码器**，全部跑在 zan 的 M:N 协程调度上，一个协程一个连接（可承载百万连接），跨平台（io_uring/IOCP/kqueue）。

### 6.5.1 事件驱动服务端（workerman 风格，但协程化）
```csharp
var server = new TcpServer("0.0.0.0:8080");
server.OnConnect = async conn => Console.WriteLine($"+ {conn.RemoteAddr}");
server.OnMessage = async (conn, msg) => await conn.Send(msg);   // echo
server.OnClose   = async conn => Console.WriteLine($"- {conn.RemoteAddr}");
await server.Start();     // 每个连接在自己的协程里跑，天然并发
```
- 也支持**纯协程式**写法（不用回调，直接 `await`，见 CONCURRENCY.md §9.3）：
  ```csharp
  var listener = await TcpListener.Bind("0.0.0.0:8080");
  while (true) {
      var conn = await listener.Accept();
      Task.Run(async () => { /* 处理这个连接 */ });   // 轻量协程
  }
  ```
- 多核：默认多 worker + `SO_REUSEPORT`（或主从 accept），无需用户操心。

### 6.5.2 可插拔协议编解码器（关键抽象）
连接与协议解耦——`IProtocol` 负责"字节流 ↔ 消息帧"，业务只处理完整消息（解决 TCP 粘包/拆包）：
```csharp
public interface IProtocol<TMessage> {
    int    Input(ByteBuffer recv);              // 返回一帧长度(0=需更多数据)
    TMessage Decode(ByteBuffer frame);          // 字节 → 消息
    ByteBuffer Encode(TMessage msg);            // 消息 → 字节
}
var ws = new TcpServer("0.0.0.0:9000", new WebSocketProtocol());
ws.OnMessage = async (conn, WebSocketFrame f) => await conn.Send(new TextFrame(f.Text));
```

### 6.5.3 内置协议（客户端 + 服务端成对提供，均协程化）
| 协议 | 服务端 | 客户端 |
|---|---|---|
| TCP / UDP / Unix socket | `TcpServer` / `UdpServer` | `TcpClient` / `UdpClient` |
| HTTP/1.1 | `HttpServer`（路由/中间件） | `HttpClient`（§6） |
| WebSocket | `WebSocketServer` | `WebSocketClient` |
| TLS/SSL | 以上均可 `.UseTls(cert)` 包一层 | `.UseTls()` |
| HTTP/2 · gRPC | `Http2Server` / `GrpcServer` | `GrpcClient` |
| MQTT / Redis / 自定义 | 用 `IProtocol` 自己插 | 同 |

- **HTTP 服务端**：路由 + 中间件 + `async` handler：
  ```csharp
  var app = new HttpServer();
  app.Get("/users/:id", async ctx => await ctx.Json(await db.Get<User>(ctx.Param("id"))));
  app.Use(async (ctx, next) => { /* 日志/鉴权 */ await next(); });
  await app.Listen(8080);
  ```
- **连接治理**：心跳/超时/自动重连(客户端)、连接池、背压(基于 `Channel` 容量)、优雅关闭、`CancellationToken` 贯穿。
- **广播/分组**（IM/推送常用）：`ConnectionGroup.Broadcast(msg)`。

> 定位：workerman 给的是"事件回调 + 多进程"；zan 版本保留同样简单的事件 API，但底层是**协程**（写起来像同步、跑起来是异步），比回调更好维护，且天然利用 M:N 调度和多核。

---

## 7. 优先级（务实顺序）
1. **地基**：迭代器/`foreach` + **异步迭代器 `IAsyncEnumerable`** + 扩展方法 + 反射（不然上面很多写不优雅）。并发原语以 `CONCURRENCY.md` 为准。
2. **LINQ + 集合家族 + 字符串扩展 + DateTime + Result/异常**（日常最高频）。
3. **跨平台异步 I/O 底座**：`Stream`(async) + `Socket/TcpListener`(io_uring/IOCP/kqueue) —— 这是网络框架和 DB 的共同地基。
4. **协议框架**：`TcpServer/UdpServer` + `IProtocol` → HTTP 服务端(路由/中间件) + `HttpClient` → WebSocket → TLS → (gRPC/MQTT)。
5. **JSON 自动映射（含流式）**。
6. **DB：SQLite 驱动(async) → 反射映射 → `Column<T>` 类型安全查询 → CodeFirst**。
7. 表达式树 lambda 查询、加密、DI/日志/配置等。

一句话：**功能对标 .NET/workerman/swoole，实现全用 zan 自己的协程/泛型/迭代器/操作符重载/ARC/反射，做成类型安全、全协程化、跨平台、有统一错误模型的原生 stdlib——写起来像同步，跑起来全异步。**
