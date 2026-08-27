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
8. **Pick a test tier; never run all 1035 tests for a small change.** The suite
   is labelled in three tiers (`scripts\test.ps1 <tier>`, or `ctest -L <tier>`):
   `smoke` (75 tests, ~5 s — goldens, diagnostics, ABI, runtimes, tools, GUI:
   run it on every edit), `standard` (399 — smoke plus every conformance
   program and library emission: the pre-commit gate, ~1 min), `full` (1035 —
   plus the determinism/leakcheck twins and self-hosting: release gate only,
   tens of minutes). Narrow further with `-Match`/`-R` when a change is local
   (a generics/ARC change: `-R "generic|leakcheck_generic"`). Every test
   artifact is keyed on the compiler binary, so relinking `zanc` invalidates
   all of them — and **nothing else may build while tests run**: the cases
   share `build\zanc.exe` and the stdlib stamp, so a concurrent
   `build_gallery`/`build_ide` makes unrelated cases fail en masse.
9. **Never create branches.** Do NOT create local or remote branches; commit
   directly to `main` and push. Delete any stray branch you find after making
   sure its commits are merged into `main`.
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
    case, and only then write the natural Zan code. If the fix is genuinely out
    of scope for the current task, do not silently work around it: record it in
    `TASKS.md` with the probe and the root cause, say so explicitly, and get
    agreement before shipping any temporary shape.

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
