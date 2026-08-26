---
name: testing-zanide-uidriver
description: How to drive the real Windows build\ZanIDE.exe end-to-end with Gui.UiDriver (ZAN_UI_SCRIPT) from a remote agent — launcher pattern, hit ids, child-window (Publish dialog) clicking, idle-loop stalls, and how to verify a Publish run.
---

# End-to-end testing of the real ZanIDE (Windows) with Gui.UiDriver

Command grammar and probe formats: `docs/ui-driver.md`. This file is about the
practical traps when an agent drives `build\ZanIDE.exe` over a remote shell.

## Launcher pattern (throwaway, keep it in `_scratch/`)

A PowerShell launcher that works well:

1. set `ZAN_UI_SCRIPT` / `ZAN_UI_OUT`, `Start-Process build\ZanIDE.exe -PassThru`
   (never kill `ZanIDE` by name — the user usually has their own instance open;
   only touch your own PID),
2. every few seconds enumerate the *visible windows of that PID*
   (`EnumWindows` + `GetWindowThreadProcessId`), log title/size and
   `user32!IsHungAppWindow(hwnd)` (objective "is the UI still responsive"
   evidence), and `CopyFromScreen` the window rect,
3. kill the process after a hard timeout, then print `results.log`.

Gotchas:

- Run the launcher **detached** (`Start-Process powershell ... -RedirectStandardOutput
  launcher.log`) and poll the log file. If the launcher runs in the foreground of a
  remote shell call, a tool/HTTP timeout kills the whole process tree — including
  the IDE — mid-test.
- PowerShell 5.1 reads BOM-less `.ps1` as ANSI/GBK: **no non-ASCII literals in the
  launcher**. Match the main window with `-match "ZanIDE"` and treat any other
  window of that PID as the dialog instead of matching Chinese titles.
- `CopyFromScreen` grabs whatever is on top of that screen rect. If the IDE is not
  foreground, screenshots show unrelated windows. `SetForegroundWindow` before the
  shots and don't trust pixels from a run where the IDE lost focus.
- The Chinese assertions inside the `.txt` driver script *do* work (UiDriver reads
  UTF-8), e.g. `assert probe log contains 发布产物打包完成`.

## The IDE event loop blocks when idle — driver `wait`s freeze

`UiDriver.Tick` is advanced from the event loop, so when the IDE has nothing to
draw the script stops progressing (a `wait 10000` can hang for minutes). Symptom:
only the first few `dump` files appear and the launcher has to time-kill.
Workaround that works: from the launcher, nudge the real cursor a few pixels
inside the window every couple of seconds (`SetCursorPos` only, no clicks) to keep
frames coming. Keep driver scripts short for the same reason.

## Child windows (Publish dialog) are NOT in the main hit table

`dump hitregions` with the 发布项目 dialog open returns exactly the same table as
before it opened, and `clickid` cannot reach the dialog: injected events go to the
main window only. Drive such dialogs with a real OS click
(`SetCursorPos` + `mouse_event`) at **proportional** offsets inside the dialog's
`GetWindowRect` (from a screenshot of it, e.g. the 发布 button sits at
~x=0.85, y=0.91 of the dialog rect; 取消 at ~x=0.61).
While the dialog is up the app is in a modal pump: the click may only be processed
tens of seconds later (add the cursor-nudge keep-alive to speed this up), and a
click that appears to close the dialog *without* starting the action has been seen
once — always confirm from the output panel/log, never from "the dialog closed".

## Useful ZanIDE hit ids (2026-08 build, may drift — re-`dump hitregions`)

- Ribbon 运行/调试/热重载/发布 = `100007..100010` (发布 = `100010`).
- Project tree rows are `150000 + row`, in visible order; expanding a folder
  inserts new ids, so re-dump after each expand.
- Probes: `dump probe log` = output panel text, `dump probe editor` = active
  editor buffer (best way to prove which tab is active / that a click landed).
  Caveat: the `log` probe came back **empty** in a run where no publish job was
  started — if a probe file is 0 bytes, fall back to a screenshot before
  concluding the log is empty.

## Verifying a Publish end-to-end (the "卡在正在打包发布产物" class of bug)

Project setup: copy `templates\console\console` into `_scratch\...`, set
`platform = windows-x64` in `zan.proj` (otherwise all 6 targets cross-compile),
and point `build\config\lastproject.txt` at it so the IDE opens it on startup.
**Back up every file in `build\config` first** (`lastproject.txt`,
`recent_projects.txt`, `config.cfg`, `dock.cfg`) — the IDE rewrites them and the
originals are the user's real settings.

Assertions that actually prove the fix:
- output panel goes `> 正在打包发布产物 ...` → `> 发布产物打包完成。`
  (and none of `> 打包失败：`, `> 打包超时，停在：`,
  `> 上一次超时的打包线程仍在运行`);
- `publish\windows-x64\<name>.exe` exists (delete `publish\` before the run so
  the artifact is provably fresh) and **running it prints the entry file's output**;
- to prove the publish target is frozen and independent of the active tab, add a
  second file with its own `Main` (e.g. printing `OTHER-MAIN-WRONG-ENTRY`), make it
  the active tab before pressing 发布, and assert the produced exe still prints the
  real entry's text;
- `IsHungAppWindow` stays `False` for every sample, and a `clickid` after the
  publish still changes the `editor` probe.

Cannot be triggered from the outside: the 10-minute packaging watchdog
(`> 打包超时，停在：<阶段>`) and `PubPack.Start()` returning -1 — both need an
injected hang, i.e. a temporary code change.

## Devin Secrets Needed

None. Access to the user's Windows workspace goes through the local file-bridge
knowledge note (base URL + bearer token live there).
