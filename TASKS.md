# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [ ] B-ID56 Zan.Web 响应/协议回归 4 案红（b1752059e/2e3932369 重构尾债，非 2026-11-01 批次引入，本批前即红）：server_mvc_security_routes（"only harmless TxProbe route generated"——路由生成的安全过滤语义）、web_list_protocol（Paged 协议深处输出分叉）、web_response_bodyless（二进制/HEAD 无体帧 contract 2 断言）、web_response_safety。另注：web_route_regex / web_mvc_base_routes 编译 120-170s 贴 180s 预算，-j3 以上并行会 Timeout，-j2 稳定绿。




