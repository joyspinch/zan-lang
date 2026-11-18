# 组件封装的反哺——配方与逐坑

> SKILL.md 主干只留原则；可照抄的封装配方与踩坑记录在此，全部真实案例。

## 组件封装的反哺

- **双击与修饰键读取本次 routed Click 快照**：消费者在 `Click` 中读取 `e.isDoubleClick` 与 `e.modifiers`，不在 PointerUp 读取帧级 DoubleClickNow，不另建控件私有计时器。共享层在业务回调前确认有效主键 Click，并用事件序号防旧兼容路径重复记账；取消、拖动和次键不推进历史。原因：回调可注入另一输入，读取窗口当前状态会把内层修饰键或点击事实误用于外层；严格回归应覆盖同目标混合路由、重绘不重放、修饰键组合及回调嵌套输入。
- **Escape 弹层钩子之后重验登记会话**：覆写 `OnPopupEscape(App)` 可处理取消而保持面板；共享宿主在钩子后检查 popup、owner App 与登记 revision，再决定默认关闭。不能沿用旧弹层快照清理回调新开的会话；普通失焦关闭不调用 Escape 钩子。原因：导出取消与关闭面板是不同动作，回调又可能重开或跨宿主；验证须覆盖 handled、默认关闭、重开和失焦路径。

- **复合行交互尊重嵌入编辑器与子控件捕获**：行拖拽在 Down 冒泡时不得覆盖子控件已有捕获；文本编辑操作按 originalSource 到本行内容根的真实祖先链及 text/IME 能力判定，不能按具体 Input 类型列白名单，也不能把所有可聚焦控件都排除。原因：Button 已收到 Click，之前 Input 聚焦却已误选行，单查按钮计数会误判捕获修复无效。验证须从未选择状态依次执行编辑器聚焦/输入、子按钮点击及普通行选择/拖拽，并分段断言；List 原生命周期与旧 Tree 筛选已通过严格金样及正常退出检查。

- **焦点能力属于当前输入树，不依赖绘制登记**：控件显式声明键盘及文本能力；Down 只检查命中祖先的当前 App、输入归属、显隐与禁用状态，Tab 临时投影当前树顺序，IME 查询当前弱焦点端点。聚合输入成员不要求 parent 链走到适配器；兼容绘制登记须使用弱端点。先提交新焦点再通知旧端点，回调重入后不写回旧状态。验证须在首次 Paint 前发送真实 Down/Up、字符和 Tab，并覆盖重排、弹层、聚合根与 weak 释放；共享焦点及真实 Tree 行编辑器均已通过严格 golden/leak/guard/runtime。

- **拖动在路由事件期提交，绘制只读状态**：主键按下认领 `CapturePointer(control, true)`，Move 与主键 Up 使用事件坐标；Up 先提交最终位置，再结束并发通知，次键释放不能结束主拖动。Cancel 只清状态；Change 回调可能关闭、禁用或替换输入根，回调后须重验捕获和有效状态再发 DragEnd。原因：绘制期轮询漏掉重绘前输入及最后一次抬起坐标；Slider/SplitPanel 与调色板严格回归已覆盖这些边界。
- **普通按压与拖动捕获分别认领触摸手势**：一参 CapturePointer 保留按压语义，触摸越过防抖阈值取消按压并清旧 pressed；拖动显式用二参 true 后继续接收 Move/Up，两种情况都禁止合成 Click。原因：普通 Button 也会捕获，不能以“有捕获”判定拖动；只清捕获又会留下 FocusManager 的旧 pressed。
- **内联 CSS padding 走样式盒，不转成 Pad()**：ApplyInline 不能把 padding 简写取首数写成显式 Pad，否则四边/单位失真，替换或清空样式仍残留 padSet 优先级。实际布局读取解析后的四边值；手工 Pad/Padding 仍保留显式优先级。判定器：`css_dpi_test` 在100%–300% DPI比较首行坐标/宽度与替换样式后的几何；ScrollColumn 回归同时覆盖四边与显式 Pad。
- **借用输入根按结构组合**：HUD/Layer 等适配器共用 InputRootAdapter，以既有 content/slot 链保留被包装的根、弱持宿主与业务 owner，绑定判环只查结构链，不能用受模态限制的 ContainsInputControl；高优先级 Layer 始终在 HUD 外层，重复 Bind 保位，Detach 跨不同适配器拼接且只释放自有输入。原因：各自维护链会产生 HUD→Layer→HUD 强环，后加入 HUD 还可能绕过模态。判定器：`input_root_composition_test` 覆盖真实 HUD 挂载与 Layer 打开、两种绑定顺序、跨 App、摘除保留其他层焦点/捕获及弱寿命。
- **显隐与尺寸通知只走共享准备期**：Control 内部的 `PrepareLayoutEvents`
  统一派发 Hide/Show/Resize，首次可见准备只建立基线，不发初始 Show/Resize，
  RenderTree 不重放。借用输入根覆写 `LayoutChildren`、`OwnsLayoutChild`、
  `LayoutChildVisible`、`LayoutRevision` 描述现有树，不重挂 parent、不另走通知遍历。
  通知前提交状态，回调后重验宿主、根与准备代次、事件身份及归属；retire/rebind
  不能让旧遍历继续处理新会话。原因：Paint 派发使重绘重放通知，回调换根或重绑又会
  把旧快照带入新树。验证覆盖首次准备、隐藏/重显、尺寸变化、重复 Paint、回调重入、
  跨 App 与实际 weak 释放，不以一次绘制成功代替生命周期验收。
- **绘制回调返回后立即核对当前帧与节点归属**：OnPaint、Paint 事件及每个子树
  返回后复用准备代次、parent、借用 owner 和 revision 检查；RenderTree 入口先拒绝
  失效帧，避免把新 App 的子控件继承回旧 App。隐藏、移除或转移只中止失效子树，
  合法父节点的其余兄弟仍可绘制；root/event/phase 换代则中止整帧旧尾段。裁剪用
  finally 恢复，styleCtx 与 paint hash 同样成对还原。control_paint_fences 严格回归
  覆盖 HTML/代码正常绘制、HUD 四种变更、节点/帧失效、异常、状态恢复及 weak 释放。
