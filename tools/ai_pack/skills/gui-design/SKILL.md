---
name: gui-design
description: Zan GUI (stdlib/Gui) 审美与排版规范——对齐、间距、尺寸档位统一、层级与克制，常用风格配方，截图还原流程，以及排版原语纪律。用 stdlib/Gui 写界面、做皮肤换风格、用户提到 好看/美观/对齐/间距/风格/审美 时使用；界面出现 控件重叠/排版混乱/尺寸忽大忽小/毛边锯齿、画图表斜线曲线、HTML+CSS 写窗口 时也用它。仓库内有同名项目级版本，会自动优先。
---

# Zan GUI 界面审美规范

**精美 = 一致 + 克制 + 有方向。** 一致靠档位:stdlib/Gui 内置了字号、高度、
间距三套全库统一的阶梯 token,永远从档位里取值,界面就自动"齐";克制靠取舍:
一个画面只有一个视觉重心;方向靠配方:选定风格方向后,一切从那张配方卡推导。

- 主流风格九张配方卡:`references/style-directions.md`(用户点名风格、
  或要求"好看一点"而现有皮肤不对味时,写码前先选卡)
- 页面搭配模式与反模式:`references/composition.md`(写页面布局前先读)
- 一张截图严格还原布局:`references/screenshot-restore.md`(用户给截图要
  求"照这个做/还原/复刻这个界面"时先读——测量转写→布局账本→原语映射
  决策树→同尺寸对拍验证;内含交互体验基线清单:反馈闭环/防呆/键盘/
  状态完整,还原或新写收尾都过一遍)
- CSS 方言/引擎解析规则全集(选择器/伪元素/单位/已知坑):
  `references/css-dialect.md`(改皮肤或写 CSS 前先查)
- 复杂窗口的迭代过程纪律(先问框架要、加高标题栏三处同步、动效帧调度、
  弹窗层序、截图驱动的小步收口):`references/layout-iteration.md`
  (标题栏+导航+内容+弹窗+动效的窗口,动手前先读——每条都是真实返工换来的)
- 把老程序迁移/复刻到 Zan、或参照现有产品做同族工具:先读 `app-migration`
  skill(复刻不创造、映射账本、行为/体验保真、验证闭环)——本文件管"好看",
  还原度纪律在那里。
- 游戏:实时/帧循环类(动作、手感、HUD 合成、失焦、移动端触屏)先读
  `game-dev` skill;文字/棋类/回合制/放置等控件驱动的游戏照常用本文件的
  排版规范即可。游戏内面板间距都走 4 的倍数档位。同屏混排(自绘 HUD +
  Gui 面板)时,缩放路径的边界按下文"缩放纪律"划分,字号必须同源。
- 立即模式心智模型/控件目录:`docs/agent-kb/gui-development.md`
- 样式解析规则:`docs/GUI_STYLE_RESOLUTION.md`;Tailwind 原子类全集:`docs/GUI_TAILWIND.md`

## 三条尺寸阶梯(硬规则)

token 定义在 `stdlib/Gui/Theme.zan`,由 `Style.zan` 导出为 `:root` 变量,
皮肤可整体改值——**永远不要写死数字**。

**字号**:`var(--font-size-tiny/small/medium/large/huge)` = 12/13/14/16/20。
正文 medium(14),辅助/标签 small(13)或 tiny(12),区块标题 large(16),
页面标题 huge(20)。更大的展示数字用 Tailwind 原子类 `text-xl..text-3xl`,
但一个画面至多出现一个超档大字。

**控件高度**:`var(--height-tiny/small/medium/large)` = 22/28/34/40。
按钮/输入框/选择框用 `.tiny/.small/.medium/.large` 皮肤档位类
(`skins/base.css` 已消费这些 token)。**同一行的操作控件必须同档**;
整块画面主按钮统一 medium,工具条统一 small,别混。

