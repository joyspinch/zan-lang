# HTML 窗口开发定式（.html 设计稿 = 唯一格式）

窗口的布局/内容/样式进设计稿，交互与数据在 code-behind。编译期
HTML 通道（GenForm/GenHtml）已整体删除——**设计稿不是编译输入**，
zanc 对 .html 显式拒绝；唯一通道是运行期装载（2026-10-31，B-ID44）。

## 唯一建树通道：运行期装载

- **约定式窗口**：`class MainForm : Form` 与同名 `MainForm.html` 配对，
  `new MainForm().Show()` 执行文档装配、App 初始化、模型与事件接线、
  `OnLoad` 和循环。零参 `Form` 构造不初始化 App，不应直接 `Run()`。
- **已有宿主中的页面**：`Gui.Html.Load(app, html, handlers, baseDir)`
  返回树，文档作用域 CSS 自动并进 App，导航自动接线；`Html.Parse`
  内自动 `Install()`，调用者不用手工安装声明层。装载不会自动挂父树
  或注册模型；由宿主 `Add(ui)`，按父布局声明根的增长，再挂所需 binder。

- **发布保护**：设计稿用 `--embed <file>=<别名>` 随 exe 烘入（压缩 +
  加密，运行期 `File.ReadAllText` 内嵌优先、内存解出，**不落盘**、
  不解压临时文件）。
- **窗口属性**：`data-win-title/win-w/win-h/win-chrome/win-center` 等
  文档键由 `Form.Show` 装配读取和应用；显式 `Html.Load` 页面入口不创建窗口，
  不消费宿主窗口键。
- **组件显式注册**：标准组件由 `ControlBootstrap.Install()` 幂等安装，
  在同一处维护70项 kind→构造器注册，包含 `RadioButton`；HTML 与代码工厂
  查询同一运行期注册表。扩展组件在自己的显式安装入口调用 `HeavyControls.Register`
  或 `ControlFactory.Register`，宿主负责安装；不依赖扫描 `.html` 生成第二份注册名单。
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
  量成几十 px 折行。**先核对父级布局上下文，再给子项 grow**：普通 `Panel`
  走 flow，子项的 `flex-grow` 不会分配父级剩余高度；即使 Panel 已有非零高度，
  内层列仍可能仅按内容高排列。沿受限高度的祖先链建立 flex 上下文，例如外层
  `display:flex; flex-direction:column`，内层列与文本区用 `flex-grow:1;
  flex-basis:0; min-height:0`；不要用固定大高度掩盖断链。素材工具已确证输出
  Panel 为 `300×936`、内层列仅 `300×24`、JSON 为 `300×0`，局部补父级纵向
  flex 后 direct `162141Z` frozen pass。回归在真实输入准备后、首次 Paint 前
  分别断言文本区宽高（保留可读高度阈值）、文字非空且等于当前模型；真实 native
  canvas 另存画面审查字形、裁剪与标签切换后的内容。离屏几何/文本 oracle 通过
  不等于画面已验收，canvas 证据也不证明锁屏外桌面或 OS 输入；后续实测状态见
  本技能 references/ 内的素材工具证据边界。
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
| `data-bind="路径"` | 绑定模型字段，由 ModelBinder 双向同步；HTML 和代码树共用 | Element 默认 text，其他控件按 PropSpec 元数据与模型真实类型选择默认投影，显式 bindProp 优先；须实际挂 ModelBinder/ModelHosts，不能仅绘制树 |
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

- **子窗口**：`ChildWindow.SetRoot(tree, model)` + `Wire()`，窗口持有的
  ModelBinder 登记到该窗口的 App；ChildWindow 与 Form 都驱动各自 App
  的模型宿主同步。
- **主窗口里的页面**：`new ModelBinder()` → `SetRoot(tree, model)` →
  `Wire()` → `ModelHosts.Register(app, binder)`；Form 已驱动该 App 的
  模型→UI 同步与 UI→模型回写，页面不必自己写帧钩子。换页时调用
  `ModelHosts.Unregister(binder)`。兼容批量入口必须显式传 App：
  `ModelHosts.SyncAll(app)` / `ModelHosts.WriteBackAll(app)`，无参版本已删除。
  页面也可用 `UiViewScope(app, owner, tree)` 的 `Mount(parent)`、
  `Bind(model)`、`Handle(name)` / `Action(name, sender)` 组合装配，换页时
  `Retire()`。Bind 只创建、注册 binder 并 Wire，模型阶段由标准宿主共享
  prepare 驱动；独立区域在 owner.PrepareView 中 `view.Layout(...)`。
  scope/输入宿主须在输入准备快照之前创建并 Bind；BeforeInput 内首次登记会改变
  generation，使旧准备失效，不能继续旧尾部或等到 Paint 才安装。
- **自定义宿主与手动帧**：无 AppHostLayout 时，App 共享准备先执行
  `SyncModelHosts()` 再推进区域布局，已准备的 null-host 帧在
  `RenderFrameOverlays` 末执行 `WriteBackModelHosts()`；原生 PresentFrame 与
  offscreen 共用此边界，Form/ChildWindow 保持既有模型阶段。手动帧必须在
  BeginFrame 和业务更新后、绘制前检查 `PrepareStandardHostFrame()`，并在
  每个业务绘制回调后及呈现前检查 `ContinuePreparedFrame()`；任一 false 就
  终止旧帧尾部与呈现，null-host 同样校验 hostLayoutRevision。页面不重复泵模型。
  该自定义宿主模型阶段已通过严格 probe；HUD、Designer、DataTable 等消费方
  仍须独立验收。完整契约见本技能 references/ 内的共享机制正文。

由此三条选路规则：

