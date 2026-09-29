# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 2026-09-29 平账：全部已闭账条目移出台账（C-1..3、B-HW1..4、A-HW4/5、B-ID1..16、B-NET1..4、B-NET4a..j 通讯协议加固各条）及"最近验证"流水；截至提交 `b380d2b7`，通讯协议安全审计批次 A–F 全部闭账（含 B-NET1/2/3 按 fail-closed 边界闭账：macOS/iOS 无 SecTrust 桥接恒拒绝、CRL/OCSP/nameConstraints 撤销未知 fail-closed、Ed25519/P-384/完整 UTS-46 严格拒绝）。

## 未完成 · IDE / 编译器

- [ ] **B-ID6** zan-lsp 请求处理仍单线程串行；诊断期间跳转/补全排队。需请求级并发或 `$/cancelRequest`。
- [ ] **B-ID7** 设计器预览、直接运行、`--publish` 三路径行为仍不一致；事件缺 sender，需评估 API 破坏性变更后统一。
- [ ] **B-ID9** IDE 代码编辑器尚无折叠 UI；LSP `foldingRange` 已就绪，前端未接。
- [ ] **B-ID14** IDE 全量输入加 `-g` 仍有两类问题：31 份设计触发 codegen 崩溃；`-g --publish` 可能触发 GNU ld `IMAGE_REL_AMD64_REL32`。复现清单在 `_scratch/ide_input_list.txt`，不得用换形输入绕过。

## 未完成 · 编译内存

- [~] **A-MEM1** 大型发布已落地保守生成前裁剪：声明先行、Main/初始化/构造/委托/虚表/反射/库导出按固定点保活；非发布与 `--emit-ir` 保留用户体以免吞掉降层诊断，发布仅裁剪 stdlib 体，未用声明留一块 `unreachable` 以满足 LLVM。冻结 OnePlus 402 输入重测：IR 定义 18,628→18,488、指令 4,022,035→3,545,750，峰值 Commit 1,530→1,414 MB（仍由 IRGen 主导，未彻底闭账）。`dead_method_pre_ir` 行为+IR 回归、smoke 313/313、standard 可执行集 999 项中仅并行 `zandb_p3` 偶发红且串行通过；阶段一 `12d7e911` 仅建立 generated-object 向量，明确无内存收益声明。独立 LLVM 生命周期探针在同一 context 串行生成/写出/销毁 8 个高负载 module（每片 128 函数×256 算术指令）通过，PrivateUsage 首片后不随 module 数线性增长；因此当前主要阻碍不是 module dispose 泄漏，而是 Zan IRGen 普遍持有 module-local LLVM handles、internal linkage 和全局辅助/类型状态，尚无可安全拆分的闭合函数族；继续保留单模块 fallback，后续需重构 ABI/声明重建后再评估分片。问题边界扩大到所有大型项目后，补做四处项目无关的规模化热点优化（2026-09-28）：输入去重 O(K²)→FNV-1a 开放寻址（含 POSIX dev/ino 索引）；binder 成员冲突检查 O(M²)→按类型成员名索引，索引异常或同名候选超过 64 时回退原全量扫描并保持诊断顺序；tuple 类型缓存线性查找→规范化签名哈希；IRGen `body_has_live_use` O(W²)→LLVM 函数指针索引，未知父函数保守视为存活。6000 成员、401 输入、七类诊断和 publish 定向探针通过。smoke 313 项两例并行负载抖动均隔离通过；standard 1003 项并行运行出现 62 个负载/环境红，代表性串行复跑 6/7 通过，唯一稳定红为需要交互桌面的 `win_tray_screen_smoke`，未发现本轮编译器回归。
