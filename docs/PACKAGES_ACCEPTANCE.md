# Package acceptance evidence — 2026-11-11

This record distinguishes the Windows acceptance runs before the package refactor
from checks run after it. Each result applies to the compiler, package sources,
peer versions and options used in that run. The checkout contains concurrent
changes; run the same acceptance against the exact build selected for deployment.

Commands and prerequisites are documented in
[tests/integration/README.md](../tests/integration/README.md). These tests reuse the
main-tree packages and compiler; no copied source tree is involved. Full runs use
one shared `build` gate. The service instances use private data directories,
loopback-only random ports and temporary credentials, and are stopped and cleaned
up after the run.

The targeted-test wrapper rotates its top-level `targeted-*.log` files. Some
historical filenames below have expired; their reported results remain in this
record. Final follow-up logs explicitly preserved under `package-refactor/` are
separate evidence files and are not subject to that wrapper's five-log rotation.

## Package refactor verification

- AppUpdate now stages validated new payloads and original destination/public
  backup snapshots under an exclusive install lock, then publishes with
  recoverable rollback state. The expanded `case_app_update` passed 1/1 in 8.10s
  (`build/test-out/appupdate-transaction/ctest.log`). A subsequent coherent direct
  compile/run passed after a small signed-plan identity refinement
  (`build/test-out/appupdate-transaction/final-probe.log`). The final source was
  also rebuilt with `--check-leaks --arc-guard` and run through redirected
  subprocess pipes: exit 0, the expected success line in
  `build/test-out/appupdate-transaction/instrumented.stdout.log`, and a zero-byte
  `instrumented.stderr.log`. This capture supplements the console transcript,
  which did not capture the program's actual output. Coverage includes
  a real child process exiting after partial publication without rollback/unlock,
  public-API recovery/retry, another-process lock denial, automatic rollback on
  a later destination failure, newly created file removal, original/public backup
  restoration, corrupt/mismatched state rejection, committed cleanup, and signed
  uppercase digest/signature save-load compatibility. These are Windows checks;
  process interruption is covered, not power loss or whole-directory atomic
  visibility. See the package README for trusted-local-state and snapshot limits.
  A later root/path follow-up rejects transaction names in every manifest path
  segment, preserves POSIX literal trailing backslashes, and binds format 2
  Windows state to directory volume/file identity. The final strict Windows
  compile/run returned exit 0 with the expected success line and empty stderr;
  `case_app_update` passed 1/1 in 9.37s. Evidence is preserved in
  `build/test-out/appupdate-transaction/root-identity/`: `windows.result.log`,
  `windows.stdout.log`, `windows.stderr.log` and `ctest.log`. Tests cover uppercase
  directory aliases for interrupted recovery and committed cleanup, copied state
  rejection at a different directory, nested reserved names, and retention/refusal
  of legacy format 1 state. Actual 8.3 aliases were not executed. The permanent
  POSIX literal-backslash regression reached object generation, but linux-musl
  linking failed on existing EH/runtime symbols including `_Unwind_Resume` and
  `__cxa_throw`; see `linux-compile.stderr.log` there. This was the boundary of
  that earlier run; the subsequent Linux runtime repair is recorded below.

- A deterministic native follow-up proved a scheduler-liveness hole in both
  Windows I/O completion polling paths: another thread observed all-idle after
  pending I/O was decremented but before continuations were published. Both paths
  now retain the I/O count until ready-queue publication. Batch testing also
  exposed outstanding-task accounting that applied only the sign of a delta;
  it now applies the full batch delta. The native test passes timeout ownership,
  single/four-completion batches, ten independent idle observations, task counts
  and final drain checks. Before/after logs:
  `build/test-out/io-handoff-repair-evidence/native-before.stdout.txt` and
  `native-after.stdout.txt` in the same directory. These are explicitly labeled
  tool-output observations recovered after the original logs rotated out.
  A rebuilt compiler/runtime then passed eight isolated MariaDB savepoint cases
  with strict runtime/leak checking and private-instance cleanup. Report:
  `build/test-out/mysql_acceptance-20261111T024454Z-69b961317c9a/mysql_acceptance.json`.
  This proves and repairs a matching premature-exit mechanism; historical failed
  runs had no handoff trace, so it does not uniquely attribute every earlier
  savepoint omission to this race.
  The same ownership transfer was repaired in DNS completion/timeouts and POSIX
  reactor completion/close queues. Windows native DNS tests cover normal,
  queued-timeout and started-timeout/late-worker completion without double wake
  or decrement (captured observations retained with the native after-output
  evidence). An actual Linux native probe passed epoll single/batch completion,
  close notification and failed registration using main-tree sources under WSL:
  `build/test-out/io-handoff-repair-evidence/io-handoff-linux-registration-final.log`.
  A final Windows rebuild and six targeted tests passed in 26.30s:
  runtime_iocp_timeout, mysql_async_nonblocking, db_pool, pool_async_reset,
  async_await_exception_finally and async_generic_frame_owner. Log:
  `build/test-out/io-handoff-repair-evidence/io-handoff-final-ctest.log`.
  Final build logs and source/binary metadata are retained in that same directory.
  macOS kqueue was source-reviewed
  only; no macOS execution is claimed.
  A second review found a separate DNS allocation-lifetime race: timeout delivery
  detached jobs while the worker could free them before delivery completed.
  A lock-protected delivery owner now keeps each job alive through `io_wake`;
  worker completion and thread-start failure hand destruction to that owner or
  reclaim after delivery, with one inflight release. The shared native regression
  forces four worker/delivery interleavings across seven cases and checks exactly
  one free, wake and release. It also exercises worker-local batch accounting and
  consumer rearm/free before notification returns. Windows passed eight native
  iterations; Linux passed eight after correcting the test's POSIX TLS success
  return check (the initial failed run is retained). Rebuilding zanc and both
  runtime variants then passed `runtime_iocp_timeout`, `case_async_dns` and
  `case_mysql_async_nonblocking`: 3/3 in 24.58s. Current-source evidence is under
  `build/test-out/io-handoff-repair-evidence/dns-timeout-owner/`: `windows-*.stdout`,
  `windows-*.stderr`, `linux-final/linux-current-results.json`, the corresponding
  Linux stdout/stderr, `final-build.log` and `final-ctest.log`. No historical
  pre-fix execution was completed for this allocation-lifetime follow-up; older
  before/after excerpts and metadata in that directory concern the preceding
  scheduler repair and do not establish a DNS before/after result.

- The focused ORM execution group passed 4/4. The SQLite snapshot fixture checks
  exact pagination beyond the former 10,000-row window and uses an executor that
  rejects synchronous queries to prove async dispatch and snapshot isolation.
  The package-free generic overload regression and ORM reverse relation case
  passed 2/2 after the checker began substituting receiver type arguments in
  overloaded return types. The migrated async CRUD case passed all 31 assertions,
  including the runtime `DbTable` query chain. A subsequent compiler fix removed
  inapplicable same-name member fallbacks, filters explicit generic arity and
  recognizes async extensions during await lowering. The final focused group
  passed 7/7, including three new regressions and existing extension/ORM cases.
  Real SQLite output `true, true, 1, one, 1` confirms async schema creation and
  typed/raw queries through `IDbExecutor`. Preserved evidence:
  `build/test-out/package-refactor/orm-compiler-focused-20261111-093043.log`,
  `orm-compiler-real-sqlite-probe.log` and `orm-compiler-package-free-after.log`
  in the same directory. The earlier overload/reverse-relation log was
  `build/test-out/targeted-20261111-085048.log`; that rotating log has expired.
  A focused selfhost differential independently reproduced wrong generic-arity
  selection and incorrect async-extension return values. Selfhost extension
  emission now marks the coroutine frame and its substituted result type, and
  extension selection rejects incompatible generic arity. Rebuilding gen1 from
  the canonical source list then passed `async_generic_extension`: 1 passed,
  0 gaps/rejections/failures. Before/after evidence is preserved as
  `build/test-out/package-refactor/orm-selfhost-before.log` and
  `build/test-out/package-refactor/orm-selfhost-after.log`. This is one focused
  differential, not a full selfhost or fixed-point run.
