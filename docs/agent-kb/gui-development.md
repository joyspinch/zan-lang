# GUI / 主题 / 设计器 / HMI 开发速查

## 核心心智模型：两大标准创作范式

Zan.Gui 遵循两大标准创作范式：

1. **范式一：HTML + CSS**：`Gui.Markup/Html` 将标记解析为现代盒模型，外观完全由 CSS 级联与 Theme token 决定，排版纯走 CSS（Flex/Grid/Flow）引擎；数据通过 `data-bind` 双向同步。
2. **范式二：保留模式声明式对象树**：代码侧使用 `Children = [ ... ]`、`ClassName = "..."` 声明建树，排版纯走 Flex/Grid/Flow；动态宿主（设计器、PropertyGrid）走 `GetProp/SetProp` 属性协议；数据通过控件的 `Binding<T> data` 字段双向绑定。

**核心铁律**：
- **禁止外部自绘**：全仓所有 UI 元素（包括图表曲线、仪表指针、代码高亮、游戏 HUD 等）一律封装为标准范式二控件。像素绘制严格封存在控件内部 `OnPaint` 中，对外仅暴露声明式属性配置与数据绑定。
- **长生命周期控件树**：`Control` 具备完整的树状对象图（`parent` 弱引用、`children` 列表、`MeasureTree`、`Arrange` 绝对坐标边界 `bx, by, bw, bh`、`RenderTree` 裁切栈）。
- **统一路由事件分发**：全面走 **Visual Tree 路由分发**（`Tunneling` 隧穿向下 + `Bubbling` 冒泡向上，`RoutedEventArgs` 携带 `originalSource`、`source` 与 `handled` 消费标记，支持指针捕获 `App.CapturePointer`）。
- **控件纯实例化**：控件为纯实例对象，按对象引用识别，状态仅保存在控件实例字段与树可达宿主中。无全局 ID 依赖，无全局静态可变状态，天然支持同屏多实例、多弹层与多窗口完全隔离。

典型应用程序循环（`Gui/App.zan` / `Form.zan`）：

```zan
Form form = new MainForm();
form.Run(); // 自动挂载 rootControl 到 App 并开启事件驱动渲染循环
```

由此推出几条必须遵守的标准架构约定：

1. **事件与输入处理走 Visual Tree 路由分发**：
   - 控件重写 `OnRoutedEvent(RoutedEventArgs e)` 或 `OnPreviewRoutedEvent(RoutedEventArgs e)` 处理事件；
   - 消费事件时显式调用 `e.Handle()`，冒泡派发自动终止；
   - 拖拽手势（分割条、滑块、窗口移动）在按下时调用 `app.CapturePointer(this)`，释放时调用 `app.ReleasePointerCapture()`，后续位移与抬键直接定向派发。

2. **控件纯实例化与状态隔离**：
   - 控件是纯实例对象，状态只存在于控件实例字段与树可达宿主上；
   - 控件按对象引用与 Visual Tree 路径识别，禁止跨实例全局静态可变状态；
   - 天然支持多实例、同屏多弹层与多窗口完全隔离。

3. **数据绑定只有两条正道**：HTML 窗口用 `data-bind`（ModelBinder 自动双向同步，
   `<template data-for>` 展开列表行）；代码侧用控件的 `Binding<T> data` 字段
   （如 `sel.data = form.color;`，`docs/STDLIB_COMPONENT_STANDARDS.md` §6）。设计器、
   PropertyGrid 这类动态宿主走 `GetProp/SetProp` 属性协议。`Signal*` 只作控件内部
   状态缓冲，Zan.Gui 以外不得新建（机检拦截）。
