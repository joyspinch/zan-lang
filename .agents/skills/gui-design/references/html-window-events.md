# HTML 窗口——事件体系与关闭生命周期（框架层）

> 从 html-window.md 拆出：声明式事件体系与组件事件契约、焦点通知与结构退休、关闭回调 ARC、关闭排空 Task。

## 声明式事件体系与丰富组件事件契约

彻底消除“HTML 仅作空壳、后台繁琐查找控件手动挂接”的反模式，由 HTML 模板承载完整结构、交互声明与事件语义，Zan 代码只作为 ViewModel / 业务逻辑层。

1. **统一声明式事件指令**：
   - 支持 `@event`、`v-on:event`、`z-on:event`、`on<event>` 及 `data-on-<event>` 等标准声明语法。
   - 解析层自动完成 kebab-case 到组件标准 PascalCase 事件名的规范化映射（如 `@row-click` -> `RowClick`、`@cell-dblclick` -> `CellDoubleClick`、`@column-resize` -> `ColumnResize`、`@tab-change` -> `Change`、`@page-change` -> `Change` 等）。
2. **单通道事件与声明式事件表**：
   - `UiEvent` 只有一个订阅通道：`Add` / `+=` / `On<Event>(…)` / `BindEvent(name, …)` 均按处理器形状重载，
     `() => …` 与 `(Control s) => …` 共用同一事件，触发 `Raise(sender)` 先无参后 sender；`Count()` 计全部。
     不存在 `AddS/RaiseS/PostS/BindEventS/OnClickS` 之类的平行方法。
   - 控件**不重写** `BindEvent` / `TriggerEvent` / `Events()` 做字符串分派：在 `UiEvent` 字段或属性上标注
     `[Event]`（列入设计器事件表）、`[Event(Alias = "Toggle PageChanged")]`（附加别名，空格分隔）、
     `[Event(Hidden = true)]`（可按名绑定但不列出）、`[Event(Display = "Upload")]`（设计器显示名），
     基类 `Control.EventOf` 经 `EventTable` 反射统一解析。
   - **事件对象在 state/model 上的复合控件**（DataGrid、浏览器盒等）以属性公开：
     `[Event] public UiEvent RowClick { get { return this.st.RowClick; } }`；state 持有 `weak Control host`，
     触发处写 `st.RowClick.Raise(st.host)`，sender 处理器才拿到正确控件。
   - `TriggerEvent` 仅在需要附带交互语义（禁用门控、切换状态、焦点副作用）时覆写。
   - **属性声明（DefineProps）**：`Field("Name")` 不带值类型参数，值类型由字段/属性的声明类型
     （string/int/bool 或对应 `Binding<T>`）反射推断；键与标签可由字段名派生时用
     `PropSpec.TextField/IntField/BoolField/ColorField("CheckedText")`（键 `checkedText`、标签
     `Checked text`），否则 `PropSpec.Text("key", "Label").Field("Name")`；一条属性一个链式表达式
     `ps.Add(…)`，不再声明临时变量。编辑器种类与值类型用 `PropKind` / `PropValueKind` 枚举比较，
     禁止 `kind == 3` 之类魔法数。
3. **表格等大型组件的多维丰富事件（Row / Cell / Column / Grid）**：
   - 行级：`RowClick`、`RowDoubleClick`、`RowRightClick`（别名 `RowContext`）、`RowInserted`、`RowDeleted`。
   - 单元格级：`CellClick`、`CellDoubleClick`（别名 `CellDblClick`）、`CellEdit`、`CellValidating`。
   - 列与表头级：`HeaderClick`（别名 `ColumnClick`）、`ColumnDoubleClick`、`ColumnResize`、`ColumnResizeEnd`、`Sort`、`ColumnReorder`。
   - 网格视图与状态：`FilterChanged`、`SelectionChanged`、`GroupChanged`、`BandToggled`、`ScrollChanged`。
   - **双击命中坐标锁定**：在双击触发判定时必须锁定列索引（如 `st.hitCol = ptrCol;`），并同时触发 `RowDoubleClick` 与 `CellDoubleClick`，保证 `DataGrid.HitField` 能解析出目标列字段，供列声明的事件绑定（`@cell-dblclick` 等）精准路由。
4. **常用组件专属语义事件标配**：
   - `TreeView`：`Select`、`Activate`、`Expand`、`Collapse`、`Context`、`Check`。
   - `Pagination`：`Change`（切页）、`SizeChange`（切每页条数）。
   - `ListView`：`Select`、`Activate`、`Context`。
   - `Tabs`：`Change`（切标签页）。
   - 全部专属事件以 `[Event]` 标注声明，设计器清单与按名接线都由事件表自动生成。

5. **设计器画布纪律（ToolStrip 项与组件配置规范）**：
   - **紧凑型关闭按钮（MiniCloseBtn）排版纪律**：属性检查器（选项列表、CSS 样式表等）中的单项删除按钮禁止使用常规 28px/34px 操作按钮，统一采用 20px 紧凑圆底微十字按钮，避免挤占输入框与标签的空间。
   - **设计器画布网格避让渲染**：画布网格必须使用淡色微点阵（35%~40% 次级透明度）替代粗实线；绘制点阵时需检查控件包围盒（`fx, fy, fw, fh`），组件覆盖区域跳过绘制，彻底消除网格穿透遮挡组件界面的视觉干扰。
   - **常用组件属性契约闭环**：基础组件在设计器 `BuiltinPropRows` 中必须完整声明属性元数据，`Button` 等支持 `type` / `size` 变体类的组件在 `SetProp` 中使用 `SetClassIn` 维护互斥语义类，确保设计器即时配置所见即所得。

6. **窗体设计器事件交互与代码后置快速定位规范（新建/编辑/双击/快捷跳转）**：
   - **三合一快捷操作区**：属性面板「事件」选项卡的每个事件条目均配齐「定位」、「新建」、「编辑」按钮，并在底部事件输入框右侧提供一键「定位代码」，同时支持直接双击事件条目快速定位。
   - **语义化事件建议与自动绑定**：点击「新建」或在未绑定状态下点击「定位」/双击时，设计器自动按组件名与事件名推导标准方法名（如 `OnSubmitBtnClick`、`OnWindowLoad`）并写入事件绑定。
   - **智能代码后置（Code-Behind）定位与桩补齐**：通过 `handler:<方法名>` 请求宿主 IDE 切换至配对的 `.zan` 业务文件。若源码中已存在该方法，精准定位至声明行号；若尚未声明，自动在 `partial class` 类体末尾（最后一个闭合花括号前）插入干净的标准方法桩（`static void <方法名> { }`）并定位光标，实现所见即所得的极速事件开发闭环。

