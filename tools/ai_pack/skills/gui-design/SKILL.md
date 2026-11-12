---
name: gui-design
description: Zan GUI（Zan.Gui 包）审美与排版规范——对齐、间距、尺寸档位统一、层级克制、常用风格配方、截图还原流程。用 Zan.Gui 写界面/做皮肤/做 HUD 菜单面板，或界面出现控件重叠/排版混乱/尺寸异常/毛边锯齿、画图表斜线曲线、HTML+CSS 写窗口时使用。 仓库内有同名项目级版本，会自动优先。
---

# Zan GUI 界面审美规范

**精美 = 一致 + 克制 + 有方向。** 一致靠档位:Zan.Gui 内置了字号、高度、
间距三套全库统一的阶梯 token,永远从档位里取值,界面就自动"齐";克制靠取舍:
一个画面只有一个视觉重心;方向靠配方:选定风格方向后,一切从那张配方卡推导。

- 主流风格九张配方卡:`references/style-directions.md`(用户点名风格、
  或要求"好看一点"而现有皮肤不对味时,写码前先选卡)
- 页面搭配模式与反模式:`references/composition.md`(写页面布局前先读)
- 一张截图严格还原布局:`references/screenshot-restore.md`(用户给截图要
  求"照这个做/还原/复刻这个界面"时先读——测量转写→布局账本→原语映射
  决策树→同尺寸对拍验证;内含交互体验基线清单:反馈闭环/防呆/键盘/
  状态完整,还原或新写收尾都过一遍)
- CSS 方言/引擎解析规则全集(选择器/伪元素/单位/已知坑):
  `references/css-dialect.md`(改皮肤或写 CSS 前先查)
- HTML 窗口开发定式(.html 设计稿五件声明原语、窗口与页面装配、
  所见所得设计器、Nav 出口、游戏帧内边界):
  `references/html-window.md`(写 .html 设计稿窗口前先读)
- 复杂窗口的迭代过程纪律(先问框架要、加高标题栏三处同步、动效帧调度、
  弹窗层序、截图驱动的小步收口):`references/layout-iteration.md`
  (标题栏+导航+内容+弹窗+动效的窗口,动手前先读——每条都是真实返工换来的)
- 把老程序迁移/复刻到 Zan、或参照现有产品做同族工具:先读 `app-migration`
  skill(复刻不创造、映射账本、行为/体验保真、验证闭环)——本文件管"好看",
  还原度纪律在那里。
- 游戏:实时/帧循环类(动作、手感、HUD 合成、失焦、移动端触屏)先读
  `game-dev` skill;文字/棋类/回合制/放置等控件驱动的游戏也遵守本文件的
  排版规范。**图形游戏使用 Gui 时必须配置游戏专属皮肤与 UI 素材，使
  控件匹配场景美术；默认桌面外观不能作为交付效果。** 菜单、HUD、设置、
  结算、提示与弹层一并覆盖，适用的悬停/按下/选中/禁用/焦点状态同风格。
  依据:用户指出两套风格混搭会降低游戏品质。实施与整屏截图验收见
  `game-dev` 的“图形游戏的 Gui 组件必须匹配游戏美术风格”；无需为换外观
  重写控件，布局与交互复用标准 Gui，风格配置集中进游戏皮肤与语义类。
  游戏内面板间距都走 4 的倍数档位。同屏混排(世界画布 + Gui HUD/面板)时,
  缩放路径的边界按下文“缩放纪律”划分，字号必须同源。
- 两范式开发心智模型/控件目录:`docs/agent-kb/gui-development.md`
- 样式解析规则:`docs/STDLIB_COMPONENT_STANDARDS.md`《附录:取色与取字的统一封装》(Tailwind 原子类引擎已移除,皮肤走语义类 + token)

## 三条尺寸阶梯(硬规则)

**唯一真源**是样式包里 `Space`/`Radius`/`WidgetSize`/`Motion` 四个常量类:
`Theme` 的默认度量从它们取值,`Style.RootFromTheme` 把同一组数导成
`:root` 变量,皮肤可整体改值——**永远不要写死数字**。

**为什么是硬规则**:同一个语义(比如"卡片内衬")在不同文件里写着不同
数字(8 / 10 / 18 / 20),相邻表面就永远对不齐——这是"界面看着不精致"
最主要的原因,比配色和图标明显得多。而且裸像素**不会**跟着皮肤、密度档
和 DPI 走,主题令牌会:写死数字的表面在换肤/换密度后立刻露馅。

**字号**:`var(--font-size-tiny/small/medium/large/huge)` = 12/13/14/16/20。
正文 medium(14),辅助/标签 small(13)或 tiny(12),区块标题 large(16),
页面标题 huge(20)。更大的展示数字用 `var(--font-size-display/hero/mega)`
= 24/32/56(基线样式表声明),但一个画面至多出现一个超档大字。**卡片墙的
组头(每卡标题)别挤在 small-medium 之间**:画廊卡片标题先后用 13/15 都被
打回"太小",17 才过——密排卡片墙里 caption 与正文只差 1px 等于没分组,
组头要么 large(16)要么自定义再大一档,和正文拉开两级才算分组。

**控件高度**:`var(--height-tiny/small/medium/large)` = 24/28/32/40
(`WidgetSize`: Mini 24 / Small 28 / Medium 32 / Large 40)。
按钮/输入框/选择框用 `.tiny/.small/.medium/.large` 皮肤档位类
(基线样式表已消费这些 token)。**同一行的操作控件必须同档**;
整块画面主按钮统一 medium,工具条统一 small,别混。

**圆角**:`var(--radius-small/medium/large)` = 4/8/12,胶囊用
`var(--radius-full)`。皮肤想要更圆就改自己的 `--border-radius-*`,
不要在规则里写 `border-radius: 18px`。

**间距**:8pt 网格 0/2/4/8/12/16/24/32/48(`Space`)。命中 7/13/17 这类
值就是错。
- 主题内边距 `var(--padding-tiny/small/medium/large)` = 4/8/12/16
- 主题间距 `var(--gap-small/medium/large)` = 4/8/12
- 原子类阶梯 `var(--space-xs/s/sm/m/l/xl)` = 4/8/12/16/24/32
- 图标/指示器 `var(--icon-size-small/medium/large)` = 16/20/24
- 惯例:卡片内边距 `var(--padding-medium)`/`--padding-large`,紧凑工具条
  `var(--padding-tiny)`,节与节之间 `var(--gap-large)`(12)再往上只有
  16/24,不要发明中间值。

**尺寸 ≠ 间距**:width/height 这类控件几何(指示器 12px、滑块点 10px、
图片 300×220)不套间距阶梯——阶梯管的是"留白",几何管的是"形状"。
但**留白类属性**(padding/margin/gap/border-radius)只准写令牌。

**动效三档**(响应反馈的时长,`Motion`):`var(--anim-fast/medium/slow)`
= 100/200/300ms——fast 是悬停/按下这类即时反馈,medium 是展开收起、淡入
淡出这类状态过渡,slow 是强调性入场。皮肤写 `transition` 只准引用这三个
令牌,不要写 `180ms`/`220ms`:各皮肤各写各的毫秒数,同一个动作在不同皮肤
里速度不一样,手感就散了。时长是**时间不是长度**,不随 DPI/密度缩放。
氛围/装饰性循环(`animation: glow 2600ms` 这类背景呼吸、轮播间隔)不
受这三档约束——它的节奏由内容决定,挤进同一把尺子会丢掉"这个皮肤就是
慢吞吞"这层性格。

## 组件排列整齐与行高间距一致性铁律（美观基本原则）

**美观的基本要求是排列整齐，这是不可违背的铁律与基本原则：**

1. **行高与步进严格统一（严禁忽高忽矮与 0px 间距挤压）**：
   - 列表、树、表格、属性面板（Inspector）、表单中的同类数据行与配置行，行高必须严格采用同一档位（常规单行统一 28px，紧凑行 24px，舒适行 34px）。
   - **Y 轴累加步进铁律**：元素/容器/按钮底部累加必须满足 `ΔY = 控件高度 + 标准间隙(8px)`。严禁直接 `y = y + btnH` 导致后续元素与按钮底边产生 0px 挤压贴死（如分类按钮紧贴搜索框缺陷）。
   - 严禁在同一面板或列表中随意混合硬编码的行高与步进（如一行 +22px、一行 +30px、一行 +38px），导致视觉错落参差、忽高忽矮。
2. **多项配置与选项必须表格化对齐（严禁杂乱字符串拼接）**：
   - 凡是展示选项列表（OPTIONS）、列集合（COLUMNS）、工具项（TOOL ITEMS）等多字段数据行时，**必须以结构化微型表格（Table Layout）呈现**。
   - 必须配备轻量表头（Header：`[ # | 标题 | 绑定/字段 | 操作 ]`）与规整的列网格对齐（序号固定居中、内容弹性左对齐、事件/字段居中徽章、操作按钮右对齐），辅以行底细分割线与微斑马纹。
   - 严禁把序号、名称、事件粗暴拼成单行裸字符串（如 `1. 保存 @OnSave`），更严禁分隔项显示为裸字符 `-`（必须跨列居中渲染 `── 分隔条 ──` 线条）。
3. **行内操作按钮尺寸统一与盒模型规范（加大触控面积防误触）**：
   - 单行内多个小按钮尺寸必须严格统一（统一为 22x22px 标准小按钮盒），常态下必须带有微底色（`bgSecondary`）与独立细边框（`borderSecondary`），鼠标悬停（Hover）呈现明确高亮（`bgHover` / `primary`）。
   - 内部图标字形统一放大至 11~12px 且严格在盒内居中，点击测试判定区域（HitTest）必须覆盖完整的 22x22px 按钮盒，**严禁使用无边框、无底色、触控面积仅 14~16px 的微小裸字形，彻底消除容易点偏点歪的误触隐患**。
   - 必须严格在行高内垂直居中（`iy + (rowH - btnH) / 2`），严禁操作按钮向下偏移下沉到下一行或遮挡下方元素。
4. **彻底杜绝控件重叠与遮挡（严格防遮挡安全边界）**：
   - 当单行右侧存在操作按钮（如编辑 `[✎]`、上下移动 `[▲][▼]`、删除 `[✕]`、事件 `[⚡]`）时，**必须预先严格计算操作按钮区的总宽度**（含按钮宽度与间距）。
   - 左侧的文本标签、输入框宽度上限必须严格扣除右侧按钮区宽度与安全间隙（`ix + iw - actionsW - gap`），并通过 `Ellipsis.Fit` 进行文本自适应截断，**严禁文本或输入框穿透挤压到操作按钮上产生重叠遮挡**。
5. **间距倍数与同列对齐**：
   - 所有间距统一遵守 4 的倍数（`gap-none/small/medium/large` = 0/6/8/12，padding/margin = 4/8/12/16px）。
   - 标签列与输入列在垂直方向上必须保持同一基准线对齐，严禁左侧标签宽度参差不齐导致右侧输入框“犬牙交错”。

## 窗口程序开发基本原则：UI 线程专职渲染与业务完全异步解耦铁律

**UI 线程必须且仅负责事件分发与渲染，所有业务操作/耗时操作/IO 必须异步非阻塞，严格完全解耦。这是窗口程序开发不可妥协的基本原则：**

1. **UI 线程单一职责约束**：
   - UI 线程（主事件消息泵）的核心生命线是保持每秒 60/120 帧的无阻塞响应。它的唯一职责是：接收并派发系统输入事件（键盘/鼠标/触控/窗口缩放）以及执行当前帧的布局与光栅化渲染（Layout -> Paint -> Present）。
   - **时间预算红线**：UI 线程单次事件响应或帧内回调耗时**严禁超过 2ms**；任何可能超出 2ms 的逻辑必须立即移出 UI 线程。
2. **UI 线程高压禁令（严禁在 UI 线程同步执行）**：
   - **网络通讯**：严禁同步调用 HTTP 客户端、下载上传、WebSocket 握手与长阻塞轮询；
   - **磁盘与文件 I/O**：严禁同步读取大文件、全量遍历磁盘深层目录树、大资源或日志落盘；
   - **数据库与存储**：严禁在 UI 线程同步执行 SQL 语句、数据库事物提交或大键值集合持久化；
   - **重度 CPU 计算**：严禁在 UI 线程执行图像像素变换/滤镜、音频重采样、大 JSON/XML 文本反序列化、大数据量排序/过滤、代码高亮词法分析与图表海量点力导向计算；
   - **阻塞式等待与休眠**：严禁在主线程直接调用 `Thread.Sleep`、`Process.WaitForExit`、子进程同步管道读取或无超时内核锁死等。
3. **异步解耦标准三段式交互范式（反馈 -> 异步作业 -> 封送回填）**：
   - **第一阶段·UI 立即反馈**：用户点击瞬间，UI 线程仅改变界面状态（如设置按钮 `Disabled = true`、展示局部 `Spin` 加载动画、骨架屏或状态栏文案），耗时 < 1ms，立即结束当前事件并放行事件循环，绝无卡顿感；
   - **第二阶段·后台非阻塞处理**：通过 `Task.Run`、`Thread.Start` 或专用 Worker 协程池执行实际的耗时业务与 IO 计算。**后台计算代码严禁直接读写、遍历任何 UI 控件实例**，所有输入数据在派发时以不可变对象或副本传给后台，实现纯内存隔离；
   - **第三阶段·App.Post 安全封送与重绘**：后台处理完毕后，将结果通过 `app.Post(() => { ... })` 打包为委托封送回 UI 线程。`App.Post` 底层通过跨线程事件与 `window.Wake` 唤醒正在休眠等待系统消息的消息泵，在 UI 线程统一安全排空回调（DrainPosts）、更新 ViewModel/控件状态，并自动置位 `needsRedraw = true` 触发下一帧呈现。
4. **生命周期协同与取消机制（防野指针与脏回调）**：
   - 启动后台任务时必须配发协作取消标记（`CancellationToken` 或线程安全 bool 标志）；
   - 当用户关闭窗口、切换选项卡或发起新请求覆盖旧请求时，必须立刻置位取消标志；
   - 后台作业在执行中及回传前检测取消状态，作废时直接丢弃结果，杜绝向已销毁或已复用的 UI 容器写入陈旧数据引发越界崩溃或状态污染。

## 对齐规则

- **表单行**:标签列固定宽(`Prefer(90, 0)`),输入列吃剩余空间;多行表单
  同一列宽、所有输入框同高同档。模式示例见 `references/composition.md`。
- **数字右对齐**(金额/计数/尺寸),**标题左对齐**,**操作按钮右对齐**
  (工具条、弹窗脚部一律右侧,主按钮在最右)。
- **垂直居中**:同行图标+文字组合挂类串 `flex items-center gap-2`
  (经 `Control.Class` 字段或 `AddClass` 挂上);控件混排靠高度档一致
  保证基线齐,不靠逐个调 y。
- **网格对齐**:等宽卡片用 `grid`(`grid-cols-3 gap-3` 原子类或
  `Grid.Of(n).Gap(px)`),不要手摆 x/y。
- **一排控件两端留白要相等**(如一排按钮右端与左端对齐):行首/行末各放一条
  **同宽撑条**,容器挂 `display: flex`,要"各宽几像素"的子控件挂
  `flex-grow: 1` —— 剩余宽度平摊到控件本身,余数由引擎给最后一个可增长项
  (两排右缘因此落在同一列);撑条 flex-grow 0 不动,所以**间隙逐条不变**。
  容器使用 Flex 布局时，子项的剩余空间分配按最终 `flex-grow` 计算。
- 容器边缘:相邻区块共享同一条左边界,面板左 padding 必须同值。

## 层级与颜色

- **层级靠中性色 + 字号阶梯**:`var(--text-primary)` 正文、`text-secondary`
  次级、`text-tertiary` 弱提示、`text-disabled` 禁用。标题只升字号,不变色。
