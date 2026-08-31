# 仓库工作区规范 (Workspace Conventions)

本规范用于避免仓库根目录再次被当成临时工作区。所有贡献者（包括 Devin/AI 会话）都必须遵守。

## 1. 根目录保持整洁
仓库根目录只允许存放**长期、受版本控制**的内容：源码目录（`src/`、`stdlib/`、`examples/`、`tests/`、`docs/`、`cmake/`、`scripts/`、`toolchain/`、`assets/`）、`CMakeLists.txt`、`README.md`、`LICENSE`、`.gitignore` 等。

禁止在根目录留下：
- 编译产物（`*.exe`、`*.dll`、`*.so` 以及无扩展名的原生二进制，如 `http_srv_new`、`ws_srv_linux`）
- 运行/调试日志（`dbg_*.txt`、`iodbg*.txt`、`debug*.txt`、`ir.txt`、`stdout`、`srv_pid.txt`）
- PR/提交流程产物（`commit_msg*.txt`、`pr*_body.md`、`pr*.diff`、`*.patch`、`pr_meta.json`、`.dev_*`）
- 一次性脚本（`mkpr*.py`、`updpr*.py`、`push.bat`、`_*.ps1`）

## 2. 所有临时文件放进被忽略的目录
任何调试、探针、benchmark、草稿文件一律放进 `_scratch/`（已被 `.gitignore` 忽略）。用完即删。不要散落在源码树里。

## 3. 构建产物只进 build/
使用 CMake 的 out-of-source 构建：`cmake -B build && cmake --build build`。产物留在 `build/`（已忽略），绝不手动拷到根目录或提交。

## 4. PR 使用规范工具
使用 `gh` CLI / PR 模板创建 PR，不要在仓库里生成 `mkpr*.py`、PR 正文 md、diff 文件。这些草稿如需保留，放 `_scratch/`。

## 5. 测试放对位置
真实测试进 `tests/` 并纳入版本控制；一次性内存/泄漏探针进 `_scratch/` 或 `tests/leakprobe/`（后者已被忽略），不要混在正式测试里。

## 6. 收尾清理与提交
每轮任务结束前运行 `git status`，确认没有多余的未跟踪文件。若有，删除或移入 `_scratch/`。提交前 review `git diff --stat`，只提交与任务相关的改动，绝不盲目 `git add .` / `git add -A`。

**验证通过后应自行提交**：相关测试档（`scripts\test.ps1 <tier>` / `ctest -L <tier>`）通过后，直接 commit 到 `main` 并尝试 push——不要等待用户批准，也不要把已验证的工作留在未提交状态。push 因网络等原因失败时，说明情况即可，提交留在本地 `main`。仅当验证未通过，或工作区混入了不属于本任务、不可触碰的在途改动时才暂缓提交（此时明确说明留了什么、为什么）。

## 7. .gitignore 是兜底，不是借口
即使某类文件已被忽略，也不应在源码树里堆积；忽略规则只是防止误提交，整洁仍需人为维护。

## 8. 编译器有缺陷就修编译器，不要绕过
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
4. 然后再用自然的 Zan 写法改写上层代码。

确实超出当前任务范围时**也不能静默绕过**：把探针和根因写进 `TASKS.md` 对应章节
（新问题编号顺延），在交付说明里明确指出，取得一致后才允许临时形态存在。

## 9. 并发会话共享同一工作区——改完即提交
多个 AI 会话/开发者可能同时在本工作区写入。任何停留在工作区或暂存区的已验证改动，
随时可能被其他会话的 `git stash pop`、`git reset`、大范围 `git checkout` 覆盖——
表象是"已提交的功能被批量回退"，实际是陈旧快照踩踏了共享工作区。

- 每个完整改动在其测试档通过后**立即 commit 并 push**，不跨步骤攒批，
  不把已完成的工作长期留在工作区/暂存区。
- stash 纪律：`git stash` / `pop` / `apply` 前先 `git status`。树上若有其他会话的
  在途改动或未跟踪文件，用路径限定的 `git stash push -- <paths>` 只搁置自己的文件，
  或把并行工作挪进独立 `git worktree`；**不要把旧快照 pop 到新 HEAD 之上**。
- pop 发生冲突时：以已提交的内容为准解决冲突，只重新套用真正的新工作；
  绝不提交一批会删除已提交特性的大回退 diff。
