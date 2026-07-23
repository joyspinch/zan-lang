# Subclassing a cross-module class silently mis-binds inherited fields

## Summary

A class that inherits from a class defined in another stdlib module (observed
with `System.Exception` under `--auto-stdlib`) compiles, but assignments to the
inherited field do not stick: reading the field back yields a default value
(`0` / null) instead of the assigned value.

Same-file inheritance works correctly (see contrast below), so this is specific
to base classes resolved across module/compile-unit boundaries.

## Minimal repro

```zan
using System;

class IOException : Exception {          // Exception from stdlib (auto-stdlib)
    public IOException(string m) { Message = m; }
}

class Program {
    static void Main() {
        IOException x = new IOException("mk");
        Console.WriteLine("made: " + x.Message);   // prints "made: 0", expected "made: mk"
    }
}
```

Also affected: `: base(m)` initializers — the C-host parser parses and then
**silently discards** the base/this constructor initializer
(`src/compiler/parser.c`, "skip base(...) or this(...) for now"), so even the
canonical C# form cannot set the base field.

## Contrast: same-file inheritance works

```zan
class A { public string M; public int N; }
class B : A { public B(string m) { M = m; N = 7; } }
// new B("hello").M == "hello"  -- correct
```

## Impact

Exception subclasses (IOException, FileNotFoundException, ...) cannot carry a
message — blocks a typed error model for the stdlib (PRODUCTION_PLAN.md 1.2).

## Status

Fixed (see Resolution below; re-verified 2026-07-23 against build/zanc).


## Resolution (2026-07)

Fixed in the C-host compiler:

- `binder.c`: base resolution and member inheritance moved out of pass 2 into
  a new pass 3 (`resolve_bases`) that runs after every type's own members are
  bound, so declaration order across merged files (user sources before
  auto-stdlib) no longer matters. Multi-level chains resolve recursively;
  inherited fields are prepended so base fields remain a prefix of the derived
  layout.
- `parser.c` / `ast.h`: `: base(args)` constructor initializers are now stored
  on the constructor node (`method_decl.base_args`) instead of being discarded.
  (`: this(...)` chaining is still unsupported and skipped.)
- `irgen_emit.c`: a derived constructor chains to the base constructor selected
  by `: base(args)` argument count (or the parameterless one implicitly).

Covered by `tests/conformance/exceptions_typed_catch.zan`.

Note: the selfhost compiler is a bootstrap subset without inheritance, so no
selfhost change is required.