- **准备回调失效须传到参与者外层**：宿主返回本轮完成证明，失败后调用
  `App.InvalidatePreparation()` 推进现有票据，停止外层旧尾段；不能只在局部 return，
  否则后续准备可能把回调设置的新根覆盖。合法准备安装输入根仍被允许，不可一律取消。
  正常 AutoClose 仅接受关闭状态和一次预期会话代次变化；回调重开、换根、推进事件或
  额外续代次仍须失败。Layer 默认正式与生命周期严格正式已覆盖正常关闭及回调失效，
  合法根安装另有独立正式回归，不能用严格旧 exe 冒充默认参数通过。
- **Layer 窗口与消息队列由 App 统一消费**：Open/消息入队接入既有准备和输入树，
  App 在模态层之前绘制已准备的普通窗口与队列；消费者不再调用 RenderStack 或
  取得 QueueHost 手动布局绘制，避免双绘与第二套所有权。借用 LayoutChildren 不等于
  真实绘制子树；页脚替换必须同步实际采用的 child 身份，最小化命中按当前任务条几何。
  IDE 已重编并通过 retained-input；这不替代 IDE 完整布局或实机验收。
- **聚合命中保留完整排除列表，测量重入按 App 分域**：HUD 与 fallback 的每级
  HitTest 都透传同一 `excludedSubtrees` 列表，不能只保留首项。测量回调转移同一控件
  到另一 App 时允许新宿主重测，拒绝回到仍在活动测量栈中的 App；finally 释放临时
  测量记录。原因：截短列表会误命中已排除子树，单一 measuring 标志会吞掉合法迁移，
  遗留宿主记录则会保活 App。现有回归覆盖多子树排除、跨 App 重测与弱寿命；
  活跃祖先 App 的循环回访拦截仍需独立用例，不能把单次跨 App 迁移当成循环验证。
- **弹层布局的每个回调后立即停止失效尾段**：Measure/Arrange 后核对登记 revision、
  App、root/根代次、事件、准备票据和 frame，再继续安排或布局通知；外层返回后才查
  代次无法阻止内部旧 Arrange。点弹层保留登记，下一轮从新状态恢复，不用 Dismiss
  掩盖失效。RenderAll 只画准备结果，不调用旧 BlockHitsBelow/Rect 改焦点或点击认领；
  隐藏与零尺寸登记在准备期退休。严格回归覆盖测量/安排×输入/绘制×换根/推进事件/
  同事件嵌套准备的12组合、弱释放、连续Paint纯度与真实点击不穿透；路由、菜单生命
  周期和准备快照三项正式通过。异常断言退出的 reachable 报告不等于正常退出泄漏。
- **当轮绑定先于测量约束计算**：父节点传入 available/clamp，子节点只同步一次
  绑定并解析当前样式，再扣本轮 margin、求声明 width，finally 恢复临时测量宽度。
  不能先按旧样式算 width 再进入 SyncBinding，否则 margin/width 改动滞后一轮；也
  不能先独立同步再测量造成重复回调。scroll 严格与正式单项覆盖 240→176px、
  200→100px 及恢复、一次同步、detach/root 换代尾段，原 HTML/代码几何断言保留。
  离屏借用 HTML 子树仍须显式绑定宿主，不能因其 parent 属于未绑定文档根而吞测量。
- **派生视觉类用纯 `EffectiveClass()` 投影**：基类默认返回作者 Class，Input 将
  Size、净化后的 FieldStatus 与 Round 投影成有效类，保留 Class/GetProp("class")
  原文。普通样式、缓存、Selector 上下文与 :has、parts、输入几何均用同一投影；
  Paint 不更新 liveApp 或 lastKeyNamedSeq。原因：临时改 Class 即使恢复，也会让
  选择器回调看到伪造声明，测量、命中和绘制还可能分裂。验证改属性后重绘前的样式与
  光标几何、部件和组合选择器，以及重复 Paint 不写宿主/序号/作者类。
- **分页绑定在准备期维护子树**：Pagination.Page 使用 `Binding<int>`，默认绑定
  属性是 page；data/value 仍是页码属性别名，不能按旧 Signal 对象示例传入。
  覆写 `SyncBinding()` 检查当前页与重建条件，在测量前更新真实按钮树，OnPaint
  不同步绑定、不重建。原因：绘制期才建树会让首绘前和连续输入使用过时页码按钮。
  验证外部页码变化后首绘前即可点击、绑定写回与 Change 一次、重复 Paint 不读绑定
  且保持子树身份；工厂属性等价另用真实 ControlFactory 验证。
- **重复项的可见部件各持有真实控件**：把准备移出 Paint 时，不能让多行共用一枚
  可变控件，否则准备完成后全部只剩最后一行的文本与几何。沿既有 Arrange 和滚动
  回调维护可见实例池，文本改变才更新生成子树，命中直接读准备后的控件边界；
  空态、模式切换和 Reset 退休旧部件。头像回归应覆盖不同首字、首绘前点击、滚动后
  点击、重复绘制不改子树及自然 weak 释放，不能只检查主气泡正文。
- **离屏滚轮重放保留原事件身份**：旧 CaptureWheel 两帧认领只恢复事件载荷，不能再次派发 routed Wheel，也不能先增后退事件序号；否则通知重复，且下一真实输入会撞上去重缓存。已路由消费的滚轮须跨 BeginFrame 保持已消费，不能被旧绘制路径重领。判定器：`scroll_routed_input_test` 的真实 EndOffscreenFrame→重放→下一指针/滚轮，以及 `scrollcolumn_input_lifecycle_test` 的混合消费。
- **旧行数 API 只投影共享像素 offset**：RichText 用真实行高前缀定位、bottomAnchor=0 精确贴底；CodeBlock 的 UseScroll 保留行数与信号版本，内部投影不能吃掉半行像素余量。OnScrollChanged 在真实提交后统一通知、同值不通知；最终 Up 先退休旧捕获再通知，回调换根/新捕获不被旧动作覆盖。固定 header/copy 是真实子树，正文 ScrollViewport 排除固定区域，Paint 不处理输入。`richtext_codeblock_input_test` 已通过 strict 精确 golden/leak/guard，覆盖输入返回前投影、链接、复制一次、最终 Up、触控取消、DPI、重入与 weak；原 `richtext_test` 旧 golden 同轮严格通过。
- **布局诊断不暴露可变缓存**：只读属性返回 List 仍可 Clear/Add；RichText.LineAt(index) 返回独立行、段与链接快照，越界/未布局为 null，修改快照不能污染实际布局。资源占位前须收口文本段宽，资源 advance 也计入行宽；探针实际复现宽度为零后才修根因，未复现的行高疑点不顺带改。
- **真实离屏输入先用标准 App 工厂**：ProcessOffscreenInput 对原生 window 按设计无动作，必须 App.CreateOffscreen；验证 App 弱释放时 Main 先建独立 frame，再在子方法创建业务 App，避开首实例由 frameApp 静态持有的既有契约，正常 Destroy canvas。不能把 fixture 错误当业务输入或 ARC 缺陷，也不能清零 witness 伪造释放。判定器：同一 richtext/codeblock 配对回归。
- **CSS 滚动接在公共路由，不能依赖子类调用 base**：滚动条 chrome 在内容指针钩子前处理，滚轮在控件未消费后处理，真移动才阻止向外冒泡；绘制只画滚动条。默认命中、拖动与绘制共用边框内的 padding box。固定列等复合控件通过 ScrollWheelViewport 扩展滚轮范围、ScrollBarViewport(bool) 分轴限定 chrome、ScrollWheelModifiers 局部映射修饰键；同一 owner 保持 offset/extent/捕获/commit，原事件不改写以保留边界冒泡语义。修饰位使用 Gui.Backend.Modifiers API，不能手写猜值：实际 Ctrl=1、Shift=2，夹具曾反置而错误断言横滚。原因：只接默认虚方法会让覆盖钩子的控件丢失 overflow 输入，固定区域共用单一几何又会把竖条画到中间带；scroll_geometry_input_test 已通过100%/150%/200% DPI、最终 Up/Cancel、原 modifiers/delta 冒泡及重复 Paint 的 strict golden/leak/guard/runtime。

