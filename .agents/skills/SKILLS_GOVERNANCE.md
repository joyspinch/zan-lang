# Skills 治理方案（待用户裁决）

日期：2026-09-13。背景：用户指出 skill 数量过多（工作区 19 个 + 全局 12 个），
很多会形成干扰、降低 AI 开发效率；skill 的定位应当是**开发规范与标准**，
用于提高 AI 对 Zan 项目的开发/维护/调试效率——不是项目流水账。

## 干扰的真实来源（盘点证据）

1. **触发面过宽**：19 个 skill 常驻 description，每轮对话全部在场。
   description 越多越长，误触发越多（比如改一行 GUI 代码被拉进 626 行排版规范）。
2. **机器/路径专属内容混进"规范"**：testing-charts-gallery 里有
   `D:/project/zan-lang/_scratch/...` 硬路径、"本机更高的画布"这类一次性调试痕迹
   （共 5 处）；testing-gui-graphview、testing-gui-gallery、
   testing-server-mvc-admin 也各有"on this box"段落——这些是**某台机器的操作
   记录**，对其他环境是纯噪音。
3. **一次性项目事件占位**：testing-server-mvc-admin（53 行）整篇就是
   "本机起 server-mvc 实例并点开 /admin/monitor"的流水，不含可迁移规范。
4. **体量失衡**：testing-charts-gallery 1080 行、zan-compiler-internals 1128 行
   ——被触发一次就是巨量 token；多数内容是逐坑流水而非决策规则。

## 处置方案（三档）

**A 保留（通用规范/标准，跨环境可用）** —— 6 个：

| skill | 定位 |
|---|---|
| zan-development | Zan 编码固定工作流（全局已有，工作区重复=应删除副本） |
| zan-debugging | 诊断链路（全局已有，同上） |
| zan-mcp | MCP 驱动（全局已有，同上） |
| server-dev-standards | 服务端架构规范（本轮产出） |
| data-modeling | 数据建模通用准则（本轮产出） |
| server-db-design | SQL 细则（本轮产出） |
| app-migration | 复刻迁移方法论（触发词明确，方法论级） |
| gui-design | GUI 排版规范（排版原语/风格配方是规范性质） |
| game-dev | 帧循环/手感方法论（规范性质） |

**B 合并归档（项目专属操作仪式，从触发面撤下）** —— testing-* 7 个：

- testing-gui-screenshot、testing-zanide-uidriver、testing-android-native
  ：内容健康（几乎无硬路径），但属"实机验证仪式"，合并为一个
  `testing-zan`（一个入口，内部分节/引用文件），或整体移到
  `docs/verification/` + AGENTS.md 链接，需要时人工读取，不占触发面。
- testing-charts-gallery、testing-gui-gallery、testing-gui-graphview、
  testing-server-mvc-admin：先剔除机器专属段落（硬路径/本机尺寸/this box），
  有留存价值的节（数值 oracle、probe 定式）并入合并版，其余归档。

**C 删除**：

- 工作区与全局重复的三份（zan-development、zan-debugging、zan-mcp 的
  工作区副本——全局已是字节级同内容，双份白占触发面）。

## 目标终态

- 工作区触发面从 19 收敛到 **9 以内**；description 全部收短到 ≤2 行，
  触发词只保留真实场景。
- 通用规范类（server-dev-standards/data-modeling/server-db-design/gui-design/
  game-dev/app-migration）是主体；项目专属仪式退到按需读取的引用文件。
- 三副本纪律不变，但 ai_pack 只随 SDK 发布通用规范类。

## 待用户裁决

1. B 档 testing-* 是"合并为一个 testing-zan 保留触发"还是"整体移出 skills
   目录改 docs 引用"？（推荐后者：验证仪式是操作手册不是规范，移出后
   AGENTS.md 指个路径即可）
2. C 档删工作区重复副本，确认？
3. gui-design（626 行）与 game-dev（383 行）保留但收短 description + 
   把逐坑细节挪 references/ 子文件，确认？
4. 本轮新产出的三个规范 skill 是否也需要瘦身（server-db-design 65 行/
   data-modeling 95 行/server-dev-standards 91 行，已较精炼）？
