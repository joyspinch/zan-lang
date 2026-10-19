# Bootstrapping the Zan compiler

This document describes how the Zan compiler bootstraps itself: a compiler for
Zan, written in Zan, that compiles its own source code — and does so as a fixed
point (two successive self-compiles produce byte-identical output).

## Generations

| Gen  | What it is                                                        | Built by |
|------|-------------------------------------------------------------------|----------|
| gen0 | The C host compiler in `src/compiler/` (LLVM API backend)         | CMake / clang |
| gen1 | The self-hosted compiler `src/selfhost/*.zan`, compiled by gen0   | gen0 |
| gen2 | gen1 compiling its own source to `g2.ll`, then linked by clang    | gen1 + clang |
| gen3 | gen2 compiling the same source to `g3.ll`                         | gen2 |

> **✅ 固定点已恢复（2026-10-20）。** 此前长期红的 `selfhost_fixed_point` 门
> （2026-08-08 记为 `TASKS.md` B6-SH1：`ref`/`out` 形参解析缺口 + `Stopwatch.Start`
> 缺口）已修复合拢：gen1 → g2.ll → gen2 → g3.ll 全链打通，**g2.ll 与 g3.ll
> 逐字节相同**（15,815,207 字节，SHA256 相同）。恢复过程中修掉的是自举后端的
> 一整类缺陷（ARC 临时值支配违规、手工发标签导致 phi 前驱错、delegate 调用
> 签名错、写文件用文本模式导致 CRLF 差异、内联协程调度器与运行时重名），
> 详见 `.agents/skills/zan-compiler-internals/references/selfhost-closure.md`。

**The self-compilation fixed point holds when `g2.ll` and `g3.ll` are
byte-identical.** At that point the compiler is a fixed point of itself: gen1
and gen2 implement the same translation, so feeding the compiler through itself
no longer changes the output. Current verification: both are 15,815,207 bytes
(~15.1 MB) with identical SHA256 — the size tracks the current sources, so
re-run the closure to confirm the two generations still match.

Note that gen1 (produced by the C host) and gen2 (produced by the self-hosted
compiler) need not be byte-identical, because gen0 and gen1 are two different
implementations of codegen. The invariant is `gen2 == gen3`, i.e. the
self-hosted backend has reached a fixed point.

## Policy: the self-hosted compiler is authoritative

`src/selfhost/*.zan` is the source of truth for the language. The C host
(`src/compiler/`) exists only to *bootstrap* gen1 and to run in CI where clang
may be absent; it is **not** the reference semantics.

- Fix language bugs and add language features in the self-hosted compiler first
  (`parser.zan` / `checker.zan` / `irgen.zan` / ...), and gate them with the
  `selfhost_gen1` program test and the `selfhost_fixed_point` closure.
- Touch the C host **only when necessary** -- typically to keep it able to
  compile the self-hosted sources (so the two implementations agree closely
  enough for the bootstrap to run). When the C host and the self-host disagree,
  the self-host wins and the C host is brought in line, not the other way round.
- Every behavioural fix should come with coverage that exercises the
  *self-hosted* compiler (extend `tests/selfhost/prog1.zan`, which runs through
  gen1), not only the C-host conformance suite.

## Architecture

`src/selfhost/` (32 files, plus 2 runtime shims under `src/selfhost_runtime/`)
mirrors the module structure of the C host in `src/compiler/`:

| File          | Role                                                       |
|---------------|------------------------------------------------------------|
| `token.zan`   | Token kinds (as static `int` methods) + `Token` type       |
| `ast.zan`     | Single tagged `Node` type + `AK` node-kind constants       |
| `lexer.zan`   | Lexer over the SPEC token set                              |
| `parser.zan`  | Recursive-descent + Pratt expression parser                |
| `diag.zan`    | Diagnostics collection                                     |
| `binder.zan`  | Symbol/type resolution                                     |
| `checker.zan` | Type checking                                              |
| `irgen.zan`   | Lowering to LLVM IR **text**                               |
| `irgen_expr.zan` | Irgen part: expression codegen (assignment, member access, indexing, …) |
| `irgen_stmt.zan` | Irgen part: statement codegen, vtable emission for polymorphic classes |
| `irgen_async.zan` | Irgen part: async/await CPS lowering (heap frames, resume steps) |
| `jsongen.zan`  | Compile-time entity-mapper generation for `System.Json` (mirrors the C host pass) |
| `dbgen.zan`    | Compile-time typed ORM query generation for `System.Data` (mirrors the C host pass) |
| `attrgen.zan` | Compile-time attribute/route synthesis (mirrors the C host pass) |
| `lsp.zan`     | Language-server surface used by the driver                    |
| `nio.zan`     | Binary file output helper                                     |
| `ngen*.zan`   | Cross-platform machine-code generator (x86/ARM, Mach-O/ELF/COFF/Wasm) |
| `main.zan`    | Driver: read files, run the pipeline, write `.ll`          |

