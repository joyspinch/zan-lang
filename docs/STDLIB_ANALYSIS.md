# Zan 标准库能力分析 (Stdlib Capability Analysis)

> 目标：盘点 zan-lang 当前"常用标准库"的真实覆盖面，找出与 C#/.NET 级别常用库的差距（含 ORM、LINQ 等），并给出分优先级的补全建议。
> 结论先行：**语言层能力已相当完整（类/泛型/接口/async/异常/模式匹配/FFI），但标准库仍停留在"能跑通 demo"的薄封装阶段——大量常用能力缺失，且现有实现存在"仅 Windows / 靠 msvcrt / 字符串手撸"三大系统性问题。**

---

## 1. 现状总览

### 1.1 内置（编译器/运行时提供，无 .zan 源文件）
| 能力 | 载体 | 状态 |
|---|---|---|
| `Console`（Write/WriteLine/ReadLine…） | 编译器内置 | ✅ 可用 |
| `Convert` / `Math` / 字符串插值 `$"..."` | 编译器内置 | ✅ 基本可用 |
| `string`（`.Length` / `.Substring` / `.IndexOf` 等） | 编译器内置 | 🟡 基础方法，缺很多 |
| `List<T>` / `Dict<K,V>` | 运行时内置 (`rt_core`) | 🟡 仅基础增删查，**无 LINQ** |
| `StringBuilder` | `src/compiler/stdlib_ext.c` | ✅ 可用 |
| JSON parse/serialize（值树） | `stdlib_ext.c` (`zan_json_*`) | 🟡 运行时有能力，但 `.zan` 侧未封装成好用 API |
| Thread / Mutex / Event / Atomic | `stdlib_ext.c` | 🟡 底层有，`.zan` 侧封装很薄 |
| HTTP get/post | `stdlib_ext.c` (`zan_http_*`) | 🟡 运行时有，`.zan` 侧 `Net.zan` 只是空壳 |

### 1.2 stdlib/ 源码模块（.zan）
| 命名空间 | 文件 | 覆盖 | 评价 |
|---|---|---|---|
| `System.IO` | File / Directory / Path | 🟡 中 | 靠 `msvcrt` fopen/remove，**仅 Windows**，无 `Stream`/`BinaryReader`/`TextReader`，无异常 |
| `System.Json` | Json.zan | 🔴 低 | **靠 Substring 手撸扫描**，只能取顶层 key，不能建对象树/数组/嵌套，与运行时的 `zan_json_*` 脱节 |
| `System.Net` | Net.zan | 🔴 低 | 只有一堆 `DllImport` 声明 + 半个 TcpSocket，`HttpClient` 无任何方法体 |
| `System.Text` | Encoding.zan | 🔴 低 | 只有 IntToString/ParseInt/ParseDouble/GetByteCount，**无真正 UTF-8/16 编解码、无 Regex** |
| `System.Threading` | Threading.zan | 🔴 低 | Thread 只暴露 Sleep/CurrentId（**不能真正启动线程**），Stopwatch 只有 ms |
| `System.Diagnostics` | Process.zan | 🟡 中 | `_popen` 抓 stdout，可用但仅 Windows、无 exit code/stdin |
| `System.Drawing` | Graphics / Primitives | 🟡 | 2D 绘图原语 |
| `System.Windows` | Forms.zan | 🟡 | Win32 表单，平台锁定 |
| `Gui` + `Gui/Widget` | 60+ 控件 | ✅ 高 | 组件库很全（类 Ant Design：DataTable/Chart/CodeEditor/PivotTable/Wizard…），是目前最完整的部分 |
| `Platform` | Windows / Posix | 🔴 低 | 各 ~1KB，占位级 |

### 1.3 规格 vs 现实
`docs/STDLIB.md` 里规划了一整套 .NET 风格库（含 `List.Map/Where/Aggregate/OrderBy/Distinct`、`Collections/Set/Queue/SortedDict`、`Serialization/Xml`、`Runtime`、`Graphics`、`UI` 等），但**大部分只在文档里，仓库中并不存在**。例如文档声称 `List<T>` 带完整 LINQ，实际运行时 `List` 只有 `Add/Insert/Remove/RemoveAt/Contains/IndexOf/Reverse/Clear/Count/[]`。

---

## 2. 常用标准库缺口清单（按"日常出现频率"排序）

