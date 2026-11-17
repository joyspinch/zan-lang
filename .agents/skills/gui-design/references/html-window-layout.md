# HTML 窗口——布局与样式

> 从 html-window.md 拆出：浏览器等价子集、CSS 两层、高度归属、浮动件定位、CSS 合并与首测量、双范式布局、复合控件。

## 布局：按浏览器等价子集写，别按浏览器全量写

引擎支持 flow / inline 行盒 / flex / grid / float / overflow 滚动，
经 Chrome getBoundingClientRect 逐盒对比（oracle）校准，多数场景
0px 偏差。CSS 方言之外的东西写上去不报错也不生效——动手前查支持度
审计脚本与 `references/css-dialect.md` 的与浏览器差异清单；"改了没反应"先怀疑
没进方言，再怀疑优先级。

## CSS 两层与基线坑

- 皮肤基线（base.css，`flex{display:flex}` 等 UA 规则）+ 应用级
  （`app.UseAppCss(...)`）。颜色走 `var(--*)` 令牌，尺寸走阶梯。
- **基线 base.css 由编译器内嵌兜底，磁盘上不要再放一份**：编译器把 stdlib
  皮肤目录整份烤进 exe，运行时是**磁盘优先、内嵌兜底**——exe 旁那份
  base.css 会盖住内嵌基线，"内嵌兜底到底生效没有"永远测不出来。
- **暂存形态分两种**：单文件形态（`--embed <pack.css>=skins/<名>/skin.css`）
  只内嵌自家皮肤包、不顶替基线，编译器仍自动内嵌 stdlib 基线，**不要**拷
  base.css；目录形态（`--embed skins`）只有该目录自带 base.css 时编译器才
  跳过 stdlib 基线内嵌，此时要换基线就得给**完整**基线，否则基线层整层丢失、
  flex 全退化成叠矩形（无为修仙传百艺页签全叠的根因：只带自家 skin.css 的
  目录暂存吞掉了 stdlib 基线内嵌，编译器已把跳过条件收紧）。
- 运行时换肤是实例方法：`form.GetApp.UseSkin("neon")`——按静态
  `App.UseSkin(...)` 调会报 cannot call instance method（踩过）。

## 高度归属：一行有多高由谁说了算（框架层硬规则）

设计稿里每个字段的**高度有且只有一个来源**，写错来源就是"改了高度不生效、
行与行叠在一起"。

- **三种高度，三条归属**（`FormField.DocHeightType` / `IsFixedHeight` /
  容器）：
  1. **皮肤定高型**（输入框/下拉/开关/标签/OTP…）：高度归**尺寸档**
     （tiny 24 / small 28 / medium 32 / large 40），设计稿**不写高度**。
     写了就会压掉尺寸档（表单整体改小一号时高度不变）。
  2. **内容型**（多行文本/附件/表格/树/时间线/图表…）：样式表没有默认
     高度，设计稿**一律写高度**，值取类型标准行高
     （`FormField.DefaultHFor`：多行文本 62 / 附件 78 / 表格 104…），
     作者改过就写作者的值。不写就塌成一行高、内容溢出压到下一行。
  3. **容器型**（面板/卡片/标签页/分栏…）：高度**由内容自适应**，
     设计稿默认不写；作者在自由布局里显式定了才写。
- **作者声明的高度优先于样式表**（CSS 的内联 > 样式表）：文档里的
  `fh` 走 `Control.DeclareHeight` 通道，运行时与预览都按它排；
  代码里手搭控件的 `logH` 只是"测量偏好"，尺寸档照旧说话——
  两条通道别混，否则代码里量尺寸的尺寸档演示会失效。
- **旧稿里的 `fh=32` 是历史默认不是作者意图**：当年给每个字段写死 32，
  载入时按上表还原（内容型→标准行高、其余→没写）。容器被钉死在 32
  行高正是"内容溢出、和下一行叠"的直接原因。
- 验收：设计器预览与生成运行时的**同一棵树**逐节点矩形一致
  （`conformance_gui_designer_wysiwyg`）；相邻两行的底 ≤ 下一行的顶。

## 浮动件的定位基准（框架层规则）

CSS absolute 与 `Control.Float(x, y)`/`Place(x, y)` 用同一定位语义，
坐标依据父包含块与 `ArrangePositioned` 的实际实现。普通顶栏、内容区、
页脚按 Flex 文档序排布；不要依据已退役的 Dock.Top 推算自动避让。
设计器把流式件转为 absolute 时，应保持当前视觉位置并写 CSS 定位，
预览与运行时必须共用同一棵控件树和包含块换算。此处未重跑像素验证。

## CSS 合并隔离与 HTML 文本首测量

