# server-collab 部署与运维手册

适用范围：`templates/server/server-collab`（Zan 企业协同平台）。
部署形态、备份恢复**演练**步骤（含 VACUUM INTO 回灌校验）、MySQL 部署
指引与旧库升级路径。

---

## 1. 部署形态

发布目录 = 可执行文件 + 三份运行时资产（`src/` 是构建输入，不随发布）：

```
app.exe                 链接器落位在 build/ 的驱动 DLL 按需带上
config/app.json         运行时配置（键名逐项说明见 README「配置项说明」）
views/                  全部 .html，目录结构与仓库一致
wwwroot/                静态资产（/static 唯一出口）
docs/                   本手册（可选）
```

- SQLite 需要 `libsqlite3-0.dll`；PostgreSQL 需要
  `libpq.dll + libssl-3-x64.dll + libcrypto-3-x64.dll + libiconv-2.dll +
  libintl-8.dll`；MySQL 走自有协议，无需客户端库。
- **必须从发布目录启动**（`config/app.json`、`views/`、`wwwroot/`、
  `data/` 均按相对路径解析）；`database.sqlitePath` 保持相对路径。
- `[auth].secret`（或环境变量 `ZAN_AUTH_SECRET`，优先）设 32+ 随机字符，
  否则登录报配置错误。
- 多进程：`worker.count=N` 在 Linux/macOS 以 `SO_REUSEPORT` 多进程同端口
  分流；Windows 恒单进程。`worker.count>1` 且 `cache.driver=memory` 时
  缓存仅 worker 本地——生产多进程建议 `cache.driver=redis`。
- 进程守护交给 systemd `Restart=always` / Docker `restart=unless-stopped`；
  二进制自身支持 `start / start -d / stop / restart / reload / status`
  （控制端口 = HTTP 端口 + 10000，或 `--ctl-port N`）。
- 首次启动 `Schema.Ensure` 自动建表 + 种子（admin/admin1234，仅空表时），
  随后立刻改密并更换 `auth.secret`。

## 2. 备份

### 2.1 应用内备份（默认路径，sqlite）

管理后台 `系统管理 → 备份恢复`（`/admin/system/backup`，超管）或
`POST /admin/system/backup/create`：

```
data/backups/{YYYYMMDD}-{HHMMSS}/
  app.db         VACUUM INTO 一致性快照（在线安全，不阻塞 WAL 读写）
  uploads.zip    data/uploads/ 存储态打包（不重压缩）
  manifest.txt   备份时间、驱动、库大小、附件数/字节
```

保留 7 份自动轮转；`data/backups/.lock` 跨 worker 串行化；每次备份可在
页面下载 `backup-{timestamp}.zip`。

### 2.2 Cron 备份（不用管理页）

同 VACUUM INTO 原理，shell 侧实现（cron 模板见 README「Backup & restore」
节，含 7 槽轮转一行式）。管理页多了 manifest 与并发锁，能用则用管理页。

### 2.3 MySQL 备份

内置备份页在 `driver=mysql` 下明确降级（页面提示 + 创建返回 400），
不静默空转。改用：

```
mysqldump --single-transaction --routines --triggers dbname > backup.sql
tar -czf uploads-$(date +%F).tar.gz data/uploads
```

## 3. 恢复与演练（验收口径：备份 → 删库 → 恢复 → 比对）

### 3.1 SQLite 恢复步骤

1. 停应用（`app.exe stop` 或进程管理器），恢复期间无写入方。
2. 取出备份文件：管理页下载 zip，或直接读主机上
   `data/backups/{timestamp}/`。
3. 覆盖主库：备份 `app.db` → `data/app.db`（连同 WAL/SHM 一并删除旧的，
   或先 `PRAGMA wal_checkpoint(TRUNCATE)`）；解包 `uploads.zip` 覆盖
   `data/uploads/`（保留 `{year-month}/` 结构）。
4. 起应用，抽查业务数据与附件可下载。

### 3.2 恢复演练：VACUUM INTO 回灌校验（T17 同款口径）

不必真删生产库即可验证备份可用性——对快照产物做独立完整性校验：