- **模型事件接线不能强持有 binder 或控件**：控件及排队 Action 只持弱端点 token，执行时查当前 handler；重复 Wire 合并同控件/事件，Detach 与更换 root/model 使旧 token 失效并重置首同步及快照。不能仅清 binder 字段而遗留保活闭包，也不能让旧队列调用新模型。判定器：`model_binding_equivalence_test` 完整 HTML/代码实际事件、替换、重复接线、迟到队列及 weak 释放已通过严格 golden/leak/guard。
- **选项索引、键与标签是不同属性投影**：SelectBox 的 value/selected/data 读写索引，selectedValue/val 读写选项键，text/label/selectedText 表示标签；数字文本键“0”不可被当作索引0。绑定默认投影由 PropSpec 元数据与模型真实类型选择，显式 bindProp 优先，不在 Binder 中列具体控件类型。`model_binding_equivalence_test` 已严格验证 JSON、字符串/数值/bool 字典、POCO、零值及分数写回。
- **表单校验空值按控件语义局部投影**：SelectBox 未选中的索引投影是“-1”，FormRow 的 Check/Peek 应用 HasValue() 将未选中投影为空校验值，同时保留 FormGroup.Value 的索引契约；不能让通用 FormRule.Required 把普通文本“-1”当空。判定器：`formgroup_test` 覆盖未选中、Clear 后的 Check/Peek、选中后的索引与标签，以及普通文本“-1”仍有效，已通过 strict golden/leak/guard。
- **虚拟行在布局期维护真实子树，保留可见交集实例**：VirtualList 的行用 Add/Remove 继承宿主，滚动只替换离开/进入视口的行；移除前 ReleaseInputSubtree，SetRows/Refresh 即时退休旧编辑器。HitTest 与焦点查找须兑现重绘前的新滚动位置，不能在 OnPaint 才重建。显式行高是逻辑 px，参与 extent 与实际行高时同样 Scale。判定器：`scroll_widgets_input_lifecycle_test` 验证连续输入、重叠行焦点、跨宿主、1000 行有界实例、150%/200% DPI 及 weak 释放，strict golden/leak/guard 通过。
- **固定表头复合控件覆写共享 ScrollViewport**：BandGrid 从 border 内侧视口扣除表头，滚轮命中、滚动条拖动与绘制均使用同一正文矩形；不造第二套滚动包装器，不伪造 border 偏移。选区在 routed PointerDown/Move/Up 更新，主键 Up 提交最终坐标、次键 Up 保持主捕获，Cancel 仅终止。判定器：同一严格回归覆盖表头不滚、正文滚轮、滚动条最终 Up、选区通知一次与取消。
- **复合控件的编辑部件必须进入真实输入子树**：独立保存并 RenderTree 不会自动接入命中与焦点路由；优先让编辑部件成为聚合面板的 children，共享布局、绘制和输入几何。确实不能加入 children（如 TreeView 的行索引契约）时，须覆盖输入归属、焦点查找和命中，并同步继承 App；替换/移除先释放旧子树状态。TreeView filter 与 ColorPicker 通道已用重绘前真实输入、替换、跨宿主及 weak 生命周期回归验证，不能用绘制成功代替输入验收。
- **弹层内容采用与完整测量职责（2026-11-12 caller 实证）**：内部内容交给 `Add/With` 采用前不能手写 `parent`，否则 parent 链与父集合不一致，Card/Flex 实际拥有内容时会跳过或错误移除；真实父链必须由采用机制一次建立。弹层宿主统一负责 `MeasureTree(app)`，完整测量包装容器及 Calendar/编辑面板等后代后再读自然尺寸并 Arrange；仅调用根 `OnMeasure` 不会遍历后代，不能让各 factory 复制补测量。共享 host 已改用完整测量，`contextmenu_routed_lifecycle_test` 与扩展 `overlay_host_routing_test` 已通过 strict golden/leak/guard；Calendar/包装容器的真实 factory 与 caller 验收由对应迁移另行收口，不能用共享回归代替 caller 证据。
- **点定位菜单复用同一 OverlayHost（2026-11-12 strict 实证）**：ContextMenu 实例通过 `ShowAt(app,x,y)` 使用普通 retained Button 子树，宿主统一 clamp 点定位、外 Down/Esc/kind9 关闭与关闭通知；ownerless static Render 仍是旧兼容入口，不能声称自动接 retained。关闭先注销活动登记并退休旧输入，再通知旧 popup/anchor；cancel 或 popup 通知若重新激活同 identity 的 popup/anchor，须跳过迟到的旧关闭通知，回调打开的新弹层不能被外层覆盖。每控件在 lazy ControlExtra 中仅存整型登记 epoch，popup 与 anchor 同 identity 时只递增一次；通知前核对旧 epoch，不能仅比较旧 host 的 activePopup，因为新会话可在另一 App。ShowAt 仅对旧 host 仍实际登记自身时 Hide，返回后核对 opened/代次，cancel 中旧登记已注销的新 ShowAt 可接管；三 App 实证曾复现双 host，已根修并通过完整 strict。同 anchor 换内容只通知旧 popup，不关闭 anchor。选择先 `Hide(this)`，检查 opening generation 未被重入改写后才记录一次性 action 结果，再调用业务通知；业务回调后再次检查代次，不能把旧通知送进新会话。queued item action 使用 weak owner 与 opening generation，迟到旧动作或旧 `Hide(popup)` 不能关闭新菜单。普通关闭保留配置的 item 回调与 `ActionSelected`，与控件配置生命周期一致；显式 `ClearCallbacks()` 结束业务订阅，关闭只退休会话状态。Add 延迟建树接既有测量生命周期，绘制不重建或重放输入。保留自然 `Children[0] is IconView` 表达式，owned 临时值问题由编译器根修。`contextmenu_routed_lifecycle_test` 与扩展 `overlay_host_routing_test` 均已 fresh 通过精确 golden、normal leak、ARC guard、strict runtime；后者用真实 Down 捕获→cancel 覆盖同 anchor 重开同 popup/替换 popup，最终关闭只通知一次。
- **Retained 长按复用原手势状态机与唤醒（2026-11-12 strict 实证）**：仅显式 LongPress 订阅或覆写 capability 认领，普通 HitTest/泛路由不认领；保留实际 Down leaf source，在原500ms/12px算法中只触发一次，越界或退出归属即取消，不能添加控件私有计时器。聚合根用 ContainsInputControl 验证成员，再检查真实祖先，不要求 parent 链走到适配器。Up 的 intrinsic hook 与同节点 MouseUp 订阅之间也须核手势票据；旧 hover 在 Leave 前注销，回调新建 Down/capture/hover 后旧尾部不得改写。完整 `retained_longpress_input_test` 和 slop Cancel→新手势探针均通过 fresh strict golden/leak/guard/runtime；原生窗口空闲唤醒与实际子窗口 deadline 唤醒仍须独立实窗验收。
- **锚定弹层随输入归属退休，重入守卫按资源分别判断（2026-11-12 strict 实证）**：换根或摘除子树时，即使 anchor 没有 focus/capture，也须按共享输入归属关闭旧锚定登记；聚合根仍包含 anchor 时保留，点坐标弹层按调用方生命周期管理。先清旧按压/悬停再发送 Cancel/Leave，focus 只由 FocusManager 与焦点端点身份保护，overlay 只由登记 revision 保护，根安装只由后续换根 generation 取代。回调只新建 capture 不能阻止旧 focus/anchor 退休；关闭回调新建的 capture/focus/popup 不能被旧清理覆盖。`overlay_anchor_retirement_test` 与原生产 SelectBox 探针已通过 fresh strict golden/leak/guard/runtime；SelectBox fixture 须安装真实 App/输入根，并按当前入口在实际 Down/Up 后配对绘制，先确认打开成功，再断言无焦点/捕获的根替换立即关闭。
- **Ribbon 业务订阅与三类按钮接线都借用 owner（2026-11-12 实证）**：Designer 外层 `CommandClicked` 使用 weak owner action，Ribbon 内部 tab、group item、flat command 三类 Button action 同样弱持 Ribbon，保留原有通知与动作码；只修外层仍会留下 Ribbon→Button→委托→Ribbon 环。`BuildTree()` 先递增 generation，再用 `RemoveAll()` 退休旧子树的 capture、focus、parent/App；取消回调可能重入，返回后核对 generation，queued action 执行前也核对所属 generation，不能只 `children.Clear()` 或靠清业务订阅释放。
- **可见生命周期与 frame fixture 分开验收**：隐藏/零尺寸路径早退的 green 不能代表可见 Ribbon 已释放；回归须实际渲染并重建可见 Ribbon，覆盖 tab/group item/flat command、真实 Down 建立 capture、focus 退休、旧队列失效与退出 weak 释放。离屏绘制每次 Begin 必须配对 EndOffscreenFrame 或 CancelOffscreenFrame，并在 finally 中用 CancelOffscreenFrame 收口早退或异常；App 弱寿命 fixture 先建立独立 frame 宿主，遵守首实例由 frameApp 静态持有的契约，不清 witness 伪造释放。可见 probe 已从 owner/host/三类 Ribbon 全存活修到全释放并通过 strict；正式 `conformance_gui_designer_contextmenu_routed` 已在父批次 PASS（150.23 秒），证据见 `build/test-out/gui-current-failed-jobs4.log`。
- **排队动作夹具遵守真实事件 turn（2026-11-12 Ribbon strict 实证）**：ProcessOffscreenInput 在 EndLoopTurn 自动 DrainPosts，真实 Up 返回后再重建不能撤销已经执行的动作。验证旧 queued action 时用真实 Down 建立捕获，随后 Post 变更，立即真实 Up；同轮 FIFO 先执行变更再执行旧动作，断言变更确实执行及旧业务计数未增加，不用私有 pump 或延迟断言制造绿色。tabbed Ribbon 的夹具高度用 HeightTabbed，无 tab 用 HeightGroups，否则窄宽 overflow 前提可能因三行压缩失效。保留旧失败与原 golden；组件严格通过不代表 Designer/Gallery 调用方或实窗验收完成。
- **关闭或替换弹层先释放旧输入子树**：共享弹层宿主须清理旧弹层的 focus、capture、press、hover；只清活动弹层字段会遗留文本焦点或拖动捕获。聚合面板反向借用 owner 用 weak，避免 Picker→Card→Panel→Picker 环；ColorPicker 关闭/替换与孤立 Panel 回归已在 golden/leak/guard 下验证。

