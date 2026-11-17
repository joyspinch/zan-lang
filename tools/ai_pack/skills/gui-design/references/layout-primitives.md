# 排版原语纪律——界面为什么会重叠

> SKILL.md 主干只留硬规则索引；判定顺序与逐坑细则在此。
## 排版原语纪律(硬规则)——界面为什么会重叠

现代 Zan 运行时提供了基于标准 CSS 盒模型的完整布局原语：**HTML+CSS 模板声明**、
**Flex 盒模型**（`Flex.Row()/Column()`，主轴/交叉轴对齐全由档位类控制）、
**Grid 网格**（`Grid.Of(n)` N 等宽列，或 CSS grid）、**标准流式容器**（`Panel.Column()/Row()`）。
历史上的 WinForms 停靠流（`Dock(1..5)`）已被彻底废除淘汰。
`Control.Arrange` 里只有绝对定位（手摆 `mx/my` + 手工宽高）能产生重叠。
AI 生成的界面之所以"经常重叠在一起、尺寸位置乱七八糟"（实证:大刷新钮盖住旁边的省略号钮、
四张指标卡宽窄不齐、底部大片死空间），根因全是**绕开布局原语、按像素手摆控件**——
手摆坐标没有弹性兜底，窗口一缩放就散架。

硬规则:

0. **禁止在自动布局中手写绝对坐标**: 凡处于 `Panel`、`Flex`、`Grid`
   或流式容器中，**严禁调用 `Place(x, y)`、`Float(x, y)` 或手设 `mx/my`**。
   最终坐标完全由父容器算法根据方向、对齐与间隙统一计算，手写数值会被底层完全忽略
   并触发 `gui-lint` 警告。排版对不齐时，绝对不要调试数值试探，改用父容器的 `Gap`、
   `Padding`、对齐类或更换容器。
1. **窗口骨架首选 HTML 模板或 Flex 盒模型**: 侧栏采用弹性伸缩固定宽
   （如 CSS `width: 240px; flex-shrink: 0;`），内容区挂 `.grow`
   （CSS `flex: 1; overflow: auto;`）。弹性容器子节点按标准盒模型自适应，
   永远不会互相压住。
2. **内容面用流式/弹性容器**: 一行多物用 Flex（`.Gap()` / `.Between()` / `.Wrap()`），
   卡片墙/指标行用 `Grid.Of(n)` 等宽——**不要逐个手设宽度**（宽窄不齐就是这么来的）。
   子项通过标准的 `Add()` 挂入容器，或直接在 HTML 模板中嵌套声明。
   吃满剩余空间挂 CSS `grow` 类（或代码调用 `.Grow()`）。
3. **手摆 `mx/my` 只属于画布类场景**:游戏场景、图表自绘、自由画布设计
   导出的坐标。表单/工具窗口里出现手摆坐标就是错的,先问"该用哪个容器"。
4. **尺寸只 `Prefer` 语义值,高度让控件自己量**:Flex/Grid 的容器高度按
   内容测量,不写死像素高度(死空间和裁剪都来自写死)。
5. **交付前跑重叠自检,清零才算完**:`ZAN_GUI_OVERLAP=1` 运行一次窗口,
   每对压在一起的兄弟会打一行
   `gui-overlap #N in <父>: <控件>[x,y w×h] overlaps <控件>[…] by ax×bypx`
   (窗口子系统程序无控制台时设 `ZAN_GUI_OVERLAP_LOG=<文件>` 落盘;
   测试里可 `Control.DebugOverlap = true` + `Control.OverlapHits` 断言)。
   刻意叠放(角标/悬浮装饰)对那个子控件挂 `.NoOverlapCheck` 免检,
   其余命中必须修到 0。命中为 0 的界面,必然不存在"叠在一起"。

6. **文字控件的宽高永远不手写**:按钮/勾选框的大小 = 文字 + 皮肤内边距,
   由测量自算;`Prefer(w,h)` 钉死按钮,改文案就裁字、换皮肤就变形,
   AI"一个按钮调来调去"反复微调的正是这个数。交付前 `ZAN_GUI_LAYOUTLINT=1`
   跑一遍,三类命中必须清零:`gui-lint … 固定尺寸`(手写宽高)、`拉高`
   (按钮被拉成巨块)、`裁剪`(矩形装不下文字);图标钮、画布图元、分隔条
   等刻意定尺寸的挂 `.FreeLayout`(同一张牌同时免重叠与尺寸两检;
   测试用 `Control.DebugLayoutLint = true` + `Control.LintHits`)。
   **同一条布局代码改到第二遍就停**:不是数值没调对,是结构选错了,
   回到规则 1-3 换容器。

7. **布局以 CSS 的最终计算值为准**。`Panel.Row/Column` 为流式容器；
   不再依据 dock 数字或旧停靠分支推导排布。Flex 的剩余空间分配读
   `StyleGrow()`，声明 `flex-grow: 1` 最明确。`Grow()` 当前设置控件
   `grow` 字段，具体容器如何消费须核对实现，不能声称它仍写 dock=5。
   Row/Column 混用时，避免同一条 CSS 把两者的方向覆盖为一个方向。

8. **边缘留白归 padding，子项间距归 gap**。不要用首尾撑条模拟边缘
   内缩，也不要套用旧停靠引擎逐项扣 gap 的推导；按最终布局树核对。

9. **贴底用有确定高度的 Flex 列**。按“内容 → 页脚”文档序挂载，
   内容声明 `flex-grow: 1; min-height: 0`，页脚声明 `flex-shrink: 0`。
   `dock=2` 不会产生底部停靠效果。

