# Game.Arpg

> 源码: `packages/Zan.Game/src/Game/Arpg/Combat.zan`, `packages/Zan.Game/src/Game/Arpg/Config.zan`, `packages/Zan.Game/src/Game/Arpg/DataBinding.zan`, `packages/Zan.Game/src/Game/Arpg/Entity.zan`, `packages/Zan.Game/src/Game/Arpg/Events.zan`, `packages/Zan.Game/src/Game/Arpg/Fonts/PixelFont.zan`, `packages/Zan.Game/src/Game/Arpg/Formula.zan`, `packages/Zan.Game/src/Game/Arpg/Global.zan`, `packages/Zan.Game/src/Game/Arpg/IsoTileMap.zan`, `packages/Zan.Game/src/Game/Arpg/LootScatter.zan`, `packages/Zan.Game/src/Game/Arpg/Map.zan`, `packages/Zan.Game/src/Game/Arpg/Menu.zan`, `packages/Zan.Game/src/Game/Arpg/Music.zan`, `packages/Zan.Game/src/Game/Arpg/Net.zan`, `packages/Zan.Game/src/Game/Arpg/NetRuntime.zan`, `packages/Zan.Game/src/Game/Arpg/Presentation.zan`, `packages/Zan.Game/src/Game/Arpg/Primitives.zan`, `packages/Zan.Game/src/Game/Arpg/Project.zan`, `packages/Zan.Game/src/Game/Arpg/RichText.zan`, `packages/Zan.Game/src/Game/Arpg/Runtime.zan`, `packages/Zan.Game/src/Game/Arpg/Server.zan`, `packages/Zan.Game/src/Game/Arpg/ServerEvents.zan`, `packages/Zan.Game/src/Game/Arpg/TextLayout.zan`, `packages/Zan.Game/src/Game/Arpg/Tween.zan`, `packages/Zan.Game/src/Game/Arpg/UiRuntime.zan`, `packages/Zan.Game/src/Game/Arpg/YSortLayer.zan`


## ArpgActor (class)

运行期角色实例：直接构造或经 FromDefinition 从定义生成，
持有基础属性、背包（默认 40 格）、装备、冷却与在身增益；
坐标与朝向用地图格坐标系。治疗/消耗/伤害结算直接在实例上
进行，实时有效属性经 EffectiveAttributes 叠加装备与增益修饰。

- string id;

- string displayName;

- string definitionId;

- string faction;

- int level;

- int gridX;

- int gridY;

- int direction;

- bool alive;

- ArpgAttributes attributes;

- List<string> skills;

- ArpgInventory inventory;

- ArpgEquipment equipment;

- ArpgCooldowns cooldowns;

- List<ArpgBuffInstance> activeBuffs;

- ArpgActor(string id, string displayName)

- static ArpgActor FromDefinition(string instanceId, ArpgActorDefinition definition)
  - 以 instanceId 创建角色实例并从 definition 灌入显示名、阵营、
    等级、朝向与基础属性（AddFrom，含全部自定义属性）；初始技能
    取自定义。初始增益不在此处施加（由 Project.ApplyStartingBuffs
    负责），背包/装备为空。

- string Id()
  - 角色 Id（实例标识，创建后不变）。

- string DisplayName()
  - 显示名（来自定义或构造参数）。

- string DefinitionId()
  - 来源定义 Id（FromDefinition 时才有值，否则为空串）。

- string Faction()
  - 阵营（ArpgFaction 常量，默认 Neutral）。

- int Level()
  - 等级。

- int GridX()
  - 当前所在格 X（地图格坐标）。

- int GridY()
  - 当前所在格 Y（地图格坐标）。

- int Direction()
  - 朝向（-1 或 0~7，-1 表示未定向）。

- bool IsAlive()
  - 是否存活（HP 归 0 由 TakeDamage/ClampVitals 置 false）。

- ArpgAttributes Attributes()
  - 基础属性（不含装备/增益，实时有效值用 EffectiveAttributes）。

- ArpgInventory Inventory()
  - 背包（默认 40 格）。

- ArpgEquipment Equipment()
  - 装备槽集合。

- ArpgCooldowns Cooldowns()
  - 技能/物品冷却表。

- ArpgAttributes EffectiveAttributes()
  - 实时有效属性：基础属性克隆后叠加装备与全部在身增益的修饰。
    每次调用都重新计算并返回新对象，帧内反复调用有开销，宜缓存。

- void SetLevel(int newLevel)
  - 设置等级。

- void SetFaction(string newValue)
  - 设置阵营（ArpgFaction 常量）。

- void SetGridPosition(int x, int y)
  - 设置所在格坐标（地图格坐标系）。

- void SetDirection(int newValue)
  - 设置朝向（-1 或 0~7）。

- void SetAlive(bool newValue)
  - 直接设置存活标记（复活/测试用；常规伤害走 TakeDamage）。

- void AddSkill(string skillId)
  - 添加技能 Id；已拥有时静默忽略（幂等）。

- bool RemoveSkill(string skillId)
  - 移除指定技能；找到并移除返回 true，未拥有返回 false。

- bool HasSkill(string skillId)
  - 是否已拥有指定技能。

- ArpgBuffInstance ApplyBuff(ArpgBuffDefinition definition, ArpgActor source, bool unique)
  - 施加增益并返回实例；definition 为 null 返回 null。unique 为
    true 且同 Id 增益已在身时返回 null 不重复施加，false 时允许
    同名增益叠加多份。

- void RemoveBuff(string buffId)
  - 移除全部同 Id 增益（含叠加的多份）。

- void RemoveAllBuffs()
  - 清空全部在身增益。

- bool HasBuff(string buffId)
  - 是否身负指定 Id 的增益。

- double Heal(double hp, double mp)
  - 恢复 HP/MP，上界为有效 MaxHp/MaxMp、下界 0（负值即扣血）。
    返回 HP 的实际变化量（目标值 − 恢复前值）；已死亡时返回 0.0
    且无副作用。

- bool Spend(double hp, double mp)
  - 扣除 HP/MP 消耗（如技能施放）。余额不足返回 false 不扣除；
    注意 HP 判定要求剩余值必须大于 hp 消耗（不能扣到 0），
    MP 判定为不小于。成功扣除返回 true。

- double TakeDamage(double amount)
  - 承受伤害：返回实际生效的伤害量（HP 被钳到 0 时小于 amount）。
    HP 归 0 时自动置为死亡；已死亡或 amount ≤ 0 时返回 0.0 且
    无副作用。

- void ClampVitals()
  - 将当前 HP/MP 钳制到 [0, 有效 Max] 区间；HP 钳后 ≤ 0 时置
    为死亡。适合在装备/增益变化后调用。

- void Revive(double hpPercent)
  - 复活并恢复有效 MaxHp × hpPercent% 的生命（至少 1 点），
    同时置为存活；不影响 MP。

- int SkillCount()
  - 已学技能数量。

- string SkillAt(int index)
  - 按下标读取技能 Id；应配合 SkillCount 使用。

- int BuffCount()
  - 在身增益数量。

- string BuffAt(int index)
  - 按下标读取增益定义 Id；应配合 BuffCount 使用。

- ArpgBuffInstance BuffInstanceAt(int index)
  - 按下标读取增益实例（含剩余时长等运行时状态）。

- void RemoveBuffAt(int index)
  - 按下标移除增益；index 越界行为未定义，应配合 BuffCount 使用。


## ArpgActorDefinition (class)

可复用的角色组件定义，对应一个角色文件。

- string id;

- string displayName;

- bool defaultPlayer;

- string faction;

- string growthId;

- int level;

- int experienceReward;

- int attackInterval;

- int moveInterval;

- int direction;

- int visionRange;

- bool aggressive;

- bool autoCombat;

- int respawnMilliseconds;

- ArpgAttributes attributes;

- List<string> skills;

- List<string> startingBuffs;

- ArpgActorDefinition(string id, string displayName)

- string Id()
  - 角色定义 Id（配置键，创建后不变）。

- string DisplayName()
  - 显示名（必填，用于 UI 展示）。

- bool DefaultPlayer()
  - 是否为默认玩家角色；项目应恰好标记一个，由 Project 校验。

- string Faction()
  - 阵营（ArpgFaction 常量，默认 Neutral）。

- string GrowthId()
  - 绑定的成长曲线组件 Id（空串表示未配置；存在性由 Project 校验）。

- int Level()
  - 等级，默认 1。

- int ExperienceReward()
  - 该角色被击杀时奖励的经验值，默认 0。

- int AttackInterval()
  - 普通攻击间隔（毫秒），默认 3000；为 0 时 Validate 给出警告。

- int MoveInterval()
  - 移动间隔（毫秒），默认 1500；为 0 时 Validate 给出警告。

- int Direction()
  - 初始朝向（-1 或 0~7，-1 表示未定向），FromDefinition 时灌入实例。

- int VisionRange()
  - 视野范围（格），默认 0 表示未配置。

- bool Aggressive()
  - 是否为主动攻击型（默认 false，仅攻击性 AI 使用）。

- bool AutoCombat()
  - 是否启用自动战斗（默认 false）。

- int RespawnMilliseconds()
  - 重生间隔（毫秒），默认 -1 表示不重生。

- ArpgAttributes Attributes()
  - 基础属性集合（只读视图，配置期通过其 Set* 修改；
    FromDefinition 时以 AddFrom 灌入实例）。

- void SetDisplayName(string newValue)
  - 设置显示名。

- void SetDefaultPlayer(bool newValue)
  - 设置是否为默认玩家角色。

- void SetFaction(string newValue)
  - 设置阵营（ArpgFaction 常量）。

- void SetGrowth(string newValue)
  - 设置绑定的成长曲线组件 Id。

- void SetLevel(int newValue)
  - 设置等级。

- void SetExperienceReward(int newValue)
  - 设置被击杀时奖励的经验值。

- void SetAttackInterval(int newValue)
  - 设置普通攻击间隔（毫秒）。

- void SetMoveInterval(int newValue)
  - 设置移动间隔（毫秒）。

- void SetDirection(int newValue)
  - 设置初始朝向（-1 或 0~7）。

- void SetVisionRange(int newValue)
  - 设置视野范围（格）。

- void SetAggressive(bool newValue)
  - 设置是否为主动攻击型。

- void SetAutoCombat(bool newValue)
  - 设置是否启用自动战斗。

- void SetRespawnMilliseconds(int newValue)
  - 设置重生间隔（毫秒）；-1 表示不重生。

- void AddSkill(string skillId)
  - 追加一个初始技能 Id。

- void AddStartingBuff(string buffId)
  - 追加一个初始增益 Id（实例化时由 Project.ApplyStartingBuffs 施加）。

- int SkillCount()
  - 初始技能数量。

- string SkillAt(int index)
  - 按下标读取初始技能 Id；应配合 SkillCount 使用。

- int StartingBuffCount()
  - 初始增益数量。

- string StartingBuffAt(int index)
  - 按下标读取初始增益 Id；应配合 StartingBuffCount 使用。

- void Validate(ArpgDiagnostics diagnostics)
  - 校验角色定义合法性，问题以 L2ACT001~L2ACT006 写入
    diagnostics（必填缺失、level 非正、direction 越界为错误，
    攻击/移动间隔为 0 仅给警告）。应在配置加载完成后调用一次。


## ArpgAttributes (class)

角色、物品、技能与增益共用的基础数值属性。
自定义数值属性，与 DM3 的扩展属性模型一致。

- double hp;

- double mp;

- double maxHp;

- double maxMp;

- double accuracy;

- double evasion;

- double criticalChance;

- double criticalDamage;

- double moveSpeedPercent;

- double attackSpeedPercent;

- List<ArpgCustomAttribute> custom;

- ArpgAttributes()

- double Hp()
  - 当前 HP（会随 Heal/TakeDamage/Spend 变化，受有效 MaxHp 钳制）。

- double Mp()
  - 当前 MP（会随 Heal/Spend 变化，受有效 MaxMp 钳制）。

- double MaxHp()
  - 基础最大 HP（不含装备/增益修饰，修饰看 EffectiveAttributes）。

- double MaxMp()
  - 基础最大 MP（不含装备/增益修饰）。

- double Accuracy()
  - 命中率基准值。

- double Evasion()
  - 闪避率基准值。

- double CriticalChance()
  - 暴击概率（百分比数值，由结算方解释）。

- double CriticalDamage()
  - 暴击伤害倍率（百分比，默认 150.0 即 1.5 倍）。

- double MoveSpeedPercent()
  - 移动速度加成（百分比，0 为不加成）。

- double AttackSpeedPercent()
  - 攻击速度加成（百分比，0 为不加成）。

- void SetHp(double amount)
  - 设置当前 HP（不做钳制，越界后应调用 ClampVitals）。

- void SetMp(double amount)
  - 设置当前 MP（不做钳制）。

- void SetMaxHp(double amount)
  - 设置基础最大 HP。

- void SetMaxMp(double amount)
  - 设置基础最大 MP。

- void SetAccuracy(double amount)
  - 设置命中率基准值。

- void SetEvasion(double amount)
  - 设置闪避率基准值。

- void SetCriticalChance(double amount)
  - 设置暴击概率。

- void SetCriticalDamage(double amount)
  - 设置暴击伤害倍率（百分比）。

- void SetMoveSpeedPercent(double amount)
  - 设置移动速度加成（百分比）。

- void SetAttackSpeedPercent(double amount)
  - 设置攻击速度加成（百分比）。

- void SetCustom(string name, double amount)
  - 设置或累加自定义属性：同名条目已存在则覆盖其数值，否则追加
    新条目。名字区分大小写。

- double GetCustom(string name)
  - 读取自定义属性数值；不存在同名条目时返回 0.0。

- double Get(string name)
  - GetCustom 的别名（兼容旧调用习惯）。

- void AddFrom(ArpgAttributes other)
  - 累加另一组属性的全部数值（含当前 HP/MP 与自定义属性）。
    criticalDamage 按增量累加（对方超出 150 的部分）。用于把
    定义的基础属性灌入实例。

- void AddModifiersFrom(ArpgAttributes other)
  - 仅累加修饰性数值（maxHp/maxMp/命中/闪避/暴击/速度与自定义
    属性），不动当前 HP/MP。装备与增益加成走此入口，避免直接
    回血回蓝。criticalDamage 按增量累加。

- ArpgAttributes Clone()
  - 深拷贝全部数值与自定义属性（自定义条目也逐个复制）。

- int CustomCount()
  - 自定义属性条目数。

- ArpgCustomAttribute CustomAt(int index)
  - 按下标读取自定义属性条目；应配合 CustomCount 使用。


## ArpgBuffDefinition (class)

增益（Buff）定义：持续时长 -1 表示永久；触发间隔决定周期效果
（如每秒掉血）的频率；移除条件与限制效果由布尔开关组合配置。

- string id;

- string displayName;

- string iconResource;

- string title;

- string tooltip;

- int durationMilliseconds;

- int triggerIntervalMilliseconds;

- bool removeOnDeath;

- bool removeOnMove;

- bool removeOnAttack;

- bool removeOnDamage;

- bool movementDisabled;

- bool attackDisabled;

- bool blinded;

- bool runningAllowed;

- ArpgAttributes attributes;

- ArpgBuffDefinition(string id, string displayName)

- string Id()
  - 增益唯一 Id（配置键，创建后不变）。

- string DisplayName()
  - 显示名（必填）。

- string IconResource()
  - 图标资源路径，默认空串。

- string Title()
  - UI 标题文本，默认空串。

- string Tooltip()
  - 悬浮提示文本，默认空串。

- int DurationMilliseconds()
  - 持续时长（毫秒），默认 -1 表示永久。

- int TriggerIntervalMilliseconds()
  - 周期触发间隔（毫秒），默认 0 表示不周期触发（Tick 返回 0）。

- bool RemoveOnDeath()
  - 携带者死亡时是否移除（默认 true）。

- bool RemoveOnMove()
  - 移动时是否移除（默认 false）。

- bool RemoveOnAttack()
  - 攻击时是否移除（默认 false）。

- bool RemoveOnDamage()
  - 受伤时是否移除（默认 false）。

- bool MovementDisabled()
  - true 时禁止移动（定身类）。

- bool AttackDisabled()
  - true 时禁止攻击（缴械类）。

- bool Blinded()
  - true 时致盲（视野受限）。

- bool RunningAllowed()
  - true 时允许奔跑（如减速/冰冻增益需置 false）。

- ArpgAttributes Attributes()
  - 增益附带的属性加成集合（只读视图，配置期通过其 Set* 修改）。

- void SetIconResource(string newValue)
  - 设置图标资源路径。

- void SetTitle(string newValue)
  - 设置 UI 标题文本。

- void SetTooltip(string newValue)
  - 设置悬浮提示文本。

- void SetDurationMilliseconds(int newValue)
  - 设置持续时长（毫秒）；-1 表示永久。

- void SetTriggerIntervalMilliseconds(int newValue)
  - 设置周期触发间隔（毫秒）；0 表示不周期触发。

- void SetRemoveConditions(bool death, bool move, bool attack, bool damage)
  - 一次性设置四个移除条件：死亡/移动/攻击/受伤。

- void SetRestrictions(bool movementDisabled, bool attackDisabled, bool blinded, bool runningAllowed)
  - 一次性设置四个限制效果：禁移动/禁攻击/致盲/允许奔跑。

- void Validate(ArpgDiagnostics diagnostics)
  - 校验增益配置合法性，问题以错误码 L2BUF001~L2BUF004 写入
    diagnostics（必填缺失、时长小于 -1、触发间隔为负）。应在
    配置加载完成后调用一次。


## ArpgBuffInstance (class)

运行中的增益实例：由 CreateFrom 从定义生成，剩余时长随 Tick 递减，
周期触发计数供引擎结算周期效果。生命周期由宿主 Actor 管理。

- ArpgBuffDefinition definition;

- ArpgActor source;

- int remainingMilliseconds;

- int triggerElapsedMilliseconds;

- static ArpgBuffInstance CreateFrom(ArpgBuffDefinition definition, ArpgActor source)
  - 从定义创建实例：剩余时长取定义的 DurationMilliseconds
    （-1 即永久），source 记录施加者（用于结算归属）。

- ArpgBuffDefinition Definition()
  - 实例对应的增益定义。

- ArpgActor Source()
  - 施加者角色引用（可能为 null，如环境施加）。

- int RemainingMilliseconds()
  - 剩余时长（毫秒）；-1 表示永久。

- bool IsInfinite()
  - 是否永久增益（剩余时长小于 0）。

- bool IsExpired()
  - 是否已到期（剩余时长恰为 0；永久增益恒为 false）。

- int Tick(int deltaMilliseconds)
  - 推进计时：递减剩余时长（下限钳到 0），并按定义的触发间隔
    累计返回本帧应触发的周期次数（无周期触发时返回 0）。
    deltaMilliseconds ≤ 0 时直接返回 0 不产生任何推进。


## ArpgColor (class)

Arpg 定义使用的 RGBA 颜色。构造时各通道钳制到 0–255。

- int red;

- int green;

- int blue;

- int alpha;

- ArpgColor(int red, int green, int blue, int alpha)
  - 构造颜色；各分量在构造时钳制到 [0,255]。

- static ArpgColor Transparent()
  - 完全透明（0,0,0,0）。

- static ArpgColor Black()
  - 不透明黑。

- static ArpgColor White()
  - 不透明白。

- int Red()
  - 红色分量（0–255）。

- int Green()
  - 绿色分量（0–255）。

- int Blue()
  - 蓝色分量（0–255）。

- int Alpha()
  - alpha 分量：0 全透明，255 不透明。


## ArpgCombat (class)

网格战斗计算：切比雪夫距离、目标合法性（阵营判定）、命中
率（命中率 = accuracy / (accuracy + evasion) × 100）与伤害
（attack × DamageFactor − defense，下限 1，暴击乘
CriticalDamage%）。

- static int GridDistance(ArpgActor left, ArpgActor right)
  - 两角色所在格子的切比雪夫距离（棋盘王步距离）。

- static bool IsValidTarget(ArpgActor caster, ArpgActor target, string targetFaction)
  - 目标是否合法：要求 caster/target 非空且 target 存活；
    按 targetFaction 判定阵营（Any 全通过，Friendly 同阵营，
    Hostile 异阵营，其余精确匹配阵营名）。

- static int HitChance(ArpgActor caster, ArpgActor target)
  - 命中率百分比 0–100：accuracy/(accuracy+evasion)×100，
    分母 ≤0 时直接判满 100。

- static double Damage(ArpgActor caster, ArpgActor target, ArpgSkillDefinition skill, bool critical)
  - 期望伤害：attack × 技能 DamageFactor − defense，下限 1.0；
    暴击再乘攻击方 CriticalDamage/100。


## ArpgComponentFile (class)

组件文件注册条目：kind 为组件类别（ArpgComponentKind 常量或
"unknown"），id 为该类别内的标识，path 为文件路径。

- string kind;

- string id;

- string file;

- ArpgComponentFile(string kind, string id, string file)

- string Kind()
  - 组件类别（ArpgComponentKind 常量或 "unknown"）。

- string Id()
  - 类别内组件 Id。

- string File()
  - 组件文件路径。


## ArpgComponentKind (class)

组件种类字符串常量。ArpgProject 按 kind 归类资源，
IsValid 判断是否为这八种之一。

- static string Map()
  - 地图组件（"map"）。

- static string Actor()
  - 角色组件（"actor"）。

- static string Item()
  - 物品组件（"item"）。

- static string Skill()
  - 技能组件（"skill"）。

- static string Buff()
  - 增益组件（"buff"）。

- static string Window()
  - 窗口组件（"window"）。

- static string Growth()
  - 成长曲线组件（"growth"）。

- static string Prefab()
  - 预制体组件（"prefab"）。

- static bool IsValid(string kind)
  - kind 是否为合法组件种类。


## ArpgConfig (class)

文档所述 App.lua 启动配置的类型化等价物。

- string title;

- int width;

- int height;

- int frameRate;

- bool verticalSync;

- int screenMode;

- bool repeatKeys;

- string defaultFont;

- ArpgColor background;

- ArpgConfig()

- string Title()
  - 窗口标题，默认 "Zan Arpg"。

- int Width()
  - 窗口宽度（像素），默认 1280。

- int Height()
  - 窗口高度（像素），默认 720。

- int FrameRate()
  - 目标帧率（FPS），默认 60。

- bool VerticalSync()
  - 是否开启垂直同步（开启时 frameRate 被忽略），默认 false。

- int ScreenMode()
  - 屏幕模式（ArpgScreenMode 常量 0~3，默认 Scale）。

- bool RepeatKeys()
  - 是否开启按键连发（开启时 OnKeyDown 连发也触发），默认 false。

- string DefaultFont()
  - 默认字体资源 Id，空串表示用内置字体。

- ArpgColor Background()
  - 背景色（默认黑色；透明无边框模式下作为 RGB 抠色键）。

- int BackgroundRed()
  - 背景色红分量（0~255）。

- int BackgroundGreen()
  - 背景色绿分量（0~255）。

- int BackgroundBlue()
  - 背景色蓝分量（0~255）。

- int BackgroundAlpha()
  - 背景色 Alpha（0~255，0 全透明）。

- void SetTitle(string newTitle)
  - 设置窗口标题。

- void SetSize(int width, int height)
  - 一次性设置窗口尺寸（像素）。

- void SetFrameRate(int framesPerSecond)
  - 设置目标帧率（FPS）。

- void SetVerticalSync(bool enabled)
  - 设置是否开启垂直同步。

- void SetScreenMode(int mode)
  - 设置屏幕模式（ArpgScreenMode 常量）。

- void SetRepeatKeys(bool enabled)
  - 设置是否开启按键连发。

- void SetDefaultFont(string resourceId)
  - 设置默认字体资源 Id。

- void SetBackground(int red, int green, int blue, int alpha)
  - 一次性设置背景色 RGBA（各分量 0~255）。

- ArpgDiagnostics Validate()
  - 校验启动配置，返回新诊断对象（错误码 L2APP001~L2APP006）。
    透明无边框模式下背景 Alpha 为 0 只给警告（抠色键不可见），
    其余为错误。应在启动前调用并检查。


## ArpgControl (class)

UI 控件运行时实例：由 ArpgControlDefinition 构造，持有展开后的
子节点（含 Prefab 节点）、求值后的标题/内容/富文本文档与指针
交互状态（悬停/按下/拖拽/焦点）。数值属性经 SetValue 修改并钳
制到范围，变化时触发 ValueChanged；按钮在区域内按下-抬起时触发
Activated。构造时即触发 Created 事件。

