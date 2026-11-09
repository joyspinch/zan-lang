# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。领账按批次：批次 1 根修优先（闭一条解锁一片），批次 2 独立账可并行，批次 3 等前置闭了再领，批次 4 大工程须独占窗口；同批内按编号序。每条正文前的〔…〕是依赖标签：〔前置 X〕= X 不闭本条闭不了；〔软前置 X〕= 没 X 也能做但成本高；〔根修·解锁 Y〕= 本条闭了 Y 的前置随之解除；〔串行前置 X〕= 与 X 同改一个文件，等 X 落地再动。根修闭账时同批摘掉下游条目的〔前置〕标签并上调批次。领账前 `git status` + 文件 mtime 查在飞——并发会话不一定走调度器注册（2026-11-03 B-ID107 即靠 mtime 抓到撞车）。
>
> 2026-11-05 平账：移出已闭账 [x] 十条（B-ID149/150/151/154/156/157/158、B-ID159 窗口声明面纯正典标签化三刀全量闭环，以及批次 4 独占大工程 B-ID103 irgen 编译单元解耦与 B-ID71 LandingPad 零开销异常系统重构落地），结论与验证证据由对应提交正文承担。
>
> 2026-11-09 平账：移出已闭账 [x] 十九条（B-ID160/161/163/164/165/166/169/180/181/182/183/191/192/193/194/195/196/197/198）、裁定搁置三条（B-ID152/153/155，不领的裁定理由由 git 历史承担，勿重复提案），另移出两条已完成却误挂 [ ] 的：B-ID184 全仓编译门（selfhost_repo / selfhost_repo_packages 已落地注册，CMakeLists.txt:3920/3935）与 B-ID185 跨编译器 IR 差分（评估后不可行，由 selfhost_ir_golden 按函数金标门替代，依据见 tests/run_selfhost_ir_golden.cmake 头注释）。批次 0 的 2026-11-06 JsonValue→JsonWriter 在飞已收口（packages/Zan.Net 已干净）。

## 未完成（按平账顺序分批）

### 批次 0 · 在飞（持有会话未提交；其他会话勿碰同文件）

- [~] 在飞观察（2026-11-06 补）：ChildWindow 声明式装配——z-mount 挂载（`DocWire.MountSlots`）、`:prop` 绑定启动期对账、`diag_cw_bad_mount` 注册；涉及 Zan.Gui 9 个文件（UiDriver/ChildWindow/Control/ControlExtra/DocWire/ModelBinder/Html/Progress/win-x64 libzan_gui.a）与 CMakeLists.txt（childwindow_budget_test.zan 2026-11-08 仍有改动，在飞持续）。持有会话未注册调度器。GUI 分层重构（B-ID171+）已绕开这些文件。
- [~] 在飞观察（2026-11-09 补）：游戏域拆包——`Zan.Game` 收缩为引擎核心（Core/Graphics/Kit/Scene/Physics 等），`Zan.Game.Arpg`/`Zan.Game.Idle` 等类目整包迁出为独立包（工作区未提交 D/?? 状态）；gui-design/game-dev skill 也有同日未提交改动（Dock 方言清理方向）。持有会话未注册调度器；B-ID177 的游戏域迁移待拆包收口后执行。
- [~] 在飞观察（2026-11-09 补）：ide_zan 面（Docs/Marketplace/Wizard 组件、AiAssistant/AiSettings 对话框、LspRange.zan 等，2026-11-08/09 mtime）另一会话在飞未提交；B-ID167 ③④ 涉及同面文件，待其收口。

### 批次 1 · 根修项（闭它解锁下游，优先领）

（当前无欠账；B-ID160/163 已闭移出。）

### 批次 2 · 独立账（无前置，任一会话可领，按编号序）

