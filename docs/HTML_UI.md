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
| data-arg="字面量" | 本控件全部 data-on-* 处理器的实参（见下） |
| data-bind="路径" | 绑定路径 bindPath（挂模型宿主每帧双向同步，见「动态原语」） |
| data-if="路径" | 真值插拔声明 bindIf（见「动态原语」） |
| data-for="路径" | `<template>` 行展开源数组（见「动态原语」；Element 上同时进属性表） |
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
focus/blur、keydown/keyup/keypress。未映射的后缀与未注册的名字不接线
也不崩溃，但**装载期各打一行 `HTML_WIRE_WARN` 警告**（未知后缀点名
控件；名字未注册点名事件与控件）——"点了没反应"从静默变成可发现的
事实。注意处理器在装载之后才注册的宿主（ChildWindow 二次接线）不受
影响：名字已落控件，那路命中照样接。

接线走 `Control.BindEvent`（多态）——Button 把 "Click" 路由到专属
`Click` 字段（与 `btn.Click += h` 同队列）。断言事件数时按控件实际
槽位查（`((Button)b).Click.Count()`，不是 `b.On.Click`）。

**带参事件 `data-arg`**：列表行/卡片上的同类按钮想共用一个处理器又
要区分来源时，`data-on-click="pick" data-arg="apple"` 把字面量
`apple` 作为实参传给带参条目。注册表挂带参槽：

```zan
h.AddArg("pick", (string arg) => { ... });   // 同名也可再 Add 无参版
```

语义要点：

- 实参是 **`data-arg` 属性值的创建时快照**（Wire 时闭包捕获），不是
  绑定路径——行身份走 `data-bind` 回写通道，`data-arg` 只表达
  "这个控件带着什么参数"这一静态事实；
- 名字命中带参槽 → 有参调用；只注册了无参 `Add` → 回落无参调用；
  都没注册 → 静默不接（同上）；
- 名字无论如何都落控件（`SetHandler`），ChildWindow 宿主可用
  `HandleArg("pick", ...)` + `Wire()` 二次解析——运行时 `Html.Parse`
  与编译期 `GenHtml`（发射 `handlerArg` 字段 + `Html.WireArg`）同一
  语义；`<template data-for>` 克隆行保留声明，展开后逐行接线共享
  原型上的同一实参；
- 设计器通道同样建模（2026-09-15 起）：DesignerHtml 编解码把模型
  `"arg"` 键折成 `data-arg` 属性（与运行时协议同键同名）、
  FieldFromJson 落 `handlerArg`、GenForm 编译期发射
  `handlerArg` 字段——.html 设计稿里写
  `data-on-click="pick" data-arg="apple"` 全链路语义一致。

## 动态原语：data-if 与 `<template data-for>`（P8）

HTML 是声明，没有循环和条件表达式；这两条原语把"逐项渲染列表、
按状态显隐"也声明化，**语义由挂了模型的宿主消费**（与 bindPath
同一契约：`ChildWindow.SetRoot(tree, model)` 挂上的 JsonValue 状态
实体，每帧双向同步——机制与边界见下节）——无模型的宿主（纯静态
UI、设计器画布、主窗口 `LoadHtmlWith` 装载的树）一律惰性。

```html
<ul class="cart">
  <li data-bind="count" data-if="hasItems"></li>
</ul>
<template data-for="items">
  <li><span data-bind="name"></span>
      <input data-bind="qty" data-if="editable" /></li>
</template>
```

**data-if → `Control.bindIf`**：挂模型的 ChildWindow 每帧取
路径真值调 `SetShown`（同步节奏见下节「双向同步怎么跑」）。真值
裁决（`ChildWindow.Truthy`）：null（缺路径）→ 假；布尔原样；数字
非 0；字符串非空且 ≠ "false"（比 `AsBool` 的严格 "true" 宽——
路径值多是字符串状态名）。

**`<template data-for>` 行展开**：模板原型被 UA 样式表
`template { display: none; }` 隐藏（Chrome 语义）；ChildWindow 在
`SyncFromModel` 开头核对源数组长度，与上帧一致就什么都不做（行是
活控件，绑定照常同步），变了才整组重建——逐**原型子项**
`Html.Clone` 成行，插在模板紧后。行内 bind/bindIf 以本项 JsonValue
为第一作用域（项内命中优先、回落根模型，相对路径 "name" 与绝对路径
"vm.title" 可在同一行混用），回写同样先落本项（数组元素的引用，
就地生效）。

**Element 的缺省绑定属性是 text**：行里 `<span data-bind="name">`
由 ChildWindow 按 `SetText` 通道推送/读回（真控件缺省是 value）。

**三条通道同语义**（同一份文档在运行时装载与编译期生成行为一致，
conformance 三用例互为镜像）：

| 通道 | data-if | data-for |
|------|---------|----------|
| 运行时 `App.LoadHtml` | 属性协议 → bindIf | Element 属性表 |
| 编译期 `GenHtml`（非设计稿 .html） | → bindIf | Element 分支 parity `SetAttr("data-for")` |
| 设计稿 `GenForm` / 设计器 | JSON `"if"` 键 → bindIf | JSON `"for"` 键 → `SetAttr("data-for")` |