- **值/显隐/列表内容 → 改模型**：JsonValue 使用
  `model.Set("title", JsonValue.NewStr(title))` /
  `model.Set("showBag", JsonValue.NewBool(showBag))` / `model.Set("items", items)`；
  items 是 JsonValue 数组。更新必须用 Set(key, JsonValue)：Put 与
  PutStr/PutInt/PutDouble/PutBool 等助手只追加，重复键 Get 取首值，用它们更新会继续
  读旧值。末尾 `app.RequestRedraw()` 出帧，不虚构 SetStr/SetInt 等公开方法。
  别既改模型又 SetProp 同一控件，帧后回写可能把模型改动顶回去。
- **结构（增删控件/换组件类型）→ 命令式**：retained 树 API
  （`Find` + `Add`/`InsertAt`/`Remove`/`SetProp`）或整页
  `Gui.Html.Load` 重装。已知代价：重载是**整树无 diff**（手工 Add
  的控件不在新文档里）；宿主须替换旧树并注销旧 binder。列表高频
  更新留在模型通道，带 data-key 的行可走增量更新。
- **模型宿主可用于任意控件树，但登记需要宿主泵**：`Gui.Html.Load` 装出的
  文档树与代码树共用 ModelBinder；注册到明确 App 后，由该 App 的标准共享
  prepare 执行同步/回写。仅创建 binder、Bind 或 RenderTree 不会自行运行模型阶段，
  自定义手动帧须遵守上述 prepare/Continue 契约。

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
Control ui = Gui.Html.Load(app, html, null, ""); // html 来自文件/内嵌资源
ui.SetProp("style", "flex-grow: 1; min-height: 0;"); // host 使用 Flex 布局
host.Add(ui);                           // host 是已有的页面宿主
// 2) 挂模型宿主：事件按名字接线 + 绑定声明从此生效
ModelBinder b = new ModelBinder();
b.Handle("save", () => { ... });          // 无参
b.HandleArg("pick", (string s) => { });   // 带参（data-arg）
b.HandleSender("open-row", (Control c) => {
    object row = b.ScopeOf(c);            // 行内控件 → 行模型项（按实际模型类型使用）
});
b.SetRoot(ui, model);
b.Wire();
ModelHosts.Register(app, b);            // 主窗口的每帧泵驱动它
```

- **装载器不带处理器表**（`Gui.Html.Load(app, html, null, dir)`）：事件统一
  由 binder 的 `Wire()` 接线。装载时若也传 handlers，同一个控件会有两
  条订阅，一次点击触发两遍。
- **注册顺序：先 `Handle` 后 `SetRoot`**：早期 `SetRoot` 会重建处理器注册
  表，把之前注册的处理器全冲掉——页面照常渲染、按钮照常画出来，**点击却
  毫无反应且不报任何错**（坏的是看不见的部分，最难查）。现在 `SetRoot`
  只挂根与模型，注册表仅在为 null 时才建。
- **声明层自动安装**：`Gui.Html.Load` 经 `Html.Parse` 自动调用
  幂等的 `Install()`；页面入口不再调用 App 上的 HTML 装载方法。
  装载后仍须挂树；使用模型时还须注册 ModelBinder。
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

代码树也可配置 bindPath/bindProp 并挂同一 ModelBinder；模型路径不限于对话框或
JsonValue。`Binding<T>` 提供强类型字段直绑，两者都通过控件公开属性承载值，
不构成不同 UI 创作模式。

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
审计脚本与 `references/css-dialect.md` 的与浏览器差异清单；"改了没反应"先怀疑
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

## 浮动件的定位基准（框架层规则）

CSS absolute 与 `Control.Float(x, y)`/`Place(x, y)` 用同一定位语义，
坐标依据父包含块与 `ArrangePositioned` 的实际实现。普通顶栏、内容区、
页脚按 Flex 文档序排布；不要依据已退役的 Dock.Top 推算自动避让。
设计器把流式件转为 absolute 时，应保持当前视觉位置并写 CSS 定位，
预览与运行时必须共用同一棵控件树和包含块换算。此处未重跑像素验证。

## 颜色属性接真取色器：色块即触发条，写回走 CSS 颜色文本（框架层硬规则）

- **色块是 `ColorPicker` 的触发条，不是自绘色块**：设计器属性行的颜色
  属性用取色器本体当触发条（皮肤透明底 + 当前色），点开就是 SV 面板 +
  色相/透明度条 + RGBA/HEX/HSL/HSB 输入行。自绘色块配"点击循环预设色"
  是死路：用户挑不了颜色，只能在几个预设之间跳。
- **弹层锚点读取实际布局**：控件树 Arrange 后的 `bx/by/bw/bh`
  是弹层定位依据；不在应用侧仿造 RenderIn 路径或另建命中与锚点体系。

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

- **预览共用完整运行时树与布局**：选框、拖动、缩放、换父读取实际
  `bx/by/bw/bh` 与父包含块。完整结构遍历 children；布局流视图会排除
  absolute/hidden，不能把不在流视图里的控件判定为丢失。

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
- **最终窗形与运行窗口共用遮罩**：逐 region 计算并集，圆角 SDF 保留
  `min(max(qx,qy),0)` 内部项；旧步进曾跳过后续区域，缺内部项使 radius=0
  整块半覆盖。区分 straight/premult，预乘缓冲须满足 RGB≤A：
  `0x80FF0000 / cov128 → 0x40FF0000 → 0x40400000`；只乘 coverage 曾造成
  亮边。快照只读借用 surface，复制到自有缓冲后裁切，避免污染原帧。
  Layered 输出先 `a' = floor(a * coverage / 255)`，再逐通道
  `rgb' = floor(rgb * a' / 255)`，保留两次截断，不合并成一次乘除。
  shape snapshot 保持 straight RGB，仅按 coverage 更新 alpha；预乘只发生在
  Layered 输出边界，不得将预乘像素写回借用 surface。
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
  复刻主窗装配（Gui.Html.Load 主窗文档 + 各页宿主挂页），跳过登录泵。
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
  就给对方造出一条通知——`Im` 发信即 `Notify.Send`；建空间/建表同理）；
  ② 客户端点完，**回服务端查状态变了没有**（未读数、记录数、版本号），
  光看像素不够——像素只证明"画出来了"，不证明"写进去了"；
  ③ 点击坐标从 `dump tree` 的 JSON 里按控件名取矩形中心
  （**顶层是 `{"root":{...}}`**，不是直接一棵子树；`name` 为空的行要靠
  容器节点的 kids 顺序认）。驱动脚本是静态文件：坐标只能来自**上一次**
  运行的树 dump，所以"定位"与"点击"天然分两轮跑。
