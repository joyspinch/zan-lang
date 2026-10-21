# Game.Board

`Game.Board` provides deterministic grid, turn and command primitives:

- cloneable integer boards with stable state hashes;
- cardinal or diagonal breadth-first pathfinding;
- player/round/phase state;
- validated commands, snapshots and command logs for undo, replay and
  reconnect workflows.

Game-specific legality and win conditions implement `IBoardRules`. Hidden
information should be projected into player-specific views by the application
or server rather than stored in a client-visible board.

## AI search (alpha-beta)

`BoardSearch` runs a negamax alpha-beta search over any game that implements
`ISearchGame` — five actions (generate moves, apply, undo, evaluate, terminal)
and nothing else about the board:

```zan
using Game.Board;

class MyGame : ISearchGame {
    List<int> GenerateMoves() { ... }
    void ApplyMove(int move) { ... }
    void UndoMove() { ... }
    int Evaluate() { ... }   // from the side to move's view
    bool IsTerminal() { ... }
}

BoardSearch ai = new BoardSearch(4)
    .WithTimeLimit(300)                                  // ms budget
    .WithMoveOrder((int move) => Heuristic(move));        // good moves first
int best = ai.BestMove(game);
```

- `Evaluate()` is scored **from the side to move** (positive = good for the
  player about to move); negamax negates it when the turn switches, so a win
  is always `+Mate` / `-Mate` (`BoardSearch.Mate()` = 1000000).
- Iterative deepening: depths 1, 2, 3… If the time budget runs out mid-depth
  the search keeps the move from the last **completed** depth.
- The search always undoes what it applied, so the game state is unchanged
  after `BestMove` — including when it aborts on the time budget.
- `Nodes`, `CompletedDepth` and `LastScore` are exposed for tuning.
- Move ordering is the biggest lever: search the most promising moves first
  and the beta cut-off prunes far more branches.

### Mate scores: put the distance in, or the search takes the slow mate

The package's terminal score is a flat `±Mate` — it says *who* won, not *how
fast*. A search that can mate in one move and also mate in three therefore
sees both as equally good, and the tie is broken by move order, not by
distance. Games that care about speed (endgame drills, puzzles, any "mate
now" rule) should carry the distance in the score themselves:

```zan
// Mate sooner = higher score. plies = plies applied since the search root.
if (!myGeneralAlive) { return -(BoardSearch.Mate() - plies); }   // I lost
if (!theirGeneralAlive) { return BoardSearch.Mate() - plies; }   // I won
```

Two things follow from that:

- The game should own the terminal decision, including the "no legal moves"
  case. `BoardSearch` treats a move-less position as `-Mate` with no distance
  term, so if the game reports that case through `IsTerminal()` instead, every
  mate goes through `Evaluate()` and the score scale stays consistent.
- Games whose "no legal moves" *is* a loss (Chinese chess: no legal move means
  checkmate, there is no stalemate rule) must say so in `IsTerminal()`; the
  search has no other way to see it.

The Xiangqi project (`D:/project/games/xiangqi`, `client/src/XqSearch.zan`) is
the worked example: its adapter owns the terminal decision, carries the
distance-to-mate term, and caches the legal-move list so the terminal check
costs no extra search work.


## Go rules (围棋)

`GoBoard` implements the Go ruleset — placement, capture, suicide and ko
prohibitions, liberties, Chinese-style territory scoring — with **no rendering
dependency**, so the same rules run in a headless server:

```zan
using Game.Board;

GoBoard board = new GoBoard(19);
board.Play(3, 3, GoBoard.Black);         // returns captured stones (0 = none)
List<int> taken = new List<int>();
board.Play(15, 3, GoBoard.Black, taken);  // taken = captured coordinates
GoScore score = board.Score(7.5);        // Chinese rules, 7.5 komi
int winner = score.Winner();
```

- `Play` returns the number of captured stones, or **-1** for an illegal move
  (occupied point, ko point, suicide). An illegal move leaves the board and
  every counter untouched.
- `Probe` / `ProbeMove` answer "what if I play here" **without touching the
  state** — the standard question a Go AI asks. `ProbeMove` returns legality,
  captures, the resulting liberties and the resulting group size in one call.
- The board owns `KoPoint`, `ConsecutivePasses` and the capture counters.
  **Read them from the board instead of keeping a second copy in the game**:
  two sources of truth drift the moment a pass or a ko is handled in one place
  and not the other.
- Effects stay on the game side — capture animations are driven from the
  coordinate list `Play` hands back, not from a board that knows about pixels.

The Weiqi project (`D:/project/games/weiqi`, `client/src/WqGame.zan`) is the
worked example: its self-written `Group` / `TryPlay` / `TryPlayRaw` / `Probe` /
`Score` are gone, and the game only keeps the burst effect for captured
stones. The rules themselves are pinned by
`tests/conformance/game_go_board.zan` in the language repository.