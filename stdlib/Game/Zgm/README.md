# Game.Zgm

`Game.Zgm` 是 ZanGameMaker 的组件模型与运行时标准库。能力范围参考 DM3
公开文档，但公开命名、类型系统、校验、网络协议和运行时实现均属于 ZGM。
DM3 只作为能力参考，不要求逐项复制其 Lua 文件、字段名称或 API；项目主路径
是 Zan 原生的强类型配置和运行时调用。

当前模块不再只是配置对象集合，已经包含：

- `ZgmProject`：地图、角色、道具、技能、状态、窗口、成长、预制体、皮肤、
  网络与数据库组件注册，重复名称覆盖及跨组件校验；
- `ZgmEngine`：启动/关闭生命周期、扩展初始化、默认地图加载、逐帧更新、
  资源解析、网络运行时创建和存档快照；
- `ZgmSaveRepository`：基于通用 SQLite 标准库的 ZGM 本地存档槽位、快照和
  背包物品、装备、技能、状态、经验和怒气持久化，不要求业务代码手写存档表结构；
- `ZgmGlobals`：供游戏逻辑、UI 与外部适配器共享的强类型全局状态；
- `ZgmMenuRuntime`：菜单、菜单项状态和激活分发；
- `ZgmWorld`：角色生成、成长、属性、经验升级、背包、装备、物品冷却、怒气、技能结算、
  角色/地图格子目标、范围命中、延迟伤害、地图摄像机、坐标转换、基础寻路、
  NPC 目标追踪、优先级技能、动态角色占位、套装阶梯属性、状态周期、移动障碍、
  门点、掉落和拾取；
- `ZgmUiRuntime`：窗口、控件、节点运行状态，鼠标分发、模板变量和补间执行；
- 控件数据绑定：`BindHeroProgress`、`BindHeroShowBox`、`BindHeroSkillBox`、`BindHeroItemBox`
  和 `BindHeroBagBox` 将英雄的血量、角色显示、技能、道具和背包数据绑定到
  渲染器无关的 `ZgmWidgetData`；背包通过 `BoundBagSlotCount`、
  `BoundBagItemNameAt`、`BoundBagQuantityAt` 直接读取本地化的 `ZgmInventory`；
- `ZgmPrefabInstance`：把 Prefab 配置实例化为独立节点状态，支持 Skin、节点查找、
  节点位置修改、透明度、删除和 Tween；
- TCP、WebSocket 与 SQLite 的真实运行时；服务端可以接受、跟踪和关闭
  `ZgmServerClient`，并通过相同的 `ZgmMessage` 帧协议收发消息；
- `ZgmServerEvents`：服务端启动、停止、客户端连接/断开和消息接收事件；
- 富文本构造、音乐状态和渲染器无关的 UI/世界数据。

渲染、音频输出和项目文件格式采用适配器边界。SDL 客户端、编辑器和无界面
服务器可以共享同一套组件语义，而不需要让标准库绑定单一后端。

核心标准库不加载或解释 `App.lua`/`Server.lua` 文件，但会实现其中对应的
应用配置、系统事件、菜单、网络、服务端、数据库和运行时能力。需要迁移旧项目
时，应由独立工具把外部配置转换为 `AppConfig`、`ZgmProject` 和其他 Zan 类型，
而不是让运行时同时维护 Lua 和 Zan 两套配置模型。

常用入口：

```zan
using Game.Zgm;

ZgmProject project = ZgmProject.Create()
    .SetApp(AppConfig.Create()
        .SetTitle("ZGM Game")
        .SetScreen(1280, 720)
        .SetDefaultFont("default-font"))
    .AddMap(MapConfig.Create("start")
        .SetGrid(64, 32, 40, 40)
        .SetDefault(true))
    .AddNpc(NpcConfig.Create("hero")
        .SetHero(true)
        .SetVitals(100, 100, 50, 50));

ZgmEngine engine = ZgmEngine.FromProject(project);
if (engine.Start("")) {
    engine.Update(16);
}
```

技能既可以按角色施放，也可以按地图格子施放：

