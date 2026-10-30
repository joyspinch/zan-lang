# 仓库工作区规范 (Workspace Conventions)

本规范是仓库全部硬规则的**唯一全文**；`AGENTS.md` 的"第 N 条"即本文的 §N
（一行门禁在 AGENTS.md，可执行流程在这里）。所有贡献者（包括 Devin/AI 会话）
都必须遵守。

## 0. 并发踩踏防护闸门（git hooks，强制生效）

本仓库通过 `core.hooksPath` 启用 `scripts/hooks/` 下的闸门脚本，把 §11/§11.1/§12
的纪律变成机器强制，任何会话（含 AI）绕不过：

- **pre-commit**：拦截带 `<<<<<<<`/`=======`/`>>>>>>>` 冲突标记的暂存内容
  （= 一次没做手工合并的单边解决），以及混进源码扩展名的 >8 MiB 大文件
  （构建产物/日志假扮源码）。
- **pre-merge-commit**：`git merge` / `git pull` 前，若索引里**非冲突文件**
  带冲突标记（= 陈旧副本要覆盖已提交内容），拒绝合并。
- **pre-push**：推送前扫描所有待推提交，重复上述两项检查。

绕过方式只有 `git commit --no-verify` / `git push --no-verify`，且仅限
**已证实为误报**的场景，并在提交说明里写明。hook 为纯 POSIX sh + git，
Windows（Git Bash）与 Linux 行为一致；`cmake -B build` 配置阶段会自动设置
`core.hooksPath`（见根 CMakeLists.txt），新 clone/新 worktree 首次配置即生效。

hooks 管不住的部分（untracked 堆积、stash 纪律、路径限定 add）仍是
本规范 §2/§7/§11 的人工义务。

## 1. 根目录保持整洁
仓库根目录只允许存放**长期、受版本控制**的内容：源码目录（`src/`、`stdlib/`、`examples/`、`tests/`、`docs/`、`cmake/`、`scripts/`、`toolchain/`、`assets/`）、`CMakeLists.txt`、`README.md`、`LICENSE`、`.gitignore` 等。

禁止在根目录留下：
- 编译产物（`*.exe`、`*.dll`、`*.so` 以及无扩展名的原生二进制）
- 运行/调试日志（`dbg_*.txt`、`iodbg*.txt`、`debug*.txt`、`ir.txt`、`stdout`、`srv_pid.txt`）
- PR/提交流程产物（`commit_msg*.txt`、`pr*_body.md`、`pr*.diff`、`*.patch`、`pr_meta.json`、`.dev_*`）
- 一次性脚本（`mkpr*.py`、`updpr*.py`、`push.bat`、`_*.ps1`）

## 2. 所有临时文件放进被忽略的目录
任何调试、探针、benchmark、草稿文件一律放进 `_scratch/`（已被 `.gitignore` 忽略）。不要散落在源码树里。

**清理是任务收尾的一部分，不是可选项。**（2026-09 清理时 `_scratch` 已积到 48G：bisect 整树、A/B 快照、SDK 解压副本只进不出，没人认领。）规则：

- 会话里造的树**当场造、当场收**：bisect 用 `git worktree add _scratch/xxx_wt <commit>` 开树，定位完 `git worktree remove --force` + `git worktree prune`（掉注册的 worktree 目录就是纯垃圾）；A/B 对照、stdlib 整树快照**复用固定目录名**（如 `_scratch/zanc-good`、`_scratch/zanc-mine`）覆盖使用，不新起名字，对照一结束当场删。二分定位的 worktree 只当检查点快照，**不当构建环境/公共包来源**（见 §8a）。
- 确实要留的大件（SDK、工具链、会话库等再取成本高的）**必须带一页 `README.md`**，写明是什么、怎么再取；带 README 的条目会被 `scripts/clean_scratch.ps1` 跳过。
- `_scratch` 涨过几个 GB 就跑 `scripts/clean_scratch.ps1`：默认 dry-run 只列清单，`-Apply` 才真删；默认清 7 天前的条目（按条目内最新文件时间算，别按目录自身时间），`-KeepDays N` 可调。

## 3. 构建产物只进 build/
使用 CMake 的 out-of-source 构建：`cmake -B build && cmake --build build`。产物留在 `build/`（已忽略），绝不手动拷到根目录或提交。并发时构建走 `scripts/build.ps1`（§8a）。

