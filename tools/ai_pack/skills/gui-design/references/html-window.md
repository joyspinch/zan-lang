# HTML 窗口开发定式（.html 设计稿 = 唯一格式）

窗口的布局/内容/样式进设计稿，交互与数据在 code-behind。编译期
HTML 通道（GenForm/GenHtml）已整体删除——**设计稿不是编译输入**，
zanc 对 .html 显式拒绝；唯一通道是运行期装载（2026-10-31，B-ID44）。

## 唯一建树通道：运行期装载

- **装载**：`Html.Install()` 后 `app.LoadHtmlWith(html, handlers,
  baseDir)`（html 文本可来自文件/网络/字符串，窗口根文档也可用
  `FormBuilder.BuildWith(doc, null)` 投影）。装载器自己把 html 根
  `Dock(Fill)` 挂进宿主——GenHtml 时代"必须补一句 root.DockFill"的
  树根坑已成历史；定制宿主（设计类控件）走
  `FormBuilder.ProjectInto(host, doc)`。
- **发布保护**：设计稿用 `--embed <file>=<别名>` 随 exe 烘入（压缩 +
  加密，运行期 `File.ReadAllText` 内嵌优先、内存解出，**不落盘**、
  不解压临时文件）。
- **窗口属性**：`data-win-title/win-w/win-h/win-chrome/win-center` 等
  是文档键，由入口代码读键后调 `Form.Create`/`SetChromeVisible` 等
  落地（一份手写入口，业务代码不碰建树）。
- **组件零清单扩展**：文档里 `zan-*`/`data-kind` 引用的组件，其类
  自声明 `static void Install()`（内部 `HeavyControls.Register`）即被
  构建期扫描自动收进注册表；设计类视图的半件与注册表由
  生成脚本扫描 .html 自动产出——新增组件不加任何清单。
- **运行时装载 + `ModelBinder` = 主窗口内的数据驱动页面**：文档负责
  结构与样式，code-behind 负责数据与事件，`data-bind`/`data-if`/
  `data-for` 照常生效（不必把页面做成独立子窗口）。

**迁移设计稿 → 运行期文档的三个语义坑（都真踩过）**：
1. **单位数字是 CSS，不是逻辑 px**：`line-height: 72` 是 72 **倍**
   行高（CSS 规范：无单位 = font-size 的倍数），30px 字号配它 =
   3240px 行盒，整棵布局被顶飞。要 72 逻辑像素必须写 `72px`。
   width/height/font-size 同理——搬 CSS 时逐条补单位。
2. **样式单一来源**：骨架样式搬进文档 `<style>` 后，把 skin/appCss
   里的同规则删掉，别留两份（改一处忘另一处）。
3. **`{{NAME}}` 两处同换**：.zan 的主类名与 .html 的根元素
   `id`/`data-win-title` 都落实名；设计稿文件随项目走
   （开发期按相对路径读，发布 `--embed` 烘入）。

**多文档 appCss 的定式（gui-wechat 实机踩过，run10~17；scoping 已自动化）**：
- **文档 `<style>` 随装载自动并进 appCss**：运行期每装载一份文档，
  其 `<style>`（已混入作用域类）自动并入应用样式表，不需要也**不能**
  手工拼（GenHtml 时代"几段 Css 全量拼进一次 UseAppCss"的定式随
  生成类一起退出历史）。`UseAppCss` 仍是皮肤/全局样式的唯一入口，
  但只喂你自己额外的那份。
- **样式 scoping 装载期自动完成**（2026-09 起，gui-wechat 四页"裸
  body 互中"踩坑后的工具链修复）：装载器给文档每个元素混入作用域
  类 `zs-load-<序>`，并把文档 `<style>` 的每个复合块补上该类——同名类
  跨文档互不串，裸 `body` 只作用自己的根，手写唯一 body id 不再是
  必须项（留作 Find/可读性）。`@media`/`@supports` 内层递归，
  `@keyframes`/`@font-face` 原样。**边界**：运行期挂载件（ListView
  行模板等 code-behind 建的控件）不携作用域类，其样式放皮肤层（全
  局通道）——与 Vue 组件样式不泄漏子组件同一取舍。（变换器共用
  System.Web.HtmlScope。）
- **flex 链条上每个"吃满"的环节都要自己声明**：页面根控件挂进 flex
  宿主后，没有声明高度就是零高（dock 时代的 MeasureDocked 不认
  flex 宿主里的 dock=5 子页）——`.page { flex-grow: 1; align-self:
  stretch; }`。反过来，**固定尺寸件要 `flex-shrink: 0`**：同列里
  ListView 报全内容高（几千 px）时，溢出收缩按比例分摊，240px 的
  封面带会被压扁成 95px（朋友圈封面实测）。装饰角/页脚文案这类
  "静态 AddText 元素"没有内在宽度申报，不吃 `flex-grow: 1` 就被
  量成几十 px 折行。
- **`<style>` 注释里别写 `*/` 序列**（如 `.wxfilt*/.wxfavrow*` 这种
  通配写法）：StripComments 按第一个 `*/` 截断注释，注释后半段
  变成裸 CSS 文本、随后的规则被吞进选择器——整段样式静默失效。

## 声明面原语与语法糖（全事件与响应式指令）

在 `.html` 模板中支持 Vue 风格声明式全事件绑定与响应式指令，装载解析期直接落成控件绑定元数据（ModelBinder 每帧消费），彻底避免后台繁琐重复的命令式查找与手动绑定：

### 1. 事件绑定语法糖（支持 `@<event>`、`v-on:<event>`、`z-on:<event>`、`on<event>`）

| 声明语法 | 规范化事件 | 对应 Control / On 槽位 | 说明 |
|---|---|---|---|
| `@click="OnSave"` / `onclick="OnSave"` | `Click` | `ctrl.Click` 或 `ctrl.On.Click` | 单击事件 |
| `@dblclick="OnEdit"` / `ondblclick="OnEdit"` | `DoubleClick` | `ctrl.On.DoubleClick` | 双击事件 |
| `@right-click="OnMenu"` / `@contextmenu="OnMenu"` | `RightClick` | `ctrl.On.RightClick` | 右键单击/上下文菜单事件 |
| `@right-dblclick="OnFastMenu"` | `RightDoubleClick` | `ctrl.On.RightDoubleClick` | 右键双击事件（WidgetEvents 底层已打通） |
| `@change="OnChange"` / `onchange="OnChange"` | `Change` | `ctrl.Change` (Input等特化) 或 `On.Change` | 内容改变事件 |
| `@show="OnOpen"` / `onshow="OnOpen"` | `Show` | `ctrl.Show` 固有事件槽 | 控件显示事件 |
| `@hide="OnClose"` / `onhide="OnClose"` | `Hide` | `ctrl.Hide` 固有事件槽 | 控件隐藏事件 |
| `@resize="OnResize"` / `onsizechange="OnResize"` | `Resize` | `ctrl.Resize` 固有事件槽 | 尺寸变化事件 |
| `@tab-changed="OnTabChange"` / `@tab-change="..."` | `TabChanged` | `tabs.TabChanged` | 标签页切换事件 |
| `@row-click="OnRowClick"` / `@row-dblclick="..."` | `RowClick` / `RowDoubleClick` | `grid.State.RowClick` 等 | 表格行级单击/双击事件 |
| `@cell-click="OnCellClick"` / `@cell-dblclick="..."` | `CellClick` / `CellDoubleClick` | `grid.State.CellClick` 等 | 表格单元格级单击/双击事件 |
| `@col-click="OnColClick"` / `columns[i].click` | `ColumnClick` / `HeaderClick` | `grid.State.HeaderClick` | 表格列头/字段级声明式点击事件 |
| `@item-click="OnItemClick"` / `@item-dblclick="..."` | `ItemClick` / `ItemDoubleClick` | `list.ItemClick` 等 | 列表控件条目点击事件 |
| `@keydown`, `@keyup`, `@focus`, `@blur` | `KeyDown` 等 | `ctrl.On.*` | 键盘输入与焦点通用事件 |

### 2. 数据双向与显隐指令

| 指令 | 解析映射 | 说明 |
|---|---|---|
| `z-model="form.username"` / `v-model="..."` | `ctrl.bindPath = "form.username";` | 表单输入控件（Input、Checkbox 等）双向数据同步 |
| `z-show="form.isVip"` / `v-show="..."` | `ctrl.bindIf = "form.isVip";` | 依布尔表达式路径控制控件显隐（真时显） |
| `z-hide="form.loading"` / `v-hide="..."` | `ctrl.bindIf = "!form.loading";` | 取反显隐控制（真时隐） |
| `z-text="form.title"` / `v-text="..."` | `ctrl.bindPath = "form.title"; ctrl.bindProp = "text";` | 文本单向渲染同步 |

### 3. 底层声明面五件套与硬边界（模型宿主与列表克隆）

| 原语 | 作用 | 硬边界（超了就静默不生效，别猜） |
|---|---|---|
| `data-on-click="名字"` | 事件 → 处理器注册表 | 设计稿只落名字；逻辑在 code-behind |
| `data-arg="字面量"` | 带参事件（列表行共用处理器区分来源） | 只支持字面量，不求值表达式 |
| `data-bind="路径"` | 绑 JsonValue 模型字段（模型宿主每帧双向同步，见下节选路） | Element 缺省绑 text；真控件缺省 value；**只在挂模型宿主生效**（主窗口内的页面用 `ModelBinder` + `ModelHosts` 挂，见下节） |
| `data-if="路径"` | 显隐插拔 | **只认路径真值**（null/假/"false" 为隐），不支持 `a==b` 比较表达式 |
| `<template data-for="路径">` | 数组行克隆 | 嵌套模板不支持；行内 bind/bindIf 以本项为第一作用域、回落根模型 |

坑出处：data-if 的真值语义见 `ModelBinder.Truthy`——想按 `page=="bag"`
切面板的人都在这里撞墙，解法见下节。模板原型子树靠 UA 样式
`template{display:none}` 隐藏；克隆行撤除走文档序表防幽灵占位。

### 4. 声明式事件绑定与控件 ID 约束（坑出处与规范）

- **带声明式事件（`@click` 等）的元素必须显式标注 `id`**：
  - 说明：运行期事件接线按处理器名走、不依赖 `id`；但 code-behind
    要 `Find(id)` 改文案/换图标/取值的控件必须有 id，否则拿不到引用。
  - 正确姿势：需要交互响应或代码访问的按钮、卡片、输入框，一律显式
    设置 `id="winBtnClose"`、`id="navBtnHome"` 等语义 ID。
