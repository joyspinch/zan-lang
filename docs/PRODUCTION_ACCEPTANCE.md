# Production acceptance

This document separates supported behavior from evidence that a particular build
passed acceptance. A test definition is not a passing test result. Demo execution
has been reported on Windows, macOS, Linux, Android and HarmonyOS; it establishes
application execution on those devices, not coverage of every failure path.

## Compiler contract

- Closed generic classes are invariant. A conversion between different type
  arguments must be rejected in initializers, assignment, arguments and returns.
- A virtual slot is identified by its parameter types, generic arity and ref/out
  modes. An overload with the same argument count cannot replace that slot.
- Parsing consumes the complete input and diagnoses unmatched top-level braces.
- Optimizer, verifier and object-emission failures stop compilation with a nonzero
  status. Sharded compilation cannot turn such a failure into successful fallback.
- CPU requirements belong to the deployment contract. The default non-Android
  x86-64 output enables AVX2/FMA/BMI/AES. Explicit `--cpu-profile baseline` selects
  SSE2 and rejects used hardware intrinsics without a supported fallback;
  `--cpu-profile avx2` selects the extended profile. Do not infer generic x86-64
  compatibility from the architecture name. Android x86-64 retains its separate
  default ABI baseline. The option controls generated code; native dependencies
  also need a compatible CPU baseline.

Regression anchors: `case_host_virtual_overload_slots`,
`diag_host_generic_invariant_return`, `diag_generic_invariant_arg`,
`diag_generic_invariant_assign`, and `compiler_failure_protocol_*`.

## Runtime ownership contract

For a submitted Windows overlapped receive, the operation record and borrowed
buffer remain alive until its completion packet has been consumed. Requesting
cancellation does not transfer buffer ownership. Timeout and completion arbitration
select one final result; only that completion path publishes the result and resumes
the caller. `ERROR_NOT_FOUND` from cancellation does not prove that a queued
completion has already been consumed.

A failed allocator page recommit must leave the allocation cursor and allocation
accounting unchanged and return failure. It must not return inaccessible storage.
Native threads created by the coroutine pool release their own EH state on exit;
a caller entering foreground scheduling retains its existing EH state.

Regression anchors: `runtime_iocp_timeout`, `runtime_mem_recommit`,
`runtime_worker_eh`. Deterministic tests cover ownership transitions; real socket
traffic, race instrumentation and long-duration runs are additional acceptance
requirements, not substitutes for these tests.

ARC does not automatically collect arbitrary strong reference cycles. Applications
must break cycles with weak edges or explicit lifecycle operations. Foreign pointers
remain borrowed unless their API explicitly transfers ownership.

## Self-host replacement contract

Fixed-point IR equality establishes self-consistency. Replacement acceptance also
requires host/self-host behavioral differential tests and representative consumer
programs built with the shipped runtime. An explicitly unsupported operation must
be rejected instead of silently using a different platform or numeric semantics.

Decimal arithmetic and closure lifetime equivalence remain separate acceptance
items until implementation and runtime regressions demonstrate equivalence. Do not
advertise production replacement based only on a fixed-point result.

The fourth ownership batch preserves the bit0 tagged `{fn, env}` call ABI and
introduces ARC-owned records, environments and captured cells. Replacement
acceptance remains incomplete: synchronous cross-frame exception cleanup still
needs a throw-before-longjmp unwind stack, including temporary and cell owners;
array element destruction and dynamic `object` ownership need a common reference
descriptor. Arrays currently have a 16-byte header while classes have 24 bytes,
so treating every non-string object as a class is unsafe. The array migration
must cover single- and multidimensional allocation, List.ToArray, string-to-byte
bridges, static arrays and immortal reference counts. Raw native function pointers
stored as `object` require an explicit record/thunk conversion; reading a function
pointer's preceding bytes as an ARC header is invalid. These paths are not
certified by the basic escaping-closure fixture or by gen1 compilation.
Task.Run action-frame changes are under validation: the delegate moves from slot
10 (the cancellation flag) to a dedicated slot 13, with retention before publication
and clear-then-release cleanup. Normal, cancelled and exceptional paths must all
release that owner. The current self-host still retains completed action frames
rather than implementing a safe handle-reclamation protocol; fixing delegate
ownership alone does not close that frame-allocation lifetime. The runtime live
address set does not pin a frame after lookup, and delayed reuse does not prevent
an old handle from naming a new task at the same address. Reclamation therefore
requires a shared handle registry with generation validation and query/scheduler
ownership, covering host and self-host await, join, cancellation and result paths.
The existing bare integer handle has no release contract; its metadata lifetime
must be defined before bounded reclamation can be certified.

The exercised host Task.Run method-group paths now pass the targeted acceptance
checks below. Initial source review found an unconsumed closure owner for
bare/static groups, a missing-receiver diagnostic bypass for bare instance methods
in static contexts, and direct thunk calls that skipped virtual override dispatch.
Natural method-group fixtures cover ownership, invalid receiver shapes and virtual
calls; Action-variable or lambda-only probes would not cover these paths. The first repaired
host build succeeded. Seven targeted invalid-signature/receiver diagnostics passed,
but the positive binding fixture initially failed to compile a type-qualified
static method group. The original action-owner fixture also exposed a cancelled
owner leak: cancellation skipped delegate release, and the self-reaper freed the
frame without invoking action cleanup. Binding the type receiver to its actual
symbol and moving clear-before-release to the common completion block, before
publishing DONE, corrected both failures. After rebuilding, both unchanged
fixtures matched expected output for eight consecutive executions each. Formal
CTest validation of both positive cases then passed (3.21 and 3.09 seconds,
6.42 seconds total); the seven earlier negative diagnostics also passed. External action exceptions and frame reclamation
remain independent acceptance gaps. Current worker actions have no exception
handoff to the submitting thread: an uncaught native exception reaches the
installed terminate handler and exits the process. Task.Wait remains completion
only. Await exception slots do not establish an action fault-storage contract;
adding such storage or Wait rethrow would change public semantics. Any exception
cleanup must preserve the distinction between worker fail-fast and an existing
same-thread handler reached through foreground scheduling.

