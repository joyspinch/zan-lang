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
- **Inputs are a file set, not a project folder**: `zanc` compiles only the `.zan` files listed on the command line (plus auto-resolved stdlib/package sources). In multi-file projects, specify all `src/**/*.zan` files explicitly -- **or hand it a directory** (`zanc src -o app.exe`), and the compiler expands `*.zan` recursively itself.
- Flags are grouped exactly like `zanc --help`: **Common** (inputs/output/optimization/targets/stdlib/native linking), **Publish** (`--publish`, `--strip-symbols`, string obfuscation), **Debug** (`-g`, leak/runtime guards, diagnostic dumps), **Packaging** (`--emit-apk`/`--emit-ipa` and their identity flags), and **Internal (unstable)** (`-ferror-limit`, package-market install channel).

---

## 1. Standard Recipes (Copy & Paste)

### 1.1 Daily Development Cycle

```bash
# Compile and run (auto pulls stdlib, default optimization -O2; only -g forces -O0)
build/zanc src/main.zan -o _scratch/app.exe && _scratch/app.exe

# Multi-file project: hand the directory to the compiler (expands *.zan
# recursively, sorted by path; the command line stays constant-length)
build/zanc src -o _scratch/app.exe

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
build/zanc src -o app.exe

# The compile-time HTML channel is gone: .html/.htm positional inputs are rejected
# ("the compile-time HTML channel has been removed"). Build the window with Gui
# widgets and load markup at runtime via Gui.App.LoadHtml; use --embed to ship assets.
build/zanc src -o app.exe
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

Debug builds (`-g`) force `-O0` and, **unless `--publish` is also given**, automatically enable leak detection and ARC quarantine (`--check-leaks --arc-guard`). `-g` does **not** imply `--strict-runtime`; the IDE's F5 runner passes it explicitly. Under `--publish` both diagnostics default off, while the over-release net stays on unless `--arc-guard`/`--no-arc-guard` is explicit; `-g --publish` still strips at link time, so it usually yields no usable debug info. Set `ZAN_ARC_TRACE=1` when running a `--check-leaks` executable to print real-time ARC events (alloc/retain/release with addresses and call sites).

---

## 2. CLI Options Reference

> Grouped exactly like `zanc --help`: **Common / Publish / Debug / Packaging /
> Internal (unstable)**. Defaults and interactions follow the parser in
> `src/compiler/main_options_parse.c` and the value application in
> `src/compiler/main.c`. Anything marked "(internal/unstable)" is not a public contract.

### 2.1 Common
| Flag | Value / default | Description |
|---|---|---|
| `-o <file>` | — | Output file. Extension determines type: `.dll/.so/.dylib` = shared lib, `.a/.lib` = static lib, otherwise executable |
| `--emit-lib` | off | Force library output (no CRT entry point); with `-o`, the suffix still decides shared vs static |
| `-O0..-O3/-Os/-Oz` | `-O2` | Optimization level; `--publish` defaults to `-Os`; `-Oz` = minimum size. An explicit `-O` overrides the publish default, but `-g` always forces `-O0` |
| `--target <name>` | host | Cross-compilation target (full list in §3) |
| `--list-targets` | — | Print all available target triples and exit |
| `--subsystem <console\|windows>` | auto | PE subsystem (Windows only); GUI apps infer `windows` |
| `--stdlib-path <dir>` | auto | Explicit stdlib root (pull-in stays on-demand) |
| `--auto-stdlib` | **on** | Locate stdlib & installed packages; on-demand pull-in of referenced namespaces; passing it restates the default |
| `--no-stdlib` | — | Disable automatic stdlib **and** package discovery; `--no-packages` disables packages only |
| `--no-packages` | — | Disable package discovery only (stdlib pull-in stays on) |
| `--fast-alloc` | on for native | Per-thread small-object allocator (`rt_mem.c`); not linked on Apple (macOS/iOS) or cross targets; warns and falls back to CRT when no allocator object exists |
| `--no-fast-alloc` | — | Fall back to CRT malloc (heap debugging / returning blocks to an external CRT); last of the two wins |
| `-L<dir>`, `--libpath <dir>` | — | Append a native library search directory (repeatable) |
| `--driver-dir <dir>` | bundled dir | Override the bundled native driver directory |
| `--link-lib <name>` | — | Extra `-l<name>`; the name may only contain `[A-Za-z0-9._+-]` (use `-L`/`--libpath` for paths) |
| `--link-input <file>` | — | Extra object/resource/library input file (repeatable) |
| `--embed <path[=name]>` | — | Bake a file/directory into executable resources (repeatable; embedded copies win at runtime, stored encrypted) |
| `--icon <f>` | built-in | Windows executable icon; `--emit-ipa` also uses it as the IPA icon (otherwise probes `assets/icon.png`, `assets/icon.ico`, `icon.png`, `icon.ico`); a failed embed warns and falls back to the built-in icon (none under `--publish` without `--default-icon`) |
| `--no-icon` | — | Embed no icon at all (skips `--icon` and the built-in default); Windows PE only |
| `--default-icon` | — | Explicitly embed the built-in icon; only needed under `--publish` (non-publish builds embed it anyway) |
| `--quiet`, `-q` | — | Suppress stdout progress lines (driver bundling, APK packaging, the "Compiled N files" summary) |
| `--time` | — | Print per-phase timings and scale stats |
| `-D<name>[=value]` | — | Preprocessor definition |
| `@<file>` | — | Response file (one per line or whitespace separated, `"quoted"` for paths with spaces, `#` comments, max 8 levels of nesting) |
| (positional, file/dir) | — | `.zan` files, or a **directory** expanded recursively into sorted `*.zan` inputs. Directory walk skips `.`-prefixed and `.git/build/publish/bin/obj/node_modules/_scratch/dist/out/vendor/.zan-packages` directories; an empty directory fails with "no .zan source files under '...'". **Only `.zan` is collected**: explicit `.html`/`.htm` inputs are rejected with "the compile-time HTML channel has been removed" — build the window with Gui widgets and load markup at runtime via `Gui.App.LoadHtml` (ship assets with `--embed`) |
| (no flag, automatic) | — | Windows/Linux/macOS x64/arm64 targets link the multi-worker coroutine scheduler (`zanrt_io_mt`); worker count order: `System.Threading.AsyncRuntime.SetWorkers` (set in Main) → `ZAN_CO_WORKERS` → logical cores. IO sharding/sync-fast paths similarly prefer `AsyncRuntime` over `ZAN_IO_SHARDS`/`ZAN_IO_SYNCFAST` |
| (default, native) | — | Windows/Linux native link the small-object allocator (`rt_mem.c`: ≤2048B size classes, lock-free caches, 8 cross-thread free bands, `ld --wrap` over malloc/free/calloc/realloc). Apple native does not link it (ld64 has no `--wrap`); Android cross never links it (bionic TLS bootstrap crash); linux-musl cross links it when the sysroot has the object. Debug with `ZAN_LINK_ECHO=1` to echo the link command |
| `--version`, `-v` / `--help`, `-h` | — | Print version / usage help and exit |