- **强调色只有一种**:`var(--primary)`。主按钮一个,其余 `.secondary/.ghost`
  变体;success/warning/error 只表状态,不当装饰。
- **颜色必须走样式层**:控件代码禁直读主题语义色、禁裸 `0xAARRGGBB`、
  禁直读字号——取色/取字一律走 StyleBox(`Style.Of(app, type, cls, ...)`,
  属性带兜底如 `s.FgOr(...)`/`s.FontOr(...)`)。
- Tailwind 原子类只用于布局/间距/圆角/阴影,**不用它的调色板**
  (`bg-slate-500` 会绕开皮肤主题,换肤即脏);要颜色写语义类或 `var(--token)`。
- 圆角同档:`var(--border-radius-small/medium/large)`;同一画面出现 3 种
  圆角就是没设计。阴影只用小/中档,弹层才允许大阴影。

## 风格方向(先选卡,再写码)

统一档位解决"任何风格都不塌";风格本身走**皮肤配方**:一个皮肤包 = 一个
`skins/<name>/skin.css` 的 `:root` token 覆写,零代码,随应用发布
(应用自带 `skins/` 优先于内置皮肤)。

九张现成配方卡(商务/街头嘻哈/赛博朋克/暗金豪华/国风/极简日式/玻璃拟态/
新拟态/卡通波普)在 `references/style-directions.md`,含可粘贴的 `:root`
覆写、排版/动效/文案性格、能力边界与验收点。选卡前过"选型三问":题材的
身体记忆、签名元素只放一处、拒绝 AI 模板脸(米色+衬线+赤陶、纯黑+荧光绿、
报纸细线)。

## 游戏素材的硬判据（模板与正式项目通用）

画面能不能看，**编译器与 ctest 都判不出来**,只有人眼能。所以素材有三条硬判据:

1. **素材必须人眼验收过**,门禁只能挡"色块"挡不住"乱图"。
   坑出处:某个平台跳跃模板的 `hero.png` 曾是一张抠图失败的图(白底一团重复
   椭圆)——编译过、门禁过、跑起来才发现主角是"一堆椭圆"。抠图类素材
   (绿幕 keying)失败率不低,生成完必须看图。
2. **HTML 场景贴不了图**:HTML/CSS 引擎不支持 `url()` 背景图
   (`background-image` 的 url 会被当无效值丢掉),所以"HTML 场景 + 真美术"
   只能是**画布层画图 + HTML 层叠字与按钮**:场景类的 `Render(Canvas c, ...)`
   里 `Canvas.ImageWidth/ImageHeight` 量比例、`CDraw.BlitImageSrc` 裁源铺底,
   HTML 那边把 body 背景改成半透明让画布透出来。
   坑出处:贪吃蛇模板的 `menu_bg.png`(霓虹星空,质量很好)一直闲置,
   菜单却是纯 CSS 渐变 + 文字——素材躺在仓库里没人看。
3. **共享资源要"拷贝"进项目,不要"引用"仓库路径**:共享纹理/底板从共享目录
   拷一份进项目自己的 `assets/`,代码只认项目内路径。项目被拷到别处
   (新建工程、导出成独立仓库)时引用仓库路径就崩。

同一套判据的可执行版本在素材规范里:色板 + 绘制规则 + 素材检查脚本
(资源引用完整性 + 模板独立性 + 非色块)。

## CSS 方言:引擎认什么、不认什么

改皮肤/CSS 前先读 `references/css-dialect.md`——选择器/伪元素/取值单位/
已知解析坑全集都在那一册。硬底线只有三条:颜色一律
`var(--*)`或皮肤令牌;字号/间距/高度一律走尺寸阶梯 token,不写死数字;
未知属性写上去不报错也不生效,收尾自查要过一遍。
## HTML 窗口开发定式(硬规则)

GUI 创作采用两条标准通道：HTML + CSS，或保留模式声明式对象树。
图表、仪表、代码高亮及游戏 HUD/菜单/SceneDesigner 也封装成标准控件；
像素绘制收在控件 `OnPaint` 内，使用处只配置属性、样式与绑定。
Zan.Game 的世界渲染属于游戏引擎，按其世界/视口渲染契约执行。

### 窗口入口与文档装载

- **嵌入宿主与内部控件同名**：DesignedControl 外层宿主 id 可以与内部文档 id 相同；内部访问器应从 children 按文档 id 搜索，排除宿主自身和文本回退，缺失仍显式失败。否则 CtxRing 的宿主 Ring 会先于内部 Progress Ring 命中，触发 InvalidCastException；不得改业务 id 绕过。回归应真实设置 host.name 与内部 id 相同，证明旧查找失败、新查找通过。

- **约定式窗口**：`class MainForm : Form` 配同名 `MainForm.html`，
  调用 `new MainForm().Show()`。`Show` 找文档、解析、读取窗口键、
  初始化 App、挂树与模型泵，再调用 `OnLoad` 并进入循环。
  零参 `Form` 构造不初始化 App；不要直接在新建子类上调用 `Run()`。
- **已有宿主中的页面**：`Gui.Html.Load(app, html, handlers, baseDir)`
  返回控件树，并合入文档作用域样式、接链接导航；`Parse` 内自动
  `Install`，调用者不用手工安装声明层。此入口不负责挂进父控件，
  也不自动为页面注册模型宿主。使用 `host.Add(ui)`，按父布局为根
  声明 `flex-grow: 1`；需要绑定时再挂 ModelBinder。
- **编译只输入 `.zan`**：`.html` 是运行期文档，zanc 拒绝将其当源码。
  发布用 `--embed` 带文档；文档 `<style>` 随装载进入应用样式，
  不再使用生成类、静态 Css/Build 或 JSON 建树入口。
- **窗口键**：同名文档的 `data-win-title/win-w/win-h/win-chrome/
  win-center` 由 `Form.Show` 装配消费。显式 `Html.Load` 只是页面装载，
  不替宿主新建或配置窗口。

### 内容、事件与模型

- **Children 集合必须维护真实父链（2026-11-12 实证）**：自然嵌套 `new Row() { Children = { new Column() { Children = { input } } } }` 通过 getter 返回的集合逐项 `Add`，不会调用属性 setter；返回裸 `List<Control>` 会绕过 adoption，导致 parent/App 链断裂和输入捕获无法退休。`Control.Children` 现为 getter-only、缓存的 `ControlChildren` 门面，提供 Count、只读索引、Add/Remove/Clear 与 foreach；不再使用 `ChildrenCollection` 或向 Children 赋 List，Row/Column 的 `Children(List<Control>)` fluent 方法保留。业务结构修改走门面或 Control 的树方法，不直接改 backing `children`。
- **移除、重挂先退休旧宿主输入**：共享 `Control.Remove` 在仍有旧父链时调用 `ReleaseInputSubtree`，再重查索引和父归属后摘除；Clear 用快照逐项 Remove，跨父/跨 App adoption 同样经过旧父 Remove。取消回调若已把节点挂到第三父或请求目标父，外层 Add/With/InsertAt 尊重新归属，不能重复追加或强抢。门面弱引用 owner，owner 释放后 Count=0、Add/Remove/Clear 无害，枚举快照仅持有子项；不得让门面或 enumerator 保活 owner。回归 `tests/gui/control_children_lifecycle_test.zan` 已以真实 offscreen 输入、嵌套初始化器、焦点/捕获退休、取消重入重挂及 weak 生命周期通过 strict/golden/leak/guard。

- 文档声明结构、样式与事件名，code-behind 管状态和事件响应。
  优先用正典语义标签与 `zan-*` 控件标签；具体映射以
  `Html.Build.zan`、控件注册与属性协议为准，不凭浏览器经验猜。
- 五件声明原语：`data-on-*` 事件、`data-arg` 字面参数、`data-bind`
  模型路径、`data-if` 路径真值、`<template data-for>` 列表。
  `@click` 等语法糖见 `references/html-window.md`。
- `Form.Show` 默认把窗口实例作为模型；已有宿主里的页面需
  `ModelBinder.SetRoot(ui, model)`、`Wire()`、
  `ModelHosts.Register(app, binder)`，撤页时 Unregister。
  同一个事件选 handlers 或 binder 一处接线，避免重复订阅。
- 值、显隐、列表内容优先改模型并 `RequestRedraw()`；结构变化走
  控件树操作或重新 `Html.Load` 后替换旧树。重新装载不会替你替换
  父节点中的旧树，也不会自动注销旧 binder。
- 文档元素携装载期作用域类；状态切换用 `SetClassIn`，不要整串覆盖
  `Class`。代码后挂的控件没有文档作用域类，共享外观放全局皮肤层。

### 布局与样式

- 根与中间内容区用 Flex/Grid/Flow；顶栏、内容、底栏按文档序排列，
  固定栏 `flex-shrink: 0`，内容 `flex-grow: 1; min-height: 0`。
  百分比高度须有确定包含块；不能用历史 dock 数字解决高度塌陷。
- `Control.dock` 仅为兼容属性：setter 只在值为 5 时调用 `Grow()`，
  其余值无操作；新代码不使用 DockTop/Bottom/Left/Right/Manual。
  需要独立浮层时使用 CSS absolute 或 `Float(x, y)`，它经 `Place`
  写 absolute 样式；普通表单排布继续留在自动流中。
- 样式使用语义类与 token；HTML 不执行脚本。`line-height: 72` 表示
  字号倍数，72 逻辑像素应写 `72px`。不要把 CSS 注释结束符写进注释文案。
- `base.css` 由编译器内嵌兜底；项目只发布自家皮肤，不用旧磁盘基线
  盖住新内嵌基线。文档样式自动作用域化，全局皮肤通过 `UseAppCss`
  或皮肤接口配置。

### 控件与浏览器集成经验

- **WebView2 完成回调的参数归引擎所有,不得释放**: 手写 COM 桥里完成
  handler 收到的结果串(`ExecuteScript` 的 JSON、`AddScript...` 的
  脚本 id)和 `GetCookies` 的列表指针,Invoke 返回后引擎自己回收——
  按"方法出参用 CoTaskMemFree"的惯例去释放,轻则堆损坏(0xC0000374,
  Eval 加一次 Com.Free 即崩),重则过释放 CookieList 后后续 COM 调用
  在 STA 里永久挂死。只有同步方法调用拿到的出参(get_Source 等)才走
  `Com.GetString/Com.Free`;completion 参数一律只读。

- **WebView2 是"加载器+系统运行时"两层,缺运行时可自动装**: 随应用只发
  166KB WebView2Loader.dll(组件 drivers/ 目录),引擎本体 Win10/11 自带;
  没装的机器调 `WebViewBootstrap.EnsureRuntime` 下载官方 bootstrapper
  (~1.8MB)静默安装(弹一次 UAC),装后要新建 WebView 实例才重试创建
  (created 标志一次性);Win7/8.1 的 bootstrapper 已不派发兼容版(109 为
  最后),会改为打开官方下载页——要 Win7 免安装就发布带 CEF 的
  zan_cef109 驱动。探测用 `WebView2.RuntimeVersion`;用户装没装运行时
  与代码无关,程序只认探测结果,别写死"一定有"。

- **滚轮认领以"真溢出"为前提,不溢出不拦截**(滚动条乱跳的框架级
  根因 修):指针在内就 `CaptureWheel` 但内容放得下的
  容器,会把 offset 钳回 0 空转、白白吃掉本属于外层的一格滚轮;
  指针扫过这类区域时消费权内外横跳,外层滚动条走走停停、来回蹦,
  用户感知为"滚轮一滚滚动条乱跳"。框架引擎已内置该门
  (`Scroll.WheelXY` 双轴、`ScrollView.Begin` 的 `mo0 > 0`),自写
  `CaptureWheel` 的内容滚动组件(日志/聊天/代码块/文本域/编辑器)
  必须自带同款溢出门(`maxScroll > 0` 才认领);弹层、图表缩放这类
  **有意拦截**滚轮的认领不受此限。回归锁定:
  `conformance_gui_wheel_route` 场景 4(放得下的 flat 容器穿透,
  外层逐格线性)。

- **组件画廊/演示墙用瀑布流,大组件跨两列**(组件全量画廊定式):固定
  N 列行打包的行高=行内最高卡,DataGrid/Transfer 这类宽组件挤在单列
  370 逻辑宽里施展不开;改每列独立记账、卡片落"最矮位置"(并列取最
  左),大卡 span=2 横跨两卡槽(双倍宽),生成器断言
  max(列高)-GAP ≤ 内容区高。落位顺序用**高卡降序稳定排序**
  (key=(-卡高,-span),同高跨列卡先落)收平页底——手工把高单卡排前面
  只是打补丁,同高时单卡抢先落位照样把跨列卡的窗口顶高
  (导航页 892>854 翻车后改排序键才收平)。

- **HTML 里"当前选中项"高亮必须 class 驱动,行内 style 只留几何**:
  HTML 的 `style="..."` 不是浏览器那套"内联样式优先"——构建器会把它合成
  一条专属类规则 `.zgen-N { ... }` 并给控件挂上 `zgen-N` 这个类,所以它和
  作者写的 class 规则**同源**,最终由 CSS 特异性决胜:`.opt.on`(0,2,0) >
  `.opt`(0,1,0) = `.zgen-N`(0,1,0)。踩过的坑:把背景/边框/颜色写在行内,
  `.opt.on` 高亮怎么加都不生效——不是优先级被压,是那条高亮规则压根没写。
  规范做法:
  1. 视觉属性(background/color/border/border-radius)全部搬进 `<style>` 里的
     `.opt` / `.opt.on` 规则;行内只留 `font-size`/`padding` 这类几何。
  2. 状态类用 `SetClassIn("optstate on", on ? "on" : "")`:`SetClassIn` 只清
     **group 里列出**的类,group 必须把要清除的状态类也写进去。写成
     `SetClassIn("opt", "opt on")` 会把 "on" 留下(取消高亮时清不掉)。
  3. 改 `Class` 不必手动 invalidate——样式缓存键含 Class 串
    (`Control.ResolveStyleCached`),Class 一变缓存自动失效重算。

完整声明面、页面绑定、设计器与验证方法见
`references/html-window.md`。本规则描述开发约定；具体实现状态及迁移缺口
以 `docs/GUI_ARCHITECTURE.md` 的现状与目标分栏为准。

## 克制

- **把大胆花在一处**:一个画面一个签名元素,其余安静;删掉不服务内容的
  装饰——"出门前照镜子,摘掉一件配饰"。
- **留白是材料**:分组靠间距与分隔线,不靠框套框;拿不准时多留 4px。
- **空态与错误给方向**:空态用 `Empty` 组件 + 一句"下一步做什么";错误
  说清原因与修法。文案写用户视角:"保存更改"不是"提交",一个动作全程同名。
- **动效一处点睛**:内置关键帧 `animate-spin/pulse/breath/shimmer/float/glow`
  等一个画面至多一处;hover 微反馈可以普遍,入场动画不要。

## 组件封装的反哺

- **双击与修饰键读取本次 routed Click 快照**：消费者在 `Click` 中读取 `e.isDoubleClick` 与 `e.modifiers`，不在 PointerUp 读取帧级 DoubleClickNow，不另建控件私有计时器。共享层在业务回调前确认有效主键 Click，并用事件序号防旧兼容路径重复记账；取消、拖动和次键不推进历史。原因：回调可注入另一输入，读取窗口当前状态会把内层修饰键或点击事实误用于外层；严格回归应覆盖同目标混合路由、重绘不重放、修饰键组合及回调嵌套输入。
- **Escape 弹层钩子之后重验登记会话**：覆写 `OnPopupEscape(App)` 可处理取消而保持面板；共享宿主在钩子后检查 popup、owner App 与登记 revision，再决定默认关闭。不能沿用旧弹层快照清理回调新开的会话；普通失焦关闭不调用 Escape 钩子。原因：导出取消与关闭面板是不同动作，回调又可能重开或跨宿主；验证须覆盖 handled、默认关闭、重开和失焦路径。