### Design choices dictated by the bootstrap subset

The self-hosted compiler is written in the *bootstrap subset* of Zan — the
subset the host can compile and that is sufficient to write a compiler. To stay
inside it:

- **Tagged AST.** A single `Node` class with an `int kind` and a fixed set of
  fields/child lists, instead of a class hierarchy with virtual dispatch. This
  avoids relying on inheritance/vtables.
- **Token kinds and node kinds are static `int` methods** (`TK.Plus()`,
  `AK.Binary()`), not `enum`s, to keep the host requirements minimal.
- **Backend emits LLVM IR as text**, which clang then compiles/links to a
  native executable — equivalent to how the C host drives the LLVM C API.

### Bootstrap subset

The compiler source uses only: namespaces/`using`; `class` with fields, a
constructor, static and instance methods; `int`/`bool`/`char`/`string`;
`List<T>`; arrays and `new`; `if`/`else`/`while`/`for`/`switch`; `break`/
`continue`; recursion; casts; string indexing/`.Length`/`.Substring`/concat/
`==`; short-circuit `&&`/`||`; and the `Console`, `Environment` and `File`
builtins listed below. It does **not** use generics beyond `List<T>`,
interfaces, inheritance, `async`/`await`, exceptions, lambdas/closures, pattern
matching or properties.

### Host builtins relied on by the driver

`main.zan` reads its inputs and writes its output through a few builtins that
both gen0 and the self-hosted `irgen.zan` implement identically:

- `Environment.ArgCount()` / `Environment.ArgAt(i)` — command-line arguments
  (lowered against module globals `@__zan_argc` / `@__zan_argv`, which `main`
  populates from its `argc`/`argv`).
- `File.ReadAllText(path)` / `File.WriteAllText(path, text)`.
- `Console.Write` / `Console.WriteLine`.

## Reproducing the closure

Prerequisites: a built host compiler (`build/zanc[.exe]`) and `clang` on `PATH`.

POSIX:

```sh
scripts/bootstrap.sh
```

Windows:

```bat
scripts\bootstrap.bat
```

Each script runs the five steps in the table above and asserts `g2.ll` ==
`g3.ll`.

> **源清单只有一份。** `scripts/selfhost_sources.txt` 是自举源清单的唯一真相；
> `scripts/bootstrap.sh` / `scripts/bootstrap.bat` 与两个 ctest harness
> （`tests/run_selfhost.cmake` / `tests/run_fixedpoint.cmake`）都从它读源文件。旧版脚本各自执点一份
> 清单（bootstrap 那份还停在 9 个文件的老子集），新增自举源时闭环会静默少编一个文件，
> 门红了很久也没人知道红在哪——这就是 2026-10 回归闭环时的第一个真正根因。

## Continuous integration

The `selfhost_gen1` ctest proves, on every platform (no clang required), that:

1. the C host can compile the **entire** self-hosted compiler into gen1,
2. gen1 lexes/parses/binds/checks/lowers a real program
   (`tests/selfhost/prog1.zan`) to valid LLVM IR (clang-linked, run, and its
   stdout diffed when clang is present), and
3. gen1 rejects a malformed program fail-closed (non-zero exit, no `.ll`).

Where clang is available, the `selfhost_fixed_point` ctest additionally runs the
**full closure** (`gen0 -> gen1 -> g2.ll -> clang -> gen2 -> g3.ll`) and asserts
`g2.ll == g3.ll` byte-for-byte — the fixed-point gate. It is skipped
automatically when clang is absent (`tests/run_fixedpoint.cmake`).
**Green on HEAD** (verified 2026-10-20: `g2.ll == g3.ll`, 15,815,207 bytes).

### The fixed point is not the whole story

`g2.ll == g3.ll` proves the self-hosted backend is a fixed point **of itself** —
both generations come from the self-hosted backend, so a systematically wrong
backend still passes. The `selfhost_diff` ctest
(`tests/run_selfhost_diff.cmake`) closes that hole: it hands a curated set of
conformance cases to gen1, links the emitted IR with the same runtime the host
links, runs the programs and compares stdout with the C host's golden file.

