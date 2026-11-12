# Zan.Gui 架构：现状、目标与迁移路线

> 2026-11-06 立。台账：B-ID171～B-ID179（`TASKS.md`）。本文是 Zan.Gui 分层重构的
> 唯一设计依据——阶段内容、验证门与完成标志以此为准；执行中结论变化时改正文，
> 并在文末「决策记录」留一行。2026-11-09 用户裁定两范式全统一并取消画布例外
> （见 §1 与决策记录）；同日下午二次复核更新现状表（Render 入口包内清零、
> WidgetId 作用域栈退役、OverlayPopup 状态转实例、HMI 包环已断），并裁定
> 以工业级标准彻底清理全部不合理封装（见决策记录）。

## 1. 设计意图（开发约定与迁移目标）

GUI 组件的开发约定与迁移目标只保留**两种创作范式**（2026-11-09 裁定，画布例外取消）：

1. **范式一：HTML + CSS**：`Gui.Markup/Html` 由底层渲染管线解析为现代盒模型，
   外观由 CSS 级联决定，布局只有 CSS（Flex/Grid/Flow）一个引擎。
2. **范式二：保留模式 + 声明式对象树**：代码侧 `Children = { ... }`、
   `ClassName = "..."` 或 Add/With 建树，排版纯 Flex/Grid/Flow；动态宿主（设计器、
   PropertyGrid）走 `GetProp/SetProp` 属性协议。集合入口必须维持 parent 与宿主继承，
   相关初始化器修复的验收状态见下文收口盘点。

两种模式共用属性与绑定协议：HTML 的 `data-bind` 和代码树的绑定元数据均由
ModelBinder 按 PropSpec 默认投影及公共 GetProp/SetProp 分派；代码也可直接使用
控件的 `Binding<T>` 字段。按目标契约，像素绘制**只存在于控件 `OnPaint` 的内部实现**——它是
范式二控件的实现机制，不是创作 API：公开面不允许出现 `Render(App, …)` 即时
入口或任何应用侧 `Draw*` 调用。图表曲线、仪表指针、代码高亮这类"画布类内容"
一律**收编为范式二控件**：控件骨架走 Children/ClassName 与 CSS，像素画在
控件自己的 OnPaint 里。

**游戏域边界**：`Zan.Game` 引擎核心的世界渲染（GameViewport/`Entity.OnRender`/
Kit）是游戏引擎职责，不属 Gui 控件两范式管辖；但游戏 HUD 面板、场景编辑器
（SceneDesigner）、游戏菜单等 UI 面是 Gui 控件，纳入范式二统一。

以上是新增代码的约定与遗留代码的迁移目标，不是全部调用方已迁完的声明。
2026-11-11 本节仅做入口与布局指导的静态源码校对，运行验证以本轮交付
记录为准；下节标注日期的既有审计结论不等于本节重新验证通过。

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

## 2. 现状（既有审计：2026-11-06 立表，2026-11-09 实测复核；非本轮重验）

已核对入口（2026-11-11，仅静态源码）：`Form.Show` 完成约定文档与窗口装配；
`Gui.Html.Load(App, string, HtmlHandlers, string)` 返回页面树并合入作用域样式，
经 `Parse` 自动 Install，不自动挂父树或注册模型宿主。`Control.dock` setter
仅 value=5 调用 Grow，其余值无操作；旧 DockBottom/DockManual 不能作布局指导。

### 上轮已修契约与验证记录（2026-11-11，保留结果；本轮未重跑）

- **GL 图像槽与 CPU 语义对齐**：使用全局 0..511 槽；rect/sub/clip/patch
  路径与 CPU 一致，资源销毁或后端切换时使相关缓存失效。
- **Metal 能力如实报告**：当前仍为 CPU 代理，不报告 ready；选择后端时
  回落到可用的 GL 或 CPU，不将代理路径计为原生 Metal 实现。
- **Control 宿主上下文传播**：迁移子树时同步继承 App 并清除样式缓存；
  显式 `UseApp(app)` 的覆盖保持有效，`UseApp(null)` 恢复继承宿主 App。
- **HUD 输入与捕获点击**：HUD 树须绑定 Visual Tree 输入根，RenderTree
  本身不接输入；捕获祖先只有在按下目标和实际释放命中均属其子树时才合成
  Click。OnClick 行为钩子之后由 TriggerEvent 统一通知一次，避免双触发。
- **属性 setter 临时接收者 ARC**：setter helper 已消费拥有所有权的临时
  接收者，赋值路径不再重复释放，修复该路径的双重释放。

上轮验证边界：`STYLE_CONTEXT_CACHE_OK`、native image + CPU/GPU 检查通过；
runtime、style、async、hardening、IDE 共 5 条 ctest，以及 ARC 新旧回归与
pipeline 共 3 条 ctest 通过。HUD 的 `guihost_loop_test` 独立探针输出
`GUIHOST_LOOP_OK`；正式 `conformance_guihost_loop` 亦通过（134.26 秒，含编译），
原四次点击及新增 disabled、拖外释放、默认/Float 单次通知回归均通过，合计
9 条正式 ctest。`game/match3`、`gui/gui-empty`、`gui/gui-html-responsive`、
`gui/gui-hud-anchor`、`gui/gui-scene-router` 五个模板编译验证通过。以上保留
上轮结果，本轮未重跑，不计为下述 SceneDesigner 检查器的本轮正式验证，
也不代表全架构迁移完成。

### SceneDesigner 检查器迁移进度（2026-11-12，检查器轮次五项 probe 与正式 12 个目标通过）

本轮检查器已改为保留式子树，五项独立 probe 的输出均已与 golden 严格比较
通过；本轮正式 CTest 的 12 个目标最终均通过。`conformance_ide_wizard_interaction`
覆盖真实点击、输入、预设、创建/取消/关闭、根替换及回调重入释放，严格 golden
与 leak/guard 均通过；验证限于离屏交互，不等同于实机交互。2026-11-12 完整
IDE 编译链接通过（`IDE_NO_PUBLISH=1`），仍保留已知 LSP/Form 闭包警告边界。
核实来源为 `tests/gui/` 下对应测试、`tests/conformance/` 下 golden、
InputNumber 两项 conformance 源码及本轮正式 CTest 收口结果；以下记录仅覆盖
所列范围，不据此声明 SceneDesigner 或 IDE 整体迁移完成。

- **持久控件树**：每个 SceneDesigner 惰性创建一个 `SceneInspectorView`，以标准
  `ScrollColumn`、`Panel`、`Label`、`Input`、`InputNumber`、`Button`、
  `ColorPicker` 组合属性行。渲染与切换选择更新值和可见性，不重建或重新挂接
  输入控件，保留实例身份、父子关系与事件订阅。
- **模型提交**：输入的 Change 与按钮的 Click 进入检查器提交方法；同步模型到
  控件时抑制提交，数值、文本、颜色及模式/锚点等写入先比较旧值，避免无变化
  的撤销记录。颜色编辑保留独立透明度。资源选择器程序调用 `SetText` 后显式
  调用 `CommitInspectorText`，不能依赖不会由 SetText 触发的 Change。
