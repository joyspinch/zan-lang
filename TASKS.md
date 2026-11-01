# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [ ] B-ID57 server_mvc 三案红（Zan GenRoute→C routegen 换代的语义对齐余债；金样锚点 7c3519da1）：admin_gate 路由计数 173 vs 金样 193（缺 ~20 条 admin 路由，疑似 Zan 版元数据拍平继承动作 Saved/Note 一类口径未对齐；gate raised=0 已达标）、generator_sources（生成产物断言 "api json projection" 红，属 Zan.Mvc 源生成器输出）、metrics_flush_retry（lease-retry-result/cleared 两断言）。已修：CMakeLists 两处 ZanWeb 旧路径（AiEndpointPolicy/Fmt）致 ai_endpoint_allowlist 与 timezone 双案瞬时红——2026-11-01 批次绿。