- **宿主与父控件的借用反向引用用 weak**：App 持有输入根，Control 不得再强持有
  App；Input/TextArea 的宿主缓存同样是借用。`control_host_lifecycle` 离屏探针
  已验证按钮树与编辑树无需手动卸载根即可释放窗口和树。复合控件指示器的 owner
  也不得强持有父控件：Checkbox 的两棵引用环曾精确保留 548 项对象，改 weak 后
  原 HTML/代码属性等价探针在相同 golden、leak/guard 下通过。真实拥有窗口的
  Form.app、ChildWindow.host 保持强引用，退出时解除根和回调，不能一律弱化。
- **复合控件的子控件事件用 weak owner action 避免 ARC 环**：父控件持有子控件，
  子控件的事件又持有绑定父控件实例的方法时，会形成父→子→委托→父的强引用环。
  用独立 action 对象接线，action 以 `weak` 字段引用 owner，回调先检查 owner
  是否仍存在，再转发操作；只把父子反向字段改 weak 不能解除委托造成的环。
  生命周期回归须销毁复合控件后检查实际释放，并同时启用 `--check-leaks`
  与 `--arc-guard`；InputNumber lifecycle 的 11 项严格 golden 比较已通过该
  组合检查，step_bounds 的 10 项亦通过同组合检查。