## 4. 测试放对位置
真实测试进 `tests/` 并纳入版本控制；一次性内存/泄漏探针进 `_scratch/` 或 `tests/leakprobe/`（后者已被忽略），不要混在正式测试里。

## 5. PR 使用规范工具
使用 `gh` CLI / PR 模板创建 PR，不要在仓库里生成 `mkpr*.py`、PR 正文 md、diff 文件。这些草稿如需保留，放 `_scratch/`。

## 6. .gitignore 是兜底，不是借口
即使某类文件已被忽略，也不应在源码树里堆积；忽略规则只是防止误提交，整洁仍需人为维护。

## 7. 收尾清理与提交
每轮任务结束前运行 `git status`，确认没有多余的未跟踪文件。若有，删除或移入 `_scratch/`。提交前 review `git diff --stat`，只提交与任务相关的改动，绝不盲目 `git add .` / `git add -A`。

**验证通过后应自行提交**：验证方式按 §8——探针 + 受影响单用例跑通即为验证
完成。通过后直接 commit 到 `main` 并尝试 push——不要等待用户批准，也不要把
已验证的工作留在未提交状态。push 因网络等原因失败时，说明情况即可，提交留在
本地 `main`。仅当验证未通过，或工作区混入了不属于本任务、不可触碰的在途改动
时才暂缓提交（此时明确说明留了什么、为什么）。

## 8. ctest 整档 opt-in——默认验证是探针 + 受影响单用例

ctest 整档（smoke/standard/full，`scripts/test.ps1 <tier>` 或 `ctest -L <tier>`）
**不是默认验证步骤，包括编译器/运行时/stdlib 改动**：整档把每个核打满几十分钟，
且重链 `zanc` 会让全部测试产物失效。

- 默认验证直接且便宜：编译并运行受影响的程序/探针
  （`build\zanc.exe <file.zan> --auto-stdlib -o _scratch\out.exe`）、diff 受影响的
  金样/诊断，有同名测试用例就**单跑该用例**（`ctest -R conformance_<name>`，秒级；
  改动面局部时再窄化，如泛型/ARC 改动 `-R "generic|leakcheck_generic"`）。
- 不要把整档当 pre-commit 步骤，也不要为小的后续修改重跑整档：只有**用户明确
  要求**或**约定好的发布门槛**（`full`——determinism/leakcheck 孪生 + 自举——仅是
  发布门）才跑整档。
- 手跑探针 + 受影响单用例就是完整验证，不需要"升级"成整档。
- **档位运行期间不得并行任何构建**：用例共享 `build\zanc.exe` 与 stdlib stamp，
  并发的 build_gallery/build_ide 会让无关用例成片假红。

### 8a. 共享构建资源要串行拿，不要另开一份

多会话同时用一个 `build/` 时，编译与测试必须互斥——机制已落地，不靠自觉：

- 构建：`pwsh -File scripts/build.ps1`（**不要裸跑 `cmake --build build`**）。
- 测试：`pwsh -File scripts/test.ps1 <tier> [-Match <name>]`。
- 两把脚本共用一把跨进程锁（`scripts/build_gate.ps1`），拿不到就**排队等**
  （默认 30 分钟超时，超时报错而不是静默绕过）；看谁占着锁：`-Status`。
- **为什么是"等"而不是"另建一棵构建树"**：另建（`build_mine`、`zan-pkgs`…）
  是用磁盘换隔离——几百 MB 到 2 GB 的重复构建，公共包一改副本就漂移；副本没有
  让共享状态消失，只是把它藏起来，漂移照旧（实测一次并发多出 2.7 GB 重复构建
  树 + 一个 247 MB 仓库副本，公共包仍旧不同步）。
- 公共包**不要用 worktree 副本**——直接用主仓库 `packages/`（编译时
  `--package-project zan-lang`）；worktree 会漂移且规则 9 禁分支。二分定位的
  短命 worktree 除外（§2）——它只当检查点快照，不当包来源/构建环境。

## 9. 分支完结即删——删除前先证内容已并入