4. **UI 线程专职渲染与交互，一切耗时业务/IO/计算完全异步解耦非阻塞（开发基本铁律）**：
   - **职责边界**：UI 线程（主线程事件循环）有且仅有两项职责：分发操作系统输入事件（按键/鼠标/焦点/窗口尺寸）与推进界面排版绘制（Layout/Paint/Present）。任何单次超过 1~2ms 的操作严禁在 UI 线程执行。
   - **严禁同步阻塞反模式**：严禁在 UI 线程直接执行网络请求（HTTP/WebSocket）、磁盘文件大读写、数据库存取、大 JSON 解析、图像编解码、压缩解压、重 CPU 矩阵/分词计算或同步 `Sleep`。同步阻塞是造成界面掉帧、卡顿、甚至触发系统“未响应”白屏的万恶之源。
   - **标准异步解耦定式（Worker + App.Post）**：
     ```zan
     // 1. UI 立即给用户反馈（置灰按钮/转圈/骨架屏），绝对不要阻塞
     btn.Disabled = true;
     status.Text = "加载中...";
     app.RequestRedraw();

     // 2. 耗时重活彻底交由后台异步执行（Worker/Thread/Task.Run）
     Thread.Start(() => {
         // 纯业务或 IO 计算，严禁触碰 UI 控件状态
         var data = FetchRemoteDataOrCompute();

         // 3. 计算完成，通过 App.Post 封送回 UI 线程安全更新
         app.Post(() => {
             btn.Disabled = false;
             status.Text = "完成";
             view.UpdateData(data);
             // Post 会自动通过 window.Wake 唤醒阻塞的消息泵，并在 DrainPosts 后置位 needsRedraw
         });
     });
     ```
   - **生命周期与取消协同**：后台任务必须支持协作取消（`CancellationToken` 或 atomic 标志），在窗口关闭或前序操作作废时停止后续回传，杜绝后台游离任务向已销毁控件回调导致的空引用或野指针异常。
5. 尺寸一律走 `app.Scale(n)` 做 DPI 缩放，不要写死像素。

## Theme token（`packages/Zan.Gui/src/Gui/Styling/Theme.zan`）

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
（CSS 式样式层，IDE 自己的布局在 `src/ide_zan/assets/ide.css`）、`skins/`（资源）。

**图形游戏使用 Gui 的现行开发规范**：玩家可见的按钮、面板、HUD、菜单、提示与
弹层必须图形化并匹配游戏场景美术。布局和交互复用标准组件，外观通过游戏专属
皮肤、语义类与 UI 素材统一配色、字体、图标、材质和动效；适用的悬停/按下/选中/
禁用/焦点状态也要同风格。用户指出默认桌面外观与图形游戏混搭会降低品质，因此
验收须把 Gui 和场景放在同一张截图中逐页复核，默认外观只能作开发占位。
详细规则见 [游戏开发规范](../../.agents/skills/game-dev/SKILL.md)
“图形游戏的 Gui 组件必须匹配游戏美术风格”及
[游戏引擎指南](../GAME_ENGINE_GUIDE.md)。

## 绘制原语（`packages/Zan.Gui/src/Gui/Rendering/Canvas.zan`）

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

**宿主必须自己有高度**：水印是覆盖层，自身测量 0×0、不参与排版——宿主
容器若靠内容撑高（`Gallery.LayoutAuto`、流式面板），`Grow()`/`DockFill()`
的水印会分到 0 高、什么都不画。给宿主 `Prefer(0, 高度)`（stack 面板不吃
CSS height，`Prefer` 才设置 `prefSet`）。另外 `Panel.Column` 的样式类型是
`stack` 不是 `panel`，给演示容器写皮肤要写 `stack.xxx`。

```zan
Panel doc = Panel.Column().Gap(8);
doc.Prefer(0, 170);                                    // 宿主定高，水印才有剩余空间
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
// 范式二挂载：加入父级容器 Children 声明式树
container.Children.Add(tags);
```

实现要点：组件自持 `List<string>`（无列表绑定），内联编辑器是成员 `Input` 保留
实例、原位渲染并 `focus.SetFocused` 接管键盘；回车/Esc 在它渲染前轮询读取
（SessionList 同款），失焦提交靠"上一帧焦点快照"。标签与删除按钮通过 Visual Tree
路由事件处理交互与焦点，无全局 ID 栈依赖。触发器走 `tag.add` 皮肤规则（虚线框）。设计器序列化经
`GetExtra/SetExtra("options")`（`|` 连接，Tabs.SetItemsText 同款）。

