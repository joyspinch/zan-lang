# Game.Board

> 源码: `packages/Zan.Game/src/Game/Board/Grid.zan`, `packages/Zan.Game/src/Game/Board/Match.zan`


## BoardCommand (class)

可序列化的玩家意图。Type、Value 与 Payload 的含义由规则决定
；标准字段涵盖网格移动及卡牌/技能指令。

- string type;

- int player;

- int fromX;

- int fromY;

- int toX;

- int toY;

- int amount;

- string payload;

- int sequence;

- static BoardCommand Place(int player, int x, int y, int cellValue)
  - 工厂：place 指令——把格子值 cellValue 放到 (x, y)。

- static BoardCommand Move(int player, int fromX, int fromY, int toX, int toY)
  - 工厂：move 指令——把棋子从 (fromX, fromY) 移到 (toX, toY)。

- static BoardCommand Action(string type, int player, int amount, string payload)
  - 工厂：自定义 action 指令——type 为规则层自定的类型标签，
    携带数值 amount 与字符串 payload，不含坐标。

- BoardCommand(string type, int player, int fromX, int fromY, int toX, int toY, int amount, string payload)

- string Type()
  - 指令类型标签（"place"/"move" 或 Action 传入的自定义类型）。

- int Player()
  - 发起指令的玩家编号。

- int FromX()
  - 起点 X（place/action 类为 -1）。

- int FromY()
  - 起点 Y（place/action 类为 -1）。

- int ToX()
  - 终点 X（action 类为 -1）。

- int ToY()
  - 终点 Y（action 类为 -1）。

- int Value()
  - 数值参数（place 为格子值，action 为 amount）。

- string Payload()
  - 附加字符串（place/move 为空串）。

- int Sequence()
  - 应用顺序号（由 BoardMatch.TryApply 分配，从 1 递增）。

- void SetSequence(int sequence)
  - 由对局在应用成功时回填顺序号；外部一般不应调用。


## BoardCommandLog (class)

回放日志：按应用顺序保存指令与应用后的状态哈希，
供确定性回放与回滚（Restore 用 Truncate 截断）。

- List<BoardReplayEntry> entries;

- BoardCommandLog()

- void Add(BoardCommand command, int stateHash)
  - 追加一条记录（由 BoardMatch.TryApply 在指令生效后调用）。

- void Truncate(int count)
  - 截断到只保留前 count 条（count 为负按 0 处理）；
    用于 Restore 回滚时丢弃之后的历史。

- int Count()
  - 记录条数。

- BoardReplayEntry At(int index)
  - 第 index 条记录；越界行为由底层 List 决定（抛错）。

- void Clear()
  - 清空全部记录。


## BoardMatch (class)

回合制棋盘对局核心：持有棋盘、回合状态、规则与指令日志。
指令必须来自当前玩家并通过规则校验才会生效，生效后
自动编号并记录（含状态哈希）供回放/回滚。

- GridBoard board;

- TurnState turn;

- IBoardRules rules;

- BoardCommandLog log;

- int sequence;

- BoardMatch(GridBoard board, int playerCount, IBoardRules rules)

- bool TryApply(BoardCommand command)
  - 尝试应用一条指令：对局未结束、指令来自当前玩家、规则
    Validate 与 Apply 全部通过才生效。生效后指令顺序号 +1 并
    写回指令、连同状态哈希记入日志；任一条件不满足返回 false
    且不修改任何状态。

- BoardSnapshot Snapshot()
  - 创建当前对局的完整快照（棋盘与回合状态深拷贝）。

- void Restore(BoardSnapshot snapshot)
  - 恢复到快照时的状态（棋盘/回合/顺序号均为深拷贝回填），
    并把指令日志截断到该顺序号，丢弃之后的历史；snapshot 为
    null 时无操作。

- int Hash()
  - 对局状态哈希：棋盘哈希混合当前玩家、回合数与指令顺序号
    （乘 31 累加）。重放指令后哈希应一致，可用于回放校验。

- GridBoard Board()
  - 当前棋盘（直接引用，修改会改变对局状态）。

- TurnState Turn()
  - 当前回合状态（直接引用）。

- BoardCommandLog Log()
  - 指令日志（可 Truncate/Clear，慎用）。

- int Sequence()
  - 当前指令顺序号（成功应用的指令数）。


## BoardReplayEntry (class)

回放日志的单条记录：指令本身 + 应用后的对局状态哈希，
用于确定性回放与校验（重放后哈希应一致）。

- BoardCommand command;

- int stateHash;

- BoardReplayEntry(BoardCommand command, int stateHash)

- BoardCommand Command()
  - 本次记录的指令。

- int StateHash()
  - 指令应用后的对局状态哈希（BoardMatch.Hash 的返回值）。


## BoardSnapshot (class)

某一指令序号处的对局完整快照：棋盘与回合状态均为深拷贝，
用于悔棋/回滚；由 BoardMatch.Snapshot 创建。

- GridBoard board;

- TurnState turn;

- int sequence;

- BoardSnapshot(GridBoard board, TurnState turn, int sequence)