- After the compiler fix for ownership of specialized async frame slots, the
  new compiler regression, DbPool, SQLite lifecycle, Firebird wire, pool async
  reset, Redis, SQL Server TDS and TDengine cases passed with ARC checks. These
  are targeted conformance fixtures; they do not establish live-service coverage
  for Firebird, Redis, SQL Server or TDengine. Log:
  `build/test-out/targeted-20261111-084949.log` (the original rotating log has
  expired; the recorded result is retained here). A later narrow run still failed
  pool saturation (one borrower had not completed) and timed out the MySQL
  nonblocking fixture at 180 seconds. Those failures are separate from the
  passing real MariaDB acceptance and remain recorded at
  `build/test-out/package-refactor/targeted-20261111-085715.log`.
  A deterministic native probe then reproduced incorrect timed-Gate ticket
  assignment and lost timeout-before-park wakeups. Per-wait registrations and
  explicit park tickets fixed all 14 native assertions with no live allocations.
  The native case plus three Gate conformance cases passed 4/4; pool saturation
  subsequently passed its isolated rerun. Both reactor runtime objects and zanc
  were rebuilt before these runs. Logs:
  `build/test-out/package-refactor/gate-timeout-native-20261111.log`,
  `build/test-out/package-refactor/targeted-20261111-092401.log` and
  `build/test-out/package-refactor/targeted-20261111-092438.log`.
  A gen1 rebuilt from the current canonical selfhost sources also emitted,
  linked and ran only the Gate timeout differential with all 10 assertions
  matching. This checks the changed selfhost ticket lowering, not the full
  corpus, fixed point or WASM. Log:
  `build/test-out/package-refactor/gate-selfhost-focus-20261111.log`.
- The MySQL nonblocking timeout was traced to eager synchronous candidate reads
  in delegate mutation `Where`, followed by an unhandled main exception and
  still-running mock/ticker tasks. The async driver rejected synchronous Query;
  this was not evidence of blocked database I/O. UPDATE/DELETE now defer candidate
  reads to their terminal, snapshot setters/conditions/parameters and use
  `QueryAsync` in async terminals. The original natural-lambda MySQL fixture
  produced its ten expected lines with empty stderr under ARC/leak checking.
  The async-only executor regression and four related cases passed 5/5 in 86.35s.
  Log: `build/test-out/package-refactor/mysql-orm-focused-20261111-093810.log`.
  Review then found an additional raw-SQL OR grouping boundary. The per-execution
  predicate append now groups the complete existing condition before adding the
  matched-key filter. The revised UPDATE/DELETE regression passed independently
  in 12.31s, including bound OR conditions that would otherwise bypass delegate
  filtering. Log: `build/test-out/package-refactor/mysql-orm-sql-or-20261111-094022.log`.
  Additional raw-OR zero-hit UPDATE and DELETE assertions passed a bounded direct
  probe with ARC/leak checking and empty stderr; production sources were unchanged.
  Log: `build/test-out/package-refactor/mysql-orm-zero-or-probe-20261111.log`.
- Independent HTTP, TLS 1.2/1.3 and HTTP/2 interoperability passed all 28 items,
  including certificate rejection, malformed response handling and H2 shutdown.
  Report: `build/test-out/net-interop/20261111T003520.205622Z/results.json`.
- Independent WS/WSS interoperability passed all 17 items with strict runtime and
  ARC checks, including concurrent sends, control frames, close and timeout.
  Report: `build/test-out/ws-client-session-guard/results.json`. The corrected
  WS auth fixture verified HTTP 401 on the wire and matched its golden output
  in an instrumented direct run (`build/test-out/ws-client-session/ws-client-auth-probe.log`).
- The persistent jobs group passed 6/6 with leak and ARC checks. It exercises
  legacy ZanStore keys, retry exhaustion and replay after an injected completion
  commit failure and reopen. It does not simulate a process kill.
- The first PostgreSQL rerun stopped reporting results during the async savepoint
  case; it did not reach stress or restart. The first MariaDB rerun passed the
  typed parameter case, bounded 400-write workload and same-pool restart, but
  omitted its savepoint result. Both suites correctly returned failure and cleaned
  up their private services. Reports: `build/test-out/postgres_acceptance.20261111T003112212151Z-c1207e.json`
  and `build/test-out/mysql_acceptance-20261111T003200Z-d53e6de77231/mysql_acceptance.json`.
- After correcting failed-await exception routing, PostgreSQL passed all 13 cases,
  including typed async ORM and both savepoint paths. Four independent workers
  issued 400 writes (264 commits), verified by psycopg2. The same process and pool
  recovered after an interrupted query and a private instance restart; a separate
  connection confirmed the recovery write. Report:
  `build/test-out/postgres_acceptance.20261111T003557353976Z-403693.json`.
- The final real-service mutation follow-up initially failed the parameter case
  on both providers because the fixture entities declared SQL primary keys but
  lacked ORM `[Column(IsPrimary = true)]` metadata. The drivers correctly rejected
  delegate mutations without an addressable key. The fixture IDs were annotated
  before the rerun; the rejected runs remain recorded at
  `build/test-out/postgres_acceptance.20261111T014246610418Z-3a5a91.json` and
  `build/test-out/mysql_acceptance-20261111T014333Z-e47a45d4b34a/mysql_acceptance.json`.
  With explicit fixture key metadata, PostgreSQL 16.15 passed all 13 baseline
  cases, including ten new async mutation assertions. Independent persisted IDs
  remained `[100,101,201,202,301,303,311,313]`. Four workers issued 400 writes
  (264 commits, 136 rollbacks), independently verified. An in-flight query was
  interrupted in 124 ms, and the same process/pool recovered on a new backend
  after restart. Recovery write visibility and cleanup passed. All six fixture,
  stress and restart stderr files were empty, with no runtime/ARC diagnostics in
  stdout. Report:
  `build/test-out/postgres_acceptance.20261111T014451976684Z-d31226.json`.
  MariaDB 10.11.14 (`mysql_native_password`) then passed all 11 baseline cases,
  including eleven new mutation assertions. A separate physical connection
  confirmed the original rows 1/2, Unicode/injection payload, SQL NULL and empty
  strings after the mutation transaction rolled back. Four workers issued
  400 writes (200 commits, 200 rollbacks); native CLI checks matched before and
  after restart. The same process/pool recovered after a bounded 2,039 ms outage
  failure, and the native CLI confirmed the recovery marker. All 16 Zan case,
  stress and restart logs were inspected without ARC/leak/runtime or unhandled
  diagnostics. Private service/data-directory cleanup passed. Report:
  `build/test-out/mysql_acceptance-20261111T014606Z-2101fd56ee18/mysql_acceptance.json`.
  These bounded final passes cover the current mutation changes; they do not
  establish a cause for the earlier intermittent savepoint failures.