- **别用字节值拼中文测试数据**：`[char]0xE8` 这类写法把 UTF-8 字节当成
  码位，服务端收到的就是乱码——排查"乱码"前先确认**造数据**的那一步
  用的是码位（`[char]0x8054`）还是字节（`0xE8`）。

## 游戏接入边界

游戏 HUD、血条、伤害数字、小地图 UI、菜单与 SceneDesigner 使用标准
控件树（HTML/CSS 或声明式对象树）；像素绘制封在控件 `OnPaint` 内。
高频刷新通过长生命周期控件、变值更新、脏区和缓存优化，不建立应用侧
直绘 HUD 或自制命中区体系。游戏世界渲染保留 Zan.Game 的专用渲染路径。
此处是开发规则；旧调用方是否完成迁移须分别核对，不能由文档规范推断。

- **GuiHost 退出走宿主生命周期**：游戏回调调用 `host.RequestStop()`；不要只写 `host.App().isRunning = false`，因为 `GuiHost.Run` 由 `host.running` 控制循环，只改 App 标志不能结束宿主。原生验证须确认真实窗口收到退出输入后正常结束，并在超时 sentinel 前退出。
  Match3 限定证据：`build/test-out/gui-native-pair-20261113T084058.019920Z/match3-escape/run.json` 记录可见 HWND、exit0、空 stderr、无 driver/runtime diagnostics，棋盘选择边框改变30个白像素；driver log 在 Esc 后结束，未到 `ESC_NOT_HANDLED`。同批 `match3-discovery/initial.png` 已目检 HUD 正确。Survivor/FPS 虽迁移同一修法，尚无各自原生证据；GUI form 回调当前失败，不属于这项语义验收。

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
   - **属性元数据驱动模型回写**：双向绑定按显式 `bindProp`、实际模型类型与 `BindingSpec` 选择属性，经公共 `GetProp` 读取；`WriteBoundValue` 根据 `PropSpec.ValueKind`、`bindNumber` 及既有模型值类型确定 bool/数值/字符串回写，不维护具体控件类型名单，也不限于空 JSON 节点。新组件声明默认绑定属性和值类型即可复用该协议；显式绑定属性优先。
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
   - **索引、选项键与标签分开投影**：ModelBinder 按 PropSpec 默认投影与模型真实类型选择属性，显式 bindProp 优先，HTML 与代码树共用分派。SelectBox 的 `value/selected/data` 只读写索引，`selectedValue/val` 只读写选项键，`text/label/selectedText` 读写标签；键为空时按现有 SelectedValue 契约使用标签。数字文本键“0”不能被当作索引0。索引 null/空串/-1 清空，非法或越界索引保持原选择。完整 `model_binding_equivalence_test` 已严格验证 JSON、字典与 POCO 的文本键、数值、bool、零值和显式投影。
   - **`<option value="...">` 选项合成**：带显式 `value` 的 `<option value="bj">北京</option>` 由装载器合成 `[{"label": "北京", "value": "bj"}, ...]` 并设置 options；这声明选项的键，不改变控件 value 属性的索引协议。支持范围以实际属性与回归为准。

17. **流式布局与盒模型契约**：
   - 容器同时声明 `display: flex` 与所需方向；只写方向不构成 Flex 容器。
   - 需要贴底时父容器定高，内容 `flex-grow: 1; min-height: 0`，底栏
     `flex-shrink: 0`；子项遵循文档序。历史数字 dock 不构成停靠机制。
   - margin/padding/gap 通过样式声明进入同一计算路径，设计器预览
     与运行期共用解析和布局，不能再指导调用已删除的 JSON 建树实现。
   - 属性修改需同步持久化并标脏；验收比较当前布局树的实际矩形。

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

