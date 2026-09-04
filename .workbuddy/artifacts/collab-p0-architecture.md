# server-collab P0 增量架构设计 + 任务分解

> 作者：架构师 高见远（Gao）　日期：2026-09-03
> 输入：`.workbuddy/artifacts/collab-gap-prd.md`（含 2026-09-03 用户决策记录①~⑤）
> 一期范围 = **P0 九项 + 表单引擎（原 P1-2 前移）+ IM 一期（原 P2-1 界定）+ 备份（原 P1-9 上升）= 12 个工作包**。
> 本文档只做设计，不写业务实现；文中的 DDL / 类签名 / 目录结构均为设计产出。

---

## 0. 设计前提（代码现状核实结论）

抽样核实了 PRD 引用的关键文件，设计假设全部成立，并补充了四个影响设计的事实：

| # | 事实 | 出处 | 对设计的影响 |
|---|---|---|---|
| F1 | 注入点基类是 `src/Controller/AppController.zan`（非 Framework 下）：`OnBeforeAsync()` 借请求租约，`__Conn()` 供表访问器（`this.User` 等）使用；`AdminController : AppController`（有 `who/scope` 缓存）、`ApiController : AppController`（空壳） | `src/Controller/AppController.zan`、`AdminController.zan`、`ApiController.zan` | 软隔离注入点候选见 §3.3 |
| F2 | 建表是 CodeFirst：模型类带 `[Table(Name="xxx")]` + `[Column]`，启动时 `Schema.Ensure → db.SyncStructureAllAsync()` 一行覆盖全部模型，新模型**无需补行**；种子带计数保护 | `src/Framework/Schema.zan` | 新增 oa_/int_ 表 = 新增模型类 + `Schema` 增 Seed 方法 |
| F3 | `FlowHub` 是 per-worker 静态连接表（WS 主 + SSE 回退），`Push(userId, json)` 只对本 worker 上的在线连接投递；`Notifies.Stream` 端点 30min 收口、前端重连重过鉴权；首帧带 unread 同步 | `src/Modules/Flow/Engine.zan`（FlowHub 类）、`Notifies.zan:89` | 通知中心/IM 的实时通道直接复用 FlowHub 模式，跨 worker 用"落库为准 + 版本号轮询 + 本地扇出" |
| F4 | stdlib 已有 **CSV 读写**（`System/Text/Csv.zan`，RFC4180、自定义分隔符）与 **XLSX 写出**（`System/Data/Excel/Xlsx.zan`：`XlsxBook/AddSheet/AddRow/SetCell/Save/SaveToBytes`，inlineStr，互通 Excel/WPS；**无读取侧**——读取只走 `Csv.Parse`）；`Feature/Prose.zan` 只是字数/摘要/分段工具，**无 Markdown 渲染**（渲染在视图/前端层） | stdlib 抽样 | P0-5 导入=CSV（XLSX 读取列二期），导出=CSV+XLSX 双格式；文档中心的 MD 渲染确认复用面在前端 |
| F5 | 其他既有设施：`[Upload]`（`ctx.uploadPath/uploadSize` 流式落盘，body 上限 `server.maxBodyMB=2` 可经 `app.MaxBody()` 配置）、`[Ai]` + `AiEndpointPolicy` 出站白名单、`DataScope`（All/ByDepts/Self）、`PermTable` 共享内存、`Cache.SetTtlAsync`（memory=worker 本地、redis=跨 worker）、`Presence`（TTL 发布汇总模式） | stdlib/Attributes.zan、Feature/* | 各工作包复用点在 §2 逐项标注 |

约束沿用 PRD §7：零 Redis（memory 缓存 per-worker）、`#if/#each` 视图 + htmx + Alpine（无 Node）、attr→编译期权限、SQLite 默认 WAL、`maxBodyMB=2`。

---

## 1. 总体架构增量图

```mermaid
graph TB
  subgraph 现有 src/
    subgraph Framework["Framework/（不动结构，小增）"]
      PERM[Perm / PermTable]
      DS[DataScope]
      AUTH[Auth.zan AuthUser]
      CACHE[Cache/CacheContext]
      TENANT[★Tenant.zan 新增<br/>TenantCtx + TenantWhere]
      SEARCH[★Search.zan 新增<br/>统一搜索入口]
      EXCELIO[★ExcelIo.zan 新增<br/>导入导出公共层]
    end
    subgraph Controller_["Controller/（既有基类）"]
      AC[AppController]
      ADC[AdminController]
      APC[ApiController]
      IDX[Admin/Dashboard.zan<br/>→ 改造为工作台]
    end
    subgraph Modules_["Modules/（既有域）"]
      FLOW[Flow/ Engine + FlowHub]
      WMS[Wms/]
      CRM[Crm/]
      ECBI[Ecbi/]
    end
    subgraph Feature_["Feature/（既有）"]
      PROSE[Prose.zan]
      AI[Ai + AiEndpointPolicy]
      UP[Upload via [Upload]]
      PRESENCE[Presence]
      SETTINGS[Settings]
      MAILER[Mailer]
      METRICS[Metrics]
    end
  end

  subgraph 增量 Modules/Oa/
    NOTIFY[Oa/NotifyCenter<br/>通知中心 P0-1]
    ATTACH[Oa/Attachments<br/>附件 P0-2]
    TODO[Oa/TodoCenter<br/>待办 P0-3]
    DOC[Oa/Docs<br/>块编辑器文档 P0-6]
    CAL[Oa/Calendar<br/>日历 P0-7]
    MSG[Oa/Messages<br/>站内信+公告 P0-9]
    IM[Oa/Chat<br/>IM 一期]
    FORM[Oa/Forms<br/>表单引擎（前移）]
    BACKUP[Oa/Backup<br/>备份]
  end
  subgraph 增量 Controllers
    EX[Admin/ExportImport<br/>P0-5 落各模块]
    GSEARCH[Admin/Search<br/>P0-4 顶栏搜索]
  end

  TENANT -->|注入 tenant_id| AC
  NOTIFY -->|"发通知 API"| WMS & CRM & FLOW & ECBI & DOC & CAL & FORM
  NOTIFY -->|复用通道| FLOW
  TODO -->|聚合 TodoSource| FLOW & WMS & CRM & CAL
  DOC -->|渲染复用| PROSE
  DOC -->|附件| ATTACH
  MSG -->|推送| NOTIFY
  IM -->|复用 Hub 模式| FLOW
  FORM -->|发起流程| FLOW
  BACKUP -->|SQLite/上传目录| SETTINGS
```

**新表归属**（全部带 `tenant_id`，遵守前缀约定）：

| 前缀域 | 表 | 工作包 |
|---|---|---|
| sys_（平台公共） | sys_notify、sys_notify_pref、sys_seq | P0-1 / 软隔离版本号 |
| oa_（办公域，本次启用） | oa_attachment、oa_todo、oa_space、oa_doc、oa_doc_rev、oa_calendar_event、oa_announcement、oa_announcement_read、oa_message、oa_message_state、oa_form_def、oa_form_data、oa_import_batch | P0-2/3/6/7/9 + 表单引擎 |
| int_（集成域，一期仅备份用 0 表） | —（P1-3 开放 API 时启用） | — |

命名与模型规范：模型类放 `Modules/Oa/Model/Oa/*.zan`（namespace `ZanWeb.Model.Oa`），经既有 `SyncStructureAllAsync` 自动建表；控制器放 `Modules/Oa/Controller/Admin/Oa/*.zan`（namespace `ZanWeb.Modules.Oa`），`[Route("admin/oa/xxx/[action]")]` + `[Custom(Authorization=Grant, Perm=…)]`，权限零迁移成本自动进路由表/菜单。

---

## 2. 逐工作包设计

### 2.1 WP-1 租户软隔离（P0-8，地基）

见 §3.3 专项方案（推荐：AppController 基类 + 模型基类双注入点）。0 新表。

### 2.2 WP-2 跨模块通知中心（P0-1）

**表结构**

```sql
sys_notify (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  user_id   INT NOT NULL,            -- 收件人
  module    TEXT NOT NULL,           -- flow/wms/crm/ecbi/oa/system
  kind      TEXT NOT NULL,           -- assign/mention/done/alert/info
  title     TEXT NOT NULL,
  content   TEXT,
  link      TEXT,                    -- 站内跳转路径
  is_read   INT NOT NULL DEFAULT 0,
  created_at INT NOT NULL
);
CREATE INDEX idx_notify_user ON sys_notify(tenant_id, user_id, is_read, id DESC);

sys_notify_pref (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  user_id INT NOT NULL,
  module  TEXT NOT NULL,             -- 免打扰粒度=模块
  muted   INT NOT NULL DEFAULT 0,
  updated_at INT NOT NULL,
  UNIQUE(tenant_id, user_id, module)
);
```

**核心 API**（`Framework/Notify.zan`，全模块唯一发送入口）

```zan
class Notify {
    /* 落库 sys_notify + 实时增量（HubPush 扇出在线者）；muted 用户只落库不推。 */
    static async int Send(IDbConnection db, int tenantId, int userId,
                          string module, string kind, string title,
                          string content, string link);
    static async void SendAll(IDbConnection db, int tenantId,
                              List<int> userIds, ...);   // 批量扇出
    static async int Unread(IDbConnection db, int tenantId, int userId);
}
```

**实时通道**：新增 `Framework/NotifyHub.zan`——把 FlowHub 的连接表/Join/Leave/Push 逻辑**泛化上移**（per-worker 注册表 + `Push(userId, payload)`），FlowHub 保留为 Flow 专用或直接改指向 NotifyHub（推荐后者：Flow 的 Note() 改调 `Notify.Send`，`flow_notify` 表停止新写、旧数据只读展示，避免双表双端点）。跨 worker 增量见 §3.2 版本号机制。

**控制器**：`Modules/Oa/Controller/Admin/Oa/Notifies.zan` 路由 `admin/oa/notifies/[action]`（Index 列表/Unread 计数/Stream 事件流/Read 已读/Pref 偏好）；原 `Admin/Flow/Notifies.zan` 页面重定向到新页或桥接读 sys_notify。顶栏铃铛 `flow-bell.js` 改指新端点。

**复用**：FlowHub 连接表模式、`Settings`（免打扰默认值）、铃铛前端件。

### 2.3 WP-3 附件管理（P0-2）

**表结构**

```sql
oa_attachment (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  module      TEXT NOT NULL,        -- oa_doc/crm_customer/flow_inst/…
  related_id  INT NOT NULL DEFAULT 0,
  file_name   TEXT NOT NULL,
  path        TEXT NOT NULL,        -- data/uploads/{tenant}/{yyyyMM}/{uuid}.bin
  mime        TEXT NOT NULL,
  size        INT NOT NULL,
  uploader_id INT NOT NULL,
  created_at  INT NOT NULL
);
CREATE INDEX idx_attach_rel ON oa_attachment(tenant_id, module, related_id);
```

**核心 API**（`Feature/Attachment.zan`）

```zan
class Attachment {
    /* [Upload] 流式落盘后调用：登记行并回填 related；超配额(可配)拒绝。 */
    static async int Save(HttpContext ctx, AppController c, string module, int relatedId);
    static async bool CanDownload(User me, DataScope sc, DataAttachmentRow att); // 同租户 + 模块授权
    /* 视图组件：任何详情页 {{> AttachBlob }} 挂附件区（htmx 局部上传/删除/列表）。 */
}
```

控制器 `Admin/Oa/Attachments.zan`：`Upload`（`[Upload=true]`，`CustomAuthorization.Login`）、`List(module,relatedId)`、`Download(id)`（权限校验 + 流式应答 + `Content-Disposition`）、`Delete`（uploader 或管理权）。上传目录按 `data/uploads/t{tenant_id}/` 物理分目录（软隔离下的目录级辅助隔离，不算全链路切分）。

**配套**：`config/app.json` 增 `[server].uploadBodyMB`（默认 20），`main.zan` `app.MaxBody(uploadBodyMB*1024*1024)`；`[Limit]` 防刷。

### 2.4 WP-4 待办中心（P0-3）

**表结构**

```sql
oa_todo (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  user_id    INT NOT NULL,          -- 指派给谁（0=广播，一期只做指派）
  source     TEXT NOT NULL,         -- manual/flow/wms/crm/calendar/form
  title      TEXT NOT NULL,
  link       TEXT,
  due_at     INT,                   -- 0=无截止
  status     INT NOT NULL DEFAULT 0,  -- 0 待办 / 1 已完成 / 2 已忽略
  related_type TEXT, related_id INT,
  created_at INT NOT NULL, done_at INT
);
CREATE INDEX idx_todo_user ON oa_todo(tenant_id, user_id, status, due_at);
```

**聚合器**（`Feature/TodoCenter.zan`）：

```zan
class TodoCenter {
    /* 各模块注册来源：返回"该用户当前待办流"（Flow=flow_task 未完成；
       CRM=到期跟进商机；WMS=低于阈值的库存补货项；Calendar=今日日程）。 */
    delegate List<TodoItem> TodoSource(IDbConnection db, int tenantId, User me);
    static void Register(string module, TodoSource src);
    static async List<TodoItem> Gather(IDbConnection db, int tenantId, User me);
}
```

控制器 `Admin/Oa/TodoCenter.zan`（Index 聚合页 + Done/Ignored 动作）；**工作台改造**：`Admin/Dashboard.zan` 首屏改为"我的待办聚合 + 我的日程 + 公告"三卡片（保留管理员折叠区放运维指标）。

### 2.5 WP-5 全局搜索（P0-4）

0 新表。`Framework/Search.zan`：

```zan
class SearchProvider {
    string module;                                  // 显示分组名
    delegate List<SearchHit> Query(IDbConnection db, int tenantId,
                                   User me, string kw, int limit);
}
class Search {
    static void Register(SearchProvider p);         // 各模块启动时注册
    static async List<SearchHit> Run(IDbConnection db, int tenantId,
                                     User me, string kw, int perModule);
}
class SearchHit { string module; string title; string link; string badge; }
```

控制器 `Admin/Search.zan` 路由 `admin/search/[action]`（`Index` 顶栏框 + `Query` 端点，htmx 局部刷新分组结果）。注册方：CRM（客户/联系人/商机）、WMS（SKU/仓库）、Flow（实例标题）、Oa Docs（标题 + content LIKE）、表单数据。LIKE 阶段每模块 LIMIT 5，MySQL 阶段可换 FULLTEXT——接口不变。搜索结果按 DataScope 过滤。

### 2.6 WP-6 导入导出（P0-5）

**表结构**

```sql
oa_import_batch (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  module TEXT NOT NULL,               -- crm_customer/wms_sku/…
  file_name TEXT, total INT, ok INT, fail INT,
  errors TEXT,                        -- JSON：[{row, field, msg}] 前 200 条
  operator_id INT NOT NULL,
  created_at INT NOT NULL
);
```

**公共层**（`Framework/ExcelIo.zan`，包装 stdlib）：

```zan
class ExcelIo {
    /* 导出：委托取行 → Csv.Serialize 下载(.csv) 或 XlsxBook.SaveToBytes(.xlsx)。 */
    delegate List<List<XlsxCell>> ExportSource(IDbConnection db, int tenantId,
                                               DataScope sc, ListQuery q);
    static async void Download(HttpContext ctx, string name, List<string> headers,
                               ExportSource src, bool xlsx);
    /* 导入：Csv.Parse（stdlib）→ 逐行校验 → 事务分批(500 行/批，SQLite 锁库对策)
       → 错误行报告落 oa_import_batch；表头映射按模板列名。 */
    static async int Import(IDbConnection db, int tenantId, User me, string module,
                            List<string> headers, List<FieldRule> rules,
                            List<List<string>> rows);
}
```

端点：各模块控制器 +`Export`（`Perm=PermBit.Export`）+`Import`（`[Upload]`）+`Template`（下载 CSV 模板）。一期：**导入仅 CSV**（stdlib Xlsx 无读取侧，XLSX 导入列二期）；导出 CSV+XLSX 双格式。首批模块：CRM 客户/联系人、WMS SKU/库存、ECBI 订单。

### 2.7 WP-7 文档中心（P0-6，块编辑器）

见 §3.1 专项方案。表：

```sql
oa_space (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  name TEXT NOT NULL, icon TEXT, sort INT DEFAULT 0,
  read_roles TEXT, write_roles TEXT,   -- 逗号分隔 roleId；空=仅管理员（沿用 RBAC 判定）
  created_at INT NOT NULL, updated_at INT NOT NULL
);
oa_doc (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  space_id INT NOT NULL,
  parent_id INT NOT NULL DEFAULT 0,     -- 树形目录
  title TEXT NOT NULL,
  content TEXT,                         -- 块 JSON（§3.1 schema）
  content_text TEXT,                    -- 拼接纯文本（搜索/摘要用，保存时服务端生成）
  version INT NOT NULL DEFAULT 1,       -- 乐观锁
  editor_id INT NOT NULL,
  created_at INT NOT NULL, updated_at INT NOT NULL
);
CREATE INDEX idx_doc_space ON oa_doc(tenant_id, space_id, parent_id);
oa_doc_rev (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  doc_id INT NOT NULL, version INT NOT NULL,
  content TEXT, editor_id INT NOT NULL, created_at INT NOT NULL
);
```

控制器 `Admin/Oa/Docs.zan`（空间列表/树/编辑器页/`Save` 乐观锁保存写 rev/`History`/`Revert`）+ `Admin/Oa/Spaces.zan`。复用：附件（WP-3）、搜索（WP-5）、通知（@与评论二期）、`content_text` 供 LIKE 搜索。

### 2.8 WP-8 日历（P0-7）

```sql
oa_calendar_event (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  owner_id INT NOT NULL,
  title TEXT NOT NULL, description TEXT,
  start_at INT NOT NULL, end_at INT NOT NULL,
  all_day INT NOT NULL DEFAULT 0,
  related_type TEXT, related_id INT,     -- 关联审批/商机/表单
  remind_minutes INT NOT NULL DEFAULT 0, -- 0=不提醒；30/60/1440
  reminded INT NOT NULL DEFAULT 0,
  created_at INT NOT NULL, updated_at INT NOT NULL
);
CREATE INDEX idx_event_owner ON oa_calendar_event(tenant_id, owner_id, start_at);
```

控制器 `Admin/Oa/Calendar.zan`：月/周视图 = htmx 按 `?month=YYYY-MM` 服务端渲染网格（`#each` 逐格），**不引入重型日历库**；增删改用对话框表单（Alpine）。提醒：启动时 `MetricsStore.Start` 同款后台协程扫描 `remind_minutes 到期 && reminded=0` → `Notify.Send` + 写 `oa_todo`（挂 WP-2/WP-4）。

