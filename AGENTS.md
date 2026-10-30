# AGENTS.md

Guidance for humans and AI agents (Devin, Copilot, etc.) working in this repo.
**本文件只是一行式门禁清单。每条规则的可执行全文都在
[`docs/WORKSPACE_CONVENTIONS.md`](docs/WORKSPACE_CONVENTIONS.md)（下称 WC），
章节号与条号一致（第 N 条 = WC §N）。** Zan 编码规范在 `.agents/skills/`
（清单与准入标准见 `.agents/skills/SKILLS_GOVERNANCE.md`）。

## AI 编码八荣八耻（行为守则，所有会话始终遵守；与用户全局守则为同一版本）

以瞎猜接口为耻，以认真查询为荣
以模糊执行为耻，以寻求确认为荣
以臆想业务为耻，以人类确认为荣
以创造接口为耻，以复用现有为荣
以跳过验证为耻，以主动测试为荣
以破坏架构为耻，以遵循规范为荣
以假装理解为耻，以诚实无知为荣
以盲目修改为耻，以谨慎重构为荣
以低阶模型为耻，以深度思考为荣

（对话与执行纪律——讲人话、禁反刍复读、假设破局——见用户全局守则与
`zan-dev-standards` §二，此处不再重复。）

## Directory map — where things go

| Path            | Contents                                                        |
|-----------------|-----------------------------------------------------------------|
| `src/compiler/` | Compiler front/back end (C11): lexer, parser, binder, checker, irgen |
| `src/runtime/`  | Runtime library (C11): ARC, strings, collections, IO, scheduler |
| `src/common/`   | Shared C utilities (json, rpc, host_oom)                        |
| `src/lsp/`      | Language server (`zan-lsp`) + `intellisense.{c,h}` engine       |
| `src/dap/`      | Debug adapter (`zan-dap`) + `debugger.{c,h}` engine             |
| `src/ide_zan/`  | The IDE — self-hosted, written in Zan (`src/shell/ZanIDE.zan` + `src/` 分层) |
| `src/fmt/`, `src/doc/` | Formatter, doc generator   |
| `src/selfhost/` | Self-hosted compiler sources (`.zan`)                           |
| `stdlib/`       | Standard library (`.zan` source + native driver bundles)        |
| `packages/`     | Reusable packages (`Zan.*`): game engines, SDKs, data formats   |
| `examples/`     | Curated, runnable example programs (each with its own README)   |
| `templates/`    | Project templates used by the IDE "New Project" flow            |
| 仓库外        | Large game projects live outside the repo (`D:/project/games/*`) |
| `tests/`        | Version-controlled test suite                                   |
| `docs/`         | Documentation                                                   |
| `cmake/`, `scripts/` | Build system + maintained helper scripts                   |
| `build/`        | Out-of-source build output (git-ignored, never commit)          |
| `dist/`         | Release staging output (git-ignored, never commit)              |
| `_scratch/`     | ALL throwaway work: probes, benchmarks, logs, drafts (git-ignored) |

## ZanIDE

ZanIDE 禁止任何自绘，必须全部用标准库组件来完成。

## Git gate hooks — enforced rules (always on)

仓库经 `core.hooksPath` 启用 `scripts/hooks/`（`cmake -B build` 自动配置，
详见 WC §0）：每次 commit / merge / push 机检**遗留冲突标记**（规则 11/12）
与**源码扩展名 >8 MiB 大文件**（规则 1–3）。修底层问题；仅对已证实的误报
才可用 `--no-verify` 绕过，并在提交说明里写明。

## Hard rules — 一行门禁，全文 = WC §N（条号一致）