- **复合行交互尊重嵌入编辑器与子控件捕获**：行拖拽在 Down 冒泡时不得覆盖子控件已有捕获；文本编辑操作按 originalSource 到本行内容根的真实祖先链及 text/IME 能力判定，不能按具体 Input 类型列白名单，也不能把所有可聚焦控件都排除。原因：Button 已收到 Click，之前 Input 聚焦却已误选行，单查按钮计数会误判捕获修复无效。验证须从未选择状态依次执行编辑器聚焦/输入、子按钮点击及普通行选择/拖拽，并分段断言；List 原生命周期与旧 Tree 筛选已通过严格金样及正常退出检查。

- **焦点能力属于当前输入树，不依赖绘制登记**：控件显式声明键盘及文本能力；Down 只检查命中祖先的当前 App、输入归属、显隐与禁用状态，Tab 临时投影当前树顺序，IME 查询当前弱焦点端点。聚合输入成员不要求 parent 链走到适配器；兼容绘制登记须使用弱端点。先提交新焦点再通知旧端点，回调重入后不写回旧状态。验证须在首次 Paint 前发送真实 Down/Up、字符和 Tab，并覆盖重排、弹层、聚合根与 weak 释放；共享焦点及真实 Tree 行编辑器均已通过严格 golden/leak/guard/runtime。

- **拖动在路由事件期提交，绘制只读状态**：主键按下认领 `CapturePointer(control, true)`，Move 与主键 Up 使用事件坐标；Up 先提交最终位置，再结束并发通知，次键释放不能结束主拖动。Cancel 只清状态；Change 回调可能关闭、禁用或替换输入根，回调后须重验捕获和有效状态再发 DragEnd。原因：绘制期轮询漏掉重绘前输入及最后一次抬起坐标；Slider/SplitPanel 与调色板严格回归已覆盖这些边界。
- **普通按压与拖动捕获分别认领触摸手势**：一参 CapturePointer 保留按压语义，触摸越过防抖阈值取消按压并清旧 pressed；拖动显式用二参 true 后继续接收 Move/Up，两种情况都禁止合成 Click。原因：普通 Button 也会捕获，不能以“有捕获”判定拖动；只清捕获又会留下 FocusManager 的旧 pressed。
- **内联 CSS padding 走样式盒，不转成 Pad()**：ApplyInline 不能把 padding 简写取首数写成显式 Pad，否则四边/单位失真，替换或清空样式仍残留 padSet 优先级。实际布局读取解析后的四边值；手工 Pad/Padding 仍保留显式优先级。判定器：`css_dpi_test` 在100%–300% DPI比较首行坐标/宽度与替换样式后的几何；ScrollColumn 回归同时覆盖四边与显式 Pad。
- **借用输入根按结构组合**：HUD/Layer 等适配器共用 InputRootAdapter，以既有 content/slot 链保留被包装的根、弱持宿主与业务 owner，绑定判环只查结构链，不能用受模态限制的 ContainsInputControl；高优先级 Layer 始终在 HUD 外层，重复 Bind 保位，Detach 跨不同适配器拼接且只释放自有输入。原因：各自维护链会产生 HUD→Layer→HUD 强环，后加入 HUD 还可能绕过模态。判定器：`input_root_composition_test` 真实 RenderIn/Open/Render、两种绑定顺序、跨 App、摘除保留其他层焦点/捕获及弱寿命，39 项 strict 通过。
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
- **可见生命周期与 frame fixture 分开验收**：隐藏/零尺寸路径早退的 green 不能代表可见 Ribbon 已释放；回归须实际渲染并重建可见 Ribbon，覆盖 tab/group item/flat command、真实 Down 建立 capture、focus 退休、旧队列失效与退出 weak 释放。离屏绘制配对 Begin/EndOffscreenFrame；App 弱寿命 fixture 先建立独立 frame 宿主，遵守首实例由 frameApp 静态持有的契约，不清 witness 伪造释放。可见 probe 已从 owner/host/三类 Ribbon 全存活修到全释放并通过 strict；正式 `conformance_gui_designer_contextmenu_routed` 已在父批次 PASS（150.23 秒），证据见 `build/test-out/gui-current-failed-jobs4.log`。
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
- **离屏交互测试须运行标准事件队列**：`Button.Click` 通过 Dispatcher 排队，
  `ProcessOffscreenInput` 后须在下一帧前取出并执行 `Dispatcher.Take()` 返回的动作，
  再测量/绘制；只重绘会让标准按钮收到捕获却不执行业务回调。关闭后的动作测试
  应排入实际订阅的 `button.Click`，保留旧根与按钮，确认 weak owner 已释放后再消费。
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
- **嵌入区域的布局、绘制与输入共用同一几何**：检查器/HUD 使用
  `HudLayer.RenderIn(app, x, y, width, height)`，在 App 坐标的宿主区域内对
  同一树 Measure/Arrange/RenderTree，并绑定 Visual Tree 输入聚合根；
  RenderTree 本身不接输入，不另算一套命中坐标、不重新收养控件。关闭 dock
  或离页显式隐藏该层；`SetVisible(false)` 摘除输入入口，只释放该层子树的
  focus 与 pointer capture，不能清掉其他层的输入状态。2026-11-11 检查器
  probe 已验证滚动、hidden 释放焦点和重新显示根稳定；capture 释放是已核对
  的实现契约，该 probe 未直接断言，不将其写为已完成的交互验证。

- **铁律:控件自己负责绘制,使用处只配置**(实例化→配属性→喂数据→摆位置)。
  禁止在使用处用 Canvas 原语重画已有控件——组件修好后示例还在按旧画法显示,
  就会"组件是对的,demo 是错的"。
- **组件的命名空间必须与它所在的目录一致**(BandGrid 教训):组件目录下的控件
  一律声明在 `Gui.Component`——类名与目录不一致就是没归位的信号。Gui 根命名空间留给
  控件基类本身,组件一律在 Gui.Component 下——用户项目里出现同名全局类时
  不会压过 stdlib 真身(限定名按命名空间解析,不按最后一段猜)。迁名的连带
  面通常很小:解析器会沿命名空间链向上找(`Gui.Component.DataTable` →
  `Gui.Component` → `Gui`),同族包里直接写类名不用补 using;只有命名空间与
  目标无关的地方(如独立命名空间的测试)才要补 `using Gui.Component;`。
- **宿主面板有专属入口时,不要在通用开关列表里再列一遍**(停靠面板教训):
  同一个面板既出现在功能区的大按钮里、又出现在停靠框架自动生成的「面板」
  开关列表里,用户会犯迷糊该点哪个。停靠组件给了 `SetNoSwitch`:面板声明
  "不进通用开关列表",入口只留显眼的那一处。
- **放不下的命令要有去处,不能被裁**(Ribbon 溢出教训):工具条/功能区宽度
  不够时,降级(先丢低价值命令、再把带文字的小命令收成图标格)只到"降级到底"
  为止;再放不下就把尾部命令收进右端「»」溢出菜单(Office 式)。命令不许
  被裁成"看得见点不着"——设计器排版组 17 个图标格塞进 324px 工具条时,右半
  (等宽/等高/等尺寸/层次)整片失联,就是没有溢出兜底。
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
- **纯几何消费点击的控件必须自己注册命中区**(Tabs 页签条教训):不走
  On 通用事件包、按几何自己判点击的区域,要 `app.hitTester.RegisterRect`
  自己的条带——FireCommon 只给 `On.Any` 的控件注册命中区,宿主只用
  `TabChanged.Add` 类型化接线(On 为空)时,按页签=按空白,命中解析不到
  任何控件(既不是控件也不是遮罩),释放就没有归属,页签永远切不动。
- **PropSpec 的 str/num/flag 必须绑定可写源**：实例可写字段和同时具有
  get/set 的属性生成实时 Binding，自定义属性保留访问器副作用与分派；
  getter-only、init-only 属性、readonly 字段和方法返回值生成快照，Set
  只修改快照。ListItem 的只读 Label 不可作为写回源，应绑定 Text；
  不能把这一限制扩展成禁止所有自定义属性。实时 target 不持有 owner，
  owner 必须活过 Get/Set；用真实读写与退出 leak/ARC 验证，不能只看 IsLive。
- **控件框高要对齐内部条带的自然高,否则皮肤底线变"两条下划线"**
  (组件画廊顶栏教训):Tabs 的选中指示线画在页签条内容底,控件的
  border-bottom 画在控件整框底;页签条内容自然高=theme.heightMedium(34),
  控件 fh 设 44 时两线错开 12px 看着像双下划线。修法是控件 fh 对齐
  heightMedium,不是去改皮肤线位。
- **设计器选框读取实际布局，默认尺寸只用于创建**
  (Pagination/仪表选区错位及 HTML 设计与运行不一致教训):
  调色板点击与拖拽添加采用一致的控件自然尺寸，避免统一 200x32 造成
  空旷错位；完整窗口预览共用 retained tree，选框、拾取、缩放、换父
  使用实际框。禁止另写逐控件布局或外层裁剪遮盖差异；那会丢掉继承、
  容器 chrome 和合法 overflow。视图 zoom 只缩放最终图像，细则与
  几何/像素/交互验证见 `references/html-window.md` 所见所得专节。
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
  1. 双击进代码时序解耦：严禁在 MouseDown (`ek == 2`) 中消费底层 `DoubleClickNow`，否则在单击选中或拖拽起步时极易误判为双击，打断画布操作并意外跳转代码生成桩事件；必须在 MouseUp (`ek == 3`) 且判定无任何物理拖拽/缩放位移（`!dragging && !freeDragHasMoved && !designResizeMoved`）时，通过组件指针与 400ms 阈值状态机精准触发跳转；
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

## 毛刺防治(斜线/曲线/圆角的抗锯齿)

**毛刺 = 数据边被量化到整像素。** 斜线/曲线在光栅化器眼里只有"每像素覆盖
多少";只要把 1/256 定点的小数坐标交给它,AA 就自动正确。标准库的折线原语
(`DrawLine`/`DrawPolyline`/`DrawPolylineFx`)与 Chart 系列填充
(`FillColumnAA`/`FillBandFx`/`FillSpanAlpha`/`FillPolyAlpha`)都已是亚像素
的;界面出现毛刺,几乎总是绕开了它们自己拼:

- **数据边定点进光栅化器**:图表的线/面积边界这类数据驱动边,计算时保留
  1/256 定点(`yF = base*256 - num*256/den`),填充用带小数覆盖率混合的
  列填充(`ChartView.FillColumnAA`,边界行按 frac 混合)。把边量化成整数 y
  再用 1px 竖条 `FillRect(x, y, 1, h, ...)` 逐列拼,缓坡上每列硬跳一整行
  = 阶梯锯齿,条顶与 AA 折线之间还会露月牙缝(真实案例:DataTable spark
  面积、ChartBig 面积,修复均收口到 FillColumnAA)。
- **折线一笔连成**:整条线一次 `DrawPolyline`/`DrawPolylineFx`;逐段
  `DrawLine` 在拐角处两端各落一次实心像素,双重混合亮一像素、还可能留缝。
- **多边形填充用亚像素扫描线**:半透明多边形(雷达/弦图)用
  `ChartView.FillPolyAlpha`(过圆心自交的非零环绕用 `FillPolyWinding`),
  行中心采样、1/256 定点求交、段首末列小数覆盖混合;不要自己写整数交点
  配整行 FillRect,斜边会成整列硬跳的阶梯。
- **叠画月牙**:两段重合圆弧叠画,下层形状的 AA 边缘会在上层弧外露出一条
  浅色月牙(真实案例:异形窗口关闭钮悬停红 × 标题栏圆角)。修法是上层沿
  下层轮廓内收 1–2 逻辑像素(半径 -2),把下层 AA 带盖进去;不要试图用
  更多叠画去补月牙。
- **一次成形**:圆角控件禁止"方角画完再叠圆角盖"——克隆样式改
  Radius/Corners,一次画对;覆盖裁剪的原语自己拥有边缘,叠补必留边。
- **AA 渐变带固定 1 物理像素**:抗锯齿过渡带宽度是原语内部固定的,别随
  线宽/DPI 自行加宽;要更柔的效果用半透明描边,不动 AA。
- **半透明量程 0–255**:`Chart.WithAlpha` 直写 alpha 字节,传 256 会整型
  溢出成全透明——画了但看不见,极难排查。
- **验证仪式**:离屏 `new Canvas(w, h)` + `GetPixel` 统计"纯色直贴纯背景"
  的硬相接对数做阈值断言(仓库范式 `tests/gui/chart_fillaa_test.zan`、
  `chart_stackedarea_aa_test.zan`:同一图形整数实现 56/152 处硬相接,
  亚像素实现 0/4);肉眼收尾用 PrintWindow 截图放大 6× 看角与斜边。

## Chart 引擎改造与渲染探针(硬规则)

- **ChartSeries/ChartOption 新增字段五处同改**:字段声明、Create/Of 缺省、
  深 Clone、`Clone(src, shareData: true)` 浅路径**早退之前**的标量抄写块、
  浅路径的集合引用共享(levels/tree 等)。渲染管线 MaterializeSeries 每帧
  都走 share 路径,漏抄一处 = 字段静默丢失:模型层解析断言全绿,渲染却
  全吃缺省(真实案例:treemap/sunburst 的 levels 全链解析正确、渲染整图
  缺省配色,根因是 levels 在浅路径早退处被丢,配置断言测不出这种丢法)。
- **渲染探针两条铁律**:要看首帧静态效果用 `ChartView.KeyedStatic`
  (staticFrame 把动画进度钉在终点;普通 `Keyed` 首帧动画进度≈0,
  不在引擎免动画名单里的图型整板近黑);GetPixel 采样必须在 `Render`
  之后、`PresentFrame` 之前——Present 后读到的已是清屏面,每张 demo
  扫出同一单色(真实案例:探针首版把两种错各踩了一遍才定位)。
- **用户实机截图 ≠ HEAD 行为,先核对构建新旧再立项**:引擎修复不断
  落地,用户跑的 gallery 二进制可能落后几天;把 stale binary 的症状
  当活缺陷修会白走一趟(真实案例:用户截图刻度 `#0/#2/#4` + smooth
  狂野过冲,HEAD 探针里 y 轴声明域/平滑包络全是对的——min/max 接线
  是当天的提交;正确动作 = 先在 HEAD 复现,复现不了就重建 gallery
  (重建 gallery 的构建脚本)再要截图)。
- **自写曲线采样先验两件事:基和=1、t=0/1 精确落端点**。平滑折线的
  控制点是绝对贝塞尔控制点,必须配 Bernstein 基 `(1-t)³/3(1-t)²t/
  3(1-t)t²/t³`;把 Hermite 基的千分幂直接配 1e6 常数项(单位错乱)
  得 b0≈0.9995 恒成立——每段几十个采样全部坍缩在段起点,平滑曲线
  退化成首尾直联折线、末段"整根消失"(真实案例:七点平滑折线图,
  末点悬空三天定位不了,真凶只有 4 行基函数+2 行配对映射)。
  配对也别抄旧代码:`qx = b0·P0 + b1·C0 + b2·C1 + b3·P1`,旧 Hermite
  配对残留会让 t=1 落在控制点上而不是端点。
- **bbox-IoU 比较器对结构性缺陷全盲**:缺末段、多余竖网格线、整带
  错位 19px,IoU 照样 ≥0.5 PASS——"比较器全绿"不等于"图对"。数值
  oracle(SSR 出 SVG 提取包围盒/解剖)+ 逐图目检缺一不可,用户报
  "基本没对得上"时先目检再信指标(真实案例:折线族 36 图 27 PASS
  的同一天,用户点名的前两张图都是结构性错的)。