### 2.1 🔴 P0——几乎每个程序都要用，但缺失/不可用
1. **LINQ / 序列查询**：`Where/Select/OrderBy/GroupBy/First/Any/All/Sum/Count/Distinct/ToList/ToDict/Aggregate/Skip/Take` 全无。当前只能手写 for 循环。
2. **可用的集合家族**：缺 `Set<T>`、`Queue<T>`、`Stack<T>`、`SortedDict`、`LinkedList`、只读集合、`IEnumerable<T>`/迭代器协议（`foreach`）。
3. **字符串能力**：`Split/Join/Trim/Replace/StartsWith/EndsWith/Contains/ToUpper/ToLower/Format/PadLeft/Split by string`——目前只有 `Substring/Length/IndexOf` 级别。
4. **DateTime / TimeSpan**：完全没有（文档里列了 `DateTime.zan` 但文件不存在）。
5. **异常与错误模型**：IO/Net/JSON 全都"失败返回空串/0"，没有异常或 `Result<T>`。日常健壮性无从谈起。
6. **真正的 JSON API**：`Json.Parse(str) -> JsonValue`、`obj["a"]["b"][0]`、`Serialize(object)`——运行时已有 `zan_json_*`，只差 `.zan` 封装。

### 2.2 🟠 P1——常见业务/服务端程序需要
7. **可用的 `HttpClient`**：`Get/Post/Put/Delete` + headers + status + body（运行时 `zan_http_*` 已具备，`.zan` 侧补封装即可）。
8. **真正的线程/异步**：`Thread.Start(delegate)`、`Task<T>` + `await` 的运行时调度、`ThreadPool`、`Channel<T>`、`lock` 语法糖。
9. **Stream 抽象**：`Stream/FileStream/MemoryStream/TextReader/TextWriter/BinaryReader/BinaryWriter`——目前 File 一次性读全文，无法处理大文件/流式。
10. **正则 `Regex`**：文档规划了 RE2，实际没有。
11. **`Environment` / `Guid` / `Random`**：环境变量、命令行参数、GUID、随机数——都缺。
12. **编码 `Encoding`**：真正的 UTF-8 ↔ UTF-16、Base64、十六进制。

### 2.3 🟡 P2——"要不缺一大截"的高阶封装（用户明确点名）
13. **LINQ-to-Objects 完整版**（延迟求值 / `IEnumerable` 管道）。
14. **ORM / 数据访问层**：
    - 底层：`System.Data` 抽象（`DbConnection/DbCommand/DbReader`）+ SQLite/MySQL/Postgres 驱动（可先 `DllImport` sqlite3）。
    - 上层：实体映射 + 查询构建器（类 EF Core：`db.Users.Where(u => u.Age > 18).ToList()`）。
    - 迁移/建表。
15. **序列化**：`System.Xml`、`System.Text.Json` 的对象↔JSON 自动映射（需要反射/`TypeInfo`）、二进制序列化。
16. **反射 / RTTI**：`TypeInfo`、特性读取——ORM/序列化/DI 的前置能力。
17. **日志 `Logging`**、**配置 `Configuration`**、**依赖注入**（服务端常用三件套）。
18. **加密/哈希**：`MD5/SHA/HMAC/AES/Base64`、`System.Security`。

---

## 3. 系统性设计问题（不解决则补再多也难用）

1. **平台锁定**：几乎所有模块直接 `DllImport("msvcrt"/"kernel32"/"winhttp"/"ws2_32")`，Linux/macOS 无法用。`docs/STDLIB.md` 设计的 `native/{win,linux,macos}` + `#if PLATFORM_*` 条件编译方案**尚未落地**。
2. **句柄用 `int`**：文件、socket、线程句柄都用 `int` 表示（`int fp = fopen(...)`），在 64 位上是隐患，也丢失类型安全，应封装成不透明句柄/类。
3. **靠字符串扫描实现协议**：JSON 用 `Substring` 逐字符扫，既慢又只能处理平坦结构；运行时已有正规 `zan_json_*` 却没接上。
4. **无统一错误模型**：需要在语言的异常 (M5.4 已实现 try/catch) 基础上，约定 stdlib 的抛错规范。
5. **运行时能力与 .zan 封装脱节**：`stdlib_ext.c` 里已有 HTTP/线程/JSON/SB 的完整 C 实现，但 `.zan` 层没有把它们 `extern` 出来做成好 API——**很多"缺失"其实是"没封装"，成本低、收益高**。
6. **缺 `mod.zan` 入口**：`docs/STDLIB.md` 约定目录模块用 `mod.zan`，但 `System/` 下无一存在，命名空间聚合与"lazy loading"未实现。

