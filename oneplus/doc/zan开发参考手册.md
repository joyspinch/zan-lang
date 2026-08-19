# zan 语言开发参考手册

> 基于对 `D:/project/zan-lang` 源码的逐层核实（2026-08-06），供"群爆款优化神器"zan 跨平台迁移使用。
> 每个 API 都标注了证据文件路径，可直接回查源码。

---

## 0. 一分钟速览

| 项目 | 内容 |
|---|---|
| 语言定位 | C# 语法、LLVM 后端、ARC 内存管理、AOT 原生编译 |
| 编译器 | `zanc`（C11 实现，源码 `src/compiler/`，依赖 LLVM 17+） |
| 标准库 | 仓库内 `stdlib/`（`--auto-stdlib` 自动携带） |
| GUI | 自绘 Canvas（软件渲染）+ 原生窗口外壳，`stdlib/Gui/` |
| WebView | Windows = Edge WebView2（COM 直驱），macOS = WKWebView |
| 平台 | Windows / macOS / Linux，`#if WINDOWS / #elif MACOS` 条件编译 |
| 关键缺口 | **客户端无 Cookie 设施**、无 `Task.WhenAll`/`Task.Run`（详见 §11） |

---

## 1. 构建与项目

### 1.1 编译命令

```bash
# 编译编译器本体（一次性）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                # 产出 zanc / zan-lsp / zan-dap

# 编译 zan 程序
build/zanc <file.zan> --auto-stdlib -o out.exe              # 开发版
build/zanc <file.zan> --auto-stdlib --publish -o out.exe    # 发布版
```

### 1.2 zanc 主要参数（`src/compiler/main.c` print_usage:1242）

| 参数 | 作用 |
|---|---|
| `-o <file>` | 输出文件 |
| `--auto-stdlib` | 自动携带 stdlib |
| `--publish` | 发布构建 |
| `--subsystem <console\|windows>` | PE 子系统；**GUI 程序用 `windows`** |
| `--target <win-x64\|win-arm64\|linux-x64\|macos-arm64\|...>` | 交叉编译目标；`--list-targets` 列出 |
| `--link-mode <shared\|static>` | 发布时驱动/TLS 库的链接方式 |
| `--emit-ir` / `--dump-ast` | 调试：输出 LLVM IR / AST |
| `--check-leaks` | ARC 泄漏检查 |
| `--no-runtime-checks` | 关闭运行时检查 |
| `-O0..-O3/-Os/-Oz`、`-g` | 优化与调试信息 |
| `-L <dir>` / `--link-lib <lib>` / `--link-input <file>` | 额外链接 |
| `--icon <file>` | Windows 程序图标 |
| `-D<name>` | 条件编译宏 |
| `--package-install/--package-api/--package-project` | 包管理 |
| 多个输入文件 | 支持（main.c:99 起） |

### 1.3 项目/包文件

- **`zan.proj`** — 项目清单，`key = value` 格式（例 `templates/server/server-iot/zan.proj`）：
  ```
  name = myapp
  type = gui          # console | gui | library | server
  entry = src/main.zan
  target = exe
  platform = windows-x64
  ```
  编译器向上查找该文件定位项目根（main.c:491）。
- **`zan.pkg`** — 包清单；**`zan.lock`** — 锁文件；**`.zan-packages/`** — 本地缓存。依赖约束：`=1.2.3` / `^1.2.3` / `>=1.2.3`（`src/compiler/package.h:37`）。
- `templates/` 下有 console/gui/library/server 的新建项目模板。
- 测试：`scripts/test.ps1 <smoke|standard|full>`。

### 1.4 入口点约定（无顶层语句）

```csharp
static void Main()                     // 标准
static void Main(string[] args)        // 带参数
static async void Main()               // 异步入口，调度器驱动
static async int Main()                // 异步 + 返回码
```

命令行参数：`Environment.ArgCount()` / `Environment.ArgAt(i)`（`src/compiler/builtin_api.c:129`）；另有 `Environment.ExeDir()`。

---

## 2. 语言核心速记

- **async/await 一等特性**：编译器 CPS 变换（`src/compiler/irgen_async.c`，设计文档 `docs/ASYNC_CPS_DESIGN.md`）。
- **条件编译**（`stdlib/Platform/Runtime.zan:11` 实例归纳）：
  ```csharp
  #if WINDOWS
  ...
  #elif MACOS
  ...
  #else
  ...
  #endif
  ```
  运行时判断：`Platform.Runtime.IsWindows()/IsLinux()/IsMacOS()`（编译期常量化）。
