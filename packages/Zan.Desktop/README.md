# Zan.Desktop

OS desktop and system integration for Zan, extracted from the standard
library. Namespaces are unchanged, so existing programs compile as-is once
the package is visible to zanc (project `packages/`, `.zan-packages/`, or a
toolchain-relative `packages/` store).

- `System.Windows` — clipboard (text + CF_HDROP file lists), tray icon,
  screen enumeration, native message box.
- `System.Input` — global mouse/keyboard state, low-level hooks, hotkeys,
  background input service.
- `System.Media` — audio device playback; `System.Audio` (a namespace-`System`
  compat entry) delegates to it.
- `System.Automation` — UI automation over windows and elements.
- `System.Drawing` — image buffers, pixel ops, primitives, and printing
  (`System.Drawing.Printing`).
- `System.Management` — CPU/memory/storage/display/device/power/registry/
  system info and task scheduling (raw OS APIs, not WMI). Windows is the
  primary target; Linux and macOS cover the query-shaped classes (Cpu,
  Memory, SystemInfo, Storage, Display, Power, TaskScheduler — macOS reads
  sysctl/`mount`/`pmset`/crontab, never throws, and falls back to empty/0
  on unknown hardware such as Apple Silicon), while Device and Registry
  stay Windows-only and throw PlatformNotSupportedException elsewhere.
  Works headless; consumers include Zan.Gui.Browser (cache sizing) and
  Zan.Commercial (device fingerprint).

Everything here talks to the operating system directly (Win32 via
DllImport); server-shaped programs that never reference these namespaces
compile without this package.
