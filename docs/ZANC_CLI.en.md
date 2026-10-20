# zanc Command Reference —— Compilation, Publishing, and Testing

<p align="left">
  <a href="ZANC_CLI.md">简体中文</a> | <b>English</b>
</p>

A comprehensive reference for the `zanc` CLI surface: flags, cross-compilation targets, environment variables, copy-paste recipes, and known pitfalls. Maintained in sync with `zanc --help` and `--list-targets`.

--------------------------------------------------------------------------------

## 0. Mental Model

A single `zanc` invocation = **Compilation + On-demand Standard Library Pull-in + Native Driver Linking**, directly producing an executable:

```bash
build/zanc <entry and source files...> [options] -o <output>
```

- No manual `.o` assembly or explicit linker steps needed; no `-I`/`-l` flags required (standard library namespaces are pulled in on-demand, and corresponding native drivers are linked automatically).
- **Inputs are a file set, not a project folder**: `zanc` compiles only the `.zan` files listed on the command line (plus auto-resolved stdlib/package sources). In multi-file projects, specify all `src/**/*.zan` files explicitly.
- Flags fall into five categories: Output mode (`-o`, `--emit-lib`, `--publish`), Standard library (`--auto-stdlib`, `--stdlib-path`, `--no-packages`), Target platform (`--target`, `--emit-apk`, `--emit-ipa`), Native linking (`-L`, `--driver-dir`, `--link-lib`), and Diagnostics (`--emit-ir`, `-g`, `--dump-*`).

---

## 1. Standard Recipes (Copy & Paste)

### 1.1 Daily Development Cycle

```bash
# Compile and run (auto pulls stdlib, default debug optimization -O0)
build/zanc src/main.zan -o _scratch/app.exe && _scratch/app.exe

# Multi-file project: explicitly list all sources (Git Bash / Unix shell)
build/zanc $(find src -name '*.zan' | sort | tr '\n' ' ') -o _scratch/app.exe

# Release build (strips symbols + optimization, default -Os)
build/zanc src/main.zan --publish -o app.exe
```

### 1.2 GUI Applications

```bash
# Headless logic test (Html.Parse, layout compute): runs without window
build/zanc probe.zan -o probe.exe && probe.exe

# Windowed GUI app: Windows automatically infers windows subsystem (no black console window)
build/zanc $(find src -name '*.zan' | tr '\n' ' ') -o app.exe

# Design file entry (.html/.zan declarative UI): design file MUST be the first argument
build/zanc src/MyApp.html $(find src -name '*.zan' | sort | tr '\n' ' ') -o app.exe
```

### 1.3 Cross-Compilation & Mobile

```bash
build/zanc --list-targets                 # List all supported targets (see §3)
build/zanc src/main.zan --auto-stdlib --target linux-musl -o app      # musl static binary
build/zanc src/main.zan --auto-stdlib --target android-arm64 --emit-apk app.apk \
    --apk-package com.example.app --apk-label "My App" -o app.so
build/zanc src/main.zan --auto-stdlib --target ios-arm64 -o app.ipa
# Or explicitly configure iOS bundle ID and App name:
build/zanc src/main.zan --auto-stdlib --target ios-arm64 --emit-ipa app.ipa \
    --ipa-bundle-id com.example.app --ipa-name "My App"
```

`--emit-apk` handles full packaging: Compile -> NativeActivity wrapper -> binary AXML -> signature (requires `JAVA_HOME`).

### 1.4 Libraries & Tooling

```bash
build/zanc src/lib.zan --auto-stdlib --emit-lib -o foo.dll   # Shared library (.so / .dylib)
build/zanc --emit-symbols index.json src/...                 # Export symbols index for IDE/LSP
build/zanc --gen-meta meta.json src/...                      # Export compilation unit metadata JSON
```

### 1.5 Diagnostic Triage (Compiler & Runtime Investigation)