- **图标按钮必须使用真实 `IconView` 子控件**：它提供测量与绘制内容；单独赋值旧
  `Icon` 字段不会创建内容，按钮会变成空内容的零宽 Button。
- **离屏交互断言以真实事件 turn 为边界**：`ProcessOffscreenInput` 返回前由
  `EndLoopTurn` 排空 App posts。写前通知应同步断言旧模型与旧通知计数，返回后
  断言新模型与一次 Change；用 FrameSeq 不变证明没有依赖 Paint。验证迟到动作时
  使用真实 Down、Post 变更、真实 Up 的 FIFO 顺序。只有显式向 Dispatcher 排队的
  独立动作才手动 Take；不能假设真实输入返回后业务队列尚未执行。
- **复合内容替换使用业务替换入口**：Card 的 Add/With 追加业务内容，RemoveAll
  只拆当前 children；重建弹层内容使用 Content 替换。原因是旧业务面板可在后续
  结构重建中再次出现，点击命中旧会话。验证连续打开、选择及 Change 回调重开，
  保留会话守卫，不能通过放宽旧动作有效期掩盖残留内容。
- **路由消费按事件序号跨绘制帧保留**：兼容点击查询须检查当前 EventSeq 的
  routed handled，BeginFrame 不能让已消费的 Up 再次命中旧绘制锚点。验证同一 Up
  重复绘制不提交、下一次真实 Down/Up 正常提交；不以业务计数限流替代共享判断。
- **固定页脚的纵向 Flex 主体设 `flex-basis:0;min-height:0`**：主体增长占剩余高度，
  详情区使用 `overflow-y:auto`；只写 `flex-grow:1` 会按内容基准把页脚推到窗口外。
  回归使用窗口内真实点击和祖先几何诊断，不能用直接触发事件替代越界按钮检查。

- **属性检查器保留一棵树，编辑只走一个提交出口**：属性行及其输入控件按实例
  创建并接线一次，绘制、选择切换与模型回填只更新值和可见性，保留根、行、
  父子关系与订阅；不能逐帧重建/重挂输入树。Change/Click 统一进入模型提交，
  回填期间抑制提交，写模型前比较旧值，一次有效编辑只记一次 undo，无变化、
  拒绝的值与重绘不记空 undo。资源选择等程序调用 `SetText` 不触发 Change，
  必须显式调用对应 commit（SceneDesigner 为 `CommitInspectorText`），不要再
  额外模拟一次 Change。RGB 编辑保留独立 alpha，不能用取色器回填覆盖透明度。
  2026-11-11 `scene_designer_inspector` 严格 golden probe 已覆盖真实离屏输入、
  单次/无变化提交、Undo、anchor、RGB/alpha、资源字段、滚动与根/行身份稳定；
  `scene_designer_color` 颜色 probe 亦通过，不外推为 IDE 实机或正式 CTest 通过。
- **嵌入区域的布局、绘制与输入共用同一几何**：HUD 在输入前调用
  `SetBounds(x,y,width,height)`（App 坐标）与 `Bind(app)`，共享 prepare 对同一树
  Measure/Arrange；绘制只调用 `Paint(app)` 消费准备结果，不再绑定、布局或重置区域。
  已移除旧 Render/RenderIn/RenderViewport/PrepareLayout/BindInputRoot 入口。Paint 须核
  当前 App、适配器实际根链及准备证明，隐藏、摘除或跨 App 后的旧绘制不能复活输入；
  连续 Paint 不增加 Measure/Arrange、不更改准备票据与根代次，首绘前即可真实点击。
  已绑定 HUD 的 Add 必须推进现有准备票据，再登记新 scope；准备后业务 Add 与
  OnPaint 内 Add 均不能把未测排成员带入同次绘制。下一共享准备完成后才允许绘制和
  命中，不靠 Paint 补布局或每帧复制成员表；guihost 严格回归保留原 golden 与 weak。
  当前 SceneDesigner 用 `InitializeInput(app)` 提前登记，再以 `SetBounds(app,...)`、
  `SetHostPanelBounds(app,tools,properties)` 更新区域；Designer Inspector 的入口为
  `PrepareInspectorInput(App,Rect)`，不沿用旧 RenderInspector 代替准备。
  RenderTree 本身不接输入，不另算命中坐标、不重新收养控件。关闭 dock/离页显式
  隐藏，HudLayer.SetVisible(false) 只退休本层输入。2026-11-11 检查器 probe 的滚动、
  hidden 焦点与根稳定属于历史局部结果；后续各消费方的 capture 释放、连续输入和重入
  直接对照当前源码与对应回归证据确认，不能外推为完整 IDE、Gallery/tools 或实窗完成。

- **铁律:控件自己负责绘制,使用处只配置**(实例化→配属性→喂数据→摆位置)。
  禁止在使用处用 Canvas 原语重画已有控件——组件修好后示例还在按旧画法显示,
  就会"组件是对的,demo 是错的"。
- **组件归属与命名空间以源码声明为准**：`Gui/Component/` 的基础复合控件
  声明在 `Gui.Component`，例如 BandGrid；独立包继续使用自己的实际声明，
  不因“组件”二字统一迁入此空间。限定名按命名空间解析，不能只猜类名末段。
  “目录即命名空间”是迁移目标：Gui 根最终
  只留 Point/Rect/Color 等基元，Core/Styling/Rendering 等分别归层；当前这些层
  仍有 `namespace Gui`，不能提前按目标补 using。解析会沿当前命名空间链向上找
  （`Gui.Component.DataTable` → `Gui.Component` → `Gui`）；无关命名空间的调用方
  使用 `using Gui.Component;` 或限定名。全层迁名涉及所有 partial 声明与调用点，
  必须按 P7 原子迁移，不能由单个控件的较小影响面推断全层改名成本。
- **宿主面板有专属入口时,不要在通用开关列表里再列一遍**(停靠面板教训):
  同一个面板既出现在功能区的「工作面」大按钮里、又出现在 dock 自动生成的
  「面板」开关列表里,用户会犯迷糊该点哪个。DockHost 给了 `SetNoSwitch`:
  面板声明"不进通用开关列表",入口只留显眼的那一处。
