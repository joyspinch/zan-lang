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