- The six-case HTTP rerun passed the failed-await compiler regression, client
  framing and content decoding, HTTP/HTTPS upload parity with exclusive spool
  creation, chunked response handling, and HTTP/2 teardown with ARC checks.
  Log: `build/test-out/targeted-20261111-083836.log`.
- MariaDB's second run still failed the savepoint fixture: it exited without its
  result and leak checking found reachable objects. The typed ORM, stress and
  restart items passed again. Report:
  `build/test-out/mysql_acceptance-20261111T003638Z-0411e0e2d042/mysql_acceptance.json`.
  The unchanged savepoint source then passed eight isolated runs. A subsequent
  full run passed all 11 baseline cases, four independent workers (400 writes,
  200 commits), native CLI verification before and after restart, same-pool
  recovery and cleanup. Reports:
  `build/test-out/mysql_acceptance-20261111T004616Z-310be974a1d3/mysql_acceptance.json`
  and `build/test-out/mysql_acceptance-20261111T004949Z-8c35bdca7a7d/mysql_acceptance.json`.
  These passes do not establish the cause of the two earlier failures or a
  scheduler fix; rebuilding `zanc` alone does not establish which runtime object
  the fixture linked.
- MCP publish and ZanIndex full linking passed. Live MCP and CLI runs produced
  byte-identical routes, entities and index JSON for real compiler metadata and
  preserved the separate RepoMap files. A failed refresh did not reuse stale
  metadata. MCP and GenKnowledge produced identical final gallery JSON (124
  entries, zero warnings). Gallery coverage, MCP RepoMap and GenIndex passed;
  the original `build/test-out/targeted-20261111-085254.log` has rotated out.
  The final GenIndex log is preserved at
  `build/test-out/package-refactor/targeted-20261111-085405.log`.
- The affected `server-collab` and `server-ws` templates both fully linked and
  produced nonempty executables. Report:
  `build/test-out/template-acceptance/20261111-003837-abd6a32c/results.json`.

## Earlier independent network interoperability

Python standard-library HTTP/SSL peers and `h2 4.3.0` exercise the production Zan
client and HTTP/2 server. Certificates are generated per run. TLS checks use an
explicit trusted root and retain chain and hostname verification.

The initial run found a TLS 1.2 interoperability defect: ClientHello advertised
`rsa_pss_rsae_sha256 (0x0804)`, but ServerKeyExchange rejected that scheme before
signature verification. The TLS 1.2 dispatch now uses the existing SHA-256 RSA-PSS
verifier. The original negotiation and fixture were retained. A captured public
OpenSSL signature vector and mutation regressions protect the dispatch.

The original failing result is retained at
`build/test-out/net-interop/20261110T163404.668188Z/results.json`.
After the fix, the unchanged test suite passed **24 protocol cases plus four
supporting checks**, with zero failures or skips:

- HTTP: 12 cases covering binary NUL GET/POST, gzip empty/single-byte/binary
  decoding, exact and exceeded expansion budgets, bad CRC, truncation, missing
  decoder rejection, recovery after a truncated response, and 128 bounded echo
  requests.
- TLS: eight cases covering TLS 1.2 and 1.3 trusted chains, wrong roots, wrong
  hostnames and missing intermediate certificates. Positive cases exchange and
  compare binary data; negative cases require the expected certificate rejection.
- HTTP/2: four cases using an independent Python peer for multiplexed streams,
  100,000-byte flow-controlled responses, binary POST and reconnection.
- Supporting checks: both fixture builds, independent OpenSSL certificate-chain
  verification, and graceful HTTP/2 server shutdown with ARC checks.

Passing report:
`build/test-out/net-interop/20261110T164425.327054Z/results.json`.
The final run with the hardened process lifecycle and runtime-diagnostic checks
again passed all 28 items with zero failures or skips:
`build/test-out/net-interop/20261110T173159.036518Z/results.json`.
The nine-assertion `case_tls12_rsa_pss_exchange` regression also passed in 8.77
seconds, including signature/transcript mutations and mismatched signature schemes.

## PostgreSQL 16.15

A private PostgreSQL 16.15 server runs in WSL Ubuntu. The native Windows Zan
fixture connects over loopback TCP with a non-superuser SCRAM-SHA-256 account.
The independently implemented Windows psycopg2 client checks authentication,
server state and final persistence.

All **13 Zan cases passed**:

- Synchronous and asynchronous bound parameters preserve quote/injection-style
  text, Unicode, SQL NULL and empty text.
- Synchronous and asynchronous UnitOfWork commits, rollbacks and savepoints agree
  with separate-connection observations and server transaction state.
- Synchronous and asynchronous COMMIT of an aborted transaction report failure
  when the server completes it as ROLLBACK.
- An idle pooled backend terminated by the server is evicted and replaced.
- Sixteen bounded acquire/query/release cycles, including deliberately closed
  connections, preserve pool accounting and drain on close.
- Repeated incorrect-password attempts do not leak pool reservations.

The separate psycopg2 check verified the exact eight persisted records and the
absence of rolled-back records. The server reported no remaining fixture sessions.
Private server stop, Linux data/password cleanup and Windows scratch cleanup passed.

Passing report:
`build/test-out/postgres_acceptance.20261110T164353136226Z-8f0006.json`.
The enhanced `full+stress+restart` run also passed all 13 baseline cases. Four
independent Zan workers reached a shared start barrier and completed 100 writes
each: 264 commits and 136 rollbacks in total. Independent psycopg2 observation
matched the exact committed row set, payloads, NULL/empty values and absence of
rolled-back keys. Each worker owned a separate size-one pool; this does not test
shared-pool thread safety.

For restart, psycopg2 observed the exact pooled backend executing `pg_sleep` before
the private instance stopped. The interrupted operation failed in 106 ms; the same
Zan process and pool reconnected on its first attempt after restart. A separate
connection verified the recovery write. Zero remaining fixture sessions, private
server stop, and Linux/Windows cleanup passed. Enhanced report:
`build/test-out/postgres_acceptance.20261110T172733635882Z-aaa7ad.json`.

These runs use `sslmode=disable` only on the private loopback test instance; they
are not PostgreSQL TLS or remote-deployment evidence.

## MariaDB 10.11.14

A private MariaDB 10.11.14 server runs in WSL Ubuntu with a temporary
`mysql_native_password` account. Its own CLI independently checks setup and
credentials; the Windows fixture then tests the Zan MySQL protocol implementation.

All **11 Zan cases passed**: native authentication, incorrect-password rejection,
parameters preserving injection-style text/Unicode/NULL/empty, prepared-statement
cache reuse, commit, rollback, savepoints, statement-error recovery, bounded pool
cycles, recovery after server-side connection termination, and transaction cleanup
when returning a pooled connection. Private server/data cleanup passed.

Passing report:
`build/test-out/mysql_acceptance-20261110T164456Z-12e295ddcfa5/mysql_acceptance.json`.
The enhanced run also passed all 11 baseline cases. Four independent worker
processes, each owning its own pool, completed 100 transaction writes each: 200
commits and 200 rollbacks. The native CLI independently matched every persisted
primary key, amount and exact UTF-8 payload, and repeated that verification after
restart. Workers were launched before waiting for completion, without a shared
start barrier; transaction overlap was not measured. This establishes bounded
worker execution and data correctness, not shared-pool thread safety or production
capacity.

