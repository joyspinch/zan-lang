# Zan.Gui 架构：现状、目标与迁移路线

> 2026-11-06 立。台账：B-ID171～B-ID179（`TASKS.md`）。本文是 Zan.Gui 分层重构的
> 唯一设计依据——阶段内容、验证门与完成标志以此为准；执行中结论变化时改正文，
> 并在文末「决策记录」留一行。2026-11-09 用户裁定两范式全统一并取消画布例外
> （见 §1 与决策记录）；同日下午二次复核更新现状表（Render 入口包内清零、
> WidgetId 作用域栈退役、OverlayPopup 状态转实例、HMI 包环已断），并裁定
> 以工业级标准彻底清理全部不合理封装（见决策记录）。

## 1. 设计意图

GUI 组件只保留**两种创作范式**（2026-11-09 裁定，画布例外取消）：

1. **范式一：HTML + CSS**：`Gui.Markup/Html` 由底层渲染管线解析为现代盒模型，
   外观由 CSS 级联决定，布局只有 CSS（Flex/Grid/Flow）一个引擎。
2. **范式二：保留模式 + 声明式对象树**：代码侧 `Children = [ ... ]`、
   `ClassName = "..."` 建树，排版纯 Flex/Grid/Flow；动态宿主（设计器、
   PropertyGrid）走 `GetProp/SetProp` 属性协议。

数据绑定是范式二的附属：HTML 窗口用 `data-bind`（ModelBinder），代码侧用
`Binding<T> data` 字段。像素绘制**只存在于控件 `OnPaint` 的内部实现**——它是
范式二控件的实现机制，不是创作 API：公开面不允许出现 `Render(App, …)` 即时
入口或任何应用侧 `Draw*` 调用。图表曲线、仪表指针、代码高亮这类"画布类内容"
一律**收编为范式二控件**：控件骨架走 Children/ClassName 与 CSS，像素画在
控件自己的 OnPaint 里。

**游戏域边界**：`Zan.Game` 引擎核心的世界渲染（GameViewport/`Entity.OnRender`/
Kit）是游戏引擎职责，不属 Gui 控件两范式管辖；但游戏 HUD 面板、场景编辑器
（SceneDesigner）、游戏菜单等 UI 面是 Gui 控件，纳入范式二统一。

**控件纯实例化**（2026-11-09 补充裁定）：控件是实例对象，其状态只存在于实例
字段与控件树可达的宿主（OverlayHost/ModelHosts）上。静态**可变**状态与静态
逐帧入口是根本性封装错误——多实例/多弹层同屏时互相污染——一律退役：
`Core/WidgetId.zan` 的全局 ID 栈（static seq/idStack/seqStack）、
`Core/OverlayPopup.zan` 的 static 遮挡与悬停字段（occl*/lastRichHover）、
`Widget/Layer.zan` 的静态打开态队列（RegisterOpen/TopmostDialog/Close 族）、
`Core/Ui.zan` 的静态轮询查询族。控件类上的 static **纯函数**（几何/文本度量/
颜色计算助手，如 `IdHash`、`Tooltip.TriDown`、`WrapLines`）无害，收敛到基元层
保留。

凡不服务于这两种范式的机制都是历史包袱，按本文路线退役。

## 2. 现状（当前实现：2026-11-06 源码审计立表，2026-11-09 实测复核更新）

主线是健康的：五个装载入口都汇聚到 `Html.Parse`，绘制管线只有一条路径，
SelectBox/Input/Button 的外观全部走 CSS（零硬编码颜色）。臃肿集中在下面几处
（路径相对 `packages/Zan.Gui/src/Gui/`；行内标注为 2026-11-09 实测差异）：