### 2.9 WP-9 站内信与公告（P0-9）

```sql
oa_announcement (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  title TEXT NOT NULL, content TEXT,
  dept_ids TEXT,                        -- 空=全员；否则逗号分隔部门
  pinned INT NOT NULL DEFAULT 0,
  publisher_id INT NOT NULL,
  created_at INT NOT NULL, updated_at INT NOT NULL
);
oa_announcement_read (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  announcement_id INT NOT NULL, user_id INT NOT NULL, read_at INT NOT NULL,
  UNIQUE(tenant_id, announcement_id, user_id)
);
oa_message (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  from_id INT NOT NULL, to_id INT NOT NULL,
  content TEXT NOT NULL,
  reply_to INT NOT NULL DEFAULT 0,
  created_at INT NOT NULL
);
CREATE INDEX idx_msg_to ON oa_message(tenant_id, to_id, id DESC);
```

控制器 `Admin/Oa/Announcements.zan`（发全员/按部门/置顶；员工端列表 + 详情 + 已读回执 + 登录后弹窗一次——`localStorage` 标记当日已弹）；`Admin/Oa/Messages.zan`（会话列表/发送/回复/已读；新消息走 `Notify.Send(module="oa",kind="message")` 复用 WP-2 管道）。公告与私信**不另建推送通道**，全部经 sys_notify。