**间距**:一切间距是 4 的倍数(命中 7/13/17 这类值就是错)。
- flex/grid 容器的 gap 用档位类:`gap-none/small/medium/large` = 0/6/8/12
  (值取 `var(--gap-*)`),换肤时整套留白跟着变。
- padding/margin 用 Tailwind 原子类:`p-2`=8px、`p-3`=12px、`p-4`=16px
  (刻度 = n×4px;样式引擎原生翻译 Tailwind token,见 `Gui/Tailwind.zan`)。
- 惯例:卡片内边距 `p-3`/`p-4`,紧凑工具条 `px-2 py-1`,节与节之间
  `gap-large`(12)再往上只有 16/24,不要发明中间值。

## 对齐规则

- **表单行**:标签列固定宽(`Prefer(90, 0)`),输入列吃剩余空间;多行表单
  同一列宽、所有输入框同高同档。模式示例见 `references/composition.md`。
- **数字右对齐**(金额/计数/尺寸),**标题左对齐**,**操作按钮右对齐**
  (工具条、弹窗脚部一律右侧,主按钮在最右)。
- **垂直居中**:同行图标+文字组合挂类串 `flex items-center gap-2`
  (经 `Control.Class` 字段或 `AddClass()` 挂上);控件混排靠高度档一致
  保证基线齐,不靠逐个调 y。
- **网格对齐**:等宽卡片用 `grid`(`grid-cols-3 gap-3` 原子类或
  `Grid.Of(n).Gap(px)`),不要手摆 x/y。
- **一排控件两端留白要相等**(如一排按钮右端与左端对齐):行首/行末各放一条
  **同宽撑条**,容器挂 `display: flex`,要"各宽几像素"的子控件挂
  `flex-grow: 1` —— 剩余宽度平摊到控件本身,余数由引擎给最后一个可增长项
  (两排右缘因此落在同一列);撑条 flex-grow 0 不动,所以**间隙逐条不变**。
  **坑**:`Panel.Row()` 默认走**停靠布局**(`With()` = 左停靠),不做剩余
  空间分配,只按子控件的 `StyleWidth` 摆位 —— 容器没写 `display: flex`
  时子控件的 `flex-grow` 一点作用都没有(实测踩空一次)。
- 容器边缘:相邻区块共享同一条左边界,面板左 padding 必须同值。

## 层级与颜色

- **层级靠中性色 + 字号阶梯**:`var(--text-primary)` 正文、`text-secondary`
  次级、`text-tertiary` 弱提示、`text-disabled` 禁用。标题只升字号,不变色。
- **强调色只有一种**:`var(--primary)`。主按钮一个,其余 `.secondary/.ghost`
  变体;success/warning/error 只表状态,不当装饰。
- **颜色必须走样式层**:控件代码禁直读主题语义色、禁裸 `0xAARRGGBB`、
  禁直读字号——取色/取字一律走 StyleBox(`Style.Of(app, type, cls, ...)`,
  属性带兜底如 `s.FgOr(...)`/`s.FontOr(...)`)。
- Tailwind 原子类只用于布局/间距/圆角/阴影,**不用它的调色板**
  (`bg-slate-500` 会绕开皮肤主题,换肤即脏);要颜色写语义类或 `var(--token)`。
- 圆角同档:`var(--border-radius-small/medium/large)`;同一画面出现 3 种
  圆角就是没设计。阴影只用小/中档,弹层才允许大阴影。

## 风格方向(先选卡,再写码)

统一档位解决"任何风格都不塌";风格本身走**皮肤配方**:一个皮肤包 = 一个
`skins/<name>/skin.css` 的 `:root` token 覆写,零代码,随应用发布
(应用自带 `skins/` 优先于内置皮肤)。

九张现成配方卡(商务/街头嘻哈/赛博朋克/暗金豪华/国风/极简日式/玻璃拟态/
新拟态/卡通波普)在 `references/style-directions.md`,含可粘贴的 `:root`
覆写、排版/动效/文案性格、能力边界与验收点。选卡前过"选型三问":题材的
身体记忆、签名元素只放一处、拒绝 AI 模板脸(米色+衬线+赤陶、纯黑+荧光绿、
报纸细线)。