Two isolated self-host action exception probes subsequently passed using the same
gen1 as the nullable-decimal probe. Fresh-exception output was `callback`,
`outer-caught`, `after`; borrowed-exception output was `borrowed-action`, `true`.
Emission, linking and execution each exited zero. Generated IR contained the
112-byte frame, dedicated owner slot, clear-before-release helper and exception
propagation. These probes establish exception propagation for their Action/lambda
shapes only. The borrowed probe still holds a receiver locally, and the fresh
exception's additional retain has no proven balanced transfer; neither probe
certifies receiver destruction, exception leak freedom or frame reclamation.

## Development validation record (2026-11-10)

Windows targeted validation passed the 17 first-batch regressions listed above
(the IOCP fixture needed a scheduler-initialization correction before its passing
single-test rerun). `compiler_cpu_profile` passed, including execution of the
baseline SIMD program. `selfhost_gen1` and the new driver option/warning checks
passed after removing a duplicate scheduler declaration from generated IR.
The synthetic release-gate regression exercised success, a missing bootstrap
registration, a failing test and a disabled test. These are development results;
the real full release gate has not been run for this change.

Linux runtime validation used the mounted main tree under WSL2 Ubuntu x86-64,
Clang 18.1.3, and the shared build gate. ASan exposed a 16-byte random read into
one 8-byte hash-salt output; the fix stages both keys in a contiguous array before
assigning the separate outputs, with a deterministic sentinel regression. TSan
exposed mixed atomic/plain scheduler and timer-counter accesses, then a ring-slot
reuse race in the release-length run. Consistent atomic accesses and a per-worker
deque lock now protect complete task pairs, slot copies and head advancement;
batches remain on the stack and cross-queue publication occurs after unlocking.
After that final deque fix, short TSan (8 rounds × 128 tasks) and release TSan,
ASan/UBSan, and ordinary runs all passed for both 2 and 4 workers. Each release
process completed 1000 rounds × 512 tasks with three producers, 256000 timer
resumes and 256000 self-ready resumes, checking lost/duplicate tasks and frame
reentry. Runs retained a 600-second monotonic watchdog, 2 MiB native stack limit,
and ASan leak detection; no suppression or failure-tolerant step was added.
GCC 13.3 TSan failed before main with an unexpected memory mapping and is not a
passing result. The successful Clang executions above are separate development
evidence, not evidence that the hosted GCC CI gate or full release gate passed.

Fourth-batch Windows validation (2026-11-11) also covers host decimal List
storage and delegate invocation ownership. Decimal elements now occupy two 64-bit
slots, preventing adjacent 16-byte values from overlapping; collection equality
uses numeric decimal comparison, so `1.00m` equals `1m`. The selected delegate
callee is retained before argument evaluation and remains owned through invocation
and argument cleanup; its EH temporary slot releases that owner on exceptional
unwind. Targeted normal/exceptional callee and float/wide-struct regressions passed.

The self-host rectangular-array fix preserves the data pointer for indexing and
length, initializes its allocation refcount, and uses a null-safe, typed rank
adapter only for ARC retain/release and the correct free base, including field
and captured-cell cleanup. Native thread/dispatcher ownership now uses explicit
backend delegate retain/release/invoke hooks: host defaults and self-host strong
hooks preserve their respective record/environment ABIs without code-pointer
heuristics. The 14-case production behavioral differential passed (16.44 seconds),
including decimal-wide (17 true), rectangular decimal ARC (23 true), and decimal
List slots (15 true), with matching host/self-host output. The subsequent eight
focused cases, including shipped-runtime coverage, and fixed-point validation
(61.84 seconds) also passed. IR-golden validation then failed after 1729.07
seconds: zero of 361 cases passed. Of these, 117 lack golden files and the other
244 differ from their existing per-function IR text fingerprints; the run reported
no gen1 emission or normalizer failure. The current `--fnhash` implementation hashes
raw function text rather than invoking the normalizer, so names and formatting can
also change these fingerprints. It does not directly cover module globals, types
or declarations. The fingerprint tool now rejects malformed, duplicate and
unterminated function inputs and invalid UTF-8 before atomically publishing output;
three standalone protocol tests passed without rewriting any golden. Both registered
CTest regressions, `selfhost_ir_golden_preflight` and
`selfhost_ir_fingerprint_protocol`, subsequently passed (2.85 seconds total).
A read-only compatibility scan accepted all 22060 function definitions in the 244
existing-baseline IR files under the stricter parser; no quoted function-name
headers occurred in that sample.
The earlier IR-golden test wrapper propagated exit status 8. These
are acceptance failures: baseline differences still require review, and missing
baselines must be supplied with evidence rather than skipped. No goldens were
rewritten by this validation. Read-only classification of existing baselines found
8 cases with runtime/entry changes only, 62 with additional library changes but no
user/closure function changes, and 174 with user/closure text changes. Missing
baselines comprise 100 cases already listed at the starting HEAD and 17 added by
pre-existing worktree changes; none of those 117 were added by this batch.

The earlier fourth-batch results did not cover wide nullable-decimal behavior,
tuple/erased-field lifetime, dynamic `object`/array descriptors or synchronous
cross-frame EH cleanup. The subsequent nullable-decimal evidence is recorded below;
the remaining ownership and exception boundaries are still open.
A subsequent Windows host probe passed all 60 nullable-decimal assertions after
fixing decimal value-type classification, nullable result inference, lifted pointer
ABI arithmetic/comparisons and unary/increment/decrement lowering. The unchanged
fixture previously failed with 30 type-checking diagnostics. The registered host
regression `case_selfhost_nullable_decimal_lifted` subsequently passed in 2.93
seconds (3.03 seconds total CTest time). The five adjacent regressions
`case_decimal_arithmetic`, `case_decimal_divide_by_zero`,
`case_decimal_implicit_int`, `case_incdec_ops` and `case_nullable_value_types`
also passed, taking 16.07 seconds total. A subsequent self-host probe completed
gen1 compilation, IR emission, native linking and execution with exit status zero
at every stage. Its output exactly matched the host golden after newline
normalization, including all 60 `true` lines. The gen1 executable was generated
at `2026-11-10T17:33:02Z`; runtime-object timestamps and SHA256 hashes were recorded.
This establishes the exercised lifted-decimal behavior, independently of the
allocation-lifetime limitations below.
Nullable boxes currently use unowned allocations in the self-host. Closing that
lifetime requires coordinated temporary, assignment, return, capture, async,
container and array-element ownership; changing allocation or `ArcManaged` alone
would introduce dangling references. The preceding array ownership step now covers
the exercised typed class/delegate producers and last-release element destruction,
including `ToArray` element retention. Its shared element descriptor covers
one-dimensional and rectangular arrays, collection conversion, byte bridges and
immortal static arrays. Nullable boxes still need their own balanced owner protocol
across these consumers before they can safely become owned array elements.
Lifted arithmetic correctness does not certify these allocations as leak-free.