- **声明块必须递归 Clone**：`Css.MergeInto` 展开分组选择器时，每个选择器须得到独立声明块；`StyleSheet.MergeSheet` 合入时同样递归 Clone，隔离源表与目标表。浅拷贝会别名共享 `__zan_lengths` / `__zan_prescaled` 等内部状态，公开 CSS 字符串虽正确，有效几何、字号与颜色仍会被串改。验证须检查分组规则及源表隔离后的实际样式与布局，不能只比较字符串。
- **设计稿文本走语义 SetProp**：HTML 装载写文本须调用控件 `SetProp`，不能直接 `PropSpec.Write`；语义 setter 在 `MeasureTree` 前创建 retained generated caption，才能让首次自然尺寸稳定。直接写属性值可能读回正确却缺少 caption，不能靠先 Paint 一次补齐。验证应在未绘制时断言 caption 与首测量，并检查重复测量尺寸不漂移。

证据按用例限定：`conformance_gui_match3_retained_hud` 在
`build/test-out/gui-refactor-old-targeted-20261113-163236-258876/result.json` 中正式及同 exe strict 通过；
该批次整体失败，不能写成整批通过。`case_retained_property_contract` 的通过收据为
`build/test-out/gui-refactor-old-targeted-20261113-163906-501763/result.json`，同样正式及同 exe strict 通过。
两用例均精确 stdout、空 runtime stderr、exe 不变，所在批次输入及编译器无漂移。
这些 strict 证据只覆盖对应 fixture；Match3 的原生证据及边界见“游戏接入边界”，不外推为 GUI form 或整个 Gui 验收。

## 双范式布局：本次分配宽度与内在测量边界

- **分配宽度是本次测量状态**：`MeasureTree(App app, int width)` 接受已分配的 border-box 宽度，以独立状态保存并在 `finally` 恢复，不写回作者的 `HintWrapWidth`。`RenderInside`、带宽度的 `RenderAt`、`UiViewScope.Layout` 在测量前传实际宽度；换行 Label 用当前内容宽计算高度。子项 auto 宽度先扣水平 margin，显式 content-box 尺寸先按 min/max 钳制再加 padding/border，避免窄宽切换残留旧高度或盒模型少算。
- **分配与重测共用布局事实**：Grid 显式传递当前 `App` 宿主，列宽确定后按 slot 重测子树再求行高，不依赖脱树 HTML 节点原先保存的 `app` 字段。nowrap Flex 行的约束测量与排列共用 `FlexLineSizes`；wrap Flex 行按当前内容宽重新分行、累计高度。HTML 与声明式对象树必须比较实际几何和滚动范围，不能只比样式字段。
- **直接测量保留显式样式边界**：完整子树约束重测仅在 scoped `MeasureTree` 内启用；`measuringTree` 保存旧状态、置位后执行，并在 `finally` 恢复。直接调用 `MeasureGridContent(app)` 保留调用者显式提供的 `computedStyle`，不触发重新解析样式的子树测量。
- **内在遍历不重复启动约束遍历**：确定列轨的 Grid 延后内在子树测量，直接按列槽测量；内在测量 flag 阻止内在遍历再次触发约束递归，消除该路径的指数式重复。嵌套且依赖内在尺寸的树仍可能二次增长，不能推导全树线性、零分配或普遍提速；源码精简仍未完成。
- **状态栏用公共单行布局承诺几何**：`statusbar` 基线使用 Flex row、交叉轴居中、overflow hidden，直属 Label 声明 `min-width:0;flex-shrink:1`。一个弹性 spacer 分隔左右组，右项用标量 order 从右向左扩展；公开索引保持添加顺序并排除 spacer，Clear/重建只保留一个 spacer。检查实际矩形在栏内、同排居中且互不重叠，覆盖左右混合插入、窄宽往返、动态文本及多 DPI；裁剪不能替代正确布局。
- **显式最小尺寸替代自动内在下限**：Flex 收缩在没有主轴 min 声明时才使用 `MinContentMain`；显式 `min-width/min-height`（含 0、正值、百分比）经既有 clamp 路径替代该自动下限。最终尺寸仍按可让空间分配，回归断言最小值、相邻关系和总尺寸，不要求每项恰好等于正最小值。
- **回归用真实文本与状态往返**：覆盖 100/150/200 DPI、窄→宽→窄、真实长文本、缩短/再增长、RemoveAll/恢复及滚动范围；覆盖 `minmax(0,1fr)` Grid、nowrap Flex、根声明宽与实际宽冲突、margin/content-box 与 wrap Flex。七层嵌套的叶子测量次数 `<= depth + 2` 只约束对应夹具，不证明任意树复杂度。

