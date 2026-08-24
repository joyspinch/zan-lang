# Game.Board

> 源码: `stdlib/Game/Board/Grid.zan`, `stdlib/Game/Board/Match.zan`


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

- static BoardCommand Move(int player, int fromX, int fromY, int toX, int toY)

- static BoardCommand Action(string type, int player, int amount, string payload)

- BoardCommand(string type, int player, int fromX, int fromY, int toX, int toY, int amount, string payload)

- string Type()

- int Player()

- int FromX()

- int FromY()

- int ToX()

- int ToY()

- int Value()

- string Payload()

- int Sequence()

- void SetSequence(int sequence)


## BoardCommandLog (class)

- List<BoardReplayEntry> entries;

- BoardCommandLog()

- void Add(BoardCommand command, int stateHash)

- void Truncate(int count)

- int Count()

- BoardReplayEntry At(int index)

- void Clear()


## BoardMatch (class)

- GridBoard board;

- TurnState turn;

- IBoardRules rules;

- BoardCommandLog log;

- int sequence;

- BoardMatch(GridBoard board, int playerCount, IBoardRules rules)

- bool TryApply(BoardCommand command)

- BoardSnapshot Snapshot()

- void Restore(BoardSnapshot snapshot)

- int Hash()

- GridBoard Board()

- TurnState Turn()

- BoardCommandLog Log()

- int Sequence()


## BoardReplayEntry (class)

- BoardCommand command;

- int stateHash;

- BoardReplayEntry(BoardCommand command, int stateHash)

- BoardCommand Command()

- int StateHash()


## BoardSnapshot (class)

- GridBoard board;

- TurnState turn;

- int sequence;

- BoardSnapshot(GridBoard board, TurnState turn, int sequence)

- GridBoard Board()

- TurnState Turn()

- int Sequence()


## GridBoard (class)

整数网格，供棋类、战棋、推箱子类解谜及
确定性地图模拟共用。

- int width;

- int height;

- List<int> cells;

- GridBoard(int width, int height, int initialValue)

- bool Inside(int x, int y)

- int Index(int x, int y)

- int Get(int x, int y)

- bool Set(int x, int y, int cellValue)

- void Fill(int cellValue)

- int CountValue(int cellValue)

- GridBoard Clone()

- int Hash()

- int Width()

- int Height()

- int CellCount()

- int CellAt(int index)


## GridPath (class)

- List<GridPoint> points;

- static GridPath Empty()

- void Add(int x, int y)

- int Count()

- GridPoint At(int index)

- bool Found()


## GridPathfinder (class)

- static GridPath Find(GridBoard board, int startX, int startY, int targetX, int targetY, int blockedValue, bool allowDiagonal)


## GridPoint (class)

- int x;

- int y;

- static GridPoint At(int x, int y)

- int X()

- int Y()


## TurnState (class)

- int playerCount;

- int currentPlayer;

- int round;

- string phase;

- bool finished;

- int winner;

- TurnState(int playerCount)

- void NextPlayer()

- void Finish(int winner)

- TurnState Clone()

- int PlayerCount()

- int CurrentPlayer()

- int Round()

- string Phase()

- bool Finished()

- int Winner()

- void SetPhase(string phase)

- void SetCurrentPlayer(int player)


## IBoardRules (interface)

- bool Validate(BoardMatch match, BoardCommand command);

- bool Apply(BoardMatch match, BoardCommand command);