### 2.2 Publish
| Flag | Value / default | Description |
|---|---|---|
| `--publish` | off | Release build: `-Os` by default, link-time strip (same as `--strip-symbols`), shard-module optimization. `--check-leaks`/`--arc-guard` default off (explicit flags still apply). The **over-release net stays on** unless `--arc-guard`/`--no-arc-guard` is given. The built-in icon is skipped by default (`--default-icon` re-enables it) |
| `--strip-symbols` | off | Link-time `-s` + ICF + link optimization; implied by `--publish`; alone it only strips and does not change the optimization level |
| `--link-mode <shared\|static>` | shared | Native driver linking on publish: `shared` copies driver libs next to the exe, `static` links them into the exe |
| `--obfuscate-strings` | off | XOR-obfuscate user string literals (also prunes stdlib bodies when not `-g`). CLI wins over `ZAN_OBF=1` (internal/unstable) |
| `--no-obfuscate-strings` | — | Force obfuscation off; last one wins; `ZAN_NO_OBF=1` has the same effect (internal/unstable) |
| `--hide-diagnostics` | off | Replace `file:line:col` in runtime guard reports with `#site` IDs (compile diagnostics unchanged) (internal/unstable) |
| `--emit-site-map <f>` | — | Write the `#site -> file:line:col` map for `--hide-diagnostics` (internal/unstable) |