- **string 内置成员**（`builtin_api.c:14`）：`Length`、`Substring(start[,len])`、`IndexOf`、`LastIndexOf`、`Contains`、`StartsWith`、`EndsWith`、`Replace`、`Trim`、`ToUpper`/`ToLower`、`Split(sep) -> List<string>`。⚠️ `s[i]` 是**字节**索引（常见写法 `s[i] & 255`）。
- 内置还有 `StringBuilder`、`Convert`（`ToInt/ToString/...`）。
- 扩展：`stdlib/System/StringExtensions.zan`、编码 `System/Text/Encoding.zan`、正则 `System/Text/RegularExpressions/Regex.zan`。

---

## 3. HTTP 客户端（`stdlib/System/Net/Http/Client/HttpClient.zan`，1078 行）

```csharp
HttpClient cli = new HttpClient("127.0.0.1", PORT);    // HTTP
HttpClient tls = HttpClient.CreateHttps(host, 443);    // HTTPS（OpenSSL 3）

cli.SetHeader("X-Api-Key", "...");     // 默认头，附加到每个请求
cli.SetTimeout(5000);                  // 毫秒；0 = 禁用

string body  = await cli.GetAsync("/path");
string body2 = await cli.PostAsync("/path", payload);
await cli.PutAsync(...);  await cli.DeleteAsync(...);

// 需要状态码/响应头时：
HttpResponse resp = await cli.SendAsync("GET", "/x", "");
// resp.statusCode / resp.statusText / resp.body / resp.contentType
// resp.headers : List<string>，每项 "Name: value"
```

| 能力 | API |
|---|---|
| 一次性静态调用 | `HttpClient.GetAsync(host, port, path)` / `PostAsync` / `GetHttpsAsync` |
| multipart 上传 | `UploadFileAsync`（:864） |
| 断点续传下载 | `DownloadRangeToFileAsync`（:904） |
| SSE 流式 | `PostSseToFileAsync` / `PostSseToSinkAsync`（:524/:536） |
| 双向 TLS | `SetClientCertificate(cert, key)`（:129） |
| 公钥固定 | `PinPublicKey(spkiSha256Base64)`（:99） |
| 跳过证书校验 | `DisableTlsVerify()`（仅开发用） |

- TLS：OpenSSL 3，`TlsStream.zan` 经 `[DllImport("ssl")]` 绑定；驱动二进制随仓库分发 `stdlib/System/Net/Tls/drivers/{win-x64,linux-x64,linux-arm64}`，`--publish` 时按 `--link-mode` 拷贝或静态链接。默认校验证书。
- ⚠️ 每次请求新连接（`Connection: close`），无连接复用。
- 示例：`examples/net/http_client.zan`（GET/POST/PUT/DELETE + SetHeader + JSON 解析，文件头有编译命令）。

## 4. Cookie —— 关键缺口与对策

**现状（已全仓库核实）**：
- 无 CookieJar / CookieContainer / 任何客户端 cookie 持久化；`HttpClient` 完全不解析、不存储 `Set-Cookie`（类注释明确"客户端不缓存上次响应的任何内容"）。
- `Set-Cookie` 全仓库仅 1 处，在**服务端** `stdlib/System/Web/HttpContext.zan:199`。

**两条对策路径（迁移时二选一/组合）**：
1. **HTTP 层手工实现 CookieJar**：`SendAsync` 拿 `resp.headers` → 解析 `Set-Cookie` → 按 domain/path 存储 → 下次请求 `SetHeader("Cookie", ...)` 回发。这是我们 P0 要自己写的。
2. **WebView.GetCookies 直取浏览器 Cookie**（zan 独有优势）：`stdlib/Gui/Component/WebView/WebView.zan` 提供 `GetCookies(forUrl)`（返回 `"name=value; ..."`）、`SetCookie(url,name,value)`、`ClearCookies()`。C# 老工具靠 WebView2 DevTools 协议嗅探 cookie，**zan 里一个 API 直接拿到**——登录态获取比 C# 版更简单。

---

## 5. SQLite（`stdlib/System/Data/Sqlite/SqliteConnection.zan`）

`[DllImport]` 直绑 sqlite3 C API（sqlite3_open/exec/prepare_v2/step/bind_*），原生库按平台内置于 `drivers/`（win-x64 `libsqlite3-0.dll` / linux `.so` / macOS `.dylib` / 各平台 static `.a`）。

