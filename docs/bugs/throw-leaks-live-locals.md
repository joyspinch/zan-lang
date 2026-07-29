# `throw` does not release live locals in the throwing frame

**Status:** Fixed (2026-07-27) — A8-3 + A8-12 (throwing frame + intermediate frames). See TASKS.md A8-3 / A8-12.

## Summary

Throwing an exception while a reference-counted object is live in the current
scope leaks that object (and everything it owns). The unwind path skips the
release sequence the normal fall-through path emits, so the allocation is
still reachable at exit and `--check-leaks` reports it.

## Minimal repro

`docs/bugs/repro_throw_leaks_locals.zan`:

```zan
using System;
using System.Json;

class Program {
    static void Doit() {
        JsonValue v = JsonValue.Parse("{\"a\":1}");
        if (v.Has("a")) {
            throw new Exception("boom");
        }
    }
    static void Main() {
        try { Doit(); } catch (Exception e) { Console.WriteLine("caught"); }
    }
}
```

Built with `--check-leaks`, observed:

```
caught
zan: memory leak detected: 6 object(s) still reachable at exit
  1 object(s) leaked, allocated at stdlib/System/Json/JsonValue.zan:39:23
  1 object(s) leaked, allocated at stdlib/System/Json/JsonValue.zan:60:23
  1 object(s) leaked, allocated at stdlib/System/Json/JsonValue.zan:62:18 [List<string>]
  1 object(s) leaked, allocated at stdlib/System/Json/JsonValue.zan:63:18 [List<JsonValue>]
```

Expected: no leak — `v` should be released as the frame unwinds.

Removing the `throw` (returning instead) leaks nothing, so the object graph
and its release sequence are otherwise correct.

## Impact

Any library that validates *after* parsing/allocating and reports failure by
throwing. `stdlib/System/Net/Http/Client/HttpClient.zan` already works around
this by documenting that it establishes the connection *before* building the
request "so a connect failure throws with no allocations live in this frame".

Locals declared *inside* a loop body count too, and are easy to miss because
they look dead by the time the `throw` is reached: the timeout path in
`HttpClient.RequestAsync` leaked exactly one object until the per-iteration
`string chunk = await conn.RecvAsync(...)` was hoisted out of the read loop and
cleared before unwinding.

`stdlib/Sdk/Jd/JdResponse.zan` follows the same discipline: everything needed
for the error message is copied out of the parsed tree, and the tree is
dropped before the `throw`. That is a workaround, not a fix — the general
pattern stays leaky until unwind runs the scope's release sequence.
