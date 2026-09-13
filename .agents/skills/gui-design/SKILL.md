---
name: gui-design
description: Zan GUI (stdlib/Gui) 审美与排版规范——对齐、间距、尺寸档位统一、层级与克制，常用风格配方，截图还原流程，以及排版原语纪律。用 stdlib/Gui 写界面、做皮肤换风格、用户提到 好看/美观/对齐/间距/风格/审美 时使用；界面出现 控件重叠/排版混乱/尺寸忽大忽小/毛边锯齿、画图表斜线曲线、HTML+CSS 写窗口 时也用它。
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
- HTML 窗口开发定式(.html 设计稿五件声明原语、两条建树通道、
  多按钮切中间区的 Nav 出口、游戏帧内边界):
  `references/html-window.md`(写 .html 设计稿窗口前先读)
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
但一个画面至多出现一个超档大字。**卡片墙的组头(每卡标题)别挤在
small-medium 之间**:画廊卡片标题先后用 13/15 都被打回"太小",17 才过
——密排卡片墙里 caption 与正文只差 1px 等于没分组,组头要么 large(16)
要么自定义再大一档,和正文拉开两级才算分组。

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
## HTML 窗口开发定式(硬规则)

窗口用 .html 设计稿声明(旧 .zform 通道已删,别再写):布局/内容/样式进
设计稿,交互与数据在 code-behind。五件声明原语:`data-on-*` 事件、
`data-arg` 带参、`data-bind` 绑模型、`data-if` 显隐、
`<template data-for>` 列表行。硬边界(每条都真踩过):

- **设计稿必须是编译输入的第一个文件**——zanc 从第一份设计文档合成
  Main 与控件字段,放后面 = 字段全缺。
- **写真 HTML 语义,不是全 div + data-* 的"类 HTML"**:凡 HTML 有原生
  等价物的组件一律写原生标签。全集:`<button>文字`、`<label>文字`、
  `<textarea>初始值`、`<img src/alt>`、`<input>` 按 type 七型
  (checkbox→Checkbox+checked、radio→Radio、range→Slider、
  color→ColorPicker、number→InputNumber、date→DatePicker、file→Upload,
  其余→Input+placeholder)、`<select><option>`(option 文本→options,
  selected→value 下标,无 selected 默认第 0 项=浏览器语义)、
  `<hr>`→Divider(void 无正文,文案走 `data-text`)、`<progress
  value max>`→Progress(折算 percent)、`<p>正文`→Typography。
  **没有原生等价物的组件写成 `zan-<kebab>` 自定义元素**(`zan-avatar`、
  `zan-data-grid`、`zan-tabs`——HTML5 合法标签名,tokenizer 收连字符,
  读端 TagKind 逐段大写反解回 Pascal kind);`data-kind` 只在"不标就
  看不出"时发(裸 div 承载非 Panel kind、span 上的 Element 覆写),
  裸 `<div>` 的 kind 兜底就是 Panel——布局壳(页面/卡片/格子)不标
  任何 kind。原生属性与 `data-x-*` 通道等价可混用(图片内容就该放
  src);复合属性(props/columns)才用 `data-x-<键>='<JSON>'`。
  旧稿的 div+data-kind 写法导入通道永久保留,不必追改。
- **裸 `<div>` 的 kind 兜底就是 Panel**:布局壳(页面/卡片/格子)不写
  `data-kind="Panel"`,剥掉后模型逐字节不变——只有"演示本尊"才值得
  标 kind。协议侧扩了原生映射后旧稿不会自动跟上:用
  `grep '<div[^>]*data-kind='` 普查各 .html 设计稿,把有原生等价物的
  控件换成原生标签;批量转换用探针校验——新旧稿各过
  `DesignerHtml.ToJsonDoc` 比模型,除有意的形状变化(如滑条 min/max
  从 props 袋提为原生属性)外应逐字节 SAME。
- **导入侧两个静默坑**(都真踩过):①读端属性循环里,专用分支必须排在
  通用分支之前——checkbox/radio 的 value 是文案(→label)、progress 的
  value/max 要截流(→percent),排进通用 value/min/max 分支之后就成了
  死代码,值静默丢失;②原生标签折进模型的键形态必须与消费端对齐——
  select 折出的 options 是 `"a|b|c"` 字符串,GenForm 老代码只认数组形态,
  options 静默不发射、SelectBox 空白。折新键前先查 GenForm/FieldSetup
  与控件 SetProp 期望的形态。