- [~] B-ID171 GUI 分层重构 P0 安全网与护栏（`docs/GUI_ARCHITECTURE.md` §4，后续 B-ID172～179 同）：① 计划落盘（立项即落）；② 补注册未登记 GUI 用例 + 布局树金标用例（P5 等价门）〔串行前置：批次 0 在飞 CMakeLists.txt〕；③ 清死 using（实测 23→5，2026-11-09）与文档漂移（zan.pkg、README、agent-kb、gui-design skill）；④ 层级机检补 static 可变字段棘轮（Gui.Core/Widget/Component，2026-11-09 裁定）；⑤ gui-design/game-dev skill 画布例外条款改写与三副本同步——有并发会话在飞未提交改动，待其收口后同提交执行。
- [ ] B-ID172 GUI 分层重构 P1 断包环、按目标层摆目录：① 控件注册梯子退役（2026-11-09 用户裁定 `Install()` 类注册方案属不可行历史方案，移除；原 HmiBootstrap 外移方案作废）——替代机制先出小设计：候选为编译器生成控件清单（类型在编译图内即自动可按 kind 构造、miss 显式报错，替代 ControlBootstrap/DataTableBootstrap/ChartBootstrap/CefControlBootstrap 全部手工注册），ControlBootstrap 删 `using Gui.Hmi` 并入该机制；② 新建 Forms/Binding/Testing 目录、基元移到 src/Gui 根（命名空间不变）〔串行前置：批次 0 在飞 ChildWindow/DocWire/ModelBinder/UiDriver〕；③ ControlBootstrap 移入 Widget/。

### 批次 3 · 有前置（前置闭了再领）

- [ ] B-ID167〔前置：批次 0 ide_zan 在飞收口（仅 ③④）〕在飞文件上的整改顺延包：① `AiClient.zan:74-76` 三参构造器漏初始化 `clients` 字段（经此构造的客户端一调 Dispose 即空引用；头部 `: this(new AiConfig(...))` 链修复）；② AiClient/AnthropicClient 提炼 AiHttp（PostJson 36 行逐行同、客户端缓存、错误提取）；③ ide_zan SseLive.zan 改用 SseLineReader 只留业务增量；④ ide_zan dialogs `T(lang,en,zh)`×8 收拢 Lang.Tr + Window `Get/GetInt`×4 提炼 ChildWindow 基类；⑤ JsonWriter 17 个单行别名三套命名收敛（动公共 API 先全仓 grep 消费方）。packages 侧 ①②⑤ 的前置已解除（JsonValue 迁移已收口），可先行。
- [ ] B-ID173〔前置 B-ID172〕GUI 分层重构 P2 绑定收敛：PropSpec 标默认绑定属性、ModelBinder 删 10 处 `is X` 特判；模型游标接口统一 POCO/JsonValue/JsonDoc/字典取值；ChildWindow 绑定泵并入 ModelHosts；IDE 里当应用状态用的 Signal 改字段 + `Binding<T>`。
- [ ] B-ID174〔前置 B-ID172〕GUI 分层重构 P3 拆 App：颜色计算、BackdropPainter、DamageTracker、FrameLoop、WindowHost、HtmlLoader、InputArbiter、OverlayHost 骨架逐项抽出，目标 App.zan < 1000 行（承接 B-ID170 ① 的 App.zan 部分）。
- [ ] B-ID175〔前置 B-ID174〕GUI 分层重构 P4 统一浮层：OverlayHost 分层结算命中与原生遮挡；OverlayPopup 下沉 Widget；SelectBox/DatePicker/ColorPicker/Popover/菜单改用共享件；模态合一。
- [ ] B-ID176〔前置 B-ID171 ②〕GUI 分层重构 P5 布局方言退役：删 Serialize.zan 与 Alert 别名（Serialize/Dock 引擎已删，2026-11-09 实测）；补齐 CSS grid/absolute；解析期脱糖 data-dock/data-fx 后删兜底（带旧属性的设计器文档实测 43 个，文件改写并入 B-ID179）。
- [ ] B-ID177〔前置 B-ID175〕GUI 分层重构 P6 即时模式与静态状态下线（2026-11-09 裁定扩为"全部统一"，画布例外取消）：路由事件转正 → 控件分批改事件驱动 → 调用方迁移（设计器预览离屏、gui_gallery 并入 html_gallery、Charts/DataTable/CodeEditor/Industrial 整体改写范式二控件——OnPaint 仅内部实现、游戏 HUD 视图与 SceneDesigner 面板纳入）→ 删 `Render(App` 入口、InputArbiter、Ui 轮询族与静态可变状态（WidgetId 全局栈、OverlayPopup static occl*/lastRichHover、Layer 静态打开态队列）；Mat4/Mesh3D 外迁 Zan.Game 引擎核心〔软前置：批次 0 游戏域拆包收口〕。
- [ ] B-ID179〔前置 B-ID176〕GUI 分层重构 P8 设计器改产出 CSS，脚本改写 42 个设计器文档（含 IDE 面板），删 P5 脱糖兜底（P5 完成后单独出计划）。