- **区域挂载与生命周期**：独立设计器和 IDE 属性 dock 共用
  `HudLayer.RenderIn(app, x, y, width, height)`，在宿主给定区域执行同一子树的
  Measure/Arrange/RenderTree，并接入 Visual Tree 输入聚合根；不重新收养节点。
  关闭属性 dock 或离开场景页时调用 `SetInspectorVisible(false)`，由
  `HudLayer.SetVisible(false)` 摘除该层输入入口，并清理属于该层的焦点与
  指针捕获。

本轮 probe 结果（严格 golden 比较通过）：

| probe | 已覆盖边界 |
|---|---|
| `scene_designer_color` | 检查器颜色编辑回归 |
| `scene_designer_inspector` | 真实离屏输入、单次提交与无变化不添 undo、Undo、anchor、RGB 与独立 alpha、资源字段显式提交、滚动、hidden 释放焦点，以及重复绘制/切换选择/重新显示后的根与属性行身份稳定 |
| `input_offscreen_sequence` | 同一事件重复绘制仅消费一次；新字符与 keydown/up 正常处理 |
| `input_number_lifecycle` | 11 项严格 golden，启用 `--check-leaks --arc-guard` 通过 |
| `input_number_step_bounds` | 10 项严格 golden，启用 `--check-leaks --arc-guard` 通过 |

`HudLayer` 隐藏时释放本层 pointer capture 属于当前实现契约；上述检查器
probe 直接断言的是 hidden 释放焦点，未把 capture 释放或 IDE 实机交互列为
本轮已验证项。两个 InputNumber 的泄漏检查结果也不外推到整个检查器或编译器。

编译器 `arc_implicit_property_receiver` 独立 probe 已通过严格 golden 比较及
`--check-leaks --arc-guard`，原 16 个残留已消失；bare getter owned/receiver
AST 与临时集合属性接 `Contains`/`IndexOf`/`LastIndexOf` 的 own/drop 均已
probe 验证。trace 确认 Bundle 正常；无 `-g` 时两个分配点共用 site 编号曾使
line 75 被误归因，需以 trace 或 `-g` 确认实际来源。

本轮正式 CTest 的 10 个目标最终均通过：整组初跑 9/10，collapse 的两条尾部
断言因测试将同步 `TriggerEvent` 当成延迟调用而失败；改用
`header.On.Click.Post()` 后，仅该 case 重跑通过（50.41 秒），实际 17 项断言
全部通过。collapse 修正的是测试的异步排队方式；wizard 修复的是关闭按钮真实
内容缺失（`singleClose = Button(IconView)`）。两者均保留 golden 与有效断言。

新增正式 `case_child_window_callback_lifecycle` 通过（57.85 秒），6 项断言及
`--check-leaks --arc-guard` 均通过；`conformance_ide_wizard_interaction` 亦
通过严格 golden 与 leak/guard，覆盖真实点击、输入、预设、创建/取消/关闭、
根替换及回调重入释放，验证限于离屏交互。本轮累计 12 个正式目标通过。

IDE 原 17 条旧 API 错误已清除，Scaffold 两处问题已修；2026-11-12 本轮完整
IDE 编译链接实际通过，exit 0，输出 `IDE_BUILD_OK build\ZanIDE.exe`
（`IDE_NO_PUBLISH=1`；日志 `call_89c0e4b0d90b4693b5b9147e8c2947be-stdout.log`）。
该结果不含发布或实机交互验证，已知 LSP/Form 闭包警告仍保留，不能当作警告已
清零。本轮 12 个正式目标及完整 IDE 编译链接通过不代表全架构迁移完成。

尚未迁移的部分仍包括元素调色板、图层/资源/上下文菜单、画布交互与绘制，以及
IDE 更广范围的面板和即时入口；本轮检查器改动不代表这些遗留路径已收口。

### 分类向导局部验证与当前优先级（2026-11-12）

IDE 分类向导已落盘持久控件树，离屏交互探针及单条
`conformance_ide_wizard_interaction` 正式通过（100.67 秒，严格 golden、
`--check-leaks --arc-guard`）。覆盖分类/模板、平台支持拒绝、设备与尺寸、
输入保留、创建/取消/浏览返回码、空分类禁用及旧控件不保活向导。
此结果只证明该局部用例；用户报告 IDE 整体无法操作，尚未定位完整根因。
本轮完整 IDE 编译按用户调整的顺序主动停止，没有本轮构建或实机可用结论。

当前先完成 Gui 两种创作模式（HTML＋CSS、代码保留控件树）的共享机制与组件
迁移，再进行 IDE 重构。P0～P8 仍是目标路线，不能将局部回归通过写成全部完成。
IDE 当前落盘改动保留，后续统一接入完成后的 Gui 契约。

### 两种模式收口盘点（2026-11-12，进行中）

本次逐项盘点确认，控件外面已有 Control 包装不能作为迁移完成的判据。
当前基础机制批次落盘了 ChildWindow 输入根登记、ProcessEvent/ApplyEvent/离屏
统一路由、按焦点投递键盘和字符、弹层命中与外击关闭，以及 HTML 属性转发。
窗口路由 O0 探针已通过严格 golden 与 leak/guard；弹层首次 leak 检查发现
App→rootControl→Control.app 强引用环，补显式根卸载后同参数探针通过，宿主
反向引用已改为 weak，随后宿主生命周期及正式定向回归通过。HTML/代码属性比较首次全部通过，但 leak 检查报
548 个残留对象；已精确对账到两棵 Checkbox 的指示器强引用父控件形成的环。
将反向 owner 改为 weak 后，原等价探针以相同 golden/leak/guard 通过，残留归零。
最新窗口路由、弹层路由、宿主生命周期、窗口队列隔离与文本事件期编辑探针均已
通过相同 golden/leak/guard，均为 O0 独立探针。跨宿主文本回归先解除显式 UseApp
覆盖再验证继承宿主，三个控件无需新窗口重绘即可编辑。
Form.Run 已用作用域在正常/异常退出恢复输入根、帧体和嵌套上下文，生命周期
首次回归行为与弱观察通过，但退出期 ARC 报 stale release；已用继承成员与类型同名的最小探针复现，定位为静态字段借用读取被误判拥有值。修正编译器分类并重建后，新增静态所有权、两条相邻 ARC 回归与 Form 正常/异常生命周期原探针均通过 golden/leak/guard。既有 input_offscreen_sequence 同参数通过；Game 多层 HUD 回归及 GuiHost 正式定向测试随后通过。
本批 12 项正式定向 CTest 已全部通过（0 失败，398.38 秒），覆盖静态所有权及相邻 ARC、窗口/弹层路由、HTML 属性等价、宿主/队列/文本/Form 生命周期和 GuiHost。日志：`build/test-out/targeted-20261112-033307.log`。这不是整个 Gui/IDE 的真实窗口交互验收；剩余迁移见下表。

TreeView filter 通过输入子树归属、焦点查找与命中覆盖接入统一路由，不混入行 children；替换、移除及跨 App 继承均释放或迁移相应输入状态。ColorPicker 的七个通道 Input 已归 ColorPalettePanel 的真实 children，按格式显隐与布局；Panel weak 借用 Picker，OverlayHost 关闭/替换时释放旧弹层输入子树。Enter 提交走 routed key，输入在重绘前生效。该生命周期回归同时暴露并修复编译器 `as` 的 owned 操作数分类：成功转移引用，失败释放 owned 源；借用源保留原语义。