10. **有意退出流才用 absolute**。`b.Float(dx, dy)` 当前转调 `Place`，
    写 `position: absolute; left/top`；不需要也不存在 DockManual 开关。
    子项挂到哪个包含块、如何定尺寸，按 CSS absolute 与父盒模型核对。

11. **`Label` 原生支持 `text-align`（left/center/right）**。
    `Label.OnPaint` 会读取样式的 `s.align` 与 `padding`/`border`，在单行或
    折行（wrap）模式下直接完成居中与靠右偏移绘制。因此无需为了给文本居中
    而在外面额外包一层 Flex 容器，直接在 CSS 里对 `label` 或类名声明
    `text-align: center` 或 `text-align: right` 即可生效。减少冗余嵌套容器
    能显著减少控件树层级并提升大规模界面的测量/排版性能。同理：控件默认尺寸/配色跟原图不一致时**先改皮肤规则**（如 stdlib
    `Pagination` 默认吃 `heightMedium` 34 逻辑，原图页脚只有 20 逻辑，加一条
    `pagination { height: 20; font-size: 12; }` 即可），不要回去自绘、也不要在
    每个使用处补坐标。

12. **图片类子项要声明宽高,否则被交叉轴 stretch 拉满整行**：行/列容器的默认
    `align-items` 是 stretch，子项**没在样式里声明宽高**就按容器交叉轴铺满。
    `Prefer(w,h)` 只钉住测量尺寸，不阻止拉伸——要按声明尺寸摆它，得用内联
    CSS：`img.SetProp("style", "width: 88px; height: 88px;")`，或给容器
    `AlignStart()`。为什么：stretch 判定读的是 `StyleDeclaresWidth/Height`
    （样式表里有没有 width/height 声明），`Prefer` 不算；实测标题图被拉成
    1920 宽的一条横幅，卡面图同理。

这两道闸门随工具链走:安装版 SDK 是发布时刻的冻结副本——早于对应闸门合入
的安装里没有它们,环境变量静默无效(skill 跑在工具链前面时先查工具链日期,
如安装目录 zanc.exe/stdlib 的时间戳)。正确动作是升级工具链后用内建闸门;
不要在业务代码里自研扫描器当长期替代——内建语义更全(签名去重/免检牌/
停靠+flex 两条出口),自研版升级即死代码。升级前的临时探针可以,但要标明
临时、升级后删。


自定义控件的子类契约:写 `Control` 子类(自绘控件/画布图元)必须有显式
构造器调用 `InitControl(名字, 兼容整数参数)`;隐式默认构造器不会跑基类字段初始化,
`children` 为 null、`visible` 为 false,首次 `With`/`Arrange` 即段错误。

排版容器三条实测：
- **`Panel.Row/Column` 当前直接走 Flex 排布**：工厂设置 `flexFlow = 1`，
  `StyleDisplay()` 返回 Flex；Add/With 按顺序排布，方向来自容器 layout。
  子项增长按 `StyleGrow()` 的 CSS 计算值声明，不套用已删除的停靠引擎。
- **flex 容器内容超出主轴就按比例收缩所有子项**（flex-shrink 默认 1）：定高的
  外框列里放「顶带 + 自适应中排 + 页脚」，中排按内容测量比可用高还大时，顶带
  从 38 被挤成 36、1 设备底线挤成 0。撑满项写 `flex-grow: 1; flex-basis: 0`
  （从 0 起分空间，不再溢出），定尺寸项写 `flex-shrink: 0`。
  **引擎的收缩没有 min-content 下限**（CSS 的 `min-width:auto` 未实现），
  所以图标钮会被切掉几像素、按钮会被压成 **0 宽**（看不见也点不着）——固定
  尺寸的图标钮/工具条必须显式 `flex-shrink: 0`。坑出处：某个即时通讯模板的
  聊天页在窄窗口（主窗口改到 520 逻辑宽）下，标题行两个图标钮被压成
  34×35（图标缺一块）、输入区工具条末项被压成 0×42（按钮消失），
  `ZAN_GUI_LAYOUTLINT=1` 报 4 条「裁剪」；加 `flex-shrink: 0` 后同一探针
  0 命中。**框架 `base.css` 里没有 `shrink-0` 工具类**（`Flex` 类注释承诺过，
  实际没定义；base.css 烤进驱动、改它要重建驱动），模板皮肤层要自己补一条
  全局 `.shrink-0 { flex-shrink: 0; }`。
  **ToolStrip 宿主必须是 flex 容器**（`display: flex`）：ToolStrip 是块级子项
  时宽度=宿主宽，窄窗口下宿主不够宽，工具条项按剩余宽被裁——最后几个直接
  0 宽。宿主改成 flex 行后工具条成为 flex 项，挂 `shrink-0` 即保持自然尺寸
  （溢出而不让位）。
  复现手段：把主窗口改窄（只在探针副本里改 `Form.Create` 的宽高，别动模板）
  再跑一次 `ZAN_GUI_LAYOUTLINT=1`——窄窗是这类"被压扁"缺陷的唯一触发态，
  正常宽度下全量重跑永远 0 命中。
- **`Prefer(w, 0)` 是"撑满可用高度"，不是"高度自适应"**：布局把声明高度 ≤ 0
  当 fill。要定高就写显式数（面板 710 设备）；要按内容就别写 Prefer 高度。
- **显式 `Padding(top, …)` 与类上 `padding` 的优先级是"显式赢"**（padSet 优先；
  `Panel.StylePadT` 曾漏这条、被类 padding 顶掉致面板顶从 167 掉到 164，已修
  stdlib）。规则：同一元素不要两头都给——要么皮肤给 padding，要么代码给
  Padding，混用只会互相顶。
