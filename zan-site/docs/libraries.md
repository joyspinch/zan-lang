# 标准库与扩展库：从需求找到可用能力

核实日期：2026-11-11。本文依据当前标准库、包清单、相关 README 与关键源码整理。具体发行包是否包含某个包、驱动或目标支持，需要在本机 SDK 中确认；下列能力介绍不代表所有模块已经完成生产环境验收。

完整名单、包描述和命名空间请查[包目录](packages.md)。本页侧重如何选择、导入和验证库，不重复维护一个容易过时的包数量。

## 先区分三层能力

| 层次 | 由谁提供 | 典型内容 |
| --- | --- | --- |
| 编译器内建 | 编译器与运行时 | 基础类型、字符串成员、`Console`、`List<T>`、字典、`StringBuilder` |
| 标准库 | SDK 随附的 `.zan` 源码 | 文件与路径、时间、编码、基础集合、线程与同步原语 |
| 扩展包 | 可发现的独立包源码与必要驱动 | GUI、网络、数据库、JSON、Web、AI、游戏和领域功能 |

标准库随用户代码编译，无需另装一个托管运行时。扩展包也可按命名空间参与编译，带原生依赖的包还需要目标平台对应的库。

许多扩展包从早期标准库拆出时保留了 `System.*` 命名空间。因此，名字以 `System` 开头并不表示文件一定在 `stdlib/`，也不表示它属于 .NET。

编译器内建能力可能没有对应 `.zan` 文件。找不到 `Console.zan`、`StringBuilder.zan` 或字符串方法源码时，应查询 API 索引，并核对 `src/compiler/builtin_api.c` 等实现；不能仅凭标准库 grep 无结果就声明 API 不存在。

## 包名、安装位置和命名空间是三件事

| 包或来源 | 已核对的命名空间例子 | 用途 |
| --- | --- | --- |
| 标准库 | `System.IO` | 文件、目录、路径与流 |
| `Zan.Net` | `System.Net.Http.Client` | `HttpClient` |
| `Zan.Json` | `System.Json` | JSON 值、读取、序列化与契约 |
| `Zan.Formats` | `Formats.Csv` | CSV 读写 |
| `Zan.Xml` | `System.Xml` | XML DOM |
| `Zan.Gui` | `Gui`、`Gui.Widget`、`Gui.Markup` | 应用宿主、控件与标记解析 |
| `Zan.Web` | `System.Web` | Web 应用与路由 |
| `Zan.Ai` | `Ai` | 模型客户端与检索管线 |
| `Zan.Game.Cards` | `Game.Cards` | 卡牌与牌堆规则 |

导入语句写真实声明，不按包名或目录层级猜测。下面只是导入片段，需要放进拥有入口与实际业务代码的项目：

```zan-fragment
using System.IO;
using System.Net.Http.Client;
using System.Json;
using Gui;
using Gui.Widget;
```

`using` 使命名空间参与解析，不下载包。`using System.Net.Http;` 与 `using System.Net.Http.Client;` 也不能互相替代，使用具体类型前应检查文件的 `namespace` 行。

## 查找、安装、编译的正确顺序

1. 用[包目录](packages.md)按场景找候选；有 MCP 时可用 `pkg_search(query)` 搜关键词或命名空间。
2. 用 `pkg_info(name)`、包的 `zan.pkg` 和 README 核对版本、许可证、命名空间与依赖。
3. 用 `pkg_list_installed` 查看项目安装记录；还要检查 SDK 自带或项目其他可发现来源，列表为空不代表编译器看不到任何包。
4. 确实缺包时使用当前 SDK 提供的安装方式；MCP 的 `pkg_install(name, scope)` 支持项目或全局作用域，检查实际安装结果。
5. 查真实 API，写明所需 `using`，把应用自身全部 `.zan` 源文件交给编译器。
6. 编译后实际运行，验证原生依赖与目标平台上的行为。

当前可发现来源包括项目 `packages/`、项目 `.zan-packages/` 和工具链附近的包存储。安装包与维护仓库里的 `packages/` 源码目录不是同一种交付物；不要把维护仓库可见的包自动当作所有 SDK 已安装的内容。

`--auto-stdlib` 当前默认开启，也负责包发现；它不会替单入口自动枚举应用的兄弟源码。多文件构建、目录输入与响应文件见[工具链](tooling.md)。

## 命令行、文件与数据交换

文件工具可以先用标准库 `System.IO` 读写文件、处理路径和流，用 `System.Text` 处理编码，再按需求增加扩展包。