7. **窗体设计器属性强类型交互与多选排版对齐规范（下拉枚举/图标拾取/主选基准对齐/等间距分布）**：
   - **属性强类型化与下拉枚举（SelectBox）渲染**：属性提示中带有枚举候选项（如 `"Type (primary/info/success/warning/error)"`）时，自动解析出合法枚举列表并在属性检查器中渲染为 `small` 尺寸的 `SelectBox` 下拉选择框，替代繁琐易错的手动输入；标签展示时自动清洗掉 `(primary/...)` 后缀，保持排版精简整洁，同时保留 `(px)` 等合法度量单位。
   - **图标与资源可视化拾取（OpenIconPicker）**：属性键为 `icon` 时，在输入框旁渲染 `[图标]` 弹窗选择按钮，点击后拉起基于 `Icon.Names` 的可视化图标网格，点选后直接填充并触发画布即时重绘。
   - **多组件排版以主选为基准（Anchor Alignment）**：自由画布多选对齐（左/居中/右/顶/中/底）在单选时对齐至所属容器内容框，多选时必须严格以主选中项（`sel`）的位置与尺寸为几何基准（而非各元素独立归位到容器边框），符合直觉的矢量/GUI 排版体验。
   - **水平与垂直等间距分布（Distribute）**：支持多选控件（≥3 个）的水平等间距分布（`dist_h`）与垂直等间距分布（`dist_v`），保持首尾两端控件绝对位置不变，中间所有控件在剩余跨度内均匀分配几何间隙，消除人工肉眼对齐的繁琐工作。
   - **多媒体与图像资源浏览拾取器（Asset Picker）**：针对 `src`、`image`、`avatar` 及各类以 `Path` 结尾的媒体资源属性，在输入框旁渲染 `[浏览]` 按钮，拉起过滤了主流图片格式（`*.png;*.jpg;*.jpeg;*.svg;*.bmp;*.gif;*.webp`）的 `FilePicker` 弹窗；选中后自动调用 `RelativizePath` 将绝对路径转为相对于当前项目工作目录的标准短路径（如 `assets/player.png`），配合控件的 `SetProp` 实现画布即时渲染更新。

8. **窗体与场景设计器键盘交互与细节完善规范（退格删除/代码后置直通/颜色色块与预设轮换/场景Tint/透明度/锁定）**：
   - **键盘删除双键适配（Delete & Backspace）**：在无输入焦点（`focus.focusedId < 0` 且无弹窗/大编辑器打开）时，删除操作除响应 `kc == 46`（Delete 键）外，必须同时响应 `kc == 8`（Backspace 键），以完全对齐苹果键盘与无独立 Delete 键的笔记本电脑用户习惯。游戏场景设计器同理，在 `HandleKeys` 中必须同步适配，并在 `AnyInputFocused` 中防卫好包含类名（`inpIdClass`）与颜色（`inpIdColor`）在内的全部检查器输入控件。
   - **游戏场景元素事件直通 Code-Behind**：游戏场景设计器属性检查器的点击事件（`clickIn`）旁标配 `[代码]` 按钮。未绑定时自动推导生成 `On<ElementName>Click` 并存入元素动作；点击后置位 `openCodeBehindElement = "handler:" + h`，直通宿主 IDE 在场景 `.zan` 代码后置文件中精确定位或在末尾自动生成事件响应方法桩。
   - **颜色属性实时色块与预设主题色轮换**：对 `color`、`bgColor`、`textColor`、`borderColor`、`fill`、`stroke` 及 `*Color` 属性，在属性行右侧渲染 28px 交互色块；支持十六进制（`#RRGGBB` / `#RRGGBBAA`）与 CSS 函数（`rgb/rgba/hsl`）实时解析渲染；点击色块可在主流高频品牌/功能色预设之间快速循环轮换（蓝/绿/橙/红/紫/青/黑/白/灰），大幅提升配色效率。
   - **游戏场景元素染色（Tint RGBA）与透明度交互**：场景设计器对元素的 `cr, cg, cb, ca` 提供第一优先级支持。输入框提供十六进制颜色（`#RRGGBB`）并附带 28px 实体预览色块与点击轮换预设，下方配备透明度（0..255）步进调节，实现游戏界面按钮、面板底色、圆圈及文字颜色的即时可视化微调。
   - **场景设计器底部按钮规整排列与锁定/解锁切换**：底部操作按钮废除粗暴的单项拉伸排布，统一采用 2 列紧凑网格（显示/隐藏、复制、锁定/解锁、上移一层、下移一层、删除），使元素的锁定状态一目了然并防范误触移动。
   - **视图缩放快速重置（Ctrl+0）与多选状态顶部计数提示**：支持 `Ctrl+0` 快捷键瞬间重置画布缩放到 100% 并平移归零；多选组件时，属性检查器顶部目标提示自动显示 `(+N 个已选)`，让开发者对当前多选对齐与排版范围获得清晰确切的感知。
   - **层级升降与锁定快捷键体系（PageUp/PageDown/Ctrl+Home/Ctrl+End/Ctrl+L）**：窗体设计器与场景设计器全面对齐图层控制热键——`PageUp` (`kc == 33`) 上移一层、`PageDown` (`kc == 34`) 下移一层、`Ctrl+Home` (`kc == 36` + Ctrl) 置顶、`Ctrl+End` (`kc == 35` + Ctrl) 置底、`Ctrl+L` (`cc == 12`) 切换当前选中项的锁定状态，无需鼠标往返点击工具栏即可完成排版层级微调。
   - **画布双击直通事件与自动桩生成（Double Click to Code-Behind Handler）**：在场景画布上双击任意元素，设计器自动解析或推导默认事件处理函数（`On<Name>Click`）并回写属性面板与元素动作；随后通过 `openCodeBehindElement = "handler:" + h` 直通 IDE 打开代码后置 `.zan` 业务文件定位或自动补齐方法桩，并在双击帧屏蔽位移拖拽，对齐现代桌面 IDE 的无缝开发体验。

9. **声明式动态属性与动态样式类绑定规范（`:class` / `:disabled` 与通用 `:attr` 机制）**：
   - **动态样式类（`:class` / `data-bind-class` / `z-class`）**：支持三元表达式（`cond ? 'active' : ''`）与模型/作用域路径。求值时与初始静态 `class`（保存在 `baseClass`）做非破坏性合并；类名变更时自动清空 `computedStyle` 缓存，下一帧重新解析样式表规则，彻底避免覆盖静态类或样式滞后。
   - **动态禁用态（`:disabled` / `data-bind-disabled` / `z-disabled`）**：按条件表达式动态同步 `c.Disabled` 绑定，沿控件树向下继承置灰并切断交互。
   - **通用动态单向属性绑定（`:prop` / `v-bind:prop` / `z-bind:prop`）**：支持在模板上声明任意属性绑定（`:placeholder`、`:status`、`:title`、`:min`、`:max` 等），通过 `c.AddBindProp` 注册并在 `SyncFromNode` 每一帧中通过 `GetProp` / `SetProp` 对比写入；支持行作用域穿透（`template` 循环行内自动继承 item 作用域），并随节点克隆（`Html.Clone`）完整保真。