- **code-behind 职责分界**:结构全部进设计稿;只有声明通道喂不活的组件
  (泛型 `ListView<T>`、需运行时模型、立即模式助手、设计器文档节点)在
  HTML 里落 Panel 占位壳,由 code-behind 构造真控件 `Add` 进壳。两个
  易错形态:Wizard 的 `Render`/`Show` 是 App 级全窗绘制,不可 Dock 进
  卡片,内嵌场景用它的 `RenderList` 等子件;`FormField` 是设计器文档
  节点、自身无运行时绘制,运行时形态是 `FormBuilder.Build(设计 JSON)`
  实例化的真控件树。
- **"喂不活"的判别法**(裁定一个类能不能进画廊/目录时先看这三条):
  ①类头注释写"每帧由调用方…"、API 只有 `Begin/End/Bind` 没有
  `Dock/Add`(ScrollView、StyledText、ChatArea)——每帧驱动的宿主件,
  不是 retained 控件;②构造私有 + 无 `Kind()`(Layer/Prompt/
  Ellipsis 这类静态助手);③App 级全窗(Dialog/Wizard 全窗形态)。
  反例教训:ScrollView 名字像容器,真身是 `ScrollColumn` 背后的
  每帧滚动助手,差点当容器补卡——读类头注释+方法面,别望文生义。
- `data-if` 只认路径真值,不支持比较表达式;多按钮控制中间区走
  `Nav.Embed`(路由出口:惰性实例化、切走保留、可配临态),别用
  N 个布尔 data-if 硬拼。
- **zanc 编 .html 设计稿会先喷 `<unknown>` 噪音诊断,不是编译失败**:
  编译前置的命名空间预扫描对原始 HTML 文本跑一遍 Zan lexer,HTML 的中文
  与超长 data-uri 会报成 "unexpected character" 和 "string literal
  exceeds 4095 characters",文件名显示 `<unknown>`。这些是良性噪音;
  判定成败看最后的 Compiled/linking 行与产物 SELFTEST——曾把 54 个噪音
  错误误判为编译失败,白查半小时才定位到预扫描。
- **组件画廊/演示墙用瀑布流,大组件跨两列**(组件全量画廊定式):固定
  N 列行打包的行高=行内最高卡,DataGrid/Transfer 这类宽组件挤在单列
  370 逻辑宽里施展不开;改每列独立记账、卡片落"最矮位置"(并列取最
  左),大卡 span=2 横跨两卡槽(双倍宽),生成器断言
  max(列高)-GAP ≤ 内容区高。落位顺序用**高卡降序稳定排序**
  (key=(-卡高,-span),同高跨列卡先落)收平页底——手工把高单卡排前面
  只是打补丁,同高时单卡抢先落位照样把跨列卡的窗口顶高
  (导航页 892>854 翻车后改排序键才收平)。
- 布局按浏览器**等价子集**写(flow/inline/flex/grid/float/overflow);
  "写上去不生效"先查支持度审计与差异清单,再查优先级。
- 项目自带 `skins/` 目录要连 base.css 一起带——同名目录整体跳过
  stdlib 基线内嵌,基线层丢了 flex 全退化成叠矩形(wuwei 百艺页签全叠)。
- 游戏帧循环:菜单/面板可嵌(脏区门控,实测均值 3ms);每帧热路径 HUD
  仍走代码直绘,不进 DOM。

完整声明面/切页三路/验证定式:`references/html-window.md`(写 .html
窗口前先读)。
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
- **纯几何消费点击的控件必须自己注册命中区**(Tabs 页签条教训):不走
  On 通用事件包、按几何自己判点击的区域,要 `app.hitTester.RegisterRect`
  自己的条带——FireCommon 只给 `On.Any()` 的控件注册命中区,宿主只用
  `TabChanged.Add` 类型化接线(On 为空)时,按页签=按空白
  (hitId<0 → pressOnBlocker),释放被 ClickAvailable 判成"点外部"吞掉,
  页签永远切不动。
- **PropSpec 的 str/num/flag 必须绑字段本身,不能绑 getter 返回值**
  (ListItem 教训):`text.str = this.Label()` 绑到的是一份值快照,设计
  通道的 SetProp 写进死快照,界面永不出现;绑 `Text` 字段才拿到编译器
  合成的实时访问器对。
