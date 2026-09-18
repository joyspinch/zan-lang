# Zan game standard libraries

The game modules are intentionally layered:

```text
Game.Arpg
Game.Zgm

Game.Board ----+
Game.Cards ----+--> Game.Foundation
Game.Arcade2D -+
```

All game modules are platform-neutral: windowing, input and rendering live in
`Gui` (GuiHost / Canvas), and game code adapts onto them from its host loop —
the same pattern the shipped `templates/game/*` use.

`Game.Foundation` owns renderer-neutral fixed-step timing, deterministic random
state, semantic input and scene lifecycle.

`Game.Board` provides cloneable grids, pathfinding, turns, validated commands,
snapshots and replay logs for board games, tactics and puzzle games.

`Game.Cards` provides card catalogs, runtime instances, deterministic zones and
a compact deck-building battle runtime.

`Game.Arcade2D` provides geometry, collision queries, animation state, pooled
entities and path following for tower defense, auto-battlers and lightweight
arcade simulations.

`Game.Zgm` is ZanGameMaker's typed component model and renderer-neutral runtime
based on the capabilities described by the published DM3 documentation. It
includes project validation, lifecycle/events, maps, roles, items, skills,
buffs, windows, widgets, tweens, save snapshots, networking and SQLite. Its
public API and branding are entirely ZGM-owned.

`Game.Arpg` is a separate typed RPG runtime with validated project data, a live
world, combat, scheduling, graphical controls and data binding. It owns no
windowing or rendering of its own.
