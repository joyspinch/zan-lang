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