- ArpgControlDefinition definition;

- string windowId;

- ArpgEvents systemEvents;

- ArpgDataSource dataSource;

- List<ArpgNode> nodes;

- string evaluatedTitle;

- string evaluatedContent;

- ArpgRichTextDocument document;

- ArpgControlEventHandlers prefabEvents;

- int x;

- int y;

- bool visible;

- bool enabled;

- bool focused;

- bool hovered;

- bool pressed;

- bool dragging;

- bool selected;

- double currentValue;

- string appendedContent;

- ArpgControl(ArpgControlDefinition definition, string windowId, ArpgDataSource defaultSource, ArpgProject project, ArpgEvents systemEvents)
  - 构造控件实例：展开定义与 Prefab 的子节点；PrefabId 找不到对应 Prefab 时静默忽略；构造即触发 Created 事件。

- string Id()
  - 返回控件唯一 Id（同定义 Id）。

- string Kind()
  - 返回控件类型字符串。

- int X()
  - 返回控件当前 X 坐标（像素，相对窗口原点；拖拽会改变运行时值）。

- int Y()
  - 返回控件当前 Y 坐标（像素，相对窗口原点；拖拽会改变运行时值）。

- int Width()
  - 返回控件宽度（像素）。

- int Height()
  - 返回控件高度（像素）。

- int Order()
  - 返回绘制顺序；数值大者在上层。

- bool Visible()
  - 返回控件当前可见性。

- bool Enabled()
  - 返回控件当前可用性；不可用时不响应指针且叠加禁用色。

- bool MouseEvents()
  - 返回控件是否接收鼠标事件。

- bool Draggable()
  - 返回控件是否可拖拽。

- bool Focused()
  - 返回控件是否持有焦点。

- bool Hovered()
  - 返回指针是否悬停在控件上。

- bool Pressed()
  - 返回左键是否正按在控件上（未抬起）。

- bool Selected()
  - 返回控件选中状态。

- double AnchorX()
  - 返回水平锚点（0=左缘，1=右缘）。

- double AnchorY()
  - 返回垂直锚点（0=上缘，1=下缘）。

- double ScaleX()
  - 返回水平缩放系数。

- double ScaleY()
  - 返回垂直缩放系数。

- int Angle()
  - 返回旋转角度（度，顺时针）。

- double Opacity()
  - 返回不透明度（0.0~1.0）。

- bool Clip()
  - 返回是否把子节点裁剪到控件边界内。

- string Title()
  - 返回模板求值后的标题。

- string Content()
  - 返回模板求值后的内容（含 AppendContent 追加的部分）。

- string Resource()
  - 返回贴图资源 Id。

- ArpgColor BackgroundColor()
  - 返回背景色。

- ArpgColor BorderColor()
  - 返回边框颜色。

- ArpgColor TextColor()
  - 返回文字颜色（进度条也用它渲染填充条）。

- ArpgColor HoverColor()
  - 返回悬停叠加色。

- ArpgColor PressedColor()
  - 返回按下叠加色。

- ArpgColor DisabledColor()
  - 返回禁用叠加色。

- int BorderWidth()
  - 返回边框宽度（像素）。

- int FontSize()
  - 返回字体大小（像素）。

- int HorizontalAlign()
  - 返回水平对齐（ArpgTextAlign 常量）。

- int VerticalAlign()
  - 返回垂直对齐（ArpgTextAlign 常量）。

- bool Wrap()
  - 返回文本是否自动换行。

- int LineSpacing()
  - 返回行间距（像素）。

- double Minimum()
  - 返回数值范围下限。

- double Maximum()
  - 返回数值范围上限。

- double Value()
  - 返回当前数值（已钳制到范围）。

- double Progress()
  - 返回进度比例（当前值相对范围的归一化，0.0~1.0；范围无效时为 0.0）。

- ArpgNineSlice NineSlice()
  - 返回九宫格描述；未设置时为 null。

- ArpgRichTextDocument Document()
  - 返回求值后的富文本文档（非富文本控件为单 run 文档）。

- ArpgDataSource DataSource()
  - 返回控件当前生效的数据源（私有的或继承的）。

- ArpgControlEventHandlers Events()
  - 返回控件事件槽位集合（单播；不包含 Prefab 事件）。

- int NodeCount()
  - 返回展开后子节点数量（含 Prefab 节点）。

- ArpgNode NodeAt(int index)
  - 返回第 index 个子节点实例；index 须在 [0, NodeCount) 内。

- ArpgNode FindNode(string id)
  - 按 Id 查找子节点（含 Prefab 展开节点）；未找到返回 null。

- void SetPosition(int x, int y)
  - 设置控件位置（像素，相对窗口原点；拖拽也直接改此位置）。

- void SetVisible(bool newValue)
  - 设置控件可见性。

- void SetEnabled(bool newValue)
  - 设置可用性；置为不可用时同时清除悬停/按下/拖拽状态。

- void SetSelected(bool newValue)
  - 设置控件选中状态。

- void SetValue(double newValue)
  - 设置数值：钳制到 [Minimum, Maximum]，值无变化时为空操作；变化时先触发 Prefab 事件再触发控件事件。

- void ResetValue()
  - 把数值重置为范围下限（等价 SetValue(Minimum)）。

- void AppendContent(string content)
  - 在内容末尾追加文本并立即重新求值（富文本控件追加的也是富文本标记）；与 ClearAppendedContent 配对使用。

- void ClearAppendedContent()
  - 清空 AppendContent 追加的全部文本并重新求值。

- void SetDataSource(ArpgDataSource newValue)
  - 设置控件私有数据源：继承中的子节点同步换成新源并重新求值。

- void Evaluate()
  - 重新求值标题/内容并重建富文本文档（含 AppendContent 追加部分）；框架每帧调用，业务一般无需手动调用。

- void Update(int deltaMilliseconds)
  - 每帧推进：重新求值内容、更新全部子节点，最后触发 Updated 事件（Prefab 事件先于控件自身事件）。

- bool HitTest(double localX, double localY)
  - 命中测试：localX/localY 为控件局部坐标；仅可见且可用的控件参与，按缩放与锚点计算的实际矩形判断。

- ArpgNode HitNode(double localX, double localY)
  - 命中测试子节点：localX/localY 为控件局部坐标；仅 MouseEvents 开启且命中的节点参与，同点多个命中取 Order 最大者；未命中返回 null。

- void SetFocused(bool newValue, ArpgNode node)
  - 设置控件焦点：变化时触发 FocusChanged，并把 node（可为 null）设为唯一获得焦点的子节点。

- bool PointerDown(int button, int modifiers, double localX, double localY)
  - 框架指针按下入口：依次分发给命中的子节点、Prefab 事件、控件事件（先到先消费），随后进入按下状态并获取焦点；返回是否被 handler 消费。

- bool PointerUp(int button, int modifiers, double localX, double localY)
  - 框架指针抬起入口：分发逻辑同 PointerDown；左键在按钮区域内按下并抬起时额外触发 Activated；返回是否被 handler 消费。

- bool PointerMove(int button, int modifiers, double localX, double localY, double deltaX, double deltaY)
  - 框架指针移动入口：更新悬停状态，可拖拽控件按位移移动位置并触发 Dragged，再按按下/抬起顺序分发；返回是否被 handler 消费。

- bool ActivateLink(int runIndex)
  - 激活富文本文档中第 runIndex 个 run 的链接：触发控件 OnLinkActivated 并同时上报系统级富文本链接事件；索引越界或该 run 不是链接时返回 false。

- void FireCustom(string name, string payload)
  - 触发控件自定义事件（Prefab 与控件自身 handler 都会收到；payload 原样透传）。


## ArpgControlDefinition (class)

UI 控件定义：静态描述一个控件（标签/按钮/图片框/进度条等），
含位置尺寸、状态（可见/可用/选中）、文本样式、数值范围、
Prefab 引用与子节点列表。坐标相对所属窗口原点。构造时取默认
值（可用、不透明、数值范围 [0, 100] 等），通过 Set* 修改；
Validate 递归校验控件与子节点并向 diagnostics 写入错误。

- string id;

- string kind;

- int x;

- int y;

- int width;

- int height;

- int order;

- bool visible;

- bool enabled;

- bool mouseEvents;

- bool draggable;

- bool selected;

- double anchorX;

- double anchorY;

- double scaleX;

- double scaleY;

- int angle;

- double opacity;

- bool clip;

- bool richText;

- string title;

- string content;

- string resource;

- string prefabId;

- ArpgColor backgroundColor;

- ArpgColor borderColor;

- ArpgColor textColor;

- ArpgColor hoverColor;

- ArpgColor pressedColor;

- ArpgColor disabledColor;

- int borderWidth;

- int fontSize;

- int horizontalAlign;

- int verticalAlign;

- bool wrap;

- int lineSpacing;

- double minimum;

- double maximum;

- double currentValue;

- ArpgNineSlice nineSlice;

- ArpgDataSource dataSource;

- List<ArpgNodeDefinition> nodes;

- ArpgControlEventHandlers events;

- ArpgControlDefinition(string id, string kind, int x, int y, int width, int height)
  - 构造控件定义：id 为控件唯一标识，kind 取 ArpgControlKind 常量，x/y/width/height 为像素。

- string Id()
  - 返回控件唯一 Id。

- string Kind()
  - 返回控件类型（ArpgControlKind 字符串）。

- int X()
  - 返回控件 X 坐标（像素，相对窗口原点）。

- int Y()
  - 返回控件 Y 坐标（像素，相对窗口原点）。

- int Width()
  - 返回控件宽度（像素）。

- int Height()
  - 返回控件高度（像素）。

- int Order()
  - 返回绘制顺序；数值大者在上层。

- bool Visible()
  - 返回控件是否可见（默认 true）。

- bool Enabled()
  - 返回控件是否可用（默认 true；不可用时灰显且不响应指针）。

- bool MouseEvents()
  - 返回控件是否接收鼠标事件（默认 false）。

- bool Draggable()
  - 返回控件是否可拖拽。

- bool Selected()
  - 返回控件初始选中状态。

- double AnchorX()
  - 返回水平锚点（0=左缘，1=右缘）。

- double AnchorY()
  - 返回垂直锚点（0=上缘，1=下缘）。

- double ScaleX()
  - 返回水平缩放系数。

- double ScaleY()
  - 返回垂直缩放系数。

- int Angle()
  - 返回旋转角度（度，顺时针）。

- double Opacity()
  - 返回不透明度（0.0~1.0）。

- bool Clip()
  - 返回是否把子节点裁剪到控件边界内。

- bool RichText()
  - 返回内容是否按富文本解析。

- string Title()
  - 返回标题（按钮渲染其标题，其他控件多用于窗口栏文本）。

- string Content()
  - 返回文本内容（模板占位符原文，运行时才求值）。

- string Resource()
  - 返回贴图资源 Id。

- string PrefabId()
  - 返回 Prefab Id；为空串表示不是 Prefab 控件。

- ArpgColor BackgroundColor()
  - 返回背景色。

- ArpgColor BorderColor()
  - 返回边框颜色。

- ArpgColor TextColor()
  - 返回文字颜色（进度条也用它渲染填充条）。

- ArpgColor HoverColor()
  - 返回悬停叠加色。

- ArpgColor PressedColor()
  - 返回按下叠加色。

- ArpgColor DisabledColor()
  - 返回禁用叠加色。

- int BorderWidth()
  - 返回边框宽度（像素）。

- int FontSize()
  - 返回字体大小（像素，默认 16）。

- int HorizontalAlign()
  - 返回水平对齐（ArpgTextAlign 常量）。

- int VerticalAlign()
  - 返回垂直对齐（ArpgTextAlign 常量）。

- bool Wrap()
  - 返回文本是否自动换行。

- int LineSpacing()
  - 返回行间距（像素）。

- double Minimum()
  - 返回数值范围下限（默认 0）。

- double Maximum()
  - 返回数值范围上限（默认 100）。

- double Value()
  - 返回初始数值（已按范围钳制，默认 0）。

- ArpgNineSlice NineSlice()
  - 返回九宫格描述；未设置时为 null。

- ArpgDataSource DataSource()
  - 返回数据源；未设置时为 null（运行时继承窗口默认数据源）。

- int NodeCount()
  - 返回子节点定义数量。

- ArpgNodeDefinition NodeAt(int index)
  - 返回第 index 个子节点定义；index 须在 [0, NodeCount) 内。

- ArpgControlEventHandlers Events()
  - 返回控件事件槽位集合（单播）。

- void SetOrder(int newValue)
  - 设置绘制顺序（数值大者显示在上层）。

- void SetVisible(bool newValue)
  - 设置控件可见性。

- void SetEnabled(bool newValue)
  - 设置控件可用性。

- void SetMouseEvents(bool newValue)
  - 设置控件是否接收鼠标事件。

- void SetDraggable(bool newValue)
  - 设置控件是否可拖拽。

- void SetSelected(bool newValue)
  - 设置控件选中状态。

- void SetAnchor(double x, double y)
  - 设置锚点（0=左/上缘，1=右/下缘）。

- void SetScale(double x, double y)
  - 设置缩放系数（1.0 为原始尺寸）。

- void SetAngle(int newValue)
  - 设置旋转角度（度，顺时针）。

- void SetOpacity(double newValue)
  - 设置不透明度；超出 [0.0, 1.0] 的值被钳制。

- void SetClip(bool newValue)
  - 设置是否把子节点裁剪到控件边界内。

- void SetTitle(string newValue)
  - 设置标题（按钮渲染其标题）。

- void SetContent(string newValue, bool richText)
  - 设置文本内容；richText=true 时按富文本标记解析。

- void SetResource(string newValue)
  - 设置贴图资源 Id。

- void SetPrefabId(string newValue)
  - 设置 Prefab Id；空串表示不是 Prefab 控件。

- void SetBackgroundColor(ArpgColor newValue)
  - 设置背景色。

- void SetBorder(int width, ArpgColor color)
  - 设置边框（此定义层不钳制负值，Validate 会报错）。

- void SetTextColor(ArpgColor newValue)
  - 设置文字颜色。

- void SetStateColors(ArpgColor hover, ArpgColor pressed, ArpgColor disabled)
  - 一次性设置悬停/按下/禁用三种状态叠加色。

- void SetTextStyle(int fontSize, int horizontalAlign, int verticalAlign, bool wrap, int lineSpacing)
  - 设置文本样式；fontSize 小于 1 按 1 处理，lineSpacing 负值按 0 处理。

- void SetRange(double minimum, double maximum, double newValue)
  - 设置数值范围与初始值；initialValue 钳制到 [minimum, maximum]。

- void SetValue(double newValue)
  - 设置当前数值；钳制到 [Minimum, Maximum]（默认 [0, 100]）。

- void SetNineSlice(ArpgNineSlice newValue)
  - 设置九宫格描述；传 null 取消九宫格渲染。

- void SetDataSource(ArpgDataSource newValue)
  - 设置控件私有数据源；设为 null 时恢复继承窗口默认数据源。

- void AddNode(ArpgNodeDefinition node)
  - 追加一个子节点定义（按添加顺序保存，渲染时按 Order 排序）。

- void Validate(ArpgDiagnostics diagnostics, string parentPath)
  - 校验定义合法性：Id/kind 非空、kind 受支持、尺寸与边框非负、缩放大于 0、字号为正、范围上限大于下限、子节点 Id 不重复；错误写入 diagnostics。


## ArpgControlEventHandlers (class)

控件事件单播槽位集合：每个槽位只保存一个 handler，后注册者覆盖
前者。经 ArpgControlDefinition.Events() 获取；业务代码调用 On*
注册回调，框架在控件生命周期与指针交互时触发 Raise*，不要手动
调用 Raise*。Prefab 控件还会叠加触发 Prefab 自身的 handler。

- ArpgControlCreated created;

- ArpgControlUpdated updated;

- ArpgControlPointerChanged pointerDown;

- ArpgControlPointerChanged pointerUp;

- ArpgControlPointerChanged pointerMove;

- ArpgControlFocusChanged focusChanged;

- ArpgControlDragged dragged;

- ArpgControlLinkActivated linkActivated;

- ArpgControlActivated activated;

- ArpgControlValueChanged valueChanged;

- ArpgControlCustomEvent customEvent;

- void OnCreated(ArpgControlCreated handler)
  - 注册控件实例化完成回调（含 Prefab 节点展开后触发一次）。

- void OnUpdated(ArpgControlUpdated handler)
  - 注册控件每帧更新回调（deltaMilliseconds 为帧间隔毫秒）。

- void OnPointerDown(ArpgControlPointerChanged handler)
  - 注册控件按下回调；返回 true 可消费事件并阻断窗口层分发。

- void OnPointerUp(ArpgControlPointerChanged handler)
  - 注册控件抬起回调；返回 true 可消费事件并阻断窗口层分发。

- void OnPointerMove(ArpgControlPointerChanged handler)
  - 注册控件移动回调；返回 true 可消费事件并阻断窗口层分发。

- void OnFocusChanged(ArpgControlFocusChanged handler)
  - 注册控件焦点变化回调（focused 为是否获得焦点）。

- void OnDragged(ArpgControlDragged handler)
  - 注册拖动回调（deltaX/deltaY 为本次位移像素）。

- void OnLinkActivated(ArpgControlLinkActivated handler)
  - 注册富文本链接激活回调（link 为链接原文）。

- void OnActivated(ArpgControlActivated handler)
  - 注册按钮激活回调（左键按下后在按钮区域内抬起）。

- void OnValueChanged(ArpgControlValueChanged handler)
  - 注册数值变化回调（newValue 已钳制到 [最小值, 最大值]）。

- void OnCustomEvent(ArpgControlCustomEvent handler)
  - 注册控件自定义事件回调（由 FireCustom 触发）。

- void RaiseCreated(string controlId)
  - 触发控件实例化回调；未注册 handler 时为空操作。

- void RaiseUpdated(string controlId, int deltaMilliseconds)
  - 触发控件每帧更新回调；未注册 handler 时为空操作。

- bool RaisePointerDown(string controlId, string nodeId, int button, int modifiers, double x, double y)
  - 触发控件按下回调；未注册时返回 false（事件未消费）。

- bool RaisePointerUp(string controlId, string nodeId, int button, int modifiers, double x, double y)
  - 触发控件抬起回调；未注册时返回 false（事件未消费）。

- bool RaisePointerMove(string controlId, string nodeId, int button, int modifiers, double x, double y)
  - 触发控件移动回调；未注册时返回 false（事件未消费）。

- void RaiseFocusChanged(string controlId, bool focused)
  - 触发焦点变化回调；未注册 handler 时为空操作。

- void RaiseDragged(string controlId, double deltaX, double deltaY)
  - 触发拖动回调；未注册 handler 时为空操作。

- void RaiseLink(string controlId, string link)
  - 触发链接激活回调；未注册 handler 时为空操作。

- void RaiseActivated(string controlId)
  - 触发按钮激活回调；未注册 handler 时为空操作。

- void RaiseValueChanged(string controlId, double newValue)
  - 触发数值变化回调；未注册 handler 时为空操作。

- void RaiseCustom(string controlId, string name, string payload)
  - 触发控件自定义事件回调；未注册 handler 时为空操作。


## ArpgControlKind (class)

控件类型常量：返回控件 kind 字符串（ArpgControlDefinition 的
kind 取值）。Image/Progress 为旧别名。IsValid 判断任意字符串
是否为受支持的控件类型。

- static string Label()
  - 标签控件类型（纯文本显示）。

- static string Button()
  - 按钮控件类型（可点击激活）。

- static string ImageBox()
  - 图片框控件类型（背景贴图）。

- static string TextBox()
  - 文本框控件类型。

- static string ProgressBar()
  - 进度条控件类型（按 Value/Range 渲染填充条）。

- static string RichTextBox()
  - 富文本控件类型（按富文本文档渲染）。

- static string Image()
  - ImageBox 的旧别名，语义相同。

- static string Progress()
  - ProgressBar 的旧别名，语义相同。

- static string Prefab()
  - Prefab 控件类型（实例化时展开 Prefab 内的节点）。

- static string Custom()
  - 自定义控件类型（渲染仅为背景/贴图，内容由业务自绘）。

- static bool IsValid(string kind)
  - 判断 kind 是否为受支持的控件类型字符串。


## ArpgCooldownEntry (class)

单个生效中的冷却：名称、剩余毫秒与总时长毫秒。

- string name;
  - 冷却名（如 "skill:fire"）。

- int remaining;
  - 剩余毫秒。

- int duration;
  - 启动时的总时长毫秒（供进度计算）。

- ArpgCooldownEntry(string name, int remaining, int duration)
  - 构造一条冷却记录。


## ArpgCooldowns (class)

按名称管理的冷却集合。同名 Start 覆盖重置；Tick 递减剩余
时间，到 0 的条目自动移除。冷却时间单位均为毫秒。

- List<ArpgCooldownEntry> entries;

- ArpgCooldowns()
  - 构造空冷却集合。

- int IndexOf(string name)
  - 冷却名对应下标；不存在返回 -1。

- void Start(string name, int milliseconds)
  - 启动/重置指定冷却。name 为 null/空或 milliseconds ≤ 0 时
    忽略；同名冷却被覆盖。

- bool IsReady(string name)
  - 冷却是否就绪（未启动或剩余 ≤0 均视为就绪）。

- int Remaining(string name)
  - 剩余毫秒；未启动该冷却返回 0。

- int Progress(string name)
  - 冷却进度百分比 0–100（已就绪/未启动为 100）。

- void Tick(int deltaMilliseconds)
  - 推进全部冷却并移除到期的条目；deltaMilliseconds ≤ 0 无效。


## ArpgCustomAttribute (class)

具名自定义属性条目：name 匹配键，amount 为该属性的数值贡献。
由 ArpgAttributes 的 Set/Get 按名字管理，一般不直接构造。

- string name;

- double amount;

- ArpgCustomAttribute(string name, double amount)

- string Name()
  - 自定义属性名（区分大小写，作为唯一匹配键）。

- double Amount()
  - 该属性的数值贡献。

- void SetAmount(double amount)
  - 修改该属性的数值贡献。


## ArpgDataSource (class)

每帧由游戏 UI 模板求值的可变 key/newValue 数据源。

- ArpgValue root;

- int revision;

- ArpgDataSource()

- ArpgValue Root()
  - 返回数据根对象（恒为 Object 形态）。

- int Revision()
  - 修改计数：每次 Set（含各类型便捷封装）自增 1，供模板
    缓存/重绘判断是否失效。

- void Set(string path, ArpgValue newValue)
  - 写入根对象上的点分路径并令 Revision() 自增。

- void SetText(string path, string newValue)
  - 写入文本值到点分路径（等价 Set(path, Text(...))）。

- void SetNumber(string path, double newValue)
  - 写入数值到点分路径。

- void SetInteger(string path, int newValue)
  - 写入整数值到点分路径。

- void SetBoolean(string path, bool newValue)
  - 写入布尔值到点分路径。

- ArpgValue Resolve(string path)
  - 在根对象上按点分路径取值；路径缺失返回 null。


## ArpgDiagnostic (class)

单条诊断信息：级别、错误码、出错路径与英文说明。

- int severity;

- string code;

- string path;

- string message;

- ArpgDiagnostic(int severity, string code, string path, string message)
  - 构造一条诊断。

- int Severity()
  - 级别（ArpgDiagnosticSeverity 常量之一）。

- string Code()
  - 机器可读错误码（如 "L2PRJ001"）。

- string Path()
  - 出错位置（点分路径，如 "actors.player.skills"）。

- string Message()
  - 英文说明文本。


## ArpgDiagnosticSeverity (class)

诊断级别：0=信息 1=警告 2=错误。

- static int Info()
  - 信息级（0）。

- static int Warning()
  - 警告级（1）。

- static int Error()
  - 错误级（2），存在时 Validate 判定为不通过。


## ArpgDiagnostics (class)

收集校验错误，而不是在首个问题处抛异常。

- List<ArpgDiagnostic> items;

- int errorCount;

- int warningCount;

- ArpgDiagnostics()
  - 构造空诊断集合。

- void AddError(string code, string path, string message)
  - 追加一条错误诊断并使 ErrorCount() 自增。

- void AddWarning(string code, string path, string message)
  - 追加一条警告诊断并使 WarningCount() 自增。