- **顶栏窗口控制按钮图标必须使用标准语义名**：
  - 踩坑：使用 `icon="square"`（试图画方形最大化框）或 `icon="chevron-down"`（试图画菜单）会导致回退字体渲染失败——`Icon.Codepoint` 只收录 `minimize` (0xE921)、`maximize` (0xE922)、`restore` (0xE923)、`close` (0xE8BB)、`menu` (0xE700) 等语义名。非标准名称在未命中 Tabler 矢量 SVG 缓存时会退化为空白或问号。
  - 正确姿势：窗口系统按钮严格使用 `icon="menu"`、`icon="minimize"`、`icon="maximize"`、`icon="close"`。
- **无边框自绘标题栏拖动区高度与右上角按钮客户区保护带（避让防误触）**：
  - 踩坑：`data-win-chrome="false"` 声明下，系统外壳会将原生标题按钮计数清零（`capCount = 0`）。在 Win32 `WM_NCHITTEST` 中，若未显式登记保护带（默认 `zoneWidth = 0`），整个顶部标题栏（`cy >= 0 && cy < titlebarH`）横跨全部窗口宽度的物理点击都会直接判定为 `HTCAPTION (2)` 拖动窗口！此时右上角的自定义功能按钮（菜单、最小化、最大化、关闭等）将永远无法接收到物理鼠标点击与悬停事件（`WM_LBUTTONDOWN` / `WM_MOUSEMOVE`），表现为“右上角功能按钮全部失效/无响应”。
  - 正确姿势：在 `OnLoad` 初始同时调用 `form.GetApp().SetTitlebarHeight(height)`（如 38px）与 `form.GetApp().SetCaptionZone(form.GetApp().Scale(btnStripWidth))`（如 160~180px）。将右上角按钮条带区域显式登记为客户区保护带，使 Win32 `WM_NCHITTEST` 在该区域返回 `HTCLIENT (1)`，确保自定义按钮正常接收物理点击与鼠标交互，而其余顶部条带正常维持窗口拖拽。

## 动态绑定：改模型，还是改树（选路）

绑定通道是**模型宿主**（`Gui.ModelBinder`）的每帧同步：帧前模型→UI
（`SyncFromModel`，值变才写）、帧后 UI→模型（`SyncChangedNode`，
快照变了就回写，**没有来源过滤**——程序化 `SetProp` 同样落模型）。
宿主有两种挂法：

- **子窗口**：`ChildWindow.SetRoot(tree, model)` + `Wire()`，子窗口
  自己的渲染循环每帧驱动（`ChildWindow` 内部持有一个 ModelBinder）。
- **主窗口里的页面**：`new ModelBinder()` → `SetRoot(tree, model)` →
  `Wire()` → `ModelHosts.Register(app, binder)`；主窗口的 `Form` 帧
  循环在测量前 `ModelHosts.SyncAll()`、渲染后
  `ModelHosts.WriteBackAll()`。页面不必自己写帧钩子；换页时
  `ModelHosts.Unregister(binder)`。

由此三条选路规则：

- **值/显隐/列表内容 → 改模型**：`model.Set("title", ...)` /
  `Set("showBag", ...)` / `Set("items", ...)`，末尾
  `RequestRedraw` 出一帧即生效（模型改了但窗口静止，屏幕不会
  自己动）。别既改模型又 `SetProp` 同一控件——两个写入源打架，
  帧后回写可能把模型改动顶回去。
- **结构（增删控件/换组件类型）→ 命令式**：retained 树 API
  （`Find` + `Add`/`InsertAt`/`Remove`/`SetProp`）或整页
  `LoadHtmlWith` 重装。已知代价：重载是**整树无 diff**（手工 Add
  的控件不在新文档里）；命令式行内 style 只吃视觉键、布局键走
  类规则；没有"改第 N 行"助手——列表局部高频更新留在模型通道
  （data-for 长度变了整组重建）。
- **模型宿主与窗口无关**：任何一棵控件树（`LoadHtmlWith` 装出的
  运行期文档树、手搭树）挂上 ModelBinder 即生效。此前只有 ChildWindow 带这层驱动——主窗口里的页面写了
  绑定也不生效，于是「一个 .html 文档 + 一个 .zan code-behind」这种
  页面写法在主窗口内拿不到判断与双向绑定。

**每帧同步与用户输入的分工（坑）**：模型→UI 同步**不覆盖"用户改过、
还没回写"的控件**——控件现值与上次模型确认值（快照）不同就说明用户
在编辑，这期间模型不抢控件；帧末回写把用户值落模型后两边又一致。
少了这条，每帧同步会把用户正在敲的字吞掉（模型→UI 与 UI→模型之间
隔着整整一帧，输入在下一帧就被模型旧值盖回去）。

## 页面范式：.html 文档 + .zan code-behind + 模型宿主

一个页面 = 一份文档（结构与样式）+ 一段 code-behind（数据与事件）+
一个模型（JsonValue）。**不是"装载文档 + 命令式填值"**——那样
列表要手搭行模板、判断与绑定都写不进声明面。主窗口里的页面分区
同样适用（不必把页面做成独立子窗口）：

```zan
// 1) 装载文档（样式随装载并进 appCss，作用域类自动混入）
Html.Install();                       // 运行时装载必须先装声明层
Control ui = app.LoadHtmlWith(html, null, "");  // html 来自文件/内嵌资源
// 2) 挂模型宿主：事件按名字接线 + 绑定声明从此生效
ModelBinder b = new ModelBinder();
b.Handle("save", () => { ... });          // 无参
b.HandleArg("pick", (string s) => { });   // 带参（data-arg）
b.HandleSender("open-row", (Control c) => {
    JsonValue row = b.ScopeOf(c);         // 行内控件 → 行模型项
});
b.SetRoot(ui, model);
b.Wire();
ModelHosts.Register(app, b);            // 主窗口的每帧泵驱动它
```

- **装载器不带处理器表**（`LoadHtmlWith(html, null, dir)`）：事件统一
  由 binder 的 `Wire()` 接线。装载时若也传 handlers，同一个控件会有两
  条订阅，一次点击触发两遍。
- **注册顺序：先 `Handle` 后 `SetRoot`**：早期 `SetRoot` 会重建处理器注册
  表，把之前注册的处理器全冲掉——页面照常渲染、按钮照常画出来，**点击却
  毫无反应且不报任何错**（坏的是看不见的部分，最难查）。现在 `SetRoot`
  只挂根与模型，注册表仅在为 null 时才建。
- **`Html.Install()` 必做**：只走 `app.LoadHtml` 的宿主不会自动装声明层，
  装载后端没注册时 `LoadHtmlWith` **静默返回 null**（页面一片空白，
  只有一行日志能看出）。
- **行点击要拿"是哪一行"**：用 sender 槽（`HandleSender`）+ binder 的
  `ScopeOf(control)`（取行模型项）；`data-arg` 只支持字面量，行内用不了。
  **行根也登记作用域**（2026-10-23 修）：`@click` 挂在 `<template data-for>`
  展开出来的**行根**上时，行根自己没有绑定声明，早期只给声明了绑定的控件登记
  作用域 → `ScopeOf(行根)` 返回 null → 处理器拿到 null 早退 → **行点击静默
  失灵**（界面正常、点击毫无反应、服务端一个请求都没发；点行内文字与点行留白
  都一样）。修在 `ModelBinder.ScopeRow`（行根无条件登记一次，撤行时对称摘除），
  conformance 用例 `gui_html_dynamic` 钉住：行根 `ScopeOf` 必须命中行模型项。
  排查这类"点了没反应"先量 `ScopeOf`：返回 null 就是登记面漏了，不是事件没接上。
- **列表用 `<template data-for>` + `data-key`**：行内 `data-bind` 以本项
  为第一作用域、回落根模型；`data-key` 走增量 Diffing（无 key 按长度
  整组重建）。要标准库组件的虚拟化/滚动就别用 data-for，改用 ListView
  绑数据 + 行模板（行模板是代码建的控件，样式只能放皮肤层）。
- **改模型后要出一帧**：帧泵每帧同步模型→界面，但窗口静止时不会自己
  出帧——改完模型记得 `app.RequestRedraw()`。

代码内实时绑定是另一层：`Binding<T>`（stdlib 规范 §6.1），
强类型字段直绑；字符串 bindPath 是"对话框 ↔ 状态实体"通道，
两者互不替代（Control.zan bindPath 头注释、STDLIB_COMPONENT_
STANDARDS.md §6.7）。

## 文档与原生控件混排：五个画不对的坑（都真踩过）

- **文档里的选择器只命中文档元素**：运行期装载的文档会给自己的元素
  混入作用域类（`zs-load-<序>`），代码后挂进去的控件（徽标、原生
  网页视图、工具条）**不带**这个类——写在文档 `<style>` 里的
  `.wxrailbadge` 这类规则永远匹配不到它们，控件会退回普通流内布局
  （把父容器挤歪）。这类控件的外观规则只能放**皮肤层**（全局）。
- **原生层是兄弟层，永远盖在保留树上**：网页视图（WebViewBox）这类
  原生控件不是画在画布上的，它是一枚独立窗口。工具条与原生视图
  不能都当"填充项"（两个 flex-grow:1 的项会整块重叠，原生视图把工具
  条盖住，布局自检会报 overlap）。原生视图的宿主必须拿到一块**明确
  的矩形**：工具条固定高（44px）+ 原生视图 grow 吃掉余下。
- **图标大小不是字号**：按钮/标签的字形边长 = 字号 × 4/3
  （`BoxContent.GlyphSize`）。想要 20px 的图标得给 15px 字号；给
  20px 字号就是 27px 的巨图标（占掉半条导航栏）。
- **单行标题类文本要 `white-space: nowrap`**：引擎量块流文本时可能
  拿不到容器宽（先量在 0 宽上），一折行就把固定高度的标题行撑爆、
  文字溢到下面的分隔线。标题/副标题/表头这类"一行的东西"直接
  nowrap；正文才让它折。
- **同一个类不要文档与皮肤各写一半**：`.wxhead` 一半在文档（只写
  flex）、一半在皮肤（高度内距），两边合起来的最终值谁也说不清
  （交叉轴对齐失效、标题块量到 0 宽）。共享给"代码建的控制"的类放
  皮肤，页面独占的类放文档，**一份类只出现在一处**。

## 内嵌真网页（WebView）：三道坎（都真踩过，症状全是"静默失效"）

`.html` 设计稿是**引擎自己的控件树**，不是网页；只有两处用真浏览器引擎：
服务端渲染好的正文（`LoadHtml`）与后端管理台（`Navigate` 到服务端页面）。
这条路上的坑没有一条会自己报错——界面正常、零日志、功能没生效。

