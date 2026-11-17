# HTML 窗口开发定式——逐坑细则

> 主干与硬边界见 SKILL.md「HTML 窗口开发定式」；本文件是窗口入口/文档装载、
> 内容事件与模型、嵌入视图输入与生命周期、布局与样式、浏览器集成的逐坑细则，
> 每条都真实踩过。核心契约另见 [html-window.md](html-window.md)。

## HTML 窗口开发定式(硬规则)

GUI 创作采用两条标准通道：HTML + CSS，或保留模式声明式对象树。
图表、仪表、代码高亮及游戏 HUD/菜单/SceneDesigner 也封装成标准控件；
像素绘制收在控件 `OnPaint` 内，使用处只配置属性、样式与绑定。
Zan.Game 的世界渲染属于游戏引擎，按其世界/视口渲染契约执行。

### 窗口入口与文档装载

- **嵌入宿主与内部控件同名**：DesignedControl 外层宿主 id 可以与内部文档 id 相同；内部访问器应从 children 按文档 id 搜索，排除宿主自身和文本回退，缺失仍显式失败。否则 CtxRing 的宿主 Ring 会先于内部 Progress Ring 命中，触发 InvalidCastException；不得改业务 id 绕过。回归应真实设置 host.name 与内部 id 相同，证明旧查找失败、新查找通过。

- **约定式窗口**：`class MainForm : Form` 配同名 `MainForm.html`，
  调用 `new MainForm().Show()`。`Show` 找文档、解析、读取窗口键、
  初始化 App、挂树与模型泵，再调用 `OnLoad` 并进入循环。
  零参 `Form` 构造不初始化 App；不要直接在新建子类上调用 `Run()`。
- **已有宿主中的页面**：`Gui.Html.Load(app, html, handlers, baseDir)`
  返回控件树，并合入文档作用域样式、接链接导航；`Parse` 内自动
  `Install`，调用者不用手工安装声明层。此入口不负责挂进父控件，
  也不自动为页面注册模型宿主。使用 `host.Add(ui)`，按父布局为根
  声明 `flex-grow: 1`；需要绑定时再挂 ModelBinder。
- **编译只输入 `.zan`**：`.html` 是运行期文档，zanc 拒绝将其当源码。
  发布用 `--embed` 带文档；文档 `<style>` 随装载进入应用样式，
  不再使用生成类、静态 Css/Build 或 JSON 建树入口。
- **窗口键**：同名文档的 `data-win-title/win-w/win-h/win-chrome/
  win-center` 由 `Form.Show` 装配消费。显式 `Html.Load` 只是页面装载，
  不替宿主新建或配置窗口。

### 内容、事件与模型

- **Children 集合必须维护真实父链（2026-11-12 实证）**：自然嵌套 `new Row() { Children = { new Column() { Children = { input } } } }` 通过 getter 返回的集合逐项 `Add`，不会调用属性 setter；返回裸 `List<Control>` 会绕过 adoption，导致 parent/App 链断裂和输入捕获无法退休。`Control.Children` 现为 getter-only、缓存的 `ControlChildren` 门面，提供 Count、只读索引、Add/Remove/Clear 与 foreach；不再使用 `ChildrenCollection` 或向 Children 赋 List，Row/Column 的 `Children(List<Control>)` fluent 方法保留。业务结构修改走门面或 Control 的树方法，不直接改 backing `children`。
- **移除、重挂先退休旧宿主输入**：共享 `Control.Remove` 在仍有旧父链时调用 `ReleaseInputSubtree`，再重查索引和父归属后摘除；Clear 用快照逐项 Remove，跨父/跨 App adoption 同样经过旧父 Remove。取消回调若已把节点挂到第三父或请求目标父，外层 Add/With/InsertAt 尊重新归属，不能重复追加或强抢。门面弱引用 owner，owner 释放后 Count=0、Add/Remove/Clear 无害，枚举快照仅持有子项；不得让门面或 enumerator 保活 owner。回归 `tests/gui/control_children_lifecycle_test.zan` 已以真实 offscreen 输入、嵌套初始化器、焦点/捕获退休、取消重入重挂及 weak 生命周期通过 strict/golden/leak/guard。

- 文档声明结构、样式与事件名，code-behind 管状态和事件响应。
  优先用正典语义标签与 `zan-*` 控件标签；具体映射以
  `Html.Build.zan`、控件注册与属性协议为准，不凭浏览器经验猜。
