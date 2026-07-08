# Async/await → CPS state-machine lowering (design)

## Goal
Compile `async` functions into real resumable **state machines** driven by a
cooperative scheduler, so `await` genuinely suspends (yielding to other tasks /
the IO reactor) instead of the current behaviour:

- today `checker` ignores `async`; `irgen` compiles an `async` function as an
  ordinary synchronous function and `await e` just evaluates `e` (a busy-wait
  poll loop on a compiler-invented `Task{completed,result,handle}` struct that
  is never populated by any scheduler). The real coroutine runtime
  (`zan_spawn`/`zan_task_await` in `rt_sched.c`) is never called by generated code.

## Why an explicit state machine (not `llvm.coro.*`)
Two findings from the codebase drove this:

1. **irgen keeps every local/param in a stack `alloca`** (Pass B in
   `emit_user_methods`), and **no mem2reg / optimization pass runs** before
   object emission. So local *values already live in memory*, not SSA registers.
   Relocating those allocas into a heap **frame** makes them survive across a
   suspension for free — the hard part of a coroutine transform (capturing live
   state) is already solved by the existing codegen shape.
2. The distro LLVM (Ubuntu `llvm-14`) is a **Release build with assertions
   off**. Hand-emitting `llvm.coro.id/begin/save/suspend/end` through the C API
   is easy to get subtly wrong, and a malformed coro IR would *silently
   miscompile* instead of tripping an assert. An explicit, hand-built state
   machine is fully inspectable via `--emit-ir` and verifiable by running.

This is still a genuine **CPS / state-machine** transform (à la C#/Rust async),
just materialised by us rather than by `CoroSplit`.

## Frame layout (compiler-emitted, per async function `C.M`)
```
%C_M$frame = type {
    i32   state          ; 0 = start; k = "resume after await #k"; -1 = done
    i32   done           ; 1 once the result slot is valid
    i8*   awaiter         ; frame waiting on this one (or null)
    void(i8*)* awaiter_step ; awaiter's resume fn (or null)
    i64   result         ; return value (all scalars are i64 in this backend)
    ; --- captured params ---
    <param0>, <param1>, ...
    ; --- captured named locals (live across any await) ---
    <local0>, <local1>, ...
    ; --- per-await sub-task handles ---
    i8*   sub0, i8* sub1, ...
}
```
The first 5 fields are a fixed **header** at known offsets so runtime helpers
and the await protocol can touch them.

## Two emitted functions per async `C.M`
1. **Ramp** `C_M(params) -> i8*` (unchanged external signature except the return
   becomes an `i8*` task handle):
   - `malloc(sizeof(frame))`, store params into the frame, `state=0`, `done=0`,
     `awaiter=null`.
   - return the frame pointer (the **Task**). Body does **not** run yet (lazy
     start; the scheduler/awaiter drives it).
2. **Resume** `C_M$resume(i8* frame)`:
   - entry `switch(frame.state)` → `case 0: body-start`, `case k: after-await-k`.
   - the original body is emitted here, but every local/param access is a GEP
     into `frame` instead of a stack alloca.
   - at each `await`: (see protocol) set `state=k`, `ret void` (suspend).
   - at function return: store into `frame.result`, `done=1`, then if
     `frame.awaiter` is set, `zan_co_ready(awaiter, awaiter_step)`; `ret void`.

## await protocol (emitted inline; offsets known to compiler)
For `x = await Sub(args)` inside async `Self`:
```
sub  = Sub(args)                 ; ramp: makes sub frame (a task handle)
sub.awaiter      = selfframe
sub.awaiter_step = Self$resume
zan_co_ready(sub, Sub$resume)    ; schedule the sub to run
selfframe.state  = k
ret void                          ; SUSPEND self
resume_k:                         ; scheduler re-enters Self$resume, switch → here
x = sub.result                    ; sub is guaranteed done before we're re-scheduled
```
Fast path: if `sub.done` is already 1 right after the ramp (e.g. a sub that
never awaits), we may fall through without suspending (optional optimisation;
initial version always suspends for uniformity/correctness).

## Runtime (Stage 1, `rt_sched.c`/new `rt_co.c`)
Minimal cooperative driver for stackless frames, independent of the existing
ucontext fiber scheduler:
```
void zan_co_sched_init(void);
void zan_co_ready(void *frame, void (*step)(void*)); ; enqueue (frame,step)
void zan_co_sched_run(void);   ; pop & step until ready-queue AND IO reactor empty
```
IO await bridges to the existing `rt_io` reactor: a one-shot registration
`when fd ready → zan_co_ready(frame, step)` (added when we wire `await` on
sockets; not needed for the compute-only first slice).

## Entry / driving
If `Main` (or any root) awaits, `main()` becomes:
```
zan_co_sched_init();
f = Main();                  ; make root frame
zan_co_ready(f, Main$resume);
zan_co_sched_run();          ; pump to completion
; (read f.result if Main returns a value)
```
Non-async callers that `await` an async call are handled the same way at the
nearest async boundary; a synchronous function that calls an async one without
awaiting just receives the task handle.

## Staging (each lands green on CI)
- **S1 runtime**: `zan_co_*` driver + unit test (spawn N compute frames, run,
  assert results). No compiler changes.
- **S2 irgen**: detect `MOD_ASYNC`; emit ramp + resume skeleton with frame;
  no awaits yet (async fn with a plain `return` → task whose result is set).
- **S3 await**: state splitting + await protocol; `await` suspends/resumes.
  End-to-end: `async_test.zan` prints `42` via the scheduler (verified by IR
  showing ramp/resume/switch + a real run).
- **S4**: broaden — multiple awaits, await in `if`/loop, async-calls-async,
  `await` on runtime IO (bridge to `rt_io`); golden-IR cases + runnable e2e.

## Test strategy
- Golden IR (`--emit-ir`, "must-contain" symbols): `C_M$frame`, `C_M$resume`,
  `switch`, `zan_co_ready`, `state` store — locks the transform shape
  cross-platform without pinning exact LLVM text.
- Runnable e2e: compile+run async programs, assert stdout (compute, chained
  awaits, loop-with-await, async IO echo).
- Sanitizers: run the e2e + `zan_co_*` unit under ASan/UBSan (existing job).

## Risks / scope notes
- Temporaries that straddle an await (e.g. `f(await a, await b)`): handled by
  emitting awaits left-to-right, spilling each result to a frame slot before the
  next — safe because irgen controls emission order.
- ARC across suspension: retained locals live in the frame; release on frame
  free / at scope end as today. Cycle handling unchanged (out of scope).
- Windows: identical codegen; the `zan_co_*` driver is plain C (no ucontext),
  so it builds on MinGW/MSVC the same as POSIX. IO bridge uses the existing
  per-platform `rt_io` reactor.
