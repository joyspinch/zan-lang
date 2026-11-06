# Zan.Gui 架构：现状、目标与迁移路线

> 2026-11-06 立。台账：B-ID171～B-ID179（`TASKS.md`）。本文是 Zan.Gui 分层重构的
> 唯一设计依据——阶段内容、验证门与完成标志以此为准；执行中结论变化时改正文，
> 并在文末「决策记录」留一行。

## 1. 设计意图

Zan.Gui 只做两件事：

1. **用 HTML + CSS 描述并绘制界面**：标签长成保留式控件树，外观由 CSS 级联决定，
   布局只有 CSS 一个引擎。
2. **Zan 数据与界面双向绑定**：HTML 窗口用 `data-bind`（ModelBinder），代码侧用
   `Binding<T> data` 字段；动态宿主（设计器、PropertyGrid）走 `GetProp/SetProp`
   属性协议。

凡不服务于这两件事的机制都是历史包袱，按本文路线退役。

## 2. 现状（当前实现：2026-11-06 源码审计核实，证据路径见表）

主线是健康的：五个装载入口都汇聚到 `Html.Parse`，绘制管线只有一条路径，
SelectBox/Input/Button 的外观全部走 CSS（零硬编码颜色）。臃肿集中在下面几处
（路径相对 `packages/Zan.Gui/src/Gui/`）：

| 问题 | 证据 |
|---|---|
| 分层没有边界 | Core/Layout/Styling/Rendering/Text/Media 共 165 个文件同属扁平 `namespace Gui`。编译器的 `internal` 按命名空间生效、没有包级可见性，77 个 zan.pkg 都不声明依赖，任何层都能调用任何层。Core 的 27 个 `using Gui.Widget;` 有 23 个是死引用，噪声掩盖了真实依赖 |
| 包级环 | `Core/ControlBootstrap.zan:3` 写着 `using Gui.Hmi;`，并直接 new 出 Zan.Industrial 的 9 个 HMI 控件；同样是扩展控件，Chart/DataGrid 走的却是 `HeavyControls.Register` 注册表 |
| App 上帝对象 | `Core/App.zan` 4,470 行、200 多个方法，揉进约 12 种职责：窗口、帧循环、故障护栏、事件分发、即时模式输入仲裁、脏区、壁纸与毛玻璃绘制、颜色计算、作用域 ID 栈、浮层列表、HTML 加载与链接导航、IME。partial 拆分只拆了文件，没拆职责 |
| 输入在绘制时轮询 | 路由事件（隧道/冒泡，`Core/Control.HitTest.zan:53-63`）已经实现，且 `App.ProcessEvent` 会先走它（`Core/App.zan:3113`），但生产代码里没有一个控件订阅。控件在 OnPaint 里调用 ClaimClick/BlockHitsRect 抢输入，命中目标依赖上一帧的绘制期登记 |
| 双范式 | 23 个控件既是保留式 Control，又提供每帧调用的 `Render(App, x, y, …)` 入口；绘制只有一份（OnPaint 委托给 Render），但外部有 514 处即时模式调用（Gallery 156、设计器、SceneDesigner、IDE 面板、测试） |
| 4 套布局 | CSS flow/flex/grid；WinForms 式 Dock（`data-dock` + `data-fw/fh`，约 300 处，是 IDE 30 多个 html 的主力写法）；设计器绝对坐标（`data-fx/fy/fw/fh`）；24 栏 `data-span`。模板里同一个元素上三种方言叠着写 |
| 绑定 7 条通道 | data-bind/ModelBinder、`Binding<T>`、控件事件、GetProp/SetProp、Signal*（IDE 约 30 个文件仍拿它当应用状态）、FormField/fname、z-mount。控件没有"可绑定值"契约，`Core/ModelBinder.zan:257-261/700-716/723-725` 只能硬编码特判 10 个控件类型；POCO/JsonValue/JsonDoc/字典各写一遍取值，约 700 行 |
| 弹层重复 | `App.AddOverlay(OverlayPopup)`（框架接口写死了一个具体控件）、`Control.OnPaintOverlay`、Layer 自带的队列、Modal 四条路径；SelectBox/DatePicker/ColorPicker 各自手搓弹层合计约 1,340 行，而 Dropdown 用共享的 OverlayPopup 只要 25 行；模态有两套 |
| 文档漂移 | `zan.pkg` 声称包含 DataTable/Chart/CodeEditor（实际在兄弟包）；README 列出本包不存在的 `Gui.Reactive`/`Gui.Designer`；`docs/agent-kb/gui-development.md` 仍推荐已移除的 Signal 绑定 |