```bash
build/zanc probe.zan --auto-stdlib --emit-ir > ir.ll   # Inspect LLVM IR
build/zanc probe.zan --auto-stdlib --dump-ast          # Inspect AST (--dump-tokens for tokens)
build/zanc probe.zan --auto-stdlib --time              # Compilation stage timings
```

The IR produced by `--emit-ir` **describes its own target features** (header
comments `; zanc-target-cpu:` / `; zanc-target-features:`), and the matching
`llc` command line is printed to stderr. Consumers must use the same features,
otherwise the 256-bit vector path cannot be legalized on a generic CPU
(`LLVM ERROR: Do not know how to split this operator's operand!`):

```bash
llc -mcpu=x86-64 -mattr=+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi \
    -filetype=obj ir.ll -o ir.o
```

(The root fix — making the IR legalizable on a generic CPU — is not done yet;
see ledger entry A438.)

Debug builds (`-g`) automatically enable leak detection and ARC quarantine (`--check-leaks --arc-guard`). Set `ZAN_ARC_TRACE=1` when running the resulting executable to print real-time ARC events (alloc/retain/release with addresses and call sites).

---

## 2. CLI Options Reference

### Output & Optimization
| Flag | Description |
|---|---|
| `-o <file>` | Output file path. Extension determines type: `.dll/.so/.dylib` = shared lib, `.a/.lib` = static lib, otherwise executable |
| `--emit-lib` | Force library output (no CRT entry point) |
| `--publish` | Release distribution: optimization (default `-Os`) + symbol stripping |
| `-O0..-O3/-Os/-Oz` | Optimization level: default `O0`, `--publish` default `Os`, `-Oz` for extreme size constraint |
| `-g, --debug` | DWARF debug symbols (forces `O0`, enables `--check-leaks` and `--arc-guard`) |
| `--icon <f.ico>` / `--no-icon` | Embed / skip Windows application icon |
| `--embed <path[=name]>` | Bake file or directory into executable resources (repeatable) |
| `--emit-apk` | Full-pipeline Android APK builder; pairs with `--apk-package` / `--apk-label` |
| `--emit-ipa` | Full-pipeline iOS IPA builder; pairs with `--ipa-bundle-id` / `--ipa-name` |

### Standard Library & Packages
| Flag | Description |
|---|---|
| `--auto-stdlib` | Locate stdlib & installed packages; on-demand pull-in of referenced namespaces |
| `--stdlib-path <dir>` | Explicit stdlib directory path |
| `--no-packages` | Disable package discovery while keeping stdlib pull-in |
| `--package-api <url>` | Package registry endpoint |
| `--package-install <dir>` | Install a local package |

### Target & Runtime
| Flag | Description |
|---|---|
| `--target <name>` | Cross-compilation target (see §3) |
| `--list-targets` | Print all available target triples |
| `--subsystem <console\|windows>` | PE subsystem for Windows binaries |
| `--fast-alloc` / `--no-fast-alloc` | Fast small-object thread-caching allocator (default on native win/linux) / fallback to CRT malloc |
| `--no-runtime-checks` | Disable runtime guards (e.g. division-by-zero checks) |
| `--strict-runtime` | Abort immediately with exit(70) upon runtime guard violation |
| `--link-mode <shared\|static>` | Native driver link mode for publish builds (default: `shared`) |

### Native Linking
| Flag | Description |
|---|---|
| `-L<dir>`, `--libpath <dir>` | Append native library search directory |
| `--driver-dir <dir>` | Override built-in native driver search directory |
| `--link-lib <name>` | Extra `-l<name>` link flag |
| `--link-input <file>` | Extra object/resource/library input file |