```csharp
SqliteConnection db = SqliteConnection.Open(path);   // 不存在则创建
db.Execute("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)");
db.Execute("INSERT INTO t (name) VALUES (?)", new DbParams().Add("hello"));

DbResult r = db.Query("SELECT * FROM t WHERE id > ?", new DbParams().AddInt(0));
for (int i = 0; i < r.RowCount(); i = i + 1) {
    string name = r.GetByName(i, "name");
    bool nil = r.IsNullByName(i, "name");
}

db.ExecuteScalar(sql);  db.LastInsertId();
db.BeginTransaction();  db.Commit();  db.Rollback();
db.Close();

// 协程感知连接池：
SqlitePool pool = new SqlitePool(path, 4);
SqliteConnection c = await pool.AcquireAsync();
pool.Release(c);  pool.Close();
```

高层设施：ORM `System.Data.Orm`（示例 `examples/db/orm_model.zan`、`orm_querybuilder.zan`）、文档库 ZanDb（`examples/db/zandb_documents.zan`）。基础示例 `examples/db/sqlite_crud.zan`。

---

## 6. 并发（async/await + 线程）

### 6.1 Task（编译器内置，`builtin_api.c:166`）

```csharp
Task.Spawn(() => { ... });            // 启动协程，返回任务句柄
await Task.Delay(ms);                 // 特判支持
Task.Cancel(handle);                  // 取消
Task.IsCancellationRequested();
```

⚠️ **没有 `Task.WhenAll`、没有 `Task.Run`** —— `docs/CONCURRENCY.md` 里写的是设计目标，未实现。并发扇出模式 = `Task.Spawn` + 共享计数器 + `await Task.Delay` 轮询，或用 `AsyncGate`。参考 `tests/conformance/async_concurrent_echo.zan`、`async_main_spawn.zan`、`async_cancel.zan`。

### 6.2 线程与同步原语（`stdlib/System/Threading/`）

| API | 说明 |
|---|---|
| `Thread.Start(ThreadStart)` | 分离后台线程（⚠️ 委托必须**非捕获**） |
| `Thread.Sleep(ms)` / `Thread.CurrentId()` | |
| `Mutex.Create/Lock/Unlock/Destroy` | |
| `Semaphore` | Win32/pthreads/dispatch 实现 |
| `SemaphoreSlim`、`Gate`、`AsyncGate`、`AsyncRwLock`、`BlockingQueue` | 协程级原语 |
| `Timer(ms, callback)` | 共享泵线程 ~10ms 唤醒，`Start/Stop/Reset/TickCount` |
| `System/Net/Worker.zan` | 多进程（workerman 风格 `count=N` 分流） |

---

## 7. JSON（`stdlib/System/Json/`）

```csharp
// DOM 风格（JsonValue.zan）
JsonValue v = JsonValue.Parse(src);              // 或 ParseLenient
string s = v.Str("name", "");                    // 类型化取值带默认值
int n = v.Int("count", 0);
JsonValue item = v.At(0);  v.Append(x);          // 数组
v.Set("k", JsonValue.NewStr("x"));  v.Put(...);
string path = v.PathGet("data.list[0].id");      // 路径访问
v.PathSetValue("a.b", ...);
string out = v.ToJson();  string pp = v.PrettyToJson();

// 构造
JsonValue.NewObject() / NewArray() / NewStr / NewNum / NewBool / NewNull

// 快速扫描风格（Json.zan）—— 不建树
JsonParser.GetString(json, "key") / GetInt / GetDouble / GetBool / HasKey / GetValueRaw
JsonBuilder  // 拼 JSON 字符串
```

---

## 8. 文件 / 路径 / 进程

| 类别 | API |
|---|---|
| File（`System/IO/File.zan`） | `ReadAllText/WriteAllText/AppendAllText`（各有 Async 变体）、`Exists/Delete/Move/Copy/GetSize`、`ReadAllLines/WriteAllLines`、`ReadBytes/WriteBytes/WriteAllBytes` |
| 目录 | `Directory.zan`、`DirectoryWatcher.zan` |
| 流 | `FileStream/MemoryStream/StreamReader/StreamWriter` |
| 压缩 | `IO/Compression/{Zip,GZip,Deflate,Tar,Crc32}.zan` |
| Path（`System/IO/Path.zan`） | `GetFileName/GetExtension/GetDirectoryName/Combine/ChangeExtension/HasExtension/GetTempPath/Normalize/Separator` |
| Process（`System/Diagnostics/Process.zan`） | `PipeOpen/PipeClose`（popen）、`ShellOpen/ShellSend/ShellClose`（交互式 shell）、`WinSpawn(cmd, waitFor)`、`WinCapture(cmd) -> List<string>`；另有 `ProcessControl/ProcessHost/ProcessList/Stopwatch` |