- **放不下的命令要有去处,不能被裁**(Ribbon 溢出教训):工具条/功能区宽度
  不够时,低优先级命令退出条带、文字收成图标格,所有隐藏命令仍须进入右端
  「»」溢出菜单。条带与菜单合起来须恰好保留每条原命令,不能因 optional
  标记而丢失入口。菜单须命中标准 ContextMenu 表面,按真实按钮声明高度和
  inset 测量；验证同时检查完整末行点击、短窗滚动与空白边距实际背景色,
  不以按钮几何或 alpha 正确代替整片菜单不透底。
- **排布只算一遍**:测量宽度、绘制、判定"放不下"必须读同一份落位结果
  (组件里一份 Layout → 槽位列表,测量/绘制/溢出三处共用),否则会出现
  "量出来放得下、画出来被裁"——两套游标算法迟早对不上。
- **左键开菜单的那一帧要 arm**:菜单开在触发按钮下方,打开的那次点击落在
  菜单外 → 菜单同帧自关(菜单"闪一下就没");第一帧必须忽略(宿主用 arm
  标志),下一帧才认选择。
- **提示气泡会盖住弹出的菜单**:菜单开着时撤掉触发按钮的气泡(宿主告知组件
  "菜单开着",如 `SetOverflowMenuOpen`),气泡画在菜单之上,会挡住菜单第一项。
- 觉得某控件"不精美"→ 修 `packages/Zan.Gui/src/Gui/Widget/` 或 `skins/base.css` 里的规则,
  让所有使用处一起变好;新皮肤值写进皮肤包的 `:root`,不散落。
- **新增 Gui 类先查重名**:历史上有在 `Gui.Component` 下新增
  彩带动画组件 `Ribbon`,把 SceneDesigner 等只 `using Gui.Component` 的文件里
  裸写的 `Gui.Widget.Ribbon`(功能区控件)整体劫持到新类上,调用点报
  "no member",离肇因提交很远——Zan 的名字解析按 using 就近绑定,同名类不警告。
  新增类落名前 `grep -rn "class <Name>" packages/Zan.Gui/src/Gui/`;撞名要么改名、要么调用点限定名。
- **Tabs 交互依托真实子树与路由**：页签、关闭与新增按钮是 `navStrip` 的
  retained Button 子控件，内容页挂在 `contentBox`；通过标准 Click 接线，
  宿主订阅 `TabChanged/TabClosed/TabAdded`。命中由当前树的 HitTest 与路由处理，
  不依赖 `On.Any`、FireCommon 绘制登记或手写 `hitTester.RegisterRect`。
  调整布局与滚动时保持按钮及内容的真实父链，不能另造几何点击路径。
- **PropSpec 的 str/num/flag 必须绑定可写源**：实例可写字段和同时具有
  get/set 的属性生成实时 Binding，自定义属性保留访问器副作用与分派；
  getter-only、init-only 属性、readonly 字段和方法返回值生成快照，Set
  只修改快照。ListItem 的只读 Label 不可作为写回源，应绑定 Text；
  不能把这一限制扩展成禁止所有自定义属性。实时 target 不持有 owner，
  owner 必须活过 Get/Set；用真实读写与退出 leak/ARC 验证，不能只看 IsLive。
- **Tabs 导航条与指示线共用 CSS 盒模型**：横向条高由
  `.tabs-nav-horizontal` 的 `var(--height-medium)` 及 small/large 档决定，
  选中线属于 `.tab-item.active`，底部分隔属于 `.tabs-nav.line`；
  内容页占 Tabs 剩余空间。仅顶栏导航按所选档位定高，不把含内容页的 Tabs
  整体锁成条高，也不使用旧 theme.heightMedium 数值手算指示线位置。
- **设计器选框读取实际布局，默认尺寸只用于创建**
  (Pagination/仪表选区错位及 HTML 设计与运行不一致教训):
  调色板点击与拖拽添加采用一致的控件自然尺寸，避免统一 200x32 造成
  空旷错位；完整窗口预览共用 retained tree，选框、拾取、缩放、换父
  使用实际框。禁止另写逐控件布局或外层裁剪遮盖差异；那会丢掉继承、
  容器 chrome 和合法 overflow。视图 zoom 只缩放最终图像，细则与
  几何/像素/交互验证见 `references/html-window-designer.md` 所见所得专节。
- **设计器画布网格、焦点与容器拖拽鲁棒性**
  (点阵不可见/撤销失效/拖拽组件消失教训):
  1. 点阵网格尺寸按 DPI 缩放（至少 2px），颜色对比度采用 `Style.Fade(t.textSecondary, 85)`，
     且仅跳过非容器叶子控件，容器内部仍正常展示网格辅助线；
  2. 画布交互（平移/点击空白/拖拽/右键）主动调用 `app.focus.ClearFocus` 释放输入框独占焦点，
     按键路由同时支持 `EventChar` 控制字符与 `EventKeyDown`（Ctrl+Z/Ctrl+Y 等组合键）；
  3. 容器 Qualification 需严格校验 `f.IsContainer || f.KidCount > 0 || (f.kind == "Element" && node.control is Element)`，
     落点命中自身或子树时安全回退至原父容器，拖拽坐标换算处强制 `fx >= 0, fy >= 0` 杜绝负坐标移出画布。