1. **根目录只放长期受控内容**；产物、日志、探针、PR 草稿一律不进根目录。— WC §1
2. **所有一次性文件进 `_scratch/`**（探针/benchmark/日志/草稿/diff），收尾清理是任务的一部分；长驻大件必须带一行 README。— WC §2
3. **只做 out-of-source 构建，产物只进 `build/`**；并发时构建走 `scripts/build.ps1`，不裸跑 `cmake --build`。— WC §3
4. **测试进 `tests/` 并提交**；一次性内存/泄漏探针不是测试，进 `_scratch/`。— WC §4
5. **PR 用 `gh` / PR 模板**；不在仓库生成 PR 脚本、正文 md、diff 文件。— WC §5
6. **`.gitignore` 是兜底不是借口**，忽略的文件也不许在源码树堆积。— WC §6
7. **每个任务收尾干净、以一次提交结束**：验证过（规则 8）即自行 commit+push，不等批准；push 失败留在本地 main 并说明；绝不 `git add .`。— WC §7
8. **ctest 整档（smoke/standard/full）opt-in**：默认验证 = 编译运行受影响程序/探针 + 单跑受影响用例（`ctest -R <name>`，可再 `-Match` 窄化）；档位只在用户点名或发布门槛跑；**档位运行期间不得并行任何构建**。探针 + 单用例即完整验证。— WC §8
8a. **共享构建资源串行拿，冲突排队等，不另开副本**：build.ps1 / test.ps1 / 裸 ctest 共用一把跨进程锁（`scripts/build_gate.ps1`，超时报错）；公共包直接用主仓库 `packages/`（`--package-project zan-lang`）。— WC §8a
9. **永不建分支**，直接提交 main 并 push；删任何遗留分支前先证内容已并入（内容判定，不是名字/血缘）。— WC §9
10. **编译器/runtime 有缺陷就修编译器，禁止绕行写法**：`_scratch` 最小探针 → 修根因 → 补 conformance 用例单跑 → 再用自然写法改上层；超范围记 TASKS.md 并明说，不静默绕过。— WC §10
11. **并发会话共享工作树，验证过即提交**；stash 前先 git status、路径限定隔离；冲突一律逐文件手工合并、UU 清零——禁止 checkout --ours/--theirs、reset --hard、删文件拷旧版等一切单边回退。— WC §11
12. **回退是决定不是调试手段**：默认向前修；仅允许用户点名回退、或有证据证明陈旧重复/确定损坏（证据写进交付说明）。— WC §12
13. **验证过的教训当天沉淀进 skills，三副本同一提交同步**：① `.agents/skills/`（活源）→ ② `tools/ai_pack/skills/`（发布变体）→ ③ `~/.agents/skills/`（② 字节副本，`sync_skills.ps1 -SyncGlobal`）；新规则带"为什么"，被推翻的同提交删除。— WC §13 + SKILLS_GOVERNANCE.md
14. **公共机制进 `packages/`，模板只放"这个游戏"本身**（判据：换个游戏还成立吗）；动手前先查 packages/ 复用；抽包把配套测试一起搬。— WC §14
15. **大型游戏项目不进仓库**，放 `D:/project/games/<名字>/` 独立项目（一游戏一目录，client/ server/ 分端）；搬迁用 `scripts/export_game_project.ps1`，仓库侧先打 archive tag。— WC §15
16. **测试产物收口**：一用例 = 一条 ctest = 一个 exe，只进 `build\cases\`；日志归档 `build\test-out\`；缓存按最近使用 + 体积上限清扫（`scripts/clean_build.ps1`），不按名字猜着删。— WC §16
17. **多任务并行走调度器**：派发前 `scripts/sched.ps1 register`（同题拒重、超限排队），长任务 heartbeat 续租、结束 finish；共享构建资源与裸 ctest 自动排队，绕不过去。— WC §17

## Build / dev quickstart

**The toolchain of each platform is fixed — see [`docs/BUILD_TOOLCHAIN.md`](docs/BUILD_TOOLCHAIN.md).**
On Windows always configure with clang plus the mozbuild LLVM: letting CMake pick
up TDM-GCC from PATH cannot link zanc, and the failed link deletes
`build\zanc.exe`, taking the whole test suite with it.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
pwsh -File scripts/build.ps1        # 构建（受串行闸保护，多会话时用这个）
# IDE (self-hosted) is a zan.proj project: scripts/build_ide.ps1 (entry src/IdeForm.html).
```

Compile a single program: `build/zanc <file.zan> --auto-stdlib -o out.exe`
Release build of a program: `build/zanc <file.zan> --auto-stdlib --publish -o out.exe`

**Full CLI reference** — every zanc flag, cross target, env var, test recipe,
and the known pitfalls: [`docs/ZANC_CLI.md`](docs/ZANC_CLI.md). When a build/
publish/test command is unclear or "worked yesterday", check there first;
update it in the same commit when changing argument parsing.

### CEF browser driver (optional)

`Gui.Component.CefBrowser` 是 opt-in 原生驱动（默认关闭：configure 会从官方
CDN 拉几百 MB CEF 头文件）：

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DZAN_BUILD_CEF=ON
# Windows: also build the 109 legacy driver for Win7/8/8.1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DZAN_BUILD_CEF=ON -DZAN_BUILD_CEF_LEGACY=ON
cmake --build build --target zan_cef
```

驱动随源码 stage，`zanc --publish` 携带；CEF 运行时首次运行按机下载，
`ZAN_CEF_*` 环境变量见 `examples/gui_cef_browser/README.md`。
