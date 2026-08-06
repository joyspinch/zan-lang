# GUI / 主题 / 设计器 / HMI 开发速查

## 心智模型：立即模式

`stdlib/Gui` 是**立即模式**（immediate mode）UI：每帧重新绘制并即时判定交互，
控件没有长生命周期对象树。典型循环（`Gui/App.zan`）：

```zan
App app = App.CreateDark("Title", 900, 640);
app.Show();
while (app.isRunning) {
    if (!app.ProcessEvent()) { break; }
    if (!app.needsRedraw) { continue; }
    app.BeginFrame();
    // 这里画内容：控件的静态 Render/Show 方法
    app.RenderChrome("Title");
    app.PresentFrame();
}
```

由此推出几条**必须遵守**的约定：

1. **控件 id 必须每帧稳定**。`WidgetId` 是进程级递增计数器；命中判定用的是上一帧算出的
   目标。如果某个窗口的绘制顺序/数量在帧间变化，按钮会"点不动"。
   子窗口的做法见 `ZanIDE.Shell.zan`：进入子窗口帧前 `WidgetId.SetSeq(850000)` 钉一个
   固定基线，画完 `WidgetId.SetSeq(savedSeq)` 还原。**新增子窗口时照抄这个模式。**
2. **状态放在调用方**，用 `SignalInt` / `Input` 等承载（`Gui/Reactive.zan`、
   `Widget/Input.zan`），控件本身不存状态。
3. **不要在绘制中做阻塞 IO**。长任务丢到后台并用面板/通知反馈（IDE 的构建/发布就是
   这么做的）。
4. 尺寸一律走 `app.Scale(n)` 做 DPI 缩放，不要写死像素。

## Theme token（`stdlib/Gui/Theme.zan`）

颜色是 `0xAARRGGBB` 打包进 `int`。**只能用已声明的 token**——写错名字以前会被静默折成 0，
现在会编译报错（见 debugging-playbook）。常用集合：

- 语义色：`primary/info/success/warning/error` + 各自 `*Hover` / `*Pressed`
- 文本：`textPrimary` `textSecondary` `textTertiary` `textDisabled` `textInverse`
- 背景：`bgPrimary` `bgSecondary` `bgTertiary` `bgHover` `bgActive` `bgDisabled`
- 边框：`borderPrimary` `borderSecondary` `borderHover` `borderFocus` `divider` `overlay`
- 皮肤开关：`glass` `gradient`(`bgGradTop`/`bgGradBottom`) `neu` `brutal` `fx`
- 浮层：`tooltipBg` `tooltipBorder` `tooltipText` `tooltipTextMuted` `scrim` `scrimChip`
- 滚动条：`scrollbar` `scrollbarHover`

**没有**的东西（常见误写）：`surface`、`windowW`/`windowH`（窗口尺寸从
`app.canvas.Width()/Height()` 取）。改主题相关代码前 grep 一下 `Theme.zan` 里的字段名。

皮肤与样式：`Skin.zan`（皮肤索引/切换）、`Style*.zan` / `StyleSheet.zan` / `Css.zan`
（CSS 式样式层，IDE 自己的布局在 `src/ide_zan/ide.css`）、`skins/`（资源）。

## 绘制原语（`stdlib/Gui/Render.zan`）

`Canvas` 上常用：`FillRect` `DrawRect` `FillRoundRect` `DrawCircle` `DrawLine`
`DrawPolyline` `DrawText` `PushClip`/`PopClip` `BlurRect`。

图片：

```zan
int w = Canvas.ImageWidth(path);      // 静态，读尺寸；失败返回 <=0
int h = Canvas.ImageHeight(path);
canvas.BlitImage(path, dx, dy, dw, dh, sx, sy, sw, sh);   // 缩放绘制
canvas.DrawImage(path, x, y);                              // 原尺寸
```

**`ImageWidth<=0` 就是"图不存在/读不了"**，用它做"有截图就画截图，没有就画占位"的判断。

## 控件与组件在哪