10. **现代桌面端（Dashboard）切页与高保真表单定式（实证踩坑与黄金解法）**：
    - **多页面切页：严禁在未激活面板上写内联 `style="display: none;"`**：
      - *踩坑实证*：在 HTML 设计稿中给未激活页面写了内联 `style="display: none;"`，后台代码在切页时设置 `page.visible = true` 却完全不生效，页面永远无法显示。
      - *原因*：`Control.StyleVisible()` 的判断逻辑是 `if (!visible) return false; return computedStyle.display != 2;`。HTML 内联 `style="display: none;"` 经 `ApplyInline` 将 `computedStyle.display` 设为了 2（none）。之后在后台只改 `visible = true` 并不会清除已固化的 `computedStyle.display`。
      - *黄金解法*：HTML 模板中所有切页面板标签**不要写内联 `style="display: none;"`**；页面初始显隐由后台 `OnLoad(Form form)` 首帧调用 `ShowPage(0)` 统一控制（通过 `page.visible = (idx == 0)`），这样 `visible = false` 干净隐藏、`visible = true` 干净恢复，零样式冲突。
    - **Checkbox 与 Radio 的文字标签：必须声明在 `<input>` 的 `value="..."` 属性**：
      - *踩坑实证*：按网页习惯写 `<label><input type="checkbox"> 记住我</label>`，复选框旁边的文字丢失。
      - *原因*：解析器将 `label` 折成独立 `Label` 控件，且含有 child `<input>`，内嵌的裸文本被忽略。而 Checkbox / Radio 在 Zan 原生控件体系中自带 label 文案属性。
      - *黄金解法*：Checkbox 与 Radio 的标签文字**直接写在 `<input>` 的 `value="..."` 属性中**（例如 `<input type="checkbox" id="chk1" value="把“病毒扫描”加入右键菜单" checked>`），`DesignerHtml.zan` 会将其自动映射为 `label` 属性并在控件内部右侧绘制文字，排版与基线对齐完美闭环。
    - **文本必须包裹在语义标签中（禁止容器 `div` 中散落裸文本）**：
      - *踩坑实证*：在 `<div class="tag">已保护<span>399</span>天</div>` 中，“已保护”和“天”丢失；在 `<div class="dropzone">拖拽到此</div>` 中，“拖拽到此”丢失。
      - *原因*：`<div>` 在解析器里兜底为容器 `Panel`。容器如果有子元素（如 span），散落在容器内部的裸文本没有对应的控件节点包裹，会被跳过。
      - *黄金解法*：所有需要呈现的文本内容，必须显式包裹在文本语义标签中：`<span>` / `<label>`（生成 Label 控件）或 `<p>` / `<h1>~<h6>`（生成 Typography 控件）。
    - **自定义无边框窗口操作**：
      - 窗口声明 `data-win-chrome="false"`，顶栏自定义按钮（`icon="minus"`, `icon="square"`, `icon="x"`），后台 `OnLoad` 订阅后直接调用 `__form.Minimize()`、`__form.ToggleMaximize()`、`__form.Close()`。
    - **Switch 自定义品牌色**：
      - `Switch.CheckedColor` 可直接赋予 ARGB 颜色（如 `unchecked((int)0xFFF58220)` 品牌橙），或在 CSS `:root` 中声明 `--primary`。
    - **现代安全软件/Dashboard 视觉精细化排版定式**：
      - *配色规范与纯净底色*：主视口采用纯白（`#ffffff`）或轻灰（`#fafbfc`），卡片边框采用柔和冷灰（`#eaedf2`），悬停时切换为微透品牌色边框（`#ffd8b3`）与淡阴影（`rgba(245, 130, 32, 0.08)`）；操作图标使用统一中性灰（`#5c6472`），避免杂色。
      - *字阶与对齐约束*：模块大标题采用 20px / 600 字重，副标题采用 13px / 400（字色 `#7e8794`，行高 1.5）；卡片项标题统一 14px / 600，描述文本采用 12px（行高 1.4~1.5）。
      - *间距与内边距档位*：视口外边距统一 40px，列表行卡片内边距统一 12px 18px，圆角统一 10px（大卡片 16~18px）；按钮/图标统一居中热区（36px~44px），消除几何错位与排版杂乱。


10. **声明式具名插槽路由与双向绑定修饰符规范（`slot` 与 `v-model.lazy/.number/.trim`）**：
   - **声明式具名插槽（`slot="xxx"` / `data-slot="xxx"`）**：复合容器（如 `Tabs`、`SplitPanel`、`Card` 等）覆写 `NamedSlotHost(string slot)`，在 `Html.AddChild` 阶段自动将带 `slot` 属性的子控件分发挂载到对应的内部宿主容器（如 `SplitPanel` 的 `left`/`right`/`pane1`/`pane2` 窗格，`Tabs` 的 `slot="Account"` 匹配标签页页面 `Page(i)`），完全免去在后台代码中手动拆分查找与挂载。
   - **双向绑定失焦防抖修饰符（`v-model.lazy`）**：在 `SyncChangedNode` 中通过 `c.HasFocus(host)` 检测用户输入焦点。处于获焦编辑状态时不触发每帧回写，直到用户失焦（Blur）或切换组件时一次性回写模型并更新快照，彻底消除键盘逐字输入过程中的状态抖动与频繁渲染开销。
   - **双向绑定类型保真修饰符（`v-model.number`）**：用户在输入框键入文本后，在 `WriteBoundValue` 回写阶段自动将其转换为 `JsonValue.NewNum` 数值节点存储，避免原本数字类型的 JSON 模型字段被降级污染为字符串类型。
   - **双向绑定首尾去空修饰符（`v-model.trim`）**：在回写前自动剥离字符串首尾的空白字符（`s.Trim`），可与 `.number` 复合链式声明（如 `v-model.number.trim="score"`）。

11. **文本胡子语法动态插值（`{{ expr }}`）与事件修饰符规范（`@click.stop` / `@keydown.enter`）**：
   - **胡子模板文本插值（`{{ expr }}`）**：在静态元素或模板行（`<template data-for>`）的文本内容中声明 `{{ user.name }}` 或 `{{ active ? '在线' : '离线' }}`。HTML 构建期将包含 `{{` 的文本捕获到 `c.bindTpl`；`ChildWindow` 在每帧 `SyncFromNode` 中调用 `EvalInterpolation` 解析各花括号内部的表达式，求值后回写到 `c.SetProp("text", val)`；支持行级数据作用域与根级模型混用及动态响应式刷新。
   - **复合标签与元子节点插槽无缝集成（`<tabs>` / `<tab title="...">`）**：`<tabs>` 容器既支持显式声明 `slot="Title"` 的直系子控件分发挂载，也支持以 `<tab title="...">` / `<tab-pane>` 包裹子内容的标准组件写法。在 `Html.Parse` 装配阶段，元子节点 `<tab>` 的内部子控件自动提取并继承标签页插槽路由至 `Tabs.Page(i)`，实现自然、声明式的选项卡界面排版。
   - **事件修饰符与防冒泡（`@click.stop` / `@keydown.enter`）**：在 `@click.stop`、`@submit.prevent`、`@keydown.enter` 等事件声明中，`HtmlText.EventKind` 自动剥离修饰符后缀以匹配核心事件种类，并在 `Html.Wire` / `Html.WireArg` 中通过 `WrapModifier` 装饰底层回调：`.stop` 自动调用 `app.ClaimClick` 截断事件向下冒泡；`.enter` 严格校验 `app.window.EventKeyCode == 13`，免去业务回调中重复编写键码和冒泡判定样板代码。

