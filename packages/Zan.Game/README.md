# Zan.Game - Zan 官方游戏开发引擎与组件库

`Zan.Game` 是面向 2D 游戏、棋盘策略、卡牌对战、塔防模拟及 ARPG/RPG 开发的分层框架体系。所有模块均遵循**渲染与逻辑分离、确定性步进、快照回放**原则。

---

## 模块分层与架构

```text
Game.Arpg        (ARPG/RPG 专业运行时)
Game.Zgm         (ZanGameMaker 组件模型运行时)
  ▲
  │
Game.Board  ----+ (棋盘/战棋/网格/A*寻路/回放日志)
Game.Cards  ----+ (卡牌/牌库/手牌区/对战结算引擎)  ───► Game.Foundation (固定步长/确定性随机数/输入/场景)
Game.Arcade2D  -+ (2D碰撞/实体池/补间/几何流形)
```

---

## GuiHost 挂载游戏视口

在 `IGuiHostLoop.Start` 创建 `GameViewport.Create(scene)`，把它加入持久控件树，以 `host.SetContentRoot(root)` 显式安装该内容树，再调用 `host.AttachViewport(viewport)` 登记逻辑步进。视口由所在树的 Measure/Arrange/RenderTree 绘制，输入通过同一控件树的路由、焦点和指针捕获到达视口；仅绘制一棵树不会安装它的输入归属。不需要在项目里再转发 `OnKeyDown`、`OnMouseMove` 或调用 `StepLogic`。重复挂载同一个视口无效。

视口输入使用相对于控件左上角的设备像素坐标，`viewport.Input.GetWorldMouse(scene.Camera, ...)` 再反投影为世界坐标。指针命中视口后由共享控件路由处理；按键需先让视口获得焦点（点击或 `viewport.Focus(host.App())`）。开始于世界的拖动使用共享捕获，视口外抬起仍收到最终坐标，取消会清理旧状态。输入事件先更新视口状态，逻辑步读取；零逻辑步保留边沿，首个实际逻辑步后清除，因此追帧不会重复触发动作。

`host.Input()` 的 `Pressed/Released` 属于宿主帧：在 `Event`、该轮 `FixedUpdate` 或 `Update` 读取，下一轮事件泵前清除；即使该轮零固定步，`Update` 仍能读到。`host.Hud().Add(control)` 的根控件在标题栏下方的内容区布局。HUD 自动绑定 Visual Tree 输入聚合根，后添加的控件优先命中，空区继续查询宿主内容树；移除、清空或隐藏后立即退出命中，不收养或重复绘制内容节点。独立的 `HudLayer.RenderViewport` 则以调用者传入的零原点视口尺寸布局。

## SceneDesigner 检查器迁移状态

2026-11-12 当前实现（本轮五项 probe 严格 golden 通过，正式 CTest 的 12 个目标最终均通过；完整 IDE 编译链接通过，`IDE_NO_PUBLISH=1`，非实机交互）：每个 `SceneDesigner` 持有一个惰性创建的 `SceneInspectorView`，使用标准 `ScrollColumn`、`Panel`、`Label`、`Input`、`InputNumber`、`Button` 与 `ColorPicker` 组成持久属性树。渲染和选择切换更新值与可见性，保留控件实例、父子关系和事件订阅。输入 Change 与按钮 Click 提交模型，模型同步期间抑制提交；数值、文本、颜色及模式/锚点等修改先比较旧值，避免无变化的撤销记录。资源选择器调用 `SetText` 后显式提交图片或背景字段，因为 SetText 不触发 Change。

检查器通过 `HudLayer.RenderIn(app, x, y, width, height)` 在独立设计器或 IDE 属性 dock 给定区域内布局、绘制并接入输入；区域使用 App 坐标，同一控件树不重新挂接。关闭属性 dock 或离开场景页时调用 `SetInspectorVisible(false)`；底层 `HudLayer.SetVisible(false)` 摘除该层输入入口，并清理属于该层的焦点和指针捕获。