本批三项正式定向 CTest 全部通过：`case_arc_as_owned_operand`（2.96 秒）、`conformance_gui_treeview_filter_input`（57.20 秒）、`conformance_gui_colorpicker_input_lifecycle`（57.35 秒），严格 golden、leak/guard 检查通过，日志 `build/test-out/targeted-20261112-040333.log`。既有弹层路由独立探针亦通过。随后 ColorPalettePanel 的 SV/hue/alpha 与 Slider/SplitPanel 已迁至事件期拖动，新增显式拖动捕获，绘制不再修改拖动值。调色板严格探针及 Slider/SplitPanel 52 项严格 golden/leak/guard 断言通过，包含鼠标/触摸/笔、次键释放、最终抬起坐标、取消与回调替换根；这些后续修改尚待本批正式定向 CTest。

共享 CSS 滚动已改在公共冒泡路由中处理：滚动条 chrome 先于内容钩子、未消费的滚轮后于内容钩子，实际移动才消费；绘制只读共享偏移。该公共路由独立严格探针已通过；离屏重放现只恢复原滚轮载荷，避免重复路由和序号回退，真实重放→下一指针/滚轮的完整严格 golden/leak/guard 已通过。ScrollColumn 已移除绘制期私有 ScrollView，使用同一滚动几何、路由与捕获；布局及完整输入生命周期两个严格探针均已通过（`build/test-out/gui-scrollcolumn-input/`），包含裁剪行释放输入、触摸/笔拖动、次键释放、取消与混合旧消费者。内联 padding 简写现由样式盒参与实际布局，四边、替换样式与显式 Pad 优先级检查，以及100%–300% DPI严格回归通过。

ScrollColumn 退出期单个列表残留由纯语言 getter.Clear 探针锁定为编译器临时接收者未释放；List.Clear 已补配对所有权清理。重建期间发现的 Windows TLS 崩溃另由无 Clear 的静态 Mutex 程序复现，定位为 GNU ld 给 TLS 偏移写入错误 PE 基址重定位。Windows 原生链接现优先 LLD；分配器真实入口从同一已加载 CRT 解析四个函数，避免导入跳板被 wrap 后递归。实际重建的 zanc/runtime 已通过 setter、Clear、静态 Mutex 及组合严格探针，PE 检查 HIGHLOW 数为0，故意引用环仍准确报告预期单个泄漏。本段严格结果已验证，本批正式定向 CTest 待统一运行。

HUD 与 Layer 已共用 `InputRootAdapter`，结构身份查询独立于模态输入可达性；Layer 优先级高于 HUD，重复绑定保留现有位置，摘除时跨适配器恢复 fallback。真实组合严格回归暴露编译器隐式属性 setter receiver 被无配对释放，已由不依赖 Gui 的最小探针复现；编译器已修为显式 borrowed `this` 接收者描述，重建后原始纯语言探针与新增 `arc_implicit_setter_receiver` 严格 golden/leak/guard 均通过；随后真实 HUD/Layer 组合的 39 项严格 golden/leak/guard 检查全部通过（`build/test-out/gui-input-root-composition/strict.verdict.log`），涵盖双向绑定顺序、模态拦截、异构摘除、跨 App 往返与 weak/fallback 寿命。Layer 自身完整生命周期严格 golden/leak/guard 随后通过，覆盖模态背景拦截、页脚按钮、标题栏拖动/关闭、最小化命中、输入根替换、适配器摘除/重绑与 weak 寿命；本批正式定向 CTest 尚待统一运行。

仍需收口的公共依赖：

