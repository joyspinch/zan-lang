# 标准库（总览与索引）

> Zan 标准库以 `.zan` 源码随编译器分发（`--auto-stdlib` 自动定位），命名空间
> 路径直接映射 `stdlib/` 目录。本页是**索引与导航**；每个命名空间的完整
> API（类型 + 成员签名 + `///` 文档注释，从源码提取）在 `/ref/<命名空间>.html`，
> 同名 `/ref/<命名空间>.md` 供 AI 直接抓取（如
> [/ref/System.IO.md](/ref/System.IO.md)）。

## 命名空间地图

| 命名空间 | 内容 | 参考页 |
|---|---|---|
| `System` | 语言内建 + 核心类型：`DateTime` `TimeSpan` `Guid` `Random` `Exception` `IDisposable` `MessageBox` `Binding<T>` `Interop` `Com` `NativeMemory` `ZanVersion` 等 | [System](/ref/System) |
| `System.Collections` / `.Generic` | `HashSet<T>` `LinkedList<T>` `Queue<T>` `Stack<T>` `KeyValuePair<K,V>` / `KVP<K,V>` | [引用](/ref/System.Collections) |
| `System.Compiler` | 编译期代码生成：`ZanGen` `GenDb` `GenForm`（.zform 投影）`GenJson` `GenRoute` `GenScene` | [引用](/ref/System.Compiler) |
| `System.Data` | 数据库统一层：`DbConnection` `DbResult` `DbParams` `DbPool` + 方言（`Sqlite` `MySql` `Postgres` `SqlServer` `Firebird` `TDengine` `Redis` `Odbc`）+ ORM（`Model` `ModelQuery` `QueryBuilder` `Migration`）+ `ZanDb` 嵌入式 KV | [引用](/ref/System.Data) |
| `System.Diagnostics` | `Process` `ProcessHost` `ProcessList` `Stopwatch` `ServerMetrics` `Log`（分级文件日志）`Privileges` | [引用](/ref/System.Diagnostics) |
| `System.Drawing` | Win32 风格：`Graphics`（HDC）`Bitmap` `Font` `Color` `Point` `Size` `Rectangle` `PrinterSettings` `RawPrinter`（无 Pen/Brush，用 color+penWidth 传参） | [引用](/ref/System.Drawing) |
| `System.Globalization` | `Lunar` `LunarDate`（农历） | [引用](/ref/System.Globalization) |
| `System.IO` | `File` `Directory` `Path` `(PathEx)` `Stream` `FileStream` `StreamReader/Writer` `MemoryStream` `ByteBuffer` `Compression`（GZip/Deflate/Zip/Tar/BZip2/Crc32）`IniFile` `MemoryMappedFile` `DirectoryWatcher` `KnownFolders` `Shortcut` | [引用](/ref/System.IO) |
| `System.Input` | `Keyboard` `Mouse` `Hotkey` `Hook` `Background` | [引用](/ref/System.Input) |
| `System.Json` | `JsonValue`（DOM）`JsonParser` `JsonBuilder` `JsonReader` | [引用](/ref/System.Json) |
| `System.Knowledge` | `GalleryIndex` `ZformSchema` `ZformResult` `KnowledgeProp` `KnowledgeControl`（AI 知识库/表单 schema） | [引用](/ref/System.Knowledge) |
| `System.Linq` | `Enumerable`（91 个 `List<T>` 扩展：Where/Select/Any/All/First/Last/Single/Take/Skip/Distinct/OrderBy/GroupBy/Aggregate/Sum/Min/Max/...）`Expr<T>` 表达式树 | [引用](/ref/System.Linq) |
| `System.Management` | `Cpu` `Memory` `DiskUsage` `Device` `Display` `Power` `Registry` `TaskScheduler` `SystemInfo` | [引用](/ref/System.Management) |
| `System.Net` | 网络全家桶：`HttpClient` `HttpServer` `HttpRequest/Response` `Framer`；`Sockets`（`Socket` `TcpClient` `TcpListener` `UdpClient` `AsyncSocket`）`WebSocket`（`WebSocketClient/Server` `Wss`）`Mqtt` `Coap` `Modbus` `Ntp` `Sip` `Sse` `WebDav` `Rpc` `Tls`；`Worker` 多进程服务器 | [引用](/ref/System.Net) |
| `System.Resources` | `ResourcePack` `ResourcePackWriter` | [引用](/ref/System.Resources) |
| `System.Scripting` | `Lua`/`LuaValue`、`Python`/`PyObject` 嵌入（带驱动） | [引用](/ref/System.Scripting) |
| `System.Security` / `.Cryptography` | 纯 Zan 实现：`Sha1/256/512` `Md5` `Sm2/3/4` `Aes` `AesGcm` `Rsa` `Hmac` `Hkdf` `Jwt` `Otp` `BigInt` `Base64` `Hex` `Bits` `RandomNumberGenerator`；`Guard` `Protected` | [引用](/ref/System.Security.Cryptography) |
| `System.ServiceProcess` | Windows 服务：`ServiceProcess` `ServiceInfo` | [引用](/ref/System.ServiceProcess) |
| `System.Text` | `Encoding`（Base64/Url/Html/Json/WebSocketAccept…）`Csv` `Markdown` `Pinyin` `Template` `TextTable` `Bm25Index` `FuzzyMatching`；`RegularExpressions/` `Regex` `Match` | [引用](/ref/System.Text) |
| `System.Threading` | `Thread` `Channel` `Mutex` `Semaphore(Slim)` `Gate/AsyncGate/AsyncRwLock` `AtomicInt` `SharedTable`（跨进程）`BlockingQueue<T>` `Timer` | [引用](/ref/System.Threading) |
| `System.Web` | server-mvc：`WebApp` `WebHost` `Router` `Route` `Controller` `HttpContext` `ApiEnvelope` `RateLimiter` `Sessions` `Hooks` `StaticFiles` `View` `MenuBuilder` `Validator` `ListQuery`；属性路由 `RouteAttribute/HttpGet/HttpPost/...` | [引用](/ref/System.Web) |
| `System.Windows` / `.Clipboard` | `Clipboard` `Screen` `TrayIcon` | [引用](/ref/System.Windows) |
| `System.Automation` | UIA 自动化：`UiElement` `Window` 等 | [引用](/ref/System.Automation) |
| `Gui` | 桌面 UI 框架核心：`App` `Control` `Form` `Layout` `Stack` `Style` `StyleBox` `StyleSheet` `Css` `Theme` `Skin` `Tailwind` `Effects` `Ui` `Icon` `Serialize` `Reactive` `ControlFactory` `ChildWindow` `NativeLayer` | [引用](/ref/Gui) |
| `Gui.Widget` | 控件（85 个类）：`Button` `Label` `Input` `TextArea` `Checkbox` `Radio` `Switch` `SelectBox` `Slider` `Rate` `Panel` `Card` `Tabs` `Table` `ListView<T>` `TreeView` `VirtualList` `ScrollColumn` `SplitPanel` `Wizard` `ToolStrip` `StatusBar` `Layer` `Popover` … | [引用](/ref/Gui.Widget) |
| `Gui.Component` | 高级组件：`Dock`（DockPanel/Group/Host）`GraphView` `FilePicker` `FileTree` `LogView` `ChatView` `ConsoleView` `PivotTable` `PropertyGrid` `SessionList` `Downloader` `CodeEditor` `WebView` `DataTable` `CefBrowser` `Chart` | [引用](/ref/Gui.Component) |
| `Gui.Hmi` | 工控：`Gauge` `Bargraph` `Led` `Digital` `EquipPanel` `Alarm(Banner/List)` `NumPad` `Trend` | [引用](/ref/Gui.Hmi) |
| `Gui.Designer` | 可视化设计器（`.zform` 编辑/检查器/画布） | [引用](/ref/Gui.Designer) |
| `Gui.Backend` | 原生层：`Window` `UiDriver` `Win32Shell` | [引用](/ref/Gui.Backend) |
| `Game.Core` | 游戏主循环 `App` | [引用](/ref/Game.Core) |
| `Game.Foundation` (+ `.Sdl`) | 固定步长计时、语义化输入、场景生命周期、SDL 宿主 | [引用](/ref/Game.Foundation) |
| `Game.Board` / `Game.Cards` / `Game.Arcade2D` | 可克隆网格/寻路、卡牌构筑、几何与碰撞 | [引用](/ref/Game.Board) · [引用](/ref/Game.Cards) · [引用](/ref/Game.Arcade2D) |
| `Game.Scene` / `Game.Render` | 场景文档/设计器、精灵与位图字体 | [引用](/ref/Game.Scene) · [引用](/ref/Game.Render) |
| `Game.Arpg` | 完整类型化 RPG 运行时（项目、世界、战斗、UI、存档、SD 渲染、联网） | [引用](/ref/Game.Arpg) |
| `SDL3` | SDL3 绑定：`Sdl` `SdlWindow` `SdlRenderer` `SdlTexture` `SdlAudio` `SdlEvent` `SdlGamepad` `SdlTouch` `SdlGpu` | [引用](/ref/SDL3) |
| `Platform` | `Runtime.GetPlatform()`（编译期目标） | [引用](/ref/Platform) |
| `Sdk.Jd` / `Sdk.Wechat` | 平台业务 SDK：京东联盟（`Api/*` `Domain/*` `JdClient/JdSign`）、微信生态（`Mp/Open/Work/TenPay/WxOpen`、`WechatClient`、`WXBizMsgCrypt`） | [引用](/ref/Sdk.Jd) · [引用](/ref/Sdk.Wechat) |