- **WebView2 的 COM 入口有 UI 线程亲和**：跨线程调用回
  `UI_E_WRONG_THREAD`（0x802A000C）。Zan 的 `async` 体由后台协程池驱动，
  不在 GUI 线程上——在协程里发 `NavigateToString`，正文永远装不上，页面
  停在 `about:blank` 且不报任何错。解法是包里的 `WebView2.IssueOnUi`
  （Dispatcher 投回 UI 线程），`Navigate`/`LoadHtml`/`ExecuteScript`
  都要走它。坑出处：文档页正文空白只能降级纯文本；内嵌后台网页永远要
  再登录一次。
- **vtable 槽位按 SDK 头文件数，别凭印象**：`ICoreWebView2` 本体 58 个
  方法（槽 3..60，0..2 是 IUnknown），`ICoreWebView2_2` 的自有方法从 61
  起——`add_WebResourceResponseReceived`=61、`remove_`=62、
  `get_CookieManager`=66。槽位写错就是读到 vtable 之外的内存，HRESULT
  报 `E_INVALIDARG`（0x80070057），症状是"某个功能从来没生效过"。
  核对来源：NuGet `microsoft.web.webview2` 的
  `build/native/include/WebView2.h`（方法按声明顺序就是槽位顺序）。
- **客户端已登录 ≠ 网页已登录**：API 走 `Authorization: Bearer <令牌>`，
  后台网页认的是 `zsession` 会话 Cookie；网页视图是**独立的浏览器
  上下文**，不继承客户端的请求头。令牌两边通用（服务端 API 登录落的
  `zsession` 就是这枚令牌）→ 客户端把令牌种成 `zsession` Cookie 即可。
  种 Cookie 的**唯一时机是首屏导航之前**（原生视图还没建出来），
  `SetCookie` 必须能排队到"创建完成、首次导航之前"再落罐。
- **别吞 COM 的 HRESULT**：正文没装上时宿主只看到"空白"，看不到原因。
  `Navigate`/`LoadHtml` 失败要打出来，否则这类问题只能靠猜。

取证与验收（网页内容抓不到像素——它画在独立子 HWND 上，压在画布之上）：
① 控件树**分支判定**（网页容器可见 / 纯文本降级容器不可见）；
② **地址条**（原生 label，显示真实 URL）——落在 `/admin/login` 还是
`/admin/dashboard` 一眼分明。A/B 对照时注意 WebView2 的 Cookie 存在磁盘
（默认 `%LOCALAPPDATA%\ZanGui\WebView2\default`），用
`WEBVIEW2_USER_DATA_FOLDER` 换一个干净目录，否则第二次跑继承第一次的
Cookie，"对照组"就不成立了。

## 多按钮控制中间区（切页三路）

1. **推荐：Nav.Embed 做骨架 + 每页 .html 内容**。`Gui/Nav.zan` 的
   Embed（Tabs-as-outlet）：路由表=按钮排，中间区=出口；每页首次
   选中才实例化（惰性）、默认切走保留（keepAlive）、可按路由配临态
   （切走销毁）。放置/面板类游戏的"切走保留"和它默认值天然一致。
2. **纯声明近似（页少面板轻）**：每按钮 `data-on-click="go"
   data-arg="bag"` + 一个处理器翻模型 + 面板挂 `data-if="showBag"`。
   页一多（十个布尔字段）就笨，别硬拼。
3. 声明式出口原语（`data-nav` 直连路由）v1 没有——想要"零接线"
   先确认它是否已落地，没有就走路 2。

## 布局：按浏览器等价子集写，别按浏览器全量写

引擎支持 flow / inline 行盒 / flex / grid / float / overflow 滚动，
经 Chrome getBoundingClientRect 逐盒对比（oracle）校准，多数场景
0px 偏差。CSS 方言之外的东西写上去不报错也不生效——动手前查支持度
审计脚本与 HTML_UI.md 的与浏览器差异清单；"改了没反应"先怀疑
没进方言，再怀疑优先级。

## CSS 两层与基线坑

- 皮肤基线（base.css，`flex{display:flex}` 等 UA 规则）+ 应用级
  （`app.UseAppCss(...)`）。颜色走 `var(--*)` 令牌，尺寸走阶梯。
- **基线 base.css 由编译器内嵌兜底，磁盘上不要再放一份**：编译器把 stdlib
  皮肤目录整份烤进 exe，运行时是**磁盘优先、内嵌兜底**——exe 旁那份
  base.css 会盖住内嵌基线，"内嵌兜底到底生效没有"永远测不出来。
- **暂存形态分两种**：单文件形态（`--embed <pack.css>=skins/<名>/skin.css`）
  只内嵌自家皮肤包、不顶替基线，编译器仍自动内嵌 stdlib 基线，**不要**拷
  base.css；目录形态（`--embed skins`）只有该目录自带 base.css 时编译器才
  跳过 stdlib 基线内嵌，此时要换基线就得给**完整**基线，否则基线层整层丢失、
  flex 全退化成叠矩形（无为修仙传百艺页签全叠的根因：只带自家 skin.css 的
  目录暂存吞掉了 stdlib 基线内嵌，编译器已把跳过条件收紧）。
- 运行时换肤是实例方法：`form.GetApp.UseSkin("neon")`——按静态
  `App.UseSkin(...)` 调会报 cannot call instance method（踩过）。

## 模板实例化（gui-* 模板带 `{{NAME}}` 占位符）

- `{{NAME}}` 在**两处**都要替换：`src/App.html`（根元素
  `id="{{NAME}}"` 与 `data-win-title`）和 `src/App.zan`
  （主类 `class {{NAME}}`），且两个入口文件要改名为
  `<NAME>.html`/`<NAME>.zan`（zan.proj `entry = src/{{NAME}}.zan`
  按名对上）。只换一处，装载路径或入口就对不上；模板缺文件
  直接编译/装载失败——整个 src 都要带。
- 参考脚本 `scripts/e2e_pipeline.ps1`：整树拷贝 + 替换 + 用
  zan.proj 编译，别手搓单文件。

## 高度归属：一行有多高由谁说了算（框架层硬规则）

设计稿里每个字段的**高度有且只有一个来源**，写错来源就是"改了高度不生效、
行与行叠在一起"。

- **三种高度，三条归属**（`FormField.DocHeightType` / `IsFixedHeight` /
  容器）：
  1. **皮肤定高型**（输入框/下拉/开关/标签/OTP…）：高度归**尺寸档**
     （tiny 24 / small 28 / medium 32 / large 40），设计稿**不写高度**。
     写了就会压掉尺寸档（表单整体改小一号时高度不变）。
  2. **内容型**（多行文本/附件/表格/树/时间线/图表…）：样式表没有默认
     高度，设计稿**一律写高度**，值取类型标准行高
     （`FormField.DefaultHFor`：多行文本 62 / 附件 78 / 表格 104…），
     作者改过就写作者的值。不写就塌成一行高、内容溢出压到下一行。
  3. **容器型**（面板/卡片/标签页/分栏…）：高度**由内容自适应**，
     设计稿默认不写；作者在自由布局里显式定了才写。
- **作者声明的高度优先于样式表**（CSS 的内联 > 样式表）：文档里的
  `fh` 走 `Control.DeclareHeight` 通道，运行时与预览都按它排；
  代码里手搭控件的 `logH` 只是"测量偏好"，尺寸档照旧说话——
  两条通道别混，否则代码里量尺寸的尺寸档演示会失效。
- **旧稿里的 `fh=32` 是历史默认不是作者意图**：当年给每个字段写死 32，
  载入时按上表还原（内容型→标准行高、其余→没写）。容器被钉死在 32
  行高正是"内容溢出、和下一行叠"的直接原因。
- 验收：设计器预览与生成运行时的**同一棵树**逐节点矩形一致
  （`conformance_gui_designer_wysiwyg`）；相邻两行的底 ≤ 下一行的顶。

## 浮动件的定位基准：让开父容器最上面那条停靠带（框架层硬规则）

浮动（绝对定位）件的 `fy=0` 指的是**内容区上沿**，不是"父容器盒子顶"。
父容器顶上有停靠带（标题栏 / 工具条 / 面包屑）时，浮动件必须落在那条
带子**下面**——否则 `fy=0` 的浮动件正好压在标题栏上（用户报的"标题栏
浮动组件忽略标题栏高度"）。

- **只让开最上面那一条**：浮动件本来就浮在内容之上，后面的内容条不挡它；
  让开全部停靠带会把浮动件推到内容底部（作者 `fy=0` 的意图全丢）。
- **谁算"停靠带"**：父容器里 `dock=Top` 的可见子项，**取最靠上的那条**；
  浮动件自己不算（它正是在从流里摘出来的那一条）；布局内部的流式容器
  （`FbFlow`）不算——那是布局结构，不是作者摆的标题栏。
- **设计器与运行时必须同一口径**：设计器的摆放基准
  （`Designer.DesignSlotOriginY`）与运行时的排布基准
  （`Control.ArrangePositioned`）都取"内容区上沿 + 首条停靠带高度"，
  否则预览与生成程序会错位（WYSIWYG 破）。
- **转换瞬间不许跳**：把一个流式件转成浮动件时，它自己那条停靠带要排除
  在外（设计器侧按"正在编辑的字段"排除，运行时侧按"浮动件"排除），
  否则基准在转换那一帧变一次，控件会突然跳一格。
- 验收：`conformance_gui_designer_float_band`——浮动件落在标题栏下沿、
  没有停靠带的容器里仍贴内容区上沿、设计器预览与生成运行时矩形一致。

## 颜色属性接真取色器：色块即触发条，写回走 CSS 颜色文本（框架层硬规则）

- **色块是 `ColorPicker` 的触发条，不是自绘色块**：设计器属性行的颜色
  属性用取色器本体当触发条（皮肤透明底 + 当前色），点开就是 SV 面板 +
  色相/透明度条 + RGBA/HEX/HSL/HSB 输入行。自绘色块配"点击循环预设色"
  是死路：用户挑不了颜色，只能在几个预设之间跳。
- **即时模式控件的弹层锚点要自己落**：弹层按 `bx/by/bh` 定位，而这三个
  字段只有 `Arrange`（保留式布局）才会设。即时模式渲染（`RenderIn`）必
  须自己写这三个字段，否则弹层跑到窗口原点——设计器属性行就踩过这个坑：
  取色器弹层开在左上角，而不是色块下方。