- GridBoard Board()
  - 快照时的棋盘（只读视图，修改它不影响原对局）。

- TurnState Turn()
  - 快照时的回合状态。

- int Sequence()
  - 快照时的指令顺序号。


## GridBoard (class)

整数网格，供棋类、战棋、推箱子类解谜及
确定性地图模拟共用。

- int width;

- int height;

- List<int> cells;

- GridBoard(int width, int height, int initialValue)

- bool Inside(int x, int y)
  - 坐标 (x, y) 是否位于网格内。

- int Index(int x, int y)
  - 行优先展开的一维索引（= y * Width + x）；越界坐标会产生越界索引。

- int Get(int x, int y)
  - 读取格子值；坐标越界时返回 0（而非报错），
    因此 0 不应被用作需要区分"格外"的语义值。

- bool Set(int x, int y, int cellValue)
  - 写入格子值；坐标越界时返回 false 不修改，成功返回 true。

- void Fill(int cellValue)
  - 将全部格子置为 cellValue。

- int CountValue(int cellValue)
  - 统计值等于 cellValue 的格子数。

- GridBoard Clone()
  - 深拷贝当前网格（尺寸与全部格子值独立，互不影响）。

- int Hash()
  - 确定性状态哈希（FNV 风格：异或后乘大质数，依次混入宽、
    高与全部格子值）。相同内容恒得相同哈希，供回放校验。

- int Width()
  - 列数。

- int Height()
  - 行数。

- int CellCount()
  - 格子总数（= Width * Height）。

- int CellAt(int index)
  - 按行优先一维索引直接读格子（配合 Index 使用）；越界行为由底层 List 决定（抛错）。


## GridPath (class)

寻路结果：格子坐标点序列（起点到终点，含两端）。
通过 Found 区分"找到路径"与"不可达/参数非法"（二者都是空路径）。

- List<GridPoint> points;

- static GridPath Empty()
  - 创建空路径（Found 为 false）。

- void Add(int x, int y)
  - 追加一个路径点（由寻路器按起点到终点顺序调用）。

- int Count()
  - 路径点数（0 表示空路径）。

- GridPoint At(int index)
  - 第 index 个路径点；越界行为由底层 List 决定（抛错）。

- bool Found()
  - 是否找到路径（路径非空；不可达与参数非法均为空路径）。


## GridPathfinder (class)

网格寻路器：广度优先搜索，按步数最短（未加权网格）求一条
从起点到终点的路径；格子值等于 blockedValue 的格子视为障碍。

- static GridPath Find(GridBoard board, int startX, int startY, int targetX, int targetY, int blockedValue, bool allowDiagonal)
  - 在 board 上寻找 (startX, startY) 到 (targetX, targetY) 的路径：
    allowDiagonal 为 true 时允许 8 方向，否则仅 4 方向；终点本身
    为障碍值时视为不可达。board 为 null、起终点越界或不可达时
    返回空路径（Found 为 false）；找到时路径含起点与终点两端。


## GridPoint (class)

整数格点（寻路结果中的坐标单元）。

- int x;

- int y;

- static GridPoint At(int x, int y)
  - 创建格点 (x, y)。

- int X()
  - 格点 X 坐标（列）。

- int Y()
  - 格点 Y 坐标（行）。


## TurnState (class)

回合状态机：轮转当前玩家与回合数，维护阶段字符串与胜负。
玩家编号从 0 起，round 从 1 起（每位玩家各行动一次为一轮）。

- int playerCount;

- int currentPlayer;

- int round;

- string phase;

- bool finished;

- int winner;

- TurnState(int playerCount)

- void NextPlayer()
  - 轮转到下一位玩家；wrap 到 0 时回合数 +1。对局已结束后无操作。

- void Finish(int winner)
  - 结束对局：置 finished、记录胜者并把阶段改为 "finished"。

- TurnState Clone()
  - 深拷贝当前回合状态（string 阶段字段为不可变引用拷贝）。

- int PlayerCount()
  - 玩家总数。

- int CurrentPlayer()
  - 当前行动的玩家编号（0 起）。

- int Round()
  - 当前回合数（从 1 开始，每位玩家各行动一次为一轮）。

- string Phase()
  - 阶段字符串（初始 "play"，结束后 "finished"，其余由规则层自定义）。

- bool Finished()
  - 对局是否已结束。

- int Winner()
  - 胜者玩家编号（未结束时 -1）。

- void SetPhase(string phase)
  - 覆写阶段字符串（任意值，不做校验）。

- void SetCurrentPlayer(int player)
  - 手动指定当前玩家；越界编号被忽略（不钳制）。


## IBoardRules (interface)

规则接口：规则层实现校验与应用，对局核心只依赖此接口，
使同一对局框架可承载不同棋类玩法。

- bool Validate(BoardMatch match, BoardCommand command);
  - 指令是否合法（不修改状态；应校验玩家/坐标/阶段等）。

- bool Apply(BoardMatch match, BoardCommand command);
  - 应用指令并修改对局状态；返回 false 表示应用失败（指令不入日志）。
