# Package service acceptance

These are opt-in integration tests against independent implementations. Run from
the main source tree; do not copy packages or the standard library. A passing
fixture, a service preflight, and a passing production release are different results.

## Shared build gate

The integration runners compile fixtures with the current `build/zanc.exe`. Wrap a
full run in the repository's default **build** lock. Do not give each suite a
separate lock name: they share the compiler and native libraries.

```powershell
pwsh -NoProfile -File scripts/build_gate.ps1 -Command '& python -I tests/integration/postgres_acceptance.py'
```

The runners return nonzero on failure. Missing prerequisites must be reported as
unavailable/skipped, never as an executed passing test. Review the JSON report and
per-case logs in `build/test-out/`, not only the shell status. Generated executables
belong to `build/cases/`.

## PostgreSQL

Requirements: Windows Python with `psycopg2`, WSL Ubuntu with PostgreSQL 16 server
tools, and the current Zan compiler. The runner creates its own temporary cluster,
random loopback port, database, and SCRAM credentials. It stops the instance and
removes its own files in `finally`; it does not use system clusters or application
credentials. The Python driver independently checks authentication and persisted
results.

```powershell
# Environment/lifecycle check only: does not compile or execute Zan.
python -I tests/integration/postgres_acceptance.py --preflight-only

# Complete test: run inside the shared gate above.
python -I tests/integration/postgres_acceptance.py

# Add bounded concurrent transactions and whole-instance restart recovery.
pwsh -NoProfile -File scripts/build_gate.ps1 -Command '& python -I tests/integration/postgres_acceptance.py --stress-workers 4 --stress-loops 100 --restart'
```

The fixture exercises bound values, NULL versus empty text, transaction commit and
rollback, savepoints, aborted commits, SQL-error recovery, and connection-pool
behavior. The async parameter case also checks a typed ORM page, including the
matching total and entity mapping, and the async ORM mutations described below.
A preflight result does not establish any of those Zan driver behaviors.
The optional stress mode uses separate Zan processes, each with its own size-one
pool. Four workers and 100 loops execute 400 transaction writes: 264 commit and 136
roll back. An independent psycopg2 connection checks the complete expected row set,
including values and absent rollback keys. This does not establish shared-pool
thread safety or production throughput. Restart mode keeps the same Zan process
and pool across a fast stop/start of its private server, observes an in-flight query
before stopping, and checks bounded failure, reconnection and independently visible
recovery writes. The JSON records which modes actually ran.

Use `--help` for distribution, WSL user and PostgreSQL-tool path overrides.

## MariaDB / MySQL protocol

Requirements: WSL Ubuntu with MariaDB server/initialization/client tools and Python
3, Windows Python, and the current Zan compiler. The runner creates a temporary
MariaDB data directory and native-password account on a random loopback port. The
server's own CLI checks setup independently. It operates only on that private
instance and cleans it up on exit.

```powershell
python -I tests/integration/mysql_acceptance.py --preflight-only
pwsh -NoProfile -File scripts/build_gate.ps1 -Command '& python -I tests/integration/mysql_acceptance.py --compile'

# Add four independent workers and same-process/same-pool instance restart.
pwsh -NoProfile -File scripts/build_gate.ps1 -Command '& python -I tests/integration/mysql_acceptance.py --compile --stress-workers 4 --stress-loops 100 --restart'
```

The parameter case also exercises typed async ORM pagination and lookup, checking
bound values, the matching total, and SQL NULL versus empty text. In both database
suites, the async parameter case runs `UPDATE` and `DELETE` with a bound SQL
condition and two typed `Func` predicates. It verifies the exact changed and
untouched rows, the bound update value, and zero affected rows when nothing
matches. Reusing the update builder after the matching key changes checks for
stale key bindings. The mutation rows are rolled back. A separate MariaDB
connection and PostgreSQL's independent Python driver check that the original
parameter rows are still the only ones persisted by these cases.

Stress mode gives each worker its own pool. With four workers and 100 loops, it
expects 400 writes split into 200 commits and 200 rollbacks; the native CLI checks
the complete persisted row set and values. Workers are launched before waiting,
without a synchronized start barrier; transaction overlap is not measured. Restart
mode retains the same Zan
process and pool, detects a broken old lease, checks connection attempts while the
private server is down, then verifies a new authenticated connection and recovery
queries and existing-data persistence after restart. Check the report's mode and
results before claiming coverage.

This is evidence for the tested MariaDB version and authentication mode. It does
not establish compatibility with every Oracle MySQL version or authentication
plugin. SQL Server requires its own TCP service and separate acceptance.