成因：四代写法叠在一起，哪一代都没删——WinForms 式保留模式（Dock/Place/
Serialize）→ HTML 时代（标记 + CSS + 绑定）→ 设计器时代（DesignerHtml 方言、
绝对坐标）→ 工具应用时代（Gallery/设计器养大的即时模式）。

## 3. 目标架构（目标设计：依赖 B-ID171～B-ID179，对应阶段落地前本节不为真）

三条规则：**依赖只能向下；Core 不认识任何具体控件；扩展包只经控件注册表挂入，
各用自己的命名空间。**

| 层 | 命名空间（=目录） | 内容 |
|---|---|---|
| L0 | `Gui` | 基元：Point/Rect/Size/Insets/Color（含颜色计算）/IdHash |
| L1 | `Gui.Backend` | 平台窗口、事件、剪贴板、IME |
| L2 | `Gui.Rendering`·`Gui.Text`·`Gui.Media` | Canvas、背景与毛玻璃绘制、模糊缓存；排版；图片 |
| L3 | `Gui.Styling`·`Gui.Layout` | CSS 解析、级联、Theme、Skin；flex/grid/行盒算法 |
| L4 | `Gui.Core` | Control 树、路由事件、焦点、命中、OverlayHost、FrameLoop、DamageTracker、WindowHost、控件注册表；App 只做薄组合根 |
| L5 | `Gui.Binding`·`Gui.Animation` | ModelBinder（只依赖属性契约）、过渡 |
| L6 | `Gui.Widget` | 基础控件（只做保留式）+ 标准控件注册 |
| L7 | `Gui.Component` | 复合件 |
| L8 | `Gui.Markup` | HTML→控件树、DesignerHtml、链接导航 |
| L9 | `Gui.Forms` | Form/ChildWindow/Nav/DocWire/ModelHosts/UserComponentRegistry：应用装配，负责调用各层的 Install |
| 旁路 | `Gui.Testing`·`Gui.Diagnostics` | UiDriver/UiProbe；UiWatchdog |

- 扩展包（Charts、DataTable、CodeEditor、Browser、Designer、Industrial）各用
  自己的命名空间，只经注册表挂入。
- `Gui.Widget`、`Gui.Component`、`Gui.Backend` 不改名：它们本来就是独立命名空间，
  改名只多出约 1,500 处机械改动，不增加任何边界。

## 4. 迁移路线（目标设计）

### 执行总则

- **一步一提交**：每步验证通过即做路径限定提交（`gui: <一句话结果>`）；每个阶段
  对应一条台账，开工前 `scripts/sched.ps1 register`。
- **先兼容后删除**：迁移期用转发方法或解析期脱糖兜底，调用方不必同步修改；机检
  棘轮保证违规只减不增；兜底代码最后删。
- **验证基线**：每步 = `_scratch/<题>/` 探针 + `tools/agent/verify.ps1 -Paths <改动>`
  （锚定单跑，≤80 条）+ 阶段点名的用例；跨边界的步骤加 `templates_build` 与
  `ide_compile_modes`。
- **并发**：领账前 `git status` + 文件 mtime 查在飞；对方未提交的文件不碰，
  相关步骤顺延。

### P0 安全网与护栏，行为不变（B-ID171）

