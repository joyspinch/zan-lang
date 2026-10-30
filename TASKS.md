# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

### B-ID44 IDE 自举构建迁移运行期设计稿通道

编译期 HTML 通道删除后（zanc 显式拒绝 `.html` 输入，
`diag_html_input_rejected` 钉住该诊断），`scripts/build_ide.ps1` 仍按
genrun 时代规则把 `src/IdeForm.html` 等 31 份设计稿当编译输入传给
zanc——IDE 自举构建当前必红。迁移形状：设计稿改 `--embed` 随 exe 烘入
（内嵌优先读回、发布加密），程序 Main 落在 Zan 代码里，启动时经
`DesignerHtml.ToJsonDoc` + `FormBuilder.Build`/`ChildWindow` 投影各设计稿
（迁移配方即 tests/gui/form_* 系列）；`docs/projects/zanide/STRUCTURE.md`
§1 的 GenForm 生成物/入口规则说法随迁移一并改写。
探针：`build/zanc.exe tests/diag/html_input_rejected.html` 报
"the compile-time HTML channel has been removed"。

### B-ID45 运行期投影的 DataGrid 类型化单元格读写

B-ID43 回填了设计稿表格的列元数据（gcols）、`of` 属性与行工厂
（CanInsert/InsertRow 经 source 生效），但类型化单元格读写仍不通：
运行期投影建出的 DataGrid 上 `GridSource.CellText(r,c)` 恒空串、
`SetCell` 不回写行对象——source 没有拿到 "of" 实体的字段访问通道
（私有/公共字段都一样，非反射可见性问题）。钉子：
tests/gui/form_grid_test.zan（注释标明补齐点）。
探针：`_scratch/gridprobe5` 形态——Build + Bind 后 CellText(0,0)=""、
SetCell 后行对象未变，同树 CanInsert/InsertRow/HideCol 全通。