The array ownership implementation is now under validation: a shared descriptor
and allocator cover dynamic one-dimensional/rectangular arrays, ToArray, byte
bridges and immortal static storage, with typed last-release element destruction.
Three new ownership fixtures provide 37 checks. Their initial host compilation
exposed missing named-type rectangular-rank declaration lookahead; after fixing
the three named-type lookahead paths and rebuilding, all three fixtures executed
and exactly matched their existing expected output. Their registered host CTest
cases also passed (3.99, 4.11 and 4.14 seconds; 12.37 seconds total). The same host probe passed
all eight selected array, Task and decimal cases and recorded source/golden and
compiler hashes. Self-host results for the three typed fixtures are recorded below.
Allocator failure guards are implemented, and independent review found
the typed rank/stride/owner layout consistent. That review also confirmed an
open erased-element defect: object/interface arrays do not retain a stored class
payload and can leave a dangling element after its external owner is released.
These arrays require a dynamic owner representation or explicit rejection before
production acceptance; treating them as ordinary class pointers would mis-handle
array, delegate and string ownership ABIs. The current eight-case probe validates
the selected typed subset only. Its first execution built gen1 successfully and
passed the class-array ownership fixture. The delegate-array fixture emitted,
linked and exited zero but failed four lifetime checks (`one-replace`, `one-dtor`,
`rect-replace`, `rect-dtor`); remaining cases were not executed after that mismatch.
The mismatch was traced to short-circuit Boolean lowering discarding a private
temporary-owner list after retaining delegate callees. Cleaning each evaluated
operand's temporaries before its branch/phi predecessor fixes that leak without
changing the array fixtures. A rebuilt gen1 subsequently matched all three array fixtures exactly, including
the producer/ToArray/byte-bridge fixture. The independent short-circuit callee
fixture also matched ten lines from an actual host run using that same gen1.
The combined probe then stopped at Task.Run method-group checking with E2039
(method is not a value). An independent probe using that same gen1 subsequently
passed the nullable, rectangular-array and List decimal cases (60, 23 and 15 output
lines), each with successful emit/link/run and exact measured host output. It also
passed nine of ten erased-array rejection checks. The interface-array cast case
exited normally with a parser E0045 instead of the required ownership diagnostic;
this is a failure, not a successful rejection. The gen1 SHA256 was
`5cb080d48e6a98b0de439bfcb0bf890084f9c94be9a42af66f4773d6208c76d3`
(timestamp `2026-11-10T17:55:53Z`). These results predate the subsequent checker
and parser changes.

The checker now binds Task Action method groups to a selected MethodDecl, and
IR generation consumes that exact binding, locates its owner by node identity,
and supports instance-thunk virtual dispatch. Named-array cast lookahead has also
been repaired. The fresh gen1 compiled successfully (SHA256
`8a661e0c9f36897ec5eb05b190692417510ce8276c285804c05b20defc539e04`)
and passed `task_action_owner` through emit/link/run with exact host output.
`task_method_group_binding` then failed during emission with E3053 at
`Task.Run(TaskMethodReceiver.Make().Work)`; the combined probe stopped there.
The temporary-receiver method-group path remains under repair. An independent
probe using this same gen1 passed all ten erased-array rejections, requiring an
ordinary nonzero exit, the pinned ownership diagnostic and no generated IR.
The positive named-array cast regression failed earlier, in actual host compilation:
`(NamedArrayCastPayload[,])rect` produced parser E0045/E0032. Its self-host stages
were not run. The fixture is unchanged and the host parser defect is under repair.
A later rebuilt gen1 (SHA256
`b6d16246dea9b04edbd7c7bbbc778788501c4c8738ac70ab8d9c4be2b4ee103a`)
passed all nine behavioral cases (the original eight plus short-circuit ownership)
and all ten erased-array rejections. The named-array cast fixture also passed
actual rebuilt-host compilation/run and same-gen1 emit/link/run with exact output.
The host parser now recognizes rectangular-rank named casts through the shared
rank scanner.

Access checking now consumes the selected MethodDecl, and the builtin Task entry
consumes a checker-set callee marker. The new local/field-shadow fixture passed
on the host but failed self-host emission with unsupported user Task.Run/Spawn
calls; the ordinary call-dispatch path remains under repair. Cross-module access
fixtures initially used two namespace declarations in one compilation unit, which
violates the documented grammar. After correction to separate source files with
explicit helper inputs, selected-public passed both compilers with output `17`;
selected-internal passed both pinned diagnostic checks with ordinary nonzero exits
and no IR/executable. The public helper now lives under `conformance/helpers` and
is explicitly supplied by the production driver and plain/det CMake registration.

The production list now includes typed-array ownership, short-circuit ownership,
named-array casts and selected-public binding. Its rejection driver enforces
compiler timeouts, ordinary exit codes 1–125 and absent rejected-program outputs;
20 synthetic rejection checks and helper-forwarding checks passed. The updated
formal production CTest has not yet run.

Two further nullable-array layout probes found open defects. The host passed the
17-line int nullable-array rank/alias/return fixture, while the measured gen1
emitted and linked it but crashed at runtime with heap-corruption status
`0xc0000374`. Review found nullable-array types entering the scalar-box mapping
and nullable byte/sbyte elements receiving a one-byte rather than pointer-sized
stride. The 40-line byte/sbyte fixture also exposed a host sign-extension defect:
nullable sbyte values `-113` and `-7` printed as `143` and `249`. It is not a valid
host baseline yet. Both compiler defects are under repair; expected output is
unchanged and nullable element ARC remains disabled.

