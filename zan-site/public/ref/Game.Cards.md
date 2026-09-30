# Game.Cards

> 源码: `packages/Zan.Game/src/Game/Cards/ActionQueue.zan`, `packages/Zan.Game/src/Game/Cards/Battle.zan`, `packages/Zan.Game/src/Game/Cards/CardZone.zan`, `packages/Zan.Game/src/Game/Cards/Cards.zan`, `packages/Zan.Game/src/Game/Cards/HandFanLayout.zan`


## ActionQueue (class)

卡牌与策略游戏动作演播序列队列。
将回合制逻辑演算与视觉动画解耦，确保法术连击、亡语触发、伤害扣血按节奏流畅展现。

- GameAction[]queue;

- int head;

- int tail;

- int count;

- int capacity;

- GameAction currentAction;

- double currentTimer;

- bool isPlaying;

- ActionQueue(int cap)

- static ActionQueue Create(int cap)

- bool IsPlaying { get }

- int PendingCount { get }

- bool Enqueue(ActionKind kind, int src, int tgt, int val, double duration)
  - 向队列尾部推入一个动作。

- void Update(double dt, Action<GameAction> onActionStart, Action<GameAction> onActionFinish)
  - 逐物理帧推进演播序列。
    触发 onActionStart 和 onActionFinish 委托。

- void Clear()


## BoardSlot (class)

战场卡槽或交互区域。

- int slotIndex;

- double x;

- double y;

- double width;

- double height;

- int occupantCardId;

- BoardSlot(int idx, double x, double y, double w, double h)

- static BoardSlot Create(int idx, double x, double y, double w, double h)

- int Index { get }

- double X { get }

- double Y { get }

- double Width { get }

- double Height { get }

- int OccupantCardId{ get set}

- bool IsEmpty { get }

- bool Contains(double px, double py)
  - 判断点 (px, py) 是否落在卡槽范围内。

- double DistanceSqToCenter(double px, double py)
  - 计算点到卡槽中心点的欧氏距离平方，用于磁吸距离计算。


## CardActor (class)

卡牌战斗中的单位（玩家或敌人）：维护生命、格挡、力量、易伤。
伤害结算顺序为：易伤增伤 → 格挡吸收 → 扣血，血量钳制在
[0, maxHp]。格挡在本方回合开始时清零，易伤在回合结束时递减。

- string id;

- int hp;

- int maxHp;

- int block;

- int strength;

- int vulnerable;

- CardActor(string id, int maxHp)
  - 构造单位：id 为唯一标识，maxHp 为初始与最大生命，小于 1 时钳制为 1；初始血量等于 maxHp，格挡/力量/易伤为 0。

- int TakeDamage(int amount)
  - 结算一次伤害：负值按 0 处理，易伤状态加成 50%，先消耗格挡再扣血；返回实际扣减的生命值（不含被格挡部分），血量下限为 0。

- int Heal(int amount)
  - 治疗单位：负值直接返回 0 不生效；返回实际恢复量，血量上限钳制到 maxHp。

- void AddBlock(int amount)
  - 增加格挡值：仅正数生效，格挡在下次本方回合开始时清零。

- void AddStrength(int amount)
  - 增加力量：可为负（削弱），力量会加成后续伤害类效果的目标结算。

- void AddVulnerable(int turns)
  - 施加易伤：仅正数生效，回合数累加；易伤期间受到的伤害增加 50%，每经过自己回合结束递减 1。

- void StartTurn()
  - 本方回合开始：格挡清零（格挡不跨回合保留）。

- void EndTurn()
  - 本方回合结束：易伤剩余回合数大于 0 时递减 1。

- string Id()
  - 唯一标识符（构造时传入，不变）。

- int Hp()
  - 当前生命值，范围 [0, maxHp]。

- int MaxHp()
  - 最大生命值（构造后不变）。

- int Block()
  - 当前格挡值，回合开始时清零。

- int Strength()
  - 当前力量值，可为负。

- int Vulnerable()
  - 易伤剩余回合数，0 表示无易伤。

- bool Alive()
  - 是否存活：当前生命值大于 0。


## CardBattle (class)

用于肉鸽卡牌游戏的紧凑型构筑战斗运行时。项目
可替换敌方意图选择，同时保留区域与效果规则。

- CardActor player;

- CardActor enemy;

- CardDeck deck;

- int energy;

- int maxEnergy;

- int handSize;

- int turn;

- int enemyDamage;

- int winner;

- CardBattle(CardActor player, CardActor enemy, CardDeck deck)
  - 构造战斗：默认每回合 3 能量、手牌上限 5、敌人每回合造成 7 点伤害；winner 为 -1 表示未分胜负。

