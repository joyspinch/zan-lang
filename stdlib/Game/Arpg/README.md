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
    static ArpgEngine Create() {
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

        ArpgEngine engine = ArpgEngine.Create(config);
        if (!engine.LoadProject(project)) {
            return null;
        }
        return engine;
    }
}
```

Call `project.Validate()` to collect every error and warning before creating a
window. `ArpgEngine.Start()` also validates the App configuration and loaded
project.

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
class exposes `CreateConfig()`, `CreateProject()` and `CreateEngine()`.

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

## Screen modes

- `ArpgScreenMode.Fixed()` (`0`): fixed-size window.
- `ArpgScreenMode.Scale()` (`1`): resizable window with scaled logical view.
- `ArpgScreenMode.ResizeWorld()` (`2`): resizable native-size view.
- `ArpgScreenMode.TransparentBorderless()` (`3`): borderless host prepared
  for color-key presentation; platform transparency still requires SDL bridge
  support.

## Native runtime

Importing `Game.Arpg` also imports the SDL3 host. On Windows, native
executables need `zan_sdl3.dll` and `SDL3.dll` from
`stdlib/SDL3/drivers/win-x64` available beside the executable or on `PATH`.
