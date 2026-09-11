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
| P1 | 块流完整语义：margin 塌陷（CSS 2.1）、auto 宽高、百分比、margin:0 auto、匿名文本块 | ☐ | |
| P2 | 行盒与 inline 流：横排/换行/text-align/vertical-align/line-height 三态消费、FontAscent 真 baseline、inline 文本混排（run+控件盒）、white-space | ☐ | |
| P3 | float：left/right 贴边、行盒绕排、clear | ☐ | |
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

## 已知偏差台账

- **父子 margin 塌陷未实现（P1）**：Chrome 里首子的 margin-top 会塌出父框
  把父整体顶开（basic 用例里 #root y=10），Zan 的 P0 骨架 margin 全算不塌陷。
  子项的绝对坐标不受影响（塌陷只挪父框），P1 实现塌陷后 basic 用例把 #root
  加回 selectors。
- **缺省 box-sizing：引擎 border-box，Chrome UA 缺省 content-box**：这是
  有意的决策——AI 生成的 CSS 几乎都带 `* { box-sizing: border-box }` reset
  （Tailwind 时代惯例），引擎缺省与之一致；oracle 用例按"现代实践"对齐
  （用例 CSS 显式带 reset）。显式 `box-sizing: content-box` 两边行为一致
  （ccbox 用例 90x50 对齐验证）。
- **块流容器的 shrink-to-fit 测量中百分比子项宽按 0**（Chrome 同款；
  basic 用例 root pref 70x145 即 .a 的 100% 落 0 后的结果）。声明了宽度的
  容器布局端不受影响。
