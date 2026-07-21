# Zan asset pipeline

How assets flow from source art into a shipped game:

```
export tool ──(assets.manifest.json)──► IDE Asset Manager ──► project assets/ ──► publish ──► .zrp packs
```

* The **Asset Manager** (IDE ribbon → Config → Asset Manager) browses the
  project's `assets/` tree, imports single files or whole folders
  (recursively, keeping directory structure), creates/renames/deletes
  entries, and authors `.anim` clip metadata for 8-direction frame sheets.
* At publish time assets are bundled with the app; the encrypted
  `System.Resources.ResourcePack` (`.zrp`) format packs entries by name and
  reads them back individually (indexed, per-entry AES-256-GCM keys, signed
  index), so grouping many assets into one large pack per *loading unit*
  (base UI pack, per-map pack, per-monster-module pack) is the recommended
  layout.

## Import manifest: `assets.manifest.json`

Any external converter/export tool can make its output one-click importable
by writing an `assets.manifest.json` next to the exported files. When the
Asset Manager's *Import folder…* is pointed at a directory containing this
file, it imports exactly what the manifest lists (instead of copying the
raw tree).

```json
{
  "version": 1,
  "entries": [
    {
      "src": "out/skeleton_walk.png",
      "name": "mon/skeleton/walk",
      "type": "anim8",
      "anim": {
        "frameW": 96, "frameH": 96,
        "dirs": 8, "framesPerDir": 6,
        "fps": 12,
        "loop": "loop", "loopStart": 0,
        "base": 0
      }
    },
    { "src": "out/skeleton_die.png", "name": "mon/skeleton/die",
      "type": "anim8",
      "anim": { "dirs": 8, "framesPerDir": 8, "fps": 10, "loop": "once" } },
    { "src": "ui/btn.png",  "name": "ui/btn",  "type": "image" },
    { "src": "sfx/hit.wav", "name": "sfx/hit", "type": "audio" }
  ]
}
```

Fields per entry:

| field  | required | meaning |
|--------|----------|---------|
| `src`  | yes | source file, relative to the manifest's folder |
| `name` | no  | target path inside `assets/` (no extension needed — the source extension is appended; defaults to `src`) |
| `type` | no  | `image` / `audio` / `font` / `data` / `anim8` / `misc` — informational except `anim8` |
| `anim` | for `anim8` | clip metadata; written as a side-car `assets/<name>.anim` |

Unknown files without a manifest are still importable: a plain folder
import copies every file recursively, preserving relative paths.

## Clip metadata: `.anim`

A `.anim` file sits next to its frame-sheet image and describes one action
(run, slash, die, cast, …) laid out direction-major — all frames of
direction 0 first, then direction 1, and so on (the classic Mir layout):

```json
{
  "version": 1,
  "image": "walk.png",
  "frameW": 96, "frameH": 96,
  "base": 0,
  "dirs": 8,
  "framesPerDir": 6,
  "fps": 12,
  "loop": "loop",
  "loopStart": 0
}
```

* `base` — first frame of this action inside a larger multi-action sheet
  (0 when each action has its own sheet).
* `loop` — `once` (attack/death: hold the last frame), `loop`
  (walk/run: wrap to frame 0), or `section` (play frames
  `0..loopStart-1` once as a wind-up, then repeat `loopStart..last` —
  e.g. sustained casting).

The frame actually drawn is always:

```
sheetFrame = base + direction * framesPerDir + currentFrame
```

Turning a character changes only `direction`; `currentFrame` progress is
preserved, so the stride continues seamlessly in the new facing.

## Runtime: `Game.Core.AnimClip` / `AnimPlayer`

```zan
AnimClip run = AnimClip.Load(dir + "/mon/skeleton/walk.anim");
AnimPlayer p = AnimPlayer.Create();
p.Play(run);
p.SetDir(3);          // facing; progress is kept when turning
p.Update(frameMs);    // advance by elapsed milliseconds
int f = p.SheetFrame();  // absolute frame index in the sheet to draw
if (p.Done()) { /* a "once" clip finished -> back to idle */ }
```