- bool HasErrors()
  - 是否存在任何错误（ErrorCount() > 0）。

- int ErrorCount()
  - 错误条数。

- int WarningCount()
  - 警告条数。

- int Count()
  - 诊断总数（错误+警告+信息）。

- ArpgDiagnostic At(int index)
  - 第 index 条诊断（不查越界）。

- void Append(ArpgDiagnostics other)
  - 把 other 的全部条目并入自身并累计各级计数。


## ArpgEquipSlot (class)

一件装备与其槽位名的绑定。

- string slot;
  - 槽位名（如 "weapon"、"armor"）。

- ArpgItemDefinition item;
  - 该槽位当前装备的物品定义。

- ArpgEquipSlot(string slot, ArpgItemDefinition item)
  - 构造一个槽位绑定。


## ArpgEquipment (class)

角色的装备栏：槽位名到物品的映射。Equip 会先从背包扣件、
换装时旧件退回背包（退不下则整体失败）；Attributes 汇总
全部已装备物品的属性修正。

- List<ArpgEquipSlot> slots;

- ArpgEquipment()
  - 构造空装备栏。

- int IndexOf(string slot)
  - 槽位名对应下标；不存在返回 -1。

- ArpgItemDefinition Get(string slot)
  - 返回槽位上当前装备的物品；未装备返回 null。

- bool Equip(string slot, ArpgItemDefinition definition, ArpgInventory inventory)
  - 装备一件物品：要求槽位名非空、物品为 Equipment 类别、背包
    能扣出 1 件。替换时旧件退回背包，退不下则整体回滚返回
    false。成功返回 true。

- bool Unequip(string slot, ArpgInventory inventory)
  - 卸下槽位装备退回背包（放不下返回 false）。槽位为空或
    inventory 为 null 返回 false。

- int Count()
  - 已装备的槽位数。

- string SlotNameAt(int index)
  - 第 index 个槽位的名称（不查越界）。

- ArpgItemDefinition ItemAt(int index)
  - 第 index 个槽位的物品（不查越界）。

- ArpgAttributes Attributes()
  - 汇总全部已装备物品的属性修正（无装备时为零值属性）。


## ArpgEvents (class)

对应 DM3 App.lua 事件的类型化系统事件接口。每个事件都是
单播（后注册的 handler 覆盖前者），返回 bool 的事件未注册
handler 时的默认值见各 Raise 方法。经 Engine.Events() 获取，
框架在主循环里自动触发，业务代码只注册 On*。

- ArpgStarting starting;

- ArpgStarted started;

- ArpgClosing closing;

- ArpgFocusChanged focusChanged;

- ArpgWindowStateChanged windowStateChanged;

- ArpgSizeChanged sizeChanged;

- ArpgKeyChanged keyDown;

- ArpgKeyChanged keyUp;

- ArpgMenuChanged menuChanged;

- ArpgSystemPrompt systemPrompt;

- ArpgRichTextLinkChanged richTextLink;

- void OnStarting(ArpgStarting handler)
  - 启动前回调：返回 false 中止启动（Start 返回 false）。

- void OnStarted(ArpgStarted handler)
  - 启动完成后回调（窗口/渲染器已就绪）。

- void OnClosing(ArpgClosing handler)
  - 用户请求关闭时回调：返回 false 否决关闭（窗口保持打开）。

- void OnFocusChanged(ArpgFocusChanged handler)
  - 窗口焦点变化；focused 为是否获得焦点。

- void OnWindowStateChanged(ArpgWindowStateChanged handler)
  - 窗口状态变化：1=最小化 2=最大化 3=恢复。

- void OnSizeChanged(ArpgSizeChanged handler)
  - 窗口尺寸变化（单位为窗口像素，非逻辑坐标）。

- void OnKeyDown(ArpgKeyChanged handler)
  - 按下回调；keycode 为宿主键码（GuiHost 事件编码），修饰键为
    独立 bool。开启 RepeatKeys 时连发也触发，否则只触发首次按下。

- void OnKeyUp(ArpgKeyChanged handler)
  - 抬起回调；参数同 OnKeyDown。

- void OnMenuChanged(ArpgMenuChanged handler)
  - 菜单项激活回调（菜单名/项名/自定义载荷）。

- void OnSystemPrompt(ArpgSystemPrompt handler)
  - 系统提示回调（如背包已满、技能冷却中）。code 为提示类别，
    message 为英文说明。返回 true 表示业务已处理，框架不再
    弹默认提示；未注册 handler 时返回 false（走默认提示）。

- void OnRichTextLink(ArpgRichTextLinkChanged handler)
  - 富文本链接点击回调（窗口 Id/控件 Id/链接原文）。

- bool RaiseStarting(string arguments)
  - 触发启动回调；未注册时返回 true（允许启动）。

- void RaiseStarted()
  - 触发启动完成回调（未注册为空操作）。

- bool RaiseClosing()
  - 触发关闭请求；未注册时返回 true（允许关闭）。

- void RaiseFocusChanged(bool focused)
  - 触发焦点变化回调（未注册为空操作）。

- void RaiseWindowStateChanged(int state)
  - 触发窗口状态变化回调（state 取值见 OnWindowStateChanged）。

- void RaiseSizeChanged(int width, int height)
  - 触发窗口尺寸变化回调（单位为窗口像素）。

- void RaiseKeyDown(int keycode, bool alt, bool shift, bool control)
  - 触发按下回调（参数见 OnKeyDown）。

- void RaiseKeyUp(int keycode, bool alt, bool shift, bool control)
  - 触发抬起回调（参数见 OnKeyDown）。

- void RaiseMenuChanged(string menuName, string itemName, string payload)
  - 触发菜单项激活回调（未注册为空操作）。

- bool RaiseSystemPrompt(int code, string message)
  - 触发系统提示；未注册时返回 false（走默认提示路径）。

- void RaiseRichTextLink(string windowId, string controlId, string link)
  - 触发富文本链接点击回调（未注册为空操作）。


## ArpgFaction (class)

阵营字符串常量。战斗目标判定按 caster 与 target 的阵营比较；
Any 通配任意阵营。

- static string Friendly()
  - 友方（与施法者同阵营）。

- static string Neutral()
  - 中立（独立阵营，与 friendly/hostile 均不同）。

- static string Hostile()
  - 敌方（与施法者不同阵营）。

- static string Any()
  - 任意阵营。


## ArpgFormula (class)

ZGM 技能公式的小型整数表达式求值器。刻意接受受限的表达式
语言（非 Lua）：整数、a.属性/b.属性（来自左右两侧属性表）、
+ - * / ^、一元 +/-、括号与 min/max/clamp 函数。除法与负指数
按错误处理；任何语法/求值错误都把 valid 置 false，Evaluate
返回错误发生前的部分结果。

- string text;

- int index;

- bool valid;

- ArpgAttributes leftAttrs;

- ArpgAttributes rightAttrs;

- ArpgFormula(string text, ArpgAttributes leftAttrs, ArpgAttributes rightAttrs)
  - 构造求值器：text 为表达式，leftAttrs/rightAttrs 分别对应
    a.* 与 b.* 属性前缀（可为 null，此时引用前缀即报错）。

- bool IsValid()
  - 表达式是否完整有效（无语法/求值错误）。

- int Current()
  - 返回当前字符的字节值；已到末尾返回 -1。

- void SkipSpace()
  - 跳过当前位置的空白字符（空格/制表/换行/回车）。

- bool Match(int expected)
  - 跳过空白后若当前字符为 expected 则消费之并返回 true。

- int ParseNumber()
  - 解析十进制整数；无数字时置错并返回 0（不识别符号与小数）。

- bool IsNameChar(int c)
  - 判断 c 是否可作为标识符字符（字母/数字/点/下划线/高位字节）。

- string ParseName()
  - 解析标识符（属性名或函数名）；无有效字符时置错并返回空串。

- int Attribute(string name)
  - 解析属性引用：a.前缀取左属性表，b.前缀取右属性表（截断取
    整）；前缀不符或对应属性表为 null 时置错并返回 0。

- int ParsePrimary()
  - 解析基础表达式：数字、括号子表达式、属性引用或
    min/max/clamp 函数调用；语法不符时置错并返回 0。

- int ParseUnary()
  - 解析一元 +/- 前缀（可叠加，如 --x）。

- int Power(int baseValue, int exponent)
  - 整数幂运算；指数为负置错并返回 0。

- int ParsePower()
  - 解析 ^ 幂层（右操作数经一元层，支持 2^-3 形式语法）。

- int ParseMulDiv()
  - 解析 * / 乘除层；除数为 0 置错且该步结果按 0 计。

- int ParseAddSub()
  - 解析 + - 加减层（优先级最低）。

- int Evaluate()
  - 求值整个表达式并返回整数结果；有剩余未消费字符视为语法
    错误（置错）。结果是否可信以 IsValid 为准。


## ArpgGameplayEvents (class)

类型化运行时发出的游戏玩法事件（物品增减/使用、技能结算、
buff 变化、伤害与死亡、切图）。同样单播，框架在世界更新时
自动触发；经 Engine.GameplayEvents() 获取。

- ArpgItemChanged itemChanged;

- ArpgItemUsed itemUsed;

- ArpgSkillResolved skillResolved;

- ArpgBuffChanged buffChanged;

- ArpgActorDamaged actorDamaged;

- ArpgActorDied actorDied;

- ArpgMapChanged mapChanged;

- void OnItemChanged(ArpgItemChanged handler)
  - 注册物品增减回调（quantity 为该物品当前持有总量）。

- void OnItemUsed(ArpgItemUsed handler)
  - 注册物品使用回调。

- void OnSkillResolved(ArpgSkillResolved handler)
  - 注册技能结算完成回调。

- void OnBuffChanged(ArpgBuffChanged handler)
  - 注册增益增删回调（added=true 添加）。

- void OnActorDamaged(ArpgActorDamaged handler)
  - 注册角色受伤回调（amount 为实际生效伤害）。

- void OnActorDied(ArpgActorDied handler)
  - 注册角色死亡回调（killer 可为 null）。

- void OnMapChanged(ArpgMapChanged handler)
  - 注册切图完成回调（首次进图 previousMapId 为空串）。

- void RaiseItemChanged(ArpgActor actor, ArpgItemDefinition item, int quantity)
  - 触发物品增减回调（未注册为空操作）。

- void RaiseItemUsed(ArpgActor actor, ArpgItemDefinition item)
  - 触发物品使用回调（未注册为空操作）。

- void RaiseSkillResolved(ArpgSkillCastResult result)
  - 触发技能结算回调（未注册为空操作）。

- void RaiseBuffChanged(ArpgActor actor, ArpgBuffInstance buff, bool added)
  - 触发增益增删回调（未注册为空操作）。

- void RaiseActorDamaged(ArpgActor source, ArpgActor target, double amount, bool critical)
  - 触发角色受伤回调（未注册为空操作）。

- void RaiseActorDied(ArpgActor actor, ArpgActor killer)
  - 触发角色死亡回调（未注册为空操作）。

- void RaiseMapChanged(string previousMapId, string currentMapId, int portalId)
  - 触发切图完成回调（未注册为空操作）。


## ArpgGlobals (class)

玩法、UI 与适配器共享的类型化进程/游戏状态。值
特意可枚举，便于应用在外部序列化。

- List<GlobalEntry> entries;

- ArpgGlobals()

- int IndexOf(string key)
  - 返回 key 的存储下标；不存在时返回 -1（区分大小写精确匹配）。

- ArpgGlobals Put(string key, string content, int kind)
  - 插入或更新条目：已存在同名 key 时覆盖值与类型标签，否则
    追加到末尾。返回 this 便于链式调用。

- ArpgGlobals Set(string key, string content)
  - 写入字符串全局变量（类型标签 0），返回 this。

- ArpgGlobals SetInt(string key, int amount)
  - 写入整数全局变量（以十进制字符串存储，类型标签 1），返回 this。

- ArpgGlobals SetBool(string key, bool enabled)
  - 写入布尔全局变量（"1"/"0" 存储，类型标签 2），返回 this。

- bool Contains(string key)
  - 是否存在指定 key 的条目。

- int Count()
  - 条目总数。

- string KeyAt(int index)
  - 第 index 个条目的名称；越界行为由底层 List 决定（抛错）。

- string ValueAt(int index)
  - 第 index 个条目的字符串编码值。

- int KindAt(int index)
  - 第 index 个条目的类型标签（0 = string，1 = int，2 = bool）。

- string Get(string key, string fallback)
  - 读取字符串全局变量；不存在时返回 fallback。

- int GetInt(string key, int fallback)
  - 读取整数全局变量；不存在或类型标签不是 1 时返回 fallback（不做转换）。

- bool GetBool(string key, bool fallback)
  - 读取布尔全局变量；不存在或类型标签不是 2 时返回 fallback，
    否则值为 "1" 即 true。

- bool Remove(string key)
  - 移除指定 key 的条目并返回 true；不存在时不做修改返回 false。

- void Clear()
  - 清空全部条目。


## ArpgGrowthDefinition (class)

成长曲线定义：按等级预先定义一组属性快照，供升级/进阶时按
等级取属性。Validate 校验 Id 非空、等级为正且不重复；无任何
等级条目时产生警告。

- string id;

- List<ArpgGrowthLevel> levels;

- ArpgGrowthDefinition(string id)
  - 构造成长曲线：id 为唯一标识，等级条目经 AddLevel 逐级添加。

- string Id()
  - 返回成长曲线唯一 Id。

- void AddLevel(int level, ArpgAttributes attributes)
  - 添加一个等级条目：level 为等级，attributes 为该等级的属性快照。

- int LevelCount()
  - 返回已添加的等级条目数量。

- ArpgGrowthLevel LevelAt(int index)
  - 返回第 index 个等级条目；index 须在 [0, LevelCount) 内。

- ArpgAttributes AttributesFor(int level)
  - 按等级精确匹配查找属性快照；该等级未定义时返回 null。

- void Validate(ArpgDiagnostics diagnostics)
  - 校验成长曲线：Id 非空；等级必须为正且互不重复；无等级条目时仅产生警告。错误/警告写入 diagnostics。


## ArpgGrowthLevel (class)

成长曲线中的单级条目：某一等级对应的完整属性快照。由
ArpgGrowthDefinition.AddLevel 创建，业务经 AttributesFor 查询。

- int level;

- ArpgAttributes attributes;

- ArpgGrowthLevel(int level, ArpgAttributes attributes)

- int Level()
  - 返回该条目对应的等级（正整数）。

- ArpgAttributes Attributes()
  - 返回该等级的属性快照（共享引用，勿就地修改）。


## ArpgInventory (class)

角色与背包控件使用的可堆叠格子背包。

- int capacity;

- List<ArpgItemInstance> slots;

- ArpgInventory(int capacity)
  - 构造背包；容量钳制到 ≥1。

- int Capacity()
  - 总格数上限（≥1）。

- int SlotCount()
  - 当前已用格数（未堆满的同 id 物品共享一格）。

- ArpgItemInstance SlotAt(int index)
  - 第 index 个格子实例（不查越界）。

- bool IsFull()
  - 已用格数是否达到容量上限。

- int QuantityOf(string itemId)
  - 指定物品 id 的总数量（跨全部格子求和）。

- ArpgItemInstance Find(string itemId)
  - 返回第一个包含指定物品的格子；没有返回 null。

- int Add(ArpgItemDefinition definition, int count)
  - 加入物品：先填已有同 id 堆（至 MaxStack），再开新格。
    返回实际加入数；容量/堆叠不足时可少于 count
    （definition 为 null 或 count ≤ 0 返回 0）。

- bool Remove(string itemId, int count)
  - 移除指定数量的物品（库存不足时整体失败返回 false，
    不做部分移除）。空格子自动回收。count ≤ 0 返回 false。


## ArpgItemCategory (class)

物品分类常量：ArpgItemDefinition.SetCategory 的合法取值。
静态方法返回字符串常量，集中定义分类字面量。

- static string Item()
  - 普通物品分类，常量 "item"（新建物品的默认分类）。

- static string Equipment()
  - 装备分类，常量 "equipment"。

- static string Special()
  - 特殊物品分类，常量 "special"。


## ArpgItemDefinition (class)

物品定义：Id 与 DisplayName 必填，其余字段有默认值，通过 Set*
逐项配置。冷却按 CooldownGroup 共享（毫秒）；消耗品用 SetRestore
配置恢复量；Validate 在配置加载后校验合法性。

- string id;

- string displayName;

- string iconResource;

- string tooltip;

- string category;

- string subcategory;

- string detailCategory;

- string cooldownGroup;

- int cooldownMilliseconds;

- int maxStack;

- bool discardDisabled;

- int disappearMilliseconds;

- int requiredLevel;

- int restoreHp;

- int restoreMp;

- double restoreHpPercent;

- double restoreMpPercent;

- ArpgAttributes attributes;

- ArpgItemDefinition(string id, string displayName)

- string Id()
  - 物品唯一 Id（配置键，创建后不变）。

- string DisplayName()
  - 显示名（必填，用于 UI 展示）。

- string IconResource()
  - 图标资源路径，默认空串。

- string Tooltip()
  - 悬浮提示文本，默认空串。

- string Category()
  - 物品分类（ArpgItemCategory 常量之一，默认 "item"）。

- string Subcategory()
  - 子分类自由文本，默认空串。

- string DetailCategory()
  - 细分类自由文本，默认空串。

- string CooldownGroup()
  - 冷却组名；同组物品共享冷却，空串表示不参与组冷却。

- int CooldownMilliseconds()
  - 冷却时长（毫秒），默认 0 表示无冷却。

- int MaxStack()
  - 单格最大堆叠数，默认 1（不可堆叠）。

- bool DiscardDisabled()
  - true 时禁止丢弃（只能使用或卖出等业务途径移除）。

- int DisappearMilliseconds()
  - 掉落物消失时间（毫秒），默认 40000；-1 表示永不消失。

- int RequiredLevel()
  - 使用所需最低角色等级，默认 1。

- int RestoreHp()
  - 使用时固定恢复的 HP 点数，默认 0。

- int RestoreMp()
  - 使用时固定恢复的 MP 点数，默认 0。

- double RestoreHpPercent()
  - 使用时按最大值百分比恢复的 HP（0.0~1.0 小数），默认 0.0。

- double RestoreMpPercent()
  - 使用时按最大值百分比恢复的 MP（0.0~1.0 小数），默认 0.0。

- ArpgAttributes Attributes()
  - 装备/使用附带的属性加成集合（只读视图，配置期通过其 Set* 修改）。

- void SetIconResource(string newValue)
  - 设置图标资源路径。

- void SetTooltip(string newValue)
  - 设置悬浮提示文本。

- void SetCategory(string newValue)
  - 设置物品分类，取值应为 ArpgItemCategory 的常量。

- void SetSubcategory(string newValue)
  - 设置子分类文本。

- void SetDetailCategory(string newValue)
  - 设置细分类文本。

- void SetCooldown(string group, int milliseconds)
  - 设置冷却组与冷却时长（毫秒）；同组物品共享冷却。

- void SetMaxStack(int newValue)
  - 设置单格最大堆叠数。

- void SetDiscardDisabled(bool newValue)
  - 设置是否禁止丢弃。

- void SetDisappearMilliseconds(int newValue)
  - 设置掉落物消失时间（毫秒）；-1 表示永不消失。

- void SetRequiredLevel(int newValue)
  - 设置使用所需最低等级。

- void SetRestore(int hp, int mp, double hpPercent, double mpPercent)
  - 一次性设置恢复量：固定点数与最大值百分比可同时生效。

- void Validate(ArpgDiagnostics diagnostics)
  - 校验物品配置合法性，问题以错误码 L2ITM001~L2ITM006 写入
    diagnostics（必填字段缺失、maxStack/requiredLevel 非正、
    冷却为负、消失时间小于 -1）。应在配置加载完成后调用一次。


## ArpgItemInstance (class)

背包内单个格子的实例：物品定义 + 当前堆叠数量。

- ArpgItemDefinition definition;

- int quantity;

- ArpgItemInstance(ArpgItemDefinition definition, int quantity)
  - 构造一个格子实例。

- ArpgItemDefinition Definition()
  - 物品定义。

- int Quantity()
  - 当前堆叠数量。

- bool IsEmpty()
  - 数量是否已耗尽（≤0）。

- int Add(int count)
  - 增加数量，受 MaxStack 上限约束，返回实际增加数
    （count ≤ 0 返回 0，可部分加入）。

- int Remove(int count)
  - 移除数量，最多移除到 0，返回实际移除数
    （count ≤ 0 返回 0，可部分移除）。


## ArpgMapCell (class)

地图格子：位置 + 类型（1 普通、2 半透明障碍、3 阻挡）。阻挡
类型为 1 或 3，遮挡视野类型为 2 或 3。由 AddCell 挂到地图。

- int x;

- int y;

- int cellType;

- ArpgMapCell(int x, int y, int cellType)

- int X()
  - 格子 X 坐标（格）。

- int Y()
  - 格子 Y 坐标（格）。

- int CellType()
  - 格子类型（1 普通、2 半透明障碍、3 阻挡）。

- bool IsBlocked()
  - 是否阻挡移动（类型 1 或 3）。

- bool IsTransparent()
  - 是否遮挡视野（类型 2 或 3）。


## ArpgMapDefinition (class)

含出生点、阻挡与传送门的网格地图定义。

- string id;

- int cellWidth;

- int cellHeight;

- int columns;

- int rows;

- bool defaultMap;

- int initialGridX;

- int initialGridY;

- int initialRange;

- string templateResource;

- string miniMapResource;

- ArpgColor ambientColor;

- bool allowDropsOnBlockedCells;

- List<ArpgMapCell> cells;

- List<ArpgMapSpawn> spawns;

- List<ArpgMapPortal> portals;

- ArpgMapDefinition(string id, int columns, int rows)

- string Id()
  - 地图 Id（配置键，创建后不变）。

- int CellWidth()
  - 单元格宽度（像素），默认 32。

- int CellHeight()
  - 单元格高度（像素），默认 32。

- int Columns()
  - 横向格数。

- int Rows()
  - 纵向格数。

- bool DefaultMap()
  - 是否默认地图（玩家初始进入的地图，应只有一张置 true）。

- int InitialGridX()
  - 初始出生格 X。

- int InitialGridY()
  - 初始出生格 Y。

- int InitialRange()
  - 初始散布半径（格）。

- string TemplateResource()
  - 地图模板资源路径，空串表示纯代码构建。

- string MiniMapResource()
  - 小地图图片资源路径，空串表示无小地图。

- ArpgColor AmbientColor()
  - 环境光颜色（默认白色即无染色）。

- void SetCellSize(int width, int height)
  - 一次性设置单元格尺寸（像素）。

- void SetDefaultMap(bool newValue)
  - 设置是否为默认地图。

- void SetInitialPosition(int x, int y, int range)
  - 一次性设置初始出生点与散布半径（格）。

- void SetTemplateResource(string newValue)
  - 设置地图模板资源路径。

- void SetMiniMapResource(string newValue)
  - 设置小地图图片资源路径。

- void SetAmbientColor(ArpgColor newValue)
  - 设置环境光颜色。

- void SetAllowDropsOnBlockedCells(bool newValue)
  - 设置是否允许在阻挡格上掉落物品（默认不允许）。

- void AddCell(ArpgMapCell newValue)
  - 添加一个格子（同坐标重复添加时以先匹配到的为准）。

- void AddSpawn(ArpgMapSpawn newValue)
  - 添加一个刷怪点。

- void AddPortal(ArpgMapPortal newValue)
  - 添加一个传送门。

- int CellCount()
  - 已添加格子数量。

- ArpgMapCell CellAt(int index)
  - 按下标读取格子；应配合 CellCount 使用。

- int SpawnCount()
  - 刷怪点数量。

- ArpgMapSpawn SpawnAt(int index)
  - 按下标读取刷怪点；应配合 SpawnCount 使用。

- int PortalCount()
  - 传送门数量。

- ArpgMapPortal PortalAt(int index)
  - 按下标读取传送门；应配合 PortalCount 使用。

- bool IsInside(int x, int y)
  - 坐标是否在地图网格内（0 ≤ x < columns 且 0 ≤ y < rows）。

- bool IsBlocked(int x, int y)
  - 指定格是否阻挡移动；无格子记录的坐标视为可通行（false）。
    线性查找，热路径宜缓存结果。

