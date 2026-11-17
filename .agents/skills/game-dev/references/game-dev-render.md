# Zan 游戏开发——门控渲染与 HUD 合成

> 从 game-dev SKILL.md 拆出：门控渲染、合成契约、世界画布与 HUD 边界、资源内嵌加载、精灵合批。

## 门控渲染：静态不重画，动画跟随显示节奏

- 游戏逻辑与呈现分离：可见世界或 HUD 有动画时按显示节奏绘制；画面完全静态
  时跳过重绘、回读和合成，继续处理输入与必要逻辑。
- HUD/合成贵（一次全画布回读、软件栅格、纹理上传和呈现可占数毫秒），应缓存
  静态内容并声明真实动画损伤，覆盖旧位置与新位置。App 默认局部帧关闭，只有
  调用 SetPartialFrames(true) 后声明才会裁剪；必须用实际 partial 统计和与整帧
  的像素对照验证，不能只看 RequestAnimationFrameIn 的参数就宣称优化已生效。
- 内存精灵缓存按真实字节量检查，并显式清理不再使用的状态/尺寸键。为什么：
  mem: 注册表的项数上限不受文件图片缓存的 12MiB 预算约束；256 个 132×148
  姿态已占约 19MiB，保留多种状态和尺寸会继续累积。用真实 MemReport 验证
  状态切换、缩放与最终释放，不能从文件缓存上限推断内存精灵已受限。
- **鼠标移动不得击穿重绘门**：MouseMotion 每秒上百条，只记坐标，不标记
  需重绘——否则轻轻晃一下鼠标，门控失效，恒定满帧率。
- 失焦"几秒一刷新"的教训：放宽档位的同时必须保留**唤醒源**（状态切换、
  输入事件立即合成的那条路）。节流节掉唤醒源，窗口就像死了一样。
- 可见窗口失焦仍按显示节奏推进世界、GUI 与皮肤动画；隐藏/最小化才暂停
  光栅化。动作/工厂游戏通过 GuiHost 分开固定逻辑步与显示帧，并保留必要
  输入状态。为什么：用户实机否决失焦时世界停住，焦点或输入空闲不能代表
  没有人在看。
- **降速闸门必须选"没人看"的真信号，输入空闲不算**（Gui App 帧循环对照
  条 用户实机反馈后删案）：App.zan `FxIntervalMs` 曾按输入
  空闲把 BackdropFx 心跳 ×2/×4/×8（基值才 80–200ms，×2 起粒子漂移就
  肉眼可见地一卡一卡）——失焦可见窗首当其冲。前提"输入空闲=没人看"
  不成立：可见动画按显示节奏推进；CPU 节省来自**隐藏/最小化**暂停绘制、
  静态缓存和**条带裁剪**。页面与背景特效同拍合成后只提交一次，但仍须
  推进条带外特效并声明旧/新损伤，不能靠放慢可见动画或跳过特效来省提交。

## 合成契约：重绘后才能 present

- 画布内容在 present 后不保留。**拿旧画布凑帧会把空 HUD 贴上屏**（整条
  顶栏闪烁）。任何"跳过一帧不画但照常 present"的优化都违反契约，症状
  就是周期性闪屏。
- 场景帧与 HUD 帧解耦时，场景帧保留在后备缓冲，中间拍不回读。
- HUD 不显示/不置顶/画布错位，先查三条：合成循环是否根本没跑（空闲
  死锁）、是否绕过了置顶链、锚点算的是窗口还是画布坐标。

## 世界画布与标准 HUD 的边界

游戏 UI 采用 HTML + CSS 或保留模式声明式对象树。HUD、血条、伤害数字、
小地图 UI、菜单、背包、提示、结算与 SceneDesigner 都属于标准控件面；
像素绘制封在控件 `OnPaint` 内，应用只配置属性、样式、绑定与数据。
RTS/动作游戏也遵守这条规则，高刷新率不是外部直绘 HUD 的例外。

世界、实体、地形、世界内粒子等仍走 Zan.Game 的世界/视口渲染，
不把世界渲染当成 Gui 控件范式。两者在同一帧合成，避免多个宿主重复
清屏或 Present；帧顺序按实际 GuiHost/视口契约核对，不照抄旧手工布局循环。