12. **HTML5 声明式输入控件多态映射与模型类型感知自动强转规范**：
   - **`<input type="...">` 多态组件实例化**：在 HTML 声明中写 `<input type="checkbox|radio|number|range|color|date|password" />` 时，装载器（`DesignerHtml.InputTypeKind`）自动映射实例化为对应的专属高阶组件（`checkbox` -> `Checkbox`、`radio` -> `Radio`、`number` -> `InputNumber`、`range` -> `Slider`、`color` -> `ColorPicker`、`date` -> `DatePicker`、`password` -> `Input` 带密码掩码属性），与自定义组件标签（`<slider>`、`<colorpicker>` 等）完全等价且支持标准 HTML 语义。
   - **属性元数据驱动模型回写**：双向绑定按显式 `bindProp`、实际模型类型与 `BindingSpec` 选择属性，经公共 `GetProp` 读取；`WriteBoundValue` 根据 `PropSpec.ValueKind`、`bindNumber` 及既有模型值类型确定 bool/数值/字符串回写，不维护具体控件类型名单，也不限于空 JSON 节点。新组件声明默认绑定属性和值类型即可复用该协议；显式绑定属性优先。
   - **快照即时同步防抖**：控件完成 `WriteBoundValue` 模型就地回写后，立即调用 `RecordSnapshot(c, s)` 同步控件快照，防止同一帧在后续流程中再次触发无效重复写入。

14. **设计器复合与容器组件项事件与细粒度配置闭环规范（ToolStrip 项事件/Tab 与选项关闭尺寸/大编辑器定位）**：
   - **工具条（ToolStrip）项点击事件在属性与事件面板双通道暴露**：
     - 在「属性」面板的选项行（OPTIONS）上，为非分隔符项直观展示已绑定的事件处理器（`-> On...Click`），并在每行配备 `[事件]` 操作按钮，点击自动推导方法名并直通代码后置。
     - 在「事件」面板中增设专属「工具条项点击 (Tool Items Click)」事件组，列出全部项的 `.Click` 事件，标配「定位」、「新建」、「编辑」三合一操作，支持一键在代码后置中定位或自动生成事件桩。
   - **大弹窗选项与列编辑器（RowsEditor）代码定位直通**：
     - 在工具条项与表格列的弹窗主从属性编辑面板中，为选中项提供「定位事件代码」/「定位点击代码」按钮，免除开发者在选项文本与事件方法名之间反复来回切换。
   - **选项与 Tab 关闭按钮精致化度量（14px~16px）**：
     - 属性检查器选项列表中删除按钮（`MiniCloseBtn`）重构为轻量 16px 矩形热区配 10px `close` 字符图标与悬停高亮；`Tabs` 标签页的关闭按钮统一收缩至 14px 方形热区并配合 10px 字符，彻底根绝关闭按钮过大挤占选项标签或误触的问题。

15. **高级组件标签映射与插槽挂载规范（Tabs/Card/Badge/Tag 等）**：
   - **高阶组件声明式类型映射**：标准组件标签（`<card>`、`<badge>`、`<avatar>`、`<tag>`、`<divider>`、`<alert-box>`、`<rate>`、`<split-panel>`、`<collapse>`、`<breadcrumb>`、`<steps>`、`<timeline>`、`<qrcode>`、`<watermark>`、`<marquee>`、`<spin>`、`<empty>`、`<result>`、`<skeleton>`、`<input-number>` 等）装载时直接实例化真组件，严禁退化为通用 `Gui.Element` 导致方法丢失。
   - **真组件文本与空白过滤（Anti-Bogus-AddText）**：真实组件继承自 `Control` 而非 `Element`，不具备 `AddText` 接口。装载装配时对判为真组件的标签，游离空白符与换行符必须严加过滤；非空白文本内容通过 `v.SetProp("text", ...)` 规范写入。
   - **Tabs 容器选项自动推导与元子节点插槽转挂**：`<tabs>` 容器若未显式指定 `options` 属性，装载器自动深层扫描子级 `<tab title="...">` / `<tab-pane>` 元素并推导拼接 `options` 字符串；元子节点标记放行，其内部实际承载的子孙控件自动注入 `slotName = tTitle` 并重定向挂载至父级 `Tabs`。
   - **密码输入与扩展属性双向对称性（GetExtra/SetExtra）**：`<input type="password">` 统一通过 `v.SetProp("password", "true")` 声明，且控件层在 `GetExtra` 与 `SetExtra` 中形成完整读写闭环，确保属性获取与状态同步双向对称。

16. **动态模型数组驱动选项绑定与双向值同步规范（`:options` 与 `<option value="...">` JIT/AOT 同构）**：
   - **JSON 模型数组动态绑定（`:options` / `v-bind:options`）**：在 `ModelBinder.ResolveOperandStr` 中，当绑定表达式求值结果为 JSON 数组（`v.IsArray`）时，自动序列化为标准 JSON 字符串（`v.ToJson`），使高阶选择组件（`SelectBox`、`ChoiceGroup`、`RadioGroup`、`Tabs` 等）能够无缝响应由模型数组驱动的动态选项变更。
   - **选项反序列化多格式兼容与元数据映射**：组件的 `SetOptionsText` / `SetItemsText` 原生兼容经典竖线分隔字符串（`"A|B|C"`）与结构化 JSON 数组（`"[{\"label\":\"北京\",\"value\":\"bj\"}, ...]"`）。解析 JSON 数组时，标签键优先读取 `label` / `title` / `name` / `text`，值键读取 `value` / `id`，并支持 `disabled` 与 `closable` 元数据映射。
   - **帧渲染缓存幂等防抖（`optionsRaw` / `itemsSpec`）**：在组件内部维护 `optionsRaw` 原始字符串缓存；当每帧数据同步写入的选项文本与当前缓存一致时立即跳过重新解析与重建子项，彻底避免动画帧循环中重复反序列化造成的性能损耗以及弹窗滚动条、焦点和选中态被意外重置的问题。
   - **索引、选项键与标签分开投影**：ModelBinder 按 PropSpec 默认投影与模型真实类型选择属性，显式 bindProp 优先，HTML 与代码树共用分派。SelectBox 的 `value/selected/data` 只读写索引，`selectedValue/val` 只读写选项键，`text/label/selectedText` 读写标签；键为空时按现有 SelectedValue 契约使用标签。数字文本键“0”不能被当作索引0。索引 null/空串/-1 清空，非法或越界索引保持原选择。完整 `model_binding_equivalence_test` 已严格验证 JSON、字典与 POCO 的文本键、数值、bool、零值和显式投影。
   - **`<option value="...">` 选项合成**：带显式 `value` 的 `<option value="bj">北京</option>` 由装载器合成 `[{"label": "北京", "value": "bj"}, ...]` 并设置 options；这声明选项的键，不改变控件 value 属性的索引协议。支持范围以实际属性与回归为准。

