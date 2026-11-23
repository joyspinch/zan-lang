# HTML 窗口——声明与数据（日常写窗面）

> 从 html-window.md 拆出：建树通道、声明面原语、动态绑定选路、模板实例化、切页三路。

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
- **组件编译期静态清单**：标准控件与已引入扩展控件统一由 `[GuiKind("...")]`
  标注，编译器 `[GuiKindManifest]` Pass 在编译期合成 `ControlFactory` 静态分派表
  （含 `RadioButton` 等别名，无需运行期 `*Bootstrap.Install()`）；HTML 与代码工厂
  查询同一编译器合成清单。运行期动态扩充仍可调用 `ControlFactory.Register`，但禁止手写第二份静态注册名单。
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

## 模板实例化（gui-* 模板带 `{{NAME}}` 占位符）

- `{{NAME}}` 在**两处**都要替换：`src/App.html`（根元素
  `id="{{NAME}}"` 与 `data-win-title`）和 `src/App.zan`
  （主类 `class {{NAME}}`），且两个入口文件要改名为
  `<NAME>.html`/`<NAME>.zan`（zan.proj `entry = src/{{NAME}}.zan`
  按名对上）。只换一处，装载路径或入口就对不上；模板缺文件
  直接编译/装载失败——整个 src 都要带。
- 参考脚本 `scripts/e2e_pipeline.ps1`：整树拷贝 + 替换 + 用
  zan.proj 编译，别手搓单文件。

