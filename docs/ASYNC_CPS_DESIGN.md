# Async/await → CPS state-machine lowering (design)

## Goal and current status
Zan lowers `async` functions into resumable **CPS state machines** driven by a
cooperative scheduler. `await` suspends the current frame, yields to other tasks
or the IO reactor, and resumes at a numbered state.

This design is implemented. The compiler emits a ramp and `$resume` function for
each async body, emits the single-threaded scheduler inline by default, and can
link the multi-worker driver from `rt_io.c` when `--async-workers` is selected.
Socket/file readiness uses the `zan_io_*_co` bridge to requeue frames. The sections
below describe the current lowering; the staging section is retained as completed
implementation history.

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
    i64   result         ; return value, encoded (see "Result slot" below)
    void(i8*)* cleanup ; releases owned slots and frees the frame
    i32   hcount       ; active try handlers recorded across suspension
    void(i8*)* self_step
    i8*   exc          ; uncaught exception value
    i8*   exc_tid      ; dynamic exception type descriptor
    i32   exc_owned    ; whether exc carries a +1 reference
    i32   cancel
    i8*   child
    i8*   lnext
    i32[] hstack       ; active handler ids, innermost last
    ; per-try catch exception value/ownership/type arrays
    ; per-finally-depth exception value/ownership/type arrays
    ; --- captured params ---
    <param0>, <param1>, ...
    ; --- captured named locals (live across any await) ---
    <local0>, <local1>, ...
    ; --- per-await sub-task handles ---
    i8*   sub0, i8* sub1, ...
}
```
The first 14 fields form a fixed **header** at known offsets (`%zan.co.header`) so
runtime helpers, the await protocol, exception propagation and cancellation can
touch any frame without knowing which function it belongs to. Handler and
catch/finally arrays follow the fixed header and are sized from the async body's
try/finally structure; captured parameters and locals follow them.

## Result slot
The slot is physically 64 bits for every async function -- it sits at a fixed
header offset, so its width cannot vary per function -- but it does not hold an
`i64` *value*. The completing frame encodes its return value according to the
function's **declared** return type, and the await site decodes it with the same
type:

| declared type | store | load |
|---|---|---|
| signed integer / bool | sign-extend to i64 | truncate |
| unsigned integer | zero-extend to i64 | truncate |
| pointer (class, string, array) | `ptrtoint` | `inttoptr` |
| `double` | `bitcast` to i64 | `bitcast` back |
| `float` | `bitcast` to i32, zero-extend | truncate, `bitcast` back |

A plain sign-extend (what this used to do) loses an unsigned value's high bit
and reads a `float` at the wrong width, and it silently truncates `int` once
`int` is 32 bits wide. The return expression is converted to the declared return
type *before* it is encoded, so `async double F() { return 3; }` puts a double in
the slot rather than the integer bits.

## Exception propagation across suspension
The synchronous exception implementation uses `setjmp`/`longjmp`, but a coroutine
must never jump into an earlier `$resume` invocation after that invocation has
returned to the scheduler. The async lowering preserves this invariant as follows:

1. Every `$resume` invocation arms an outer trampoline plus the try handlers
   recorded in `frame.hcount` / `frame.hstack` before dispatching on `state`.
2. Before every suspension or completion, `emit_async_eh_unarm` restores the
   global handler stack to the invocation's entry depth. No dead stack handler
   remains reachable while the frame is parked.
3. On resume, the prologue re-arms the handlers for the try regions that span the
   suspension. A throw then lands in a handler belonging to the current, live
   `$resume` invocation.
4. An exception escaping the async body is moved into `frame.exc`, `exc_tid` and
   `exc_owned`; the coroutine completes and wakes its awaiter. At the await resume
   point, the awaiter moves that exception back into its own EH globals and
   rethrows it inside its live invocation.
5. Exceptions being handled by `catch` and exceptions crossing `finally` are
   stored in per-region frame arrays. This preserves the original exception,
   dynamic type and ownership across an `await`, including bare `throw;`.

Owned locals remain frame-resident across suspension and are released on normal,
exceptional or cancelled completion. This is a compensation layer over the
current setjmp/longjmp EH. Moving to LLVM `invoke`/landingpad/personality remains
a future performance and simplification project, not a correctness prerequisite
for the implemented async state machine.

## Cancellation
Cancellation is cooperative and lives entirely in the generated program (the
driver is compiler-emitted, so there is nothing for the runtime to own):

- `frame.cancel` (i32) is set to 1 by `__zan_co_cancel`.
- `frame.child` (i8*) is the sub-frame the coroutine is currently suspended on;
  it is written at the await site and cleared at the top of every resume.
- `__zan_co_cancel(handle)` marks the target and then walks the `child` chain, so
  cancelling a coroutine also cancels whatever it is waiting on.
- `frame.lnext` links every *detached* frame (`Task.Spawn`) into the internal
  `__zan_co_live` list. A spawn handle outlives its coroutine, so `Task.Cancel`
  looks the handle up in that list first and does nothing if the reaper has
  already freed the frame; the `child` chain below a live frame is alive by
  construction, since a suspended awaiter owns its sub-frame. `__zan_co_reap`
  unlinks a frame before freeing it.

A cancelled coroutine is not killed asynchronously. The body tests the flag at
body start (so a coroutine cancelled before it is ever stepped never runs) and at
each statement boundary after an await; if set, it jumps to the normal completion
path, so owning locals are released, the awaiter is woken and the frame is freed
exactly once. The check is skipped inside `finally`/`catch` cleanup regions, so
cancellation can never bypass cleanup. Being cooperative costs one thing: a
coroutine parked on a timer or a socket observes cancellation only once that wait
completes.

Surface API: `Task.Spawn(asyncCall)` returns the task handle (discarding it stays
fire-and-forget), `Task.Cancel(handle)` requests cancellation, and
`Task.IsCancellationRequested()` reads the enclosing coroutine's flag (0 outside
one) for a body that wants to bail out at a point of its own choosing. There is
no `OperationCanceledException`: a cancelled coroutine completes, it does not
throw.

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
The current lowering always schedules the sub-task and suspends the awaiting
frame, even when the sub-task could complete immediately. A `sub.done` fast path
would be a valid future optimisation, but is not part of the current protocol.

## Runtime and scheduler
The compiler-facing scheduler ABI is:
```
void zan_co_sched_init(void);
void zan_co_ready(void *frame, void (*step)(void*));
void zan_co_sched_run(void);
```
The default single-threaded implementation is emitted into the generated module.
When `--async-workers` is used, `zanrt_io_mt` supplies the same ABI with an OS
worker pool. IO await operations register a one-shot watcher through `rt_io`; when
it completes, the reactor calls `zan_co_ready(frame, step)`.

The ready queue and IO reactor are driven together: the scheduler drains ready
frames, pumps IO while watchers remain, and stops when no runnable or pending IO
work is left.

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

## Implementation history
The original staged plan has landed:
- **S1 runtime — complete**: cooperative ready queue and scheduler ABI.
- **S2 irgen — complete**: ramp + `$resume` skeleton and heap frame.
- **S3 await — complete**: state splitting, suspension and resumption.
- **S4 control flow / IO — complete**: multiple awaits, conditionals, loops,
  nested async calls and runtime IO bridges.
- **EH / ARC / cancellation follow-up — complete for current semantics**:
  try/catch/finally across suspension, cross-coroutine exception propagation,
  frame-resident ownership cleanup and cooperative cancellation.

Async generic-method monomorphization is **complete** (A32-3b, 2026-08-04):
the specialized frame, ramp and `$resume` are cloned together, covered by
`tests/conformance/async_generic_method.zan`.

## Test strategy
- Runnable conformance cases cover compute and chained awaits, loop/foreach and
  control-flow awaits, IO, cancellation, try/catch/finally, rethrow and exceptions
  crossing coroutine boundaries.
- Determinism and leakcheck variants exercise the same programs and lock frame
  ownership behavior.
- Targeted IR inspection remains useful for transform-shape regressions: ramp,
  `$resume`, state dispatch, `zan_co_ready`, frame exception slots and handler
  re-arm blocks.
- Sanitizer runs of the scheduler/reactor remain the native-runtime safety gate.

## Risks / scope notes
- Temporaries that straddle an await (e.g. `f(await a, await b)`): handled by
  emitting awaits left-to-right, spilling each result to a frame slot before the
  next — safe because irgen controls emission order.
- ARC across suspension: retained locals live in the frame; release on frame
  free / at scope end as today. Cycle handling unchanged (out of scope).
- Windows: identical codegen; the `zan_co_*` driver is plain C (no ucontext),
  so it builds on MinGW/MSVC the same as POSIX. IO bridge uses the existing
  per-platform `rt_io` reactor.
