# Platform Targets

Status of Zan's cross-platform / cross-compilation support: what is **implemented**
(build + link + run), what carries **cross-compile restrictions** (implemented, but
some runtime subsystems are unavailable when producing that target from another
host), and what is **future** (needs new target entries plus runtime work). Also
lists, per platform, the runtime subsystems that must exist before a target can be
considered complete.

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

Being listed here means the triple parses and platform macros
(`WINDOWS`/`LINUX`/`MACOS`) plus arch macros (`ARM64`/`X86_64`) are defined for the
target. All nine now also have a working link path — see §2 for the per-target
runtime restrictions that apply when cross-compiling.

---

## 2. Implementation status

Every declared target has a link path in `main.c`. What differs is **which runtime
subsystems are available** for a given target, and whether it is being built
**natively** (host OS == target OS, full support) or **cross** (from another host,
possibly restricted). The only case that still errors `cross-compilation to '<triple>'
is not supported yet` is a target OS outside {Linux, Windows, macOS, WASI} — none of
the declared targets fall there today.

### Native builds (host OS == target OS) — full support

| Target                     | Native link path                                                     |
|----------------------------|----------------------------------------------------------------------|
| `win-x64` / `win-arm64`    | Bundled `ld.lld` + MinGW-w64 / llvm-mingw runtime (`<zanc>/win-<arch>/mingw`), `*-w64-windows-gnu` ABI. |
| `linux-x64` / `linux-arm64`| Host toolchain / bundled musl (see cross below).                     |
| `macos-x64` / `macos-arm64`| Host toolchain on the matching Mac (Cocoa GUI, threads, async all available). |

A native build uses the full runtime (async I/O reactor, threads/atomics, GUI),
because it is the same code path a normal `zanc` invocation on that OS takes.

### Cross builds (`--target` from a different host) — implemented, with restrictions

| Cross target                       | How it links (from any host)                                       | Restrictions when cross-compiling |
|------------------------------------|--------------------------------------------------------------------|-----------------------------------|
| `linux-x64` / `linux-musl` / `linux-arm64` / `riscv64` | `ld.lld -static` + bundled musl sysroot `<zanc>/{linux-musl,linux-arm64,linux-riscv64}` → a dependency-free static ELF. | **None** — async-socket (`zanrt_io.o`) *and* the sync runtime (`zanrt_sync.o`: `AtomicInt`/`SharedTable`) are both linked in. "Build on Windows/macOS → upload → run on Linux" just works. |
| `win-x64` / `win-arm64`            | `ld.lld` MinGW driver + bundled `win-<arch>/mingw/lib`; PE output. | **No async-socket**, and **no `AtomicInt`/`SharedTable`** (sync runtime is Linux-only when cross-compiling). Ordinary console/GUI programs link fine. |
| `macos-x64` / `macos-arm64`        | `ld64.lld -arch <arch> -platform_version macos 11.0` + bundled `macos/libSystem.tbd` (MIT symbol stub; no Apple SDK redistributed) + the Mach-O runtime objects in `macos/<arch>/` for async/atomics; Mach-O output linking only `-lSystem`. arm64 output is ad-hoc code-signed by the linker (`LC_CODE_SIGNATURE`, `CS_ADHOC|CS_LINKER_SIGNED`), which is what Apple Silicon requires to execute it at all. | **No GUI**: only `libSystem` is stubbed, so Cocoa/other frameworks cannot be linked. Async-socket and `AtomicInt`/`SharedTable` **do** work (see below). Developer ID signing/notarization is a separate, not-yet-implemented step needed only for distribution. |
| `wasm32` (WASI)                    | `wasm-ld` + bundled wasm32 sysroot (`crt1.o`); WASI command module. | **No async-socket**; single-threaded WASI model. |

Key properties:
- **Linux is the universal cross target** — full-featured and self-contained from
  any host.
- **Windows and macOS cross targets exist** but are restricted: no async-socket and
  no atomics/shared-table for Windows, no GUI/frameworks for macOS. For a
  full-feature Windows program, or any GUI program, build **natively on that OS**.
- The sync runtime (`AtomicInt`/`SharedTable`) is available natively everywhere and,
  when cross-compiling, for **Linux and macOS** targets (`main.c` errors early on the
  remaining ones).

### macOS: covering both architectures

Cross-compiling from one Mac architecture to the other (`macos-arm64` host →
`macos-x64`, or vice-versa) goes through the `ld64.lld` path above, so it carries the
same single restriction as any other host: **no GUI**. To ship a GUI binary for both
Mac architectures you must build each **natively on its own arch** (an Apple Silicon
Mac for `arm64`, an Intel Mac — or Rosetta — for `x64`), optionally combining the
two slices into a Universal 2 binary with `lipo -create`.

---

## 3. Runtime subsystems (what each target needs)

A target is only "complete" when these back ends exist for it. Current coverage:

