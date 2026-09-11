# Web 等价 GUI 路线图（Web-Equivalent GUI Roadmap）

> 目标：让 AI 用 web 心智写的界面在 Zan GUI 产生与浏览器相同的渲染结果。
> 三层：布局引擎 CSS 语义化（P0-P4）→ HTML/CSS 声明层（P5-P6）→ 设计器统一（P7）。
> 决策记录：用户拍板"不考虑兼容、彻底改造"；声明载体选**真 HTML/CSS 文件**；
> 事件走 `data-on-*` 属性协议；游戏与工具共用一套窗口设计器，帧内热路径 HUD
> 用 dirty-region 门控。旧 .zform 废弃不迁移。
> 验收："web 一样"由 `scripts/web_oracle.py`（Chrome headless getBoundingClientRect
> 与 Zan Arrange 坐标逐盒对比）裁决，>1px 偏差要么修引擎要么在此记录为已知偏差。

## 分期状态

| 期 | 内容 | 状态 | 提交 |
|----|------|------|------|
| P0 | 布局基建：display 值集（flow=3/inlineLevel）、StyleBox 新字段（inlineLevel/floatSide/clearSide/boxSizing/whiteSpace/lineHeightKind）、UA 样式表 + Element 容器、border 参与布局（box-sizing 真语义）、最小块流骨架、web_oracle.py | ✅ 2026-09-12 | oracle basic 5 盒 0px 偏差 |
| P1 | 块流完整语义：margin 塌陷（CSS 2.1）、auto 宽高、百分比、margin:0 auto、匿名文本块 | ✅ 2026-09-12 | oracle basic 7 盒 + collapse 8 盒均 0px 偏差 |
| P2 | 行盒与 inline 流：横排/换行/text-align/vertical-align/line-height 三态消费、FontAscent 真 baseline、inline 文本混排（run+控件盒）、white-space | ✅ 2026-09-12 | oracle inline 4 盒（tol 3）：y/行高/盒高 0 偏差，x ≤3px 步进台账 |
| P3 | float：left/right 贴边、行盒绕排、clear、BFC 收编 | ✅ 2026-09-12 | b432d5c0 引擎 + 9ddcffa5 规则 2 + 本条（断言/文档）；oracle float 17 盒：11 精确、6 处 ≤2px（行高取整台账） |
| P4 | grid：track sizing（auto/fr/minmax/px/%）、span、隐式轨道、gap、网格线放置 | ☐ | |
| P5 | HTML 声明层：Html.zan parser、tag→控件映射、data-on-* 事件、data-bind、style/link 接线、GenHtml 编译期生成器、App.LoadHtml()、oracle 闭环 | ☐ | |
| P6 | overflow 滚动：auto/scroll 真语义（clip+偏移+滚动条） | ☐ | |
| P7 | 设计器 + HUD：存取格式 = .html、Inspector CSS 编辑、拖拽翻译 CSS、游戏窗口层嵌入帧循环、IDE 自用窗体重写 | ☐ | |

## 每期验收纪律

headless 坐标/像素断言（css_test 探针模式）→ oracle 逐盒对比 → conformance_gui_css
→ stdlib 改动跑 smoke 全层（HEAD 既有失败 pagination/transfer 除外）→
golden/audit/Inert 名单同步 → 提交 `gui-web(Pn): 主题`。

## 关键设计决策

- **display 值集**：0=legacy（无 CSS 时缺省，旧 dock/手摆行为，老组件零回归）、
  1=flex、2=none、3=flow（真块流）。CSS 写 `block`/`flow-root` 才落 3——
  现有皮肤没写过 `display: block`（base.css 里 display 声明仅 1 处 flex），
  所以语义替换零爆炸。
- **border 参与布局**：内容框 = 框 − border − padding（border-box 真语义）；
  padding box（absolute 包含块）= border 内侧不变。全局几何变化，golden 连锁更新
  （已授权）；17 个硬编码 Arrange 的 Widget 自算布局不受影响。
- **UA 样式表**：引擎内置 web 缺省（div/p/h1-h6 的 display/字号/margin），
  装载链 UA → skin → appCss。AI 不写 `display: block`——靠 UA 缺省，
  这是对"AI 熟悉 web"的核心承接。
- **line-height**：normal（驱动行盒）/ 数字倍数 / 长度 / %，存储分
  `lineHeightKind`（0 未设 / 1 px / 2 千分倍），消费端换算。
- **塌陷引擎（P1）**：pending-margin 状态机——`pending` 携带已塌陷未结算的
  下义务 margin，遇下一块的上边距时 `CollapseMargins` 合并落位（同正取大/
  同负取绝对值大/正负相加，CSS 2.1 §8.3.1）。首/尾链在"分隔"容器
  （`FlowSepT/B`：BFC=flow-root/行内级/声明了 overflow≠visible/absolute
  容器，或 border/padding 内衬）里计入内容高，普通容器里塌出容器外。
  空块（无子/无文本/块尺寸为零，`height:0` 也算零）自塌塌穿，在兄弟链
  合并；"只有空块的 BFC"把链关在内容框里占高（Chrome 同款，oracle 实测 25px）。