- **官方缺省值一律 SSR 实测,不凭文档或旧版记忆**:splitLine 缺省按
  **维度**(x 轴 show:false、y 轴 show:true,与轴类型无关),ECharts2
  "类目轴竖线默认开"的旧注释会误导出多余竖线;axisPointer.show 缺省
  是字符串 **'auto'**(声明了 value 就显示),按 bool false 处理会整根
  漏画静态指示线。SSR 一行 `getComponent(axis).get(key)` 拿到的就是
  合并后真值。
- **主题/CSS 调色板只填未声明的槽**:系列自带 color/itemStyle.color、
  根级 option.color(整表替换主题调色板)都必须跳过 chart::series-N
  的 CSS 覆写——常驻基线主题包一上,不带守卫的覆写通道会把自带
  调色板的图逐槽盖回默认色,而且渲染不报错、只有颜色对不上(真实
  案例:官方缺省主题包设为 App 常驻当天)。
- **值对系列([[ts,v],...])落在 points 而不是 data,分发判定要照顾**:
  "点系列→折线渲染器"的判定若排在堆叠判定之前,stack+time 图永远
  走不到堆叠面积渲染器,两条同名 stack 系列各自按原始值画(真实
  案例:移动端 tooltip 图粉带不堆叠,分发器里换两行顺序即修)。
- **图表组件每帧新建实例时,跨帧悬停状态必须放组件级按 wid 键的
  槽**:宿主的惯用法是每帧 `组件.Keyed(option, key).Render(...)`,
  实例随帧丢弃;上一帧命中/指针是否在图上这类状态放实例字段时,
  focus 渐隐读"上一帧命中"永远是空。实测的坑:无头探针复用同一
  实例渲染三帧全绿,活窗口光标钉在数据点上却死活不渐隐——两种
  用法行为分裂,探针测不出实例生命周期的坑。
- **GUI 重绘是按需的:hover 状态变化才调度,组件内部悬停要自己
  补帧**:框架的 mousemove 只在命中测试器 hover id 变化时置
  needsRedraw,而系列级命中不在命中测试器里——光标停在数据点上
  后没有任何下一帧,晚一帧的渐隐/悬停卡永远不渲染。实测:同一
  时刻列表行 CSS :hover 会亮(命中测试链活),图表拾取却"死";
  修法是指针事件派发在命中变化/进入/离开时补 `needsRedraw = true`。
- **悬停命中要对齐参考引擎的 linePrecision:折线拾取含线段距离,
  不只数据点半径**:官方 trigger:'item' 沿整条折线可悬停;只拿
  "最近数据点 < 抓取半径"判定时,线段中段悬停毫无反应(真实案例:
  凹凸图圆点悬停修完当天,用户指出"鼠标经过线就有效果,不只是
  圆点"——光标离最近点 300+px 仍应命中)。实现 = 指针到相邻两点
  连线(数据点弦)的距离平方也进最近候选,命中归属较近端点;另外
  启动参数路径会早于容器惰性初始化,替换缓存前先判空建表。
- **数据值直接进像素公式的通道必须过显式域映射或硬帽,大值域会画出
  天文数字图元刷满画布**:地图投影散点的旧启发式拿数据值直接乘系数
  当半径——人口量级(1e9)的数据算出上亿像素的圆,整张画布被刷成一
  个系列色。正确语义是 visualMap inRange.symbolSize [lo,hi] 按声明的
  min/max(未声明则数据域)线性映射符号直径;凡数据值驱动半径一律再
  过硬帽兜底(真实案例:全量截图审查的"空白"分诊里,两张满屏纯色
  图都是它,纯色恰是散点系列的调色板色)。
- **全量示例审查靠批量截图机器落 id,不靠人眼扫**:给示例程序加批量
  启动参数,逐示例解析+静态渲染一帧 WritePixels 落盘 + manifest
  (时延/异常按例捕获不中断),再用脚本解码成 png + 启发 FLAG(非
  背景占比/调色板命中/内容包围盒/边缘接触/轴线暗游程)。用户
  "有的缺轴/有的空白/有的刷色"式模糊指控,一晚上落成示例 id 清单。
  三类启发误报要先看图再信 FLAG:矩形树图/旭日图满幅裁边正当、
  极坐标类本无直角轴、浅色轴线过不了暗游程阈值。
- **SVG path 命令字母的大小写就是绝对/相对语义,解析不得归一化**:
  小写 l/c/s/q/a 是相对坐标,解析时把命令字母统一转大写
  (`cmd = toupper(ch)`),相对命令全部被当绝对执行,一条路径的后续
  点漂移成横跨全图的弦多边形,渲染与命中双双炸穿不报错(真实案例:
  SVG 底图示例悬停海面蒙上巨大半透明三角,插桩打环 bbox 前后对比
  一锤定音:漂移 bbox 跨全图 → 正确 bbox 几十像素)。命令只存原
  字母,绝对/相对在消费处按 `rel = ch >= 97` 推导。
- **SVG 底图的交互区域只来自命名形状,fill="none" 线稿不可区域命中**:
  参考引擎语义是 SVG 原样渲染(光栅/矢量皆可)、区域只叠加交互,
  区域名优先级 形状 name > data-name > 祖先 `<g name>` > id——组名
  要向子形状传播(航班选座 SVG 的座位名全在 `<g name>` 上,漏了
  区域收 0 个、整图连底图都不画)。世界地图类素材几千个形状仅个别
  命名且多是 stroke-only 航线:未命名形状(海面/装饰)跳过不命中,
  `fill:none/transparent`、`fill-opacity<=0`、`style fill:` 的线稿
  只画不做实心区域命中——否则悬停海面误触航线环的 emphasis 填充,
  整图蒙色(真实案例:"鼠标经过对 SVG 的干扰")。零命名区域的纯
  装饰素材也要照画光栅底图,早退分支先判 SVG 模式。
- **同一份数据不许两条解析车道齐收**:地理系列的 [经度,纬度,值] 数值
  对被通用数值对车道(千分定点)与地理专用车道(经纬度定点、y 取反
  纬度向上)各收一遍,同一系列两套点一半屏外一半屏内,悬停命中框与
  可见点错位(真实案例:SVG 底图上的特效散点六个点消失/错位,根因
  是解析分支没先判坐标系声明)。通用数值对车道必须先排除地理坐标系;
  同理 SVG 底图(用户单位)与矢量地图(经纬度定点)两套坐标尺度的
  投影必须分开,封装成带单位旗的单一投影函数,禁止在调用处散落换算。
- **同一配置键两条解析道先后各收一遍时,后跑的"整表重建"会覆盖先
  跑的正确结果**:关系图系列的连线先被通用道收好(数字端点=节点
  序号),专用道随后按「节点名表」重建整个列表,而新形态系列的节点
  没有名字、名表全退化成值串,数字端点全被改写成同一个名字——边
  全并到一对节点上,图"渲染成功"但连线全错(真实案例:矩阵坐标系
  上的关系图,七条链接探针实锤端点全变 '1'→'1')。给通用解析道补
  新形态时,必须查专用道是否会用自己那份重建覆盖;修法是让专用道
  在新型坐标系下跳过重建,保留通用道的原始端点,渲染端按「纯数字=
  序号,否则名字」解析。渲染还错时先探针打印解析产物再怀疑渲染
  端——产物在渲染前就被换掉是这类坑的共同形态。
- **复用整幅渲染器画"子面板"时,先过查三道隐形壳带与动画门**:
  把渲染器当成子矩形里的黑盒重入,是复合布局(多网格面板/矩阵锚格/
  迷你图宫格)的通用打法,但有四个坑都以「渲染了但看不见」出现
  (真实案例:矩阵格里嵌 40 根迷你柱 + 10 张小地图,全灭,连修四
  轮):①主题 padding 在整幅上是留白,在 ~40px 迷你格里上下各吃一
  行后绘图区算成负值/几像素,整格内容静默早退或退化成细条——迷你
  面板按格高收缩 pad;②轴标签带/图例带等 gutter 不看 show 声明照
  占位——gutter 跟随标签显隐声明;③渲染器把「正式填色」整个包在
  「终帧判定」分支里,动画中间帧只填底——复用时为防快照互踩传了
  动画帧值,结果区域全无;无入场动画的子面板必须传终帧值;④帧缓
  存槽按控件全局编址,同帧多实例要么各给独立指纹(随矩形互异),
  要么干脆绕过缓存。
- **给配置结构加字段,深拷贝/过河路径必须同提交逐条过查**:源配置
  探针全绿、渲染端字段全默认——渲染前那份深拷贝没把新字段带上
  (真实案例:地理组件的「锚定到格」字段漏拷,锚格全灭,而按引用
  过河的标题/网格全正常)。验证「解析成功」必须走到渲染端可见为止;
  新字段过河要么进深拷贝,要么改成按引用共享并写明「只读共享」
  契约。
- **像素验收必须用刚刷新的转换产物**:离屏批量渲染落盘后还有一步
  转换成查看格式,忘了跑就拿着旧图判断——「修好了但看着还空」白
  绕四轮构建。看完渲染先问自己:这张图是这一版二进制画的吗?
- **接参考引擎的系列先读它的 defaultOption,缺省坐标系和遗留键
  别名都是引擎缺口**:路线/迁徙类系列的坐标系缺省即地理坐标——
  这类示例大都不写坐标系声明,只认显式声明的解析器让整条车道
  静默不画(真实案例:迁徙航线图整组航线消失);补缺省要带条件
  (配置里真有地理组件),无地图的兜底车道不能陪葬。同理旧版本
  遗留键:地理组件的区域填充色有新旧两个键名同义,官方示例写的是
  旧键,只认新键会把深色大陆渲染成默认浅蓝。对拍前先拿官方配置
  原文(含遗留键)过一遍解析。
- **同色折线大批量必须走批量入口,且批量入口要链接期验真**:逐条
  折线是每条一次覆盖缓冲清+合成+一次跨界调用,几万条航线秒级卡死;
  批量=扁平交错顶点数组+每路径顶点数,每帧每色一次,GPU 后端整个
  批次共享一次清+采样+合成(真实案例:World Flights 3.2 万条航线
  接上解析后秒卡,批量后单帧 ~0.7s)。坑:批量 API 可能只有头文件
  槽位和托管侧 extern,原生实现从未接线——extern 悬空到链接期才爆
  undefined symbol,首次使用先 grep 原生导出是否存在。参考引擎的
  large 模式(声明 large 且条数超阈值)跳过事件层,逐线命中要做
  同等豁免,否则每帧白付数万次线段距离。整图容器的底色声明属于
  整个绘图区,地图车道不能再用主题底色把它盖回去。

- **参考图型的数据项可以是"值向量"，标量车道会静默取 0**:雷达图每
  个 data 项是一条独立多边形(对象 {value:[每轴值],name} 或裸数组)
  ——拿标量读取去读数组得 0,整图塌成圆心一个点,而解析/渲染全绿
  不报错。接入新图型先看官方数据形态;多边形语义的值数组在解析期
  物化成"每项一个系列"(图例取数据项名,未命名继承系列名——"同名系列
  随主项一起开关"的图例语义正好成立),值用千分定点、显式上限同步
  放大(渲染式里 val×g×r/(max×1000) 的 g≈1000 会约掉),tooltip 显示
  走定点格式化除回,0.46 这类小数才不丢(真实案例:雷达主力 demo 双
  多边形消失+轴标签错拿图例文案+31 天星爆图全炸,一批修)。
- **组件级 option(只有坐标系没有系列)官方画骨架不画 no data**:
  分发链"0 系列→空状态"之前先认坐标系组件——渲染器的骨架路径
  0 系列本就安全,白白被空状态短路(真实案例:纯雷达组件示例落
  "(no data)",官方画五轴网格+指示器名)。
- **渲染形态不对,先 dump 中间数据的对应键再定责**:官方示例的
  组件数组会在中间 json 里完整保留(多雷达示例的 center/radius/
  指示器文本一直在案),缺的可能是引擎解析入口(数组形态没解析,
  只认对象形态)——不 dump 就动手,会白查一轮数据侧。口诀:中间
  数据有=引擎缺,没有=物化缺;同语义的新旧两代组件字段往往同构,
  消费端齐全时补的只是解析(真实案例:三雷达全画一个圈,以为是
  多坐标系圆心语义没实现)。
- **逐系列物化块里的全局字段只准改一次**:物化循环逐系列进,里面
  的全局放大/归一会连乘(真实案例:3 个系列把满刻度 ×1000 连乘
  三次成 1e8,单系列场景永远不暴露)——用已置位的旗守卫"只做
  一次",旗天然标记"首轮已做"。
- **热循环先审被调函数的收敛成本,再怀疑容器**:力导向 30 轮 × nn²
  的内循环 8.3s/帧,把对象 List 访问换平铺表毫无变化——真凶是被调的
  整型开方:牛顿迭代初值取 r=v 本身,要 ~17 次带 long 除法的对折才到
  √v,每对 ~1μs。修法 = Math.Sqrt(硬件 sqrt)+平方域剪枝(d²≥k² 直接
  跳,与 d<k 分支语义等价)+原生 int[]/CSR 邻接——8111→78ms(真实案例:
  graph-webkit-dep nn=513 首帧卡 8s)。插桩标记必须放进嫌疑函数内部
  (入口/出参两侧),只框调用侧会把热点错钉到别的段,白改一轮。
- **重活下放后台的现成定式 = Thread.Start + app.Post(捕获闭包)**:
  worker 只做纯计算(不触 UI 状态),成品经 `app.Post( => {收货})`
  整只移交——App.Post 自带 window.Wake(唤醒阻塞中的 WaitEvent),
  DrainPosts 执行后自动置 needsRedraw,不用自己搭互斥队列;UI 侧用
  pending 槽画「加载中…」占位(范例 ImageHttp.zan)。--nomouse/
  回放/审查这类"单帧截图必须终态"的路径保持同步装载。
- **插桩轮次先核产物 mtime 再信输出**:一次编译失败(比如往表达式
  续行中间插了语句)会让 sweep 跑旧 exe,新加的打印零输出,诊断被旧
  二进制牵着走;跑前核对 build 产物 mtime 晚于最后一次源码编辑。
    python 批量插桩只准插在语句起始行前(上一非空行以 `;` `{` `}`
    结尾),`+ Call(...)` 续行中间插语句 = 编译失败。
- **PropertyGrid 嵌入控件高度严禁脱节，必须显式传递行槽位高度**:
  属性表中嵌入 SelectBox/LineEdit 等复合控件时，必须传递显式槽位高度 `eh`
  （如 `r.pick.RenderIn(app, ex, ey, ew, eh)`），严禁使用缺省高度的无参 `Render`
  导致控件回退到默认大尺寸（32px/36px）撑破 24px 行槽位覆盖下一行或被裁剪截断。
- **长属性检查器必须具备分组折叠能力（Collapsible Sections）**:
  属性项动辄几十项时必须分组并支持折叠展开，大类标题（`kind == 5`）展示 `▶` / `▼`
  指示状态，`ContentHeight` 与逐行绘制均需联动跳过折叠项，并在标题栏使用 `Ui.ClickedIn`
  阻断点击穿透，保证滚动条量程与视野紧凑清晰。
- **HTML+CSS+Zan 设计稿零胶水规范与纯矢量图标体系（从截图还原精美客户端铁律）**:
- **HTML + CSS + Zan 的装配边界**：窗口子类使用同名文档与 `Show()`；
  页面使用 `Gui.Html.Load(app, html, handlers, baseDir)` 返回树，再由宿主挂载。
  HTML 只在运行期解析；发布用 `--embed` 携带设计稿，业务代码管理状态与事件。
  文档文本、图标与容器的映射以当前解析器和控件属性协议为准。
  - **无边框窗口右上角系统控制按钮保护区（SetCaptionZone）**:声明 `data-win-chrome="false"` 沉浸式无边框窗口时，若标题栏高度生效，Win32 的 `WM_NCHITTEST` 会将整个窗口顶栏判定为 `HTCAPTION (2)` 导致拖拽吞掉右上角按钮点击；必须在 `OnLoad` 中调用 `form.GetApp().SetCaptionZone(width)`（如 `form.GetApp().Scale(160)`）为右上角功能区（菜单/最小化/最大化/关闭）保留非拖拽客户区。
  - **Tabler SVG 纯矢量图标铁律(杜绝伪 Unicode 字符)**:按钮与图标控件一律声明标准 SVG 矢量名(如 `icon="player-play"`、`icon="player-skip-back"`、`icon="player-skip-forward"`、`icon="heart"`、`icon="volume"`、`icon="reload"`、`icon="playlist"`、`icon="chevron-down"` 等),走内置 997+ Tabler SVG 矢量图标集经 nanosvg 矢量光栅化与 GPU 贴图缓存。严禁使用 `▶`、`⏮`、`❤` 等 Unicode 假字符冒充图标。
  - **Nanosvg 矢量图元约束与动态透明 SVG 规范**:
    1. 颜色 Hex 解析：nanosvg 只接受 3 位或 6 位 Hex（`#RGB` / `#RRGGBB`），不支持 8 位 Hex（`#AARRGGBB` 会解析失败并静默回落为灰色 `#808080`）；透明度必须使用标准 XML 属性：`opacity="0.5"`、`fill-opacity="0.3"` 或 `stroke-opacity="0.8"`。
    2. 几何图元限制：nanosvg 不解析 `<text>` 标签，动态矢量芯片、HUD 刻度等指示图形必须由 `<path>`、`<polygon>`、`<line>`、`<circle>`、`<rect>` 等几何图元绘制。
    3. 动态透明 SVG 承载：通过 `Image.Src.Set("data:image/svg+xml;utf8," + svg)` 动态帧刷新时，图片控件必须在 CSS 中显式声明 `background: transparent; border: none;`，消除基础皮肤默认占位灰底。
  - **高性能帧循环动画（FrameHook）**:复杂 2.5D/动态视觉（如雷达扫描、呼吸光晕、状态气泡）通过 `Form.FrameHook(OnFrameAnimation)` 注册帧回调，利用 `Stopwatch.GetMicroseconds() / 1000` 实施帧率节流（如 90ms / ~11 FPS）；每帧计算离散步进增量构造动态 SVG 并更新，调用 `form.GetApp().RequestRedraw()` 实现丝滑低占用原生动画。
  - **HTML 文本与布局容器语义映射**:原生 `div` 标签映射为纯无格式布局容器(`Panel`,不绘制自身文本)。段落与标题必须使用语义标签 `<p>` 或 `<h1>`~`<h6>`(映射为 `Typography` 排版控件),行内文案使用 `<label>` 或 `<span>`(映射为 `Label` 控件)。当 `div` 元素承载动态单行文本胶囊时，声明 `display: block; white-space: nowrap;` 触发 `ArrangeFlow` 块流完成排版绘制。容器排版纵向排列时必须显式声明 `display: flex; flex-direction: column;` 避免子元素横向挤占。
- **分段单选按钮（Segmented Buttons）文字安全内边距阈值**:
  判定是否由下拉框升格为单行分段按钮时，文字宽度测量 `tw` 必须预留至少 `app.Scale(16)`
  的安全 padding（左右各 8px），否则在高 DPI 或多字宽下极易贴边或溢出截断；放不下时应
  平滑自动回退为下拉单选框。
- **设计器画布双击与代码跳转防误触机制**:
  画布组件双击默认优先进入原地行内文案编辑（`StartInlineRename`，按钮/标签等全量包含），
  严禁将普通双击硬编码为跳转代码；只有在显式按下 `Ctrl+双击` 或属性面板点击“定位代码”
  时才触发 `RequestOpenCodeBehindHandler`，杜绝用户点选/拖拽组件时误跳后置代码打断思路。
- **组件大纲树与画布双向精准联动与深层路径自动展开**:
  画布点选任意组件时必须同步通知大纲树（`SyncOutlineSelection`）；若选中的组件深层嵌套
  在容器/折叠目录内，必须顺沿向上将其所有祖先节点全部自动展开（`expanded = true`），
  确保选中项在树视图中立即可见且处于焦点，消除深层组件在树中“失踪”的交互断层。
- **配置里的颜色声明走全链才生效**:图例文字色、轴指示器名色这类
  字段要过五处:模型字段+构造/深拷贝+帧内快照拷贝+渲染器传参/
  消费端。漏最后一级(渲染器读主题色没读声明)时解析全绿、渲染
  死灰(真实案例:深底示例的图例白字画成主题灰字,金轴名根本没接)。
- **组件级 dispatch 通道(整块坐标系/骨架类图表)必须随帧内快照
  拷贝过河,新组件入库先核对快照拷贝清单**:渲染分派读的是解析
  产物(帧内快照)而非源配置,它逐字段手工过河——漏拷组件 = 按
  组件存在性分派的判定在渲染形态上永不命中,系列级残留(数据已
  解析进点/下标)会被别的车道捡走画出"错而不空"的图,比空板更
  难发现(真实案例:矩阵坐标系首版被热力图车道抢走填色、层级
  头带全丢;日历组件漏拷后整族条纹化)。定位法:对截图量剖面
  反推几何——填色起点/带宽与布局预期对不上,一眼锁定"画图的
  是另一条车道"。修法 = 快照拷贝处补一行组件传递(解析后不可变
  的规格只读共享即可;可变组件走深拷贝)。
- **相邻填充区域的接缝要按参考引擎的"区域描边"语义封,不能用
  同色双描 workaround**:参考引擎每个区域 path 是 fill+stroke
  同帧落盘(缺省半像素中性灰描边),相邻区域共享边各描一次天然
  密封;只画填充时共享边单像素 AA 合成后透背景成"虫洞",画一遍
  自己填充色压不住。先 SSR/dump 实证缺省描边的色与宽再落地:
  描边车道独立于通用样式结构(显式 0=关描边是合法值,通用结构
  缺省 0 分不清"没声明");渲染时描边必须压在所有填充之后画——
  同 path 先 fill 后 stroke,提前画会被后画的相邻填充盖掉
  (真实案例:地图相邻州界放大透背景缝,首版描边画在填充循环前
  全部消失)。区域轮廓带宽描边顺手把闭多边形轮廓助手补上宽度
  参数(底层折线接口对亚像素宽会钳到 1px)。
- **接缝修了描边还在,真根因多半在数据:逐要素独立简化的多边形
  集合,共享边必然分叉成楔**:多边形数据若逐区域独立做抽稀/量化
  (如 Douglas-Peucker + 坐标取整),相邻区域对同一段共享边界各自
  走样,锚点之间路径分叉,放大后成楔形透底——描边 1px 盖不住
  几像素宽的透镜(真实案例:地图数据逐省独立抽稀,某两省一段
  共享边中点偏差 15 个最小坐标单位,深底下放大透底;修了描边后
  用户实机仍报)。先对拍相邻环:精确共享顶点/边一档、近邻顶点
  (量化噪声)一档;修法在数据侧——分叉弦换成邻省同锚点的细分链
  (选链以弦偏差为准,链提取沿环 walk 且支持 wrap,否则会拿到绕
  整环的大弯链),量化漂移的近邻锚点用并查集聚类后取中位数对齐。
  验证用像素十字邻居探测(内部透底=真缝)与外侧轮廓/文字 AA 分辨
  开,余量要与基线持平才算非回归。
- **即时模式渲染器"每帧全量重画"是大网格的隐形杀手,离屏单帧
  测不出无响应**:实时路径每帧都走完整渲染器,20301 格热力图首版
  逐帧重画格子 + 4 万列表分配 + 数百次文本测宽 + 双份全量扫描,
  实机秒级无响应;而离屏审查单帧(36ms)全绿看不出问题。帧内快照
  缓存是现成定式:静态内容按指纹 SnapshotRect/RestoreRect,指纹
  必须覆盖**全部取像素输入**(几何/缩放/逐格数据/系列开关位/色带
  与值域声明/主题色,漏一路就是主题切换后出僵尸像素);命中帧只付
  一趟 O(n) 指纹 fold。悬停覆盖层与核心层**共用的几何推导**必须
  缓存或早退——指针不在本图面板直接 return,几何用"option 引用+
  矩形+缩放"为键的备忘复用,否则缓存了像素缓存不了卡顿(每帧照样
  重扫/重测一遍)。验收用帧耗时插桩量稳态帧:稳态帧低于"重画一遍
  的单项成本"即命中实锤(真实案例:20301 格热力图用户实机"一次性
  同步加载无响应",离屏审查全绿,稳态帧 36ms→7-15ms 定谳)。
