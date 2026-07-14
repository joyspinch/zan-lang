# Game.Legend2 architecture

This module uses the published Dream Mod 3 documentation as a domain reference,
not as a binary or source dependency. The implementation keeps the component
model while exposing typed Zan APIs.

## Layers

1. `Primitives` owns color, geometry, component-kind constants and diagnostics.
2. `Config` models App settings, the resource table, extension components and
   extension scripts.
3. `Map`, `Entity`, `Combat` and `Presentation` contain immutable definitions
   for maps, actors, items, skills, buffs, growth, windows and prefabs.
4. `Legend2Project` is the validated component database. Cross references such
   as actor skills, map spawns and portals are checked before startup.
5. `Runtime` owns inventory stacks, equipment slots, cooldowns, deterministic
   random numbers and the default hit/damage calculations.
6. `Legend2World` owns live actor instances, combat operations, Buff updates
   and current-map state.
7. `Legend2Scheduler` implements named one-shot and repeating events.
8. `Legend2Engine` owns SDL3 lifecycle, input translation, fixed frame pacing
   and the bridge between project data and live world state.

Definitions are intentionally independent from serialization. A project can be
assembled in Zan, generated from `legend2.project.json`, or supplied by a future
editor without changing runtime types.

## Compatibility scope

Implemented:

- App title, dimensions, frame rate, vertical sync, RGBA background, screen
  modes 0-3, repeated-key behavior, resources, components and scripts;
- startup, close, focus, window-state, resize, key, menu and system-prompt
  events;
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

- rendering map objects, animations, skins and rich-text controls;
- inventory/equipment UI and delayed or area-based skill effects;
- rage, threat, custom formula evaluation and NPC combat decision making;
- audio/music, tweening, networking and SQLite facades;
- loading legacy Lua component files directly.

The SDL3 layer owns platform I/O. Legend2 public gameplay APIs do not expose raw
SDL handles.
