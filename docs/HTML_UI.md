# HTML 窗口规范（HTML UI）

> WEB_GUI_ROADMAP P5 的主文档：用 HTML + CSS 描述窗口，事件走
> `data-on-*` 属性协议。同一份 .html 既能运行时动态装载（`App.LoadHtml`），
> 也能编译期展开成建树代码（GenHtml 生成器）——两条路径吃同一个解析器，
> 几何逐盒全等（oracle 裁决）。"web 一样"的布局语义（块流/行盒/float/
> grid）见 `docs/WEB_GUI_ROADMAP.md`；本文只讲声明层本身的协议与支持面。

## 两条装载路径

**运行时动态装载**（html 文本可来自文件/网络/字符串）：

```zan
HtmlHandlers h = new HtmlHandlers();
h.Add("submit", () => { /* ... */ });
Control root = app.LoadHtmlWith(html, h, baseDir);   // <link> 相对 baseDir
// 或 app.LoadHtml(html)（无处理器、不解析 link）
```

`LoadHtmlWith` 把解析收集到的样式（`<style>`/`<link rel=stylesheet>`/
style 属性合成规则）并进 appCss 再建树。`root` 恒为 body 元素——片段
没有 body 标签时包一层隐式 body（对齐 Chrome 的文档语义）。

**编译期展开**（发布不携带 HTML 文本与解析器）：

```bash
zanc app.zan ui.html --stdlib-path <stdlib> -o app.exe
```

`.html`/`.htm` 输入经 ZanGen 的 GenHtml 生成器展开成一个建树类
（`ui.html` → 类 `UiHtml`；基名帕斯卡化 + `Html` 后缀）：

```zan
public class UiHtml {
    public static string Css;   // <style>/<link>/style 属性收集的 CSS
    public static Control Build(HtmlHandlers handlers);
}
```

调用方在 Main 里 `app.UseAppCss(UiHtml.Css); Control root =
UiHtml.Build(handlers);`。生成器与运行时共用 `System.Web.HtmlParser`
的声明记录（WDoc/WNode），建树顺序同构——`UiHtml.Build` 的几何与
`App.LoadHtml` 逐盒全等（tests/weboracle/html 13 盒 diff 全等）。
生成器 exe 按 zanc+stdlib+源内容哈希缓存在
`%LOCALAPPDATA%\Zan\gen`，stdlib 变了自动重编。

## 元素映射

| HTML | 控件 | 文本/属性去向 |
|------|------|--------------|
| body（显式或隐式） | Element "body" | 树根 |
| div/p/h1-h6/ul/ol/li/span/b/i/strong/em/… | Element(tag, id) | UA 样式表给 web 缺省语义（display/字号/margin） |
| button | Button | 捕获文本（内嵌元素忽略） |
| textarea | TextArea | 捕获文本 |
| input | Input | value/placeholder 属性 |
| input type=checkbox | Checkbox | — |
| img | Image | src/alt 属性，object-fit cover |
| select 等未映射 tag | Element 占位 | 容器语义 |
| style/script/title | 原文模式 | style 收集为 CSS，script/title 丢弃 |
| link rel=stylesheet | — | 读 baseDir/href 并进 CSS |
| meta/base/br/hr | — | meta/base 忽略；br/hr 记为 void 节点 |

HTML 实体（`&amp;` `&lt;` `&#65;` 等）在文本与属性值里都解码。

## 属性协议

| 属性 | 语义 |
|------|------|
| id | Element → 选择器名（nodeName）；真控件 → `SetProp("name", id)` |
| class | 逐个 AddClass |
| style | 行内样式合成专属类 `.zgen-N { ... }`（复用整套级联与 !important 机制，见差异清单） |
| data-on-\<evt\>="名" | 事件接线（见下） |
| data-bind="路径" | 绑定路径 bindPath |
| href/title/disabled/… | 忽略（行为在宿主语言，不装死） |

## 事件协议

