# Game.Arcade2D

`Game.Arcade2D` is a lightweight renderer-neutral simulation layer:

- vectors, rectangles, circles, overlap tests and a 2D camera;
- source-frame animation clips and players;
- pooled entities with interpolation state, velocity, health, cooldowns and
  finite lifetimes;
- nearest-enemy queries and reusable polyline path following.

It is intended for tower defense, auto-battlers, grid roguelites and moderate
entity-count arcade games. A future high-volume renderer and spatial hash can
extend the same entity surface for survivor-like games.
