# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### B-ID117 [~] zan-lsp 智能感知精度批次：测试断言已写、服务器实现缺位
`tests/lsp/lsp_integration_test.c` 工作树有一批未提交新增断言（约 +527 行），对当前 zan-lsp 有 36 条 completion 相失败（HEAD 旧断言全绿，见提交历史）。缺的服务器侧能力，按失败类归组：
- 同行块作用域需列感知：`{ T x = ..; } x.M;` 出块后仍可见、块内普通前缀补全看不到块局部、for 初值出循环不过期（`intel_position_in`/补全过滤链，isym_t 的 scope 列已记录）。
- references/rename 精确性：嵌套同名局部不互捕、lambda 参数捕获、插值串洞只改代码片段、跨文件接收者身份过滤、includeDeclaration=false 排除声明。
- var 初值跨后置方法/跨文件静态工厂/提供者编辑后的推断缓存失效。
- didChange 版本守卫：stale/duplicate 版本不得覆盖更新文本。
- rename 的 newName 拒绝关键字（现仅验证标识符字符）。
证据：`_scratch/lsp_int_full.log`（可按上述分类重生成）；HEAD 版测试对当前服务器 exit=0。注意：工作树该文件不要随无关提交带入 main，否则 lsp_integration_utf16 必红。

