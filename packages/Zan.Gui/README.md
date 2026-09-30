# Zan.Gui

The Zan GUI toolkit, extracted from the standard library. Namespaces are
unchanged (`Gui`, `Gui.Core`, `Gui.Widget`, `Gui.Component`, ...), so existing
programs compile as-is once the package is visible to zanc (project
`packages/`, `.zan-packages/`, or a toolchain-relative `packages/` store).

- `Gui` / `Gui.Core` — `App`, window shell, frame pump, hit testing, overlays.
- `Gui.Widget`, `Gui.Component` — the control library (buttons, lists, tabs,
  DataTable, Chart, ChatView, ...). `Gui.Component.DataTable` carries its
  localization packs under `src/Gui/Component/DataTable/lang/`.
- `Gui.Styling` / `Gui.Rendering` / `Gui.Layout` / `Gui.Text` / `Gui.Media` /
  `Gui.Markup` / `Gui.Animation` / `Gui.Reactive` / `Gui.Designer` — style,
  render, layout, text and tooling layers.
- `Gui.Backend` — the Zan-side half of the native window shells (Win32/X11/
  Cocoa) whose native halves live in the compiler's `zan_gui` runtime driver.

Resources ride inside the package and zanc discovers them from package source
roots (stdlib copies win if present):

- `src/Gui/skins/`, `src/Gui/icons/` — embedded at compile time, resolved at
  run time.
- `src/Gui/drivers/<target>/` — per-target `zan_gui` driver import libraries
  and `driver.manifest`; zanc bundles the driver when the image emits
  `zan_gui_*` imports.

Browser components live in the separate `Zan.Gui.Browser` package (WebView /
CefBrowser); `App` has no build-time dependency on it.
