# 09 · Zan 游戏开发借鉴报告 —— DM3 迁移现状与下一步

> 本文是 DM3（DreamMod3）文档体系分析（00~08 篇）的收口：对照仓库中已完成的 Zan 化迁移成果，
> 回答"Zan 要开发游戏，还能从 DM3 借鉴什么"。
> 依据：stdlib/Game/Arpg（10,191 行/30 文件）、examples/game/legend（6,306 行）、
> examples/game/common/GameKit.zan（1,524 行）、scripts/legend2_tool.ps1（631 行）、examples/game/ra2。
> 评审方法：架构师（完成度矩阵）+ 工程师（工程实践模式）双线独立评审后汇总。

---

## 一、迁移现状一句话

**DM3→Zan 的类型化迁移约完成 70%：结构层（配置/属性/战斗结算/数据绑定/持久化/网络 facade）已反超 DM3 对应物，
但 DM3 文档行数最多的两块——表现层（物件/动画/音频）与行为层（AoE/仇恨 AI/掉落）尚未迁移；
同时 Game.Arpg 库零消费、GameKit 九个消费者，两条线各自健壮但未打通。**

### 完成度矩阵（架构师评审）

| DM3 机制 | Zan 落点 | 状态 |
|---|---|---|
| 组件配置表（Lua table） | ArpgConfig / ArpgValue 类型化树 | ✅ 已迁移（且更严格：Validate 交叉引用） |
| 10 基础属性 + 扩展属性 | ArpgAttributes / ArpgCustomAttribute | ✅ 已迁移（无 `_百分比` 双模式，见 P1-3） |
| 命中公式 `a/(a+b)×100` | Runtime.zan `ArpgCombat.HitChance` | ✅ 逐字对应 |
| 暴击 `随机(1,100)≤暴击` | Runtime.zan + ArpgRandom.LCG | ✅ 确定性可复现 |
| 伤害公式字符串表达式 | Formula.zan ArpgFormula（受限整数求值器） | ✅ 迁移（缺 rand/c.对象，见 P0-3） |
| Buff/技能/物品定义 | Combat.zan 六个 Definition 类 | ✅ 已迁移（Threat/RageCost/AlwaysHit 已有） |
| 地图/出生点/传送门 | Map.zan ArpgMapSpawn/ArpgMapPortal | ✅ 已迁移 |
| UI 皮肤/九宫贴图/拖拽 | UiRuntime.zan（2,390 行 ArpgNineSlice/事件冒泡） | ✅ 已迁移，超出 DM3 结构 |
| 富文本标记 | RichText.zan | ✅ 已迁移 |
| 模板变量 `&path&` | DataBinding.zan（+{if} 条件块） | ✅ 已迁移 |
| Tween 缓动（31 曲线） | ArpgTween | ✅ 已迁移（Architecture.md 误标 Deferred） |
| 网络层（TCP+WS） | NetRuntime.zan + Server.zan（2MB 上限） | ⚠️ facade 已迁，心跳/重连未做 |
| Sqlite 存档 | Data/Db.zan + Save.zan（规范化 schema + EnsureColumn 迁移） | ✅ 已迁移，**优于** DM3 整块序列化 |
| **AoE 伤害范围（点/面/角度+延迟多段）** | — | ❌ **未迁移（P0）** |
| **NPC AI（巡逻/仇恨表/威望/视野/自动反击）** | — | ❌ **未迁移（P0）** |
| 掉落表 | — | ❌ 未迁移（P1） |
| 装备套装 | — | ❌ 未迁移（P2） |
| 资源表 `{标识,文件}` 间接层 | — | ❌ 未迁移（P1，GameKit 侧补） |
| `-b` 发布打包 / F12 调试层 / `-n` 脚手架 | — | ❌ 工具链侧未迁移（P1/P2） |

---

## 二、直接可用的能力清单（今天就能用的）

**库层（stdlib/Game/Arpg）——注意：目前零 example 消费者，首次接入可能暴露 API 易用性问题：**