### 2.10 WP-10 表单引擎（原 P1-2 前移）

```sql
oa_form_def (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  code TEXT NOT NULL,                   -- e.g. expense/room_booking
  name TEXT NOT NULL, description TEXT,
  schema_json TEXT NOT NULL,            -- §字段 schema
  status INT NOT NULL DEFAULT 1,        -- 1 启用 / 0 停用
  flow_def_id INT NOT NULL DEFAULT 0,   -- 绑定流程：提交后自动 Engine.Start
  created_by INT NOT NULL,
  created_at INT NOT NULL, updated_at INT NOT NULL,
  UNIQUE(tenant_id, code)
);
oa_form_data (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  form_id INT NOT NULL,
  data_json TEXT NOT NULL,              -- {fieldCode: value}
  summary TEXT,                         -- 摘要行（列表显示"报销 ¥320"）
  status INT NOT NULL DEFAULT 0,        -- 0 草稿/1 已提交/2 审批中/3 通过/4 驳回
  submitter_id INT NOT NULL,
  inst_id INT NOT NULL DEFAULT 0,       -- 关联 flow_inst
  created_at INT NOT NULL, updated_at INT NOT NULL
);
```

**schema_json 约定**（Alpine 动态渲染，字段类型一期 8 种）：

```json
{ "fields": [
  { "code": "amount", "label": "金额", "type": "number", "required": true,
    "min": 0, "unit": "元" },
  { "code": "reason", "label": "事由", "type": "textarea", "maxlen": 500 },
  { "code": "dept",   "label": "部门", "type": "select",
    "options": "dict:user_status" },
  { "code": "pics",   "label": "发票", "type": "attachment", "max": 5 } ] }
```