22. **声明式窗口与单管线装载规范（B-ID149：`class X : Form`、`<include>` 组合与零胶水建树）**：
   - **窗口即对象**：`class X : Form` 与同名 `X.html` 配对，通过
     `new X().Show()` 完成装配，不绕过装配直接调用 Run。
   - **事件反射按名自动接线与绑定对账**：树上所有事件（`@click="OnSave"`）按名自动匹配实例方法，缺失即 fail-fast 退出；`data-bind` 路径由 `CheckBindsDoc` 启动期静态对账，杜绝静默失败。
   - **模板脚手架约束**：新建项目模板中的入口类必须声明为 `partial class {{NAME}} : Form`，严禁遗漏 `{{NAME}}` 占位符或 `: Form` 继承，否则编译器入口合成（`new <基名>().Show()`）将因类型未定义中断。
   - **`<include src="...">` 组合化**：支持在 HTML 中通过 `<include src="fragment.html" />` 组合引入组件化片段，装载期展开（深度 8 封顶防环），片段内事件与数据绑定对宿主窗口类同构生效。
   - **窗口键与子窗模态**：根/body 的 `data-win-title/w/h/center/chrome/round` 装配期消费（`:data-win-*` 走 bindProps 泵）；子窗 `new` + `Show()`，归属语义设 `w.owner = this` 后 `w.ShowDialog()`（关闭后宿主补 `MarkDirty()` 重绘），`Show()` 恒为非模态。
   - **publish 框架钩子默认自动保留 thunk（B-ID160 编译器根修闭环）**：`OnLoad(Form)` 等框架按字符串名反射回调的钩子方法，在 `--publish` 形态下编译器原生识别 `On*` 框架生命周期钩子规范（`irgen_reflect.c`），默认生成 Invoke thunk 与反射导出并保留为链接 root，无需手动附加 `[Custom]` 注解。
   - **ChildWindow 子窗同类名约定**：`EnsureDesign()` 按"类名 ↔ 同名 .html"装载（覆写 `DesignName()` 可自定义名），与 Form.Show 同源；开发态（plain 构建）散盘扫描从工作目录与 src 树递归找。注意 `Directory.GetDirectories` 返回**裸目录名**（stdlib 刻意口径，与 GetFiles 全路径刻意不同）——递归扫描必须手动拼回父路径，否则嵌套设计稿永远 MISS。
   - **ChildWindow 装配序（与 Form.assembleDoc 同语义）**：`EnsureDesign` 载树并默认挂**窗自身**为模型（实例字段即 ViewModel、方法即事件处理器；Build 里 `SetRoot(root, mdl)` 换挂 JsonValue 等模型的尊重之）；`OpenHost` 在 host 就绪后做声明式装配——`@click` 等按名对账接线（缺失 **exit 1** 列名）、bind 路径启动期对账（JsonValue 模型的键不是反射字段，跳过对账）、`OnLoad` 钩子 `Invoke` + 标脏。双模型语义：**事件恒解析窗方法，bind 走当前模型**。publish 形态 `OnLoad` 默认自动保留 thunk（同上条契约）。
   - **html 事件处理器在 publish 自动保留**：zanc 扫内嵌设计稿的 @x / v-on / z-on / data-on / on* 处理器名，按名保留反射条目、thunk 与函数体，处理器无需 `[Custom]`；框架按名调的钩子（OnLoad 等）由编译器原生识别 On* 钩子保留，亦无需 `[Custom]`。
   - **反射方法表含继承方法**：HasMethod/Invoke 能命中基类方法（名字级遮蔽、最近声明胜出，基类静态方法除外），html 处理器可声明在基类窗上。
   - **`OnCloseRequested` 里可以再调 `Close()`**：Close 先置 open=false 再回调，重入早退，不会互递归。
   - **`(Label)r.Find(id)` 只对 `<label>` 成立**：`<span>` 不产出 Label；`<label><input/></label>` 里的 input 取不到，复选框文字写 `<input type="checkbox" value="文字">`。子类窗没有同名 html 时覆写 `DesignName()` 指向基类设计稿。
   - **html 裸布尔属性通道（`readonly`，与 `disabled` 同语义）**：html 里裸写 `readonly` 出现即生效，解析期直落 `SetProp("readonly","true")`。键名走 PropSpec **精确匹配（区分大小写）**：TextArea 的键是小写 `readonly`（与 input/rate 先例对齐）——历史键名 `readOnly` 双重失配（键名不命中 + 裸属性无值按 off 判 false）会**静默失效**。契约钉在 GUI 一致性用例（cwdocscan；TextCw：初始推送/一帧同步/readonly 命中三断言）。

23. **Retained 属性、内容与克隆的五条边界（2026-11-13 属性 case 验证）**：
   - **options 的全局 closable 不固化为逐项状态**：Tabs 从 options 建项时逐项 `closable` 为 false，有效关闭状态由逐项 flag 与 `closableAll` 组合；全局 true→false 必须能移除仅由全局开关产生的关闭按钮。显式逐项状态独立保留；此规则不改写 `Add(string)` 默认取全局值的既有语义。
   - **Add/AddPane 使用独立装配游标**：options 预声明标签，`pendingPaneIndex` 决定后续 AddPane 消费哪项，不能用 `pages.Count` 推断；提前创建命名页不应消费标签。直接 Add 结束预声明消费阶段，之后 AddPane 追加；保留 early activeTab。检查 Add→AddPane、options→提前取页→AddPane、options→Add→AddPane 三种顺序。
   - **clone 区分 own node 与业务内容，并配齐工厂注册**：普通 `Html.Clone` 先回放 Props，再调用 `CloneContentTo`；setter 可能已生成 caption/icon/panel，内容 hook 须去掉回放脚手架，恰好一次恢复业务 children/slot，并让回调归目标树。每个经通用 clone 的具体 Kind 都须注册工厂（如 CollapseItem），避免返回 null 后静默丢子树。空树、独立子控件、自定义 slot 和克隆后再次修改都要覆盖；Tabs 重建导航/内容壳，不重复复制生成节点。
   - **语义样式用独立 canonical key**：Tabs 用 `tabStyle` 供 Props/GetProp/clone，兼容 `style=card/segment` 时只拦截声明支持的枚举值；inline CSS 继续走基类。验证 CSS getter、实际 padding/background 以及随后改语义样式时 CSS 仍保留，不能把 style 设为吞掉任意 CSS 的普遍 alias。
   - **比较已应用投影，正常更新复用 Binding**：公开字段或 Binding 可绕过 setter，early return 要比较已应用状态/实际节点；同值字段不代表投影已更新。已有文本 Binding 用 `.Set(...)`，仅 null 时补建；同色但旧 gradient 尾色未清仍须失效缓存。只撤销控件负责的颜色投影，保留业务节点自定义色；生成节点被移除/换 parent 后放弃旧身份，未变化时保留节点。Binding 身份/订阅持续性没有独立计数断言，不据此声称零分配。

   验证定位：`tests/conformance/retained_property_contract.zan` 的 `case_retained_property_contract`，在
   `build/test-out/gui-refactor-old-targeted-20261113-064300-431057/result.json` 与
   `build/test-out/gui-refactor-old-targeted-20261113-065950-731288/result.json` 中均正式及同 exe strict 通过，
   精确 stdout、空 runtime stderr、无泄漏、exe 不变，批次输入及编译器无漂移。
   只覆盖各收据版本的属性用例；不据此认定后续 HTML wrapper/data-child-tab 装配、Designer 或整个 Gui 已验收，
   也不推导总体精简、普遍提速或零分配。