| 能力 | 入口 | 备注 |
|---|---|---|
| 项目校验 | `ArpgProject.AddMap/AddActor/AddItem/... + Validate()` | 交叉引用检查，配置错误在启动期报 |
| 战斗结算 | `ArpgCombat`（命中/暴击/伤害） | 公式与 DM3 对齐 |
| 确定性随机 | `ArpgRandom`（LCG + NextPercent） | 种子可复现，挂机/回放友好 |
| UI 运行时 | `ArpgUiRuntime` + `ArpgNineSlice`（2,390 行） | 九宫/拖拽/事件冒泡，全标准库组件 |
| 数据绑定 | `DataBinding`（`&path&` + {if}） | 配置→UI 直连 |
| 表达式公式 | `ArpgFormula`（a./b. 属性 + 四则/幂/min/max/clamp） | 沙箱化，无副作用 |
| 缓动 | `ArpgTween`（31 条曲线）+ `ArpgScheduler` | 延迟调度骨架已就位（AoE 多段的宿主） |
| 持久化 | `Data/Save.zan`（SQLite 规范化 + EnsureColumn） | 存档版本迁移内建 |
| 网络 | `ArpgTcpServerRuntime` / `ArpgWebSocketRuntime` | 2MB 消息上限；心跳/重连需自补 |

**示例工程层（可整工程抄结构的"模板"）：**

| 工程 | 价值 |
|---|---|
| examples/game/legend（传奇放置） | 三文件拆分（Data 598/Game 2,614/main 3,094）、影子单机化、key=value 存档、LEGEND_SIM=1 无头自测的完整范本 |
| examples/game/ra2 | 分层单向依赖（formats 无 SDL → assets → sim → render → game → main）的标准分层模板 |
| examples/game/common/GameKit.zan | 被 9 个 example 消费的事实标准：exe 锚定资源、内嵌字体三级回落、Host SDL 封装、Ui 烘焙 |
| scripts/legend2_tool.ps1 | manifest（legend2.project.json）→ 生成 `*.g.zan` 工厂的 L2 元数据驱动工作流，8 种组件 kind |

**测试基线**：tests/conformance 已有 7 个 arpg_*.zan（database/formula/net/net_runtime/save_repository/tween/ui_runtime）。

---

## 三、从 DM3 再借的 10 条具体机制（含落点文件）

> 排序原则：先补"传奇味"的核心手感（P0），再补工作流效率（P1），最后锦上添花（P2）。

### P0 —— 决定游戏手感的三件事

**1. AoE 伤害范围（点/面/角度）+ 延迟多段**
- DM3：伤害范围 3 类型（点=圆形半径 / 面=矩形区域 / 角度=扇形），支持延迟多段命中（刀光过后才结算）。
- 落点：`stdlib/Game/Arpg/Combat.zan` 加 `ArpgSkillAoe`（Shape: Point/Plane/Arc + Radius/Width/Angle + DelayMs + HitCount/HitIntervalMs）；
  延迟结算挂在已有的 `ArpgScheduler` 上（骨架已就位，无需新调度器）；
  命中判定需要 `ArpgMapDefinition` 提供格子几何（斜 45 距离换算）。
- 理由：这是传奇式 ARPG 手感的核心，DM3 文档中占比最大的玩法块。

**2. NPC 仇恨/威望 AI**
- DM3：巡逻路线、仇恨表（多目标按威胁排序）、威望、视野范围、自动反击。
- 落点：`stdlib/Game/Arpg/Entity.zan` 的 ArpgActor 加 `threat` 表（actorId→value，技能 Threat 值已在 Combat.zan 定义，正好接上）+
  `patrolRoute` + `visionRange` + `aggroMode`；行为态机（Idle/Patrol/Chase/Attack/Return）放 `Runtime.zan` 或新 `Ai.zan`。
