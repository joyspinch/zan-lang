# Platform Targets

Status of Zan's cross-platform / cross-compilation support: what is **implemented**
(build + link + run), what is a **placeholder** (declared in the target table but
not yet wired for linking), and what is **future** (needs new target entries plus
runtime work). Also lists, per platform, the runtime subsystems that must exist
before a target can be considered complete.

This file is the source of truth for target support. Keep it in sync with:
- `src/compiler/crosscomp.c` — the target table (`s_targets[]`) and triple parsing.
- `src/compiler/main.c` — the actual link paths (`zan_target_host`, the
  `cross_compiling` branches, `zan_driver_subdir`).
- `stdlib/System/Data/<Module>/drivers/<target>/` — per-target native DB drivers.

---

## 1. Target table (`--list-targets`)

Nine targets are currently declared in `crosscomp.c`:

| Name          | Triple                          | Notes                    |
|---------------|---------------------------------|--------------------------|
| `win-x64`     | `x86_64-pc-windows-msvc`        | Windows x86-64           |
| `win-arm64`   | `aarch64-pc-windows-msvc`       | Windows ARM64            |
| `linux-x64`   | `x86_64-unknown-linux-gnu`      | Linux x86-64 (glibc)     |
| `linux-musl`  | `x86_64-unknown-linux-musl`     | Linux x86-64 (musl, static) |
| `linux-arm64` | `aarch64-unknown-linux-gnu`     | Linux ARM64              |
| `macos-x64`   | `x86_64-apple-macosx`           | macOS Intel              |
| `macos-arm64` | `aarch64-apple-macosx`          | macOS Apple Silicon      |
| `wasm32`      | `wasm32-unknown-wasi`           | WebAssembly (WASI)       |
| `riscv64`     | `riscv64-unknown-linux-gnu`     | RISC-V 64-bit Linux      |

Being listed here only means the triple parses and platform macros
(`WINDOWS`/`LINUX`/`MACOS`) are defined for the target. It does **not** imply a
working link path — see §2.

---

## 2. Implementation status

### Implemented — build, link, run

| Target        | How it links today                                                        |
|---------------|---------------------------------------------------------------------------|
| `win-x64`     | **Native on Windows.** Bundled `ld.lld` + MinGW-w64 runtime (`<zanc>/toolchain`), mingw ABI; falls back to a system clang with the same ABI. |
| `linux-x64`   | **Native on Linux**, and **cross from any host** as a dependency-free static musl binary (`ld.lld` + bundled `linux-musl` sysroot). |
| `linux-musl`  | Cross, static musl (same path as `linux-x64` cross).                      |
| `linux-arm64` | Cross, static musl (`linux-arm64` sysroot); native on a Linux/ARM host.   |
| `macos-x64`   | **Native on macOS Intel** (host toolchain).                               |
| `macos-arm64` | **Native on macOS Apple Silicon** (host toolchain).                       |