- **控件框高要对齐内部条带的自然高,否则皮肤底线变"两条下划线"**
  (组件画廊顶栏教训):Tabs 的选中指示线画在页签条内容底,控件的
  border-bottom 画在控件整框底;页签条内容自然高=theme.heightMedium(34),
  控件 fh 设 44 时两线错开 12px 看着像双下划线。修法是控件 fh 对齐
  heightMedium,不是去改皮肤线位。
- **富文本/自绘文本的默认前景兜底是纯白**(Arpg 深色底习惯):亮色皮肤
  里标记文本没写色码的段落、以及 #W 白/#Y 纯黄这类深底快捷色,画在
  亮底上全部隐形——不是"渲染丢了"。皮肤补
  `richtext { color: var(--text-primary) }` 兜住默认段;演示文案挑
  #R/#B/#H/#L 这类亮底可见色。

- **设计稿壳色写主题 token,不写字面 hex**(组件画廊暗皮肤翻车):卡片壳
  写死 `background:#ffffff`,暗色皮肤整面墙留白补丁,内容文字(跟随
  --text-primary 翻白)在白底上集体隐形——壳和组件用了两个颜色来源。
  壳的正规画法与 base.css 对 card/panel 同源:
  `background:var(--surface-bg); color:var(--text-primary);
  border:1 var(--border-secondary)`,辅助文字 --text-secondary/tertiary;
  演示数据本身的字面色(ColorPicker 初始值)才保留 hex。

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
  (`scripts\build_charts.ps1`)再要截图)。
- **自写曲线采样先验两件事:基和=1、t=0/1 精确落端点**。平滑折线的
  控制点是绝对贝塞尔控制点,必须配 Bernstein 基 `(1-t)³/3(1-t)²t/
  3(1-t)t²/t³`;把 Hermite 基的千分幂直接配 1e6 常数项(单位错乱)
  得 b0≈0.9995 恒成立——每段几十个采样全部坍缩在段起点,平滑曲线
  退化成首尾直联折线、末段"整根消失"(真实案例:line-smooth 七点
  图 Sun 悬空,根因查了三天,定位后改动只有 4 行基函数+2 行配对)。
  配对也别抄旧代码:`qx = b0·P0 + b1·C0 + b2·C1 + b3·P1`,旧 Hermite
  配对残留会让 t=1 落在控制点上而不是端点。
- **bbox-IoU 比较器对结构性缺陷全盲**:缺末段、多余竖网格线、整带
  错位 19px,IoU 照样 ≥0.5 PASS——"比较器全绿"不等于"图对"。数值
  oracle(SSR 出 SVG 提取包围盒/解剖)+ 逐图目检缺一不可,用户报
  "基本没对得上"时先目检再信指标(真实案例:折线族 36 图 27 PASS
  的同一天,用户点名的前两张图都是结构性错的)。
- **官方缺省值一律 SSR 实测,不凭文档或旧版记忆**:splitLine 缺省按
  **维度**(xAxis show:false、yAxis show:true,与轴类型无关),ECharts2
  "类目轴竖线默认开"的旧注释会误导出多余竖线;axisPointer.show 缺省
  是字符串 **'auto'**(声明了 value 就显示),按 bool false 处理会整根
  漏画静态指示线。SSR 一行 `getComponent(axis).get(key)` 拿到的就是
  合并后真值。
- **主题/CSS 调色板只填未声明的槽**:系列自带 color/itemStyle.color、
  根级 option.color(整表替换主题调色板)都必须跳过 chart::series-N
  的 CSS 覆写——常驻基线主题包一上,不带守卫的覆写通道会把
  multiple-x-axis 这类自带调色板的图逐槽盖回默认色,而且渲染不报
  错、只有颜色对不上(真实案例:echarts6.css 设为 App 缺省包当天)。
- **值对系列([[ts,v],...])落在 points 而不是 data,分发判定要照顾**:
  "点系列→lines"的判定若排在堆叠判定之前,stack+time 图永远走不到
  堆叠面积渲染器,两条同名 stack 系列各自按原始值画(真实案例:
  line-tooltip-touch 粉带不堆叠,DispatchKind 里换两行顺序即修)。