---

## 9. GUI 开发

### 9.1 架构总览

自绘（软件渲染 Canvas）+ 原生窗口外壳。核心四件套：

| 类 | 位置 | 角色 |
|---|---|---|
| `App` | `stdlib/Gui/App.zan:31`（2807 行） | 窗口 + 事件循环 + 主题/CSS/自绘标题栏 |
| `Control` | `stdlib/Gui/Control.zan:34`（1348 行） | 所有控件基类 |
| `Window` | `stdlib/Gui/Backend/Native.zan:11` | 原生窗口 extern 绑定 |
| `Canvas` | `stdlib/Gui/Render.zan:26` | 软件渲染画布 |

> 没有 WinForms 式 `Form` 类，`App` 承担其角色。目录：`stdlib/Gui/{Widget(56控件), Component(复合组件), Backend, Hmi, Designer, drivers(预编译原生运行时), skins(16套CSS皮肤)}`。

### 9.2 最小窗口（真实示例 `examples/gui_browser/gui_browser.zan`）

```csharp
using System;
using Gui;
using Gui.Widget;
using Gui.Component;

class Prog {
    static void Main() {
        App app = App.CreateDark("标题", 1100, 760);
        // ... 创建控件、绑定事件 ...
        app.Show();
        WidgetId.Mark();
        while (app.isRunning) {
            if (!app.ProcessEvent()) { break; }   // 事件泵（内部 60fps 节流）
            app.BeginFrame();
            WidgetId.ResetFrame();
            // 每帧绘制：控件各自 Render(app, x, y, w, h)
            app.RenderChrome("标题");              // 自绘标题栏（最小/最大/关闭/主题切换）
            app.PresentFrame();
        }
    }
}
```

懒人版：`app.Run(title)` / `app.RunLoop(() => { ... })`（App.zan:1382），自动 Show + 循环 + 异常保护，仅在 `needsRedraw` 时绘制。

App 常用方法：`ProcessEvent()`(:1989)、`BeginFrame()`(:2530)、`PresentFrame()`(:2394)、`RenderChrome(title)`(:1452)、`RequestRedraw()`(:1636)、**`Post(Action)`**(:1698，后台线程回 UI 线程)、`Scale(n)`（DPI 缩放）、`UseAppCss(css)`（叠加 CSS 规则）、`RequestAnimationFrame(ms)`(:1651)。

事件码（App.ProcessEvent 语义）：1=鼠标移动，2=按下，3=抬起(点击)，4=按键，5/6=字符输入，8=窗口关闭，13=滚轮。

### 9.3 控件清单（`stdlib/Gui/Widget/` 56 个 + Component 复合组件）

**基础控件**：Button、ButtonGroup、Checkbox、Radio、Switch、Slider、Rate、Input（单行）、TextArea、SelectBox（=ComboBox，含 `SelectBox.Multi` 多选）、Label、Typography、**Tabs**（Line/Card/Segment 样式 + 垂直 + 可关闭/添加）、TreeView、ListView（泛型 `ListView<T>`）、**VirtualList**（字符串行虚拟化）、Table、Pagination、Steps、Wizard、Progress、Spin、Skeleton、Badge、Tag、Statistic、Card、Carousel、Collapse、Panel（容器）、ScrollView、ScrollColumn、Split/SplitPanel、Menu、ContextMenu、Dropdown、ToolStrip、Ribbon、StatusBar、Breadcrumb、PageHeader、Timeline、Tooltip、Popover、Layer（弹层/对话框/Toast）、FloatButton、FormField、Prompt、Empty、Result、States、Ellipsis、Avatar、Divider、CodeBlock、BoxContent。

**复合组件（Component/）**：**DataTable**（大表格，见 §9.6）、**WebView**（§9.7）、Chart（图表 + ChartBig 大数据）、CodeEditor、FilePicker（文件对话框）、Dock（IDE 式停靠面板）、LogView、PivotTable、SessionList。

**工控（Hmi/）**：Gauge、Trend、Alarm、Indicator、IoTag、NumPad、EquipPanel。

**皮肤**：`skins/` 16 套 CSS 皮肤（dark/light/neon/brutalism/liquidglass/chinese...），`app.ThemeMenuIndex()` 切换。

### 9.4 事件绑定（三种并存）

