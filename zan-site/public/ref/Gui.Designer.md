# Gui.Designer

> 源码: `stdlib/Gui/Designer/DesignExport.zan`, `stdlib/Gui/Designer/Designer.Form.zan`, `stdlib/Gui/Designer/Designer.Html.zan`, `stdlib/Gui/Designer/Designer.Inspector.zan`, `stdlib/Gui/Designer/Designer.zan`


## DesignExport (class)

控件树 → 设计文档导出器（WEB_GUI_ROADMAP P8）：把运行期/代码建的
保留控件树反向序列化成设计器 JSON 文档模型（再经
`DesignerHtml.FromJsonDoc` 得到 .html 设计稿），让存量手搭界面
一次性搬进声明层——之后设计器可打开编辑、CSS 皮肤按 class 生效。

映射规则（与 GenForm.EmitField 的消费端一一对应，读写对称）：
- kind ← `Control.Kind()`；Element 例外——裸标签名（div/span）
不是合法控件 kind，固定导出 kind="Element"，真实标签进 "tag"
键、纯文本内容进 "text" 键（装载端 InitElement/SetText 还原）；
- name/class/style ← 同名属性（style 反向自 ApplyInline 落下的
style* 字段重组，只发与缺省不同的键）；
- dock/fx/fy/fw/fh ← dock 与 logX/logY/logW/logH（logPlaceSet
为假时不发几何，装载端按内容测量）；
- label ← `GetProp("text")`（容器没有 text，发空串即跳过）；
- options ← Props() 里 kind==3（枚举）spec 的选项表；
- on<Event> ← events 槽（SetHandler 存的处理器名）；
- bind/bindProp ← bindPath/bindProp；
- children ← kids 递归（Element 的文档序混合内容里，文本 run
并入元素文本，子控件仍是 kids）。

导出是**有损有界**的：只保结构/几何/样式/事件名，回调 Action、
signal 订阅、运行期动态状态不在文档模型里（本来也存不进声明层）。
文档级键（winW/winTitle/layoutMode…）由调用方补：`ExportDoc` 的
覆载接收一个现成的文档头。

- static string Export(Control root, string name, int winW, int winH)
  - 控件树 → 设计文档 JSON（紧凑 PrettyToJson）。`name` 是文档名
    （生成 partial class 名），`winW`/`winH` 是窗口文档尺寸——
    传 role="control" 的文档头可用零值，本方法不填窗口键。

- static string ExportHtml(Control root, string name, int winW, int winH)
  - 控件树 → .html 设计稿文本（Export + FromJsonDoc 的组合捷径）。

- static JsonValue NodeJson(Control c)
  - 单个控件 → 字段对象。容器递归 kids；Element 的纯文本内容进
    "text" 键（装载端 InitElement 后 SetText 还原 elText）。

- static string StyleJson(Control c)
  - 反向重组内联 style 文本：只发 ApplyInline 会落控件的视觉键且
    值非缺省。样式层是单向解析（原文不存），这里按 style* 字段
    的现值重建，顺序固定——装载端 ApplyInline 语义与原声明等价。

- static JsonValue EnumProps(Control c)
  - 控件发布且已绑定的枚举属性（kind==3）：当前下标 → 选项名。
    未绑定或空选项表的 spec 跳过（那些是派生键，导不出快照）。


## Designer (class)

设计器部分：表单画布——行/容器渲染、拖放
放置和实时控件预览。

- int RibbonH(App app)
  - 设计窗口顶部的功能区：不分标签页，命名分组一次
    全部铺开（排版 / 层次 / 网格 / 缩放 / 视图），命令
    一律是小图标 + 一行标签、每组两行堆叠，这样一屏
    放得下又不占画布高度。命令的登记顺序必须与 acts
    一一对应：Ribbon 返回的是被点命令的全局序号。

- void RenderRibbon(App app, Canvas c, Theme t, int x, int y, int w)
  - 渲染窗口顶部的功能区：按当前选择与布局模式组装五组命令并
    控制可用性，命令排完剩下的空档够时在右端补只读状态（布局
    模型 / 设计尺寸 / 缩放），命中的动作码交给 RunRibbon 执行。

- void RunRibbon(App app, string act)
  - 执行功能区命令。动作码与 RenderRibbon 里登记的
    acts 一一对应。

- void ZoomStep(int dir)
  - 放大/缩小一级（10%），钳制在 25%–200%。从「适应」
    起步时以上一帧实际算出的适应比例为基准，缩放是连续的。

- List<int> ZoomPresets()
  - 缩放档位（千分比，1000 = 100%），0 = 适应视口，与
    缩放下拉菜单一一对应。

- List<string> ZoomMenuLabels()
  - 缩放下拉菜单的显示标签（"适应"、"25%" .. "200%"）。

- void AlignSelGroup(int mode)
  - 把选中组件对齐到它所在的框（顶层为设计窗口，容器子元素
    为容器的内容框）：0-2 左/水平居中/右，3-5 上/垂直居中/下。
    对整个多选做对齐：只有一个选中项时就是它自己
    对齐到所属框；多选时所有成员对齐到同一条边
    （主选中项所在框的边）。

- void AlignSel(int mode)
  - 把单个字段对齐到所属框（父容器的内容框，顶层为设计窗口）：
    mode 0-2 左/水平居中/右，3-5 上/垂直居中/下，坐标钳制非负。

- void RenderForm(App app, Canvas c, Theme t, int cx, int cy, int cw, int ch)
  - 表单（自动吸附）和自由窗口共用同一块设计画布：抓手平移、
    缩放、框选、多选完全一致，区别只有组件坐标从哪来——流式
    布局每帧自动排版算出来，自由布局由拖放决定。

- int FlowTop()
  - 设计窗口内容区的上边（留出表单标题）。

- int LayoutFlow(int dw)
  - 重算整个流式布局，返回内容底边（设计空间）。

- int LayoutFlowList(List<FormField> lst, int x0, int y0, int w0, int tabFilter)
  - 把一列字段按 24 列打包成行并写入设计空间矩形。
    tabFilter >= 0 时只排布该标签页下的子级。返回底边。

- int KidInsetX(FormField f)
  - 容器内容框相对容器矩形的偏移（设计空间）。自由画布上
    运行时给容器 Pad(0)、子级按自己的停靠边铺满整块，所以只有
    Tabs 需要让出标签条（横排让高 26，纵排让宽 TabRailW）；
    再多让 6 就是设计器比运行时缩一截、子级整体偏移的来源。
    流式布局一致。

- int KidInsetY(FormField f)
  - KidInsetX 的纵向对应：Tabs 横排让出标签条（26），纵排只剩
    装饰边距（6），自由画布为 0。

- static int TabRailW()
  - 纵向 Tabs 左侧轨道宽（设计像素，运行时 trackW 的未缩放宽）。

- int FlowFieldH(FormField f)
  - 一个字段在流式布局里的高度（宽度须已确定）。容器
    顺带排布自己的子级。

- int LayoutFlowKids(FormField f)
  - 排布容器的子级（坐标相对容器内容框，与 RenderFreeField
    的 +6 / +26 偏移一致），返回内容框高度。

- void FlowDropTarget(App app, Canvas c, Theme t, int ox, int oy, int psMilli, int dw)
  - 流式模式的放置目标：指针下的容器（放入），否则顶层
    插入位置。顺带画出容器高亮 / 插入线。

- FormField TopLevelOf(FormField f)
  - 某个字段所属的顶层字段（自身或最外层容器）。

- void DropField()
  - 把待处理的拖放落到表单上：面板拖拽在 dropIndex 插入新字段；
    表单内拖拽把被拖的行移动到那里。