| 范围 | 当前缺口与验收边界 |
|---|---|
| 窗口生命周期 | Dispatcher 无重置初始化、App 待执行工作按窗口取消、Control.app/文本宿主缓存 weak 与 Form 退出作用域已通过本批正式定向回归，覆盖兄弟窗口队列与正常/异常释放；其他组件借用的宿主缓存仍待逐项迁移 |
| 文本编辑 | Input、TextArea、InputOtp 的事件期编辑与跨宿主路由已通过正式定向回归；TreeView filter、ColorPicker 通道输入子树及生命周期已通过新增正式测试。Layer prompt/view/footer 已通过完整严格生命周期探针及本批正式定向回归。经典 Browser 的地址栏现登记为持久输入根，SkinBuilder 的 colA/colB 与独立 cssBox 归同一普通 Control 根及两列裁剪视口；关闭/重建释放输入和静态强引用，实际四文件多源严格回归已通过 exact golden、正常退出 leak/guard 与 strict-runtime，覆盖地址栏编辑、绘制不重放、缩放/重建/关闭、HEX/CSS 编辑、原高度150隐藏列的捕获释放与弱寿命（`build/test-out/browser-skin-caller/result.json`）。Workplace SessionList 的搜索/活动重命名、GameAssetTool/Mge 的活动页与裁剪内字段、Gallery 的临时包装树仍须接入输入树。CEF Form 示例不属于该缺口 |
| 拖拽与滚动 | ColorPalettePanel 的 SV/hue/alpha、Slider、SplitPanel 已移至事件期，严格探针通过，待本批正式回归；共享 CSS 滚动与 ScrollColumn 的共同路由、捕获与取消严格探针已通过，ScrollState 已移 Core 以消除 Layout 向上依赖。BandGrid 使用固定表头之外的共享 ScrollViewport 与事件期选区；VirtualList 使用真实行子树，保留重叠行实例、释放离屏行输入，并缩放显式逻辑行高。两者新严格回归已通过，包含150%/200% DPI、重绘前连续输入、跨宿主与 weak 寿命（`build/test-out/scroll-widgets-input-strict.log`）；正式 CTest 待本批统一运行。共享滚动的 CommitScrollOffset/OnScrollChanged 现统一程序赋值、滚轮、拖动及布局钳制；同值不通知，双轴提交后再通知，最终抬起先结束旧捕获，回调重入不覆盖新捕获。RichText 与 CodeBlock 已使用该协议，保留行数信号兼容投影，链接、选区与复制按钮走真实输入；配对输入用例及原 RichText 金样均通过严格 exact golden/leak/guard，正常退出 stderr 为空，覆盖独立 frame 下的自然弱释放（`build/test-out/richtext-codeblock/final-gate.log`）。RichText 的 LineAt 返回深快照，资源前文本段与资源 advance 的实际宽度缺陷已由探针确认并修复。TreeView/ListView、GraphView/Transfer 的迁移已通过下述独立 fresh strict 验证，相关新增正式 CTest 与其他旧调用仍待收口；混合路由/旧绘制消费的事件序号防线严格探针已通过 |
| 属性与绑定 | 既有 HTML/代码属性等价测试已通过；ControlBootstrap 已合并为70项单处构造注册并补 RadioButton，Radio 指示器反向 owner 改 weak。新工厂严格回归发现的默认事件反向强环已改用 weak owner action；70项构造/克隆/HTML映射的弱寿命检查已全部执行通过，Tabs 重建父链及业务内容 helper 已修正；工厂完整功能与正常退出严格 golden/leak/guard 均已通过，原420项残留在编译器 is 临时对象根修后归零。PropSpec 默认绑定/值类型与 ModelBinder 公共分派已通过完整 HTML/代码配对严格回归：字符串选项键、数字索引、显式 bindProp、POCO/dictionary 数值与 bool 均通过。WireNode 的 weak binder/control token、重复接线合并、调用时查询当前 handler，以及 Detach/root/model 替换时旧 token 失效、迟到队列与弱释放断言均实际执行并通过 golden/leak/guard。SelectBox 索引/键/标签已分离；FormGroup 的 FormRow 以 HasValue 将未选 SelectBox 投影为空校验值，Check/Peek 共用该逻辑，保留普通文本“-1”的有效语义。直接消费者独立严格 golden/leak/guard 回归已通过。更广范围的布局与实际事件等价仍须逐项补齐 |
| 复合控件与扩展包 | ContextMenu、Ribbon、Transfer、Charts、DataTable、CodeEditor、Workplace、Industrial、Designer 等仍有旧入口或绘制期业务输入。ContextMenu 的 ShowAt 已复用共享 OverlayHost 与真实 Button 子树，新严格回归通过 exact golden/leak/guard，覆盖重复配置复用、迟到会话、关闭或业务回调重开与跨 App 寿命（`build/test-out/contextmenu-routed-strict.log`）。同 App 的捕获取消重开及锚点通知守卫也已严格通过；后续静态复核发现取消回调跨 App 重开时的旧会话覆盖边界，已由真实探针复现旧通知清状态及双 host 登记；控件附属整型登记代次与 ShowAt 回调后检查修复后，探针和完整菜单/弹层严格回归均通过，同一实例只留在最终宿主，后续实际选择正常（`build/test-out/contextmenu-crossapp-strict.log`）。Workplace DockHost 已迁至持久 ContextMenu 与真实 ShowAt，旧 RenderMenu 及唯一调用已删除；实际调用方严格 exact golden/leak/guard 回归通过，覆盖打开时面板快照、重绘前选择、关闭后的排队动作失效、捕获/焦点释放及弱 owner 寿命（`build/test-out/contextmenu-callers/result.json`）。Game SceneDesigner 和其余模板等静态 Render 调用仍待迁移；WeChat 实际调用方已完成下述独立严格验收；Designer 四种菜单已接入 ShowAt，隐藏路径严格回归通过，可见 Ribbon 回调环经 Designer 外层与 Ribbon tab/item/command 三类 weak action 修复，BuildTree 以 RemoveAll 和代次清退旧子树与动作；独立严格探针确认 Designer/App/三类树自然释放，正式 `conformance_gui_designer_contextmenu_routed` 随4并发失败项复验通过（150.23秒），覆盖可见重建、真实输入及焦点/捕获清退（`build/test-out/gui-migration-acceptance/retry-28-jobs4.log`）。Ribbon 完整 Fit/overflow 与 Gallery 静态调用迁移仍待完成。Layer 严格生命周期与本批正式回归均已通过。需逐个迁移实际调用方；控件内部像素绘制本身允许保留 |
| Game UI | Control 输入子树归属/焦点查询、HudInputRoot 聚合覆盖、移除/清空释放与根重排保留有效捕获已落盘；GuiHost 新增多层键盘/捕获/隐藏断言及既有帧循环在严格 golden/leak/guard 下通过。初次正常退出的 1024 残留精确归因于测试循环↔Button 回调环，Stop 解除测试回调后同参数归零。GuiHost 已随 12 项基础机制正式定向 CTest 通过。后续针对世界输入另行迁移：GuiHost 显式声明内容布局根，GameViewport 在共享指针/键盘路由中接收局部坐标、焦点和拖动捕获，已移除绘制后旧命中桥接；FocusManager 保存弱控件身份以即时清理隐藏/禁用旧焦点，内容根替换在释放回调后重验适配器链，避免覆盖回调的新树。新迁移完整既有回归已独立通过严格 exact golden、正常退出 leak/guard（`build/test-out/guihost-routed/green-compile.log`、`green-run.log`）；SceneRouter 已补显式内容根挂载和场景进入、切换、退出的重入保护，HtmlScene 反向 router 引用改 weak；独立严格回归已通过 exact golden、正常退出 leak/guard，覆盖实际 HTML 输入与参数按钮、OnEnter Go/Stop、OnExit Stop、重复 Stop、保留 scene 的弱寿命，以及焦点/取消回调换根（`build/test-out/guihost-routed/final-router-compile.log`、`final-router-run.log`、空 `final-router-stderr.log`）。真实游戏窗口的视觉、交互和各调用方迁移仍未完成。世界渲染不列入普通 UI 控件迁移 |

当前完整模型绑定严格回归已通过。其前置编译器缺陷由纯语言探针复现：三种 Dictionary 对三种泛型类型的9次 `is` 原先全为 true。共享完整类型名修复后，site 表与 descriptor 两条路径共5项严格验证全部通过，涵盖键值实参、嵌套集合、跨 namespace 同名类型、is/as 与所有权；Binder 沿用公共属性分派。SelectBox 的索引、选项键、标签协议随后分离，完整 UI 回归实际验证 JSON/字符串字典/POCO 键写回、数值与 bool、重复接线、模型替换、迟到队列及弱寿命。直接消费者 FormGroup 的未选必填语义修复随后通过独立严格验收，覆盖 Check/Peek、清空后重新校验及普通文本“-1”。

工厂完整严格回归已通过。已修正 Flex/Grid/GridItem 未知属性委派递归、Clone 属性重放覆盖独立 name/Class 的顺序、内部挂载误走业务 Add，以及 DynamicTags/Calendar/GraphView 等默认事件的反向强环。Breadcrumb 的停滞由遍历时向同一列表追加造成，已分离解析源与目标列表。最新严格运行已完成70类构造、工厂、HTML及双向克隆的弱寿命检查；随后发现的 Tabs 重建遗留 parent 已改为共享 RemoveAll，复用业务子节点的同族重建同步处理。With 现转发 virtual Add，使自然 Children 与 HTML 使用同一业务入口；Card/Empty/Result 普通 Add 按顺序追加，专用 Content/Extra 保留替换语义，FormGroup 内部挂载使用 base.Add 避免再次分派。完整功能运行最初到达 CONTROL_FACTORY_EQUIVALENCE_OK 后仍在正常退出报告420项残留；编译器修复后保持自然测试写法，fresh 完整 golden 与正常退出生命周期均通过，Children 与 FormGroup 最终版本随后通过同一轮严格验证（`build/test-out/gui-factory-equivalence/green-three-strict.rc.json`）。新增业务内容聚合使用纵向 Panel，克隆逐业务子项重建，避免拷贝生成包装层。独立纯语言探针已确证 `Make() is Derived` 漏释放一个 owned 临时对象；编译器已修复无模式与模式绑定的所有权及条件分支清理，实际重建后原探针、新 `arc_is_owned_operand`、既有 pattern 与 as 共四项严格 golden/leak/guard 全部通过（`build/test-out/compiler-owned-is/`）。工厂随后使用该修复版本完成严格重验，原420项残留归零。独立静态复核又指出循环条件整体为 false，以及 break/continue 后外层异常清理的两处模式变量边界；纯语言 normal/strict 探针分别复现两个对象残留和 stale-reference 释放。补齐退出边与释放后清槽并实际重建后，三个探针双模式、扩展 conformance 双模式和既有 pattern/as 严格用例共10项全部通过，独立静态复查原两路径闭合（`build/test-out/compiler-owned-is-edges/`）。原临时探针目录已由所属任务清理，额外第11项未运行，沿用此前通过证据；没有将缺失输入当作测试通过。异常退出对象报告不能作为正常生命周期泄漏结论。

