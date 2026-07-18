# Game.Zgm capability scope

`Game.Zgm` is ZanGameMaker's typed component and runtime standard library. The
published DM3 documentation is used as a functional reference, while the
namespace, types, validation rules, message framing and runtime implementation
belong to ZanGameMaker.

DM3 is a capability reference, not a compatibility contract. ZGM does not aim
to reproduce every DM3/Lua file, name or API one-to-one. The primary project
format is strongly typed Zan code, and a capability is included when it is
useful to ZanGameMaker applications, headless servers or tools.

## Component model

| Documented area | ZGM API |
| --- | --- |
| Application settings, resources and extensions | `AppConfig`, `ZgmResource`, `ZgmAttr` |
| Maps, cells, objects, spawns, portals, lights, viewport and pathfinding | `MapConfig`, `MapCell`, `MapObject`, `MapSpawn`, `MapPortal`, `MapLight`, `MapViewport`, `ZgmPathfinder` |
| Roles, appearances, equipment, equipment sets, drops, initial skills and AI behavior | `NpcConfig`, `Appearance`, `EquipSlot`, `EquipSet`, `DropEntry` |
| Items, equipment attributes, cooldowns and resources | `ItemConfig` |
| Skills, actor/cell targeting, area hits, formulas, delayed damage, resource costs, hurt effects and status effects | `SkillConfig`, `SkillTarget`, `HurtEffect` |
| Status duration, triggers, restrictions and modifiers | `BuffConfig` |
| Level growth | `LevelGrowth` |
| Windows, controls and custom nodes | `WindowConfig`, widget types in `Widget.zan`, node types in `Node.zan` |
| Rich text, template variables and tween timelines | `RichText`, `TemplateVars`, `Tween` |
| Prefabs and skins | `PrefabConfig`, `Skin`, `SkinLibrary`, `ZgmPrefabInstance` |
| TCP, WebSocket, server and SQLite components | `TcpClientConfig`, `WebSocketClientConfig`, `ServerConfig`, `SqliteComponentConfig` |

The control set includes labels, rich-text boxes, buttons, image and animation
boxes, progress bars, edit boxes, sliders, role displays, item/skill/quick/bag
slots, map boxes, check boxes, containers, scrollbars, combo boxes, list boxes
and tab boxes.

## Runtime model

| Runtime area | ZGM API |
| --- | --- |
| Project registration and overwrite-by-name component database | `ZgmProject` |
| Cross-component validation | `ZgmDiagnostics`, `ZgmDiagnostic` |
| Application and gameplay events | `ZgmEvents`, `ZgmGameplayEvents`, `ZgmServerEvents` |
| Named one-shot and repeating events | `ZgmScheduler` |
| Typed global game state | `ZgmGlobals` |
| Menus, menu items and activation dispatch | `ZgmMenu`, `ZgmMenuItem`, `ZgmMenuRuntime` |
| Attributes, inventory, equipment, equipment-set bonuses and cooldowns | `ZgmAttributes`, `ZgmInventory`, `ZgmEquipment`, `ZgmCooldowns` |
| Live roles, HP/MP/rage/experience, buffs, skill results and ground items | `ZgmActor`, `ZgmBuffInstance`, `ZgmSkillResult`, `ZgmGroundItem` |
| Map loading, spawning, movement occupancy, camera follow, coordinate conversion, static/dynamic pathfinding, portals, NPC target tracking, priority skill casts, actor/cell skill casts, area combat, delayed damage, experience rewards, drops and pickup | `ZgmWorld`, `ZgmCamera`, `ZgmPath` |
| Window/widget/node/prefab state, input dispatch, templates, tweens and localized hero-widget bindings | `ZgmUiRuntime`, `ZgmWindowState`, `ZgmWidgetState`, `ZgmWidgetData`, `ZgmNodeState`, `ZgmPrefabInstance` |
| Lifecycle and subsystem orchestration | `ZgmEngine` |
| Renderer-neutral music state | `ZgmMusic` |
| Save snapshots with progress, inventory, equipment, learned skills and buffs | `ZgmSaveState`, `ZgmSaveItem`, `ZgmSaveEquipment`, `ZgmSaveSkill`, `ZgmSaveBuff` |
| TCP/WebSocket live transports | `ZgmTcpClientRuntime`, `ZgmTcpServerRuntime`, `ZgmServerClient`, `ZgmServerEvents`, `ZgmWebSocketRuntime` |
| SQLite database and ZGM save slots | `Game.Zgm.Data.ZgmDatabase`, `ZgmSaveRepository` |

`ZgmEngine` resolves resources across application and component-local resource
tables, starts registered extensions in registration order, loads the default
map, advances scheduled/world/UI state, creates configured network runtimes,
and captures save snapshots.

`ZgmWorld.CastSkill` keeps the common actor-target call. Cell-targeted skills use
`CastSkillAt`; `SkillConfig.SetArea` selects a Chebyshev-radius target set,
`SetDamageDelay` schedules the hit for a later `ZgmWorld.Update`, and
`SetCost` consumes HP, MP and rage together. `ZgmSkillResult` exposes the
pending state, resolved target list and accumulated damage.

`ZgmWorld.GainExperience` applies the actor's `LevelGrowth` component. Each
growth level may provide an `升级经验` (or `expToNext`) attribute; when absent,
the localized default is `level * 100`. Level-up refreshes the configured growth
attributes and emits gameplay events for experience gain and level-up.