### 2.3 Debug
| Flag | Value / default | Description |
|---|---|---|
| `-g`, `--debug` | off | DWARF debug info; forces `-O0`; **unless `--publish` is also given**, enables `--check-leaks` + `--arc-guard`. It does **not** enable `--strict-runtime` (the IDE F5 runner passes that explicitly). With `--publish` the release link still strips, so debug info is usually unusable — avoid the combination |
| `--check-leaks` | `-g` and not publish | Report unreleased objects at program exit |
| `--no-check-leaks` | — | Turn leak reporting off |
| `--arc-guard` | `-g` and not publish | Quarantine freed blocks and trap stale retain/release (memory grows with allocations; diagnostics only); explicit under `--publish` too |
| `--no-arc-guard` | — | Turn quarantine off, which also turns off the `--publish` over-release net |
| `--runtime-checks` | **on** | Enable runtime guards (bounds, division by zero, etc.), including under `--publish` |
| `--no-runtime-checks` | — | Disable runtime guards; last of the two wins |
| `--strict-runtime` | off | Guard failures call `exit(70)`; `ZAN_RT_HARD` non-empty and non-`0` is equivalent; `ZAN_RT_HARD=0` explicitly forces soft mode |
| `--deny-warnings` | off | Treat compiler warnings as fatal errors |
| `--warn-level <none\|default\|error>` | `default` | `none` hides warnings; `default` prints normally; `error` equals `--deny-warnings`; invalid values exit 2 |
| `--suppress <list>` | — | Suppress **warnings** by numeric code (comma/space separated, letter prefix optional, e.g. `W0003,0003`); with `--deny-warnings`/`--warn-level=error` warnings are already errors and are not suppressed |
| `--dump-tokens` | — | Dump lexer tokens and exit |
| `--dump-ast` | — | Dump the parse tree and continue compiling |
| `--emit-ir` | — | Emit LLVM IR to stdout and exit without linking |
| `--emit-symbols <f>` / `--gen-meta <f>` | — | Write the symbol index / compilation-unit metadata and exit (IDE/LSP) |
| `--api <symbol>` | — | Query signatures and locations from the API index (no compilation) |

### 2.4 Packaging
| Flag | Value / default | Description |
|---|---|---|
| `--emit-apk <f>` | — | Full Android APK pipeline; **requires** `--target android-x64` or `android-arm64`; compiles as a shared library (`-o` names the temporary libmain path, removed after packaging; the final artifact is `<f>`); signing needs Java (`JAVA_HOME` or PATH) |
| `--apk-package <id>` | `dev.zan.<entry>` | APK package name; CLI beats `zan.proj` `androidPackage` |
| `--apk-label <name>` | entry basename | APK display label; CLI beats `zan.proj` `androidLabel` |
| `--emit-ipa <f>` | — | Full iOS IPA pipeline; **requires** `--target ios-arm64`; `-o <name>.ipa` enables it too; icon from `--icon` or `assets/icon.png` probes |
| `--ipa-bundle-id <id>` | `dev.zan.<app name>` | IPA bundle identifier |
| `--ipa-name <name>` | from the `.ipa` path | App name inside the IPA |