## CSS 方言:引擎认什么、不认什么

改皮肤/CSS 前先读 `references/css-dialect.md`——选择器/伪元素/取值单位/
已知解析坑全集都在那一册。硬底线只有三条:颜色一律
`var(--*)`或皮肤令牌;字号/间距/高度一律走尺寸阶梯 token,不写死数字;
未知属性写上去不报错也不生效,收尾自查要过一遍。
## 克制

- **把大胆花在一处**:一个画面一个签名元素,其余安静;删掉不服务内容的
  装饰——"出门前照镜子,摘掉一件配饰"。
- **留白是材料**:分组靠间距与分隔线,不靠框套框;拿不准时多留 4px。
- **空态与错误给方向**:空态用 `Empty` 组件 + 一句"下一步做什么";错误
  说清原因与修法。文案写用户视角:"保存更改"不是"提交",一个动作全程同名。
- **动效一处点睛**:内置关键帧 `animate-spin/pulse/breath/shimmer/float/glow`
  等一个画面至多一处;hover 微反馈可以普遍,入场动画不要。

## 组件封装的反哺

- **铁律:控件自己负责绘制,使用处只配置**(实例化→配属性→喂数据→摆位置)。
  禁止在使用处用 Canvas 原语重画已有控件——组件修好后示例还在按旧画法显示,
  就会"组件是对的,demo 是错的"。
- 觉得某控件"不精美"→ 修 `stdlib/Gui/Widget/` 或 `skins/base.css` 里的规则,
  让所有使用处一起变好;新皮肤值写进皮肤包的 `:root`,不散落。
- **新增 Gui 类先查重名**:历史上有在 `Gui.Component` 下新增
  彩带动画组件 `Ribbon`,把 SceneDesigner 等只 `using Gui.Component` 的文件里
  裸写的 `Gui.Widget.Ribbon`(功能区控件)整体劫持到新类上,调用点报
  "no member",离肇因提交很远——Zan 的名字解析按 using 就近绑定,同名类不警告。
  新增类落名前 `grep -rn "class <Name>" stdlib/Gui/`;撞名要么改名、要么调用点限定名。

## 毛刺防治(斜线/曲线/圆角的抗锯齿)

**毛刺 = 数据边被量化到整像素。** 斜线/曲线在光栅化器眼里只有"每像素覆盖
多少";只要把 1/256 定点的小数坐标交给它,AA 就自动正确。标准库的折线原语
(`DrawLine`/`DrawPolyline`/`DrawPolylineFx`)与 Chart 系列填充
(`FillColumnAA`/`FillBandFx`/`FillSpanAlpha`/`FillPolyAlpha`)都已是亚像素
的;界面出现毛刺,几乎总是绕开了它们自己拼:

- **数据边定点进光栅化器**:图表的线/面积边界这类数据驱动边,计算时保留
  1/256 定点(`yF = base*256 - num*256/den`),填充用带小数覆盖率混合的
  列填充(`ChartView.FillColumnAA`,边界行按 frac 混合)。把边量化成整数 y
  再用 1px 竖条 `FillRect(x, y, 1, h, ...)` 逐列拼,缓坡上每列硬跳一整行
  = 阶梯锯齿,条顶与 AA 折线之间还会露月牙缝(真实案例:DataTable spark
  面积、ChartBig 面积,修复均收口到 FillColumnAA)。
- **折线一笔连成**:整条线一次 `DrawPolyline`/`DrawPolylineFx`;逐段
  `DrawLine` 在拐角处两端各落一次实心像素,双重混合亮一像素、还可能留缝。
