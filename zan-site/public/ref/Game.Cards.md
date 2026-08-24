# Game.Cards

> 源码: `stdlib/Game/Cards/Battle.zan`, `stdlib/Game/Cards/Cards.zan`


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
