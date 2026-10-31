# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [ ] B-ID51 HEAD 上 case_generic_iface_convert 红：`up 0`（期望 `up 11`），iface_convert.zan:47 运行期报 "interface dispatch has no implementation"。8d4a3e72d 交付时该用例 4/4 绿，其后某提交回退了接口分发。复现：`build/zanc.exe tests/conformance/generic_iface_convert.zan --package-project . -o x.exe && x.exe`。嫌疑范围：2df60642a（方法组→委托验签）、2e3932369（ORM 泛型模板重构）；仅注释/测试注册的 e91c05f59、727fdea73 可排除。