1. 计划落盘：本文 + 台账。
2. 补安全网：补注册尚未登记到 ctest 的 GUI 用例（如 `tests/gui/gui_declarative_*`、
   `datepicker_smoke_test`，以 CMakeLists 实查为准）；新增布局树金标用例——离屏装载
   每个含 `data-dock/data-fx/data-span` 的 html（`Core/Form.Show.zan:31`
   `Form.ShowOffscreen` + UiDriver 树导出）生成金标，覆盖 templates/gui、
   examples/html_gallery、tests，IDE 侧放 tests/ide。它是 P5 的等价门。
3. 层级机检（棘轮）：`scripts/check_code_standards.py` 加规则（pre-commit 经
   `scripts/hooks/gate_common.sh` 已接入）。按目录判定文件所在层，从声明生成
   "类型→层"表，剥掉注释与字符串后检查向上的 `using` 与类型引用；现存违规记入
   基线，只拒新增。附带棘轮：Widget/Component 不得新增 `Render(App` 入口；不得新增
   App 仲裁 API 调用；设计器文档以外不得新增 `data-dock/data-fx/data-span`；
   Zan.Gui 不得引用兄弟包命名空间；Zan.Gui 以外不得新增 `new Signal*`。
4. 清噪声、修文档：删死 using（Core 23、Widget 7）；修 `zan.pkg` 描述、README、
   `docs/agent-kb/gui-development.md` 的 Signal 推荐、gui-design skill 里已不存在的
   QualifyKind 描述（skills 三副本同步）。

### P1 断开包环，按目标层摆目录（B-ID172）

1. HMI 注册外移：新建 `packages/Zan.Industrial/src/Gui/Hmi/HmiBootstrap.zan`
   （照 `DataTableBootstrap.zan` 写法）；`ControlBootstrap.zan` 删 `using Gui.Hmi`
   与 9 个 HMI 分支；用到 HMI 标签的调用方补 `HmiBootstrap.Install()`。
2. 按目标层落目录，命名空间暂不变（调用方零改动）：新建 `Forms/`、`Binding/`、
   `Testing/`，基元移到 `src/Gui/` 根；同步按物理路径枚举源码的构建脚本
   （`scripts/build_gallery*.ps1`、`build_charts.ps1`、`build_ide.ps1`）。
3. ControlBootstrap 移入 `Widget/`（`namespace Gui.Widget`，约 30 个调用点同提交）；
   `Control.Layout.zan` 的 4 处字符串 kind 特判改为控件自报。

验证：`templates_build`（含 gui-hmi）、`ide_compile_modes`、`policy_control_factory`、
`conformance_gui_html_runtime`、`conformance_gui_form_control`；机检基线中
Core→Widget、Gui→Hmi 两类边清零。

### P2 绑定收敛：一个契约，两条正道（B-ID173）

1. 可绑定值契约：在 PropSpec（`Core/PropSpec.zan`）上标记控件的默认绑定属性与类型
   （文本/布尔/数值/索引），不另开机制；ModelBinder 改读契约，删 10 处 `is X` 特判。
2. 模型访问统一：POCO/JsonValue/JsonDoc/JsonDocCursor/字典的取值收成一个模型游标
   接口 + 适配器；`DocWire.CheckBinds` 共用它，修掉 JsonValue 模型无法校验绑定路径
   的问题。
3. 绑定泵统一：ChildWindow 自泵改走 ModelHosts，与 Form 一致。
4. Signal 退出外部 API：IDE 里当应用状态用的 SignalInt 改普通字段 +
   `Binding<T>`/data-bind。

验证：`conformance_gui_form_control`、`conformance_gui_form_dynamic`、
`conformance_gui_props_contract`、`diag_cw_bad_bindprop`、`conformance_gui_cw_docscan`、
`case_binding_sugar`、`case_binding_field_sugar`、`case_binding_live_read`、
`case_gui_model_poco_jsondoc`、`case_gui_model_host`、`case_gui_html_dynamic`、
`templates_build`（gui-wechat 是 data-bind 重度用户）；第 4 步加 `ide_compile_modes`。

