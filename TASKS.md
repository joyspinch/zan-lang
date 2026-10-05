# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

- [ ] **B-ID114（P3·运行时杂项批）**：fork 前 argv 置空（rt_sync.c ~3250）；capture 循环 EINTR（~3548）；reactor fd CLOEXEC；线程池创建失败记账（rt_io.c ~5769）；shutdown 竞态（~5904）；select 后端 rto_wait_ms 桩；rt_file.c ~723 gen 回绕退役；rt_timer POSIX hard-mode 打印（~466）；rt_crash.h tm_yday 时区回绕（~1239）。