| Subsystem            | Windows | Linux | macOS | Notes / mechanism                         |
|----------------------|:-------:|:-----:|:-----:|-------------------------------------------|
| I/O reactor          | IOCP    | epoll | kqueue| `src/runtime` async socket/file reactor; **cross builds to Win/WASI cannot link it yet** (macOS cross links `macos/<arch>/zanrt_io{,_mt}.o`) |
| Threads / sync       | Win32   | pthread| pthread| mutex, semaphore (macOS uses GCD dispatch), atomics; **`AtomicInt`/`SharedTable` cross to Linux and macOS** |
| C FFI (`DllImport`)  | crt/msvcrt → CRT | libc | libc | resolved by the linker; names unified as `crt` |
| Filesystem / dirent  | ✅      | glibc layout | Darwin layout | `Directory.zan` branches on dirent offsets |
| Monotonic clock      | ✅      | `CLOCK_MONOTONIC`=1 | =6 | `Stopwatch` |
| GUI backend          | Win32   | X11   | Cocoa | Wayland and all other windowing systems are stubs; macOS **cross** links only `libSystem` (no Cocoa) |
| Native DB drivers    | `win-x64`/`win-arm64` | `linux-x64`/`linux-arm64` | `macos-x64`/`macos-arm64` | `<stdlib>/System/Data/<Module>/drivers/<target>/`; binaries gitignored |

Anything not listed (Wayland, BSD, mobile, WASM GUI, bare-metal) has **no** back end
yet.

---

## 4. Per-platform gap analysis (future targets)

Difficulty is for **CLI/compute** first; GUI is a separate, larger effort on each.

### Async-socket / sync runtime for Windows cross — small–medium
- Done for macOS: `scripts/build_macos_rt.sh` compiles `rt_io.c` (kqueue) and
  `rt_sync.c` for `{aarch64,x86_64}-macos.11.0` with `zig cc` (it carries the Darwin
  libc headers, so no Apple SDK and no Mac are needed) into
  `toolchain/macos/<arch>/zanrt_{io,io_mt,sync}.o`, which ship next to `zanc` and are
  linked on demand. Every symbol they import is in the bundled `libSystem.tbd` —
  `scripts/check_macos_rt.py` asserts that, so a missing stub is caught here instead
  of in a user's link.
- Still open for Windows: the same objects in the mingw ABI, staged beside
  `toolchain/win-<arch>/`.

### macOS GUI cross (Cocoa) — medium
- `src/runtime/gui_runtime_mac.m` is Objective-C against Cocoa/CoreText/QuartzCore/
  IOSurface/WebKit headers, which only the Apple SDK provides, so the object itself
  must be built on a Mac (the `macos-13`/`macos-14` runners already used by
  `.github/workflows/drivers.yml` are enough) and committed like the objects above.
- The link then also needs `.tbd` stubs for those frameworks. They can be generated
  on any host from the undefined symbols of that object (including the
  `_OBJC_CLASS_$_*` references) into fresh tbd-v4 documents — the approach
  AotAnywhere uses for .NET's Apple dependencies.

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

### WebAssembly (`wasm32-wasi`) — GUI/threads are hard
- CLI/compute-only WASI links today. Threading/sync and the epoll/kqueue/IOCP
  reactor assume threads + syscalls, so networking and GUI need a different model
  (async host imports / canvas/DOM). ARC itself is fine (refcounting over `malloc`).

### Embedded / bare-metal (Cortex-M, RISC-V MCU) — largest effort
- Needs a **freestanding** runtime: no libc, no OS, custom linker script, and a
  heap for ARC/`malloc`. No reactor, threads, or GUI.
- Only a stripped "core" subset of the language/stdlib is realistic.

---

## 5. IDE Publish dialog (host-aware program publishing)

The IDE's **Publish** dialog exposes only the targets the current IDE host can
build fully or usefully (`ZanIDE.PublishTargetIds`), so users never pick a target
that would fail:

| IDE host   | Targets offered                                              |
|------------|--------------------------------------------------------------|
| Windows    | `windows-x64`, `windows-arm64`, `linux-x64`, `linux-arm64`   |
| macOS      | `macos-x64`, `macos-arm64`, `linux-x64`, `linux-arm64`       |
| Linux      | `linux-x64`, `linux-arm64`                                   |

- The host's **own** OS/arch is built natively (full support); the *other* arch of
  the same OS and all Linux targets use the cross paths in §2 (Linux cross is full;
  the other Windows/macOS arch carries that OS's cross restrictions).
- Cross to a *different* desktop OS (Windows ⇄ macOS) is intentionally **not**
  offered in the dialog: it is either impossible or too restricted for typical
  (GUI) projects. Such builds remain available from the `zanc --target` CLI subject
  to §2.

---

## 6. Recommended order

1. Stage `zanrt_io.o` / `zanrt_sync.o` for the Windows & macOS ABIs to lift the
   async-socket / sync-runtime cross restrictions (§4).
2. Android / OHOS CLI (Linux-like reactor, per-NDK sysroot).
3. `wasm32` WASI networking/threads model.
4. iOS, then bare-metal.