type ∈ `text / textarea / number / date / select(含 optionsSrc=dict:xx) / radio / checkbox / attachment`（attachment 指向 WP-3）。服务端校验与渲染同源于 schema（`FormValidator.Validate(schemaJson, dataJson)`）。

控制器 `Admin/Oa/Forms.zan`（定义 CRUD：**JSON 编辑器先行，拖拽设计器列二期**——无 Node 工具链下拖拽画布成本高，一期用"表单预览 + 字段表格编辑"折中）+ `Admin/Oa/FormFill.zan`（填写/提交/我的提交）。流程打通：`status=1 提交 → FlowEngine.Start(def, "表单:"+name, "form_data:"+id)`，`inst_id` 回填；审批完成回调置 3/4。复用：字典（select 选项）、附件、Flow、通知、待办。

### 2.11 WP-11 IM 一期（原 P2-1 界定）

见 §3.2 专项方案。表 `oa_message`（WP-9 已建，IM 复用为消息存储，增列不设新表）+ `oa_message_state`：

```sql
oa_message_state (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  tenant_id INT NOT NULL DEFAULT 1,
  user_id INT NOT NULL,           -- 会话归属人
  peer_id INT NOT NULL,           -- 对端
  last_read_msg_id INT NOT NULL DEFAULT 0,   -- 已读回执
  updated_at INT NOT NULL,
  UNIQUE(tenant_id, user_id, peer_id)
);
```