仓库纪律是**永不建分支、只推 main**（AGENTS.md 第 9 条），但仍会出现并行会话
或外部工具留下的分支。分支滞留本身不是"合并待办"——很多分支的工作早已通过
cherry-pick / 重写提交 / 并行同修进了 main，只有血缘断了
（`git log main..<branch>` 永远显示"未合并"）。判据是**内容，不是提交号**。

**删除一个分支前的证据清单**（每条独占提交都要过）：

1. `git log --oneline main..<branch>` 列出提交号上 main 没有的提交；
2. 逐提交找 main 里的对应物：同文件 blob 对比
   （`git rev-parse <branch>:<file>` vs `main:<file>` / `<等价提交>:<file>`）、
   或按提交信息/特征内容（函数名、TASKS 条目）grep main 侧历史；
3. 三种可判"已并入"的结论：① tip 是 main 祖先；② 有逐字节等价的
   main 提交；③ main 侧以**超集形态**包含其改动（后续提交在同一文件上
   继续演进，分支侧只是旧快照——用"main 在该文件的后续提交数 + 特征
   内容已在"作证）；
4. 证据齐了先钉 tip 再删：`git tag archive/<分支名>-<日期> <tip>`，
   然后 `git branch -D`（远端 `git push origin --delete <branch>`）。
   archive tag 保留至少一个月后可随大扫除清理。

**禁止**：看到 `not fully merged` 就把分支留着当"待办"、或反过来不取证据
直接 `git branch -D` 扔掉真未合并的工作；拿不准的分支在交付说明里列出来
请人工裁决。

## 10. 编译器有缺陷就修编译器，不要绕过
上层 Zan 代码（stdlib、examples、IDE、工具链）跑不通时，若根因在编译器或运行时，
**默认做法是去修 `src/compiler/` / `src/runtime/`**，而不是把 Zan 代码改写成一种
能躲开缺陷的形态。典型的"绕过"包括：`foreach` 坏了就手写 `while (i < n)` 下标循环、
`byte[]` 坏了就用 `string` / `NativeMemory` 手搓字节缓冲、Zan 实现不可达就在 C 侧
加一层硬编码、数组没有长度就额外传一个 `count` 参数。这些形态正是 `TASKS.md`
A15 里那批债务的来源：每绕一次，根因就被掩盖一次，并顺着标准库扩散。

流程：
1. 在 `_scratch/` 里缩成一个最小探针，确认是编译器/运行时的问题而不是用法问题；
2. 定位根因并修复，宁可改 irgen / checker / runtime，也不改上层写法；
3. 在 `tests/conformance/` 补一个用例锁住行为；
4. 验证：探针跑通 + 新增 conformance 用例**单跑**（`ctest -R conformance_<name>`，
   秒级）；ctest 整档只在用户明确要求或发布门槛时跑（§8），不要默认跑档；
5. 然后再用自然的 Zan 写法改写上层代码。

确实超出当前任务范围时**也不能静默绕过**：把探针和根因写进 `TASKS.md` 对应章节
（新问题编号顺延），在交付说明里明确指出，取得一致后才允许临时形态存在。

## 11. 并发会话共享同一工作区——改完即提交
多个 AI 会话/开发者可能同时在本工作区写入。任何停留在工作区或暂存区的已验证改动，
随时可能被其他会话的 `git stash pop`、`git reset`、大范围 `git checkout` 覆盖——
表象是"已提交的功能被批量回退"，实际是陈旧快照踩踏了共享工作区。

- 每个完整改动在验证通过后**立即 commit 并 push**，不跨步骤攒批，
  不把已完成的工作长期留在工作区/暂存区。
- stash 纪律：`git stash` / `pop` / `apply` 前先 `git status`。树上若有其他会话的
  在途改动或未跟踪文件，用路径限定的 `git stash push -- <paths>` 只搁置自己的文件，
  或把并行工作挪进独立 `git worktree`；**不要把旧快照 pop 到新 HEAD 之上**。
- pop 发生冲突时：以已提交的内容为准解决冲突，只重新套用真正的新工作；
  绝不提交一批会删除已提交特性的大回退 diff。

### 11.1 冲突与并发读写：修复合并，禁止回滚丢弃

共享工作区里 UU 冲突、`<<<<<<<` 标记、或对同一文件的并发编辑，含义是**两条工作线
都需要保留**，处置方式是手工合并，而不是让冲突"消失"。

**逐文件修复流程**（每个冲突文件都过一遍，不许跳）：

