# 数据字典与生效范围

## 数据链与生效范围

原版数据已入库服务端 `templates/server/server-legend/data/M2.DB`（权威载体）；
服务端侧还有一份本目录规则表的发布副本 `data/rules/`，由
`templates/server/server-legend/tools/sync_rules.py` 单向同步：

- 原表：`GameItem/GameMap/GameMonster/GameBoss/GameSkill/GameSc/GameTitle/
  GameCity/GameAchieve/GameFw/GameJue/GameShuxing/GameTask`——从迷你传奇
  `data/data.dll` 只读导出，逐字保留 ID、原字段与空值；`GameSource` 记录
  来源 SHA256，`GameMeta` 记录各表行数。
- 数据链：`data.dll --import_reference.py--> reference/*.csv --> M2.DB`。
  `reference/` 是一次性中间产物，导出后不入库；需要重同步时重跑
  `import_reference.py` 再入库。入库后 M2.DB 即只读种子，运行期不写回。
- 本目录的运行时 CSV：暂由 `src/Tables.zan` 启动读取校验；正逐步改为
  客户端走协议取服务端数据，完成后本目录仅保留资产映射并删除其余 CSV。
  **改原版数据改 M2.DB（或重跑同步），改模板数值才改这里的 CSV。**

| 表 | 主要职责 |
|---|---|
| items（732） | 物品定义、装备十二属性、槽位、需求、元数据 |
| monsters（36）、bosses（28） | 战斗属性、伤害类型、奖励；原始六条无生命怪物占位记录仅保留在 reference |
| maps（35）、drops（234） | 地图及掉落装备池；运行时掉落权重属于模板规则，不冒充原版爆率 |
| slots（12）、qualities（5） | 穿戴组、品质权重/倍率/系数区间 |
| jobs（3）、skills（24）、levels（1500） | 角色基础/每级属性、3 职业 × 8 技能槽、经验曲线 |
| attributes（12） | 各属性评分权重、强化比例、修炼/收集/转生/称谓增益 |
| gems（6） | 六槽宝石的属性、等级上限与金币/黑铁/元宝成本 |
| forge（4）、shop（18） | 锻造操作、材料费用、成功率/失败结果与商店商品 |
| balance、quests（8）、rebirth（10）、growth（3）、encounters（3） | 平衡参数、任务、转生、养成和非原表挑战曲线 |

职业基础成长、技能公式、品质加成、强化、爆率、挑战曲线、回收和商店价格等是**可编辑的模板数值**，不是从原客户端验证出的原版公式。

## 装备属性：不再使用 base_power 推导

| 运行时字段 | 原 item 字段 | 含义 |
|---|---|---|
| hp、mp | hp、mp | 装备生命/法力加成 |
| attack_min、attack_max | a1、a2 | 攻击下限/上限的独立加成 |
| magic_min、magic_max | b1、b2 | 魔法下限/上限的独立加成 |
| tao_min、tao_max | c1、c2 | 道术下限/上限的独立加成 |
| defense_min、defense_max | d1、d2 | 防御下限/上限的独立加成 |
| magic_defense_min、magic_defense_max | e1、e2 | 魔防下限/上限的独立加成 |
| required_job | job | 源 1/2/3 → 运行时 0/1/2；空值 → -1 全职业 |
| min_level、required_rebirth | lvl、met | 最低等级、最低转生 |
| weight、sell_price、stack_limit | weight、pirce、overlap | 重量、基准售价、堆叠上限元数据 |
| description | present | 原描述，空值显示占位短横线 |

原 `shuxing` 表是 a/b/c/d/e 字段含义的依据。怪物和 BOSS 使用同名字段的 `z` 前缀（如 `za1`、`zd2`、`zhp`）。

**必须按物品类别解释**：消耗品里的 hp/mp 可能表示奖励类型与奖励数量，例如金条。它们不能被导入成装备生命/法力。因此非装备十二属性全部为 0，原值仍在 reference。龙珠等装备数据里的 hp/mp 保留，但它们目前没有可穿戴的槽位。

**单件装备下限加成可大于上限加成**：原表存在仅增加下限的装备。原始贡献必须保留，不强制排序、不补造上限；角色聚合完后才确保最终上限不少于下限。怪物本身是完整属性区间，反向区间则拒绝加载。

## 运行时计算

1. 装备单项加成 = 基础值 × `qualities.stat_pct` × 实例系数 ÷ 100，再 × (100 + 强化等级 × `attributes.upgrade_pct`) ÷ 100；每步整数运算。原始 0 属性保持 0。
2. 角色单项 = 职业基础 + 等级成长 + 满足穿戴条件的装备单项 + 对应属性的养成项/百分比。具体运算顺序以 `Game.Stat` 为准。
3. 战士普通攻击取攻击区间，法师取魔法，道士取道术；技能由 `skills.school` 决定伤害类型。
4. 物理伤害使用目标防御；魔法、道术使用目标魔防。攻击和防御在各自闭区间掷值，最终伤害设下限；不再从综合评分反推生命或防御。
5. `score_weight` 只负责展示评分及按职业挑选装备，绝不作为攻击/生命的源数据。
6. 穿戴检查等级、职业和转生。存档只保存定义 ID 与实例品质/强化等；重启重新按 CSV 聚合。因新配置而不合格的已穿戴装备不提供属性。

## 导入与校验

```powershell
python templates/game/legend/tools/import_reference.py --source 'D:/game/迷你传奇/data/data.dll' --output templates/game/legend/data/reference
python templates/game/legend/tools/enrich_attributes.py
```

第二条命令只补齐现有运行时 items/monsters/bosses 的原始属性，保留奖励/地图/进度等设计列，不是从空目录生成所有玩法表。运行时手调过的这些源属性会被重新导入覆盖；先提交或备份自己的调整。脚本可指定 `--data <其他目录>`。

校验包含表头、重复 ID、非整数/溢出/负数、外键、职业与伤害类型枚举、必需的十二属性规则。失败时中止加载，不悄悄用硬编码默认值继续。

## 尚待确认的特殊数据

`need`、`f1`、`fj`、`ztx` 等原字段保持原样，没有根据缩写猜测并实现吸血、暴击、套装等效果。它们的解释和触发逻辑必须对照原程序验证后再接入。当前完整的是上述十二个基础战斗属性，不是“所有原版特殊词条和所有玩法”。

回归测试位于仓库 `tests/templates/legend/`：189 条 Zan 断言覆盖穿戴、伤害分流、
强化、地图行进、五行、祭坛、存档和三职业一小时离线模拟；Python 测试逐字段核对
全量原始定义，并验证导入幂等和八类错误配置拒绝。

服务端参数与「客户端一份规则 + 服务端一份规则」的同步纪律见
`templates/server/server-legend/data/RULES.md`（冲突裁决台账 + 数值依据）。


## 五行突破字段

`elements.csv` 的 `upgrade_iron` / `upgrade_iron_step` 与 `upgrade_yuan` / `upgrade_yuan_step` 定义每次突破的基础材料和随等级增长的材料。客户端升级前先检查金币、黑铁、元宝，材料不足时经验和资源均不改变。


## 图鉴套装字段

`sets.csv` 以物品定义 ID 列表配置套装门槛、属性与数值。运行时按图鉴中已收录的定义计数；重复定义不会再次消耗，套装效果会参与角色属性计算并写入存档。
