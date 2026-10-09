# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

（暂无欠账）


## 未完成

- [ ] IDE-1 跨文件重命名缺预览与全局撤销：挂钩点在 CodeNav 异步重命名管线（RenameSymbolAsync/FinishRenamePlanCommit，尚在会话在途未落地 HEAD）。落地后在 FinishRenamePlanCommit 提交磁盘前调 PushRenameHistory（计划条目含 originalText/newText/open/version），撤销=校验当前性（打开页签 body==newText、磁盘==newText）→ RollbackRenameDiskPlan 语义还盘 → 恢复打开页签 body → 重载活动编辑器 → lspSess.SyncDoc → 注册撤销命令。
- [ ] IDE-2 设计稿 head 重建不保真：Html.zan 解析器有意丢弃 head 的 meta/link/title（只透传 head data-* 与 style css），ToJsonDoc 无法还原。需解析器保留 head 元数据通道后，DesignerHtml.FromJsonDoc 才能重建原 head。