`data-on-click="submit"` 把名为 `submit` 的注册表条目接到控件的
Click 事件。注册表是 `HtmlHandlers`：

```zan
HtmlHandlers h = new HtmlHandlers();
h.Add("submit", () => { ... });
```

后缀 → 事件名映射（`HtmlText.EventKind`，运行时与生成器共用）：
click→Click、dblclick/doubleclick→DoubleClick、
rightclick/contextmenu→RightClick、mousedown/mouseup、
mouseenter/mouseover→Enter、mouseleave/mouseout→Leave、wheel→Wheel、
focus/blur、keydown/keyup/keypress。未映射的后缀与未注册的名字都
**静默不接线**——HTML 是声明，名字打错表现为没反应而非崩溃。

接线走 `Control.BindEvent`（多态）——Button 把 "Click" 路由到专属
`Click` 字段（与 `btn.Click += h` 同队列）。断言事件数时按控件实际
槽位查（`((Button)b).Click.Count()`，不是 `b.On.Click`）。

## 空白与文本语义（Chrome 同款）

- run 内连续空白塌缩成单空格；
- 纯空白 run 只在两个行内级兄弟之间保留；
- 块边界的空白丢弃；
- 容器的行内内容按文档序保真（文本 run 与子控件交错）——这是混排
  行盒的输入，生成代码的内容游走（EmitContent）与运行时装配同一顺序。

## 样式装载与优先级

装载链：UA 样式表 → skin → appCss。三条来源都并进 appCss 段：

1. `<style>` 块（任意位置，文档序拼接）；
2. `<link rel=stylesheet href=...>`（相对 baseDir）；
3. style 属性合成的 `.zgen-N` 规则（按文档序编号）。

级联、特异性、!important 全部复用引擎既有机制（`docs/WEB_GUI_ROADMAP.md`
P0-P4 节）。CSS 支持面（含 grid/flex/float）见 TASKS.md A16。

## 验证

- **oracle 闭环**：`python scripts/web_oracle.py tests/weboracle/html.json
  --compare <zan 输出> --tol 3`。fixture 的 css+body 单一来源（html.json），
  Chrome 侧 getBoundingClientRect 采集、Zan 侧
  `tests/weboracle/html_driver.zan`（运行时路径）按 selectors 数组序输出
  `#id x,y wxh` 行。当前 13 盒全绿（唯一非零是 #s1 的 x/w GDI 步进台账）。
- **同构断言**：`_scratch` 之外，css_test.zan 的 DisplayHtml 段断言
  解析/建树/事件接线/样式合并；conformance_gui_css golden 含 html 11 行。
- **端到端**：同一份 html 喂 GenHtml（UiHtml.Build）与运行时
  （LoadHtmlWith），13 盒输出 diff 全等。

## 设计器文档格式（P7a）

窗口设计器的存取格式就是本文件的 HTML 子集：文档 `<body>` 带裸属性
`data-zan-design`（区分设计稿与运行期 UI 文档——编译期前者进 GenForm 窗体
投影，后者进 GenHtml 建树类；IDE 里前者开设计器）。同一份 JSON 文档模型
（原 .zform）经 `System.Web.DesignerHtml` 与 HTML 互转，全键保真往返：

- 文档级键（winW/role/layoutMode/…）→ `<body>` 的 `data-<kebab>` 属性；
  `name` 另发 `<body id>`；裸属性即 `true`；`0`/`""`/`false` 不发（读端有
  缺省）。
- 字段 → 元素：`kind` 决定 tag（Panel/自定义→div、Label→label、Button→
  button、Input→input、TextArea→textarea、Image→img），`data-kind` 是权
  威标记；`name` → `id`、`class` → `class`、`style` → `style`、`kids` →
  子元素。
- 标量键 → `data-<kebab>`（camelCase kebab 化，读端还原）；数字/布尔自动
  嗅探。
