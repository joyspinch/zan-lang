# Zan Release & Versioning Specification

This document defines how Zan is versioned and how the self-contained IDE bundle
(the aardio-style "open the IDE and go" deliverable) is built, laid out, named,
and published across Windows, Linux, and macOS.

The release artifact is **an IDE, not a bare compiler**: the user unzips one
archive, launches `ZanIDE`, and can create → develop → build → debug → publish
projects with no external LLVM, linker, SDK, or PATH setup. `zanc` and the other
CLIs ship *inside* the bundle as tools the IDE drives.

---

## 1. Versioning (SemVer)

Zan uses [Semantic Versioning](https://semver.org/) `MAJOR.MINOR.PATCH`:

- **MAJOR** — breaking language/stdlib/ABI or IDE project-format changes.
- **MINOR** — backward-compatible language/stdlib/IDE features.
- **PATCH** — backward-compatible fixes only.
- Pre-releases: `-alpha.N`, `-beta.N`, `-rc.N` (e.g. `0.2.0-rc.1`).
- Build metadata (optional): `+<gitshortsha>` (e.g. `0.2.0+1a2b3c4`).

The compiler, language, stdlib, and IDE share **one** version number per release.

### 1.1 Single source of truth (resolved)

The version is **single-sourced today** — the earlier duplication is gone:

| Location                     | Current value |
|------------------------------|---------------|
| root `VERSION` file          | `0.2.1` (the only place the number is edited) |

CMake reads the root `VERSION` file and generates `build/generated/zan_version.h`
from `cmake/zan_version.h.in`, injecting `#define ZAN_VERSION "…"` into every
binary — `zanc --version`, the LSP `initialize` result and the IDE About box
all report the same string (`main.c` and `lsp_main.c` both use `ZAN_VERSION`).
Tag releases as `v<VERSION>`.

---

## 2. Target platforms

| Platform      | Triple / arch            | IDE binary   | GUI runtime      | Status |
|---------------|--------------------------|--------------|------------------|--------|
| Windows x64   | `x86_64-pc-windows`      | `ZanIDE.exe` | SDL3 statically linked into `ZanIDE.exe` (no DLL shipped) | **Shipping** |
| Linux x64     | `x86_64-linux-gnu`       | `ZanIDE`     | `libSDL3.so.0`   | Planned |
| Linux arm64   | `aarch64-linux-gnu`      | `ZanIDE`     | `libSDL3.so.0`   | Planned |
| macOS arm64   | `aarch64-apple-darwin`   | `ZanIDE`     | `libSDL3.dylib`  | Planned |
| macOS x64     | `x86_64-apple-darwin`    | `ZanIDE`     | `libSDL3.dylib`  | Planned |

Notes:
- The **CLIs** (`zanc`, `zan-lsp`, `zan-dap`, `zanfmt`, `zandoc`) build
  natively on all three OSes today — `CMakeLists.txt` already has the non-Windows
  link path (`stdc++ m pthread`). Only the **IDE build recipe** (SDL3 GUI runtime
  + linking `ZanIDE.zan`) is currently Windows-only and must be ported.
- `zanc` cross-compiles **user programs** (distinct from IDE distribution above)
  from any host to Linux (`linux-x64`/`linux-musl`/`linux-arm64`/`linux-riscv64`,
  static musl ELF — full runtime), Windows (`win-x64`/`win-arm64`) and macOS
  (`macos-x64`/`macos-arm64`), plus `wasm32` (WASI). Linux cross is unrestricted;
  Windows cross links the bundled `win-<arch>/` runtime objects on demand
  (async-socket, sync, timer, embed); WASI carries runtime restrictions
  (no socket-async; single-threaded); macOS cross links async-socket,
  `AtomicInt`/`SharedTable` and the Cocoa GUI dylib from any host — only
  execution on real Mac hardware remains unverified (it is **not** console-only
  anymore). See `docs/platform-targets.md §2` for the authoritative matrix. This
  is orthogonal to which OS the *IDE itself* runs on.

---

## 3. Bundle layout (per platform, identical logical shape)

> Note: the current publish output is `dist\win-x64` — a flat directory with
> **no** `<version>` subdirectory, and **no `VERSION` file written into it**
> (the release number is read from the repo root `VERSION` by
> `publish_ide.ps1`). The layout below is the logical shape for all platforms.

```
zan-ide-<version>-<os>-<arch>/
  ZanIDE[.exe]              # IDE — the main entry point the user launches
                            # (SDL3 statically linked in; no SDL3.dll beside it)
  README.txt                # what's inside + how to run
  toolchain/                # everything the compiler needs, all siblings:
    zanc[.exe]              #   the compiler (the IDE resolves it here)
    zan-lsp[.exe]           #   language server (completion/diagnostics)
    zan-dap[.exe]           #   debug adapter (breakpoints/stepping)
    zanfmt[.exe] zandoc[.exe]
    ld.exe | ld.lld | ld64.lld   # bundled linkers (win native: GNU ld.exe)
    zanrt_*.obj             #   flat runtime objects — 12 zanrt_* (io/io_mt/
                            #   sync/timer/sched_msvc/ide/gallery/...) + zan_embed_api.obj
    linux-musl/ linux-arm64/ linux-riscv64/   # musl cross sysroots (all hosts)
    win-x64/ win-arm64/ macos/                # per-target runtime objects
    zan_gui.lib|.a          #   native GUI runtime for user GUI projects
  stdlib/                   # Zan standard library sources (auto-included)
  templates/                # built-in New Project templates (data-driven)
  examples/                 # curated sample projects opened from the IDE
```

Rules:
- **No nested `toolchain/toolchain`.** `zanc` finds its linker/sysroot/runtime
  objects as its own siblings, exactly as in the build tree.
- The IDE needs **no separate GUI runtime DLL**: SDL3 is statically linked into
  `ZanIDE.exe` (`scripts/publish_ide.ps1`; `build_ide.ps1` uses the Win32
  backend, `ZAN_GUI_STATIC`). SDL3 remains only as user-program drivers under
  `stdlib/SDL3/drivers/`.
- `stdlib/`, `templates/`, `examples/` are copied verbatim; they are
  platform-neutral sources. Native driver bundles under
  `stdlib/**/drivers/<os>-<arch>/` are already multi-platform.

---

## 4. Artifact naming & checksums

- Directory / archive base name: `zan-ide-<version>-<os>-<arch>`
  - `<os>` ∈ `win` | `linux` | `macos`; `<arch>` ∈ `x64` | `arm64`.
- Archive format: `.zip` on Windows, `.tar.gz` on Linux/macOS.
  - e.g. `zan-ide-0.2.0-win-x64.zip`, `zan-ide-0.2.0-linux-x64.tar.gz`,
    `zan-ide-0.2.0-macos-arm64.tar.gz`.
- Every release publishes `SHA256SUMS.txt` covering all archives.
- Optional (recommended for GA): Authenticode sign `ZanIDE.exe`/`zanc.exe` on
  Windows; notarize the `.app`/binaries on macOS.

---

## 5. Build recipes

### 5.1 Windows x64 (shipping)

```powershell
# 1. compiler + CLIs + runtime + bundled linker toolchain
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
      -DCMAKE_LINKER=lld-link -DLLVM_DIR=<llvm>/lib/cmake/llvm `
      -DZAN_MINGW_ROOT=C:/TDM-GCC-64
cmake --build build
# 2. IDE (zanc compiles ZanIDE.zan; SDL3 statically linked in; also compiles the app icon)
powershell -ExecutionPolicy Bypass -File scripts\build_ide.ps1
# 3. assemble the self-contained bundle into dist\win-x64 (bumps VERSION unless -NoBump)
powershell -ExecutionPolicy Bypass -File scripts\publish_ide.ps1 -SkipBuild
```

Prerequisites: LLVM (for `find_package(LLVM)`), `clang`/`llvm-*` on PATH, and
TDM-GCC (bundled ld). A staged SDL3 mingw-devel package (see
`scripts/stage_sdl3.ps1`) is only needed to build the **user-program** SDL3
drivers (`stdlib/SDL3/drivers/`) — the IDE itself does not use it.

### 5.2 Linux / macOS (planned — recipe to implement)

1. `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` with system clang +
   LLVM → builds `zanc`, `zan-lsp`, `zan-dap`, `zanfmt`, `zandoc`.
2. Build the native GUI runtime against the platform SDL3 (`libSDL3.so` /
   `libSDL3.dylib`) → `zan_gui.a`.
3. `zanc src/ide_zan/ZanIDE.zan -o build/ZanIDE` linking `zan_gui.a` + SDL3.
4. A `publish_ide.sh` mirroring `publish_ide.ps1` assembles `dist/` with the
   POSIX file names from §3 (ship `libSDL3.*` beside `ZanIDE`).

These three shell steps are the only remaining work to make the bundle
cross-platform; the compiler/stdlib/templates/examples are already portable.

---

## 6. Release process checklist

1. `publish_ide.ps1` bumps the root `VERSION` automatically
   (`scripts\bump_version.ps1`; pass `-NoBump` to skip). There is no
   `CHANGELOG.md` to update.
2. Green CI on all target platforms (build + `ctest`).
3. Build the bundle on each platform (§5) and smoke-test:
   launch IDE → New Project (each template) → Build → Run → Debug (breakpoint) →
   Publish. The bundle must work on a machine with **no** dev toolchain.
4. Produce archives (§4) + `SHA256SUMS.txt`; sign/notarize if GA.
5. Tag `v<VERSION>`, push, and attach archives to the release.
6. Verify each archive unzips and launches on a clean VM per OS.

---

## 7. Current deliverable vs. gaps

- **Deliverable now:** `zan-ide-<v>-win-x64` — reproducible clean build via §5.1,
  self-contained (`ZanIDE.exe` with SDL3 statically linked + `toolchain/` +
  stdlib/templates/examples), no external toolchain needed to run the IDE or
  build user programs.
- **Gaps to close for a multi-platform release:**
  1. Linux/macOS IDE build recipe + `publish_ide.sh` (§5.2).
  2. `SHA256SUMS` + archive packaging step in the publish scripts (§4).
  3. Per-OS clean-VM smoke test in CI (§6.3).