---

## 4. 建议的补全路线（分阶段）

### 阶段 A（地基，1–2 周量级）
- **接通运行时**：给 `stdlib_ext.c` 的 HTTP/JSON/Thread 写 `.zan` 绑定，产出可用的 `HttpClient`、`Json.Parse/Serialize`、`Thread.Start`。**最高性价比**。
- **字符串扩展**：`Split/Join/Trim/Replace/StartsWith/EndsWith/Contains/ToUpper/ToLower/PadLeft/Format`。
- **DateTime/TimeSpan、Random、Guid、Environment**。
- **统一错误策略**：定义 `System` 异常层级（`IOException/FormatException/...`）并改造 IO/JSON。

### 阶段 B（集合 + LINQ，用户点名）
- 补齐集合家族：`Set/Queue/Stack/SortedDict/LinkedList` + `IEnumerable<T>`/迭代器 + `foreach`。
- **LINQ**：以 `IEnumerable<T>` 扩展方法（或 List 方法）实现 `Where/Select/OrderBy/GroupBy/Aggregate/First/Any/All/Sum/Count/Distinct/Skip/Take/ToList/ToDict`。建议延迟求值。

### 阶段 C（I/O 与网络成熟化）
- `Stream` 家族 + `TextReader/Writer`、`BinaryReader/Writer`。
- 跨平台化：落地 `native/{win,linux,macos}` + `#if PLATFORM_*`，先让 `File/Directory/Console/Process` 在 Linux 跑起来。
- `Regex`（RE2 或纯 Zan NFA）、`Encoding`（UTF-8/16、Base64）。

### 阶段 D（反射 → 序列化 → ORM，用户点名）
- `System.Reflection`/`TypeInfo` + 特性读取（ORM/序列化前置）。
- 对象↔JSON 自动映射；`System.Xml`。
- `System.Data` 抽象 + SQLite 驱动（`DllImport sqlite3`）→ 查询构建器 → 轻量 ORM（`db.Set<T>().Where(...).ToList()`、变更跟踪、迁移）。

### 阶段 E（服务端周边）
- Logging、Configuration、DI、加密/哈希、HttpServer。

---

## 5. 关键 API 草案（供讨论）

**LINQ（作为 `List<T>` / `IEnumerable<T>` 扩展）**
```csharp
var adults = users
    .Where(u => u.Age >= 18)
    .OrderBy(u => u.Name)
    .Select(u => u.Name)
    .ToList();

int total = orders.Sum(o => o.Amount);
var byCity = users.GroupBy(u => u.City);   // Dict<string, List<User>>
```

**JSON（接通运行时值树）**
```csharp
JsonValue v = Json.Parse(text);
string name = v["user"]["name"].AsString();
int age     = v["user"]["age"].AsInt();
string s    = Json.Serialize(myObject, pretty: true);
```

**HttpClient（封装 `zan_http_*`）**
```csharp
var http = new HttpClient();
http.SetHeader("Authorization", "Bearer ...");
HttpResponse r = await http.Get("https://api.example.com/users");
if (r.IsSuccess) { var data = Json.Parse(r.Body); }
```

**ORM（长期目标，类 EF Core）**
```csharp
using var db = new DbContext("sqlite://app.db");
var adults = db.Set<User>()
               .Where(u => u.Age >= 18)
               .OrderBy(u => u.Name)
               .ToList();
db.Add(new User { Name = "Zan", Age = 1 });
db.SaveChanges();
```

---

## 6. 一句话总结
- **能力最全**：`Gui`（60+ 控件）。
- **最该马上做**：把 `stdlib_ext.c` 已有的 HTTP/JSON/线程封装成 `.zan` API（低成本高收益）。
- **用户点名的 LINQ / ORM**：LINQ 属阶段 B（需迭代器 + 扩展方法）；ORM 属阶段 D（需先补反射 + `System.Data` + SQLite 驱动）。
- **不解决会拖后腿的根因**：平台锁定（仅 Windows/msvcrt）、`int` 句柄、字符串手撸协议、无统一异常、运行时与 `.zan` 封装脱节。
