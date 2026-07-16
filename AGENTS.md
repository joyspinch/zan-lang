# AGENTS.md

Guidance for humans and AI agents (Devin, Copilot, etc.) working in this repo.
**Read this before creating files.** The full rules live in
[`docs/WORKSPACE_CONVENTIONS.md`](docs/WORKSPACE_CONVENTIONS.md); this file is the
short, enforceable summary.

## Directory map — where things go

| Path            | Contents                                                        |
|-----------------|-----------------------------------------------------------------|
| `src/compiler/` | Compiler front/back end (C11): lexer, parser, binder, checker, irgen |
| `src/runtime/`  | Runtime library (C11): ARC, strings, collections, IO, scheduler |
| `src/common/`   | Shared C utilities (json, rpc, host_oom)                        |
| `src/lsp/`      | Language server (`zan-lsp`) + `intellisense.{c,h}` engine       |
| `src/dap/`      | Debug adapter (`zan-dap`) + `debugger.{c,h}` engine             |
| `src/ide_zan/`  | The IDE — self-hosted, written in Zan (`ZanIDE.zan` + components) |
| `src/fmt/`, `src/doc/`, `src/pkg/` | Formatter, doc generator, package manager    |
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

## Hard rules — DO NOT break these

1. **Keep the repo root clean.** Only long-lived, version-controlled entries
   belong there (source dirs, `CMakeLists.txt`, `README.md`, `LICENSE`,
   `.gitignore`, `AGENTS.md`). Never drop build artifacts, logs, probes,
   one-off scripts, PR drafts, diffs, or patches in the root.
2. **All throwaway files go in `_scratch/`** (git-ignored). This includes
   debug probes, benchmarks, ad-hoc scripts, screenshots, `*.log`, PR bodies,
   commit-message drafts, `*.diff`/`*.patch`. Delete them when done.
3. **Build only out-of-source into `build/`** via `cmake -B build && cmake --build build`.
   Never copy build products into the source tree or root.
4. **Tests go in `tests/`** and are committed. One-off memory/leak probes are
   NOT tests — put them in `_scratch/`.
5. **Use `gh` / the PR template** to open PRs. Do not generate `mkpr*.py`,
   PR-body `.md`, or `*.diff` files in the repo.
6. **`.gitignore` is a safety net, not a license to litter.** Even ignored
   files must not pile up in the source tree.
7. **Every task ends clean.** Run `git status` before finishing; there should
   be no stray untracked files. Review `git diff --stat` and commit only what
   the task requires. Never `git add .` / `git add -A` blindly.

## Build / dev quickstart

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build            # zanc, zan-lsp, zan-dap, tools
# IDE (self-hosted) is built from src/ide_zan/ZanIDE.zan by zanc.
```

Compile a single program: `build/zanc <file.zan> --auto-stdlib -o out.exe`
Release build of a program: `build/zanc <file.zan> --auto-stdlib --publish -o out.exe`