- **多边形填充用亚像素扫描线**:半透明多边形(雷达/弦图)用
  `ChartView.FillPolyAlpha`(过圆心自交的非零环绕用 `FillPolyWinding`),
  行中心采样、1/256 定点求交、段首末列小数覆盖混合;不要自己写整数交点
  配整行 FillRect,斜边会成整列硬跳的阶梯。
- **叠画月牙**:两段重合圆弧叠画,下层形状的 AA 边缘会在上层弧外露出一条
  浅色月牙(真实案例:异形窗口关闭钮悬停红 × 标题栏圆角)。修法是上层沿
  下层轮廓内收 1–2 逻辑像素(半径 -2),把下层 AA 带盖进去;不要试图用
  更多叠画去补月牙。
- **一次成形**:圆角控件禁止"方角画完再叠圆角盖"——克隆样式改
  Radius/Corners,一次画对;覆盖裁剪的原语自己拥有边缘,叠补必留边。
- **AA 渐变带固定 1 物理像素**:抗锯齿过渡带宽度是原语内部固定的,别随
  线宽/DPI 自行加宽;要更柔的效果用半透明描边,不动 AA。
- **半透明量程 0–255**:`Chart.WithAlpha` 直写 alpha 字节,传 256 会整型
  溢出成全透明——画了但看不见,极难排查。
- **验证仪式**:离屏 `new Canvas(w, h)` + `GetPixel` 统计"纯色直贴纯背景"
  的硬相接对数做阈值断言(仓库范式 `tests/gui/chart_fillaa_test.zan`、
  `chart_stackedarea_aa_test.zan`:同一图形整数实现 56/152 处硬相接,
  亚像素实现 0/4);肉眼收尾用 PrintWindow 截图放大 6× 看角与斜边。

## Chart 引擎改造与渲染探针(硬规则)

- **ChartSeries/ChartOption 新增字段五处同改**:字段声明、Create/Of 缺省、
  深 Clone、`Clone(src, shareData: true)` 浅路径**早退之前**的标量抄写块、
  浅路径的集合引用共享(levels/tree 等)。渲染管线 MaterializeSeries 每帧
  都走 share 路径,漏抄一处 = 字段静默丢失:模型层解析断言全绿,渲染却
  全吃缺省(真实案例:treemap/sunburst 的 levels 全链解析正确、渲染整图
  缺省配色,根因是 levels 在浅路径早退处被丢,配置断言测不出这种丢法)。
- **渲染探针两条铁律**:要看首帧静态效果用 `ChartView.KeyedStatic`
  (staticFrame 把动画进度钉在终点;普通 `Keyed` 首帧动画进度≈0,
  不在引擎免动画名单里的图型整板近黑);GetPixel 采样必须在 `Render`
  之后、`PresentFrame` 之前——Present 后读到的已是清屏面,每张 demo
  扫出同一单色(真实案例:探针首版把两种错各踩了一遍才定位)。
- **用户实机截图 ≠ HEAD 行为,先核对构建新旧再立项**:引擎修复不断
  落地,用户跑的 gallery 二进制可能落后几天;把 stale binary 的症状
  当活缺陷修会白走一趟(真实案例:用户截图刻度 `#0/#2/#4` + smooth
  狂野过冲,HEAD 探针里 y 轴声明域/平滑包络全是对的——min/max 接线
  是当天的提交;正确动作 = 先在 HEAD 复现,复现不了就重建 gallery
  (`scriptsuild_charts.ps1`)再要截图)。

## 缩放纪律(DPI:为什么界面忽大忽小)

框架的缩放是自动且不重复的,混乱全是绕开它造成的。机制:主题 token
(字号/高度/间距)由 `App.ScaleThemeMetrics()` 按"基线×密度档×DPI"统一重算;
CSS 里非 token 的长度由 `Style.ScaleLayout` 补乘,`StyleBox.IsPrescaled`
保证来自 `var(--token)` 的值不再乘第二次;Canvas 自绘是唯一例外——
`Canvas.DrawText` 的 fontSize、手算的坐标间距都不经过任何自动缩放。