- **颜色属性（`PropSpec` kind 4）的文档值有两种形态**：十进制打包色
  （`Read()` 的输出，0xAARRGGBB 回绕成负 int）与 CSS 颜色文本
  （`#rrggbb` / `#aarrggbb` / `rgba(...)` / 命名色）。两种都要认——只按
  整数解析的话，`SetProp("color", "#ff0000")` 静默落成 0（= 没设颜色），
  用户挑了色却什么都没发生。
- **写回的取值文本用 CSS 形态**（`Color.ToCss()`），与 STYLE 通道、控件
  属性协议认的形态一致；半透明写 `#aarrggbb`——本仓库的 CSS 颜色解析器
  是 ARGB 序，不是 CSS Color 4 的 RGBA 序，别混。
- **属性行里的取色器用值轮询，不用事件**：弹层在覆盖阶段绘制，事件要等
  下一帧才到；每帧比对取色器模型值与上次的值，变了就写回，反而更直白。
- **非字面色要标出来**：`var(--x)` 这类令牌解析不出颜色，色块上画一道斜
  杠表示"这不是字面颜色"，别拿上一轮的旧色冒充当前值。
- **打包色拆分量必须走掩码**：`0xAARRGGBB` 在 32 位 int 里 alpha≥0x80 时
  是负数，裸除（`packed / 65536 % 256`）会算出负分量（-231 这种），颜
  色直接变花。统一走 `ColorPicker.RgbOf(packed)` 取低 24 位再拆——场景
  设计器的颜色行就栽在这儿（文本写回元素染色时把分量写成负数）。
- **文本框与取色器互相跟随时要用影子值**："用户改了文本"和"我们把取
  色器的值写回文本"必须能分开，否则用户刚在弹层里挑的色会被上一帧的
  旧文本顶回去（两条同步路径打了一架，谁后跑谁赢）。用一个影子字段记
  住上次写回的值：文本与影子不等 = 用户改的 → 取色器跟文本；取色器模
  型值变了 = 用户拖的 → 翻成 CSS 文本写回文本框并更新影子。
- 验收：`conformance_gui_designer_color_picker`——属性值的两种形态、
  弹层锚点落在色块下方、取色器改色与手改文本两条写回路径；
  `conformance_gui_scene_designer_color`——场景设计器颜色行的同款两条
  路径 + 锚点 + alpha 不被取色器接管。

## 属性行契约：键必须能读回来，基类字段必须应答（框架层硬规则）

设计器"组件属性"行里的每一个 `(kind, key)` 都必须被对应控件**真实消
费**：键在该控件 `Props()` 的 PropSpec 里（key/alias 应答），或
`SetProp` 写入后 `GetProp` 读回非空（覆写截获的键）。基类兜底会把不
认识的键退化成样式类——那种键在面板上能改，改完什么都没发生。

- **基类字段要走属性协议**：每个控件都有 `Disabled`（沿树继承的禁用
  标志），但基类 `GetProp`/`SetProp` 曾经不答它——设计器的"禁用"行写
  进去的是样式类 `disabled`，控件本身没被禁用。现在基类直接应答这个
  字段（控件自己声明了同名 spec 时走 spec，同一个字段）。
- **写侧截获的键要补读侧**：`SetProp` 截获的键（`Badge` 的 `dot`、
  `Empty` 的 `description`）此前只有写没有读——属性契约探针判它们
  是死键，用户在面板里改完读不回来。补法要么给读侧（`GetExtra`），
  要么把字段正式声明进 `Props()`。
- **布尔键要读写对称**：只写不读的布尔属性写 `false` 等于没写。`dot`
  的语义是"无计数形态"，写 `false` 至少要留一个计数，否则徽标什么
  都不画。
- **类修饰键不走属性协议**：按钮的 `type`/`round`/`circle`/`loading`
  是基类兜底写进 class 名单的（控件/皮肤按 class 生效），`GetProp` 读
  不回值——契约探针不能拿"读不回"判它死键。这类键要在探针里单列
  登记并注明消费处（`Style.ButtonDefaults` 认 round/circle、Button
  绘制认 loading、皮肤有 button.primary 等角色类）；登记表里没有的
  类键一律判死键，防止拼错的类名悄悄落地。
- **字段存在 ≠ 属性可达**：`Button.TipSide` 字段在、渲染也用（气泡
  方位），但没进 `Props()`——设计器那一行写进的是样式类，提示方位
  纹丝不动。控件里凡是渲染读到的字段，都要在 `Props()` 里声明一
  行，否则那一行属性是死的。
- 验收：`conformance_gui_props_contract`——`BuiltinPropRows` 里每个
  键都必须被控件消费（死键当场 FAIL）。这个用例红了就是属性协议漏了
  键，不是设计器的问题。颜色属性另有一条
  `conformance_gui_designer_color_props`：每个颜色键要同时满足"行表
  里有这一行 / `IsColorProp` 认它 / 控件写进去读回来还是同一个颜
  色"——三缺一就是"能改但不变"的哑行。

## HTML 窗口设计器的所见所得

- **共用完整建树与布局，拾取读取实际框**：预览树按原始
  `fields/kids` DFS 装配（prepared 槽），FormBuilder 统一装配；混排顺序只影响
  展示，不改变槽序。完整结构遍历 `Control.children`，文本次序结合
  `Element.elOrder`；`FlowEntries` 是流布局视图，会排除 absolute/hidden，
  用它分配槽或断言整树曾把仍存在的定位控件误判为丢失。选框、拖动、
  缩放、换父均读取实际 `bx/by/bw/bh` 和父内容框/SlotHost；Flow 转 Float
  时先保存世界坐标，显式 fw/fh 表示最终 border-box，避免旧 CSS 尺寸
  让选框变大而控件不变。
- **目标 App 与编辑视图分离**：离屏 App 自持文档 CSS、基线缓存、主题、
  DPI 和事件；视图 zoom 只缩放最终图像。借用编辑器的样式上下文曾使
  继承/后代选择器度量不同，把 zoom 当 DPI 又会二次缩放。目标 DPI 是会话
  预览选项,不写进设计稿;开发屏 150% 时也要能预览 175%/225%。验收比较
  100%–300% 每 25% 一档的同目标 DPI 整窗几何和全部像素,再操作真实 IDE;
  另断言逻辑尺寸及 CSS 长度的预期缩放,避免双方共用错误却像素相等。
  纯序列化往返不能证明所见所得。原生建窗/改尺寸还须断言实际客户区：无边框壳若在
  `WM_NCCALCSIZE` 中已移除系统框，就不能再 `AdjustWindowRect` 外扩；
  150% DPI 真窗曾把 480×320 的设计开成 742×536，而离屏是 720×480，
  右下锚点随之偏移。逻辑尺寸与设备尺寸入口只差 DPI 换算，共用精确
  客户区契约，不能靠预览补出多余边框。
- **编辑坐标反算须可还原,先减父原点再换单位**：正向整数 DPI 缩放会截断,
  反向再截断曾使奇数 fx/fy/fw/fh 在 125%/175%/225% 等档反复编辑时丢一
  逻辑像素。反算选择能复现设备值的逻辑整数,对正负数分别处理;相对位置
  先用设备坐标减实际父内容框原点,不要分别反算两个绝对坐标后相减。
  回归覆盖带符号的逻辑→设备→逻辑、奇数尺寸重复 Flow/Float、拖动/缩放
  与 Undo,以及目标 DPI 往返切换后作者文档不变。
- **保全 HTML，结构编辑按身份重映射**：保存 doc CSS、body class/style、
  htmlTag 和字符串/子索引组成的有序内容；删除、复制、换父后按原
  JsonValue 身份重映射索引。只保存 kind/kids 曾丢掉原生标签、选择器
  和混排文字；直接复用旧索引会指向另一个孩子。同级重排还须保持文本槽
  并重写子节点槽的显示顺序：只重映射身份曾让 MoveSel/SelToEdge 改了
  kids 顺序，混排画面却仍按旧序，验收要检查实际 elOrder 的控件引用。
- **FormBuilder 装配契约（Element 混排）**：HTML 容器节点导出的
  JSON 结构常为 `kind: "Panel"` 且带有 `htmlTag`。父级是 Element
  （满足 `IsElement`：带有 `htmlTag` 或 `kind == "Element"`）时必须走
  `AddKid`/`AddChild` 保 `elOrder`，绕过它子节点会被推迟到全部内联
  文本流条目之后，混排流序与设计器预览出现偏差。
- **最终窗形与运行窗口共用遮罩**：逐 region 计算并集，圆角 SDF 保留
  `min(max(qx,qy),0)` 内部项；旧步进曾跳过后续区域，缺内部项使 radius=0
  整块半覆盖。区分 straight/premult，预乘缓冲须满足 RGB≤A：
  `0x80FF0000 / cov128 → 0x40FF0000 → 0x40400000`；只乘 coverage 曾造成
  亮边。快照只读借用 surface，复制到自有缓冲后裁切，避免污染原帧。
- **预览状态不反写作者文档**：进出预览从文档恢复运行树，避免已切换的
  Tabs 状态泄漏回编辑页。中键、抓手、Ctrl-wheel 归编辑器，并取消目标
  press/anchors；否则平移会拖 splitter、缩放会滚内容或留下误点击。
  普通滚轮使用局部坐标和正常 App 的认领/至多一次重放；缺重放曾吞掉
  刚进入滚动区域的第一格。Tooltip 等静态待办和 App.FrameApp 须在嵌套帧
  进入时切换到目标，退出时在 finally 中恢复，异常取消也须走相同出口；
  否则 HTML title 提示会读取宿主焦点或串到外层画布。离屏延迟帧请求还须
  转交可见宿主兑现，否则停稳悬停后没有事件唤醒预览。离屏帧不重置宿主
  Dispatcher 或平台 IME。

## 验证定式

- 纯逻辑（Html.Parse/绑定/同步）headless 可跑，无窗口：conformance
  形态的探针 + 金样（CRLF）。
- 布局几何：oracle 逐盒对比（同一份 .html 喂 Chrome 与引擎，
  getBoundingClientRect vs Arrange）。
- 手工编 GUI 程序：zanc 编全部 .zan（入口走 zan.proj `entry`）+
  `--subsystem windows`；设计稿 .html 不是编译输入（运行期加载，
  发布 `--embed`）。跑起来截图对照（锚定 PID），交互用
  UiDriver 可重复驱动。
