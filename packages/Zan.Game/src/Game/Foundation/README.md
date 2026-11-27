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

## Player profile, procedural SFX and auto-pause

Three things every shipped game needs and the game templates share:

- `GameProfile` — best scores (`SubmitBest` / `SubmitLowest` return `true` on a
  new record and save at once), counters (`Add`) and on/off preferences
  (`Flag` / `SetFlag`, e.g. mute). It lives in the **per-user data folder**
  (`%LOCALAPPDATA%`, `~/Library/Application Support`, `~/.local/share`, under
  `ZanGames/<gameId>/`; `ZAN_GAME_DATA_DIR` overrides it), never the working
  directory, and it writes through `GameSaveStore` (atomic + `.bak`).
  Headless replays use `GameProfile.InMemory()`: same API, no file system.

  ```zan
  GameProfile profile = GameProfile.Open("snake");
  if (profile.SubmitBest("score", score)) { /* show NEW BEST */ }
  ```

- `SfxPresets` + `SfxBank.AddSynth` — ten short synthesized event sounds
  (`click select coin hit explode launch powerup success fail error`), so a
  game has full audio feedback with zero audio files. `bank.AddPresets(names)`
  renders them at startup; `bank.Warm()` opens the device during loading
  instead of on the player's first action. Without an audio device every call
  is a no-op. Recorded WAVs loaded under the same name replace a preset.

  ```zan
  SfxBank sfx = new SfxBank(new AudioBus());
  sfx.AddPresets(new string[] { "click", "coin", "fail" });
  sfx.Warm();
  sfx.Play("coin");
  ```

- `GuiHost.WindowActive()` — visible and focused. The host keeps stepping
  logic while minimized, so real-time games pause themselves when this turns
  false.

Pinned by `tests/conformance/game_profile_sfx.zan`.

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

## Settings overlay (standard widgets)

`SettingsOverlay` (in `Game.Foundation.Gui`) is the **generic** view for
projects that have no art direction of their own yet — a canvas game with a
hand-written frame loop. It builds the panel from standard widgets (Panel /
Flex / Label / Button), one button group per `SettingsModel` row, and it
highlights the current choice with a class (never an inline style).

```zan
using Game.Foundation;
using Game.Foundation.Gui;

SettingsModel m = new SettingsModel(settings);
SettingsOverlay ov = new SettingsOverlay(m);
form.Add(ov.Root());     // mount the layer
ov.Tick();               // every frame: poll clicks -> write back + save
ov.Toggle();             // open / close
```

Nothing in it is project-specific, so a project that later grows its own
look replaces the whole layer behind the same three calls. Pinned by
`tests/conformance/game_settings_overlay.zan`.

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

## Replays are regression tests (S26 / S27)

A game that wants headless replay wraps its *logic* (never its window) in an
`IReplaySim`: `Reset(seed)` starts a run whose randomness comes only from the
seed (`DeterministicRandom`), `Step(frame, input)` advances one fixed frame
with the player's input encoded as an `int`, and `StateChecksum()` folds the
state that matters into a `StateHasher`.

```zan
using Game.Foundation;

public class VaultSim : IReplaySim {
    public void Reset(long seed) { ... }
    public void Step(int frame, int input) { ... }
    public int StateChecksum() { return new StateHasher().Int(gold).Int(spent).Value(); }

    // A game rule: checked after every frame by Verify and Fuzz.
    [Invariant] public bool GoldConserved() { return gold == 10 + minted - spent; }

    // Optional bot for fuzzing (zan-check runs 3 seeds x 2000 frames).
    public static int Bot(DeterministicRandom rng, int frame) { return rng.NextBelow(3); }
}
```

- `Replay.Record(sim, "VaultSim", seed, inputs, 30)` plays an input list and
  stores a checksum every 30 frames; `ReplayLog.Save/Load` use a small text
  format (`zan-replay 1`, `sim`, `seed`, `frames`, `i <frame> <input>`,
  `c <frame> <checksum>`, `end <checksum>`) that diffs cleanly.
- `Replay.Verify(sim, log)` replays and stops at the first divergence,
  broken `[Invariant]` (public, parameterless, returns `bool`) or exception —
  `ReplayResult.Describe()` names the frame.
- `Replay.Fuzz(sim, name, seed, frames, bot, stallFrames)` drives the sim
  with a bot, checking rules every frame; on failure `result.log` is the
  replay of what happened — save it next to the others and it becomes a
  regression test.

Put replays under `<project>/replays/*.replay`; `zan-check` finds them, builds
the named sim, replays every file and fuzzes every sim that has a `Bot`.
Every game template ships `src/<Name>Sim.zan` and `replays/bot.replay`. When a
rule change is intended, re-record:

```zan
ReplayResult r = Replay.Fuzz(new TrapCatSim(), "TrapCatSim", 20261125, 600, TrapCatSim.Bot, 0);
if (r.ok) { r.log.Save("replays/bot.replay"); }
```

`DeterministicRandom` mixes its output (splitmix64 finalizer over the LCG
state): raw LCG low bits have tiny periods (`NextBelow(2)` used to alternate
0, 1, 0, 1), which bots and shuffles would lock onto.