### 2.12 WP-12 备份（原 P1-9 上升为交付必选）

0 新表（备份元信息写 `sys_setting` 或本地清单文件）。

`Feature/Backup.zan` + 控制器 `Admin/Oa/Backup.zan`（`[Custom(Authorization=Grant)]`，仅 admin 屏）：

```zan
class Backup {
    /* SQLite：WAL checkpoint + 文件复制到 data/backups/{yyyyMMdd-HHmm}/app.db；
       MySQL/PG：不做产品内 dump，页面给出运维指引（导出命令模板）。 */
    static async string Run(string reason);          // 返回备份目录
    /* 上传目录整目录复制（附件+上传文件）。 */
    static async int BackupUploads();
    /* 保留策略：保留最近 N 份（默认 7），超出删除最旧。 */
    static async int Prune(int keep);
    /* 定时：后台协程每日 HH:mm 触发（复用 metrics 后台协程模式）。 */
    static async void StartScheduler();
}
```

端点：`Run`（手动触发）、`List`（备份目录清单：时间/大小/内容）、`Download(dir)`（打包 zip 流式下载）、`Auto`（定时开关与保留份数设置，落 Settings）。单机私有化承诺闭环：**SQLite 文件级备份 + 上传目录快照 + 一键下载**。

---

## 3. 三个攻坚专项方案

### 3.1 专项一：块编辑器（文档中心一期路线）

**约束**：视图引擎只有 `#if/#each`；前端只有 htmx 1.9.12 + Alpine 3.14.1（本地 vendor，无 CDN/Node）。**结论：块编辑器的前端交互用 Alpine 承担，块数据模型服务端化，一期做"结构化块 + 有限块型 + 表单化编辑"，放弃自由拖拽画布与多人实时协同。**

**数据模型：`oa_doc.content` 块 JSON schema**

```json
{ "v": 1,
  "blocks": [
    { "id": "b1", "type": "h2",      "text": "报销制度" },
    { "id": "b2", "type": "p",       "text": "正文，**粗体**与 `代码` 内联语法",
      "inline": "md" },
    { "id": "b3", "type": "list",    "style": "bullet|number|todo",
      "items": [ {"text":"…","checked":false} ] },
    { "id": "b4", "type": "quote",   "text": "引用" },
    { "id": "b5", "type": "code",    "lang": "sql", "text": "SELECT 1" },
    { "id": "b6", "type": "image",   "attachmentId": 123,
      "caption": "截图", "width": 640 },
    { "id": "b7", "type": "table",
      "header": ["项目","金额"],
      "rows": [ ["差旅","320"] ] },
    { "id": "b8", "type": "callout", "tone": "info|warn", "text": "注意" },
    { "id": "b9", "type": "divider" } ] }
```

- 内联格式一期只用**受限 Markdown 语法**（`**粗体** *斜体* \`代码\` [链接](url)`），渲染期服务端转 HTML（补一个 `Feature/Blocks.zan` 的 `InlineMd()`，复用/扩展现有 Prose 工具家族；转义先行防 XSS）。
- 每个 block 有稳定 `id`（保存时保持不变），为二期"块级操作/锚点/评论"预留。

**前端策略（Alpine 状态机，无拖拽画布）**

- 编辑器 = `x-data="blockEditor"` 持有 `blocks[]`；每个块渲染为一行"类型图标 + 内容控件 + 上移/下移/插入/删除"按钮（**上下移替代拖拽**，htmx-free 纯 Alpine 操作本地数组，防抖 2s 自动 `POST /admin/oa/docs/save`）。
- 文本块 = `contenteditable` 单块编辑（Enter 退出即"块结束"，不拆分块）；表格式块用 Alpine 模板行列增删。
- 光标策略：**一期不做跨块光标连续移动**（上下键跨块、选区跨块均列二期）；块内光标由浏览器原生 contenteditable 承担，保存不打断输入焦点。
- 拖拽排序（HTML5 draggable + Alpine 同步数组）作为**二期增强**；一期上移/下移/置顶满足文档排版需求。

**后端保存协议**

- `POST /admin/oa/docs/save`：`{docId, baseVersion, blocksJson}`；服务端 `version == baseVersion ? 写库+写 oa_doc_rev+version+1 : 409 冲突`（乐观锁，前端提示"他人已修改，刷新对比"）。前端本地留草稿（localStorage 按 docId）。
- 阅读页：服务端 `Blocks.zan.Render(blocksJson)` → HTML 片段（`#each blocks` 分派类型模板）；`content_text` 同步生成供搜索（WP-5）与摘要。

**一期块类型清单**：`h1~h3 / p / list(bullet,number,todo) / quote / code / image(附件) / table(2维) / callout / divider` = 10 种。
**二期清单**：块拖拽排序、跨块光标与选区、多人光标（FRP 级，依赖协同算法，明确不做）、块级评论与锚点、视频/附件嵌入块、Markdown 整篇粘贴自动切块、模板库。

### 3.2 专项二：IM 一期（零 Redis 多 worker 可靠投递）

**结论：`oa_message` 落库为唯一事实源；FlowHub 模式 per-worker 扇出只做"在线加速"；跨 worker 经 `sys_seq` 全局版本号 + 短轮询发现增量后本地扇出——与 sys_notify 投递保持同一管道。**

