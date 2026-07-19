# Game.Cards

`Game.Cards` supplies deterministic card definitions and runtime zones:

- catalogued card definitions with typed effects;
- unique card instances, draw/hand/discard/exhaust zones and seeded shuffling;
- draw-pile recycling, retained cards and zone movement;
- a compact deck-building battle model with energy, block, strength,
  vulnerability and enemy intent damage.

The battle layer is deliberately small. Projects can build richer effect
stacks, targeting and encounter AI on the same `CardDeck` and `CardZone`
primitives.
