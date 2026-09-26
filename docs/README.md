# docs 索引

`docs/` 全部文档的地图。维护规则见
[DOCS_MAINTENANCE.md](DOCS_MAINTENANCE.md)：每份文档分三层——**当前实现**
（描述代码此刻真实行为）、**目标设计**（未落地意图，挂 `TASKS.md` 能力编号）、
**历史归档**（`archive/`，冻结）。新增/移动/归档文档时**同一次提交**更新本索引。

> 根目录文档被源码注释、脚本和 skills 按路径锚定（如 `publish_ide.ps1` 的
> 发布清单），移动前先全仓库 grep 旧路径。

## 语言与运行时（当前实现）

| 文档 | 内容 |
| --- | --- |
| [SPEC.md](SPEC.md) | 语言规范——语法与语义的权威 |
| [ABI.md](ABI.md) | 二进制接口：类型布局、调用约定、运行时 ABI |
| [ASYNC_CPS_DESIGN.md](ASYNC_CPS_DESIGN.md) | async/await 的 CPS 下降设计 + I/O 反应器现状（编译器/运行时注释锚定） |
| [SECURITY.md](SECURITY.md) | 内存安全/unsafe/FFI 的当前行为（含"不存在 X"负向事实清单） |

## 编译器与工具链

| 文档 | 内容 |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 编译器架构总览（2026-07 后未逐节复审，细节以 `src/compiler/` 为准） |
| [ZANC_CLI.md](ZANC_CLI.md) | `zanc` 全部命令行参数与已知坑——改参数解析时同提交更新 |
| [BUILD_TOOLCHAIN.md](BUILD_TOOLCHAIN.md) | 各平台固定的构建工具链（勿自行更换） |
| [platform-targets.md](platform-targets.md) | 交叉编译目标平台矩阵 |
| [BOOTSTRAP.md](BOOTSTRAP.md) | 自举三代 fixpoint（gen0→gen1→g2/g3 字节等值） |
| [SELF_CONTAINED_TOOLCHAIN.md](SELF_CONTAINED_TOOLCHAIN.md) | 自包含工具链：MinGW ABI + 随包 `ld`，已落地 |
| [TOOLING.md](TOOLING.md) | `zan-lsp` / `zan-dap` 协议与能力 |
| [CODING_STANDARDS.md](CODING_STANDARDS.md) | 编译器 C11 开发标准 |
| 仓库代码地图 | `AGENTS.md` 目录表 + [agent-kb/project-map.md](agent-kb/project-map.md)（需求 → 改哪儿，带 grep 关键字） |

## 标准库与 GUI

| 文档 | 内容 |
| --- | --- |
| [STDLIB.md](STDLIB.md) | 标准库总览——写 Zan 代码前先查这里 |
| [STDLIB_COMPONENT_STANDARDS.md](STDLIB_COMPONENT_STANDARDS.md) | 组件编写标准（通道/契约/样式解析） |
| [HTML_UI.md](HTML_UI.md) | HTML+CSS 声明式窗口协议（运行时装载 + GenHtml 编译期展开） |
| [ui-driver.md](ui-driver.md) | web 等价布局驱动与 oracle 对拍 |
| [GUI_STYLE_RESOLUTION.md](GUI_STYLE_RESOLUTION.md) | 取色取字一律走样式层的三条门禁 |
| [GAME_ENGINE_GUIDE.md](GAME_ENGINE_GUIDE.md) | 游戏引擎使用指南 |
| [ASSET_PIPELINE.md](ASSET_PIPELINE.md) | 资产管线：IDE 资产管理器 → `.zrp` 资源包 |

## 发布与运维

| 文档 | 内容 |
| --- | --- |
| [RELEASE.md](RELEASE.md) | 发布流程与包布局（`package_bundle.sh` 锚定） |
| [IDE_PUBLISH.md](IDE_PUBLISH.md) | IDE 发布布局（`dist\<platform>`） |

## AI 协作

| 文档 | 内容 |
| --- | --- |
| [AI_ONBOARDING.md](AI_ONBOARDING.md) | AI 客户端接入指南（英文，随 SDK 发布） |
| [AI_README.md](AI_README.md) | 给人的总说明：AI 怎么用这个 SDK |
| [AI_DEV_INFRASTRUCTURE.md](AI_DEV_INFRASTRUCTURE.md) | 方案层：AI 辅助开发基础设施（与现状解耦） |
| [ai-assist.md](ai-assist.md) | 现状层：RepoMap / MCP 桥契约 / IDE AI 设置 |
| [MCP_HOSTING.md](MCP_HOSTING.md) | MCP 服务器 stdio/HTTP 共享部署 |

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
| [agent-kb/](agent-kb/README.md) | 智能体开发知识库：怎么找到该改的地方、怎么证明改对了、怎么定位根因 |
| [bugs/](bugs/) | 编译器 bug 复盘（每份带 Status 字段，规则见 DOCS_MAINTENANCE §4） |
| [projects/zanide/](projects/zanide/STRUCTURE.md) | ZanIDE 项目专属文档（STRUCTURE.md 由 `check_structure.ps1` 强制） |
| [arpg/](arpg/00-DM3体系总览.md) | ARPG（DM3 体系）参考资料（`templates/game/legend` 引用） |
| [archive/](archive/) | 历史/过时文档，冻结不随实现更新 |