## 语言内建类型（无需 using）

以下类型由编译器内建（`src/compiler/builtin_api.c`），与 stdlib `System.*`
并存：

| 类型 | 关键成员 |
|---|---|
| `string` | `Length` `Substring` `IndexOf/LastIndexOf` `Contains` `StartsWith/EndsWith` `Replace` `Trim` `ToUpper/Lower` `Split` |
| `List<T>` | `Count` `Add` `AddRange` `Insert` `RemoveAt` `Clear` `Contains` `IndexOf/LastIndexOf` `Reverse` `[i]`；扩展：`Remove` `GetRange` `Sort`（`stdlib/System/ListExtensions.zan`，`Sort` 仅 int/double/string 三版） |
| `Dictionary<K,V>`（等义 `Dict`） | `Count` `Keys` `Values` `Add` `Remove` `Clear` `ContainsKey` `TryGetValue(k, out V)` |
| `StringBuilder` | `Append(object)` `AppendLine(object)` `Length` `ToString()` |
| `Console` | `WriteLine/Write/PrintLine` `ReadLine/Read/ReadKey` `Clear` `ResetColor` `ForegroundColor/BackgroundColor` `Title` |
| `Math` | `Abs` `Max` `Min` `Pow` `Sqrt` `Round` `Floor` `Ceiling` `Sin` `Cos` `Tan` `Log` `Exp` `PI` |
| `Convert` | `ToDouble` `ToInt32` `ToInt64` |
| `String` | `Format(fmt, params object[])` `Join(sep, List<string>)` `IsNullOrEmpty` `CompareOrdinal` |
| `File` / `Directory` / `Path` | 基础文件操作（stdlib `System.IO` 提供更多重载与异步版本） |
| `Environment` | `ArgCount()` `ArgAt(i)` `ExeDir()` |
| `NativeMemory` | `Alloc/Free/Copy/Fill/Compare/GetString/PutString` |
| `Task` | `Spawn` `Run` `Delay` `WhenAll` `WhenAny` `IsDone` `Cancel` `IsCancellationRequested` |