The subsequent host rebuild passed, and gen1 SHA256
`9e38fffbbeb88bb73f4a702a3a5995ba0c8e216263491f4cffc4cfee458585da`
passed exact host comparisons for Task shadowing, nullable-array rank (17 lines)
and nullable byte/sbyte stride (40 lines). The scalar nullable predicate now excludes
array ranks, nullable elements use pointer-sized slots, and host Console output
sign-extends semantic sbyte values. The production list grew to 24 behavioral cases.
Direct execution of the formal production driver passed 23/24 behavioral cases
and all 10 erased-array rejections; the independent internal check passed on both
compilers. `closure_capture_async` emitted and linked successfully but exited
`3221226505` (`0xc0000409`) without output. This is an unresolved failure, not a
passing production gate. Evidence is retained under
`_scratch/production-array/closeout-20261110T182114162`.

Six registered host regressions passed in one targeted CTest invocation (7.27 seconds):
named-array cast, nullable-array rank, nullable byte-array stride, selected-public
Task binding, Task shadowing and short-circuit callee ownership. This establishes
the host regressions and the exercised self-host differences; it does not certify
nullable box leak freedom or close the asynchronous closure failure.

Read-only IR review of the passing nullable fixtures confirmed all six Echo
functions use array `i64*` parameters/returns, the exercised allocation descriptors
use stride8/kind0, and local stores retain arrays before releasing creation
temporaries. Nullable payloads remain unowned `i8*` boxes.

Review of the failing asynchronous closure IR found captured cells allocated with
bare `malloc(8)` and subsequently passed to standard ARC retain/release, which reads
an absent object header. Async variable declarations also omitted target-delegate
lambda annotation, generating pointer-returning lambdas for `Func<int>`. The async
variable-declaration path now uses `OwnedCell` and `AnnotateLambda`, matching the
existing ordinary-method contracts. A fresh-gen1 focused probe then passed all
five selected cases: `closure_capture_async`, `closure_capture_by_ref`,
`closure_capture_object`, `closure_captures_this` and `async_delegate`. Every
emit/link/run stage exited zero and output exactly matched existing host goldens.
The gen1 SHA256 was
`f224bc6d6e70963fd7405f6512a0a0f8cdc861245503ba45226e441c7a6e239b`
(timestamp `2026-11-10T18:25:19Z`); evidence is retained under
`_scratch/production-array/closure-async-closeout-20261110T182512186`.
The former asynchronous closure failure is resolved in this focused execution.
The full 24-case production driver was not rerun after this fix, and no new
fixed-point or full release result is claimed.
A subsequent direct invocation of the formal `tests/run_fixedpoint.cmake`
completed successfully (exit zero; this was not a CTest invocation). Generation 2
and generation 3 IR were byte-identical: 28,474,342 bytes each, SHA256
`b02b35372deb80cf02200f716b554341f0d71aeaa6dc387601a2540d99e64e75`.
The generation 1 executable SHA256 was
`1b6e49ea41a4818393c6073266b191a1e916478fbe31dab78085b63e89fd4770`;
generation 2 executable SHA256 was
`9d82bb33450c6e2bd243494360fa263608cd42eaa0aa16fb9c64122b6495fa2f`.
Evidence is retained under
`_scratch/production-array/fixedpoint-checkpoint-20261110T184252495`, including
source, toolchain and the formal fixed-point `zanrt_sh_*` runtime identities.
This establishes self-consistency for that input checkpoint. The same generation 1
then passed direct execution of the formal production differential driver:
24 behavioral cases passed, all 10 erased-array cases were rejected with their
required diagnostics, and zero failures were reported (exit zero). The recorded
source and host inputs remained unchanged during this run. This closes the earlier
23/24 behavioral checkpoint after the asynchronous closure fix. The fixed-point
and production runs record their separate runtime object sets; neither result is
a full release gate, and subsequent source changes require separate validation.

The next ownership fixture, `selfhost_async_pending_result_owner`, reproduced a
separate defect using that same preserved generation 1. A class-valued return is
retained into frame slot 6 before running a suspending finally. If that finally
throws to the coroutine trampoline or cancels its next resume, terminal completion
replaces slot 6 with zero without releasing the pending result. Both lifetime
checks incorrectly reported a live payload after its external owner was cleared.
The normal return across Yield and the finally-internal catch control paths passed.
The baseline evidence is retained under `_scratch/async-pending-owner/baseline`.
A typed load-clear-release helper now handles abandoned result owners at exception,
cancellation, cleanup and replacement exits, preserving normal result transfer.
A single rebuilt generation 1 (SHA256
`c7614f9ab367a98983898b7b5e05ecd0a3d8ad07fbafec11145c49e9af7f0454`)
passed all four fixture paths and three neighboring fixtures (`closure_capture_async`,
`async_delegate`, `selfhost_array_class_owner`). The host ordinary probe also passed.
However, the subsequently registered host CTest failed its ARC integrity mode:
after the exception path passed, cancellation reported release through an already
freed reference. The targeted invocation exited 8; evidence is in
`build/test-out/targeted-20261111-025030.log`. At that checkpoint this was an open
host ownership defect, not a passing regression; its later resolution is below. The separate cleanup callback and return-replacement
paths have generation/link coverage but no independent runtime fixture yet.
These changes and this failure are outside the preceding 24-case acceptance set.
The host defect was then traced to orphan result cleanup reading the stale typed
RVAL cache after cancellation had discarded the pending owner. Cleanup now clears
RVAL and uses the completed RESULT to determine whether a result was delivered;
normal pointer results are decoded from RESULT, and typed aggregate cleanup runs
only for an actual delivered result. The unchanged new fixture and existing
`async_spawn_result_release` both passed direct `--arc-guard --check-leaks`
execution. Their two registered host CTests subsequently passed together (4.79
seconds total), with output archived in
`build/test-out/targeted-20261111-025356.log`. The rebuilt host executable SHA256 was
`00e4c961b409c8901eabcd317f4a94dab19d4398e20f45fae63b5c9252e0df71`.
This resolves the recorded stale-release failure for the exercised paths. The
latest host/self-host ownership edits were subsequently checked by one direct
formal fixed-point run and a production-driver run reusing its generation 1.
Both exited zero: gen2/gen3 IR were byte-identical at 28,476,832 bytes, SHA256
`45b028f07a5f67391cd7cf706a31c0e265615b01d0b2d65e61d9defff7382700`;
the driver reported 25 passed, 0 known gaps, 10 rejected and 0 failures.
Generation 1 SHA256 was
`bb199c5905d46a7b37ae49975c7645682181e3b28b004ae71b92d27382dca572`.
Source hashes and host/runtime/toolchain identities remained unchanged during the
runs. Evidence is retained under
`_scratch/production-array/latest-fixedpoint-20261110T191220787`.
These are direct formal-script results, not CTest or full release-gate results;
subsequent synchronous ownership changes are outside this checkpoint.

