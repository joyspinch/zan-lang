# CSS 方言与引擎解析规则（gui-design 附册）

> 引擎认什么、不认什么，改皮肤/CSS 前先查本册；正文只保留排版规范。

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
- **动效系是真属性,整链生效**:`transform: translate(...)/scale(...)/rotate(...)`
  (可多函数串联)、`transition`/`transition-duration`(属性级时长) +
  `transition-timing-function`(内置 8 曲线)、`animation: <name> <dur> <easing>`
  (只认内置曲线,@keyframes 不解析)。现代独立写法同样认:
  `rotate: 90`/`scale: 1.2`/`translate: 10px 20px` 可脱离 transform 单发。
- **视觉效果**:`backdrop-filter`/`filter` 认 `blur(Npx)`(玻璃拟态主通道)、
  `opacity`、`aspect-ratio`(定宽推高/定高推宽)、`text-shadow`(取第一层
  几何,≥3 层零模糊)、`border` 的 `dashed/dotted` 走虚线描边。
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