- 五件声明原语：`data-on-*` 事件、`data-arg` 字面参数、`data-bind`
  模型路径、`data-if` 路径真值、`<template data-for>` 列表。
  `@click` 等语法糖见 `references/html-window.md`。
- `Form.Show` 默认把窗口实例作为模型；已有宿主里的页面需
  `ModelBinder.SetRoot(ui, model)`、`Wire()`、
  `ModelHosts.Register(app, binder)`，撤页时 Unregister。
  同一个事件选 handlers 或 binder 一处接线，避免重复订阅。
- 值、显隐、列表内容优先改模型并 `RequestRedraw()`；结构变化走
  控件树操作或重新 `Html.Load` 后替换旧树。重新装载不会替你替换
  父节点中的旧树，也不会自动注销旧 binder。
- 文档元素携装载期作用域类；状态切换用 `SetClassIn`，不要整串覆盖
  `Class`。代码后挂的控件没有文档作用域类，共享外观放全局皮肤层。

### 嵌入视图的输入、生命周期与模型泵

<!-- experience:stdlib-preparation-2026-11-16 -->
- **同一 List 的树内容变化仍须准备失效**：复用既有 scope/输入宿主，先核对 Prepare 的更新契约；
  当前 `IdeTreePanelHost` 每次 Prepare 无条件筛选、Bind、Layout，Paint 只消费。
  原因：节点 expanded 可原地变化，仅按 List 引用缓存会漏掉收起/展开后的可见 extent 与滚动范围。
  回归先用真实首次 wheel/filter 进入准备，再在同一列表改 expanded；Paint 消费检查须执行真实绘制，
  同时核对筛选调用数、行控件身份、Measure/Arrange 计数及输入 proof，覆盖隐藏/重挂/关闭退休。
  判定器：`conformance_gui_ide_stdlib_preparation`（2026-11-16 PASS 69.26s）；同批4/5，
  `initial_shape_fill` 因 Gui internal 访问限制编译失败。此结果限于该契约，未验收新 profile 性能预算或 native close。
<!-- /experience:stdlib-preparation-2026-11-16 -->

- **借用树须在输入前同时挂载与约束视口**：装配时登记 scope/host，PrepareView 过滤、绑定并 Layout，Paint 只消费；隐藏 Hide、关闭 Retire，重显先准备。Form 换启动根后用真实 App 输入入口推进 BeforeInput，目标快照前可恢复已有 adapters；BeforePaint 改根会拒绝当前 proof，不能靠加重试预算掩盖。自定义设计宿主不能假定 ProjectBody 复制 body 的 class；检查当前投影契约，用 AddClass 保留既有类并兑现标准 Flex/min-height。原因：Explorer/Tools 缺宿主布局类使220高区域撑成986，body视口等于extent，滚轮命中却不消费；局部补类后视口194、extent960，真实 wheel handled=true、内部offset/兼容signal均40。回归须同时断言分配区域、真实wheel/filter、隐藏退休/重挂与Paint proof，不能只看signal。判定器：`conformance_gui_ide_editor_tabs_preparation`（2026-11-16 strict精确通过，保留Tabs/Scene覆盖）。

- **首次登记早于输入准备快照**：scope/输入宿主在装配时创建并 Bind，区域几何
  留给 PrepareView。prepare 保存 participant/generation 快照；在 BeforeInput 内
  首次登记会使旧准备失效，必须放弃旧尾部，不能放宽守卫或等 Paint 才登记。
  SceneDesigner 当前 `InitializeInput(App)` 正为提前登记入口，后续
  `SetBounds(App,x,y,w,h)` 更新区域；源码接入不等于该 caller 已最终验收。
- **Paint 不复活已退休的借用输入域**：装配或输入前准备阶段负责登记、绑定与显隐；
  准备阶段已经隐藏或退休的域，Paint 不得以“确保初始化”为由重新 Bind/Initialize。
  否则绘制会改变输入根代际，使已保存的准备证明失效；重新显示也先走活动视图准备。
- **JsonValue 模型更新使用 Set**：`model.Set("title", JsonValue.NewStr(text))`
  替换已有键，不存在时追加；随后 RequestRedraw/MarkDirty。
  Put 与 PutStr/PutInt/PutDouble/PutBool 等助手只追加，重复键 Get 取首值，
  因而用它们“更新”会继续读旧值。不要虚构 SetStr/SetInt 等入口。