24. **预览客户区、取消重入与回调保活（2026-11-13 Designer fixture 验证）**：
   - **客户区根约束检查实际几何**：预览文档承担完整客户区及命中裁剪范围时，用 `position: absolute; left: 0; top: 0; right: 0; bottom: 0;`，不要仅凭百分比宽高认定已铺满；断言实际 bw/bh 等于父 viewport 扣除 inset 的尺寸。此约束针对预览根，不要求普通页面都绝对定位，也不让布局负责安装输入根。
   - **取消旧手势须在回调前完成，不留旧清理尾部**：`App.CancelRoutedPointer` 保存旧 target 后，先退休 capture/down/hover、click history、long-press 和 press 状态，再通知 PointerCancel。回调可能建立新 press/capture，回调后再清旧状态会抹掉合法重入。用取消回调建立替代按压的回归验证，同时检查物理 release、专用 Click 一次及 drain 不重复。
   - **Hover 回调后复核输入身份**：Leave 前先注销旧 hover；Leave/Enter 回调后，复核
     App 仍运行、事件序列、输入根代次、手势票据、活动输入根及当前 hover 身份，
     再继续 Enter/Move。目标须仍属于输入树，其实际祖先须属于当前 App、可见且启用；
     adapter 以输入成员归属为准，不要求 parent 链抵达输入根。嵌套回调建立的新 hover
     不得被旧尾部覆盖；旧目标失效时不得继续派发旧 Move。
   - **观察引用与子控件回调避免保活环**：仅观察 sender 身份的 fixture 字段用 weak，避免 control→闭包→observer→control；Designer 自持 Palette TreeView 的回调捕获 `WeakReference<Designer>`，触发时取 Target、判空后临时调用，避免 owner→child→callback→owner 环。weak 不负责保活，也不表示整条 GUI 引用图都应弱化。

   定位：`packages/Zan.Gui.Designer/src/Gui/Designer/Designer.Preview.zan`、`Designer.Palette.zan`，
   `packages/Zan.Gui/src/Gui/Core/App.VisualTree.zan`；回归为
   `tests/gui/designer_interaction_test.zan` 的 root viewport、SlotsAndReparent、click/drain 与 PreviewCancelReentry。
   直接运行收据 `build/test-out/gui-designer-direct-20261113-080718/result.json` compile0/run0、strictPass true；
   正式 `conformance_gui_designer_interaction` 收据
   `build/test-out/gui-refactor-old-targeted-20261113-080832-559569/result.json` 于08:10:27关闭，success true，
   正式及同 exe strict 通过，精确 stdout、空 runtime stderr、无泄漏、exe 不变，输入及编译器无漂移。
   此结论只覆盖相应 fixture 版本，不替代产品构建门、原生窗口实机或整个 Gui 验收，也不推导零分配或普遍提速。

## CSS 合并隔离与 HTML 文本首测量

- **声明块必须递归 Clone**：`Css.MergeInto` 展开分组选择器时，每个选择器须得到独立声明块；`StyleSheet.MergeSheet` 合入时同样递归 Clone，隔离源表与目标表。浅拷贝会别名共享 `__zan_lengths` / `__zan_prescaled` 等内部状态，公开 CSS 字符串虽正确，有效几何、字号与颜色仍会被串改。验证须检查分组规则及源表隔离后的实际样式与布局，不能只比较字符串。
- **设计稿文本走语义 SetProp**：HTML 装载写文本须调用控件 `SetProp`，不能直接 `PropSpec.Write`；语义 setter 在 `MeasureTree` 前创建 retained generated caption，才能让首次自然尺寸稳定。直接写属性值可能读回正确却缺少 caption，不能靠先 Paint 一次补齐。验证应在未绘制时断言 caption 与首测量，并检查重复测量尺寸不漂移。

证据按用例限定：`conformance_gui_match3_retained_hud` 在
`build/test-out/gui-refactor-old-targeted-20261113-163236-258876/result.json` 中正式及同 exe strict 通过；
该批次整体失败，不能写成整批通过。`case_retained_property_contract` 的通过收据为
`build/test-out/gui-refactor-old-targeted-20261113-163906-501763/result.json`，同样正式及同 exe strict 通过。
两用例均精确 stdout、空 runtime stderr、exe 不变，所在批次输入及编译器无漂移。
这些 strict 证据只覆盖对应 fixture；Match3 的原生证据及边界见“游戏接入边界”，不外推为 GUI form 或整个 Gui 验收。

## 焦点业务通知与结构退休（2026-11-13 定向验证）

- **Focus/Blur 跟随已提交的焦点转换，与绘制无关**：`FocusManager.SetFocused` 先提交 id、endpoint 与 IME，再执行旧控件 `OnInputFocusLost` 清理及业务 `Blur`，最后按 revision 检查派发当前 `Focus`。清理 hook 重入也必须兑现旧转换欠下的 Blur；回调即使绕行后回到同一 endpoint，旧尾部仍须由 revision 截断，防重复 Focus。通知沿 `TriggerEvent` → `WidgetEvents.FireByName` 仅 Peek 已有惰性槽，不为未订阅事件建槽；首次绘制前可通知，重复绘制不补发。
- **退休在结构提交后定向核对旧子树**：`Control.Remove/RemoveChildren` 完成摘除、`App.SetRootControl` 安装新根后，再 `ReconcileFocus`；只处理属于此次旧移除子树且未重新托管的当前焦点，不全局清扫所有暂未挂树 endpoint。保留回调显式选择待挂载新目标的既有兼容语义；新 capture 也不能豁免未变的旧焦点或旧 overlay。
- **批量移除的临时 fence 覆盖整批旧子树**：全部结构移除提交后，在同步清焦回调期间阻止向这些尚未重新托管的节点设焦，防 Blur 内 A→B→A 恢复已脱树焦点；嵌套退休合并 fence，finally 恢复外层。合法重新托管的目标可保留，不把 fence 变成永久禁用。