- **设计器画布双击时序与盒模型一等公民属性规范**
  (点击设计窗口误跳代码/拖拽浮动层级混乱/盒模型只能手写 CSS 教训):
  1. 双击消费共享 Click 快照：在 routed `Click` 的 `OnClick(RoutedEventArgs e)` 或路由处理器中读取 `e.isDoubleClick`，修饰键使用 `Modifiers.HasCtrl(e.modifiers)`；拖拽、缩放及取消不得触发文案编辑或代码导航。不在 MouseDown/MouseUp 轮询 `DoubleClickNow`，也不维护组件指针、时钟和位移组成的私有双击状态机；具体编辑/导航策略见下文“设计器画布双击与代码跳转防误触机制”；
  2. 盒模型一等公民快捷设置：Margin（外边距）、Padding（内边距）与容器 Gap（子间距）必须作为 FormField 核心建模字段，并在 Inspector 检查器提供数字步进器（0~200px 范围），同时闭环 FieldToJson / FieldFromJson 序列化、IsModeledKey 白名单以及快照 Copy/Equal 比对，避免开发者在自由/流式排版中被迫手写额外内联 CSS；
  3. 自由与流式拖拽位移隔离：仅当鼠标移动超出物理阈值（`dx >= 4 || dy >= 4`）时才触发 `DesignMakeFloat`，避免原地单击或抖动将流式/停靠组件意外变为绝对定位；外壳/顶栏组件（如 ToolStrip）优先使用 `dock: top` 或主 Flex 列首项，避免绝对定位脱离文档流被后续流式容器覆盖；
  4. Z-Order 渲染层级与列表下标契约（置顶置底倒置教训）：GUI Retained 模式与 HTML 遵循自然文档序绘制（`0 -> Count - 1`，下标 0 最先绘制处于最底层，下标末尾最后绘制处于最顶层，`HitTest` 命中测试从末尾向前逆序检测）。因此置顶（Bring to Front）必须移至末尾下标 `Count - 1`，置底（Send to Back）移至头部下标 `0`；上移一层（Bring Forward）为正向位移 `+1`，下移一层（Send Backward）为负向位移 `-1`，切勿倒置导致“越置顶越在底层被遮挡”；
  6. 组件大纲树（Component Tree / Outline）大纲导航与双向联动选择：左侧面板提供 `[ 组件库 | 大纲树 ]` 双 Tab 切换，递归遍历显示完整窗口的嵌套结构、图标、类型与定位模式（Dock/Float）；树节点单击直接同步选中目标组件，彻底解决画布深层嵌套、零尺寸或被遮挡组件无法点中的痛点；
  8. 多选画布上下文菜单与就地排版对齐：多选（freeExtra.Count > 0）时，右键上下文菜单直接呼出「左/顶/右/底对齐、水平/垂直居中、水平/垂直均分、等宽/等高」，无需移至顶部 Ribbon 翻找；
  9. 大纲树就地右键菜单与 Alt+方向键尺寸缩放：左侧大纲树支持右键就地呼出操作菜单；自由排版模式下支持 Alt+方向键（结合网格吸附与 Shift 步长）快捷调整控件宽高；
  10. 多选批量属性同步（Batch Property Sync）：多选状态下在 Inspector 检查器修改通用尺寸（w/h）、盒模型（margin/padding/gap）、排版定位（dock/flexGrow/position/anchor）或容器弹性属性时，自动批量同步给 freeExtra 中所有未锁定控件，并统一纳入单一撤销快照，杜绝逐个重复配置；
	  11. F2 原地快捷重命名（Inline Quick Rename）：画布与大纲树选中控件按 F2（或右键菜单重命名）就地呼出浮动输入卡，回车确认、ESC 取消，避免频繁在画布与右侧属性栏之间大幅往返移动；
	  12. 大纲树搜索过滤与层级穿透（Outline Tree Filter）：大纲树与组件库共享搜索框，输入关键词实时穿透过滤保留自身或子孙节点命中的组件，支持深层嵌套复杂表单秒级定位；
	  13. 智能吸附参考线与拖拽几何 HUD 指示（Smart Guides & Drag HUD）：自由排版拖拽移动与尺寸缩放时，贯穿画布绘制高亮磁吸辅助线并在边缘显示坐标徽标（X/Y），同时在控件角落浮动实时坐标与宽高几何 HUD（如 120, 80 · 200x32），让排版对齐达到像素级直观精准；
	  14. 空格键抓手画布平移与中键漫游（Space Pan）：按住空格键临时切换抓手手势（光标 1），左键直接漫游视口，配合中键平移彻底解放对微细滚动条的依赖；
	  15. 空白处橡皮筋框选高亮（Marquee Selection）：在空白处拖拽拉出半透明高亮填充选框，松手批量多选所有相交的顶层及子组件；
	  16. 组件一键成组与解散成组（Group / Ungroup Packaging）：多选组件支持 Ctrl+G 一键计算最小外接矩形并打包进新 Panel 容器，坐标无缝转换为局部相对坐标；选中容器支持 Ctrl+Shift+G 一键将子组件提升解散回父级并销毁外壳；
	  17. Alt 悬停间距测量标尺（Alt Spacing Inspection）：按住 Alt 键将鼠标悬停在其他控件上，实时高亮目标控件并绘制两组件之间的水平/垂直间距参考线与数字标尺胶囊，实现专业级设计稿测距；
	  18. 以光标为锚点的平滑画布缩放与底栏快捷复位（Cursor-Anchored Smooth Zoom）：Ctrl+滚轮缩放时记录光标相对原点的偏移量，根据新旧缩放比例动态补偿视口平移 `freePanX` 与 `freePanY`，确保光标所指物理坐标在屏幕上绝对静止；底部状态栏提供 `[-]` `[100% 快捷复位]` `[+]` `[适应 (Fit)]` 微型控制条；
	  19. 多控件等间距推导智能磁吸（Equal Spacing Snap Lines）：磁吸参考线自动推导兄弟组件两两之间的间隙（Gap），拖动组件接近等间距位置时自动触发吸附与贯通高亮参考线指示；
	  20. 大纲树右键节点就地精准定位调序与换父操作（Outline Context Operations）：大纲树右键命中行直接通过 `ContextRow()` 同步焦点 `sel`，就地呼出快捷菜单，支持置顶/置底/上移/下移快速调序，以及一键移至窗口根层或移入激活容器；
	  21. 底栏可视化撤销/重做步数指示与快捷操作（Visual Undo / Redo Actions & Stack Depth）：在底栏微型控制栏中集成 `↶ (N)` 撤销与 `↷ (N)` 重做按钮，根据 `undoStack` 与 `redoStack` 深度动态高亮/置灰，给设计者提供明确的可逆步数安全感与一键回退路径；
	  22. 锁定组件友好拦截提示与相邻同级容器快速移入（Lock Guard Feedback & Sibling Container Reparenting）：组件被锁定后试图点击/拉拽时实时提示 `Ctrl+L` 解锁指引，杜绝“以为拖不动卡死”的疑惑；上下文菜单自动探测同级相邻兄弟节点是否为容器，提供一键「移入上方容器 (panel1)」/「移入下方容器 (panel2)」，极大简化嵌套与打包工作流；
	  23. 快速组件插入浮动面板（Quick Insert Spotlight HUD, `Ctrl+I`）：现代编辑器级聚焦弹窗，支持按拼音/英文关键词快速模糊匹配组件与描述，方向键快速切换候选，回车一键就地生成并自动选中聚焦，大幅缩短寻找并拖拽工具箱组件的时间；
	  24. 大纲树一键全部展开/折叠（Outline Bulk Tree Toggle）：在侧边栏大纲树模式顶部提供 `[+]` 展开全部与 `[-]` 折叠全部快捷控制，基于 `TreeView.ExpandAll()` 与 `CollapseAll()` 快速纵览复杂多层级容器或收拢聚焦局部；
	  25. 设计器全景快捷键速查面板（Keyboard Shortcuts CheatSheet HUD, `F1` / `?`）：在按 `F1` 或 `?` 时呼出半透明速查卡片，底栏状态条提供 `[ ? ]` 入口与上下文菜单挂接，将 20+ 个设计器快捷键按排版、图层、组件、视图分类直观呈现；
	  26. 容器与窗口中心十字智能磁吸参考线（Center Crosshair Snapping）：在自由拖拽对齐推导中注入父容器与顶层窗口居中基线，组件接近视口或容器中心时自动触发对齐吸附与高亮参考线。