1. 读两侧内容：`git show :2:<file>` 是已提交/HEAD 一侧，`git show :3:<file>` 是
   在途（stash/并行会话）一侧；`git log --oneline -5 -- <file>` 看两条线各自的
   提交意图。
2. 合并原则：**已提交的特性无条件保留**；在途一侧只取"HEAD 里没有的真新工作"。
   两侧各自新增的代码块通常都要；同一段代码两边改了，按语义取舍后手工重排，
   必要时把两侧的意图都接住（如两批新 API 并存）。
3. 判定某侧可丢弃必须有证据：grep HEAD 确认内容已提交，或 blob 对比
   （`git show :3:<file> | git hash-object --stdin` 对比 `HEAD:<file>`），
   证明它是已提交内容的陈旧副本——并在交付说明里写明证据。
4. 逐文件 `git add <file>` 标记解决；**必须清零每一个 UU**
   （`git status` / `git diff --name-only --diff-filter=U` 为空）后才算修完。
5. 用最窄的方式验证：直接编译运行受影响的程序/探针；确需测试档时按改动面
   `-R` 窄化，不要整档跑 `standard`/`full`。通过后立即 commit。
   commit message 说明冲突来源与两侧取舍。

**明令禁止的"解法"**（全部等同于把并行会话的工作批量回退）：

- `git checkout --ours .` / `git checkout --theirs .` / `git checkout .` 整树单边
- `git reset --hard`、`git merge --abort` / `git stash pop` 冲突后不处理直接再 pop
- 删除冲突文件再从某一侧拷回旧版本
- 挑几个文件解决、剩下的 UU 留给"下一个会话"
- 留着 `<<<<<<<` 标记提交

**判断题**：只有一种情况允许单边——有第 3 步的证据证明另一侧是陈旧重复。
其余一切冲突都按第 1-5 步合并。修不动、两侧语义无法调和时，停下来在交付说明里
列明冲突文件与两侧差异，请人工裁决；不要为了"收尾干净"倒向任何一侧。

## 12. 回退是决定，不是调试手段——禁止随意回退代码

回退代码的默认动机是"让眼前的问题消失"，这会把别人（或上一个会话）已验证的
工作一起埋掉。**默认永远向前修**：复现 → 定位根因 → 修复；而不是 `git
checkout` / `git restore` / `git reset --hard` 抹掉报错、拿旧版本文件整个
覆盖、`git revert` 别人的提交、或因为行为不对就把刚提交的代码注释/删掉。

允许回退的只有两种情况：

1. 用户明确要求回退；
2. 有证据证明目标代码是陈旧重复或确定损坏（`git log` / `git blame` 查来历、
   blob 对比证内容），并在交付说明里写明证据。

涉及其他会话的在途改动或已提交特性时，先停下来向用户说明，取得同意再动手；
冲突场景一律走 §11.1 的手工合并流程。拿不准就报告问题，不许为了"收尾干净"
倒退回去。

## 13. 经验沉淀进 skills——三副本同一提交同步

验证过的教训必须进 skill，且持续增删迭代。项目级 skills 在 `.agents/skills/`
（当前 13 个；清单、定位与准入标准见
`.agents/skills/SKILLS_GOVERNANCE.md`）。规则：

- 会话中**验证过**的新经验（踩坑后成立的解法）当天并入对应 skill，
  与功能同一提交；跨项目可复用的写通用节，项目专属写项目节。
- 被**推翻**的旧经验在同一提交里删除——skill 里不留"可能也对"的
  僵尸规则，宁缺毋滥。
