# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。领账按批次：批次 1 根修优先（闭一条解锁一片），批次 2 独立账可并行，批次 3 等前置闭了再领，批次 4 大工程须独占窗口；同批内按编号序。每条正文前的〔…〕是依赖标签：〔前置 X〕= X 不闭本条闭不了；〔软前置 X〕= 没 X 也能做但成本高；〔根修·解锁 Y〕= 本条闭了 Y 的前置随之解除；〔串行前置 X〕= 与 X 同改一个文件，等 X 落地再动。根修闭账时同批摘掉下游条目的〔前置〕标签并上调批次。领账前 `git status` + 文件 mtime 查在飞——并发会话不一定走调度器注册（2026-11-03 B-ID107 即靠 mtime 抓到撞车）。
>
> 2026-11-05 平账：移出已闭账 [x] 十条（B-ID149/150/151/154/156/157/158、B-ID159 窗口声明面纯正典标签化三刀全量闭环，以及批次 4 独占大工程 B-ID103 irgen 编译单元解耦与 B-ID71 LandingPad 零开销异常系统重构落地），结论与验证证据由对应提交正文承担。
>
## 未完成（按平账顺序分批）

### 批次 0 · 在飞（持有会话未提交；其他会话勿碰同文件）

- [~] 在飞观察（2026-11-06）：工作区约 177 个文件未提交——JsonValue→JsonWriter 迁移（Zan.Ai/Zan.Json/Zan.Mvc/Zan.Net/ide_zan services+dialogs/templates 全家）+ IDE LSP 模型调整（LspRange/LspTextEdit 新增、dap/lsp 旧模型删除）+ `src/compiler/irgen_async.c` 改动 + 新增 conformance（json_serializer_dict）。持有会话未走调度器注册；本轮全量整改（B-ID163-166）已按黑名单绕开这些文件。
- [~] 在飞观察（2026-11-06 补）：ChildWindow 声明式装配——z-mount 挂载（`DocWire.MountSlots`）、`:prop` 绑定启动期对账、`diag_cw_bad_mount` 注册；涉及 Zan.Gui 9 个文件（UiDriver/ChildWindow/Control/ControlExtra/DocWire/ModelBinder/Html/Progress/win-x64 libzan_gui.a）与 CMakeLists.txt，持有会话未注册调度器。GUI 分层重构（B-ID171+）已绕开这些文件。
- [~] 在飞观察（2026-11-09 补）：游戏域拆包——`Zan.Game` 收缩为引擎核心（Core/Graphics/Kit/Scene/Physics 等），`Zan.Game.Arpg`/`Zan.Game.Idle` 等类目整包迁出为独立包（工作区未提交 D/?? 状态）；gui-design/game-dev skill 也有同日未提交改动（Dock 方言清理方向）。持有会话未注册调度器；B-ID177 的游戏域迁移待拆包收口后执行。

### 批次 1 · 根修项（闭它解锁下游，优先领）

- [x] B-ID163 编译器 fail-loud 四收口（本轮整改，C 宿主，commit 91ef9e8bd）：① 同步 lambda 赋 async 委托发 E3104（irgen_expr_lambda.c）；② 擦除体 new T() 硬失败（irgen_expr_construct.c）；③ (T)x 强转/拆箱失败无条件抛 InvalidCastException，stdlib 补异常类（irgen_expr_construct.c）；④ as 表达式对 string/object/interface 补运行时类型检查。测试集 42/42 全绿。
- [x] B-ID160 publish 反射 thunk 门控使框架字符串钩子哑火：`irgen_reflect.c` 完善 `refl_is_framework_hook_name` 原生识别 `On*` 框架生命周期钩子规范（OnLoad/OnInit 等），need_thunk 门控及 refl_method_should_export 与 irgen_emit root 判定全链路放行，解决 HasMethod 为真而 Invoke 哑火问题，无需手动 `[Custom]` 注解。用例 case_reflect_publish_hook 与 cwpublish 验证全绿。

