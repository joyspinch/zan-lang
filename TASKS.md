# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

- [ ] **B-ID113（P3·编译器杂项批）**：async EH 搁浅临时（irgen_async.c ~1748）；irgen_shard.c ~238 固定 32768 表自旋；irgen.c site 表退出泄漏（site_syms/site_coll/…）；checked 上下文无符号下溢（irgen_expr.c ~4095）；ScanNotByte 常量判定（~874）；Prefetch 元素宽（~2475/2805）；插值 char/T? 洞 strlen（~6680/6695）；NaN punning memcpy（~7046）；binder 泛型元数校验；nsresolve prune 泄漏+重复诊断；genrun.c ~877 调试打印；main.c 引号/截断/malloc 检查；package.c % 白名单+realloc 检查；apk.c 错误路径释放；embedres.c 守卫次序；incremental.c 最小加固。
- [ ] **B-ID114（P3·运行时杂项批）**：fork 前 argv 置空（rt_sync.c ~3250）；capture 循环 EINTR（~3548）；reactor fd CLOEXEC；线程池创建失败记账（rt_io.c ~5769）；shutdown 竞态（~5904）；select 后端 rto_wait_ms 桩；rt_file.c ~723 gen 回绕退役；rt_timer POSIX hard-mode 打印（~466）；rt_crash.h tm_yday 时区回绕（~1239）。