| 想要 | 去哪 |
| --- | --- |
| 基础控件（58 个） | `stdlib/Gui/Widget/`：Button Input SelectBox Table Tabs TreeView ListView VirtualList Slider Switch Steps Timeline Pagination Progress Rate Tag Card Panel Collapse Popover Tooltip Dropdown Menu ContextMenu Breadcrumb PageHeader Result Empty Skeleton Spin Statistic … |
| 外壳容器 | `Widget/ToolStrip.zan` `StatusBar.zan` `SplitPanel.zan` `Split.zan` `Ribbon.zan` `Component/Dock.zan`（DockPanel）；自定义标题栏见 `App.RenderChrome` |
| 向导/对话框 | `Widget/Wizard.zan`（新建项目/新建文件都用它）、`Widget/Prompt.zan`、`Widget/Layer.zan`（通知/浮层） |
| 复合组件 | `Component/`：`Chart` `DataTable` `CodeEditor` `WebView` `PivotTable` `LogView` `FilePicker` `SessionList` |
| 工控 HMI | `Gui/Hmi/`：`IoTag`（位号绑定）`Indicator`（LED/数显）`Gauge` `Trend`（趋势曲线）`Alarm`（报警横幅+列表）`NumPad`（工业键盘）`EquipPanel`（设备卡/状态面板） |
| 设计器 | `Gui/Designer/Designer.zan` / `Designer.Form.zan` / `Designer.Inspector.zan` |

新增控件的落地清单：`Widget/<Name>.zan` → 在 gallery 里加演示 → 需要能被设计器摆放的话
补 `PropSpec`/`ControlFactory` 注册 → `standard` + gallery 构建 + 截图。

## HMI 与周期刷新

工控画面要"自己动"：`Form.Every(...)` 注册周期回调（位号轮询、趋势推点、报警刷新）。
写 HMI 画面时注意：

- 位号读写统一走 `IoTag`，不要在控件里直接摸驱动。
- 周期回调里只更新数据 + `RequestRedraw()`，绘制仍在帧里做。
- 报警/趋势的数据量要有上限（环形缓冲），否则长时间运行内存持续增长。

## 铁律：控件自己负责绘制，使用处只配置

**禁止在使用处自绘控件元素。** 示例、gallery、模板只能：实例化组件 → 配置属性 →
喂数据 → 摆位置。仪表的圆弧、趋势的折线、柱状的条、LED 的灯珠，一律画在
`stdlib/Gui/Hmi/*` 或 `stdlib/Gui/Component/Chart/*` 里面。

为什么不是风格问题：同一个控件存在两份绘制，组件修好之后示例还在按旧的错的画法
显示，于是「组件是对的，demo 是错的」——gallery 的仪表演示就是这么歪的。

- 守门测试 `policy_no_widget_drawing`（smoke 层）扫 `examples/` 与 `templates/`，
  出现 `FillSector/DrawSector/FillArc/DrawArc/FillPie/DrawPie` 直接失败并打印行号。
  这些原语除了表盘/圆环/饼图没别的用处，所以是很准的信号。
- 游戏例外（`examples/game/`）：游戏本来就自己渲染整个场景，不是在冒充控件。
- 示例画自己的外壳（面板、标题、表格行这些 `FillRect`/`DrawText`）是允许的——
  界限是「有没有在重画一个已经存在的控件」。
- 审过一遍的结论（2026-08）：`examples/`、`templates/` 里没有 HMI/图表自绘，
  gallery 的仪表演示全是 `ChartSeries.Gauge(...)` 配置。所以仪表画错要去
  `stdlib/Gui/Component/Chart/ChartViewPie.zan` 的 `DrawGauge` 查，别改 demo。

## 设计器窗体（`.zform`）的数据流

```text
.zform (JSON, FormDoc)  --IDE/编译器 formgen--> <Name>.g.zan --+
                                                               |--> 与手写 App.zan 一起编译
手写逻辑 <Name>.zan / App.zan --------------------------------+
```

- 文档模型在 `src/ide_zan/ZanIDE.CodeNav.zan`：`FormDoc { name, winW, winH, winTitle,
  winCenter, layoutMode, winZoom, fields }`、`FormFieldDoc { type, label, name,
  required, wrap, span, fx, fy, fw, fh }`（`fx/fy/fw/fh` 是自由布局的绝对坐标）。
- 生成器在 `src/compiler/formgen.c`（编译期）与 IDE 侧的生成代码路径。
- 因此：**改窗口尺寸/控件位置就是改 `FormDoc` 再写回 `.zform`**，不要去改生成的 `.g.zan`。

## 验证 GUI 改动

1. `scripts\build_gallery.ps1` 必须过。
2. `scripts\build_ide.ps1` 必须过（IDE 用了大量控件，是第二道回归网）。
3. 真实窗口看一眼；能用 `scripts\shot_*.ps1` 截图就截图存档。
4. 涉及交互（点击/拖拽/键盘）的，用 `scripts\drive_ide.ps1` / `designer_drag.ps1`
   这类驱动脚本做可重复流程，而不是手点一次就算完。
