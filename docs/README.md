# docs 索引

本目录共 18 份现行文档（+1 份数据文件）。2026-09-26 深度整合：AI 系文档移至
`tools/ai_pack/docs/`（随 SDK 发布，dist 布局不变），同主题文档并入对应主文档
（RELEASE、HTML_UI、STDLIB_COMPONENT_STANDARDS、WORKSPACE_CONVENTIONS、
GAME_ENGINE_GUIDE 各自的附录），逐文件仓库地图与编译器架构总览分别由
`agent-kb/project-map.md` 与 `archive/ARCHITECTURE.md` 承担。

维护规则见 [WORKSPACE_CONVENTIONS.md](WORKSPACE_CONVENTIONS.md)《附录：docs
文档维护规则》：每份文档分三层——**当前实现**（描述代码此刻真实行为）、
**目标设计**（未落地意图，挂 `TASKS.md` 能力编号）、**历史归档**（`archive/`，
冻结）。新增/移动/归档文档时**同一次提交**更新本索引。

> 根目录文档被源码注释、脚本和 skills 按路径锚定（如 `publish_ide.ps1` 的
> 发布清单），移动前先全仓库 grep 旧路径。

## 语言与运行时

| 文档 | 内容 |
| --- | --- |
| [SPEC.md](SPEC.md) | 语言规范——语法与语义的权威 |
| [ABI.md](ABI.md) | 二进制接口：类型布局、调用约定、运行时 ABI |
| [ASYNC_CPS_DESIGN.md](ASYNC_CPS_DESIGN.md) | async/await 的 CPS 下降设计 + I/O 反应器现状（编译器/运行时注释锚定） |
| [SECURITY.md](SECURITY.md) | 内存安全/unsafe/FFI 的当前行为（含"不存在 X"负向事实清单） |

## 编译器与工具链

| 文档 | 内容 |
| --- | --- |
| [ZANC_CLI.md](ZANC_CLI.md) | `zanc` 全部命令行参数与已知坑——改参数解析时同提交更新 |
| [BUILD_TOOLCHAIN.md](BUILD_TOOLCHAIN.md) | 各平台固定的构建工具链（勿自行更换） |
| [platform-targets.md](platform-targets.md) | 交叉编译目标平台矩阵 |
| [TOOLING.md](TOOLING.md) | `zan-lsp` / `zan-dap` 协议与能力 |
| 仓库代码地图 | `AGENTS.md` 目录表 + [agent-kb/project-map.md](agent-kb/project-map.md)（需求 → 改哪儿，带 grep 关键字） |

## 标准库与 GUI

| 文档 | 内容 |
| --- | --- |
| [STDLIB.md](STDLIB.md) | 标准库总览（含 §3.7 任务/通道/异步 I/O 现状）——写 Zan 代码前先查这里 |
| [STDLIB_COMPONENT_STANDARDS.md](STDLIB_COMPONENT_STANDARDS.md) | 组件编写标准 + 取色取字样式解析门禁（附录） |
| [HTML_UI.md](HTML_UI.md) | HTML+CSS 声明式窗口协议 + web 等价布局驱动与 oracle（附录） |
| [GAME_ENGINE_GUIDE.md](GAME_ENGINE_GUIDE.md) | 游戏引擎使用指南 + 资产管线（附录） |

## 发布与工具链状态

| 文档 | 内容 |
| --- | --- |
| [RELEASE.md](RELEASE.md) | 发布流程 + IDE 发布布局（附录 A）+ 自包含工具链（附录 B）；`package_bundle.sh` 锚定 |

## 仓库协作

| 文档 | 内容 |
| --- | --- |
| [WORKSPACE_CONVENTIONS.md](WORKSPACE_CONVENTIONS.md) | 仓库硬规则全文（hooks 强制）+ docs 维护规则（附录）；`AGENTS.md` 是其摘要 |

## 活跃设计 / 账本（目标设计层）

| 文档 | 内容 |
| --- | --- |
| [ENGINE_REDESIGN.md](ENGINE_REDESIGN.md) | 渲染引擎增强 + 游戏引擎重设计（A356 挂账，未实施） |
| [EVOLUTION_PLAN_CSHARP_ALIGNMENT.md](EVOLUTION_PLAN_CSHARP_ALIGNMENT.md) | 与 C# 对齐并超越的标准库/工具演进路线 |
| [CHART_COMPATIBILITY_5.md](CHART_COMPATIBILITY_5.md) | Chart 与 ECharts 6.x 官方示例的迁移账本 |
| [CHART_CODE_GAP_LEDGER.md](CHART_CODE_GAP_LEDGER.md) | Chart 引擎代码级缺口账本（`chart_gap_audit.py` 读写 [chart_gap_baseline.json](chart_gap_baseline.json)） |

## 专项目录

| 目录 | 内容 |
| --- | --- |
| [agent-kb/](agent-kb/README.md) | 智能体开发知识库：怎么找到该改的地方、怎么证明改对了、怎么定位根因；含编译器 C 标准（coding-standards.md）与自举（bootstrap.md） |
| [bugs/](bugs/) | 编译器 bug 复盘（每份带 Status 字段）——stdlib 与各 SDK 包的代码注释按路径引用 |
| [projects/zanide/](projects/zanide/STRUCTURE.md) | ZanIDE 项目专属文档（STRUCTURE.md 由 `check_structure.ps1` 强制） |
| [arpg/](arpg/00-DM3体系总览.md) | ARPG（DM3 体系）参考资料（`templates/game/legend` 引用） |
| [archive/](archive/) | 历史/过时文档，冻结不随实现更新 |
| [compiler-finally-cost-2026-10-08](archive/compiler-finally-cost-2026-10-08.md) | async finally 共享发射的规模探针、冻结 OnePlus 对照及已知异常泄漏边界（时点报告） |

## AI 协作文档（不在本目录）

随 SDK 发布的 AI 接入文档在 `tools/ai_pack/docs/`：`AI_README.md`（给人的
总说明）、`AI_ONBOARDING.md`（客户端接入）、`ai-assist.md`（RepoMap/MCP 桥
现状）、`AI_DEV_INFRASTRUCTURE.md`（方案层）、`MCP_HOSTING.md`（共享部署）。