- **长生命周期控件**：初始化时建 HUD 树并接事件，对局中更新状态与绑定。
  不逐帧重建控件，不另建 zone 表与手写按钮命中体系。
- **检查器的持久属性树与单一提交**：SceneDesigner 等属性面板按实例保留
  根、属性行与输入控件，渲染/选择切换只同步值和显隐，不重建或重挂子树。
  Change/Click 走一个模型提交出口，回填时抑制提交，比较旧值后才写入并
  记录一次 undo；无变化、拒绝值和绘制不添空 undo。资源选择调用 `SetText`
  后须显式 commit（如 `CommitInspectorText`），因为 SetText 不触发 Change；
  RGB 提交保留独立 alpha。2026-11-11 `scene_designer_inspector` 严格 golden
  probe 覆盖真实离屏输入、undo、anchor、RGB/alpha、资源、滚动和根/行稳定，
  `scene_designer_color` 亦通过；这些结果不代表 IDE 或正式 CTest 已通过。
- **区域 HUD 先绑定与准备，再绘制**：输入前用 `HudLayer.SetBounds(x,y,width,height)`
  （App 坐标）与 `Bind(app)`，共享 prepare 对同一树 Measure/Arrange；
  `Paint(app)` 只消费该区域的准备结果，不 Bind、不 Measure/Arrange、不重置区域。
  旧 Render/RenderIn/RenderViewport/PrepareLayout/BindInputRoot 已移除；Paint 核对
  当前 App、适配器实际根链与准备证明，隐藏、摘除或跨 App 的旧绘制不能复活输入。
  连续 Paint 不改准备票据、根代次与布局次数，首绘前真实输入另作断言。
  区域尺寸决定的子控件样式须在 Measure 前由现有 PrepareView/状态同步配置，
  不能在 Arrange 才改 CSS、再靠重复 Bind 补测；Pin 首菜单点击曾暴露此迟到布局，
  前移配置后删除了只转发布局的 PinHudRoot，直接采用标准 Panel。
  缩放时隐藏的按钮可保留旧几何；验证刚显示弹层的首次输入，应从当前区域的
  可见目标位置发送 Down 并断言真实命中，再 Up 验证动作，不能先采旧 bx/by，
  也不能预热 Paint/布局。casual HUD 严格回归保留原 golden 与恢复游戏断言。
  SceneDesigner 当前用 `InitializeInput(app)`、`SetBounds(app,x,y,w,h)` 与
  `SetHostPanelBounds(app,tools,properties)` 更新自身区域；SceneDesigner、Cards、Inspector
  与 ScrollColumn 新旧布局/输入生命周期均有对应 frozen strict 收据，SceneView 与 casual HUD
  仅按各自 direct probe 计。不要外推为完整 IDE、Gallery/tools 或实窗验收。
  不另算命中坐标、不重新收养节点，单独 RenderTree 不接输入。关闭 dock/离页用
  `SetInspectorVisible(false)` 或该层 `SetVisible(false)`，只退休本层输入。
- **验收状态入口（2026-11-14）**：具体收据、版本与未验项按本节列出的
  单项结论与待验项为准。
  ControlInputHost reentry、DynamicTags options 已有各自正式单项通过，不外推为完整产品验收。
  Tag 当前缺陷及后续测试、素材工具新布局、AI 修订仍待验；Gallery 当前官方构建仍在运行，
  完整 IDE 性能/帧预算仍待验。capture、首输入与重入按对应版本收据确认，历史局部通过不替代当前实窗证据。
- **ScrollColumn/Cards 的 viewport cull 不改作者可见性**：新旧 ScrollColumn 与 Cards
  收据验证 viewport cull 不覆写作者设置的 `visible`；绘制、命中与焦点统一使用
  `ChildInViewport` eligibility，且该 eligibility 不参与 measure，避免上一轮视口结果改变内容
  extent。保留 snapshot 与 generation/reentry 守卫，断言按当前真实 clip 更新，不用应用侧扩大
  viewport 绕过生产裁剪或测量缺陷。