直接 strict 探针定位：`scroll_routed_input` 的 `build/test-out/gui-layout-20261113T160722.781351Z/report.json`，
以及 `view_scope` 的 `build/test-out/gui-layout-20261113T160634.712937Z/report.json`，两者 compile/run exit 0、success true。
直接探针只证明相应夹具；正式收口及源码版本边界见本文件「双范式布局：本次分配宽度与内在测量边界」小节，各用例通过不构成整个 Gui 验收。

## 复合控件的布局与真实输入（2026-11-14）

- **嵌套按钮保留最近子控件的捕获**：按钮冒泡处理 Down 时只在当前没有捕获者时捕获自身；Up 只释放自身持有的捕获。Tabs 关闭图标的 Down 必须捕获关闭 Button，Up 合成一次关闭；直接调用 Click 事件绕过这条路径，不能验证捕获正确。
- **CSS 对应实际生成节点**：Tabs 标题与关闭目标是 `button.tab-item`、`button.tab-item-close`，伪元素规则不能代替真实子控件样式。分别验证 line/card/segment、横竖方向、选中边框与图标中心；关系选择器的独立 ResolveStyle 查询须提供当前节点上下文，或读取当前布局的 computedStyle。
- **长标题约束整条祖先链**：标题可收缩、关闭按钮不收缩，并同时约束导航列与 header 的最大宽度；只给 header 加百分比上限仍可能随过宽导航列一起溢出。自动显示选中项按 ScrollViewport 的可见内容矩形计算，包含边框和滚动条的外框不能作为可见边界。
- **异步动作带树版本**：用数字索引捕获 Tabs 选择/关闭/添加动作时，同时捕获构建 generation；重建后旧动作失效。弱 owner 只避免生命周期保活，不能证明索引仍指向原项。用真实 Down → Post(重建) → Up 验证，不假设 Up 返回后动作仍在队列里。
- **首次测量前建立内容**：ToolStrip 创建按钮时就用 `Button(caption)` 和 icon 属性建立真实子节点，不能等 Paint 中 SyncBinding 再补标题。工具栏明确使用 Flex row，共用弹性 spacer 和 order 区分左右项，公开索引排除 spacer；分隔符禁用且不产生点击回调。
- **宿主先接收组件输入结果**：把业务 activeTab 回写 Tabs 之前先消费组件 TakeChanged，避免覆盖上一输入轮次的选择；延迟创建的组件须先确认实例已存在，首次帧没有待消费事件。同时复核原生 IDE 的标题切换、关闭、上下文和添加路径。组件矩形断言、事件数量和实窗截图各有边界，单项通过不代表完整 Gui 验收。
- **长工具项保留相邻操作命中宽度**：仅测短标题会漏掉短按钮被长标题挤到 10px 的问题。按钮以主题 height-large 为最小宽度，图标不收缩，标题收缩；代码与 HTML 都要在窄宽、多个 DPI 下于首次 Paint 前断言矩形，并用真实输入点击每一项。该规则不代表已有通用溢出菜单。
- **菜单状态落到实际子节点**：保留树 Menu 的 .menu-item、标题、组和 separator 需要真实 CSS，旧 menu::item 不能替代。禁用组按钮也要 Disabled；非空 selectedKey 优先于旧数字索引。重建用 RemoveAll 退休捕获/父链，弱 owner 加 generation 退休旧动作；验证多层组和重建期间的 Down/Up。
- **组件尺寸进入测量契约**：Avatar 等公开 Size/Diameter 的组件须在 OnMeasure 给出对应两轴默认尺寸，不能让字形宽度代替直径；显式单轴尺寸仍由布局引擎保留。回归同时测短/长标签、公开尺寸与实际 Flex 行排列，flex-shrink:0 只能禁止收缩，不能补出缺失的首选尺寸。Flex 居中须同时声明 align-items/justify-content；长标签另设 min-width:0、最大宽度与收缩边界，断言实际内容中心及包含关系，不能只检查外框为圆。
- **菜单 Escape 用按键事件并消费一次**：legacy ContextMenu.Render 与 retained 弹层都应按 App.KeyPressed(27) 的窗口/消费约束处理退出，返回已有 dismiss 结果并 ClaimKey；不能在各业务窗口重复补 guard。UiDriver 的 char 27 是字符事件，应发送 keydown 27 0 与 keyup 27 0，回归同时覆盖字符、释放、其他键、消费后不重复。
- **绘制与输入挂载分别验证**：RenderInside/RenderAt/RenderTree 负责测量、排列、绘制，命中转储有区域仍可能脱离当前输入树。混合宿主复用 ControlInputHost 借用控件树、UiViewScope 在输入前布局；重建先退休旧 scope/host，Bind 保留其他 adapter，禁止每帧 SetRootControl 覆盖整个输入链。实窗确认标题切换、非当前关闭、最后标签关闭后的有效页面；UiDriver click/ev 使用物理客户区坐标，不二次乘除 DPI。
- **退出清理覆盖仍打开的子窗口**：向导 renderCb 可能捕获自身，closeCb 也可能通过业务 owner/dlg 成环；主窗口退出先用局部强引用 Close 当前子窗口，走既有回调与 Teardown 清链。分别验证正常关闭子窗和主窗带子窗退出；非视觉 HWND 取消只证明生命周期，不证明子窗排版。单 App UiDriver 的主 canvas 截图不能冒充独立 HWND 截图。
- **实窗验收读取捕获的帧异常**：stdout/stderr 为空且进程正常退出仍可能每帧 catch 异常。运行前后记录可执行文件旁 zan_ui_errors.log 的新增部分；存在新错误就失败，并结合实际编辑区、探针和截图复核，不能只看脚本走到末尾。
- **生成操作节点与作者内容分开克隆**：Tag 关闭按钮在构造时创建，属性 setter 在首次测量前同步可见性；CloneContentTo 只跳过该实例，保留作者按钮、图标及专属文字的样式/顺序，并恢复目标的文字节点身份。SetText 不按首节点类型认领用户 Label。重加已有节点时，先完成 Adopt 再定位生成按钮，避免移除旧位置或取消回调使索引失效。所有可克隆的子节点 Kind 都须有公共工厂注册，不能让 null 克隆悄悄丢内容。
- **排队关闭核对条目身份**：DynamicTags 关闭动作弱引用创建它的 Tag，执行时同时核对当前下标和子节点身份；列表重建后旧动作失效，重复文字不能用文本匹配代替身份。保留 UiEvent 已排队回调的通用语义，不为单组件取消请求改全局队列。回归须覆盖排队后删除前项、SetItems 替换及重复标签。
- **失焦通知逐段确认宿主所有权**：焦点切换先提交当前身份，再调用旧端点清理与 Blur；每个回调前分别确认旧端点仍归原 App，回调后按 revision 检查是否重入。语义失焦回调本身也可能转移控件，首次检查不能覆盖后续业务通知。真实 Down 建立捕获，在 PointerCancel 中绑定替代宿主并重新取得焦点，断言旧 Hide 不清掉新状态，再用 Up 验证正常释放；仅检查最终 root 无法发现新捕获被旧清理误伤。

