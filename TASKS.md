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

- [ ] B-ID161 examples/html_gallery 源级损坏：Page0-7 类全仓无定义、无 zan.proj、入口 .zan 引用它们必然 unresolved（HEAD snapshot 与实验编译器一致报错），主类曾于 d364a8167 恢复但引用页未随之恢复；且 CLI 拒收 .html 直编，模板门不覆盖它。修复 = 补齐 Page0-7 定义与 zan.proj 或删净引用，纳入模板门。
- [ ] B-ID162 zanc `--debug` 档编译 oneplus 图形程序段崩溃（rc=139，无诊断输出）：HEAD 快照编译器（`git archive HEAD` 实体落盘）复现，hello --debug 正常——oneplus 图特有（疑与设计稿内嵌/WebView 相关的 irgen 路径）。2026-11-05 当日实验树 zanc（B-ID71 在飞态）plain/publish 也段崩溃，B-ID71 落地后未复测；修复时先拿当前 HEAD 复测两个崩溃面再定位。
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

- [ ] B-ID168 selfhost 孪生与巨石拆分：① A1（lambda→async 委托，selfhost 无 delegate_is_async 概念需补判定）与 A3（强转异常同号）的 selfhost 同号孪生；② ngen.zan(15608)/irgen_expr.zan(11305)/ngen_host.zan(7496) 等拆分——验证走三门 `selfhost_gen1`+`selfhost_fixed_point`(g2==g3 字节等值)+`selfhost_diff`，新文件必须插 `scripts/selfhost_sources.txt` 原文件相邻位、搬家不重排（MergePartials 按源清单顺序合并）。
- [ ] B-ID169 `Convert.*` 静默 0 语义收敛：坏输入返回 0 使 try/catch 兜底成死代码（SKILL.md:496-509 已记陷阱）；落地前先全仓用量审计，或新增 Strict 变体保留旧径并公告。
- [ ] B-ID170 结构收口三件：① 中风险拆分（Designer 三件套：Designer.Form.zan 4301行已于 commit 3b8cdd439 按 partial 拆出 Ribbon/Layout/Free/Canvas/Ruler/Preview 6切面并完成 RenderDesignCanvas 巨函数拆解；余 Designer.zan 6909 与 Designer.Inspector.zan 3466、SceneDesigner 2951——后两者将在 B-ID177 改写为保留式，拆前先评估；App.zan 已并入 B-ID174）；② 命名空间三前缀统一（System.* 被 stdlib/packages 同名分仓、Game 裸名——独立大工程，动前定一尊；Gui 已定案并入 B-ID178）；③ zan.pkg 加 deps 字段 + zan-pm restore 下载闭环（pm_main.c:216-248 现为空转）。

- [ ] B-ID178〔前置 B-ID172～B-ID177〕GUI 分层重构 P7 按层拆命名空间：目录即命名空间，codemod 原子提交（约 1,285 个调用方文件 + 977 处限定引用），兄弟包各占己名，机检切严格（承接 B-ID170 ② 的 Gui 部分）。

- [ ] B-ID152〔裁定搁置〕ZanDB 刀8 段 zstd 压缩：stdlib 无压缩绑定，需引入外部库链接与三平台分发——与自包含分发架构相抵；且压缩块缓存仍需解压驻留，对 2G 内存机目标无益。数据面收益已被 tag 11 行编码（4×↓）替代大半，除非出现大文本字段压缩需求，不领。
- [ ] B-ID153〔裁定搁置〕ZanDB 刀9 SIMD 距离内核：运行时只有 CPU 特性探测（System.Runtime.Intrinsics.Cpu）无向量算术内建，落地须编译器/运行时新增 AVX2/NEON 内建——超出 ZanDB 数据面范围。评估前置已满足：发布档基线实测 approx 2ms/20k 库（debug 9.9ms，约 5×，见 ZanDb README 量化检索实测），而真实查询延迟由重打分文档行点查主导（12ms，IO 不随构建档变），SIMD 对端到端收益被摊薄；除非出现纯 ADC 高 QPS 场景，不领。
- [ ] B-ID155〔裁定搁置〕zanc 编译缓存重做：旧缓存架构因产物散落源码树、无界堆积成巨量垃圾文件被移除；每次全量重编（GUI 程序 726 文件，Zan.Gui 包 387 文件/23MB 源码）是其接受代价，不是遗漏。重提必须先满足四约束：产物只进单一 git-ignored 固定目录、容量有界（LRU 封顶驱逐）、内容哈希键失效（杜绝陈旧产物）、带一键清理入口。实测 hello+stdlib 30 文件峰值 18MB——进程基线轻，内存大头是包编译面；包缓存收益随 Gui 包肥大持续增长。