- 理由：ArpgCombat 已产 Threat 值却无消费者；补上后战斗闭环才完整。

**3. 伤害公式扩展（不照搬 Lua，但要够用）**
- DM3：公式是字符串表达式（a./b./c. 三对象）或 Lua 函数。
- Zan 取舍：**不要** Lua 函数形态（失去类型安全与确定性）；保留字符串表达式走 `ArpgFormula`，但需：
  ① 加 `c.`（技能对象）属性访问；② 求值器升 double（现在整数，除法截断会毁手感）；
  ③ 加 `rand(a,b)`——**必须在 Validate 期静态检查**，保证同一 seed 下可复现（用 ArpgRandom 注入而非系统随机）。
- 落点：`stdlib/Game/Arpg/Formula.zan` + `Project.zan` Validate。

### P1 —— 工作流与内容生产

**4. 掉落表（loot table）**
- DM3：怪物→掉落组（物品+权重+数量区间）。
- 落点：`stdlib/Game/Arpg/Combat.zan` 加 `ArpgDropDefinition`（itemId/weight/min/max/条件）；legend2_tool.ps1 加 `-Kind drop`。
- 理由：P0-2 的仇恨 AI 打完怪，下一步必然是掉落；成本极低。

**5. 资源表间接层 `{标识,文件}` + 在线素材**
- DM3：资源经 `{标识,文件}` 表间接引用，`dmstore_` 前缀支持在线素材库。
- 落点：`examples/game/common/GameKit.zan` 的 Assets 加 `FindById(id)`（manifest 资源表：id→相对路径）；
  在线素材可先做 `scripts/fetch_assets.ps1`（拉清单+校验 hash），不急着做运行时在线加载。
- 理由：换皮/换素材零代码改动，是配置驱动的最后一公里。

**6. `-b` 一键发布打包**
- DM3：`App.exe -b` 产出可直接分发的目录。
- 落点：新建 `scripts/pack_game.ps1`（exe + assets + manifest 清单 + 版本号），供 9 个 GameKit example 通用。
- 理由：Zan 侧目前完全没有发布工作流；脚本级成本，收益长期。

**7. Architecture.md 纠偏 + 战斗内核专项测试**
- 落点：`stdlib/Game/Arpg/Architecture.md` 的 Deferred 清单改为"facades 已实现、运行时接线（心跳/重连/在线资源）延后"——现状是 tween/网络/SQLite 已实现但文档滞后；
  新增 `tests/conformance/arpg_world.zan`（AoE/仇恨/战斗内核端到端）与 `arpg_databinding.zan`（模板变量+{if}）。
- 理由：文档滞后会让下一个接入者误判能力边界；测试缺口恰好盖住 P0-1/2 的落点。

### P2 —— 提效项

**8. `-n` 模板脚手架（带样例模板库）**
- DM3：`-n` 按模板生成新组件配置文件。
- 落点：`scripts/legend2_tool.ps1` 的 `init` 支持从模板库拷贝（每种 kind 配一个带注释的样例 json）。

**9. 扩展属性 `_百分比` 双模式**
- DM3：扩展属性声明一处，全组件生效，`属性名`/`属性名_百分比` 双模式。
- 落点：`stdlib/Game/Arpg/Entity.zan` ArpgCustomAttribute 加 percent 标志，`ArpgAttributes` 汇总时区分加法/乘法两段。

**10. F12 调试覆盖层 + `&path&` 手写工程约定**
- F12：DM3 引擎内置调试面板（属性/坐标/帧率）。落点：`GameKit.Host` 加 `DebugOverlay`，`#if !PUBLISH` 排除发布版。
- `&path&`：Zan 已在 DataBinding 实现此机制，建议下放为手写工程（legend 风格）的文案约定，统一两侧习惯。

### 明确不借的三条（架构判断，防止走回头路）