- void Start()
  - 开始战斗：洗牌并进入第一个玩家回合。需在出牌前调用一次。

- void StartPlayerTurn()
  - 开始玩家回合：回合计数 +1、玩家格挡清零、能量回满，并从抽牌堆补牌到手牌上限；已分胜负时无操作。

- bool CanPlay(int handIndex)
  - 指定手牌索引当前能否打出：战斗已结束或索引越界返回 false；否则要求卡牌定义非 null 且费用不超过剩余能量。

- CardPlayResult Play(int handIndex)
  - 打出指定手牌：先扣能量，再依次结算卡牌全部效果（伤害加成玩家力量，Target 为 Enemy 的效果作用于敌人），最后弃掉该牌并检查胜负。不可打出时返回 Failed 结果（各字段为 0）。

- int EndPlayerTurn()
  - 结束玩家回合：弃掉全部手牌、玩家易伤递减、玩家受敌人回合伤害，随后敌人易伤递减；未分胜负则自动开始下一玩家回合。已分胜负返回 0，否则返回玩家本回合实际受到的伤害。

- void UpdateWinner()
  - 重算胜负：敌人死亡则玩家胜（0），否则玩家死亡则敌人胜（1）；
    双方都存活时不改变当前结果。由 Play/EndPlayerTurn 自动调用。

- CardActor Player()
  - 玩家单位。

- CardActor Enemy()
  - 敌人单位。

- CardDeck Deck()
  - 战斗使用的牌堆。

- int Energy()
  - 当前剩余能量。

- int MaxEnergy()
  - 每回合能量上限。

- int Turn()
  - 当前回合数，从 1 开始计数（Start 前/战斗结束后不再推进）。

- int EnemyDamage()
  - 敌人每回合对玩家造成的固定伤害。

- int Winner()
  - 胜负结果：-1 未分胜负，0 玩家胜，1 敌人胜。

- void SetEnergy(int maxEnergy)
  - 设置每回合能量上限：负值钳制为 0；在下一次 StartPlayerTurn 时生效。

- void SetHandSize(int handSize)
  - 设置手牌上限（补牌目标数）：负值钳制为 0。

- void SetEnemyDamage(int damage)
  - 设置敌人每回合对玩家造成的固定伤害：负值钳制为 0。


## CardCatalog (class)

卡牌目录：按 id 唯一收纳 CardDefinition 的注册表。重复
Add 同 id 的定义会覆盖旧定义。

- List<CardDefinition> cards;

- CardCatalog()
  - 构造空目录。

- CardCatalog Add(CardDefinition card)
  - 添加或覆盖卡牌定义：id 已存在时替换旧定义，否则追加；返回自身支持链式调用。

- int IndexOf(string id)
  - 按 id 查找定义的索引：找到返回其下标，未找到返回 -1。

- CardDefinition Find(string id)
  - 按 id 查找定义：未找到返回 null。

- int Count()
  - 目录中的定义总数。

- CardDefinition At(int index)
  - 取第 index 个定义；索引越界行为由 List 决定（调用方应保证 0 <= index < Count）。


## CardDeck (class)

三区牌堆运行时：管理抽牌堆、手牌、弃牌堆与消耗堆，抽空
自动回收弃牌堆洗入抽牌堆。实例 id 从 1 递增分配；随机源
由种子确定，可复现。

- CardZone draw;

- CardZone hand;

- CardZone discard;

- CardZone exhaust;

- DeterministicRandom random;

- int nextInstanceId;

- CardDeck(int seed)
  - 构造牌堆：seed 为随机源种子（同种子产生相同抽牌序列）；四个区域初始为空，下一个实例 id 为 1。

- CardInstance Add(CardDefinition definition)
  - 向抽牌堆添加一张卡并返回其实例：definition 为 null 返回 null 不产生实例；实例 id 自动递增分配。

- void ShuffleDraw()
  - 洗牌抽牌堆：使用牌堆自带的确定性随机源；在抽牌前调用一次以保证可复现。

- void RecycleDiscard()
  - 回收弃牌堆：把弃牌堆全部卡移回抽牌堆并重新洗牌。

- CardInstance DrawOne()
  - 抽一张牌到手牌：抽牌堆为空时先回收弃牌堆；仍无牌可抽返回 null（不消耗堆中其他状态）。

- int Draw(int amount)
  - 连续抽牌：在牌源耗尽时提前停止；返回实际抽到的张数（可小于 amount，包括 0；负数按 0 张处理）。

- CardInstance DiscardFromHand(int index)
  - 弃掉手牌指定索引的牌：成功返回该卡并移入弃牌堆；索引越界返回 null。

