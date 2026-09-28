# AGENTS.md


Guidance for humans and AI agents (Devin, Copilot, etc.) working in this repo.
**Read this before creating files.** The full rules live in
[`docs/WORKSPACE_CONVENTIONS.md`](docs/WORKSPACE_CONVENTIONS.md); this file is the
short, enforceable summary.

## AI 编码八荣八耻（行为守则，所有会话始终遵守）

以瞎猜接口为耻，以认真查询为荣
以模糊执行为耻，以寻求确认为荣
以臆想业务为耻，以人类确认为荣
以创造接口为耻，以复用现有为荣
以跳过验证为耻，以主动测试为荣
以破坏架构为耻，以遵循规范为荣
以假装理解为耻，以诚实无知为荣
以盲目修改为耻，以谨慎重构为荣
以低阶模型为耻，以深度思考为荣

## Directory map — where things go

| Path            | Contents                                                        |
|-----------------|-----------------------------------------------------------------|
| `src/compiler/` | Compiler front/back end (C11): lexer, parser, binder, checker, irgen |
| `src/runtime/`  | Runtime library (C11): ARC, strings, collections, IO, scheduler |
| `src/common/`   | Shared C utilities (json, rpc, host_oom)                        |
| `src/lsp/`      | Language server (`zan-lsp`) + `intellisense.{c,h}` engine       |
| `src/dap/`      | Debug adapter (`zan-dap`) + `debugger.{c,h}` engine             |
| `src/ide_zan/`  | The IDE — self-hosted, written in Zan (`ZanIDE.zan` + components) |
| `src/fmt/`, `src/doc/` | Formatter, doc generator   |
| `src/selfhost/` | Self-hosted compiler sources (`.zan`)                           |
| `stdlib/`       | Standard library (`.zan` source + native driver bundles)        |
| `examples/`     | Curated, runnable example programs (each with its own README)   |
| `templates/`    | Project templates used by the IDE "New Project" flow            |
| `tests/`        | Version-controlled test suite                                   |
| `docs/`         | Documentation                                                   |
| `cmake/`, `scripts/` | Build system + maintained helper scripts                   |
| `build/`        | Out-of-source build output (git-ignored, never commit)          |
| `dist/`         | Release staging output (git-ignored, never commit)              |
| `_scratch/`     | ALL throwaway work: probes, benchmarks, logs, drafts (git-ignored) |

> Note: the old C IDE (`src/ide/`) was removed. The IDE is now `src/ide_zan/`;
> its analysis/debug backends live in `src/lsp/` and `src/dap/`.
## ZanIDE
ZanIDE禁止任何自绘必须全部用标准库组件来完成

## Git gate hooks — enforced rules (always on)

The repo enables `scripts/hooks/` via `core.hooksPath` (set automatically by
`cmake -B build`; see `docs/WORKSPACE_CONVENTIONS.md` §0). Every commit,
merge/pull and push is machine-checked for **leftover conflict markers**
(rules 11/12 — a blocked commit means the conflict was "resolved" without a
hand merge) and **>8 MiB blobs on source extensions** (rules 1–3). Fix the
underlying problem; bypass with `--no-verify` only for a proven false
positive, stated in the commit message.

## Hard rules — DO NOT break these

1. **Keep the repo root clean.** Only long-lived, version-controlled entries
   belong there (source dirs, `CMakeLists.txt`, `README.md`, `LICENSE`,
   `.gitignore`, `AGENTS.md`). Never drop build artifacts, logs, probes,
   one-off scripts, PR drafts, diffs, or patches in the root.
2. **All throwaway files go in `_scratch/`** (git-ignored). This includes
   debug probes, benchmarks, ad-hoc scripts, screenshots, `*.log`, PR bodies,
   commit-message drafts, `*.diff`/`*.patch`. **Cleanup is part of finishing
   the task** — `_scratch` once hit 48G because it only ever grew: before
   committing, delete what you created this session (bisect trees via
   `git worktree add`/`git worktree remove`, A/B compiler snapshots, stdlib
   copies — reuse fixed names like `_scratch/zanc-good` instead of minting
   new ones). Keepers (SDKs, toolchains, anything expensive to re-acquire)
   must carry a one-line `README.md` saying what it is and how to re-acquire
   it; README-marked entries are skipped by `scripts/clean_scratch.ps1`,
   which sweeps entries older than 7 days (`-Apply` to actually delete,
   dry-run by default) — run it when `_scratch` grows past a few GB.
3. **Build only out-of-source into `build/`** via `cmake -B build && cmake --build build`.
   Never copy build products into the source tree or root.
4. **Tests go in `tests/`** and are committed. One-off memory/leak probes are
   NOT tests — put them in `_scratch/`.
5. **Use `gh` / the PR template** to open PRs. Do not generate `mkpr*.py`,
   PR-body `.md`, or `*.diff` files in the repo.
6. **`.gitignore` is a safety net, not a license to litter.** Even ignored
   files must not pile up in the source tree.
