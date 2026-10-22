# Zan.Data

Data access for Zan, extracted from the standard library. Namespaces are
unchanged, so existing programs compile as-is once the package is visible
to zanc (project `packages/`, `.zan-packages/`, or a toolchain-relative
`packages/` store).

- `System.Data` — `DbConnection` / `DbPool` / `DbResult` core, connector and
  executor interfaces, tracing wrapper.
- `System.Data.Orm` — ZanSQL ORM. `[DbTable]` models plus
  `db.Select<T>()` fluent queries; the compiler's ORM codegen
  (`System/Compiler/ZanGen.zan`) emits the per-entity facades, so user code
  never spells `OrmMeta`/`OrmSelect` directly. Navigation is **bidirectional**:
  `Include(p => p.user)` joins the parent into the child row, and
  `Children(parentId)` / `ChildrenOf(parentTable, parentId)` fetch the
  children of a parent row — the reverse relation is *derived* from the
  child's navigation declaration (foreign key column = `<navField>Id`, the
  same convention as the join's ON clause), so callers never spell the
  foreign key column by hand.
- Wire-protocol drivers (no ODBC needed): `System.Data.MySql`,
  `System.Data.Postgres` (native `pq` driver bundle under `drivers/`),
  `System.Data.SqlServer` (TDS), `System.Data.Sqlite` (embedded),
  `System.Data.Firebird`, `System.Data.TDengine` (REST), plus the generic
  `OdbcConnector`.
- `System.Data.Redis` — client and connection pool.
- `System.Data.ZanDb` — embedded pure-Zan document/KV store (no native
  dependencies).
- `System.Data.Excel` — Xlsx spreadsheet reader.

Server-shaped code pulls these namespaces on demand exactly as before; a
program that never references them compiles without this package.