### P3 拆 App，只当组合根（B-ID174）

逐项抽出，每项一提交。App 留单行转发、调用方不动；会形成向上依赖的转发不留。

1. 颜色计算（Lerp/LerpColor/Darken/Lighten/ScaleAlpha、ISqrt）→ 基元层。
2. BackdropPainter（Rendering）：壁纸、渐变、毛玻璃、FillEllipse、PaintShape*、Fx*，
   以及 App.Skinning 的绘制部分。
3. DamageTracker（Core）：脏区记录、玻璃区扩张、RectHitsDamage、MarkPresentDirty、
   模糊缓存槽。
4. FrameLoop（Core）：Run/RunLoop/Pump*/SafeFrame/TakeDueFrame/PollOneEvent/
   Wake*/Post/DrainPosts，帧错误计数。
5. WindowHost（Core）：窗口位置尺寸、最小化/最大化、标题栏按钮与提示、安全区、
   IME 光标。
6. HtmlLoader → Markup：LoadHtml 系列、OpenLink、OpenInSystemBrowser、链接导航器；
   调用 LoadHtml 的 9 个文件同提交修改。
7. InputArbiter 隔离：Claim*/Block*/Reserve*/Press*/Click*/Swipe*/LongPress*/
   PushId/GetId 收进一个类，P6 整体删除。
8. OverlayHost 骨架：迁出 AddOverlay/RunOverlays/HasOverlays，P4 充实。

目标：App.zan 少于 1,000 行。验证：`conformance_gui_frame_cadence`、
`conformance_gui_effects_coalesce`、`conformance_guihost_loop`、
`conformance_gui_ui_watchdog`、`perf_frame_budget`（不得退化）、UiDriver 五例
（`conformance_gui_uidriver_label_click`、`conformance_gui_maxbtn_dblclick`、
`conformance_gui_wheel_route`、`conformance_gui_scroll_drag_reanchor`、
`conformance_gui_listview_scrollbar_drag`）；第 2 步加 `conformance_gui_css`、
`policy_theme_color_budget`；第 6 步加 `conformance_gui_html_runtime`、
`conformance_gui_html_link`。

### P4 统一浮层（B-ID175）

1. OverlayHost 分层：从下到上为下拉/菜单、气泡/提示、模态、消息通知；每个条目自带
   绘制、命中拦截与关闭策略；命中拦截与原生层遮挡（NativeLayer）在此一处结算；
   `Control.OnPaintOverlay` 改为向它登记。
2. OverlayPopup（1,858 行，本质是下拉/菜单列表控件）下沉到 Widget 层，基于
   OverlayHost 实现。
3. 手搓弹层改用共享件：SelectBox（级联/树/筛选面板拆成可复用部件，目标 < 1,200 行）、
   DatePicker、ColorPicker、Popover、ContextMenu/Menu。
4. 模态合一：Layer 的 alert/confirm/prompt 改用 Modal 实现，Layer 保留服务式 API
   作门面。

验证：`conformance_gui_selectbox_cascade`、`conformance_gui_selectbox_popup_hit`、
`conformance_gui_datepicker`、`conformance_gui_colorpicker`、`conformance_gui_layer`、
`conformance_gui_overlay_penetrate`、`conformance_gui_overlay_penetration_core`、
UiDriver 五例。

### P5 布局方言退役，运行时只剩 CSS（B-ID176）

1. 删死码：`Core/Serialize.zan`（唯一调用方 `examples/gui_gallery/Gallery.zan`）；
   Alert→AlertBox 别名（先确认无文档在用）。
2. CSS 能力探针：确认 grid 显式行列定位（px+fr 轨道、grid-row/column）与
   `position:absolute` 完备，缺的按 CSS 标准补齐。