`ZgmWorld.Camera()` exposes the map camera. It follows the hero on map entry and
movement, clamps to map bounds, and converts between pixel coordinates and map
cells. `ZgmWorld.FindPath` and `ZgmPathfinder.Find` provide deterministic grid
BFS paths around obstacle cells, with optional diagonal movement.

`ZgmWorld.IsOccupied` and `MoveActor` also account for other living actors.
`FindPathForActor` builds a temporary occupancy mask so an AI actor routes
around living actors while still allowing its selected target as the final cell.

`ZgmUiRuntime.InstantiatePrefab` materializes a `PrefabConfig` into an
independent `ZgmPrefabInstance` with copied node states. Instances expose
position, visibility, alpha, node lookup and node property mutation, can refer
to a project `Skin`, and participate in the same tween property surface as
windows, widgets and regular nodes.

`ZgmUiRuntime` also provides renderer-neutral hero bindings for progress bars,
role display boxes, skill slots, quick slots, item slots and bag widgets. Register a binding
with `BindHeroProgress`, `BindHeroShowBox`, `BindHeroSkillBox`,
`BindHeroItemBox`, `BindHeroBagBox` or `BindHeroQuickBox`; `Update` refreshes
values, cooldowns, quantities and learned state from the live `ZgmActor`.
Bag data is not copied
into a second inventory: `BoundBagSlotCount`, `BoundBagItemNameAt` and
`BoundBagQuantityAt` read the filtered `ZgmInventory` view directly. This keeps
the standard library localized to Zan's typed runtime rather than mirroring
DM3's renderer-specific widget internals.

Common control interaction is also exposed by the typed UI runtime:
`SetCheckBox` enforces positive radio groups, `ToggleCombo` and
`SetComboSelection` control dropdown state, `SetTabPage` changes the active
page, `AddListRow`/`SetListSelection` manage list data, and
`SetMapBoxView` overrides a minimap view. Map widgets otherwise follow the
live hero cell and camera zoom during `Update`.

Input and window lifecycle are also exposed without a Lua event loop:
`SetFocus` and `FocusedWidget` manage focus transitions, `KeyDown`/`KeyUp`
forward keyboard events, `TextInput` and `SetEditText` update localized edit
box state with read-only, numeric, multiline and length-limit rules, and
`Resize` raises the typed size event. `SetProgressPosition`,
`SetSliderPosition` and `SetScrollbarPosition` clamp range values against the
control configuration and raise a `数值变化` widget event. `RequestItemTooltip`
and `RequestSkillTooltip` provide configuration defaults before invoking the
application tooltip handlers.

`SetWindowState` emits renderer-neutral minimized/maximized/restored states
(`1/2/3`), while `ActivateRichTextLink` validates a rich-text widget and emits
the typed link callback with the link identifier.

`ShowSystemPrompt` and `DispatchCustomEvent` expose the corresponding typed
system/custom event paths. `SetWidgetVisible`, `SetWidgetEnabled` and
`SetWidgetText` mutate renderer-neutral widget state; hiding or disabling the
focused widget clears focus first. `SetWindowPosition` moves a live window
without rebuilding its widget states, and closing or hiding that window clears
focus when necessary.

NPCs with `NpcConfig.SetBehavior(aiLevel, autoCounter)` and `aiLevel > 0`
receive a fixed 100ms AI tick. The current localized behavior finds the nearest
hostile actor, follows a path when outside attack range, and chooses the
highest-priority learned autocastable skill when in range. A positive
`autoCounter` enables one immediate counter skill after surviving a hit;
delayed-damage skills are excluded from this immediate reaction. Skill
cooldowns, resources and target validation still go through the normal
`ZgmWorld` combat path.

## Adapter boundary

The standard library executes component semantics but intentionally does not
hard-code a renderer, audio mixer, input backend or project file format.

`scripts/zgm_tool.ps1 coverage` prints the current capability boundary and its
representative direct-`zanc` tests. It distinguishes completed Zan-native
areas from adapter-bound areas and the intentionally excluded Lua layers.

- A renderer consumes `ZgmWorld` and `ZgmUiRuntime` state.
- An audio adapter consumes `ZgmMusic` state and component sound bindings.
- Editors and build tools construct `ZgmProject` directly or generate Zan code.
- Applications serialize `ZgmSaveState` with JSON, SQLite or another format.
- `ZgmSaveState.Capture` includes map position, level, experience, HP/MP, rage,
  inventory, equipment, learned skills and active buff remaining time.
- `ZgmEngine.Load` rebuilds the typed actor state from that snapshot and reapplies
  project growth, equipment, skills and buffs.
- SQLite stays in `Game.Zgm.Data` so importing the root namespace does not force
  every program to link the native SQLite driver.
- `ZgmSaveRepository` is the ZGM-local persistence layer on top of the generic
  SQLite connection. It owns the save-slot schema and snapshot/item
  serialization, exposes slot enumeration, and applications do not need to
  rebuild those SQL tables.
- `App.lua` and `Server.lua` are not loaded or interpreted as source files.
  Their functional areas are implemented as typed ZGM configuration and
  runtime APIs; a future migration tool, if needed, belongs in a separate
  package and must translate into the typed ZGM model.

This boundary lets SDL, a native editor, a headless server and test tools share
the same ZGM component behavior.

## Naming policy

Public code uses `Game.Zgm` and `Zgm*` names. Reference-product namespace and
type aliases are not exposed. `Game.Arpg` remains a separate higher-level RPG
library and is not renamed or moved as part of ZGM.