```zan
SkillConfig burst = SkillConfig.Create("burst", "Burst")
    .SetTarget(SkillTarget.MouseCell)
    .SetRange(6, 0)
    .SetArea(1)
    .SetDamageDelay(120)
    .SetCost(0, 10, 5);

ZgmSkillResult result =
    engine.World().CastSkillAt(hero, "burst", 12, 8);
engine.Update(120);
```

`ZgmSkillResult.Pending()` 在延迟结算前为真，结算后可通过
`TargetCount()`/`TargetAt()` 查看实际命中目标，并通过 `TotalDamage()` 读取
累计伤害。公式使用 ZGM 的受限表达式语法，不加载或解释 Lua。

成长表可以用 `ZgmAttr.Of("升级经验", 50)` 指定当前等级的升级阈值。
运行时调用 `engine.World().GainExperience(actor, amount)`，会自动扣除升级所需
经验、应用下一等级属性，并通过 `ZgmGameplayEvents` 发出经验获得和升级事件。

SQLite 需要显式导入：

```zan
using Game.Zgm.Data;

ZgmDatabase db =
    ZgmDatabase.OpenProject(project, "save-db", "save/game.db");
```

`ZgmSaveState.Capture(world)` 会捕获地图位置、等级、经验、HP/MP、怒气、背包、
装备、已学习技能和状态剩余时间。`ZgmEngine.Load(state)` 会依据项目配置重建
角色并恢复这些运行时数据。

地图运行时通过 `engine.World().Camera()` 获取摄像机，通过
`engine.World().FindPath(startX, startY, targetX, targetY)` 查询绕过障碍格的
路径。需要允许斜向移动时传入第五个 `true` 参数。
角色移动会拒绝进入其他存活角色占用的格子；NPC 使用
`FindPathForActor(actor, targetX, targetY)` 查询带动态角色占位的路径。

NPC 使用：

```zan
NpcConfig enemy = NpcConfig.Create("enemy")
    .SetBehavior(1, -1)
    .AddSkill("bite");
```

`aiLevel > 0` 时，世界每 100ms 执行一次基础 AI：寻找最近敌对角色，
在技能距离外沿路径追踪，进入距离后按 `SkillConfig.Priority()` 选择可施放技能。
`autoCounter > 0` 时，角色受到伤害且仍存活，会立即尝试一次可用的反击技能；
带延迟伤害的技能不会作为即时反击技能。

Prefab 使用：

```zan
ZgmPrefabInstance instance =
    engine.Ui().InstantiatePrefab(
        "button", 100, 60, "button-skin");
instance.SetNodePosition("label", 12, 6);
instance.SetAlpha(0.8);
```

Prefab 实例和普通窗口节点共享 Tween 属性目标，可以直接对实例名称或实例内
节点名称执行 `x`、`y`、`alpha` 等补间属性。

控件绑定使用 Zan 原生的显式来源，不解析或猜测控件名称：

```zan
ZgmUiRuntime ui = engine.Ui();
ui.BindHeroProgress("hud", "hp", "hp", "maxhp");
ui.BindHeroShowBox("hud", "hero", 0);
ui.BindHeroSkillBox("hud", "skill-1", "slash");
ui.BindHeroItemBox("hud", "potion", "potion");
ui.BindHeroBagBox("hud", "bag", "道具", "");

ZgmWidgetState bag = ui.FindWidget("hud", "bag");
int count = ui.BoundBagSlotCount("hud", "bag");
string firstName = ui.BoundBagItemNameAt("hud", "bag", 0);
int firstQuantity = ui.BoundBagQuantityAt("hud", "bag", 0);
```

绑定会随 `engine.Update` 自动刷新。技能绑定包含是否已学习和技能冷却，
道具绑定包含数量和物品冷却，背包绑定按分类/子类过滤现有库存；业务代码
不需要维护一份与角色背包重复的 UI 数据副本。

仓库工具：

```powershell
.\scripts\zgm_tool.ps1 stats
.\scripts\zgm_tool.ps1 test
.\scripts\zgm_tool.ps1 audit
```

完整能力映射和适配器边界见 [Capabilities](Capabilities.md)。
