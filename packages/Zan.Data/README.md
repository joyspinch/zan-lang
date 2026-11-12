# Zan.Data

Data access for Zan, extracted from the standard library. Namespaces are
unchanged. Make the package visible to zanc through project `packages/`,
`.zan-packages/`, or a toolchain-relative `packages/` store.

- `System.Data` — `DbConnection` / `DbPool` / `DbResult` core, connector and
  executor interfaces, tracing wrapper.
- `System.Data.Orm` — ZanSQL ORM. `[DbTable]` models plus
  `db.Select<T>()` fluent queries; entity facades are built from runtime
  reflection (`TypeInfo`), so user code never spells `OrmMeta`/`OrmSelect`
  directly. Navigation is **bidirectional**:
  `Include(p => p.user)` loads the parent for each returned child, and
  `Children(parentId)` / `ChildrenOf(parentTable, parentId)` fetch the
  children of a parent row. The reverse relation derives its foreign key
  (`<navField>Id`) from the child's navigation declaration. `IUnitOfWork`
  also offers **savepoints**
  (`CreateSavepoint` / `RollbackToSavepoint` / `ReleaseSavepoint`, plus async
  twins) so a transaction can carve out a rollback-able sub-section; the
  dialect follows the backend (SQL Server uses `SAVE TRANSACTION`, the Oracle
  family has no `RELEASE`), and savepoint names are validated as identifiers
  because they land in the SQL text.
- Wire-protocol drivers (no ODBC needed): `System.Data.MySql`,
  `System.Data.Postgres` (pure-Zan PostgreSQL wire protocol; no libpq dependency),
  `System.Data.SqlServer` (TDS), `System.Data.Sqlite` (embedded),
  `System.Data.Firebird`, `System.Data.TDengine` (REST), plus the generic
  `OdbcConnector`.
- `System.Data.Redis` — client and connection pool.
- `System.Data.ZanDb` — embedded pure-Zan document/KV store (no native
  dependencies).
- `System.Data.Excel` — Xlsx spreadsheet reader.

Server-shaped code pulls these namespaces on demand exactly as before; a
program that never references them compiles without this package.

### ORM query execution

Prefer expression-tree conditions or bound builder methods such as `WhereEq`
for large tables. An explicit `Func<T, bool>` passed to `Where` runs on the
client. The driver materializes the complete SQL candidate result, then the
query filters before applying `Skip`/`Take`. This returns exact pages even
when matches are sparse, but memory and network use scale with the candidate
set. `Count` and `ToPageResult` totals ignore pagination; delegate-based scalar
aggregates also use the full filtered set. Prefer SQL predicates and aggregates
when the candidate result would be large.

Terminal operations use a snapshot of the query and its bound parameters.
`First`, `ToPageResult` and `ToSeekResult` do not change the original builder.
Async terminals call the executor's async methods for rows, totals and related
entities. Whether the database I/O itself yields depends on the driver.

`SyncStructureAsync<T>()` and the typed `Query<T>()` alias are supported through
`IDbExecutor`. The compiler now distinguishes applicable raw SQL members from
generic extensions and recognizes async extensions when lowering `await`.
The SQLite regression verifies async schema creation, typed queries and raw SQL
queries together.

Delegate conditions on `Update<T>()` and `Delete<T>()` are recorded by `Where`;
they read candidates only when the terminal executes. Synchronous terminals use
`Query`, and async terminals await `QueryAsync`. Multiple delegate conditions
intersect with the bound SQL conditions. Each execution snapshots builder state
and re-evaluates candidates; zero matches produce a false condition. Matched
primary keys remain bound parameters, and composite-key alternatives are grouped
under the existing SQL condition. SQLite entities without a primary key use
`rowid`; other providers require primary-key metadata for delegate mutations.
The existing 2,000-key-parameter limit remains for SQLite rowid and composite-key
mutations. Candidate reads and writes are separate statements: use an appropriate
transaction when their consistency matters. Snapshots isolate builder changes;
they do not freeze captured delegate state or provide transaction isolation.

### Wire receive budgets

The synchronous and asynchronous MySQL readers limit one logical response message
including all continuation payloads to 64 MiB, excluding packet headers and the
array's trailing NUL slot. Login and TLS-upgrade requests advertise the same
budget. Each header is checked before allocating or reading its payload. Oversize
or incomplete messages fail the read and close the session; accumulated prefixes
are never returned as complete packets. The bound applies to one message, not to
the total rows materialized by an entire query.

Redis RESP lines allow 65,536 content bytes, including the type prefix and
excluding CRLF. Bulk trailers must be CRLF, numeric headers must fit signed
Int64, and arrays allow at most 64 nesting levels. Existing limits remain:
512 MiB per bulk value, 1,048,576 elements per array, and a 1 GiB receive buffer.
Malformed, truncated or overlimit responses close the session and retain a
`protocol error:` for subsequent commands. Ordinary Redis error replies remain
usable responses.

Each root reply also shares a cumulative budget across all nested arrays:
64 MiB of logical payload bytes and 1,048,576 reply nodes by default. Bulk,
simple-string and error values count their wire bytes; integers count eight bytes.
Arrays, scalars and nil each consume one node. Child counts and bulk lengths are
admitted before allocation or payload reads. `SetReplyLimits(maxPayloadBytes,
maxNodes)` configures positive limits before commands; invalid or busy changes
return false. A pooled client applies these limits for each borrowed command and
restores the physical client's previous configuration afterward. Direct pool users
can configure an acquired client. These logical budgets bound payload and node
counts separately; allocator overhead and temporary receive buffers are additional.

### Pool cleanup failures

`DbPool` releases connection accounting even when physical close throws. Closing
it attempts every idle connection, wakes waiters, and then rethrows the first
cleanup exception with its original type. Borrowed connections close when returned.
`WarmupAsync` also releases earlier borrows if acquisition fails and continues
releasing the tail after a return failure, preserving the first exception without
retrying the failed return.
`FirebirdPool` also evicts a session when rollback or replacement-transaction reset
returns false or throws; a connected socket alone does not make that session safe
to reuse.

### Transaction scope completion

`TransactionScope` makes one commit or rollback attempt. Check the boolean from
`CommitAsync`, `RollbackAsync` and `DisposeAsync`; `false` does not mark a commit
successful. A failed explicit attempt is not retried by `Complete` or disposal.
Repeated asynchronous disposal returns the recorded result and does not repeat
cleanup. A transaction failure takes precedence over a cleanup exception;
cleanup-only exceptions still propagate and leave the recorded result false.
The underlying `IUnitOfWork.Dispose` retains responsibility for its own unfinished
transaction cleanup. A scope is intended for one serialized transaction flow;
these guarantees do not make overlapping operations on it safe.

### Migrating from the legacy ORM

Use entity-based `db.Select<T>()`, `db.Insert<T>()`, `db.Update<T>()` and
`db.Delete<T>()` in place of `Model` and its row/query helpers. For a table
known only at runtime, use `DbTable`, `DbSchema` or `QueryBuilder` as needed.
Use `BaseRepository<T>` for application repositories in place of the generic
`Dao<T>`; concrete entity DAOs still work. The `DbExtensions` extension methods
remain the single implementation of the ORM entry points.