- void Validate(ArpgDiagnostics diagnostics)
  - 校验地图配置合法性，问题以错误码 L2MAP001~L2MAP016 写入
    diagnostics（必填缺失、尺寸非正、出生点越界、格子类型非法、
    传送门/刷怪点配置错误）。应在配置加载完成后调用一次。


## ArpgMapPortal (class)

传送门定义：踩到 (gridX, gridY) 时切到 targetMapId 的
(targetGridX, targetGridY)。id 由 SetId 指定后可用于事件定位。

- int gridX;

- int gridY;

- string targetMapId;

- int targetGridX;

- int targetGridY;

- int targetDirection;

- bool clearTargetMap;

- int id;

- ArpgMapPortal(int gridX, int gridY, string targetMapId, int targetGridX, int targetGridY)

- int GridX()
  - 传送门所在格 X。

- int GridY()
  - 传送门所在格 Y。

- string TargetMapId()
  - 目标地图 Id（切图后 ArpgMapChanged 回调的 currentMapId）。

- int TargetGridX()
  - 落点格 X。

- int TargetGridY()
  - 落点格 Y。

- int TargetDirection()
  - 落点朝向（0~7），默认 0。

- bool ClearTargetMap()
  - true 时到达后清空目标地图数据（一次性传送，防回传死循环）。

- int Id()
  - 传送门 Id（默认 0，事件回调中非传送门切图为 -1）。

- void SetTargetDirection(int newValue)
  - 设置落点朝向（0~7）。

- void SetClearTargetMap(bool newValue)
  - 设置是否清空目标地图数据（一次性传送）。

- void SetId(int newValue)
  - 设置传送门 Id。


## ArpgMapSpawn (class)

刷怪点定义：在 (gridX, gridY) 附近 range 格范围内刷 count 只
指定阵营/等级的怪物。respawnMilliseconds 为 -1 表示不重生。

- string actorId;

- string faction;

- int gridX;

- int gridY;

- int range;

- int count;

- int level;

- int direction;

- int respawnMilliseconds;

- ArpgMapSpawn(string actorId, string faction, int gridX, int gridY, int count)

- string ActorId()
  - 刷出的角色定义 Id。

- string Faction()
  - 刷出角色的阵营。

- int GridX()
  - 刷怪中心格 X。

- int GridY()
  - 刷怪中心格 Y。

- int Range()
  - 随机散布半径（格），默认 0 表示固定在中心点。

- int Count()
  - 刷出数量。

- int Level()
  - 刷出等级，默认 1。

- int Direction()
  - 初始朝向（-1 或 0~7），默认 -1 表示未定向。

- int RespawnMilliseconds()
  - 重生间隔（毫秒），默认 -1 表示不重生。

- void SetRange(int newValue)
  - 设置随机散布半径（格）。

- void SetLevel(int newValue)
  - 设置刷出等级。

- void SetDirection(int newValue)
  - 设置初始朝向（-1 或 0~7）。

- void SetRespawnMilliseconds(int newValue)
  - 设置重生间隔（毫秒）；-1 表示不重生。


## ArpgMath (class)

Arpg 校验/构造时使用的整型与浮点小工具。

- static int Clamp(int newValue, int minimum, int maximum)
  - 返回 newValue 钳制到 [minimum, maximum] 后的结果。

- static double ClampDouble(double newValue, double minimum, double maximum)
  - 返回 newValue 钳制到 [minimum, maximum] 后的结果（浮点版）。

- static int Max(int left, int right)
  - 返回两者中较大的整数；相等时返回 right。


## ArpgMenu (class)

类型化菜单：菜单名 + 按序排列的菜单项集合。AddItem 按项名
upsert（同名覆盖），渲染适配器可消费同一菜单状态。

- string name;

- List<ArpgMenuItem> items;

- ArpgMenu(string name)
  - 构造空菜单。

- string Name()
  - 菜单名（同一运行时内的唯一键）。

- ArpgMenu AddItem(ArpgMenuItem item)
  - 追加或按名称覆盖菜单项（item 为 null 忽略），返回 this。

- int IndexOf(string itemName)
  - 按项名查找索引；未找到返回 -1。

- ArpgMenuItem FindItem(string itemName)
  - 按项名查找菜单项；未找到返回 null。

- int ItemCount()
  - 菜单项数量。

- ArpgMenuItem ItemAt(int index)
  - 第 index 个菜单项（不查越界）。

- void Dispose()
  - 释放菜单项集合（之后不应再使用本菜单）。


## ArpgMenuItem (class)

应用菜单中的单个菜单项：名称（唯一键）、标题、自定义载荷与
可见/启用/选中三个状态位。构造时默认可见且启用、未选中；
Set* 修饰器均返回 this，可链式配置。项被激活时 Payload 随
ArpgEvents 的 MenuChanged 回调上抛。

- string name;

- string title;

- string payload;

- bool visible;

- bool enabled;

- bool marked;

- ArpgMenuItem(string name, string title, string payload)
  - 构造菜单项；visible/enabled 默认 true，marked 默认 false。

- string Name()
  - 菜单项名（同一菜单内的唯一键）。

- string Title()
  - 显示标题。

- string Payload()
  - 激活时随菜单事件上抛的自定义载荷。

- bool Visible()
  - 是否可见（不可见项不会被分派激活）。

- bool Enabled()
  - 是否启用（禁用项不会被分派激活）。

- bool Marked()
  - 是否带选中标记。

- ArpgMenuItem SetTitle(string title)
  - 设置标题并返回 this（链式）。

- ArpgMenuItem SetPayload(string payload)
  - 设置载荷并返回 this（链式）。

- ArpgMenuItem SetVisible(bool visible)
  - 设置可见性并返回 this（链式）。

- ArpgMenuItem SetEnabled(bool enabled)
  - 设置启用状态并返回 this（链式）。

- ArpgMenuItem SetMarked(bool marked)
  - 设置选中标记并返回 this（链式）。


## ArpgMenuRuntime (class)

菜单运行时：管理命名菜单集合并分派激活。只分派可见且启用
的项，激活成功经 ArpgEvents.RaiseMenuChanged 上抛（菜单名/
项名/载荷）。渲染适配器可消费同一菜单状态并订阅 ArpgEvents。

- List<ArpgMenu> menus;

- ArpgEvents events;

- ArpgMenuRuntime(ArpgEvents events)
  - 构造运行时；events 可为 null（激活时不上抛事件）。

- int IndexOf(string name)
  - 按菜单名查找索引；未找到返回 -1。

- ArpgMenuRuntime AddMenu(ArpgMenu menu)
  - 追加或按名称覆盖菜单（menu 为 null 忽略），返回 this。

- ArpgMenu FindMenu(string name)
  - 按菜单名查找菜单；未找到返回 null。

- int MenuCount()
  - 菜单数量。

- ArpgMenu MenuAt(int index)
  - 第 index 个菜单（不查越界）。

- bool Activate(string menuName, string itemName)
  - 激活指定菜单的指定项：菜单或项不存在、项不可见或禁用时
    返回 false（不上抛事件）；成功时经 RaiseMenuChanged 上抛
    （菜单名/项名/载荷）并返回 true。

- void Dispose()
  - 释放全部菜单并断开事件引用（之后不应再使用本运行时）。


## ArpgMessage (class)

以通信 id（cid）为键的 ZGM 消息。线上帧格式为
u32 载荷长度 + u32 cid + 正文（各字段大端序），Encode/Decode
互为逆操作；正文按单字节字符收发。

- int cid;
  - 通信 id，业务侧用于路由消息。

- string body;
  - 消息正文。

- int bodyLen;
  - 正文长度（与 body.Length 一致）。

- static ArpgMessage Of(int cid, string body, int bodyLen)
  - 构造消息（显式给出正文长度）。

- static ArpgMessage Text(int cid, string body)
  - 构造文本消息（正文长度自动取 body.Length）。

- int Cid()
  - 通信 id。

- string Body()
  - 消息正文。

- int BodyLength()
  - 正文长度。

- int FrameLength()
  - 整帧字节数：8 字节头 + 正文长度。

- byte[]Encode()
  - 编码为帧字节序列（长度前缀 + cid 大端序 + 正文）。

- static ArpgMessage Decode(string buf, int len)
  - 解码 buf[0..len) 中的第一条帧记录；字节不足 8（头）或不足
    声明长度时返回 null，调用方应继续等待更多数据。


## ArpgMusic (class)

与渲染器/音频后端无关的音乐对象。音频适配器可观察
该状态，并将 Play/Pause/Stop 映射到其原生混音器。

- string resource;

- double volume;

- double pitch;

- int channel;

- int length;

- bool loop;

- MusicState state;

- ArpgMusic(string resource)

- ArpgMusic SetVolume(double volume)
  - 设置音量（1.0 为默认，取值范围由音频适配器解释），返回 this。

- ArpgMusic SetPitch(double pitch)
  - 设置音调/速度倍率（1.0 为原速），返回 this。

- ArpgMusic SetChannel(int channel)
  - 设置混音通道（默认 -1，语义由音频适配器解释），返回 this。

- ArpgMusic SetLoop(bool loop)
  - 设置是否循环播放，返回 this。

- void Play()
  - 开始播放：状态无条件置为 Playing（含从 Paused 恢复）。

- void Pause()
  - 暂停播放；仅当前为 Playing 时转入 Paused，其余状态无操作。

- void Stop()
  - 停止播放：状态无条件回到 Stopped。

- string Resource()
  - 音乐资源路径。

- double Volume()
  - 音量（默认 1.0）。

- double Pitch()
  - 音调/速度倍率（默认 1.0）。

- int Channel()
  - 混音通道（默认 -1）。

- int Length()
  - 音频长度（毫秒）；本类不维护（恒为构造初值 0），语义由适配器/调用方定义。

- bool Loop()
  - 是否循环播放。

- MusicState State()
  - 当前播放状态（MusicState 之一，初始 Stopped）。

- void Dispose()
  - 释放资源路径引用（置 null）；此后不应再使用该对象。


## ArpgNetDefaults (class)

TCP 客户端/服务器共用的默认消息包上限：2 * 1024 * 1024 字节。

- static int MaxMessageLength()
  - 默认单条消息包上限（字节），即 2 MiB。


## ArpgNineSlice (class)

九宫格贴图描述：四角/四边/中心各用一张资源 Id 拉伸或平铺填充，
SetInsets 决定四条边的留边宽度（像素）。构造时留边默认为 0，
即全部区域用 Center 资源填充。

- string topLeft;

- string top;

- string topRight;

- string left;

- string center;

- string right;

- string bottomLeft;

- string bottom;

- string bottomRight;

- int leftWidth;

- int topHeight;

- int rightWidth;

- int bottomHeight;

- ArpgNineSlice(string topLeft, string top, string topRight, string left, string center, string right, string bottomLeft, string bottom, string bottomRight)
  - 构造九宫格：依次传入左上/上/右上/左/中/右/左下/下/右下九块资源 Id；留边默认 0。

- string TopLeft()
  - 返回左上角资源 Id。

- string Top()
  - 返回上边资源 Id。

- string TopRight()
  - 返回右上角资源 Id。

- string Left()
  - 返回左边资源 Id。

- string Center()
  - 返回中心资源 Id。

- string Right()
  - 返回右边资源 Id。

- string BottomLeft()
  - 返回左下角资源 Id。

- string Bottom()
  - 返回下边资源 Id。

- string BottomRight()
  - 返回右下角资源 Id。

- int LeftWidth()
  - 返回左边留边宽度（像素）。

- int TopHeight()
  - 返回上边留边高度（像素）。

- int RightWidth()
  - 返回右边留边宽度（像素）。

- int BottomHeight()
  - 返回下边留边高度（像素）。

- void SetInsets(int left, int top, int right, int bottom)
  - 设置四边留边（像素）；负值一律按 0 处理。


## ArpgNode (class)

UI 节点运行时实例：由 ArpgNodeDefinition 构造，包装定义并提供
求值后的内容/富文本文档、动画播放状态与指针命中测试。大部分
属性读取透传定义；Content() 返回模板求值后的文本，Resource()
在动画节点上返回当前帧资源。构造时即触发 Created 事件。

- ArpgNodeDefinition definition;

- ArpgDataSource dataSource;

- string evaluatedContent;

- ArpgRichTextDocument document;

- bool focused;

- bool inheritedDataSource;

- bool playing;

- int frameIndex;

- int frameElapsed;

- ArpgNode(ArpgNodeDefinition definition, ArpgDataSource defaultSource)
  - 构造节点实例；defaultSource 为定义未绑定数据源时的继承来源，构造即触发 Created 事件。

- string Id()
  - 返回节点唯一 Id（同定义 Id）。

- string Kind()
  - 返回节点类型字符串。

- int X()
  - 返回节点 X 坐标（像素，相对控件原点）。

- int Y()
  - 返回节点 Y 坐标（像素，相对控件原点）。

- int Width()
  - 返回节点宽度（像素）。

- int Height()
  - 返回节点高度（像素）。

- int Order()
  - 返回绘制顺序；数值大者在上层。

- bool Visible()
  - 返回节点是否可见。

- bool MouseEvents()
  - 返回节点是否接收鼠标事件。

- double AnchorX()
  - 返回水平锚点（0=左缘，1=右缘）。

- double AnchorY()
  - 返回垂直锚点（0=上缘，1=下缘）。

- double ScaleX()
  - 返回水平缩放系数。

- double ScaleY()
  - 返回垂直缩放系数。

- int Angle()
  - 返回旋转角度（度，顺时针）。

- double Opacity()
  - 返回不透明度（0.0~1.0）。

- bool Clip()
  - 返回是否把内容裁剪到节点边界内。

- string Content()
  - 返回模板求值后的文本内容（含 AppendContent 追加部分在控件层才生效，节点层仅定义内容求值）。

- string Resource()
  - 返回当前生效的资源 Id：动画节点返回当前帧资源，其他节点返回定义资源。

- ArpgColor Color()
  - 返回前景/文字颜色。

- ArpgColor BackgroundColor()
  - 返回背景色。

- ArpgColor BorderColor()
  - 返回边框颜色。

- ArpgColor OutlineColor()
  - 返回描边颜色（仅 ArtText 使用）。

- int BorderWidth()
  - 返回边框宽度（像素）。

- int Radius()
  - 返回圆角半径（像素）。

- int FontSize()
  - 返回字体大小（像素）。

- int HorizontalAlign()
  - 返回水平对齐（ArpgTextAlign 常量）。

- int VerticalAlign()
  - 返回垂直对齐（ArpgTextAlign 常量）。

- bool Wrap()
  - 返回文本是否自动换行。

- int LineSpacing()
  - 返回行间距（像素）。

- ArpgNineSlice NineSlice()
  - 返回九宫格描述；未设置时为 null。

- bool Playing()
  - 返回动画是否正在播放。

- int FrameIndex()
  - 返回当前动画帧索引（从 0 开始）。

- ArpgRichTextDocument Document()
  - 返回求值后的富文本文档（非富文本节点为单 run 文档）。

- ArpgNodeEventHandlers Events()
  - 返回节点事件槽位集合（单播）。

- ArpgDataSource DataSource()
  - 返回节点当前生效的数据源（私有的或继承的）。

- bool UsesInheritedDataSource()
  - 返回节点是否正在使用继承来的数据源（定义未绑定私有数据源时为 true）。

- void SetDataSource(ArpgDataSource newValue)
  - 设置节点私有数据源并立即重新求值内容；之后不再继承。

- void SetInheritedDataSource(ArpgDataSource newValue)
  - 设置继承数据源并立即重新求值内容（继承标记保持为 true）。

- void Evaluate()
  - 重新求值内容：模板占位符按当前数据源替换，富文本节点重新解析文档；框架每帧调用，业务一般无需手动调用。

- void Update(int deltaMilliseconds)
  - 每帧推进：重新求值内容，并按帧率推进动画帧（到末尾时按 LoopAnimation 循环或停在最后一帧）；随后触发 Updated 事件。

- void Play()
  - 恢复动画播放。

- void Pause()
  - 暂停动画（停在当前帧）。

- void ResetAnimation()
  - 把动画重置回第 0 帧（不改变播放状态）。

- bool HitTest(double localX, double localY)
  - 命中测试：localX/localY 为节点局部坐标（相对节点原点），可见节点按缩放与锚点计算的实际矩形判断，命中返回 true。

- void SetFocused(bool newValue)
  - 设置焦点状态；变化时触发 FocusChanged 事件，不变时为空操作。

- void FireCustom(string name, string payload)
  - 触发节点自定义事件（payload 原样透传给 OnCustomEvent 注册的 handler）。


## ArpgNodeDefinition (class)

UI 节点定义：静态描述一个节点（文本/精灵/矩形/圆/动画等）的
位置、尺寸、变换、样式、内容与事件。所有数值属性 x/y/width/
height 均为像素，坐标相对所属控件原点。构造时各属性取默认值
（可见、不透明、缩放 1、字号 16、帧率 12、循环自动播放等），
通过 Set* 方法修改；Validate 校验定义合法性并向 diagnostics
写入错误。

- string id;

- string kind;

- int x;

- int y;

- int width;

- int height;

- int order;

- bool visible;

- bool mouseEvents;

- double anchorX;

- double anchorY;

- double scaleX;

- double scaleY;

- int angle;

- double opacity;

- bool clip;

- bool richText;

- string content;

- string resource;

- ArpgColor color;

- ArpgColor backgroundColor;

- ArpgColor borderColor;

- ArpgColor outlineColor;

- int borderWidth;

- int radius;

- int fontSize;

- int horizontalAlign;

- int verticalAlign;

- bool wrap;

- int lineSpacing;

- int frameRate;

- bool loopAnimation;

- bool autoPlay;

- List<string> animationFrames;

- ArpgNineSlice nineSlice;

- ArpgDataSource dataSource;

- ArpgNodeEventHandlers events;

- ArpgNodeDefinition(string id, string kind, int x, int y, int width, int height)
  - 构造节点定义：id 为节点唯一标识，kind 取 ArpgNodeKind 常量，x/y/width/height 为像素。

- string Id()
  - 返回节点唯一 Id。

- string Kind()
  - 返回节点类型（ArpgNodeKind 字符串）。

- int X()
  - 返回节点 X 坐标（像素，相对控件原点）。

- int Y()
  - 返回节点 Y 坐标（像素，相对控件原点）。

- int Width()
  - 返回节点宽度（像素）。

- int Height()
  - 返回节点高度（像素）。

- int Order()
  - 返回绘制顺序；同区域按值从小到大后绘制（数值大者在上层）。

- bool Visible()
  - 返回节点是否可见（默认 true）。

- bool MouseEvents()
  - 返回节点是否接收鼠标事件（默认 false）。

- double AnchorX()
  - 返回水平锚点（0=左缘，1=右缘）。

- double AnchorY()
  - 返回垂直锚点（0=上缘，1=下缘）。

- double ScaleX()
  - 返回水平缩放系数。

- double ScaleY()
  - 返回垂直缩放系数。

- int Angle()
  - 返回旋转角度（度，顺时针）。

- double Opacity()
  - 返回不透明度（0.0~1.0）。

- bool Clip()
  - 返回是否把子内容裁剪到节点边界内。

- bool RichText()
  - 返回内容是否按富文本解析（由 SetContent 指定）。

- string Content()
  - 返回文本内容（模板占位符原文，运行时才求值）。

- string Resource()
  - 返回贴图资源 Id（动画节点返回空，帧序列用 AddAnimationFrame）。

- ArpgColor Color()
  - 返回前景/文字颜色。

- ArpgColor BackgroundColor()
  - 返回背景色。

- ArpgColor BorderColor()
  - 返回边框颜色。

- ArpgColor OutlineColor()
  - 返回描边颜色（仅 ArtText 使用）。

- int BorderWidth()
  - 返回边框宽度（像素）。

- int Radius()
  - 返回圆角半径（像素，仅矩形节点使用；圆节点另用 SetRadius）。

- int FontSize()
  - 返回字体大小（像素，默认 16）。

- int HorizontalAlign()
  - 返回水平对齐（ArpgTextAlign 常量）。

- int VerticalAlign()
  - 返回垂直对齐（ArpgTextAlign 常量）。

- bool Wrap()
  - 返回文本是否自动换行。

- int LineSpacing()
  - 返回行间距（像素）。

- int FrameRate()
  - 返回动画帧率（帧/秒，默认 12）。

- bool LoopAnimation()
  - 返回动画是否循环播放（默认 true）。

- bool AutoPlay()
  - 返回动画是否创建即播放（默认 true）。

- int AnimationFrameCount()
  - 返回动画帧数量（未添加帧时为 0）。

- string AnimationFrameAt(int index)
  - 返回第 index 帧的资源 Id；index 越界行为未定义（调用方需保证 0 <= index < AnimationFrameCount）。

- ArpgNineSlice NineSlice()
  - 返回九宫格描述；未设置时为 null。

- ArpgDataSource DataSource()
  - 返回数据源；未设置时为 null（运行时继承控件/窗口数据源）。

- ArpgNodeEventHandlers Events()
  - 返回节点事件槽位集合（单播）。

- void SetOrder(int newValue)
  - 设置绘制顺序（数值大者后绘制、显示在上层）。

- void SetVisible(bool newValue)
  - 设置节点可见性。

- void SetMouseEvents(bool newValue)
  - 设置节点是否接收鼠标事件。

- void SetAnchor(double x, double y)
  - 设置锚点（0=左/上缘，1=右/下缘；缩放与旋转围绕锚点）。

- void SetScale(double x, double y)
  - 设置缩放系数（1.0 为原始尺寸）。

- void SetAngle(int newValue)
  - 设置旋转角度（度，顺时针）。

- void SetOpacity(double newValue)
  - 设置不透明度；超出 [0.0, 1.0] 的值被钳制。

- void SetClip(bool newValue)
  - 设置是否把内容裁剪到节点边界内。

- void SetContent(string newValue, bool richText)
  - 设置文本内容；richText=true 时按富文本标记解析。

- void SetResource(string newValue)
  - 设置贴图资源 Id。

- void SetColor(ArpgColor newValue)
  - 设置前景/文字颜色。

- void SetBackgroundColor(ArpgColor newValue)
  - 设置背景色。

- void SetBorder(int width, ArpgColor color)
  - 设置边框；宽度为负时按 0 处理。

- void SetOutlineColor(ArpgColor newValue)
  - 设置描边颜色（仅 ArtText 节点生效）。

- void SetRadius(int newValue)
  - 设置圆角半径；负值按 0 处理。

- void SetTextStyle(int fontSize, int horizontalAlign, int verticalAlign, bool wrap, int lineSpacing)
  - 设置文本样式；fontSize 小于 1 按 1 处理，lineSpacing 负值按 0 处理。

- void SetAnimation(int frameRate, bool loop, bool play)
  - 设置动画参数；frameRate 小于 1 按 1 处理，loop 为是否循环，play 为是否自动播放。

- void AddAnimationFrame(string resource)
  - 追加一帧动画资源 Id（按添加顺序播放）。

- void SetNineSlice(ArpgNineSlice newValue)
  - 设置九宫格描述；传 null 取消九宫格渲染。

- void SetDataSource(ArpgDataSource newValue)
  - 设置节点私有数据源；设为 null 时恢复继承控件/窗口数据源。

- void Validate(ArpgDiagnostics diagnostics, string parentPath)
  - 校验定义合法性：Id 非空、kind 受支持、尺寸非负、缩放大于 0、样式与帧率合法；错误写入 diagnostics。


## ArpgNodeEventHandlers (class)

节点事件单播槽位集合：每个槽位只保存一个 handler，后注册者覆盖
前者。经 ArpgNodeDefinition.Events() 获取；业务代码调用 On* 注册
回调，框架在节点生命周期内触发 Raise*，不要手动调用 Raise*。

- ArpgNodeCreated created;

- ArpgNodeUpdated updated;

- ArpgNodePointerChanged pointerDown;

- ArpgNodePointerChanged pointerUp;

- ArpgNodePointerChanged pointerMove;

- ArpgNodeFocusChanged focusChanged;

- ArpgNodeCustomEvent customEvent;

- void OnCreated(ArpgNodeCreated handler)
  - 注册节点实例化完成回调（节点运行时创建后触发一次）。

- void OnUpdated(ArpgNodeUpdated handler)
  - 注册节点每帧更新回调（deltaMilliseconds 为帧间隔毫秒）。

- void OnPointerDown(ArpgNodePointerChanged handler)
  - 注册节点按下回调；返回 true 可消费事件并阻断后续分发。