- **聚合输入根须覆盖焦点与成员查询**：HudInputRoot 不收养 HUD 节点，必须通过 `Control.ContainsInputControl` / `FindInputFocus` 覆盖它聚合的子树。重排根仅取消已经离开新输入树的捕获；隐藏、Remove、Clear 调用 `App.ReleaseInputSubtree`，只释放本层状态。2026-11-12 GuiHost 原用例新增多层字符输入、重排/移出区域后捕获与隐藏隔离断言，以相同 golden/leak/guard 通过；测试循环持有 Button 时，Stop 必须解除反向强捕获的测试回调，否则测试自身成环。此结果不等于全部 HUD/IDE 已迁移。
- **性能优化留在组件层**：静态缓存、变值更新、真实动画损伤、限速
  重绘与素材合批。帧预算包括世界、HUD 与最终提交，不能凭历史均值
  宣称当前组合有固定余量。样式 offscreen before `gui-style-benchmark-20261114T104202Z`
  与 after `gui-style-benchmark-20261114T105803Z` 均为 201 nodes × 120 frames、两模式 style_resolve=0。
  总分配均 57758400→53899200 bytes（−6.68%）；measure 27916800→26947200、
  arrange 4819200→1929600（−60%）、render 25022400 不变。普通/关系 pipeline
  270588/272022→256466/253595 μs，单轮观察下降约5.2%/6.8%，不代表普遍加速、
  零分配或游戏整帧达标，也不冒充历史旧版全面基线。style 功能和 flow regression
  分别通过，core 三文件输入冻结；broad manifest 有 Designer 并发变化、功能收据
  frozenPass false，不能合称整批冻结通过；该轮性能证据不代表消费方当前验收状态，后续结果统一查上述架构文档入口。
  经验：按 measure/arrange/render 归因分配，核心冻结与广域无漂移分别核对。
- **JsonValue 模型更新用 Set**：如 `model.Set("hp", JsonValue.NewInt(hp))`，再标脏。
  Put 与 PutStr/PutInt/PutDouble/PutBool 等助手只追加，重复键读取首值，不能更新
  已存在的 HUD/页面字段；更新用 Set(key, JsonValue)，不虚构 SetInt/SetStr。
- **游戏发布与原生证据分开**：五个 game 的实际 Main publish 编译均通过；FPS 最新实际发布
  `gui-consumer-fps-3d-20261114T134002Z` 为 pass、无 fallback、12 objects、changedInputs:[]。
  具体收据及编译器身份见上述架构文档。native 的 `observed-native-routing-only` 只证明
  对应启动、路由和关闭，不构成画面、玩法或完整产品验收。
- **文档入口**：约定式窗口 `new MainForm().Show()` 配同名 HTML；
  已有宿主页面 `Gui.Html.Load(app, html, handlers, baseDir)` 返回树，
  再挂进父控件，并按需注册 ModelBinder。Parse 内自动 Install。
  zanc 只编译 `.zan`，HTML 运行期读取，发布用 `--embed`。
- **CSS 组合**：HUD 根与窗口表面需要让世界透出时配置透明；根尺寸
  按 Flex/Grid 与视口确定，不能靠数字 dock 或已删除的 JSON 建树接口。
- **输入分工**：HUD 使用控件路由、焦点与指针捕获；世界输入应尊重
  UI 消费结果，防止同一次操作同时触发 UI 和世界。
- **像素检查在 Present 前**：Present 后不再假定画布保留；采样回读
  自身有成本，不放进生产帧热路径。FillRect 的夹具标记须有不透明 ARGB alpha，
  如 `unchecked((int)0xFFEA00D4)`；省略 FF 是透明色，会伪装成没画或 clip 错误。
  先确认标记可见，再检查裁剪/合成，不能据透明夹具误修生产代码。

以上是当前开发约定，不表示仓库旧 HUD、SceneDesigner 或各模板已全部
迁移或通过实机验证。实现现状与迁移门见本节前述验收边界。

## 资源：内嵌内存加载