- **Keyed 每帧新建实例,图表跨帧悬停状态必须放 wid 悬停槽**:宿主
  每帧 `ChartView.Keyed(o, key).Render(...)`,实例随帧丢弃;
  previousHit/pointerWasOver 这类状态放实例字段时,fadeFocus 渐隐读
  "上一帧命中"永远是空。实测的坑:无头探针复用同一实例渲染三帧全绿,
  活窗口光标钉在数据点上却死活不渐隐——两种用法行为分裂,探针测不出
  实例生命周期的坑。跨帧状态进 `ChartView.HoverSlot*`(按 wid 键)。
- **GUI 重绘是按需的:hitTester hover 变化才调度,图表内部悬停要自己
  补帧**:App 的 mousemove 只在 hitTester hover id 变化时置
  needsRedraw,而系列命中(currentHit)不在 hitTester 里——光标停在
  数据点上后没有任何下一帧,晚一帧的渐隐/item 卡永远不渲染。实测:
  同一时刻导航行 CSS :hover 会亮(hitTester 链活),图表拾取却"死";
  修法是 DispatchPointerEvents 在 hitChanged/entered/left 时补
  `needsRedraw = true`。
- **悬停命中要对齐官方 linePrecision:类目折线拾取含线段距离,不只
  数据点半径**:官方 trigger:'item' 沿整条折线可悬停;只拿"最近数据
  点 < 抓取半径"判定时,线段中段悬停毫无反应(真实案例:bump 图圆点
  悬停修完当天,用户指出"鼠标经过线就有效果,不只是圆点"——光标离
  最近点 300+px 仍应命中)。实现 = 指针到相邻两点连线(数据点弦)的
  SegDist2 也进最近候选,命中归属较近端点;启动参数路径(SelectDemo
  带 demo 参数)会早于容器惰性初始化,重掷/替换缓存前先判空建表。
- **数据值直接进像素公式的通道必须过显式域映射或硬帽,大值域会画出
  天文数字图元刷满画布**:geo 投影散点旧启发式 `r=Scale(3)+z*Scale(14)/100`
  拿数据值当尺寸——scatter-world-population 人口 1.35e9 算出半径
  1.9e8px,整张画布被一个圆刷成系列调色板色(geo-choropleth-scatter
  同炸)。官方语义是 visualMap inRange.symbolSize [lo,hi] 按声明的
  min/max(未声明则数据域)线性映射直径;凡 z 驱动半径一律再过
  Scale(60) 硬帽兜底(真实案例:全量 sweep 的 BLANK 分诊里,两张
  "满屏纯色"图都是它)。
- **全量 demo 审查靠 sweep 机器落 id,不靠人眼扫**:gallery 加 `--sweep`
  启动参数,逐 demo FromJson+KeyedStatic 渲一帧 Canvas.WritePixels 落
  ZPX1 + manifest(时延/异常按例捕获不中断),python 解码 png + 启发
  FLAG(非背景占比/调色板命中/内容 bbox/边缘接触/轴线暗游程)。用户
  "有的缺轴/有的空白/有的刷色"式模糊指控,一晚上落成 demo id 清单。
  三类启发误报要先看图再信 FLAG:treemap/sunburst 满幅 EDGE-CLIP
  正当、polar 类本无 X/Y 轴、ECharts6 浅色轴线过不了暗游程阈值。
- **SVG path 命令字母的大小写就是绝对/相对语义,解析不得归一化**:
  小写 l/c/s/q/a 是相对坐标,解析时把命令字母统一转大写
  (`cmd = toupper(ch)`),相对命令全部被当绝对执行,一条路径的后续
  点漂移成横跨全图的弦多边形,渲染与命中双双炸穿不报错(真实案例:
  geo-svg 冰岛底图悬停海面蒙上巨大半透明三角,插桩打 ring bbox
  前后对比一锤定音:漂移 bbox 跨全图 → 正确 bbox 60px)。命令只存
  原字母,绝对/相对在消费处按 `rel = ch >= 97` 推导。
- **SVG 底图的交互区域只来自命名形状,fill="none" 线稿不可区域命中**:
  官方语义是 SVG 原样渲染(光栅/矢量皆可)、区域只叠加交互,区域名
  优先级 形状 name > data-name > 祖先 `<g name>` > id——组名要向
  子形状传播(flight-seats 座位名全在 `<g name>` 上,漏了 regions
  收 0 个、整图连底图都不画)。冰岛 3061 个形状仅 2 个命名
  (trip1/trip2 且是 stroke-only 航线):未命名形状(海面/装饰)跳过
  不命中,`fill:none/transparent`、`fill-opacity<=0`、`style fill:`
  的线稿只画不做实心区域命中——否则悬停海面误触航线 ring 的
  emphasis 填充,整图蒙色(真实案例:"鼠标经过对 SVG 的干扰")。
  零命名区域的纯装饰素材也要照画光栅底图,早退分支先判 svgMode。
