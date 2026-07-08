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

**Self-hosting is proven when `g2.ll` and `g3.ll` are byte-identical.** At that
point the compiler is a fixed point of itself: gen1 and gen2 implement the same
translation, so feeding the compiler through itself no longer changes the
output. Verified: both are 744,014 bytes and `fc /b` / `cmp` report no
difference.

Note that gen1 (produced by the C host) and gen2 (produced by the self-hosted
compiler) need not be byte-identical, because gen0 and gen1 are two different
implementations of codegen. The invariant is `gen2 == gen3`, i.e. the
self-hosted backend has reached a fixed point.

## Architecture

`src/selfhost/` mirrors the module structure of the C host in `src/compiler/`:

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

## Continuous integration

CI does not run the full closure (it needs clang to link the emitted `.ll`, and
a large stack). Instead the `selfhost_gen1_emits_ir` ctest proves, on every
platform, that:

1. the C host can compile the **entire** self-hosted compiler into gen1, and
2. gen1 lexes/parses/binds/checks/lowers a real program
   (`tests/selfhost/prog1.zan`) to valid LLVM IR.

## Known limitations / future work

- **Memory.** The bootstrap subset has no ARC/`free`, and `irgen.zan`
  accumulates the whole module as a string (`body = body + ...`), which is
  O(n²) and never released — a full self-compile peaks around 13 GB. Buffered
  output (a `StringBuilder`/chunked file writes) and reclaiming temporaries
  would bring this down dramatically.
- **Language coverage.** The self-hosted compiler implements the bootstrap
  subset, not the full language in `docs/SPEC.md` (generics, interfaces,
  inheritance, `async`/`await`, exceptions, lambdas, pattern matching,
  properties, operator overloading, `Dictionary`, `foreach`, string
  interpolation remain).
- **Diagnostics** are basic (limited positions, no recovery).
- **No optimization**: the emitted IR is naive.