### 倒计时 Gui.Widget.Countdown（Naive UI n-countdown）

从 `Duration` 毫秒倒数到零触发 `Finish`（每轮一次）。`Active` 暂停/恢复（剩余值冻结、
恢复不跳变），`Format` 用令牌选位面：`D` 总天数、`HH`/`mm`/`ss`（一律双写）、`S` 十分
之一秒、`SS` 百分之一秒（补零 2 位）；其余字符原样输出，`"T-minus"` 这类字面量不会被吃掉，`"sss"` 渲染成 `"05s"`。
`Restart()` 重摆到当前 `Duration`；外部改 `Duration`（含双向绑定）自动重摆。
走动时限速重绘：秒级格式睡到秒边界、`S` 100ms、`SS` 16ms（60fps 显示帧率封顶，显示值绘制时由 tick 差值现算，不按 10ms 排程——100 唤醒/秒会让事件循环永远睡不成）。另外 Win32 后端在 Init 里 `timeBeginPeriod(1)`：系统默认时钟中断约 15.6ms，rAF-in/唤醒的超时会拖到下一拍，动画帧间隔 16↔31ms 交替正是"背景动画一卡一卡"的根因；macOS/Linux 时钟本就高精度。

```zan
Countdown cd = new Countdown(5 * 60000, "mm:ss");
cd.Finish += () => { status.Text = "Done"; };
// 范式二挂载：声明式加入容器
panel.Children.Add(cd);
```

实现要点：剩余时刻由 tick 差值推得（`Tick(nowMs)` 以 `Window.GetTickMs()` 推进，测试
可注入确定时刻），掉帧/失焦不走慢；走动期间经 `App.RequestAnimationFrameIn` 限速重绘
——秒级格式睡到下一个秒边界（`remainingMs % 1000 + 1`），含 `S` 才以 100ms 节奏刷新，
且声明损伤矩形只重绘自己。格式化是纯函数 `Countdown.FormatText(fmt, ms)`，无窗口
回归在 `tests/gui/countdown_test.zan`。皮肤走 `countdown::value`（字号三档小/中/大
对应 `.small`/默认/`.large`，状态色 `.success`/`.warning`/`.error`）。

### 数值动画 Gui.Widget.NumberAnimation（countup.js 风格数值滚动）

在 `Duration` 毫秒内把显示值从 `From` 缓动滚到 `To`，到位触发一次 `Finish`
（每轮恰一次，`Restart()` 或改 `From/To/Duration` 重摆后可再触发）。数值是
**scaled 定点整数**（显示值 × 10^`Precision`，口径同 InputNumber）：
`Precision` 0..6 补小数位、`Separator` 千分位分组（`""` 关闭）、`Decimal`
小数点串（俄语 `Separator=" " Decimal=","` → `699 700,699`）、`Prefix`/`Suffix`
贴单位；展示符变化不重摆，只有 `From/To/Duration` 变化才重摆。`Active`
暂停/恢复：落沿在 `SyncProps` 结算/重锚时间轴，恢复第一拍零增量（无跳变）。
`SetRange(double from, double to)` 按当前 Precision 舍入，宿主免手算缩放。
每帧显示值 = `ValueAt(From, To, App.Ease(p))` 现算（不逐帧累加），掉帧/失焦
不走样；走动期间 `RequestAnimationFrameIn(16, …)` 按显示帧率封顶只刷自己条带。

```zan
NumberAnimation na = new NumberAnimation(0, 12039, 2000);
na.Finish += () => { status.Text = "Done"; };
playBtn.Click += () => na.Restart();

NumberAnimation ru = new NumberAnimation(0, 699700699, 2000);
ru.Precision = 3; ru.Separator = " "; ru.Decimal = ",";  // 699 700,699
```

