# ZanIDE / Gui AI-operable UI Driver

`Gui.UiDriver` is a built-in automation channel for the GUI framework and the
self-hosted ZanIDE. It lets an AI agent (or a test) drive the *real* application
— the same window, widgets, event dispatch, hit-testing and rendering that a
human uses — **without** screenshots, OS-level `SetCursorPos`, or simulated
hardware clicks.

## Why it exists

The framework already routes every real mouse/keyboard event through one path:

```
WM_* / SDL event → App.ProcessEvent → hitTester.HitTest(x,y) → widget handler
```

and every clickable widget registers a flat `HitRegion { id, x, y, w, h, widgetType }`
with `app.hitTester` each frame. The driver reuses exactly this path:

- It **injects synthetic events** into the native queue (`Window.InjectEvent`),
  so dispatch, focus, hit-testing and click targets behave identically to real
  input.
- It can **target a widget by its registered hit-region id** (`clickid`), so no
  screen coordinates need to be discovered.
- It exposes **machine-readable introspection**: a JSON dump of all hit regions,
  and named probes for editor/completion/log state.

This makes acceptance tests deterministic and coordinate-free.

## Activation

The driver is inert unless a script path is provided via environment variables:

| Variable | Meaning | Default |
|----------|---------|---------|
| `ZAN_UI_SCRIPT` | Absolute path to a driver script to run after the window is shown | (unset → driver off) |
| `ZAN_UI_OUT` | Directory for `dump` output files and `results.log` | `_scratch/uidrv` |

The driver is started from `App.Show()` (`UiDriver.Begin`) and advanced from the
event loop (`UiDriver.Tick`). The standard `App.Run`/`ProcessEvent` loop ticks it
automatically; ZanIDE's custom loop ticks it explicitly. When active it also
publishes the `editor` and `log` probes each frame.

Helper launcher (`_scratch/run_uidrv.ps1`):

```powershell
powershell -ExecutionPolicy Bypass -File _scratch\run_uidrv.ps1 `
    -Script _scratch\uidrv\my_script.zscript -TimeoutSec 30
```

It sets `ZAN_UI_SCRIPT`/`ZAN_UI_OUT`, launches `build\ZanIDE.exe`, and kills it
if it does not exit within the timeout.

## Command grammar

One command per line; blank lines ignored. `#`-style comments are not stripped,
so use the `log` command for annotations.

| Command | Effect |
|---------|--------|
| `wait <ms>` | Pause the script for `<ms>` milliseconds (frames keep rendering) |
| `move <x> <y>` | Inject a mouse-move to `(x,y)` |
| `click <x> <y>` | Inject move+press+release (left) at `(x,y)` |
| `rclick <x> <y>` | Right-button click at `(x,y)` |
| `clickid <hitId>` | Look up the hit region with id `<hitId>` and click its center — **preferred, coordinate-free** |
| `scroll <x> <y> <delta>` | Inject a wheel event at `(x,y)` |
| `char <code>` | Inject a character/text event (kind 6) with the given code (e.g. `9` = Tab, `13` = Enter, `27` = Esc) |
| `type "<text>"` | Inject one `char` per ASCII byte of `<text>` |
| `keydown <code> [mods]` | Inject a key-down (kind 4) |
| `keyup <code> [mods]` | Inject a key-up (kind 5) |
| `ev <kind> <x> <y> <button> <code> <mods>` | Raw event injection (escape hatch) |
| `dump hitregions <file>` | Write the full hit-region table as JSON to `<ZAN_UI_OUT>/<file>` |
| `dump probe <name> <file>` | Write named probe (`editor`, `log`) to `<file>` |
| `assert probe <name> contains <text>` | Pass/fail assertion; substring match |
| `assert probe <name> equals <text>` | Pass/fail assertion; exact match |
| `log <text>` | Append a note to `results.log` |
| `quit` | Close the window and end the session |

`clickid` resolves the id with **last-registration-wins** precedence, matching
`HitTester.HitTest`, so it clicks the top-most (overlay) region for that id.