| 问题 | 证据 |
|---|---|
| 分层没有边界 | Core/Layout/Styling/Rendering/Text/Media 同属扁平 `namespace Gui`。编译器的 `internal` 按命名空间生效、没有包级可见性，zan.pkg 都不声明依赖，任何层都能调用任何层。Core 的 `using Gui.Widget;` 已从 23 个文件清到 6 个（2026-11-09 下午实测；ControlBootstrap/ModelBinder 为活引用，随 P1/P2 清零） |
| 注册机制过渡态（包环已断） | ControlBootstrap 的 `using Gui.Hmi` 与 9 个 HMI 分支已删（4d81e9010）；HeavyControls 第二梯子正由并发会话并入 `ControlFactory.Register`；kind→new 大开关仍躺在 Core（P1 移 Widget/），终态待编译器生成控件清单（P1 ① 小设计先行） |
| App 上帝对象 | App 主文件已瘦到 804 行（2026-11-09 实测），但职责仍揉在 `Core/App.*.zan` partial 里（合计约 6,981 行，2026-11-09 下午实测）：窗口、帧循环、故障护栏、事件分发、即时模式输入仲裁、脏区、壁纸与毛玻璃绘制、颜色计算、作用域 ID 栈、浮层列表、HTML 加载与链接导航、IME——DamageTracker/FrameLoop/WindowHost/BackdropPainter 等独立类尚未抽出，partial 拆分只拆了文件，没拆职责 |
| 静态可变状态（2026-11-09 下午复核） | e7cad1a60 已退役 WidgetId 作用域栈（仅剩全局计数器 seq）、OverlayPopup 的 occl*/lastRichHover 转实例字段（剩余 static 全为纯工厂/几何助手）；Layer 打开态队列改 per-App AppLayerContext，仅剩 `Widget/Layer.zan:15` 静态 fallbackContext 兜底；`Core/Ui.zan` 静态轮询查询族（状态读自 App 仲裁区）与 WidgetId 全局计数器仍待 P6 退役 |
| 输入在绘制时轮询 | 路由事件（隧道/冒泡）已经实现且 `App.ProcessEvent` 会先走它，但生产控件仍以绘制期 ClaimClick/BlockHitsRect + `Ui.Clicked` 轮询为主路径，命中目标依赖上一帧的绘制期登记 |
| 双范式（包内入口已清零） | Zan.Gui 内 10 个 `Render(App, …)` 即时入口已由 e7cad1a60 清零（2026-11-09 下午实测全包零命中）；外部调用面（Charts 517 处 Draw、DataTable 75+、Designer 209、游戏 HUD、IDE 面板等）仍是即时模式，以 P6 第 6/7 步改写与盘点为准 |
| 4 套布局 | CSS flow/flex/grid 为正道；Dock 引擎（`Core/Dock.zan`、`Core/Serialize.zan`）已删（P5 第 1 步落地，2026-11-09 实测），剩解析期脱糖兜底：`Markup/Html.Build.zan:44,568-575` 的 data-dock/data-fx 分支；带 `data-dock/fx/span` 的设计器文档实测 43 个（原 42） |
| 绑定 7 条通道 | data-bind/ModelBinder、`Binding<T>`、控件事件、GetProp/SetProp、Signal*、FormField/fname、z-mount。`Signal.Changed`/`ChangeTracker` 已删、SyncBinding 已由 `Control.RenderTree` 统一驱动（STDLIB_COMPONENT_STANDARDS §6）；但 Signal* 仍是 IDE 应用状态载体、ModelBinder 的 `is <控件>` 特判仍在（`Core/ModelBinder.zan:260`），可绑定值契约未落 |
| 弹层重复 | `App.AddOverlay(OverlayPopup)` 仍写死具体控件（`Core/App.Render.zan:418`）、`Control.OnPaintOverlay` 6 处、Layer 自带静态队列、Modal——四条路径并存；`Core/OverlayHost.zan`（133 行）骨架已落；OverlayPopup 已减到 1,085 行，SelectBox 已拆入 `Widget/`（.Cascader/.Hierarchy/.Props） |
| 文档漂移（收口中） | zan.pkg 描述与 README 已在并发会话工作树重写（兄弟包边界已澄清，待其提交）；`docs/agent-kb/gui-development.md` 有在飞改动，Signal 推荐清理以该会话收口为准；本文 §2 现状表已于本轮完成下午二次复核 |

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
- **干净断裂**（2026-11-09 用户裁定，取代「先兼容后删除」）：职责搬走即同步改
  全部调用方（同一提交），不留转发壳、不留脱糖兜底；设计器文档的方言改写并入
  对应手批。机检棘轮保证违规只减不增；static 可变字段棘轮延后到清理批次完成
  后上，锁定更低基线。
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
   Zan.Gui 不得引用兄弟包命名空间；Zan.Gui 以外不得新增 `new Signal*`；
   Gui.Core/Widget/Component 不得新增 static **可变**字段与控件静态逐帧入口
   （static 纯函数助手除外，2026-11-09 裁定）。
4. 清噪声、修文档：删死 using（Core 23、Widget 7）；修 `zan.pkg` 描述、README、
   `docs/agent-kb/gui-development.md` 的 Signal 推荐、gui-design skill 里已不存在的
   QualifyKind 描述（skills 三副本同步）。

### P1 断开包环，按目标层摆目录（B-ID172）

1. 注册梯子退役（2026-11-09 裁定：Install() 类手工注册属不可行历史方案，
   原 HmiBootstrap 外移方案作废）：候选替代为编译器生成控件清单——类型在
   编译图内即自动可按 kind 构造、miss 显式报错，替代 ControlBootstrap/
   DataTableBootstrap/ChartBootstrap/CefControlBootstrap 全部手工注册；
   小设计先行为本步前置。HMI 包环已删（4d81e9010）；HeavyControls→
   ControlFactory 归并由并发会话落地中。
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