### 2.5 Internal (unstable)
| Flag | Value / default | Description |
|---|---|---|
| `-ferror-limit=N` | 100 | Stop after N errors; `0` = unlimited (internal/unstable) |
| `--package-api <url>` | — | Configure the package-market API (HTTPS; localhost HTTP allowed); must be paired with `--package-install` (internal/unstable) |
| `--package-list-missing` | — | Scan inputs and print missing-namespace diagnostics; exits 3 when anything is missing (internal/unstable) |
| `--package-install <dir>` | — | Install a local package directory; requires `--package-name` (internal/unstable) |
| `--package-name <name>` | — | Package name to install (internal/unstable) |
| `--package-scope <project\|global>` | `project` | Install scope (internal/unstable) |
| `--package-project <dir>` | `.` | Project root for package discovery / project-scope installation (internal/unstable) |

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
| Variable | Effect |
|---|---|
| `ZAN_LIB_PATH` | Extra native library search directories (`;` on Windows, `:` on Unix) — set it when TLS/DB driver import libs are not on the default path |
| `ZAN_VERBOSE_LINK` | Echo the native link command line (to see where `-L`/`-l` come from) |
| `ZAN_PULLIN_DEBUG` | Print standard-library pull-in decisions |
| `ZAN_NO_PULLIN_FILTER` / `ZAN_NO_PRUNE` | Pull-in debugging: disable filtering / disable pruning |
| `ZANC_DUMP_BAD_IR` | Dump the offending function's IR when the LLVM verifier rejects it |
| `ZANC_TRACE` | Compiler-internal tracing (unified diag.c exit) |
| `ZAN_TRACE_SYNC` | Print which external symbols pull in the sync/coroutine runtime |
| `ZAN_WARN_NARROW` | Narrowing-conversion warnings (silent by default) |
| `ZAN_OBF` / `ZAN_NO_OBF` | `=1` turns string obfuscation on/off by default; the CLI `--obfuscate-strings`/`--no-obfuscate-strings` wins |
| `ZAN_RT_HARD` | Non-empty and non-`0`: guard failures exit immediately; `=0`: force soft mode (overrides `--strict-runtime`) |
| `JAVA_HOME` | JDK used for APK signing in `--emit-apk` |
| `ZAN_GENMETA_DUMP` / `ZAN_GEN_REPLY` | genrun code-generator debugging |

### Runtime (read by the built executable; `env` is the only channel once the exe is built)
| Variable | Effect |
|---|---|
| `ZAN_RT_HARD` | Non-empty and non-`0`: guard failures exit immediately (equivalent to `--strict-runtime`); `=0` explicitly forces soft mode |
| `ZAN_CO_WORKERS` | Worker count of the multi-worker coroutine scheduler (native 64-bit targets use the multi-worker driver by default with no build flag; default `min(logical cores, 4)`, see `AsyncRuntime.SetWorkers` precedence) |
| `ZAN_CO_QUANTUM_MS` | Cooperative preemption time slice for coroutines (ms, default 2, `0` = off): the compiler emits `zan_co_poll` on every loop back-edge of async functions so compute-heavy frames exceeding the slice are requeued instead of starving timers/IO callbacks/sibling coroutines; accounting uses a microsecond monotonic clock (Windows QPC, POSIX CLOCK_MONOTONIC) |
| `ZAN_CO_STACK` | Stack size for `Task.Spawn`/`Run` fibers (default 128KB, range 64KB–16MB) |
| `ZAN_GUI_ICONS` | Override the icon-pack directory (search order: env → `icons/` next to the exe → embedded → stdlib) |
| `ZAN_CEF_*` | CEF browser driver runtime location (see `examples/gui_cef_browser/README.md` for the full table: RUNTIME/CACHE/MIRROR/LOG/SWITCHES/HELPER*) |
| `ZAN_ARC_TRACE` | Set to `1` on `--check-leaks` binaries to trace all ARC allocation/retain/release events |

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
3. **The compile-time HTML entry is gone**: `.html`/`.htm` positional inputs are rejected outright — "put the design first" no longer works. Build the window with Gui widgets and load markup at runtime via `Gui.App.LoadHtml`.
4. **Use `@file` for long commands**: On Windows, command-line length limits can be exceeded; place arguments in a response file.