限定证据：`build/test-out/gui-refactor-old-targeted-20261113-200834-041767/result.json` 中
`conformance_gui_retained_focus_lifecycle`、`conformance_gui_overlay_anchor_retirement`、
`conformance_gui_control_host_lifecycle` 三个既有用例均 officialPass/strictPass，正式通过后同 exe strict 复跑通过，
精确 stdout、空 runtime stderr、exe 不变；`changedInputs: []`、`compilerUnchanged: true`，
编译器 SHA-256 为 `f51abfc8103be942c459c3761095d97bd15814d83b59046ddeb6b4d21d48590c`。
结论只覆盖该收据的三个 fixture 版本，尚不构成原生窗口实机或完整 Gui 验收，也不推导普遍提速或零分配。

## 关闭回调的 ARC 所有权（2026-11-13 定向验证）

- **在真正停循环时退休关闭回调**：`App.StopLoop()` 清空 `closeRequest` 与 `closeGuard`，打断 owner → App → 回调 → owner 环；不要在首次关闭请求时提前清空，否决关闭与异步清理期间仍需保留关闭语义，pending 请求只通知一次，排空完成后才停循环。
- **执行中的委托由局部强引用保活**：调用前分别取 `Func<bool> guard = closeGuard`、`Action handler = closeRequest`，再调用局部；guard/handler 内重入 `StopLoop()` 会清掉字段，局部必须保护当前执行及其捕获对象，直到调用返回。
- **LSP 在服务线程汇合后释放宿主回调**：`Shutdown()` 先标记 stopping、取消并解绑编辑器；`ShutdownAsync()` 等 `serviceStopped.Task` 完成后，仅在 `stopping` 时清空 `onDiagnostics` 与 `uiDispatcher`，打断退休 session → 诊断回调 → IDE 环。单纯观察完成不应清掉未 stopping 会话的回调。

源码已确认上述 owner 环；弱引用探针验证停循环释放、否决保留、pending 一次通知、委托重入保活及 LSP 汇合后的释放边界。
strict 探针收据：`build/test-out/gui-close-probe-20261113T130446.414248Z/close-probe-report.json`（两探针通过）。
正式 App 用例 `conformance_gui_app_post_lifecycle` 及同 exe strict 通过：`build/test-out/gui-refactor-old-targeted-20261113-210647-706765/result.json`；
正式 `lsp_session_editor` 通过（119.05 秒）：`build/test-out/gui-close-lsp-formal-20261113T1307.log`。
旧 native IDE 正常关闭报告仍为 52,268 个对象；新 exe 的原生关闭验证尚待完成。这些源码与 fixture 证据不证明该总量已消除，不得写成 native 泄漏已修复。

## 关闭排空的 Task 契约（2026-11-13 定向验证）

- `Task` 是有类型的任务值，不与整数比较，不用 `return 0` 表示空任务或已完成任务。
- 本次已观察到 async property 结果直接转交后 `Wait` 未形成有效启动/回收链，TCS owner 残留；不可将 `Wait` 当成显式启动，未将其定为转换缺陷，也不外推所有 async property 的行为。
- 非阻塞 drain 可同步登记 completion，再返回 `Task.Run(this.WaitForDrain(completion))`；具名 async 方法在 completion 非空时 `await completion.Task`，由 `Task.Run` 显式注册并调度。
- async 入口只建帧；active async resume 内的 `await` 挂起当前帧，普通函数上下文的 `await` 才走同步 pump。
- drain 回归勿在 `Task.Run(Action)` worker 内嵌套 `Task.Run(WaitGate(...)) + Wait()`，以免同步泵制造 drain 依赖环；取得 drain 后由测试独立推进 Complete。
- DONE 可早于帧尾声释放；弱 owner 检查用有截止时间的 eventual 断言，轮询时避免强引用保活目标，不要求即时清空或无限等待。
- 导航 strict probe：`build/test-out/navigation-work-20261113T141215.704486Z/report.json`，编译/运行 exit 0、精确 marker、runtime stderr 空、changed_inputs=[]；编译警告 W2072 不属于 runtime stderr。
- fresh native：`build/test-out/gui-ide-native-20261113T141513.882231Z/result.json`，result=capture-complete、exit 0、stdout/stderr 均 0 bytes、diagnostics=[]，raw_close=true 且 visible；结论限于本次捕获的正常关闭路径。

## 双范式布局：本次分配宽度与内在测量边界