- **渐变遮罩先核对混合契约**：`Canvas.FillVGrad` / `CDraw.FillVGrad`
  是覆写像素的背景填充，透明端点不等于叠加透明；盖在贴图上会抹掉
  材质，并在矩形边缘产生断层。半透明遮罩用 `Canvas.FillGradMask`
  的混合路径，纯色遮罩用 alpha 混合的 `FillRect`；背景已足够清晰时
  不加遮罩。用遮罩内外的像素与实际截图验收，不能只检查 ARGB 参数。

- 内嵌资源全程内存加载（ReadAllBytes→内存解码），不落盘解压。字节链要
  显式长度——**内嵌 NUL 会截断**，音频/图片"随机坏一块"先查这里。

- **模板的 `assets/` 会被编译期自动内嵌进可执行文件**：编译器在入口文件同级、
  上一级以及包项目根下找 `assets/` 目录，找到就整体内嵌（资源名保持
  `assets/xxx`）。所以发布单文件程序不必带旁挂素材目录；反过来，
  "把素材目录删掉再跑"**验不到矢量兜底**——内嵌副本还在，
  `File.Exists("assets/x.png")` 依然为真。要真验兜底，必须在**没有该目录的
  情况下重新编译**（临时把目录改名再编译）。为什么：platformer 首轮
  "缺素材"测试连续三次看到背景照常出现，误判兜底失效，真因是资源已进 exe。

- **视差背景裁源矩形，不要平移两张接起来**：把同一张图按 `cameraX*0.25`
  平移两份拼起来，接缝处会有一条竖直硬边（天空/山景尤其明显）。正确做法是
  图放大到比视口宽（cover×1.2 左右），按偏移**裁源矩形**取景
  （`Canvas.BlitImage` 的 9 参重载 / `CDraw.BlitImageSrc`），越界偏移夹取，
  取景永远落在图内，横向滚动不会有缝。

## 渲染增强与精灵合批（SpriteBatch、烘焙与动画时间轴）

- **精灵批量渲染（SpriteBatch）性能基线**：
  - 弃用逐个 `DrawImage` 的高频开销，使用 `SpriteBatch` + GL `ZGL_K_SPRITE`（Kind 10）单批次提交成千上万个带 tint 颜色的四边形。万精灵批处理可在 0.1ms 内完成，完全满足 60fps 预算。
  - **浮点转整型 NaN 踩坑规避**：C/GL 后端解析 tint 时，严禁把 float 数组槽位强转为 int `(int)q[8]`，因为 `0xFFFFFFFF`（纯白不透明）在 IEEE 754 浮点下是 NaN，x86 `cvttss2si` 指令会将其强制转换为 `0x80000000`（导致半透明纯黑）。必须使用 `memcpy(&tint_raw, q + 8, sizeof(u32))` 保持原始 bit 模式。
- **离屏自绘烘焙为 GPU 纹理（BakeSprite）**：
  - 复杂粒子、光环与动态生成的矢量图，可通过 `Canvas.FillCircle/DrawRect` 等离屏绘制后调用 `Canvas.BakeSprite(key, ...)` 直接写入 GPU 纹理缓存并获取 handle，供 `SpriteBatch` 单批次极速复用。
  - 若重新烘焙同名 key，GL 后端自动销毁旧纹理并重绑，避免 GPU 句柄泄漏。
- **动画驱动与按需出帧契约（AnimationTimeline.BindApp）**：
  - 放置类游戏或交互界面最佳实践：使用 `AnimationTimeline.Shared.BindApp(app)`。当有 `Tween`、数值滚动或粒子发射时按需触发重绘；当动画结束时自动休眠，实现真正的 0% CPU 闲置占用。
  - **App.Show 后的表面重建契约**：`App.Show` 内部会调用 `SwapCanvas` 重新分配主表面，因此在 `Show` 之前的 Canvas 句柄会失效，绘制代码中必须始终动态获取 `app.canvas`。
  - **规范命名正交解耦**：动画调度时间轴统一采用 `AnimationTimeline`，与界面展示控件 `Gui.Widget.Timeline` 彻底分离，同时引用两命名空间时零歧义冲突。

