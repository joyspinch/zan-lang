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

- [ ] B-ID167〔前置：批次 0 ide_zan 在飞收口（仅 ③④）〕在飞文件上的整改顺延包：② AiClient/AnthropicClient 提炼 AiHttp（PostJson 36 行逐行同、客户端缓存、错误提取）；③ ide_zan SseLive.zan 改用 SseLineReader 只留业务增量；④ ide_zan dialogs `T(lang,en,zh)`×8 收拢 Lang.Tr + Window `Get/GetInt`×4 提炼 ChildWindow 基类；⑤ JsonWriter 17 个单行别名三套命名收敛（动公共 API 先全仓 grep 消费方）。packages 侧 ②⑤ 的前置已解除（JsonValue 迁移已收口），可先行；① 已修（2026-11-10，`: this(new AiConfig(...))` 链委托 + conformance `ai_client_ctor` 红绿钉住，case_ai_* 9 条全绿）。
- [ ] B-ID173〔前置 B-ID172〕GUI 分层重构 P2 绑定收敛：PropSpec 标默认绑定属性、ModelBinder 删 10 处 `is X` 特判；模型游标接口统一 POCO/JsonValue/JsonDoc/字典取值；ChildWindow 绑定泵并入 ModelHosts；IDE 里当应用状态用的 Signal 改字段 + `Binding<T>`。
- [ ] B-ID174〔前置 B-ID172〕GUI 分层重构 P3 拆 App：颜色计算、BackdropPainter、DamageTracker、FrameLoop、WindowHost、HtmlLoader、InputArbiter、OverlayHost 骨架逐项抽出，目标 App.zan < 1000 行（承接 B-ID170 ① 的 App.zan 部分）。
- [ ] B-ID175〔前置 B-ID174〕GUI 分层重构 P4 统一浮层：OverlayHost 分层结算命中与原生遮挡；OverlayPopup 下沉 Widget；SelectBox/DatePicker/ColorPicker/Popover/菜单改用共享件；模态合一。
- [ ] B-ID176〔前置 B-ID171 ②〕GUI 分层重构 P5 布局方言退役：删 Serialize.zan 与 Alert 别名（Serialize/Dock 引擎已删，2026-11-09 实测）；补齐 CSS grid/absolute；解析期脱糖 data-dock/data-fx 后删兜底（带旧属性的设计器文档实测 43 个，文件改写并入 B-ID179）。
- [ ] B-ID177〔前置 B-ID175〕GUI 分层重构 P6 即时模式与静态状态下线（2026-11-09 裁定扩为"全部统一"，画布例外取消）：路由事件转正 → 控件分批改事件驱动 → 调用方迁移（设计器预览离屏、gui_gallery 并入 html_gallery、Charts/DataTable/CodeEditor/Industrial 整体改写范式二控件——OnPaint 仅内部实现、游戏 HUD 视图与 SceneDesigner 面板纳入）→ 删 `Render(App` 入口、InputArbiter、Ui 轮询族与静态可变状态（WidgetId 全局栈、OverlayPopup static occl*/lastRichHover、Layer 静态打开态队列）；Mat4/Mesh3D 外迁 Zan.Game 引擎核心〔软前置：批次 0 游戏域拆包收口〕。
- [ ] B-ID179〔前置 B-ID176〕GUI 分层重构 P8 设计器改产出 CSS，脚本改写 42 个设计器文档（含 IDE 面板），删 P5 脱糖兜底（P5 完成后单独出计划）。

### 批次 5 · 自举工业化（自举闭环已达 L5 固定点；当前主线 = 能力对齐，性能优化待能力追平后再启动。蓝本 docs/agent-kb/bootstrap.md "Known limitations" G1~G10）

