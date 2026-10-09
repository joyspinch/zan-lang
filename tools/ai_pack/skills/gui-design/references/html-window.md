# HTML 窗口开发定式（.html 设计稿 = 唯一格式）

窗口的布局/内容/样式进设计稿，交互与数据在 code-behind。旧 `.html`
通道已删（GenForm 只认 .html）——别再写、别再提，见到旧稿用设计器
往返转一次即可。

## 三条建树通道（同解析器，按窗口形态选）

- **编译期设计稿（GenForm）**：body 带 `data-zan-design` 的设计文档
  作为编译输入，GenForm 合成 Main、控件字段、`__BuildForm`（含
  `root.Dock(Dock.Fill())`）。**设计稿必须是命令行第一个文件**——
  zanc 从第一份设计文档合成入口，放后面 = 字段全缺（模板门配方：
  html 首参 + 全部 src/*.zan）。
- **编译期运行期文档（GenHtml）**：body **不带** `data-zan-design`
  的普通 .html 作为编译输入，GenHtml 展开成
  `class <文件基名帕斯卡化>Html { public static string Css;
  public static Control Build(HtmlHandlers); }`——类名来自**文件基名**
  （App.html → `AppHtml`），不是 body id 也不是项目占位名；成员只有
  静态字段 `Css`（无括号）与静态方法 `Build`。**不发射 Main、不发射
  字段、不管 dock**：`data-win-*` 是 GenForm 专属，dock/图标/Tip 等
  行为配置在 code-behind 里 `Find(id)` 后补。适合"文档=结构快照、
  行为=代码"的窗口。
- **运行时**：`App.LoadHtml(...)` 同一解析器动态建树（html 文本可来自
  文件/网络/字符串）。切页出口里装每页内容时用这条。

**GenHtml 通道的树根坑（实机踩过）**：`Build` 返回的 body 元素 dock=0
（Manual）——窗口只对 dock=Fill 的根做客户区填充，**code-behind 必须
补一句 `root.DockFill()`**，否则整棵树缩在窗口左上角按内容尺寸排版
（GenForm 时代这句由 `__BuildForm` 发射，runtime 文档没人替你发射）。
同理：左栏宽度这类"GenForm 靠 logW/logH 声明"的定尺寸，在 GenHtml
通道走 CSS `width/height` 声明（dock 排布的 `DockSize` 取
`StyleWidthIn/StyleHeightIn`）。

**迁移设计稿 → 运行期文档的三个语义坑（都真踩过）**：
1. **单位数字是 CSS，不是逻辑 px**：`line-height: 72` 是 72 **倍**
   行高（CSS 规范：无单位 = font-size 的倍数），30px 字号配它 =
   3240px 行盒，整棵布局被顶飞。要 72 逻辑像素必须写 `72px`。
   width/height/font-size 同理——搬 CSS 时逐条补单位。
2. **样式单一来源**：骨架样式搬进文档 `<style>` 后，把 skin/appCss
   里的同规则删掉，别留两份（改一处忘另一处）。
3. **`{{NAME}}` 只在 .zan 里替换**；GenHtml 类名跟文件名走，
   code-behind 里引用的是 `<基名>Html.Build/Css`。

**多文档 appCss 的定式（gui-wechat 实机踩过，run10~17；scoping 已自动化）**：
- **`UseAppCss` 是整表替换**（`appCss = css`），且它就是 app 唯一的
  应用样式入口——每份文档的 `<style>` 只进了自己生成类的 `Css`
  字段，**装载几份文档就要把它们几段 Css 全量拼进同一次
  `UseAppCss` 调用**（`AppHtml.Css + "\n" + FavsHtml.Css + ...`）。
  漏拼哪份，那份的骨架样式（flex、栏宽、渐变）整体静默失效，页面
  塌回纵向块流——不报错，只有截图能看出来。
- **样式 scoping 编译/装载期自动完成**（2026-09 起，gui-wechat 四页"裸
  body 互中"踩坑后的工具链修复）：GenHtml 给文档每个元素混入作用域
  类 `zs-<基名>`，并把文档 `<style>` 的每个复合块补上该类——同名类
  跨文档互不串，裸 `body` 只作用自己的根，手写唯一 body id 不再是
  必须项（留作 Find/可读性）。`@media`/`@supports` 内层递归，
  `@keyframes`/`@font-face` 原样。**边界**：运行期挂载件（ListView
  行模板等 code-behind 建的控件）不携作用域类，其样式放皮肤层（全
  局通道）——与 Vue 组件样式不泄漏子组件同一取舍。运行期
  `App.LoadHtml` 同语义：装载即组件，每次装载造 `zs-load-<序>` 作用域
  （变换器共用 System.Web.HtmlScope，与编译通道同一保证）。
- **flex 链条上每个"吃满"的环节都要自己声明**：页面根控件挂进 flex
  宿主后，没有声明高度就是零高（dock 时代的 MeasureDocked 不认
  flex 宿主里的 dock=5 子页）——`.page { flex-grow: 1; align-self:
  stretch; }`。反过来，**固定尺寸件要 `flex-shrink: 0`**：同列里
  ListView 报全内容高（几千 px）时，溢出收缩按比例分摊，240px 的
  封面带会被压扁成 95px（朋友圈封面实测）。装饰角/页脚文案这类
  "静态 AddText 元素"没有内在宽度申报，不吃 `flex-grow: 1` 就被
  量成几十 px 折行。
- **`<style>` 注释里别写 `*/` 序列**（如 `.wxfilt*/.wxfavrow*` 这种
  通配写法）：StripComments 按第一个 `*/` 截断注释，注释后半段
  变成裸 CSS 文本、随后的规则被吞进选择器——整段样式静默失效。

## 声明面五件套与硬边界

| 原语 | 作用 | 硬边界（超了就静默不生效，别猜） |
|---|---|---|
| `data-on-click="名字"` | 事件 → 处理器注册表 | 设计稿只落名字；逻辑在 code-behind |
| `data-arg="字面量"` | 带参事件（列表行共用处理器区分来源） | 只支持字面量，不求值表达式 |
| `data-bind="路径"` | 绑 JsonValue 模型字段（ChildWindow 每帧双向同步，见下节选路） | Element 缺省绑 text；真控件缺省 value；**只在挂模型宿主生效**（主窗口 LoadHtmlWith 树上惰性） |
| `data-if="路径"` | 显隐插拔 | **只认路径真值**（null/假/"false" 为隐），不支持 `a==b` 比较表达式 |
| `<template data-for="路径">` | 数组行克隆 | 嵌套模板不支持；行内 bind/bindIf 以本项为第一作用域、回落根模型 |

坑出处：data-if 的真值语义见 ChildWindow.Truthy——想按 `page=="bag"`
切面板的人都在这里撞墙，解法见下节。模板原型子树靠 UA 样式
`template{display:none}` 隐藏；克隆行撤除走文档序表防幽灵占位。

## 动态绑定：改模型，还是改树（选路）

绑定通道是 ChildWindow 的每帧同步（`SetRoot(tree, model)` 挂
JsonValue 状态实体）：帧前模型→UI（`SyncFromModel`，值变才写）、
帧后 UI→模型（`SyncChangedNode`，快照变了就回写，**没有来源
过滤**——程序化 `SetProp` 同样落模型）。由此三条选路规则：

- **值/显隐/列表内容 → 改模型**：`model.Set("title", ...)` /
  `Set("showBag", ...)` / `Set("items", ...)`，末尾
  `RequestRedraw()` 出一帧即生效（模型改了但窗口静止，屏幕不会
  自己动）。别既改模型又 `SetProp` 同一控件——两个写入源打架，
  帧后回写可能把模型改动顶回去。
- **结构（增删控件/换组件类型）→ 命令式**：retained 树 API
  （`Find` + `Add`/`InsertAt`/`Remove`/`SetProp`）或整页
  `LoadHtmlWith` 重装。已知代价：重载是**整树无 diff**（手工 Add
  的控件不在新文档里）；命令式行内 style 只吃视觉键、布局键走
  类规则；没有"改第 N 行"助手——列表局部高频更新留在模型通道
  （data-for 长度变了整组重建）。
- **主窗口 `LoadHtmlWith` 装的树没有模型**：data-bind/data-if/
  data-for 全部惰性，动态绑定内容必须放进 ChildWindow 宿主。

代码内实时绑定是另一层：`Binding<T>`（stdlib 规范 §6.1），
编译期字段直绑；字符串 bindPath 是"对话框 ↔ 状态实体"通道，
两者互不替代（Control.zan bindPath 头注释、STDLIB_COMPONENT_
STANDARDS.md §6.7）。

## 多按钮控制中间区（切页三路）

1. **推荐：Nav.Embed 做骨架 + 每页 .html 内容**。`Gui/Nav.zan` 的
   Embed（Tabs-as-outlet）：路由表=按钮排，中间区=出口；每页首次
   选中才实例化（惰性）、默认切走保留（keepAlive）、可按路由配临态
   （切走销毁）。放置/面板类游戏的"切走保留"和它默认值天然一致。
2. **纯声明近似（页少面板轻）**：每按钮 `data-on-click="go"
   data-arg="bag"` + 一个处理器翻模型 + 面板挂 `data-if="showBag"`。
   页一多（十个布尔字段）就笨，别硬拼。
3. 声明式出口原语（`data-nav` 直连路由）v1 没有——想要"零接线"
   先确认它是否已落地，没有就走路 2。

## 布局：按浏览器等价子集写，别按浏览器全量写

引擎支持 flow / inline 行盒 / flex / grid / float / overflow 滚动，
经 Chrome getBoundingClientRect 逐盒对比（oracle）校准，多数场景
0px 偏差。CSS 方言之外的东西写上去不报错也不生效——动手前查支持度
审计脚本与 HTML_UI.md 的与浏览器差异清单；"改了没反应"先怀疑
没进方言，再怀疑优先级。

## CSS 两层与基线坑

- 皮肤基线（base.css，`flex{display:flex}` 等 UA 规则）+ 应用级
  （`app.UseAppCss(...)`）。颜色走 `var(--*)` 令牌，尺寸走阶梯。
- **项目自带 `skins/` 目录要连 base.css 一起带**：`--embed skins`
  见到同名目录就整体跳过 stdlib 基线内嵌，只放自家 skin.css 会让
  基线层整层丢失、flex 全退化成叠矩形（无为修仙传百艺页签全叠的根因，
  模板补带 base.css 后消除）。
- 运行时换肤是实例方法：`form.GetApp().UseSkin("neon")`——按静态
  `App.UseSkin(...)` 调会报 cannot call instance method（踩过）。

## 模板实例化（gui-* 模板带 `{{NAME}}` 占位符）

- `{{NAME}}` 在**两处**都要替换：`src/App.html`（根元素
  `id="{{NAME}}"` 与 `data-win-title`）和 `src/App.zan`
  （`partial class {{NAME}}`），且两个入口文件要改名为
  `<NAME>.html`/`<NAME>.zan`（zan.proj `entry = src/{{NAME}}.html`
  按名对上）。只换 .zan 不换 .html，GenForm 合成的
  `<NAME>.OnLoad` 找不到方法、模板内组件字段全裸（只复制
  App.zan/App.html 各一份编译 gui-wechat 这类多文件模板也会
  undefined type——整个 src 都要带）。
- 参考实例化脚本（仓库 `scripts/e2e_pipeline.ps1` 的做法）：整树
  拷贝 + 替换 + 用 zan.proj 编译，别手搓单文件。

## HTML 窗口设计器的所见所得

- **共用完整建树与布局，拾取读取实际框**：生成代码的 typed 对象按原始
  `fields/kids` DFS 填 prepared 槽，FormBuilder 统一装配；混排顺序只影响
  展示，不改变槽序。完整结构遍历 `Control.children`，文本次序结合
  `Element.elOrder`；`FlowEntries` 是流布局视图，会排除 absolute/hidden，
  用它分配槽或断言整树曾把仍存在的定位控件误判为丢失。选框、拖动、
  缩放、换父均读取实际 `bx/by/bw/bh` 和父内容框/SlotHost；Flow 转 Float
  时先保存世界坐标，显式 fw/fh 表示最终 border-box，避免旧 CSS 尺寸
  让选框变大而控件不变。
- **目标 App 与编辑视图分离**：离屏 App 自持文档 CSS、基线缓存、主题、
  DPI 和事件；视图 zoom 只缩放最终图像。借用编辑器的样式上下文曾使
  继承/后代选择器度量不同，把 zoom 当 DPI 又会二次缩放。目标 DPI 是会话
  预览选项,不写进设计稿;开发屏 150% 时也要能预览 175%/225%。验收比较
  100%–300% 每 25% 一档的同目标 DPI 整窗几何和全部像素,再操作真实 IDE;
  另断言逻辑尺寸及 CSS 长度的预期缩放,避免双方共用错误却像素相等。
  纯序列化往返不能证明所见所得。原生建窗/改尺寸还须断言实际客户区：无边框壳若在
  `WM_NCCALCSIZE` 中已移除系统框，就不能再 `AdjustWindowRect` 外扩；
  150% DPI 真窗曾把 480×320 的设计开成 742×536，而离屏是 720×480，
  右下锚点随之偏移。逻辑尺寸与设备尺寸入口只差 DPI 换算，共用精确
  客户区契约，不能靠预览补出多余边框。
- **编辑坐标反算须可还原,先减父原点再换单位**：正向整数 DPI 缩放会截断,
  反向再截断曾使奇数 fx/fy/fw/fh 在 125%/175%/225% 等档反复编辑时丢一
  逻辑像素。反算选择能复现设备值的逻辑整数,对正负数分别处理;相对位置
  先用设备坐标减实际父内容框原点,不要分别反算两个绝对坐标后相减。
  回归覆盖带符号的逻辑→设备→逻辑、奇数尺寸重复 Flow/Float、拖动/缩放
  与 Undo,以及目标 DPI 往返切换后作者文档不变。
- **保全 HTML，结构编辑按身份重映射**：保存 doc CSS、body class/style、
  htmlTag 和字符串/子索引组成的有序内容；删除、复制、换父后按原
  JsonValue 身份重映射索引。只保存 kind/kids 曾丢掉原生标签、选择器
  和混排文字；直接复用旧索引会指向另一个孩子。同级重排还须保持文本槽
  并重写子节点槽的显示顺序：只重映射身份曾让 MoveSel/SelToEdge 改了
  kids 顺序，混排画面却仍按旧序，验收要检查实际 elOrder 的控件引用。
- **GenForm 与 FormBuilder 装配契约 100% 对齐**：HTML 容器节点导出的
  JSON 结构常为 `kind: "Panel"` 且带有 `htmlTag`。GenForm 在发射子节点
  装配代码时，若父级是 Element（满足 `IsElement`：带有 `htmlTag` 或
  `kind == "Element"`），必须调用 `parent.AddKid(child)` 而不是 `parent.Add(child)`；
  调用 `Add` 会绕过 `elOrder`，使子节点被推迟到全部内联文本流条目之后，
  导致编译后 AOT 窗体与设计器预览的行盒混排流序与几何出现偏差。
- **最终窗形与运行窗口共用遮罩**：逐 region 计算并集，圆角 SDF 保留
  `min(max(qx,qy),0)` 内部项；旧步进曾跳过后续区域，缺内部项使 radius=0
  整块半覆盖。区分 straight/premult，预乘缓冲须满足 RGB≤A：
  `0x80FF0000 / cov128 → 0x40FF0000 → 0x40400000`；只乘 coverage 曾造成
  亮边。快照只读借用 surface，复制到自有缓冲后裁切，避免污染原帧。
- **预览状态不反写作者文档**：进出预览从文档恢复运行树，避免已切换的
  Tabs 状态泄漏回编辑页。中键、抓手、Ctrl-wheel 归编辑器，并取消目标
  press/anchors；否则平移会拖 splitter、缩放会滚内容或留下误点击。
  普通滚轮使用局部坐标和正常 App 的认领/至多一次重放；缺重放曾吞掉
  刚进入滚动区域的第一格。Tooltip 等静态待办和 App.FrameApp 须在嵌套帧
  进入时切换到目标，退出时在 finally 中恢复，异常取消也须走相同出口；
  否则 HTML title 提示会读取宿主焦点或串到外层画布。离屏延迟帧请求还须
  转交可见宿主兑现，否则停稳悬停后没有事件唤醒预览。离屏帧不重置宿主
  Dispatcher 或平台 IME。

## 验证定式

- 纯逻辑（Html.Parse/绑定/同步）headless 可跑，无窗口：conformance
  形态的探针 + 金样（CRLF）。
- 布局几何：oracle 逐盒对比（同一份 .html 喂 Chrome 与引擎，
  getBoundingClientRect vs Arrange）。
- 手工编 GUI 设计稿程序：设计稿 html 首参 + 全部 src + 
  `--subsystem windows`；跑起来截图对照（锚定 PID），交互用
  UiDriver 可重复驱动。
- **真窗冒烟要带主窗骨架，裸页截图会"缺导航栏"**：页面文档只含
  页内骨架，图标导航栏在主窗文档里——把单页 `Build(null)` 直接装
  进裸 Form 截图，看到的永远是"少了左侧导航"（不是回归）。要看
  真实形态就复刻 `Root` 的装配（UseAppCss 拼表 + 主窗文档 Build +
  各页宿主挂页 + Pick），跳过登录泵。
- **截屏三坑（当天连踩五次才抓到图）**：① 带中文注释的 `.ps1`
  以无 BOM UTF-8 落盘，PowerShell 5.1 按 ANSI 误解析，多字节序列
  会吞掉下一行语句——参数/环境变量"神秘变空"多半是这个，自动化
  ps1 只写 ASCII；② bash 的 `$!` 可能是包装进程——以 ProcessStartInfo
  返回的子进程 PID 枚举窗口，并用 GetWindowThreadProcessId 核对归属，
  不按名字批量关闭；遮挡时用 `PrintWindow`（flags=2）抓整窗；
  ③ 150% DPI 下先核对逻辑尺寸、物理客户区和缩放：只有证实冒烟窗口
  按物理尺寸错误打开，才可把右缘裁切归因于环境；修正宿主尺寸后仍须
  比较同 DPI 的整窗几何与像素，不能用 headless 通过豁免截图差异。

## 游戏接入边界

菜单/面板/商店这类低频交互面可嵌进游戏帧循环（同一引擎，脏区门控，
实测 clean/dirty 均值 3ms、峰值 23ms、预算 16.6ms，空闲 120 拍仅
3 帧重绘）；**每帧热路径 HUD（血条/伤害数字/小地图）仍走代码直绘
+ 门控渲染，不进 DOM**——那是性能边界，不是能力缺口。