SkinBuilder 高度150隐藏列残留捕获的根因已证实为自然 `Children = { ... }` 初始化绕过 Control.Add：旧 Children getter 返回裸 List，输入框到 Row 的 parent 链从构建起即不完整。Children 已改为缓存的弱 owner ControlChildren 门面，Remove/RemoveAll/跨父 Adopt 统一清退输入并尊重取消回调中的新归属；无调用的 ChildrenCollection 别名已移除。独立严格回归通过 exact golden、正常退出 leak/guard 与 strict-runtime，覆盖自然嵌套初始化、重绘前输入、跨 App、弱门面/枚举器和取消重入（`build/test-out/control-children-strict.log`）。随后 With 统一转发 virtual Add，Children/FormGroup 已随最终工厂版本通过严格重验；FormGroup 保存输出对既有 `tests/conformance/gui_formgroup.out` 精确核验，修正的是验证脚本的金样路径，没有重跑程序。实际 Browser/SkinBuilder 四文件调用方完整严格回归已通过，保留原高度150场景。后续 HEX 输入零几何由四处弹层内部控件仅设 parent、未实际加入 Card 造成；删除伪 parent 赋值并经正常 Add 收编后，HEX 编辑与弱寿命通过。四类弹层独立回归又发现 DatePicker 递归测量不足，OverlayHost 已改用 MeasureTree 测量完整子树；实际 Browser/SkinBuilder 已在该共享版本重验通过。填充真实树数据后，四类挂载、几何和关闭输入清退均通过；CascadePanel 反向强 owner 的弱释放断言由真实用例确认，改为 weak owner 后，四类完整严格 golden/leak/guard 回归已通过，也验证保留 panel、原 owner 释放后的测量/布局/绘制安全和最终 panel 释放（`build/test-out/popup-factory-mount/result.json`）。BandGrid/VirtualList 新严格回归已通过。本批37条非模板正式用例现已全部通过。首轮38项保留原始结果：9条通过、Designer 测试保留字参数导致1条编译失败、27条单项超时、官方 templates_build 整体900秒超时。修正测试命名后，仅以4并发复验28条失败的非模板用例，27条通过；剩余旧 ColorPicker 夹具因直接写 open、未安装输入根、沿用旧命中编号及没有真实 Down 的捕获断言而失败。夹具改用 SetRootControl、SetOpen、真实 Down/Up 和弹层实际几何，保留原 golden 与 RenderInside 兼容覆盖后，单项正式复验通过（93.70秒）。RenderInside 本身包含测量、布局和绘制，不能将其描述为仅绘制入口。原超时原因由不改源码的 ScrollColumn/Game/滚动用例复验确认与并发竞争相关，但不以此代替逐项结果。模板已删除 idle/RTS 六处无效 Gui.Rendering 导入；官方逐项脚本保留初轮11项及后续8项通过证据，剩余21项经 FILTER 分四组完成；40/40 项均通过完整链接、非空产物与逐项目日志检查。此结果不代表真实窗口交互通过，也不改写首轮 CTest 的900秒超时状态。逐项产物长度、SHA256、原始日志及各片 manifest 汇总在 `build/test-out/gui-migration-acceptance/templates-consolidated.json`，本批37条正式用例与模板续验合并状态见 `current-batch.json`。原始正式输出归档至 `build/test-out/gui-migration-acceptance/initial-38.log`、`retry-28-jobs4.log`、`colorpicker-final.log`；模板检查点与编译器身份一并保存在该验收目录。

上述验收之后又由实际 SelectBox 探针确认：锚点没有焦点或捕获时，替换输入根仍会遗留弹层。App 的根替换与子树摘除现按旧登记代次清退锚定弹层，保留仍属于聚合输入根的锚点；点坐标弹层继续由调用方管理。旧按压与悬停状态先于取消通知清退，焦点按自身端点身份判断，弹层按登记代次判断，根安装仅由另一次换根操作取代，避免回调中新建的捕获、焦点或弹层被旧操作覆盖。完整生命周期回归及原生产 SelectBox 探针均已通过 fresh strict 编译运行、exact golden、正常退出 leak/guard 与 strict-runtime，证据见 `build/test-out/gui-input-retirement/overlay-green-result.json`；正式 `overlay_anchor_retirement` 用例已登记，等待本轮统一定向 CTest。此结果不包含原生窗口验收。

Tree/List 的真实行子树迁移已落工作树。首次绘制前的行编辑器点击曾实测命中、App 与输入归属均正确，但 `focus.IndexOf` 为 -1，暴露 Input 只在 Paint 登记焦点的共享缺陷；原失败和窄探针保留于 `build/test-out/gui-tree-list-strict/`。Control 现显式声明键盘/文本焦点能力，Down 仅检查命中祖先的当前输入归属及可用状态，Tab 临时收集当前树顺序，IME 直接查询当前弱焦点端点；聚合根转发实际输入子树，旧绘制登记保留弱端点兼容。共享焦点正式源与原 TreeView 正式源均已通过 fresh strict 编译运行、exact golden、空 runtime stderr 及双流 leak/guard/runtime 扫描；未先 Paint 或手工赋焦。证据分别为 `build/test-out/gui-retained-focus/20261112-150838-063-bf7c221f/attempt-01` 与 `build/test-out/gui-retained-focus-tree/20261112-151038-189-5ffda7cb/attempt-01`。焦点测试此前两次失败分别为退格事件种类与点弹层宿主/测量遗漏，修正夹具后通过，原红日志保留；生产机制未因此追加变更。ListView 随后暴露继承方法组自然链的编译器缺陷：IR 识别补齐 local receiver 与 this，保留委托字段和局部遮蔽，扩展纯语言 conformance 严格通过后，原 `return new Button("Action").OnClick(this.Click).Height(36)` 可自然编译。最初 Column AV 在最终编译器运行中未复现，未据此宣称已查明其根因。真实输入又确认 List 的 DragOut 会覆盖子 Button 捕获，而点击嵌入 Input 会提前选择行；现保留已有子捕获，并只沿 originalSource 到本 rowsBody 的祖先链识别文本能力，编辑操作不启动行拖出、不选择行。原正式 List 源与旧 treeview_filter_input 均已 fresh strict 通过，golden 未改、runtime stderr 为空、正常退出无 ARC/leak；证据为 `build/test-out/gui-tree-list-strict/20261112-081232-773-30800/summary.json` 与 `20261112-081340-972-31296/summary.json`。临时诊断已精确移除，List 正式源恢复原哈希。最终扩展编译器 conformance 的正式单例以及这些新增 Gui CTest 仍待本轮统一收口；不以中间编译器的单例通过替代最终版本。