- [ ] B-ID199〔主线〕gen1 能力对齐 C 宿主，按类修复：台账 tests/selfhost/corpus_gaps.txt（369 真缺口，每案过绿毕业进 diff_cases.txt，STALE 段勿修）。当前按类欠账：undefined value 22 案；crash 0xC0000005 110 案；reject-async 31 案（foreach/循环条件/花括号体未降级）；timeout 31 案；wrong-stdout 91 案。link-env 29 案是差分门链接环境不公平（sqlite3/SetupDi/原生驱动束/openssl），另册处理不算 gen1 缺陷。纪律：探针 → 根因 → conformance 单跑 → 自然写法（WC §10），修一类毕业一类，不无脑修。
- [ ] B-ID186 差分 Fuzzer 种子变异：`scripts/fuzz_mutate.py` 以 diff_cases 243 用例为变异源（数值边界 0/MIN/MAX/NaN/±Inf、null 注入、越界索引、try/catch 包裹剥离、整型宽度翻转）；驱动放 `_scratch/`（一次性，不进 tests）；猎物缩减最小复现 → conformance 双端金标入库 → WC §10 收口。
- [ ] B-ID187 诊断表驱动：DIAGNOSTIC_CODES.md 表格单源机器可读，C 宿主与 selfhost `Diag` 同表生成（C 侧 diag_codes.h、selfhost 侧 .zan 常量片段，生成物进提交）；已对齐 E3012/E3049/E3052/E3053/E3060/E3086 先迁作模板。
- [ ] B-ID188 reject 金标：reject_cases.txt 每用例配 `.stderr` 金标（先只锁 `code: 摘要` 两字段，容忍位置差异），run_selfhost_diff reject 分支从"rc≠0 即过"升级为"rc≠0 且 stderr 匹配金标"；前置 B-ID187（金标依赖 code 字段）。
- [ ] B-ID189 防倒退指纹（②fixedpoint.sha、③commit-msg 的 `selfhost-verified:` 机检、④check_selfhost_docs.py 已落地移出）：① gen1/gen0 `--version` 输出代次 + selfhost_sources.txt 内容 SHA256（差分报告可复现）；gen1 driver 目前无 `--version`。
- [ ] B-ID190〔独占大项〕parser 错误恢复：错误后继续 parse，服务 LSP/IDE 体验；单开计划，不与 B-ID187/188 混线。
- [ ] B-ID200〔主线〕zan-lsp 全特性对齐 C 宿主。已落地：首片（commit 7e196a4be）真诊断 + 符号索引（documentSymbol 嵌套/definition/references 词边界近似/hover/补全）；UTF-16 列口径 + 传输层字节二次编码修复（0a6f49605）；rename/prepareRename 词边界近似（跨打开文档统一改写、-32602 关键字新名，与 C 宿主 handle_rename 同形）；documentHighlight（当前文档词边界 kind=1）+ foldingRange（跨字符串/注释状态机，与 C 宿主同口径）；semanticTokens/full（真前端 Lexer 流 + 符号索引分类，legend 与 C 宿主同表 15 类，相对编码五元组，列/长度出站 UTF-16，BMP 汉字 1 unit 口径）；formatting / rangeFormatting（括号深度状态机 FmtScan 缩进 4 空格重写，全文档单范围替换 + 行范围前导空白调整，同 C 宿主口径）；inlayHint（AST Checker 推导类型 VarDecl `: int` kind=1 paddingLeft + 方法调用实参前 `param:` kind=2 paddingRight，同 C 宿主口径）。tool_lsp_zan + tool_lsp_zan_session 守门（含 CJK 双向断言、格式化断言与 InlayHint 断言）。余量：项目级符号解析（现单文档语义视图，using 指向的外部类型误报 undefined）；references/rename 语义过滤。边界决议（2026-11-10）：**C 运行时（src/runtime）与 third_party 保留不 zan 化；工具链 + 编译器缺口（B-ID199）是 zan 化主线**。
- [ ] B-ID201 zan-dap 重写为 Zan（src/dap → src/selfhost/tools/dap.zan 首切片起步）。〔前置 B-ID202〕活进程管道原语首片已落地；POSIX 管道与断点事件通道按 DAP 语义需要再补。
- [ ] B-ID202 stdlib Process 活进程管道原语。首片已落地（commit 341efe035）：ProcessSpawn（Spawn/ReadAvailable/WriteLine/Kill/WaitExitCode/Dispose/Pid，双匿名管道 + STARTF_USESTDHANDLES，Windows-first）+ unit_process_spawn（cmd.exe 交互回环守门）。余量：POSIX 管道版（Spawn 现显式返回 null）、stderr 分管道、按需的 stdin 二进制写。
- [ ] B-ID206 结构加固顺延包（2026-11-10 五项结构风险批次落地后余量，来源会话存档）：① 已落地（2026-11-10）：unity_order_check.py 补齐 check_fragment_contracts 校验器（函数体静态分析，验证 host 函数体内拼接、consumes 在宿主前置作用域且在片段内引用、defines 在片段内声明且在下游延续与后续片段消费），规范 4 个 fragment 头注释并通过 policy_unity_order 门禁；② LSP 端口发现升级：bind 预检已落地（047aa…0474aa56e），真发现 = zan-lsp bind 成功后 getsockname 回显实际端口，客户端免猜算（Socket 包 + lsp_main.c 双端）；③ 已修（2026-11-10）：去重签名改完整格式化串（vemit 量长+落盘各一次 va_copy，msgbuf 格式化同步改副本保调用方 va_list 未消费），单元钉 unit_diag_dedup::test_diag_dedup_full_string——修复前两条超 1024 字节、前 1023 字节相同尾部不同的诊断被截断串误判成一条。

