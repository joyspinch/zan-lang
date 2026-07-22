# -O0 codegen: index-writing a `List<T>` at a varying index inside a loop leaks stack

## Summary

At the default optimization level (`-O0`), an indexed **write** to a `List<T>`
whose index expression **varies per iteration** (e.g. `a[k % 5 + 1] = ...`)
allocates stack space on every iteration without reclaiming it. In a loop with
enough iterations this overflows the stack and the program crashes with
`EXC_BAD_ACCESS` (SIGSEGV). Compiling the same program with `-O2` runs correctly.

## Minimal repro

`repro_list_index_write.zan`:

```zan
using System;

class Repro {
    static void Main() {
        List<int> a = new List<int>();
        int c = 0;
        while (c < 70) { a.Add(0); c = c + 1; }
        int k = 0;
        while (k < 1000000) {
            int idx = k % 5 + 1;
            a[idx] = a[idx] + 1;   // varying write index
            k = k + 1;
        }
        Console.WriteLine("ok " + Convert.ToString(a[1]));
    }
}
```

```
zanc      repro_list_index_write.zan -o repro && ./repro   # -> Segmentation fault: 11
zanc -O2  repro_list_index_write.zan -o repro && ./repro   # -> ok 200000
```

## Crash detail

```
stop reason = EXC_BAD_ACCESS (code=2, address=0x16f603ff0)   # a stack address
frame #0: main + 308
->  stur   x12, [x10, #-0x10]
```

`code=2` (write) at a descending stack address is a guard-page hit from the
stack having grown across the loop — i.e. an `alloca` emitted **inside** the
loop body for the indexed-store lvalue, never freed until the function returns.

## What does / doesn't trigger it (all at -O0)

| write form | index | result |
|---|---|---|
| `a[0] = a[0] + 1` | constant | ok |
| `a[idx] = ...`, `idx` set once before loop | loop-invariant | ok |
| `a[k] = a[k] + 1` | equals loop counter | ok |
| `a[k % 5 + 1] = ...` | varying computed | **crash** |

Reads at a varying index (`s = s + a[k % 5 + 1]`) are fine — only the **store**
lvalue leaks.

## Impact

Hits any hot loop that accumulates into list slots by a computed index — e.g.
column-wise aggregation over ~1M rows (`sum[col] += cell`) and the bottom-up
merge sort's merge step. Both crash at `-O0` and are fine at `-O2`.

## Workarounds

- Build with `-O2` (`--publish` already does).
- Accumulate into scalar locals and write the list slot **once**, outside the
  hot loop.

## Fix direction

At `-O0`, emit the indexed-store address computation without a per-iteration
`alloca` (reuse a fixed stack slot / compute into a register), matching how the
constant- and loop-counter-indexed cases are already handled.

## Status

Fixed. Locals declared in loop bodies (and codegen helper temporaries) are now allocated once in the function entry block (emit_entry_alloca) instead of a fresh alloca at the current insertion point, so the stack no longer grows per iteration at -O0. Regression test: tests/conformance/list_index_write_o0.zan.
