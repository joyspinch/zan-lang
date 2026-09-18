# Game.Arpg

`Game.Arpg` is a typed Zan component RPG library based on the public Dream
Mod 3 documentation. It is a clean implementation and does not load or embed
the original engine.

## Build a project in Zan

```zan
using System;
using Game.Arpg;

namespace MyGame;

class Bootstrap {
    static ArpgProject Create() {
        ArpgConfig config = ArpgConfig.Create();
        config.SetTitle("My Legend");
        config.SetSize(1280, 720);
        config.SetScreenMode(ArpgScreenMode.Scale());

        ArpgProject project = ArpgProject.Create();

        ArpgActorDefinition hero =
            ArpgActorDefinition.Create("hero", "Hero");
        hero.SetDefaultPlayer(true);
        hero.Attributes().SetMaxHp(100.0);
        project.AddActor(hero);

        ArpgMapDefinition start =
            ArpgMapDefinition.Create("start", 64, 64);
        start.SetDefaultMap(true);
        start.SetInitialPosition(10, 10, 0);
        project.AddMap(start);

        return project;
    }
}
```

Call `project.Validate()` to collect every error and warning before running.
Hosting is app-side: open a `Foundation.Gui` `GuiHost` loop (the same pattern
as the shipped game templates) and drive the world, scheduler and UI runtime
from it — `Game.Arpg` itself owns no window or platform lifecycle.

## Manifest workflow

Create a project:

```powershell
.\scripts\legend2_tool.ps1 init .\MyGame -Namespace MyGame.Generated
```

Edit `MyGame\legend2.project.json`, then validate referenced files:

```powershell
.\scripts\legend2_tool.ps1 validate .\MyGame
```

Generate typed registration code:

```powershell
.\scripts\legend2_tool.ps1 generate .\MyGame
```

Create a typed component and register it in the manifest:

```powershell
.\scripts\legend2_tool.ps1 component .\MyGame -Kind map -Id start
.\scripts\legend2_tool.ps1 component .\MyGame -Kind actor -Id hero
.\scripts\legend2_tool.ps1 component .\MyGame -Kind skill -Id slash
```

`component` supports `map`, `actor`, `item`, `skill`, `buff`, `window`,
`growth`, and `prefab`. It creates a `Register(ArpgProject)` factory,
updates `legend2.project.json`, validates the project and regenerates the Zan
project source and `legend2.sources.txt`. Use `-Factory` to choose a custom
fully qualified factory type and `-Force` to replace an existing component.

The default output is `MyGame\Generated\ArpgProject.g.zan`. The generated
class exposes `CreateConfig()` and `CreateProject()`.

Manifest shape:

```json
{
  "schemaVersion": 1,
  "namespace": "MyGame.Generated",
  "app": {
    "title": "My Legend",
    "width": 1280,
    "height": 720,
    "frameRate": 60,
    "verticalSync": false,
    "screenMode": 1,
    "repeatKeys": false,
    "defaultFont": "font.default",
    "background": [0, 0, 0, 255]
  },
  "resources": [
    { "id": "font.default", "file": "Assets/font.ttf" }
  ],
  "components": [
    {
      "kind": "map",
      "id": "start",
      "file": "Components/Maps/start.zan",
      "factory": "MyGame.Components.StartMap"
    }
  ],
  "scripts": [
    "Scripts/gameplay.zan"
  ]
}
```

Component kinds are `map`, `actor`, `item`, `skill`, `buff`, `window`,
`growth`, and `prefab`. Paths must remain inside the project root.

`factory` is optional. When present, the generated project calls:

```zan
MyGame.Components.StartMap.Register(project);
```

The component file should therefore expose a static
`Register(ArpgProject project)` method that creates and adds its typed
definition. Without `factory`, the file is tracked in `ArpgRegistry` only.

Generation also writes `Generated\legend2.sources.txt`. It contains the
generated source followed by all component and extension-script files that
must be supplied to `zanc`.

`init` also creates a starter `App.zan`. Compile the whole project from its
source list:

```powershell
$root = Resolve-Path .\MyGame
$sources = Get-Content .\MyGame\Generated\legend2.sources.txt |
    ForEach-Object { Join-Path $root $_ }
.\build\zanc.exe $sources -o .\MyGame\MyGame.exe
```

## Gameplay runtime

`ArpgWorld` provides the live RPG operations:

