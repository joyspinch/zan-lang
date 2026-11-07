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
- [~] B-ID171 GUI 分层重构 P0 安全网与护栏（`docs/GUI_ARCHITECTURE.md` §4，后续 B-ID172～179 同）：① 计划落盘（立项即落）；② 补注册未登记 GUI 用例 + 布局树金标用例（P5 等价门）〔串行前置：批次 0 在飞 CMakeLists.txt〕；③ `scripts/check_code_standards.py` 层级棘轮 + 基线（已落 eb289a6bd）；④ 清死 using（Core 23、Widget 7；在飞的 Control.zan/ChildWindow.zan 顺延）与文档漂移（zan.pkg、README、agent-kb、gui-design skill）。
- [ ] B-ID172 GUI 分层重构 P1 断包环、按目标层摆目录：① HMI 注册移入 Zan.Industrial `HmiBootstrap`，ControlBootstrap 删 `using Gui.Hmi`；② 新建 Forms/Binding/Testing 目录、基元移到 src/Gui 根（命名空间不变）〔串行前置：批次 0 在飞 ChildWindow/DocWire/ModelBinder/UiDriver〕；③ ControlBootstrap 移入 Widget/。

### 批次 3 · 有前置（前置闭了再领）

- [ ] B-ID167〔前置：批次 0 在飞 JsonValue→JsonWriter 迁移落地〕在飞文件上的整改顺延包：① `AiClient.zan:74-76` 三参构造器漏初始化 `clients` 字段（经此构造的客户端一调 Dispose 即空引用；头部 `: this(new AiConfig(...))` 链修复）；② AiClient/AnthropicClient 提炼 AiHttp（PostJson 36 行逐行同、客户端缓存、错误提取）；③ ide_zan SseLive.zan 改用 SseLineReader 只留业务增量；④ ide_zan dialogs `T(lang,en,zh)`×8 收拢 Lang.Tr + Window `Get/GetInt`×4 提炼 ChildWindow 基类；⑤ JsonWriter 17 个单行别名三套命名收敛（动公共 API 先全仓 grep 消费方）。
- [ ] B-ID173〔前置 B-ID172〕GUI 分层重构 P2 绑定收敛：PropSpec 标默认绑定属性、ModelBinder 删 10 处 `is X` 特判；模型游标接口统一 POCO/JsonValue/JsonDoc/字典取值；ChildWindow 绑定泵并入 ModelHosts；IDE 里当应用状态用的 Signal 改字段 + `Binding<T>`。
- [ ] B-ID174〔前置 B-ID172〕GUI 分层重构 P3 拆 App：颜色计算、BackdropPainter、DamageTracker、FrameLoop、WindowHost、HtmlLoader、InputArbiter、OverlayHost 骨架逐项抽出，目标 App.zan < 1000 行（承接 B-ID170 ① 的 App.zan 部分）。
- [ ] B-ID175〔前置 B-ID174〕GUI 分层重构 P4 统一浮层：OverlayHost 分层结算命中与原生遮挡；OverlayPopup 下沉 Widget；SelectBox/DatePicker/ColorPicker/Popover/菜单改用共享件；模态合一。
- [ ] B-ID176〔前置 B-ID171 ②〕GUI 分层重构 P5 布局方言退役：删 Serialize.zan 与 Alert 别名；补齐 CSS grid/absolute；解析期脱糖 data-dock/data-fx 后删 Dock 引擎（带旧属性的 42 个 html 全是设计器文档，文件改写并入 B-ID179）。
- [ ] B-ID177〔前置 B-ID175〕GUI 分层重构 P6 即时模式下线：路由事件转正 → 控件分批改事件驱动 → 514 处调用方迁移（设计器预览离屏、gui_gallery 并入 html_gallery 等）→ 删 `Render(App` 入口与 InputArbiter。
- [ ] B-ID179〔前置 B-ID176〕GUI 分层重构 P8 设计器改产出 CSS，脚本改写 42 个设计器文档（含 IDE 面板），删 P5 脱糖兜底（P5 完成后单独出计划）。

### 批次 4 · 大工程 / 独占窗口（无其他 irgen/runtime 改动在飞时领）

