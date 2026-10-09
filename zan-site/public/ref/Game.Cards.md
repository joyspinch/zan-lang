# Game.Cards

> 源码: `packages/Zan.Game/src/Game/Cards/ActionQueue.zan`, `packages/Zan.Game/src/Game/Cards/Battle.zan`, `packages/Zan.Game/src/Game/Cards/CardZone.zan`, `packages/Zan.Game/src/Game/Cards/Cards.zan`, `packages/Zan.Game/src/Game/Cards/HandFanLayout.zan`


## ActionQueue (class)

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

- void Update(double dt, System.Linq.Action<GameAction> onActionStart, System.Linq.Action<GameAction> onActionFinish)

- void Clear()


## BoardSlot (class)

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

- double DistanceSqToCenter(double px, double py)


## CardActor (class)

- string id;

- int hp;

- int maxHp;

- int block;

- int strength;

- int vulnerable;

- CardActor(string id, int maxHp)

- int TakeDamage(int amount)

- int Heal(int amount)

- void AddBlock(int amount)

- void AddStrength(int amount)

- void AddVulnerable(int turns)

- void StartTurn()

- void EndTurn()

- string Id()

- int Hp()

- int MaxHp()

- int Block()

- int Strength()

- int Vulnerable()

- bool Alive()


## CardBattle (class)

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

- void Start()

- void StartPlayerTurn()

- bool CanPlay(int handIndex)

- CardPlayResult Play(int handIndex)

- int EndPlayerTurn()

- void UpdateWinner()

- CardActor Player()

- CardActor Enemy()

- CardDeck Deck()

- int Energy()

- int MaxEnergy()

- int Turn()

- int EnemyDamage()

- int Winner()

- void SetEnergy(int maxEnergy)

- void SetHandSize(int handSize)

- void SetEnemyDamage(int damage)


## CardCatalog (class)

- List<CardDefinition> cards;

- CardCatalog()

- CardCatalog Add(CardDefinition card)

- int IndexOf(string id)

- CardDefinition Find(string id)

- int Count()

- CardDefinition At(int index)


## CardDeck (class)

- CardZone draw;

- CardZone hand;

- CardZone discard;

- CardZone exhaust;

- DeterministicRandom random;

- int nextInstanceId;

- CardDeck(int seed)

- CardInstance Add(CardDefinition definition)

- void ShuffleDraw()

- void RecycleDiscard()

- CardInstance DrawOne()

- int Draw(int amount)

- CardInstance DiscardFromHand(int index)

- CardInstance ExhaustFromHand(int index)

- void DiscardHand()

- CardZone DrawPile()

- CardZone Hand()

- CardZone DiscardPile()

- CardZone ExhaustPile()

- int RandomState()


## CardDefinition (class)

- string id;

- string name;

- int cost;

- string category;

- string art;

- List<CardEffect> effects;

- CardDefinition(string id, string name, int cost, string category)

- CardDefinition AddEffect(int kind, int amount, int target)

- CardDefinition SetArt(string resource)

- string Id()

- string Name()

- int Cost()

- string Category()

- string Art()

- int EffectCount()

- CardEffect EffectAt(int index)


## CardEffect (class)

- int kind;

- int amount;

- int target;

- static CardEffect Of(int kind, int amount, int target)

- int Kind()

- int Amount()

- int Target()


## CardEffectKind (class)

- static int Damage()

- static int Block()

- static int Heal()

- static int Draw()

- static int Energy()

- static int Strength()

- static int Vulnerable()


## CardInstance (class)

- int instanceId;

- CardDefinition definition;

- int upgrade;

- bool retained;

- CardInstance(int instanceId, CardDefinition definition)

- int InstanceId()

- CardDefinition Definition()

- int Upgrade()

- bool Retained()

- void SetUpgrade(int upgrade)

- void SetRetained(bool retained)


## CardPlayResult (class)

- bool success;

- int damage;

- int blocked;

- int healed;

- int drawn;

- int energyDelta;

- static CardPlayResult Failed()

- void SetSuccess()

- void AddDamage(int amount)

- void AddBlock(int amount)

- void AddHeal(int amount)

- void AddDraw(int amount)

- void AddEnergy(int amount)

- bool Success()

- int Damage()

- int Blocked()

- int Healed()

- int Drawn()

- int EnergyDelta()


## CardTarget (class)

- static int Self()

- static int Enemy()


## CardZone (class)

- string name;

- List<CardInstance> cards;

- CardZone(string name)

- void Add(CardInstance card)

- CardInstance TakeAt(int index)

- CardInstance TakeLast()

- int IndexOfInstance(int instanceId)

- void Shuffle(DeterministicRandom random)

- string Name()

- int Count()

- CardInstance At(int index)

- void Clear()


## CardZoneManager (class)

- BoardSlot[]playerSlots;

- BoardSlot[]enemySlots;

- int slotCapacity;

- CardZoneManager(int maxSlots)

- static CardZoneManager Create(int maxSlots)

- void SetupPlayerSlot(int index, double x, double y, double w, double h)

- void SetupEnemySlot(int index, double x, double y, double w, double h)

- BoardSlot GetPlayerSlot(int index)

- int FindSnapPlayerSlot(double cardCenterX, double cardCenterY, double snapThresholdPx)


## GameAction (class)

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

- double arcRadius;

- double maxSpreadAngle;

- double cardSpacing;

- double hoverLift;

- double hoverScale;

- HandFanLayout(double radius, double maxAngle, double spacing)

- static HandFanLayout Create(double radius, double maxAngle, double spacing)

- void CalculateCardTransform(int index, int count, int hoveredIndex, double centerX, double bottomY, out double outX, out double outY, out double outRotRad, out double outScale)


## ActionKind (enum)

- PlayCardAnim

- DealDamage

- MinionDeath

- DrawCardAnim

- PauseWait = 战术性短暂停顿


## ZoneType (enum)

- Deck

- Hand

- Board

- Discard

- Exile = 放逐区
