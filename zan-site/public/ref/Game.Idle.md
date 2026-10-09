# Game.Idle

> 源码: `packages/Zan.Game/src/Game/Idle/BigNum.zan`, `packages/Zan.Game/src/Game/Idle/Curves.zan`, `packages/Zan.Game/src/Game/Idle/FloatingText.zan`, `packages/Zan.Game/src/Game/Idle/JuiceEffect.zan`, `packages/Zan.Game/src/Game/Idle/Offline.zan`, `packages/Zan.Game/src/Game/Idle/OfflineReward.zan`, `packages/Zan.Game/src/Game/Idle/Wallet.zan`


## BigNum (class)

- static string Suffix(int tier)

- static string Short(long v)

- static string Grouped(long v)

- static string GroupDigits(string digits)

- static double ToDouble(long v)


## Curves (class)

- static long CostNext(long baseCost, int ratePermille, int owned)

- static long CostBulk(long baseCost, int ratePermille, int owned, int count)

- static long MulPermille(long v, int p)

- static long RateAt(long unitRate, int owned, int prestigePermille)

- static long PowerCurve(long baseVal, int level, int expPermille)

- static long ClampLong(long v)


## FloatingTextItem (class)

- bool active;

- string text;

- double x;

- double y;

- double vx;

- double vy;

- double lifeTime;

- double maxLife;

- int color;

- double scale;

- bool isCrit;

- FloatingTextItem()

- static FloatingTextItem Create()

- bool Active { get }

- string Text { get }

- double X { get }

- double Y { get }

- double Scale { get }

- int Color { get }

- double Alpha { get }

- void Spawn(double startX, double startY, string text, int color, double scale, bool isCrit, double duration)

- void Update(double dt)


## FloatingTextManager (class)

- FloatingTextItem[]pool;

- int maxCount;

- FloatingTextManager(int capacity)

- static FloatingTextManager Create(int capacity)

- int ActiveCount { get }

- void SpawnDamage(double x, double y, long amount, bool isCrit)

- void SpawnGold(double x, double y, string formattedAmount)

- void Spawn(double x, double y, string text, int color, double scale, bool isCrit, double duration)

- void Update(double dt)

- void Render(Canvas c, Camera2D cam)


## Offline (class)

- static int CapDefault()

- static int ReportThreshold()

- class OfflineResult

- static OfflineResult Settle(int savedAt, int nowAt, int capSeconds)

- static OfflineResult Settle(int savedAt, int nowAt)

- static bool PreferStepping(int awaySeconds)


## OfflineRewardCalculator (class)

- double maxOfflineHours;

- double goldPerSecond;

- OfflineRewardCalculator(double maxHours, double ratePerSec)

- static OfflineRewardCalculator Create(double maxHours, double ratePerSec)

- double MaxOfflineHours{ get set}

- double GoldPerSecond{ get set}

- void CalculateReward(long lastLogoutUnixSec, long nowUnixSec, out long outElapsedSec, out long outCappedSec, out double outTotalGold)

- static string FormatDuration(long totalSeconds)


## SquashAndStretch (class)

- double scaleX;

- double scaleY;

- double velocityX;

- double velocityY;

- double stiffness;

- double damping;

- SquashAndStretch()

- static SquashAndStretch Create()

- double ScaleX { get }

- double ScaleY { get }

- void Trigger(double amount)

- void Update(double dt)


## Wallet (class)

- List<WalletSlot> slots;

- string changedKey;

- long changedValue;

- Wallet()

- bool Define(string key, long cap)

- int IndexOf(string key)

- long Amount(string key)

- long Cap(string key)

- long Add(string key, long v)

- bool TrySpend(string key, long v)

- bool TrySpendItems(List<WalletCostItem> items)

- bool TrySpendAll(List<string> keysReq, List<long> costs)

- void Fire(string key, long now)

- string ChangeKey()

- long ChangeValue()

- void ClearChange()

- string ToSaveText()

- void LoadSaveText(string s)


## WalletCostItem (class)

- public string key;

- public long cost;

- public WalletCostItem(string key, long cost)

- public static WalletCostItem Of(string key, long cost)


## WalletSlot (class)

- public string key;

- public long amount;

- public long cap;

- public WalletSlot(string key, long amount, long cap)
