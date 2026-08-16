---
name: testing-gui-graphview
description: How to build, launch and GUI-verify Zan stdlib GUI components (e.g. Gui.Component.GraphView) with a small headed probe on Linux, including event cross-verification via Console.WriteLine logs.
---

# Probe-based GUI testing of a single Zan stdlib component

Instead of bringing up the whole `examples/gui_gallery`, drive one component
with a ~60-line probe in `_scratch/`. It compiles in 1-2 min and gives you a
window with only the widget under test.

## Probe pattern

```zan
using System; using Gui; using Gui.Widget; using Gui.Component;
class Probe {
    static GraphView g; static Label info;
    static void Main() {
        Form f = Form.CreateDark("GraphView probe", 900, 640);
        Panel col = Panel.Column(); col.Dock(Dock.Fill());
        Probe.info = new Label("..."); col.Add(Probe.info);
        Probe.g = new GraphView(); Probe.g.Dock(Dock.Fill());
        Probe.g.Bind(Probe.Demo());
        Probe.g.OnSelect(Probe.Selected);   // handler updates the Label AND
        col.Add(Probe.g); f.Add(col); f.Run();
    }
    static void Selected() {
        string s = "select: " + Probe.g.Selected().title;
        Probe.info.SetProp("text", s);
        Console.WriteLine(s);               // ... prints to the redirected log
    }
}
```

Always do **both** — set a visible Label *and* `Console.WriteLine`. The log
gives you machine-checkable proof an event actually fired (and, just as
important, proof that an event did *not* fire), which pixels alone cannot.

## Build & launch

```bash
cd /path/to/zan-lang
./build/zanc _scratch/myprobe.zan --auto-stdlib -o _scratch/myprobe   # 1-2 min
```
(`Deflate.zan` "condition should be bool, got 'int'" warnings are pre-existing.)

```bash
cd /path/to/zan-lang && (ZAN_LIB_PATH=$PWD/build LD_LIBRARY_PATH=$PWD/build \
  DISPLAY=:0 setsid nohup ./_scratch/myprobe > _scratch/myprobe.log 2>&1 < /dev/null &)
```

Gotcha: the launch **must** be its own `exec` call that returns immediately.
If a tool timeout kills the launching shell, the app dies with it even under
`nohup`; `setsid` + backgrounded subshell + immediate return is required.

Window management (do these in a separate call after ~3 s):
```bash
DISPLAY=:0 wmctrl -r "GraphView probe" -b add,maximized_vert,maximized_horz
DISPLAY=:0 wmctrl -r "GraphView probe" -b remove,maximized_vert,maximized_horz
DISPLAY=:0 wmctrl -r "GraphView probe" -e 0,60,50,420,300   # force wrap/scroll
```
Shrinking the window is the cheapest way to exercise reflow, same-layer
wrapping and vertical scrolling paths.

## Known environment limitations on this box (NOT regressions)

- **Right-click / `Context` events do not reach the app.** Button-3 press and
  release synthesized with xdotool (fast click or slow `mousedown 3` / `mouseup 3`)
  produce no `Context`/`Select` in `GraphView` *and none in `ListView` either*,
  which is a known-good widget. Left click, double click and wheel all work.
  Before blaming the component under test, build a two-widget probe
  (`ListView` + component, both with `OnContext`) and compare: if neither
  fires, it is the environment. Right-click may therefore have to be reported
  as untested; ask the user to verify it manually.
- **Keyboard focus may be dead** — `X_SetInputFocus` fails with `BadMatch`.
  Use mouse-only interactions.
- **No freetype/fontconfig** in the prebuilt `stdlib/Gui/drivers/linux-x64/libzan_gui.a`,
  so CJK text renders as `?`. Write probe strings in ASCII; `?` glyphs are not
  a failure.

## Judging GraphView-style canvas output from pixels

- Layering: read the y coordinates — nodes in the same layer must share a y.
- Edge colour is semantic: `t.primary` (teal) when the source node is `done`,
  `t.divider` (grey) otherwise. `zoom` into a 60x30 px box around a single edge;
  at 1x the arrowheads (two 4 px diagonals) are nearly invisible.
- Selected / hovered / locked are CSS classes (`graphnodesel` / `graphnodehover`
  / `graphnodelocked`), so hover state can masquerade as selection in a
  screenshot taken with the cursor still over the card. **Always move the mouse
  away and re-screenshot before judging selection.**

## Devin Secrets Needed

none