**跨 worker 增量机制（沿用 Presence/PRD §7 的轮询模式）**

- `sys_seq`（单行表，`SyncStructureAllAsync` 自动建）：`name TEXT PRIMARY KEY, value INT`，存"最新消息 id"与"最新通知 id"。写入方在事务内 `UPDATE sys_seq SET value=value+1 WHERE name='im'`（或直接 `MAX(id)` 查询 + 兜底）。
- 每个 worker 常驻一个后台协程（复用 `MetricsStore.Start` 的启动模式）：每 1s 查 `sys_seq`/`MAX(id)`，发现 > 本地已广播水位 → 查增量行 → 对本 worker 在线连接 `HubPush`（WS 主/SSE 回退，与 FlowHub 同构的 `ChatHub`）→ 推进水位。
- 投递语义分级：**在线** = WS 直推（毫秒级）；**离线/漏推** = 下次打开时按 `last_read_msg_id` 拉历史（HTTP 端点，落库为准，不丢不重）。SQLite 并发写用"合并/节流"对策：心跳读 sys_seq 走共享缓存兜底（`Cache.GetAsync` 1s TTL），写侧只有用户消息本身。
- 已读回执：`POST /im/read {peerId, lastMsgId}` 写 `oa_message_state`；对端在线时由同管道推 `{type:"read", peer, lastMsgId}`。

**通道复用**：`ChatHub` 直接复制 FlowHub 的并行数组结构（wsIds/wsConns + sseIds/sseConns），`Notifies.Stream` 端点扩展为统一事件流：`type ∈ notify | im | presence`，**一个长连接承载全部实时事件**（避免每页各开 WS）。

**一期范围**：单聊（1:1）、文本+附件消息、未读角标、已读回执、会话按人分组。
**一期不做（明确清单）**：群聊（需成员表+扇出放大，二期待单聊稳定）、消息撤回/编辑、正在输入指示、在线状态精确展示（沿用 Presence 粗粒度）、消息全文搜索、表情回应、离线推送（Mailer 通道列 P2）。

### 3.3 专项三：租户软隔离（注入点选型）

**候选对比**

| 方案 | 做法 | 优点 | 缺点 | 判定 |
|---|---|---|---|---|
| A. ORM 层（改 stdlib `System.Data.Orm`，Select 自动附 Where） | 查询编译期/运行期统一注入 | 全覆盖、零遗漏 | **改 stdlib 影响所有项目**；ORM 无请求上下文概念，需引 AsyncLocal 类机制；Zan 编译型 ORM 的表访问器是普通成员，无拦截点 | ✗ 成本外溢 |
| B. 编译期（attr 声明 → 编译器改写查询） | `[Tenant]` attr + 编译期 pass 改写 | 符合"声明在代码旁"哲学 | 编译器 pass 工程量大、调试困难；跨项目扩散慢；一期风险不可控 | ✗ 二期演进方向 |
| C. **AppController 基类 + Model 基类双注入（推荐）** | 见下 | 不动 stdlib；注入点集中两处；漏网面可穷举 | 依赖"业务查询都过 Model/DAO 约定"——正是本项目既有约定 | ✓ 一期落地 |

**C 方案设计**

1. **模型基类**：所有业务模型（sys_/oa_/wms_/crm_/ecbi_/flow_ 除 `sys_user` 登录域外）加列 `tenant_id INT NOT NULL DEFAULT 1`；约定每查询手写 `Where(a => a.tenantId == TenantCtx.Id())`——配合 2 的强制钩子保证不漏。
2. **请求上下文**：`Framework/Tenant.zan`：

```zan
class TenantCtx {
    static int current;                       // 协程模型：请求内绑定，worker 单协程串行时可直接静态槽
    static int Id() { return TenantCtx.current; }   // 0=未初始化（哨兵，禁止查询）
    static void Bind(int tenantId);
}
```

   `AppController.OnBeforeAsync()` 借到租约后：从 `this.uid` 解析用户 → 绑定 `TenantCtx.Bind(user.tenantId)`（user.tenantId 由 WP-1 在 `sys_user` 加列）；`OnAfterAsync` 复位为 0。**"0=哨兵"策略**：未绑定租户时任何业务查询直接抛错/断言失败，把"漏加过滤"从静默泄露变成显式失败。
3. **校验兜底**：`DbTrace.Sink` 已有 SQL 追踪钩子（main.zan:82）——开发态断言：凡命中租户表的 SQL 不含 `tenant_id` 谓词即在日志告警（SQLite/MySQL 通用，无需 ORM 改造）。
4. **平台模式**：`isSuper=1` 的引导账号 + `[Tenant(Bypass)]` 式端点白名单（登录、租户切换管理屏），其余一律强制绑定。
5. **登录会话**：`AuthToken.Resolve` 时已能拿到 user，tenant_id 随 user 行解析，不进 cookie 签名（避免令牌格式变更）。

**全库回归验证的查询面**（grep 穷举 + 手测矩阵）：

- `src/Controller/`：Account/Login、Register（登录域，Bypass）、Admin/System/{Users,Roles,Dicts,Departments,Logs,SiteConfig}、Admin/Profile、Admin/Dashboard。
- `src/Modules/*/Controller`：Wms 4 屏、Crm 3 屏、Ecbi 8 屏、Flow 3 屏（含 Notifies.Stream/Unread）。
- Framework：`AuthUser.RolesOf/MaskOf`（sys_user_role/sys_role_grant 查询要带 tenant）、`PermTable.LoadRoles`（master 建、worker 继承——租户维度需进共享内存 key）、`Schema.Seed*`（种子行 tenant_id=1）。
- 手测矩阵：双租户各建客户/商机/文档/消息 → 交叉登录验证不可见；super 账号验证平台模式可见性。

