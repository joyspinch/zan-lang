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

## CSS 方言:引擎认什么、不认什么(改皮肤前先看)

皮肤/CSS 走 `stdlib/Gui/Css.zan` + `StyleSheet.zan`,它是 CSS 的**子集**,
写超出子集的写法会被静默丢掉(以前连"丢了多少"都看不见)。

- **八位十六进制是 `AARRGGBB`,alpha 在前**(`#22c9962f` = alpha 0x22 的金色)。
  按网页习惯写成 `#c9962f22` 会被读成 alpha=c9 的暗红——hover 一整块变不透明
  深色就是这么来的。四位 `#RGBA` 同理前置展开。
- **at-rule**:`@media` 现在运行期求值(min/max-width/height、orientation、
  `prefers-color-scheme: dark`、`prefers-reduced-motion`、pointer/hover,Level 4
  范围语法 `(width >= 800px)`/`(400px <= width <= 2000px)`;逗号=或、`not X and
  Y`);媒体环境是设计视口(designW/designH)与主题暗色/减弱动效开关,不是
  OS 窗口像素。`@import "x.css";` 按文件展开(相对路径按引入者目录、防环、
  深度 8,皮肤包可以拆文件;`Skin.zan` 磁盘加载已接线,内嵌资源里的
  @import 指不到磁盘文件)。`@supports (prop: value)` 是静态可判定的——条件
  成立把内层规则照常解析,判假只跳过这一块并按条件原文报出;取值不在引擎
  取值表就判假(`@supports (display: grid)` 为假),这是刻意的兜底保护。
  `@layer` 没有层叠优先级模型,按文档顺序摊平。`@keyframes`/`@font-face`
  仍整块跳过并计数(`animation` 只能引用内置 8 条曲线)。