17. **流式布局与盒模型契约**：
   - 容器同时声明 `display: flex` 与所需方向；只写方向不构成 Flex 容器。
   - 需要贴底时父容器定高，内容 `flex-grow: 1; min-height: 0`，底栏
     `flex-shrink: 0`；子项遵循文档序。历史数字 dock 不构成停靠机制。
   - margin/padding/gap 通过样式声明进入同一计算路径，设计器预览
     与运行期共用解析和布局，不能再指导调用已删除的 JSON 建树实现。
   - 属性修改需同步持久化并标脏；验收比较当前布局树的实际矩形。

18. **设计器属性检查器 Visual Studio 规范重构与垂直间距律（VS 分类、高频前置与呼吸节奏）**：
   - **Visual Studio 属性网格分区分级契约**：
     - 属性列表平铺无章会导致翻找成本极高。属性规格（`AllFieldSpecs`）必须通过 `PropSpec.Section` 划分为 4 大标准大类：
       1. `IDENTITY / COMMON`（标识与常用）：`name`（变量名）置顶，`label`（显示文本）紧随其后，配合 `placeholder`（占位文本）与组件类型。
       2. `LAYOUT`（布局与几何）：`dock`（停靠方式：None/Top/Bot/Left/Right/Fill）置顶，随后为 `position`（定位模式）、`flexGrow`（弹性撑满）、`anchor`（九宫格锚点）、浮动坐标 `x/y`、物理尺寸 `w/h`、边距 `margin/padding` 以及容器专属流向/间距。
       3. `DATA`（数据与绑定）：`bind`（绑定路径/数据源）、`bindProp`（绑定属性）及 `of`（表格行实体）。
       4. `BEHAVIOR`（行为与状态）：`required`（必填校验）、`wrap`（自动折行）、`orient`（Tab方向）及 `defOn`（默认开启）。
     - 分类过滤（`inspCategory`）只在选择“全部 (All)”时渲染 Section 分组栏；在单项分类下隐去分组头直接展示过滤后的紧凑属性。
   - **PropertyGrid Section Bar 视觉质感**：
     - `PropertyGrid` 渲染 `PropSpec.kind == PropKind.Section`（分组头）时，必须画出专属条带（`t.bgSecondary` 背景、左侧垂直强调色条 `t.primary`、高亮分类文字），与普通属性行形成鲜明视觉阶梯。
   - **快捷工具栏卡片行高计算与垂直呼吸律（Anti-Card-Overflow）**：
     - 快捷按钮（`SmallBtn`）的高度约为 24px。卡片内部放 N 行按钮时，行间步进距必须保持至少 28px~30px（例如 `row1: 8px`、`row2: 36px`、`row3: 66px`），卡片总高度必须为 `app.Scale(96~130)` 并留足底边距，严禁卡片高度不足 78px 强塞 3 行导致按钮垂直重叠、文字压盖与溢出边界。
     - 快捷卡片与卡片之间统一保持 10px 间距；快捷操作区与下方的 `PropertyGrid` 之间必须保持 12px 留白；多选批量卡片（3行按钮+双行文字）必须保留 126px 充足纵深。
     - 避免在顶部堆砌过多卡片导致属性网格被挤出屏幕。常用尺寸预设等次要辅助卡片仅在布局专属分类（`inspCategory == 2`）下展开，全部模式下让位给核心属性网格。

19. **设计器交互防误触与组件大纲树体系（点击防护、双击防跳码与全树精准点选）**：
   - **画布点选与双击跳转防护（Anti-False-Positive Code Jump）**：
     - 在设计器画布上快速点选组件、微调焦点或执行多选时，若双击判定容差过宽（如 400ms），会导致用户正常点选频繁误触发“双击进入代码编辑框”，严重破坏排版流畅度。
     - 画布双击时间容差必须严格收窄至 260ms，空间位移容差限制在 5px 内。
     - 限制双击动作的业务控件白名单：普通文本类控件双击优先触发行内就地重命名（`StartInlineRename`）；仅当用户按住 `Ctrl` 键，或双击对象属于明确命令操作类控件（如 `Button`，`ftype == 1`）且未被其他操作拦截时，才放行 `RequestOpenCodeBehindHandler` 切换 Tab 导航至代码后置文件。
   - **大纲树（Outline Tree）全树精准点选与层级漫游**：
     - 当界面存在绝对浮动（Float）、停靠重叠（Dock.Fill / Dock.Top）、零尺寸占位或深层嵌套（Panel / Tabs / SplitPanel）时，纯靠画布鼠标拾取必然出现选不中或被上层覆盖组件拦截的问题。
     - 左侧面板必须提供组件工具库（Palette）与大纲树（Outline）双 Tab 视图。大纲树以整棵设计文档树为蓝本递归渲染层级结构，节点显式标注控件类型、变量名、停靠方式（`[Top]` / `[Fill]` 等）以及容器/锁定标识。
     - 大纲树节点与画布/检查器保持绝对实时双向同步：单击树节点立即激活对应组件的选中包围框、句柄与属性检查器焦点，右键呼出同构上下文菜单，并提供同级上下微调、一键锁定与折叠展开工具条，彻底消除界面元素“看得见选不中、选得中挪不了”的死角。

