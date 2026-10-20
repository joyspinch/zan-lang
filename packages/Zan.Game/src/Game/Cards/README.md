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

## Combo rules (hand shapes)

`ComboType` / `Combo` / `ComboRuleSet` / `ComboRules` recognise and compare
Chinese-style card hands (single, pair, triple, triple+one, triple+pair,
straight, pair-straight, bomb, rocket):

```zan
using Game.Cards;

Combo c = ComboRules.Classify(ranks);        // ranks → hand shape
bool bigger = ComboRules.Beats(c, table);     // can this beat the table?
List<Combo> all = ComboRules.Candidates(hand, table);  // every legal reply
```

The package only knows **ranks** (ints), never suits or card instances — the
game maps `Combo.Ranks()` back to its own cards. `ComboRuleSet` carries the
game-specific knobs (straight length and rank window, bomb size, which two
ranks are the jokers); `ComboRuleSet.Default()` is the Dou Dizhu shape, other
variants override it:

```zan
ComboRuleSet rules = ComboRuleSet.Default()
    .WithStraight(3, 6)        // 3-card straights allowed
    .WithStraightRanks(3, 15);  // ...up to the "2"
Combo c = ComboRules.ClassifyWith(ranks, rules);
```

Straight and pair-straight detection honours the rule set, so a variant that
allows short straights classifies them instead of rejecting them.