- **同一数据形态在不同坐标系下语义不同,分流护栏是坐标系统计
  落字**:热力图的 [x,y,v] 裸三元组在直角系是(类目下标,类目下标,
  值),在地图系是(经度,纬度,值),矩阵系首两位还是类目名字符串
  ——新车道按"类型+数据形态"拦截之前,先把 option 里声明的
  coordinateSystem 落进系列的坐标系统计字段再判(没落字的坐标系
  值是空串,会被直角车道误吞,把经纬度当类目下标)。附带语义:
  类目 y 轴声明序自上而下(行 0 在顶),类目 x 轴左→右;分片
  (piecewise)上色 = 连续色带的桶中点量化。

## 动画与保留树缓存

- 测量与绘制必须以同一当前控件和祖先链解析 CSS。无关系选择器时不拼祖先
  签名；有关系选择器时两个阶段都建立 styleCtx 并在 finally 恢复。为什么：
  上下文键不一致曾让 201 个静态节点在 120 帧中重复解析 48,240 次，后代
  选择器尺寸在测量时也不正确。缓存仍须按 DPI、主题和结构变化失效，不能
  共享可变 StyleBox；验证结构重挂、异常恢复和 Clone 独立性。
- 连续视觉动画使用自适应 RequestAnimationFrame/In 重载，显式毫秒间隔用于
  定时内容。相位必须按真实时间连续计算，并用实际像素变化断言验证；只把
  请求改快，90ms 整步的加载圆点仍会一格格跳。性能统计分开冷启动与预热后
  的 render/present，并按目标刷新率预算检查尾延迟。
- 只绘制、不占流式尺寸的叠层仍须保存真实绘制几何，动画损伤覆盖旧/新像素。
  为什么：零测量高度的精灵叠层在姿态命令哈希变化时报告高度 0 的损伤，
  把局部动画交替升级成整窗帧。修复实际 bx/by/bw/bh，并用真实 partial 比例
  及整帧像素对照验证；不能关闭 paint hash 来掩盖损伤错误。

- 页面动画与背景特效同一显示拍先合成再提交一次，局部页帧仍推进条带外
  特效，并合并旧/新特效损伤。为什么：FxPresent 先提交、PresentFrame 再提交
  曾在同拍执行两次原生呈现。宽条带也须更新干净快照；用真实提交日志和
  整帧像素参考验证，否则下一特效拍会恢复旧页面。任何仍标记有效的干净
  快照都须补齐重画条带；复制面积阈值只能触发快照失效，不能跳过补丁仍
  宣称有效。

## 缩放纪律(DPI:为什么界面忽大忽小)

框架样式路径负责自动缩放一次:主题 token (字号/高度/间距)由
`App.ScaleThemeMetrics` 按"基线×密度档×DPI"重算;CSS 字面长度在共享
解析层换算。来源必须随每个标量及级联胜出值保留,不能凭数值等于主题值
猜测已经缩放。跨 DPI 异常也可能是框架缺陷:曾复现绝对行高、字距、边框
漏乘 DPI,设计器与运行时共用错误仍能像素相等。验收要同时断言逻辑长度
的预期换算和同目标 DPI 的布局/像素,发现缺陷修共享层,不在应用里补乘。
`Canvas.DrawText` 的 fontSize、手算的坐标间距不经过样式自动缩放。

- **主题重算后重建已安装的应用 CSS**:主题 token 展开成物理值后会留在样式表里;
  原生窗口显示时才获知 DPI,若只缩放 theme,会出现按钮尺寸正确而文字仍为旧字号。
  验证须先安装 CSS,再改变 DPI/密度,同时断言全局、部件及控件缓存的结果。
- **响应式视口取实际逻辑客户区**:原生窗口的媒体查询及 vw/vh 随客户区变化,
  离屏设计预览使用声明的逻辑尺寸。换视口还须作废控件缓存,包括没有 @media 的
  vw/vh 样式;固定读初始设计尺寸曾使手机断点失效,只改全局键又会留下旧布局。
- **每个独立属性独立保留来源**:width/min-width、四侧 padding/margin、
  gap/row-gap/column-gap 不共用缩放位;shorthand 与 calc 按分量保留来源,
  `!important` 的值与来源同取胜出声明。共用来源曾使未缩放长度被误当物理值。
- **内联 style 保留原文,在解析上下文中重放**:不要同时烤进控件标量;
  替换/清空样式后旧字号、边框与背景曾因此残留。用实际
  `SetProp("style", ...)`→`ResolveStyle`→量测路径验证初次、替换、清空。
- **Clone 复制单位环境,文本 fallback 与 rem 基准分别保存**:补拷贝单位
  环境曾把历史 16px 默认文字悄悄换成主题 14px,改变未声明字号的布局;
  单位求值应读根字号,文字 fallback 保持原契约,各自只缩放一次。

从截图还原界面时的倍数判定是另一类坑:先按 `references/screenshot-restore.md`
1.5 节"三票定倍数"判出原图 DPI 档,换算只在布局账本里发生一次;把截图
物理像素直接抄进代码是双重缩放的头号来源。

硬规则只有一条:**每个尺寸值必须明确属于下面两条路径之一,全项目不得第三种**:

1. **样式路径(自动缩放,禁止再乘)**:CSS 声明、控件属性、`FontOr/Width`
   等 StyleBox 取值——写 token/档位值即可,框架已缩放,手再乘一遍就是
   "150% 显示下按钮大 1.5 倍"的双重缩放。
2. **控件内部绘制/游戏世界路径(手动缩放,禁止忘记)**:`Canvas.DrawText` 字号、内部绘制的
   坐标与边距——必须过 `app.Scale`;且**封装成项目内单一 helper**(如
   `Ui.Dp`),禁止在使用处散落内联 `* dpiScale / 100`。

自绘文字字号不许裸写数字:取主题字号阶梯(`Style.FontFallback(app,
"medium")` 等)或经 helper 缩放的档位值。审计手段:grep 直写字号数字
(如 `DrawText(..., 20)` 这类)、内联 `dpiScale`/`Scale(` 乘法、裸
`0xAARRGGBB`——每处命中都是"忽大忽小"的候选。

悬停/点击命中是缩放纪律的第三条战线:**鼠标坐标与画布坐标必须同一
空间,且 DPI 缩放全链只准发生一次**。150% DPI 下窗口客户区是逻辑
尺寸 ×1.5 的物理像素,画布 1:1 画在物理像素上,应用层鼠标坐标也是
物理客户像素——三者本就同空间(进程内探针实证:鼠标坐标与
GetCursorPos+ScreenToClient 的物理落点逐像素相等,实机悬停卡、轴
指针全钉在光标处)。**遇到"悬停/命中整体偏移 ×DPI"的疑云,先怀疑
测量工具**:DPI 不感知进程(默认的 PowerShell/脚本宿主)里
SetCursorPos/GetCursorPos/GetClientRect 被 OS 按 DPI 虚拟化——拿
它摆光标、算窗口原点,会伪造出恰 ×1.5 的"错位",引擎如实上报却被
记成缺陷(真实案例:"全图表车道悬停右下偏 1.5 倍"挂账一轮,进程内
对照定谳为工具伪影)。外部光标自动化必须先声明 DPI 感知;鼠标
空间疑云的判据 = 探针进程内直接调用 GetCursorPos + ScreenToClient
与应用鼠标坐标同帧对照,不用注入式 SetCursorPos 当证据。

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