- void ApplyDesignSize(int newW, int newH)
  - 把设计窗口切换到新尺寸。一份设计只有一块画板：
    改宽高就是改这块画板，组件矩形原样保留，不再按
    "WxH" 存多套断点布局（那会让同一个设计出现好几个
    版本，而且改回尺寸时画布和设置对不上）。

- void ApplyDockLayout(List<FormField> lst, int ax, int ay, int aw, int ah)
  - 停靠布局：设了停靠边的组件在所在层里自动吸边——按顺序
    从剩余矩形上切走一条（厚度沿用组件自己的宽/高），Fill
    铺满剩下的全部；停靠为「自由」(0) 的组件不参与，保持
    绝对定位。每帧算一遍并写入运行时矩形，画布和命中测试
    看到解算结果，而保存的 JSON 仍只看作者矩形。

- void ApplyDockNested(List<FormField> lst)
  - 对每个容器的内容框再停靠一层（与 RenderFreeField 的
    +6 / +26 内边距一致）。标签页容器的每一页各自为一层：
    否则看不见的页里的停靠组件会吃掉当前页的空间，
    当前页的 Fill 就只剩 0。

- void ApplyDockLayer(List<FormField> lst, int ax, int ay, int aw, int ah, int tabFilter)
  - 把一层停靠组件排进 (ax, ay, aw, ah)。`tabFilter >= 0` 时
    只排该标签页下的子级。两遍式与运行时 Control.Arrange 同构：
    第一遍四边停靠按子序切剩余矩形，第二遍 Fill 统一铺最终的
    剩余矩形——Fill 若内联在切边途中取矩形，会盖住排在其后
    的边停靠条（运行时不会），画布和发布窗体从此不一致。

- void RenderFreeField(App app, Canvas c, Theme t, FormField f, int ox, int oy, int psMilli, int px, int py)
  - 自由画布元素的设计空间 -> 屏幕空间转换（子节点已在 px/py 中
    累积了父内容框的偏移）。

- static int TabPageCount(FormField f)
  - Tabs 容器的页数：没写选项时仍算一页，容器里总有地方放组件。

- static int TabActivePage(FormField f)
  - Tabs 容器当前活动页的序号，钳制到有效范围。

- static string TabPageTitle(FormField f, int i)
  - 第 i 页的页签标题（没写选项时是 "Tab i"）。

- void RenderTabStrip(App app, Canvas c, Theme t, FormField f, int ex, int ey, int ew, int eh, int psMilli, int active)
  - 画 Tabs 容器的标签条（跟随画布缩放），并把每个页签的屏幕
    矩形登记到 tabHits，供画布的指针处理切页。纵向（tabOrient
    == 1）画成左侧轨道：整列页签、活动项左缘高亮，与运行时
    RenderVertical 同构。

- TabHeaderHit PickTabHeader(int mx, int my)
  - 指针位置上的页签（后画的在上层），没有则 null。

- FormField FreePick(List<FormField> lst, int mx, int my, int px, int py)
  - 设计空间 (mx, my) 处最上层的字段，从后往前搜索 `lst`，
    后面的兄弟节点优先；子节点优先于其容器。父节点的
    内容框原点存入 freeSelPX/freeSelPY，供拖拽使用。

- FormField FreePickIn(List<FormField> lst, int mx, int my, int px, int py, int tabFilter)
  - `tabFilter >= 0` 时只看该标签页下的子级——看不见的页里的
    组件也点不中。

- int AbsOriginX(FormField f)
  - `f` 的父内容框在设计空间的绝对原点：自由画布子级的 fx/fy
    是父相对坐标，拖拽基必须能按父链随时重算——框选、撤销、
    换父之后 freeSelPX/freeSelPY 里存的旧值都可能是错的。

- int AbsOriginY(FormField f)

- void SyncFreeSelBase()
  - 按当前 sel 重算拖拽基（父内容框原点）。基过去只在
    FreePick 直接命中时顺带写入——框选、撤销或代码置选中之后
    基还是旧值，嵌套子级一拖就按画布原点算，组件“瞬移”出容器。

- bool IsAncestorOf(FormField anc, FormField f)
  - `anc` 是否是 `f` 的严格祖先容器。

- int ContOriginX(FormField cont)
  - `cont` 的内容框在设计空间的绝对原点——它的子级 fx/fy 的
    参考基（cont == null 不适用，调用方自取 0）。

- int ContOriginY(FormField cont)

- void FreeReparentSel(FormField cont)
  - 自由画布拖拽收尾：把选中组件换父到 `cont`（null = 顶层），
    拖进/拖出容器在松手这一刻生效。fx/fy 按新旧父内容框基
    换算，组件的视觉位置不动；落进 Tabs/SplitPanel 时进正在
    看的那页，与流式 DropField 同约定。

- void DrawFreeDropHi(App app, Canvas c, int ox, int oy, int psMilli, FormField hi)
  - 自由画布拖拽落点预告：给容器 `hi`（FreeDropContainer 解析
    的结果，null = 无）画描边，提示松手会放进哪个容器。

- void MarqueeCollect(List<FormField> lst, int px, int py, int selL, int selT, int selR, int selB, List<FormField> got)
  - 框选收集：递归收集与设计空间矩形相交的可见组件（含容器
    内的子组件）。px/py 为父链累计原点；Tabs 只看当前显示的
    标签页——看不见的页里的组件也框不中，与点选一致。

- bool HasAncestorIn(FormField f, List<FormField> got)
  - `f` 的某个祖先是否也在 `got` 里——框选时容器和它的子级
    同时相交只留容器：拖动父级本来就带着子级，两组一起选
    会把子级移两次。

- static int FreeHandleX(int hn, int ex, int ew)
  - 第 hn 号调整大小手柄（0 左上 1 上 2 右上 3 右 4 右下 5 下
    6 左下 7 左）的 X：右手柄贴右边、左手柄贴左边，上/下手柄取中线。

- static int FreeHandleY(int hn, int ey, int eh)
  - 第 hn 号手柄的 Y：上手柄（0/1/2）贴顶边、下手柄（4/5/6）
    贴底边，左/右手柄取中线。

- int FreeSnapTol(App app, int psMilli)
  - 磁吸阈值：屏幕上约 6px 换算回设计像素，于是任何显示/视图缩放下手感一致
    （放大后要贴得更近才吸，缩小后更远就吸），而不是写死设计像素。

- static List<int> FreeSnapLines(bool horiz, List<FormField> sibs, FormField self, int span)
  - 一根轴上的候选对齐线：画布的起点/中点/终点，加上除自己外每个可见字段的
    起点/中点/终点。X 轴传 horiz=true，`span` 是该轴上画布的尺寸。静态是为了
    无窗口回归能直接喂一组字段进来。

- static List<int> SnapAxis(int start, int size, List<int> lines, int tol)
  - 一根轴上的磁吸：把移动元素的起点/中点/终点三个锚点吸到最近的候选线，
    取三者里最近的那一次命中，所以「贴边、居中、和另一个控件对齐」都能触发。
    
    返回两个值：[0] = 校正后的起点（没命中就是原值），[1] = 命中的参考线坐标
    （-1 = 没命中，调用方据此决定画不画线）。纯几何、无 App，方便无窗口回归。

- int FreeSnapX(App app, int newLeft, int w, int dw, FormField self, int psMilli)
  - 横向磁吸：命中则写 freeGuideX 参考线，返回校正后的左边。

- int FreeSnapY(App app, int newTop, int h, int dh, FormField self, int psMilli)
  - FreeSnapX 的纵向对应，写 freeGuideY。

