# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

- [ ] **A375 `.zanmap` 调用图 id 退化为裸成员名**
  证据：重构后 `callgraph.jsonl` 的 `id`/`name` 是裸成员名、`owner` 恒空
  （对照重构前为 `Owner.Member`），`File.ReadAllText` 与任意 `ReadAllText`
  无法区分，`-Callers` 只能按成员名查。
  修点：`tools/repomap` 恢复 `<Owner>.<Member>` 形式的 id 并同步
  `symbols.json`。现状：`tools/agent/map.ps1 -Callers` 已做裸名容错并提示。

- [ ] **A376 模板验收门缺跨平台官方入口**
  证据：`scripts/test_all_templates.py` 硬编码 `/Users/qq` 与
  `libzan_gui.dylib`，并依赖 `build/zangen`（源仓库构建目录没有）。
  现状：`tools/agent/gate-templates.ps1` 是 Windows 等价物（按
  `template.manifest` + `zan.proj` 的声明做替换副本，46 个模板全量可跑）。
  修点：官方脚本走平台探测，或把 Windows 侧纳入 `scripts/` 统一入口。