- 独立绘制的现有树借用输入时，持有 `new ControlInputHost(root)`，调用
  `Bind(app)` 接入共享输入适配链；它不收养 root、不选择布局区域、不负责绘制。
  root 与适配器都由页面持有，离页或隐藏调用 `Hide()`，退休本子树输入并摘链。
  普通子树仍直接 Add/Mount，不能用借用宿主代替真实 parent 链。
- 页面持有 `new UiViewScope(app, owner, root)`，owner 实现 `UiViewOwner`。
  `Mount(parent)` 可挂到普通父树；独立区域在 `PrepareView(UiViewScope view, App app)`
  中调用 `view.Layout(x, y, width, height)`，使输入命中前与绘制前使用同一
  Measure/Arrange 几何。只在 Paint 中 Arrange 无法保证连续输入命中新位置。
  scope 不自动安装输入根；其 owner/root/App 为借用，调用方须保持存活。
- `view.Bind(model)` 创建、注册并 Wire ModelBinder，不自动执行首同步或循环。
  宿主泵必须在布局前推进 `SyncModelHosts()`，在呈现前推进
  `WriteBackModelHosts()`；这些阶段由标准共享准备推进，不在每个页面重复泵。
  自定义宿主检查 `PrepareStandardHostFrame()` 的返回值，并用
  `ContinuePreparedFrame()` 检查回调后的登记/revision proof，失效即中止旧帧。
  无 AppHostLayout 时，App 共享准备在区域布局前同步模型，并在 overlay 尾部
  回写；原生呈现与 offscreen 共用此边界。该模型阶段已通过严格 probe，
  不代表 HUD、Designer、DataTable 消费方已验收。自定义手动帧须在 BeginFrame
  和业务更新后、绘制前显式 prepare，并在回调后及呈现前检查 Continue；
  null-host 分支也核对 hostLayoutRevision，页面不重复执行模型阶段。
- 事件可经 `view.Action(name, sender)` 的 Run/Send/Pointer 与
  `view.Handle(name)` 转交 owner；修改模型用 `view.MarkDirty()`。
  离页调用 `view.Retire()` 使旧动作失效、Detach 自有 binder 并移除 Mount 的树，
  同时对独立 `ControlInputHost` 调用 Hide；Retire 不代替借用输入宿主的摘链。

### 布局与样式

- 根与中间内容区用 Flex/Grid/Flow；顶栏、内容、底栏按文档序排列，
  固定栏 `flex-shrink: 0`，内容 `flex-grow: 1; min-height: 0`。
  百分比高度须有确定包含块；不能用历史 dock 数字解决高度塌陷。
- `Control.dock` 仅为兼容属性：setter 只在值为 5 时调用 `Grow()`，
  其余值无操作；新代码不使用 DockTop/Bottom/Left/Right/Manual。
  需要独立浮层时使用 CSS absolute 或 `Float(x, y)`，它经 `Place`
  写 absolute 样式；普通表单排布继续留在自动流中。
- 样式使用语义类与 token；HTML 不执行脚本。`line-height: 72` 表示
  字号倍数，72 逻辑像素应写 `72px`。不要把 CSS 注释结束符写进注释文案。
- `base.css` 由编译器内嵌兜底；项目只发布自家皮肤，不用旧磁盘基线
  盖住新内嵌基线。文档样式自动作用域化，全局皮肤通过 `UseAppCss`
  或皮肤接口配置。

### 控件与浏览器集成经验

- **验收结论绑定具体版本与消费方**：用编译器、源码及成品身份关联构建、业务断言与截图，
  分别记录发布、交互、视觉及性能结果；不把不同冻结批次合称完整产品验收。
  共享机制或旧组件通过不替代当前调用方与实窗证据。项目当前状态统一查
  `TASKS.md` 的 B-ID171～179（GUI 分层重构台账），技能不复制运行状态。
- **FilePicker 先准备后绘制**：内联显式用
  `BeginInline(app, mode, initial, extension, defaultName, title)`，在输入前装配；
  跨 App 用 BindInline，隐藏 HideInline，结束 Close。持久宿主在共享 prepare 中建树、
  挂载与布局，RenderOverlay 只绘制；dirty 重建及 Cancel 重入保护新会话。