- 每条规则带"为什么"（踩过的坑）；没有坑出处的新增不予合入。
- 动手前先查 skill：同类的坑别人踩过就不要再踩一遍。
- **三份副本，同一提交走完全程**（只改工作区 = 用户项目永远收不到教训）：
  ① 工作区 `.agents/skills/`（活源，LF）→ ② `tools/ai_pack/skills/`
  （发布变体：剥离"在 zan-lang 仓库内工作"节、仓库内路径改写成通用说法）→
  ③ `C:\Users\<user>\.agents\skills\`（②的字节副本，zcode 在仓库外使用）。
  `scripts\sync_skills.ps1` 报告 ①↔② 漂移（VARIANT 条目 = 人工维护的
  变体，需人工比对），`-SyncGlobal` 把 ② 字节复制到 ③；② 再由
  `publish_ide.ps1` 随 SDK 进 `dist\ai\skills\`。
- skill 写作规范（行数上限、description 长度、references/ 拆分、路径可落地）
  以 `SKILLS_GOVERNANCE.md` 为准。

## 14. 公共机制进包，模板只放"这个游戏"本身

`templates/` 里出现通用子系统（物流/配方、存档、寻路、成长表、回合规则……）
就是放错了——判据是"**换个游戏还成立吗**"：成立 → 抽成 `packages/Zan.*` 包，
模板只留玩法规则、界面、入口与资源。模板是"怎么做一个游戏"的示范，公共库散在
模板里会被每个项目各抄一份，修复无法回流。

- 动手前先查 `packages/` 是否已有同类模块（复用优先，§10）。
- 抽包时把配套的无头测试一起搬过去——包的测试跑在包上，不是跑在某个模板上。
- 坑出处：coreforge 曾在模板里长出 261KB 工厂自动化引擎，抽出为
  `Zan.Game.Factory` 时才发现其存档读回全线失败（查裸字段判整数、解析出的
  JSON 数字带 `numRaw` 文本）——被模板藏起来的腐化，进了包就会被迫面对。

## 15. 测试产物必须收口，不许散着堆

每个 conformance 用例都把自己的可执行文件编到 `build\` 下——历史上全编在
`build\` 根目录，几百轮测试之后根目录堆了 **1884 个一次性 exe / 6 GB**，
真产物只剩 4 个。规矩：

- **一个用例 = 一条 ctest 条目 = 一个可执行文件**：conformance / 泄漏 /
  ARC 三个结论共用同一份插桩产物（`build\cases\case_*.exe`），determinism
  另立一条 `case_det_*`（它要比两次代码生成，多半不产可执行文件，只留一枚
  stamp）。**别再把同一个源编成多个 exe**——孪生档的老写法是同一份源码编
  三遍（普通版 / `--check-leaks` / `--arc-guard`）跑三遍，一轮全量下来是
  3500+ 个 exe、4700 个一次性文件，而每个程序本身只跑几毫秒。
- **用例产物只有一个去处**：`build\cases\`（`CMakeLists.txt` 里所有
  `OUT_EXE=${CMAKE_BINARY_DIR}/...` 都指向那里）。`build\` 根只留真产物
  与工具链。新增用例时不要另开目录，也不要写回根目录。
- **它是缓存，不是垃圾**：`run_case_modes.cmake` 靠"源文件/编译器/stdlib
  stamp 没变就复用旧 exe"加速。全删等于每轮重编一千多个程序，所以按
  **最近使用 + 体积上限** 清扫，别一把删光——`scripts\clean_build.ps1`
  干这件事（`-KeepDays` / `-MaxCacheMB`）。
- **日志合并成一份**：`ctest` 的完整输出归档到
  `build\test-out\<tier>-<时间>.log`，只留最近 5 份；不要每个用例一个
  输出文件散在 `build\` 里。
- 清扫在 `scripts\test.ps1` 跑完 ctest 后自动执行；手动扫用
  `scripts\clean_build.ps1`（默认 dry-run，`-Apply` 才删）。
- 坑出处：`build\` 根的用例 exe **不是 cmake 编译产物**（没有同名
  `.dir` 中间态，是 `tests/run_case.cmake` 每次现场编译的），所以删掉
  安全、下次会重新生成——但 `build\` 根同时躺着 `ld.lld.exe`、
  `repomap.exe`、`zan-mcp.exe` 等真工具链，**按名字猜着删会踩到它们**，
  所以保留清单写死在 `clean_build.ps1` 里，不按通配符删。

## 16. 多任务并行靠调度器，不靠"复制一份躲开"

仓库里已经有自动协调机制，用它，别手抄、别另开一份：

- **共享构建资源**：`scripts/build.ps1`、`scripts/test.ps1`、**裸 `ctest`**
  都走同一把锁（`scripts/sched.ps1` 的租约）。裸 ctest 也自动排队——钩子
  挂在 CMake 生成的 `CTestCustom.cmake` 上（`scripts/ctest_gate.ps1`），
  谁跑都一样，**绕不过去**。拿不到就排队等（默认 30 分钟，超时报错，
  不静默绕过）。
- **任务准入**：派 worker 前先
  `pwsh -File scripts/sched.ps1 register -Title "<任务名>"`——**同题已在跑
  直接拒**（exit=2，点名是谁在跑），并发超上限就排队（默认 4）。长任务
  用 `heartbeat -Id <lease>` 续租，结束时 `finish -Id <lease>`（顺手清掉
  它登记的副本）。派发前先 `status` 看一眼有几条在跑。
- **副本自动回收**：`scripts/sched.ps1 reap`（每次 build/test 跑完自动
  执行）清掉过期任务租约，以及**没人引用的仓库外 worktree 副本**——删前
  先证明"内容已在主仓"（工作树干净 + HEAD 是 main 的祖先）；有独有提交
  或未提交改动的会保留并报出来，不硬删。
- 机制细节与踩过的坑见 `.agents/skills/zan-dev-standards/references/verification.md`
  的「并发会话的串行纪律」与「PowerShell 脚本协作的几个坑」两节。

---

## 附录：docs 文档维护规则（原 DOCS_MAINTENANCE.md，2026-09-26 并入）

本节定义 `docs/` 下所有文档的维护约定。目标是让每份文档的**可信度一眼可判**，
并防止"文档与实现漂移"再次发生（历史问题见 `TASKS.md` 的 C 组）。

### 1. 分层：当前实现 / 目标设计 / 历史记录

每份文档（或文档内每个小节）必须能被归入以下三层之一，并在显要位置标明：

- **当前实现（current implementation）**——描述代码此刻的真实行为。**以实现和 C# 为准**，
  不以旧文档为准（见 §3）。写明核实来源（源文件路径 / 测试 / 探针）与核实日期。
- **目标设计（target design）**——描述尚未落地的意图。**必须写明它依赖哪个能力项**
  （`TASKS.md` 里的 A 编号），并注明"该能力落地前本节不为真"。
- **历史记录（history / archive）**——已过时、仅作参考。移入 `docs/archive/`，
  顶部加归档说明（日期 + 指向当前来源）。**不要用"本文已过时"横幅代替归档或重写。**

一份文档可以同时含前两层，但要分节标清，例如 `docs/ABI.md` 的 §1.1「Document status」。

### 2. 目标设计随能力落地同步更新

当某个 A 能力项（`TASKS.md`）落地时，**同一次改动**要把所有"依赖该 A 编号"的目标设计
段落改写成当前实现层，或删除已不成立的描述。反向也成立：发现实现变了而文档没跟上，
按 §3 就地修正。

### 3. 以实现和 C# 为准（C10）

- 文档与实现冲突时，**改文档**，不要改实现去迁就文档。
- 已知不合理/失真的描述直接按实现重写或删除。
- 反向检查：文档里记录的、偏离 C# 习惯的设计，优先提出按 C# 靠拢（在 `TASKS.md` 立项），
  而不是把偏差固化进文档。

### 4. bug 文档状态字段（C6）

`docs/archive/bugs/*.md` 每份在标题下第一行给出统一状态字段：

```
**Status:** Fixed (YYYY-MM-DD) — <task ref>；see Resolution / TASKS.md <编号>.
```

或 `**Status:** Open — <blocker>`。读者不必读全文即可判断该 bug 是否仍然有效。

### 5. 位置与索引

- `docs/README.md` 是全部文档的索引（分区分层 + 一句话说明）。
- 语言/编译器/标准库等**现行**文档放 `docs/` 根目录。时点报告、被推翻的设计、
  已全部落地的计划不是现行文档——移入 `docs/archive/`，顶部加归档说明
  （日期 + 指向现行来源）。
- 项目专属文档：`docs/projects/<project>/`。
- 历史/过时文档：`docs/archive/`，冻结不再随实现更新。

### 6. 移动/归档必须同步引用

根目录文档被源码注释、脚本、skills 按路径锚定（`publish_ide.ps1` 有固定发布
清单，skills 有三份副本：`.agents/skills/` → `tools/ai_pack/skills/` →
`C:\Users\<user>\.agents\skills\`）。移动或归档文档时，同一次提交里：

1. 全仓库 grep 旧路径，逐处改指新位置（含 skills 三副本）；
2. 更新 `docs/README.md` 索引。