```csharp
// 1. C# 风格 +=（主流，UiEvent 定义于 Event.zan:138，op_add/op_sub 重载）
Button save = new Button { Text = "保存", Class = "primary small" };
save.Click += () => { ... };
save.Click += SomeStaticHandler;      // 也支持方法名
save.Disabled = busy;

// 2. 流式 OnXxx（Control 基类 Control.zan:229）
ctl.OnClick(...).OnDoubleClick(...).OnRightClick(...).OnChange(...)
   .OnEnter(...).OnLeave(...).OnWheel(...).OnKeyDown(...).OnKeyUp(...)
   .OnLongPress(...).OnSwipe(...).OnDrag(...).OnDrop(...).OnResize(...);
btn.On.Click += ...;                   // WidgetEvents On 字段

// 3. 即时模式轮询
if (Ui.Clicked(app, id)) { ... }       // 或 button.WasClicked()
```

### 9.5 布局（四种并存）

1. **绝对坐标 + Stack 辅助**（最常用）：控件 `Render(app, x, y, w, h)` 直接给坐标；`Stack.Column(x,y,w,h)` / `Stack.Row(...)` / `Slot(size)` / `Fill()` / `SetGap/SetPad` 分行分列；DPI 用 `app.Scale(n)`。
2. **Dock 停靠**：`Control.dock` 字段（Control.zan:36）：0 手动 / 1 顶 / 2 底 / 3 左 / 4 右 / 5 填充；`prefW/prefH` 首选尺寸；`grow` 填满。
3. **Panel 流式容器**：
   ```csharp
   Panel card = new Panel("card");
   card.Titled("创建账户");
   card.Add(Panel.Column().Gap(app.Scale(10))
       .With(new Label { Text = "填写以下信息", Class = "secondary" })
       .With(Panel.Row().Gap(app.Scale(10))
           .With(new Button { Text = "取消" })
           .With(new Button { Text = "保存", Class = "primary" })));
   loaded.MeasureTree(app);  loaded.Arrange(px, py, w, h);   // 自动测量+停靠
   ```
4. **Flex**：`Layout.zan` 的 `FlexStyle`（Row/Column/SetPadding/SetGap/SetSize/SetGrow/SetJustify/SetAlign）+ `FlexNode.Compute(w,h)`；IDE 式停靠面板 `Component/Dock.zan`（DockHost/DockPanel）。

控件双向绑定模型字段：`input.data = model.someSignal;`、`selectBox.data = model.pick;`；响应式信号 `SignalInt/SignalString/SignalBool`（`Reactive.zan`）。

### 9.6 DataTable —— 迁移的核心控件（`stdlib/Gui/Component/DataTable/`，14 文件）

```csharp
// 小数据：内存行
DataTable.Render(app, x, y, viewW, viewH, cols, rows, st);          // List<DataRow>
// 大数据/远程数据：虚拟化数据源
DataTable.RenderSource(app, x, y, viewW, viewH, cols, src, st);     // DataSource
```

**虚拟化**：不存行，单元格按 (row, col) 按需向 `DataSource` 索取，只渲染可视区。`DataSource` 虚方法（DataTableModel.zan:1116）：

```csharp
class DataSource {
    virtual int RowCount() { ... }
    virtual string CellText(int row, int col) { ... }
    virtual int CellNum(...); virtual double CellReal(...);
    virtual int CellDay(...); virtual bool CellBool(...);
    virtual void SetCell(int row, int col, string v) { }    // 内联编辑写回
    virtual bool CanInsert(); virtual bool InsertRow(int at); virtual bool RemoveRow(int at);
    // ---- 服务端分页/无限滚动协议（对应我们"拉取计划列表"场景）----
    virtual bool ServerControlled() { return false; }       // 源自管排序/过滤
    virtual bool CellReady(int row, int col) { return true; }  // false 显示加载占位
    virtual void RequestBlock(int row) { }                  // 拉取包含 row 的块
    virtual void RequestNextBlock() { }                     // 滚到底继续拉
    virtual bool HasMore() { return false; }
}
```

百万行示例（gui_gallery.zan:4497）：`BigDataSource : DataSource` 按坐标 O(1) 计算单元格，10 万~500 万行 × 70 列流畅。

**列定义**（工厂 + 装饰器链，DataTableModel.zan:10/165-434）：

