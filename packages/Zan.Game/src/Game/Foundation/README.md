# Game.Foundation

`Game.Foundation` contains renderer-neutral building blocks shared by board,
card, tactics, puzzle, idle and lightweight arcade games:

- deterministic fixed-step timing and replay-safe random numbers;
- semantic input actions with multiple key bindings;
- one-shot and repeating timers;
- a stack-based scene lifecycle.

GUI runtime host integration lives in `Game.Foundation.Gui`, keeping headless
simulations and servers independent from the windowing driver.

## Settings and save slots

Two bases that every shipped game would otherwise rewrite by hand:

- `GameSettings` — the project **declares** its options (key / label / type /
  default / range), the base owns the values, the range checks and the
  persistence. It is **data only**: the panel can be built with code widgets
  or with an HTML design sheet, both read and write the same keys.

  ```zan
  using Game.Foundation;

  GameSettings settings = new GameSettings().At("settings.json");
  settings.AddBool("sound", "音效", true);
  settings.AddInt("difficulty", "难度", 2, 1, 3, 1);
  settings.AddOption("mode", "模式", "easy", new List<string> { "easy", "hard" });
  settings.Load();
  settings.SetInt("difficulty", 3);
  settings.Save();
  ```

  Values are stored as strings and parsed per type, so adding a new option
  type never invalidates an old config file. A write outside the declared
  range (or outside the option list) is **ignored** — a slider dragged too far
  must not corrupt the config. A missing or broken config file leaves the
  defaults in place and reports failure; a bad config never stops the game
  from starting.

- `GameSaveStore` — multi-slot JSON saves with the three things games
  otherwise get wrong: **atomic writes** (temp file then replace, so a crash
  never leaves a half-written save), **backup fallback** (the previous save is
  kept as `.bak` and read back when the main file is damaged), and a
  **version number** (so a changed save layout is recognisable).

  ```zan
  GameSaveStore saves = new GameSaveStore("saves", "mygame", 1);
  saves.Save(0, data);          // data is a JsonValue
  JsonValue back = saves.Load(0);   // null = no save (or unreadable)
  ```

Both bases are pinned by `tests/conformance/game_settings_save.zan` in the
language repository.

## Settings panel logic

`SettingsModel` turns a declared `GameSettings` into **rows of choices**
("this option has these buttons, this one is selected"), and owns writing a
chosen button back (validate + save).

```zan
using Game.Foundation;

SettingsModel m = new SettingsModel(settings);
m.RowAt(0).Choices;      // ["开", "关"]
m.Select(0, 1);          // pick the second choice: write it back and save
m.SelectValue("mode", "hard");
m.ResetAll();
```

The split is deliberate: the **view** (the HTML sheet, the button look) is
per-project, because each game has its own art direction; the **logic**
(how many choices an option has, which one is current, how a pick is
written back and persisted) is shared, because that is the part every
project would otherwise get subtly wrong. Pinned by
`tests/conformance/game_settings_model.zan`.

## Combo meter and ring QTE

Two mechanics that every "keep the streak going" game otherwise rewrites
with its own off-by-one:

- `ComboMeter` — the streak count, the score multiplier, the timeout that
  breaks the streak, and the brightness target the HUD glows towards. The
  multiplier is computed **in one place**: score and HUD can never disagree.

  ```zan
  using Game.Foundation;

  ComboMeter m = new ComboMeter()
      .WithTimeout(3200).WithStep(0.1).WithMax(2.0).WithFever(3, 200, 40);
  m.Hit(nowMs);        // streak +1, multiplier recomputed
  m.Expire(nowMs);     // per frame: clears the streak when it timed out
  int score = (int)(base * m.multiplier);
  ```

  `GlowTarget()` only returns a number — the game decides how to draw it.

- `RingQte` — a ring that shrinks towards the centre; a press counts as a
  hit while the ring is inside the hit window, a miss resets the ring.
  Timing, radius and the required number of hits are parameters.

  ```zan
  RingQte q = new RingQte()
      .WithTiming(96.0, 14.0, 340.0, 34.0, 54.0).WithNeed(3);
  q.Update(dtMs);          // advance the shrink
  if (q.Hit()) { ... }     // press: hit only inside the window
  if (q.Complete()) { ... }
  ```

  The package owns the **judgement**; the game still draws the ring, the
  progress dots and the sparks.

Both are pinned by `tests/conformance/game_combo_qte.zan` in the language
repository.