- CardInstance ExhaustFromHand(int index)
  - 消耗掉手牌指定索引的牌：成功返回该卡并移入消耗堆（本局不再循环）；索引越界返回 null。

- void DiscardHand()
  - 弃掉整手牌：Retained 为 true 的保留卡留在手中，其余按顺序移入弃牌堆。

- CardZone DrawPile()
  - 抽牌堆（直接访问可查看/清点，勿绕过 Draw 增删以免破坏流程）。

- CardZone Hand()
  - 手牌区域。

- CardZone DiscardPile()
  - 弃牌堆。

- CardZone ExhaustPile()
  - 消耗堆（被消耗的卡本局不再进入循环）。

- int RandomState()
  - 随机源当前状态，可用于存档/恢复随机序列。


## CardDefinition (class)

卡牌定义（模板）：不可变的基础属性 id/名称/费用/分类，
外加可链式追加的效果列表与可选美术资源。同一定义可实例化
出多张卡。

- string id;

- string name;

- int cost;

- string category;

- string art;

- List<CardEffect> effects;

- CardDefinition(string id, string name, int cost, string category)
  - 构造卡牌定义：id 为唯一标识，name 为显示名，cost 为打出费用（能量），category 为分类标签；效果列表初始为空，art 为空字符串。

- CardDefinition AddEffect(int kind, int amount, int target)
  - 追加一条效果并返回自身，支持链式调用；kind/target 取常量类值。

- CardDefinition SetArt(string resource)
  - 设置美术资源标识并返回自身，支持链式调用；传空字符串表示无美术。

- string Id()
  - 唯一标识符（构造时传入，不变）。

- string Name()
  - 显示名称。

- int Cost()
  - 打出费用（能量单位）。

- string Category()
  - 分类标签字符串。

- string Art()
  - 美术资源标识，未设置时为空字符串。

- int EffectCount()
  - 效果条数。

- CardEffect EffectAt(int index)
  - 取第 index 条效果；索引越界行为由 List 决定（调用方应保证 0 <= index < EffectCount）。


## CardEffect (class)

单条卡牌效果：由类型（kind）、数值（amount）与目标
（target）组成的最小效果单元，一张卡可含多条。

- int kind;

- int amount;

- int target;

- static CardEffect Of(int kind, int amount, int target)
  - 构造效果：kind 取 CardEffectKind 常量，target 取 CardTarget 常量，amount 为该效果的数值量（伤害/治疗/格挡/抽牌数/能量/回合数）。

- int Kind()
  - 效果类型标识（CardEffectKind 常量之一）。

- int Amount()
  - 效果数值量。

- int Target()
  - 效果目标（CardTarget 常量之一）。


## CardEffectKind (class)

卡牌效果类型常量：以静态方法返回整数标识，供效果构建与
战斗结算时比较。取值互不相同。

- static int Damage()
  - 伤害效果标识。

- static int Block()
  - 格挡效果标识。

- static int Heal()
  - 治疗效果标识。

- static int Draw()
  - 抽牌效果标识。

- static int Energy()
  - 能量增减效果标识。

- static int Strength()
  - 力量增减效果标识。

- static int Vulnerable()
  - 易伤效果标识。


## CardInstance (class)

卡牌实例：一次具体掉落/获得的卡，引用共享的 CardDefinition
并携带独立状态（实例 id、升级等级、保留标记）。instanceId
全局递增且不复用。

- int instanceId;

- CardDefinition definition;

- int upgrade;

- bool retained;

- CardInstance(int instanceId, CardDefinition definition)
  - 构造实例：instanceId 由牌堆分配（从 1 递增），definition 为共享模板；升级等级 0、保留标记 false。

- int InstanceId()
  - 实例唯一 id（牌堆内递增分配，不复用）。

- CardDefinition Definition()
  - 共享的卡牌定义模板。

- int Upgrade()
  - 升级等级，0 表示未升级。

- bool Retained()
  - 保留标记：为 true 时回合结束弃牌阶段不弃此牌。

- void SetUpgrade(int upgrade)
  - 设置升级等级（无钳制）。

- void SetRetained(bool retained)
  - 设置保留标记。


## CardPlayResult (class)

出牌结果的累计记录：一次 Play 的伤害、格挡、治疗、抽牌、
能量变化汇总。失败时各字段均为 0 且 Success 为 false。

- bool success;

- int damage;

- int blocked;

- int healed;

- int drawn;

- int energyDelta;

- static CardPlayResult Failed()
  - 构造失败结果：success 为 false，各数值字段为 0。

- void SetSuccess()
  - 标记本次出牌成功。

- void AddDamage(int amount)
  - 累加本次出牌造成的实际伤害总量。

- void AddBlock(int amount)
  - 累加本次出牌获得的格挡总量。

