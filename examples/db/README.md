# examples/db — 数据库操作示例

每个示例都是单文件、可独立编译运行的完整程序。SQLite / ZanDb / ORM 示例
零外部依赖，开箱即跑；MySQL / PostgreSQL / Redis / ODBC 示例在服务不可用时
打印提示并跳过（文件头注释里有一行 docker 启动命令）。

编译运行（在仓库根目录）：

```
build/zanc.exe examples/db/sqlite_crud.zan --auto-stdlib -o sqlite_crud.exe
./sqlite_crud.exe
```

| 文件 | 数据库 | 演示要点 |
| --- | --- | --- |
| `sqlite_crud.zan` | SQLite（内嵌，零依赖） | 打开/建表、参数化增删改查、DbResult 遍历与 NULL、LastInsertId、事务批量提速、SqlitePool 协程连接池 |
| `mysql_crud.zan` | MySQL / MariaDB（原生协议，纯 Zan） | 异步连接与认证、文本协议查询、预处理语句（二进制协议 + 语句缓存）、事务 |
| `postgres_crud.zan` | PostgreSQL 系（原生 libpq） | 同步/异步连接、`?`→`$n` 参数化、事务、ORM 直接驱动 PgConnection |
| `odbc_multidb.zan` | SQL Server / Oracle / 达梦 / 金仓 / ClickHouse / DuckDB / TDengine…（ODBC） | 各引擎便捷工厂 + 万能 OpenODBC、统一 CRUD、GetProvider、分页方言适配 |
| `redis_cache.zan` | Redis（RESP 协议，纯 Zan） | SET/GET/INCR/EXPIRE、任意命令（哈希/列表）、RedisReply 解析、cache-aside 缓存模式 |
| `zandb_documents.zan` | ZanDb（内置嵌入式文档库） | JsonValue 文档 CRUD、原子批量、二级索引、LINQ 风格查询（lambda/排序/分页）、原子读改写 |
| `orm_model.zan` | ORM 模型层（FreeSQL 风格） | 模型定义、建表、链式增删改查、分页、事务、Migration 迁移、FromTable 反向映射 + 代码生成 |
| `orm_querybuilder.zan` | ORM SQL 构建层 | 参数化 CRUD、全部条件族、Join、聚合、分页方言、注入安全 |

## 连接类型与 ORM 的关系

ORM（`System.Data.Orm` 的 `Model` / `QueryBuilder`）通过 `IDbExecutor`
接口驱动任意连接，换数据库不换业务代码：

- `System.Data.Sqlite.SqliteConnection` —— 内嵌 SQLite，零配置（sqlite3 原生绑定）
- `System.Data.Postgres.PgConnection` —— 原生 PostgreSQL / openGauss / 金仓等 PG 协议引擎（libpq）
- `System.Data.MySql.MySqlConnection` —— 原生 MySQL/MariaDB 线协议（纯 Zan，异步）
- `System.Data.DbConnection` —— ODBC 通道，可达 SQL Server、Oracle、达梦、ClickHouse 等一切有 ODBC 驱动的引擎
- `System.Data.Redis.RedisClient` —— Redis 客户端（缓存/计数，不走 SQL/ORM）
- `System.Data.ZanDb.ZanDatabase` —— 内置文档数据库（JsonValue 文档，不走 SQL/ORM）

选型速查：

- 单机/嵌入式关系存储、测试 → SQLite（`SqliteConnection`）
- 本地文档/配置/状态，不想写 SQL → ZanDb（`ZanDatabase`）
- 生产 PostgreSQL 系 → `PgConnection`（原生 libpq，性能最好）
- 生产 MySQL 系 → `MySqlConnection`（原生协议、预处理语句、协程异步）
- 其它引擎（SQL Server/Oracle/达梦/金仓/ClickHouse…） → `DbConnection`（ODBC）
- 缓存、计数、队列 → Redis（`RedisClient`）
- 结构化模型 + 自动 SQL → `Model`；手写复杂 SQL 但要安全拼接 → `QueryBuilder`
