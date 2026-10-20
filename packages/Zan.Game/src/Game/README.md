# Zan game standard libraries

The game modules are intentionally layered:

```text
Game.Arpg
Game.Zgm

Game.Board ----+
Game.Cards ----+--> Game.Foundation
Game.Arcade2D -+

Game.Idle (idle/incremental mechanics: offline rewards, wallet, state machine)

Game.Factory (separate package: Zan.Game.Factory)
```

Sibling packages built on the same base: `Zan.Game.Ecs` (entity-component-system),
`Zan.Game.Physics2D` (rigid bodies, joints), `Zan.Game.Pathfinding` (A*, JPS, navmesh),
`Zan.Game.Particles`, `Zan.Game.Procgen` (WFC), `Zan.Game.Assets`, `Zan.Game.Audio`,
`Zan.Game.Animation` (3D skeletons, skinning, clips, state machine, two-bone IK),
`Zan.Game.Factory` (factory-automation + tower-defense simulation, no GUI).

All game modules are platform-neutral: windowing, input and rendering live in
`Gui` (GuiHost / Canvas), and game code adapts onto them from its host loop —
the same pattern the shipped `templates/game/*` use.

`Game.Foundation` owns renderer-neutral fixed-step timing, deterministic random
state, semantic input and scene lifecycle.

`Game.Board` provides cloneable grids, pathfinding, turns, validated commands,
snapshots and replay logs for board games, tactics and puzzle games, plus a
generic alpha-beta AI search (`BoardSearch` + `ISearchGame`) that plugs into
any game exposing moves/apply/undo/evaluate.

`Game.Cards` provides card catalogs, runtime instances, deterministic zones, a
compact deck-building battle runtime, and Chinese-style hand-shape rules
(`ComboType` / `Combo` / `ComboRuleSet` / `ComboRules`: recognise, compare and
enumerate the legal replies for single/pair/triple/straight/bomb hands).

`Game.Arcade2D` provides geometry, collision queries, animation state, pooled
entities and path following for tower defense, auto-battlers and lightweight
arcade simulations.

`Game.Idle` provides the mechanics of idle/incremental games: offline reward
settlement, wallet slots and costs, floating text items, and the core state
machine (`IdleState` / `IdleBuilding` — click power, cost multiplier curve,
per-second output, fixed-step accrual). The catalog of what the player builds
stays with the game; only the rules that hold for every idle game live here.

`Game.Zgm` is ZanGameMaker's typed component model and renderer-neutral runtime
based on the capabilities described by the published DM3 documentation. It
includes project validation, lifecycle/events, maps, roles, items, skills,
buffs, windows, widgets, tweens, save snapshots, networking and SQLite. Its
public API and branding are entirely ZGM-owned.

`Game.Arpg` is a separate typed RPG runtime with validated project data, a live
world, combat, scheduling, graphical controls and data binding. It owns no
windowing or rendering of its own.