The next synchronous fixture, `selfhost_sync_pending_return_owner`, has a recorded
failing dual-compiler baseline. With the preserved generation 1 above, self-host
emission, linking and execution exited zero, but five ownership checks failed:
direct throw, called throw, replacement of the pending return, nested abort and
same-function catch boundary. Normal finally-internal catch and mutation controls
passed. The host compiled the same source but exited 1: it also failed return-owner
checks, repeated an inner finally on a nested exceptional path, and reported an
unhandled exception at the catch boundary. Evidence is retained under
`_scratch/pending-return/baseline`. These are open defects under repair; the fixture
is outside the preceding 25-case production result.

The synchronous pending-return repair subsequently passed eight focused self-host
cases using one newly built generation 1. The two new fixtures matched all 23 and
8 expected lines, covering class, delegate and typed-array cancellation of pending
returns, finally-internal catch preservation, return replacement, mutation and
nested handler boundaries. Pending records are synchronous entry-stack records;
each handler saves a chain watermark, and raising unwinds typed owners before
longjmp. Successful return unlinks its record without releasing the transferred
reference. Lambda and task-action generation isolate this compile-time state.
The implementation reuses typed payload destructors rather than treating every
pointer as an ordinary class. It does not register general local owners or enable
nullable ARC.

The host repair registers the pending return as an internal owned local and switches
to the enclosing EH context while executing a departing finally. A nearby
finally-only landing pad also needed a catch clause when an enclosing catch exists
in the same function, so native exception search can discover that catch. The
nearby failure had no original-host baseline and is not attributed to this change.
Host probes passed with ARC integrity and leak checks; the two new cases plus
`exception_finally_paths` and `exception_finally_managed_owner` then passed their
four registered CTests in 4.90 seconds total. The log is
`build/test-out/targeted-20261111-032824.log`; focused self-host evidence is under
`_scratch/pending-return/focused`. The host identity remained unchanged during the
self-host run, SHA256
`186b6d63aa3aee4cc6e14abe00f3711ed09d5a7fbf72509cbb579b4ea539eb7e`.
A subsequent direct invocation of the formal fixed-point script on the synchronous
repair passed: gen2 and gen3 IR were byte-identical at 28,793,582 bytes each,
SHA256 `6d21f3e657180f9a118621d1a996c3b859e0b8dc477544b8ce7fa494c62c0bbb`.
The formal production driver reused its generation 1 executable (SHA256
`1db70767a8a00016c0dc4f0a1d4468d54a9b414bbfeb6724fe4253d95f3681f6`): 27 behavior
cases passed, all 10 negative cases produced their required rejections, and no
known gaps or failures were reported. Both new synchronous cases passed. The
3,407 recorded source inputs and 18 host/toolchain/runtime identities remained
stable. Evidence is under
`_scratch/production-array/sync-pending-fixedpoint-20261110T235946607`.
These direct runs establish this checkpoint independently of the earlier
25-case checkpoint and are not full release-gate results.

Separate self-host probes reproduced a remaining local-owner defect on that
checkpoint: a class local remained alive after both same-frame and cross-frame
exceptional exits, while the normal exit released it. The `throw local` control
still reached a typed catch with its message intact. The host released the
class locals on both paths, and both compilers passed the throw-local controls.
These probes establish the need for local-slot cleanup and a separate
exception-payload lifetime contract. Probe sources and results are kept under
`_scratch/sync-owner-eh-review`.

The synchronous exception-dispatch prerequisite was repaired separately. An
exception caught inside an inline finally previously replaced the object and type
of the exception already propagating. With the prior gen1 the unchanged fixture
entered the wrong outer handler for direct throw, bare rethrow and unmatched
catch. The repair preserves the original pair through a normally finishing
finally; an exception that escapes finally still replaces it. One rebuilt gen1
(`bd26ae31bbff7b529cc6584e25e6edc0b121eea171a91718cba3caac34962ecf`)
matched the new fixture and both synchronous pending-return regressions. This
changes dispatch state; general local-owner unwind and exception-payload ARC
remain open. The host CTest `case_selfhost_sync_finally_payload` printed the
expected sequence, including the escaping replacement. Leak checking failed:
one object allocated for the superseded exception at fixture line 49 remained
live at exit (1 of 1 cases failed, 4.30 seconds, wrapper exit 8;
`build/test-out/targeted-20261111-084829.log`). This is a host ownership failure,
separate from the repaired self-host dispatch behavior.

The fixed-point run for this edit passed. Generation 2 and 3 IR were
byte-identical at 28,810,268 bytes, with SHA256
`839c1f5513e0e8c14fe2576909648567278d131d571644ec656e3fa5db571a27`.
The production driver reused the same generation 1 compiler: all 28 behavior
cases passed, including `selfhost_sync_finally_payload`, and all 10 negative
cases produced the required rejections, with no known gaps or failures. All
762 recorded input hashes and 62 runtime and tool identities matched before
and after. The generation 1 executable, SHA256
`f1d5d196c3751741e1d195281cfb809e23320eb9a1d0fdc5404af598d657bf09`, was unchanged
through the differential. Evidence is under
`_scratch/production-array/sync-finally-fixedpoint-20261111T004133453`.
This checkpoint does not close the host ARC leak or the self-host payload
ownership and local unwind work.

Five unchanged exception-lifetime probes were then run against this checkpoint's
gen1 and host. All ten compile/link/run combinations exited zero, but weak
references exposed four self-host lifetime failures: a thrown local died after
a named catch while its enclosing scope was still live; fresh exceptions
survived unnamed typed and catch-all handlers; and an exception replaced by
finally remained alive. Nested bare rethrow preserved identity on both
compilers. The host passed the first three probes but also retained the replaced
exception, reporting one leaked object on stdout despite run exit zero.
The host used ARC guard and leak checks; the self-host observations came from
weak references, not equivalent instrumentation. Compiler, runtime, and fixture
hashes remained stable. Raw evidence is under
`_scratch/exception-payload-ownership/run-20261111T085607354`.