逐项抽出，每项一提交。干净断裂：职责类落地即同步改全部调用方（含包外），
App 不留转发壳。

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
6. 复合控件与兄弟包（2026-11-09 裁定：画布例外取消，全部统一）：Tabs/TreeView/
   ListView/VirtualList/Pagination/Wizard/Transfer/Ribbon/Layer；FilePicker/
   SessionList/BandGrid/PropertyGrid/LogView/GraphView/ChatView；DockHost 退役，
   IDE/Workplace 停靠改 Flex 分栏 + 拖拽逻辑；DataTable/Charts/CodeEditor/
   Industrial 整体改写为范式二控件——外壳/骨架走 Children/ClassName 与 CSS，
   曲线/指针/高亮等像素绘制收敛为各控件自己的 OnPaint 内部实现，公开 API 只留
   控件挂载 + data/Binding 数据面，删全部 `Render(App…)` 入口与外部 Draw 调用面
   （Charts 517 处、DataTable 75+ 处等）；游戏 HUD 视图（templates/game 的
   OnPaint 视图）与 SceneDesigner/HUD 面板同理纳入（世界渲染不在此列）。
7. 调用方迁移（514 处）：设计器预览改离屏渲染（`Form.CaptureSnapshot`、
   `App.Offscreen`）；Designer.Inspector/Designer.zan 改保留式属性行（复用
   PropertyGrid）；SceneDesigner 改保留式属性列表；IDE 面板逐个改保留式子树；
   `examples/gui_gallery` 并入 `examples/html_gallery` 后退役
   （`policy_gallery_coverage` 改查后者）；`examples/gui_charts`、游戏模板与
   `Zan.Game` 类目包的 HUD/面板调用方逐个改挂范式二控件；改写 16 个即时模式测试。
8. 删除：控件 `Render(App, …)` 入口、InputArbiter、Ui.Activate*、WidgetEvents 轮询；
   **静态可变状态退役**（2026-11-09 补）：WidgetId 稳定 id 全局栈并入焦点树/
   控件引用，OverlayPopup 的 static occl*/lastRichHover 改实例字段，Layer 静态
   打开态队列并入 OverlayHost；`Rendering/Mat4.zan`、`Mesh3D.zan` 外迁
   `Zan.Game` 引擎核心（Gui 包内零消费者）。机检禁止回流。

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
- 控件纯实例化：Gui.Core/Widget/Component 无 static 可变字段与控件静态逐帧入口
  （static 纯函数助手除外）；除控件 OnPaint 内部实现外，全仓无 Canvas/Draw 创作
  调用（图表/HMI/游戏 HUD 的像素绘制只出现在控件 OnPaint 内部）。
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
- 2026-11-09（干净断裂裁定）：迁移策略由「先兼容后删除」改为**干净断裂**——
  职责搬走即同步改全部调用方（同一提交），不留转发壳与脱糖兜底；static 棘轮
  延后到清理批次完成后锁定更低基线；P3 先行开工（P1 注册机制由并发会话与小
  设计推进，不阻塞职责拆分）。执行总则与 P3 正文随改。
- 2026-11-09（下午）用户裁定：以工业级高可用高扩展标准执行彻底清理——全部
  不合理封装、不必要文件、臃肿不可扩展组件都在清理范围，不限于路线点名项；
  两范式与控件纯实例化裁定不变。同日二次复核落地项：Render 入口包内清零、
  WidgetId 作用域栈退役、OverlayPopup 状态转实例（e7cad1a60），HMI 包环删除
  （4d81e9010），Component 重控件迁出 Workplace（bdea08a5b）；§2 现状表随改。
- 2026-11-09 注册机制裁定（自 B-ID172 台账同步）：Install() 类注册属不可行
  历史方案，退役；HmiBootstrap 外移方案作废；候选替代为编译器生成控件清单，
  小设计先行（见 §4 P1 ①）。
- 2026-11-09 用户裁定：GUI 组件只保留两范式（范式一 HTML+CSS；范式二声明式对象树
  `Children`/`ClassName` + 纯 Flex/Grid/Flow），画布例外取消——原 P6 第 6 步
  "DataTable/Charts/CodeEditor/Industrial 只改输入，Canvas 自绘保留"作废，全部统一
  改写为范式二控件（OnPaint 仅为控件内部实现）；游戏 HUD 视图/SceneDesigner 面板
  纳入，游戏世界渲染保留为引擎职责。同日补充裁定"控件纯实例化"：静态可变状态与
  控件静态逐帧入口（WidgetId 全局栈、OverlayPopup static 遮挡/悬停字段、Layer
  静态打开态队列、Ui 轮询族）随 P6 一并退役，static 纯函数助手保留。现状表按
  2026-11-09 源码实测复核（App 主文件 804 行但职责仍在 partial、Dock 引擎已删、
  OverlayHost 骨架已落、设计器方言文档实测 43 个）。
