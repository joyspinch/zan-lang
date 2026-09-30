# Game.Idle

> 源码: `packages/Zan.Game/src/Game/Idle/BigNum.zan`, `packages/Zan.Game/src/Game/Idle/Curves.zan`, `packages/Zan.Game/src/Game/Idle/FloatingText.zan`, `packages/Zan.Game/src/Game/Idle/JuiceEffect.zan`, `packages/Zan.Game/src/Game/Idle/Offline.zan`, `packages/Zan.Game/src/Game/Idle/OfflineReward.zan`, `packages/Zan.Game/src/Game/Idle/Wallet.zan`


## BigNum (class)

放置类大数：以 long 原始值（通常"放大 1000 倍"的定点
或单纯金币计数）驱动，显示层换算成 1.2K / 3.4M / 5.6B / 7.8T /
9.0Qa 的科学后缀——长线放置超过 int 上限是必然事件，legend 模板
`Rules.Bounded(1e9)` 那种钳制只是权宜。数值本体保持 long（约
±9.2e18，够绝大多数单机放置跑到 prestige 之后），格式化在这里
集中，模板与 UI 控件统一取文本。

- static string Suffix(int tier)
  - 后缀阶梯：千进制，K M B T Qa Qi Sx Sp Oc No Dc。

- static string Short(long v)
  - 值 → 展示文本（千进制后缀，1 位小数，整数段 1..3 位）。
    负数带符号；|v| < 1000 原样整数。ex: 999 -> "999"，
    1234 -> "1.2K"，5678000000 -> "5.7B"。

- static string Grouped(long v)
  - 值 → 带千分位分组的完整文本（短数值、结算面板/审计日志用）。
    1234567 -> "1,234,567"。

- static string GroupDigits(string digits)
  - 纯数字串按 3 位分组（无符号，调用方已保证形状）。

- static double ToDouble(long v)
  - 当前值换算成 double（用于与公式库互操作；>2^53 精度自然损失，
    显示层容忍）。


## Curves (class)

放置类数值曲线：生产/成本/收益三件套的标准公式库。
成本走 `cost = base * rate^(owned - baseOwned)`（指数，买 N 个后
下一个的价格），产出走 `rate * owned * prestigeMul`，等级收益走
幂律。全部 long 运算（千分位定点，除法带四舍五入），与 BigNum/
资源容器同单位。曲线参数一旦发布不可改——会破坏存量存档的
购买力，改数值只能加新参数并做版本迁移。

- static long CostNext(long baseCost, int ratePermille, int owned)
  - 指数成本：第 `owned`（已拥有数，从 0 起）件之后的下一件价格。
    cost = base * rate^owned，rate 千分比（1150 = ×1.15）。
    乘方用循环乘，`owned` 超过 64 时按 64 截断（rate>=1050 时
    价格早已溢出 long，截断点由 ClampLong 收口）。

- static long CostBulk(long baseCost, int ratePermille, int owned, int count)
  - 从 `owned` 起、连买 `count` 件的总价（批量购买面板用）。
    等比求和的整数近似：逐件累加（count 上限 1000，防长循环）。

- static long MulPermille(long v, int p)
  - 千分比乘法：(v * p) / 1000，四舍五入、long 钳制。

- static long RateAt(long unitRate, int owned, int prestigePermille)
  - 产出速率：`unitRate * owned`（千分位/秒），乘 prestige 倍率。

- static long PowerCurve(long baseVal, int level, int expPermille)
  - 等级幂律属性：`base * (level ^ exp)` 的千分位定点近似——
    逐级累乘逼近（level^exp 用循环平方根因式避免 double 漂移，
    精度需求放置类足够）。exp 千分比（1500 = 1.5 次幂）。

- static long ClampLong(long v)
  - long 饱和钳制（正向上限，溢出停在 MaxValue 而不是变负——
    放置数值炸表时宁可"卡住"也不能返 0 触发免费购买 bug）。


## FloatingTextItem (class)

单个动态跳字粒子。

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

放置游戏/ARPG 高性能飘字管理器（零 GC 对象池）。
支持伤害跳字、暴击金黄放大、金币产出喷涌与经验获取飘字。

- FloatingTextItem[]pool;

- int maxCount;

- FloatingTextManager(int capacity)

- static FloatingTextManager Create(int capacity)

- int ActiveCount { get }

- void SpawnDamage(double x, double y, long amount, bool isCrit)
  - 产生伤害飘字。

- void SpawnGold(double x, double y, string formattedAmount)
  - 产生金币产出飘字。

- void Spawn(double x, double y, string text, int color, double scale, bool isCrit, double duration)
  - 底层生成跳字。

- void Update(double dt)
  - 时钟推进。

- void Render(Canvas c, Camera2D cam)
  - 极速渲染所有活跃跳字。


## Offline (class)