本轮五项独立 probe 输出均已与 golden 严格比较通过：`scene_designer_color`（颜色回归）、`scene_designer_inspector`（真实离屏输入、单次提交/无空 undo 与 Undo、anchor、RGB/独立 alpha、资源字段显式提交、scroll、hidden 释放焦点、根与属性行身份稳定）、`input_offscreen_sequence`（同一事件重复绘制仅消费一次、新字符、keydown/up）、`input_number_lifecycle`（11 项）与 `input_number_step_bounds`（10 项）。后两项同时启用 `--check-leaks --arc-guard` 通过；该泄漏检查结论仅覆盖这两个 InputNumber probe。核实来源为 `tests/gui/` 对应测试与 `tests/conformance/` 对应源码及 golden。隐藏释放 pointer capture 为实现契约，检查器 probe 直接断言 hidden 释放焦点，未直接验证 capture 释放或 IDE 实机交互。

编译器 `arc_implicit_property_receiver` 独立 probe 已通过严格 golden 比较与 `--check-leaks --arc-guard`，原 16 个残留已消失；bare getter owned/receiver AST 与临时集合属性接 `Contains`/`IndexOf`/`LastIndexOf` 的 own/drop 均已 probe 验证。trace 确认 Bundle 正常；无 `-g` 时两个分配点共用 site 编号曾使 line 75 被误归因，需用 trace 或 `-g` 确认来源。

本轮正式 CTest 的 12 个目标最终均通过：初始 10 个中 collapse 修正测试为 `header.On.Click.Post()` 后单条重跑通过，wizard 零宽根因修复后单条重跑通过。collapse 修正的是测试的异步排队方式；wizard 修复的是关闭按钮真实内容缺失（`singleClose = Button(IconView)`）。两者均保留 golden 与有效断言。新增正式 `case_child_window_callback_lifecycle` 通过（57.85 秒），6 项断言与 `--check-leaks --arc-guard` 均通过；`conformance_ide_wizard_interaction` 通过（91.07 秒），覆盖真实点击、输入、预设、创建/取消/关闭、根替换及回调重入释放，严格 golden 与 leak/guard 均通过，验证限于离屏交互。

IDE 原 17 条旧 API 错误已清除，Scaffold 两处问题已修；2026-11-12 完整 IDE 编译链接实际通过，exit 0，输出 `IDE_BUILD_OK build\ZanIDE.exe`（`IDE_NO_PUBLISH=1`；日志 `call_89c0e4b0d90b4693b5b9147e8c2947be-stdout.log`）。不含发布或实机交互验证，已知 LSP/Form 闭包警告仍保留。上述 12 个正式目标与完整 IDE 编译链接通过不代表全架构迁移完成。上轮 9 条正式 ctest 与 `game/match3`、`gui/gui-empty`、`gui/gui-html-responsive`、`gui/gui-hud-anchor`、`gui/gui-scene-router` 五个模板编译通过的记录保留，独立于本轮验证。

元素调色板、图层/资源/上下文菜单、画布及 IDE 更广范围的迁移仍属遗留路径，尚未完成。架构进度及两轮验证边界见 [GUI_ARCHITECTURE.md](../../docs/GUI_ARCHITECTURE.md)。

## 各子模块文档入口

- 🎮 **基础核心**：[`src/Game/Foundation/README.md`](src/Game/Foundation/README.md) - 定步循环、随机数种子与场景栈。
- ♟️ **棋盘与战棋**：[`Zan.Game.Board`](../Zan.Game.Board/src/Game/Board/README.md) - 方格/六边形网格、命令验证、快照恢复。
- 🃏 **卡牌与构筑**：[`Zan.Game.Cards`](../Zan.Game.Cards/src/Game/Cards/README.md) - 牌堆流转、洗牌、结算堆栈与战斗规则。
- 👾 **2D 街机与碰撞**：[`src/Game/Arcade2D/README.md`](src/Game/Arcade2D/README.md) - 实体对象池、AABB碰撞、弹道与射线检测。
- ⚔️ **动作角色扮演**：[`Zan.Game.Arpg`](../Zan.Game.Arpg/src/Game/Arpg/README.md) - 地图、技能冷却、Buff系统与实体调度。
- 📖 **全模块总览**：[`src/Game/README.md`](src/Game/README.md) - 完整架构设计白皮书。