从截图还原界面时的倍数判定是另一类坑:先按 `references/screenshot-restore.md`
1.5 节"三票定倍数"判出原图 DPI 档,换算只在布局账本里发生一次;把截图
物理像素直接抄进代码是双重缩放的头号来源。

硬规则只有一条:**每个尺寸值必须明确属于下面两条路径之一,全项目不得第三种**:

1. **样式路径(自动缩放,禁止再乘)**:CSS 声明、控件属性、`FontOr/Width`
   等 StyleBox 取值——写 token/档位值即可,框架已缩放,手再乘一遍就是
   "150% 显示下按钮大 1.5 倍"的双重缩放。
2. **自绘路径(手动缩放,禁止忘记)**:`Canvas.DrawText` 字号、HUD/自绘的
   坐标与边距——必须过 `app.Scale()`;且**封装成项目内单一 helper**(如
   `Ui.Dp()`),禁止在使用处散落内联 `* dpiScale / 100`。

自绘文字字号不许裸写数字:取主题字号阶梯(`Style.FontFallback(app,
"medium")` 等)或经 helper 缩放的档位值。审计手段:grep 直写字号数字
(如 `DrawText(..., 20)` 这类)、内联 `dpiScale`/`Scale(` 乘法、裸
`0xAARRGGBB`——每处命中都是"忽大忽小"的候选。

## 排版原语纪律(硬规则)——界面为什么会重叠

运行时给了完整的布局原语:**停靠**(Dock(1..5) 吃边、构造上互不重叠)、
**自动流**(`Panel.Column()/Row()` + `With()`)、**Flex**(一行/一列、可换行、
间距对齐全由档位类)、**Grid**(N 等宽列、可响应降列)、**FormBuilder**(表单)。
`Control.Arrange` 里只有 `dock==0`(手摆 `mx/my` + 手工宽高)这一条分支
能产生重叠。AI 生成的界面之所以"经常重叠在一起、尺寸位置乱七八糟"
(实证:大刷新钮盖住旁边的省略号钮、四张指标卡宽窄不齐、底部大片死
空间),根因全是**绕开布局原语、按像素手摆控件**——HTML 绝对定位的
习惯在这里没有兜底,窗口一缩放就散架。

硬规则:

1. **窗口骨架用停靠**:侧栏 `Dock(3).Prefer(w,0)`、顶栏 `Dock(1).Prefer(0,h)`、
   内容 `Dock(5)`;状态条 `Dock(2)`。停靠的子节点按声明顺序吃边,
   永远不会互相压住。
2. **内容面用流式/弹性容器**:`Panel.Column()/Row()` 的 `With()` 自动流是
   默认;一行多物用 Flex(`.Gap()`/`.Between()`/`.Wrap()`),卡片墙/指标行用
   `Grid.Of(n)` 等宽——**不要逐个手设宽度**(宽窄不齐就是这么来的)。
3. **手摆 `mx/my` 只属于画布类场景**:游戏场景、图表自绘、自由画布设计
   导出的坐标。表单/工具窗口里出现手摆坐标就是错的,先问"该用哪个容器"。
4. **尺寸只 `Prefer` 语义值,高度让控件自己量**:Flex/Grid 的容器高度按
   内容测量,不写死像素高度(死空间和裁剪都来自写死)。
5. **交付前跑重叠自检,清零才算完**:`ZAN_GUI_OVERLAP=1` 运行一次窗口,
   每对压在一起的兄弟会打一行
   `gui-overlap #N in <父>: <控件>[x,y w×h] overlaps <控件>[…] by ax×bypx`
   (窗口子系统程序无控制台时设 `ZAN_GUI_OVERLAP_LOG=<文件>` 落盘;
   测试里可 `Control.DebugOverlap = true` + `Control.OverlapHits()` 断言)。
   刻意叠放(角标/悬浮装饰)对那个子控件挂 `.NoOverlapCheck()` 免检,
   其余命中必须修到 0。命中为 0 的界面,必然不存在"叠在一起"。

