# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

### B-ID42 compiler_driver_liveness 全档红：提取分片缺 live extern caller

- [ ] 现象：`ctest -R compiler_driver_liveness` 失败——"extracted shard did
  not contain the live extern caller"（sharded 模式提取出的 shard IR 没有
  `@PublishFeature_` 定义与 `@EmitLib_Add` 调用；sharding 本身没回退）。
  2026-10-31 在干净 HEAD（000d4c721+）A/B 实锭与本轮 genrun 拔除无关。
  怀疑与近期 publish 分节/瘦身提交（80082105e 一类）相关——DCE 把跨分片
  live 的 extern caller 判死或移出了提取分片。复现：`cd build && ctest -R
  compiler_driver_liveness --output-on-failure`。