- **真窗冒烟要带主窗骨架，裸页截图会"缺导航栏"**：页面文档只含
  页内骨架，图标导航栏在主窗文档里——把单页文档直接装进裸 Form
  截图，看到的永远是"少了左侧导航"（不是回归）。要看真实形态就
  复刻主窗装配（LoadHtmlWith 主窗文档 + 各页宿主挂页），跳过登录泵。
- **截屏三坑（当天连踩五次才抓到图）**：① 带中文注释的 `.ps1`
  以无 BOM UTF-8 落盘，PowerShell 5.1 按 ANSI 误解析，多字节序列
  会吞掉下一行语句——参数/环境变量"神秘变空"多半是这个，自动化
  ps1 只写 ASCII；② bash 的 `$!` 可能是包装进程——以 ProcessStartInfo
  返回的子进程 PID 枚举窗口，并用 GetWindowThreadProcessId 核对归属，
  不按名字批量关闭；遮挡时用 `PrintWindow`（flags=2）抓整窗；
  ③ 150% DPI 下先核对逻辑尺寸、物理客户区和缩放：只有证实冒烟窗口
  按物理尺寸错误打开，才可把右缘裁切归因于环境；修正宿主尺寸后仍须
  比较同 DPI 的整窗几何与像素，不能用 headless 通过豁免截图差异。
- **功能验收（不是页面加载）要走"真服务端 + 状态断言"三段式**：
  页面能打开 ≠ 功能通。① 用服务端自己的接口把数据种进去（发一条私信
  就给对方造出一条通知——发信即落通知；建空间/建表同理）；② 客户端点完，
  **回服务端查状态变了没有**（未读数、记录数、版本号），光看像素不够——
  像素只证明"画出来了"，不证明"写进去了"；③ 点击坐标从 `dump tree` 的
  JSON 里按控件名取矩形中心（**顶层是 `{"root":{...}}`**，不是直接一棵
  子树；`name` 为空的行要靠容器节点的 kids 顺序认）。驱动脚本是静态文件：
  坐标只能来自**上一次**运行的树 dump，所以"定位"与"点击"天然分两轮跑。
- **别用字节值拼中文测试数据**：`[char]0xE8` 这类写法把 UTF-8 字节当成
  码位，服务端收到的就是乱码——排查"乱码"前先确认**造数据**的那一步
  用的是码位（`[char]0x8054`）还是字节（`0xE8`）。

## 游戏接入边界

菜单/面板/商店这类低频交互面可嵌进游戏帧循环（同一引擎，脏区门控，
实测 clean/dirty 均值 3ms、峰值 23ms、预算 16.6ms，空闲 120 拍仅
3 帧重绘）；**每帧热路径 HUD（血条/伤害数字/小地图）仍走代码直绘
+ 门控渲染，不进 DOM**——那是性能边界，不是能力缺口。

## 声明式事件体系与丰富组件事件契约

彻底消除“HTML 仅作空壳、后台繁琐查找控件手动挂接”的反模式，由 HTML 模板承载完整结构、交互声明与事件语义，Zan 代码只作为 ViewModel / 业务逻辑层。

1. **统一声明式事件指令**：
   - 支持 `@event`、`v-on:event`、`z-on:event`、`on<event>` 及 `data-on-<event>` 等标准声明语法。
   - 解析层自动完成 kebab-case 到组件标准 PascalCase 事件名的规范化映射（如 `@row-click` -> `RowClick`、`@cell-dblclick` -> `CellDoubleClick`、`@column-resize` -> `ColumnResize`、`@tab-change` -> `Change`、`@page-change` -> `Change` 等）。
2. **双通道事件路由（BindEvent 与 BindEventS）**：
   - 无参通道：`BindEvent(string evt, Action a)` 接收轻量无参回调。
   - Sender 通道：`BindEventS(string evt, ControlEvent a)` 接收带 `Control sender` 实例强类型回调。
   - **包装 state/model 的复合控件（如 DataGrid/TreeView/Pagination 等）必须重写 BindEventS**：不可仅重写 `BindEvent` 并任由 `BindEventS` 回退到基类的 `On.AddByNameS`，否则会导致带参回调无法触发底层状态机专属事件；必须显式通过 `this.st.<Event>.Add( => a(this))` 将自身封装为 sender 准确派发。
3. **表格等大型组件的多维丰富事件（Row / Cell / Column / Grid）**：
   - 行级：`RowClick`、`RowDoubleClick`、`RowRightClick`（别名 `RowContext`）、`RowInserted`、`RowDeleted`。
   - 单元格级：`CellClick`、`CellDoubleClick`（别名 `CellDblClick`）、`CellEdit`、`CellValidating`。
   - 列与表头级：`HeaderClick`（别名 `ColumnClick`）、`ColumnDoubleClick`、`ColumnResize`、`ColumnResizeEnd`、`Sort`、`ColumnReorder`。
   - 网格视图与状态：`FilterChanged`、`SelectionChanged`、`GroupChanged`、`BandToggled`、`ScrollChanged`。
   - **双击命中坐标锁定**：在双击触发判定时必须锁定列索引（如 `st.hitCol = ptrCol;`），并同时触发 `RowDoubleClick` 与 `CellDoubleClick`，保证 `DataGrid.HitField` 能解析出目标列字段，供列声明的事件绑定（`@cell-dblclick` 等）精准路由。
4. **常用组件专属语义事件标配**：
   - `TreeView`：`Select`、`Activate`、`Expand`、`Collapse`、`Context`、`Check`。
   - `Pagination`：`Change`（切页）、`SizeChange`（切每页条数）。
   - `ListView`：`Select`、`Activate`、`Context`。
   - `Tabs`：`Change`（切标签页）。
   - 全部专属事件必须注册在 `Events` 清单中，并通过 `BindEvent` / `BindEventS` 双通道无缝转发。

5. **设计器画布纪律（ToolStrip 项与组件配置规范）**：
   - **紧凑型关闭按钮（MiniCloseBtn）排版纪律**：属性检查器（选项列表、CSS 样式表等）中的单项删除按钮禁止使用常规 28px/34px 操作按钮，统一采用 20px 紧凑圆底微十字按钮，避免挤占输入框与标签的空间。
   - **设计器画布网格避让渲染**：画布网格必须使用淡色微点阵（35%~40% 次级透明度）替代粗实线；绘制点阵时需检查控件包围盒（`fx, fy, fw, fh`），组件覆盖区域跳过绘制，彻底消除网格穿透遮挡组件界面的视觉干扰。
   - **常用组件属性契约闭环**：基础组件在设计器 `BuiltinPropRows` 中必须完整声明属性元数据，`Button` 等支持 `type` / `size` 变体类的组件在 `SetProp` 中使用 `SetClassIn` 维护互斥语义类，确保设计器即时配置所见即所得。

6. **窗体设计器事件交互与代码后置快速定位规范（新建/编辑/双击/快捷跳转）**：
   - **三合一快捷操作区**：属性面板「事件」选项卡的每个事件条目均配齐「定位」、「新建」、「编辑」按钮，并在底部事件输入框右侧提供一键「定位代码」，同时支持直接双击事件条目快速定位。
   - **语义化事件建议与自动绑定**：点击「新建」或在未绑定状态下点击「定位」/双击时，设计器自动按组件名与事件名推导标准方法名（如 `OnSubmitBtnClick`、`OnWindowLoad`）并写入事件绑定。
   - **智能代码后置（Code-Behind）定位与桩补齐**：通过 `handler:<方法名>` 请求宿主 IDE 切换至配对的 `.zan` 业务文件。若源码中已存在该方法，精准定位至声明行号；若尚未声明，自动在 `partial class` 类体末尾（最后一个闭合花括号前）插入干净的标准方法桩（`static void <方法名> { }`）并定位光标，实现所见即所得的极速事件开发闭环。

7. **窗体设计器属性强类型交互与多选排版对齐规范（下拉枚举/图标拾取/主选基准对齐/等间距分布）**：
   - **属性强类型化与下拉枚举（SelectBox）渲染**：属性提示中带有枚举候选项（如 `"Type (primary/info/success/warning/error)"`）时，自动解析出合法枚举列表并在属性检查器中渲染为 `small` 尺寸的 `SelectBox` 下拉选择框，替代繁琐易错的手动输入；标签展示时自动清洗掉 `(primary/...)` 后缀，保持排版精简整洁，同时保留 `(px)` 等合法度量单位。
   - **图标与资源可视化拾取（OpenIconPicker）**：属性键为 `icon` 时，在输入框旁渲染 `[图标]` 弹窗选择按钮，点击后拉起基于 `Icon.Names` 的可视化图标网格，点选后直接填充并触发画布即时重绘。
   - **多组件排版以主选为基准（Anchor Alignment）**：自由画布多选对齐（左/居中/右/顶/中/底）在单选时对齐至所属容器内容框，多选时必须严格以主选中项（`sel`）的位置与尺寸为几何基准（而非各元素独立归位到容器边框），符合直觉的矢量/GUI 排版体验。
   - **水平与垂直等间距分布（Distribute）**：支持多选控件（≥3 个）的水平等间距分布（`dist_h`）与垂直等间距分布（`dist_v`），保持首尾两端控件绝对位置不变，中间所有控件在剩余跨度内均匀分配几何间隙，消除人工肉眼对齐的繁琐工作。
   - **多媒体与图像资源浏览拾取器（Asset Picker）**：针对 `src`、`image`、`avatar` 及各类以 `Path` 结尾的媒体资源属性，在输入框旁渲染 `[浏览]` 按钮，拉起过滤了主流图片格式（`*.png;*.jpg;*.jpeg;*.svg;*.bmp;*.gif;*.webp`）的 `FilePicker` 弹窗；选中后自动调用 `RelativizePath` 将绝对路径转为相对于当前项目工作目录的标准短路径（如 `assets/player.png`），配合控件的 `SetProp` 实现画布即时渲染更新。