```sh
TS=data/backups/20260904-115419        # 换成最新备份目录

# 1) 完整性：integrity_check 必须 ok
sqlite3 "$TS/app.db" "PRAGMA integrity_check;"

# 2) 回灌：从快照 VACUUM INTO 一份新库，证明文件可被 SQLite 正常打开重写
rm -f /tmp/rehydrated.db
sqlite3 "$TS/app.db" "VACUUM INTO '/tmp/rehydrated.db';"
sqlite3 /tmp/rehydrated.db "PRAGMA integrity_check;"

# 3) 行数比对：快照 vs 运行库（示例核心表）
for t in sys_user crm_customer oa_doc oa_form_data flow_inst oa_message sys_notify; do
  echo -n "$t: snap="
  sqlite3 "$TS/app.db" "SELECT count(*) FROM $t;"
  echo -n "    live="
  sqlite3 data/app.db       "SELECT count(*) FROM $t;"
done

# 4) 附件：uploads.zip 解包后与 data/uploads/ 目录条目数/字节数 diff
```

- 校验通过 = 快照非零字节、integrity ok、回灌成功、行数与运行库一致。
- T17 演练基线：50 表 integrity ok，抽查 7 张核心表行数与运行库全等。
- 建议节奏：每次版本发布前 + 每月各演练一次；演练脚本可放
  `ops/restore-drill.sh` 纳入 CI 定期跑。

### 3.3 MySQL 恢复

```
mysql dbname < backup.sql          # 回灌进空 schema（CodeFirst 可先起一次应用建表）
tar -xzf uploads-YYYY-MM-DD.tar.gz -C data/   # 覆盖 uploads/
```

同样在 staging 先走一遍完整恢复再谈生产 RTO/RPO。

## 4. MySQL 部署指引

1. **建库**：`CREATE DATABASE app DEFAULT CHARACTER SET utf8mb4
   COLLATE utf8mb4_unicode_ci;` 业务连接账号仅授该库 DML/DDL
   （首启建表需要 DDL，之后可回收）。
2. **切换配置**（`config/app.json`）：

   ```json
   "database": { "driver": "mysql", "host": "127.0.0.1", "port": 3306,
                 "name": "app", "user": "app", "password": "…", "poolSize": 8 }
   ```

3. **首启迁移**：直接启动应用，`Schema.Ensure` 走 CodeFirst 对 MySQL 发
   DDL 建表 + 种子；日志出现 `[db] mysql pool size=…` 与
   `[rbac] role grants…` 即成功。
4. **备份策略**：mysqldump（§2.3）+ uploads 目录打包；备份页仅服务 sqlite。
5. **选型建议**：IM/通知高频写、4+ worker 并发场景优先 MySQL（架构风险
   R2 的定案口径）；SQLite 适合小规模与单机。
6. 注意 SQLite → MySQL 的数据搬迁无内置工具：schema 由 CodeFirst 重建，
   数据侧自行按表导出导入（CSV 导出端点可作为业务数据搬运通道）。

## 5. 升级注意

- **Schema 是 CodeFirst 自动迁移**：新版本二进制首启即按实体元数据
  「缺表建表、缺列加列（`ALTER TABLE … ADD COLUMN`）、缺索引补索引」，
  幂等可重复执行；**旧库升级 = 替换二进制 + 重启**，无手工 SQL。
- 升级前先备份（§2）；降级不做列删除——新列在旧二进制下被忽略，属
  前向兼容，但不要跨大版本反复回滚。
- 种子全部带计数保护（表非空即跳过），重启/升级不会重置业务数据，也
  不会覆盖已修改的种子账号密码。
- 升级后核对：`/admin` 工作台正常、`/admin/system/docs` 能列出全部路由、
  登录/待办/通知各抽一笔（回归最小集，详见 T17 报告 A~N 项）。
- 视图与静态资产随发布目录替换（启动时一次性读入内存），改完 `views/`
  需重启生效。

## 6. 故障速查

| 症状 | 排查 |
|---|---|
| 登录一律"密码错误" | `sqlitePath` 是绝对路径落到盘根 → 库没建上；或 `auth.secret` 缺失/过短 |
| DB 路由 503 | `[database]` 不可达；看启动行 `[db] schema not applied` |
| 多 worker 下 IM/通知延迟大或丢失 | `cache.driver=memory` 且 `worker.count>1`：跨 worker 共享需 redis |
| 备份按钮 400 | 当前 driver=mysql（设计降级），走 §2.3 |
| `database is locked` | 确认 WAL 已启用（默认）；并发写大库迁 MySQL；检查是否有进程直接写库文件 |