- void RenderDesignCanvas(App app, Canvas c, Theme t, int cx, int cy, int cw, int ch)
  - 设计画布：信箱式窗口 + 网格 + 字段 + 场景设计器那套交互
    模型（选择 / 拖拽 / 框选 / 抓手平移 / 缩放 / 面板放置）。
    表单（自动吸附）和自由窗口共用这一份实现。

- FormField FreeDropContainer(int dx, int dy)
  - 自由画布上 (dx, dy) 落入的容器，其内容框原点写入
    freeDropPX/freeDropPY；null 表示顶层表单。指到已有组件上
    时取它所在的容器，于是拖到标签页内任何地方都是
    「加到正看着的这一页」。

- void PlaceFree(FormField f, FormField cont, int lx, int ly)
  - 把新组件以容器内容框坐标 (lx, ly) 为中心放进 cont
    （null = 顶层）；标签页容器落在正看着的那一页。

- void DropFreePalette(int dx, int dy)
  - 在设计空间 (dx, dy) 处把面板字段添加到自由画布，
    新元素以光标为中心；光标下有容器则成为它的子级。

- static int DefaultFreeW(FormField f)
  - 新拖入自由画布组件的默认宽度：依据各控件标准尺寸与内容度量确定，
    确保点选、选中框手柄与实际呈现区域严丝合缝。

- static int DefaultFreeH(FormField f)
  - 新拖入自由画布组件的默认高度：依据各控件标准尺寸与内容度量确定。

- void DrawLabel(App app, Canvas c, Theme t, FormField f, int lx, int ly, int lw, int align)
  - 画字段的标签文本：必填字段前置红色星号（hideStar 时省略），
    align 为 1 时右对齐到给定宽度，其余左对齐。

- int PvS(App app, int v)
  - 预览度量换算：把 v 按画布缩放（pvScale 千分比）折算成屏幕
    像素；已夹在 BeginCtrlZoom 里时度量自带缩放，直接走 app.Scale。

- int PvFont(int fsz)
  - PvS 的字号版：乘 pvScale（BeginCtrlZoom 里跳过）并钳制下限 7。

- void ApplyPreviewFormSize(Control ctl, int size)
  - 表单级尺寸逐节点补给没有显式 tiny/small/medium/large 的控件；
    容器自己显式定档不应阻断后代，与 GenForm 的递归生成规则一致。

- void PreviewControl(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail)
  - 绘制字段输入控件的非交互预览。

- void PreviewDisplay(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail, int line)
  - 为显示/布局/反馈/
    导航组件（ftype >= 15）绘制非交互预览。每个分支绘制对应 stdlib 控件的
    可辨识的缩略图。

- void PvCtrlAuto(App app, Control ctl, int x, int y, int w, int h)
  - 在设计画布上按给定矩形绘制一个保留式控件的实时预览。
    用 BeginCtrlZoom/EndCtrlZoom 夹住渲染：真控件内部只认 app.dpiScale
    和主题度量，本身不知道画布视图缩放，因此临时把两者按视图缩放放大/
    缩小，控件的字号、内边距就随画布一起等比缩放（放大不再「有的变有的
    不变」）。给定矩形 x/y/w/h 已是缩放后的屏幕坐标，渲染完精确还原。
    与 PvCtrl 相同，但高度取控件自己测量出来的（不小于 `h`）：
    换行的容器在设计器里也能完整显示。

- void PvCtrl(App app, Control ctl, int x, int y, int w, int h)
  - 在给定矩形里渲染一个保留式控件的实时预览：量测、按给定矩形
    排布、渲染，全程夹在 BeginCtrlZoom/EndCtrlZoom 里随画布等比
    缩放（PvCtrlAuto 是高度按测量结果重排的版本）。

- void PvCtrlPrepared(App app, Control ctl, int x, int y, int w, int h)
  - 已按自己的文档应用过尺寸默认值的组件树只负责渲染，不能再被
    宿主设计稿的 formSize 覆盖。

- void BeginCtrlZoom(App app)
  - 进入真控件等比缩放：按视图缩放（winZoom，50%–200%）临时放大/缩小
    app.dpiScale 和主题度量。100% 时为恒等变换，真控件保持 1:1 清晰。

- void EndCtrlZoom(App app)
  - 退出真控件等比缩放：从基线精确还原主题度量与 app.dpiScale。

- bool PvBasic(App app, FormField f, int x, int y, int w, int avail, int line)
  - 基础输入控件（输入/多行/密码/数字/下拉/开关/评分/滑块）用
    FormBuilder 构造的真控件渲染预览，取代仿画，使设计与运行完全
    一致。这些类型不把标题嵌进控件（标题由 DrawLabel 单独绘制），
    故不会重复。返回 true 表示已处理。

- static string ChartKindAlias(int k)
  - 旧 uiState 图型序号（0 柱状 .. 7 散点）到 SampleOf 别名
    字符串的映射（存量设计稿的直通行还没有 "type" 时回退用）。

- void PreviewChart(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail)
  - Chart 组件的画布预览。类型优先取直通行（extra["props"] 的
    "type"，ECharts 注册表字符串/别名，经 ChartHost.SampleOf 现拼
    示例——与生成代码 ChartHost.SetProp("type") 同一份示例数据，
    设计器所见即发布窗体初见）；旧设计稿类型存 uiState
    （0 柱状 .. 7 散点），回退映射。

- void PreviewTable(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int avail)
  - 渲染表格字段的紧凑非交互预览：由字段选项（其列）构建的
    表头行加上几行占位行。

- void PreviewChoice(App app, Canvas c, Theme t, FormField f, int x, int y, int w, int line)
  - 水平渲染单选/复选字段选项的预览。