- **retained 绘制只读输入状态**：Input.OnPaint 不登记焦点/命中、不派发输入或通知；
  旧 Render 的兼容登记只属于该入口。DataTableControl 的可见单元控件/明细是真实
  children，`ChildControlsRenderedByOwner()` 避免通用树再次绘制；paint clip 与 hit clip
  同源，Bind/Cancel/嵌套绘制以 generation 废弃旧尾部。标准 comp/detail/provider 的
  可见子树由 Arrange 经 UiViewScope/HUD prepare 建立，Paint 复用；custom
  `PaintCell`/`Paint` 的 bool 完全接管语义保留，默认 fallback 仅在真实 Paint
  确认 false 后才采用或退休；header/filter/footer 的即时输入尚未全部迁移，
  purity 也不等于零分配。
- **ScrollColumn/Cards 的 viewport cull 不改作者可见性**：新旧 ScrollColumn 与 Cards
  收据验证 viewport cull 不覆写作者设置的 `visible`；绘制、命中与焦点统一使用
  `ChildInViewport` eligibility，且该 eligibility 不参与 measure，避免上一轮视口结果改变内容
  extent。保留 snapshot 与 generation/reentry 守卫，断言按当前真实 clip 更新，不用应用侧扩大
  viewport 绕过生产裁剪或测量缺陷。
- **浏览器按逻辑 root turn 推进**：Work 已由共享循环驱动，各 App 共用 root 所有的
  CefWorkLoop；非零 turn 去重，turn 0 的关闭工作不擦掉旧认领。离线 WorkBackend
  覆盖调度与 weak root，不证明真实 CEF/WebView/Gecko pump、键鼠或 IME。
- **子语义度量不得覆盖本体样式缓存（TreeView RowHeight）**：先查清 `ResolveStyleAs`
  会 Full/Clone 并记入本体 computedStyle；它不是只读子样式查询。RowHeight 仅当现有
  `tree/normal` 通过 `ComputedStyleCurrentAs` 的完整校验时复用，否则临时 Full+Inline，
  不写回本体，不用缺失 theme/DPI/metrics/Class/id/context 的行样式缓存取代它。
  当前默认 TreeView.StyleType 是 tree，仍须覆盖重复 Measure、hover 与派生 selector；
  live-only 用例先 Measure 建立缓存，再改变 sibling，验证不陈旧且本体缓存未被覆盖。
  回归 `conformance_gui_treeview_rowheight_style` 已正式 PASS；离屏像素保持与该用例通过
  不外推正式 IDE 帧预算，单项 PASS 与整批结果分别记录（具体证据见提交说明与该用例的 conformance 记录）。
- **样式缓存按依赖判断**：无节点依赖不构造祖先路径，祖先路径依赖纳入 id/状态/位置；
  属性/兄弟/结构/后代等 live-only 依赖保守绕过 Control/App 缓存，media 环境进入键。
  offscreen before `gui-style-benchmark-20261114T104202Z` 与 after `gui-style-benchmark-20261114T105803Z`
  均为 201 nodes × 120 frames、两模式 style_resolve=0。总分配均 57758400→53899200 bytes（−6.68%）；
  measure 27916800→26947200、arrange 4819200→1929600（−60%）、render 25022400 不变。
  普通/关系 pipeline 270588/272022→256466/253595 μs，单轮观察下降约5.2%/6.8%，
  不代表普遍加速、零分配或实窗达标，也不冒充历史旧版全面基线。
  style 功能和 flow regression 分别通过；core 三文件输入冻结，但 broad manifest 记录
  Designer 并发变化、功能收据 frozenPass false，不能合称整批冻结通过；该轮性能证据不代表消费方当前验收状态，后续结果统一查上述架构文档入口。
  分配按 measure/arrange/render 归因，核心冻结与广域无漂移分别核对，不能互相替代。

- **WebView2 完成回调的参数归引擎所有,不得释放**: 手写 COM 桥里完成
  handler 收到的结果串(`ExecuteScript` 的 JSON、`AddScript...` 的
  脚本 id)和 `GetCookies` 的列表指针,Invoke 返回后引擎自己回收——
  按"方法出参用 CoTaskMemFree"的惯例去释放,轻则堆损坏(0xC0000374,
  Eval 加一次 Com.Free 即崩),重则过释放 CookieList 后后续 COM 调用
  在 STA 里永久挂死。只有同步方法调用拿到的出参(get_Source 等)才走
  `Com.GetString/Com.Free`;completion 参数一律只读。