8. **窗体与场景设计器键盘交互与细节完善规范（退格删除/代码后置直通/颜色色块与预设轮换/场景Tint/透明度/锁定）**：
   - **键盘删除双键适配（Delete & Backspace）**：在无输入焦点（`focus.focusedId < 0` 且无弹窗/大编辑器打开）时，删除操作除响应 `kc == 46`（Delete 键）外，必须同时响应 `kc == 8`（Backspace 键），以完全对齐苹果键盘与无独立 Delete 键的笔记本电脑用户习惯。游戏场景设计器同理，在 `HandleKeys` 中必须同步适配，并在 `AnyInputFocused` 中防卫好包含类名（`inpIdClass`）与颜色（`inpIdColor`）在内的全部检查器输入控件。
   - **游戏场景元素事件直通 Code-Behind**：游戏场景设计器属性检查器的点击事件（`clickIn`）旁标配 `[代码]` 按钮。未绑定时自动推导生成 `On<ElementName>Click` 并存入元素动作；点击后置位 `openCodeBehindElement = "handler:" + h`，直通宿主 IDE 在场景 `.zan` 代码后置文件中精确定位或在末尾自动生成事件响应方法桩。
   - **颜色属性实时色块与预设主题色轮换**：对 `color`、`bgColor`、`textColor`、`borderColor`、`fill`、`stroke` 及 `*Color` 属性，在属性行右侧渲染 28px 交互色块；支持十六进制（`#RRGGBB` / `#RRGGBBAA`）与 CSS 函数（`rgb/rgba/hsl`）实时解析渲染；点击色块可在主流高频品牌/功能色预设之间快速循环轮换（蓝/绿/橙/红/紫/青/黑/白/灰），大幅提升配色效率。
   - **游戏场景元素染色（Tint RGBA）与透明度交互**：场景设计器对元素的 `cr, cg, cb, ca` 提供第一优先级支持。输入框提供十六进制颜色（`#RRGGBB`）并附带 28px 实体预览色块与点击轮换预设，下方配备透明度（0..255）步进调节，实现游戏界面按钮、面板底色、圆圈及文字颜色的即时可视化微调。
   - **场景设计器底部按钮规整排列与锁定/解锁切换**：底部操作按钮废除粗暴的单项拉伸排布，统一采用 2 列紧凑网格（显示/隐藏、复制、锁定/解锁、上移一层、下移一层、删除），使元素的锁定状态一目了然并防范误触移动。
   - **视图缩放快速重置（Ctrl+0）与多选状态顶部计数提示**：支持 `Ctrl+0` 快捷键瞬间重置画布缩放到 100% 并平移归零；多选组件时，属性检查器顶部目标提示自动显示 `(+N 个已选)`，让开发者对当前多选对齐与排版范围获得清晰确切的感知。
   - **层级升降与锁定快捷键体系（PageUp/PageDown/Ctrl+Home/Ctrl+End/Ctrl+L）**：窗体设计器与场景设计器全面对齐图层控制热键——`PageUp` (`kc == 33`) 上移一层、`PageDown` (`kc == 34`) 下移一层、`Ctrl+Home` (`kc == 36` + Ctrl) 置顶、`Ctrl+End` (`kc == 35` + Ctrl) 置底、`Ctrl+L` (`cc == 12`) 切换当前选中项的锁定状态，无需鼠标往返点击工具栏即可完成排版层级微调。
   - **画布双击直通事件与自动桩生成（Double Click to Code-Behind Handler）**：在场景画布上双击任意元素，设计器自动解析或推导默认事件处理函数（`On<Name>Click`）并回写属性面板与元素动作；随后通过 `openCodeBehindElement = "handler:" + h` 直通 IDE 打开代码后置 `.zan` 业务文件定位或自动补齐方法桩，并在双击帧屏蔽位移拖拽，对齐现代桌面 IDE 的无缝开发体验。

9. **声明式动态属性与动态样式类绑定规范（`:class` / `:disabled` 与通用 `:attr` 机制）**：
   - **动态样式类（`:class` / `data-bind-class` / `z-class`）**：支持三元表达式（`cond ? 'active' : ''`）与模型/作用域路径。求值时与初始静态 `class`（保存在 `baseClass`）做非破坏性合并；类名变更时自动清空 `computedStyle` 缓存，下一帧重新解析样式表规则，彻底避免覆盖静态类或样式滞后。
   - **动态禁用态（`:disabled` / `data-bind-disabled` / `z-disabled`）**：按条件表达式动态同步 `c.Disabled` 绑定，沿控件树向下继承置灰并切断交互。
   - **通用动态单向属性绑定（`:prop` / `v-bind:prop` / `z-bind:prop`）**：支持在模板上声明任意属性绑定（`:placeholder`、`:status`、`:title`、`:min`、`:max` 等），通过 `c.AddBindProp` 注册并在 `SyncFromNode` 每一帧中通过 `GetProp` / `SetProp` 对比写入；支持行作用域穿透（`template` 循环行内自动继承 item 作用域），并随节点克隆（`Html.Clone`）完整保真。

10. **现代桌面端（Dashboard）切页与高保真表单定式（实证踩坑与黄金解法）**：
    - **多页面切页：严禁在未激活面板上写内联 `style="display: none;"`**：
      - *踩坑实证*：在 HTML 设计稿中给未激活页面写了内联 `style="display: none;"`，后台代码在切页时设置 `page.visible = true` 却完全不生效，页面永远无法显示。
      - *原因*：`Control.StyleVisible()` 的判断逻辑是 `if (!visible) return false; return computedStyle.display != 2;`。HTML 内联 `style="display: none;"` 经 `ApplyInline` 将 `computedStyle.display` 设为了 2（none）。之后在后台只改 `visible = true` 并不会清除已固化的 `computedStyle.display`。
      - *黄金解法*：HTML 模板中所有切页面板标签**不要写内联 `style="display: none;"`**；页面初始显隐由后台 `OnLoad(Form form)` 首帧调用 `ShowPage(0)` 统一控制（通过 `page.visible = (idx == 0)`），这样 `visible = false` 干净隐藏、`visible = true` 干净恢复，零样式冲突。
    - **Checkbox 与 Radio 的文字标签：必须声明在 `<input>` 的 `value="..."` 属性**：
      - *踩坑实证*：按网页习惯写 `<label><input type="checkbox"> 记住我</label>`，复选框旁边的文字丢失。
      - *原因*：解析器将 `label` 折成独立 `Label` 控件，且含有 child `<input>`，内嵌的裸文本被忽略。而 Checkbox / Radio 在 Zan 原生控件体系中自带 label 文案属性。
      - *黄金解法*：Checkbox 与 Radio 的标签文字**直接写在 `<input>` 的 `value="..."` 属性中**（例如 `<input type="checkbox" id="chk1" value="把“病毒扫描”加入右键菜单" checked>`），`DesignerHtml.zan` 会将其自动映射为 `label` 属性并在控件内部右侧绘制文字，排版与基线对齐完美闭环。
    - **文本必须包裹在语义标签中（禁止容器 `div` 中散落裸文本）**：
      - *踩坑实证*：在 `<div class="tag">已保护<span>399</span>天</div>` 中，“已保护”和“天”丢失；在 `<div class="dropzone">拖拽到此</div>` 中，“拖拽到此”丢失。
      - *原因*：`<div>` 在解析器里兜底为容器 `Panel`。容器如果有子元素（如 span），散落在容器内部的裸文本没有对应的控件节点包裹，会被跳过。
      - *黄金解法*：所有需要呈现的文本内容，必须显式包裹在文本语义标签中：`<span>` / `<label>`（生成 Label 控件）或 `<p>` / `<h1>~<h6>`（生成 Typography 控件）。
    - **自定义无边框窗口操作**：
      - 窗口声明 `data-win-chrome="false"`，顶栏自定义按钮（`icon="minus"`, `icon="square"`, `icon="x"`），后台 `OnLoad` 订阅后直接调用 `__form.Minimize()`、`__form.ToggleMaximize()`、`__form.Close()`。
    - **Switch 自定义品牌色**：
      - `Switch.CheckedColor` 可直接赋予 ARGB 颜色（如 `unchecked((int)0xFFF58220)` 品牌橙），或在 CSS `:root` 中声明 `--primary`。
    - **现代安全软件/Dashboard 视觉精细化排版定式**：
      - *配色规范与纯净底色*：主视口采用纯白（`#ffffff`）或轻灰（`#fafbfc`），卡片边框采用柔和冷灰（`#eaedf2`），悬停时切换为微透品牌色边框（`#ffd8b3`）与淡阴影（`rgba(245, 130, 32, 0.08)`）；操作图标使用统一中性灰（`#5c6472`），避免杂色。
      - *字阶与对齐约束*：模块大标题采用 20px / 600 字重，副标题采用 13px / 400（字色 `#7e8794`，行高 1.5）；卡片项标题统一 14px / 600，描述文本采用 12px（行高 1.4~1.5）。
      - *间距与内边距档位*：视口外边距统一 40px，列表行卡片内边距统一 12px 18px，圆角统一 10px（大卡片 16~18px）；按钮/图标统一居中热区（36px~44px），消除几何错位与排版杂乱。


10. **声明式具名插槽路由与双向绑定修饰符规范（`slot` 与 `v-model.lazy/.number/.trim`）**：
   - **声明式具名插槽（`slot="xxx"` / `data-slot="xxx"`）**：复合容器（如 `Tabs`、`SplitPanel`、`Card` 等）覆写 `NamedSlotHost(string slot)`，在 `Html.AddChild` 阶段自动将带 `slot` 属性的子控件分发挂载到对应的内部宿主容器（如 `SplitPanel` 的 `left`/`right`/`pane1`/`pane2` 窗格，`Tabs` 的 `slot="Account"` 匹配标签页页面 `Page(i)`），完全免去在后台代码中手动拆分查找与挂载。
   - **双向绑定失焦防抖修饰符（`v-model.lazy`）**：在 `SyncChangedNode` 中通过 `c.HasFocus(host)` 检测用户输入焦点。处于获焦编辑状态时不触发每帧回写，直到用户失焦（Blur）或切换组件时一次性回写模型并更新快照，彻底消除键盘逐字输入过程中的状态抖动与频繁渲染开销。
   - **双向绑定类型保真修饰符（`v-model.number`）**：用户在输入框键入文本后，在 `WriteBoundValue` 回写阶段自动将其转换为 `JsonValue.NewNum` 数值节点存储，避免原本数字类型的 JSON 模型字段被降级污染为字符串类型。
   - **双向绑定首尾去空修饰符（`v-model.trim`）**：在回写前自动剥离字符串首尾的空白字符（`s.Trim`），可与 `.number` 复合链式声明（如 `v-model.number.trim="score"`）。

