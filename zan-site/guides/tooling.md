# 工具协议（AI 与人的项目操作手册）

> 本文回答三件事：**AI 怎么快速定位必要代码、怎么调用工具、怎么使用编译器
> 与测试**。原则只有一条：**先看结构索引，再只读改动点；架构约定优先于逐一
> 通读文件**。阅读顺序建议：先读本项目根的 `AGENTS.md`/`zan.proj`（约定与
> 入口），再按本文工作流执行。

## 为什么不该"通读代码"

"翻阅大量文件 + 大量思考"的浪费来自三重缺失：没有架构地图、没有语义定位、
没有工具协议。Zan 的解法是三件套：

| 层 | 工具 | 解决什么 |
|---|---|---|
| L1 架构约定 | `zan.proj` + 目录规范（`src/Controller`/`Dao`/`Model`/`Feature`/`Framework`）+ 命名 | 知道"什么在哪"，一步定位，零阅读 |
| L2 语义索引 | RepoMap：`repo.map.txt`（大纲）/ `symbols.json`（符号）/ `routes.json`（路由） | 知道"定义/引用/路由在哪",无需 grep 海捞 |
| L3 启动器 | MCP 服务（CRUD + run_command + `repo://` 资源）/ 编译器 CLI / 测试分层 | 知道"怎么编译、验证、修改" |

## 一、架构定位（先于任何阅读）

### 项目即约定

Zan 项目的 `zan.proj` 声明类型与入口；模板已把分层做成目录约定：

```text
templates/server/server-mvc/src/
├── Controller/   # 请求入口：HttpContext → 校验 → 调 Dao/Model → 回复
│   ├── Api/      # JSON API（[HttpPost] 等属性路由）
│   ├── Admin/    # 后台页
│   └── ...
├── Dao/          # 数据库读写：SQL、ORM 查询，只做数据
├── Model/        # 实体/行映射/校验
├── Feature/      # 业务编排（跨 Dao/Model 的组合）
├── Framework/    # 基础设施：Db/AppServices/Cfg/Auth/...
└── main.zan      # WebApp 装配入口
```

对应到需求：`/api/users 列表` → `Controller/Api` 找路由 → `Dao` 数据段 →
`Model` 实体，按路径直接到达，**不需要读其它代码**。

### 生成的路由表（MVC 专用索引）

RepoMap 从 Controller 属性中提取 `routes.json`
（仅当项目有 `src/Controller` 时生成）：

```json
[{"method":"GET","path":"/api/users","action":"Api.User.List","title":"用户列表","auth":"None"}]
```

看到 `/api/users` 的请求需要改动 → 先查 `routes.json` → 命中
`action: Api.User.List` → 打开 `src/Controller/Api/User.zan` 的 `List` 方法
→ 按方法体里的调用继续向下定位（`Dao` / `Model`）。一次查询，零通读。

## 二、语义索引（RepoMap）

### 生成

```bash
zanc tools/repomap/RepoMap.zan --auto-stdlib -o build/repomap.exe
build/repomap.exe .                # 项目根；写出 <root>/.zanmap/
```

`build/` 里的 `zanc` 可从 `build/zanc.exe` 复制，或让 IDE 生成一次。

### 产物与用途

| 文件 | 内容 | 典型用法 |
|---|---|---|
| `.zanmap/repo.map.txt` | 每个文件 + 类/方法签名 + 行号的精简大纲（几 KB 级） | **作为 AI 首次上下文**直接喂入（几 KB 换全局，不必读源码） |
| `.zanmap/symbols.json` | 扁平索引 `[{name,kind,file,line,sig}]` | 定点跳转：改哪个符号 → 精确到文件+行号 |
| `.zanmap/routes.json` | 路由表（仅 server 模板输出） | 按 URL 定位控制器 |
| `.zanmap/gallery.json` / `zform.json` | 模板/示例目录 + `.zform` schema 与控件目录 | 查"现成能力"（模板/控件/属性） |

### 推荐工作流（改一个功能）

1. `repo.map.txt` → 全貌（一次）；
2. 要改的符号 → `symbols.json` 查出 `file:line`（一次）；
3. 只读该符号上下 ±30 行与它的直接调用点（从 `routes.json`/`symbols.json` 找引用，不 grep 全文）；
4. 改 → 编译 → 测试（见下）→ 再动的下一个点重复 2~3。