GraphView/Transfer 的真实输入与共享滚动迁移已通过两条新增生命周期正式源的 fresh strict 探针，exact golden、空 runtime stderr、正常退出无 leak/guard 诊断；证据为 `build/test-out/graph-transfer-scroll/strict-20261112T072938404Z-20704/summary.json`。Graph 的 Gap/OnPaint 改走自身 ResolveStyle，避免覆盖默认 overflow；平移在下一次真实 Move 遇到禁用或隐藏时取消，不提交新坐标。Binding 属性赋值本身没有同步取消回调契约，原要求赋值瞬间清退的夹具失败保留，并改为真实输入校验；独立 Cancel、最终 Up、子树摘除与绘制不重放覆盖仍保留。原 first-wheel 失败已由新严格运行确认修复；Transfer 本轮首次严格运行通过。相关既有回归与新增 CTest 等待统一收口。

WeChat 聊天气泡菜单已迁至持久 ContextMenu/ShowAt，保留复制0/引用1、真实剪贴板与引用草稿、会话/消息身份及迟到动作失效。以下历史证据路径均相对 `build/test-out/wechat-contextmenu-callers/`：原几何失败保留在 `evidence/` 与 `attempts/20261112T064121Z-37716656bc6e/`；异常退出的73476/81883项报告不作为正常生命周期结论，首轮独立 stdout/stderr 未留存，缺失情况见 `evidence/archive-provenance.json`。ChatBody 补标准 CSS absolute/inset fill 后，原 exe 的全部行为断言通过，但正常退出仍报79149项残留，原输出保留于 `attempts/20261112T070137Z-html-only-fcdea2f4bca7/`。

页面/面板与 ToolStrip/ChatView 的反向回调改为具名 weak owner、宿主缓存改 weak，保留 ChatView 原缓存宿主双击语义；未清空业务订阅。联合 fresh strict 已通过编译/运行0、exact golden、空 runtime stderr、正常退出无 leak/ARC/runtime 诊断，证据为 `build/test-out/wechat-contextmenu-callers/attempts/20261112T073251Z-4cbb71954c1a/result.txt` 及同目录原始双流/编译日志。weak witness 在正常 root/static 清理、退栈及 Drain 后实际确认 Page、Menu、Chat、Toolbar、Drawer、Emoji、Profile、Task 释放。该结果限于正式调用方源的离屏路径，未覆盖异步请求、动态生命周期任务卡片、三个侧栏展开几何或原生窗口；正式定向 CTest 仍待统一收口。joint runner 的 `joint-weak-runner-20261112T073251Z-bfaeed74d903/input.json` 仅记录编译前编译器 SHA256/时间，未记录编译后身份，不能据此声称前后一致。

Retained 静止长按已接入原500ms/12px手势算法与唤醒通道，由显式订阅或能力声明认领，实际 Down 的叶节点作为 source；不依赖绘制、不增加第二个计时器。取消、越界、换根以及 Up/Leave 回调启动新手势的边界已由完整回归与最小 slop 探针通过 fresh strict golden/leak/guard/runtime，证据见 `build/test-out/gui-input-retirement/longpress-green-result.json`。正式用例已登记，原生窗口空闲唤醒、实际子窗口 deadline 投递与更广旧消费者混合场景仍待验收。

PropertyGrid 的保留行/弱动作迁移候选已应用，但本轮实际验证未通过，不能计入完成项。原零 Paint 选择器探针编译成功后在真实点击打开处失败，SelectBox 的 PaintHead 仍处理 leftUp 打开；原失败保留在 `build/test-out/propertygrid-routed-attempt1/`，没有将断言后移绘制来代替输入验收。新生命周期测试在 `Binding<string>.setter` 的静态方法组赋值报 E2098，无 Gui 依赖的纯语言探针已复现，证据见 `build/test-out/propertygrid-routed/binding-setter-compile.stderr.log`。旧 Designer reset 编译通过后在 Input.zan:192 触发 null string length runtime error、退出70，component 测试因 fail-fast 未运行；原双流、退出状态及应用前后身份均保存在 `build/test-out/propertygrid-routed/`。后续真实诊断已将 null 来源锁定为自动属性 Binding：直接读取 Designer 模型的 bindPath/bindProp 均非 null，同一 PropSpec 的合成 Binding 却读到 null；其余18项正常，首次 Bind 后即在 Input192 失败，尚未进入 Reset。无 Gui 依赖的纯语言对照进一步确认自定义 getter/setter 均被绕过，普通字段 Binding 读写正常；输出为 direct=111/property=0/field=31、getter_delta=0，Set 后 backing=11/setters=0/field=44。证据分别在 `build/test-out/propertygrid-routed/null-source-1/`、`null-source-2/` 与 `auto-property-binding/`。源码复核显示 ops_binding 合成访问器把自定义属性当字段槽 load/store，此 IR 缺陷与前述 checker 丢失泛型字段特化的 E2098 是两条独立根因；均需编译器根修后重验，未以 Input 空值归一化或手写委托绕行。SelectBox 零 Paint 输入缺口也仍未完成。

共享 App 循环已落地 `AppLoopParticipant`、`AppLoopRegistration` 与弱 `AppLoopOwner` 登记协议。Tick 在真实逻辑 turn 的输入、Surface 同步与 Post 排空后执行，同 turn 不因 Paint 或递归渲染重复；绝对 deadline 合入主窗口和子窗口唤醒，Invalidate 立即释放 participant，Stop 清退登记。当前语言的 weak 不接受 object，实际编译失败后改为显式 owner marker，App/Control 实现该接口，业务 owner 显式声明；未扩宽编译器规则。原正式测试源已通过 fresh strict，包含真实 PointerDown 中子窗口重挂及嵌套输入、turn 根恢复、Post 顺序、弱 owner/host 释放与 Stop 重入。exact golden、空 runtime stderr、日志哈希和编译器前后同一身份已独立核对，归档为 `build/test-out/gui-shared-loop-result.json`；既有 Form.Show W2072 编译告警保留。此证据仅覆盖共享循环；Browser/native 后续离屏验证见下段，正式 CTest 与实窗空闲唤醒尚未验收。

Browser 的 Cef/WebView/Firefox 视图与 Box 已接入共享循环及原生 entry 生命周期；`ContainsHostedControl` 独立表达结构托管归属，不能复用受模态限制的输入成员查询。隐藏、最小化或模态遮罩保持引擎推进并隐藏原生表面，实际失托管才退休；单 App 停止清退自身 handle，不调用进程级 CefHost.Shutdown。CDP 路由反向 owner 改 weak，跨宿主重建保留 URL 规范化并重置原生焦点同步。native 与 hosted 两组正式源已依次 fresh strict 编译运行通过，精确 golden、空 runtime stderr，编译器前后同一 F4DF 身份；native 覆盖离屏登记/共享 Work/绘制纯度、隐藏祖先/移除、App Stop、CDP weak owner、URL 重挂与自然释放，hosted 覆盖真实 Layer 模态、HUD、adapter Detach、跨 App 与换根。原始运行日志分别为 `build/test-out/browser-retained-lifecycle-20261112-163604-127255/` 与 `build/test-out/browser-retained-lifecycle-20261112-163808-965033/`，对应编译日志为 `163447-097922` 与 `163641-026380` 同前缀目录。此次未启动真实 CEF/WebView/Gecko，未验证原生键鼠、IME 或剪贴板；ControlInputHost 组合及正式 CTest 也仍待后续收口。后续源码复核确认 WorkOnce 的去重字段属于每个 App 服务实例，已通过的测试只覆盖同 App 多 view；主窗口与子窗口分别登记服务时仍可能在同逻辑根 turn 重复 Work。跨 App 去重须补真实反例并向前修复，不能以当前绿色结果宣称该范围已完成。