- void OnPointerUp(ArpgNodePointerChanged handler)
  - 注册节点抬起回调；返回 true 可消费事件并阻断后续分发。

- void OnPointerMove(ArpgNodePointerChanged handler)
  - 注册节点移动回调；返回 true 可消费事件并阻断后续分发。

- void OnFocusChanged(ArpgNodeFocusChanged handler)
  - 注册节点焦点变化回调（focused 为是否获得焦点）。

- void OnCustomEvent(ArpgNodeCustomEvent handler)
  - 注册节点自定义事件回调（由 FireCustom 触发）。

- void RaiseCreated(string nodeId)
  - 触发实例化回调；未注册 handler 时为空操作。

- void RaiseUpdated(string nodeId, int deltaMilliseconds)
  - 触发每帧更新回调；未注册 handler 时为空操作。

- bool RaisePointerDown(string nodeId, int button, int modifiers, double x, double y)
  - 触发按下回调；未注册时返回 false（事件未消费）。

- bool RaisePointerUp(string nodeId, int button, int modifiers, double x, double y)
  - 触发抬起回调；未注册时返回 false（事件未消费）。

- bool RaisePointerMove(string nodeId, int button, int modifiers, double x, double y)
  - 触发移动回调；未注册时返回 false（事件未消费）。

- void RaiseFocusChanged(string nodeId, bool focused)
  - 触发焦点变化回调；未注册 handler 时为空操作。

- void RaiseCustom(string nodeId, string name, string payload)
  - 触发自定义事件回调；未注册 handler 时为空操作。


## ArpgNodeKind (class)

节点类型常量：返回节点 kind 字符串（ArpgNodeDefinition 的 kind
取值）。IsValid 判断任意字符串是否为受支持的节点类型。

- static string Text()
  - 文本节点类型。

- static string ArtText()
  - 艺术字节点类型（支持描边渲染）。

- static string Sprite()
  - 静态图片节点类型。

- static string Rectangle()
  - 矩形色块节点类型。

- static string Circle()
  - 圆形节点类型（按 Radius 渲染，缺省取宽高的一半）。

- static string Animation()
  - 帧动画节点类型（按 AddAnimationFrame 的帧序列播放）。

- static bool IsValid(string kind)
  - 判断 kind 是否为受支持的节点类型字符串。


## ArpgPixelFont (class)

自包含的 8x8 位图字形集，覆盖 ASCII 32..126，供所有 UI
后端共用，使文本以真实字形栅格化（无需外部字体资源，也无
原生渲染依赖，因此可安全地自动包含进软件/无头构建）。

图集以十六进制字符串存储（每字形 8 个行字节，字形索引 =
codepoint - 32），首次使用时一次性解码为扁平的 int[]。每个行
字节中，位 7 是最左像素列，位 0 是最右。

- static int[]rows;

- static bool ready;

- static int CellW()
  - 字形单元宽度（像素，恒 8）。

- static int CellH()
  - 字形单元高度（像素，恒 8）。

- static int Advance()
  - 每个字形的水平步进（源像素；字形为 8px，但
    DejaVu 等宽字形实际约占 6px，取 6 使文本紧凑且不重叠）。

- static string Data()
  - 字形图集的十六进制编码数据（每字节 2 个十六进制字符）。

- static int HexVal(int ch)
  - 单个十六进制字符的数值（0-9/a-f/A-F）；其余字符返回 0。

- static void Ensure()
  - 首次调用时把十六进制图集一次性解码为行字节数组。

- static int Row(int codepoint, int row)
  - 返回 codepoint 与行 (0..7) 对应的 8 位行掩码；位 7 是
    最左列。32..126 之外的码点渲染为空白。

- static int Measure(string text, int scale)
  - 字符串按给定整数缩放（>= 1）渲染后的像素宽度。


## ArpgPoint (class)

逻辑坐标下的二维点。

- double x;

- double y;

- ArpgPoint(double x, double y)
  - 构造点。

- double X()
  - X 坐标。

- double Y()
  - Y 坐标。

- void Set(double x, double y)
  - 同时重设两个坐标。


## ArpgPointerButton (class)

鼠标按键常量：Left=0，Right=1，Middle=2。指针事件回调的 button
参数取这些值。

- static int Left()
  - 鼠标左键。

- static int Right()
  - 鼠标右键。

- static int Middle()
  - 鼠标中键。


## ArpgPrefabDefinition (class)

Prefab 定义：可复用的节点组合模板。声明默认尺寸、锚点、样式与
变换，并携带节点列表与属性声明；控件通过 PrefabId 引用，实例化
时展开其中全部节点并继承控件事件。Validate 校验尺寸、缩放、
属性名与节点 Id 的唯一性。

- string id;

- int width;

- int height;

- double anchorX;

- double anchorY;

- ArpgColor backgroundColor;

- int borderWidth;

- ArpgColor borderColor;

- double scaleX;

- double scaleY;

- int order;

- int angle;

- double opacity;

- string backgroundResource;

- bool autoClip;

- bool mouseEvents;

- List<ArpgPrefabProperty> properties;

- List<ArpgNodeDefinition> nodes;

- ArpgControlEventHandlers events;

- ArpgPrefabDefinition(string id, int width, int height)
  - 构造 Prefab：id 为唯一标识，width/height 为默认像素尺寸。

- string Id()
  - 返回 Prefab 唯一 Id。

- int Width()
  - 返回默认宽度（像素）。

- int Height()
  - 返回默认高度（像素）。

- double AnchorX()
  - 返回水平锚点（0=左缘，1=右缘）。

- double AnchorY()
  - 返回垂直锚点（0=上缘，1=下缘）。

- ArpgColor BackgroundColor()
  - 返回背景色。

- int BorderWidth()
  - 返回边框宽度（像素）。

- ArpgColor BorderColor()
  - 返回边框颜色。

- double ScaleX()
  - 返回水平缩放系数。

- double ScaleY()
  - 返回垂直缩放系数。

- int Order()
  - 返回默认绘制顺序；数值大者在上层。

- int Angle()
  - 返回默认旋转角度（度，顺时针）。

- double Opacity()
  - 返回默认不透明度（0.0~1.0）。

- string BackgroundResource()
  - 返回背景贴图资源 Id；空串表示无背景贴图。

- bool AutoClip()
  - 返回是否按内容自动裁剪子节点。

- bool MouseEvents()
  - 返回展开后的节点是否默认接收鼠标事件。

- int NodeCount()
  - 返回节点定义数量。

- ArpgNodeDefinition NodeAt(int index)
  - 返回第 index 个节点定义；index 须在 [0, NodeCount) 内。

- ArpgControlEventHandlers Events()
  - 返回 Prefab 事件槽位集合（单播；实例化时叠加到宿主控件事件之前触发）。

- void SetAnchor(double x, double y)
  - 设置锚点（0=左/上缘，1=右/下缘）。

- void SetBackgroundColor(ArpgColor newValue)
  - 设置背景色。

- void SetBorder(int width, ArpgColor color)
  - 设置边框（此定义层不钳制负值，Validate 不单独检查边框）。

- void SetScale(double x, double y)
  - 设置缩放系数（1.0 为原始尺寸）。

- void SetOrder(int newValue)
  - 设置默认绘制顺序。

- void SetAngle(int newValue)
  - 设置默认旋转角度（度，顺时针）。

- void SetOpacity(double newValue)
  - 设置默认不透明度；超出 [0.0, 1.0] 的值被钳制。

- void SetBackgroundResource(string newValue)
  - 设置背景贴图资源 Id。

- void SetAutoClip(bool newValue)
  - 设置是否按内容自动裁剪子节点。

- void SetMouseEvents(bool newValue)
  - 设置展开节点是否默认接收鼠标事件。

- void AddProperty(string name, string defaultValue)
  - 声明一个可配置属性（name 须非空且唯一，否则 Validate 报错）。

- int PropertyCount()
  - 返回已声明的属性数量。

- ArpgPrefabProperty PropertyAt(int index)
  - 返回第 index 个属性声明；index 须在 [0, PropertyCount) 内。

- void AddNode(ArpgNodeDefinition node)
  - 追加一个节点定义（实例化时按 Order 排序渲染）。

- void Validate(ArpgDiagnostics diagnostics)
  - 校验 Prefab：Id 非空、宽高为正、缩放大于 0、属性名非空且不重复、节点 Id 不重复并递归校验节点；错误写入 diagnostics。


## ArpgPrefabProperty (class)

Prefab 可配置属性声明：属性名与其默认值（均为字符串）。控件
引用 Prefab 时可按名覆盖属性值；Validate 要求属性名非空且唯一。

- string name;

- string defaultValue;

- ArpgPrefabProperty(string name, string defaultValue)

- string Name()
  - 返回属性名（Prefab 内唯一）。

- string DefaultValue()
  - 返回属性默认值（未被覆盖时使用）。


## ArpgProject (class)

内存组件数据库。与文件格式无关，因此工具
可从 JSON、Zan 源码、编辑器或其他管线生成它。

- ArpgRegistry registry;

- List<ArpgMapDefinition> maps;

- List<ArpgActorDefinition> actors;

- List<ArpgItemDefinition> items;

- List<ArpgSkillDefinition> skills;

- List<ArpgBuffDefinition> buffs;

- List<ArpgWindowDefinition> windows;

- List<ArpgGrowthDefinition> growth;

- List<ArpgPrefabDefinition> prefabs;

- List<SqliteComponentConfig> databases;

- ArpgProject()

- ArpgRegistry Registry()
  - 组件注册表（资源定义），随 LoadProject 替换。

- void AddMap(ArpgMapDefinition newValue)
  - 追加一个地图定义（不做去重，重复 id 由 Validate 报告）。

- void AddActor(ArpgActorDefinition newValue)
  - 追加一个角色定义。

- void AddItem(ArpgItemDefinition newValue)
  - 追加一个物品定义。

- void AddSkill(ArpgSkillDefinition newValue)
  - 追加一个技能定义。

- void AddBuff(ArpgBuffDefinition newValue)
  - 追加一个增益定义。

- void AddWindow(ArpgWindowDefinition newValue)
  - 追加一个窗口定义。

- void AddGrowth(ArpgGrowthDefinition newValue)
  - 追加一个成长曲线定义。

- void AddPrefab(ArpgPrefabDefinition newValue)
  - 追加一个预制体定义。

- void AddDatabase(SqliteComponentConfig newValue)
  - 追加一个 SQLite 组件数据库配置。

- int MapCount()
  - 地图定义数量。

- int ActorCount()
  - 角色定义数量。

- int ItemCount()
  - 物品定义数量。

- int SkillCount()
  - 技能定义数量。

- int BuffCount()
  - 增益定义数量。

- int WindowCount()
  - 窗口定义数量。

- int GrowthCount()
  - 成长曲线定义数量。

- int PrefabCount()
  - 预制体定义数量。

- ArpgMapDefinition MapAt(int index)
  - 第 index 个地图定义（不查越界）。

- ArpgActorDefinition ActorAt(int index)
  - 第 index 个角色定义（不查越界）。

- ArpgItemDefinition ItemAt(int index)
  - 第 index 个物品定义（不查越界）。

- ArpgSkillDefinition SkillAt(int index)
  - 第 index 个技能定义（不查越界）。

- ArpgBuffDefinition BuffAt(int index)
  - 第 index 个增益定义（不查越界）。

- ArpgWindowDefinition WindowAt(int index)
  - 第 index 个窗口定义（不查越界）。

- ArpgGrowthDefinition GrowthAt(int index)
  - 第 index 个成长曲线定义（不查越界）。

- ArpgPrefabDefinition PrefabAt(int index)
  - 第 index 个预制体定义（不查越界）。

- int DatabaseCount()
  - 数据库配置数量。

- SqliteComponentConfig DatabaseAt(int index)
  - 第 index 个数据库配置（不查越界）。

- ArpgMapDefinition FindMap(string id)
  - 按 id 查找地图定义；找不到返回 null。

- ArpgActorDefinition FindActor(string id)
  - 按 id 查找角色定义；找不到返回 null。

- ArpgItemDefinition FindItem(string id)
  - 按 id 查找物品定义；找不到返回 null。

- ArpgSkillDefinition FindSkill(string id)
  - 按 id 查找技能定义；找不到返回 null。

- ArpgBuffDefinition FindBuff(string id)
  - 按 id 查找增益定义；找不到返回 null。

- ArpgWindowDefinition FindWindow(string id)
  - 按 id 查找窗口定义；找不到返回 null。

- ArpgGrowthDefinition FindGrowth(string id)
  - 按 id 查找成长曲线定义；找不到返回 null。

- ArpgPrefabDefinition FindPrefab(string id)
  - 按 id 查找预制体定义；找不到返回 null。

- SqliteComponentConfig FindDatabase(string name)
  - 按名称查找数据库配置；找不到返回 null。

- ArpgMapDefinition DefaultMap()
  - 返回标记为默认地图的定义；没有或多个默认时返回第一个
    命中项，全无默认返回 null。

- ArpgActorDefinition DefaultPlayer()
  - 返回标记为默认玩家的角色定义；无默认玩家返回 null。

- void ValidateResource(ArpgDiagnostics diagnostics, string resourceId, string path)
  - 校验资源 id 引用：非空且注册表中不存在时记录
    L2PRJ017 错误。空串视为未引用，跳过。

- ArpgDiagnostics Validate()
  - 全量校验项目：汇总注册表校验与各类定义的逐项校验、
    传送门/出生点与默认地图/默认玩家规则。返回诊断集合；
    HasErrors() 为 true 表示项目不可启动。

- void ValidateMaps(ArpgDiagnostics diagnostics)
  - 地图定义：逐图校验、资源、重复 id。

- void ValidateActors(ArpgDiagnostics diagnostics)
  - 角色定义：校验、重复 id、成长/技能/增益引用。

- void ValidateItems(ArpgDiagnostics diagnostics)
  - 物品定义：校验、图标资源、重复 id。

- void ValidateSkills(ArpgDiagnostics diagnostics)
  - 技能定义：校验、图标、增益效果引用、重复 id。

- void ValidateBuffs(ArpgDiagnostics diagnostics)
  - 增益定义：校验、图标、重复 id。

- void ValidateWindows(ArpgDiagnostics diagnostics)
  - 窗口定义：校验、背景、重复 id。

- void ValidateGrowth(ArpgDiagnostics diagnostics)
  - 成长定义：校验、重复 id。

- void ValidatePrefabs(ArpgDiagnostics diagnostics)
  - 预制体定义：校验、背景、重复 id。

- void ValidateMapPortals(ArpgDiagnostics diagnostics)
  - 地图传送门/出生点及单一默认地图规则。

- void ValidateDefaultPlayer(ArpgDiagnostics diagnostics)
  - 单一默认玩家规则。


## ArpgProperty (class)

ArpgValue 对象形态下的单个键值属性。

- string key;
  - 属性名。

- ArpgValue propertyValue;
  - 属性值。

- ArpgProperty(string key, ArpgValue newValue)
  - 构造一个键值属性对。


## ArpgRandom (class)

适合回放与测试的小型确定性随机数生成器。

- int state;

- ArpgRandom(int seed)
  - 构造生成器；种子与 0x7FFFFFFF 按位与，0 时强制为 1。

- int Next()
  - 返回 [0, 2147483647] 内的下一个伪随机整数（LCG）。

- int NextPercent()
  - 返回 [1, 100] 内的百分比掷点（命中/暴击判定用）。


## ArpgRect (class)

逻辑坐标下的轴对齐矩形（x,y 为左上角）。

- double x;

- double y;

- double width;

- double height;

- ArpgRect(double x, double y, double width, double height)
  - 构造矩形。

- double X()
  - 左上角 X。

- double Y()
  - 左上角 Y。

- double Width()
  - 宽度。

- double Height()
  - 高度。


## ArpgRegistry (class)

项目资源/组件/扩展脚本的注册表：启动前集中登记，Validate 检查
必填与重复。Engine 加载时按此清单读取文件。

- List<ArpgResource> resources;

- List<ArpgComponentFile> components;

- List<string> scriptFiles;

- ArpgRegistry()

- void AddResource(string id, string file)
  - 注册资源：id 为引用键，file 为文件路径。

- void AddComponent(string path)
  - 以简化方式注册组件（kind/id 均记为 "unknown"，路径即身份）。

- void AddComponentFile(string kind, string id, string path)
  - 注册组件文件：kind 为类别、id 为类别内标识、path 为路径。

- void AddScript(string path)
  - 注册扩展脚本路径（按登记顺序加载）。

- int ResourceCount()
  - 已注册资源数量。

- int ComponentCount()
  - 已注册组件数量。

- int ScriptCount()
  - 已注册扩展脚本数量。

- ArpgResource ResourceAt(int index)
  - 按下标读取资源条目；应配合 ResourceCount 使用。

- string ComponentAt(int index)
  - 按下标读取组件文件路径；完整条目用 ComponentFileAt。

- ArpgComponentFile ComponentFileAt(int index)
  - 按下标读取组件完整条目；应配合 ComponentCount 使用。

- string ScriptAt(int index)
  - 按下标读取扩展脚本路径；应配合 ScriptCount 使用。

- bool HasResource(string id)
  - 是否已注册指定 Id 的资源。

- bool HasComponent(string kind, string id)
  - 是否已注册指定类别+Id 的组件。

- ArpgDiagnostics Validate()
  - 校验注册表：资源（L2RES001~003）、组件（L2CMP001~004）、
    扩展脚本（L2SCR001~002）的必填与重复检查。返回新诊断对象，
    应在启动前调用并检查。


## ArpgResource (class)

资源注册条目：id 为引用键，file 为文件路径。

- string id;

- string file;

- ArpgResource(string id, string file)

- string Id()
  - 资源 Id（注册表内唯一）。

- string File()
  - 资源文件路径。


## ArpgRichText (class)

富文本解析静态入口：先用数据源对 content 做模板求值（展开
&path& 与 {if} 分支），再按默认样式解析为 run 文档。
content 为空时返回空文档。

- static ArpgRichTextDocument Parse(string content, ArpgDataSource source)
  - 模板求值 + 富文本解析；source 可为 null（占位符解析为空）。

- static int PackColor(ArpgColor color)
  - ArpgColor → Gui 打包色（0xAARRGGBB）。

- static ArpgColor UnpackColor(int packed)
  - Gui 打包色（0xAARRGGBB）→ ArpgColor。


## ArpgRichTextDocument (class)

富文本解析结果：原始输入与按序排列的 run 列表。绘制/布局层
遍历 runs 并按各 run 的 Kind 分派处理。
薄适配：包住 Gui.RichTextDocument；RunAt 的 Arpg 包装按需建
一次并缓存（文档随后 Add 时缓存惰性续建）。

- Gui.RichTextDocument inner;

- List<ArpgRichTextRun> cache;

- ArpgRichTextDocument(string sourceText)

- ArpgRichTextDocument(Gui.RichTextDocument inner)

- void SyncCache()
  - 把包装缓存补到与内部 run 数一致。

- string SourceText()
  - 解析前的原始输入。

- int RunCount()
  - run 总数。

- ArpgRichTextRun RunAt(int index)
  - 第 index 个 run（不查越界）。

- void Add(ArpgRichTextRun run)
  - 追加一个 run 到末尾。


## ArpgRichTextLayout (class)

富文本排版结果：条目列表与整体尺寸/行数。Width 为内容实际
最宽行宽（非可用宽度），Height 为总高（含末行行高）。

- List<ArpgRichTextLayoutItem> items;

- int width;

- int height;

- int lineCount;

- ArpgRichTextLayout()

- int ItemCount()
  - 条目总数。

- ArpgRichTextLayoutItem ItemAt(int index)
  - 第 index 个条目（不查越界）。

- int Width()
  - 内容实际宽度（最宽行，像素）。

- int Height()
  - 内容总高度（像素）。

- int LineCount()
  - 行数。

- void Add(ArpgRichTextLayoutItem item)
  - 追加一个条目（布局器内部使用）。

- void SetSize(int width, int height, int lineCount)
  - 一次性设置整体尺寸与行数（布局器内部使用）。


## ArpgRichTextLayoutItem (class)

富文本排版结果中的单个条目：源 run 索引、所在行号、条目文本
（图片/占位类为空串）与排版矩形（含偏移后的最终像素坐标）。
非文本条目的矩形宽高来自 run 设置（未指定时按字号兜底）。

- int runIndex;

- int line;

- string text;

- int x;

- int y;

- int width;

- int height;

- ArpgRichTextLayoutItem(int runIndex, int line, string text, int x, int y, int width, int height)

- int RunIndex()
  - 对应源文档中的 run 索引。

- int Line()
  - 所在行号（从 0 起）。

- string Text()
  - 条目文本（图片/动画/占位条目为空串）。

- int X()
  - 条目左上角 X（像素；对齐偏移已计入）。

- int Y()
  - 条目左上角 Y（像素，含行高与行距累积）。

- int Width()
  - 条目宽度（像素）。

- int Height()
  - 条目高度（像素）。

- void OffsetX(int offset)
  - 把条目 X 平移 offset 像素（布局器内部做对齐用）。


## ArpgRichTextLayouter (class)

富文本排版器：把 ArpgRichTextDocument 排成带坐标的条目流。
处理换行（LineBreak 与文本内 \n）、按可用宽度自动换行
（wrap=true）、WrapWidth run 调整换行宽度、图片/动画/占位的
兜底尺寸与偏移，以及 Center/End 水平对齐（偏移非负）。

- static ArpgRichTextLayout Layout(ArpgRichTextDocument document, int availableWidth, int fontSize, int lineSpacing, bool wrap, int horizontalAlign)
  - 排版整个文档。availableWidth 为可用宽度（像素，<=0 表示
    不限宽不换行）；fontSize 为基准字号（像素）；lineSpacing 为
    行间额外间距（像素）；horizontalAlign 用 ArpgTextAlign 常量
    （0 左 1 中 2 右，仅 availableWidth>0 时生效）。返回的布局
    尺寸为内容实际宽高，行数至少为 1。


## ArpgRichTextLink (class)

富文本链接（#@标记@内容@ 语法中标记部分的类型化表示）。
标记格式为 "action|参数列表"：竖线前是链接动作文本，竖线后
可带逗号分隔的 1~4 个参数：样式编号、常态色、悬停色、按下色。
颜色参数支持 0xRRGGBBAA；未提供的颜色为全透明。
薄适配：解析与存取都委托 Gui.RichTextLink，颜色在边界换算成
ArpgColor。

- Gui.RichTextLink inner;

- ArpgRichTextLink(string marker)

- ArpgRichTextLink(Gui.RichTextLink inner)

- Gui.RichTextLink Inner()
  - 内部 Gui 链接对象（适配层内部用）。

- string Raw()
  - 链接动作原文（标记中竖线前的部分）。

- int Count()
  - 动作参数个数（按逗号分隔；无参数为 0）。

- string At(int index)
  - 第 index 个动作参数（两侧去空白）；越界返回空串。

- int Style()
  - 样式编号（标记第 1 个参数，未提供为 0）。

- ArpgColor NormalColor()
  - 常态颜色（未提供时为全透明）。

- ArpgColor HoverColor()
  - 悬停颜色（未提供时为全透明）。

- ArpgColor PressedColor()
  - 按下颜色（未提供时为全透明）。


## ArpgRichTextParser (class)

富文本解析器：把标签化文本切分为类型化 run 流。支持颜色
快捷标记（#W #R #Y #B #G #H #L）、#c()/#bg()/#f() 样式标签、
#p()/#a()/#z()/#item() 资源标签、#br(宽度)/#md/#rt/#lf 排版标签
与 #@标记@内容@ 超链接。无法识别的 # 按普通文本保留，
## 转义为字面 '#'。
薄适配：解析完全委托 Gui.RichTextParser。

- Gui.RichTextParser inner;

- ArpgRichTextParser(string input, ArpgRichTextStyle style, ArpgRichTextLink link)

- static int FindIn(string text, string token, int start)
  - 在 text 中从 start 起查找 token 首次出现的位置；未找到返回 -1。

- static int ArgCount(string text)
  - 按逗号分隔的参数个数（空串为 0，"a,,b" 计 3）。

- static string ArgAt(string text, int index)
  - 第 index 个逗号分隔参数（两侧去空白）；越界返回空串。

- static int HexDigit(string digit)
  - 单个十六进制字符的数值（0-9/a-f/A-F）；其他字符返回 0。