---

## 4. 任务列表（实现顺序，粒度=一次可提交）

> 依赖：`→` 前置。关键路径 = T1 → T2 → T7 → T8 → T9 → T10 → T11 → T12 → T13 → T17 → T18。

| # | 任务 | 涉及文件（相对 templates/server/server-collab/） | 依赖 | 验收标准（可测） | 规模 | 状态 |
|---|---|---|---|---|---|---|
| T1 | 租户上下文与模型基列 | `src/Framework/Tenant.zan`、`src/Controller/AppController.zan`、`src/Model/Sys/User.zan`(+tenant_id)、`src/Framework/Schema.zan` | — | 双租户手测矩阵通过（§3.3-5）；未绑定租户时业务查询报错而非放行；全部既有屏回归无异常 | L | ✅ 完成 |
| T2 | 通知中心公共层 | `src/Framework/Notify.zan`、`src/Framework/NotifyHub.zan`（自 FlowHub 泛化）、`src/Modules/Flow/Engine.zan`(Note→Notify.Send)、`src/Modules/Oa/Model/Oa/SysNotify.zan`+`SysNotifyPref.zan`、Schema.SeedNotify | T1 | 各模块发一条测试通知：落库+在线 WS 秒达+铃铛未读数正确；flow_notify 不再新写 | M | ✅ 完成 |
| T3 | 通知中心页面迁移 | `src/Modules/Oa/Controller/Admin/Oa/Notifies.zan`、`views/Admin/Oa/Notifies.Index.html`、`wwwroot/js/flow-bell.js`→`bell.js`、`views/Admin/layout.html` | T2 | 铃铛读 sys_notify；免打扰模块不推不打扰；旧通知页 301 到新页 | M | ✅ 完成 |
| T4 | 附件对象化 | `src/Feature/Attachment.zan`、`src/Modules/Oa/Model/Oa/OaAttachment.zan`、`src/Modules/Oa/Controller/Admin/Oa/Attachments.zan`、`src/main.zan`(MaxBody)、`config/app.json`(+uploadBodyMB)、views | T1 | 上传 20MB 文件成功；跨租户下载 403；详情页挂附件区可用 | M | ✅ 完成 |
| T5 | 待办中心与工作台 | `src/Feature/TodoCenter.zan`、`src/Modules/Oa/Model/Oa/OaTodo.zan`、`src/Modules/Oa/Controller/Admin/Oa/TodoCenter.zan`、`src/Controller/Admin/Dashboard.zan`、views | T2 | 工作台显示聚合待办（Flow 任务/CRM 到期/日程）；可一键跳转；完成状态同步 | M | ✅ 完成 |
| T6 | 导入导出公共层 | `src/Framework/ExcelIo.zan`、`src/Modules/Oa/Model/Oa/OaImportBatch.zan` | T1 | 导出 1 万行 CSV/XLSX 可用 Excel 打开；导入 500 行含 3 条错行，错误报告完整、分批提交无锁库 | M | ✅ 完成 |
| T7 | 各模块导入导出端点 | `Modules/Crm/Controller/Admin/Crm/*.zan`(+Export/Import/Template)、`Modules/Wms/...`、`Modules/Ecbi/...`、对应 views | T6 | 客户名单导入→列表可见；商机导出发老板邮箱场景闭环 | M | ✅ 完成 |
| T8 | 全局搜索 | `src/Framework/Search.zan`、`src/Controller/Admin/Search.zan`、`views/Admin/Search.*`、各模块 `Register` 调用、`views/Admin/layout.html`(顶栏框) | T1 | "客户A"分组命中客户/联系人/商机/文档；无权限模块不出现；响应 <300ms（万行级） | S | ✅ 完成 |
| T9 | 文档中心数据层+渲染 | `src/Modules/Oa/Model/Oa/{OaSpace,OaDoc,OaDocRev}.zan`、`src/Feature/Blocks.zan`(Render+InlineMd)、`src/Modules/Oa/Controller/Admin/Oa/{Spaces,Docs}.zan`（列表/树/阅读页） | T1,T4 | 空间-树形文档导航可用；10 种块类型阅读渲染正确；版本历史落 rev 表 | L | ✅ 完成 |
| T10 | 块编辑器前端 | `views/Admin/Oa/Docs.Edit.html`、`wwwroot/js/blocks.js`(Alpine)、`Docs.Save` 乐观锁端点 | T9 | 10 种块可增删改/上下移；2s 防抖自动保存；双开冲突 409 提示；content_text 同步生成 | L | ✅ 完成 |
| T11 | 日历 | `src/Modules/Oa/Model/Oa/OaCalendarEvent.zan`、`src/Modules/Oa/Controller/Admin/Oa/Calendar.zan`、views、提醒协程挂 `src/main.zan` | T2,T5 | 月/周视图切换；建日程；到期触发通知+待办各一条 | M | ✅ 完成 |
| T12 | 站内信+公告 | `src/Modules/Oa/Model/Oa/{OaAnnouncement,OaAnnouncementRead,OaMessage,OaMessageState}.zan`、`Controller/Admin/Oa/{Announcements,Messages}.zan`、views | T2 | 全员公告+登录弹窗一次；私信发送→对端铃铛+会话页已读回执 | M | ✅ 完成 |
| T13 | IM 一期（ChatHub+轮询） | `src/Framework/ChatHub.zan`、`src/Modules/Oa/Controller/Admin/Oa/Chat.zan`（Stream 扩展统一事件流）、`wwwroot/js/chat.js`、sys_seq 模型 | T12 | 双浏览器跨 worker 会话：A 发 B 秒收；B 离线再上线拉到消息；已读回执双向正确 | L | ✅ 完成 |
| T14 | 表单引擎定义侧 | `src/Modules/Oa/Model/Oa/{OaFormDef,OaFormData}.zan`、`src/Feature/FormSchema.zan`(校验+渲染模型)、`Controller/Admin/Oa/Forms.zan`、views(JSON 编辑+预览) | T4 | 8 种字段类型 schema 定义→预览渲染一致；服务端校验拦截非法提交 | L | ✅ 完成 |
| T15 | 表单填报+流程打通 | `src/Modules/Oa/Controller/Admin/Oa/FormFill.zan`、`src/Modules/Flow/Engine.zan`(Start 支持 bizRef=form_data)、views | T14 | 报销单提交→自动起流程→审批通过后表单状态=通过；提交进待办中心 | M | ✅ 完成 |
| T16 | 备份 | `src/Feature/Backup.zan`、`src/Modules/Oa/Controller/Admin/Oa/Backup.zan`、`src/main.zan`(StartScheduler)、views | T1 | 手动备份生成 data/backups/{ts}/(app.db+uploads)；保留 7 份自动清理；可下载 zip | S | ✅ 完成 |
| T17 | 端到端联调与回归 | 全模块 views、`src/Modules/Flow/...`、双租户矩阵 | T3~T16 | PRD §3 断点 2/4/5/6/7/8/10 全部走通；双租户隔离矩阵全绿；SQLite 4 worker 压测无锁死 | L | ✅ 完成（QA PASS，报告 `_scratch/t17_run/REPORT.md`） |
| T18 | 文档与交付 | `README` 增量、`config/app.json` 注释、`/api/docs` 校对 | T17 | OpenAPI 文档覆盖新端点；部署文档含备份恢复演练步骤 | S | ✅ 完成（README 定位纠偏+功能面+已知行为+配置项说明；config/app.json 为 JSON 无注释语法 → 说明落 README；`docs/deploy-collab.md` 含 VACUUM INTO 回灌演练/MySQL 指引/CodeFirst 升级路径；docs 派生核查见 T18 提交说明） |