7. **Every task ends clean — and ends with a commit.** Run `git status` before
   finishing; there should be no stray untracked files. Review
   `git diff --stat` and commit only what the task requires. Never
   `git add .` / `git add -A` blindly. Once the change is verified (rule 8),
   **commit on your own**: do not ask for permission and do not leave verified
   work uncommitted. If `git push` fails (e.g. network), leave the commit on
   local `main` and say so. Hold the commit only when verification failed or
   the working tree mixes in unrelated in-flight changes you must not touch.
8. **ctest tiers are opt-in, never a reflex — do not saturate the machine.**
   A tier run fans out across every core for tens of minutes and maxes the
   CPU, and relinking `zanc` invalidates every test artifact — so tiers are
   NOT the default verification step, **including for compiler/runtime/
   stdlib changes**. Default verification is direct and cheap: compile and
   run the affected program/probe (`build\zanc.exe <file.zan> --auto-stdlib
   -o _scratch\out.exe`), diff the one affected golden/diagnostic, and run
   the single affected test case when one exists (`ctest -R
   conformance_<name>` — seconds, vs tens of minutes for a tier; narrow
   further with `-Match`/`-R` when the change is local, e.g. a generics/ARC
   change: `-R "generic|leakcheck_generic"`). Do NOT run `smoke`/`standard`/
   `full` tiers (`scripts\test.ps1 <tier>`, or `ctest -L <tier>`) as a
   pre-commit step, and do not re-run a tier after small follow-up edits:
   run a tier only when the user explicitly asks for it, or at an agreed
   release gate (`full` — determinism/leakcheck twins + self-hosting — is a
   release gate only). A hand-run probe plus the affected single case is a
   complete verification; it does not need to be "upgraded" to a tier run.
   **Nothing else may build while tests run**: the cases share
   `build\zanc.exe` and the stdlib stamp, so a concurrent
   `build_gallery`/`build_ide` makes unrelated cases fail en masse.
9. **Never create branches.** Do NOT create local or remote branches; commit
   directly to `main` and push. Delete any stray branch you find after making
   sure its commits are merged into `main`. Content check, not name check:
   `git log main..<branch>` alone is not proof of unmerged work — a
   cherry-picked or re-committed fix has no ancestry link. Before deleting,
   prove every unique commit's content lives in main (blob-compare per file
   `git rev-parse <branch>:<file>` vs `main:<file>`, or find the equivalent
   commit), keep the tip reachable (`git tag archive/<name>-<date> <tip>`),
   then `git branch -D`. Ancestry-only reasoning ("tip is ancestor of main")
   plus grep of the main-side feature is the evidence to write into the
   delivery notes.
10. **Fix the compiler, don't route around it.** When Zan code fails because the
    compiler/runtime has a bug or a missing capability, the default is to fix
    the compiler or runtime. Do NOT rewrite the Zan code into a shape that
    dodges the defect (hand-rolled `while` instead of a broken `foreach`,
    `string`/`NativeMemory` instead of a broken `byte[]`, a C shim instead of
    the Zan implementation, an extra `count` parameter because arrays lack a
    length). Those workarounds are how the debt in A15 accumulated: each one
    hides the root cause and spreads through the standard library.
    Procedure: reduce it to a minimal probe in `_scratch/`, find the root cause
    in `src/compiler/` or `src/runtime/`, fix it, add a `tests/conformance/`
    case and run just that case (rule 8 — the probe plus the new conformance
    case IS the verification; a tier run is not required and must not be run
    by reflex), and only then write the natural Zan code. If the fix is genuinely out
    of scope for the current task, do not silently work around it: record it in
    `TASKS.md` with the probe and the root cause, say so explicitly, and get
    agreement before shipping any temporary shape.
11. **Concurrent sessions share this working tree — commit finished work the
    moment it is verified.** Do not accumulate completed changes in the working
    tree or index across steps: another session's `git stash pop`, `git reset`,
    or broad `git checkout` can silently overwrite them, which reads as a
    mass-revert of committed features. Commit (and push) each coherent change
    as soon as it is verified (rule 8). Stash discipline: before any
    `git stash`/`push`/`pop`/`apply`, run `git status` — if the tree holds
    another session's in-flight edits or untracked files, isolate your own work
    with a targeted path spec (`git stash push -- <paths>`) or use a separate
    `git worktree`; never pop a stale snapshot over a newer HEAD.
    **Conflicts are repaired, never rolled back or discarded.** Hitting UU index
    entries, `<<<<<<<` markers, or concurrent edits to the same files means two
    lines of work must be **merged by hand**: for each conflicted file compare
    both sides (`git show :2:<file>` = committed/HEAD side, `:3:<file>` =
    in-flight side) against HEAD, keep committed features unconditionally, fold
    in genuinely new work from the other side, verify with the narrowest check
    (rule 8 — probe/single case, not a tier), resolve **every** UU entry, and
    commit. Making the conflict "go away"
    is forbidden — no bulk one-sided resolves (`git checkout --ours/--theirs .`,
    `git checkout .`), no `git reset --hard`, no deleting the file or copying an
    older version over it: all of those silently revert committed features and
    read as a mass-revert. Drop one side only after proving it is a stale
    duplicate of already-committed content (evidence: HEAD grep / blob compare).
    Do not leave unresolved conflicts or markers for the next session. Full
    procedure: `docs/WORKSPACE_CONVENTIONS.md` §9.1.
