# async finally 共享发射成本记录（2026-10-08）

**层级：历史测量记录。** 本文冻结本轮实现与测量，不随编译器后续演进更新。当前代码以 `src/compiler/irgen_stmt.c`、`irgen_async.c`、`irgen_emit.c`、`irgen.c`、`irgen.h` 为准；后续未修异常边界见 `TASKS.md` B-ID125。原始数据、源文件与对象哈希、逐阶段日志、50 ms 内存采样、回归输出和最小未修探针见 [JSON 证据](compiler-finally-cost-2026-10-08.json)。

本轮消除了兼容提前退出路径上的 finally 乘法展开：32 组出口与清理语句的 resume 从 8,636 降至 1,314 条指令，减少 84.8%。冻结 OnePlus 的同源对照没有获得实质指令或内存下降，因此不能把探针收益当成 OnePlus 收益，也不能将这次耗时均值下降直接归因于本补丁。

## 实现边界

旧 pending-finally lowering 在每个 return/break/continue/goto 出口重新发射整个 finally AST。新实现按词法局部、模式绑定、catch cleanup、armed handler slot、外层 finally region、循环目标与 checked/throw 基线分组，兼容出口共享 body。每个 try AST 规划一个 storage-only i32 帧槽，进入时存 continuation id，body 末尾 switch 回原出口；原出口仍执行自己的后续清理，返回值仍通过 RETSPILL 跨 awaiting finally 保存。

正常 try 退出与异常入口仍使用原有独立 lowering。它们已经弹出所属 handler，而提前 return 入口仍可能 armed，合并这几类入口需要先统一 EH 退出协议。保守扫描仍为 finally 预留副本容量，本轮没有宣称已消除扫描、handler/state 数组的全部冗余。

实现同时修复了嵌套发射覆盖编译期 finally 描述符的问题：隐藏一个 region 以发射其 body 时，嵌套 try 可复用整个被隐藏后缀，恢复 count 或只恢复单个条目都不充分。现在 pending/exception 发射保存恢复相应后缀，并保留当前 region 新增加的共享组。

审查及差分探针发现并修正了初版补丁的两处回归：

- 保守扫描与共享发射不再访问同样数量的 foreach 副本。旧的 `$fe.N` 扫描/发射双计数发生漂移，随后 protocol foreach 缺 enumerator 槽。现在按 foreach AST 和 element/index/collection/enumerator 用途定位帧槽。
- 嵌套 selector 重复扫描命中已有节点后，storage-only 包装器曾盲改最后一个 local.no_arc，使后续带动态字符串字段的结构体失去 owner。扫描辅助函数现在返回准确的已有/新增 index，包装器只修改该槽。初版泄漏、修复后无泄漏的输出都保留于 JSON。

## 多出口增长探针

同一探针同时扩大 return 站点数和 finally 清理语句数，每四条清理语句加入一次 `Task.Yield()`；Main 实际调用所有出口，精确断言返回和清理总数，并要求 leakcheck 无额外输出。

| 规模 | 原始快照指令 | 最终指令 | 原始块数 | 最终块数 |
| --- | ---: | ---: | ---: | ---: |
| 8 | 890 | 456 | 84 | 63 |
| 16 | 2,544 | 742 | 208 | 99 |
| 32 | 8,636 | 1,314 | 648 | 171 |

16→32 的增长为 339.5%→177.1%。永久 `conformance_async_finally_ir_scaling` 采用 250% 上限；旧编译器确实被该门禁拒绝，最终编译器通过。指令来自 whole-module 优化前的 codegen manifest，但已经过函数内早期 compaction；不是原始未压缩 LLVM 指令数。

## 冻结 OnePlus 同源对照

输入沿用已有冻结 bundle：400 个入口输入、应用提交 `eb11f4c5d03d2e59555ed0b55fdae000af1e0e7c`、依赖提交 `6cd90920cdd08fbb1e783b942778c6e632eefabf`。owner 与 rsp 哈希和 2,037 个源文件、2,103 个原生依赖的完整性在每次测量前后检查。所有测量静态发布成功。

对照编译器从当前 emitter 源码副本构建，只将调用共享 pending-finally 的条件置为 false；foreach 槽身份、扫描所有权与嵌套描述符修复在两边相同。复用完全相同的非 irgen 编译对象并记录对象哈希，避免把共享工作树其他改动算作优化收益。LLVM 20.1.8、Release、同一 clang 工具链。