- The case list lives in `tests/selfhost/diff_cases.txt`.
- Cases that still differ live in `tests/selfhost/diff_known_gaps.txt`: they are
  reported (so the gap stays visible) but do not fail the gate. Delete a line
  there once the gap is closed and the gate starts holding that case.
- The gate found three silent errors on its first run (2026-10-20): `foreach`
  only handled `List<T>` and silently dropped arrays and string arrays, an empty
  `case` section did not fall through into the next section, and `x is null`
  dereferenced a null type node inside the self-hosted compiler.
- A second round (2026-10-20) fixed the whole **async** path: an `async Main` was
  never started (the entry called the coroutine's ramp like an ordinary function,
  so the program printed nothing), and values that live across an `await` inside
  a `foreach` (the collection, the loop counter, the element variable) were kept
  in stack allocas that a resume re-creates — the loop restarted from zero.
- A third round (2026-10-20) fixed the **closure value ABI**: the self-hosted
  backend built closure records without the closure tag (bit 0) while its own
  call site discriminated a heap closure from a bare native function pointer by
  that very bit — so the record address was called as code and the program died
  with an access violation. Producer and consumer now agree on the tag, the same
  convention the C host uses (`rec | 1`).
- A fourth round (2026-10-20) fixed the **closure capture semantics**: a local
  that a closure captures now lives in a heap cell, and the closure record holds
  the cell, so a write to the local after the capture is visible inside the
  closure (C# semantics). Loop variables are snapshotted per iteration, as
  before. Async methods still capture by value (`TASKS.md`, A387).
- A fifth round (2026-10-20) fixed the **container built-ins**: `List<T>.AddRange`,
  `Insert`, `IndexOf` and `Contains` were never lowered by the self-hosted
  backend — the fallback branch of `EmitListMethod` returned 0 and the call was
  silently dropped, so the list stayed empty and the following element read ran
  out of bounds (access violation). They are now really lowered (the runtime
  grows `zlist_addrange` / `zlist_insert`), element comparison goes through a
  typed equality (`operator==` → struct field by field → string content →
  class identity → slot value), and a list method the backend does not
  implement is now refused with a diagnostic instead of silently doing nothing.
- A sixth round (2026-10-20) gave the self-hosted backend **managed
  strings**: a `string` value points at the payload of a block whose header
  carries the exact byte length and a magic word, and the payload stays
  NUL-terminated so the ABI towards the C runtime is unchanged. Before this
  the backend modelled strings as raw C strings, so `Length` was a
  `strlen`: `string view = byteArray;` produced a garbage length (measured
  1 and 6), `"AB\0CD"` was truncated to 2, and an embedded NUL truncated the
  value. The differential-gate case `string_view_length` now matches the C
  host line for line. A `byte[]`/`char[]` used as a string is **copied and
  packed** rather than shared as a view (the self-hosted array stores one
  element per 8-byte slot) — tracked in `TASKS.md` (A390), together with
  the substring searches that still use `strstr`.
- Note that `clang` does **not** run the LLVM verifier on `.ll` input, so invalid
  IR (e.g. a value used in a block it does not dominate) reaches the backend
  silently and shows up as an access violation. Keep the emitted IR
  self-consistent; see the skill reference for the rules.

## Known limitations / future work

- **The self-hosted coroutine frame layout differs from the C host's
  `zan.co.header`.** The C host's frames are driven by the shipped runtime
  (`zanrt_io_mt.o`), which reads the frame header as a `zan_co_fhdr`; the
  self-hosted backend uses its own layout and therefore ships its own (weak)
  cooperative scheduler. A program compiled by the self-host can therefore not
  be linked against the shipped multi-threaded runtime for async work. Tracked in
  `TASKS.md` (A385).
- **Runtime class identity is missing in the self-hosted IR backend.** The C host
  decides `x is T` from the allocation site's id plus an ancestor-name table
  (`__zan_site_tynames`); the self-hosted backend stamps no per-object class id
  at all (its object header is `[-16]=rc, [-8]=dtor` and its strings are raw
  `malloc`ed C strings). A switch pattern over an `object`-typed scrutinee
  (`case Dog d`, `case string s`) therefore cannot be decided at runtime: the
  self-hosted compiler now **refuses to compile it** with an explicit diagnostic
  instead of silently never matching. Tracked in `TASKS.md`.
- **Closure capture is by value inside async methods.** In synchronous code a
  local that a closure captures lives in a heap cell and the closure holds the
  cell, so the closure observes later writes (C# semantics). An async method's
  locals live in the coroutine frame instead of stack allocas, and that path
  does not box them yet, so a closure created in an async method still gets a
  value snapshot. Tracked in `TASKS.md` (A387) with the differential-gate case
  `tests/conformance/closure_capture_async.zan`.
- **`struct` is a heap object (reference semantics) in the self-hosted
  compiler, while the C host copies it by value.** A `struct` that is
  assigned, passed as an argument or pushed into a `List<T>` shares one heap
  object in the self-hosted build, so a write through any alias is visible
  everywhere; the C host copies the value (C# `struct` semantics). Measured
  2026-10-20 on the same program: after `list.Add(p); p.x = 99;` the
  self-hosted build prints `list[0].x` as `99` and the C host as `1`; a
  by-value parameter write and a plain `P3 r = q;` assignment behave the
  same way. Tracked in `TASKS.md` (A389).
- **Language coverage.** The self-hosted compiler covers substantially more of
  the language than the subset its own sources are written in — including
  classes with inheritance and virtual dispatch, interfaces, `List<T>`/
  `Dictionary<K,V>`, `foreach`, `try`/`catch`/`throw`, lambdas, properties,
  operator overloading, type-checked overload resolution, `async`/`await`
  CPS lowering, `yield`, and LINQ query syntax. Remaining gaps versus
  `docs/SPEC.md` / the C host: full pattern matching, string interpolation,
  and exception handling refinements — a single catch clause per try (no
  type-based dispatch or rethrow-to-outer), and ARC edge cases the C host
  now covers (a throw escaping a catch handler releases the caught object
  via the EH temp stack; async CPS frames skipped by an exception unwind are
  released via `__zan_async_unwind`). The basic caught-exception release is
  present in both compilers: the C host transfers an in-flight +1 to the
  handler, the selfhost releases the exception through the catch-variable
  local on normal handler exit.
- **Diagnostics** are basic (limited positions, no recovery).
- **No optimization**: the emitted IR is naive.

### Generics, numeric and equality semantics

- **Nested generic close (`>>`).** The lexer greedily forms the compound
  `>>` / `>>=` operator tokens, so a nested type argument list such as
  `Box<Box<int>>` closes two levels with a single token. The host parser
  splits this virtually at the parse site (`parser_expect_gt` in
  `src/compiler/parser.c`): the first `>` closes the inner list and the token
  is rewritten to a single `>` for the enclosing level, without disturbing the
  lexer. Nested generics therefore parse and compile; note that value storage
  through *nested erased* generic slots (e.g. reading back a `List<List<int>>`
  element) is still incomplete.
- **`double` in the self-hosted compiler.** `gen1` now lowers `double`
  end-to-end: float literals, `fadd`/`fsub`/`fmul`/`fdiv`/`frem` arithmetic,
  `fcmp` comparisons, `fneg`, compound assignment, `int`↔`double` casts
  (`sitofp`/`fptosi`), implicit int→double promotion, `%g` printing and
  string concatenation. Combined with the erased-slot `bitcast double <-> i64`
  boundary, `Box<double>` passes through `gen1`.
- **`bool` printing.** A `bool` is an LLVM `i1`; when widened to `i64` for
  numeric formatting it is **zero-extended** (`zext`), not sign-extended, so
  `true` prints as `1` rather than `-1` (`sext i1 1` is all-ones = `-1`).
  Other narrow integers keep signed widening (`sext`).
- **Monomorphized generics keep the erased LLVM ABI.** User-generic
  specialization only specializes the function *body*; signatures still pass
  and return values through the erased `i64` slot ABI (they are not rewritten
  to concrete-typed parameters). Value types are packed/unpacked at the slot
  boundary (`ptrtoint`/`inttoptr`, or `bitcast` for `double`).
- **Content equality is intrinsic only for `string`.** The built-in equality
  intrinsic performs a `strcmp` for strings; equality of user-defined
  reference types remains whatever that type's own logic defines (identity, or
  a user `op_eq`). There is no universal structural-equality intrinsic.

(The emitter uses a `StringBuilder`, so IR assembly is O(output size); see
`../archive/PERFORMANCE.md` §6 for the memory history.)