The same Zan process and pool observed an invalid old lease and a failed connection
attempt while the private instance was down, then acquired a new authenticated
physical connection after restart. Independent CLI persistence and private
server/data/Windows scratch cleanup passed. Enhanced report:
`build/test-out/mysql_acceptance-20261110T173135Z-b8dad6980f6b/mysql_acceptance.json`.
This is MariaDB native-password evidence, not Oracle MySQL 8,
`caching_sha2_password`, MySQL TLS or SQL Server evidence.

## Validation infrastructure

The synthetic `tests/wrapper_exit_selftest.ps1` passed. It checks native command
failure, exceptions, CTest failure, successful CTest, failing builds and successful
builds through the real wrappers. Failed tests must not be reported as successful
because cleanup or scheduler commands overwrite an exit code.

The template harness now has exact filtering, a bounded compiler timeout for each
project, persistent stdout/stderr and JSON checkpoints, and requires a nonempty
artifact after a successful compiler exit. Fifteen lightweight harness checks
passed, including the optional bounded two-worker runner, which retains complete
EXE/DLL linking behavior.

The network harness also passed 15 Python self-checks on Windows. These cover
runtime-diagnostic rejection, forced `ZAN_RT_HARD=1`, timeout and interrupt cleanup,
and a parent exiting while a descendant still holds its output pipe. Windows Job
Objects own descendants from suspended creation until cleanup; POSIX cleanup was
mock-tested only. These checks validate the harness, not additional protocol cases.

Template linking results are recorded separately as the run finishes. The first
full-link partition exposed a stale `Tabs.AddNew()` call in `gui/gui-webview`
(`E3063`, before code generation). The template now creates its page, WebView and
label using the current `Page`, `Add` and `Select` APIs. Its first exact rerun then
exposed a missing `Tabs.SetLabel` public entry point also used by the CEF example;
adding the setter let the exact WebView rerun link successfully in 95.657 seconds:
`build/test-out/template-acceptance/20261110-173757-d2704efd/results.json`.
Together with the two partitions below, all 40 projects have a successful full-link
record. A later Tabs runtime regression found an ARC cycle: button handlers strongly
captured their owning Tabs. Weak-owner action handlers remove that cycle while
retaining actual select/close/add callbacks. Closing also restores pinned-tab
protection, active-page correction, bulk close and FIFO single-consumption close
notifications. Both `case_tabs_set_label` (54.83 s) and `conformance_gui_tabs`
(100.53 s) passed with leak checks. A further active-page identity notification
edge case was fixed by comparing both the active item's identity and its index;
the extended `conformance_gui_tabs` passed in 106.95 s, including same-index page
replacement for `CloseAll` and `CloseOthers`, single notification consumption and
pinned-only no-op behavior. Final targeted log:
`build/test-out/targeted-20261111-014807.log`.
After the final Tabs repair, WebView full linking passed again in 103.219 s:
`build/test-out/template-acceptance/20261110-174820-256471c5/results.json`.
The consolidated 40-project artifact index is
`build/test-out/template-acceptance/package-acceptance-summary.json`; it points to
original per-project reports and verifies nonempty artifacts, rather than hiding
initial failures.

The 34-project partition completed with 33 passes and the WebView failure:
`build/test-out/template-acceptance/20261110-164700-e10049ad/results.json`.
The other six console/game projects were relinked using the rebuilt compiler and
all passed:
`build/test-out/template-acceptance/20261110-172800-d19d9c94/results.json`.
A successful link establishes compile/link compatibility, not interactive GUI/game
execution.

## Lifecycle and protocol boundary follow-up

The main-tree Windows compiler and local fixtures were used for this follow-up.
The following direct probes passed; these results are separate from the earlier
service and interoperability runs. Evidence is retained under
`build/test-out/package-lifecycle-followup/`.

- `transaction_scope_failure` covers false/throwing commit and rollback, one
  scope-level terminal attempt, repeated disposal, suspended completion, and
  cleanup exception precedence. It and the existing `orm_transaction_scope`
  compatibility probe compiled and ran with exit 0, exact golden output and empty
  runtime stderr, using `--arc-guard --check-leaks --strict-runtime`. Logs are in
  `transaction-scope/`.
- `pool_cleanup_failure` reproduced lost capacity and interrupted shutdown before
  the fix; its fixed direct probe passed 24 checks. `firebird_wire` reproduced
  repooling after failed rollback/replacement-transaction reset; the fixed SRP wire
  fixture passed 147 checks, including physical closure, zero idle/live counts
  and fresh acquisition. Both compiled/ran with exit 0 and exact golden output;
  program stderr was empty. Logs in `pool-cleanup/` preserve before/fixed results.
  This uses an actual pool against a local wire fixture, not a real Firebird server.
- `mysql_packet_bounds` passed arithmetic admission boundaries and 16 real-reader
  loopback cases across sync/async readers: binary and empty packets, full-size
  continuation with zero or nonzero tail, and truncated initial/continuation
  headers/bodies. Compile/run exits were 0, output matched the golden, and runtime
  stderr was empty. Logs are in `mysql-packet/`. The 64 MiB refusal boundary is
  exercised through the production admission helper; no full 64 MiB wire refusal
  or forced transport-exception run was performed.
- `redis_protocol_bounds` passed line length, CRLF, integer and depth boundaries,
  including a legal 64-level array and terminal rejection at level 65. It also
  checks nonterminal Redis errors, prefetched replies, compaction and refusal of
  later commands after protocol failure. Compile/run exits were 0, stdout was
  `redis protocol bounds failures=0`, and runtime stderr was empty. Logs are in
  `redis-protocol/`. There is no separate aggregate decoded-response memory budget.

The initial `ws_reader_close` direct probe also passed five groups covering buffer
admission, invalidation without consuming a live sentinel, pending receive close
and drain, idle-timeout prefix retention, and verified-TLS receive close. It ran
with exit 0, exact five-line golden output and empty stderr under leak checks.
Those logs in `ws-close/` precede the additional send-lifetime repair and do not
establish blocked-send closure coverage. An initial combined fixture failed its
plain-send result assertion (`sent=true`, handle already closed); this confirmed
physical drain but did not establish actual transport backpressure. Its evidence
is retained separately as `send-first-failure.probe.*`. The final combined fixture
first prefills the nonblocking socket until would-block (bounded to 8 MiB), then
uses the production sender. WS and WSS both retain the descriptor immediately
after close while the sender is still admitted, then complete with send failure,
closed descriptor and zero send leases. Fresh-connection sentinels remain intact.
The six-group final direct probe passed with exit 0, exact golden output and empty
stderr under leak checks. Final logs are `ws-close/probe.*`; earlier receive-only
logs are `ws-close/receive-only.probe.*`. These runs are Windows-only and do not
force numeric descriptor reuse or the exact instruction-level registration gap.

After the data sources stabilized, the anchored 13-case CTest group passed 13/13 in
43.35 s: the five new/extended data regressions and existing transaction scope,
pool lifecycle/reset, MySQL async/TLS and Redis client/pool/TLS cases. No full test
tier was run. Final log: `package-lifecycle-followup/data-ctest.log`.

After the final WS/WSS send lease and backpressure fixture stabilized, the anchored
seven-case network group passed 7/7 in 47.35 s: `ws_reader_close`,
`ws_client_session`, `ws_loopback`, `security_ws_client_protocol`,
`security_ws_handshake`, `websocket_concurrency_resilience` and `tls_recv_timeout`.
Final log: `package-lifecycle-followup/ws-ctest.log`. This extends the direct local
fixtures; it is not a fresh full independent network-interoperability matrix or
POSIX execution result.

