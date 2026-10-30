# AGENTS.md

Guidance for humans and AI agents working in this repo. **本文件只是一行式门禁
清单：每条规则的可执行全文在
[`docs/WORKSPACE_CONVENTIONS.md`](docs/WORKSPACE_CONVENTIONS.md)（下称 WC），
行尾标注对应章节；Zan 编码规范在 `.agents/skills/`（清单与准入标准见
`.agents/skills/SKILLS_GOVERNANCE.md`）。行为守则（八荣八耻、对话与执行纪律）
见用户全局守则与 `zan-dev-standards` §二，此处不重复。**

## Directory map

| Path | Contents |
|---|---|
| `src/compiler` `src/runtime` `src/common` | C11 编译器前后端 / C11 运行时（ARC、集合、IO、调度）/ 共享 C 工具 |
| `src/lsp` `src/dap` | 语言服务器 zan-lsp（intellisense 引擎）/ 调试适配器 zan-dap |
| `src/ide_zan` `src/fmt` `src/doc` `src/selfhost` | 自举 IDE（入口 src/IdeForm.html）/ 格式化 / 文档生成 / 自举编译器（.zan） |
| `stdlib` `packages` | 标准库（.zan + 原生驱动束）/ 可复用包 Zan.* |
| `examples` `templates` `tests` `docs` | 示例 / IDE 新建项目模板 / 测试 / 文档 |
| `cmake` `scripts` | 构建系统与维护脚本 |
| `build` `dist` `_scratch` | 构建/发布产物与一次性工作（均 git-ignored，绝不提交） |

ZanIDE 禁止任何自绘，必须全部用标准库组件来完成。

git hooks（`core.hooksPath`，`cmake -B build` 自动配置）每次 commit/merge/push
机检**遗留冲突标记**与**源码扩展名 >8 MiB 大文件**（WC §0）；`--no-verify`
仅限已证实的误报并写进提交说明。

## Hard rules — 一行门禁，全文见 WC 对应章节

1. **根目录只放长期受控内容**；产物、日志、探针、PR 草稿不进根目录。— §1
2. **一次性文件全进 `_scratch/`**，收尾清理是任务的一部分；长驻大件带一行 README。— §2
3. **只做 out-of-source 构建进 `build/`**；并发构建走 `scripts/build.ps1`，不裸跑 `cmake --build`。— §3
4. **测试进 `tests/` 并提交**；一次性内存/泄漏探针不是测试，进 `_scratch/`。— §4
5. **PR 用 `gh` / 模板**；不在仓库生成 PR 脚本、正文 md、diff 文件。— §5
6. **`.gitignore` 是兜底不是借口**，忽略的文件也不许在源码树堆积。— §6
7. **任务收尾干净、以一次提交结束**：验证过（规则 8）即自行 commit+push，不等批准；绝不 `git add .`。— §7
8. **ctest 整档 opt-in**：默认验证 = 探针 + 单跑受影响用例（`ctest -R <name>`）；档位只在用户点名或发布门槛跑；档位运行期间禁止任何并行构建。— §8
8a. **共享构建资源串行拿**：build.ps1 / test.ps1 / 裸 ctest 共用一把跨进程锁，拿不到排队等；冲突排队，不另开副本树；公共包用主仓 `packages/`。— §8a
9. **永不建分支**，直接提交 main；删任何分支前先证内容已并入（内容判定）。— §9
10. **编译器有缺陷修编译器，禁止绕行写法**：探针 → 修根因 → 补 conformance 单跑 → 再用自然写法。— §10
11. **并发会话验证过即提交**；stash 前 git status、路径限定；冲突逐文件手工合并、UU 清零，禁止一切单边回退。— §11
12. **回退是决定不是调试手段**，默认向前修；仅限用户点名或有证据的陈旧重复。— §12
13. **验证过的教训当天进 skills，三副本同一提交同步**：`.agents/skills/` → `tools/ai_pack/skills/` → `~/.agents/skills/`（`sync_skills.ps1 -SyncGlobal`）。— §13
14. **公共机制进 `packages/`，模板只放"这个游戏"本身**（判据：换个游戏还成立吗）。— §14
15. **测试产物收口**：一用例 = 一条 ctest = 一个 exe，只进 `build\cases\`；日志归 `build\test-out\`；清扫用 `scripts/clean_build.ps1`。— §16
16. **多任务并行走调度器**：`scripts/sched.ps1 register/heartbeat/finish`，同题拒重、超限排队；裸 ctest 也自动排队。— §17

## Build / dev quickstart

- Windows 固定 clang + mozbuild LLVM 配置，别让 CMake 抓到 PATH 里的 TDM-GCC
  （链不上 zanc，失败的链接会删掉 `build\zanc.exe`）——见
  [`docs/BUILD_TOOLCHAIN.md`](docs/BUILD_TOOLCHAIN.md)。
- `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`；并发构建/测试一律走
  `scripts/build.ps1` / `scripts/test.ps1`（规则 8a 串行闸）。
- 单程序：`build/zanc <file.zan> --auto-stdlib -o out.exe`（发布加 `--publish`）。
  全部旗标/交叉目标/测试配方见 [`docs/ZANC_CLI.md`](docs/ZANC_CLI.md)，
  改参数解析时同一提交更新它。
- CEF 浏览器驱动默认关（configure 从 CDN 拉几百 MB）：`-DZAN_BUILD_CEF=ON`
  （Win7/8.1 另加 `-DZAN_BUILD_CEF_LEGACY=ON`）；`ZAN_CEF_*` 环境变量见
  `examples/gui_cef_browser/README.md`。
