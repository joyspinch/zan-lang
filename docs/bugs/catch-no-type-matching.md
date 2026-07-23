# catch clauses: no type-based matching; only the first clause is emitted

## Summary

The C-host irgen lowers `try/catch` by unconditionally binding the thrown
exception to the **first** catch clause (`irgen_stmt.c`, `AST_TRY_STMT`:
`stmt->try_stmt.catches.items[0]`). There is:

- no runtime type test of the thrown object against the catch clause type;
- no support for multiple catch clauses (clauses after the first are ignored);
- consequently `catch (DerivedException e)` will also "catch" a plain
  `Exception` (and vice versa), and a non-matching first clause can crash when
  the handler touches members the object does not have.

## Minimal repro

```zan
try { throw new Exception("plain"); }
catch (IOException e) { Console.WriteLine("WRONG: matched wrong type"); }
catch (Exception e)   { Console.WriteLine("expected here"); }
```

Observed: crashes (0xC0000005) / wrong-clause execution; expected: second
clause runs. SPEC.md 5.3 documents multiple typed catch clauses
(`catch (FileNotFoundException e)` then `catch (Exception e)`).

## Fix direction

Attach type info to thrown class objects (ARC header already carries
type_info), emit a per-clause dispatch chain in the catch block (compare
type_info against each clause's type, walking the base-class chain), and
re-throw to the outer handler when no clause matches. Mirror in
`src/selfhost/irgen*.zan` first (self-host is authoritative), then align the
C host.

## Impact

Typed exception hierarchies are unusable; blocks the stdlib error model
(PRODUCTION_PLAN.md 1.2). Workaround until fixed: throw plain `Exception`
with a standardized message prefix ("IOException: ...").

## Status

Fixed (see Resolution below; re-verified 2026-07-23 against build/zanc).


## Resolution (2026-07)

Fixed in the C-host compiler (`irgen_stmt.c` + `irgen_builtins.c`):

- every class gets an `__zan_tid_<Class>` descriptor global whose value links
  to its base class's descriptor (inheritance chain);
- `throw` records the thrown class's descriptor in `__zan_eh_exc_tid`;
- the catch path tests each clause in order via `__zan_eh_tid_match` (walking
  the base chain, so `catch (Base)` matches derived throws); an untyped clause
  catches everything; a null descriptor (string/legacy throw) matches the
  first clause;
- if no clause matches, the exception is rethrown to the next outer handler
  (or reports `Unhandled exception` and exits when none remains).

Known limitation: a rethrow bypasses this try's `finally` body.

Covered by `tests/conformance/exceptions_typed_catch.zan`.

Note: the selfhost compiler is an explicitly single-catch bootstrap subset
(no inheritance), so type dispatch there is out of scope until the subset
grows inheritance.