- [x] B-ID169 Convert/Parse 异常抛出与自举孪生对齐：全仓审计 345 处调用方证实绝大多数依赖非 try 默认安全兜底，C 宿主原有 try 内部 emit_runtime_check 异常分发路径成立；修复自举端 irgen_expr_member.zan 在 @__exc_sp > 1（进入用户级 try 块）时经由 EmitExceptionThrow 抛出 FormatException/OverflowException，非 try 作用域保持 stderr soft note + 0 兜底。内置异常类描述符在 binder/checker/irgen/irgen_expr_assign 注册并对齐 tid_Exception 继承链。新增 conformance/convert_invalid_catch.zan，selfhost_gen1、selfhost_fixed_point（37.9MB 逐字节对齐）、selfhost_diff（213通过/0已知缺口/50拒绝）三门自举全体验证通过。
- [x] B-ID180 浮点→整型全通道 poison/UB 收口（C 宿主 + 自举孪生）：裸 fptosi 对 NaN/±Inf/越界值产生 LLVM poison——x86 得 0x8000…、ARM64 饱和、WASM trap，三平台三种结果且违反 SECURITY.md §1「无 UB」承诺，try 内毫无异常分发（探针实测越界值打印 -0 垃圾）。C 宿主新增 emit_fptosi_guarded（irgen_generics.c：范围谓词 [-2^63,2^63)（NaN 两比较全 false 自动落网）→ emit_runtime_check（try 内抛 OverflowException，try 外 soft 报告）→ in_range 归 0.0 + clamp（上越界→INT64_MAX、下越界→INT64_MIN、NaN→0）→ fptosi 输入必定义），接入 cast/三元统一/Convert.ToDouble 族/反射 pget·ld·SetField 六调用点（select 内嵌两处带 is_fp 臂掩码防整型槽位型重释假守卫）；自举孪生 EmitFptosiGuarded（irgen_expr_member.zan）接入 cast/Convert/GetFieldInt 三处。窄目标域外值由 poison 变定义值（截断），域内值逐位不变。新增 conformance/fp2int_overflow 双端金标 + diff_cases.txt 登记，case_fp2int_overflow、cast/convert/checked 定向回归 4/4 绿，selfhost_gen1、selfhost_fixed_point（g2==g3 逐字节）、selfhost_diff 全门通过。
- [x] B-ID181 自举端整数除法守卫双轨对齐（try 内 DivideByZeroException + 商归一化）：自举 GuardDivisor 曾直调 zan_rt_guard_fail3 只报告不抛——try{1/0}catch(DivideByZeroException) 在 C 宿主可捕获、自举端静默吞掉继续跑（B-ID169 同款口径偏差的除法版）；且自举端只把除数 select 为 1 不替换商——零除商=被除数本身、MIN/-1 商=MIN，而 C 宿主 divz.res/remz.res 把 unsafe 商归一化为 0。改 GuardDivisor 走 EmitRuntimeCheckIf（try 内抛/try 外 soft 报告双轨），divUnsafe 字段记录 unsafe 谓词，binary 与复合赋值两调用点 sdiv/srem 后补 select 商 0。新增 conformance/div_zero_catch 双端金标（binary+assign 两通道、long、MIN/-1、负除数截断回归 14 行），gen1 实测 STDOUT 逐字节一致，case_div_zero_catch 与 selfhost 三门全绿。
- [x] B-ID182 自举端字符串索引守卫收口（null NRE + 越界 IndexOutOfRange + 钳 0）：自举 GenIndex 字符串分支是零页 select + 裸 GEP——null 无报告无抛、越界读越界内存（真实 OOB read），且过时注释声称宿主仅 soft；探针实测宿主 irgen_expr_access.c:1275 走 emit_string_elem_guard / emit_index_bounds_check：try 内 null 抛 NullReferenceException、负或 >= len 抛 IndexOutOfRangeException，try 外 soft 报告 + 索引钳 0（越界读返回 0 号字符）。自举端补齐同口径：null EmitRuntimeCheckIf + 零页 select、zstr_len（头 -16 精确长）上下界判定 + EmitRuntimeCheckIf、oob 索引钳 0。新增 conformance/str_index_guard 双端金标（越界/负/null/try 内分发/域内回归 5 行），gen1 实测 STDOUT 逐字节一致，case_str_index_guard 与 selfhost 三门全绿。
- [x] B-ID183 数组索引 null/越界双端收口（宿主段错误根治）：探针实测 C 宿主 null 数组 t[0] 在 try 内也直接段错误——一维分支（irgen_expr_access.c:1300）只查越界不查 null（zan_array_len 解引用 null 头），字符串分支有 base guard 数组分支漏配；自举端同点位裸 GEP——越界读越界内存（负索引读到数组头垃圾）、null 无报告。C 宿主一维 + 多维（emit_mdarray_elem_ptr 同款面）补 null 守卫（emit_runtime_check + emit_soft_base_select 落 scratch 页，len 读 0 → 越界钳 0 → 读 0，try 内抛 NRE）；自举端一维数组分支补 null EmitRuntimeCheckIf + 零页 select、头 -8 精确长上下界判定 + EmitRuntimeCheckIf、oob 索引钳 0（多维 MdFlatIndex 折算面顺延同模式）。新增 conformance/arr_index_guard 双端金标（越界/负/null/try 内分发/域内回归 5 行）+ diff_cases.txt 登记，gen1 实测 STDOUT 逐字节一致，case_arr_index_guard 与 selfhost 三门全绿。
- [ ] B-ID170 结构收口三件：① 中风险拆分（Designer 三件套：Designer.Form.zan 4301行已于 commit 3b8cdd439 按 partial 拆出 Ribbon/Layout/Free/Canvas/Ruler/Preview 6切面并完成 RenderDesignCanvas 巨函数拆解；余 Designer.zan 6909 与 Designer.Inspector.zan 3466、SceneDesigner 2951——后两者将在 B-ID177 改写为保留式，拆前先评估；App.zan 已并入 B-ID174）；② 命名空间三前缀统一（System.* 被 stdlib/packages 同名分仓、Game 裸名——独立大工程，动前定一尊；Gui 已定案并入 B-ID178）；③ [x] zan.pkg / zan.json 支持 deps 顶层/数组与约束解析，zan_pkg_fetch 补齐本地目录及 monorepo 回退，闭环 zan-pm restore 下载与 sha256 树校验（unit_zan_pm_cli 覆盖端到端 12 步验证全过）。