- void Box(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 画预览里单行输入框的底：圆角填充 + 边框，各仿画控件共用。


## Designer (class)

设计器部分：HTML 存取格式（WEB_GUI_ROADMAP P7）——设计文档的
存盘/打开格式就是 .html（JSON 文档模型退役为内部表示）。外皮只有
这两个方法：内部模型、Undo/Redo 快照与 JSON 抽屉仍是文档 JSON。

- string SaveHtml()
  - 当前设计 → HTML 文本（存盘格式）：文档级键落在 <body> 的
    data-* 属性上，字段树变元素树（kind 存 data-kind，与运行期
    data-on-* 事件协议同形）。

- void LoadHtmlText(string s)
  - HTML 文本 → 设计模型（打开 .html 设计稿）。带 data-zan-design
    标记的文档逐键还原；普通 HTML 也能导入（tag 兜底 kind）。
    解析失败走 loadError/lastError（宿主提示），模型保持清空。


## Designer (class)

设计器部分：属性检查器（逐字段编辑器、双向
同步）和设计 JSON 序列化。

- void RenderInspector(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 渲染右侧检查器面板：目标名 + 属性/事件选项卡对，可滚动主体
    按选项卡分派到 RenderEventsTab / RenderWindowProps /
    RenderFieldProps（滚轮滚动，内容高度取上一帧测量值）。

- void SyncEditors(FormField f)
  - 选区变化时重置选项 / 事件编辑器（普通属性行直接
    绑定字段，不需要手动同步）。

- static string SuggestHandler(FormField f, string ev)
  - 为事件建议委托方法名，例如字段 "amount" +
    "Change" -> "OnAmountChange"。类型名中的空格会被去掉，
    显示组件（如 "Data Table"）仍能产生合法标识符。

- List<PropSpec> FieldSpecs(FormField f)
  - 选中组件的属性列表：只声明要编辑哪些字段，
    行的标签 / 编辑器 / 滚动全由 PropertyGrid 完成。

- int RenderFieldProps(App app, Canvas c, Theme t, FormField f, int ix, int iy, int iw)
  - 选中组件的属性选项卡（不含事件——它们在事件选项卡）。
    属性行交给 PropertyGrid，这里只处理绑定选择器、选项表
    这些语义动作。返回最后一行之后的 y，供主体滚动。

- void RemoveOptionFixup(FormField f, int oi)
  - 从 `f` 中移除选项 `oi` 并修复依赖状态，
    UI 永远不会索引超出缩小后的列表（旧的直接 RemoveOptionAt 可能
    让 Tabs 容器的活动标签/子标签超出范围）。

- static string ColTitle(string opt)
  - 表格列选项的显示标题（"标题:字段"约定）。

- static string ColField(string opt)
  - 表格列选项的数据字段键（未声明时为 ""）。

- static int IndexOfColon(string s)
  - s 中第一个冒号的下标，没有则 -1（"标题:字段" 约定的分隔符）。

- static List<string> SplitDecls(string s)
  - 内联声明文本按 ";" 拆成声明行（STYLE 编辑器逐行显示用）：
    去首尾空白、丢空行。v1 限制：值里带分号（如 content:";"）
    会被拆开——设计期样式里极罕见，遇上的手工编辑 JSON 画布。

- List<PropSpec> WindowSpecs()
  - 根 Window 的属性列表：窗口装饰（标题 / 尺寸 / 标志）以及
    应用于其内带标签输入行的共享布局默认值。

- int RenderWindowProps(App app, Canvas c, Theme t, int ix, int iy, int iw)
  - 根 Window 的属性选项卡：属性行交给 PropertyGrid，
    这里只留「保存为组件」和自定义形状的只读提示。

- void ApplyDevice(DeviceProfile dp)
  - 应用目标设备画像选择（属性面板 device 行的差量落点）：
    锁定类设备把画布切到画像视口（允许横竖屏时保持当前
    方向）并关掉 resizable——移动端运行时窗口就是整块表面；
    桌面恢复自由尺寸（device 清空，即文档缺省）。

- int RenderEventsTab(App app, Canvas c, Theme t, int ix, int iy, int iw)
  - 事件选项卡：当前目标（窗口根或选中组件）的所有事件，
    按类别分组（Pointer / Keyboard / Focus / Drag /
    Lifecycle / Value），可滚动，带行内处理器编辑器。

- static List<string> WindowEventNames()
  - 根 Window 的可设计事件（生命周期/尺寸/焦点/键盘）。

- string WinGetHandler(string ev)
  - 根 Window 的事件 ev 已绑定的处理器名，没绑过为 ""。

- void WinSetHandler(string ev, string h)
  - 设置根 Window 事件 ev 的处理器名；winEvents 里没有该键时追加。

- static string EventCategory(string ev)
  - 事件名的逻辑类别，事件选项卡据此把长列表
    分组到可读标题之下而不是溢出。

- static string GroupLabel(string grp)
  - 事件类别标题的显示标签。类别键保持
    英文（按字符串相等匹配）；只有事件选项卡中
    显示的标题文本被翻译。

- static List<string> EventGroups()
  - 事件选项卡的类别显示顺序。

- static bool Contains(string hay, string needle)
  - 面板过滤器使用的区分大小写子串测试（它匹配
    小写类型键，因此小写查询即可）。

- void RenderStatus(App app, Canvas c, Theme t, int x, int y, int w)
  - 渲染底部状态栏：组件总数、当前选中、活动容器（Tabs 带页号）、
    当前缩放（含「适应」）和最近一条提示消息。

- string SaveJson()
  - 把整个表单序列化为人类可编辑的 JSON 文档：表单
    配置加有序的 "fields" 数组。这是交换格式——
    手工编辑 JSON 再加载回去即可重新配置表单，
    设计器完全由数据驱动。与 LoadJson 往返兼容。
    { name, labelPos, formSize, labelWidth, hideStar, showSubmit,
    showReset, submit, fields:[ { type,label,name,placeholder,
    required,change,options:[...] } ] }

- JsonValue ShapeJson()
  - winShape 的 JSON：Rect/Rounded/Ellipse 为普通整数，
    自定义并集形状为区域数组（"t,x,y,w,h,r;..." -> [{"t":..},..]）。

- string ShapeSpecFromJson(JsonValue arr)
  - ShapeJson 的逆操作：winShape 区域数组 -> 运行时消费的紧凑
    "t,x,y,w,h,r;..." 规格。

- JsonValue FieldJson(FormField f)
  - 从 FormField 构建一个字段对象，递归进入容器
    `kids`，使 JSON 往返包含所有设计器事件绑定、选项和
    子节点。读取器（FieldFromJson）与此键集镜像对应。

- void ApplyJson(string s)
  - 根据 SaveJson 生成（或
    手写/工具生成的）JSON 设计文档重建表单。用真正的 JSON 解析器读取，因此任意
    合法格式——美化打印、紧凑、或键乱序——加载结果都一致，
    而不依赖每行一个键的布局。
    把一份设计 JSON 恢复到模型上。Undo/Redo 走这里：历史栈就
    是这两个操作自己的工作对象，历史上 LoadJson 无条件清栈，
    Ctrl+Z 只能退一步、Ctrl+Y 永远落空。打开新文档用 LoadJson。

- void LoadJson(string s)
  - 打开/换入一份新文档：恢复模型后清空撤销/重做历史——快照
    是整份设计 JSON，不清理的话，上一个文档的撤销会把别的设计
    灌进当前画布并在帧末落盘覆盖本文件（IDE 所有设计稿标签页
    共用一个设计器单例）。JSON 抽屉 Apply 也走这里，行为一致。
    解析失败（loadError）保持历史，与旧行为一致。

- void ResetHistoryLocked()
  - 载入新文档时清空撤销/重做历史：快照是整份设计 JSON，
    不清理的话，上一个文档的撤销会把别的设计灌进当前画布
    并在帧末落盘覆盖本文件（IDE 所有设计稿标签页共用一个
    设计器单例）。JSON 抽屉 Apply 也走这里，行为一致。

- bool loadError;
  - 上一次 LoadJson 是否失败（文档损坏被拒绝）。宿主（IDE）
    据此提示；失败时表单被清空，lastError 带原因。

- string lastError;

- List<FormField> ParseComponentFields(string s)
  - 仅解析设计 JSON 的 "fields" 数组到一个新列表，用于
    在不触碰表单的情况下实例化已保存的用户组件。接受
    完整设计文档或纯 fields 数组。

- void BuildFieldsFromJson(JsonValue arr, List<FormField> dst)
  - 将 JSON 数组中的每个对象追加为 FormField（递归处理
    容器 "kids"），保持顺序。

- static bool RetargetDoc(JsonValue doc, int devW, int devH)
  - 把设计文档改到 devW x devH 画布，并把绝对定位的控件夹回
    新画布内。就地编辑 JSON：设计文档每个字段携带的状态（kind、
    placeholder、绑定、事件、options、容器 kids）远多于这里要动的
    几何，任何「反序列化到模型再序列化回去」的写法都会静默丢掉
    其余键——控件会全部退化成默认 Input。
    返回 true 表示文档被改动。

- static bool RetargetDoc(JsonValue doc, int devW, int devH, string devId)
  - RetargetDoc 带目标设备画像 id（"" = 桌面自由尺寸，不写键）：
    锁定类画像把 device 写进文档、关掉 resizable（移动端运行时
    窗口就是整块表面），圆表盘画像顺带声明 winRound。

- static void FitFieldsToCanvas(JsonValue fields, int boxW, int boxH, int margin)
  - 把每个字段的绝对矩形夹进 boxW x boxH，边距 margin。容器的 kids
    用的是父级坐标系，所以递归时换成父级矩形、不再留窗口边距。

- FormField FieldFromJson(JsonValue o)
  - 从 JSON 对象构建一个 FormField。先应用 "type"，这样
    字段的事件集（Events()）和容器属性反映真实类型，
    然后才读取 handlers、options 和子级。

- static JsonValue UnmodeledKeys(JsonValue o)
  - 收集字段对象里设计器没有建模的键(FieldFromJson 读的之外
    的一切,含 DataGrid 的 "of"/"columns"/"rowFactory" 和 "props"
    直通表),供 FieldJson 原样写回。没有时返回 null。

- static bool IsModeledKey(string k)
  - FieldJson / FieldFromJson 已经读写的键集(事件处理器是
    "on" 前缀的动态键)。与两者的键集镜像对应:新建模一个键
    时这里同步加上,否则会重复写出。

- int CountFields(List<FormField> lst)
  - 字段总数（含嵌套容器子级）；加载后为唯一名计数器
    提供初始值。

- bool UsedFieldName(FormField except, string nn)
  - nn 是否已被 except（含其子级）之外的其它组件占用。
    重名会让编译期投影生成重复字段，提交前拒绝。

- bool FieldNameIn(List<FormField> lst, FormField except, string nn)
  - UsedFieldName 的递归实现：在 lst（含各级容器子级）里查
    除 except 之外是否有组件叫 nn。


## Designer (class)

- static string lang="en";

- List<FormField> fields;

- FormField sel;

- FormField selPrev;

- string doc;

- string msg;

- int inspTab;

- bool preview;

- bool hostPanels;

- int seq;

- FormField activeContainer;

- TreeView palTv;

- List<TreeNode> palNodes;

- List<int> palKind;

- List<string> palCustom;

- bool palClickPending;

- int palClickKind;

- string palClickCustom;

- bool palBuilt;

- string palBuiltQ;

- int palBuiltUser;

- List<string> palFolded;

- int layoutMode;

- int winZoom;

- int lastFitPct;

- bool freeDragging;

- bool freeResizing;

- int freeResizeH;

- int freeDragDx;

- int freeDragDy;

- int freeDragStartX;

- int freeDragStartY;

- bool freeDragHasMoved;

- bool canvasMenuOpen;

- int canvasMenuX;

- int canvasMenuY;

- bool canvasMenuArm;

- FormField canvasMenuHit;

- int freeGuideX;

- int freeGuideY;

- FormField hoverField;

- int freeSelPX;

- int freeSelPY;

- int freeDropPX;

- int freeDropPY;

- bool showGrid;

- bool snapGrid;

- int gridSize;

- int freePanX;

- int freePanY;

- bool freePanning;

- List<FormField> freeExtra;
  - 自由画布上的附加选中项（Ctrl+点击 / 框选），
    `sel` 是其中的主选中项（手柄和检查器跟着它）。

- List<string> undoStack;
  - 整份设计 JSON 快照（最多 60 份）：任何修改前压栈，
    撤销时把当前快照压回重做栈再载入栈顶。和场景设计器
    同一套模型——SaveJson/LoadJson 就是文档的完整往返。

- List<string> redoStack;

- List<string> clipFields;
  - 剪贴板：复制选中项的 FieldJson 片段，粘贴时重建
    （容器连子级一起）。

- bool freeHand;

- bool freeMarquee;

- int freeMqSX;

- int freeMqSY;

- int freePanSX;

- int freePanSY;

- int freePanOX;

- int freePanOY;

- bool winRound;

- int pvScale;

- List<TabHeaderHit> tabHits;
  - 本帧画出的页签矩形（渲染时填，指针处理时读）。

- List<int> czBase;

- int czDpi;

- int czScale;

- int czDepth;

- bool zoomMenuOpen;

- int zoomMenuX;

- int zoomMenuY;

- bool zoomMenuArm;

- int labelPos;

- int formSize;

- int labelWidth;

- bool hideStar;

- bool showSubmit;

- bool showReset;

- bool dragging;

- bool dragFromPalette;

- bool dragMoved;

- int dragKind;

- string dragCustomKind;

- int dragIndex;

- int dropIndex;

- int dropY;

- int dragMouseX;

- int dragMouseY;

- FormField dropContainer;

- int dragUserComp;

- int hoverFt;

- int hoverX;

- int hoverY;

- PropertyGrid propGrid;

- int inspKeySeq;

- Input optInput;

- Input optFieldInput;

- Input evtInput;

- string evtEditing;

- Input formNameInput;

- Input submitInput;

- string winTitle;
  - 窗口标题的唯一真相（普通字段）：标题输入框经 `data` 绑定、
    PropertyGrid 的 title 行经 PropSpec.str 指到同一字段。

- Input winTitleInput;

- int winW;

- int winH;

- bool winResizable;

- bool winTool;

- bool winCenter;

- bool winGlass;

- int winShape;

- int winShapeRadius;

- string winShapeSpec;

- bool winChrome;

- int winOpacity;

- int winShadow;

- string docRole;
  - 设计文档的 "role"："control" 的文档编译成可嵌入的自定义
    组件（partial class X : Control）而不是窗口。设计器自己
    不用它，但必须原样往返：丢了这一键，一次另存就把组件
    变成窗口，代码后置里的字段访问全部编译不过。

- int winPosX;

- int winPosY;

- string device;
  - 目标设备画像 id（DeviceProfile，"" = 桌面自由尺寸）。锁定类
    画像下 winW/winH 由画像给出，属性面板不再提供宽高输入，
    只保留画像允许的横竖屏切换。

- int devSelIdx;
  - device/方向在属性面板里的镜像下标：每帧从模型同步，
    PropertyGrid 经绑定写回，渲染后与模型差量即用户改动。

- int devOrient;

- List<EventBinding> winEvents;

- string winEvtEditing;

- Input filterInput;

- int inspScroll;

- int inspContentH;

- List<string> bindVars;

- bool bindPickOpen;

- TextArea jsonArea;

- bool jsonOpen;

- bool evtRootPrev;

- FormField evtSelPrev;

- List<UserComponent> userComps;

- bool saveCompRequested;

- List<string> pvCompKeys;

- List<Control> pvCompCtls;

- int pvCompGen;

- string compPropFor;

- List<string> compPropKeys;

- List<string> compPropLabels;

- List<string> compPropDefs;

- string compPropEditFor;

- List<Input> compPropInputs;

- List<Switch> compPropSwitches;

- List<bool> compPropIsBool;

- List<string> compPropShadow;

- string styleEditFor;

- List<Input> styleInputs;

- FormField rowsEdField;

- FormField propsEdField;

- int rowsEdSel;

- int rowsEdSelPrev;

- int propsEdSel;

- int propsEdSelPrev;

- int rowsEdGen;

- int propsEdGen;

- Input rowsFilter;

- Input propsFilter;

- int rowsScroll;

- int propsScroll;

- string rowEdTitle;

- string rowEdField;

- bool rowEdRight;

- bool rowEdSep;

- string propEdKey;

- string propEdVal;

- int colEdWidth;

- int colEdAlign;

- int colEdType;

- bool colEdSortable;

- bool colEdResizable;

- bool colEdEditable;

- int colEdDecimals;

- string colEdMoney;

- bool colEdPercent;

- string colEdPrefix;

- string colEdSuffix;

- bool colEdGrouped;

- int colEdSummary;

- int colEdPin;

- string colEdBand;

- string colEdClick;

- string ofEd;

- PropertyGrid edGridRows;

- PropertyGrid edGridProps;

- Input iconHolder;

- Input iconPickTarget;

- Input iconPickSearch;

- LayerState rowsWin;

- LayerState propsWin;

- LayerState iconWin;

- bool openCompRequested;

- string openCompName;

- void SetLayoutMode(int mode)
  - 切换被设计窗口的布局模型。宿主在打开一份设计时
    调用（设计文档里存了 layoutMode），设计器自己不提供
    切换命令：两种布局不共享坐标，来回切会丢摆放。

- void SeedFreeBounds()
  - 给还没有绝对坐标的组件在自由画布上排一列，
    免得它们全叠在原点。

- static string T(string en, string zh)
  - 按当前 UI 语言选取字符串：键式查找（System.Globalization.Lang），
    英文原文作键、内置中文为缺省串；语言包未加载时回退缺省串。

- Designer()

- void ScrollPaletteToEnd()
  - 把工具箱滚到末尾。

- void Seed(App app)
  - 可选的示例内容，让新建的设计器不是空表单——
    包含一个 Card 和带嵌套组件的 Tabs 容器，让组合 /
    标签切换行为开箱即可见。

- bool FieldAlive(List<FormField> lst, FormField f)
  - `f` 是否还挂在设计树上。删除组件后 activeContainer /
    dropContainer 这类引用可能指向已经被删掉的容器，往里加组件
    会得到一个谁也看不见、也存不下去的字段。

- void DropDeadRefs()
  - 丢弃指向已删组件的容器引用。

- void AddField(int ftype)
  - 添加新组件。有活动容器时组件会嵌套进去（Tabs 则进入活动
    标签页）；否则追加到顶层表单。添加容器后会把它设为活动
    容器，可以立刻往里面放置组件。

- void SetBindVars(List<string> vars)
  - 宿主 IDE 提供从表单配对的 code-behind 文件
    扫描出的可绑定变量名（见检查器的绑定路径选择器）。

- void SetUserComponents(List<UserComponent> comps)
  - 宿主 IDE 提供项目保存的用户组件（每个组件一个实体，
    来自 components/*.zcomp）。组件文档可能随时被重新加载
    或保存，引用节点据此作废预览缓存。

- string UserCompJson(string name)
  - 组件名对应的 .zcomp 文档原文；不存在返回 ""。

- bool ConsumeSaveComponent()
  - 用户请求把当前设计保存为可复用组件后，恰好返回一次 true；
    宿主 IDE 持久化它并重新加载列表。

- string FormNameText()
  - 表单名称输入框的当前文本（宿主保存组件时用作文件名）。

- string ConsumeOpenComponent()
  - 双击引用节点后恰好返回一次组件名（"" 表示无请求）；宿主
    打开 components/<名>.zcomp 文档本身供编辑。

- void AddUserComponent(int k)
  - 在表单上放置一个已保存用户组件的**引用**节点：设计文档里
    只写 {"kind":名,"ref":名,...}——组件设计的每次进化自动
    跟随所有实例（WinForms UserControl 语义），要改内部就打开
    组件本身，检查器提供「解包为内联副本」作逃生门。默认矩形
    取组件文档自己的画布尺寸（winW/winH），缺省 240x一行高。
    画布预览按组件文档用 FormBuilder 建真控件渲染。

- void UnpackUserComponent(FormField f)
  - 「解包为内联副本」：把引用节点换成组件文档的字段树
    （单容器文档直接换成那个容器，多字段的包一层同名
    Card），首节点继承引用的矩形/停靠/跨度。解包后的副本
    与组件脱钩，之后组件进化不再跟随。

- void EnsureCompPropDecls(string compName)
  - 组件文档根上声明的对外属性（"props":[{key,label,kind,
    target,prop,def}]，v1 只消费 text 型），解析进并行表缓存，
    缓存键为 组件名|代次。组件没声明或文档缺失时表为空。

- static string CompPropValue(FormField f, string key, string def)
  - 引用节点实例值读/写：存 f.extra 的 "props" 直通对象
    （"props" 不在 IsModeledKey 里，SaveJson/LoadJson 原样
    透传，GenForm 展开时落给目标内部控件）。

- static void SetCompPropValue(FormField f, string key, string val)

- static int AlertLevelOf(string spec)
  - 告警横幅语义词 → Layer 通知类型（2 info / 3 success /
    4 warning / 5 error）；数字串与角色词都认，其余落 info。

- static bool RenameCompProp(FormField f, string from, string to)
  - 把 props 直通表里的 `from` 键改名为 `to`（值随迁）。三种情况
    拒绝并原样返回 false：`to` 已存在（先 Drop 再 Set 会把已有
    键的值静默覆盖掉——改名是编辑意图，吞别人的值属于数据丢
    失，调用方可用 HasCompProp 预检给出提示）、`from` 不存在、
    from == to。返回是否真的发生了改名。

- static void DropCompProp(FormField f, string key)
  - 从 props 直通表里删掉 `key`（不存在则无操作）。JsonValue
    没有按键删除，直接改写 keys/vals 两个平行表（keys 顺序
    决定 FieldJson 写回顺序，保持稳定）。

- static bool IsBoolProp(string key, string label, string val)
  - 识别属性是否为布尔型（以 Switch 开关进行展示与编辑）。

- static string CleanPropLabel(string rawLabel)
  - 清理属性标签中用于提示类型的冗余后缀（例如 "(true/false)"），
    使设计器属性面板的 Label 干净利落，不被无谓文字挤占。

- static List<string> BuiltinPropRows(string kind)
  - 内置组件的直通属性行：kind → "键|双语标签" 列表。值存
    f.extra["props"]——与引用节点实例值同一张直通表，GenForm
    泛化发射 SetProp、画布预览经 ApplyFieldProps 落到真控件。
    只列设计期有意义的形态/展示属性；运行态开关（如 Countdown
    的 active）不进来。键全部取自各控件 Props()/SetExtra 的
    已支持集，新增前先核对控件源码。没有行的 kind 返回 null。

- static void ApplyFieldProps(Control ctl, FormField f)
  - 把字段 extra["props"] 直通表落到真控件上（画布预览用）：
    预览件每帧新建，逐键 SetProp；控件不认的键走基类 fallback
    退化为样式类，无副作用。

- void ApplyCompProps(Control root, JsonValue compRoot, FormField f)
  - 预览树上应用组件属性：与运行期注册表（UserComponentRegistry.
    ApplyProps）共用同一语义——实例值优先，缺省取声明里的 def，
    落到目标控件（预览树按组件文档原名查找）。实例值存
    f.extra 的 "props" 直通对象。

- Control PvCompCtl(FormField f, int w, int h)
  - 引用节点画布预览的构建缓存：组件文档只在代次变化时
    重建（SetUserComponents / 尺寸变化 / 实例属性变化），
    其余帧复用整棵 FormBuilder 树只重渲染。找不到组件或
    解析失败返回 null，调用方退回占位块。

- void AddCustomField(string kind)
  - 添加运行期注册（HeavyControls）的自定义组件
    到表单/活动容器，与内置组件的 AddField 对应。

- bool InSubtree(FormField owner, FormField inner)
  - 当 `inner` 位于 `owner` 子树的任意位置时返回 true。

- bool ContainsRef(List<FormField> lst, FormField target)
  - 列表中是否含该组件引用。

- int IndexIn(List<FormField> lst, FormField f)
  - 组件在列表中的下标，不存在时 -1。

- FormField ParentContainerOf(FormField target)
  - 把 `target` 作为子级的容器；若 target 是
    顶层字段（或未找到）则返回 null。递归查找。

- FormField FindOwner(List<FormField> lst, FormField target)
  - 递归查找把 target 作为直接子级的容器，找不到返回 null。

- List<FormField> ListRemove(List<FormField> lst, FormField target)
  - 重建不含 `target` 的 `lst`（列表中间位置的 RemoveAt 在其他地方使用，
    但重建与下方的 Insert 规避方案保持一致）。

- List<FormField> ListMove(List<FormField> lst, int from, int to)
  - 把 `from` 处的元素移动到 `to`，通过 Add 重建（
    运行时在列表中间 Insert 会溢出缓冲区并破坏堆）。

- void MoveSel(int delta)
  - 把选中项在同级列表内上/下移动 delta 个位置（先压撤销栈）。

- void InsertFieldAt(int at, FormField nf)
  - 插入 `nf`，使其最终位于顶层列表的 `at` 索引处。

- bool IsExtraSel(FormField f)
  - 组件是否在多选里（主选中项或附加项）。

- void ToggleExtraSel(FormField f)
  - 把组件加入/移出多选的附加选中项（Ctrl+点击）。

- void ClearExtraSel()
  - 清空多选的附加选中项。

- void SizeSelGroup(int mode)
  - 把多选里其余成员的宽/高设成主选中项的（0=等宽，1=等高），
    和场景设计器的「尺寸」组同义。

- void SelToEdge(int top)
  - 把选中项移到同级的最前/最后（1=置顶，0=置底）。

- void DuplicateSel()
  - 复制选中项，副本偏移一点并成为新的选中项。

- void PushUndo()
  - 记录当前设计；在任何修改前调用。当前状态和栈顶相同
    （上一次压栈后设计没真正变过）时不重复压栈。

- void PopUndoIfUnchanged()
  - 拖拽/缩放结束（或任何「压了栈但没改成」的操作收尾）时调用：
    设计和栈顶一致说明这次操作没有实际改动，把栈顶退掉，
    免得 Ctrl+Z 第一脚踩空。

- void Undo()
  - 撤销：当前设计压入重做栈，载入撤销栈顶；栈空时仅提示。
    恢复快照必须走 ApplyJson——LoadJson 会清空两个历史栈，
    一步撤销之后整个历史就没了。

- void Redo()
  - 重做：当前设计压入撤销栈，载入重做栈顶；栈空时仅提示。

- void CopySel()
  - 复制选中项（含多选）为 FieldJson 片段列表；无选中时仅提示。

- void RenameFresh(FormField f)
  - 粘贴的组件整组换新名字（含容器子级），避免和原组件、
    以及代码后置里的字段撞名。

- void PasteClip()
  - 粘贴到原父容器（没有选中就贴到顶层），整组偏移 16px，
    贴完的组成为新的选中组。

- void NudgeSel(int dx, int dy)
  - 方向键微调整个多选组（自由布局；Shift=1px）。

- void NudgeOne(FormField f, int dx, int dy)
  - 单个组件按像素增量微调并钳制到非负坐标。

- void ToggleLockSel()
  - 锁定/解锁整个多选：以主选中项的状态取反，其余成员跟随，
    这样一组组件的锁定状态始终一致。

- void DeleteSelGroup()
  - 删除整个多选：先删附加项，再删主选中项。
    无论从功能区按钮还是 Delete 键触发，都走这里，
    保证多选里的每一个都被删掉（而不是只删主选中项）。

- void DeleteSel()
  - 删除主选中项，并清掉多选与可能引用它的交互状态
    （避免悬空引用崩溃）。

- void CutSel()
  - 剪切选中组件到剪贴板并从画布移除。

- void BringToFront()
  - 将选中组件移至其容器/根列表的顶层（最上方绘制）。

- void SendToBack()
  - 将选中组件移至其容器/根列表的底层（最下方绘制）。

- void SetSelDock(int side)
  - 快捷设置选中组件的停靠方式。

- List<string> CanvasMenuLabels()

- void HandleCanvasMenu(App app, int cc)

- void Render(App app, int x, int y, int w, int h)
  - 每帧入口：用 app.focus.PushIds 固定设计器专用基值
    每帧入口：基于 Path Hash ID 作用域绘制整个设计器，
    确保即时模式控件每帧获得稳定唯一的层级路径 ID，
    与保留式长生命周期控件（Input 等）完全物理隔离。

- void RenderHostPanels(App app, Rect toolRect, Rect propRect)
  - 宿主把组件面板和属性面板画进自己的 dock 面板里。
    分别使用独立的作用域隔离，两面板互不影响。

- void RenderPinned(App app, int x, int y, int w, int h)
  - 设计器本体的一帧：功能区、画布（或 JSON 抽屉）、组件托盘、
    左右面板与状态栏；处理键盘快捷键、拖拽收尾、工具箱
    tooltip、拖拽幽灵块与缩放下拉菜单。

- void RenderJsonPanel(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - 开启 JSON 时替代画布显示的可编辑 JSON 抽屉。
    文本框就是文档：Apply 从中重建设计，
    Regenerate 把当前设计重新导出到其中。

- void RenderPalette(App app, Canvas c, Theme t, int px, int py, int pw, int ph)
  - 组件工具箱：一棵 TreeView（分组是目录，组件是叶子），
    行的绘制、滚动、过滤、折叠全部由控件自己完成，这里
    只负责喂数据和处理「选中 = 添加 / 按下 = 开始拖拽」。

- void EnsurePalette()
  - 惰性创建工具箱 TreeView，并在过滤串或用户组件数量变化时重建节点。

- void BuildPalette(string q)
  - 重建工具箱节点。过滤串非空时是一层扁平的匹配结果，
    否则是按类别分组的两层树。

- void PalGroup(string title, int lo, int hi)
  - 添加一个分组目录及其 [lo, hi) 类型区间内的组件叶子。

- void PalDir(string title)
  - 添加一个分组目录节点（折叠状态按标题跨帧记忆）。

- void PalLeaf(string label, string icon, int kind, string custom, int depth)
  - 添加一个组件叶子节点（palKind/palCustom 与行下标一一对应）。

- int PalLeafRow()
  - 事件涉及的行是组件叶子时返回其下标，否则返回 -1。

- int UserCompIndex(string name)
  - 按名查找用户组件下标，不存在时 -1。

- void PalActivate()
  - 单击工具箱里的组件即添加一个；随后清掉选中，
    这样连点同一行可以连续添加。实际添加发生在抬起时
    （见 PalClickRelease），因为按下这一刻还分不清点击和拖拽。

- void PalClickRelease()
  - 在工具箱上按下又原地抬起：这是一次点击，添加组件。

- void PalPress()
  - 在组件行上按下即开始一次拖拽，拖到画布上放开即添加。

- void PalFoldNote(bool folded)
  - 记住分组的折叠状态，重建节点后仍然保留。

- bool MatchesFilter(int ft, string q)
  - 当字段类型的 key 或显示名包含过滤关键字时为 true。

- bool PalFolded(string title)
  - 已折叠的分组标题。折叠状态按标题记住，跨帧保留。

- void PalToggleFold(string title)
  - 切换分组标题的折叠记忆。

- static List<string> ListRemoveStr(List<string> src, int idx)
  - 重建不含 idx 处元素的字符串列表（与 ListMove 同一规避）。

- bool InRect(int mx, int my, int x, int y, int w, int h)
  - 点是否在矩形内（含右/下边界）。

- void RenderTray(App app, Canvas c, Theme t, int x, int y, int w, int h)
  - WinForms 风格组件托盘：表单上的每个非可视组件
    在此显示为一个小块。点击选中它，其属性和
    事件即可在检查器中编辑；选中的小块会高亮。

- int ClampSpan(int sp)
  - 把字段的列跨度限制到合法的 1..24 网格范围。

- string FieldDesc(int ft)
  - 面板悬停 tooltip 中显示的一行描述。

- string FieldIcon(int ft)
  - 把字段类型映射为面板用的 Segoe MDL2 图标名。

- int SmallBtn(App app, string text, int type, int x, int y, int w)
  - 渲染一个小按钮（高 28）并返回其 id。检查器各处使用，
    让控件能放进紧凑的行内。使用 AllocId() 保证即时模式每帧稳定 id
    且与保留式持久控件（Input 等，>=1000000）完全隔离。

- int SmallIconBtn(App app, string iconName, int type, int x, int y, int w)
  - 小号（高 28）纯图标按钮——尺寸与 SmallBtn 相同，但
    标签是矢量 Icon 字形而非文本字符。

- bool OpenRowsEditor(App app, FormField f)
  - 检查器「OPTIONS/COLUMNS」区头部的打开按钮：把选中组件
    装进行编辑窗口。非选项类组件返回 false（不画按钮）。

- void OpenPropsEditor(App app, FormField f)
  - 检查器 WIDGET PROPS 区头部的打开按钮：extra["props"]
    直通表的窗口编辑器。没有直通行（BuiltinPropRows 为空）
    的内置组件与引用节点都可用（手写键也能进来改）。

- LayerState NewEditorWindow(App app, int w, int h, string title)
  - 大编辑器统一开 LayerState 仿真窗口：居中、可拖动、可缩放，
    内容几何每帧从窗口读。每次打开新建（几何随内容变），
    构造器自带 Layer 保留 id 段并恢复宿主计数器，随开随建无害。

- bool BigEditorOpen()
  - 任一大编辑器窗口开着。画布的原始指针路径（选中/拖拽/滚轮
    直接读指针事件、不走命中测试）在这期间整体让位——窗口
    矩形的吞命中只护得住走命中测试的控件，护不住这些裸路径。

- void CloseBigEditors()
  - 关闭全部编辑器窗口并清掉编辑态。选中下标一并复位，下次
    打开按打开逻辑重选。

- void LoadRowScratch(FormField f, int i)
  - 把第 `i` 行选项原文解析进暂存字段（选中行变化时调用）。

- string JoinRowOption(FormField f)
  - 把暂存字段按组件的存储约定拼回选项原文。全空行返回 ""，
    调用方按普通编辑写回（清空行文字 = 行变空，删除整行走
    工具条的删除按钮）。

- List<PropSpec> RowSpecs(FormField f)
  - 选中行的右侧属性表：常规段（标题 + 图标/数据字段）+
    选项段（分隔条 / 靠右，仅工具条与功能区）。绑定目标是
    暂存字段，写回在帧末统一做。

- List<PropSpec> PropSpecs()
  - 选中键的右侧属性表（键 + 值）。绑定目标是暂存字段。

- void LoadPropScratch(FormField f, int i)
  - 把第 `i` 个键的键值读进暂存字段（选中键变化时调用）。

- void WriteBackProps(App app, FormField f)
  - 帧末把暂存字段写回选中键（原位改名保持键序，值直接覆盖）。
    改名撞上已有键时忽略改名（表里不允许重复键）。

- void EnsureSelVisible(int count, int slot, int viewH)
  - 行/键列表滚动：保证选中行可见（增删/移动/键盘换选后调用）。

- void EnsurePropSelVisible(int count, int slot, int viewH)
  - props 列表滚动：保证选中键可见。

- static JsonValue ColEntry(FormField f, int i)
  - 第 `i` 列的 columns 条目（没有或不是对象返回 null = 全缺省）。

- static JsonValue ColsArray(FormField f)
  - extra["columns"] 数组（不存在或不是数组返回 null）。

- static void DropKey(JsonValue o, string key)
  - 从 JSON 对象里删掉一个键（JsonValue 没有按键删除，直接
    改写 keys/vals 平行表；键不存在时无操作）。

- void LoadColScratch(FormField f, int i)
  - 选中列的声明式属性读进暂存字段（选中行变化时随
    LoadRowScratch 一起调）。

- static bool ColBool(JsonValue e, string key)
  - 列条目里的布尔键（缺/false）。

- void ColDesired(List<string> keys, List<JsonValue> vals)
  - 暂存值对应的声明键值（偏离缺省的才进表）。读的是暂存
    字段本身，帧末写回与探针共用。

- void WriteBackCol(App app, FormField f)
  - 帧末把列属性暂存写回 extra["columns"][i]：全部键都在缺省
    且条目不存在时不动文档；需要条目时按需补位（空对象占位，
    GenForm 对缺 field 的条目整条跳过，占位无副作用）。

- static void MoveColEntry(FormField f, int from, int to)
  - columns 条目跟随 options 移动（下标对齐约定）。

- static void RemoveColEntry(FormField f, int i)
  - columns 条目跟随 options 删除。

- static void CopyColEntry(FormField f, int from, int to)
  - 复制列时属性条目跟随：from 处有条目就在 to 处放副本
    （按需补位），没有条目就把 to 处清成空对象。

- static JsonValue NormalizedColumns(FormField f, JsonValue cols)
  - 把 columns 数组按 options 的顺序重排（field 名匹配，退而
    按 title，再退按位置）。GenForm 先行的设计稿里列声明顺序
    可能与 options 不同，设计器以 options 为列序真相。条目数
    可以少于 options（缺 = 全缺省），匹配不上的原样保留在尾部
    （不吞键，GenForm 对它们照常发射）。

- void RenderRowsEditor(App app, Canvas c, Theme t)
  - 选项/列编辑窗口（LayerState 仿真窗口，可拖动/缩放）。商业
    组件设计器同款的主从布局，每帧调用；内容几何从窗口实时
    x/y/w/h 读出，拖动/缩放自动跟随：
    
    ┌ 工具条：选项 (N) ··················· [↑][↓] [添加][复制][删除] ┐
    ├ 筛选…   ┬ 行属性 · 第 i/N 行                                   ┤
    │ 行列表   │ PropertyGrid（标题 / 图标或字段 / 分隔条 / 靠右）      ┤
    ├ 说明区：选中行原文 · 格式 · 绑定路径 ················ [关闭] ──┤

- void OpenIconPicker(App app, Input target)
  - 点图标格旁的格子按钮：开选择器窗口，锁定写回目标输入框。

- void RenderIconPicker(App app, Canvas c, Theme t)
  - 图标选择器窗口：网格铺开 Icon.Names() 的全部内置
    图标（图标 + 名字），顶部搜索框按名字过滤；点选写回发起
    的输入框并收起。当前值命中的格子高亮。几何随窗口实时读。

- static bool NameHas(string hay, string sub)
  - 大小写不敏感的子串测试（图标搜索过滤用）。

- static string RowMainText(FormField f, int i)
  - 第 `i` 行选项的主文本：标题；工具条/功能区的分隔条行显示
    「— 分隔条 —」。解析与 LoadRowScratch 同拆法。

- static string RowSubText(FormField f, int i)
  - 第 `i` 行选项的副文本：数据字段键 / 图标名（没有则 ""）。

- static bool RowIsSep(FormField f, int i)
  - 第 `i` 行是否是分隔条（工具条/功能区的 "-"）。

- static bool HasCompProp(FormField f, string key)
  - extra["props"] 里是否已有键 `key`（props 新增键的去重用）。

- void RenderPropsEditor(App app, Canvas c, Theme t)
  - 组件属性（extra["props"] 直通表）弹窗编辑器：与行编辑器同一
    套主从布局——左侧键列表（可筛选，副文本是当前值），右侧选中
    键的键/值属性表，底部说明。键名就是控件 SetProp 认的名字，
    生成代码与画布预览共用同一张表。


## TabHeaderHit (class)

画布上一个页签的屏幕矩形。渲染标签条时逐个登记，指针按下
时据此把点击判成「翻到那一页」，而不是选中或拖动 Tabs 容器。

- FormField tabs;

- int page;

- int x;

- int y;

- int w;

- int h;

- TabHeaderHit(FormField tabs, int page, int x, int y, int w, int h)