规模合计：S×3、M×8、L×6（T17 联调按 L 计入但可并行拆分）。

---

## 5. 风险与待明确事项

| # | 风险/待明确 | 影响 | 对策/建议 |
|---|---|---|---|
| R1 | **T1 租户隔离是全局地基**，改 AppController/模型列/共享内存 key，回归面=全部既有屏 | 最高；改错即全站不可用或数据泄露 | 首个提交只加机制不改行为（tenant_id 列默认 1 + 哨兵断言仅开发态开启）；回归矩阵进 CI 脚本 |
| R2 | SQLite 高频写（IM/通知）锁库 | 4 worker 并发写恶化 | WAL 已启用；IM 心跳读走共享缓存；写侧节流合并；生产部署文档明确"MySQL 优先"；导入 500 行/批事务 |
| R3 | 块编辑器交互天花板 | 用户预期飞书级体验落差 | 一期边界已在 §3.1 明确公示（上下移替代拖拽、不做跨块光标）；二期路线写进产品文档管理预期 |
| R4 | `maxBodyMB=2` → 上传配置放宽后的滥用 | 磁盘膨胀 | 配额校验（Attachment.Save）+ `[Limit]` + 保留策略联动备份清理 |
| R5 | 跨 worker 轮询的延迟与开销（1s 粒度） | IM 体感非"真实时" | 一期接受 1s 级（局域网私有化场景可接受）；预留 cache.driver=redis 时无缝升级为发布订阅 |
| R6 | 权限 attr 编译期生成 vs 运行时租户开关 | 无法按租户启停模块 | 用 sys_setting 租户级配置行实现功能开关（PRD §7 已定，勿发明运行时权限表） |
| Q1 | **待明确**：`sys_user` 等平台表是否参与租户过滤（账号归属唯一租户 or 可多租户） | T1 建模 | 建议一期：单租户归属（user.tenantId 单值）；多租户成员关系列 P2 |
| Q2 | **待明确**：XLSX 导入是否一期必须 | T6/T7 范围 | stdlib 无 XLSX 读取侧；建议一期 CSV+模板，客户存量 Excel 另存为 CSV（导入页给指引） |
| Q3 | **待明确**：IM 是否需要服务端消息持久化上限 | 存储增长 | 建议：保留策略随备份 Prune 一并设计（如 1 年） |
| Q4 | **待明确**：备份恢复演练是否进验收 | WP-12 | 建议进 T18 验收：备份→删库→恢复→启动比对 |

---

## 6. 复用与不造轮子清单（核实自 stdlib）

- **CSV**：`System/Text/Csv.zan`（Parse/Serialize，RFC4180）——直接用，勿重写。
- **XLSX 写出**：`System/Data/Excel/Xlsx.zan`（XlsxBook/AddSheet/AddRow/SetCell/SaveToBytes）——导出直接用；读取侧缺失是 Q2 的原因。
- **实时通道**：FlowHub（Engine.zan）连接表结构 → NotifyHub/ChatHub 泛化复用；`ctx.WsUpgrade`/`SseSend` 框架能力直接用。
- **上传**：`[Upload]` 路由属性 + `ctx.uploadPath/uploadSize` 流式落盘——Attachment 只做登记与权限。
- **AI**：`[Ai]` + `AiEndpointPolicy` 白名单——表单/文档的 AI 辅助（二期）走同一策略。
- **RBAC**：`[Custom(Perm=PermBit.*)]` 声明即授权，新控制器零迁移。
- **后台协程模式**：`MetricsStore.Start` —— 提醒扫描/备份调度/IM 轮询照此模式。
- **共享缓存**：`Cache.SetTtlAsync` + worker 槽 key —— Presence 已示范跨 worker 轮询汇总范式。
