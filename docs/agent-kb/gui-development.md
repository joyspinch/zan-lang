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
// 内存图像：字节（含 WebP）或 SVG 文本注册到 key，之后 key 当路径用
int w = Canvas.ImageLoadMem("mem:logo", bytes, len);       // PNG/JPEG/BMP/GIF/TGA/PNM/PSD/WebP
int w = Canvas.ImageLoadSvg("mem:icon", svgText, boxW, boxH);  // SVG，contain 适配盒子
Canvas.EvictImage("mem:logo");                              // 丢弃（路径或 mem key 通用）
```

**`ImageWidth<=0` 就是"图不存在/读不了"**，用它做"有截图就画截图，没有就画占位"的判断。

### 图片组件 Gui.Widget.Image（传地址即渲染）

`new Image(src, alt, fit)` 直接吃四类地址，按前缀自动识别：本地文件路径
（PNG/JPEG/BMP/GIF/TGA/PNM/PSD/WebP，`.svg` 走矢量光栅）、`http(s)://` URL
（`Gui.ImageHttp` 后台线程取回 + `App.Post` 封送，不阻塞 UI）、
`data:image/...;base64,...` 数据 URI、内联/远程 SVG（首帧后按盒子尺寸重光栅）。
`Fit`：contain（默认）/cover/fill/none；`Loaded`/`Error` 事件报告结果；
`Reload()` 重新解析。加载中与失败画 `image` CSS 规则的底色 + `Alt` 文本占位。
注意：位图内容按矩形裁剪，CSS 圆角只作用于背景与边框（光栅器暂无圆角剪裁路径）。

### 水印组件 Gui.Widget.Watermark（平铺旋转文字/图像，天然点击穿透）

盖在页面内容上的透明平铺层（Naive UI n-watermark 同位）：`Content` 文字
（`\n` 多行）或 `Src` 图像地址（同 Image 的本地路径/data URI，按自然尺寸
平铺）反复重复铺满控件矩形。`Rotate` 角度默认 -22（度，正=顺时针，CSS
惯例，限 [-90,90]）；`Color` 默认 rgba(0,0,0,0.145)（0=吃 `watermark {}`
皮肤规则的 color）；`FontPx` 默认 14；`GapX/GapY` 瓦片间距（默认 100）、
`OffsetX/OffsetY` 整体偏移。纯绘制层：不注册命中 id，事件照常落到下层
控件。

```zan
Panel doc = Panel.Column().Gap(8);
doc.With(new Label { Text = "合同正文..." });
doc.With(new Watermark("INTERNAL USE ONLY").Grow());   // 默认 -22°
// 品牌样式：
Watermark wm = new Watermark("ACME");
wm.Color = 0x2E1890FF; wm.FontPx = 18; wm.Rotate = 0; wm.GapX = 44;
```

实现要点：组件按瓦片网格从可视区外一格 pitch 起循环，每瓦把"行盒中心
平移 + 运行时锚点旋转"复合成整瓦刚体旋转；文字经
`Canvas.DrawTextRot(x, y, text, color, size, angleDeg)` 绘制——锚点 (x,y)
是**未旋转行盒左上角**，旋转绕它进行。运行时新增导出
`zan_gui_draw_text_rot`（驱动导出 66→68，Windows 改了运行时记得先跑
`scripts/build_gui_driver.ps1` 重新 staging 再链 GUI 程序）：GDI 走
`GM_ADVANCED` + `SetWorldTransform`（变换下 GDI 强制灰度 AA，ClearType
不可用），FreeType 走 `FT_Set_Transform`，旋转结果按角度进 glyph atlas
（run 瓦片 key 带 0xFF 前缀 + 角度，不与普通文本 key 碰撞）；CPU/GL
合成器不变（瓦片内已烘焙旋转）。macOS 暂回落不旋转（CoreText 旋转蒙版
待补）。

### 动态标签 Gui.Widget.DynamicTags（Naive UI n-dynamic-tags）

一排可增删的 Tag + 尾部虚线「+ 新建标签」触发器；点击触发器原位变成输入框，
回车或点击别处提交（非空才追加），Esc 放弃（Naive 没有的桌面补充），标签尾部
x 移除。`Closable` 默认 true；`Max` 限制数量、达到后触发器置灰；`Size`
（tiny/small/medium/large）映射 tag 皮肤类；`AddText`/`Placeholder` 定文案。

