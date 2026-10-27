# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

### A595 `--publish` 构建 ff_bidi_demo 挂起 + null length probe 软错（publish 路径缺陷）

- 症状：同一份 `examples/ff_bidi_demo` 源（685 文件，2026-10-27），普通
  构建全新解压实跑 exit=0 全 PASS；`--publish` 构建跑出三处
  `soft runtime error: null reference where a string/byte buffer is
  required (length probe)`——`stdlib/System/IO/Directory.zan:593`、
  `packages/Zan.Diagnostics/src/System/Diagnostics/Process.zan:521`、
  `packages/Zan.Gui.Browser/.../FirefoxBrowser/FfWs.zan:84`——随后卡在
  BiDi session.new 之前不退出；`zan_crash.log` 只有 soft 记录、无 fault。
- 复现：`build/zanc.exe examples/ff_bidi_demo/FfBidiDemo.zan
  --auto-stdlib --package-project zan-lang --publish -o out.exe` 后运行
  （机器需有 Zan ff 缓存或 `ZAN_FF_NO_SYSTEM=1`）。
- 定向：探针点在普通构建下工作正常的字符串上 → 差异在 publish 专属
  插桩/优化（arc_net 等），不在三处业务调用点本身。**publish 并非普遍
  坏**：最小程序（字符串拼接 + List(数组) 拷贝构造）--publish 构建运行
  正常，缺陷范围收窄为 publish 插桩/优化与本 demo 特定代码路径
  （Download/Process/FfWs 的字符串处理）的相互作用。下一步做最小探针
  二分：是插桩误报还是真 UB。
- 本任务处置：dist/ff-bidi-test 测试包按 README 配方用普通构建交付
  （两轮解压验证 PASS）；publish 缺陷留此挂账，不许绕行改写业务代码。