- **同一份数据不许两条解析车道齐收**:geo 系列的 [[lng,lat,z]] 数值对
  被通用数值对车道(×1000 定点)与 geo 专用车道(GeoQ ×100、y 取反
  纬度向上)各收一遍,同一系列两套点一半屏外一半屏内,悬停命中框与
  可见点错位(真实案例:geo-svg effectScatter 六个点消失/错位,根因
  是解析分支没先判 coordinateSystem)。通用数值对车道必须先排除
  geo;同理 SVG 底图(用户单位 ×1)与矢量地图(度 ×100)两套坐标
  尺度的投影必须分开,封装成带单位旗的单一投影函数,禁止在调用处
  散落换算。

- **官方图型的数据项可以是"值向量"，标量车道会静默取 0**:radar 每个
  data 项是一条独立多边形(对象 {value:[每轴值],name} 或裸数组)——
  拿 Double("value") 读数组得 0,整图塌成圆心一个点,而解析/渲染全绿
  不报错。接入新图型先看官方数据形态;多边形语义的值数组在解析期
  物化成"每项一个系列"(图例取数据项名,未命名继承系列名——"同名系列
  随主项一起开关"的图例语义正好成立),值 ×1000 定点、显式 max 同步
  放大(渲染式 val×g×r/(max×1000) 的 g≈1000 会约掉),tooltip 显示走
  FracText 除回,0.46 这类小数才不丢(真实案例:radar 主力 demo 双
  多边形消失+轴标签错拿图例文案+radar-aqi 星爆全炸,一批修)。
- **组件级 option(只有坐标系没有系列)官方画骨架不画 no data**:
  分发链"0 系列→空状态"之前先认组件 radar/polar——两渲染器的骨架
  路径 0 系列本就安全,白白被空状态短路(真实案例:doc-example/radar
  的纯雷达组件落 "(no data)",官方画五轴网格+指示器名)。
- **渲染形态不对,先 dump 物化 json 的对应键再定责**:官方例 TS 的
  组件数组会在 json 里完整保留(radar-multiple 三联雷达的 center/
  radius/indicator.text 一直在案),缺的可能是引擎解析入口(radar
  数组形态没解析,只认对象形态)——不 dump 就动手,会白查一轮数据侧。
  口诀:json 有=引擎缺,json 无=物化缺;ECharts5 radar[]与 ECharts2
  polar[] 字段同构,消费端齐全时补的只是解析(真实案例:三雷达全画
  一个圈,以为是双 polar 圆心语义没实现)。
- **逐系列物化块里的 option 级字段只准改一次**:物化循环逐系列进,
  里面的全局放大/归一会连乘(真实案例:多雷达 3 个系列把 radarMax
  ×1000 连乘三次成 1e8,单系列 demo 永远不暴露)——用已置位的旗
  (radarScaled)守卫"只做一次",旗天然标记"首轮已做"。
- **热循环先审被调函数的收敛成本,再怀疑容器**:力导向 30 轮 × nn²
  的内循环 8.3s/帧,把对象 List 访问换平铺表毫无变化——真凶是被调的
  整型开方:牛顿迭代初值取 r=v 本身,要 ~17 次带 long 除法的对折才到
  √v,每对 ~1μs。修法 = Math.Sqrt(硬件 sqrt)+平方域剪枝(d²≥k² 直接
  跳,与 d<k 分支语义等价)+原生 int[]/CSR 邻接——8111→78ms(真实案例:
  graph-webkit-dep nn=513 首帧卡 8s)。插桩标记必须放进嫌疑函数内部
  (入口/出参两侧),只框调用侧会把热点错钉到别的段,白改一轮。
- **重活下放后台的现成定式 = Thread.Start + app.Post(捕获闭包)**:
  worker 只做纯计算(不触 UI 状态),成品经 `app.Post(() => {收货})`
  整只移交——App.Post 自带 window.Wake()(唤醒阻塞中的 WaitEvent),
  DrainPosts 执行后自动置 needsRedraw,不用自己搭互斥队列;UI 侧用
  pending 槽画「加载中…」占位(范例 ImageHttp.zan)。--nomouse/
  回放/审查这类"单帧截图必须终态"的路径保持同步装载。
- **插桩轮次先核产物 mtime 再信输出**:一次编译失败(比如往表达式
  续行中间插了语句)会让 sweep 跑旧 exe,新加的打印零输出,诊断被旧
  二进制牵着走;跑前核对 build 产物 mtime 晚于最后一次源码编辑。
  python 批量插桩只准插在语句起始行前(上一非空行以 `;` `{` `}`
  结尾),`+ Call(...)` 续行中间插语句 = 编译失败。
- **option 里的颜色声明走全链才生效**:legend.textStyle.color、
  radar.axisName.color 这类字段要过五处:ChartOption 字段+Create/
  Clone+DrawOption 拷贝+渲染器传参/消费端。漏最后一级(渲染器读
  主题色没读声明)时解析全绿、渲染死灰(真实案例:radar-aqi 深底
  图例白字画成主题灰字,金轴名根本没接)。

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

悬停/点击命中是缩放纪律的第三条战线:**鼠标坐标与画布坐标必须同一
空间**。150% DPI 下宿主把物理光标差 ×1.5 喂给 `app.mouseX/Y`,而
自绘/图表画布坐标是 1:1 客户区像素——拿 `app.mouseX` 直接对自绘
坐标做命中,命中点系统性往右下漂 1.5 倍(真实案例:图表悬停卡总在
光标右下方、geo 地图悬停打不中可见散点,同帧插桩 px=788 vs mx=1211
恰 ×1.5 定谳,机器全图表车道通病)。命中前先核对两侧坐标空间——
DPI 缩放全链只准发生一次,鼠标入口与渲染出口必须约定同一次缩放;
合成注入的测试坐标(如 UiDriver 事件)走画布空间,可绕开做回归。

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
- **多窗口端到端验证用真实 Win32 点击驱动,别硬掰 UiDriver**:登录窗
  (独立泵)→主窗这种多窗口流,UiDriver 绑在第一个 App 上驱动不到主窗;
  改用 Win32 层驱动(SetCursorPos+mouse_event 客户区坐标+标题栏高度
  修正、PrintWindow 按 hwnd 截图、Stop-Process 收尾),打字走
  **剪贴板粘贴**(Set-Clipboard+^v)——机器只有中文 IME 时 SendKeys
  会把 "admin1234" 组成 "admin安德敏234"。粘贴中文文本的进程管道
  (bash→powershell)按 GBK 显示乱码是**终端显示问题**:
  `乱码.encode('gbk').decode('utf-8')` 还原后与服务端存储 byte 级比对,
  别当成输入 bug 修。
- **HTML 运行期文档通道(GenHtml)的树根必须代码补 DockFill**:
  `Build()` 返回的 body 元素 dock=Manual,窗口只填 dock=Fill 的根——
  漏一句整棵树缩在左上角;类名=文件基名帕斯卡化+Html(App.html→
  AppHtml),成员只有静态 Css 字段(无括号)+Build 方法,不发射 Main。
  搬 CSS 进 `<style>` 时逐条补单位:`line-height: 72` 是 72 倍行高
  (无单位=倍数),30px 字号配它=3240px 行盒整树顶飞,要 72px 必须
  写 `72px`(详见 references/html-window.md)。

## 在 zan-lang 仓库内工作(仅仓库内,发布给用户的版面无此节内容)

- 完整心智模型、皮肤与样式解析:`docs/agent-kb/gui-development.md`、
  `docs/GUI_STYLE_RESOLUTION.md`。
- 守门测试:`policy_no_widget_drawing`(examples 自绘)、三条颜色/字号预算
  棘轮;新组件必带 conformance 测试(`docs/STDLIB_COMPONENT_STANDARDS.md`)。
- 构建回归:`scripts\build_gallery.ps1` + `scripts\build_ide.ps1` 必须过;
  视觉检查用 gallery 深链(组件名+皮肤直达,如 `./gui_gallery Slider
  liquidglass zh`),完整构建/启动流程见仓库文档;
  像素级改动参照 `tests/conformance/conformance_gui_chart_symbol` 加离屏回归。
- 新增内置皮肤:在 `stdlib/Gui/skins/<name>/skin.css` 建包即可自动发现
  (可选 `banner.png` 预览图)。