6. **文字控件的宽高永远不手写**:按钮/勾选框的大小 = 文字 + 皮肤内边距,
   由测量自算;`Prefer(w,h)` 钉死按钮,改文案就裁字、换皮肤就变形,
   AI"一个按钮调来调去"反复微调的正是这个数。交付前 `ZAN_GUI_LAYOUTLINT=1`
   跑一遍,三类命中必须清零:`gui-lint … 固定尺寸`(手写宽高)、`拉高`
   (按钮被拉成巨块)、`裁剪`(矩形装不下文字);图标钮、画布图元、分隔条
   等刻意定尺寸的挂 `.FreeLayout()`(同一张牌同时免重叠与尺寸两检;
   测试用 `Control.DebugLayoutLint = true` + `Control.LintHits()`)。
   **同一条布局代码改到第二遍就停**:不是数值没调对,是结构选错了,
   回到规则 1-3 换容器。

7. **`Grow()` 只在停靠路径有效;进了 Flex 容器撑满要写 CSS `flex-grow`**。
   `Control.Grow()` 只做两件事:`grow = true; dock = 5`;而 `ArrangeFlex`
   分配剩余空间读的是 `StyleGrow()`,它返回的是 **CSS `flex-grow`**,与代码的
   `grow` 字段无关。两条后果(都是背包页实测踩出来的):
   - 停靠路径把 `dock == 5` 的子项**排到最后、共享同一块剩余矩形**,不按顺序
     各吃一段——`Row` 里"左固定 + 撑满 + 右固定"三兄弟,撑满的那个会跑到最右
     并与其他 Grow 兄弟重叠。要"左固定…右贴边"就得用 `DockRight()`(从右往左
     吃边,注意声明顺序会反过来),或者把容器改成 flex。
   - 容器要 `display: flex`(挂皮肤类),撑满的子项要 `flex-grow: 1`(挂皮肤类);
     代码里写 `Grow()` 在 flex 容器里等于没写。实证:背包下段 tab 行"左三 tab +
     撑满条 + 右六 tab"用 `Grow()` 撑条,六个右 tab 一直贴左边不动;取代表
     "格 + 中部撑满 + 数量 + 升级钮"的中部被排到最后,渲染顺序变成
     「格 / 数量 / 升级钮 / 名称」。
   - 皮肤里一条 `flex-direction` 会盖掉代码的容器方向:`.bag-top, .bag-lower
     { flex-direction: row }` 把 `Panel.Column()` 的下段压成横排(tab 行被拉满高、
     内容挤到右边)。Row/Column 混用时**一条规则只写一个方向**。

8. **停靠容器的 `Gap` 连第一个子项也算一份——首尾对齐别用撑条**。
   `Arrange` 的停靠分支在**每个**子项之后都扣一次 `StyleGap()`，包括第一个；
   于是「首撑条 + 缝」= 内缩 + gap，整行/整列右移/下移一个 gap 的量。
   实证(商店页):卡片行 `Gap(21)` + 首撑条 `Prefer(22,…)` → 卡阵整体右移 21
   设备；货币行 `Gap(9)` + 首撑条 → 右移 9。**左右(上下)内缩一律走容器
   `Padding(top, right, bottom, left)`，只有子项之间的缝用 `Gap()`**；确实
   要在两端留白又不想动 padding 时，把首尾撑条的宽度减去一个 gap。
   注意 `Padding()` 会被皮肤里声明的 `padding` 顶掉——那个类就别在 CSS 里
   写 padding。