```csharp
cols.Add(DataColumn.Field(DataColumn.Pin(DataColumn.Text("计划", app.Scale(110))), "name"));
cols.Add(DataColumn.Field(DataColumn.Sum(DataColumn.Editable(
    DataColumn.Money(DataColumn.Numeric("日限额", app.Scale(120)), "¥"))), "budget"));
cols.Add(DataColumn.Field(DataColumn.Dropdown(DataColumn.Text("状态", app.Scale(120)),
    statusOptions, true), "status"));
// 工厂：Text/Numeric/Money/Percent/Date/Bool/Link/TagCol/ProgressCol/BadgeCol/Dropdown/Spinner/Derived
// 装饰器：Pin/PinRight/Editable/Required/MaxLength/Range/Decimals/AsDate/DateFormat/
//         BoolText/Affix/DataBar/ColorScale/HighlightGreater/HighlightLess/Sum/Avg/Min/Max/Count
```

**状态与选择**：`DataTableState`（DataTableModel.zan:728）保存排序/过滤/列宽/列序/滚动/选择/编辑/分页/右键菜单状态；`st.selection = vm.selectedRow;` 双向绑定。

**右键菜单**：
- DataTable **内置**行右键菜单（冻结/分组/汇总/插入/删除，DataTable.Render.zan:1917 起自动 RenderContextMenu）。
- 自定义菜单用 `Widget/ContextMenu.zan:49`：`ContextMenu.Render(app, mx, my, labels)` 返回点中项索引；配合 TreeView 模式 `OnContext(() => { node = tv.ContextRow(); mx = tv.ContextX(); my = tv.ContextY(); })`。

**单元格样式/自绘**：`CellStyler`（DataTableModel.zan:1314）虚方法 `RowBg/RowFg/CellBg/CellFg/PaintCell`；`DetailProvider` 提供主从展开行；`DataGrid.zan` 是泛型包装 `GridColumn<T>/GridSource<T>`。

轻量替代：`VirtualList`（行虚拟化字符串列表，"10 万行开销与一屏相同"）、`ListView<T>`（非虚拟化，多选）。

### 9.7 WebView（`stdlib/Gui/Component/WebView/`）

| 文件 | 角色 |
|---|---|
| `WebView.zan`（280 行） | 跨平台高层控件 |
| `WebView2.zan`（549 行） | **Windows**：COM vtable 直驱 Edge WebView2 |
| `WebViewBackend.zan`（301 行） | extern 桥接 `zan_gui_webview_*`，由原生驱动实现 |

原生驱动：Win = `drivers/win-x64/zan_gui.dll` + `WebView2Loader.dll`；mac = `drivers/macos-arm64|macos-x64/libzan_gui.dylib`（WKWebView，ObjC 源码 `src/runtime/gui_runtime_mac.m`）。无后端时 `IsSupported()` 为 false 并画占位符。

```csharp
WebView web = WebView.CreateWithProfile("");   // ""=共享存储；非空=多账户隔离配置
web.NavComplete += () => { ... };
web.Navigate("https://one.alimama.com");
web.Eval("document.title");                    // 执行 JS 返回字符串
string cookies = web.GetCookies("");           // 全部 cookie，"name=value; ..." 格式
web.SetCookie(url, name, value);  web.ClearCookies();

// 每帧定位（原生视图是浮在画布上的兄弟窗口）：
web.Render(app, x, y, w, h);      // 可见时
web.Hide();                       // 隐藏时
// 其他：Back/Forward/Reload/Stop、CanGoBack/CanGoForward、IsLoading、
//       LastStatus()（HTTP 状态码）、CurrentUrl/CurrentTitle、
//       信号 Url()/Title()、事件 NavStart/TitleChanged/LoadingChanged、LoadHtml
```

> 迁移意义：C# 版嗅探登录态要走 WebView2 DevTools 协议；zan 版 `GetCookies(forUrl)` 一个调用直取，**登录 Cookie 注入 HttpClient 的链路大幅简化**（配合 §4 的自写 CookieJar）。

### 9.8 辅助能力

| 能力 | API |
|---|---|
| 消息框/Toast/模态 | `Layer.Msg(text, type)`、`Layer.Notify(title, type)`、`LayerState.Alert/Confirm/Choose/Prompt` + `state.Open()` + 每帧 `Layer.Render(app, state)`（Widget/Layer.zan） |
| 文件对话框 | `Component/FilePicker.zan`：`BeginOpenFile/BeginOpenFileFilters/BeginFolder/BeginSaveFile/SetFilters/IsOpen/NeedsRedraw`（独立子窗口，同一事件泵驱动） |
| 定时器 | `System/Threading/Timer.zan`：`new Timer(1000, cb); t.Start(); t.Stop();`；动画定时 `App.RequestAnimationFrame(ms)` |
| 托盘 | `System/Windows/TrayIcon.zan`（仅 Windows，其他平台抛异常）：`TrayIcon.Add(icon, tip, cb)`、`SetMenu(items, cb)`、`ShowBalloon`、`Remove` |
| 剪贴板 | `Gui/Backend/Native.zan:606` class Clipboard |
| UI 自动化测试 | `Backend/UiDriver.zan`：环境变量 `ZAN_UI_SCRIPT` 注入合成事件 |
| 可视化设计器 | `Gui/Designer/`（Designer.Form + Inspector） |