共享 Click 的双击事实与修饰键快照、OverlayHost 的 Escape 钩子均已应用并通过各自正式测试源的 fresh strict 编译运行。Click 在业务路由前沿用既有 WidgetId/400ms 历史确认双击，以 EventSeq 防止旧绘制兼容路径重复记账；`RoutedEventArgs.isDoubleClick` 与 `modifiers` 保存本次事件事实，回调注入更新输入不会改写外层参数。取消、越过拖动阈值及次键操作不推进主键点击历史。Escape 调用 `OnPopupEscape(App)` 后核对弹层登记版本与宿主，保留回调新建会话；普通失焦关闭不调用该钩子。两组均 compile/run 退出0、exact golden、空 runtime stderr，启用 strict-runtime、ARC guard、leak 检查及 ZAN_RT_HARD；编译器前后 SHA256 均为 F4DF81E69E58B197214C896F5B8C57C0AC6518998616301B8CFE23248D308ABB。证据分别为 `build/test-out/gui-shared-input-01-doubleclick-strict/20261112-082939-798-20464/summary.json` 与 `build/test-out/gui-shared-input-02-escape-strict/20261112-083055-319-27796/summary.json`；既有 Form.Show W2072 编译告警保留。正式 CTest 登记及最终定向收口仍待进行，消费者不能据此视为已迁移完成。

共享滚动几何扩展已应用：默认 `ScrollWheelViewport`、`ScrollBarViewport(bool)` 沿用 `ScrollViewport`，`ScrollWheelModifiers` 默认保留原修饰键；复合控件可扩展滚轮命中范围并分别约束两轴 chrome，仍由同一 owner 保存 offset、extent、捕获与提交通知。原事件 modifiers/delta 不因局部映射而改变，边界继续向祖先冒泡。独立正式源 `scroll_geometry_input_test` 已 fresh strict 通过，覆盖100%/150%/200% DPI、固定列范围、右侧竖条、最终 Up 无 Move、取消后旧 Up、重复 Paint 不提交。证据为 `build/test-out/gui-shared-scroll-strict/20261112-090633-704-33560/summary.json`，compile/run 均0、exact golden、空 runtime stderr，编译器前后同一 C62CD4B7DF9EA9447986F9A8BEE02EF47A3312A99F3243B15DBB05B3CF48D43C。前两次失败分别为测试外部访问 protected pressed 与夹具误置 Ctrl/Shift 位，原日志保留；现使用测试子类观察方法及标准 Modifiers API，未放宽生产访问限制或改 golden。DataTable 消费迁移和正式 CTest 仍待收口。

泛型 delegate 字段赋值的 checker 根修已应用并重建编译器：保留经 receiver 特化的左值类型，正确 getter/setter 正例均编译及执行成功，错误参数与返回类型仍分别报 E2098。但两正例正常退出仍泄漏一个 target 业务对象，strict 尚未通过，不能视为编译器验收完成。最小化确认空 Binding、单独 delegate 赋值及 Get/Set 均不泄漏，单独显式 `binding.target = state` 即复现；赋值 retain 与 Binding.target 析构 skip 不对称，自动 Binding 的非持有 target ABI 必须保留，需独立统一显式存储契约。历史红证据在 `build/test-out/binding-setter-actual/20261112-165251/strict-result.json` 及同目录 leak-diagnosis 文件；未覆盖原探针或修改 golden。后续独立所有权根修已应用并完成 fresh strict：显式 target 存储与自动 Binding 共用非持有 ABI，不 retain 新借用、不 release 旧借用，owned 临时 RHS 报 E3105；该 object 槽不提供 tracked weak 自动置 null，owner 必须活过 Get/Set。原方法组探针、泛型 delegate conformance、新 borrowed 生命周期及既有自动 target 生命周期共四正例均 compile/run0、exact golden、空 stderr、无 leak/ARC；三处 E3105 与两处 E2098 错误签名均准确拒绝。证据为 `build/test-out/binding-target-ownership/20261112-175243/strict-result.json`，验证前后同一编译器 SHA256 `59dd34a8df4c641e68513c82838f398a6ba285dc44795e8207608f895cbc8e1f`。正式 CTest 尚待收口；自定义属性 Binding 的访问器 IR 缺陷仍是独立阻断。其 getter/setter 分派、泛型实例 cache、readonly/init 快照以及 base 直调和调试状态修复已应用，但尚未验证通过：首轮重建遇到运行时旧 SDK 缺少 WC_ERR_INVALID_CHARS，补兼容宏后重建成功；首个 conformance 又在 `9.ToString()` 命中独立的数字词法边界错误，原始语法失败及未运行项保留在 `build/test-out/property-binding-ir-1c/20261112T100256388Z/`。后续数字成员词法修复已通过14组 token 边界与严格成员正例，保留 trailing-dot float、指数、f/m 后缀和范围语法；两条缺失指数数字负例亦已精确拒绝。属性 IR 原严格矩阵随后完成9正例与2负例，全部通过：正例 compile/run0、exact golden、空 runtime stderr、无 leak/ARC，readonly E2006 与 init-only E2007 诊断位置和消息匹配。原自动属性故障探针未改源亦转绿：property=111/getter_delta=1、backing=23/setters=1。证据分别为 `build/test-out/property-binding-ir-1c/20261112T102606633Z/strict-result.json`、`build/test-out/property-binding-original-auto/20261112T102641161446Z/result.json` 和 `build/test-out/property-binding-revision3/final-receipt/receipt.json`；同一矩阵/原探针编译器 SHA256 为 `d0b893341b9228a1e706c5af2e58f81d169b5878e41612765675054f0bad4831`。strict receipt 的 remaining 字段是固定模板文字，本次11项全部 pass，无未运行项。此前数字正例属 f71b 编译器独立证据；SDK并发重链后经来源核对和共享闸增量构建确认，再使用 d0b8 完成负例与属性矩阵，未混写版本。编译器正式 CTest 已完成本轮18条定向闭包：初轮17条通过，owned target 负例已准确输出三处 E3105，但新注册误要求 IR 阶段不存在的错误总数汇总行；移除该附加要求、保留三处位置/诊断码正则后，唯一失败项单独复验通过。证据为 `build/test-out/targeted-20261112-215619.log` 与 `build/test-out/targeted-20261112-215748.log`，不改写初轮失败记录，不重跑已通过整组。PropertyGrid 原完整序列及 Designer null-source 调用方仍待各自验收。