9. **贴底元素用 `DockBottom()`(dock=2)，不要指望 `Grow()` 撑条把它推下去**。
   停靠分支是「先按声明顺序摆 dock 1..4，再摆 dock 5」，而 `Grow()` 把子项
   变成 dock=5。所以「内容 + 撑条(Grow) + 页脚」里，撑条排在页脚**之后**，
   页脚被摆在内容正下方、页面底部留一大块空。要贴底必须
   `footer.DockBottom(); body.Add(footer);`——**先 Add 的 dock=2 先占底**，
   所以「页脚之下那条底缝」要在页脚之前 Add。实证(商店页):页脚原本落在
   卡阵下 15 设备处，改 DockBottom 后才回到页底。

10. **流式容器里要手摆一个子项，必须 `Add()` + `DockManual()` + `Place()`，
    不能用 `With()` 再指望 `Place()` 生效**。`With()` 在 `Panel.Row()/Column()`
    里会按容器方向重设 `dock`（Row→3 左停靠、Column→1 上停靠），子项于是被
    摆到流的位置上，`Place()` 的坐标被忽略——实证（称谓页）「领取」钮本来要
    在格子里居中，用 `With()` 后贴在格子左边还拉满高；改成
    `b.DockManual(); b.Place(dx, dy); box.Add(b);` 才落回量出来的位置。
    注意 `DockManual()` 是 `dock = 0`，容器只在 `dock==0` 分支读 `mx/my`，
    所以这条也意味着**这个子项退出了自动流**，尺寸要自己给（`Prefer`）。
11. **`Label` 不认 `text-align`，居中靠容器的 flex `justify-content`**。
    `Label.OnPaint` 直接 `canvas.DrawText(bx, …)` 按盒子左缘画字；认
    `text-align` 的是 `StyleBox.DrawLabel`（自绘/控件内部走的那条路），Label
    没走。所以给 Label 的类写 `text-align: center` 是**死规则**——实测整张表
    的文字都贴左、与原版差 29 设备。正解：让**容器**挂
    `display: flex; justify-content: center; align-items: center`，Label 作为
    flex 项按自身测量宽排布（代码里的 `Grow()` 在 flex 容器里不参与，见第 7 条）。
    同理：控件默认尺寸/配色跟原图不一致时**先改皮肤规则**（如 stdlib
    `Pagination` 默认吃 `heightMedium` 34 逻辑，原图页脚只有 20 逻辑，加一条
    `pagination { height: 20; font-size: 12; }` 即可），不要回去自绘、也不要在
    每个使用处补坐标。

这两道闸门随工具链走:安装版 SDK 是发布时刻的冻结副本——早于对应闸门合入
的安装里没有它们,环境变量静默无效(skill 跑在工具链前面时先查工具链日期,
如安装目录 zanc.exe/stdlib 的时间戳)。正确动作是升级工具链后用内建闸门;
不要在业务代码里自研扫描器当长期替代——内建语义更全(签名去重/免检牌/
停靠+flex 两条出口),自研版升级即死代码。升级前的临时探针可以,但要标明
临时、升级后删。


自定义控件的子类契约:写 `Control` 子类(自绘控件/画布图元)必须有显式
构造器调用 `InitControl(名字, 停靠)`;隐式默认构造器不会跑基类字段初始化,
`children` 为 null、`visible` 为 false,首次 `With`/`Arrange` 即段错误。

排版容器三条实测：
- **`Panel.Row()` / `Column()` 默认是停靠布局，不是 flex**：`align-items`、
  `justify-content`、CSS `flex-grow` 只在该元素的 CSS 类显式写了
  `display: flex; flex-direction: row|column` 时才被采纳；而代码里的 `Grow()`
  **只在停靠路径有效**（第 7 条），进了 flex 容器要撑满必须给子项挂
  `flex-grow: 1` 的类。坑：六枚等分页签叠成一枚（容器没声明 flex）；交易市场
  表格盒右缘短 70 设备、页脚撑条不推分页（容器是 flex 却只写了 `Grow()`）；
  左面板被 `align-items` 默认 stretch 拉满整页高（给 flex +
  `align-items: flex-start` 才对上原图 167..876）。