### 9.9 GUI 跨平台编译

```bash
# Windows GUI（zan_gui 驱动自动复制到 exe 旁）
zanc app.zan --auto-stdlib --subsystem windows -o app.exe

# 发布 + 静态
zanc app.zan --auto-stdlib --publish --target win-arm64 --link-mode static --subsystem windows -o app.exe

# macOS 交叉链接（链接 libzan_gui.dylib；⚠️ 文档注明 "links but not yet run on real Mac"）
zanc app.zan --auto-stdlib --target macos-arm64 -o app
```

GUI 项目识别：`zan.proj` 的 `type = gui` 或源码含 `using Gui;`。后端矩阵：Win32 / X11 / Cocoa（Wayland 为 stub）。

---

## 10. C# WinForms → zan 概念映射（迁移对照表）

| C# / WinForms | zan | 备注 |
|---|---|---|
| `Form` + `Application.Run` | `App` + `App.Run(title)` / 手写 while 循环 | App = 窗口+循环+主题 |
| `Button.Click += handler` | `Button.Click += handler` | UiEvent，**语法相同** |
| 设计器拖控件 | 代码构造 + `Designer/`（可选） | 每帧 Render 或 Panel 容器 |
| `DataGridView` | `DataTable` + `DataSource` | 虚拟化是默认优势 |
| `ContextMenuStrip` | `ContextMenu.Render(app,x,y,labels)` / DataTable 内置 | 返回点中索引 |
| `TabControl` | `Tabs`（Line/Card/Segment） | |
| `ComboBox` | `SelectBox`（含 `.Multi`） | |
| `TextBox` / `RichTextBox` | `Input` / `TextArea` | |
| `ListView`（虚拟模式） | `VirtualList` / `ListView<T>` | |
| `WebBrowser` / WebView2 控件 | `WebView` 组件 | GetCookies 直取 |
| `HttpClient` + `CookieContainer` | `HttpClient` + **自写 CookieJar** | 关键缺口，P0 |
| `Task.Run` / `Task.WhenAll` | `Task.Spawn` + 计数/`AsyncGate` | 无 WhenAll，需封装 |
| `System.Timers.Timer` | `System/Threading/Timer` | |
| ADO.NET / Dapper + SQLite | `SqliteConnection` / `DbResult` / `SqlitePool` | API 更薄，够用 |
| `JsonConvert` / JObject | `JsonValue` / `JsonParser` | DOM + 快速扫描两套 |
| `NotifyIcon` | `TrayIcon`（仅 Win） | mac 待补 |
| `MessageBox` | `Layer.Alert/Confirm/Prompt` | |
| 文件对话框 | `FilePicker` | |

---

## 11. 已确认缺口清单（迁移必须面对）

| # | 缺口 | 影响 | 对策 | 状态 |
|---|---|---|---|---|
| 1 | 客户端 Cookie 设施（CookieJar/CookieContainer/Set-Cookie 解析） | 所有 alimama 接口会话 | CookieJar（domain/path 规则 + 持久化）；登录态用 `WebView.GetCookies` 直取 | **用户团队已安排实现** |
| 2 | `Task.WhenAll` / `Task.Run` | 批量改价等扇出并发 | 标准库补齐 | **用户团队已安排实现** |
| 3 | HTTP 无连接复用（每请求 `Connection: close`） | 大批量请求吞吐 | 批量接口优先（batchModifyV2 类）；非阻塞 | 性能注意事项 |
| 4 | HttpClient 不自动跟随重定向 | 登录跳转/会话过期检测 | Session 层手动处理 302+Location（恰好是检测会话过期的正确方式，对应 C# 版 LoginOutException 逻辑） | 编码时处理 |
| 5 | HttpClient 不自动解 gzip | 响应压缩 | 不发送 `Accept-Encoding: gzip` 即可；或手动 `Compression.GZip` | 编码时处理 |
| 6 | WebView 无网络响应嗅探（C# 版靠 DevTools 拿 csrfId） | csrfId 获取 | `web.Eval()` 执行页面内同步 XHR 请求 checkAccess.json 取回 csrfId（WebView 已带登录 Cookie），无需原生驱动改动 | 有 workaround |
| 7 | macOS GUI 产物"能链接、未实机验证" | mac 发布 | 先在 Windows 完成全部功能，mac 后置验证 | 计划内 |
| 8 | TrayIcon 仅 Windows | mac 托盘 | 后置 | 计划内 |