## Remaining-gap repairs

Evidence for this follow-up is retained under
`build/test-out/package-final-gaps/`.

- Redis now shares a per-root logical payload/node budget across recursive arrays
  and both pooled command routes. The default limits are 64 MiB and 1,048,576
  nodes. The expanded direct regression passed with exact golden output, exit 0
  and empty runtime stderr under ARC/leak checks; logs: `redis-aggregate/`.
- Generic pool warmup now returns prior borrows after an acquisition exception and
  releases the remaining tail after a return exception, preserving the first
  concrete exception. The expanded probe reproduced nine failures before the fix
  and passed 39 checks afterward; logs: `pool-warmup/`.
- File text/byte writes and appends check buffered-close failure; copy checks read
  errors and output close. Windows normal/empty/Unicode/binary/copy checks passed.
  A separate actual WSL libc probe reproduced delayed ENOSPC at fclose after a
  successful small fwrite. At this stage the Linux Zan `/dev/full` cases were
  blocked by missing exception/runtime symbols; that original failure is retained
  in `file-write/`. The subsequent Linux runtime repair is recorded below.
- Linux cross linking now forwards explicit inputs/libraries. A production argv
  capture regression failed before the rebuild and passed after it, including
  paths containing spaces and archive-group order; logs: `appupdate-posix/`.
  That argv-only check did not establish Linux execution; the runtime/EH
  prerequisites were repaired in the subsequent follow-up below.

TLS client state/identity guards were also tightened. The direct regression log
`tls-policy/before-run.log` records eight failing checks before the fix;
`tls-policy/after-run.log` records 40 successful checks and `failures=0` afterward.
The wrapper initially reported failure because its case-insensitive `^FAIL`
matcher also matched `failures=0`; this must not be mistaken for a program failure.
Before full task closure, the TLS subagent was stopped by the platform's security
policy. Its additional signature-scheme correction and final regression closure
were not completed. In particular, the observed `0x0805` ECDSA alias remains a
known pending conformance issue; `0x0805` denotes RSA-PSS/SHA-384, not P-256/SHA-256.
No completed full TLS repair is claimed from the partial evidence. The unchanged
independent WS/WSS compatibility harness was then freshly compiled with ARC/leak
and strict-runtime checks and passed 17/17 across WS, TLS 1.2 and TLS 1.3, with
zero skips, empty server-error lists and successful fixture cleanup. Wrong CA and
hostname remained rejected. Results: `package-final-gaps/ws-interop/results.json`.

The final anchored data/file/compiler group passed 11/11 in 31.21 s, including
AppUpdate and existing file, stream, pool and Redis compatibility cases. Log:
`package-final-gaps/data-file-ctest.log`. No full tier was run. The separate existing
TLS/WS compatibility group passed 5/5 in 17.02 s (`tls12_rsa_pss_exchange`,
`tls_recv_timeout`, `redis_tls`, `ws_client_session`, `ws_reader_close`); log:
`package-final-gaps/tls-compatibility-ctest.log`. The expanded authentication
regression has direct-run evidence above but did not receive final registered
CTest closure after the platform interruption.

## Linux static-runtime repair

Evidence for this follow-up is retained under
`build/test-out/linux-package-runtime/` and `build/test-out/linux-runtime/`.

The x86-64 musl bundle was rebuilt from current main-tree runtime sources using
Zig 0.13.0. A real static C++ throw/catch executable selected matching target
startup, libc, libc++, libc++abi, libunwind and compiler-runtime inputs and ran
successfully. The refresh now selects exact inputs from that successful link,
validates the complete selection, preserves optional target libraries, and swaps
the completed target with rollback for catchable installation failures. An abrupt
kill during the swap can leave a recovery backup; this is not a filesystem
transaction. The final reviewed refresh completed successfully. The parser and
publisher passed 17 isolated tests; nested-source freshness detection passed four.

CMake now stages the missing embedded-resource and exception-runtime inputs. Linux
linking supplies the target crash object, the exception archives within the libc
rescan group and an EH frame header. Rebuilding strict C11 runtime sources also
exposed missing feature macros before the first system headers in file, timer
and crash units; those declarations are now enabled at their source entry points.

Actual execution then exposed a separate allocator defect: musl's internal
`calloc` called interposed `malloc`, received a Zan small block, and inspected it
as native mallocng metadata. The retained first runs crashed in
`__malloc_allzerop`. An ownership-aware wrapper now requests ordinary zeroing for
Zan blocks and preserves libc's probe for native blocks. This repairs the root
allocation contract rather than disabling the allocator or leak checks. The
standalone regression was then compiled for musl, explicitly statically linked
against the actual staged runtime and executed successfully: 32 rounds of dirty
small-block reuse, native large allocations and overflow rejection, with exact
PASS output and empty runtime stderr. Evidence: `calloc-bounded/`.

After that repair, actual Linux `io_write_failure` ran successfully with ARC,
leak and strict-runtime checks, exact golden output and empty runtime stderr
(`actual-fixed/`). It exercises repeated `/dev/full` delayed write/close failures,
a large write and `/proc/self/mem` read failure, as well as normal file operations.
AppUpdate reached its functional checks but the DrvFS working directory did not
support the executable-bit fixture: `/mnt/d` without metadata reports executable
permissions independently of chmod. Running the same main-tree ELF with only test
data under native Linux `/tmp` then passed all AppUpdate assertions, with empty
runtime stderr (`actual-native/`). That attempt remains an incomplete overall
acceptance result because two compiler source files changed during its input
capture; the result explicitly rejects the unstable snapshot.

The interruption child now calls fixture-local `_exit(23)`: ordinary C `exit`
runs the atexit leak reporter on deliberately abandoned child transaction objects
and does not accurately model abrupt process interruption. Parent leak checks,
child exit-code assertions, recovery and rollback assertions remain enabled.
The final separate runs `actual-closure-io/result.json` and
`actual-closure-app/result.json` both passed with **all input hashes stable**,
including compiler sources/executable, runtime sources/objects, stdlib, package
inputs and test files. Both programs returned 0, matched their exact normalized
goldens, and produced empty runtime stderr under ARC/leak/strict checks. AppUpdate
used native `/tmp` data and removed its working directory after inventory capture;
the file regression used its main-tree working directory because it does not test
executable-bit semantics. Earlier source-drift and WSL startup-timeout attempts
remain retained as failures, rather than being overwritten by this result.
The native follow-up also found an undeclared effective return type in async
method emission; it now resolves/concretizes the same result type used by the
resume path before registering discard cleanup. The first targeted run exposed
a separate stale Windows timer object: its build dependencies omitted the nested
task implementation. After regenerating that dependency and rebuilding,
`compiler_linux_link_inputs`, `async_direct_aggregate_return`,
`async_struct_return` and `async_spawn_result_release` passed 4/4 in 5.05 s.
The argv regression also normalizes native Windows path separators before
comparing exact target archive paths. Windows `io_write_failure` and `app_update`
passed in the initial targeted run. Failure and repaired logs are retained as
`ctest-initial-failure.log` and `ctest-repaired.log`; no full test tier was run.

