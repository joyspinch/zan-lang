# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [~] **B-ID49** Gui 工程 `--publish` 在 LLVM 分片阶段确定性失败：`shard: shard 0: parse: <exe>.shard0.frag.ll:3102:168: error: use of undefined value '@__zan.refl.str.1910'`。复现：`templates/gui/gui-components`（sed 掉 `{{NAME}}` 后 `zanc src/App.zan --publish --auto-stdlib --package-project <repo>`）与 `_scratch/bid48/Fail.zan --publish`，发布链最终仍产出 exe（走了回退路径），但分片错误使 publish 无法走主路径；非 publish 编译与显式 `--embed` 发布不受影响。`__zan.refl.str.*` 是反射字符串全局——疑似分片器拆分反射串定义/引用时把定义留在别的分片或丢弃。证据与最小复刻：`_scratch/bid49/README.md`。注意：当前工作区有另一会话在飞的 `src/compiler/routegen.c`（未提交），定位时先分清 HEAD 状态与在飞改动。