离线收益结算：放置类"回来时有事发生"的标准算法。
输入上次存档时间戳与每秒产出速率，按封顶时长折算收益；同时
给出"应展示"的判定（离线久才弹结算面板，1 分钟内不烦人）。
时间源由调用方注入（客户端用本地时钟，服务端权威用
WebApp.NowSeconds），本库不做 IO。legend/wuwei 模板各有一份
手写等价物（24h 封顶、逐秒步进），这里是抽干后的公共形状。

- static int CapDefault()
  - 缺省封顶：24 小时（秒）。长线放置可放宽到 7 天，但必须有——
    无封顶的离线收益会让"回来"变成"爆炸"。

- static int ReportThreshold()
  - 最短弹窗时长（秒）：低于它静默入账。

- class OfflineResult
  - 结算结果：实际入账秒数（封顶后）与原始离线秒数。
    earnings 由调用方按 RateAt 自乘——本库只管时间账。

- static OfflineResult Settle(int savedAt, int nowAt, int capSeconds)
  - 折算离线时长。`savedAt`/`nowAt` 同一时区/纪元的秒；时钟被
    拨回（now < savedAt）视为 0 而不是负数——防"倒计时送钱"。

- static OfflineResult Settle(int savedAt, int nowAt)
  - 便捷重载：用缺省 24h 封顶。

- static bool PreferStepping(int awaySeconds)
  - 逐秒步进还是闭式一次入账：离线 ≤ threshold 秒走逐秒（与
    在线手感一致，冷却/任务计时同步走），更长走闭式（性能）。
    返回 true 时调用方应循环 Step()，false 直接乘。


## OfflineRewardCalculator (class)

放置游戏离线挂机收益计算与结算驱动器。

- double maxOfflineHours;

- double goldPerSecond;

- OfflineRewardCalculator(double maxHours, double ratePerSec)

- static OfflineRewardCalculator Create(double maxHours, double ratePerSec)

- double MaxOfflineHours{ get set}

- double GoldPerSecond{ get set}

- void CalculateReward(long lastLogoutUnixSec, long nowUnixSec, out long outElapsedSec, out long outCappedSec, out double outTotalGold)
  - 计算离线时间与金币收益结算结果。

- static string FormatDuration(long totalSeconds)
  - 将秒数格式化为人类可读的挂机时长文本（例如 "05时24分"）。


## SquashAndStretch (class)

游戏爽快感打击动效（Juice Effects）：
包含点击果冻缩放（Squash & Stretch）弹簧阻尼振荡器。

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
  - 触发一次挤压打击或点击（例如 X 变宽 1.25，Y 被压扁 0.75）。

- void Update(double dt)
  - 弹簧阻尼物理步进更新（内部自动子步切分保证数值稳定性）。


## Wallet (class)

多资源容器：放置类"金币/宝石/素材…"的通用账本。
每种资源一个槽（名字→余额+上限），Add/TrySpend 原子增减，
变更走事件（UI 进度条/按钮可用性订阅）。余额 long，钳在
[0, cap]——放置数值只涨不缩，溢出停在 cap（与 Curves.ClampLong
同一策略：卡住而不是回绕出负余额）。名字区分大小写，槽序即
插入序（UI 按创建顺序画进度条）。

- List<WalletSlot> slots;

- string changedKey;
  - 变更快照：最近一次变动的键与新余额（UI 每帧轮询对账，
    放置结算一秒可触发几百次，内联事件反而烧帧——宿主自己
    节流）。changedKey 为空串表示本帧无变更。

- long changedValue;

- Wallet()

- bool Define(string key, long cap)
  - 注册一个资源槽（重复注册返回 false 不覆盖）。

- int IndexOf(string key)
  - 槽下标；未注册返回 -1。

- long Amount(string key)

- long Cap(string key)

- long Add(string key, long v)
  - 入账 `v`（可为负），返回实际入账量（被 cap 吃掉的部分不计）。
    未注册的键拒收。v<0 时余额不会穿过 0。

- bool TrySpend(string key, long v)
  - 消费：余额足够才扣（放置购买的经典形状——不透支），
    成功返回 true。

- bool TrySpendAll(List<string> keysReq, List<long> costs)
  - `costs` 全部可负担时一次扣清（多资源打包购买）；任一不足
    则整体不动。costs 是 key→数量成对的奇偶序列。

- void Fire(string key, long now)

- string ChangeKey()
  - 最近一次变更（轮询后用 TakeChange 取走并清零）。

- long ChangeValue()

- void ClearChange()

- string ToSaveText()
  - 序列化：两行 JSON 兼容的 key|amount|cap 文本（存档用，
    与引擎无关——放置存档里只需要余额，cap 由代码重定义）。

- void LoadSaveText(string s)
  - 从 ToSaveText 恢复余额；cap 沿用当前 Define 的值（版本迁移
    点：上调 cap 不需要动存档）。未知键忽略（老存档的废弃资源
    自然蒸发），缺失键保持 0。


## WalletSlot (class)

- public string key;

- public long amount;

- public long cap;

- public WalletSlot(string key, long amount, long cap)