- static int ParseInt(string text)
  - 解析整数：支持十进制与 0x 前缀十六进制（可带负号），
    两侧去空白；无有效数字时返回 0（不置错）。

- static ArpgColor ParseColor(string text)
  - 解析 0xRRGGBBAA 颜色；空串、"0" 或格式不符返回全透明。

- static ArpgColor ParseColorArgs(string args)
  - 解析颜色参数串：1 个参数按 0xRRGGBBAA，4 个参数按
    r,g,b,a 十进制；其他情况返回全透明。

- ArpgRichTextDocument Parse()
  - 解析全部输入并返回文档：\n 产生 LineBreak，'#' 触发标签
    解析（未识别时按普通文本保留），其余字符累积为文本 run。


## ArpgRichTextRun (class)

富文本解析产物的单个片段（run）：一段文本、一张图片、一个
动画、一个占位或一次换行。携带创建时刻的样式快照与所在链接
（非链接 run 的 Link 为 null），布局/绘制层按 Kind 分派处理。
薄适配：包住 Gui.RichTextRun，样式/链接包装按需建一次。

- Gui.RichTextRun inner;

- ArpgRichTextStyle styleWrap;

- ArpgRichTextLink linkWrap;

- ArpgRichTextRun(int kind, ArpgRichTextStyle style, ArpgRichTextLink link)

- ArpgRichTextRun(Gui.RichTextRun inner)

- Gui.RichTextRun Inner()
  - 内部 Gui run 对象（适配层内部用）。

- static ArpgRichTextRun CreateText(string text)
  - 创建一个默认样式、无链接的纯文本 run。

- int Kind()
  - run 类型（ArpgRichTextRunKind 常量）。

- string Text()
  - 文本内容（仅 Text run 有意义，其他为空串）。

- string Resource()
  - 资源 Id（Image/Animation/Item run）。

- string Action()
  - 动画动作名（仅 Animation run 有意义）。

- int Quantity()
  - 物品数量（仅 Item run 有意义）。

- int OffsetX()
  - 相对排版位置的 X 偏移（像素）。

- int OffsetY()
  - 相对排版位置的 Y 偏移（像素）。

- int Width()
  - 宽度（像素；图片/动画未指定时布局按字号兜底）。

- int Height()
  - 高度（像素；未指定时布局按字号兜底）。

- double Scale()
  - 动画缩放（默认 1.0）。

- ArpgRichTextStyle Style()
  - 创建时刻的样式快照（独立副本，修改不影响其他 run）。

- ArpgRichTextLink Link()
  - 所在链接；非链接 run 返回 null。

- bool IsLink()
  - 该 run 是否位于超链接内。

- void SetText(string newValue)
  - 设置文本内容。

- void SetResource(string newValue)
  - 设置资源 Id。

- void SetAction(string newValue)
  - 设置动画动作名。

- void SetQuantity(int newValue)
  - 设置物品数量。

- void SetOffset(int x, int y)
  - 同时设置 X/Y 偏移（像素）。

- void SetSize(int width, int height)
  - 同时设置宽高（像素）。

- void SetScale(double newValue)
  - 设置动画缩放。


## ArpgRichTextRunKind (class)

富文本 run 类型常量：Text=0 纯文本、Image=1 图片（#p）、
Animation=2 动画（#a）、Spacer=3 空白占位（#z）、
Item=4 物品片段（#item）、LineBreak=5 换行（源文本 \n）、
WrapWidth=6 换行宽度段（#br(宽度) 或 #md/#rt 携带宽度时）。

实现在 Gui.RichTextRunKind——富文本的解析与数据模型已经
统一收敛到 stdlib/Gui（组件都到 Gui 下），本命名空间只保留
Arpg 时代的类型名作薄适配，外部工程无需迁移。

- static int Text()
  - 纯文本 run 类型常量（0）。

- static int Image()
  - 图片 run 类型常量（1），由 #p(资源,x,y,宽,高) 产生。

- static int Animation()
  - 动画 run 类型常量（2），由 #a(资源,动作,缩放,宽,高,x,y) 产生。

- static int Spacer()
  - 空白占位 run 类型常量（3），由 #z(宽,高) 产生。

- static int Item()
  - 物品片段 run 类型常量（4），由 #item(资源,数量) 产生。

- static int LineBreak()
  - 换行 run 类型常量（5），源文本中的每个 \n 产生一个。

- static int WrapWidth()
  - 换行宽度 run 类型常量（6）；携带宽度时改变后续换行宽度，宽度 0 仅切换对齐。


## ArpgRichTextStyle (class)

富文本样式快照：前景/背景色、字体名与水平对齐。
解析过程中随 #c/#bg/#f/#md/#rt 等标签变化，并拷贝进
后续创建的 run。对齐取值：0=左，1=中（#md），2=右（#rt）。
薄适配：存取委托 Gui.RichTextStyle，颜色在边界换算。

- Gui.RichTextStyle inner;

- ArpgRichTextStyle(ArpgColor color, ArpgColor background, string font, int alignment)

- ArpgRichTextStyle(Gui.RichTextStyle inner)

- Gui.RichTextStyle Inner()
  - 内部 Gui 样式对象（适配层内部用）。

- static ArpgRichTextStyle Default()
  - 默认样式：白字、透明背景、空字体名、左对齐。

- ArpgRichTextStyle Clone()
  - 返回独立副本（修改副本不影响原样式）。

- ArpgColor Color()
  - 当前前景色。

- ArpgColor Background()
  - 当前背景色。

- string Font()
  - 当前字体名（空串表示默认字体）。

- int Alignment()
  - 当前水平对齐：0=左，1=中（#md），2=右（#rt）。

- void SetColor(ArpgColor newValue)
  - 设置前景色（影响后续创建的 run）。

- void SetBackground(ArpgColor newValue)
  - 设置背景色。

- void SetFont(string newValue)
  - 设置字体名。

- void SetAlignment(int newValue)
  - 设置水平对齐（0=左 1=中 2=右）。


## ArpgScheduler (class)

与文档所述 "join event" 行为一致的具名一次性/循环事件
。载荷特意序列化，以保持调度器的类型安全。

- List<ArpgTimerTask> tasks;

- ArpgScheduler()
  - 构造空调度器。

- void Add(string name, string payload, int delayMilliseconds, bool autoRemove, ArpgTimedEvent handler)
  - 注册具名定时事件：delayMilliseconds 后触发 handler；
    autoRemove=true 触发一次后移除，false 则按间隔循环。
    同名任务先移除再注册（天然去重）。间隔最小为 1ms。

- bool Remove(string name)
  - 按名移除任务（删掉全部同名项），删到了返回 true。

- bool Contains(string name)
  - 是否存在同名任务。

- int Count()
  - 当前任务数。

- int IndexOf(string name)
  - 第一个同名任务的下标；没有返回 -1。

- int Tick(int deltaMilliseconds)
  - 推进全部任务 elapsed 并触发到期的：回调参数依次为任务名、
    载荷、本次帧间隔毫秒、累计毫秒。回调可安全移除/替换自身
    （框架先快照任务列表再逐个触发）。返回本次触发的任务数。


## ArpgScreenMode (class)

屏幕适配模式：0=Fixed 固定窗口 1=Scale 等比缩放（窗口可拉伸，
逻辑分辨率不变）2=ResizeWorld 拉伸并扩展可视世界
3=TransparentBorderless 无边框透明。IsValid 判断 0–3。

- static int Fixed()
  - 固定尺寸窗口模式（0）。

- static int Scale()
  - 等比缩放模式（1），窗口可调整、渲染按逻辑分辨率拉伸。

- static int ResizeWorld()
  - 扩展世界模式（2），可视范围随窗口变化。

- static int TransparentBorderless()
  - 无边框透明窗口模式（3）。

- static bool IsValid(int mode)
  - mode 是否为合法的屏幕模式（0–3）。


## ArpgServerClient (class)

已连接的服务器端客户端：与客户端运行时共用同一套帧封装
ArpgMessage 协议，消息上限取自 ServerConfig。收发均为 async；
断开时自动触发 ArpgServerEvents 的 ClientDisconnected。

- ServerConfig config;

- int id;

- nint sock;

- bool connected;

- byte[]recvBuf;

- ArpgServerEvents events;

- static ArpgServerClient FromSocket(ServerConfig config, int id, nint sock, ArpgServerEvents events)
  - 包装服务器 accept 得到的套接字并分配客户端 id。

- int Id()
  - 服务器分配的连接 id（从 1 起自增）。

- nint Handle()
  - 底层套接字句柄。

- bool IsConnected()
  - 是否仍处于连接状态。

- async int SendMessageAsync(ArpgMessage message)
  - 异步发送一条帧封装消息，返回 AsyncSocket.Send 的错误码
    （0 为成功）。

- async ArpgMessage ReceiveMessageAsync()
  - 异步接收一条消息：断开时置 connected=false、触发
    ClientDisconnected 并返回 null；成功时触发 MessageReceived
    后返回消息（帧不完整返回 null 且不触发事件）。

- void Close()
  - 关闭连接并触发 ClientDisconnected（已关闭时安全调用）。


## ArpgServerEvents (class)

类型化的服务器生命周期与传输事件。宿主可拒绝启动或
关闭，而已接受的连接与帧消息可被观察。

- ArpgServerStarting starting;

- ArpgServerStarted started;

- ArpgServerStopping stopping;

- ArpgServerStopped stopped;

- ArpgServerClientChanged clientConnected;

- ArpgServerClientChanged clientDisconnected;

- ArpgServerMessageReceived messageReceived;

- void OnStarting(ArpgServerStarting handler)
  - 注册启动前回调（单播，覆盖此前注册；传 null 即清除）。

- void OnStarted(ArpgServerStarted handler)
  - 注册启动成功回调（单播，覆盖此前注册；传 null 即清除）。

- void OnStopping(ArpgServerStopping handler)
  - 注册关闭前回调（单播，覆盖此前注册；传 null 即清除）。

- void OnStopped(ArpgServerStopped handler)
  - 注册关闭完成回调（单播，覆盖此前注册；传 null 即清除）。

- void OnClientConnected(ArpgServerClientChanged handler)
  - 注册客户端接入回调（单播，覆盖此前注册；传 null 即清除）。

- void OnClientDisconnected(ArpgServerClientChanged handler)
  - 注册客户端断开回调（单播，覆盖此前注册；传 null 即清除）。

- void OnMessageReceived(ArpgServerMessageReceived handler)
  - 注册消息帧到达回调（单播，覆盖此前注册；传 null 即清除）。

- bool RaiseStarting(string bindHost)
  - 触发启动前回调；无处理器或处理器返回 true 表示允许启动。

- void RaiseStarted(ServerConfig config, string bindHost)
  - 触发启动成功回调；无处理器时为无操作。

- bool RaiseStopping()
  - 触发关闭前回调；无处理器或处理器返回 true 表示允许关闭。

- void RaiseStopped(ServerConfig config)
  - 触发关闭完成回调；无处理器时为无操作。

- void RaiseClientConnected(ArpgServerClient client)
  - 触发客户端接入回调；无处理器时为无操作。

- void RaiseClientDisconnected(ArpgServerClient client)
  - 触发客户端断开回调；无处理器时为无操作。

- void RaiseMessageReceived(ArpgServerClient client, ArpgMessage message)
  - 触发消息帧到达回调；无处理器时为无操作。


## ArpgSkillCastResult (class)

一次技能施放的结算结果快照。CastSkill 返回；hit/critical/damage
仅在成功结算后由引擎填充，失败施放时 damage 为 0.0。

- int status;

- ArpgActor caster;

- ArpgActor target;

- ArpgSkillDefinition skill;

- bool hit;

- bool critical;

- double damage;

- ArpgSkillCastResult(int status, ArpgActor caster, ArpgActor target, ArpgSkillDefinition skill)

- int Status()
  - 施放状态码（ArpgSkillCastStatus 常量之一）。

- bool Success()
  - 仅当 Status 为 Success 时为 true。

- ArpgActor Caster()
  - 施法者角色引用。

- ArpgActor Target()
  - 目标角色引用，无目标模式时可能为 null。

- ArpgSkillDefinition Skill()
  - 本次施放的技能定义。

- bool Hit()
  - 是否命中（未命中时 damage 仍可能为 0）。

- bool Critical()
  - 是否暴击。

- double Damage()
  - 实际结算伤害（0.0 表示未命中或施放失败）。

- void SetResolved(bool hit, bool critical, double damage)
  - 结算结果回填：hit/critical/damage 一次性写入，供内部结算使用。


## ArpgSkillCastStatus (class)

技能施放状态码：ArpgSkillCastResult.Status 的取值，0 为成功，
其余为各类失败原因。

- static int Success()
  - 施放成功（常量 0）。

- static int UnknownSkill()
  - 技能 Id 不存在（常量 1）。

- static int NotLearned()
  - 施法者未学会该技能（常量 2）。

- static int CasterDead()
  - 施法者已死亡（常量 3）。

- static int Cooldown()
  - 技能处于冷却中（常量 4）。

- static int InsufficientResources()
  - HP/MP/怒气消耗不足（常量 5）。

- static int OutOfRange()
  - 目标超出射程（切比雪夫距离判定，常量 6）。

- static int InvalidTarget()
  - 目标无效（如 null 或阵营不符，常量 7）。


## ArpgSkillDefinition (class)

技能定义：伤害 = attack × DamageFactor − defense（最小 1），
射程用切比雪夫距离判定（minimum~attackRange 区间）。消耗/冷却/
延迟结算等均通过 Set* 配置，Validate 在加载后校验。

- string id;

- string displayName;

- string iconResource;

- string tooltip;

- int level;

- double damageFactor;

- string targetFaction;

- int attackRange;

- int minimumRange;

- string action;

- int hpCost;

- int mpCost;

- int rageCost;

- int threat;

- int cooldownMilliseconds;

- int targetMode;

- double priority;

- int damageDelayMilliseconds;

- bool autoEndPrevious;

- bool alwaysHit;

- bool alwaysCritical;

- int startingDirectionMode;

- string damageFormula;

- List<string> buffEffects;

- ArpgSkillDefinition(string id, string displayName)

- string Id()
  - 技能唯一 Id（配置键，创建后不变）。

- string DisplayName()
  - 显示名（必填）。

- string IconResource()
  - 图标资源路径，默认空串。

- string Tooltip()
  - 悬浮提示文本，默认空串。

- int Level()
  - 技能等级，默认 1。

- double DamageFactor()
  - 伤害倍率，默认 1.0（伤害 = attack × 倍率 − defense）。

- string TargetFaction()
  - 可作用的目标阵营（ArpgFaction 常量，默认 Hostile）。

- int AttackRange()
  - 最大射程（切比雪夫距离，格），默认 1。

- int MinimumRange()
  - 最小射程（格），默认 0 表示无近身下限；目标必须落在
    minimum~attackRange 闭区间内。

- string Action()
  - 施法动作名（动画/表现用），默认空串。

- int HpCost()
  - 施放消耗的 HP 点数，默认 0。

- int MpCost()
  - 施放消耗的 MP 点数，默认 0。

- int RageCost()
  - 施放消耗的怒气点数，默认 0。

- int Threat()
  - 施放产生的仇恨值，默认 0。

- int CooldownMilliseconds()
  - 冷却时长（毫秒），默认 0 表示无冷却。

- int TargetMode()
  - 目标模式（ArpgSkillTarget 常量，默认 Actor）。

- double Priority()
  - AI 自动施法优先级，默认 -1.0（越大越优先）。

- int DamageDelayMilliseconds()
  - 施法到伤害结算的延迟（毫秒），默认 0 立即结算。

- bool AutoEndPrevious()
  - true 时新施放自动结束上一个同类施放（引导/换弹类技能用）。

- bool AlwaysHit()
  - true 时必定命中（跳过命中判定）。

- bool AlwaysCritical()
  - true 时必定暴击。

- int StartingDirectionMode()
  - 起手朝向模式（0 默认朝目标方向）。

- string DamageFormula()
  - 自定义伤害公式表达式，空串表示用默认公式。

- int BuffEffectCount()
  - 附带增益数量。

- string BuffEffectAt(int index)
  - 按下标读取附带增益 Id；index 越界行为未定义，应配合 Count 使用。

- void SetIconResource(string newValue)
  - 设置图标资源路径。

- void SetTooltip(string newValue)
  - 设置悬浮提示文本。

- void SetLevel(int newValue)
  - 设置技能等级。

- void SetDamageFactor(double newValue)
  - 设置伤害倍率（伤害 = attack × 倍率 − defense，最小 1）。

- void SetTargetFaction(string newValue)
  - 设置可作用目标阵营（ArpgFaction 常量）。

- void SetRange(int minimum, int maximum)
  - 一次性设置射程区间：minimum 为最小、maximum 为最大（切比雪夫
    距离，格）；要求 maximum ≥ minimum ≥ 0。

- void SetAction(string newValue)
  - 设置施法动作名。

- void SetCosts(int hp, int mp, int rage)
  - 一次性设置三种消耗：HP/MP/怒气点数。

- void SetThreat(int newValue)
  - 设置施放产生的仇恨值。

- void SetCooldownMilliseconds(int newValue)
  - 设置冷却时长（毫秒）。

- void SetTargetMode(int newValue)
  - 设置目标模式（ArpgSkillTarget 常量 0~3）。

- void SetPriority(double newValue)
  - 设置 AI 自动施法优先级（越大越优先）。

- void SetDamageDelayMilliseconds(int newValue)
  - 设置施法到伤害结算的延迟（毫秒）。

- void SetAutoEndPrevious(bool newValue)
  - 设置是否自动结束上一个同类施放。

- void SetAlwaysHit(bool newValue)
  - 设置是否必定命中。

- void SetAlwaysCritical(bool newValue)
  - 设置是否必定暴击。

- void SetStartingDirectionMode(int newValue)
  - 设置起手朝向模式。

- void SetDamageFormula(string newValue)
  - 设置自定义伤害公式表达式（空串恢复默认公式）。

- void AddBuffEffect(string buffId)
  - 追加一个施放时施加的增益 Id。

- void Validate(ArpgDiagnostics diagnostics)
  - 校验技能配置合法性，问题以错误码 L2SKL001~L2SKL006 写入
    diagnostics（必填缺失、level 非正、射程区间非法、冷却为负、
    目标模式越界）。应在配置加载完成后调用一次。


## ArpgSkillTarget (class)

技能目标模式常量：ArpgSkillDefinition.SetTargetMode 的合法取值
（0~3），决定 CastSkill 的目标解析方式。

- static int Actor()
  - 以施法者自身为目标（常量 0，默认）。

- static int MouseCell()
  - 以鼠标指向的格子为目标（常量 1）。

- static int ActorThenMouseCell()
  - 先自身后鼠标格子（常量 2）。

- static int Player()
  - 以玩家角色为目标（常量 3）。


## ArpgTcpClientRuntime (class)

ArpgTcpClient 组件的实时 TCP 客户端运行时：把 TcpClientConfig
绑定到非阻塞套接字，收发长度帧封装的 ArpgMessage。所有
Connect/Send/Receive 均为 async；ReceiveMessageAsync 返回 null
表示连接已断开（IsConnected 随之变 false）。

- TcpClientConfig config;

- nint sock;

- bool connected;

- byte[]recvBuf;

- static ArpgTcpClientRuntime FromConfig(TcpClientConfig config)
  - 由组件配置构造未连接的客户端运行时。

- static ArpgTcpClientRuntime FromSocket(TcpClientConfig config, nint sock)
  - 包装服务器端已接受的套接字（直接处于已连接状态）。

- bool IsConnected()
  - 是否已连接。

- nint Handle()
  - 底层套接字句柄（未连接为 -1）。

- async void ConnectAsync()
  - 异步发起连接（不等待结果，用 IsConnected 查询）。

- async int ConnectAndWaitAsync()
  - 异步连接并等待结果，返回 Socket 约定的错误码（0 为成功）。

- async int SendMessageAsync(ArpgMessage message)
  - 异步发送一条帧封装消息，返回 AsyncSocket.Send 的错误码
    （0 为成功）。

- async ArpgMessage ReceiveMessageAsync()
  - 异步接收一条帧封装消息（假定一次读取收到完整帧）。读到
    EOF/错误时把 IsConnected 置 false 并返回 null；帧不完整时
    也返回 null。接收缓冲容量取自 config.MessageLength()。

- void Close()
  - 关闭套接字并释放缓冲；未连接时也安全。


## ArpgTcpServerRuntime (class)

ArpgTcpServer 组件的实时 TCP 服务器运行时：绑定监听套接字
（非阻塞、SO_REUSEADDR），跟踪已接受的 ArpgServerClient。
Start 失败（配置为空、被 Starting 否决、bind/listen 失败）返回
null；事件在 Starting/Started/Stopping/Stopped/ClientConnected/
ClientDisconnected 上回调。

- ServerConfig config;

- nint listener;

- bool running;

- int nextClientId;

- List<ArpgServerClient> clients;

- ArpgServerEvents events;

- static ArpgTcpServerRuntime Start(ServerConfig config, string bindHost)
  - 以默认事件总线启动监听（无 Starting/Stopping 否决）。

- static ArpgTcpServerRuntime Start(ServerConfig config, string bindHost, ArpgServerEvents events)
  - 启动监听：先触发 Starting（返回 false 则放弃），随后
    bind/listen/设非阻塞，任一步失败即返回 null（套接字已
    清理）；成功后触发 Started 并返回运行中的服务器。

- static async ArpgTcpServerRuntime StartAsync(ServerConfig config, string bindHost)
  - Start 的 async 包装（设置本身同步，无需额外让出）。

- async nint AcceptAsync()
  - 异步接受一个裸套接字（已设为非阻塞）。

- async ArpgServerClient AcceptClientAsync()
  - 异步接受并登记一个客户端，分配自增 id，触发
    ClientConnected 后返回客户端对象。

- bool IsRunning()
  - 服务器是否处于运行状态。

- int Port()
  - 监听端口（来自 ServerConfig）。

- int ClientCount()
  - 当前已接受的客户端数。

- ArpgServerClient ClientAt(int index)
  - 第 index 个客户端（不查越界）。

- bool RemoveClient(int id)
  - 按 id 关闭并移除客户端；找到返回 true，否则 false。

- void Stop()
  - 停止服务器：先触发 Stopping（返回 false 则中止停止），
    随后关闭全部客户端与监听套接字并触发 Stopped。未运行时
    调用为空操作。


## ArpgTemplate (class)

模板求值的静态入口。

- static string Evaluate(string template, ArpgDataSource source)
  - 用数据源求值模板：展开 `&path&` 与 {if} 分支后返回结果
    文本。template 为 null/空返回 ""；source 为 null 时按
    空对象求值（占位符解析为空）。

- static string EvaluateValue(string template, ArpgValue source)
  - 同 Evaluate，但直接以 ArpgValue 为解析根（适合一次性
    求值）。template 为 null/空返回 ""；source 为 null 按空
    对象求值。


## ArpgTemplateParser (class)

模板引擎求值器：解析 `{if 条件}…{elseif 条件}…{else}…{end}`
分支与 `&path&` 占位符，占位符从绑定的数据源解析值并按
文本插入。通常经 ArpgTemplate 间接使用。

- string input;

- ArpgValue source;

- int position;

- ArpgTemplateParser(string input, ArpgValue source)
  - 构造求值器：input 为模板原文，source 为 `&path&` 的解析根。

- static bool StartsAt(string text, int position, string prefix)
  - 判断 text 在 position 处是否以 prefix 开头（越界为 false）。

- static string Trim(string text)
  - 去除 text 两端的空格/制表/回车/换行字符。

- int Find(string token, int start)
  - 从 start 起查找 token 首次出现的下标；找不到返回 -1。

- static int FindOperator(string expression, string token)
  - 在表达式中查找比较运算符首次出现的下标；找不到返回 -1。

- static bool LooksNumeric(string text)
  - 判断 text 是否形如数值字面量（可选 +/- 前缀、数字与小数点，
    至少含一个数字）。空串为 false。

- ArpgValue Operand(string token)
  - 解析单个操作数：`&path&` 解析为数据源值（缺失返回 Null）；
    引号包裹为文本字面量；true/false 为布尔；数字形态为数值；
    其余按原文文本处理。

- bool Compare(string leftToken, string rightToken, string operation)
  - 比较两个操作数。==/!= 在任一侧为 Number 时按数值比较，
    否则按文本比较；>/>=/</<= 恒按数值比较。未知运算返回 false。