内置类型成员不全时优先用 stdlib 扩展（`StringExtensions.zan` 提供
`PadLeft/PadRight/Reverse/CharAt` 等；`ListExtensions` 提供
`Remove/GetRange/Sort`）。

## 常用速查

### 控制台 + 文件

```zan
Console.WriteLine("hello");
string s = File.ReadAllText("a.txt");
File.WriteAllText("b.txt", s);
File.AppendAllText("log.txt", "line\n");
if (File.Exists("c.txt")) { File.Copy("c.txt", "d.txt"); }
Directory.CreateDirectoryRecursive("a/b/c");
List<string> lines = File.ReadAllLines("data.txt");
```

### JSON

```zan
JsonValue v = JsonValue.Parse(raw);
v.AsString(); v.AsInt(); v.AsBool(); v.AsDouble();
v.Get("key"); v.Set("key", JsonValue.NewStr("x"));
string json = v.ToJson();
JsonValue arr = v.Get("items").At(0);        // 数组访问
```

### HTTP 客户端

```zan
HttpClient c = new HttpClient("api.example.com", 443);   // 或 HttpClient.CreateHttps(host,port)
string body = await c.GetAsync("/v1/users");
string post = await c.PostAsync("/v1/login", "{\"u\":\"a\"}");
HttpClient.GetAsync("h", 80, "/")            // 静态快捷
```