- **分配宽度是本次测量状态**：`MeasureTree(App app, int width)` 接受已分配的 border-box 宽度，以独立状态保存并在 `finally` 恢复，不写回作者的 `HintWrapWidth`。`RenderInside`、带宽度的 `RenderAt`、`UiViewScope.Layout` 在测量前传实际宽度；换行 Label 用当前内容宽计算高度。子项 auto 宽度先扣水平 margin，显式 content-box 尺寸先按 min/max 钳制再加 padding/border，避免窄宽切换残留旧高度或盒模型少算。
- **分配与重测共用布局事实**：Grid 显式传递当前 `App` 宿主，列宽确定后按 slot 重测子树再求行高，不依赖脱树 HTML 节点原先保存的 `app` 字段。nowrap Flex 行的约束测量与排列共用 `FlexLineSizes`；wrap Flex 行按当前内容宽重新分行、累计高度。HTML 与声明式对象树必须比较实际几何和滚动范围，不能只比样式字段。
- **直接测量保留显式样式边界**：完整子树约束重测仅在 scoped `MeasureTree` 内启用；`measuringTree` 保存旧状态、置位后执行，并在 `finally` 恢复。直接调用 `MeasureGridContent(app)` 保留调用者显式提供的 `computedStyle`，不触发重新解析样式的子树测量。
- **内在遍历不重复启动约束遍历**：确定列轨的 Grid 延后内在子树测量，直接按列槽测量；内在测量 flag 阻止内在遍历再次触发约束递归，消除该路径的指数式重复。嵌套且依赖内在尺寸的树仍可能二次增长，不能推导全树线性、零分配或普遍提速；源码精简仍未完成。
- **状态栏用公共单行布局承诺几何**：`statusbar` 基线使用 Flex row、交叉轴居中、overflow hidden，直属 Label 声明 `min-width:0;flex-shrink:1`。一个弹性 spacer 分隔左右组，右项用标量 order 从右向左扩展；公开索引保持添加顺序并排除 spacer，Clear/重建只保留一个 spacer。检查实际矩形在栏内、同排居中且互不重叠，覆盖左右混合插入、窄宽往返、动态文本及多 DPI；裁剪不能替代正确布局。
- **显式最小尺寸替代自动内在下限**：Flex 收缩在没有主轴 min 声明时才使用 `MinContentMain`；显式 `min-width/min-height`（含 0、正值、百分比）经既有 clamp 路径替代该自动下限。最终尺寸仍按可让空间分配，回归断言最小值、相邻关系和总尺寸，不要求每项恰好等于正最小值。
- **回归用真实文本与状态往返**：覆盖 100/150/200 DPI、窄→宽→窄、真实长文本、缩短/再增长、RemoveAll/恢复及滚动范围；覆盖 `minmax(0,1fr)` Grid、nowrap Flex、根声明宽与实际宽冲突、margin/content-box 与 wrap Flex。七层嵌套的叶子测量次数 `<= depth + 2` 只约束对应夹具，不证明任意树复杂度。

直接 strict 探针定位：`scroll_routed_input` 的 `build/test-out/gui-layout-20261113T160722.781351Z/report.json`，
以及 `view_scope` 的 `build/test-out/gui-layout-20261113T160634.712937Z/report.json`，两者 compile/run exit 0、success true。
直接探针只证明相应夹具；正式收口及源码版本边界见本文件「双范式布局：本次分配宽度与内在测量边界」小节，各用例通过不构成整个 Gui 验收。

## 复合控件的布局与真实输入（2026-11-14）

