# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [ ] B-ID43 运行期通道回填设计稿的类型化表格键：FormBuilder/Html.Build 应消费 `data-of`/`data-x-columns`/`data-row-factory`（把列定义解析进 DataGrid 的列模型、保留 of 契约名），现在这三键被整体剥掉、连属性表都不进。根因：三键是 GenForm 生成期扩展，运行期建造器从未读过；Zan 泛型擦除使 `DataGrid<string>` 能通过 `DataGrid<T>` 转型，但类型化列访问器只能程序侧代码给。探针：`tests/gui/genform_contract_test.zan` 的 grid 断言（现钉 "runtime factory 建出 DataGrid"，回填后加强为列数 2 + factory）。