11. **文本胡子语法动态插值（`{{ expr }}`）与事件修饰符规范（`@click.stop` / `@keydown.enter`）**：
   - **胡子模板文本插值（`{{ expr }}`）**：在静态元素或模板行（`<template data-for>`）的文本内容中声明 `{{ user.name }}` 或 `{{ active ? '在线' : '离线' }}`。HTML 构建期将包含 `{{` 的文本捕获到 `c.bindTpl`；`ChildWindow` 在每帧 `SyncFromNode` 中调用 `EvalInterpolation` 解析各花括号内部的表达式，求值后回写到 `c.SetProp("text", val)`；支持行级数据作用域与根级模型混用及动态响应式刷新。
   - **复合标签与元子节点插槽无缝集成（`<tabs>` / `<tab title="...">`）**：`<tabs>` 容器既支持显式声明 `slot="Title"` 的直系子控件分发挂载，也支持以 `<tab title="...">` / `<tab-pane>` 包裹子内容的标准组件写法。在 `Html.Parse` 装配阶段，元子节点 `<tab>` 的内部子控件自动提取并继承标签页插槽路由至 `Tabs.Page(i)`，实现自然、声明式的选项卡界面排版。
   - **事件修饰符与防冒泡（`@click.stop` / `@keydown.enter`）**：在 `@click.stop`、`@submit.prevent`、`@keydown.enter` 等事件声明中，`HtmlText.EventKind` 自动剥离修饰符后缀以匹配核心事件种类，并在 `Html.Wire` / `Html.WireArg` 中通过 `WrapModifier` 装饰底层回调：`.stop` 自动调用 `app.ClaimClick` 截断事件向下冒泡；`.enter` 严格校验 `app.window.EventKeyCode == 13`，免去业务回调中重复编写键码和冒泡判定样板代码。

12. **HTML5 声明式输入控件多态映射与模型类型感知自动强转规范**：
   - **`<input type="...">` 多态组件实例化**：在 HTML 声明中写 `<input type="checkbox|radio|number|range|color|date|password" />` 时，装载器（`DesignerHtml.InputTypeKind`）自动映射实例化为对应的专属高阶组件（`checkbox` -> `Checkbox`、`radio` -> `Radio`、`number` -> `InputNumber`、`range` -> `Slider`、`color` -> `ColorPicker`、`date` -> `DatePicker`、`password` -> `Input` 带密码掩码属性），与自定义组件标签（`<slider>`、`<colorpicker>` 等）完全等价且支持标准 HTML 语义。
   - **组件类型感知模型强转回写（Coercion On Writeback）**：双向绑定 `data-bind` 回写时，若目标 JSON 模型节点尚未初始化（`cur == null`），`WriteBoundValue` 自动根据当前绑定的控件类型进行精准的类型归一化：`Checkbox` / `Switch` 强制以 `JsonValue.NewBool` 写入；`Slider` / `InputNumber` / `Progress` / `Rate` 及声明了 `.number` 的控件强制以 `JsonValue.NewNum` 写入；避免空模型初次绑定输入时被统一回写为字符串而破坏数据契约。
   - **快照即时同步防抖**：控件完成 `WriteBoundValue` 模型就地回写后，立即调用 `RecordSnapshot(c, s)` 同步控件快照，防止同一帧在后续流程中再次触发无效重复写入。

14. **设计器复合与容器组件项事件与细粒度配置闭环规范（ToolStrip 项事件/Tab 与选项关闭尺寸/大编辑器定位）**：
   - **工具条（ToolStrip）项点击事件在属性与事件面板双通道暴露**：
     - 在「属性」面板的选项行（OPTIONS）上，为非分隔符项直观展示已绑定的事件处理器（`-> On...Click`），并在每行配备 `[事件]` 操作按钮，点击自动推导方法名并直通代码后置。
     - 在「事件」面板中增设专属「工具条项点击 (Tool Items Click)」事件组，列出全部项的 `.Click` 事件，标配「定位」、「新建」、「编辑」三合一操作，支持一键在代码后置中定位或自动生成事件桩。
   - **大弹窗选项与列编辑器（RowsEditor）代码定位直通**：
     - 在工具条项与表格列的弹窗主从属性编辑面板中，为选中项提供「定位事件代码」/「定位点击代码」按钮，免除开发者在选项文本与事件方法名之间反复来回切换。
   - **选项与 Tab 关闭按钮精致化度量（14px~16px）**：
     - 属性检查器选项列表中删除按钮（`MiniCloseBtn`）重构为轻量 16px 矩形热区配 10px `close` 字符图标与悬停高亮；`Tabs` 标签页的关闭按钮统一收缩至 14px 方形热区并配合 10px 字符，彻底根绝关闭按钮过大挤占选项标签或误触的问题。

15. **高级组件标签映射与插槽挂载规范（Tabs/Card/Badge/Tag 等）**：
   - **高阶组件声明式类型映射**：标准组件标签（`<card>`、`<badge>`、`<avatar>`、`<tag>`、`<divider>`、`<alert-box>`、`<rate>`、`<split-panel>`、`<collapse>`、`<breadcrumb>`、`<steps>`、`<timeline>`、`<qrcode>`、`<watermark>`、`<marquee>`、`<spin>`、`<empty>`、`<result>`、`<skeleton>`、`<input-number>` 等）装载时直接实例化真组件，严禁退化为通用 `Gui.Element` 导致方法丢失。
   - **真组件文本与空白过滤（Anti-Bogus-AddText）**：真实组件继承自 `Control` 而非 `Element`，不具备 `AddText` 接口。装载装配时对判为真组件的标签，游离空白符与换行符必须严加过滤；非空白文本内容通过 `v.SetProp("text", ...)` 规范写入。
   - **Tabs 容器选项自动推导与元子节点插槽转挂**：`<tabs>` 容器若未显式指定 `options` 属性，装载器自动深层扫描子级 `<tab title="...">` / `<tab-pane>` 元素并推导拼接 `options` 字符串；元子节点标记放行，其内部实际承载的子孙控件自动注入 `slotName = tTitle` 并重定向挂载至父级 `Tabs`。
   - **密码输入与扩展属性双向对称性（GetExtra/SetExtra）**：`<input type="password">` 统一通过 `v.SetProp("password", "true")` 声明，且控件层在 `GetExtra` 与 `SetExtra` 中形成完整读写闭环，确保属性获取与状态同步双向对称。

16. **动态模型数组驱动选项绑定与双向值同步规范（`:options` 与 `<option value="...">` JIT/AOT 同构）**：
   - **JSON 模型数组动态绑定（`:options` / `v-bind:options`）**：在 `ModelBinder.ResolveOperandStr` 中，当绑定表达式求值结果为 JSON 数组（`v.IsArray`）时，自动序列化为标准 JSON 字符串（`v.ToJson`），使高阶选择组件（`SelectBox`、`ChoiceGroup`、`RadioGroup`、`Tabs` 等）能够无缝响应由模型数组驱动的动态选项变更。
   - **选项反序列化多格式兼容与元数据映射**：组件的 `SetOptionsText` / `SetItemsText` 原生兼容经典竖线分隔字符串（`"A|B|C"`）与结构化 JSON 数组（`"[{\"label\":\"北京\",\"value\":\"bj\"}, ...]"`）。解析 JSON 数组时，标签键优先读取 `label` / `title` / `name` / `text`，值键读取 `value` / `id`，并支持 `disabled` 与 `closable` 元数据映射。
   - **帧渲染缓存幂等防抖（`optionsRaw` / `itemsSpec`）**：在组件内部维护 `optionsRaw` 原始字符串缓存；当每帧数据同步写入的选项文本与当前缓存一致时立即跳过重新解析与重建子项，彻底避免动画帧循环中重复反序列化造成的性能损耗以及弹窗滚动条、焦点和选中态被意外重置的问题。
   - **双向绑定语义值与下标自适应（Two-Way Binding Type Adaptability）**：
     - 当从 `SelectBox` 读取值回写模型时（`ChildWindow.BoundValue`），先检查目标模型字段当前是否为数值类型；若为数值，则回写整型下标字符串（`Convert.ToString(sb.Selected)`）；若为字符串或未初始化，则回写语义值（`sb.SelectedValue`，回退至 `sb.Text`）。
     - 当从模型向 `SelectBox` 写入属性时（`SetProp("value", val)`），优先在选项列表中匹配 `opts[i].val == val || opts[i].label == val`；若未命中则回退为数值下标解析，实现语义值与下标的双向平滑转换。
   - **`<option value="...">` 语义合成**：声明式 HTML 里带显式 `value` 属性的 `<option value="bj">北京</option>`，装载器统一探测子级非空自定义 `value`，自动合成 `[{"label": "北京", "value": "bj"}, ...]` JSON 数组并 `SetProp("options", ...)`，原生 `<select>` 语义完整保真。

17. **流式排版、停靠体系与边距内外消费全链路契约（Fill Height / Flex / Margin / Padding）**：
   - **FormBuilder 布局流与容器 Display 属性声明**：
     - 当控件在 JSON 中声明 `flexDir` 时，解析器在生成样式时必须注入 `;display:flex`（若仅声明 `flex-direction` 会导致容器退化为 `display:block` 并进入传统停靠/流式分支）。
   - **自动撑满高度（Fill Height）与停靠语义映射**：
     - 在非 Flex 停靠容器中，撑满剩余高度的语义对应 `c.dock = Dock.Fill()`（`5`）；在 Flex 弹性容器中，撑满对应 `flex-grow: 1` 且 `fh = 0`。
     - `FormBuilder.SetupLayout` 在流式模式（`position == 0`）下，必须优先识别显式 `dock` 属性；当 `flexGrow > 0 && fh == 0` 时自动赋给 `ctl.dock = Dock.Fill()`，避免将流式控件重置为 `Dock.Top()` 导致高度取 0 塌陷。
     - `FormBuilder.AttachNode` 当 `ctl.dock > 0` 时直接挂载到宿主容器，不被 `FbFlow` 匿名网格拦截。
   - **Control.Arrange 停靠与浮动分支的边距消费**：
     - 控件排布引擎 `Control.Arrange` 在处理各停靠分支（Top/Bottom/Left/Right/Fill）及未停靠流式分支时，必须完整消费 `StyleMarL/T/R/B` 边距扣减、自身位置平移与下一项步进累加（`stepH = hh + mt + mb`），确保边距直观生效。
     - 在 `ArrangePositioned` 的 `c.designFloat` 浮动分支中，改用 `StylePadL/T/R/B` 避免边框重复累加，并加上子控件自身的 `c.StyleMarL()` 与 `c.StyleMarT()`。
   - **设计器属性检查器实时脏标记与数据模型同步**：
     - `PropertyGrid.OnChange` 必须显式触发 `Designer.MarkDocDirty()`，使得属性检查器中编辑任何数值均即时触发文档树重建并更新画布预览。
     - `DesignDeclareRect` 同步持久化 `flexGrow`、`margin`、`padding`、`gap` 至 `f.designSource`，确保设计器与序列化快照双向保真。