20. **高保真桌面软件现代动效与交互架构规范（自主帧循环、数据驱动 SVG 动效、三态拖拽落区与克制美学）**：
   - **自主动画帧循环驱动契约（Autonomous Frame Loop Driving）**：
     - *踩坑*：在 Zan HTML 窗口开发中，使用 `form.Every(ms, cb)` 绑定的定时动画在没有鼠标移动时会停止刷新（窗口卡死在当前帧，必须持续晃动鼠标才刷一帧）。
     - *根因*：Win32 事件循环 `TakeDueFrame` 依赖 `RequestWakeIn` 结合系统定时器，Win32 定时器存在微小提前触发抖动，未到达 due 且 `animNextMs < 0` 时未置 `needsRedraw = true`，导致窗口进入深休眠。
     - *规范*：在动画回调（`OnTimerAnimation` / `OnFrameAnimation`）中必须主动调用 `app.RequestAnimationFrame(intervalMs)`（例如 40ms 对应 25 FPS），持续为 `animNextMs` 赋正值，由帧循环自主驱动下一次重绘，彻底解耦鼠标运动。
   - **纯数据驱动动态矢量 SVG（Data-driven Dynamic SVG Data URI）**：
     - *踩坑*：雷达旋转扫描、同心光晕呼吸、跑马灯流动虚线等复杂动效，若通过频繁操作 DOM 树会引发重排抖动；若手写自绘控件则违反 ZanIDE/HTML 窗口的“禁止自绘”纪律。
     - *规范*：在 HTML 中放置 `<img id="..." class="..." />`，后端通过纯函数 `BuildDynamicSvg(tick, ...)` 生成 `data:image/svg+xml;utf8,<svg ...>...</svg>`，通过 `img.Src.Set(...)` 极速更新。计算纯在数学层面（`stroke-dashoffset`、坐标矩阵、透明度），零布局重排、零自绘违反，画质矢量保真且帧率极高。
   - **桌面端拖拽三态反馈模型（Three-State Dropzone & Avatar Drag Feedback）**：
     - *踩坑*：卡片拖拽若仅靠静态提示或等待 drop 事件，用户无法感知卡片是否已被抓取、当前悬停在何处、落区是否就绪。
     - *规范*：建立三态落区模型（静止态、拖拽进行中流动虚线跑马灯态、落区悬停吸附高亮态） + 悬浮跟随 Avatar（绝对定位，在 `FrameHook` 中每帧读取 `app.mouseX, app.mouseY` 更新坐标） + 释放吸附交互。
   - **界面美学克制与品牌主视觉对齐（Anti-Cyberpunk Overfitting）**：
     - *踩坑*：AI 开发安全卫士/系统工具时，容易臆想出密密麻麻的 PCB 微电路、芯片针脚、暗黑赛博朋克风，破坏桌面产品的现代清爽扁平/微立体质感。
     - *规范*：严格遵循 `gui-design` 规范：纯白卡片底色（`#ffffff`）、极细边缘（`#eaedf2`）、品牌主色（如火绒橙 `#F58220`）、微透明柔和光晕与 2.5D 切面金盾，克制配色与呼吸节奏。
21. **同构集合列表的数据驱动与模板复用架构（Anti-Hardcoded-DOM-List-Duplication）**：
   - **踩坑出处**：在开发火绒安全卫士的“防护中心”（22项开关）、“访问控制”（6项规则）、“安全工具”（11项工具）等大量结构相同但数据不同的卡片视图时，若直接在 HTML 中手写复制数十份静态 `<div class="protect-row-card">` 节点，会导致：
     1) HTML 臃肿失控（单文件 2000 多行，充斥重复代码）；
     2) 排版高度出现细微差异（某些卡片因微小的 CSS 语法、换行或 `flex-shrink` 产生高度不一致，出现参差不齐）；
     3) 状态切换需要分别操纵 22 个子面板或大量 ID 变量，脆弱难维。
   - **规范要求**：
     1) **数据与模板彻底分离**：所有同构列表，严禁在 HTML 中硬编码重复卡片！HTML 只声明纯净的容器节点（如 `<div id="protectItemsList" class="protect-items-list"></div>`）。
     2) **强类型数据模型**：在 Code-behind 中定义扁平微型数据实体（如 `ProtectItem`、`AccessItem`、`ToolEntry`）。
     3) **单一行模板工厂函数**：提供单一的行/卡片构造工厂（如 `CreateProtectRow(item)`、`CreateAccessCard(item)`），统一赋予 CSS 样式类（如 `.protect-row-card`），绑定图标、标签与事件，保证 100% 像素级一致的行高（如 `height: 54px; min-height: 54px; max-height: 54px; flex-shrink: 0; flex: 0 0 54px;`）、内边距与对齐基准。
     4) **统一容器刷新**：数据更新或分类切换时，调用容器 `ClearKids()` 移除旧行后遍历模型列表逐项追加 `AddKid(CreateRow(item))`，内存轻量、渲染瞬时完成。
     5) **主轴滚动容器 flex-shrink 锁定**：当 Flex 容器声明了 `overflow-y: auto` / `overflow-x: auto` 时，它是滚动容器，排布引擎必须跳过主轴收缩压缩（否则 7 项会按 `room/7`、8 项会按 `room/8` 强制压扁，导致切换分类时列表行高剧烈跳变）。行模板根节点必须显式锁定偏好与弹性收缩尺寸（如 `row.Prefer(0, 54)` 并配 `flex: 0 0 54px; flex-shrink: 0;`），确保无论多少条目行高均严格恒定在像素基准线。
     6) **Flex-wrap 换行网格与滚动延伸（Scroll Extent）全貌闭环**：当卡片采用百分比宽度（如 `width: 31%` 或 `flex: 0 0 31%`）并声明 `flex-wrap: wrap` 时，排布测量引擎必须根据宿主换行宽度解析子项百分比尺寸（`StyleWidthIn(wrapMain)`）并累计多行交叉轴跨度（`crossSum + crossGap * (lines - 1)`）。滚动容器（如 `.tools-scroll`）依靠此真实高度计算完整的 `scrollExtent` 并唤出原生纵向滚动条，彻底根除因高度虚低导致下方卡片被窗口硬生生裁切成半截且无法滚动的恶性排版缺陷。