## 三、编译器（zanc 命令行）

```bash
zanc src/main.zan ../src/App.zform --auto-stdlib -o app.exe   # 编译运行
zanc src/main.zan --publish -o app.exe                        # 发布（-Os、strip、链接驱动）
zanc src/*.zan --auto-stdlib -o lib.dll --emit-lib            # 库
zanc src/*.zan --target linux-x64 -o app                      # 交叉编译（--list-targets）
zanc src/*.zan --check-leaks -g                               # 泄漏检查（-g 默认开）
zanc src/*.zan --dump-ast / --emit-ir                         # 调试
zanc @args.txt                                                # 响应文件（参数多时）
```

诊断即定位：编译错误给出**文件:行号 + 消息**，这是最快的反馈，不需要猜。
配套：`zanfmt`（格式化）、`zandoc`（`///` 文档）、`zan-lsp`（补全/诊断/跳转）、
`zan-dap`（调试）。全部命令在同一目录（`build/` 或 IDE 安装目录）。

## 四、测试分层（只跑相关的一层）

```bash
scripts/test.ps1 smoke          # 75 个用例 ~5s：goldens/诊断/ABI/runtime/GUI——每次编辑必跑
scripts/test.ps1 standard       # smoke + 全 conformance + 库发射 ~1min——提交前
scripts/test.ps1 full           # + 确定性/leakcheck 孪生 + 自举——发布前
ctest -L standard -R "generic"  # 按标签/名称缩小
```

**并发纪律**：所有测试用例共享 `build\zanc.exe` 与 stdlib 戳记，测试期间不得
同时构建 gallery/IDE（会让无关用例集体失败）。
GUI 改动还要：`scripts/build_gallery.ps1` + `scripts/build_ide.ps1` 编译验证；
模板改动跑 `scripts/e2e_pipeline.ps1`。临时探针一律放 `_scratch/`。

## 五、MCP 服务（外部 AI 的"手"）

### 启动

```bash
tools/zan-mcp.exe .                      # stdio（本地开发工具典型）
tools/zan-mcp.exe . --host 0.0.0.0 --token-env ZAN_MCP_TOKEN --read-only  # HTTP 共享
```

- `--read-only`：只读部署（不写工作区）。
- `--frozen-tools`：跨工作区保持工具目录稳定；不可用工具仍返回诊断。
- 每个请求都要 Bearer 授权（`--token-env` 注入环境变量）。

### 契约（docs/ai-assist.md 定义）

- **路径约束**：所有路径相对单一 workspace 根，绝对路径/`..` 拒绝——
  AI 不需要也**不允许**乱翻根目录以外的文件。
- **资源**（read-only，免文件漫步）：`repo://map`、`repo://symbols`、
  `repo://routes`。
- **操作**：文件 CRUD + `apply_patch` + `run_command`（构建/测试都走它，
  输出的诊断直接回给 AI）。

典型编辑循环：`repo://map` → `repo://symbols`/`search` 定位 → 读该文件 →
`apply_patch` → `run_command` 编译 → 诊断修正。结构改动后重跑 `repomap.exe`
让资源保持新鲜。

## 六、写作与验证纪律（减少无效思考的软约束）

- **先查现成能力，再写新代码**：模板（`templates/`）→ 组件/控件
  （`Gui.Widget/Component/Hmi`）→ 标准库（`System.*`）→ 真实示例
  （`examples/`）→ 才新写。查"有没有"用 gallery.json/能力目录，不靠记忆。
- **一次只改一个点**，每步可独立编译；不混入无关重构。
- **报告区分"我验证过的"与"我推断的"**；没验证到的路径显式说明。
- **收尾**：仓库根干净（探针在 `_scratch/`）、改了什么/为什么/跑了哪些
  验证、知识索引有增量就更新——见 `docs/agent-kb/agent-workflow.md`。

## 相关

- [AI 开发助手入口（文档地图）](/ai) — 本套文档的检索指南
- [语言参考](/lang) — 语法能力与工具链细节
- [GUI 指南](/gui) — 界面开发
- 仓库内：`docs/ai-assist.md`（RepoMap+MCP 契约）、`docs/MCP_HOSTING.md`
  （部署）、`docs/agent-kb/`（方法论：workflow/debugging/testing/project-map）