3. 解析期脱糖：`data-dock/data-fw/data-fh/data-gap/data-pad` 换算成父容器的 CSS grid
   定位（不插包装元素，id 与子序不变）；`data-fx/fy/fw/fh` 换算成 absolute 定位；
   代码里的 `.dock = N` 逐处改 CSS。随后删 Dock 引擎（`Core/Dock.zan`、StyleBox 的
   display:0 旧模式、`Control.Layout.zan` 的 dock 分支、仅代码可达的 StackLayout）。
4. 设计器文档以外不得使用旧方言：现存 42 个带 `data-dock/fx/span` 的 html 全部是带
   `data-zan-design` 的设计器文档（含 IDE 30 多个面板、templates/gui、examples/html_gallery、
   测试夹具），由第 3 步的脱糖兜底，文件改写并入 P8；本步只靠 P0 的机检拦住手写 html
   新增旧方言。

验证：P0 布局树金标逐个比对（±1px）；`conformance_gui_flex`、
`conformance_gui_flex_shrink_floor`、`conformance_gui_panel_flex`、
`conformance_gui_layout`、`conformance_gui_form_grid`、`conformance_gui_html_*`；
`templates_build`、`ide_compile_modes`。

### P6 即时模式下线（B-ID177，分批交付）

1. 路由事件转正：OS 事件走已有的隧道/冒泡与指针捕获，目标沿用绘制期登记的命中矩形；
   Control 增加类型化事件虚方法（按下/抬起/移动/点击/滚轮/按键/字符/焦点），默认实现
   触发现有 UiEvent，`@click` 等声明不受影响；UiDriver 用同一套目标解析点击。
2. 共用件：滚动条与滚动容器、菜单。
3. 叶子控件：Button/Checkbox/Radio/Switch/Tag/FloatButton/RadioButton/Rate/Steps/
   Collapse/Upload 等从 FireCommon/Ui.Activate 轮询改为路由事件。
4. 拖拽类：Slider、SplitPanel、滚动拖拽改 CapturePointer。
5. 文本栈：Input/TextArea/InputNumber/InputOtp 改路由按键/字符事件，焦点按控件
   引用，退役 PushId/GetId 稳定 id 方案。
6. 复合控件与兄弟包：Tabs/TreeView/ListView/VirtualList/Pagination/Wizard/Transfer/
   Ribbon/Layer；FilePicker/SessionList/BandGrid/PropertyGrid/LogView/GraphView/
   ChatView/DockHost；DataTable/Charts/CodeEditor/Industrial 只改输入，Canvas 自绘保留。
7. 调用方迁移（514 处）：设计器预览改离屏渲染（`Form.CaptureSnapshot`、
   `App.Offscreen`）；Designer.Inspector/Designer.zan 改保留式属性行（复用
   PropertyGrid）；SceneDesigner 改保留式属性列表；IDE 面板逐个改保留式子树；
   `examples/gui_gallery` 并入 `examples/html_gallery` 后退役
   （`policy_gallery_coverage` 改查后者）；改写 16 个即时模式测试。
8. 删除：控件 `Render(App, …)` 入口、InputArbiter、Ui.Activate*、WidgetEvents 轮询；
   机检禁止回流。

每批验证：本批控件的 `conformance_gui_<控件>`、UiDriver 五例、路由事件测试
（gui_visual_tree_routing、datatable_nested_visual_tree）、`perf_frame_budget`、
`ide_compile_modes`、`templates_build`、`policy_gallery_coverage`；IDE 交互用 UiDriver
脚本实机走查。

### P7 按层拆命名空间（B-ID178，独占窗口，原子提交）

1. 目录即命名空间：基元留在 `Gui`，其余按第 3 节表格划分。
2. codemod 脚本入库 `scripts/`（外部用户迁移可复用）：改写 `namespace` 行；按
   "类型→命名空间"表为约 1,285 个调用方文件补最小 using 集合；改写 977 处 `Gui.X`
   限定引用；同步 IDE 代码生成器（`ZanIDE.DesignCodeGen.zan`、`ZanIDE.SceneCodeGen.zan`、
   `ZanIDE.Project.zan` 的 GUI 工程探测）、`tools/mcp_server/McpServer.zan`、文档与 skills。