22. **声明式窗口与单管线装载规范（B-ID149：`class X : Form`、`<include>` 组合与零胶水建树）**：
   - **窗口即对象**：`class X : Form` 与同名 `X.html` 配对，通过
     `new X().Show()` 完成装配，不绕过装配直接调用 Run。
   - **事件反射按名自动接线与绑定对账**：树上所有事件（`@click="OnSave"`）按名自动匹配实例方法，缺失即 fail-fast 退出；`data-bind` 路径由 `CheckBindsDoc` 启动期静态对账，杜绝静默失败。
   - **模板脚手架约束**：新建项目模板中的入口类必须声明为 `partial class {{NAME}} : Form`，严禁遗漏 `{{NAME}}` 占位符或 `: Form` 继承，否则编译器入口合成（`new <基名>().Show()`）将因类型未定义中断。
   - **`<include src="...">` 组合化**：支持在 HTML 中通过 `<include src="fragment.html" />` 组合引入组件化片段，装载期展开（深度 8 封顶防环），片段内事件与数据绑定对宿主窗口类同构生效。
   - **窗口键与子窗模态**：根/body 的 `data-win-title/w/h/center/chrome/round` 装配期消费（`:data-win-*` 走 bindProps 泵）；子窗 `new` + `Show()`，归属语义设 `w.owner = this` 后 `w.ShowDialog()`（关闭后宿主补 `MarkDirty()` 重绘），`Show()` 恒为非模态。
   - **publish 框架钩子默认自动保留 thunk（B-ID160 编译器根修闭环）**：`OnLoad(Form)` 等框架按字符串名反射回调的钩子方法，在 `--publish` 形态下编译器原生识别 `On*` 框架生命周期钩子规范（`irgen_reflect.c`），默认生成 Invoke thunk 与反射导出并保留为链接 root，无需手动附加 `[Custom]` 注解。
   - **ChildWindow 子窗同类名约定**：`EnsureDesign()` 按"类名 ↔ 同名 .html"装载（覆写 `DesignName()` 可自定义名），与 Form.Show 同源；开发态（plain 构建）散盘扫描从工作目录与 src 树递归找。注意 `Directory.GetDirectories` 返回**裸目录名**（stdlib 刻意口径，与 GetFiles 全路径刻意不同）——递归扫描必须手动拼回父路径，否则嵌套设计稿永远 MISS。
   - **ChildWindow 装配序（与 Form.assembleDoc 同语义）**：`EnsureDesign` 载树并默认挂**窗自身**为模型（实例字段即 ViewModel、方法即事件处理器；Build 里 `SetRoot(root, mdl)` 换挂 JsonValue 等模型的尊重之）；`OpenHost` 在 host 就绪后做声明式装配——`@click` 等按名对账接线（缺失 **exit 1** 列名）、bind 路径启动期对账（JsonValue 模型的键不是反射字段，跳过对账）、`OnLoad` 钩子 `Invoke` + 标脏。双模型语义：**事件恒解析窗方法，bind 走当前模型**。publish 形态 `OnLoad` 默认自动保留 thunk（同上条契约）。
   - **html 事件处理器在 publish 自动保留**：zanc 扫内嵌设计稿的 @x / v-on / z-on / data-on / on* 处理器名，按名保留反射条目、thunk 与函数体，处理器无需 `[Custom]`；框架按名调的钩子（OnLoad 等）由编译器原生识别 On* 钩子保留，亦无需 `[Custom]`。
   - **反射方法表含继承方法**：HasMethod/Invoke 能命中基类方法（名字级遮蔽、最近声明胜出，基类静态方法除外），html 处理器可声明在基类窗上。
   - **`OnCloseRequested` 里可以再调 `Close()`**：Close 先置 open=false 再回调，重入早退，不会互递归。
   - **`(Label)r.Find(id)` 只对 `<label>` 成立**：`<span>` 不产出 Label；`<label><input/></label>` 里的 input 取不到，复选框文字写 `<input type="checkbox" value="文字">`。子类窗没有同名 html 时覆写 `DesignName()` 指向基类设计稿。
   - **html 裸布尔属性通道（`readonly`，与 `disabled` 同语义）**：html 里裸写 `readonly` 出现即生效，解析期直落 `SetProp("readonly","true")`。键名走 PropSpec **精确匹配（区分大小写）**：TextArea 的键是小写 `readonly`（与 input/rate 先例对齐）——历史键名 `readOnly` 双重失配（键名不命中 + 裸属性无值按 off 判 false）会**静默失效**。契约钉在 GUI 一致性用例（cwdocscan；TextCw：初始推送/一帧同步/readonly 命中三断言）。

23. **Retained 属性、内容与克隆的五条边界（2026-11-13 属性 case 验证）**：
   - **options 的全局 closable 不固化为逐项状态**：Tabs 从 options 建项时逐项 `closable` 为 false，有效关闭状态由逐项 flag 与 `closableAll` 组合；全局 true→false 必须能移除仅由全局开关产生的关闭按钮。显式逐项状态独立保留；此规则不改写 `Add(string)` 默认取全局值的既有语义。
   - **Add/AddPane 使用独立装配游标**：options 预声明标签，`pendingPaneIndex` 决定后续 AddPane 消费哪项，不能用 `pages.Count` 推断；提前创建命名页不应消费标签。直接 Add 结束预声明消费阶段，之后 AddPane 追加；保留 early activeTab。检查 Add→AddPane、options→提前取页→AddPane、options→Add→AddPane 三种顺序。
   - **clone 区分 own node 与业务内容，并配齐工厂注册**：普通 `Html.Clone` 先回放 Props，再调用 `CloneContentTo`；setter 可能已生成 caption/icon/panel，内容 hook 须去掉回放脚手架，恰好一次恢复业务 children/slot，并让回调归目标树。每个经通用 clone 的具体 Kind 都须注册工厂（如 CollapseItem），避免返回 null 后静默丢子树。空树、独立子控件、自定义 slot 和克隆后再次修改都要覆盖；Tabs 重建导航/内容壳，不重复复制生成节点。
   - **语义样式用独立 canonical key**：Tabs 用 `tabStyle` 供 Props/GetProp/clone，兼容 `style=card/segment` 时只拦截声明支持的枚举值；inline CSS 继续走基类。验证 CSS getter、实际 padding/background 以及随后改语义样式时 CSS 仍保留，不能把 style 设为吞掉任意 CSS 的普遍 alias。
   - **比较已应用投影，正常更新复用 Binding**：公开字段或 Binding 可绕过 setter，early return 要比较已应用状态/实际节点；同值字段不代表投影已更新。已有文本 Binding 用 `.Set(...)`，仅 null 时补建；同色但旧 gradient 尾色未清仍须失效缓存。只撤销控件负责的颜色投影，保留业务节点自定义色；生成节点被移除/换 parent 后放弃旧身份，未变化时保留节点。Binding 身份/订阅持续性没有独立计数断言，不据此声称零分配。

   验证定位：`tests/conformance/retained_property_contract.zan` 的 `case_retained_property_contract`，在
   `build/test-out/gui-refactor-old-targeted-20261113-064300-431057/result.json` 与
   `build/test-out/gui-refactor-old-targeted-20261113-065950-731288/result.json` 中均正式及同 exe strict 通过，
   精确 stdout、空 runtime stderr、无泄漏、exe 不变，批次输入及编译器无漂移。
   只覆盖各收据版本的属性用例；不据此认定后续 HTML wrapper/data-child-tab 装配、Designer 或整个 Gui 已验收，
   也不推导总体精简、普遍提速或零分配。

24. **预览客户区、取消重入与回调保活（2026-11-13 Designer fixture 验证）**：
   - **客户区根约束检查实际几何**：预览文档承担完整客户区及命中裁剪范围时，用 `position: absolute; left: 0; top: 0; right: 0; bottom: 0;`，不要仅凭百分比宽高认定已铺满；断言实际 bw/bh 等于父 viewport 扣除 inset 的尺寸。此约束针对预览根，不要求普通页面都绝对定位，也不让布局负责安装输入根。
   - **取消旧手势须在回调前完成，不留旧清理尾部**：`App.CancelRoutedPointer` 保存旧 target 后，先退休 capture/down/hover、click history、long-press 和 press 状态，再通知 PointerCancel。回调可能建立新 press/capture，回调后再清旧状态会抹掉合法重入。用取消回调建立替代按压的回归验证，同时检查物理 release、专用 Click 一次及 drain 不重复。
   - **Hover 回调后复核输入身份**：Leave 前先注销旧 hover；Leave/Enter 回调后，复核
     App 仍运行、事件序列、输入根代次、手势票据、活动输入根及当前 hover 身份，
     再继续 Enter/Move。目标须仍属于输入树，其实际祖先须属于当前 App、可见且启用；
     adapter 以输入成员归属为准，不要求 parent 链抵达输入根。嵌套回调建立的新 hover
     不得被旧尾部覆盖；旧目标失效时不得继续派发旧 Move。
   - **观察引用与子控件回调避免保活环**：仅观察 sender 身份的 fixture 字段用 weak，避免 control→闭包→observer→control；Designer 自持 Palette TreeView 的回调捕获 `WeakReference<Designer>`，触发时取 Target、判空后临时调用，避免 owner→child→callback→owner 环。weak 不负责保活，也不表示整条 GUI 引用图都应弱化。

   定位：`packages/Zan.Gui.Designer/src/Gui/Designer/Designer.Preview.zan`、`Designer.Palette.zan`，
   `packages/Zan.Gui/src/Gui/Core/App/App.VisualTree.zan`；回归为
   `tests/gui/designer_interaction_test.zan` 的 root viewport、SlotsAndReparent、click/drain 与 PreviewCancelReentry。
   直接运行收据 `build/test-out/gui-designer-direct-20261113-080718/result.json` compile0/run0、strictPass true；
   正式 `conformance_gui_designer_interaction` 收据
   `build/test-out/gui-refactor-old-targeted-20261113-080832-559569/result.json` 于08:10:27关闭，success true，
   正式及同 exe strict 通过，精确 stdout、空 runtime stderr、无泄漏、exe 不变，输入及编译器无漂移。
   此结论只覆盖相应 fixture 版本，不替代产品构建门、原生窗口实机或整个 Gui 验收，也不推导零分配或普遍提速。