- **富文本/自绘文本的默认前景兜底是纯白**(Arpg 深色底习惯):亮色皮肤
  里标记文本没写色码的段落、以及 #W 白/#Y 纯黄这类深底快捷色,画在
  亮底上全部隐形——不是"渲染丢了"。皮肤补
  `richtext { color: var(--text-primary) }` 兜住默认段;演示文案挑
  #R/#B/#H/#L 这类亮底可见色。

- **设计稿壳色写主题 token,不写字面 hex**(组件画廊暗皮肤翻车):卡片壳
  写死 `background:#ffffff`,暗色皮肤整面墙留白补丁,内容文字(跟随
  --text-primary 翻白)在白底上集体隐形——壳和组件用了两个颜色来源。
  壳的正规画法与 base.css 对 card/panel 同源:
  `background:var(--surface-bg); color:var(--text-primary);
  border:1 var(--border-secondary)`,辅助文字 --text-secondary/tertiary;
  演示数据本身的字面色(ColorPicker 初始值)才保留 hex。

- **输入框与按钮密集槽位内边距紧凑化（解耦 slotGap 与 pad，自适应尺寸档位）**：
  单行/多行输入控件（`Input`、`InputNumber`、`SelectBox`、`TextArea`）及通用盒子/按钮（`Button`、`BoxContent`）
  的内外边距与内部槽位间距必须严格解耦并紧凑化。
  1. 输入类控件：边框内衬使用紧凑内边距（`Scale(6)`），槽位之间（图标、前后缀文本、清除按钮、字数统计）统一采用
     局部紧凑间距 `slotGap = Scale(4)`；严禁将每个槽位都粗暴累加完整的外边距 `pad`（8px），否则会导致
     带前后缀与图标的小宽度输入框被多重累加扣除 40~50px，极度压缩有效编辑区域，造成正文被过早截断显示不全；
  2. 按钮与操作盒（`Button`、`BoxContent`）：默认内边距解耦自适应尺寸档位（small/tiny 采用 paddingSmall 8px，
     常规采用 paddingMedium 12px），严禁无差别粗暴写死 paddingLarge（16px+16px=32px），否则紧凑小按钮内的标签文本
     会被过早压缩截断为省略号。
- **组件固有几何约束机制（通栏横幅锁定宽度、单行控件锁定高度、安全防塌陷阈值）**：
  在设计器和布局引擎中，各类型组件必须遵循固有几何约束，严禁随意拖拽拉伸变形：
  1. 通栏横幅型（`ToolStrip`、`StatusBar`、`MenuBar`、`Divider`）：在容器中始终 100% 满宽呈现，
     水平方向坐标 `fx = 0`，宽度锁定为父容器内容宽度；设计器中屏蔽左右（3、7）手柄与角手柄横向调整，
     拖拽移动时锁定横向只允许纵向位移，属性面板与统一尺寸（SizeSelGroup）中禁止横向拉伸覆盖，杜绝工具条/状态栏被拉断为窄半截；
  2. 固定单行高度型（`Input`、`SelectBox`、`InputNumber`、`DatePicker`、`Switch`、`Slider`、`Pagination`）：
     纵向高度由规范档位与字号严格决定，设计器中屏蔽上下（1、5）手柄，角手柄退化为仅横向拉伸宽度，杜绝单行输入框被误拖成超高异形大方块；
  3. 固定尺寸/固定正方形型（`Switch`、`Avatar`、`Icon`）：锁定宽高比与固定尺寸，禁止自由缩放；
  4. 自由容器与复合组件（`Panel`、`DataGrid`、`TreeView`、`TextArea`、`Chart`、`Tabs`）：开放 8 向自由伸缩，
     但必须受最小安全尺寸（`MinSafeWOf / MinSafeHOf`）门控，防止反向或过度拖拽导致组件崩溃塌陷为不可选中的 0/负数。
- **属性检查器分层过滤与渐进披露机制（Inspector Progressive Disclosure）**：
  为彻底解决组件属性检查器（PropertyGrid）列表过长、翻找困难的操作痛点：
  1. 顶部分类标签分流：在检查器顶部提供【全部 (All) / 基础 (Basic) / 布局 (Layout) / 样式 (Style) / 数据 (Data)】五类一级分类标签；
  2. 属性智能归类：`label/name/placeholder/required/wrap` 归入基础，`position/dock/flexGrow/anchor/x/y/w/h/margin/padding/gap/flexDir/justify/alignItems/flexWrap/span` 归入布局，`color/bg/border/radius/font/opacity` 归入样式，`bind/options/columns` 归入数据；
  3. 卡片与分区联动过滤：布局快捷卡片、样式编辑器、数据绑定选择器、选项列表分区与分类标签严格联动过滤呈现，大幅收敛首屏高度。
- **Flex 主轴/交叉轴对称性与行高最小容纳约束（Line Height Minimum Bounding Constraint）**：
  1. Flex 测量主轴与交叉轴判定：横向流（`row == true`）主轴为宽（width），交叉轴为高（height）；纵向流（`!row`）主轴为高（height），交叉轴为宽（width）。断行主轴累计判定（`m > lineRem`）必须与排布阶段取值同源一致，严禁颠倒导致子项意外提前断行；
  2. 行高基线保证与防截断：容器声明的 `line-height`（`declaredLineH`）为单行最小基线，不能强行压制比其更高的子元素（如 Button、Input）；行高计算应满足 `maxCross = Math.Max(maxCross, declaredLineH)`，杜绝子控件被压扁截断；