InputRootAdapter 第一阶段已应用：Bind/Detach 以生命周期 epoch 防止重入回调后的旧尾部覆盖，SetRootControl 的 in-flight ticket 在 finally 中按身份退休。`adapter_lifecycle_base_test` 已 fresh strict 编译运行通过，覆盖旧 root 尾部、待安装 root、异常清退及跨 App；compile/run 证据分别为 `build/test-out/gui-mechanism-adapter-compile-20261112-172808-489801/` 与 `build/test-out/gui-mechanism-adapter-run-20261112-172948-540591/result.json`，运行结果 STRICT_GOLDEN_PASS、stderr 为0，原 golden 未改，使用 C62 编译器。夹具先前订阅了不支持的 BindEvent("Cancel")，现经 OnRouted 观察 PointerCancel；待安装场景补齐 fallback 输入不可达的前提，结构托管查询保持独立。组合输入与长按随后独立 fresh strict 通过，原 source/golden 未改：composition compile/run 日志尾分别为 `174209-917181`/`174317-588702`，longpress 为 `174331-576119`/`174956-519089`，统一前缀 `build/test-out/gui-mechanism-<case>-<phase>-20261112-`；两组使用编译器 `284a5b24ad9e5ae461896cad207721104835046f3799362035bc9c490f4b953d`。完整逐 case 身份见 `_scratch/ribbon-retained-prep/mechanism-workorder/stage1.actual.after.json`。这些是跨版本独立证据，不能合称同一编译器版本全绿；旧 C62 排队任务被 compiler guard 拒绝且未编译，拒绝记录保留。ControlInputHost/Ribbon、活动会话清退和真实 Designer 调用方尚未因此完成验收。

ControlInputHost/Ribbon 的组件与测试共15条路径已应用，真实 Designer/Gallery 调用方仍另行维护未应用的显式 Prepare 候选。Host 已独立严格编译运行通过；Ribbon 初次编译暴露 Math.Min/Max 的 double 结果用于整数布局边界，已补显式转换。Ribbon 编译通过后的首次运行又发现夹具仍假设 Up 返回后才手动 Drain；App 现于 EndLoopTurn 自动 DrainPosts，夹具已改为真实 Down、Post 变更、真实 Up 的同轮 FIFO 顺序，并保留原业务断言和 golden、增加变更确实执行的断言。随后 tabbed Ribbon 夹具改用 HeightTabbed 而非 HeightGroups，保留全部原断言和 golden。Host、Ribbon、ControlInputHost 组合现已分别通过严格编译运行，exact golden、空 runtime stderr、leak/ARC/strict-runtime/HARD 均通过；最终身份与共享输入核验见 `build/test-out/gui-component-split-r5-final-20261112-215323-763787/result.json`。Host 复用已通过且输入身份一致的原证据，其余两组在 r5 fresh 编译运行；本次未外推到真实调用方或实窗验收。原失败记录及整数转换、夹具修订保留在 `_scratch/host-ribbon-component-split/actual-r3/`、`actual-r4/`、`actual-r5/` 和 `build/test-out/`。

显式准备可把创建、模型同步、绑定与布局移出 Paint，但 App/native 输入前准备、子窗口最终布局及 Gallery 滚动卡片几何的自动接线仍未完成，因此显式 fixture 不代表真实调用方迁移通过。默认 Form/ChildWindow 的两阶段准备候选正在静态审查，需覆盖具体登记身份和代次、同 App 嵌套输入、绘制尾部失效检查，并保留公开 RenderFrame 手写循环与 CaptureSnapshot 的既有语义；截图不应通过更换输入根来取得布局资格。Zan 无修饰符默认 public，internal 按命名空间约束，不能把未写 public 的实现误称为包私有。Game 准备接线尚未验收。

活动会话清退的 Layer 候选尚未应用：静态复核确认相同 App/端点组的重复 SetLayerInput 无条件递增代次，会使取消回调中的普通重同步错误地跳过剩余旧端点清理。候选已补同组几何更新不续期、外部换根后恢复实际 adapter 托管，以及真实 PointerCancel 抛异常后的显式重试夹具，仍需组合检查和严格运行。原冻结候选和已有失败证据保留，不外推为生命周期验收。

Game 消费方复核仍有具体遗留：SceneDesigner 资源 ListView 在绘制时创建/Bind/RenderInside，未挂入明确输入 owner；其 Ribbon、上下文菜单、画布及调色板仍有绘制期读事件或消费滚轮。SceneView.HandleClicks 仍是绘制后的几何点击入口。match3、survivor、fps-3d、snake、pin-circle 模板及 gui_3d_demo 尚有应用侧直接绘制的 HUD/菜单，需要迁移为持久标准控件；世界实体、棋盘、3D 场景和引擎图元继续按世界渲染契约处理。上述是静态消费方清单，不代表已复现同一事件重复提交，也不因 GuiHost 基础机制通过而自动验收。

Gui 其余调用方迁移及实窗验收完成前维持 IDE 重构暂停。本文既有审计表中的“清零”等历史表述
不能作为本次重新核实的结论，按实际公开面、调用方及新回归逐项更新。

### 既有审计记录（保留原审计日期）

以下为既有审计记录：HTML 装载汇聚到 `Html.Parse`，绘制管线只有一条路径，
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
2. 补安全网：补注册尚未登记到 ctest 的 GUI 用例（如 `tests/gui/` 下文件名前缀为 gui_declarative_ 的用例、
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
   `Testing/`，基元移到包内根目录 `packages/Zan.Gui/src/Gui/`；同步按物理路径
   枚举源码的构建脚本（`scripts/build_gallery.ps1`、`scripts/build_gallery_single.ps1`、
   `scripts/build_charts.ps1`、`scripts/build_ide.ps1`）。
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
- 2026-11-12 SceneDesigner 检查器轮次：检查器持久属性树与区域 HUD 契约已落，
  五项独立 probe 严格 golden 比较通过，InputNumber lifecycle 11 项与
  step_bounds 10 项另通过 `--check-leaks --arc-guard`；编译器
  `arc_implicit_property_receiver` 亦通过严格 golden 与 leak/guard probe，原
  16 个临时集合 receiver 残留已消失。正式 12 个目标最终均通过；初始 10 个
  中 collapse 修正测试为 Post 排队后单条重跑通过；wizard 关闭按钮改用真实
  `IconView` 内容后单条重跑通过。两者保留原 golden 和有效断言，wizard 包含
  产品代码修复。新增正式
  `case_child_window_callback_lifecycle` 通过（57.85 秒，6 项断言及 leak/guard），
  `conformance_ide_wizard_interaction` 通过（91.07 秒，严格 golden 与 leak/guard），
  覆盖真实点击、输入、预设、创建/取消/关闭、根替换及回调重入释放，验证限于
  离屏交互。IDE 原 17 条错误与 Scaffold 两处问题已修；2026-11-12 完整 IDE
  编译链接通过（`IDE_NO_PUBLISH=1`，exit 0，非实机交互；LSP/Form 闭包警告仍保留），
  不记全迁移完成。上轮 9 条 ctest/5 个模板结果独立保留。详见 §2 本轮检查器记录。