- **WebView2 是"加载器+系统运行时"两层,缺运行时可自动装**: 随应用只发
  166KB WebView2Loader.dll(组件 drivers/ 目录),引擎本体 Win10/11 自带;
  没装的机器调 `WebViewBootstrap.EnsureRuntime` 下载官方 bootstrapper
  (~1.8MB)静默安装(弹一次 UAC),装后要新建 WebView 实例才重试创建
  (created 标志一次性);Win7/8.1 的 bootstrapper 已不派发兼容版(109 为
  最后),会改为打开官方下载页——要 Win7 免安装就发布带 CEF 的
  zan_cef109 驱动。探测用 `WebView2.RuntimeVersion`;用户装没装运行时
  与代码无关,程序只认探测结果,别写死"一定有"。

- **滚轮认领以"真溢出"为前提,不溢出不拦截**(滚动条乱跳的框架级
  根因 修):指针在内就 `CaptureWheel` 但内容放得下的
  容器,会把 offset 钳回 0 空转、白白吃掉本属于外层的一格滚轮;
  指针扫过这类区域时消费权内外横跳,外层滚动条走走停停、来回蹦,
  用户感知为"滚轮一滚滚动条乱跳"。框架引擎已内置该门
  (`Scroll.WheelXY` 双轴、`ScrollView.Begin` 的 `mo0 > 0`),自写
  `CaptureWheel` 的内容滚动组件(日志/聊天/代码块/文本域/编辑器)
  必须自带同款溢出门(`maxScroll > 0` 才认领);弹层、图表缩放这类
  **有意拦截**滚轮的认领不受此限。回归锁定:
  `conformance_gui_wheel_route` 场景 4(放得下的 flat 容器穿透,
  外层逐格线性)。

- **组件画廊/演示墙用瀑布流,大组件跨两列**(组件全量画廊定式):固定
  N 列行打包的行高=行内最高卡,DataGrid/Transfer 这类宽组件挤在单列
  370 逻辑宽里施展不开;改每列独立记账、卡片落"最矮位置"(并列取最
  左),大卡 span=2 横跨两卡槽(双倍宽),生成器断言
  max(列高)-GAP ≤ 内容区高。落位顺序用**高卡降序稳定排序**
  (key=(-卡高,-span),同高跨列卡先落)收平页底——手工把高单卡排前面
  只是打补丁,同高时单卡抢先落位照样把跨列卡的窗口顶高
  (导航页 892>854 翻车后改排序键才收平)。

- **HTML 里"当前选中项"高亮必须 class 驱动,行内 style 只留几何**:
  HTML 的 `style="..."` 不是浏览器那套"内联样式优先"——构建器会把它合成
  一条专属类规则 `.zgen-N { ... }` 并给控件挂上 `zgen-N` 这个类,所以它和
  作者写的 class 规则**同源**,最终由 CSS 特异性决胜:`.opt.on`(0,2,0) >
  `.opt`(0,1,0) = `.zgen-N`(0,1,0)。踩过的坑:把背景/边框/颜色写在行内,
  `.opt.on` 高亮怎么加都不生效——不是优先级被压,是那条高亮规则压根没写。
  规范做法:
  1. 视觉属性(background/color/border/border-radius)全部搬进 `<style>` 里的
     `.opt` / `.opt.on` 规则;行内只留 `font-size`/`padding` 这类几何。
  2. 状态类用 `SetClassIn("optstate on", on ? "on" : "")`:`SetClassIn` 只清
     **group 里列出**的类,group 必须把要清除的状态类也写进去。写成
     `SetClassIn("opt", "opt on")` 会把 "on" 留下(取消高亮时清不掉)。
  3. 改 `Class` 不必手动 invalidate——样式缓存键含 Class 串
    (`Control.ResolveStyleCached`),Class 一变缓存自动失效重算。

完整声明面、页面绑定、设计器与验证方法见
`references/html-window.md`。本规则描述开发约定；具体实现状态及迁移缺口
以源码声明与 `TASKS.md` 的迁移台账为准。
