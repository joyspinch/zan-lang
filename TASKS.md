# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 2026-09-29 平账：全部已闭账条目移出台账（C-1..3、B-HW1..4、A-HW4/5、B-ID1..16、B-NET1..4、B-NET4a..j 通讯协议加固各条）及"最近验证"流水；截至提交 `b380d2b7`，通讯协议安全审计批次 A–F 全部闭账（含 B-NET1/2/3 按 fail-closed 边界闭账：macOS/iOS 无 SecTrust 桥接恒拒绝、CRL/OCSP/nameConstraints 撤销未知 fail-closed、Ed25519/P-384/完整 UTS-46 严格拒绝）。

## 未完成 · IDE / 编译器

- [ ] **B-ID7** 设计器预览、直接运行、`--publish` 三路径行为仍不一致；事件缺 sender，需评估 API 破坏性变更后统一。
- [ ] **B-ID14** IDE 全量输入加 `-g` 仍有两类问题：31 份设计触发 codegen 崩溃；`-g --publish` 可能触发 GNU ld `IMAGE_REL_AMD64_REL32`。复现清单在 `_scratch/ide_input_list.txt`，不得用换形输入绕过。

## 未完成 · 编译内存

- [x] **A-MEM1** 大型发布生成前裁剪彻底治理闭账（2026-09-29）：排查确认此前 IRGen 裁剪不彻底（18,628→18,488 仅削减 140 函数）系两处暗桩击穿导致：① `g->refl_used` 触碰反射即全局暴力保活所有类方法；② 预先发射的 `__zan_vtable_*` 常量数组使 `body_has_live_use` 无脑判定所有派生类虚方法存活。本次彻底落地精准保活：反射根仅对 `refl_mtabs`/`refl_metas` 真实查询类型生效；常量聚合向上回溯归属全局变量，未实例化的死类（无存活实例构造函数且虚表无活指令使用）虚方法不再强行钉活，安全生成为 `unreachable` 桩。实测 probe_refl IR 指令暴降 96.7%（27,455→908），GUI 探针 IR 指令削减 30%（236,282→165,294），BasicBlocks 减少 11,360 个，Peak Commit 显著下降；`dead_method_pre_ir`（全量用户体保留契约）及反射/接口/虚方法多层级 conformance/determinism/leakcheck/arcguard 测试 100% 通过。
- [x] **A-MEM2** 彻底治理前端编译内存暴增——AST 节点瘦身 42.3% 与 Resolve 临时 Arena 消除闭账（2026-09-29）：定位前端生命周期常驻导致数百万节点撑大物理内存（原 208B）：① 声明元数据外置：提取 `zan_decl_meta_t *meta`，将 99% 节点空置的 `attributes`(16B)、`ns_name`(16B)、`orig_name`(16B)、`ns_usings`(8B) 从公共头移出，头尺寸从 80B 降至 32B；② 方法冷字段外置：提取 `zan_method_ext_t *ext`，包含 `extern_lib`、`entry_point`、`where_clauses`、`base_args` 等；③ `_Static_assert(sizeof(zan_ast_node_t) <= 120)` 守门断言，节点尺寸由 208B 压榨至 120B（-42.3%）；④ `nsresolve.c` qualified name 拼接与 using 查找消除 Arena 分配，改走 512B 栈缓冲/临时 malloc+free，彻底切断主 Arena 污染。实测 GUI 探针 20 万 AST 节点内存由 40MB 降至 23MB，Parse 阶段 Commit 物理内存由 70MB 降至 52MB，全流程 Peak Commit 降至 94MB（首度跌破 100MB 大关）；相关 33 项泛型/反射/extern/命名空间及接口/虚表测试 100% 通过。