- void AddHeal(int amount)
  - 累加本次出牌的实际治疗总量（受上限钳制后的值）。

- void AddDraw(int amount)
  - 累加本次出牌实际抽到的牌数。

- void AddEnergy(int amount)
  - 累加本次出牌的能量净变化（可为负）。

- bool Success()
  - 出牌是否成功（费用足够且手牌索引合法）。

- int Damage()
  - 本次出牌造成的实际伤害总量（经易伤/格挡结算后）。

- int Blocked()
  - 本次出牌获得的格挡总量。

- int Healed()
  - 本次出牌的实际治疗总量。

- int Drawn()
  - 本次出牌实际抽到的牌数。

- int EnergyDelta()
  - 本次出牌的能量净变化（正为获得，负为额外消耗）。


## CardTarget (class)

卡牌效果目标常量：Self 作用于出牌者自身，Enemy 作用于对手。

- static int Self()
  - 目标为自身。

- static int Enemy()
  - 目标为敌人。


## CardZone (class)

卡牌区域（牌堆槽位）：有名字的有序卡牌集合，可作抽牌堆、
手牌、弃牌堆或消耗堆使用。null 卡不会被加入。

- string name;

- List<CardInstance> cards;

- CardZone(string name)
  - 构造区域：name 为区域显示名（如 "draw"/"hand"），初始为空。

- void Add(CardInstance card)
  - 将卡追加到区域末尾：null 卡被忽略。

- CardInstance TakeAt(int index)
  - 移除并返回指定索引的卡：索引越界返回 null 且不改动区域。

- CardInstance TakeLast()
  - 移除并返回最后一张卡：区域为空时返回 null。

- int IndexOfInstance(int instanceId)
  - 按实例 id 查找卡在区域中的索引：找到返回下标，未找到返回 -1。

- void Shuffle(DeterministicRandom random)
  - 原地洗牌（Fisher-Yates）：使用确定性随机源保证同种子同序列；random 为 null 时不洗牌。

- string Name()
  - 区域名称（构造时传入，不变）。

- int Count()
  - 区域内卡牌数。

- CardInstance At(int index)
  - 查看第 index 张卡（不移除）；索引越界行为由 List 决定（调用方应保证 0 <= index < Count）。

- void Clear()
  - 清空区域内全部卡牌。


## CardZoneManager (class)

卡牌容器与拖拽磁吸判定管理器。

- BoardSlot[]playerSlots;

- BoardSlot[]enemySlots;

- int slotCapacity;

- CardZoneManager(int maxSlots)

- static CardZoneManager Create(int maxSlots)

- void SetupPlayerSlot(int index, double x, double y, double w, double h)

- void SetupEnemySlot(int index, double x, double y, double w, double h)

- BoardSlot GetPlayerSlot(int index)

- int FindSnapPlayerSlot(double cardCenterX, double cardCenterY, double snapThresholdPx)
  - 寻找玩家拖拽卡牌时距离最近且合法的空置卡槽。
    snapThresholdPx: 磁吸感应门限像素。
    未命中或超出磁吸范围返回 -1。


## GameAction (class)

单个待演播动作节拍。

- ActionKind kind;

- int sourceId;

- int targetId;

- int value;

- double duration;

- GameAction(ActionKind kind, int src, int tgt, int val, double dur)

- static GameAction Create(ActionKind kind, int src, int tgt, int val, double dur)

- ActionKind Kind { get }

- int SourceId { get }

- int TargetId { get }

- int Value { get }

- double Duration { get }


## HandFanLayout (class)

手牌扇形弧度自然排布算法（类似《炉石传说》《杀戮尖塔》）。
解决卡牌平铺呆板重叠的问题，根据持牌张数自适应计算每张牌的位置、拱起弧度与旋转倾角。

- double arcRadius;

- double maxSpreadAngle;

- double cardSpacing;

- double hoverLift;

- double hoverScale;

- HandFanLayout(double radius, double maxAngle, double spacing)

- static HandFanLayout Create(double radius, double maxAngle, double spacing)

- void CalculateCardTransform(int index, int count, int hoveredIndex, double centerX, double bottomY, out double outX, out double outY, out double outRotRad, out double outScale)
  - 为拥有 count 张手牌的序列计算第 index 张牌的目标变换。
    输出：目标世界坐标 (outX, outY), 倾斜角弧度 outRotRad, 缩放比率 outScale。


## ActionKind (enum)

演播动作类型。

- PlayCardAnim

- DealDamage

- MinionDeath

- DrawCardAnim

- PauseWait = 战术性短暂停顿


## ZoneType (enum)

卡牌区域类型。

- Deck

- Hand

- Board

- Discard

- Exile = 放逐区
