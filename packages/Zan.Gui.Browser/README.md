# Zan.Gui.Browser

In-app web browser components for Zan Gui, extracted from the standard
library. Namespaces are unchanged, so existing programs compile as-is once
the package is visible to zanc (project `packages/`, `.zan-packages/`, or a
toolchain-relative `packages/` store).

- `Gui.Component.WebView` — `WebViewBox` on the system WebView (WebView2 on
  Windows, WebKitGTK on Linux), link-window routing into `App`.
- `Gui.Component.CefBrowser` — `CefBrowserBox` on the Chromium Embedded
  Framework: CDP control, cookies, fingerprints. The CEF runtime itself is
  downloaded per machine at first run; the native `zan_cef` driver ships in
  `src/Gui/Component/CefBrowser/drivers/<plat>/` and zanc carries it next to
  published executables (driver discovery walks package source roots).

Both components are opt-in: a program that never references them pulls
nothing from this package. `App` registers the `<a href>` link navigator at
run time via `WebViewBootstrap.Install()`, so the stdlib Gui core has no
build-time dependency on this package.

Build the CEF driver with `cmake --build build --target zan_cef`
(Windows), `scripts/build_cef_linux.sh` (Linux) or
`scripts/build_macos_cef.sh` (macOS cross-build).