### HTTP 服务器

```zan
HttpServer server = new HttpServer("0.0.0.0", 8080);
server.OnRequest(async (HttpRequest req) => HttpResponse.Json("{\"ok\":true}"));
await server.Start();
```

server-mvc 请用 `System.Web`（`WebApp` + 属性路由 `[HttpPost("/api/x")]` +
`Controller`/`Router`/`HttpContext`，见 [System.Web](/ref/System.Web)）。

### 数据库

```zan
SqliteConnection db = SqliteConnection.Open("app.db");
int rows = db.Execute("create table t(id int, name text)");
DbResult r = db.Query("select id, name from t where id = ?", new DbParams().Add(1));
long n = r.RowCount; string name = r.GetString("name");
```

ORM（`System.Data.Orm`）：`Model.Define("users").Column("id", "int", true).CreateTable(db);
Model.Select(db).WhereEq("name", "x").ExecuteAsync();` 详见
[System.Data](/ref/System.Data) 与 [System.Data.Orm](/ref/System.Data.Orm)。

### 加密与编码

```zan
string hex = Sha256.Hash("hello", 5).Hex();   // 或 hash 后 ToHex()
byte[] aes = Aes.EncryptCbc(key, keyLen, iv, ivLen, data, dataLen);
string b64 = Base64.Encode(data);
string url = Encoding.UrlEncode("中文");
```

### 线程 / 通道

```zan
Thread.Start(() => Loop());
await Thread.SleepAsync(50);
Channel ch = new Channel(8);
ch.Send("task"); string s = await ch.Receive();
```

## 平台与驱动

- 跨平台代码用条件编译：`#if WINDOWS` / `#elif LINUX` / `#elif MACOS`（预定义
  符号见「语言参考 → 预处理」）。
- 原生依赖（`libcrypto`、`zan_gui`、`zan_sdl3`、`lua`/`python` 驱动、CEF、
  Tls 等）位于模块的 `drivers/<target>/`，`--publish` 时按
  `--link-mode shared|static` 链接。
- `System.Scripting` / `Gui.Component.CefBrowser` / `System.Net.Tls` /
  `System.Net.WebSocket.Secure` 等依赖特定平台驱动，无驱动平台会退化或报
  `PlatformNotSupportedException`。

## 更多

- 每个命名空间页从左下侧栏进入；`/ref/index.json` 是全量索引
  （命名空间 → 类型清单 + 文件列表），便于程序化检索。
- 完整语言语义见「语言参考」，GUI 开发见「GUI 指南」；
- **可以直接抄的完整代码**见「代码示例库」[cookbook](/cookbook)。