### DynamicTags / Tag 与 HTML 标识边界（2026-11-14 冻结正式三项）

- **重建 proof 进入退休过程**：DynamicTags 每次 RebuildChildren 先递增 generation，以保存该代次的 action.Current 作为 `RemoveChildren(children, proof.Current)` 的继续条件；取消回调可嵌套 SetItems，返回后及逐项 Add 后都核对 proof，旧调用不得继续摘除或追加新一代子树。只在 RemoveChildren 返回后检查不足以保护退休过程。
- **旧构造器也保存身份**：保留 `DynamicTagsAction(tags, index)` 两参入口，创建时快照当前 generation 与该索引的 Tag；Close 同时核对代次、有效索引及当前子节点身份。当前动作正常关闭，重建后的旧动作失效，不能因缺少 source 而让旧 API 永久无效。
- **编辑由真实 Input 完成**：StartEdit 在挂载后 Focus 实际 editor，挂载前开始的编辑在 OnMeasure 补焦点；Enter 接 Input.Submit，Escape 接 routed OnKeyDown 并 Handle。重建移除旧 Submit 订阅后接新代次动作，不在 Paint 轮询输入。回归在零 Paint 下验证焦点、输入、提交/取消，再重复绘制确认不重放；不据此新增失焦提交语义。
- **树拥有文字，缓存只借用**：Tag 的 owned Label 缓存用 weak；新 Label 先由局部强引用持有，再 Add 交给子树。移出后即使 Tag 仍存活、未再 SetText，Label 也应释放；单测同时保活 owner、只弱持移出的 Label，避免测试自身延长寿命。
- **HTML id 早晚都直写元数据**：Html.Build 初次分配 id 与属性回放后恢复 id 都直接写 `Control.name`，不能走 `SetProp("name", id)`。IconView.Name 是字形，id 不得覆盖默认空字形或注册工厂的默认字形；验证 id-only、克隆与工厂默认字形，不能仅靠末尾恢复 id 掩盖首次 setter 的副作用。

证据：`build/test-out/gui-followup-cases-20261114T170531.457974Z/report.json` 的
`tag_close`、`control_factory_equivalence`、`html_props_equivalence` 三条 exact conformance 均 Passed，
pass=true，compiler 前后均 `d08d28837b05b41d85279faff258646972d7f11308108c879fbd042d54d9d5ef`，
changedInputs/changedRegistration=[]；结论限于这三条，不外推完整 GUI 或原生窗口验收。