### 批次 2 · 独立账（无前置，任一会话可领，按编号序）

- [x] B-ID161 examples/html_gallery 源级损坏修复与纳入模板门：补齐 Page0-7 控件字段与 zan.proj，规范主窗声明式 Form 代码，补齐 ControlBootstrap 与 TagToKind 对 Typography/IconView/RadioButton/ListItem/Upload 等的映射与组件工厂自注册，更新 tests/run_templates.cmake 与 CMakeLists.txt templates_build 测试集成，实机自检 86 组控件自检全部通过（fails=0 of 86）。
- [x] B-ID164 SSE 流式解析四份平行实现收拢（commit bbf290694）：新建 `packages/Zan.Net/src/System/Net/Sse/SseLineReader.zan`，AiStream 与 Mvc.Ai 全量收敛改用；ide_zan/SseLive 待在飞迁移落地后改用。
- [x] B-ID165 小工具重复收口（commit e833ababa）：新建 `stdlib/System/Text/TextUtil.zan` 提供空安全 TrimOr/TrimOrEmpty/StartsWithOr；TaskScheduler 与 ServiceProcess 消除手写 O(n) 空白剥离；Arpg 家族/Ansi/ChartTimeline 统一收拢至 MathUtil.Clamp。
- [x] B-ID166 超长文件 partial 拆分第一批 + 巨函数拆解：
  - Chart.zan (3170行) -> Scale/Extent/Label/Keys/Breaks 5切面 (commit 59e78833c)
  - ChartOption.Json FromJsonValue (2419行) -> 15个组件方法分解，瘦身至149行 (commit f7a224ca4)
  - Collection.zan (2626行) -> Codec/Fields/Crud/Index/Scan/Search 6切面 (commit d5d699a1b)
  - StyleSheet.zan (3445行) -> Lint/Decl/Effect/Color/Calc 5切面 (commit 375bdf71d)
  - Win32Shell.zan (3085行) -> Imports/Icon/WndProc/Present/Window/Events 6切面 (commit 7eacc4d57)
  - CodeEditor.Completion.zan (2826行) -> Match/Refs/Members/Project/Fixup 5切面 (commit 250e62ba7)
  所有新拆出文件严格 ≤ 1200 行门禁，conformance/gui 测试全绿通过。
- [~] B-ID171 GUI 分层重构 P0 安全网与护栏（`docs/GUI_ARCHITECTURE.md` §4，后续 B-ID172～179 同）：① 计划落盘（立项即落）；② 补注册未登记 GUI 用例 + 布局树金标用例（P5 等价门）〔串行前置：批次 0 在飞 CMakeLists.txt〕；③ `scripts/check_code_standards.py` 层级棘轮 + 基线（已落 eb289a6bd）；④ 清死 using（实测 23→5，2026-11-09）与文档漂移（zan.pkg、README、agent-kb、gui-design skill）；层级机检补 static 可变字段棘轮（Gui.Core/Widget/Component，2026-11-09 裁定）；gui-design/game-dev skill 画布例外条款改写与三副本同步——有并发会话在飞未提交改动，待其收口后同提交执行。
- [ ] B-ID172 GUI 分层重构 P1 断包环、按目标层摆目录：① 控件注册梯子退役（2026-11-09 用户裁定 `Install()` 类注册方案属不可行历史方案，移除；原 HmiBootstrap 外移方案作废）——替代机制先出小设计：候选为编译器生成控件清单（类型在编译图内即自动可按 kind 构造、miss 显式报错，替代 ControlBootstrap/DataTableBootstrap/ChartBootstrap/CefControlBootstrap 全部手工注册），ControlBootstrap 删 `using Gui.Hmi` 并入该机制；② 新建 Forms/Binding/Testing 目录、基元移到 src/Gui 根（命名空间不变）〔串行前置：批次 0 在飞 ChildWindow/DocWire/ModelBinder/UiDriver〕；③ ControlBootstrap 移入 Widget/。

### 批次 3 · 有前置（前置闭了再领）