设计器存取格式里模板字段的形态（"tag"/"for"/"if" 都不在
IsModeledKey 名单，Inspector 经 extra 原样透传保真）：

```json
{ "kind": "Element", "tag": "template", "for": "items", "name": "Rows",
  "kids": [ { "kind": "Element", "tag": "li", ... } ] }
```

**语义边界（台账）**：
- 无模型宿主：data-if 惒性恒显示，模板不展开（原型隐藏）；
  挂模型后缺路径 = 假（隐藏）。
- 嵌套模板 v1 不支持（原型子树里的模板不登记、随行原样克隆但
  不展开）；原型子树不参与同步/回写。
- 模板在 Element 父里应为**最后一个子项**：行经普通 InsertAt
  插入（不进 Element 文档序表），流布局下行渲染在该父全部已
  跟踪内容之后；Panel 等普通容器父无此限制。
- 行克隆沿用原型的 `Class`：HTML 通道的行内 style 经 zgen-N 类
  规则对克隆同样生效；设计器/FormBuilder 通道里经
  `SetProp("style")` 落实例字段（ApplyInline）的内联声明克隆
  不带走。
- 撤行走 Element 父的 `DropKid`（文档序表一并清），不留幽灵
  占位；行作用域登记随撤行清空（数组频繁重建不涨表）。

**双向同步怎么跑（每帧两趟，无事件订阅）**：挂模型宿主
（`ChildWindow.SetRoot(tree, model)`，模型是 JsonValue 状态实体）由
渲染循环驱动两个方向——

- **模型 → UI（帧前 `SyncFromModel`）**：`bindIf` 路径真值调
  `SetShown`；`bindPath` 路径值经缺省绑定属性写控件（真控件 `value`、
  Element `text`，`bindProp` 可指定），**值变才写**——无条件写会把
  文本框光标弹回行尾。代码改模型（`Set`/`PathSet`/数组 `Append`）后
  `RequestRedraw()` 出一帧即生效；模型改了但窗口静止，屏幕不会自己动。
- **UI → 模型（帧后 `SyncChangedNode`）**：控件当前值与快照（模型侧
  上次确认值）不同才回写；按叶子原类型折回 bool/number/string；行
  作用域控件写回本项 JsonValue（数组元素引用，就地生效）。**回写没有
  来源过滤**：绑定控件只要值变了就落模型，程序化 `SetProp` 与用户
  编辑同待遇。因此"让 UI 显示新值"的正向姿势是改模型；既改模型又
  `SetProp` 同一控件是两个写入源打架（帧后回写可能覆盖模型改动），
  别混用。
- **绑的是值槽不是结构**：`data-bind` 同步的是标量投影；对象/数组
  的结构变化只有 `<template data-for>` 的长度对账一条路（整组重建，
  无 keyed diff、无"改第 N 行"助手）——列表内容的局部高频更新不是
  这条通道的设计场景。

**动态操作选路：改模型，还是改树**。"程序运行中改变界面"两条路按
变化类型选：

- **值/显隐/列表内容 → 改模型（声明式）**：挂模型宿主里全是赋值——
  `model.Set("title", ...)` 改 data-bind 文本、`Set("showBag", ...)`
  翻 data-if、`Set("items", ...)` 重刷 data-for 行，末尾一句
  `RequestRedraw()`。语义正向（帧前推送）、无树重建。
- **结构 → 命令式树操作**：增删控件/换组件类型没有声明通道，走
  retained 树 API（`Find` 定位 + `Add`/`InsertAt`/`Remove`/
  `SetProp`）；整页换装 `app.LoadHtmlWith(newHtml, ...)`。已知代价
  （台账）：① `LoadHtmlWith` 是**整树重载**，没有 diff——旧树上
  手工 Add 的控件不在新文档里，频繁局部刷新留在模型通道；② 命令式
  建树的行内 style 只吃视觉键，布局键（width/gap/pad…）要走类规则
  或 log* 声明字段（见「字段内联 style」的几何拥有权边界）；③ 无
  行级更新助手，"改第 N 行"要么整组重建要么自己定位行内控件。
- **主窗口的树没有模型**：`App.LoadHtmlWith` 装在主窗口的树不走上述
  两趟同步——bindPath/bindIf/data-for 全部惰性（data-bind 落了字段
  也没人消费）。动态绑定内容放进 ChildWindow 宿主（`SetRoot(tree,
  model)`）；主窗口树上改内容就是普通命令式 `SetProp`/树操作。

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
设计稿标签页（body 有标记）自动进设计器并回存 HTML。JSON 文档模型仍是
内部表示（撤销快照、JSON 抽屉、LSP 供数），不落盘、不手写。

P7d 起全仓库窗口声明只有 .html 一种形态：templates/gui 12 份与 IDE 自用
31 份设计稿均已迁移（模型级等价校验），模板/新建文件/编译发现全部以
.html 为入口。P8-4 起 .zform 编译通道删除：zanc 对 .zform 输入报定向
错误（"the legacy .zform design format is no longer supported"），旧项目
把设计文档转成 .html（`DesignerHtml.FromJsonDoc` 是规范转换器）后编译；
GenForm 只认 .html 设计稿，`.zscene`（场景）与 `.zcomp`（用户组件）不受
影响。

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