实现要点：绘制形态与 Countdown 同构（`numberanim::box` 卡片 + `::track`/`::fill`
完成度进度条 + `::value` 居中文本，太小退化裸文本），暂停落 `:disabled` 档
（到位不降调）；`OnMeasure` 宽度取 From/To **端点文本较大者**——动画中值单调
介于两端、字符串长度不超包络，一次测量全程不重排。缓动/格式化是纯函数
`NumberAnimation.ValueAt/Format/GroupWith/ScaleIt`，无窗口回归在
`tests/gui/numberanimation_test.zan`（conformance_gui_numberanimation，自动进
smoke/standard 档）。皮肤命名空间 `numberanim`（base.css 三处：size 档、
`::value` + 角色类、box/track/fill + disabled）。

## 控件与组件在哪

| 想要 | 去哪 |
| --- | --- |
| 基础控件（58 个） | `packages/Zan.Gui/src/Gui/Widget/`：Button Input SelectBox Image Table Tabs TreeView ListView VirtualList Slider Switch Steps Timeline Pagination Progress Rate Tag Card Panel Collapse Popover Tooltip Dropdown Menu ContextMenu Breadcrumb PageHeader Result Empty Skeleton Spin Statistic … |
| 外壳容器 | `Widget/ToolStrip.zan` `StatusBar.zan` `SplitPanel.zan` `Split.zan` `Ribbon.zan`；自定义标题栏见 `Core/App.Chrome.zan` |
| 向导/对话框 | `Widget/Wizard.zan`（新建项目/新建文件都用它）、`Widget/Prompt.zan`、`Widget/Layer.zan`（通知/浮层） |
| 基础复合件 | `packages/Zan.Gui/src/Gui/Component/`：`FilePicker` `GraphView` `BandGrid` 等 |
| 图表控件包 | `packages/Zan.Gui.Charts/`（`Gui.Charts`：折线、柱状、饼图、仪表、散点、热力等范式二图表组件） |
| 高性能表格包 | `packages/Zan.Gui.DataTable/`（`Gui.DataTable`：千万级虚拟化报表组件） |
| 代码编辑器包 | `packages/Zan.Gui.CodeEditor/`（`Gui.CodeEditor`：语法高亮与代码编辑组件） |
| 浏览器控件包 | `packages/Zan.Gui.Browser/`（`WebView` / `CefBrowser`） |
| 工控 HMI 包 | `packages/Zan.Industrial/`（`Gui.Hmi`：`IoTag` 位号、`Indicator` LED、`Gauge` 仪表、`Trend` 趋势曲线、`Alarm` 报警） |
| 可视化设计器 | `packages/Zan.Gui.Designer/` |

新增控件的落地清单：`Widget/<Name>.zan` → 在 gallery 里加演示 → 需要能被设计器摆放的话
补 `PropSpec`/`ControlFactory` 注册 → `standard` + gallery 构建 + 截图。

## 自定义组件按 kind 解析：HeavyControls 运行期注册，不要生成注册表文件

宿主自有组件要在声明式页面（`.html`，运行期经 `ControlFactory` 按 kind 字符串建树）
和设计器面板里可用，做法是**启动时显式注册**进 `HeavyControls`
（`HeavyControls.Register(kind, 工厂委托)`，幂等的 `XxxBootstrap.Install()`，
IDE 见 `src/ide_zan/src/components/IdeBootstrap.zan`，画廊在 `gui_gallery.zan`
的 Main 里注册 StatusPill）。

不要回到"构建期扫描源码生成注册表文件"的老路（已删的 `scan_components.ps1`
+ `ProjectComponents.*.zan`）：

- 那张生成表把 `new DocsSectionTabs()` 这样的**活标识符**写进一个游离文件，
  只能与宿主全量源码同编译；一旦被无关编译带上就是一串 undefined type。
- 它落在 `build/` 里，而 IDE 的命名空间本地解析（工具面板双击运行时
  `FindNamespaceFile` 沿目录上行找 `using Gui;` 的提供者）会物理落进 build
  目录把它捞走——2026-09-26 工具面板运行报 8 个 undefined type 的根源。