- [ ] B-ID167〔前置：批次 0 在飞 JsonValue→JsonWriter 迁移落地〕在飞文件上的整改顺延包：① `AiClient.zan:74-76` 三参构造器漏初始化 `clients` 字段（经此构造的客户端一调 Dispose 即空引用；头部 `: this(new AiConfig(...))` 链修复）；② AiClient/AnthropicClient 提炼 AiHttp（PostJson 36 行逐行同、客户端缓存、错误提取）；③ ide_zan SseLive.zan 改用 SseLineReader 只留业务增量；④ ide_zan dialogs `T(lang,en,zh)`×8 收拢 Lang.Tr + Window `Get/GetInt`×4 提炼 ChildWindow 基类；⑤ JsonWriter 17 个单行别名三套命名收敛（动公共 API 先全仓 grep 消费方）。
- [ ] B-ID173〔前置 B-ID172〕GUI 分层重构 P2 绑定收敛：PropSpec 标默认绑定属性、ModelBinder 删 10 处 `is X` 特判；模型游标接口统一 POCO/JsonValue/JsonDoc/字典取值；ChildWindow 绑定泵并入 ModelHosts；IDE 里当应用状态用的 Signal 改字段 + `Binding<T>`。
- [ ] B-ID174〔前置 B-ID172〕GUI 分层重构 P3 拆 App：颜色计算、BackdropPainter、DamageTracker、FrameLoop、WindowHost、HtmlLoader、InputArbiter、OverlayHost 骨架逐项抽出，目标 App.zan < 1000 行（承接 B-ID170 ① 的 App.zan 部分）。
- [ ] B-ID175〔前置 B-ID174〕GUI 分层重构 P4 统一浮层：OverlayHost 分层结算命中与原生遮挡；OverlayPopup 下沉 Widget；SelectBox/DatePicker/ColorPicker/Popover/菜单改用共享件；模态合一。
- [ ] B-ID176〔前置 B-ID171 ②〕GUI 分层重构 P5 布局方言退役：删 Serialize.zan 与 Alert 别名（Serialize/Dock 引擎已删，2026-11-09 实测）；补齐 CSS grid/absolute；解析期脱糖 data-dock/data-fx 后删兜底（带旧属性的设计器文档实测 43 个，文件改写并入 B-ID179）。
- [ ] B-ID177〔前置 B-ID175〕GUI 分层重构 P6 即时模式与静态状态下线（2026-11-09 裁定扩为"全部统一"，画布例外取消）：路由事件转正 → 控件分批改事件驱动 → 调用方迁移（设计器预览离屏、gui_gallery 并入 html_gallery、Charts/DataTable/CodeEditor/Industrial 整体改写范式二控件——OnPaint 仅内部实现、游戏 HUD 视图与 SceneDesigner 面板纳入）→ 删 `Render(App` 入口、InputArbiter、Ui 轮询族与静态可变状态（WidgetId 全局栈、OverlayPopup static occl*/lastRichHover、Layer 静态打开态队列）；Mat4/Mesh3D 外迁 Zan.Game 引擎核心。
- [ ] B-ID179〔前置 B-ID176〕GUI 分层重构 P8 设计器改产出 CSS，脚本改写 42 个设计器文档（含 IDE 面板），删 P5 脱糖兜底（P5 完成后单独出计划）。

### 批次 5 · 自举工业化（2026-11-07 立项：自举闭环已达 L5 固定点，转入验证面扩容与诊断工程；蓝本见 docs/agent-kb/bootstrap.md "Known limitations" 缺口索引 G1~G10）

