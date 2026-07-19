# Zan game standard libraries

The game modules are intentionally layered:

```text
Game.Rts ------+
Game.Arpg -----+--> SDL3 --> zan_sdl3 --> SDL3
Game.Zgm

Game.Board ----+
Game.Cards ----+--> Game.Foundation
Game.Arcade2D -+
```

`Game.Foundation` owns renderer-neutral fixed-step timing, deterministic random
state, semantic input and scene lifecycle. `Game.Foundation.Sdl` provides the
optional SDL application host.

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
world, combat, scheduling, graphical controls, data binding and SDL rendering.

`SDL3` owns platform windows, rendering, input and timing. Game modules own
domain concepts and do not expose raw SDL handles through gameplay APIs.

`Game.Rts` is a clean-room compatibility engine for user-owned RA2/Yuri assets.