## 焦点业务通知与结构退休（2026-11-13 定向验证）

- **Focus/Blur 跟随已提交的焦点转换，与绘制无关**：`FocusManager.SetFocused` 先提交 id、endpoint 与 IME，再执行旧控件 `OnInputFocusLost` 清理及业务 `Blur`，最后按 revision 检查派发当前 `Focus`。清理 hook 重入也必须兑现旧转换欠下的 Blur；回调即使绕行后回到同一 endpoint，旧尾部仍须由 revision 截断，防重复 Focus。通知沿 `TriggerEvent` → `WidgetEvents.FireByName` 仅 Peek 已有惰性槽，不为未订阅事件建槽；首次绘制前可通知，重复绘制不补发。
- **退休在结构提交后定向核对旧子树**：`Control.Remove/RemoveChildren` 完成摘除、`App.SetRootControl` 安装新根后，再 `ReconcileFocus`；只处理属于此次旧移除子树且未重新托管的当前焦点，不全局清扫所有暂未挂树 endpoint。保留回调显式选择待挂载新目标的既有兼容语义；新 capture 也不能豁免未变的旧焦点或旧 overlay。
- **批量移除的临时 fence 覆盖整批旧子树**：全部结构移除提交后，在同步清焦回调期间阻止向这些尚未重新托管的节点设焦，防 Blur 内 A→B→A 恢复已脱树焦点；嵌套退休合并 fence，finally 恢复外层。合法重新托管的目标可保留，不把 fence 变成永久禁用。

限定证据：`build/test-out/gui-refactor-old-targeted-20261113-200834-041767/result.json` 中
`conformance_gui_retained_focus_lifecycle`、`conformance_gui_overlay_anchor_retirement`、
`conformance_gui_control_host_lifecycle` 三个既有用例均 officialPass/strictPass，正式通过后同 exe strict 复跑通过，
精确 stdout、空 runtime stderr、exe 不变；`changedInputs: []`、`compilerUnchanged: true`，
编译器 SHA-256 为 `f51abfc8103be942c459c3761095d97bd15814d83b59046ddeb6b4d21d48590c`。
结论只覆盖该收据的三个 fixture 版本，尚不构成原生窗口实机或完整 Gui 验收，也不推导普遍提速或零分配。

## 关闭回调的 ARC 所有权（2026-11-13 定向验证）

- **在真正停循环时退休关闭回调**：`App.StopLoop()` 清空 `closeRequest` 与 `closeGuard`，打断 owner → App → 回调 → owner 环；不要在首次关闭请求时提前清空，否决关闭与异步清理期间仍需保留关闭语义，pending 请求只通知一次，排空完成后才停循环。
- **执行中的委托由局部强引用保活**：调用前分别取 `Func<bool> guard = closeGuard`、`Action handler = closeRequest`，再调用局部；guard/handler 内重入 `StopLoop()` 会清掉字段，局部必须保护当前执行及其捕获对象，直到调用返回。
- **LSP 在服务线程汇合后释放宿主回调**：`Shutdown()` 先标记 stopping、取消并解绑编辑器；`ShutdownAsync()` 等 `serviceStopped.Task` 完成后，仅在 `stopping` 时清空 `onDiagnostics` 与 `uiDispatcher`，打断退休 session → 诊断回调 → IDE 环。单纯观察完成不应清掉未 stopping 会话的回调。

源码已确认上述 owner 环；弱引用探针验证停循环释放、否决保留、pending 一次通知、委托重入保活及 LSP 汇合后的释放边界。
strict 探针收据：`build/test-out/gui-close-probe-20261113T130446.414248Z/close-probe-report.json`（两探针通过）。
正式 App 用例 `conformance_gui_app_post_lifecycle` 及同 exe strict 通过：`build/test-out/gui-refactor-old-targeted-20261113-210647-706765/result.json`；
正式 `lsp_session_editor` 通过（119.05 秒）：`build/test-out/gui-close-lsp-formal-20261113T1307.log`。
旧 native IDE 正常关闭报告仍为 52,268 个对象；新 exe 的原生关闭验证尚待完成。这些源码与 fixture 证据不证明该总量已消除，不得写成 native 泄漏已修复。

## 关闭排空的 Task 契约（2026-11-13 定向验证）

- `Task` 是有类型的任务值，不与整数比较，不用 `return 0` 表示空任务或已完成任务。
- 本次已观察到 async property 结果直接转交后 `Wait` 未形成有效启动/回收链，TCS owner 残留；不可将 `Wait` 当成显式启动，未将其定为转换缺陷，也不外推所有 async property 的行为。
- 非阻塞 drain 可同步登记 completion，再返回 `Task.Run(this.WaitForDrain(completion))`；具名 async 方法在 completion 非空时 `await completion.Task`，由 `Task.Run` 显式注册并调度。
- async 入口只建帧；active async resume 内的 `await` 挂起当前帧，普通函数上下文的 `await` 才走同步 pump。
- drain 回归勿在 `Task.Run(Action)` worker 内嵌套 `Task.Run(WaitGate(...)) + Wait()`，以免同步泵制造 drain 依赖环；取得 drain 后由测试独立推进 Complete。
- DONE 可早于帧尾声释放；弱 owner 检查用有截止时间的 eventual 断言，轮询时避免强引用保活目标，不要求即时清空或无限等待。
- 导航 strict probe：`build/test-out/navigation-work-20261113T141215.704486Z/report.json`，编译/运行 exit 0、精确 marker、runtime stderr 空、changed_inputs=[]；编译警告 W2072 不属于 runtime stderr。
- fresh native：`build/test-out/gui-ide-native-20261113T141513.882231Z/result.json`，result=capture-complete、exit 0、stdout/stderr 均 0 bytes、diagnostics=[]，raw_close=true 且 visible；结论限于本次捕获的正常关闭路径。