### 11.1 已核实具备的硬依赖（2026-08-06，无需补齐）

| 依赖 | zan 设施 | 证据 |
|---|---|---|
| **MD5 签名**（mtop `_m_h5_tk` sign = MD5(token&t&appKey&data)） | `Md5.Hash(msg, len) -> byte[]` | `stdlib/System/Security/Cryptography/Md5.zan` |
| 完整密码学套件 | `Sha1/Sha256/Sha512/Hmac/Base64/Hex/Aes/AesGcm/Rsa/Jwt/Hkdf/Sm2/Sm3/Sm4/RandomNumberGenerator/BigInt` | `stdlib/System/Security/Cryptography/` 全目录 |
| **中文参数 URL 编码** | `Encoding.UrlEncode(text)` / `UrlDecode` | `stdlib/System/Text/Encoding.zan:148/171` |
| **创意缩略图显示** | `Canvas.DrawImage(path,x,y)` / `BlitImage(path,...)`（先下载到本地文件再绘制） | `stdlib/Gui/Render.zan:539/545` |
| **图片 multipart 上传**（创意素材） | `HttpClient.UploadFileAsync` | `HttpClient.zan:864` |
| 限流并发 | `SemaphoreSlim` | `stdlib/System/Threading/SemaphoreSlim.zan` |
| 正则（解析响应） | `Regex` | `stdlib/System/Text/RegularExpressions/Regex.zan` |

---

## 12. 示例索引（可照抄的起点）

| 场景 | 路径 |
|---|---|
| GUI 多标签浏览器（WebView+Tabs+Input） | `examples/gui_browser/gui_browser.zan` |
| GUI 组件全集（5544 行，含 DataTable 百万行/TreeView 右键/Layer/FilePicker） | `examples/gui_gallery/gui_gallery.zan` |
| HTTP 客户端全动词 + JSON | `examples/net/http_client.zan` |
| HTTP 服务端 / WebSocket / SSE / MQTT | `examples/net/http_server.zan`、`websocket_chat.zan`、`sse_events.zan`、`mqtt_pubsub.zan` |
| SQLite CRUD | `examples/db/sqlite_crud.zan` |
| ORM / 查询构造器 / 文档库 | `examples/db/orm_model.zan`、`orm_querybuilder.zan`、`zandb_documents.zan` |
| 真实 HTTPS SDK（async Main + 参数） | `examples/sdk/jd_open_api.zan`、`examples/sdk/wechat_mp.zan` |
| 并发模式 | `tests/conformance/async_concurrent_echo.zan`、`async_main_spawn.zan`、`async_cancel.zan`、`http_client_timeout.zan` |
| 托盘/输入演示 | `examples/input/tray_screen_demo.zan` |

---

## 13. 对本项目（群爆款 zan 迁移）的直接结论

1. **GUI 可行且优势明显**：`DataTable` 虚拟化（百万行）+ 内置右键菜单 + `ServerControlled/RequestBlock` 分页协议，正好对应"计划列表 + 报表数据 + 右键批量操作"的核心场景；`Tabs` 对应多标签页架构。
2. **登录态链路比 C# 版更简单**：`WebView` 登录页 → `GetCookies(forUrl)` 直取 → 注入自写 CookieJar → `HttpClient.SetHeader("Cookie", ...)`。无需 DevTools 嗅探。
3. **csfId/信封中间件**：HttpClient 的 `SetHeader` 默认头机制可直接承载 `_aem_uid/Bx-V/X-Requested-With` 等固定头；信封 body 参数在请求构造层统一注入。
4. **批量并发**：`Task.Spawn` + `SemaphoreSlim`（stdlib 已有）可复刻 C# 版的限流并发；jitter 用 `Task.Delay`。
5. **SQLite 本地缓存**：`SqliteConnection/SqlitePool` 直接替代现有缓存层。
6. **P0 开发顺序**（不变，现更明确）：
   - P0-1 `CookieJar.zan`（Set-Cookie 解析 + domain/path 匹配 + SQLite 持久化）
   - P0-2 `Session.zan`（HttpClient 封装 + 信封中间件 + Cookie 自动附加）
   - P0-3 登录窗口（App + WebView + GetCookies 导入）
   - P1 只读列表（DataTable + ServerControlled 分页）
   - P2 写操作（ReadModifyWrite helper + 状态机）
