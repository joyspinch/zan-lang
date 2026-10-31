# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [ ] **B-ID48** UiEvent sender 通道委托实参腐坏（编译器/运行时缺陷）：`UiEvent.RaiseS` 里 `h(sender)` 按名调用 sender 通道委托，处理器体里读形参得到的是坏引用——虚分派即段错误，读字段得空值；忽略形参则正常；无参 Action 通道完全正常。证据：`_scratch/bid47` 探针（Form 桥 ControlEvent 处理器读 `c.name` 得空串、`c.Kind()` 崩；Main 里直接 `in1.OnKeyDownS((Control c) => ... c == in1 ...)` 同样崩）；脱离 GUI 的最小复刻（List 存委托 + 循环 `h(sender)`）通过，说明需 GUI 规模上下文才现形；`--emit-ir` 检查 RaiseS/FireByName/FireKeyNamed 调用链 IR 全部正确（closure 分支 `fn(env, sender)`、bare 分支 `fn(sender)`），疑点在委托元素的 tag/调用约定错配（崩溃现场 `call *0xf8(%rax)` 的 rax 是函数序言机器码，即形参拿到代码指针）。修法方向：查 zanc 委托创建/加载的 tag 编码与 callee 参数槽映射；连带核查 ModelBinder.WireNode 与 Html 通道的 sender 处理器是否同样受害（生产面板处理器普遍忽略 sender 参数，缺陷因此潜伏）。修好后把 B-ID47 的探针升级出 sender 形态断言。