- [ ] B-ID184 全仓编译门 `selfhost_repo`：gen1 编译面从 230 手选用例扩到 stdlib/packages/templates/examples 全量 `.zan`；首轮盘点失败项记 `tests/selfhost/repo_known_gaps.txt`（与 diff_known_gaps.txt 同模式白名单记账），门按"白名单外全绿"判；新增 `tests/run_selfhost_repo.cmake` + CMakeLists 注册。
- [ ] B-ID185 IR 规范化差分 `selfhost_ir_diff`：`scripts/normalize_ir.py` 归一化两侧 IR（gen0 走 LLVM C API、gen1 手写文本——label/SS 重编号、metadata 剥离、属性排序）；先 `_scratch/` 对 diff_cases 230 用例盘点剩余差异形态定白名单规则，再落 `tests/run_selfhost_ir_diff.cmake`，白名单 `tests/selfhost/ir_diff_known_gaps.txt` 记账、长期目标清零。目标：抓 B-ID180 型"stdout 碰巧一致、发射路径已分叉"。
- [ ] B-ID186 差分 Fuzzer 种子变异：`scripts/fuzz_mutate.py` 以 diff_cases 230 用例为变异源（数值边界 0/MIN/MAX/NaN/±Inf、null 注入、越界索引、try/catch 包裹剥离、整型宽度翻转）；驱动放 `_scratch/`（一次性，不进 tests）；猎物缩减最小复现 → conformance 双端金标入库 → 按 WC §10 探针→根因→单跑→自然写法收口。
- [ ] B-ID187 诊断表驱动：DIAGNOSTIC_CODES.md 表格单源机器可读，C 宿主与 selfhost `Diag` 同表生成（C 侧 diag_codes.h、selfhost 侧 .zan 常量片段，生成物进提交）；已对齐 E3012/E3049/E3052/E3053/E3060/E3086 先迁作模板。
- [ ] B-ID188 reject 金标：reject_cases.txt 每用例配 `.stderr` 金标（先只锁 `code: 摘要` 两字段，容忍位置差异），run_selfhost_diff reject 分支从"rc≠0 即过"升级为"rc≠0 且 stderr 匹配金标"；前置 B-ID187（金标依赖 code 字段）。
- [ ] B-ID189 防倒退指纹：① gen1/gen0 `--version` 输出代次 + selfhost_sources.txt 内容 SHA256（差分报告可复现）；② run_fixedpoint 把 g2 SHA256 写 `build/fixedpoint.sha`、bootstrap.md 维护最近固定点 SHA；③ pre-commit 增检：diff 触及 src/selfhost/** 或 src/compiler/** 时提交说明须含 `selfhost-verified:` 标记（只机检标记存在，真跑仍按 WC §8 定向执行）；④ `scripts/check_selfhost_docs.py` 机检 selfhost_sources.txt 与 bootstrap.md 文件表的存在性。
- [ ] B-ID190〔独占大项〕parser 错误恢复：错误后继续 parse，服务 LSP/IDE 体验；单开计划，不与 B-ID187/188 混线。
- [x] B-ID191 G8 第一轮 IR 发射消减：静态全常量数组折叠——`static T[] f = new T[]{…}`（一维、8 字节整数槽、全字面量元素）在 EmitStaticFieldInits 折叠为 `internal global [N+2 x i64]`（保持 [pad][len][elements] bundle 布局、槽指元素基址、可变 global 支持运行期变异），main 里逐元素 GEP+store 展开退役；GenNew 数组初始化器元素 0 复用基址 GEP（ToArray/StrToBytes 核实为动态索引循环无静态重复）。实测 arithmetic 用例 IR 83990→52883 行、main 31177→63 行，fixedpoint g2.ll 42182181→41102400 字节。新增 conformance/static_const_array.zan（读回/foreach/变异/bool-char-byte-double 混合，双端金标），selfhost_gen1、selfhost_ir_golden（231 金标 BLESS 同提交）、selfhost_diff（全案+reject）、selfhost_fixed_point（g2==g3 逐字节，SHA 6f2ffc3a…）四门全绿。double/string/byte 元素折叠、复合赋值双寻址、~50k 行运行时垫片留下轮。
- [x] B-ID192 多行字符串换行归一（预存缺陷，差分门首红暴露）：gen1 的 Verbatim/InterpVerbatim 把源文件原始 CRLF 直接收进字符串值——CRLF 源文件上 `@"line 1
line 2"` 与 `$@"..."` 的字符串内容带 \r\n，WriteLine 后输出 \r\r\n，与 C 宿主（按 C# 规范把多行字符串内换行归一为 \n）逐字节分歧；三引号 Raw() 本已有正确归一先例，Verbatim/InterpVerbatim 缺同款。修复：两处 CR 分支按 Raw() 同款归一。用 verbatim_interpolated_string 双端差分实证（pre-G8 gen1 与 HEAD gen1 输出逐字节相同证实与 B-ID191 无关，系该用例入册后门首次跑红被串行版 tail 截断掩盖）。ir_golden 231 金标重 BLESS，selfhost_diff（231 案 0 缺口 49 拒绝 0 失败）、selfhost_gen1、selfhost_fixed_point（g2==g3）三门全绿。
- [x] B-ID193 selfhost_diff 门并行化：CMake 脚本模式无作业控制，230 案串行 clang 链接是全门墙钟大头（1039s）。新增 scripts/run_diff_parallel.py 线程池驱动（emit→link→run→归一化 stdout 对比、GAP 报告不失败、reject fail-closed + 双编译器措辞锁定语义逐条保留；syslibs 按平台内置，绕开 cmake→python 的分号实参边界），run_selfhost_diff.cmake 收缩为 gen1 构建 + 委托 + 退出码边界，ctest 注册零改动。全门实测 1039s→415s（-60%），231 案结果与串行版逐字一致。
- [x] B-ID194 G8 第二轮 IR 消减（门提速 + 全模块 DCE）：① gen1 门构建走 zanc -O0 fast_codegen 路径（LLVMCodeGenLevelNone + 跳过 optimize 趟），gen1 构建 131s→8.5s，发射 IR 逐字节不变；② 新增 src/selfhost/irgen/irgen_prune.zan 全模块死代码剪枝——LLVM IR 调用边全是文本级 @name，从 @main + 全局初始化器（vtable/函数指针表）+ 弱协程符号集做传递闭包，不可达 define 整块丢弃（漏边由链接器 LNK2019 兜底，非静默）；--emit-lib 跳过（导出 API 外部引用）。arithmetic 用例 IR 52883→2673 行（-95%，stdlib 整文件 demand-pull 带入的死方法 Sha1/Base64/QuickSort/Unicode 大表全剪）；自举编译器自身调用面广，g2.ll 41.1MB→38.6MB（-6%）。高风险面（async/反射/虚分派/lambda/泛型/Task 句柄）12 案探针全过。门套件实测：gen1 117→9.2s、diff 415→331s、fixed_point 185→100s、ir_golden 757→681s，四门全绿（g2==g3 逐字节，SHA 2a56422d）。optimize 趟缩放定性为线性（88.5k 指令 1.1s / 1.74M 指令 27s，n^1.1），门侧已由 -O0 绕开，真正提速需 profiler 火焰图留下轮。
- [x] B-ID195〔能力对齐主线立项〕全语料差分 sweep：231 案差分门是幸存者集合（gen1 对着它长出来的）。用 scripts/run_diff_parallel.py（补 per-case 超时护栏：emit 120s/link 180s/run 5s，未入册语料的误编译死循环曾 24 路并发放大成数十 GB 内存）全量扫 tests/conformance 1049 个 C 宿主金标用例：gen1 通过 500；549 失败中经 C 宿主当日复编对账，382 个为真缺口、167 个为语料陈旧（C 宿主今日同样拒绝——Gui 包重构中间态等，金标过期）。真缺口分类：illegal-ir 98（含 strcasecmp 缺 Windows CRT 垫片、%tN ptr/i64 擦除边界、undefined value '%this'）、reject 31（async 在 foreach/循环条件/花括号体未降级）、wrong-stdout 91、crash 110、timeout 31、stackoverflow 2、other 19。对"今日可编译语料"覆盖率 500/882=56.7%。台账 tests/selfhost/corpus_gaps.txt（机器可引用，每案过绿即毕业进 diff_cases.txt）。预存缺陷实证：@WebApp_time 类"只发调用不发定义"在 DCE 前的 gen1 同样存在，与 B-ID194 剪枝无关。能力对齐优先于性能优化（B-ID196+ 按类修复）。
- [x] B-ID197 illegal-ir 第二批：① GenUnary `!` 与三元条件类型感知化（原硬编码 icmp eq i64，`!SomeAsync()` 对协程帧指针 i8* 被 clang 拒，对齐 GenCond 既有口径：指针比 null、double 走 fcmp）；② foreach 枚举元素去掉对派发结果的二次 FromSlot（派发已在边界做擦除→具体转换，二次 inttoptr 对已是 i8* 的寄存器非法）；③ 三处派发结果返回统一擦除转换规则（IsErasedTypeDecl 或 rt==i64 且返回类型提及类型参数 → FromSlot+ConcreteTP）。净效果：foreach_iface_enumerator/generic_iface_foreach 转绿；collections_foreach 触碰 G9 深水区（泛型 GetEnumerator 返回类型丢失类型实参，ConcreteTP 无法从无实参的接收者类型映射 T）——如实移回缺口台账标注 G9，它是 `List<List<int>>` 同族缺口。四门全绿（diff 243 案 93s、fixed_point g2==g3、gen1 9s）。

- [x] B-ID196 能力对齐第一仗：illegal-ir 类 98 案修绿 13 案毕业进差分门。五项修复：① strcasecmp weak 垫片（MSVC CRT 无 POSIX strcasecmp，gen1 发自带 ASCII 不区分大小写比较，CRT 有则让位；bool.Parse/Parse 簇 4 案转绿）；② 派发兜底 dummyVal 凡指针类型发 null（原只认 i8*，类指针槽发整数 0 被 clang 拒）；③ BitOperations.ReverseEndianness 补 llvm.bswap.i32/i64 内联（原漏表漏到裸名调用）；④ checker 把 var 推断类型写回 AST（async 预扫描快照槽类型时 var 全记 int，`var d=1.5` 帧槽存 double 报 floating point constant invalid）；⑤ Coerce 补 double→i64 位型边界。对账修正：剩余 85 案中 29 个是链接环境问题（sqlite3/SetupDi/原生驱动束/openssl 符号，差分门只给 8 个 runtime obj 的不公平链接，另册处理），真编译缺陷约 53 案（ptr/i64 擦除 27、undefined value 22 等）。四门全绿：diff 244 案 80s、fixed_point g2==g3、gen1 8s。


### 批次 4 · 大工程 / 独占窗口（无其他 irgen/runtime 改动在飞时领）

- [x] B-ID169 Convert/Parse 异常抛出与自举孪生对齐：全仓审计 345 处调用方证实绝大多数依赖非 try 默认安全兜底，C 宿主原有 try 内部 emit_runtime_check 异常分发路径成立；修复自举端 irgen_expr_member.zan 在 @__exc_sp > 1（进入用户级 try 块）时经由 EmitExceptionThrow 抛出 FormatException/OverflowException，非 try 作用域保持 stderr soft note + 0 兜底。内置异常类描述符在 binder/checker/irgen/irgen_expr_assign 注册并对齐 tid_Exception 继承链。新增 conformance/convert_invalid_catch.zan，selfhost_gen1、selfhost_fixed_point（37.9MB 逐字节对齐）、selfhost_diff（213通过/0已知缺口/50拒绝）三门自举全体验证通过。
- [x] B-ID180 浮点→整型全通道 poison/UB 收口（C 宿主 + 自举孪生）：裸 fptosi 对 NaN/±Inf/越界值产生 LLVM poison——x86 得 0x8000…、ARM64 饱和、WASM trap，三平台三种结果且违反 SECURITY.md §1「无 UB」承诺，try 内毫无异常分发（探针实测越界值打印 -0 垃圾）。C 宿主新增 emit_fptosi_guarded（irgen_generics.c：范围谓词 [-2^63,2^63)（NaN 两比较全 false 自动落网）→ emit_runtime_check（try 内抛 OverflowException，try 外 soft 报告）→ in_range 归 0.0 + clamp（上越界→INT64_MAX、下越界→INT64_MIN、NaN→0）→ fptosi 输入必定义），接入 cast/三元统一/Convert.ToDouble 族/反射 pget·ld·SetField 六调用点（select 内嵌两处带 is_fp 臂掩码防整型槽位型重释假守卫）；自举孪生 EmitFptosiGuarded（irgen_expr_member.zan）接入 cast/Convert/GetFieldInt 三处。窄目标域外值由 poison 变定义值（截断），域内值逐位不变。新增 conformance/fp2int_overflow 双端金标 + diff_cases.txt 登记，case_fp2int_overflow、cast/convert/checked 定向回归 4/4 绿，selfhost_gen1、selfhost_fixed_point（g2==g3 逐字节）、selfhost_diff 全门通过。
- [x] B-ID181 自举端整数除法守卫双轨对齐（try 内 DivideByZeroException + 商归一化）：自举 GuardDivisor 曾直调 zan_rt_guard_fail3 只报告不抛——try{1/0}catch(DivideByZeroException) 在 C 宿主可捕获、自举端静默吞掉继续跑（B-ID169 同款口径偏差的除法版）；且自举端只把除数 select 为 1 不替换商——零除商=被除数本身、MIN/-1 商=MIN，而 C 宿主 divz.res/remz.res 把 unsafe 商归一化为 0。改 GuardDivisor 走 EmitRuntimeCheckIf（try 内抛/try 外 soft 报告双轨），divUnsafe 字段记录 unsafe 谓词，binary 与复合赋值两调用点 sdiv/srem 后补 select 商 0。新增 conformance/div_zero_catch 双端金标（binary+assign 两通道、long、MIN/-1、负除数截断回归 14 行），gen1 实测 STDOUT 逐字节一致，case_div_zero_catch 与 selfhost 三门全绿。
- [x] B-ID182 自举端字符串索引守卫收口（null NRE + 越界 IndexOutOfRange + 钳 0）：自举 GenIndex 字符串分支是零页 select + 裸 GEP——null 无报告无抛、越界读越界内存（真实 OOB read），且过时注释声称宿主仅 soft；探针实测宿主 irgen_expr_access.c:1275 走 emit_string_elem_guard / emit_index_bounds_check：try 内 null 抛 NullReferenceException、负或 >= len 抛 IndexOutOfRangeException，try 外 soft 报告 + 索引钳 0（越界读返回 0 号字符）。自举端补齐同口径：null EmitRuntimeCheckIf + 零页 select、zstr_len（头 -16 精确长）上下界判定 + EmitRuntimeCheckIf、oob 索引钳 0。新增 conformance/str_index_guard 双端金标（越界/负/null/try 内分发/域内回归 5 行），gen1 实测 STDOUT 逐字节一致，case_str_index_guard 与 selfhost 三门全绿。
- [x] B-ID183 数组索引 null/越界双端收口（宿主段错误根治）：探针实测 C 宿主 null 数组 t[0] 在 try 内也直接段错误——一维分支（irgen_expr_access.c:1300）只查越界不查 null（zan_array_len 解引用 null 头），字符串分支有 base guard 数组分支漏配；自举端同点位裸 GEP——越界读越界内存（负索引读到数组头垃圾）、null 无报告。C 宿主一维 + 多维（emit_mdarray_elem_ptr 同款面）补 null 守卫（emit_runtime_check + emit_soft_base_select 落 scratch 页，len 读 0 → 越界钳 0 → 读 0，try 内抛 NRE）；自举端一维数组分支补 null EmitRuntimeCheckIf + 零页 select、oob 索引钳 0，数组 .Length 同步走 null 安全探针（多维 MdFlatIndex 折算面顺延同模式）。diff 门首跑暴露自举端 len 探针缺陷：null 软报告后换入的 @.zeropage 无头，对选中基址读 -8/-16 槽位读到零页前面的内存——字符串分支 zstr_len magic 不符 abort（null_string_index rc=127、stdout 全丢），数组分支读垃圾 len。修复 = len 探针一律走原始基址的 null 安全助手：字符串 zstr_len(null→0)（并发会话同文件修复）、数组新增 EmitArrLenRuntime @zarr_len（null→宿主 scratch 哨兵 1<<62，与宿主 .Length 软失败口径逐字对齐）。conformance/arr_index_guard 扩展为 7 行双端金标（越界/负/null try 内分发/try 外 null 读 0 + Length 哨兵/域内回归）+ diff_cases.txt 登记，null_string_index/str_index_guard gen1 端 STDOUT 逐字节一致 rc=0，case_arr_index_guard 等三索引用例与 selfhost 三门全绿。
- [ ] B-ID170 结构收口三件：① 中风险拆分（Designer 三件套：Designer.Form.zan 4301行已于 commit 3b8cdd439 按 partial 拆出 Ribbon/Layout/Free/Canvas/Ruler/Preview 6切面并完成 RenderDesignCanvas 巨函数拆解；余 Designer.zan 6909 与 Designer.Inspector.zan 3466、SceneDesigner 2951——后两者将在 B-ID177 改写为保留式，拆前先评估；App.zan 已并入 B-ID174）；② 命名空间三前缀统一（System.* 被 stdlib/packages 同名分仓、Game 裸名——独立大工程，动前定一尊；Gui 已定案并入 B-ID178）；③ [x] zan.pkg / zan.json 支持 deps 顶层/数组与约束解析，zan_pkg_fetch 补齐本地目录及 monorepo 回退，闭环 zan-pm restore 下载与 sha256 树校验（unit_zan_pm_cli 覆盖端到端 12 步验证全过）。

- [ ] B-ID178〔前置 B-ID172～B-ID177〕GUI 分层重构 P7 按层拆命名空间：目录即命名空间，codemod 原子提交（约 1,285 个调用方文件 + 977 处限定引用），兄弟包各占己名，机检切严格（承接 B-ID170 ② 的 Gui 部分）。

- [ ] B-ID152〔裁定搁置〕ZanDB 刀8 段 zstd 压缩：stdlib 无压缩绑定，需引入外部库链接与三平台分发——与自包含分发架构相抵；且压缩块缓存仍需解压驻留，对 2G 内存机目标无益。数据面收益已被 tag 11 行编码（4×↓）替代大半，除非出现大文本字段压缩需求，不领。
- [ ] B-ID153〔裁定搁置〕ZanDB 刀9 SIMD 距离内核：运行时只有 CPU 特性探测（System.Runtime.Intrinsics.Cpu）无向量算术内建，落地须编译器/运行时新增 AVX2/NEON 内建——超出 ZanDB 数据面范围。评估前置已满足：发布档基线实测 approx 2ms/20k 库（debug 9.9ms，约 5×，见 ZanDb README 量化检索实测），而真实查询延迟由重打分文档行点查主导（12ms，IO 不随构建档变），SIMD 对端到端收益被摊薄；除非出现纯 ADC 高 QPS 场景，不领。
- [ ] B-ID155〔裁定搁置〕zanc 编译缓存重做：旧缓存架构因产物散落源码树、无界堆积成巨量垃圾文件被移除；每次全量重编（GUI 程序 726 文件，Zan.Gui 包 387 文件/23MB 源码）是其接受代价，不是遗漏。重提必须先满足四约束：产物只进单一 git-ignored 固定目录、容量有界（LRU 封顶驱逐）、内容哈希键失效（杜绝陈旧产物）、带一键清理入口。实测 hello+stdlib 30 文件峰值 18MB——进程基线轻，内存大头是包编译面；包缓存收益随 Gui 包肥大持续增长。