执行顺序为新→旧→旧→新。每轮使用独立空 metadata/profile 目录；均为 543 次 metadata miss/543 次 written、830 次真实 parse、1,586,484 个 AST 节点。进程闸门排除已运行的 zanc/ctest/ninja，但不能保证共享机器上没有其他负载。

| 顺序 | 编译器 | 发布秒数 | 峰值 private commit MiB | 峰值 working set MiB |
| --- | --- | ---: | ---: | ---: |
| 1 | 共享 | 78.021 | 951.91 | 948.03 |
| 2 | 对照 | 85.490 | 951.90 | 947.83 |
| 3 | 对照 | 103.338 | 950.80 | 946.77 |
| 4 | 共享 | 78.105 | 951.50 | 947.16 |

| 指标 | 对照 | 共享 |
| --- | ---: | ---: |
| IRGen 结束指令 | 2,243,251 | 2,243,252 |
| IRGen 结束块数 | 405,762 | 405,766 |
| manifest 指令 | 2,213,838 | 2,213,839 |
| async resume 个数 | 535 | 535 |
| async resume 指令 | 515,745 | 515,746 |
| DoTaskCore resume 指令 | 11,798 | 11,798 |
| 分片对象数 | 34 | 34 |
| movable 函数数/指令数 | 9,120 / 1,593,706 | 9,120 / 1,593,706 |
| EXE 字节数 | 18,641,920 | 18,642,432 |

仅四个 resume 指令数发生变化：HarvestCookiesAsync +3、HttpTurn_Send +3、HttpTurn_SendCH −7、WebSocketClient_SendFrame +2，总计 +1。函数/global 数相同。

测得均值为 94.414→78.063 秒（−17.32%），private commit 951.350→951.705 MiB（+0.037%），working set 947.300→947.595 MiB（+0.031%）。耗时下降主要在 shard emit/optimize 阶段，而移动到分片的指令完全相同；两次旧侧耗时也相差约 18 秒。**本轮不据此宣称稳定 17% 加速。可归因的确定结论是：该冻结输入的 IRGen-end 指令和内存基本不变。**

源码适用性检查也解释了低命中率：冻结应用中六个 async 函数含 finally，主要用于正常/异常清理。`DoTaskJob`、`WorkJob`、`CheckLoginAsync` 的显式 return/break 位于受保护 region 之外；`Send` 的两个 return 分属 try/catch；`HarvestCookiesAsync` 的两个早退之间新增了局部 `cur`，上下文不能直接合并。`SendCH` 的小幅 −7 与少量兼容早退共享一致。不同 if 块本身不一定不兼容，实际仍由 lowering 的完整上下文匹配决定；不能仅靠源码块数推导共享命中。`DoTaskCore` 并未获得本轮优化收益。

## 正确性验证

- 首批 34/34 定向 conformance/leakcheck 通过，23.74 秒，包括三个新增 shared-finally 用例、现有 finally/EH/goto/lock/aggregate/await-tail 用例和两个规模门禁。
- foreach 存储身份影响面另跑 10/10 个 array/list foreach、同名遮蔽、frame storage/ref frame 孪生，8.09 秒。
- 三个新用例全部另做 `--publish --check-leaks`，要求精确输出且没有泄漏文本。
- 独立只读复核修正后的扫描角色、所有权与隐藏后缀恢复，未发现需要修复的新具体问题。
- 未运行 smoke/standard/full 整档。未运行 OnePlus GUI；此处的 OnePlus 验证是冻结输入静态发布与输入完整性。

永久回归：`async_finally_shared_paths` 覆盖 fast/slow 子任务、字符串返回、catch-return、normal/unwind、循环 break/continue；`async_finally_shared_contexts` 覆盖不同词法局部、forward goto、两层活动 region 内嵌两层 finally、跨 await 的引用字段结构体；`async_finally_shared_foreach` 覆盖共享 continue/break 清理后继续 protocol foreach。

## 已复现但未修的异常边界

最小 `try { return 17; } finally { await Task.Yield(); attempts++; throw new ReplacementError(); }` 应只执行一次 finally。原始与最终编译器都输出 `replacement:2` 并泄漏一个异常，退出码却为 0。计数直接确认重入；源码路径表明第一次异常在 FINEXC 保存后，被第二次 throw 放弃而没有 release。这仍属于 B-ID125，既未被本轮引入，也未被本轮修复。

pending/unwind/normal 的全面共享必须处理 region handler 退出及被替换的 pending return/exception owner；当前兼容上下文共享不替代该修复。OnePlus 仍有约 51.6 万条 resume 指令，不能根据本轮探针下降就认定其剩余成本已得到解决。