The remaining generic-async failure came from ANF receiver staging: a native
static type such as `Console` has no ordinary binder declaration and was
incorrectly hoisted as a runtime value before an awaited argument. ANF now uses
builtin static-type metadata when no binder or resolved user-call symbol exists.
Actual instance receivers still evaluate before their arguments, including a local
instance named `Console` that the awaited argument reassigns. The unchanged
`case_async_generic_method` and new `case_async_builtin_static_receiver` both
passed on 2026-11-11 (2/2, 2.81 s). The latter checks exact output `42`,
`original:7`, `replacement:9`. Evidence: `ctest-anf-final.log`. This focused result
does not establish all async receiver or generic-lowering paths.

Owned scratch scripts and the failed DrvFS fixture were archived as
`runner-and-failed-fixture.zip` before cleanup. `cleanup.json` records removal
of this task's scratch directory and 16 disposable probe/case artifacts.

The common refresh path supports the existing ARM64/RISC-V bundles, but this
follow-up rebuilt and executed only x86-64 musl; no ARM64, RISC-V or macOS runtime
acceptance is implied. Upstream Zig 0.13.0 license notices accompany the newly
bundled target archives in `toolchain/linux-musl/`.

## MySQL TLS capability consistency

SSLRequest and the encrypted HandshakeResponse41 now serialize one shared client
capability value. Previously the encrypted login omitted `CLIENT_SSL` despite
advertising it before the TLS upgrade. The existing loopback TLS fixture now checks
that the encrypted login retains that flag and all four capability bytes match
SSLRequest. `case_mysql_tls` passed (1/1, 11.85 s); evidence is retained as
`build/test-out/package-durability/mysql-tls.log`. This is protocol-fixture evidence,
not an Oracle MySQL authentication or real-server database TLS matrix, and the
previous mismatch did not establish a plaintext downgrade.

## Explicit filesystem persistence barriers

`File.FlushToDisk` now checks the result of `FlushFileBuffers` on Windows and
`fsync` on POSIX for an existing regular file. It does not create or truncate the
path. POSIX `Directory.FlushToDisk` separately synchronizes directory entries;
Windows directory barriers and WebAssembly barriers report
`PlatformNotSupportedException`. Operation failures report `IOException`.
Writers must first flush/close their userspace buffers; Windows file barriers
also require write access.

The new conformance cases check Unicode/space paths, binary and empty-file
preservation, missing/wrong-type failures, repeated synchronization and handle
release. Windows additionally checks exclusive sharing denial. Unsupported
WebAssembly branches assert the explicit exception; no WebAssembly execution
acceptance is established here.

The first Windows batch passed four existing regressions (`app_update`,
`file_move`, `io_write_failure`, `pal_filesystem_smoke`) but the two new cases failed
leak checking. Linux reproduced the same failure. Interface dispatch retained
owned getter receivers on normal calls but did not register them for exception
unwinding: throwing methods or arguments skipped the release. The compiler now
arms the receiver before evaluating arguments and pops its exception slot after
argument slots on both dispatch paths. Static cleanup itself was already present.
The dedicated `arc_interface_receiver_throw` regression covers single/multiple
implementations, method/argument throws, normal return and borrowed receivers.

After the compiler fix, the two barrier cases and receiver regression passed
Windows CTest (3/3, 5.62 s). All three also passed as x86-64 musl executables under
WSL Ubuntu with `--check-leaks --arc-guard --strict-runtime`: exact normalized
golden stdout, empty runtime stderr, unchanged executable hashes, stable recorded
compiler/runtime-source/stdlib/test inputs and successful owned `/tmp` cleanup.
Each main-tree ELF ran directly from an owned native Linux working directory;
no source or binary tree was copied. These results establish API behavior and
exception cleanup, not storage fault injection. ARM64, RISC-V, macOS and WebAssembly
execution were not part of this batch.

Evidence is under `build/test-out/package-durability/`: initial Windows failure in
`windows-before-receiver-fix.log`, initial Linux failure in `linux/`, final Windows
run in `windows-final.log`, and final Linux command logs, input hashes and verdict
in `linux-final/result.json`. `runner-and-probe.zip` preserves the final runner
and minimal probe; `cleanup.json` records removal of the owned scratch files and
three disposable Linux executables.

These primitives have not been integrated into AppUpdate's preparation, commit,
rollback or cleanup ordering. Neither successful API calls nor subsequent reads
establish power-loss recovery or physical-media guarantees.

## POSIX update-path links and native allocation provenance

The POSIX implementation of `File.IsReparsePoint` previously returned `false`
for every path, so AppUpdate's existing leaf-and-ancestor checks did not reject
symbolic links there. It now delegates to the existing non-following `IsSymlink`
query, including dangling links. The public documentation describes the platform
semantics and the need to check ancestors and control concurrent path mutation.
This boolean query does not establish authorization to open or modify a path.

The new `file_reparse_point` regression checks ordinary/missing paths and POSIX
file, directory, dangling and self-referencing links. The expanded `app_update`
regression checks linked destination files/directories, dangling destinations,
lock/state/backup directories, and linked recovery phase/snapshot files. Rejected
updates preserve the link, recovery journal and an owned external sentinel;
restoring valid recovery files allows rollback to restore every original file.
Windows runs the applicable platform contract and existing transaction cases;
the POSIX symbolic-link scenarios execute on Linux.

The initial Linux batch passed the file-link contract but crashed in AppUpdate
cleanup. The retained native backtrace traced the failure to live-object registry
growth: static musl's `__real_calloc` can call wrapped `malloc`, producing slab
memory that registry growth later passed to `__real_free`. Internal metadata now
uses overflow-checked `__real_malloc` plus explicit zeroing, paired with native
free, at the registry and cycle-detector map allocation sites. User-facing
`calloc` behavior is unchanged. The new `live_registry_growth` regression retains
and verifies 100,000 strings, releases them, then repeats the workload.

After the runtime fix, Windows CTest passed all three focused cases (3/3,
7.60 s). The same three passed as x86-64 musl executables under WSL Ubuntu with
`--check-leaks --arc-guard --strict-runtime`, exact normalized golden stdout,
empty runtime stderr, unchanged executable hashes, stable compiler/runtime/
stdlib/package/test inputs and successful owned native `/tmp` cleanup. They ran
from the main tree without copied source or binary trees. No ARM64, RISC-V,
macOS or WebAssembly execution is implied.

Evidence is retained under `build/test-out/appupdate-links/`: `linux/` records
the initial failed batch, `gdb.stdout.log` records the native backtrace,
`failed-binary-hashes.json` identifies the failed executables, `windows-final.log`
records the final Windows batch, and `linux-final/result.json` records the final
Linux verdict with companion command logs and input hashes. Build logs record
the Linux runtime refresh and compiler rebuild. `helpers.zip` preserves the
runner and diagnostic helper; `cleanup.json` records removal of owned scratch
files and disposable Linux executables.

Concurrent hostile directory replacement, hard-link isolation, whole-directory
instantaneous visibility and power-loss recovery remain outside this evidence.
At this checkpoint the persistence barriers still required transaction integration;
the later integration and its separate acceptance appear below.

## Repeated recovery after partial rollback and cleanup

A follow-up review found no confirmed process-interruption or throwing-I/O defect
in the current transaction ordering. `File.WriteAllText` checks both writes and
close, and `File.Move` checks rename failure; hypothetical silent phase writes
are not a finding against these implementations. Production transaction code was
not changed in this follow-up.

`app_update` now reconstructs a partial rollback after a destination is restored
but before its caller backup is restored. Recovery must finish restoring original
files and caller backups, remove a newly introduced destination, and succeed
again when called repeatedly. Windows additionally makes the caller backup
read-only so an actual restoration attempt fails after the destination has been
restored; the journal and snapshot must survive, and recovery must succeed after
write access is restored.