| 需求 | 候选能力 | 需要核对的边界 |
| --- | --- | --- |
| 终端表格、进度与提示 | `Zan.Cli` | 终端颜色支持、文本显示宽度与交互方式 |
| JSON 文档与校验 | `Zan.Json` | Schema 为声明的支持子集；JSONPath 与 Patch 各有规则 |
| CSV、YAML、TOML、二进制交换 | `Zan.Formats` | 分格式选择命名空间；Protobuf 提供基础 wire 能力 |
| XML 读写与树导航 | `Zan.Xml` | 不提供完整 XPath、XSLT 或 Schema/DTD 校验 |
| 表格计算与聚合 | `Zan.DataFrame` | 列类型、空值、数据规模与计算结果类型 |
| 目录监视、映射文件与压缩 | `Zan.IO` | 操作系统、格式支持与资源关闭方式 |

当前 CSV、YAML、TOML、BSON、CBOR、MessagePack 和 Protobuf wire 归入 `Zan.Formats`；JSON 与 XML 分别属于独立包。不要沿用旧清单把每种格式都当成单独安装包。

异步方法的名字不能代替实现核对。例如当前部分文件异步方法先让出调度，再调用同步文件操作；选择它们时还要评估调用线程、阻塞耗时和并发需求。异步模型见[异步与并发](async.md)。

## 桌面 GUI 与 HTML/CSS 窗口

`Zan.Gui` 提供保留式控件树、窗口宿主、事件、布局和样式。可以直接组合控件，也可以把 HTML/CSS 声明投影成控件树；业务界面优先复用标准控件及扩展组件。

HTML/CSS 通道是 Zan 的 UI 声明支持子集，不是完整浏览器。它具有自己的标签映射、布局、样式和数据绑定协议；脚本标签不会为窗口启动通用 JavaScript 引擎，未映射标签也不保证有浏览器同名控件的交互行为。

当前源码中的装载入口是 `Gui.Html.Load`。下面是需已有 `App`、HTML 文本、处理器表和资源基目录的片段：

```zan-fragment
Control root = Html.Load(app, html, handlers, baseDir);
```

其完整重载为 `public static Control Load(App app, string html, HtmlHandlers handlers, string baseDir)`；也有只接受 `App` 和 HTML 文本的重载。旧材料中的 `App.LoadHtml` 写法应按所用 SDK 版本核对。

HTML 在运行时装载；当前 `zanc` 不接受 `.html`/`.htm` 作为编译入口。发布时可以用 `--embed` 把标记与资源嵌入产物，然后通过装载入口读取。

图表、数据表格、代码编辑器和设计器分别由 `Zan.Gui.Charts`、`Zan.Gui.DataTable`、`Zan.Gui.CodeEditor`、`Zan.Gui.Designer` 提供。选择时检查其实际控件、数据模型与事件接口，不把包名称当作对其他 UI 框架的完整兼容承诺。

需要运行网页和 JavaScript 时，另查 `Zan.Gui.Browser` 等浏览器集成包。系统 WebView 与 CEF 路径有各自原生驱动和运行时依赖，不能把 Zan HTML 控件投影与浏览器嵌入混为同一功能。

## 网络、服务端与后台任务

`Zan.Net` 提供 Socket/TCP/UDP、TLS、HTTP 客户端与服务端相关能力，以及 WebSocket、SSE、MQTT 等协议模块。HTTP、TLS、下载与代理有不同命名空间和数据流接口，调用前查具体类。

`Zan.Web` 提供 `System.Web` 下的 Web 应用、路由、请求上下文、控制器、静态文件、会话与相关安全组件。业务服务还可以组合 `Zan.Configuration` 管理配置、`Zan.Hosting` 管理后台服务生命周期。

`Zan.Mvc` 是建立在这些能力上的应用框架与管理台；其当前 README 明确标为“迁移中”，并提示迁移与用例补齐前不建议新项目采用。应把底层网络能力、Web 内核与更高层框架成熟度分别评估。

一个服务的验收至少覆盖路由、输入验证、错误返回、连接关闭和并发行为。远程访问还需核对证书验证、超时、取消与响应状态；“客户端有方法”不等于全部对端和协议扩展都已互操作通过。

## 数据库与持久化

`Zan.Data` 包含连接与执行接口、连接池、ORM、数据库连接器、Redis、嵌入式 ZanDb 和表格文件相关能力。

| 选择 | 当前提供的路径 | 使用前关注 |
| --- | --- | --- |
| SQLite | `System.Data.Sqlite`，调用 sqlite3 C 库 | 目标原生库、事务、连接与语句生命周期 |
| MySQL / PostgreSQL / SQL Server | 对应协议连接器 | 服务端版本、认证、TLS、参数绑定和事务语义 |
| ODBC | 通用连接器路径 | 系统 ODBC 与具体驱动的安装 |
| ZanDb | `System.Data.ZanDb`，Zan 实现的嵌入式存储 | 持久性模式、恢复、索引与并发访问 |
| ORM | `System.Data.Orm` | SQL 下推、映射、关联加载与数据库方言 |