### Compiler Internals & Diagnostics
| Flag | Description |
|---|---|
| `--dump-tokens`, `--dump-ast`, `--emit-ir` | Frontend & IR diagnostics |
| `--check-leaks`, `--arc-guard` | Leak reporting & quarantined free blocks |
| `--deny-warnings` | Treat compilation warnings as fatal errors |
| `--no-gen` | Disable Zan code generators (used in bootstrap) |
| `--api <symbol>` | Instant symbol lookup from knowledge index without compilation |
| `--time`, `-q`, `--quiet` | Timing breakdown / quiet mode |
| `-D<name>[=value]` | Preprocessor definition |
| `@<file>` | Response file: read CLI arguments from file (newline/space separated) |
| `-v, --version`, `-h, --help` | Print version / usage help |

---

## 3. Supported Targets (`--list-targets`)

| Target | Triple / ABI | Description |
|---|---|---|
| `win-x64` / `win-arm64` | MSVC ABI | Windows 64-bit |
| `linux-x64` / `linux-arm64` | glibc | Linux 64-bit |
| `linux-musl` | musl static | Portable container / single-binary server |
| `linux-riscv64` (alias `riscv64`) | musl static | RISC-V 64 Linux |
| `riscv32` (alias `esp32c3`) | bare-metal | ESP32-C3 / C6 (object output) |
| `macos-x64` / `macos-arm64` | Apple Mach-O | macOS Intel / Apple Silicon |
| `wasm32` | WASI | WebAssembly / Node (EH supported) |
| `android-x64` / `android-arm64` | bionic API 28+ | Android (pairs with `--emit-apk`) |
| `ios-arm64` | Apple iOS | iPhone / iPad (pairs with `--emit-ipa`) |
| `ohos-x64` / `ohos-arm64` | musl static | OpenHarmony |

---

## 4. Environment Variables

### Compile Time (`zanc`)
- `ZAN_LIB_PATH`: Additional native library search paths (`;` on Windows, `:` on Unix).
- `ZAN_VERBOSE_LINK`: Echo link command line to inspect `-L` / `-l` flags.
- `ZAN_PULLIN_DEBUG`: Print standard library pull-in decisions.
- `ZANC_DUMP_BAD_IR`: Dump offending function IR when LLVM verifier fails.
- `JAVA_HOME`: JDK directory for APK signing in `--emit-apk`.

### Runtime (Target Executable)
- `ZAN_RT_HARD`: Exit immediately on guard violation (equivalent to `--strict-runtime`).
- `ZAN_CO_WORKERS`: Number of workers in multithreaded coroutine scheduler (default: `min(cores, 4)`).
- `ZAN_CO_QUANTUM_MS`: Cooperative preemption time slice in ms (default: `2`).
- `ZAN_CO_STACK`: Stack size for `Task.Spawn`/`Run` fibers (default: `128KB`).
- `ZAN_ARC_TRACE`: Set to `1` on `--check-leaks` binaries to trace all ARC allocation/retain/release events.

---

## 5. Testing & Verification

| Tier | Command | Scope & Usage |
|---|---|---|
| Single Case | `cd build && ctest -R <name>` | Narrowest check, runs in seconds; run for any touched feature |
| Filtered | `scripts/test.ps1 <tier> -Match "<regex>"` | Targeted subset run |
| Smoke | `scripts/test.ps1 smoke` | 75 cases; run on explicit demand |
| Standard | `scripts/test.ps1 standard` | 399 cases; pre-release milestone verification |
| Full | `scripts/test.ps1 full` | 1000+ cases; full release gate |

---

## 6. Known Pitfalls

1. **Pull-in scope affects resolution**: `--auto-stdlib` only pulls referenced namespaces. If symbols clash across unimported namespaces, it will only manifest in full-pull scenarios (such as IDE builds).
2. **Multi-file projects require listing all sources**: Passing only `src/main.zan` will not discover peer files automatically. Use shell globbing or response files (`@sources.rsp`).
3. **Design files (.html) must come first**: In declarative GUI apps, the design template must be the first command-line argument for code synthesis to wire up controls properly.
4. **Use `@file` for long commands**: On Windows, command-line length limits can be exceeded; place arguments in a response file.