- **auto 关键字（P1）**：`width/height: auto` = 保持未声明哨兵 -1（此前
  "auto" 被当裸数字静默写 0）；margin 边 auto = 哨兵 -1，`StyleMarX` 消费端
  统一按 0，块流排布端直接读 `computedStyle.marX` 区分——两边 auto = 水平
  居中、单边 auto = 吃掉剩余空间（CSS 10.3.3）。
- **视口模型（P1）**：Chrome 里塌出 body 的链成为 html 外边距、把内容从
  视口顶推下来。窗口渲染入口 `RenderInside` 等价 html/视口，根塌陷链
  （`FlowMarT(root)`，根为普通块流容器时）落地为内容偏移 escT。
  **直接 `Arrange` 的子树根会丢弃逃逸链**——oracle 驱动按同款 escT 公式
  对齐（tests/weboracle/*_driver.zan）。
- **BFC 判据用真实声明通道**：`StyleBox.overflow` 缺省 1 是引擎渲染裁剪
  约定（子不画出界），不能当 BFC 依据；新增 `overflowCss`（-1 未声明/
  0 visible/1 hidden…）只在样式真声明时置位，`FlowSepT/B` 用它。
- **字体度量与 Chrome 同源（P2）**：`zan_gui_font_ascent/height` 从字体
  OS/2 表读 usWinAscent/usWinDescent + head.unitsPerEm（GetFontData，tag
  需字节交换传 0x322F534F/0x64616568），按 `floor(size×units/upem)` 逐项
  计算——与 Chrome/Skia(DirectWrite) fontBoundingBox 完全一致；GDI
  GetTextMetrics 自己取整（16px 相同、28px 起 asc 多 1、14px h 多 1），
  只作位图字体回退。FreeType 端同理 ascender/descender 各自 floor（不含
  lineGap），不再用 round(height)。
- **行盒模型（P2）**：piece（带样式文本 run / 原子盒 / 行首 strut）→
  贪心折行 → 行 A/D = 各 piece `asc = cellAsc + floor(lead/2)`、
  `desc = lh − asc` 的最大值（**负 lead 向下取整**，Chrome 基线探针实测；
  C 的 `/` 向零截断会整体抬 1px）。行内段的真混排 = `FlowEntry` 文档序
  （`Element.AddText/AddKid` 交错记录），块级边界 flush 成段；文本 piece
  回填 owner 的 run 缓存（`InlineRunPlace`），原子盒按 vertical-align
  落位（baseline 下 margin 边坐基线 / **middle = 盒中点对基线向上半个
  x 高**——CSS 原文 "baseline + 半 x 高" 是排版向上方向的加法，浏览器
  实测即 `top = baseline − xh/2 − boxH/2`，x 高用父字体
  `FontHeight×5/12` 近似 / top、bottom 对行盒上下）。
- **行内盒的矩形 = 字型内容区**：inline span 的并集矩形取
  `(基线 − FontAscent, FontHeight)`，不是行高——Chrome 对 inline 元素
  getBoundingClientRect 的语义（.big 28px 盒高 37 非 20）。
- **行内文本继承（P2）**：段内文本 piece 未声明 font-size/line-height/
  white-space 时随容器 strut（`InheritText`）——span 不写 line-height 时
  浏览器用的就是父级行高；块级子树样式解析不受影响。
- **float 引擎（P3）**：`FloatIntrusion`（margin box，容器内容框流坐标）
  入侵表随块树下传（`SetHostFloats` 按 dx/dy 换算到子块内容框），
  auto 高子块带表重测（被 float 顶高的部分计入堆叠）。行盒侧 `LineAvail`
  在**行顶处**查询可用区（Chrome 同款：float 贴着行底擦过不收窄本行），
  piece 放不下且 xOff 已越过可用右缘 → 整行下坠到 `NextShelf` 搁架、
  从行首重排；行几何（xOff/availW/yOff）随 `InlineLine` 记录，落位端
  按段顶+yOff 放（下坠行间有空洞，不能靠行高累加——盲路径 yOff=累加值，
  P2 行为逐 bit 不变）。放位 `PlaceFloatKid`：clear 先推 → 左 float 贴
  同侧右缘/右 float 镜像 → 放不下 NextShelf 下坠（16 次防呆原地溢出）；
  **CSS 9.5.1 规则 2**：后声明 float 顶边不低于先声明 float 顶边（oracle
  实测：f4 不能回 f3 之上的空档，被顶到 101 再撞 f3 坠 100）。
  **clearance 不进 margin 塌陷链**——它是物理位移，非分隔容器的首占位块
  margin 会塌出容器，clearance 跟着丢就错（cbox 70 高依赖它）。
  BFC 容器（flow-root）auto 高收编自己放的 float 底（CSS 10.6.7）；
  非 BFC 容器 float 溢出（Chrome 同款），clear 的兄弟把高撑起来。

## 已知偏差台账

- **（P1 已修）父子 margin 塌陷**：P0 骨架不塌陷；P1 落地 pending 链模型，
  basic 用例 #root 加回 selectors（塌出链经 escT 推到 y=10，与 Chrome 一致）。
  P0 的"5 盒 0px"其实是旧引擎"根内直接加 margin"与 Chrome 逃逸语义的巧合
  相等，P1 起才是真语义。
- **负 margin 塌陷链的折叠顺序**：`CollapseMargins` 按 2.1 公式（正最大+
  负最小）逐边折叠，n 元链非严格结合——极端正负混合链可能差一次合并。
  正值链（绝对多数）逐边折叠即取大，无歧义。未做负值 oracle 用例。
- **gap 只加在相邻"已放置"块之间**：空块不产生 gap（Zan 扩展语义，
  CSS row-gap 不参与塌陷，行为一致）。
- **匿名文本块总在子项之后**：P1 不做文本与块交错的真混排；P2 起
  `Element.AddText/AddKid` 文档序交错 = 真混排（仅显式用 AddText/AddKid
  建树的元素），`SetText` 整块文本仍最先入段。
- **shrink-to-fit 测量的文本断行**：测量期只有 hint 宽（PropagateWrapHint），
  无 hint 按一行估高；容器最终更窄时文本可能溢出容器（P2 复查结论见
  下方"流文本块测量/排布高度不一致"条）。
- **缺省 box-sizing：引擎 border-box，Chrome UA 缺省 content-box**：这是
  有意的决策——AI 生成的 CSS 几乎都带 `* { box-sizing: border-box }` reset
  （Tailwind 时代惯例），引擎缺省与之一致；oracle 用例按"现代实践"对齐
  （用例 CSS 显式带 reset）。显式 `box-sizing: content-box` 两边行为一致
  （ccbox 用例 90x50 对齐验证）。
- **块流容器的 shrink-to-fit 测量中百分比子项宽按 0**（Chrome 同款；
  basic 用例 root pref 70x145 即 .a 的 100% 落 0 后的结果）。声明了宽度的
  容器布局端不受影响。
- **（P2 已修）流文本块测量/排布高度不一致**：流文本的高取决于宽，测量期
  hint 缺失时按一词一行估高，排布端叠放却用测量高度 → 容器整体下坠。
  解法沿用 flex-wrap 的 hint 通道：宿主/驱动在 MeasureTree 前
  `HintWrapWidth(实际宽)`（real app 每帧有上一帧宽度，自然收敛）。
  inline oracle 用例即按此写法。
- **（P2 台账）行内 run 的 x 累计步进 ±3px**：GDI TextOut/度量按整数
  步进（"some text " @16 = 71px），DirectWrite/Chrome 按小数步进
  （73.64px），长 run 累计 ~0.3px/字符。基线、行高、盒高、原子盒落位
  与 Chrome 逐像素一致（oracle inline 4 盒 tol3 全过，残差全在 x）；
  逐字符像素级等价需要 DirectWrite 渲染管线，不在本路线图范围。
- **（P2 台账）x 高用 FontHeight×5/12 近似**：Segoe UI 真值 0.546em /
  1.3125em ≈ 0.416（Chrome 实测校准）；vertical-align:middle 的盒顶
  残差 ≤1px。其他字体族比例不同（引擎缺省 Segoe UI，可
  ZAN_GUI_FONT_FACE 换）。
- **（P2 台账）inline-block 声明文本时基线不取末行基线**：CSS 对有行内
  内容的 inline-block 取最后一行基线；引擎里"无子有文本"的行内子项直接
  PushText 成文本 run（视觉等价），"有子"的一律原子盒按 margin 边坐
  基线。极端嵌套（inline-block 内多行文本参与外层基线）未模拟。
- **（P3 台账）行高分数取整的逐行累计**：Chrome strut = winAsc/winDesc
  分数（Segoe UI 14px asc 15.1/desc 3.5，行盒 desc 取进位 4 → 行高 24），
  Zan 整数度量 asc 15/desc 3（行高 23）→ 多行块每行差 1px 累计
  （float oracle：words 146 vs 148、wrap 288 vs 290；x 几何与单行内
  逐像素一致）。修法 = 度量管线浮点化（牵动 P2 golden 全链），留 P5
  HTML 层一并评估。
- **（P3 台账）块级 strut 的 font-size 不继承**：`InheritText` 只服务
  行内 piece；块级子树未声明 font-size 时 strut 用引擎缺省（16px），
  Chrome 从 body 继承（14px）→ float oracle 的 #words 需显式
  `font-size: 14px` 才对齐。修法 = 样式解析加继承链（computed 传递），
  P5 声明层一起做。
- **（P3 台账）BFC 盒被祖先 float 挤窄未实现**：Chrome 里 float 旁的
  flow-root 块会收窄到剩余空间；引擎的 BFC 盒仍占全宽（行内内容经
  hostFloats 绕排，盒矩形不缩）。普通块盒两边行为一致（Chrome 普通
  块也不避让 float，只有行内内容绕排）。