Key property: cross-compilation is currently **only implemented for Linux
targets** (self-contained static musl, so "build on Windows → upload → run on
Linux" works). Windows and macOS binaries are produced **natively on that OS**.

### Placeholder — declared, but link errors "not supported yet"

These parse and pick correct platform macros / driver subdirs, but the linker
branch in `main.c` rejects them with
`cross-compilation to '<triple>' is not supported yet; only linux targets are
implemented`:

| Target                     | What already exists            | What is missing to implement                    |
|----------------------------|--------------------------------|-------------------------------------------------|
| `win-arm64`                | triple, macros, `zan_driver_subdir` → `win-arm64`, SQLite manifest | a cross link path (native build on Windows/ARM, or an aarch64 mingw sysroot + `ld.lld`) |
| `macos-x64` / `macos-arm64` **as a cross target from non-mac** | native build works | Apple SDK/sysroot, `ld64`/codesign; cross from Linux/Windows is legally/technically constrained |
| `wasm32` (WASI)            | triple, macros                 | WASM object emission, WASI sysroot linker, and a single-threaded runtime model (see §3) |
| `riscv64`                  | triple, macros                 | RISC-V sysroot + `ld.lld` link path (Linux-like, so closest to feasible) |

Additional constraint: `AtomicInt` / `SharedTable` (the sync runtime) are
**not available for any non-Linux cross target** yet (`main.c` errors early if a
program uses them while cross-compiling to a non-Linux OS).

### Future — not in the table yet; need new entries + runtime port

`Android`, `iOS`, `HarmonyOS / OHOS`, and bare-metal `embedded` (Cortex-M,
RISC-V MCU). LLVM can already emit machine code for all of these ISAs — the work
is runtime/stdlib/linker, not code generation. See §3–§4.

---

## 3. Runtime subsystems (what each target needs)

A target is only "complete" when these back ends exist for it. Current coverage:

| Subsystem            | Windows | Linux | macOS | Notes / mechanism                         |
|----------------------|:-------:|:-----:|:-----:|-------------------------------------------|
| I/O reactor          | IOCP    | epoll | kqueue| `src/runtime` async socket/file reactor   |
| Threads / sync       | Win32   | pthread| pthread| mutex, semaphore (macOS uses GCD dispatch), atomics |
| C FFI (`DllImport`)  | crt/msvcrt → CRT | libc | libc | resolved by the linker; names unified as `crt` |
| Filesystem / dirent  | ✅      | glibc layout | Darwin layout | `Directory.zan` branches on dirent offsets |
| Monotonic clock      | ✅      | `CLOCK_MONOTONIC`=1 | =6 | `Stopwatch` |
| GUI backend          | Win32   | X11   | Cocoa | Wayland and all other windowing systems are stubs |
| Native DB drivers    | `win-x64`/`win-arm64` | `linux-x64`/`linux-arm64` | `macos-x64`/`macos-arm64` | `<stdlib>/System/Data/<Module>/drivers/<target>/`; binaries gitignored |

Anything not listed (Wayland, BSD, mobile, WASM, bare-metal) has **no** back end
yet.

---

## 4. Per-platform gap analysis (future targets)

Difficulty is for **CLI/compute** first; GUI is a separate, larger effort on each.

### Android (`aarch64/x86_64-linux-android`) — medium
- Linux-like: epoll reactor and pthread work; needs the **NDK sysroot** and
  **bionic** libc FFI names, plus an `ld.lld` link path keyed on the android ABI.
- GUI would need an Android-specific back end (no X11).

### HarmonyOS / OHOS — medium
- OHOS native uses a musl-based NDK; for CLI/services this is close to Android.
- GUI would need an ArkUI/native back end.

### iOS (`arm64-apple-ios`) — medium-hard
- AOT-only fits us (we don't rely on JIT); kqueue works.
- Requires the iOS SDK/sysroot, code signing + provisioning, and a UIKit GUI
  back end. App Store sandboxing restricts process/FS APIs.

### WebAssembly (`wasm32-wasi`) — hard (biggest runtime rework)
- Single-threaded by default and **no raw syscalls**; our threading/sync and the
  epoll/kqueue/IOCP reactor all assume threads + syscalls.
- Realistic first step: **compute-only CLI on WASI** (no threads, no native GUI,
  limited C FFI). Networking and GUI require a fundamentally different model
  (async host imports / canvas/DOM).
- ARC itself is fine (it's just refcounting over `malloc`).

### Embedded / bare-metal (Cortex-M, RISC-V MCU) — largest effort
- Needs a **freestanding** runtime: no libc, no OS, custom linker script, and a
  heap for ARC/`malloc`. No reactor, threads, or GUI.
- Only a stripped "core" subset of the language/stdlib is realistic.

---

## 5. Recommended order

1. `win-arm64` link path (native on Windows/ARM, or aarch64 mingw cross) — table,
   macros and driver layout are already consistent.
2. `riscv64` Linux link (reuse the musl static path).
3. Android / OHOS CLI (Linux-like reactor, per-NDK sysroot).
4. `wasm32` WASI compute-only.
5. iOS, then bare-metal.