- **嵌套按钮保留最近子控件的捕获**：按钮冒泡处理 Down 时只在当前没有捕获者时捕获自身；Up 只释放自身持有的捕获。Tabs 关闭图标的 Down 必须捕获关闭 Button，Up 合成一次关闭；直接调用 Click 事件绕过这条路径，不能验证捕获正确。
- **CSS 对应实际生成节点**：Tabs 标题与关闭目标是 `button.tab-item`、`button.tab-item-close`，伪元素规则不能代替真实子控件样式。分别验证 line/card/segment、横竖方向、选中边框与图标中心；关系选择器的独立 ResolveStyle 查询须提供当前节点上下文，或读取当前布局的 computedStyle。
- **长标题约束整条祖先链**：标题可收缩、关闭按钮不收缩，并同时约束导航列与 header 的最大宽度；只给 header 加百分比上限仍可能随过宽导航列一起溢出。自动显示选中项按 ScrollViewport 的可见内容矩形计算，包含边框和滚动条的外框不能作为可见边界。
- **异步动作带树版本**：用数字索引捕获 Tabs 选择/关闭/添加动作时，同时捕获构建 generation；重建后旧动作失效。弱 owner 只避免生命周期保活，不能证明索引仍指向原项。用真实 Down → Post(重建) → Up 验证，不假设 Up 返回后动作仍在队列里。
- **首次测量前建立内容**：ToolStrip 创建按钮时就用 `Button(caption)` 和 icon 属性建立真实子节点，不能等 Paint 中 SyncBinding 再补标题。工具栏明确使用 Flex row，共用弹性 spacer 和 order 区分左右项，公开索引排除 spacer；分隔符禁用且不产生点击回调。
- **宿主先接收组件输入结果**：把业务 activeTab 回写 Tabs 之前先消费组件 TakeChanged，避免覆盖上一输入轮次的选择；延迟创建的组件须先确认实例已存在，首次帧没有待消费事件。同时复核原生 IDE 的标题切换、关闭、上下文和添加路径。组件矩形断言、事件数量和实窗截图各有边界，单项通过不代表完整 Gui 验收。
- **长工具项保留相邻操作命中宽度**：仅测短标题会漏掉短按钮被长标题挤到 10px 的问题。按钮以主题 height-large 为最小宽度，图标不收缩，标题收缩；代码与 HTML 都要在窄宽、多个 DPI 下于首次 Paint 前断言矩形，并用真实输入点击每一项。该规则不代表已有通用溢出菜单。
- **菜单状态落到实际子节点**：保留树 Menu 的 .menu-item、标题、组和 separator 需要真实 CSS，旧 menu::item 不能替代。禁用组按钮也要 Disabled；非空 selectedKey 优先于旧数字索引。重建用 RemoveAll 退休捕获/父链，弱 owner 加 generation 退休旧动作；验证多层组和重建期间的 Down/Up。
- **组件尺寸进入测量契约**：Avatar 等公开 Size/Diameter 的组件须在 OnMeasure 给出对应两轴默认尺寸，不能让字形宽度代替直径；显式单轴尺寸仍由布局引擎保留。回归同时测短/长标签、公开尺寸与实际 Flex 行排列，flex-shrink:0 只能禁止收缩，不能补出缺失的首选尺寸。Flex 居中须同时声明 align-items/justify-content；长标签另设 min-width:0、最大宽度与收缩边界，断言实际内容中心及包含关系，不能只检查外框为圆。
- **菜单 Escape 用按键事件并消费一次**：legacy ContextMenu.Render 与 retained 弹层都应按 App.KeyPressed(27) 的窗口/消费约束处理退出，返回已有 dismiss 结果并 ClaimKey；不能在各业务窗口重复补 guard。UiDriver 的 char 27 是字符事件，应发送 keydown 27 0 与 keyup 27 0，回归同时覆盖字符、释放、其他键、消费后不重复。
- **绘制与输入挂载分别验证**：RenderInside/RenderAt/RenderTree 负责测量、排列、绘制，命中转储有区域仍可能脱离当前输入树。混合宿主复用 ControlInputHost 借用控件树、UiViewScope 在输入前布局；重建先退休旧 scope/host，Bind 保留其他 adapter，禁止每帧 SetRootControl 覆盖整个输入链。实窗确认标题切换、非当前关闭、最后标签关闭后的有效页面；UiDriver click/ev 使用物理客户区坐标，不二次乘除 DPI。
- **退出清理覆盖仍打开的子窗口**：向导 renderCb 可能捕获自身，closeCb 也可能通过业务 owner/dlg 成环；主窗口退出先用局部强引用 Close 当前子窗口，走既有回调与 Teardown 清链。分别验证正常关闭子窗和主窗带子窗退出；非视觉 HWND 取消只证明生命周期，不证明子窗排版。单 App UiDriver 的主 canvas 截图不能冒充独立 HWND 截图。
- **实窗验收读取捕获的帧异常**：stdout/stderr 为空且进程正常退出仍可能每帧 catch 异常。运行前后记录可执行文件旁 zan_ui_errors.log 的新增部分；存在新错误就失败，并结合实际编辑区、探针和截图复核，不能只看脚本走到末尾。
- **生成操作节点与作者内容分开克隆**：Tag 关闭按钮在构造时创建，属性 setter 在首次测量前同步可见性；CloneContentTo 只跳过该实例，保留作者按钮、图标及专属文字的样式/顺序，并恢复目标的文字节点身份。SetText 不按首节点类型认领用户 Label。重加已有节点时，先完成 Adopt 再定位生成按钮，避免移除旧位置或取消回调使索引失效。所有可克隆的子节点 Kind 都须有公共工厂注册，不能让 null 克隆悄悄丢内容。
- **排队关闭核对条目身份**：DynamicTags 关闭动作弱引用创建它的 Tag，执行时同时核对当前下标和子节点身份；列表重建后旧动作失效，重复文字不能用文本匹配代替身份。保留 UiEvent 已排队回调的通用语义，不为单组件取消请求改全局队列。回归须覆盖排队后删除前项、SetItems 替换及重复标签。
- **失焦通知逐段确认宿主所有权**：焦点切换先提交当前身份，再调用旧端点清理与 Blur；每个回调前分别确认旧端点仍归原 App，回调后按 revision 检查是否重入。语义失焦回调本身也可能转移控件，首次检查不能覆盖后续业务通知。真实 Down 建立捕获，在 PointerCancel 中绑定替代宿主并重新取得焦点，断言旧 Hide 不清掉新状态，再用 Up 验证正常释放；仅检查最终 root 无法发现新捕获被旧清理误伤。

### DynamicTags / Tag 与 HTML 标识边界（2026-11-14 冻结正式三项）

- **重建 proof 进入退休过程**：DynamicTags 每次 RebuildChildren 先递增 generation，以保存该代次的 action.Current 作为 `RemoveChildren(children, proof.Current)` 的继续条件；取消回调可嵌套 SetItems，返回后及逐项 Add 后都核对 proof，旧调用不得继续摘除或追加新一代子树。只在 RemoveChildren 返回后检查不足以保护退休过程。
- **旧构造器也保存身份**：保留 `DynamicTagsAction(tags, index)` 两参入口，创建时快照当前 generation 与该索引的 Tag；Close 同时核对代次、有效索引及当前子节点身份。当前动作正常关闭，重建后的旧动作失效，不能因缺少 source 而让旧 API 永久无效。
- **编辑由真实 Input 完成**：StartEdit 在挂载后 Focus 实际 editor，挂载前开始的编辑在 OnMeasure 补焦点；Enter 接 Input.Submit，Escape 接 routed OnKeyDown 并 Handle。重建移除旧 Submit 订阅后接新代次动作，不在 Paint 轮询输入。回归在零 Paint 下验证焦点、输入、提交/取消，再重复绘制确认不重放；不据此新增失焦提交语义。
- **树拥有文字，缓存只借用**：Tag 的 owned Label 缓存用 weak；新 Label 先由局部强引用持有，再 Add 交给子树。移出后即使 Tag 仍存活、未再 SetText，Label 也应释放；单测同时保活 owner、只弱持移出的 Label，避免测试自身延长寿命。
- **HTML id 早晚都直写元数据**：Html.Build 初次分配 id 与属性回放后恢复 id 都直接写 `Control.name`，不能走 `SetProp("name", id)`。IconView.Name 是字形，id 不得覆盖默认空字形或注册工厂的默认字形；验证 id-only、克隆与工厂默认字形，不能仅靠末尾恢复 id 掩盖首次 setter 的副作用。

证据：`build/test-out/gui-followup-cases-20261114T170531.457974Z/report.json` 的
`tag_close`、`control_factory_equivalence`、`html_props_equivalence` 三条 exact conformance 均 Passed，
pass=true，compiler 前后均 `d08d28837b05b41d85279faff258646972d7f11308108c879fbd042d54d9d5ef`，
changedInputs/changedRegistration=[]；结论限于这三条，不外推完整 GUI 或原生窗口验收。
