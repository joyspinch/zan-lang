# Zan game standard libraries

The game modules are intentionally layered:

```text
Game.Ra2 ---------+
                  +--> SDL3 --> zan_sdl3 --> SDL3
Game.Legend2 -----+
```

`SDL3` owns platform windows, rendering, input, timing and audio integration.
Game modules own domain concepts and must not expose raw SDL handles through
their public gameplay APIs.

`Game.Ra2` is a clean-room compatibility engine for user-owned RA2/Yuri assets.
Its next milestone is the format pipeline beginning with MIX and INI.

`Game.Legend2` is a typed component RPG engine inspired by the useful concepts
in DM3's documented Lua model. It includes validated map, actor, item, skill,
buff, growth, window and prefab definitions, a live world, named event
scheduling, and a manifest generator in `scripts/legend2_tool.ps1`.