### 批次 4 · 大工程 / 独占窗口（无其他 irgen/runtime 改动在飞时领）

- [ ] B-ID170 结构收口二件：① 中风险拆分（Designer 三件套：Designer.Form.zan 4301行已于 commit 3b8cdd439 按 partial 拆出 Ribbon/Layout/Free/Canvas/Ruler/Preview 6切面并完成 RenderDesignCanvas 巨函数拆解；余 Designer.zan 6909 与 Designer.Inspector.zan 3466、SceneDesigner 2951——后两者将在 B-ID177 改写为保留式，拆前先评估；App.zan 已并入 B-ID174）；② 命名空间三前缀统一（System.* 被 stdlib/packages 同名分仓、Game 裸名——独立大工程，动前定一尊；Gui 已定案并入 B-ID178）。
- [ ] B-ID178〔前置 B-ID172～B-ID177〕GUI 分层重构 P7 按层拆命名空间：目录即命名空间，codemod 原子提交（约 1,285 个调用方文件 + 977 处限定引用），兄弟包各占己名，机检切严格（承接 B-ID170 ② 的 Gui 部分）。

### 批次 4 附件 · 编译器缺陷档案与顺延（取证存档 2026-11-10）

**E3090 编译器缺陷（已修，lambda ARC 跨函数引用）**：
- 症状：`LLVM verification failed in lambda_2: Referring to an instruction in another function!` —— lambda_2 的 ARC 释放序列引用了宿主函数（LspSession.Start）的 `%box.owner` alloca。
- 根因：lambda 函数体 emit 切换函数上下文时保存了 finallys/catches/eh 等十几个清单但**漏了 lpad 栈**（src/compiler/lambda/lambda_tree.c）；宿主方法的方法级 lpad entry（emit_methods.c:216 起，含已注册 ARC/boxed 局部）滞留栈上，lambda 体内 try/finally 的 landing pad 异常传播兜底（stmt_helpers.c emit_eh_propagate_tail_lpad 对 lpad_stack[0] 的 emit_lpad_cleanups）在 lambda 函数的 builder 里 load/store 宿主 alloca → 跨函数 IR 引用。
- 修复：lambda_tree.c 切换上下文时按 finallys/catches 同款 memcpy 模式保存/恢复 lpad 栈段并清 lpad_depth；红-绿验证（回退补丁重建旧 zanc 复现 E3090，恢复后零错）+ 回归锁 tests/conformance/lambda_lpad_stack（宿主 ARC 局部 + lambda 内 try/finally 正常/异常双路径）。
- 排查工具：`ZANC_DUMP_BAD_IR=1 build/zanc.exe <src> --auto-stdlib -o out.exe` 转储坏函数 IR（_scratch/bad_<fn>.txt）；`--emit-ir` 拿不到半成品 IR（验证器先跑，失败即退出）；全局 lambda 命名计数（lambda_tree.c lambda_id）从 IR 名反查宿主函数。
- 备注：前置修复 E2002 私有 API 拦截（Socket 公共 API，commit 5245df9f3）后本缺陷才暴露。

**Gallery.zan 拆类顺延（配方已验证，待并发会话批次落地后执行）**：
- 目标形态：单类 12,188 行 → Gallery 壳（Main/导航/GalleryPack）+ `examples/gui_gallery/demos/DemoXxx.zan` 逐控件家族；RenderPreviewEx（:5240-11503，约 300 分支）保留 name 分发，分支体迁 demos：`rendered = DemoButton.Render(app, c, t, name, px, py, y, w);`
- 配方：① else-if 链花括号配平解析（闭括号是下一链头行前缀 `} else if`，body 切片不含闭括号，生成器须补）② build_gallery.ps1 加 demos/ 文件清单 ③ 先做外部引用清单（Button 家族 15 分支仅依赖 Gallery.Btn/IconBtn/LayoutAuto/PreviewPadTop/ShowAt/evtClicks/popTipOn + px/py 参数，无 rendered 写点）④ 逐家族编译推进，每家族一提交。
- 硬门：生成物必须过花括号配平断言（Gallery.zan 与 DemoXxx.zan 各自 `{`/`}` 计数为 0）再编译——2026-11-10 首迁 Button 家族的生成物是坏中间态（Gallery.zan -15、DemoButton.zan -13，分支头重复嵌套、链闭括号错位），已整体回退不入库。
- 顺延原因：并发会话活跃于 templates/packages/examples 区域，避免手工冲突（WC §11）。

**模板与根目录清理顺延**：templates/server/server-collab/ 的 app.db-wal、e2e_*.log、m4.log、*.pyc 已提交残留；根目录未跟踪杂物（zan_leaks.log、CMakeCache.txt、Testing/）。均在并发会话活跃区，下轮收口。
