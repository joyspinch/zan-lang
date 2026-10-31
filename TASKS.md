# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

- [~] **B-ID46** `templates/gui/` 家族迁移到运行期设计稿通道。编译期生成器删除后，19 个 GUI 模板里 17 个仍是死通道形态：zan.proj `entry` 指向 `.html`（编译器已拒收）、App.zan 是无 Main 的纯 code-behind（靠已删除的 GenForm 补控件字段）、脚手架 `ZanIDE.Scaffold.zan` 还在给新项目写 `entry = src/<名>.html`、`GenBusinessShell`/`GenFreeShell` 生成的壳声明"编译期投影"并引用不存在的字段访问器——今天从 IDE 新建任何 GUI 项目都编不过。修法：全部模板改运行期 Main（读文档 → DesignerHtml.ToJsonDoc → FormBuilder.BuildWith → form.On 注册 @事件 → Run），zan.proj `entry=src/{{NAME}}.zan` + `gen.embed=src/{{NAME}}.html`（发布烘入加密，开发期散盘），脚手架与壳生成器同步改。重点：gui-wechat（83 文件）与 gui-webapp（8 文件）需按页迁移；gui-scene-router 已是游戏通道只验证。
- [ ] **B-ID47** 运行期文档通道缺键事件派发：`DesignerHtml.NormalizeEventName` 认 `@keydown`/`@focus`/`@blur`（产出 `onKeyDown` 等文档键），但运行期没有任何控件触发这些命名事件——绑定静默失效（gui-free/gui-watch 模板因此删掉 @keydown 演示）。修法：Input 按键路径接 `BindEvent("KeyDown", ...)` 一类的命名事件出口，conformance 用例钉住。