### Event kinds (for `char`/`ev`)

`1` move · `2` press · `3` release · `4` key-down · `5` key-up · `6` char/text ·
`7` resize · `8` close · `13` scroll.

Tab acceptance in the editor is driven with `char 9` (a kind-6 text event), which
is the path the editor's completion-accept logic listens on.

## Introspection formats

### Hit-region dump (`dump hitregions`)

A JSON array; each region:

```json
{ "z": 5, "id": 42, "x": 404, "y": 156, "w": 120, "h": 30, "type": 0 }
```

- `z` = registration order (draw order). Higher `z` is drawn later / on top.
- `type` = widget type hint; `-1` marks a modal/scrim **hit barrier**
  (`BlockHitsBelow`/`BlockHitsRect`) that swallows clicks to content beneath it.

Because hit-testing searches regions in reverse `z` order, overlays/popups —
which register *after* page content each frame — win the pointer.

### Editor probe (`dump probe editor`)

```json
{
  "caretLine": 20, "caretCol": 21,
  "completionActive": true, "completionSel": 0,
  "completion": [ { "label": "foreach", "kind": "S" }, ... ],
  "text": "…full buffer, \n-joined…"
}
```

`kind`: `S` snippet · `W` word · `K` keyword · `M`/`T` type/member · `F` field.

### Log probe (`dump probe log`)

The IDE output/log panel, `\n`-joined.

### Results file

Every `assert`/`log`/action appends to `<ZAN_UI_OUT>/results.log`, ending with:

```
DONE pass=<N> fail=<M>
```

## Example scripts

### Message-board Build acceptance (real IDE)

```
log MB build acceptance via real IDE
wait 1600
clickid 18            # expand src/ in the project tree
wait 600
clickid 19            # open main.zan
wait 800
clickid 7             # Build (ribbon)
wait 9000
dump probe log mb_build_log.txt
assert probe log contains "Build succeeded"
quit
```

### Tab completion / snippet acceptance (real editor)

```
log completion/snippet acceptance via real editor
wait 1600
clickid 18
wait 500
clickid 19
wait 800
click 1200 900        # focus the editor
wait 250
char 27               # Esc (dismiss any popup)
char 13               # Enter (fresh line)
type "fore"
wait 500
dump probe editor accpop.json
assert probe editor contains "completionActive":true
assert probe editor contains "label":"foreach"
char 9                # Tab → accept snippet
wait 500
dump probe editor accexp.json
assert probe editor contains foreach (var item in
quit
```

### GUI hit-region / floating-surface audit

```
wait 1600
clickid 18
wait 500
clickid 19
wait 800
dump hitregions audit_hit.json
quit
```

Then analyze `audit_hit.json`: confirm no region extends past the window
bounds; confirm tab-strip regions stay within the tab bar's width (clipped);
open a modal/menu and confirm a `type:-1` barrier plus the overlay's own
regions register with the highest `z`.

## Limitations

- **Real GUI stack required.** The driver exercises the actual `ZanIDE.exe` with
  the SDL3/native runtime and self-hosted toolchain. It cannot run in a
  display-less environment; run it on the Windows workspace.
- `type` injects ASCII bytes 32–126 only; use `char <code>` for control keys.
- Timing is wall-clock (`wait`); slow machines may need larger waits before a
  `dump`/`assert` that depends on a build or async result.
- Assertions are substring/exact string matches over the probe JSON; prefer
  distinctive fragments (e.g. `"label":"foreach"`, not `"for"`, which also
  matches `"foreach"`).

## Interpreting failures

- `ASSERT FAIL probe <name> ...` → the probe did not contain/equal the expected
  text; inspect the matching `dump` file written just before it.
- `UNKNOWN COMMAND: ...` → typo in the script grammar.
- `TIMEOUT after Ns` (from the launcher) → the app did not `quit`; usually a
  missing/blocked `clickid` target or a crash — check `<exe_dir>\zan_crash.log`.
- Empty/`path not found` dump file → the `dump` line never executed (script
  ended early or crashed before it).