The subsequent host repair passed O0 and O1 focused runs of the original
fixture and the new `sync_finally_exception_owner` matrix with ARC guard and
leak checking. Its first formal targeted group passed 11 of 12 cases, including
both fixtures and synchronous pending-return/catch/goto controls. The async
`async_finally_pending_owners` control failed its assertion and subsequently
reported three live allocations. No pre-repair executable comparison for that
control is available, so attribution remains open and this group is not green.
A reduced probe localized the failed counter assertion: exceptions from a
helper or awaited child entered an already-departing inner finally twice,
whereas explicit throw ran it once. Return values were preserved. Repair of
that handler-routing defect was verified separately from the synchronous
payload-owner change. Hiding departing landing pads fixed the duplicate finally;
the previously failing control then passed its anchored formal rerun (4.13
seconds; `build/test-out/targeted-20261111-092534.log`). Independent review also
found that the coroutine root landing pad needed `fin_idx = -1` to exclude it
from departing-try scans; that sentinel is now initialized and the finding was
closed by source review.

After the sentinel repair, ten final O0/O1 probes passed exact output, leak
checking and ARC guard on host SHA256
`11130416da5edf31b30246455ebbc1ed3ee7b124ca78f29fa417731d06fcf6c8`.
They cover the unchanged original fixture, the new synchronous owner matrix,
the async pending-owner control, the reduced duplicate-finally probe and
coroutine-root helper escapes before and after suspension. Full commands,
outputs and hashes are preserved in
`_scratch/host-finally-owner/final-build-and-probes.log`.
Formal evidence remains eleven earlier passes plus one later pass, not a
12/12 run on the final binary. Windows native execution does not establish
native Itanium-target coverage. The original group log is
`build/test-out/targeted-20261111-092212.log`, preserved under
`_scratch/sync-owner-eh-review/host-ctest-20261111-092212.log`.

A later synchronous self-host cleanup probe found that an exception thrown by
a callee inside a catch skipped its sibling finally. Both the checkpoint gen1
and the unedited current gen1 exited zero while omitting `catch finally`.
The host on the same natural source also omitted that line and ran another
rethrow-finally path twice. These additional host defects remain open; they
are not cleared by the earlier host owner matrix. Baseline evidence is under
`_scratch/sync-exception-landing/before-20261111T093543169`.
The self-host cleanup-landing prerequisite passed its frozen-input validation.
The new synchronous catch cleanup entry executes the sibling finally without
rematching sibling catches; exceptional lock exits release the monitor. A
second native OS thread acquired the monitor in the repaired self-host, while
the old gen1 and host reported it blocked. All seven EH/pending-return globals
are thread-local, and monitor exit is retired before outer finalizers to avoid
a second unlock on a replacement exit. Independent static review confirmed
these boundaries. The standalone durable runner is
`tests/selfhost/run_sync_exception_landing.cmake`; it removes stale artifacts
and checks compiler, tool, runtime, fixture and golden identities.

Frozen tests passed both semantic fixtures and five existing controls. The
formal `selfhost_gen1` CTest passed 1/1 in 8.11 seconds
(`build/test-out/targeted-20261111-095347.log`). Generation 2 and 3 IR were
byte-identical at 28,836,909 bytes, SHA256
`ba5feb6dbdcdc159b9f6ca7b2bd49059740466a7f8c3f7ea83b2c75c6aa3f566`.
The final gen1 SHA256 is
`d23a913c0265bae215eb9d3d8d17db945a5072e3ee74aed5608500688451ffd3`.
LLVM assembly validation passed the semantic, lock and async controls and both
fixed-point modules. This is not a new 28+10 production checkpoint.

Old/new frozen probes also reproduced three unresolved self-host defects on
both binaries: nested async finally can overwrite the propagating exception;
a break abandoning a pending return leaves its object alive; and an interrupted
replacement return caught inside the original finally can crash with Windows
access violation 3221225477. The latter two require pending-return scope and
watermark repair, not a null-guard workaround. Evidence is under
`_scratch/sync-exception-landing/after-20261111T095057276` and
`_scratch/sync-exception-landing/closure-20261111T095145770`.
Exception-payload ownership and generic local unwind remain open.

The subsequent synchronous pending-scope batch repairs those two recorded return
failures. A provisional replacement return keeps the original record reachable
until handlers that could mark it have retired; cancellation can then unlink and
release an interior record without invalidating a handler watermark. Outward
break/continue/goto cancel only exited owner scopes. Internal transfers retain
the enclosing return, and finally clones and nested native functions keep distinct
label and pending contexts. Independent review also caught an intermediate
regression: hiding completed finally tails without hiding their `finActive`
indices let a new try reuse an index and silently skip its finally. The preserved
pre-fix binary printed only `3`; the corrected probe prints `observe` then `3`.

Frozen evidence is under
`_scratch/sync-exception-landing/pending-after-20261111T103048184`.
The original nested probe changed from access violation 3221225477 to `inner`
then `1`, and the abandoned-return probe changed from `2/true` to `2/false`.
The new `tests/selfhost/sync_pending_scope.zan` matches all 80 expected lines,
including weak lifetimes, internal/outward transfers, outer-handler abort,
for/do/foreach record reuse followed by an unrelated throw, clone labels,
lambda isolation and the hidden-active sentinel exactly once. Eight existing
controls also match. The durable `run_sync_pending_scope.cmake` passed twice
under the shared gate; this standalone regression is not registered in the
`selfhost_gen1` CTest and must not be counted as its coverage. Final gen1 SHA256:
`177c913125d978bfa7112ebc8ea2411056d94a53c3f5cecae0916f1c697b4781`.
Closure evidence is under
`_scratch/sync-exception-landing/pending-closure-20261111T103747147`:
13 generated probe/control IR modules passed `llvm-as`, and generation 2/3 IR
were byte-identical at 28,918,550 bytes, SHA256
`138513d23f8d550a27cb27b1d64a2433183ae6414178475c8b69eb5e15b32728`.
Source identities remained unchanged and matched the frozen matrix. The formal
`selfhost_gen1` single CTest passed 1/1 in 8.33 seconds
(`build/test-out/targeted-20261111-103858.log`). The independent final static
review found no remaining blocker in this scope repair.
The async exception-identity probe still produces its previously recorded wrong
identity. These Windows results do not establish full exception-payload ARC,
generic local unwind or a new complete production differential checkpoint.