### 批次 5 · 自举工业化（自举闭环已达 L5 固定点；当前主线 = 能力对齐，性能优化待能力追平后再启动。蓝本 docs/agent-kb/bootstrap.md "Known limitations" G1~G10）

- [ ] B-ID199〔主线〕gen1 能力对齐 C 宿主，按类修复：台账 tests/selfhost/corpus_gaps.txt（382 真缺口，每案过绿毕业进 diff_cases.txt，STALE 段勿修）。当前按类欠账：G9 嵌套擦除泛型读回（Queue/List<List<int>> 型接收者类型节点 kids 被提前剥空，ConcreteRetTy 无实参可映射；审计线索 cast.zan:512 与元素 store 旁路，插桩记录在 corpus_gaps.txt collections_foreach 条目）；undefined value 22 案；crash 0xC0000005 110 案；reject-async 31 案（foreach/循环条件/花括号体未降级）；timeout 31 案；wrong-stdout 91 案。link-env 29 案是差分门链接环境不公平（sqlite3/SetupDi/原生驱动束/openssl），另册处理不算 gen1 缺陷。纪律：探针 → 根因 → conformance 单跑 → 自然写法（WC §10），修一类毕业一类，不无脑修。
- [ ] B-ID186 差分 Fuzzer 种子变异：`scripts/fuzz_mutate.py` 以 diff_cases 243 用例为变异源（数值边界 0/MIN/MAX/NaN/±Inf、null 注入、越界索引、try/catch 包裹剥离、整型宽度翻转）；驱动放 `_scratch/`（一次性，不进 tests）；猎物缩减最小复现 → conformance 双端金标入库 → WC §10 收口。
- [ ] B-ID187 诊断表驱动：DIAGNOSTIC_CODES.md 表格单源机器可读，C 宿主与 selfhost `Diag` 同表生成（C 侧 diag_codes.h、selfhost 侧 .zan 常量片段，生成物进提交）；已对齐 E3012/E3049/E3052/E3053/E3060/E3086 先迁作模板。
- [ ] B-ID188 reject 金标：reject_cases.txt 每用例配 `.stderr` 金标（先只锁 `code: 摘要` 两字段，容忍位置差异），run_selfhost_diff reject 分支从"rc≠0 即过"升级为"rc≠0 且 stderr 匹配金标"；前置 B-ID187（金标依赖 code 字段）。
- [ ] B-ID189 防倒退指纹（② fixedpoint.sha 已落地移出）：① gen1/gen0 `--version` 输出代次 + selfhost_sources.txt 内容 SHA256（差分报告可复现）；③ pre-commit 增检：diff 触及 src/selfhost/** 或 src/compiler/** 时提交说明须含 `selfhost-verified:` 标记（只机检标记存在，真跑仍按 WC §8 定向执行）；④ `scripts/check_selfhost_docs.py` 机检 selfhost_sources.txt 与 bootstrap.md 文件表的存在性。
- [ ] B-ID190〔独占大项〕parser 错误恢复：错误后继续 parse，服务 LSP/IDE 体验；单开计划，不与 B-ID187/188 混线。

### 批次 4 · 大工程 / 独占窗口（无其他 irgen/runtime 改动在飞时领）

- [ ] B-ID170 结构收口二件：① 中风险拆分（Designer 三件套：Designer.Form.zan 4301行已于 commit 3b8cdd439 按 partial 拆出 Ribbon/Layout/Free/Canvas/Ruler/Preview 6切面并完成 RenderDesignCanvas 巨函数拆解；余 Designer.zan 6909 与 Designer.Inspector.zan 3466、SceneDesigner 2951——后两者将在 B-ID177 改写为保留式，拆前先评估；App.zan 已并入 B-ID174）；② 命名空间三前缀统一（System.* 被 stdlib/packages 同名分仓、Game 裸名——独立大工程，动前定一尊；Gui 已定案并入 B-ID178）。
- [ ] B-ID178〔前置 B-ID172～B-ID177〕GUI 分层重构 P7 按层拆命名空间：目录即命名空间，codemod 原子提交（约 1,285 个调用方文件 + 977 处限定引用），兄弟包各占己名，机检切严格（承接 B-ID170 ② 的 Gui 部分）。