```zan
ArpgWorld world = engine.World();
world.EnterDefaultMap();

ArpgActor player = world.Player();
world.AddItem(player, "potion", 5);
world.UseItem(player, "potion");
world.EquipItem(player, "iron-sword", "weapon");

ArpgSkillCastResult result =
    world.CastSkill(player, "slash", world.ActorAt(1));
world.Update(16);
world.TryUsePortal(player);
```

Actors own stack-based inventories, named equipment slots, shared item
cooldowns, per-skill cooldowns and active Buff instances. Effective attributes
combine base values, equipment modifiers and Buff modifiers without replacing
the actor's current HP or MP.

The default combat implementation uses the documented accuracy formula:

```text
accuracy / (accuracy + evasion)
```

Damage uses the typed custom attributes `attack` and `defense`:

```text
max(1, attack * skill.damageFactor - defense)
```

Critical hits multiply this result by `criticalDamage / 100`. A skill can add
Buff definitions with `AddBuffEffect`. Periodic Buffs use their HP and MP
attribute values as per-trigger changes; a negative HP value deals damage.

Gameplay callbacks are registered through `engine.GameplayEvents()`:

```zan
engine.GameplayEvents().OnItemUsed(GameEvents.OnItemUsed);
engine.GameplayEvents().OnSkillResolved(GameEvents.OnSkillResolved);
engine.GameplayEvents().OnBuffChanged(GameEvents.OnBuffChanged);
engine.GameplayEvents().OnActorDamaged(GameEvents.OnActorDamaged);
engine.GameplayEvents().OnActorDied(GameEvents.OnActorDied);
engine.GameplayEvents().OnMapChanged(GameEvents.OnMapChanged);
```

System prompt codes follow the documented App contract: `1` skill cooldown,
`2` item cooldown, `3` item level requirement and `4` full inventory.
`TryUsePortal` preserves the player instance, including inventory, equipment,
cooldowns and Buffs, while replacing map-local NPC instances.

## Graphical controls and data binding

DM-style controls live in `Game.Arpg` rather than `Gui`. They are rendered and
hit-tested in game coordinates and can contain text or sprite prefab nodes:

```zan
ArpgDataSource status = ArpgDataSource.Create();
status.SetText("name", "Hero");
status.SetNumber("hp", 80.0);
status.SetNumber("maxhp", 100.0);

ArpgNodeDefinition caption = ArpgNodeDefinition.Create(
    "caption", ArpgNodeKind.Text(), 8, 8, 180, 24);
caption.SetContent("&name&", false);
caption.SetMouseEvents(true);

ArpgControlDefinition box = ArpgControlDefinition.Create(
    "status", ArpgControlKind.RichTextBox(), 20, 20, 240, 80);
box.SetDataSource(status);
box.SetMouseEvents(true);
box.SetContent(
    "#Y&name&#W {if &hp& > 0}&hp&/&maxhp&{else}defeated{end}",
    true);
box.AddNode(caption);

ArpgWindowDefinition hud = ArpgWindowDefinition.Create("hud", 300, 120);
hud.SetVisibleByDefault(true);
hud.AddControl(box);
project.AddWindow(hud);
```

If a control or node has no explicit data source, `ArpgUiRuntime` evaluates it
against the current player's live values. Nested paths such as
`&属性.攻击&` resolve through `ArpgDataSource.SetNumber("属性.攻击", ...)`.
Templates support nested `if`, `elseif`, `else` and `end` blocks.

`ArpgRichText.Parse` evaluates templates first and then emits typed runs for
color/font/background changes, images, animations, spacing, wrapping, items
and `#@trigger@label@` links. Link activation is delivered to both the control
handler and the app's `ArpgEvents` hub (`OnRichTextLink(...)`).

Pointer events are dispatched from the highest graphical window and control to
the highest node under the cursor. A node can consume the event; otherwise it
bubbles through its control to the window. The UI runtime exposes live windows,
controls, evaluated content and RichText documents to the host app.

## Screen modes

- `ArpgScreenMode.Fixed()` (`0`): fixed-size window.
- `ArpgScreenMode.Scale()` (`1`): resizable window with scaled logical view.
- `ArpgScreenMode.ResizeWorld()` (`2`): resizable native-size view.
- `ArpgScreenMode.TransparentBorderless()` (`3`): borderless host prepared
  for color-key presentation; platform transparency depends on the host
  backend.

## Hosting

`Game.Arpg` contains no windowing or platform I/O. Open a `Foundation.Gui`
`GuiHost` (as the shipped game templates do) and drive the world, scheduler,
events and UI runtime from the host loop; rendering adapts onto `Gui` Canvas.
