# AI_README — 怎么让 AI 用这个 SDK

> 这份是给**人和团队**看的总说明（中文）。AI 客户端自己读的是英文的
> `AGENTS.md` / `llms.txt` / skills——本文解释它们各自是什么、怎么接、怎么用。
> 路径都相对本 SDK 根目录。

## 1. 一句话

把 AI 编辑器（Claude Code / Cursor / Copilot / Windsurf…）指到你的**项目目录**
（不是这个 SDK 目录），AI 就能通过 `tools\zan-mcp.exe` 查标准库真实签名、
抄可编译的官方示例、编译验证、按规矩改代码——不猜 API、不编造接口。

## 2. 接入：三种方式任选其一

| 方式 | 操作 | 适合 |
| --- | --- | --- |
| IDE 一键 | ZanIDE 助手面板 →「为当前项目启用 AI 接入」 | 已打开项目 |
| 命令行 | `tools\zan-mcp.exe --init-agent D:\path\to\project`（加 `--force` 覆盖已有） | 脚本/无 IDE |
| 不用 MCP | 让 AI 直接读根目录 `llms.txt`（一页索引）和 `docs\AI_ONBOARDING.md`，用 `toolchain\zanc.exe` 编译验证 | 任何客户端 |

前两种会往**项目**里写入（占位符 `<ZAN_MCP>`/`<ZAN_SDK>`/`<ZAN_PROJECT>` 自动
展开成这台 SDK 的真实路径，已有文件不覆盖）：

```
AGENTS.md            AI 在 Zan 项目里必须守的规矩（短，先读它）
.agents\skills\      三个技能（见 §4）
.mcp.json            Claude Code 等大多数客户端
.cursor\mcp.json     Cursor
.vscode\mcp.json     VS Code / Copilot
```

注意：`ai\` 目录里的同名文件是**模板**，占位符原样保留——手工复制无效，
必须走 `--init-agent`。

接好后，AI 的第一个调用应该是 **`zan_start_here`**：一次拿全项目布局、
入口文件、构建命令、编码规矩和工具目录，不用爬目录。

## 3. zan-mcp 工具目录（按用途）

完整清单以 `tools/list` 为准；Zan 语义工具只在 SDK 齐备时出现，不会开出
注定失败的工具。

| 用途 | 工具 |
| --- | --- |
| 入口 | `zan_start_here`（第一调用）、`health_check` |
| 查 API——写码前先查，别猜 | `zan_api_search`（符号检索）、`zan_lookup_symbol`（精确定位定义+签名+摘要）、`zan_list_members`（一个类型的全部成员）、`zan_read_context`（按符号名直接读那段源码窗口） |
| 改之前评估影响 | `zan_find_refs`（谁引用它/谁继承它/多少是测试）、`zan_change_impact`（引用 + 受影响的路由和表）、`zan_project_overview`（工程全貌：模块/路由/实体） |
| 服务器工程 | `zan_route_lookup`（URL→action→权限门）、`zan_orm_lookup`（实体→表→列→门面） |
| 验证——说完要能证 | `zan_compile`（编译一个片段，返回结构化诊断）、`zan_build_project`（编译整个工程） |
| 示例与表单 | `zan_example`（可编译的官方示例，带目录）、`zan_form_schema`（.zform schema + 控件 Props/Events 目录） |
| 文件与检索 | `list_dir` `read_file` `write_file` `edit_file` `search_text` `find_files` `run_command`、`mkdir`/`move_path`/`copy_path`/`delete_path`/`stat_path` |
| 索引维护 | `zan_refresh_index`（加过控制器/实体后重建语义索引）、`zan_refresh_knowledge`（重建 gallery/zform） |
| 技能 | `skills_list`（名字+一行摘要）、`skill_read`（读选中那一个的正文） |

收权开关：`--read-only`（禁写）、`--no-exec`（禁 shell）。HTTP 共享部署见
`docs\MCP_HOSTING.md`（令牌走环境变量，不进配置文件）。

## 4. Skills（按需读一个，别全读）

`skills_list` 只给名字和一行摘要，`skill_read(name)` 才取正文——省上下文。

| 技能 | 什么时候读 |
| --- | --- |
| `zan-development` | 任何写/改 Zan 代码的任务：固定流程 定位→查 API→改→编译→运行 |
| `zan-debugging` | 编译失败、运行不对、内存可疑：诊断分层排查 + ARC 泄漏报告 + LSP/DAP |
| `zan-mcp` | 接 MCP 客户端、MCP 行为异常、或要部署共享 HTTP 服务器 |

位置：模板在 SDK 的 `ai\skills\`，`--init-agent` 后进项目的 `.agents\skills\`。

## 5. 知识库（离线生成物，别手改）

`knowledge\` 下的文件由 `scripts\gen_knowledge.ps1` 从随包 stdlib 生成，
是"查得到"而不是"记得住"的数据源：

| 文件 | 内容 | 谁在用 |
| --- | --- | --- |
| `symbols.json` | 标准库全量符号索引（名字/签名/文件：行/摘要） | `zan_api_search`、IDE 助手面板 |
| `gallery.json` | 金标示例目录 | `zan_example` |
| `zform.json` / `zform.doc.json` | .zform 表单 schema、控件 PropSpec 目录 | `zan_form_schema`、IDE 设计器 |

## 6. 常见问题

- **AI 写的代码编译不过/方法不存在？** 它在猜 API。规矩（`AGENTS.md` §1）是
  写前必须 `zan_api_search` / `zan_example`；让它先调这两个再动手。
- **`<ZAN_MCP>` 字样的文件能直接用吗？** 不能。那是模板占位符，
  用 `tools\zan-mcp.exe --init-agent <项目>` 展开。
- **想换规矩/加技能？** 改 SDK 的 `ai\` 模板，然后在项目里
  `--init-agent --force`（会覆盖它写入的那几个文件，不动你的代码）。
- **多人/CI 共用一个服务器？** 见 `docs\MCP_HOSTING.md`：一个 HTTP 实例服务
  多项目，`--frozen-tools` 保持工具目录稳定，令牌走 `ZAN_MCP_TOKEN`。
- **这个 SDK 目录本身能当工作区吗？** 不能。项目建在别处，SDK 只提供
  IDE、编译器和知识库。