## HTTP, TLS and HTTP/2

Requirements: Python 3, OpenSSL, and optionally `h2==4.3.0` with its dependencies for
HTTP/2. Install optional dependencies in an isolated environment; do not modify the
global Python installation. Invoke that environment's Python with `-I`.

```powershell
pwsh -NoProfile -File scripts/build_gate.ps1 -Command '& python -I tests/integration/net_interop_test.py --loops 128'
```

Python's HTTP/SSL servers receive requests from the production Zan client. The TLS
fixture supplies a freshly generated root CA explicitly and retains verification;
it tests TLS 1.2 and 1.3, including wrong CA, wrong hostname and missing intermediate
certificates. HTTP tests cover binary NUL, bounded gzip and framing failures,
recovery after disconnect, and a bounded request loop. The independent Python `h2`
client exercises the production Zan HTTP/2 server's multiplexing, binary bodies,
flow control, and reconnection.

Network results are stored in a unique `build/test-out/net-interop/<run-id>/`
directory. Exit codes: 0 means every selected item passed, 1 means failure, and 2
means at least one prerequisite was skipped without another failure. Zan processes
force `ZAN_RT_HARD=1`; runtime errors, unhandled exceptions, ARC guards and leak
diagnostics also fail the run even with exit code zero. Timeouts and interrupts
clean up the owned process tree, including descendants whose parent has exited.
Harness-only checks run with `python -I tests/integration/test_net_interop_harness.py`.
The bounded loops are regression workloads, not a long-duration capacity benchmark
or a measured production throughput guarantee.

## Template linking

The repeatable acceptance runner enumerates the same 39 two-level templates as
CMake plus `examples/html_gallery/zan.proj` (40 projects in the current tree).
It delegates each exact selection to `tests/run_templates.cmake`, preserving
complete EXE/DLL linking. Run the entire runner inside one shared build gate:

```powershell
& ./scripts/build_gate.ps1 -Name build -Script {
    & python -I tests/run_template_acceptance.py
}
```

The runner acquires no locks itself. It defaults to two workers and accepts only
`--workers 1` or `--workers 2`. Each project has an independent working directory;
compiler timeout is 600 seconds and outer CMake timeout is 660 seconds by default.
Override with `--timeout` and `--outer-timeout`. The outer timeout terminates the
project's process tree. Any failed, timed-out or interrupted item makes the runner
exit nonzero.

Use `--filter` with a case-sensitive **Python** regex to select remaining projects,
or `--list` to preview without executing or writing anything:

```powershell
python -I tests/run_template_acceptance.py --list
python -I tests/run_template_acceptance.py --filter '^gui/' --list
# Inside the same external build gate:
python -I tests/run_template_acceptance.py --filter '^gui/'
```

An empty selection fails. Every selected project rebuilds: there is no automatic
resume or reuse of prior passes. Exclude completed items explicitly only after
checking the prior result and nonempty artifact, and only while the build inputs
remain unchanged. Results go to a new run directory under
`build/test-out/template-acceptance/` (or a new `--output` directory). It contains
an atomically updated `results.json`, an append-only `journal.jsonl`, and per-item
CMake logs plus compiler logs/results. `complete` means every selected item reached
a terminal status; inspect `failed` and each row's status to determine success.

The underlying single-batch template harness also supports bounded linking in
partitions. It substitutes template names and preserves project target handling.
Use the shared build gate around the following command:

```powershell
cmake -DZANC=D:/project/zan-lang/build/zanc.exe `
  -DTEMPLATES=D:/project/zan-lang/templates `
  -DEXTRA_PROJECTS=D:/project/zan-lang/examples/html_gallery/zan.proj `
  -DSTDLIB=D:/project/zan-lang/stdlib -DDRIVER_DIR=D:/project/zan-lang/build `
  -DWORK=D:/project/zan-lang/build/templates_build -DEXE_EXT=.exe `
  '-DFILTER=^gui/' -DCOMPILE_TIMEOUT=600 -P tests/run_templates.cmake
```

Adjust the repository paths for the checkout. `FILTER` is a case-sensitive CMake
regular expression over paths such as `gui/gui-wechat`; an empty selection fails.
The timeout applies to each compiler invocation. Each selected project gets
`stdout.log`, `stderr.log` and `result.json`; `WORK/results.json` describes the
current partition. Selected projects always rebuild. A pass requires exit code 0
and a nonempty output artifact. Unselected project directories are retained, so
save batch reports separately when running several partitions. A successful link
does not prove interactive behavior of a GUI or game.

Harness-only regressions do not require a shared compiler build:

```powershell
python -I tests/test_run_templates.py
```