Two committed cleanup boundaries are reconstructed separately: after deletion of
one original snapshot, and after deletion of all inventory except the committed
phase (including the journal). Recovery uses a different, smaller next manifest,
repeats successfully, removes state and preserves every committed payload hash.
These deterministic state reconstructions complement the existing abrupt child
process exit during publication; they are not new process-kill or power-loss
fault-injection runs.

Windows `case_app_update` passed (1/1, 7.17 s test time; 7.27 s total). The same
expanded case passed on x86-64 musl under WSL Ubuntu with
`--check-leaks --arc-guard --strict-runtime`, exact normalized golden output,
empty runtime stderr, unchanged executable hash, stable recorded inputs and owned
native temporary-directory cleanup. The Windows read-only failure scenario is
platform-specific and was not executed on POSIX. Evidence is retained under
`build/test-out/appupdate-retry/`: `windows-final.log`, `linux-final/result.json`
and companion command/input logs, `helpers.zip`, and `cleanup.json`.

At that checkpoint the installer did not integrate file/directory persistence
barriers. The subsequent integration is recorded below; actual power-loss
acceptance, whole-directory instantaneous visibility and the listed path-mutation
boundaries remain open.

## AppUpdate persistence ordering and barrier failure recovery

Transaction format 3 adds a synchronized `preparing` marker and orders staged
payloads, immutable snapshots and journal synchronization before `publishing`.
Replacement files are synchronized before rename; destination/source directories
are synchronized before commit. Rollback synchronizes matching files and restored
modes, or the parent of a removed new file. Inventory deletion precedes synchronized
phase deletion and state-directory removal. Newly created parent chains are also
synchronized. POSIX barrier failures propagate. Windows has file barriers only;
its directory namespace durability remains unsupported. Format 2 retains its
original process-interruption contract, with no retrospective power-loss claim.

A visible commit rename followed by a barrier failure returns `false`, retains
recovery material and requires recovery to confirm the marker before cleanup.
Confirmed commits still return `true` if only later cleanup fails. Each transaction
owns an internal persistence collaborator; the regression records real operations
and injects failures before and after preparation, publication, commit, recovery
material and cleanup barriers. It checks retained snapshots, unchanged pre-rollback
payloads, repeated recovery, missing-file removal, matching-file synchronization,
legacy publishing/committed state and phase/journal version mismatch rejection.

The initial fault-injection executable exposed a compiler defect: an empty
`Substring` branch returned a raw interned C string, while exception cleanup expected
an ARC string with an aligned header. The original executable failed with
`0xC0000005` in `__zan_eh_tmp_unwind`. The compiler now emits an ARC string literal
for that branch. A focused regression checks the header/alignment invariant and
24 nested throwing-argument cases; the unfixed compiler fails the invariant
independently of the layout-dependent crash.

Windows closeout passed 5/5 in 21.81 seconds: `case_app_update`,
`case_app_update_persistence`, `case_substring_empty_exception_owner`,
`runtime_substring_start_oob` and `runtime_substring_window_oob`.
The three conformance cases also passed on x86-64 musl under WSL Ubuntu with
`--check-leaks --arc-guard --strict-runtime`, exact normalized golden output,
empty runtime stderr, stable recorded source/runtime inputs and unchanged binary
hashes. The owned Linux temporary directory was removed. The first Linux preflight
correctly stopped before compilation when a test source changed; that rejected run
is retained separately and is not counted as a passed run.

Evidence is retained under `build/test-out/appupdate-persistence/`, including
`windows-final.log`, `linux-final/result.json` and companion command/input logs,
`linux-preflight-input-drift/`, the original crash/binary archive and deterministic
compiler regression evidence under `debug/`. Eight additional separate processes
running the fixed compiler regression passed with empty stderr.

These checks establish software barrier ordering, error propagation and recovery
behavior. They do not establish actual power-loss recovery or filesystem/device
write-cache behavior on any platform.

## ZanDb persistence and backup validation (2026-11-12)

RecordTable now checks WAL append/sync, replay writes, tail repair and checkpoint
results. Blob content and its header synchronize before WAL publication; table,
blob and index checkpoints must succeed before retiring the WAL. A failed handle
retains its first error and rejects later writes, while Close releases resources.
Successful open reaffirms file and supported parent-directory barriers, including
retained files after a failed open. Empty tables extend to their declared aligned
data offset before publication.

Windows preflight probes passed `zandb_recordtable_io_failures` (121 checks),
`zandb_recordtable_recovery` (17) and `zandb_recordtable` (79), all with exit 0,
exact normalized golden output and empty runtime stderr. The three-mode backup
probe passed 48 checks, including buffered WAL contents, updates/deletes,
revision preservation and write isolation. Its successful stdout and generated
golden have identical SHA256. The WAL-directory probe also passed its Windows
compatibility contract; the subsequent Linux directory-barrier results are recorded below.
Evidence is under `build/test-out/zandb-record-io/`, `zandb-copy-debug/` and
`zandb-wal-dir/`.

The expanded backup matrix also passed all 14 before/after barrier-failure
branches, the real sidecar-copy obstruction, cleanup/source-usability checks and
normal-export ordering assertions, with exact golden output and empty runtime
stderr. Evidence is under `build/test-out/zandb-backup-io/`. Windows event-path
assertions use the same normalized target path as the production implementation.
The probe exposed repeated terminal exception cleanup in the compiler. The fix
separates cleanup-phase resume from selected-handler propagation and releases the
TLS-owned exception and static resources on terminal failure. Its strict formal
probe passed caller-catch controls and three terminal child-process shapes; the
`-g` minimal reproduction now runs cleanup exactly once and reports the original
exception with the expected exit 1 and empty stderr. Native evidence and verified
compressed IR are under `build/test-out/zandb-backup-io/debug/`.

The final anchored Windows CTest batch passed all 11 cases in 62.91 seconds:
`arc_release_tls_static_mutex`, `selfhost_sync_finally_payload`,
`sync_catch_cleanup_finally`, `sync_finally_terminal_unwind`, `zandb_atomic`,
`zandb_backup`, `zandb_backup_persistence`, `zandb_recordtable`,
`zandb_recordtable_io_failures`, `zandb_recordtable_recovery` and
`zandb_wal_directory` (each with the `case_` prefix). The wrapper exited 0 with
`TEST_OK tier=targeted`; evidence is
`build/test-out/zandb-persistence/windows-final.log`.

