# Game.Arpg architecture

This module uses the published Dream Mod 3 documentation as a domain reference,
not as a binary or source dependency. The implementation keeps the component
model while exposing typed Zan APIs.

## Layers

1. `Primitives` owns color, geometry, component-kind constants and diagnostics.
2. `Config` models App settings, the resource table, extension components and
   extension scripts.
3. `Map`, `Entity`, `Combat` and `Presentation` contain definitions for maps,
   actors, items, skills, buffs, growth, graphical windows, controls and
   prefab nodes.
4. `ArpgProject` is the validated component database. Cross references such
   as actor skills, map spawns and portals are checked before startup.
5. `Runtime` owns inventory stacks, equipment slots, cooldowns, deterministic
   random numbers and the default hit/damage calculations.
6. `ArpgWorld` owns live actor instances, combat operations, Buff updates
   and current-map state.
7. `ArpgScheduler` implements named one-shot and repeating events.
8. `ArpgEngine` owns SDL3 lifecycle, input translation, fixed frame pacing
   and the bridge between project data and live world state.
9. `DataBinding` evaluates nested data sources, `&path&` variables and nested
   `{if}`/`{elseif}`/`{else}` template blocks.
10. `RichText` converts evaluated content into typed text, image, animation,
    item, spacing, wrapping and hyperlink runs.
11. `UiRuntime` owns the DM-style graphical window/control/node tree,
    hit-testing, focus, dragging, event bubbling and SDL rendering.

Definitions are intentionally independent from serialization. A project can be
assembled in Zan, generated from `legend2.project.json`, or supplied by a future
editor without changing runtime types.

## Compatibility scope

Implemented:

- App title, dimensions, frame rate, vertical sync, RGBA background, screen
  modes 0-3, repeated-key behavior, resources, components and scripts;
- startup, close, focus, window-state, resize, key, menu and system-prompt
  events;
- graphical window, control, prefab and node lifecycle/pointer/custom events;
- per-control and per-node data sources with live nested-path evaluation;
- current-player default template data, including base and custom attributes;
- nested template conditionals and `&path&` substitution;
- typed RichText runs, inline resources, item fragments and hyperlink events;
- graphical hit-testing, z-order, focus, capture, control/window dragging and
  node-to-control-to-window event bubbling;
- SDL-rendered window/control/node backgrounds, borders and BMP resources;
- named delayed and repeating events;
- map grids, barriers, actor spawns, portals and default-map entry;
- actor base/custom attributes, skills, starting buffs and live instances;
- item, skill, buff, growth, window and prefab definitions;
- stack-based inventories, named equipment slots and item/skill cooldowns;
- effective attributes composed from actor, equipment and active Buff values;
- deterministic hit, critical-hit and damage resolution;
- skill-applied Buffs, periodic Buff effects and conditional Buff removal;
- item use, equipment changes, actor damage/death and map-change events;
- portal transitions that preserve player runtime state;
- accumulated validation diagnostics and cross-reference checks.

Deferred to dedicated subsystems:

- map-object rendering, animation playback, skins and font glyph shaping;
- inventory/equipment UI and delayed or area-based skill effects;
- rage, threat, custom formula evaluation and NPC combat decision making;
- audio/music, tweening, networking and SQLite facades;
- loading legacy Lua component files directly.

The graphical control runtime belongs to `Game.Arpg`, not `Gui`: it uses game
coordinates, z-order and SDL resources rather than desktop widget layout or
native controls. The SDL3 layer owns platform I/O, and Arpg public APIs do not
expose raw SDL handles.