- **选择器是复合块链**:type/`.class`/`#id`/`::part` 可用后代(空格)、`>`、`+`、
  `~` 组合成链(`panel > title`、`row > label + label`),复合块内可带状态伪类、
  结构性伪类(`:first-child`/`:last-child`/`:only-child`/`:nth-child(2)`/
  `:nth-child(odd)`/`:nth-of-type(2n)` 含 an±b/`:empty`)、`*`、属性选择器
  (任意属性,`=`/`~=`/`|=`/`^=`/`$=`/`*=`,`i` 标志,`[disabled]`/`[checked]`/
  `[selected]` 映射状态位;Element 的属性来自 HTML 属性表,普通控件只有 id)、
  `:not()`/`:is()`/`:where()`、`:has()`。**组合器、结构性伪类、`:has()` 与非
  class 属性条件只在 retained 控件树内命中**(`Control.RenderTree` 渲染时注入
  树上下文);手工调 `Style.Full` 的即时路径它们不命中(规则不丢,Lint 汇总
  报告)。`:has(img)`/`:has(> img)`/`:has(+ p)` 真实判定(前导组合器定候选
  范围:缺省严格后代、`>` 直接子、`+` 紧邻后兄弟、`~` 全部后兄弟),内层含
  伪元素部件或嵌套 `:has` 判 never。`::before`/`::after` 是部件:声明
  `content: "…"/attr(name)/none` 后 Element 的伪文本会真的拼进行盒(引号串
  支持 `\` 转义与 hex 码点);first-line/first-letter 判 never 并由 Lint 点名。
  `:where()` 权重计 0,`:is()`/`:has()` 取内层最大。
- **长度单位全套有求值器**:`em`/`rem`(当前字号/根字号,声明字号时的 em
  按未缩放前的当前值)、`ex`/`ch`(近似半字宽/半字高)、`vw`/`vh`/`vmin`/
  `vmax`(设计视口)、`pt`/`pc`/`in`/`cm`/`mm`/`q`(96dpi 换算)、`ms`/`s`/
  `deg`/`turn`;`calc()/min()/max()/clamp()` 可嵌套(calc 里 `%` 按视口宽
  近似)。长度不再需要写裸数字,`padding: 12px 1.5em` 这类网页写法直接抄。
- **现代颜色函数**:`rgb(0 128 255)`/`rgb(0 128 255 / 50%)`(空格语法 + 斜杠
  alpha,逗号旧语法也认)、hue 单位 `0.5turn`/`200grad`/`3.14rad`、
  `hwb(h w% b%)`、`oklab()/oklch()`(Tailwind 调色板的缺省写法)、
  `lab()/lch()`、`color-mix(in srgb, a, b)`(混合空间按 sRGB 近似,百分比
  缺省 50%)。**`currentColor` 生效**:`border-color: currentColor` 记账后
  在级联完成时替换成最终前景色(嵌在 `border: 1px solid currentcolor`
  shorthand 里也认;写 `color: currentColor` 等于保持主题前景)。
- **`var(--x, fallback)` 支持回退值**,未定义或空值时用回退;定义了就永远
  赢不了回退(CSS 语义)。所以"少给一个 token 整条声明消失"已经不是问题。
- **`position`/`z-index`/`order`/`overflow` 现在真的生效**(此前解析进样式盒
  但布局/绘制不读=写而不读):`position: absolute` 脱流(不占停靠/flex 的流
  空间),包含块是父 **padding box**,`top`/`right`/`bottom`/`left`/`inset`
  定位(对立边同设取差值,未声明宽高回退测量偏好);`relative` 在流位置上
  平移(同设 left/right 按 left)。`z-index` 决定兄弟绘制序,**命中测试用同一
  顺序的逆序**——画在上面的控件也先被点中。flex 容器里 `order` 改主轴
  顺序。`overflow: visible` 放行子节点溢出(缺省引擎裁剪,显式声明才变
  行为)。type 选择器大小写不敏感(`Button` 与 `button` 都命中,引擎按
  `Kind()` 的小写匹配);class/id 仍区分大小写。
- **Web 等价布局已落地前三批（WEB_GUI_ROADMAP P0-P2)**:
  `display: block` 是**真块流**——写成 CSS 的树走 web 语义,没写 display 的
  老代码走 legacy 零回归。**margin 是塌陷的(CSS 2.1)**:相邻兄弟取大合并,
  首子的 margin-top 塌出无内衬的父框把它整体顶开;空块(height:0/无内容/
  无边距内衬)上下边自塌塌穿。不塌陷的"分隔":容器写了 `flow-root`、
  `overflow: hidden`(必须真声明,引擎缺省的裁剪不是)、absolute 定位,或
  有 border/padding——AI 想避免塌陷用 `flow-root`,别学 overflow hack。
  **auto 关键字真语义**:`margin: 0 auto` 水平居中、`margin-left: auto`
  贴右、`width/height: auto` 等于没写(此前 auto 被静默当 0)。**匿名文本
  块**:`Element.SetText("...")` 的文本按行高断行占位参与块流。**border
  参与布局**:内容框 = 框 − border − padding(引擎缺省 border-box;显式
  `content-box` 反推框宽);`line-height` 三态(normal/倍数/%);UA 样式表
  内置 web 缺省(div/p/h1-h6 的 display/字号/margin),`Element` 通用容器
  (kind=标签名)写 web 风格容器用。**窗口根的塌陷链会推内容**
  (等价 Chrome 的 html 外边距;直接 `Arrange` 子树根则丢弃——测试对齐
  Chrome 时用带 padding 的接收者或 oracle 驱动的 escT 公式)。与 Chrome 的
  逐盒一致性由 `scripts/web_oracle.py` 裁决:用例 JSON(tests/weboracle/*.json)
  + Zan 侧驱动(*_driver.zan)输出同名 `sel x,y wxh` 行 `--compare` 对比;
  其余偏差记录在 `docs/WEB_GUI_ROADMAP.md` 台账。
- **行内混排(行盒,P2)**:`AddText("...")`/`AddKid(span)` 文档序交错 =
  真混排(16px 文本里混 28px span、inline-block 徽标都按浏览器行盒模型
  摆);span 不写 line-height/font-size 会**继承父级行高字号**(浏览器同款)。
  `vertical-align: middle` 对 inline-block = 盒中点对基线向上半个 x 高
  (浏览器实测语义);`white-space: nowrap` 单行不折、`pre` 把 \n 当硬
  分段(空行也占高)。字体度量与 Chrome 同源(OS/2 比例下取整),行盒
  基线/行高逐像素一致;仅行内 run 的 x 有 GDI 整数步进 vs 浏览器小数
  步进的 ~3px 累计差(台账,自洽渲染不受影响)。

- **float 布局(块流,P3)**:`float: left/right` 真贴边绕排——后续块的
  行内内容整行绕到 float 旁,放不下整行坠到 float 底下(Chrome 同款:
  float 贴着行底擦过不收窄本行);`clear: left/right/both` 把块顶压到
  相关 float 底下,且 clearance 是物理位移、不受 margin 塌陷影响。
  两个坑:① **后声明的 float 顶边不会高于先声明的 float**(CSS 9.5.1
  规则 2,oracle 实测)——右边看着还空着也不能"飘回去",它从上一个
  float 的顶边起找位、撞了照样下坠;② 容器要包住 float 高度必须写
  `display: flow-root`(BFC 收编),普通 div 的 auto 高无视 float
  (Chrome 同款),靠 clear 的兄弟把高撑起来。块级子树的 font-size 目前
  不继承(Chrome 会从 body 传下来),需要字号的块要显式声明,否则 strut
  落 16px 缺省。

- **grid 布局(display:grid,P4)**:真网格——`grid-template-columns/rows`
  认 `px`/`%`/`auto`/`fr`(整数)/`minmax(a,b)`/`repeat(N, 轨道)`;没显式
  模板的行吃 `grid-auto-rows`(轨道列表短了会**循环取用**,写一个值多行
  全用)。`gap: 10px` 或 `gap: 行 列` 双轴。条目放置:`grid-column/row:
  线号 / 线号` 或 `span N` 显式跨格,不写的走 auto-placement(行主序、
  放不下换行、只进不退)。定宽:px/% 先定,fr 分剩余,无 fr 时剩余均分给
  auto 轨(等于 Chrome 缺省的拉伸);条目高度按所在列宽**重测**(文本在
  窄列里会折行)。格内对齐 `justify-items`(水平)/`align-items`(垂直),
  写了尺寸的条目不拉伸、按 start 落。与 Chrome 逐盒 0px 对齐(oracle
  grid 19 盒)。坑:① `grid-column: 2` 的线号是**1 基**,span 是数量,
  `"1 / 3"` = 从线 1 跨到线 3(占 2 格);② 类名/组件名撞车——Zan 里
  `Grid` 已是 Widget 布局组件,CSS grid 引擎类叫 `CssGrid`,自己写
  Zan 代码别 import 锋利的 `Grid` 名;③ 命名线/命名区/负线号/
  `fit-content()`/`dense` 不认(按 auto 兜底),条目级
  justify-self/align-self 未接入,跨 span>1 的条目不参与 auto 轨的
  内容定宽。

- **HTML 声明窗口(P5)**:窗口可以直接用 HTML + CSS 描述——
  `app.LoadHtmlWith(html, handlers, baseDir)` 运行时建树(片段也行,
  没写 body 会包隐式 body),或把 `.html` 喂 zanc 编译期展开成
  `UiHtml.Build(handlers)`(发布不携带 HTML 文本与解析器;生成器与
  运行时吃同一解析器,几何逐盒全等)。属性协议:`data-on-click="名"`
  接事件(`handlers.Add("名", () => ...)` 注册;**Button 的 Click 落
  专属字段**,断言 `((Button)b).Click.Count()` 而非 `b.On.Click`)、
  `data-bind` 绑定路径、`style` 属性合成 `.zgen-N` 类规则(类级特异性,
  不是浏览器 inline style 特异性,`!important` 可覆盖)。容器 tag→
  Element(UA 样式表给 web 缺省 display/字号/margin,AI 不用写
  `display: block`)、button/textarea/input/img→真控件、select 落
  Element 占位。`<style>`/`<link>`/style 三路样式并进 appCss。规范:
  `docs/HTML_UI.md`。坑:① href/title/disabled 等属性静默忽略,行为
  在宿主语言;② 引擎级台账(行内 x 步进 ±3px、行高取整逐行 ±1px、
  块级 strut 字号不继承)对 HTML 层同样适用,fixture 里显式
  line-height/font-size 规避。

- **overflow 滚动(P6)**:`overflow-y: auto/hidden/scroll` 是真滚动容器
  ——内容溢出时钳位平移、在 padding box 裁剪、滚动条可用。`auto`
  溢出才出滚动条,`scroll` 常驻,`hidden` 裁剪且无交互但可程序滚动
  (`SetScrollTop`,Chrome scrollTop 同语义);单轴声明时另一根 visible
  按规范计算成 auto。滚动条是**覆盖式**(画在内容上,不占布局宽,
  不会像 Chrome 经典条那样把内容挤窄 17px);水平轴只裁剪不滚动。
  坑:① flex 容器要滚动,子项必须 `flex-shrink: 0`——否则弹性收缩
  把内容恰好压进容器,永远不溢出(Chrome 同款);② 滚动容器里的
  绝对定位后代也随内容滚(计入延伸);③ 滚轮认领要有指针悬停的
  先帧历史,UiDriver 脚本先 click 落点再 scroll,否则静默无效。

- **百分比尺寸(宽/高)**:`width:50%`/`height:50%`
  (样式表与内联 alike)对**流内块**按包含块解析,Chrome 逐盒 0px
  (oracle `tests/weboracle/pct.json`)。语义边界(都实测过):
  ① 包含块必须**定尺寸**——父块 `width/height` 声明(px 或 %,% 一路
  向上解析到定尺寸祖先);② 容器 auto 高(按内容补齐)里子项 % 高
  按 auto 回落测量偏好,不是 0(浏览器同款,别指望 % 高撑开 auto
  父);③ **内在宽场景(shrink-to-fit、float 测量、grid auto 轨道、
  flex 断行)% 按 auto**——css-sizing 规范,`width:50%` 的 float 不
  会把容器内在宽算成一半;④ 声明了 % 高的空块不参与 margin 塌穿
  (有确定高就不塌)。此前 % 宽整体失效(声明门不认千分比字段,
  流内块宽回落 100% 母宽、高塌 0),修在 P8(提交 b0b6a583),
  血条/HUD 宽度驱动从此可用 `SetProp("style","width:40%")`。

- **`!important` 真的压得住 inline**:带标记的声明单独存、在普通级联
  (含宿主 inline)之后统一再套一遍,不是"剥掉标记按顺序碰运气"。
- **渐变只认两端+中间一档**(`linear-gradient([dir,] a, b[, c])`);停靠点上的
  百分比位置(`#fff 40%`)被丢掉,运行时的 `grad_sample` 只采样 0/500/1000。
  `to left`/`270deg` 靠交换首末停靠点实现。
- **网页布局专属属性**(content/quotes/counter-*/list-style*/user-select/
  outline* 等)进了 Inert 白名单:收下、不变成 class、Lint 报 inert,不会有
  "漏进 SetProp 变 class"的灵异效果。
- **`text-shadow` 现在是真属性**(此前写了没反应):取第一层几何,≥3 层零模糊
  投影按四向描边绘制——压在图片上的白字用这个惯用法保可读性。
- **`border` 的 style 词生效**:`dashed`/`dotted` 走 `Fx.DashedBorder`,
  `none` 把宽度归零(此前所有 style 词被 `continue` 掉,虚线静默变实线)。
- **皮肤文件带 UTF-8 BOM 只有在走 `File.ReadAllText` 时才没问题**:它现在会
  剥掉行首的 `EF BB BF`。此前 BOM 让 `:root` 变成
  `\uFEFF:root`,不等于 `":root"`,**整张皮肤的 token 静默归零**——legend
  皮肤实测 vars 46→0、`.frame` 背景与字号全变 0,页面看起来"皮肤没生效"。
  自己拼 CSS 字符串(不经 `File.ReadAllText`)时仍要自己剥:解析器只认
  正好等于 `:root` 的选择器。
- **改完皮肤用 `sheet.Lint()`/`Audit()` 自查**:返回"写了但没生效"的清单——
  被跳过的 at-rule 数与名字、判假的 `@supports` 守卫(按条件原文)、语法拒绝的
  选择器、永不匹配的选择器(first-line/first-letter、内层含伪元素部件或嵌套
  `:has` 的 `:has()`,各带原因)、值里白名单外单位的裸数字强转、一条声明都没
  被消费的规则、收下但无效果的属性、只在树内匹配的组合器/结构性/`:has`/
  属性选择器条数。返回空表 = 每条都真的会生效。
  正常渲染不调用它,没开销。诊断皮肤"改了没反应"从这里开始,不要靠猜。
- **评估"支持度"必须带语料**:`scripts/css_coverage_audit.py` 按出现次数计权,
  分三层报(选择器:接受/语法接受但永不匹配/整条被丢弃;声明:认得/空转/不认得;
  取值:白名单外单位的静默强转),外加"解析进盒子但没有绘制消费者"的静默失败
  档。`@supports`/`@layer`/`@media` 内容递归计入,厂商前缀按 StripVendor 剥后
  计数。`--corpus-a` 跑仓内皮肤,给目录跑外部语料。bootstrap 语料按轮次迭代:
  声明不认识 30.6%→0.7%、值静默强转→0%;后续轮次(属性
  选择器/`::before::after`/`:has()` 落地)后选择器 live 70.9%→**99.9%**、声明
  accepted 75.3%→**95.2%**(`content` 由空转转正)、effective **99.4%**——
  **分母不同结论差一倍以上,所以报数字必须写清是哪份 CSS 量的**。

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

## 行内分隔线、Flex 方向类与 OnMeasure 表面着色(踩过的坑)

1. **行内分隔线用 `border-bottom`,不要在带 `gap` 的行里塞 dock2 分隔线
   控件。** dock 排布的 gap 会施加在「内容 ↔ 分隔线」之间:行高 64、gap 10
   的行,内容盒只剩 48,右列深处的徽标行被 `FitSize` 钳到 29px,33px 的
   胶囊画满即被自己的矩形裁掉底边(微信模板「徽标底部被切割」,。分隔线写成
   `.row { border-bottom: 1 var(--divider); }`,零布局成本;行 gap 只承担
   水平间距,或把水平间距挪到子类 `padding-left`。
2. **行内容要垂直居中:中列/右列用 `Flex.Column()` + `Justify("center")`。**
   `Panel.Column` 是 dock 顶对齐,文本块贴顶、行底留白偏大,肉眼即见。
3. **`Flex.Column()` 之后不能再用 `Class = ` 赋值**——`Class` setter 整体
   替换类列表,`column` 方向类被冲掉,纵列当场变横排(整行塌成一行)。
   追加类用 `AddClass("...")`。
4. **自定义控件 OnMeasure 里改表面色,直写字段,不调 `Bg()/Gradient()`。**
   这两个 setter 会把 computedStyle 置空,而 MeasureTree 里样式解析先于
   OnMeasure;随后父容器的 flex 排布从 computedStyle 读 `flex-grow` 拿到 0,
   控件挂 `.grow` 也不生长(ToolStrip 在 Flex 行里永远只有内容宽)。
   修法:`styleBg = c; styleBgTo = d;` 再
   `if (computedStyle != null) { Style.Inline(computedStyle, this); }`
   把 inline 覆盖补映到已解析的 box。ToolStrip/StatusBar 都因此修过。
5. **弹出面板/抽屉这类高度随内容的容器,根节点用 `Flex.Column()`,别用
   `Panel.Column`。** Panel(dock 容器)把 prefH 报小,宿主按小值分高度,
   Arrange 时内容按真实子项摆,尾部子项互相叠、被裁(微信模板表情/
   头像/文件面板「最后一行与提示语重叠」,;flex 的自然
   高度求和是准的。宿主还要 `AlignStart()`,否则列的交叉轴 stretch
   把子项拉满整行,`width: 424` 形同虚设。
6. **ToolStrip 的项自带皮肤类,加自有类用 `AddClass`,互斥状态类用
   `SetClassIn("wxon", ...)`。** `item.Class = "wxvoice"` 整体顶掉
   `text small` 后图标盒 51x43 装不下 51x51 的图标内容(lint:
   「矩形装不下内容」);反复 AddClass("wxon") 切选中会累积旧状态类。
   另外 `ItemAt` 返回可空,每个调用点判空太吵,收拢一个
   「越界给哑按钮」的助手最省。
7. **Zan 字符串按字节索引,`Substring(0, 1)` 对中文切出半个字**(渲染
   成「?」)。头像首字/缩写一律由数据显式给出(发言人注册表带 ini
   字段),代码里不要对中文切片(微信模板群成员格「过客云飞」头像
   变「?」,。需要**字数**时同样别用 `s.Length`——它是
   UTF-8 **字节**数;逐字走 `QrEncoder.SeqByteLen(s[i] & 255)` 才是
   字数(legend 名牌按 1..6 字选素材宽,用 `Length` 会把 2 字名字
   算成 6 字节、选错名牌素材,。
8. **聊天抽屉/表情面板这类要装完整控件树的「弹出」,用 dock + visible
   翻面的真控件列,不用覆盖层自管分发**(OverlayPopup.Host 是给选项
   列表/菜单自绘用的)。互斥显隐:再点同一图标=收起,开一个关其余;
   隐藏的 dock 子项不参与排版/命中/绘制,不会被布局自检报重叠。
   emoji 字形事实:Windows 上运行时字体回退把 emoji 渲染成单色轮廓
   (Segoe UI Symbol 一系),60 个常用 emoji 全有字形、无豆腐,但不是
   彩色——表情面板可以直接用 emoji 字符,深浅色主题都不挑。
9. **Flex 容器里 dock 不参与排布**:add 序即排布序(dock=4 不会跑到
   最右,「从右往左排」的注释在 flex 里是错的),拉伸要给子项挂
   `.grow`,否则按 pref 宽摆下一条,行右侧留一截「点了没反应」的
   死角(微信模板管理窗「朋友权限」行只有按钮那截可点、底部操作钮
   顺序反了,。顺序敏感的左右分栏容器用 Flex flow,别用
   Panel——Panel 对默认 dock 子项不保证 add 序(实测子项被排到尾部)。
10. **ListView 行模板里别放无行为的 Button**:按钮把点击吃掉,行的
    Select 就不触发了(点勾选圈勾不中行,。纯视觉件
    (行内勾选圈)用 Flex+样式做,点击穿透给行;要接行为的圈(表头
    全选)才用 Button 并自己绑 OnClick。
11. **ListView 对同一行的第二次点击走 Activate 不走 Select**
    (`again = (sel == index)` 才发 Activate):「再点一下收起」这类
    切换语义必须同时绑 OnSelect 与 OnActivate,只绑 Select 的点开
    就收不起(微信模板通讯录折叠分组,。
12. **Label 的文字从盒子左缘起画,`text-align` 对它无效**。`Label.OnPaint`
    只做垂直居中(`Canvas.CenterTextY`),水平方向不做对齐——给标签挂
    `text-align:center` 是**静默无效**的(legend 页 31 血字实测贴左约 10
    设备像素)。要水平居中就把标签装进挂
    `display:flex; justify-content:center` 的盒子(皮肤里已有先例
    `.tbl-cell`),或容器用 Flex 的 `Justify("center")`;别指望标签自己
    居中,也别用一个"和字一样宽"的盒子去蒙(字宽随字体度量变)。
13. **要手摆子项的宿主容器不能是 flex**。给子项 `DockManual()` +
    `Place(x,y)` 的宿主,皮肤类里不能有 `display:flex`——flex 把子项按
    add 序流式排,`Place` 与 dock 一起被忽略(同第 9 条,。**分工写死**:手摆宿主只给背景/边框,子项一个
    个 `Place`;自己需要 `display:flex` 的行盒(如要横排分段文字),只能
    当别人手摆的**子项**,不能再当"手摆子项的宿主"。

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