同一机制的连带坑：**任何按级上行的目录遍历，构建输出目录必须在每一级落点
排除**，`PathFilter.Skip("build dist bin obj publish")` 只挡"进入哪些子目录"，
挡不住字符串 `DirOf` 上行落回的目录本身（`build\ZanIDE.exe` 跑出的 ToolsDir
带 `..`，上行会经过物理的 build/）。修复见 `ZanIDE.FindNamespaceFile`。

## HMI 与周期刷新

工控画面要"自己动"：`Form.Every(...)` 注册周期回调（位号轮询、趋势推点、报警刷新）。
写 HMI 画面时注意：

- 位号读写统一走 `IoTag`，不要在控件里直接摸驱动。
- 周期回调里只更新数据 + `RequestRedraw()`，绘制仍在帧里做。
- 报警/趋势的数据量要有上限（环形缓冲），否则长时间运行内存持续增长。

## 铁律：控件自己负责内部绘制，使用处只做声明与绑定（取消画布例外）

**全仓严禁在使用处自绘控件元素，彻底取消画布例外。**
2026-11-09 用户裁定：所有 UI 界面（包括图表、HMI、代码高亮、游戏 HUD）一律收编为**范式二控件**。
- 控件骨架、外壳与排版走 `Children`/`ClassName` 与标准 CSS（Flex/Grid/Flow）；
- 曲线、指针、仪表圆弧、LED 灯珠等像素绘制**完全封装在控件自己的 `OnPaint` 内部**；
- 外部使用处只能：**挂载组件 → 配置属性 → 绑定数据**；绝不允许向外暴露即时模式 `Render(app, …)` 入口或任何应用侧 `Draw*` 调用。

为什么不是风格问题：同一个控件存在两份绘制，组件修好之后示例还在按旧的错的画法
显示，于是「组件是对的，demo 是错的」——gallery 的仪表演示就是这么歪的。

- 守门测试 `policy_no_widget_drawing`（smoke 层）扫 `examples/` 与 `templates/`，
  出现 `FillSector/DrawSector/FillArc/DrawArc/FillPie/DrawPie` 直接失败并打印行号。
- 游戏世界渲染（GameViewport / Entity.OnRender）是引擎内部职责，但游戏 HUD、面板、菜单必须使用标准范式二控件。
- 示例与应用代码严禁自绘现有控件。仪表画错去 `packages/Zan.Gui.Charts` 或 `packages/Zan.Industrial` 改控件自己的 OnPaint，绝不在调用处修补。

## 设计器窗体（`.html` 设计稿）的数据流

```text
.html 设计稿 (JSON 文档模型)  --IDE/编译器 GenForm--> <Name>.g.zan --+
                                                               |--> 与手写 App.zan 一起编译
手写逻辑 <Name>.zan / App.zan --------------------------------+
```

- 文档模型在 `src/ide_zan/src/editor/ZanIDE.CodeNav.zan`：`FormDoc { name, winW, winH, winTitle,
  winCenter, layoutMode, winZoom, fields }`、`FormFieldDoc { type, label, name,
  required, wrap, span, fx, fy, fw, fh }`（`fx/fy/fw/fh` 是自由布局的绝对坐标）。
- 生成器在 `stdlib/System/Compiler/GenForm.zan`（编译期）与 IDE 侧的生成代码路径。
- 因此：**改窗口尺寸/控件位置就是改设计文档再写回 `.html` 设计稿**，不要去改生成的 `.g.zan`。

## 验证 GUI 改动

1. `scripts\build_gallery.ps1` 必须过。
2. `scripts\build_ide.ps1` 必须过（IDE 用了大量控件，是第二道回归网）。
3. 真实窗口看一眼；能用 `scripts\shot_*.ps1` 截图就截图存档。
4. 涉及交互（点击/拖拽/键盘）的，用 `scripts\drive_ide.ps1` / `designer_drag.ps1`
   这类驱动脚本做可重复流程，而不是手点一次就算完。