- [ ] B-ID178〔前置 B-ID172～B-ID177〕GUI 分层重构 P7 按层拆命名空间：目录即命名空间，codemod 原子提交（约 1,285 个调用方文件 + 977 处限定引用），兄弟包各占己名，机检切严格（承接 B-ID170 ② 的 Gui 部分）。

- [ ] B-ID152〔裁定搁置〕ZanDB 刀8 段 zstd 压缩：stdlib 无压缩绑定，需引入外部库链接与三平台分发——与自包含分发架构相抵；且压缩块缓存仍需解压驻留，对 2G 内存机目标无益。数据面收益已被 tag 11 行编码（4×↓）替代大半，除非出现大文本字段压缩需求，不领。
- [ ] B-ID153〔裁定搁置〕ZanDB 刀9 SIMD 距离内核：运行时只有 CPU 特性探测（System.Runtime.Intrinsics.Cpu）无向量算术内建，落地须编译器/运行时新增 AVX2/NEON 内建——超出 ZanDB 数据面范围。评估前置已满足：发布档基线实测 approx 2ms/20k 库（debug 9.9ms，约 5×，见 ZanDb README 量化检索实测），而真实查询延迟由重打分文档行点查主导（12ms，IO 不随构建档变），SIMD 对端到端收益被摊薄；除非出现纯 ADC 高 QPS 场景，不领。
- [ ] B-ID155〔裁定搁置〕zanc 编译缓存重做：旧缓存架构因产物散落源码树、无界堆积成巨量垃圾文件被移除；每次全量重编（GUI 程序 726 文件，Zan.Gui 包 387 文件/23MB 源码）是其接受代价，不是遗漏。重提必须先满足四约束：产物只进单一 git-ignored 固定目录、容量有界（LRU 封顶驱逐）、内容哈希键失效（杜绝陈旧产物）、带一键清理入口。实测 hello+stdlib 30 文件峰值 18MB——进程基线轻，内存大头是包编译面；包缓存收益随 Gui 包肥大持续增长。