ORM 的泛型查询入口由扩展方法提供，不能只查连接接口成员就判断不存在。显式委托条件可能在客户端过滤：当前实现先取 SQL 候选集，再过滤和分页，内存与网络开销取决于候选规模。

异步终结操作会调用执行器的异步接口，但是否在数据库 I/O 处让出、哪些步骤卸载到阻塞线程池，需要逐驱动检查。真实数据库验收还应覆盖失败、取消、回滚和资源关闭。

## 在应用里使用 AI

`Zan.Ai` 的 `Ai` 命名空间提供模型客户端、统一对话契约、流式事件、Embeddings、文档分块、RAG、重排、会话历史与工具调用循环。它不依赖把应用做成 Web 服务，桌面、CLI 或服务端均可按需组合。

检索有不同存储形态：内存 `VectorStore` 与 `Retriever` 适合小规模或临时数据；`ZanDbRetriever` 提供本地持久化路径；`RemoteZanDbRetriever` 连接远端存储。选择时确认索引与检索使用同一后端、向量维度匹配，并核对恢复与并发语义。

模型服务地址、凭据和模型名来自应用配置。验证流式分片、工具参数、错误状态与成本边界；应用包中存在客户端并不等于某个模型服务已经可用。编程助手接入另见[AI 协作开发](ai.md)。

## 游戏与可复用玩法

`Zan.Game` 的基础模块包含固定步长、可复现随机数、输入、计时器与场景生命周期；GUI 宿主集成单独分层，便于让逻辑在无窗口仿真或服务器中运行。

棋盘、卡牌和 ARPG 等能力还分布在 `Zan.Game.Board`、`Zan.Game.Cards`、`Zan.Game.Arpg` 等包。应从与玩法匹配的示例出发，把共享机制留在库中，把规则、关卡、资源与美术组织留在游戏项目中。

无窗口逻辑验证与实机体验验证相互补充。固定步长结果、输入响应、存档恢复、资源释放和目标设备上的帧耗时，应分别检查；ARC 不意味着热循环分配没有成本。

## 原生依赖、许可证与采用标准

包可以同时含 Zan 源码和原生驱动。原生目录通常按目标组织，并通过 manifest 参与发现与发布；`--publish` 默认共享链接并装运需要的驱动，有静态归档时才可选择相应静态路径。

GUI 驱动、SQLite、系统 WebView 和 CEF 的交付条件各不相同。能编译某个目标、不缺链接符号、能启动窗口和真实设备验收通过，是不同阶段；详细说明见[原生互操作](interop.md)与[平台与发布](platforms.md)。

采用一个包前核对其清单、源码、示例、测试范围、目标依赖与许可证。部分包明确带有实验或迁移状态；例如 `Zan.Commercial` 清单标为实验性且使用专有许可证。不要把统一目录或统一命名当作统一成熟度与授权范围。

## 核实来源

以下为维护仓库本地路径；具体包的最新声明与清单应优先于旧目录概览。

- `docs/STDLIB.md`、`docs/ZANC_CLI.md`、`src/compiler/builtin_api.c`：标准库、内建能力与装运规则。
- `packages/Zan.Net/README.md`、`packages/Zan.Net/src/System/Net/Http/Client/HttpClient.zan`：网络能力与命名空间。
- `packages/Zan.Formats/zan.pkg`、`packages/Zan.Formats/README.md`、`packages/Zan.Json/README.md`、`packages/Zan.Xml/README.md`：格式包归属与支持范围。
- `packages/Zan.Gui/README.md`、`packages/Zan.Gui/src/Gui/Markup/Html.zan`、`packages/Zan.Gui/src/Gui/Markup/HtmlParser.zan`、`packages/Zan.Gui.Browser/README.md`：GUI 与 HTML/浏览器边界。
- `packages/Zan.Web/README.md`、`packages/Zan.Mvc/README.md`、`packages/Zan.Hosting/zan.pkg`：服务端能力与成熟度。
- `packages/Zan.Data/README.md`、`packages/Zan.Data/src/System/Data/Sqlite/SqliteConnection.zan`：持久化、ORM 与原生依赖。
- `packages/Zan.Ai/README.md`、`packages/Zan.Game/src/Game/Foundation/README.md`、`packages/Zan.Game.Cards/zan.pkg`：AI 和游戏能力。
- `stdlib/System/IO/File.zan`、`packages/Zan.Commercial/zan.pkg`：文件异步实现与许可证边界。