The next self-host batch repairs asynchronous exception identity and named catch
binding ownership. Pending exception value/type pairs and immutable bare-rethrow
snapshots survive suspension through distinct frame scratch slots. Explicit throw,
callee exceptions and failed awaits share cleanup landings; catch cleanup does not
rematch sibling catches. Normal finally completion restores the pending pair;
escaping replacement exceptions and return overrides do not. Spill registrations
are removed by identity rather than by assuming a shared spill list is LIFO.
Named bindings hold their own reference, independent of the immutable snapshot;
normal, exceptional, return and outward-goto exits release that reference, clearing
local/frame slots before typed or cell release. Internal goto preserves its owner.
Captured catch bindings use the same cell layout as other captured locals.

The original async probe now prints `nested/original/nested/original`. Review
caught and corrected intermediate spill removal, cleanup-entry, captured-cell and
binding-exit defects. A natural repeated-exception matrix first exposed heap
corruption after identity restoration reached a previously bypassed path: a
synchronous named catch released a borrowed exception while a static field still
referenced it. Adding the binding's own reference corrected that matrix; all
binding exits were then completed rather than retaining objects to hide the fault.
The preserved old natural matrix exits 1 with an unhandled B, whereas an
intermediate new binary exits 3221226356; these are distinct recorded failures,
not evidence that old and new had the same heap-corruption cause.

Durable regressions are `tests/selfhost/async_exception_identity.{zan,out}` and
`async_exception_natural.{zan,out}`, with `run_async_exception_identity.cmake`.
The 45-line identity/owner matrix explicitly uses external anchors to isolate
identity from the still-open payload protocol, and independently checks weak
lifetimes for reassigned bindings. The natural regression repeats the same A/B
without extra keepalive anchors. Final gen1 SHA256 is
`40a36fd500778c46819cc304a645e62682b9bdcf5a0654ce897031f2821594ab`.
Final generation 2/3 IR are byte-identical at 29,013,801 bytes, SHA256
`fd9350d82260ee6f13b205139537be1ed884076838375ab83a86b723591aab2b`;
source identities remained stable. Final direct evidence is under
`_scratch/sync-exception-landing/async-identity-durable-final`: all 10 matrix/control
entries match exact stdout, and the standalone runner validates both durable
fixtures (45 and 10 lines) with LLVM verification and stable input hashes. The
complete natural matrix also exits zero in
`async-identity-binding-final/results.json`. Final fixed-point evidence is under
`_scratch/sync-exception-landing/async-owner-fixedpoint`. Independent frozen-source
review found no remaining blocker within identity and named-binding scope. The earlier formal
2/2 CTest run (`build/test-out/targeted-20261111-114022.log`) predates the final
binding fixes and is not final-binary CTest evidence. Full payload ownership,
generic local exceptional unwind, cross-platform validation and the complete
production differential remain open.

The subsequent exception-payload batch gives exception records an explicit active
state and transferable ownership. Failed await checks the active state independently
of the payload pointer, takes and clears the child record, and frees the consumed
child frame before raising in the caller. The payload regression also exercises
fresh and borrowed objects, named and bare rethrow, replacement exceptions,
finally, suspension, and a caught binding returned across suspension. Weak-reference
checks observe lifetimes without adding strong keepalive anchors. String checks
cover values, embedded NUL and escape, rather than complete allocation balance.

A formal targeted run passed `selfhost_fixed_point`, `selfhost_gen1` and the new
`selfhost_exception_ownership` CTest, 3/3 in 129.95 seconds
(`build/test-out/targeted-20261111-142222.log`). The ownership test builds one fresh
gen1 from the canonical source manifest, then runs pending-scope (80 output lines),
async identity/natural (45 and 10 lines), and payload ownership (121 lines) oracles.
These are output counts, not separate CTest counts. Each generated fixture passes
LLVM verification, linking, exact stdout and runtime-stderr checks. The runner
rejects stale outputs, failed child stages and changes to its recorded source,
tool, runtime and file-valued linked-library identities. Automatic standard-library
source dependencies are not included in that identity list, so this establishes
stability of the recorded inputs rather than a complete build dependency closure.
This formal checkpoint predates the subsequent `Task<T>.Result` repair and expanded
payload fixture.

The follow-up fixes failed `Task<T>.Result` consumption through that same exception
transfer path. The preserved old gen1 returns zero for both immediate failure and
failure after Yield, skips the catch, and leaves the payload alive. The corrected
direct probe keeps the payload alive through Wait and catch, then releases it on
catch exit; the successful result remains 42. Each handle is consumed once.
The durable fixture adds both failure paths and a successful value of 75, bringing
its exact output to 130 lines. The final `selfhost_exception_ownership` CTest passes
1/1 in 25.47 seconds (`build/test-out/targeted-20261111-142846.log`). It freshly
builds gen1 SHA256
`1709f6e7b4516d774194dd5118e2c0112f29c1115fa6843ea016281702686934`
and passes all four fixture oracles with unchanged recorded input identities.
A preceding direct suite run matched the output but was correctly rejected for
concurrent source changes; it is diagnostic evidence only. The earlier fixed-point
pass predates this follow-up and is not a fixed-point proof for this final gen1.
This checkpoint does not cover the later successful-completion handler-depth fix:
a subsequent probe established that an already-installed caller catch could jump
into a returned child trampoline after successful await or Result consumption.
Six of eight immediate/Yield, sync/async, await/Result shapes fail with Windows
access violation or heap corruption; async await's rearm path masks the other two.
The repair restores the resume invocation's saved exception-handler depth before
terminal return. The same eight direct probes then pass LLVM verification, linking
and execution, each reporting its successful value and catching the subsequent
exception. The durable payload fixture now has 162 expected lines, adding all
eight shapes with fresh-payload weak-lifetime assertions. The final follow-up
`selfhost_exception_ownership` CTest passes 1/1 in 24.32 seconds
(`build/test-out/targeted-20261111-143802.log`), using fresh gen1 SHA256
`29563114760647dd03afb1bd7f0f08fc0a821c2d46927533e722886001059700`.
All four fixtures pass, and recorded input identities remain unchanged across the
build and execution. Independent static review found no blocker in the Result
transfer or completion-depth restoration paths. This final follow-up does not have
a new fixed-point run. Generic local exceptional unwind, complete string allocation
balance, cross-platform validation and the complete production differential remain
open.