18. **设计器属性检查器 Visual Studio 规范重构与垂直间距律（VS 分类、高频前置与呼吸节奏）**：
   - **Visual Studio 属性网格分区分级契约**：
     - 属性列表平铺无章会导致翻找成本极高。属性规格（`AllFieldSpecs`）必须通过 `PropSpec.Section` 划分为 4 大标准大类：
       1. `IDENTITY / COMMON`（标识与常用）：`name`（变量名）置顶，`label`（显示文本）紧随其后，配合 `placeholder`（占位文本）与组件类型。
       2. `LAYOUT`（布局与几何）：`dock`（停靠方式：None/Top/Bot/Left/Right/Fill）置顶，随后为 `position`（定位模式）、`flexGrow`（弹性撑满）、`anchor`（九宫格锚点）、浮动坐标 `x/y`、物理尺寸 `w/h`、边距 `margin/padding` 以及容器专属流向/间距。
       3. `DATA`（数据与绑定）：`bind`（绑定路径/数据源）、`bindProp`（绑定属性）及 `of`（表格行实体）。
       4. `BEHAVIOR`（行为与状态）：`required`（必填校验）、`wrap`（自动折行）、`orient`（Tab方向）及 `defOn`（默认开启）。
     - 分类过滤（`inspCategory`）只在选择“全部 (All)”时渲染 Section 分组栏；在单项分类下隐去分组头直接展示过滤后的紧凑属性。
   - **PropertyGrid Section Bar 视觉质感**：
     - `PropertyGrid` 渲染 `PropSpec.kind == 5`（Section 头）时，必须画出专属条带（`t.bgSecondary` 背景、左侧垂直强调色条 `t.primary`、高亮分类文字），与普通属性行形成鲜明视觉阶梯。
   - **快捷工具栏卡片行高计算与垂直呼吸律（Anti-Card-Overflow）**：
     - 快捷按钮（`SmallBtn`）的高度约为 24px。卡片内部放 N 行按钮时，行间步进距必须保持至少 28px~30px（例如 `row1: 8px`、`row2: 36px`、`row3: 66px`），卡片总高度必须为 `app.Scale(96~130)` 并留足底边距，严禁卡片高度不足 78px 强塞 3 行导致按钮垂直重叠、文字压盖与溢出边界。
     - 快捷卡片与卡片之间统一保持 10px 间距；快捷操作区与下方的 `PropertyGrid` 之间必须保持 12px 留白；多选批量卡片（3行按钮+双行文字）必须保留 126px 充足纵深。
     - 避免在顶部堆砌过多卡片导致属性网格被挤出屏幕。常用尺寸预设等次要辅助卡片仅在布局专属分类（`inspCategory == 2`）下展开，全部模式下让位给核心属性网格。

19. **设计器交互防误触与组件大纲树体系（点击防护、双击防跳码与全树精准点选）**：
   - **画布点选与双击跳转防护（Anti-False-Positive Code Jump）**：
     - 在设计器画布上快速点选组件、微调焦点或执行多选时，若双击判定容差过宽（如 400ms），会导致用户正常点选频繁误触发“双击进入代码编辑框”，严重破坏排版流畅度。
     - 画布双击时间容差必须严格收窄至 260ms，空间位移容差限制在 5px 内。
     - 限制双击动作的业务控件白名单：普通文本类控件双击优先触发行内就地重命名（`StartInlineRename`）；仅当用户按住 `Ctrl` 键，或双击对象属于明确命令操作类控件（如 `Button`，`ftype == 1`）且未被其他操作拦截时，才放行 `RequestOpenCodeBehindHandler` 切换 Tab 导航至代码后置文件。
   - **大纲树（Outline Tree）全树精准点选与层级漫游**：
     - 当界面存在绝对浮动（Float）、停靠重叠（Dock.Fill / Dock.Top）、零尺寸占位或深层嵌套（Panel / Tabs / SplitPanel）时，纯靠画布鼠标拾取必然出现选不中或被上层覆盖组件拦截的问题。
     - 左侧面板必须提供组件工具库（Palette）与大纲树（Outline）双 Tab 视图。大纲树以整棵设计文档树为蓝本递归渲染层级结构，节点显式标注控件类型、变量名、停靠方式（`[Top]` / `[Fill]` 等）以及容器/锁定标识。
     - 大纲树节点与画布/检查器保持绝对实时双向同步：单击树节点立即激活对应组件的选中包围框、句柄与属性检查器焦点，右键呼出同构上下文菜单，并提供同级上下微调、一键锁定与折叠展开工具条，彻底消除界面元素“看得见选不中、选得中挪不了”的死角。

20. **高保真桌面软件现代动效与交互架构规范（自主帧循环、数据驱动 SVG 动效、三态拖拽落区与克制美学）**：
   - **自主动画帧循环驱动契约（Autonomous Frame Loop Driving）**：
     - *踩坑*：在 Zan HTML 窗口开发中，使用 `form.Every(ms, cb)` 绑定的定时动画在没有鼠标移动时会停止刷新（窗口卡死在当前帧，必须持续晃动鼠标才刷一帧）。
     - *根因*：Win32 事件循环 `TakeDueFrame` 依赖 `RequestWakeIn` 结合系统定时器，Win32 定时器存在微小提前触发抖动，未到达 due 且 `animNextMs < 0` 时未置 `needsRedraw = true`，导致窗口进入深休眠。
     - *规范*：在动画回调（`OnTimerAnimation` / `OnFrameAnimation`）中必须主动调用 `app.RequestAnimationFrame(intervalMs)`（例如 40ms 对应 25 FPS），持续为 `animNextMs` 赋正值，由帧循环自主驱动下一次重绘，彻底解耦鼠标运动。
   - **纯数据驱动动态矢量 SVG（Data-driven Dynamic SVG Data URI）**：
     - *踩坑*：雷达旋转扫描、同心光晕呼吸、跑马灯流动虚线等复杂动效，若通过频繁操作 DOM 树会引发重排抖动；若手写自绘控件则违反 ZanIDE/HTML 窗口的“禁止自绘”纪律。
     - *规范*：在 HTML 中放置 `<img id="..." class="..." />`，后端通过纯函数 `BuildDynamicSvg(tick, ...)` 生成 `data:image/svg+xml;utf8,<svg ...>...</svg>`，通过 `img.Src.Set(...)` 极速更新。计算纯在数学层面（`stroke-dashoffset`、坐标矩阵、透明度），零布局重排、零自绘违反，画质矢量保真且帧率极高。
   - **桌面端拖拽三态反馈模型（Three-State Dropzone & Avatar Drag Feedback）**：
     - *踩坑*：卡片拖拽若仅靠静态提示或等待 drop 事件，用户无法感知卡片是否已被抓取、当前悬停在何处、落区是否就绪。
     - *规范*：建立三态落区模型（静止态、拖拽进行中流动虚线跑马灯态、落区悬停吸附高亮态） + 悬浮跟随 Avatar（绝对定位，在 `FrameHook` 中每帧读取 `app.mouseX, app.mouseY` 更新坐标） + 释放吸附交互。
   - **界面美学克制与品牌主视觉对齐（Anti-Cyberpunk Overfitting）**：
     - *踩坑*：AI 开发安全卫士/系统工具时，容易臆想出密密麻麻的 PCB 微电路、芯片针脚、暗黑赛博朋克风，破坏桌面产品的现代清爽扁平/微立体质感。
     - *规范*：严格遵循 `gui-design` 规范：纯白卡片底色（`#ffffff`）、极细边缘（`#eaedf2`）、品牌主色（如火绒橙 `#F58220`）、微透明柔和光晕与 2.5D 切面金盾，克制配色与呼吸节奏。
21. **同构集合列表的数据驱动与模板复用架构（Anti-Hardcoded-DOM-List-Duplication）**：
   - **踩坑出处**：在开发火绒安全卫士的“防护中心”（22项开关）、“访问控制”（6项规则）、“安全工具”（11项工具）等大量结构相同但数据不同的卡片视图时，若直接在 HTML 中手写复制数十份静态 `<div class="protect-row-card">` 节点，会导致：
     1) HTML 臃肿失控（单文件 2000 多行，充斥重复代码）；
     2) 排版高度出现细微差异（某些卡片因微小的 CSS 语法、换行或 `flex-shrink` 产生高度不一致，出现参差不齐）；
     3) 状态切换需要分别操纵 22 个子面板或大量 ID 变量，脆弱难维。
   - **规范要求**：
     1) **数据与模板彻底分离**：所有同构列表，严禁在 HTML 中硬编码重复卡片！HTML 只声明纯净的容器节点（如 `<div id="protectItemsList" class="protect-items-list"></div>`）。
     2) **强类型数据模型**：在 Code-behind 中定义扁平微型数据实体（如 `ProtectItem`、`AccessItem`、`ToolEntry`）。
     3) **单一行模板工厂函数**：提供单一的行/卡片构造工厂（如 `CreateProtectRow(item)`、`CreateAccessCard(item)`），统一赋予 CSS 样式类（如 `.protect-row-card`），绑定图标、标签与事件，保证 100% 像素级一致的行高（如 `height: 54px; min-height: 54px; max-height: 54px; flex-shrink: 0; flex: 0 0 54px;`）、内边距与对齐基准。
     4) **统一容器刷新**：数据更新或分类切换时，调用容器 `ClearKids()` 移除旧行后遍历模型列表逐项追加 `AddKid(CreateRow(item))`，内存轻量、渲染瞬时完成。
     5) **主轴滚动容器 flex-shrink 锁定**：当 Flex 容器声明了 `overflow-y: auto` / `overflow-x: auto` 时，它是滚动容器，排布引擎必须跳过主轴收缩压缩（否则 7 项会按 `room/7`、8 项会按 `room/8` 强制压扁，导致切换分类时列表行高剧烈跳变）。行模板根节点必须显式锁定偏好与弹性收缩尺寸（如 `row.Prefer(0, 54)` 并配 `flex: 0 0 54px; flex-shrink: 0;`），确保无论多少条目行高均严格恒定在像素基准线。
     6) **Flex-wrap 换行网格与滚动延伸（Scroll Extent）全貌闭环**：当卡片采用百分比宽度（如 `width: 31%` 或 `flex: 0 0 31%`）并声明 `flex-wrap: wrap` 时，排布测量引擎必须根据宿主换行宽度解析子项百分比尺寸（`StyleWidthIn(wrapMain)`）并累计多行交叉轴跨度（`crossSum + crossGap * (lines - 1)`）。滚动容器（如 `.tools-scroll`）依靠此真实高度计算完整的 `scrollExtent` 并唤出原生纵向滚动条，彻底根除因高度虚低导致下方卡片被窗口硬生生裁切成半截且无法滚动的恶性排版缺陷。