## 收尾自查(逐条过)

1. 字号只来自阶梯(含 Tailwind `text-*` 档),没有即兴值。
2. 同排/同组控件高度同档;按钮不再三种高度并存。
3. 一切间距 ∈ 4 的倍数;gap 用档位类;区块间隙全画面一致。
4. 相邻区块左边界共线;表单列宽全表统一。
5. 数字列右对齐;操作按钮集中在右侧;主按钮只有一个且最右。
6. 颜色只来自 token/语义类,没有调色板色/裸色值/直读主题字段。
7. 中性色三档承担全部次级信息,没用加粗/彩色冒充层级。
8. 圆角、阴影、图标尺寸全画面同档。
9. 至多一个签名元素 + 至多一处氛围动效;风格方向有明确出处(配方卡)。
10. 空态/加载/错误都有下文(Empty/Spin/具体错误文案)。
11. 文案:动词具体、全程同名、句式一致,气质匹配所选风格卡。
12. 通用 GUI 换 dark/light 两个皮肤各看一眼,没有写死的颜色残留；图形游戏
    逐页验收游戏专属皮肤及适用交互状态，与场景美术一致，无默认桌面样式回落。
13. 斜线/曲线/圆角放大看无硬跳阶梯与浅色月牙;数据边走了定点亚像素原语,
    没有整数 1px 条拼接或逐段 DrawLine。
14. 每个尺寸值出处明确:样式值走 token 没被手动乘过缩放;自绘值全走
    项目单一缩放 helper,没有内联 dpiScale 乘法与裸字号。
15. Gui 面板与自绘 HUD 混排的画面,两边的字号/间距同源(同一 theme 或
    同一 helper),肉眼没有"一边大一边小"。
16. 骨架是停靠、内容面是流式/弹性容器;表单/工具窗口里没有手摆 mx/my。
17. `ZAN_GUI_OVERLAP=1` 跑过一遍,`gui-overlap` 命中为 0(免检牌只给
    刻意叠放的装饰)。
18. `ZAN_GUI_LAYOUTLINT=1` 跑过一遍,`gui-lint` 命中为 0;按钮/勾选框
    代码里没有 `Prefer` 写死的宽高(免检牌只给图标钮/画布图元)。

## 浮层与模态:下层内容必须停手(弹层点击穿透的根因)

- **「画出来」不等于「拦得住」**:保留模式(声明式)的遮罩——`Modal`/`Drawer`
  的半透明变暗层、设计器的速查 HUD 全屏遮罩——只调 `canvas.FillRect`,**没有
  任何命中登记**,于是遮罩上的点击原样落到下层按钮上(打开一个 Modal,顺手把
  背景页面的「删除」也点了)。自绘遮罩必须**同时** `app.BlockHitsRect(...)`:
  遮罩本体、弹窗内容、浮层自身的小控件,三者都要占住各自那块。
  坑出处:核心层有 `pressOnBlocker`/`ClickAvailable`/`BlockerAt` 一整套防线,
  组件却以为「铺了遮罩就等于挡住了」,于是整类保留模式浮层全无防护。
- **`pressOnBlocker` 的判据要问「命中的是不是遮罩」,不是「有没有命中」**:
  `AnchorAt` 解析上一帧快照时**不跳过** `wtype==-1` 的遮罩(只有右键的
  `below=true` 才跳),所以按在遮罩上时 `hitId` 就是遮罩自己的 id(>=0),
  旧的 `hitId < 0 && BlockerAt(...)` **恒为假**——这道防线形同虚设。
  正确判据:`hitId >= 0 ? hitTester.AnchorTypeOf(0) == -1 : hitTester.BlockerAt(...)`。
- **释放抑制不能只写 `clickTargetId`**:那个字段运行时**没有任何读取点**
  (只有测试读),而 `Ui.Clicked` 读的是**锚点**(`ClickTarget()`)。只写
  `clickTargetId = -1` 拦不住任何点击;`Ui.Clicked` 必须自己认这道闸
  (`if (app.PressOnBlocker()) { return false; }`)。离屏路径早就同时清了锚点,
  窗口路径没清——两套语义分叉的坑。
- **浮层几何有多块,命中判定就得覆盖全部**:补全弹窗 = 候选列表 + 种类过滤条
  + 右侧说明面板;签名帮助 = 气泡本体 + 两个重载箭头。只登记主矩形的话,点
  说明面板/气泡正文会穿透到文本区(光标跳行 + 浮层被顺带关掉)。
  **几何必须落库成字段,且不画的时候要清零**——否则上一帧的矩形会继续被
  当成浮层区域(该落回文本区的点击被吞掉)。
- **「按外部关闭」的浮层要在按下帧就消费这次手势**:浮层在按下帧关掉自己,
  遮罩随之不在该帧的命中快照里,同一次手势的释放会解析成下层控件的点击。
  存量代码的做法是 `app.ClaimClick()`(认领)+ 组件级提前返回。即时模式输入仲裁
  (ClaimClick / BlockHitsRect / BlockHits* 等)正在逐步下线:新代码优先复用
  `OverlayPopup.Host`、`Layer` 等共享浮层件,不要再新增仲裁调用。本节以下几条
  描述的是现行仲裁机制,供维护存量代码。
- **遮罩只挡自身矩形 = 点外面也穿透**:下拉/菜单类的浮层若只
  `BlockHitsRect(自身矩形)`,用户在浮层**外面**点一下时,这次点击照样落回
  下层内容(菜单关了,画布还顺手改了选中/起了拖拽)。按语义升级:
  菜单 → `app.BlockHitsMenu()`(全窗 + 右键仍解析到下方,Windows 语义:
  别处右键把菜单换个地方重开);模态 → `app.BlockHitsBelow()`(全窗)。
  只盖自己的浮层(说明面板、气泡、popconfirm)要自己另设组件级守卫。
- **自绘的模态菜单要自己铺全窗拦截区,而且必须画在帧末**:命中区按绘制
  次序注册、**后注册者优先**,浮层只有画在帧末(宿主在帧末调
  `RenderCtxMenu`)它的全窗拦截区才是最顶层命中区,才真挡得住同窗别的
  控件;画在帧中的话,比它晚画的控件照样接到这次点击。拦截区只吞点击、
  不画像素,必须登记为 hit-only(`app.hitOnlyBlockers.Add(id)`),否则
  原生浮层(WebView)会被它整片裁掉。代价:菜单打开期间同窗指针交互被
  冻结(模态语义),右键换位置重开的行为让位给模态。
  坑出处:代码编辑器的右键菜单只注册了菜单自身的矩形,菜单外那一下点击
  既关了菜单又点到了下层按钮。
- **浮层拦截区会吃掉键盘焦点,关闭时要把焦点还给宿主控件**:按下落在
  拦截区上时核心把焦点写到了拦截区的 id(`focus.SetFocused(hitId)`),
  宿主控件就此失焦——菜单关掉后编辑器不再接收按键。浮层关闭时要把焦点
  还回去(`app.focus.SetFocused(宿主控件的 id)`)。
- **浮层的命中遮罩拦不住同帧下层的几何命中**:遮罩(`BlockHitsRect`)
  是在**覆盖层阶段**才注册的,而下层内容(表头 chrome、表格体、芯片条、
  自绘列表)在**内容阶段**就把原始事件处理完了——同一帧里遮罩还不存在,
  下层按几何算出的命中照样成立。判据:**凡是"读原始事件 + 自己算命中"
  的表面,浮层打开期间都得停手**;靠遮罩挡是挡不住的。
- **做法是组件级的模态事件清单**:浮层打开时把内容阶段的事件种类就地
  清零(`DataTable.ModalEvt` 是现成范例:右键菜单 / 表头菜单 / 列选择器 /
  条件格式清除 / 编辑器弹窗 / 列筛选弹窗 / 图表浮层 / 导出进度弹层都在
  清单里),浮层自身的内容在覆盖层阶段绘制、仍读原始事件,不受抑制影响。
  **新加浮层就要往清单里加**,否则弹层上的点击会落回下层:勾选框被点掉、
  选区被改写,用户看到的就是"弹层鼠标穿透到表格"。
- **浮层自己的开关要单独放行**:表头筛选钮是筛选弹窗的开关(再点一次
  关闭、点另一列切过去),它会跟着模态抑制一起失效——这一处用原始事件
  种类单独走。
- 坑出处:同一组件里不同浮层的待遇不一致才是这类 bug 反复出现的原因——
  行上下文菜单早就在模态清单里(所以右键菜单没这个毛病),而列筛选弹窗、
  图表浮层、导出进度弹层都曾漏在外面,用户实测"弹层鼠标穿透到表格"。
- 回归锁定:DataTable 的弹层穿透 conformance 用例(筛选弹窗内/外点击、
  筛选钮开关、图表与导出浮层)。

## 验证

- **延迟生命周期测试须用 `Post` 排队**：`TriggerEvent` 同步执行，验证 owner 释放后的回调应使用 `header.On.Click.Post()`；collapse 修正该测试后实际 17 项断言及正式单 case 重跑通过（50.41 秒），golden 与产品代码未为失败修改。
- **关闭窗口先断开持有回调，清理按 owner 身份限定**：窗口关闭须清掉持有回调以破除 owner 自环；根树与 UiDriver 清理按 owner 身份匹配，不能误清其他窗口。

- **真实离屏输入按当前 Window.EventSeq 去重与消费**：事件序号取当前窗口的
  `app.window.EventSeq()`，App 暂存、控件去重及输入消费标记必须使用同一窗口局部
  序号；不能读取平台 global 序号，否则离屏注入或其他窗口的事件会让各阶段
  对不上，造成重复处理或吞掉后续输入。验证须走实际离屏窗口事件入口：同一
  事件跨多次渲染只生效一次，新序号的后续字符、按键仍正常生效；多窗口的
  事件不能相互消费。仅直接调用 Change/Click 回调不能覆盖此契约。

- **游戏 HUD 的绘制与输入必须接同一棵树**：宿主须通过 `app.SetRootControl(hud)` 绑定 Visual Tree 输入根，`RenderTree` 自身不接输入。按下命中 Button 的子 Label、由祖先 Button 捕获时，Click 要求按下目标与实际释放命中都属于捕获祖先的子树；捕获仅保证 Up 投递，不能把拖到外部释放判成点击。`OnClick` 行为钩子执行后由 `TriggerEvent` 统一通知一次，钩子里不再重复通知订阅者。已由 `guihost_loop_test` 独立探针验证通过（`GUIHOST_LOOP_OK`），覆盖原四次点击及 disabled、拖外释放、默认/Float 单次通知回归。

- 依赖 Console 输出或 stdout golden 的 GUI 回归编译时显式加 `--subsystem console`；自动选择 Windows 子系统可能正常退出却没有 stdout，本轮样式探针已证。

- **运行期守卫的错误只进 stderr，进程照样退出 0**：除零这类软错误由运行期
  守卫打印到 stderr（并追加崩溃日志），**退出码仍是 0**——用例"绿"了，错误
  却已经发生（设计器宿主模式窄区除零就是这么溜过去的：IDE 里只留下一行
  stderr，画布照旧刷新，看着像没事）。盯这类错误要用独立门禁脚本把 stderr
  合进输出一并判，别只比 stdout。
- **设计器宿主模式（宿主把组件面板/属性面板挂进自己的 dock）下两栏宽度都
  归零**：三栏按比例压缩那句 `side * palW / sum` 的分母就是 0，编辑区一窄
  就除零。宿主模式要**整段跳过**压缩（画布照可用宽度铺满），不能靠"分母
  恒为正"的巧合——非宿主模式两栏才有固定宽度。
- **驱动 IDE 本身（外壳/设计器）要隔离配置**：用配置目录环境变量指到临时目录并
  写入"上一个工程"指针（工程绝对路径），IDE 启动即打开该工程，不碰用户自己的
  配置；UI 脚本指向脚本**文件**路径、输出目录指到临时目录。命中区的
  **id 每帧都在变**（每帧重新分配），要判断"视图换没换"只能比**几何**
  （矩形集合），不能比 id。
- 编译:`zanc <file>.zan --auto-stdlib -o out.exe`(GUI 程序自动带 zan_gui 驱动)。
- 跑起来真实看一眼,截图对照自查清单(截图必须锚定被调试窗口的 PID、按窗口
  截取并先验证再判断);交互(点击/拖拽/键盘)用
  `ZAN_UI_SCRIPT` UiDriver 驱动做可重复流程,不要手点一次就算完。
- **判读像素前先核对产物时间戳**:`dump pixels x.png` 写出的**就是原始像素**
  (24 字节头 + RGBA,不是 PNG);自己另存的 `.raw` 副本下一轮不会被覆盖,
  手里那份就成了**上一轮的旧画面**——拿旧文件判读会把「已修好的」当成还在,
  或把「旧缺陷」当成新问题(坑出处:为一个 14:59 的旧截图追了 40 分钟,
  结论全错)。规矩:只读本轮 driver 直出的文件,判读前看 mtime 与尺寸,
  转换副本用带时间戳的新名字,别覆盖同名。