- **flex 容器内容超出主轴就按比例收缩所有子项**（flex-shrink 默认 1）：定高的
  外框列里放「顶带 + 自适应中排 + 页脚」，中排按内容测量比可用高还大时，顶带
  从 38 被挤成 36、1 设备底线挤成 0。撑满项写 `flex-grow: 1; flex-basis: 0`
  （从 0 起分空间，不再溢出），定尺寸项写 `flex-shrink: 0`。
- **`Prefer(w, 0)` 是"撑满可用高度"，不是"高度自适应"**：布局把声明高度 ≤ 0
  当 fill。要定高就写显式数（面板 710 设备）；要按内容就别写 Prefer 高度。
- **显式 `Padding(top, …)` 与类上 `padding` 的优先级是"显式赢"**（padSet 优先；
  `Panel.StylePadT` 曾漏这条、被类 padding 顶掉致面板顶从 167 掉到 164，已修
  stdlib）。规则：同一元素不要两头都给——要么皮肤给 padding，要么代码给
  Padding，混用只会互相顶。

## 收尾自查(逐条过)

1. 字号只来自阶梯(含 Tailwind `text-*` 档),没有即兴值。
2. 同排/同组控件高度同档;按钮不再三种高度并存。
3. 一切间距 ∈ 4 的倍数;gap 用档位类;区块间隙全画面一致。
4. 相邻区块左边界共线;表单列宽全表统一。
5. 数字列右对齐;操作按钮集中在右侧;主按钮只有一个且最右。
6. 颜色只来自 token/语义类,没有调色板色/裸色值/直读主题字段。
7. 中性色三档承担全部次级信息,没用加粗/彩色冒充层级。
8. 圆角、阴影、图标尺寸全画面同档。
9. 至多一个签名元素 + 至多一处氛围动效;风格方向有明确出处(配方卡)。
10. 空态/加载/错误都有下文(Empty/Spin/具体错误文案)。
11. 文案:动词具体、全程同名、句式一致,气质匹配所选风格卡。
12. 换 dark/light 两个皮肤各看一眼,没有写死的颜色残留。
13. 斜线/曲线/圆角放大看无硬跳阶梯与浅色月牙;数据边走了定点亚像素原语,
    没有整数 1px 条拼接或逐段 DrawLine。
14. 每个尺寸值出处明确:样式值走 token 没被手动乘过缩放;自绘值全走
    项目单一缩放 helper,没有内联 dpiScale 乘法与裸字号。
15. Gui 面板与自绘 HUD 混排的画面,两边的字号/间距同源(同一 theme 或
    同一 helper),肉眼没有"一边大一边小"。
16. 骨架是停靠、内容面是流式/弹性容器;表单/工具窗口里没有手摆 mx/my。
17. `ZAN_GUI_OVERLAP=1` 跑过一遍,`gui-overlap` 命中为 0(免检牌只给
    刻意叠放的装饰)。
18. `ZAN_GUI_LAYOUTLINT=1` 跑过一遍,`gui-lint` 命中为 0;按钮/勾选框
    代码里没有 `Prefer` 写死的宽高(免检牌只给图标钮/画布图元)。

## 验证

- 编译:`zanc <file>.zan --auto-stdlib -o out.exe`(GUI 程序自动带 zan_gui 驱动)。
- 跑起来真实看一眼,截图对照自查清单(截图必须锚定被调试窗口的 PID、按窗口
  截取并先验证再判断);交互(点击/拖拽/键盘)用
  `ZAN_UI_SCRIPT` UiDriver 驱动做可重复流程,不要手点一次就算完。
- UiDriver 只绑进程里第一个 App:ChildWindow 里的树驱动不到。要端到端
  驱动子窗口界面,拆成 View 控件(真实整棵树)+ 薄 ChildWindow 壳
  (`SetRoot(new View(), null)`):探针把 View 挂进主窗口驱动全部交互,
  生产路径仍走子窗口壳,两边同源(微信模板通讯录管理窗,。
