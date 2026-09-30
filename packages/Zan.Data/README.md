# Zan.Data

Data access for Zan, extracted from the standard library. Namespaces are
unchanged, so existing programs compile as-is once the package is visible
to zanc (project `packages/`, `.zan-packages/`, or a toolchain-relative
`packages/` store).

- `System.Data` — `DbConnection` / `DbPool` / `DbResult` core, connector and
  executor interfaces, tracing wrapper.
- `System.Data.Orm` — FreeSQL-style ORM. `[DbTable]` models plus
  `db.Select<T>()` fluent queries; the compiler's ORM codegen
  (`System/Compiler/ZanGen.zan`) emits the per-entity facades, so user code
  never spells `OrmMeta`/`OrmSelect` directly.
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