- 事件键 `onClick` 等 → `data-on-click`（与运行时 data-on-* 协议同形）。
- 复合值（props 直通表、DataGrid columns、winShape）→
  `data-x-<kebab>` = 紧凑 JSON；字符串值不作数字嗅探（`"min":"0"` 不漂）。
- `options`（SelectBox 等）→ `data-options="a|b|c"`（`|` 分隔，与
  JoinOpts 同约定）。

设计器侧 API：`Designer.SaveHtml()` / `LoadHtmlText(text)`；IDE 的 .html
设计稿标签页（body 有标记）自动进设计器并回存 HTML。`.zform` JSON 仍作为
内部表示存在（撤销快照、JSON 抽屉、LSP 供数），不再手写。

## 字段内联 style（P7b）

字段的 `style` 键（设计稿里就是元素的 `style` 属性）是控件内联 CSS 声明，
Inspector 的 STYLE 区逐行编辑（`键: 值`，一行一条）。运行时通道：
`SetProp("style", ...)` → `StyleSheet.ApplyInline`——声明文本交给整块 CSS
解析器（颜色函数/缩写键/`!important` 全复用），布局键（pad/gap/width/
height/dock/x/y）落控件字段，视觉键经 `DeclSource`+`CopyToControl` 落
`style*` 覆盖字段（在每次样式解析的 Inline 覆盖之后，优先于类规则）。三个
消费端同落点：画布预览（FormBuilder.FromField → MakeControl）、生成代码
（GenForm 发射 `SetProp("style", ...)`）、运行期文档（UiDoc）。

**语义边界（台账）**：设计几何拥有布局——字段的 X/Y/宽/高（或流式跨距、
显式 pad/gap）由发射器以 `logW/Prefer/logPad` 在 style 之后接管，measure
期 `ApplyDeclaredUnits` 每帧重算，故 style 里的 width/height/x/y/dock/
gap/pad 对设计字段只在几何沉默处生效（如流式字段未声明 pad 时 style 的
padding 生效）。视觉键（background/color/border/border-radius/box-shadow/
font-size/transition）完全生效，不受几何影响。这是对浏览器 "inline 不败"
原则的刻意例外：设计画布的拖拽手柄、对齐命令都基于同一份几何。

## 与浏览器的差异清单（台账）

- **select 没有下拉语义**：映射 Element 占位（容器盒），选项当文本。
  真下拉待后续期（复用 SelectBox）。
- **行内样式是类级特异性**：style 属性合成 `.zgen-N` 规则参与级联，
  不是浏览器的 inline style 特异性（高于任何选择器）。与 id/类选择器
  的先后按"appCss 段内出现顺序"结算；`!important` 可覆盖。
- **忽略的属性**：href/title/disabled/alt 之外的 ARIA 等一律静默忽略；
  链接没有导航语义（`<a>` 是行内 Element）。
- **捕获控件的内嵌元素忽略**：`<button><span>x</span></button>` 的
  span 不建树，文本并入按钮标签。
- **事件模型是宿主委托**：没有 DOM 冒泡/捕获/.preventDefault——
  data-on-* 直连控件事件槽，一个名字一个 Action。
- **滚动条是覆盖式**：`overflow-y: auto/scroll` 出的滚动条画在内容
  上、不占布局宽（Chrome 经典条占 17px、出现/消失引起 reflow；
  oracle 用 --hide-scrollbars 对齐）。水平轴只裁剪不滚动；
  overflow:hidden 可程序滚动（SetScrollTop）但无滚轮/滚动条交互。
  详见 roadmap P6 节。
- **引擎级已知偏差**（P0-P4 遗留，对 HTML 层同样适用）：行内 run x
  累计 ±3px（GDI 整数步进）、行高分数取整逐行 ±1px、块级 strut
  font-size 不继承（HTML 层已给 body 显式字号的写法规避）、
  BFC 盒被祖先 float 挤窄未实现——详见 roadmap 台账节。