```zan
DynamicTags tags = new DynamicTags();
tags.SetItems(new List<string>{ "调查中", "已发布" });
tags.Change += () => { Save(tags.Items()); };   // 增删时触发（SetItems 不触发）
tags.Render(app, 40, 80);
```

实现要点：组件自持 `List<string>`（无列表绑定），内联编辑器是成员 `Input` 保留
实例、原位渲染并 `focus.SetFocused` 接管键盘；回车/Esc 在它渲染前轮询读取
（SessionList 同款），失焦提交靠"上一帧焦点快照"。每标签一对命中 id 从
`WidgetId.Block(1024)` 段按下标分配（tagBase+i / tagBase+512+i），关 x 后注册、
命中测试取最后注册者。触发器走 `tag.add` 皮肤规则（虚线框）。设计器序列化经
`GetExtra/SetExtra("options")`（`|` 连接，Tabs.SetItemsText 同款）。

### 倒计时 Gui.Widget.Countdown（Naive UI n-countdown）

从 `Duration` 毫秒倒数到零触发 `Finish`（每轮一次）。`Active` 暂停/恢复（剩余值冻结、
恢复不跳变），`Format` 用令牌选位面：`D` 总天数、`HH`/`mm`/`ss`（一律双写）、`S` 十分
之一秒、`SS` 百分之一秒（补零 2 位）；其余字符原样输出，`"T-minus"` 这类字面量不会被吃掉，`"sss"` 渲染成 `"05s"`。
`Restart()` 重摆到当前 `Duration`；外部改 `Duration`（含双向绑定）自动重摆。
走动时限速重绘：秒级格式睡到秒边界、`S` 100ms、`SS` 16ms（60fps 显示帧率封顶，显示值绘制时由 tick 差值现算，不按 10ms 排程——100 唤醒/秒会让事件循环永远睡不成）。另外 Win32 后端在 Init 里 `timeBeginPeriod(1)`：系统默认时钟中断约 15.6ms，rAF-in/唤醒的超时会拖到下一拍，动画帧间隔 16↔31ms 交替正是"背景动画一卡一卡"的根因；SDL 后端 SDL 自理，macOS/Linux 时钟本就高精度。

```zan
Countdown cd = new Countdown(5 * 60000, "mm:ss");
cd.Finish += () => { status.Text = "Done"; };
cd.RenderInside(app, new Rect(40, 60, 140, 34));
```

实现要点：剩余时刻由 tick 差值推得（`Tick(nowMs)` 以 `Window.GetTickMs()` 推进，测试
可注入确定时刻），掉帧/失焦不走慢；走动期间经 `App.RequestAnimationFrameIn` 限速重绘
——秒级格式睡到下一个秒边界（`remainingMs % 1000 + 1`），含 `S` 才以 100ms 节奏刷新，
且声明损伤矩形只重绘自己。格式化是纯函数 `Countdown.FormatText(fmt, ms)`，无窗口
回归在 `tests/gui/countdown_test.zan`。皮肤走 `countdown::value`（字号三档小/中/大
对应 `.small`/默认/`.large`，状态色 `.success`/`.warning`/`.error`）。

## 控件与组件在哪

| 想要 | 去哪 |
| --- | --- |
| 基础控件（58 个） | `stdlib/Gui/Widget/`：Button Input SelectBox Image Table Tabs TreeView ListView VirtualList Slider Switch Steps Timeline Pagination Progress Rate Tag Card Panel Collapse Popover Tooltip Dropdown Menu ContextMenu Breadcrumb PageHeader Result Empty Skeleton Spin Statistic … |
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
- 生成器在 `stdlib/System/Compiler/GenForm.zan`（编译期）与 IDE 侧的生成代码路径。
- 因此：**改窗口尺寸/控件位置就是改 `FormDoc` 再写回 `.zform`**，不要去改生成的 `.g.zan`。

## 验证 GUI 改动

1. `scripts\build_gallery.ps1` 必须过。
2. `scripts\build_ide.ps1` 必须过（IDE 用了大量控件，是第二道回归网）。
3. 真实窗口看一眼；能用 `scripts\shot_*.ps1` 截图就截图存档。
4. 涉及交互（点击/拖拽/键盘）的，用 `scripts\drive_ide.ps1` / `designer_drag.ps1`
   这类驱动脚本做可重复流程，而不是手点一次就算完。
