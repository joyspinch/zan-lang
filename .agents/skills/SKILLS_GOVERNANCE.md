# Skills 治理（已执行）

原则：**skill = AI 可读、可落地的通用规范标准**，不含流水账、不含机器专属
内容、不记日期编号。经验教训沉淀时先问"换个项目还成立吗"，不成立就扔。

## 准入标准（新增/修订 skill 时执行）

1. 规则三要素齐全：动作 + 理由 + 适用边界；缺一不收。
2. 无流水账：日期、会话编号、"本轮/本机/这个 demo"、一次性排查过程不进正文。
3. 被推翻的旧规则同一提交删除，不留僵尸条目。
4. 单 skill ≈300 行内；逐坑细节/长流程挪 references/ 子文件。
5. description ≤2 行，只写真实触发场景。
6. 机器专属内容（路径/尺寸/环境限制）进项目文档或代码注释，不进 skill。
7. **路径必须能落地**：skill 里写的文件/脚本在仓库里要真实存在。搬过家的
   东西要在同一提交里改掉引用（GUI 源码在 `packages/Zan.Gui/src/Gui`，
   不在 `stdlib/Gui`；IDE 在 `src/ide_zan/src/`，入口是 `src/IdeForm.html`）。
   机器绝对路径一律换成仓库相对路径或 `<ZAN_SDK>` 一类占位符。

## 当前 skill 清单（工作区 13 个）

| skill | 定位 | 发布包 |
|---|---|---|
| zan-dev-standards | Zan 开发总纲：工作流闭环、代码规范、验证纪律、沉淀纪律 | 是 |
| zan-workflow | 版本提交与任务管理规范：提交信息格式、提交纪律、任务台账、会话卫生 | 是 |
| app-migration | 业务程序迁移与复刻规范：C#/强类型/业务系统向 Zan 迁移的方法论与纪律 | 是 |
| gui-design | GUI 排版与审美规范（含 references/ 细节） | 是 |
| game-dev | 游戏帧循环/HUD/手感规范 | 是 |
| server-dev-standards | 服务端架构规范 | 是 |
| data-modeling | 数据建模通用准则（桌面/游戏/服务端） | 是 |
| server-db-design | SQL 专属数据层细则 | 是 |
| zan-compiler-internals | 编译器内部契约（改 src/compiler 才触发） | 是 |
| zan-development | 写/改 Zan 代码的固定流程（SDK 使用面） | 是 |
| zan-debugging | 诊断跑不起来的 Zan 程序（SDK 使用面） | 是 |
| zan-mcp | 驱动 SDK 的 MCP 服务（SDK 使用面） | 是 |
| zan-sdk-agent | 仓库内配套工具用法：查地图定位、查影响面、算该跑哪组 ctest | 否（仅工作区） |

**活源与发布变体**：工作区 `.agents/skills/`（13 个）是活源；发布变体
`tools/ai_pack/skills/`（12 个，不含仓库专属的 zan-sdk-agent）随 SDK 进
`dist/ai/skills/`。两处必须同一提交更新（规则 13），全局副本
`~/.agents/skills/` 由 `scripts/sync_skills.ps1 -SyncGlobal` 同步。

## 文档与 skill 的边界（HTML/设计器等声明层不新建 skill）

HTML 声明层（`App.LoadHtmlWith`/GenHtml 编译期展开/`data-on-*` 协议/
tag→控件映射/空白语义/设计器 .html P7a、P7b 字段内联 style）的能力
全集与差异台账在**仓库文档** `docs/HTML_UI.md`（随代码更新），
布局语义在 `docs/archive/WEB_GUI_ROADMAP.md`（已全量落地，归档留档）；skill 侧只在 gui-design 的
`references/css-dialect.md` 留"HTML 声明窗口"一节摘要与指针。
理由：docs 跟代码同仓库同提交，是能力面的权威；skill 只放"写界面时
要遵守的规范"。能力清单在 docs、行为规范在 skill，两边不重复维护。

## 已执行的历史处置

- 2026-09 删除 11 个：testing-* 7 个（实机验证仪式，机器专属内容混入，可迁移规则
  已提炼进 zan-dev-standards 第三节）、zan-lsp-intellisense / zan-dap-debugging /
  zan-designer-components / game-online / app-migration（项目专属全链路定式，
  历史内容在 git 历史可找回）。
- 2026-10 **app-migration 重新加入**（通用方法论改写版），其余三个
  SDK 使用面 skill（zan-development / zan-debugging / zan-mcp）同期加入，
  仓库专属的 zan-sdk-agent 只进工作区。
- 全部保留 skill 已去日期流水、收短 description；game-dev 删除进行中的
  会话记录节。
- ai_pack 发布变体只保留规范类与 SDK 使用面；全局 ~/.agents/skills 同步收敛。