- **select 有真下拉语义**：`<select><option>` 运行时映射 `SelectBox`
  （option 文本折 `a|b|c` options、`selected` 属性定选中下标、无
  selected 显示首项），与设计稿通道（`SelectBox→select` 往返）同形；
  change 经 `data-on-change` 接 Change 槽。多选/级联/表格式下拉仍是
  纯代码形态。
- **行内样式是类级特异性**：style 属性合成 `.zgen-N` 规则参与级联，
  不是浏览器的 inline style 特异性（高于任何选择器）。与 id/类选择器
  的先后按"appCss 段内出现顺序"结算；`!important` 可覆盖。
- **忽略的属性**：ARIA 等 a11y 属性一律静默忽略。href/target 是例外
  ——`<a>` 有缺省导航语义（见下条）；`title` 也是例外——悬停提示
  （见下下条）。
- **链接导航是桌面映射**：`<a href="http(s)://...">` 建树时由
  `Html.AutoLink` 接上缺省 Click 导航——缺省路由到内嵌 WebView
  （宿主 `App.UseWebview(box)` 注册的就地图优先，没有则懒建一个
  App 级链接窗口 `LinkWindow`，全程复用、页面标题同步到窗口标题）；
  `target="_blank"`（新弹窗）弹系统浏览器（Windows
  ShellExecuteW / POSIX xdg-open+open，URL 引号转义防注入）。WebView
  运行时不可用（缺 WebView2/WKWebView）时首帧回落系统浏览器。
  三个接线入口：`App.LoadHtmlWith` 装载扫描、`ChildWindow.WireNode`
  二次接线（模板行克隆也生效）、挂主窗口的生成/手搭树由宿主
  `Html.AutoLinkTree(root, app)` 扫一遍。边界：已有 data-on-click
  即宿主接管不叠加（幂等）；href 只认 http/https——相对路径（无
  base 可解析）、`#` 锚（保留树无滚动目标）、`javascript:`/`mailto:`
  等其它 scheme（脚本执行/shell 关联程序）一律不路由；`data-for`
  行克隆走 WireNode 补接，语义与浏览器差异见上。UA 样式表给
  `a { color: var(--primary) }` 示能（无 underline 绘制原语，
  retained 模式也没有 cursor 消费点——见 TASKS.md A47）。
- **title 是悬停提示（tooltip）**：Element 记 `title` 文本，渲染期
  `Html.PaintTip` 轮询——指针在元素上停稳 500ms（Tooltip
  DefaultDelayMs）即向帧末提示队列登记，气泡贴元素底边、260px
  自动换行；指针挪走或悬停目标一变即消失（计时在
  `focus.hoverStartMs`，随 SetHovered 目标变化重置）。前提是元素
  有命中区（链接/带 data-on-click 的元素天然有；纯展示容器无人
  监听时不给指针，也就无提示）。真控件（button/input/...）的
  title 忽略——不进控件属性面；GenHtml 编译通道对 Element 发
  `elTitle`，双通道同形。与浏览器差异：气泡是 Zan 皮肤样式而非
  系统原生；`title` 不承担浏览器里的其它兜底语义（如 img 替换
  文本）。
- **捕获控件的内嵌元素忽略**：`<button><span>x</span></button>` 的
  span 不建树，文本并入按钮标签。
- **事件模型是宿主委托**：没有 DOM 冒泡/捕获/.preventDefault——
  data-on-* 直连控件事件槽，一个名字一个 Action。data-arg 是宿主侧
  的静态实参快照，不是 DOM data-* 属性（浏览器打开无事件语义）。
- **滚动条是覆盖式**：`overflow-y / overflow-x` 的 auto/scroll 出的
  滚动条画在内容上、不占布局宽（Chrome 经典条占 17px、出现/消失
  引起 reflow；oracle 用 --hide-scrollbars 对齐）。水平轴与纵向同
  语义：程序性 SetScrollLeft/ScrollLeft/ScrollExtentX（scrollWidth）、
  shift+滚轮横滚、横向滚动条（贴容器底边）；overflow:hidden 可程序
  滚动（SetScrollTop/SetScrollLeft）但无滚轮/滚动条交互。
  详见 roadmap P6 节。
- **动态原语是宿主语义不是浏览器语义**：data-if/data-for 只在挂
  JsonValue 模型的宿主里生效（见「动态原语」），浏览器打开同一份
  文件时 template 内容本就不渲染，data-if 只是未知属性。
- **引擎级已知偏差**（P0-P4 遗留，对 HTML 层同样适用）：行内 run x
  累计 ±3px（GDI 整数步进）、行高分数取整逐行 ±1px、块级 strut
  font-size 不继承（HTML 层已给 body 显式字号的写法规避）、
  BFC 盒被祖先 float 挤窄未实现——详见 roadmap 台账节。
