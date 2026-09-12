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

## 当前 skill 清单（工作区触发面 8）

| skill | 定位 |
|---|---|
| zan-dev-standards | Zan 开发总纲：工作流闭环、代码规范、验证纪律、沉淀纪律 |
| gui-design | GUI 排版与审美规范（含 references/ 细节） |
| game-dev | 游戏帧循环/HUD/手感规范 |
| server-dev-standards | 服务端架构规范 |
| data-modeling | 数据建模通用准则（桌面/游戏/服务端） |
| server-db-design | SQL 专属数据层细则 |
| zan-compiler-internals | 编译器内部契约（改 src/compiler 才触发） |

## 已执行的历史处置（2026-09）

- 删除 11 个：testing-* 7 个（实机验证仪式，机器专属内容混入，可迁移规则
  已提炼进 zan-dev-standards 第三节）、zan-lsp-intellisense / zan-dap-debugging /
  zan-designer-components / game-online / app-migration（项目专属全链路定式，
  历史内容在 git 历史可找回）。
- 全部保留 skill 已去日期流水、收短 description；game-dev 删除进行中的
  会话记录节。
- ai_pack 发布变体只保留规范类；全局 ~/.agents/skills 同步收敛。