12. **Never revert code casually — reverting is a decision, not a debugging
    move.** Rolling code back to make an error "go away" is forbidden by
    default: no `git checkout` / `git restore` / `git reset --hard` to dodge a
    build or test failure, no copying an older version of a file over the
    current one, no `git revert` of commits you did not author, no commenting
    out or deleting freshly committed code because it misbehaves. The default
    is to **fix forward**: reproduce, find the root cause, repair. A revert is
    legitimate only when (a) the user explicitly asked for it, or (b) you have
    evidence the target code is a stale duplicate or provably broken — state
    that evidence (`git log` / `git blame`, blob compare) in the delivery
    notes. A revert touching another session's in-flight or committed work
    needs the user's explicit go-ahead first; conflict scenarios follow rule
    11. When in doubt: surface the problem, never roll back to finish cleanly.
    Full text: `docs/WORKSPACE_CONVENTIONS.md` §10.

13. **经验沉淀纪律：验证过的教训必须进 skill，且持续增删迭代。** 项目级
    skills 在 `.agents/skills/`（当前清单与准入标准见
    `.agents/skills/SKILLS_GOVERNANCE.md`，已收敛为 8 个通用规范类；
    项目专属的 game-online/app-migration/testing-* 已删，历史在 git）。规则：
    - 会话中**验证过**的新经验（踩坑后成立的解法）当天并入对应 skill，
      与功能同一提交；跨项目可复用的写通用节，项目专属写项目节。
    - 被**推翻**的旧经验在同一提交里删除——skill 里不留"可能也对"的
      僵尸规则，宁缺毋滥。
    - 每条规则带"为什么"（踩过的坑）；没有坑出处的新增不予合入。
    - 动手前先查 skill：同类的坑别人踩过就不要再踩一遍。
    - skills 有**三份副本**，更新必须同一提交走完全程，只改工作区 =
      用户项目永远收不到教训：① 工作区 `.agents/skills/`（活源，LF）→
      ② `tools/ai_pack/skills/`（发布变体：剥离"在 zan-lang 仓库内工作"
      节、仓库内路径改写成通用说法、description 尾保留"仓库内有同名
      项目级版本，会自动优先"提示）→ ③ `C:\Users\<user>\.agents\skills\`
      （②的字节副本，zcode 在仓库外使用）。`scripts\sync_skills.ps1`
      报告 ①↔② 漂移（VARIANT 条目 = 人工维护的变体，需人工比对），
      `-SyncGlobal` 把 ② 字节复制到 ③；② 再由 `publish_ide.ps1` 随
      SDK 进 `dist\ai\skills\`。

## Build / dev quickstart

**The toolchain of each platform is fixed — see [`docs/BUILD_TOOLCHAIN.md`](docs/BUILD_TOOLCHAIN.md).**
On Windows always configure with clang plus the mozbuild LLVM: letting CMake pick
up TDM-GCC from PATH cannot link zanc, and the failed link deletes
`build\zanc.exe`, taking the whole test suite with it.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build            # zanc, zan-lsp, zan-dap, tools
# IDE (self-hosted) is built from src/ide_zan/ZanIDE.zan by zanc.
```

Compile a single program: `build/zanc <file.zan> --auto-stdlib -o out.exe`
Release build of a program: `build/zanc <file.zan> --auto-stdlib --publish -o out.exe`

**Full CLI reference** — every zanc flag, cross target, env var, test recipe,
and the known pitfalls: [`docs/ZANC_CLI.md`](docs/ZANC_CLI.md). When a build/
publish/test command is unclear or "worked yesterday", check there first;
update it in the same commit when changing argument parsing.

### CEF browser driver (optional)

`Gui.Component.CefBrowser` ships as an opt-in native driver (`zan_cef` / on
Windows also `zan_cef109` for Win7/8/8.1). It is **off by default** because the
configure step fetches CEF headers from the official Spotify CDN (a few hundred
MB). Enable with:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DZAN_BUILD_CEF=ON
# Windows: also build the 109 legacy driver for Win7/8/8.1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DZAN_BUILD_CEF=ON -DZAN_BUILD_CEF_LEGACY=ON
cmake --build build --target zan_cef
```

Built drivers are staged next to the source (`stdlib/Gui/Component/CefBrowser/drivers/<plat>/`)
so `zanc --publish` carries them with the program. The CEF *runtime* (libcef)
is still downloaded per-machine at first run; see
`examples/gui_cef_browser/README.md` for `ZAN_CEF_*` env vars.
