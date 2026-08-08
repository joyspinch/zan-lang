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

> **⚠️ 2026-08-08 状态：固定点已破（`TASKS.md` B6-SH1）。** gen1 → g2.ll 当前
> 失败，根因是自举编译器没实现 `ref`/`out` **形参声明**（`dbgen.zan` 自
> `e01975ee` 起用了 `ref int outKind`，1 个签名 + 2 处调用；把 `ref` 改写为
> `out` 也失败——selfhost parser 只在调用实参位置认 `out`，形参声明完全不认
> 修饰符）。另有独立缺口 "type 'Stopwatch' has no member 'Start'"。修复前
> `selfhost_fixed_point` 允许红（smoke/standard 档不含它，full 档 `ctest -R
> selfhost` 会红）。

**The self-compilation fixed point holds when `g2.ll` and `g3.ll` are
byte-identical.** At that point the compiler is a fixed point of itself: gen1
and gen2 implement the same translation, so feeding the compiler through itself
no longer changes the output. Historical verification: both were 2,106,150
bytes (~2.0 MB) and `fc /b` / `cmp` reported no difference — this predates the
B6-SH1 breakage above and no longer holds. (The exact size tracks the current
sources; re-run the closure to confirm the two generations match.)

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

`src/selfhost/` (currently 14 files) mirrors the module structure of the C host
in `src/compiler/`:

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

> **注意：脚本需同步。** `scripts/bootstrap.sh` / `scripts/bootstrap.bat` 目前只
> 编译 **9 个**源文件（缺 `irgen_async.zan` / `irgen_expr.zan` / `irgen_stmt.zan` /
> `jsongen.zan` / `dbgen.zan`），而规范清单（`tests/run_selfhost.cmake` 列 14 个
> 源文件）已包含全部文件——按脚本当前清单无法成功闭环（gen1 缺文件，`g2.ll`
> 不可能产出；叠加顶部 B6-SH1 的解析缺口）。

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
**Currently red on HEAD** — see the B6-SH1 warning at the top of this file.

## Known limitations / future work

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
- **`ref`/`out` parameter declarations.** The selfhost parser's `ParseParams`
  only accepts `params` on formal parameters; `out`/`ref` modifiers are not
  parsed there (call-site `out` is special-cased for the built-in `TryParse`
  only). `dbgen.zan` needs `ref int outKind` — this is the root cause of the
  broken self-compilation fixed point (B6-SH1).
- **`Stopwatch.Start` member resolution.** gen1's binder does not resolve
  `Stopwatch.Start` (`Stopwatch.zan:35`) — an independent stdlib-coverage gap
  in the bootstrap compiler (B6-SH1).
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
`docs/PERFORMANCE.md` for the memory history.)
