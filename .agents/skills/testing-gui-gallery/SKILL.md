---
name: testing-gui-gallery
description: How to build, launch and visually verify the Zan GUI gallery (examples/gui_gallery) on a headed Linux box — used for glass/backdrop-blur, skin/theme and widget rendering checks.
---

# Visual testing of the Zan GUI gallery on Linux

## Build (Linux, out-of-source build dir with a working LLVM)

Use a build dir whose LLVM actually codegens (on this box `build15` = LLVM 15;
the default `build/` with LLVM 14 fails to codegen). Rebuild the runtime/stdlib
first if you changed C or `.zan` sources:

```bash
cmake --build /path/to/repo/build15 -j8
```

Compile the gallery (all component files first, then the main file; ~2-4 min):

```bash
cd /path/to/repo
./build15/zanc examples/gui_gallery/components/*.zan examples/gui_gallery/gui_gallery.zan \
  --auto-stdlib --driver-dir build15 -o _scratch/gui_gallery
```

`--driver-dir <build dir>` makes `[DllImport("zan_gui")]` resolve against the
freshly built `libzan_gui.so` (same trick the CMake conformance targets use:
see the `ZANC_ARGS=...;--driver-dir;$<TARGET_FILE_DIR:zan_gui>` entries in
`CMakeLists.txt`). The Windows `scripts/build_gallery*.ps1` scripts are not
usable on Linux.

## Launch

```bash
ZAN_LIB_PATH=$PWD/build15 LD_LIBRARY_PATH=$PWD/build15 DISPLAY=:0 \
  setsid nohup ./_scratch/gui_gallery <ComponentName> <skin> > _scratch/gallery.log 2>&1 < /dev/null &
```

Gotcha: launch it in its own `exec` call that returns immediately. If the shell
command that starts it is killed by a tool timeout, the gallery dies with it
(empty log, no window) even with `nohup`. Use `setsid` and keep sleeps in a
separate call.

Deep-link CLI args (see `Gallery` arg loop in `examples/gui_gallery/gui_gallery.zan`):
first arg = component to open (`Card`, `Chart`, `Select`, ...); extra args
`light`/`dark`/`glass`/`liquidglass`/`neon`/`brutalism`/`chinese` pick a skin,
any installed skin pack name works too, `zh`/`en` set language, `yNNN` scrolls
the detail pane to pixel NNN, `open` pre-opens the selected popup demo. Use
`glass` when testing frosted glass / `backdrop-filter` blur — the liquidglass
skin puts `backdrop-filter: blur(...)` on `panel`/`card`/`popup`
(`stdlib/Gui/skins/base.css`, `stdlib/Gui/skins/liquidglass/skin.css`).

Maximize before recording:
`DISPLAY=:0 wmctrl -r "Zan GUI Components" -b add,maximized_vert,maximized_horz`
Resize test: `DISPLAY=:0 wmctrl -r "Zan GUI Components" -e 0,100,80,1300,900`.

## UI navigation notes

- Left column is the component nav; clicking an entry switches the detail page.
  The nav list itself scrolls with the wheel.
- The title-bar palette icon (left of the pin/minimize/maximize group) opens the
  **Theme** side panel with the skin grid (Dark / Light / Liquid Glass / Neon /
  ... plus external packs). It may need a deliberate press-and-release
  (mouse_down then mouse_up) — a fast synthetic click sometimes does not open it.
  Some external packs (e.g. `dreamy`) may silently not apply; `Sunset`,
  `Dark`, `Liquid Glass` do.
- Mouse input works; keyboard focus may not. This X server fails
  `X_SetInputFocus` with `BadMatch` (`zan_gui_titlebar_test` fails the same way
  on an unmodified runtime), so prefer mouse-only interactions and treat a
  focus failure as environmental, not a regression.

## Judging frosted-glass / blur correctness from pixels

A frosted panel is correct iff its interior is a smoothed continuation of the
wallpaper right behind it. Look for: interior hue matching the local wallpaper
(cards on the pink area frosted pink, on the teal area frosted teal), no hard
seam inside a panel (half-blurred/half-sharp), no rectangle carrying another
panel's backdrop (wrong cache slot), no ghost frosted rect where no panel is.
High-signal scenarios that stress the blur cache: hovering (repaints use a
damage strip so panels straddle the clip window), scrolling a detail page until
a glass box is half cut by the viewport top, opening/closing popups and
popovers that straddle the nav/content boundary, switching pages (slot LRU
re-keying), switching skins, and resizing the window.

## Known pre-existing test failures

`ctest -L smoke` in the LLVM-15 build dir fails only `runtime_gui_titlebar` and
`conformance_gui_webview_interop`; both fail identically on an unmodified
runtime in this environment.

## Devin Secrets Needed

none