Allocator failure injection has not passed: its separate execution task was
blocked by host security policy and was not retried through another agent.
They establish the exercised cases, not complete production replacement. The full
test suite and real full release gate were not run for this batch. The separate
Linux runtime/sanitizer evidence above remains unchanged.

The expanded lifecycle repair has a direct frozen-source checkpoint at
`_scratch/production-closure/third`, gen1 SHA256
`ebe2ded3d5aa19596d3f47785bc030f3ef2a0fab8f6378a79ca2ad13ce333f4e`.
The minimal local-owner probe, the 46-line `selfhost_local_exception_owner` oracle
and the 15-line `nullable_box_owner` oracle all pass emission, whole-module LLVM
verification, linking, exact stdout and empty runtime stderr. The recorded host,
canonical selfhost manifest, emitter sources and tools were unchanged across this
run. This diagnostic harness does not capture the complete standard-library and
native dependency closure, so it is not a replacement for the durable suite.

Local owners now use typed slot destructors in the exception-owner chain. Normal
scope exits retire owners and clear slots before release; async locals use stable
frame slots. The exercised paths include cross-frame unwinding, reassignment,
captured cells, arrays/delegates, return protection, query ranges, pattern guards,
awaited captured values, async foreach and goto exits from ordinary blocks, loop
headers and switch pattern sections. A switch-expression reference fallback also
uses the correct LLVM null constant. The checkpoint includes async return statement
temporary cleanup; await argument temporary cleanup is a separate follow-up.

Allocation-monitor diagnostics additionally exercise generated-module allocation
and release, forwarding to the real CRT. These measurements include emitted
ownership records, exclude runtime-internal allocations and do not establish
concurrent allocator correctness. Before the async follow-ups, ten string groups
pair every allocation/free with zero live block/byte deltas and zero invalid frees;
the pre-follow-up async group leaves one 39-byte block. The subsequent durable
string run at `_scratch/selfhost-string-balance/third-durable-string` passes all
11 groups, including async at 18 allocations and 18 frees. It verifies original
and monitored LLVM, exact values, empty runtime stderr and stable full stdlib plus
recorded tool/runtime identities. That first monitor models realloc as allocate/copy/free;
the transparent-CRT monitor revision forwards real realloc and requires a separate
rerun before its measurements can be attributed to that revision. Nullable local/loop, decimal postfix
and array/List groups balance at 133/133, 6/6 and 15/15 respectively; async consumption
still leaves one 32-byte box. These are explicit failing follow-up cases, not closed
allocation acceptance. Durable runners now pin the standard-library tree and verify
both original and observed LLVM modules; their final execution is pending.

The standalone Task registry regression passes four query threads against 50,000
concurrent retirement/child-edge cycles plus 100,000 serial reuse cycles, ending
with zero live entries and capacity 16. Its callback re-enters the registry to
exercise destruction outside the lock. The later raw-edge version (registry SHA256
`822a192f328511b01dd8b33383f9bb01d91290d502574e7217429fe61990ae23`)
also passes real Ubuntu WSL clang 18 ASan/UBSan and TSan builds and execution with
no diagnostics or suppressions. Both runs match the expected output and 85 recorded
source/header/tool/sanitizer-library inputs remain stable. Evidence is retained at
`build/cases/task-registry-sanitizers`. This is registry evidence only; typed
result-discard callbacks are a subsequent change. Generated public tokens,
scheduler integration and actual frame allocation/free compatibility remain under
repair.
Nine typed-array diagnostic regressions also pass LLVM verification and their exact
oracles. Erased-array feature gates have not yet been removed at this checkpoint.

Release provenance now has capture/seal/verify tooling and a locked
`build.ps1 -ReleaseIdentity` entry point. Synthetic protocol tests cover source,
cache, tool, native input and artifact drift, plus identity replacement during the
gate. No successful observed release build or real release gate is claimed yet.
The final fixed point, fresh golden review, production differential, Linux sanitizer
checks and full release acceptance remain pending for this expanded batch.

A later host-oracle check rejects the expanded local fixture's cross-scope owning
`goto` with E3083, while nullable's 15-line fixture passes. Thus the 46-line selfhost
checkpoint is a diagnostic of cleanup machinery, not a host/selfhost semantic
closure: language-rejected goto cases must become rejection tests and be separated
from the positive production fixture. The selfhost checker must preserve the host's
restriction rather than silently broaden accepted source programs.

The resumed lifecycle batch adds allocator provenance to the Task registry: pool
allocation records its release callback before publication, and retirement chooses
that callback without probing bytes before an unknown payload. The entry is cleared
on final retirement and callbacks still execute outside the lock. The expanded
registry fixture covers typed discard with callback re-entry, one consuming owner,
a simultaneous observer pin and a custom allocator. Windows execution plus WSL
clang 18 ASan/UBSan and TSan all pass with exact output and empty stderr; the
recorded fixture, registry, header and Windows executable hashes remain stable.
Evidence is in `build/cases/task-registry-final`. This identity scope is narrower
than the earlier 85-input sanitizer record and does not establish native-pool or
generated Task integration correctness.

The transparent CRT monitor rerun at
`_scratch/selfhost-string-balance/third-durable-string-crt-realloc` passes all eleven
string groups (134 allocations and 134 frees, zero live block/byte deltas and zero
invalid frees) using the recorded third gen1. That result verifies the revised
monitor, not the final selfhost source state. A subsequent fresh canonical gen1
passes nullable's fifteen lines and backward goto's three lines, but its public
Task.Release fixture exposes a Result reference left alive after the user clears
it. The host version compiles but aborts after the first output. Both are failing
integration follow-ups; no final Task or production acceptance is claimed.

## Release evidence

The release workflow invokes `scripts/release_quality_gate.cmake` on the exact build
that supplies its archives. The gate checks required bootstrap test registration,
runs the nonempty full suite and the required bootstrap tests, and stops packaging
on failure. JUnit reports are retained as workflow artifacts. Publication also
depends on Linux ASan/UBSan and TSan multiworker release runs (2 and 4 workers,
1000 pool cycles each). Their execution logs are retained even when a run fails;
a post-publication sanitizer run cannot satisfy this prerequisite.

A release record must identify the source revision, toolchain, platform/CPU baseline,
archive checksums, test results and any declared exclusions. Device smoke tests and
business soak results must identify their actual devices and workload. A local
Windows run does not certify Linux sanitizers or Android/HarmonyOS device behavior.