- bool EvaluateCondition(string expression)
  - 求值条件表达式：按 >= <= == != > < 顺序检测运算符并比较；
    无运算符时按布尔操作数解释。

- string ParseConditional(string condition)
  - 求值一段 {if 条件}…{elseif}…{else}…{end} 分支，返回
    命中分支的文本（无命中且无 else 时为空串）。

- ArpgTemplateSection ParseSection()
  - 从当前位置解析下一段：展开 `&path&` 占位符与嵌套 {if}，
    在 {elseif}/{else}/{end} 或输入末尾处停止，返回文本段并
    前进 position。


## ArpgTemplateSection (class)

模板解析出的一个连续文本段：内容、终止标记与
终止分支（{elseif}）携带的条件表达式。

- string text;

- int terminator;

- string condition;

- ArpgTemplateSection(string text, int terminator, string condition)
  - 构造一个文本段。

- string Text()
  - 段文本内容。

- int Terminator()
  - 终止标记（ArpgTemplateTerminator 常量之一）。

- string Condition()
  - {elseif} 分支携带的条件表达式；非 elseif 段为空串。


## ArpgTemplateTerminator (class)

模板分支段的终止标记：0=普通文本段 1=后接 {elseif }
2=后接 {else} 3=后接 {end}。

- static int None()
  - 无终止符（普通段或输入耗尽）。

- static int ElseIf()
  - 该段由 {elseif 条件} 终止。

- static int Else()
  - 该段由 {else} 终止。

- static int End()
  - 该段由 {end} 终止。


## ArpgTextAlign (class)

文本对齐常量：Start=起始对齐（水平=左/垂直=顶），Center=居中，
End=末尾对齐（水平=右/垂直=底）。用于 HorizontalAlign/
VerticalAlign 风格属性与富文本排版。

- static int Start()
  - 起始对齐（水平=左，垂直=顶）。

- static int Center()
  - 居中对齐。

- static int End()
  - 末尾对齐（水平=右，垂直=底）。


## ArpgTextMetrics (class)

无字体依赖的文本度量启发式：按 UTF-8 序列长度区分单字节与
多字节字符并估算宽度。仅供无真实字体度量时的回退排版。

- static int NextUtf8(string text, int index)
  - 返回 text 中 index 处 UTF-8 字符的字节长度（1~4）；
    首字节非法时按前缀区间就近取值，不校验越界。

- static int GlyphWidth(string glyph, int fontSize)
  - 单个字形的估算宽度（像素）：空白（空格/制表）取
    max(1, 字号/3)，其他单字节字符取 max(1, 字号*3/5)，
    多字节字符（CJK 等）取整字号。

- static int Measure(string text, int fontSize)
  - 整段文本的估算宽度（像素）：逐 UTF-8 字符累加 GlyphWidth。


## ArpgTimerTask (class)

调度器内的单个定时任务（字段说明见各 /// 注释）。

- string name;
  - 任务名（同一调度器内唯一，重名 Add 会先移除旧任务）。

- string payload;
  - 透传给回调的不透明载荷。

- int interval;
  - 触发间隔毫秒（构造时钳制到 ≥1）。

- int elapsed;
  - 已累计的毫秒数（Tick 累加，触发后扣掉一个间隔）。

- bool oneShot;
  - 是否一次性（触发后自动移除）。

- ArpgTimedEvent handler;

- ArpgTimerTask(string name, string payload, int interval, bool oneShot, ArpgTimedEvent handler)
  - 构造任务；interval 由 Add 钳制，elapsed 从 0 起。


## ArpgTween (class)

缓动时间线：按序执行的步骤链（引擎.创建缓动 产生，Add 链式
追加步骤）。运行时由引擎驱动各步骤，call 步骤按回调 id 触发
注册的回调。

- List<ArpgTweenStep> steps;

- ArpgTween()
  - 构造空时间线（一般经引擎工厂创建）。

- ArpgTween Add(ArpgTweenStep step)
  - 追加一个步骤并返回 this（链式）。

- int StepCount()
  - 步骤总数。

- ArpgTweenStep StepAt(int index)
  - 第 index 个步骤；越界返回 null。

- int TotalDuration()
  - 所有步骤的调度总时长（毫秒）。

- void Dispose()
  - 释放全部步骤（之后不应再使用本时间线）。


## ArpgTweenStep (class)

缓动时间线中的单步：to（把目标对象属性动画到绝对值）、
by（按相对增量动画）、wait（延时）、call（调用具名回调）。
经 To/By/Wait/Call 工厂创建，Set 链式追加属性目标。

- string op;

- string target;

- List<TweenTarget> props;

- int duration;

- ArpgEasing easing;

- string callback;

- static ArpgTweenStep To(string target, int duration, ArpgEasing easing)
  - 创建 to 步骤：duration 为毫秒，easing 为缓动曲线。

- static ArpgTweenStep By(string target, int duration, ArpgEasing easing)
  - 创建 by 步骤（属性按相对增量动画）。

- static ArpgTweenStep Wait(int duration)
  - 创建 wait 步骤：duration 为毫秒延时。

- static ArpgTweenStep Call(string callback)
  - 创建 call 步骤：callback 为注册的回调标识，不占用时长。

- ArpgTweenStep Set(string prop, double amount)
  - 追加一个属性目标并返回 this（链式）。

- string Op()
  - 步骤类型（"to"/"by"/"wait"/"call"）。

- string Target()
  - 目标对象标识符（wait/call 为空串）。

- int Duration()
  - 时长（毫秒；call 为 0）。

- ArpgEasing Curve()
  - 缓动曲线。

- string Callback()
  - 回调标识（仅 call 有意义）。

- int PropCount()
  - 属性目标个数。

- TweenTarget PropAt(int index)
  - 第 index 个属性目标（不查越界）。

- void Dispose()
  - 释放属性目标集合（之后不应再使用本步骤）。


## ArpgUiRuntime (class)

UI 运行时总入口：按 ArpgProject 实例化全部窗口，负责每帧同步
默认数据源（玩家属性模板变量）、驱动窗口更新、指针事件的捕获
与分发（窗口 -> 控件 -> 节点，先到先消费）与焦点管理。业务代码
通过 Runtime 的入口获取实例，每帧调用 Update 与（渲染层的）
Render，把宿主指针事件转投给 PointerDown/Up/Move。

- ArpgProject project;

- ArpgWorld world;

- ArpgEvents systemEvents;

- ArpgDataSource defaultDataSource;

- List<ArpgWindow> windows;

- ArpgControl focusedControl;

- ArpgControl capturedControl;

- ArpgWindow capturedWindow;

- int capturedButton;

- ArpgUiRuntime(ArpgProject project, ArpgWorld world, ArpgEvents systemEvents)

- ArpgProject Project()
  - 返回所属项目定义（只读视图）。

- ArpgWorld World()
  - 返回所属游戏世界（用于同步玩家属性到默认数据源）。

- ArpgEvents SystemEvents()
  - 返回系统级事件总线（如富文本链接全局回调）。

- ArpgDataSource DefaultDataSource()
  - 返回默认数据源（每帧同步玩家属性的模板变量集合）。

- int WindowCount()
  - 返回窗口实例数量（同项目定义的窗口数）。

- ArpgWindow WindowAt(int index)
  - 返回第 index 个窗口实例；index 须在 [0, WindowCount) 内。

- ArpgControl FocusedControl()
  - 返回当前持有焦点的控件；无焦点时为 null。

- ArpgControl CapturedControl()
  - 返回被指针按下捕获的控件（仅按下到抬起之间非 null）。

- int CapturedButton()
  - 返回捕获的指针按键（ArpgPointerButton 值；未捕获时为 -1）。

- ArpgWindow FindWindow(string id)
  - 按 Id 查找窗口；未找到返回 null。

- ArpgControl FindControl(string windowId, string controlId)
  - 按 窗口 Id + 控件 Id 查找控件；任一级未找到返回 null。

- void SyncDefaultDataSource()
  - 把玩家属性同步进默认数据源（id/name/level/hp 等英文与中文键、自定义属性与"属性.*"键）；无玩家时为空操作，框架每帧自动调用。

- void Update(int deltaMilliseconds)
  - 每帧推进：先同步默认数据源，再更新全部窗口；由框架主循环调用。

- ArpgWindow HitWindow(double x, double y)
  - 命中测试窗口：x/y 为游戏画面坐标；穿透窗口（ClickThrough）不参与，置顶窗口优先于普通窗口；未命中返回 null。

- void ClearFocus(ArpgControl next)
  - 焦点管理：把焦点移交给 next（可为 null），原焦点控件在变化时失焦；由指针按下流程内部调用。

- bool PointerDown(int button, int modifiers, double x, double y)
  - 框架指针按下入口：先分发给命中的控件，未消费再交给窗口 handler；空白处左键按下且 Movable 时进入窗口拖动；返回是否被消费。
    指针按下总入口：命中窗口则捕获窗口/控件并向下分发，未命中任何窗口时清除焦点与捕获状态并返回 false；返回是否被 handler 消费。

- bool PointerUp(int button, int modifiers, double x, double y)
  - 指针抬起总入口：把事件投给被捕获窗口（无捕获时按坐标命中），随后清除捕获状态；返回是否被消费。

- bool PointerMove(int button, int modifiers, double x, double y, double deltaX, double deltaY)
  - 指针移动总入口：投给被捕获窗口（无捕获时按坐标命中）；拖拽窗口/控件的位移也在其中处理；返回是否被消费。


## ArpgValidation (class)

常用字段断言助手：失败时向 diagnostics 记错误。

- static bool Required(ArpgDiagnostics diagnostics, string newValue, string path, string code)
  - 必填字符串校验：为 null 或空串时记录错误并返回 false，
    否则返回 true。

- static bool Positive(ArpgDiagnostics diagnostics, int newValue, string path, string code)
  - 正数校验：newValue ≤ 0 时记录错误并返回 false。


## ArpgValue (class)

游戏控件与模板表达式使用的动态值树。

- int kind;

- string textValue;

- double numberValue;

- bool boolValue;

- List<ArpgProperty> properties;

- static ArpgValue Null()
  - 构造一个 Null 值（textValue 为空串、properties 为空容器）。

- static ArpgValue Text(string text)
  - 构造文本值。text 允许为 null/空串。

- static ArpgValue Number(double number)
  - 构造数值值。

- static ArpgValue Integer(int number)
  - 构造数值值（int 隐式转 double，等价于 Number）。

- static ArpgValue Boolean(bool enabled)
  - 构造布尔值。

- static ArpgValue Object()
  - 构造空对象值（无任何属性）。

- int Kind()
  - 返回类型标签（ArpgValueKind 常量之一）。

- bool IsNull()
  - 是否为 Null 值。

- bool IsObject()
  - 是否为对象值。

- string AsText()
  - 按文本解释：Text 返回原文；Number 按数值转字符串；
    Boolean 返回 "true"/"false"；Null 与 Object 返回空串。

- double AsNumber()
  - 按数值解释：Number 返回原值；Boolean 转 1.0/0.0；
    Text 用 ParseDouble 解析（解析失败时按其约定返回 0.0）；
    Null 与 Object 返回 0.0。

- bool AsBoolean()
  - 按布尔解释：Boolean 返回原值；Number 非 0.0 即 true；
    Text 为空串/"0"/"false" 时 false，其余 true；
    Null 与 Object 返回 false。

- int IndexOf(string key)
  - 返回键 first 次出现的下标；不存在或非对象返回 -1。

- void Set(string key, ArpgValue newValue)
  - 设置属性；若当前不是对象值会自动转为对象（原有标量值被
    丢弃）。同名键覆盖，新键追加。

- void SetText(string key, string newValue)
  - Set(key, Text(newValue)) 的便捷封装。

- void SetNumber(string key, double newValue)
  - Set(key, Number(newValue)) 的便捷封装。

- void SetInteger(string key, int newValue)
  - Set(key, Integer(newValue)) 的便捷封装。

- void SetBoolean(string key, bool newValue)
  - Set(key, Boolean(newValue)) 的便捷封装。

- ArpgValue Get(string key)
  - 按键取属性值；非对象值或键不存在返回 null。

- int Count()
  - 属性个数（非对象值恒为 0）。

- string KeyAt(int index)
  - 第 index 个属性的键名（不查越界）。

- ArpgValue ValueAt(int index)
  - 第 index 个属性的值（不查越界）。

- static int DotAt(string path)
  - 返回 path 中第一个 '.' 的下标；没有返回 -1。

- ArpgValue Resolve(string path)
  - 按 "a.b.c" 点分路径逐层取属性。path 为 null/空时返回自身；
    任一层缺失返回 null。

- void SetPath(string path, ArpgValue newValue)
  - 按点分路径设置属性；中间层不存在或不是对象时自动创建
    空对象补齐。等价于多级 Set。


## ArpgValueKind (class)

ArpgValue 的类型标签常量：0=Null 1=Text 2=Number
3=Boolean 4=Object。配合 Kind() 使用。

- static int Null()
  - Null 值类型标签（0）。

- static int Text()
  - 文本值类型标签（1）。

- static int Number()
  - 数值值类型标签（2）。

- static int Boolean()
  - 布尔值类型标签（3）。

- static int Object()
  - 对象（属性容器）类型标签（4）。


## ArpgWebSocketRuntime (class)

WebSocketClient 组件的实时 WebSocket 客户端运行时：解析配置
地址为 WsUrl，把 RFC 6455 握手与帧处理委托给
System.Net.WebSocket.WebSocketClient。收发均为 async。

- WebSocketClientConfig config;

- WsUrl url;

- WebSocketClient ws;

- bool connected;

- static ArpgWebSocketRuntime FromConfig(WebSocketClientConfig config)
  - 由组件配置构造运行时并立即解析地址（未连接）。

- WsUrl Url()
  - 解析出的目标地址。

- bool IsConnected()
  - 是否已连接。

- async void ConnectAsync()
  - 异步执行握手连接；完成后用 IsConnected 判断结果。

- async void SendText(string text)
  - 异步发送一条文本帧。

- async string RecvText()
  - 异步接收一条文本帧；连接关闭时返回 null。

- async void Close()
  - 异步关闭连接（未连接时安全调用）。


## ArpgWindow (class)

UI 窗口运行时实例：由 ArpgWindowDefinition 构造，持有全部控件、
求值后的标题、运行时位置与可见性。Open/Close 切换可见性并各触发
一次 Opened/Closed；指针事件先分发给命中的控件，未消费再交给
窗口 handler；可拖动窗口按 Movable 在空白处拖拽。

- ArpgWindowDefinition definition;

- ArpgDataSource defaultDataSource;

- List<ArpgControl> controls;

- string evaluatedTitle;

- int x;

- int y;

- bool visible;

- bool dragging;

- ArpgWindow(ArpgWindowDefinition definition, ArpgProject project, ArpgDataSource defaultDataSource, ArpgEvents systemEvents)
  - 构造窗口实例：按定义实例化全部控件；初始可见时构造即触发 Created 与 Opened。

- string Id()
  - 返回窗口唯一 Id。

- string Title()
  - 返回模板求值后的标题。

- int X()
  - 返回窗口当前 X 坐标（像素，相对游戏画面左上角；拖动会改变运行时值）。

- int Y()
  - 返回窗口当前 Y 坐标（像素，相对游戏画面左上角）。

- int Width()
  - 返回窗口宽度（像素）。

- int Height()
  - 返回窗口高度（像素）。

- int BorderWidth()
  - 返回边框宽度（像素）。

- ArpgColor BorderColor()
  - 返回边框颜色。

- ArpgColor BackgroundColor()
  - 返回背景色。

- string BackgroundResource()
  - 返回背景贴图资源 Id；空串表示无背景贴图。

- bool AlwaysOnTop()
  - 返回窗口是否始终置顶。

- bool Movable()
  - 返回窗口是否可在空白处拖动。

- bool ClickThrough()
  - 返回窗口是否鼠标穿透（不参与命中测试）。

- bool Visible()
  - 返回窗口当前可见性。

- ArpgWindowEventHandlers Events()
  - 返回窗口事件槽位集合（单播）。

- int ControlCount()
  - 返回控件实例数量。

- ArpgControl ControlAt(int index)
  - 返回第 index 个控件实例；index 须在 [0, ControlCount) 内。

- ArpgControl FindControl(string id)
  - 按 Id 查找控件；未找到返回 null。

- void SetPosition(int x, int y)
  - 设置窗口位置（像素，相对游戏画面左上角）。

- void Open()
  - 打开窗口（置为可见并触发 Opened）；已可见时为空操作。

- void Close()
  - 关闭窗口（置为不可见并触发 Closed）；已不可见时为空操作。

- void Evaluate()
  - 重新求值标题（按窗口默认数据源替换模板占位符）；框架每帧调用。

- void Update(int deltaMilliseconds)
  - 每帧推进：重新求值标题并更新全部控件，最后触发 Updated 事件。

- bool HitTest(double x, double y)
  - 命中测试：x/y 为游戏画面坐标；仅可见窗口参与，按当前位置与尺寸判断。

- ArpgControl HitControl(double x, double y)
  - 命中测试控件：x/y 为游戏画面坐标；仅 MouseEvents 开启且命中的控件参与，同点多个命中取 Order 最大者；未命中返回 null。

- bool PointerDown(int button, int modifiers, double x, double y)
  - 框架指针按下入口：先分发给命中的控件，未消费再交给窗口 handler；空白处左键按下且 Movable 时进入窗口拖动；返回是否被消费。

- bool PointerUp(ArpgControl capturedControl, int button, int modifiers, double x, double y)
  - 框架指针抬起入口：capturedControl 为按下时捕获的控件（可传 null 改为按当前位置命中）；结束窗口拖动状态；返回是否被消费。

- bool PointerMove(ArpgControl capturedControl, int button, int modifiers, double x, double y, double deltaX, double deltaY)
  - 框架指针移动入口：拖动中先按位移移动窗口，再分发给捕获或命中的控件，未消费交给窗口 handler；返回是否被消费。

- void FireCustom(string name, string payload)
  - 触发窗口自定义事件（payload 原样透传给 OnCustomEvent 注册的 handler）。


## ArpgWindowDefinition (class)

UI 窗口定义：静态描述一个窗口的标题、位置尺寸、边框/背景、
行为开关（置顶/可拖动/鼠标穿透/默认可见）、蒙版不透明度、皮肤
与控件列表。x/y/width/height 均为像素，坐标相对游戏画面左上角。
构造时取默认值（默认不可见、可穿透为 false 等），通过 Set*
修改；Validate 校验尺寸与控件 Id 唯一性。

- string id;

- string title;

- int x;

- int y;

- int width;

- int height;

- int borderWidth;

- ArpgColor borderColor;

- ArpgColor backgroundColor;

- bool alwaysOnTop;

- bool movable;

- bool clickThrough;

- bool visibleByDefault;

- int maskOpacity;

- string backgroundResource;

- string skinId;

- List<ArpgControlDefinition> controls;

- ArpgWindowEventHandlers events;

- ArpgWindowDefinition(string id, int width, int height)
  - 构造窗口定义：id 为唯一标识，width/height 为像素尺寸。

- string Id()
  - 返回窗口唯一 Id。

- string Title()
  - 返回标题原文（模板占位符在运行时求值）。

- int X()
  - 返回窗口 X 坐标（像素，相对游戏画面左上角）。

- int Y()
  - 返回窗口 Y 坐标（像素，相对游戏画面左上角）。

- int Width()
  - 返回窗口宽度（像素）。

- int Height()
  - 返回窗口高度（像素）。

- int BorderWidth()
  - 返回边框宽度（像素）。

- ArpgColor BorderColor()
  - 返回边框颜色。

- ArpgColor BackgroundColor()
  - 返回背景色。

- bool AlwaysOnTop()
  - 返回是否始终置顶（渲染与命中时优先于普通窗口）。

- bool Movable()
  - 返回是否可在空白处拖动窗口。

- bool ClickThrough()
  - 返回是否鼠标穿透（不参与命中测试）。

- bool VisibleByDefault()
  - 返回是否默认可见（运行时创建即打开并触发 Opened）。

- int MaskOpacity()
  - 返回模态蒙版不透明度（0~255，0 为无蒙版）。

- string BackgroundResource()
  - 返回背景贴图资源 Id；空串表示无背景贴图。

- string SkinId()
  - 返回皮肤 Id；空串表示未指定皮肤。

- int ControlCount()
  - 返回控件定义数量。

- ArpgControlDefinition ControlAt(int index)
  - 返回第 index 个控件定义；index 须在 [0, ControlCount) 内。

- ArpgWindowEventHandlers Events()
  - 返回窗口事件槽位集合（单播）。

- void SetTitle(string newValue)
  - 设置标题（支持模板占位符）。

- void SetPosition(int x, int y)
  - 设置窗口位置（像素）。

- void SetBorder(int width, ArpgColor color)
  - 设置边框（此定义层不钳制负值，Validate 会报错）。

- void SetBackgroundColor(ArpgColor newValue)
  - 设置背景色。

- void SetAlwaysOnTop(bool newValue)
  - 设置是否始终置顶。

- void SetMovable(bool newValue)
  - 设置是否可拖动。

- void SetClickThrough(bool newValue)
  - 设置是否鼠标穿透。

- void SetVisibleByDefault(bool newValue)
  - 设置是否默认可见。

- void SetMaskOpacity(int newValue)
  - 设置蒙版不透明度；超出 [0, 255] 的值被钳制。

- void SetBackgroundResource(string newValue)
  - 设置背景贴图资源 Id。

- void SetSkinId(string newValue)
  - 设置皮肤 Id。

- void AddControl(ArpgControlDefinition control)
  - 追加一个控件定义（运行时按定义顺序实例化）。

- void Validate(ArpgDiagnostics diagnostics)
  - 校验窗口定义：Id 非空、宽高为正、边框宽度非负、控件 Id 不重复并递归校验控件；错误写入 diagnostics。


## ArpgWindowEventHandlers (class)

窗口事件单播槽位集合：每个槽位只保存一个 handler，后注册者覆盖
前者。经 ArpgWindowDefinition.Events() 获取；业务代码调用 On*
注册回调，框架在窗口生命周期与指针交互时触发 Raise*，不要手动
调用 Raise*。

- ArpgWindowCreated created;

- ArpgWindowVisibilityChanged opened;

- ArpgWindowVisibilityChanged closed;

- ArpgWindowUpdated updated;

- ArpgWindowPointerChanged pointerDown;

- ArpgWindowPointerChanged pointerUp;

- ArpgWindowPointerChanged pointerMove;

- ArpgWindowCustomEvent customEvent;

- void OnCreated(ArpgWindowCreated handler)
  - 注册窗口实例化完成回调（全部控件实例化后触发一次）。

- void OnOpened(ArpgWindowVisibilityChanged handler)
  - 注册窗口打开回调（Open 或初始可见时触发）。

- void OnClosed(ArpgWindowVisibilityChanged handler)
  - 注册窗口关闭回调（Close 时触发一次）。

- void OnUpdated(ArpgWindowUpdated handler)
  - 注册窗口每帧更新回调（deltaMilliseconds 为帧间隔毫秒）。

- void OnPointerDown(ArpgWindowPointerChanged handler)
  - 注册窗口按下回调；返回 true 可消费事件。

- void OnPointerUp(ArpgWindowPointerChanged handler)
  - 注册窗口抬起回调；返回 true 可消费事件。

- void OnPointerMove(ArpgWindowPointerChanged handler)
  - 注册窗口移动回调；返回 true 可消费事件。

- void OnCustomEvent(ArpgWindowCustomEvent handler)
  - 注册窗口自定义事件回调（由 FireCustom 触发）。

- void RaiseCreated(string windowId)
  - 触发窗口实例化回调；未注册 handler 时为空操作。

- void RaiseOpened(string windowId)
  - 触发窗口打开回调；未注册 handler 时为空操作。

- void RaiseClosed(string windowId)
  - 触发窗口关闭回调；未注册 handler 时为空操作。

- void RaiseUpdated(string windowId, int deltaMilliseconds)
  - 触发窗口每帧更新回调；未注册 handler 时为空操作。

- bool RaisePointerDown(string windowId, string controlId, int button, int modifiers, double x, double y)
  - 触发窗口按下回调；未注册时返回 false（事件未消费）。

- bool RaisePointerUp(string windowId, string controlId, int button, int modifiers, double x, double y)
  - 触发窗口抬起回调；未注册时返回 false（事件未消费）。