- UiDriver 只绑进程里第一个 App:ChildWindow 里的树驱动不到。要端到端
  驱动子窗口界面,拆成 View 控件(真实整棵树)+ 薄 ChildWindow 壳
  (`SetRoot(new View, null)`):探针把 View 挂进主窗口驱动全部交互,
  生产路径仍走子窗口壳,两边同源(微信模板通讯录管理窗,。
- **多窗口端到端验证用真实 Win32 点击驱动,别硬掰 UiDriver**:登录窗
  (独立泵)→主窗这种多窗口流,UiDriver 绑在第一个 App 上驱动不到主窗;
  改用 Win32 层驱动(SetCursorPos+mouse_event 客户区坐标+标题栏高度
  修正、PrintWindow 按 hwnd 截图、Stop-Process 收尾),打字走
  **剪贴板粘贴**(Set-Clipboard+^v)——机器只有中文 IME 时 SendKeys
  会把 "admin1234" 组成 "admin安德敏234"。粘贴中文文本的进程管道
  (bash→powershell)按 GBK 显示乱码是**终端显示问题**:
  `乱码.encode('gbk').decode('utf-8')` 还原后与服务端存储 byte 级比对,
  别当成输入 bug 修。
- **运行期文档使用真实装载入口**：约定式窗口调用 `Show()`；已有
  App 的页面调用 `Gui.Html.Load(app, html, handlers, baseDir)`，再挂到
  父树。没有生成的 AppHtml.Css/Build；CSS 单位与根高度按本文布局规则核对。
- **HUD 根按 CSS 排版**：满屏 HUD 用 Flex 列，中间层声明
  `flex-grow: 1; min-height: 0`，顶部/底部条 `flex-shrink: 0`；
  不使用 `data-dock` 或数字 dock 作为运行时停靠指令。
  用 UiDriver 布局树核对实际矩形，不能从旧停靠推导当前行为。
- **窗口与内容面板贴边纪律与圆角状态契约（Window Margin & Corner Invariant）**：
  ① 桌面窗口根节点（`body` 与 `.designroot`）默认必须 0 外边距与 0 内边距、100% 贴边撑满客户区；留白应由具体业务卡片/内联布局显式决定，根节点绝不强行内缩，禁止机械带出浏览器的 `body { margin: 8px }` 导致主界面悬空漏边；
  ② 通用容器/面板（`panel`）默认必须直角透明无内边距（`border-radius: 0; padding: 0; box-shadow: none`），填满窗口（Flex 增长项）时天然贴合无缝咬合；只有明确声明为展示卡片的独立部件（`card` / `panel.card`）才带圆角与投影，禁止普通面板默认带圆角导致贴边时四角切开漏底；
  ③ 窗口圆角与最大化状态严格联动：最大化（`SIZE_MAXIMIZED`）时铺满全屏，DWM 与异形遮罩必须自动取消圆角（直角平铺），防止屏幕四角漏出桌面背景；从最大化还原（`SIZE_RESTORED`）时必须完整恢复原本的圆角与阴影规格，严禁单向破坏不可逆。
- **自由画布动态像素标尺系统（Dynamic Pixel Rulers）与视口投影**：
  设计器画布支持视口平移（ox/oy）与光标中心缩放（psMilli）时，屏幕物理像素与文档设计逻辑像素（fx/fy/fw/fh）
  动态映射。顶部与左侧动态标尺必须依据逻辑逆映射 `minLx = (inX + rThick - ox) * 1000 / psMilli` 循环发射，
  并根据缩放档位阶梯自适应主副刻度步长（>=140% 为 50px/10px，<=50% 为 200px/50px，常规为 100px/20px），
  副刻度短线、主刻度长线带像素值标签。标尺需双向联动：主选中控件在标尺对应轴向投影半透明高亮跨度区间，
  鼠标光标所在像素线上于标尺槽内绘制高亮指示游标线（Cursor Indicator），实现专业级的像素距离感知反馈。
- **按固定间隙均匀重排（Uniform Gap Distribution）必须解除旧约束**：
  多选组件进行水平或垂直排版时，经典“水平均分 / 垂直均分”仅平分首尾组件间的剩余空间，无法满足设计系统
  对固定档位间距（如 8px / 12px / 16px）的一键重排需求。实现按固定间距重排时，首个元素作为起始锚点，
  后续元素在写入新坐标前必须先解除旧约束（`DesignMakeFloat(f)`），并在累加前一项实际渲染宽高（`LayoutW()` /
  `LayoutH()`）与指定间距 `gapPx` 之后写入 `f.fx`/`f.fy` 并重声明矩形（`DesignDeclareRect(f)`），确保在不同盒模型
  或内联尺寸下均保持严格像素级间距一致。
- **自由画布辅助参考线（Canvas Guide Lines）的标尺拖出交互与智能磁吸优先**：
  设计器支持标尺后，辅助参考线是从标尺直接延展的一等公民排版辅助工具。从顶部标尺（Y轴）或左侧标尺（X轴）
  按下拖出时，需实时以逆投影逻辑像素计算位置并在光标旁展示坐标浮层（Tooltip，如 `X: 120px`）；
  拖回标尺或拖离视口松开时自动删除对应参考线。在移动或拉伸组件的磁吸计算（`FreeSnapLines`）中，
  参考线坐标必须作为一等公民候选线并入，使得组件边缘与中轴线能够精准吸附到参考线上，实现工业级排版手感。
- **多选外包围盒统一变换（Multi-Selection Bounding Box & Group Transform）与等比约束**：
  自由画布多选时，单个组件再分别展示 8 个手柄会导致视觉重叠、手柄密集错乱且无法整体拉伸。
  规范做法是：多选时各子控件仅保留高亮选中边框，在外部统一计算多选最小包围盒（`GetMultiSelWorldRect`），
  仅由外包围盒发射 8 个外角/边缘控制手柄并附带顶部徽标（如 `3 项  240 × 120 px`）；
  拉动外手柄时记录全部成员初始相对坐标（`MultiResizeItem`），按总宽高变化率（`newW / origW`, `newH / origH`）
  同比例拉伸并重新映射每个成员的 `fx/fy/fw/fh`（按住 Shift 键时角手柄严格按宽高比等比约束）。
- **大纲树图层管理工具条与容器状态显式化（Outline Tree Toolbar & State Badges）**：
  当窗口文档包含深度嵌套的容器（Card/Tabs/Panel）或大量浮动/停靠组件时，画布容易出现层叠遮挡无法点击选中的问题。
  大纲树（Outline Tree）必须作为一等公民图层管理器：在树视图顶部配置紧凑快捷工具栏（上移一层 ▲、下移一层 ▼、
  锁定/解锁 🔒、删除 ✕、全部展开/折叠 +/-），免除繁琐的右键菜单跳转；在树节点标签上显式标注停靠方向（如 `[Top]`、`[Fill]`）、
  浮动状态（`[Float]`）、锁定状态（`[🔒]`）以及当前接收新加入控件的容器（`[★ Active]`），提供一目了然的层级与状态反馈。
- **键盘方向键微调（Nudging）与容器相对坐标纪律**：
  在设计器中使用方向键进行像素微调时，严禁使用组件绝对渲染坐标（`LayoutX()` / `LayoutY()`）进行累加后直接写回 `fx`/`fy`，
  因为嵌套在容器内的子组件 `fx`/`fy` 必须始终保持相对于父容器内部的局部坐标；直接累加绝对渲染坐标会导致组件跳变瞬移出父容器。
  正确做法是以局部坐标 `f.fx + dx` / `f.fy + dy` 进行增量偏移，并在微调时自动调用 `DesignMakeFloat(f)` 解除流式/停靠约束、
  `DesignDeclareRect(f)` 持久化至设计文档。步长设定遵循标准规范：常规方向键 1px 像素级微调，Shift + 方向键大步长（8px 或网格尺寸），
  Alt + 方向键按相同步长微调组件尺寸（`fw`/`fh`），并同步作用于全部多选未锁定组件。
- **多选对齐与统一尺寸（Alignments & Make Same Size）的局部坐标与约束解除**：
  设计器中对多选组件做对齐（左/右/顶/底/居中）时，基准虽然是主选中项的全局逻辑坐标（`DesignWorldX(sel)` / `DesignWorldY(sel)`），
  但写回各从属组件时，必须减去目标所在容器的编辑原点偏移（`f.fx = targetWorldX - this.DesignEditOriginX(f)`），
  切忌直接把绝对渲染坐标 `LayoutX` 赋给 `fx`，否则嵌套容器内的组件将发生跳变脱离父容器；
  在执行“等宽”、“等高”或“等尺寸（宽高同时匹配）”时，必须自动调用 `DesignMakeFloat(f)` 解除流式/停靠约束，
  并调用 `DesignDeclareRect(f)` 持久化进设计文档（`designSource`），避免重载后尺寸丢失。
- **F2 原地快捷重命名（Inline Rename HUD）的视口组件自适应锚定**：
  原地重命名浮层绝不能简单固定在设计器视口/画布的静态绝对位置（如固定在左上角），而应在每帧渲染覆盖层（`RenderDesignOverlay`）
  中实时缓存主选中项在视口中的屏幕几何（`lastSelViewRect`）；原地重命名浮层动态计算并水平居中锚定于组件上方，
  遇到视口顶部边界时智能翻转至组件下方，并对视口边缘做严格钳制，提供就地编辑的自然交互，并配合确认/取消双操作与回车/Esc 响应。
- **画布缩放重置与视口自适应归位（Zoom Reset & Fit to Window Viewport Reset）**：
  在设计器中提供 100% 原始缩放（`Ctrl+0`）和适应窗口（`Ctrl+1` / `Fit to Window`）时，不能仅仅更新缩放比例 `winZoom`，
  必须同步重置抓手平移偏移量（`freePanX = 0; freePanY = 0;`）。如果只改缩放比例而保留旧平移量，会导致画布内容缩放后飞出当前视口，
  用户需要再次盲目拖拽抓手寻找设计内容。重置平移量后能确保设计窗口瞬间精确居中于当前工作区。
- **多选批量属性编辑与高频批处理（Multi-Select Batch Inspector & Quick Actions）**：
  在多选状态下，属性检查器必须显式展示多选批量编辑提示卡片（表明当前已选 N 项，尺寸、内外边距与定位修改将批量同步），
  并提供高频批量操作工具栏（批量切换锁定/解锁、一键清空外边距、一键清空内边距、批量切换流式/浮动），
  彻底消除多选时用户对“改动是否会生效到其他项”的不确定性，提升工业级排版与样式批量微调效率。
- **组件创建副本（Duplicate）的局部坐标与批量克隆纪律**：
  在实现 Ctrl+D（Duplicate 创建副本）时，严禁使用组件绝对渲染坐标（`LayoutX() + 16`）赋给新组件局部坐标 `fx`，
  否则父容器内部组件会跳出容器飞出视口；必须严格基于原组件局部坐标 `orig.fx + 16`、`orig.fy + 16` 进行偏移。
  同时，当处于多选状态时，Ctrl+D 必须支持多选整组批量克隆（保留各组件父级归属与相对坐标），并将克隆出的一组新组件设为新的选中组，
  支持无缝连续克隆排版。
- **多选成组与解组的公共宿主归属与矩形声明纪律（Group & Ungroup Container Scoping）**：
  对多选组件执行成组（`GroupSel` / `Ctrl+G`）时，若所有子项来自同一公共父容器（`commonParent`），新生成的包装 Panel 必须归入该父容器（`commonParent.kids.Add(grp)`），局部坐标直接基于其局部几何运算；若跨容器才折算世界坐标提升至根窗口。解散成组（`UngroupSel` / `Ctrl+Shift+G`）提取子组件时，必须正确折算偏移并对所有子项与新容器调用 `DesignDeclareRect`，确保修改完整持久化到源码，杜绝重载后层级倒流或坐标归零。
- **左侧面板即时搜索过滤框（Palette / Outline Quick Filter）的常驻呈现**：
  拥有庞大组件库与大纲树的设计器左侧面板，必须在顶层 Tab 下方常驻即时搜索输入框（`filterInput`），联动树视图的 `WithFilter` 机制，支持输入拼音/英文/汉字关键字（如 "button", "table", "tab", "按钮"）毫秒级过滤匹配项，避免用户在几十个折叠分组中反复翻找。
- **大纲树节点激活重命名与状态栏实时几何指示（Outline Activation & Status Live Geometry）**：
  设计器中组件大纲树（`TreeView`）应挂载 `OnActivate` 回调：当用户在已选节点上再次点击或回车激活时，即时就地触发 F2 原地重命名（`StartInlineRename`），免除用户在树节点与右键菜单之间往返切换。同时，底部状态栏应动态显示当前选中组件（或根窗口）的实时逻辑位置与宽高（如 `'btnSave' [120, 80  100×32]`），并在多选时标注附加项数，为像素微调和对齐校验提供毫秒级直接反馈。
- **设计器双击防误跳代码机制（Double-Click Navigation Tolerance & Component Whitelist）**：
  在可视化设计器画布中，用户频繁进行组件点选、拖拽微调或多选框选，若仅凭“400ms 内两次点击同一组件”就触发跳转后台代码，极易因快速双击选中、手抖轻微位移或无意识连击而频频误跳到代码编辑框，打断排版心流。
  工业级防误触必须同时满足两层严密防护：
  1. **屏幕位移容差校验**：两次点击在屏幕空间中的位移必须限制在极小容差内（如 `abs(dx) <= Scale(5) && abs(dy) <= Scale(5)`），只要产生拖拽位移或跨区域点击即刻重置，严禁触发代码跳转；
  2. **业务控件白名单与容器/静态项绝对排除**：容器类组件（Panel, Card, Tabs, Collapse 等）与静态纯展示组件（Label, Title, Divider, Tag, Badge 等）在排版时属于高频承载与对齐参照物，绝对禁止双击跳代码；只有明确具备交互业务事件的组件（Button, Input, Checkbox, Switch, Tree, DataGrid 等）或自定义组件引用（`compRef`，双击打开子设计稿）才允许双击导航。
- **全流式排版高度自动撑满（Fill Remaining Height）一键快捷动作**：
  在全流式（Flex/Flow）布局下，组件撑满剩余高度的规范语义是 `position = 0`、`flexGrow = 1` 且 `fh = 0`（由弹性主轴自动分配撑满余量空间）。
  若仅提供抽象的 `flexGrow` 整数输入，用户极难联想到高度填满需要将固定像素高度置 0。
  工业级设计器必须在属性检查器（单选流式组件与多选批量编辑卡片）中直接提供显式的 `[ ↕ 自动撑满高度 (Fill) ]` 与 `[ 0 内容自适应 (Auto) ]` 快捷动作，
  并在 Height 属性提示中清晰注明，一键消除认知障碍。
- **画布多选外包围盒常驻悬浮排版与尺寸统一工具条（Quick Align & Size HUD）**：
  在可视化画布中多选组件后，用户最高频的排版操作是对齐、均分间距与统一尺寸。若强制用户每次都要在右侧属性面板或右键菜单中寻找这些命令，操作路径过长且割裂视线。
  规范做法：在多选外包围盒（`RenderMultiSelBounds`）顶部（视口越界时自适应置于底部）常驻渲染半透明微型悬浮排版工具条（HUD），集成完整 10 键排版矩阵：
  1. 四向对齐：`[ ⇤ 左对齐 | ⇥ 右对齐 | ⤒ 顶对齐 | ⤓ 底对齐 ]`；
  2. 均分分布：`[ ⇋ 水平均分 | ⇅ 垂直均分 ]`；
  3. 规范间距重排：`[ ⇹ 水平 8px 间隙 | ⇳ 垂直 8px 间隙 ]`；
  4. 尺寸统一：`[ ↔ 等宽 (Match Width) | ↕ 等高 (Match Height) ]`。
  无需往返属性表，多选组件一键就地完成对齐、分布、间距和尺寸规范化。
- **自由画布跨容器拖拽重排与吸附高亮（Container Reparenting & Drop Feedback）**：
  在自由画布拖拽移动组件时，用户常需要将组件移入某个 Panel/Card 容器，或从子容器拖出提升回窗口根层。
  规范做法：
  1. 拖拽过程中动态探测指针下方的承载容器（排除自身及其所有子树，防止循环嵌套）；
  2. 目标容器实时给予半透明高亮填充、外边框及浮动徽标（如 `"↳ 移入容器: Panel1"`），拖出容器至根层时高亮画布边框并展示 `"⇪ 移回窗口根层"`；
  3. 鼠标松开（Drop）时自动更新父子归属（`pc.kids.Remove` 与 `cont.kids.Add` 或 `fields.Add`），并以新宿主原点精准折算相对逻辑坐标 `DesignDeclareRect`，保证绝对视觉位置无缝承接。
- **容器专属排版快捷工具栏与常用间距档位（Container Quick Layout & Gap/Padding Presets）**：
  当用户选中容器类组件（Panel, Card, GroupBox 等）时，排版流向与间距是核心高频属性。
  若将流向（`flexDir`）、间距（`gap`）和内边距（`padding`）淹没在冗长抽象的属性网格中，用户需要反复翻找并手动输入数字。
  规范做法：在属性检查器顶部常驻容器排版快捷卡片，集成「⬇ 纵向 (Column) / ➔ 横向 (Row)」单选切换，以及 Gap（0, 4, 8, 12, 16）与 Pad（0, 8, 12, 16, 24）常用规格一键快捷按钮，配合撤销记录与实时重绘，实现秒级排版定型。
- **智能 Alt 测距之父容器内缩四周边距（Alt Parent Inset / Padding Guides）**：
  按住 Alt 测量组件间距时，除两个独立组件之间的间距连线外，当光标悬停在空白处或父容器上时，应自动测量当前选中组件到其父容器（或根窗口）四边（Top, Bottom, Left, Right）的内缩距离，
  并绘制高亮参考边框与带像素数值的半透明数字徽标。这与 Figma/Sketch 行业标杆体验无缝对齐，消除目测与心算容器边距的成本。
- **常用界面骨架复合模板（Starter Layout Presets）**：
  从零拼凑容器层级、手动配置工具条 Dock.Top、状态栏 Dock.Bottom 以及主工作区 Flex.Fill 是界面设计初期的主要心智负担，且极易因手动设置疏漏导致浮动遮挡或无法自动撑满高度。
  工业级设计器应在画布右键菜单（空白处与容器节点）及快速插入（Quick Insert Ctrl+I）中提供常用骨架复合模板一键生成能力：
  1. **经典主窗口骨架 (Classic App Skeleton)**：置顶 ToolStrip (Dock.Top) + 主工作区 (Dock.Fill / Flex.Fill) + 状态栏 (Dock.Bottom)；
  2. **左右双栏工作台 (Split Workspace)**：外层 Fill 容器 + 左侧导航栏 (Dock.Left, 宽220) + 右侧主工作区 (Dock.Fill)；
  3. **标准水平表单行 (Standard Form Row)**：横向流式行，内置 80px 标题标签 + 自动撑满余量输入框 (Fill) + 确认按钮；
  4. **对话框底部操作栏 (Dialog Action Buttons)**：底部容器 (Dock.Bottom)，横向靠右居中排布标准规格的「取消」与「确定」按钮。
  一键生成完整规范的父子结构、停靠边、弹性填充、内外边距与间隙，从根本上杜绝布局畸变并提升排版效率。
- **单选组件对齐到父容器（Align to Parent）与一键全选同类组件（Select All of Same Type）**：
  1. **单选对齐到父容器**：在自由排版或混合布局中，单选单个组件在父容器内的居中与贴边（水平居中、垂直居中、靠左/靠右/靠顶/靠底）同样属于高频操作。若右键菜单仅在多选时展示对齐选项，单选时完全隐藏，用户将被迫手动计算坐标或拖拽对齐，极易出现 1~2px 视觉漂移。规范做法是在单选右键菜单中提供「对齐到父容器」系列命令（`Align to Parent: Center H / Middle V / Left / Right / Top / Bottom`），自动抓取父容器内容矩形（`DesignContentRect`）并贴边/居中；
  2. **一键全选所有同类组件**：用户经常需要对同一类组件（如所有 Button、所有 Input）进行批量尺寸统一或样式调整。单选右键菜单中提供「全选所有同类 (Select All of Type: X)」，递归遍历整张文档或容器树将所有同类组件自动加入多选组（`freeExtra`），无缝衔接 HUD 10 键排版或属性批量修改。
- **流式布局键盘流与同级兄弟调序（Reorder Sibling via Ctrl+Arrow）**：
  在自由画布和流式排版中，方向键在绝对定位模式下用于微调像素位置，但在流式模式（Flex / `position == 0`）下由于视觉位置完全由兄弟节点顺序决定，纯像素位移无效且用户无任何反馈。
  规范做法：
  1. 全局支持 `Ctrl + ↑ / ←`（向前调序一位 `MoveSel(-1)`）与 `Ctrl + ↓ / →`（向后调序一位 `MoveSel(1)`），并在右键菜单中提供「同级调序: 向前移动一位 / 向后移动一位」与快捷键指南说明；
  2. 在流式模式下，普通按方向键（无 Ctrl、无 Alt）直接作为顺应文档流方向的同级前后调序，杜绝“按了按键毫无反应”的体验断层。
- **多选对齐与尺寸统一之基准项（Key Object / Anchor）视觉辨识**：
  多选组件执行对齐（居左/居中/居右/居顶/居底）或尺寸统一（等宽/等高）时，算法必然以某一个组件为基准锚点（主选中项 `sel`）。
  若画布上所有多选组件外边框完全相同，用户无法预知究竟以谁为基准，导致对齐方向与预期偏离。
  规范做法：在多选绘制（`RenderDesignOverlay`）中，对主选中项（`sel`）绘制双层高亮边框并显示醒目的 `[基准]` / `[Key]` 角标徽标，与其他附加项清晰区隔，使用户在点击 HUD 工具条时对锚点一目了然。
- **容器内流式拖拽精确定位与插入指示线（In-Container Insertion Indicator & Precise Drop）**：
  在自由或流式排版中，当用户将组件从工具箱拖入已有的容器（Panel, Card 等）或在容器间移动时，若仅将容器整体高亮并将新组件生硬地追加（Add）到容器末尾，用户无法控制组件在兄弟元素中的相对次序。
  规范做法：
  1. 探测宿主容器的流向（横向 Row / 纵向 Col）；
  2. 动态比较光标指针坐标与容器各子节点的视觉包围盒中轴位置，实时计算精准的插入插槽索引 `dropIndex`；
  3. 在相邻两个子组件之间绘制高亮端点指示线（带左右/上下端点凸起提示）；
  4. 在鼠标释放（Drop）时直接调用 `kids.Insert(dropIndex, item)` 精确插槽落位。
- **属性检查器常用尺寸规格快捷档位（Quick Size Presets Card）**：
  高度 `H` 与宽度 `W` 是组件最频繁调整的物理几何属性。若强迫用户每次双击 PropertyGrid 文本框手工输入纯数字，操作成本高且极易违背设计系统的基线网格。
  规范做法：在属性面板顶部常驻紧凑的尺寸规格卡片：
  1. 高度 `H`: 提供常用的 `[Auto: 0]`, `[24]`, `[28]`, `[32]`, `[36]` 档位快捷切换；
  2. 宽度 `W`: 提供常用的 `[Auto: 0]`, `[80]`, `[120]`, `[160]`, `[200]` 档位快捷切换；
  3. 配合撤销事务记录、当前规格按钮高亮与即时重绘，消除重复手工输入成本。
- **Shift 拖拽正交轴向平移锁定（Orthogonal Axis Constraint via Shift+Drag）**：
  在自由画布上拖拽移动组件时，人工手部移动很容易伴随微小颤动或斜向漂移。
  规范做法：检测 `Shift` 修饰键，计算 X 与 Y 轴的相对位移绝对值，保留位移较大的一侧为主平移方向，将另一轴位移严格置零，实现纯水平或纯垂直的正交锁定移动，配合状态栏明确提示（如 `Shift: 锁定水平平移` / `Shift: 锁定垂直平移`）。
- **Alt+拖拽就地克隆副本并连续平移（Alt+Drag Clone on Move）**：
  在需要快速排列多个相同或相似组件（如一系列并列按钮、数据列标签）时，传统的先 Ctrl+C / Ctrl+V 再拖开操作割裂视线。
  规范做法：在拖拽产生真实位移时检测 `Alt` 键，原地克隆当前选中项（或多选组），将新副本直接无缝切换为当前拖拽对象继续移动，松开鼠标时原组件保持不动、新组件已在目标位置就绪。
- **画布双击智能区分原地编辑显示文本与后台代码跳转（Double-Click Inline Text vs Code Navigation）**：
  在可视化画布中，用户快速双击或连击组件时，过去曾无差别触发跳转后台代码（`RequestOpenCodeBehindHandler`），导致焦点丢失，且无法直观修改按钮/标签文案。
  规范做法：
  1. 双击时优先通过 `HasDisplayText(sel)` 判定是否为带有界面展示文本特性的组件（如 Button, Label, Checkbox, Radio, Title 等），若有则就地激活 F2 原地编辑浮层（`StartInlineRename`），自动聚焦输入框并全选文案；
  2. 仅在明确按住 `Ctrl` 键或双击无展示文本的业务控件时才允许导航至代码后置事件，彻底消除排版心流被强制打断的痛点。
- **流式组件拖拽保护与容器内精准插槽调序（Flow Drag Preservation & In-Container Slot Reordering）**：
  在自由画布或混合排版模式下，过去 `DesignBeginMove` 曾无差别对所有拖拽组件调用 `DesignMakeFloat`，导致在流式容器或流式表单里的组件一拖动就被破坏性降级为浮动绝对定位（`position = 1`），不仅破坏 Flex 流式自适应特性还会引发图层遮挡；同时，在同容器内拖拽松手时因 `pc == cont` 判定直接 `continue` 跳过，无法通过拖拽进行子项排序。
  规范做法：
  2. 拖拽悬停于容器上方时，始终激活 `FlowDropTarget` 探测流向与子节点包围盒，实时渲染插入线；
  3. 松手 `FreeReparentSel` 中识别同容器内部拖拽（`pc == cont`），结合 `dropIndex` 实现 `kids` 精确插槽调序，无论在容器内还是跨容器均完美保持流式属性。
- **属性检查器多选批量外边距与内边距档位矩阵（Batch Margin & Padding Matrix in Inspector）**：
  在多选批量编辑卡片（Batch Editing Card）中，集成 Row 2 外边距 `Mg (px)` 与 Row 3 内边距 `Pad (px)` 快捷档位矩阵（0/4/8/12/16/24px），使选中的多个组件能够一键批量统一并规范外边距与内边距，配合高亮按钮与实时重绘，消除逐个反复修改的机械重复。
- **全局快捷键均分分布与撤销/重做语义动作跟踪（Global Distribution Shortcuts & Semantic Undo Action Tracking）**：
  1. 全局支持 `Ctrl+Shift+H`（水平均分）与 `Ctrl+Shift+V`（垂直均分）快捷键，无缝衔接多选排版流水线；
  2. 撤销/重做系统（Undo/Redo）维护动作语义栈（`undoActionStack` / `redoActionStack`），在压栈时同步记录具体操作类型（如“对齐组件”、“水平均分”、“批量外边距”、“锁定/解锁”等），撤销或重做时在状态栏精准呈现具体操作描述与剩余步数，并在工具栏按钮上展示实时步数徽标，消除心智盲区。
- **外壳控件智能默认Dock停靠与检查器停靠矩阵（Smart Docking on Creation & Quick Dock Matrix）**：
  在自由或混合排版中，外壳控件（ToolStrip, StatusBar）如果被默认赋予绝对浮动坐标（Float），后续添加的流式容器（Panel, Form, GroupBox）会从 (0, 0) 开始排版并将工具条覆盖遮挡。
  规范做法：
  2. 属性检查器单选排版卡片常驻提供 `Dock: [None 0] [Top 1] [Bot 2] [Left 3] [Right 4] [Fill 5]` 胶囊切换按钮矩阵，一秒在常用停靠与铺满余量间切换，并自动同步坐标与重算布局。
	- **大纲树同级调序方向与视图闭环联动（Outline Sibling Reordering & Immediate Refresh）**：
	  在组件大纲树（Outline TreeView）等纵向层级树视图中，列表物理索引为从上至下（第 0 项在最顶部，第 Count-1 项在最底部）。
	  规范做法：
	  1. 大纲树工具条中 `▲`（向上移动）应对应兄弟列表索引减 1（`MoveSel(-1)`），`▼`（向下移动）对应索引加 1（`MoveSel(1)`），与树节点在视觉列表中的物理位移直觉严格保持一致；
	  2. 触发调序、锁定/解锁或删除后，必须立即触发 `EnsureOutline()` 与 `RequestRedraw()`，使大纲树节点物理顺序与高亮焦点无缝跟随，杜绝视图滞后或焦点跑偏；
	  3. 大纲树激活双击（`OnActivate`）时智能区分容器与展示组件：具备文本属性的控件直接呼出原地编辑框，非文本控件与容器避免误弹重命名浮层。
	- **设计器持久化与尺寸微调之布局模式保护（Layout Mode Preservation in DesignDeclareRect & Resize）**：
	  过去 `DesignDeclareRect` 曾错误硬编码 `position = "1"` 与 `dock = "0"`，导致任何通过检查器修改属性或微调尺寸的流式/停靠组件，一被持久化或刷新就强制降级为绝对浮动（`position = 1`）；同时 `DesignMakeFloat` 在调整尺寸手柄时也无差别将流式组件降级为浮动。
	  规范做法：
	  2. `DesignMakeFloat` 在检测到组件为流式（`f.position == 0`）时，拖动手柄调整尺寸仅更新 `fw/fh` 推荐像素，保留 `position == 0` 流式属性，绝不强行降级为绝对浮动；
		  3. 画布右键上下文菜单（`CanvasMenuLabels` & `HandleCanvasMenu`）同步完备集成 `Dock`（Top/Bottom/Left/Right/Fill/None）与 `Flow: Fill Height`（撑满剩余高度 `flexGrow=1, fh=0`）/ `Flow: Auto Height` 一键命令，消除全流布局填满高度的心智门槛。
	- **设计器右键上下文菜单（ContextMenu）精简规范与事件防穿透门控**：
	  对标工业级 IDE（如 Visual Studio 设计器规范）：
	  1. 上下文菜单项数必须精简克制（单选 10~15 项，多选 15~18 项，空白根 8~10 项），严禁将模板插入、底层排版流等杂项平铺进右键菜单导致菜单高达上千像素溢出屏幕被截断；
	  2. 通用 ContextMenu 必须标准化识别 `"-"`、`""`、`"---"` 分隔线，制表符 `\t` 或双空格后的快捷键（accel）必须自动剥离并两端右对齐渲染，单项行高不低于 26px 并配备悬停圆角底衬，确保留白舒适；
	  3. 裸指针事件防穿透门控：设计器画布、绘图区等直接读取底层裸事件（`app.EventKind()`、`mouseX`、`mouseY`）的组件，当存在浮层弹窗、ContextMenu 或帮助浮层时，必须建立联合锁定门控（`locked = BigEditorOpen || canvasMenuOpen || quickInsertOpen || keymapHelpOpen`），并在鼠标抬起（`EventKind == 3`）、双击跳转、标尺拖拽等关键路径全量施加 `!locked` 门控，彻底封死在菜单项上点击时穿透到底层画布篡改组件或误跳代码的历史顽疾。
		- **属性检查器窄面板下密集按钮网格排布（Inspector Grid Adaptability in Narrow Panels）**：
		  在属性检查器（宽 200~300px）中，当操作选项多于 4 项时（如 Dock 的 6 种停靠方式：None/Top/Bottom/Left/Right/Fill），严禁在单行硬塞 6 个按钮导致文字严重截断挤压与视觉畸变；规范做法是拆分为标准 3+3 两行等宽网格排布，垂直与水平间距遵循 4px 阶梯，行高宽裕舒适，彻底根治窄面板下按钮变形的顽疾。
		- **无边框窗口边框拖拽手势与客户区光标切换纪律（Border Resize Cursor vs Client Cursor Synchronization）**：
		  无边框自定义窗口通过 `WM_NCHITTEST` 将边缘映射为系统缩放手柄（HTLEFT/HTRIGHT/HTTOP/HTBOTTOM 等）时，系统底层会在鼠标经过边缘时由 `DefWindowProcW` 派发 `WM_SETCURSOR` 自动设置调整大小光标（双向拖拽手势）。
		  若光标进入窗口客户区（`HTCLIENT`）后仍残留拖动手势，典型根因有二：
		  1. `WM_SETCURSOR` 捕获到 `HTCLIENT` 时直接 `return 1` 但未调用 `SetCursor`，导致 Win32 维持上次的光标手势；
		  2. 底层应用层缓存了 `currentCursorKind`（如进入边框前本就是 0 默认箭头），鼠标回客户区后 App 再次设置 0 被 `kind == currentCursorKind` 短路跳过，系统 API 从未被触发。
		  规范做法：
		  1. 状态双轨分离：分离 `desiredCursorKind`（应用期望的目标光标）与 `appliedCursorKind`（Win32 实际生效的光标）；
		  2. 非客户区失效标记：在 `WM_SETCURSOR` 处理中，当 `hit != HTCLIENT`（鼠标在非客户区时），将 `appliedCursorKind` 标记为 `-1`（失效），放行给系统设置拖拽光标；
		  3. 客户区即时重置：当 `hit == HTCLIENT` 时，立即无条件调用 `SetCursor(CursorHandleOf(desiredCursorKind))` 并更新 `appliedCursorKind` 返回 1，使光标在跨过边框的第一时间恢复默认/目标光标，消除滞留延迟。

	- 跑 GUI 程序别用 `Start-Process -WindowStyle Hidden` 启动:STARTUPINFO 的
  SW_HIDE 会传给子进程,Zan 窗口创建即隐藏、事件循环立刻返回,症状像
  "窗口创建失败/进程秒退";这次被它带偏,顺着去查机器环境半小时。直接
  `Start-Process exe` 本体,要静默就最小化到任务栏。