The initial Linux x86-64 musl strict attempt was incomplete. `zandb_atomic` compiled and
ran successfully with exact golden output and empty stderr. `zandb_backup`
compiled, but a host-side `wslpath` timeout occurred before execution; the other
six cases were not compiled or run. All 4,029 input hashes and generated-binary
hashes stayed unchanged. Subsequent bounded WSL status and `/bin/true` queries
also timed out, so this does not establish a backup runtime failure or a Linux
pass. The shared gate was released and the owned Windows binary directory was
removed. Cleanup of two owned Linux temporary directories is unconfirmed; exact
paths and attempted cleanup are retained in
`build/test-out/zandb-persistence/linux-final/cleanup.json`, alongside results,
read-only diagnostics and the archived executed helper. A later `/bin/true`
health check briefly recovered, but exact owned-directory cleanup again timed
out with no output; no new validation run started. A comparison with the original
input manifest found only an unrelated concurrent `Zan.Gui/Widget/Flex.zan`
change (apart from the two deliberately edited runner helpers), with no compiler,
ZanDb, EH or toolchain changes. Since the old snapshot has drifted, recovery must
first confirm cleanup of the two owned directories, then establish a new input
snapshot and run all eight planned Linux cases; the old atomic pass remains
historical evidence rather than acceptance of that new snapshot. Recovery commands
and precise owned-path cleanup are documented in
`build/test-out/zandb-persistence/linux-continuation-preparation/README.md`;
`helpers.prepared.zip` contains the syntax-checked recovery runner. Its Windows
scratch directory was removed at that closeout. In the subsequent recovery
attempt, both old Linux paths were removed or already absent and their absence
was confirmed with exit 0 and empty stderr; evidence is
`build/test-out/zandb-persistence/linux-recovery-attempt-02/previous-owned-cleanup.json`.
The subsequent fresh eight-case attempt again passed `zandb_atomic`, then failed
before invoking the backup binary. WSL reported a timeout waiting for the distro
process to exit, followed by a missing freshly-created `/tmp` working directory.
This is launch-stage evidence, not a backup runtime result. Only two GUI files
changed in its full input manifest; database, EH, compiler and toolchain inputs
were stable. Its two new Linux temporary paths were later confirmed already
absent by a single bounded WSL check (exit 0, empty stderr), recorded in
`build/test-out/terminal-linux-root/fresh8-cleanup-confirmation.json`. These are
distinct from the original two paths already cleaned above. Failed-run evidence
is under `build/test-out/zandb-persistence/linux-fresh8/`.

The single-session runner compiled all eight binaries, entered WSL once, and
ran all eight with `--check-leaks --arc-guard --strict-runtime`, `ZAN_RT_HARD=1`
and `LC_ALL=C`. All seven ZanDb cases passed: `zandb_atomic`, `zandb_backup`,
`zandb_backup_persistence`, `zandb_recordtable`, `zandb_recordtable_recovery`,
`zandb_recordtable_io_failures` and `zandb_wal_directory`. Each exited 0 with exact
LF golden output (only golden CRLF normalized), empty runtime stderr and unchanged
binary hashes. All 4,030 full input entries were unchanged; no auxiliary GUI
exclusion was needed. This includes actual supported POSIX directory barriers,
the 14-branch backup fault matrix and record-table failure/recovery checks.

The eighth case, `sync_finally_terminal_unwind`, compiled but crashed with signal
11 before producing stdout (exit -11). Native debugging located the first fault
in `memcpy` while copying captured process output: byte-wise pointer decoding
used `int * nint`, and the compiler omitted `nint` from numeric promotion,
truncating the product to 32 bits before sign extension. A minimal probe and
pre-fix IR confirmed that reversing operand order avoided the truncation. This
is an actual compiler defect, distinct from the prior WSL launch failures. The
checker/inference and corresponding selfhost promotion rules now retain `nint`
after the existing `long` precedence. The dedicated `nint_mixed_arithmetic`
regression covers both operand orders, high pointer bytes, signed comparisons,
long mixing and conditional-result width. After a successful compiler build,
both this regression and the unchanged `sync_finally_terminal_unwind` passed on
Windows x86-64 and Linux x86-64 musl: all four direct runs exited 0, matched the
exact golden and produced empty stderr. Results and native root-cause evidence
are under `build/test-out/terminal-linux-root/` (`windows-results.json` and
`linux-results.json`). Consequently the original eight-case batch remains a
recorded exit 1, while its failed case has a successful post-fix Linux result. The worker's owned Linux root and
Windows binary directory were removed and confirmed absent. Full per-case
results, command/link logs, crash stderr, input manifests and cleanup evidence
are under `build/test-out/zandb-persistence/linux-single-session/`.

Windows directory namespace durability and actual power-loss behavior remain
outside this evidence. ZanDb has the Windows and focused seven-case Linux results
above, and the companion exception regression now has the post-fix Windows/Linux
results recorded above. The final anchored Windows CTest batch passed
`case_nint_mixed_arithmetic`, `case_process_safe_exec` and
`case_sync_finally_terminal_unwind`. `case_ffi_nint_handle` failed compilation
with E2024 on same-type `List<T>` assignments in `ReadOnlyCollection.zan:17/51`,
exposing a declaration-scope defect after generic invariance was tightened; the batch
exited 8 with 3/4 passing in 8.74 seconds. The generic declaration's `T` had been
resolved as the fixture's unrelated global class `T`. The repair retains generic
invariance and adds global-name collision and method/class shadowing coverage to
`type_param_same_name`. Declaration-bound signatures and body type references must
retain their open type parameters until IR applies the current method/class
arguments; resolving them again in the caller/root scope loses that identity.
Receiver substitution must also respect method parameters shadowing class
parameters. After the repair, direct compilation, execution and golden comparison
passed for `ffi_nint_handle` and the enhanced `type_param_same_name`, with empty
runtime stderr. The negative probes still reject `List<int>` to `List<Box>`
assignment/arguments (E2024/E2035) and `HostInvariantBox<int>` to
`HostInvariantBox<string>` returns (E2024). The final anchored Windows CTest
verification passed `case_ffi_nint_handle` and the modified
`case_type_param_same_name`: 2/2, wrapper exit 0, 4.64 seconds. The earlier 3/4
batch remains recorded in `build/test-out/terminal-linux-root/windows-final.log`;
the repaired controls are archived in
`build/test-out/generic-identity-root/ctest-final.log` (original targeted log:
`build/test-out/targeted-20261112-124524.log`). Direct probes, root-cause evidence
and hashes are retained in the same evidence directory, including `closeout.json`.
The declaration-scope skill correction was synchronized to all three copies with
identical SHA256. Its owned scratch directory and temporary binaries were removed
and confirmed absent, and scheduler lease `9ebda504fd78` was finished. This generic
repair has focused Windows evidence; Linux revalidation and broad test tiers were
not run for it. The terminal/numeric
probes, loose IR and owned scratch were archived with verified hashes and removed;
their skill copies were synchronized. Selfhost promotion source was updated, but
bootstrap was not run in this round.

## Remaining acceptance boundaries

- SQL Server TCP/TDS integration was not run: no suitable isolated TCP service is
  available on this host. Installed SQLCMD/LocalDB does not establish this coverage.
- Oracle MySQL versions and additional authentication modes need their own matrix.
- Database TLS, sustained concurrent database workloads and long-duration
  application soak are not established by these runs. PostgreSQL and MariaDB
  whole-instance stop/restart have bounded local evidence above; this does not
  establish network partition, failover or crash-consistency behavior.
- The HTTP bounded request loop is a regression workload, not a production capacity
  measurement. HTTP/2 interoperability here is cleartext prior-knowledge HTTP/2;
  it does not establish HTTP/2-over-TLS ALPN interoperability.
- Most package-level Zan fixtures ran on Windows. The focused Linux file,
  AppUpdate and seven-case ZanDb runs above are separate evidence; WSL hosting
  external database services does not establish execution of their database
  drivers on Linux, macOS or devices.
  The Linux C runtime reactor regression likewise does not expand driver coverage.
- Package limitations documented in [PACKAGES_SECURITY.md](PACKAGES_SECURITY.md),
  including unavailable mTLS/required OCSP and the lack of whole-directory atomic
  visibility or power-loss durability during updates, remain. Process-interruption
  recovery and rollback now have the focused evidence recorded above.