- bool RaisePointerMove(string windowId, string controlId, int button, int modifiers, double x, double y)
  - 触发窗口移动回调；未注册时返回 false（事件未消费）。

- void RaiseCustom(string windowId, string name, string payload)
  - 触发窗口自定义事件回调；未注册 handler 时为空操作。


## ArpgWorld (class)

独立于不可变组件定义的实时地图状态。经
CreateWithEvents 构造（固定种子随机数），承载当前地图、
全部角色实例与玩家引用；所有玩法操作（移动/物品/装备/
buff/技能/传送门/时间推进）都从这里进入。

- ArpgProject project;

- ArpgMapDefinition currentMap;

- List<ArpgActor> actors;

- ArpgActor player;

- int nextInstanceId;

- ArpgEvents systemEvents;

- ArpgGameplayEvents gameplayEvents;

- ArpgRandom random;

- static ArpgWorld CreateWithEvents(ArpgProject project, ArpgEvents systemEvents, ArpgGameplayEvents gameplayEvents)
  - 构造一个未进图的空世界，绑定事件总线；随机数使用固定
    种子 19790327（回放/测试确定性）。

- ArpgProject Project()
  - 所属项目。

- ArpgMapDefinition CurrentMap()
  - 当前所在地图定义；未进图为 null。

- ArpgActor Player()
  - 玩家实例；未进图或项目没有默认玩家为 null。

- ArpgGameplayEvents GameplayEvents()
  - 玩法事件总线（与传入的一致）。

- int ActorCount()
  - 场上角色数（含玩家）。

- ArpgActor ActorAt(int index)
  - 第 index 个角色（不查越界）。

- ArpgActor CreateActor(string actorId, string faction, int gridX, int gridY, int level)
  - 追加新的玩家角色实例（等级钳制到 ≥1，空 faction 不覆盖）。
    定义不存在、未进入地图或坐标越界时返回 null。

- bool EnterDefaultMap()
  - 进入项目默认地图；没有默认地图时返回 false。

- bool EnterMap(string mapId)
  - 按地图 id 进入地图（不保留玩家实例，portalId 传 -1）；
    地图不存在返回 false。

- bool LoadMap(string mapId, ArpgActor preservedPlayer, int portalId)
  - 加载地图并重建场上角色：preservedPlayer 非空时保留原玩家
    实例，否则用默认玩家重建并置于地图出生点；随后按地图
    spawn 表刷出角色。成功后触发 MapChanged 事件；地图不存在
    返回 false。

- void ApplyStartingBuffs(ArpgActor actor, ArpgActorDefinition definition)
  - 按角色定义的初始增益列表依次施加（unique=true）。

- bool MoveActor(ArpgActor actor, int x, int y)
  - 把角色移动到目标格子：要求已进图、角色存活、无移动限制
    buff、目标在图内且非阻挡格。成功后触发 RemoveOnMove 类
    buff 移除；任一条件不满足返回 false。

- bool HasMovementRestriction(ArpgActor actor)
  - 角色是否被禁止移动（身上存在 MovementDisabled 的 buff）。

- bool HasAttackRestriction(ArpgActor actor)
  - 角色是否被禁止攻击（身上存在 AttackDisabled 的 buff）。

- int AddItem(ArpgActor actor, string itemId, int count)
  - 给角色添加物品：受 MaxStack 与背包容量限制，返回实际加入
    数量（0 表示全失败）。部分成功时触发 ItemChanged，未加满
    时触发系统提示 4（背包已满）。count ≤ 0 或物品不存在返回 0。

- bool UseItem(ArpgActor actor, string itemId)
  - 使用一件消耗品：要求角色存活、物品存在且非装备类、库存
    足够、等级达标（不足触发提示 3）、冷却就绪（未就绪触发
    提示 2，冷却组为空时用 "item:<id>"）。成功时扣 1 个、按
    固定值+最大值百分比恢复 HP/MP、启动冷却并触发
    ItemChanged/ItemUsed。失败返回 false（不消耗）。

- bool EquipItem(ArpgActor actor, string itemId, string slot)
  - 从背包装备一件物品到槽位：从背包扣除 1 件，同槽位旧装备
    退回背包（放不下则整体失败）。成功后钳制生命值并触发
    ItemChanged。物品不存在/等级不足（触发提示 3）/非装备类
    返回 false。

- bool UnequipItem(ArpgActor actor, string slot)
  - 卸下槽位装备并放回背包（放不下返回 false）。成功后钳制
    生命值并触发 ItemChanged。槽位为空返回 false。

- ArpgBuffInstance ApplyBuff(ArpgActor actor, string buffId, ArpgActor source, bool unique)
  - 给存活角色施加增益：unique=true 时先移除同 id 旧实例。
    成功后钳制生命值并触发 BuffChanged(added=true)；角色死亡
    或增益定义不存在返回 null。

- bool RemoveBuff(ArpgActor actor, string buffId)
  - 移除角色身上全部指定 id 的增益，逐个触发
    BuffChanged(added=false)，移除后钳制生命值。删到了返回 true。

- ArpgSkillCastResult CastSkill(ArpgActor caster, string skillId, ArpgActor target)
  - 施放技能的完整结算入口：依次校验技能存在（UnknownSkill）、
    已学会（NotLearned）、施法者存活且无攻击限制
    （CasterDead）、目标合法（InvalidTarget）、射程（OutOfRange）、
    冷却（Cooldown，触发提示 1）、消耗（InsufficientResources）。
    通过后掷命中/暴击、结算伤害、施加技能携带的 buff，返回
    带结算结果的 ArpgSkillCastResult（含失败状态）并触发
    SkillResolved。

- ArpgSkillCastResult ResolveCast(ArpgSkillCastResult result)
  - 触发 SkillResolved 事件并原样返回 result。

- double DamageActor(ArpgActor source, ArpgActor target, double amount, bool critical)
  - 对目标造成实际伤害（受目标自身减伤逻辑约束），返回实际
    生效值。生效时触发 ActorDamaged 并移除 RemoveOnDamage 类
    buff；由存活转为死亡时移除 RemoveOnDeath 类 buff 并触发
    ActorDied。target 为 null 返回 0。

- void RemoveConditionalBuffs(ArpgActor actor, int condition)
  - 按条件码移除条件类 buff：1=移动后 2=攻击后 3=受伤后
    4=死亡后，逐个触发 BuffChanged，最后钳制生命值。

- void Update(int deltaMilliseconds)
  - 推进世界时间：递减全部角色冷却、推进 buff 计时并结算其
    周期效果（负 HP 属性为持续伤害，正值为持续回复），过期
    buff 自动移除并触发 BuffChanged。deltaMilliseconds ≤ 0 时
    不做任何事。

- bool TryUsePortal(ArpgActor actor)
  - 若玩家站在当前地图的传送门格子上，切换到目标地图：保留
    玩家实例、按传送门重设出生坐标与朝向。仅对玩家本人生效，
    不在传送门上或目标地图加载失败返回 false。


## GlobalEntry (class)

单个类型化全局变量：名称、字符串编码的值与类型标签
（0 = string，1 = int，2 = bool）。用单个实体代替平行列表。

- string key;

- string val;

- int kind;

- GlobalEntry(string key, string val, int kind)


## IsoTileMap (class)

2.5D 等轴测（Isometric）斜 45 度菱形地图映射引擎。
专为传奇类、暗黑类、经典模拟经营游戏设计。
采用工业界标准的 2:1 等轴测投影变换（例如 64x32 菱形地砖）。

- int mapWidth;

- int mapHeight;

- int tileWidth;

- int tileHeight;

- int halfTileW;

- int halfTileH;

- int[]groundTiles;

- byte[]collision;

- IsoTileMap(int mapW, int mapH, int tileW, int tileH)

- static IsoTileMap Create(int mapW, int mapH, int tileW, int tileH)

- int MapWidth { get }

- int MapHeight { get }

- int TileWidth { get }

- int TileHeight { get }

- int Index(int gx, int gy)

- void SetTile(int gx, int gy, int tileId, bool blocked)

- int GetTile(int gx, int gy)

- bool IsBlocked(int gx, int gy)

- void GridToWorld(int gx, int gy, out double sx, out double sy)
  - 网格坐标 (gx, gy) 转换为屏幕空间坐标 (sx, sy)。
    菱形地表中心点。

- void WorldToGrid(double sx, double sy, out int gx, out int gy)
  - 世界/屏幕拾取坐标 (sx, sy) 逆转换为网格坐标 (gx, gy)。
    鼠标点击寻路、移动落点的核心算子。

- static int GetDirection8(double fromX, double fromY, double toX, double toY)
  - 计算 8 方向朝向索引（0: 上, 1: 右上, 2: 右, 3: 右下, 4: 下, 5: 左下, 6: 左, 7: 左上）。
    ARPG 角色移动、砍怪朝向判定的标准算法。


## LootScatter (class)

经典 ARPG 爆装与金币四散飞出弹跳物理系统（“怪物大爆”）。
模拟物品从怪物体内向四周抛射、在重力作用下抛物线运动并在地面多次弹性跳跃落地的全套真实物理轨迹。

- int capacity;

- int activeCount;

- bool[]active;

- int[]itemIds;

- int[]itemTypes;

- double[]posX;

- double[]posY;

- double[]posZ;

- double[]velX;

- double[]velY;

- double[]velZ;

- int[]bounceCount;

- bool[]isSettled;

- Random rnd;

- LootScatter(int cap)

- static LootScatter Create(int cap)

- int ActiveCount { get }

- bool IsActive(int idx)

- bool IsSettled(int idx)

- double GetX(int idx)

- double GetY(int idx)

- double GetGroundY(int idx)

- int GetType(int idx)

- int GetItemId(int idx)

- void Burst(double originX, double originY, int count, int startItemId)
  - 在 (originX, originY) 处引爆多件战利品向四周喷射。

- void Update(double dt)
  - 逐物理步长更新弹跳重力模拟。

- bool Collect(int slot)
  - 拾取战利品。


## ServerConfig (class)

Server.lua 配置：服务端入口（通过 `-s` 启动）、ArpgTcpServer
（配对客户端组件为 ArpgTcpClient）。服务器与客户端交换
table-format messages keyed by a unique 通信标识 cid。Available server-side
components: Sqlite, 地图, 角色。
Config: { 名称, 端口, 消息长度, 扩展组件, 扩展脚本, 系统事件 }。

- string name;

- int port;

- int messageLength;

- List<string> extComponents;

- List<string> extScripts;

- ServerConfig(string name, int port)

- ServerConfig SetMessageLength(int amount)
  - 设置单条消息包上限（字节，默认 2 MiB）；返回 this。

- ServerConfig AddComponent(string script)
  - 追加一个扩展组件名（按登记顺序）；返回 this。

- ServerConfig AddScript(string script)
  - 追加一个扩展脚本（按登记顺序）；返回 this。

- string Name()
  - 服务器名（同时用作控制台标题）。

- int Port()
  - 监听端口。

- int MessageLength()
  - 单条消息包上限（字节，默认 2 MiB）。

- int ComponentCount()
  - 扩展组件数量。

- int ScriptCount()
  - 扩展脚本数量。

- string ComponentAt(int index)
  - 按下标读取扩展组件名；应配合 ComponentCount 使用。

- string ScriptAt(int index)
  - 按下标读取扩展脚本；应配合 ScriptCount 使用。


## SqliteComponentConfig (class)

SqliteDB 组件配置: { 类型="Sqlite", 名称=... }。扩展组件中引用的
Sqlite 数据库组件；运行期方法（打开/执行）作用于实时对象。

- string kind;

- string name;

- SqliteComponentConfig(string name)

- string Kind()
  - 组件类型标识，固定为 "Sqlite"。

- string Name()
  - 组件全局唯一名称。


## TcpClientConfig (class)

ArpgTcpClient 组件配置：与 Server.lua 的 ArpgTcpServer 配对的
客户端，带自动心跳与自动重连。{ 类型, 名称, ip, 端口, 消息长度, token }。

- string kind;

- string name;

- string ip;

- int port;

- int messageLength;

- int token;

- TcpClientConfig(string name, string ip, int port)

- TcpClientConfig SetMessageLength(int amount)
  - 设置单条消息包上限（字节）；返回 this。

- TcpClientConfig SetToken(int t)
  - 设置连接令牌（来自服务器连接事件，默认 0）；返回 this。

- string Kind()
  - 组件类型标识，固定为 "ArpgTcpClient"。

- string Name()
  - 组件全局唯一名称。

- string Ip()
  - 服务器 IP。

- int Port()
  - 服务器端口。

- int MessageLength()
  - 单条消息包上限（字节，默认 2 MiB）。

- int Token()
  - 连接令牌（默认 0）。


## TweenTarget (class)

单个动画属性目标：属性名 + 终值/增量。to 步骤中 amount 为
绝对终值，by 步骤中为相对增量。

- string prop;

- double amount;

- static TweenTarget Of(string prop, double amount)
  - 构造属性目标。

- string Prop()
  - 属性名（如 "x" / "y" / "透明度"）。

- double Amount()
  - 目标值（to 为绝对值，by 为增量）。


## WebSocketClientConfig (class)

WebSocketClient 组件配置：{ 类型, 名称, 地址 }，连接到
ws:// 或 wss:// 地址。

- string kind;

- string name;

- string url;
  - 服务器地址（ws:// 或 wss:// URL）。

- WebSocketClientConfig(string name)

- WebSocketClientConfig SetUrl(string url)
  - 设置服务器地址（ws:// 或 wss:// URL）；返回 this。

- string Kind()
  - 组件类型标识，固定为 "WebSocketClient"。

- string Name()
  - 组件全局唯一名称。

- string Url()
  - 服务器地址（ws:// 或 wss:// URL，默认空串）。


## WsUrl (class)

解析后的 WebSocket 客户端地址（ws:// 或 wss://）：scheme、
host、port、path 与 secure 标志。端口省略时默认 80（ws）
或 443（wss）；path 省略时默认 "/"。

- string scheme;
  - 协议名："ws" 或 "wss"。

- string host;
  - 主机名或 IP（authority 中端口前的部分）。

- int port;
  - 端口号；未显式给出时为 80（ws）或 443（wss）。

- string path;
  - 路径，以 "/" 开头；省略时为 "/"。

- bool secure;
  - 是否为加密连接（wss）。

- static int DigitsToInt(string s, int start, int end)
  - 把 s 的 [start, end) 区间数字字符转为 int，遇非数字即停
    并返回已累计值；非数字开头返回 0。

- static WsUrl Parse(string url)
  - 解析 ws:// 或 wss:// 地址。无法识别前缀时按 host 为空、
    ws 默认端口处理；不抛异常。

- string Scheme()
  - 协议名（"ws"/"wss"）。

- string Host()
  - 主机名或 IP。

- int Port()
  - 端口（默认 80/443）。

- string Path()
  - 请求路径（以 "/" 开头）。

- bool Secure()
  - 是否 wss（加密）。


## YSortLayer (class)

2.5D 遮挡关系 Y 轴动态深度排序层（YSort）。
解决树木、建筑物、玩家、怪物、NPC 之间的相互遮挡层级。
零 GC 预分配，支持千级实体每帧原位快速排序。

- int capacity;

- int count;

- int[]entityIds;

- int[]entityTypes;

- double[]footY;

- int[]orderIndices;

- YSortLayer(int cap)

- static YSortLayer Create(int cap)

- int Count { get }

- void Clear()

- bool Push(int id, int type, double y)
  - 登记一个待渲染实体及其脚底世界 Y 坐标。

- void Sort()
  - 原位快速排序，按 footY 从小到大排定渲染顺序。

- void QuickSort(int left, int right)

- int GetSortedEntityId(int i)
  - 获取排序后第 i 个槽位的实体 ID（0 分配直通）。

- int GetSortedEntityType(int i)
  - 获取排序后第 i 个槽位的实体类型。

- double GetSortedFootY(int i)
  - 获取排序后第 i 个槽位的脚底 Y 坐标。

- void ForEachSorted(Action <int, int, double> renderAction)
  - 依正确的深度遮挡顺序逐个提取实体渲染。


## bool (delegate)

系统事件总线（ArpgEvents）的启动前回调签名。

`delegate bool ArpgStarting(string arguments);`


## bool (delegate)

系统事件总线的关闭请求回调签名。

`delegate bool ArpgClosing();`


## bool (delegate)

系统事件总线的系统提示回调签名。

`delegate bool ArpgSystemPrompt(int code, string message);`


## bool (delegate)

服务器启动前回调：bindHost 为待绑定的监听地址，返回 false 否决本次启动。

`delegate bool ArpgServerStarting(string bindHost);`


## bool (delegate)

服务器关闭前回调：返回 false 否决本次关闭。

`delegate bool ArpgServerStopping();`


## bool (delegate)

节点指针事件回调；button 取 ArpgPointerButton 值，modifiers 为
修饰键掩码，x/y 为相对所属控件原点的节点局部坐标。返回 true
表示事件已消费，不再向控件/窗口层分发。

`delegate bool ArpgNodePointerChanged(string nodeId, int button, int modifiers, double x, double y);`


## bool (delegate)

控件指针事件回调；nodeId 为命中的子节点 Id（未命中节点时为空
串），x/y 为相对窗口原点的控件局部坐标。返回 true 表示事件已
消费，不再向窗口层分发。

`delegate bool ArpgControlPointerChanged(string controlId, string nodeId, int button, int modifiers, double x, double y);`


## bool (delegate)

窗口指针事件回调；controlId 为命中的控件 Id（未命中时为空
串），x/y 为相对窗口左上角的局部坐标。返回 true 表示事件已
消费。

`delegate bool ArpgWindowPointerChanged(string windowId, string controlId, int button, int modifiers, double x, double y);`


## void (delegate)

系统事件总线的启动完成回调签名。

`delegate void ArpgStarted();`


## void (delegate)

系统事件总线的焦点变化回调签名。

`delegate void ArpgFocusChanged(bool focused);`


## void (delegate)

系统事件总线的窗口状态变化回调签名。

`delegate void ArpgWindowStateChanged(int state);`


## void (delegate)

系统事件总线的窗口尺寸变化回调签名。

`delegate void ArpgSizeChanged(int width, int height);`


## void (delegate)

系统事件总线的按键按下/抬起回调签名。

`delegate void ArpgKeyChanged(int keycode, bool alt, bool shift, bool control);`


## void (delegate)

系统事件总线的菜单项激活回调签名。

`delegate void ArpgMenuChanged(string menuName, string itemName, string payload);`


## void (delegate)

富文本链接点击回调签名（窗口 Id/控件 Id/链接原文）。

`delegate void ArpgRichTextLinkChanged(string windowId, string controlId, string link);`


## void (delegate)

定时事件回调签名：任务名/载荷/本次帧间隔毫秒/累计毫秒。

`delegate void ArpgTimedEvent(string name, string payload, int deltaMilliseconds, int elapsedMilliseconds);`


## void (delegate)

物品增减回调签名（数量为该物品当前持有总量）。

`delegate void ArpgItemChanged(ArpgActor actor, ArpgItemDefinition item, int quantity);`


## void (delegate)

物品使用回调签名。

`delegate void ArpgItemUsed(ArpgActor actor, ArpgItemDefinition item);`


## void (delegate)

技能结算完成回调签名。

`delegate void ArpgSkillResolved(ArpgSkillCastResult result);`


## void (delegate)

增益增删回调签名（added=true 添加，false 移除）。

`delegate void ArpgBuffChanged(ArpgActor actor, ArpgBuffInstance buff, bool added);`


## void (delegate)

角色受伤回调签名（amount 为实际生效伤害，critical 为暴击）。

`delegate void ArpgActorDamaged(ArpgActor source, ArpgActor target, double amount, bool critical);`


## void (delegate)

角色死亡回调签名（killer 可为 null，如毒亡时 source 为 null）。

`delegate void ArpgActorDied(ArpgActor actor, ArpgActor killer);`


## void (delegate)

切图完成回调签名（首次进图 previousMapId 为空串，非传送门
切图 portalId 为 -1）。

`delegate void ArpgMapChanged(string previousMapId, string currentMapId, int portalId);`


## void (delegate)

服务器启动成功后回调：config 为服务器配置，bindHost 为实际绑定的地址。

`delegate void ArpgServerStarted(ServerConfig config, string bindHost);`


## void (delegate)

服务器关闭完成后回调。

`delegate void ArpgServerStopped(ServerConfig config);`


## void (delegate)

客户端接入/断开回调：client 为发生变化的客户端。

`delegate void ArpgServerClientChanged(ArpgServerClient client);`


## void (delegate)

收到客户端消息帧回调：client 为来源客户端，message 为消息内容。

`delegate void ArpgServerMessageReceived(ArpgServerClient client, ArpgMessage message);`


## void (delegate)

节点实例创建回调：节点运行时实例化完成后触发一次。

`delegate void ArpgNodeCreated(string nodeId);`


## void (delegate)

节点每帧更新回调；deltaMilliseconds 为距上一帧的毫秒数。

`delegate void ArpgNodeUpdated(string nodeId, int deltaMilliseconds);`


## void (delegate)

节点焦点变化回调；focused 为是否获得焦点。

`delegate void ArpgNodeFocusChanged(string nodeId, bool focused);`


## void (delegate)

节点自定义事件回调（由 FireCustom 触发，payload 原样透传）。

`delegate void ArpgNodeCustomEvent(string nodeId, string name, string payload);`


## void (delegate)

控件实例创建回调：控件运行时实例化（含 Prefab 节点展开）后触发一次。

`delegate void ArpgControlCreated(string controlId);`


## void (delegate)

控件每帧更新回调；deltaMilliseconds 为距上一帧的毫秒数。

`delegate void ArpgControlUpdated(string controlId, int deltaMilliseconds);`


## void (delegate)

控件焦点变化回调；focused 为是否获得焦点。

`delegate void ArpgControlFocusChanged(string controlId, bool focused);`


## void (delegate)

可拖拽控件的拖动回调；deltaX/deltaY 为相对上次的位移（像素，
累加进控件位置后再触发）。

`delegate void ArpgControlDragged(string controlId, double deltaX, double deltaY);`


## void (delegate)

富文本链接激活回调；link 为链接原文（点击链接 run 时触发）。

`delegate void ArpgControlLinkActivated(string controlId, string link);`


## void (delegate)

按钮激活回调：左键在按钮命中区域内按下、随后在其区域内抬起时触发。

`delegate void ArpgControlActivated(string controlId);`


## void (delegate)

数值型控件（进度条等）当前值变化回调；newValue 为钳制到
[最小值, 最大值] 后的新值。

`delegate void ArpgControlValueChanged(string controlId, double newValue);`


## void (delegate)

控件自定义事件回调（由 FireCustom 触发，payload 原样透传）。

`delegate void ArpgControlCustomEvent(string controlId, string name, string payload);`


## void (delegate)

窗口实例创建回调：窗口及其全部控件实例化完成后触发一次。

`delegate void ArpgWindowCreated(string windowId);`


## void (delegate)

窗口可见性变化回调：Open/Close 各触发一次（初始即可见的窗口
在创建时也会触发一次"打开"）。

`delegate void ArpgWindowVisibilityChanged(string windowId);`


## void (delegate)

窗口每帧更新回调；deltaMilliseconds 为距上一帧的毫秒数。

`delegate void ArpgWindowUpdated(string windowId, int deltaMilliseconds);`


## void (delegate)

窗口自定义事件回调（由 FireCustom 触发，payload 原样透传）。

`delegate void ArpgWindowCustomEvent(string windowId, string name, string payload);`


## ArpgEasing (enum)

缓动曲线枚举：每个族都有 _in / _out / _inOut 变体（由引擎
创建缓动时按时间进度映射属性值）。线性族 Linear 无变体。

- Linear

- QuadIn

- QuadOut

- QuadInOut

- CubicIn

- CubicOut

- CubicInOut

- QuartIn

- QuartOut

- QuartInOut

- QuintIn

- QuintOut

- QuintInOut

- SineIn

- SineOut

- SineInOut

- ExpoIn

- ExpoOut

- ExpoInOut

- CircIn

- CircOut

- CircInOut

- ElasticIn

- ElasticOut

- ElasticInOut

- BackIn

- BackOut

- BackInOut

- BounceIn

- BounceOut

- BounceInOut = 反弹衰减


## MusicState (enum)

音乐播放状态：由 Play/Pause/Stop 驱动，音频适配器据此
控制原生混音器。

- Stopped

- Playing

- Paused