3. 兄弟包各占己名：`Gui.Component.Chart`→`Gui.Charts`、`…DataTable`→`Gui.DataTable`、
   `…CodeEditor`→`Gui.CodeEditor`、CefBrowser/WebView/FirefoxBrowser/BrowserUrl→
   `Gui.Browser.*`（每包一提交）。
4. 机检切严格模式，基线必须为空。
5. 编译器迁移提示（若缺）：类型找不到、但存在于别的 `Gui.*` 命名空间时提示补 using
   ——按 WORKSPACE_CONVENTIONS §10 修编译器并补 conformance 用例。

约束：命名空间首段必须保持 `Gui`（编译器按 `Gui_` 符号判定 Windows GUI 子系统，
`src/compiler/main.c` 驱动前缀表与子系统判定处）；Zan 没有 using 别名、global using
与类型转发，只能一次原子提交；partial 类各部分须在同一命名空间；新增歧义
（Color/Point/Size 对 System.Drawing、Style 对 System.Cli.Style、Window 对
System.Automation/System.Data）逐个加限定；同一目录不得混放不同的声明命名空间
（包索引的"同质目录"快路径会误归属）。

验证：`templates_build`、`ide_compile_modes`、全部包编译、examples；改动超过千个
文件，届时由用户点名跑一次 standard 档。

### P8 设计器改为产出 CSS（B-ID179，后续）

Zan.Gui.Designer、`Markup/DesignerHtml.zan`、IDE DesignCodeGen 改为直接读写 CSS；
脚本改写全部 42 个设计器文档（IDE 面板、模板、html_gallery、测试夹具）；删除 P5 第 3 步的
脱糖兜底。P5 完成后单独出计划。

## 5. 顺序、完成标志与风险

顺序：P0 → P1 → P2/P3（可并行）→ P4 → P5 → P6 → P7 → P8。护栏先行；P1 先把目录
摆成目标层（调用方零改动，机检按目录即可生效）；P3 先隔离输入仲裁、P6 再整体删除；
P4 先于 P6（弹层是输入轮询最密的地方）；P7 放最后（没有转发手段，只能在内容稳定后
一次做完）。

完成标志：

- 机检严格模式下基线为空；Zan.Gui 不引用任何兄弟包命名空间。
- App.zan 少于 1,000 行；ModelBinder 没有 `is <控件>` 特判；SelectBox 少于 1,200 行。
- Widget/Component 没有 `Render(App` 入口，App 上没有仲裁 API。
- 运行时只有 CSS 一个布局引擎；设计器文档以外没有 `data-dock/data-fx/data-span`。
- 绑定只剩 `data-bind`（HTML）与 `Binding<T>`（代码）两条正道，外加 GetProp/SetProp
  动态宿主协议。

风险：

- 并发会话：P7 的千文件原子提交必须独占窗口；其余阶段路径限定提交，冲突逐文件
  手工合并。
- 帧耗时：路由事件转正可能拖慢每帧，每批都跑 `perf_frame_budget`。
- UiDriver 点击依赖绘制期命中登记：P6 第 1 步起两条路径并行，删旧路径前确认 E2E
  全部通过。
- 设计器耦合：P5 只做脱糖、不改设计器文件，设计器改造留到 P8。

## 6. 决策记录

- 2026-11-06 立项（用户裁定）：旧写法分阶段退役；即时模式逐步迁移下线；按层拆
  命名空间并放在最后；`Gui.Widget`/`Gui.Component`/`Gui.Backend` 不改名。
- 2026-11-06 更正：旧 kind 别名只有 1 个（`HeavyControls.RegisterAlias("Alert",
  "AlertBox")`），审计初稿误记为 34 个。
- 2026-11-06 更正（P0 机检落地时实测）：带旧布局属性的 42 个 html 全部是设计器文档，
  IDE 面板也在内。P5 只做运行时脱糖与删除 Dock 引擎，文件改写并入 P8。
