# `await` inside a `catch` block crashes on the second loop iteration

**Status:** Fixed (2026-07-27) — A8-1 (`await` in a `catch` block; the loop was incidental). See TASKS.md A8-1.

## Summary

An `await` executed inside a `catch` block that sits in a loop crashes
(SIGSEGV) when the loop comes back around and the `try` throws again. The
first iteration completes correctly — including the suspension and resume —
so the failure is in re-entering the try/catch region after an async resume,
not in the await itself.

Likely the exception-handler state armed for the `try` is not re-armed (or is
left dangling) across the CPS suspend/resume that the `await` in the handler
introduces, so the second `throw` unwinds into a stale landing pad.

## Minimal repro

`docs/bugs/repro_await_in_catch_loop.zan`:

```zan
using System;

class Program {
    static void Boom() { throw new Exception("x"); }

    static async int Run() {
        int i = 0;
        while (i < 3) {
            try {
                Boom();
            } catch (Exception e) {
                Console.WriteLine("caught " + Convert.ToString(i));
                i = i + 1;
                await Task.Delay(10);
                Console.WriteLine("  after delay " + Convert.ToString(i));
            }
        }
        return i;
    }

    static async void Main() {
        int v = await Run();
        Console.WriteLine("v=" + Convert.ToString(v));
    }
}
```

Observed:

```
caught 0
  after delay 1
<SIGSEGV>
```

Expected: three `caught`/`after delay` pairs, then `v=3`.

Without the `await` in the handler the same loop runs to completion, and the
same `await` outside the `catch` is fine — only the combination fails.

## Impact

The natural shape for "retry with async backoff" — catch the failure, wait,
retry — is unavailable. Found while implementing rate-limit retry in
`stdlib/Sdk/Jd/JdClient.zan`; the SDK now works around it by hoisting the
retry decision out of the handler into a plain flag tested after the `try`
region ends.