| DM3 形态 | Zan 替代 | 理由 |
|---|---|---|
| Lua 动态 table（无 schema） | ArpgValue 类型化树 + ArpgProject.Validate | 类型安全 + 启动期校验是 Zan 相对 Lua 的代差优势 |
| Lua 函数伤害公式 | Zan delegate + ArpgFormula 扩展 | 函数公式破坏确定性回放；沙箱表达式可静态审计 |
| 存档整块序列化 | Save.zan 规范化 SQLite + EnsureColumn | 现有实现版本迁移能力优于 DM3，勿降级 |

---

## 四、Game.Arpg 与 legend/GameKit 的收敛建议（工程师评审）

**结论：不合并，分层定位。**

| | GameKit | Game.Arpg |
|---|---|---|
| 定位 | 手写游戏的便携运行库 | DM3 式数据驱动运行时 |
| 消费者 | 9 个 example（事实标准） | **0 个** |
| 适配 | 程序员手工编码玩法 | 配置表驱动 + 生成代码 |

具体动作：

1. **共同底座下沉**：Rng / Utf8 / 资源定位（Assets）/ 字体三级回落从 GameKit 抽到 `stdlib/Game/Foundation`（新目录），
   GameKit 与 Game.Arpg 都改为引用之——避免两套随机数/字体/资源逻辑漂移。
2. **给 Game.Arpg 造第一个真实消费者**（当前最大风险：10,191 行零消费的库=未经实战检验的 API）：
   走 legend2_tool.ps1 的 manifest → `generate` 产出 `*.g.zan` → 一个最小 legend 式示例接入 Game.Arpg。
   消费过程中暴露的 API 别扭处，改库不改例。
3. **legend2_tool 是 L2 元数据驱动的正确方向，予以坚持**：json 单一事实源 → 生成工厂 + sources.txt。
   扩展 kind：drop（见 P1-4）、database；中期可把生成器从 PowerShell 迁为 Zan 自举（仓库已有 selfhost 先例）。
4. **ra2 分层（formats 无 SDL → assets → sim → render → game → main）定为标准分层模板**，写入本文档即为定版，
   后续新游戏工程照此开目录。

---

## 五、下一步优先级

### P0（下一个开发窗口）
1. `ArpgSkillAoe`（点/面/角度 + 延迟多段，挂 ArpgScheduler）→ Combat.zan / Map.zan
2. NPC 仇恨/巡逻 AI（threat 表 + patrolRoute + 行为态机）→ Entity.zan / 新 Ai.zan
3. ArpgFormula 升级（c. 对象 + double + 受控 rand）→ Formula.zan / Project.zan Validate
4. Architecture.md 纠偏 + 新增 arpg_world.zan / arpg_databinding.zan 专项测试
5. Game.Arpg 首个消费示例（legend2_tool generate → 最小接入示例）

### P1（P0 验证后）
6. 掉落表 ArpgDropDefinition + legend2_tool `-Kind drop`
7. GameKit.Assets.FindById 资源表 + fetch_assets.ps1
8. scripts/pack_game.ps1 发布打包
9. Game/Foundation 底座下沉（Rng/Utf8/Assets/字体）

### P2（有空再做）
10. legend2_tool init 模板库
11. 扩展属性 `_百分比` 双模式
12. GameKit.Host.DebugOverlay（F12）+ `&path&` 手写工程约定
13. 网络层心跳/自动重连补全（NetRuntime.zan）
14. 生成器 Zan 自举

---

## 附：本文证据链

- 架构师评审：DM3→Zan 完成度矩阵（25 机制逐项对照）+ 4 条架构判断
- 工程师评审：11 条 Zan 游戏工程实践模式 + GameKit/Game.Arpg 消费者分析 + Top5 借鉴清单
- 原始文档分析：本目录 00~08 篇（62 篇 DM3 文档 / 29,755 行）
- 代码落点全部经实际读源核对（Runtime.zan / Combat.zan / Entity.zan / Formula.zan / UiRuntime.zan / Data/Save.zan / legend 三件套 / GameKit / legend2_tool / ra2）
