# Publishing the Zan IDE

The IDE is released as a **self-contained folder** at the canonical location:

```
d:\project\zan-lang\dist
```

This is the single, clean release output. It contains **only** the files an end
user needs — nothing else should be dropped here. Re-run the publish script to
refresh it rather than hand-editing.

## How to publish

```powershell
powershell -ExecutionPolicy Bypass -File scripts\publish_ide.ps1
```

The script (`scripts/publish_ide.ps1`):

1. Builds the IDE via `scripts/build_ide.ps1` (pass `-SkipBuild` to package the
   existing `build\` artifacts as-is).
2. Wipes and recreates `dist\`.
3. Copies the IDE, the compiler + bundled linker toolchain, companion CLIs,
   the bundled gdb debugger, the standard library, examples, and templates.
4. Writes a `README.txt` describing usage.

## Layout of `dist\`

| Path            | Purpose                                                              |
| --------------- | -------------------------------------------------------------------- |
| `ZanIDE.exe`    | The IDE.                                                             |
| `SDL3.dll`      | SDL3 runtime the IDE window uses; must sit next to `ZanIDE.exe`.     |
| `toolchain\`    | The compiler and everything it links with, as siblings: `zanc.exe`, `zan-lsp.exe`, `zan-dap.exe`, `zanpkg`/`zanfmt`/`zandoc`, the bundled linker (`ld.exe` + `mingw\` MinGW-w64 runtime), cross sysroots (`linux-musl\` …), runtime objects (`zanrt_io*`, `zanrt_sync*`), `zan_gui.lib`, and `debugger\bin\gdb.exe`. |
| `stdlib\`       | Standard library sources. `zanc` auto-includes what it needs.        |
| `examples\`     | Sample programs for the IDE's Examples pane (optional).              |
| `templates\`    | Built-in New Project templates (data-driven, `template.manifest`).   |
| `README.txt`    | Usage notes.                                                          |

## Why everything lives inside the release folder

The IDE resolves the compiler relative to **its own executable directory**
(`toolchain\zanc.exe`), and `zanc` in turn resolves its linker, sysroots, and
runtime objects as **its own siblings** in that same `toolchain\` folder, and
its stdlib relative to its executable (`<exe_dir>/stdlib` or
`<exe_dir>/../stdlib`).

Because `ZanIDE.exe`, `toolchain\` and `stdlib\` all travel together in
`dist\`, the release is fully relocatable: copy the folder anywhere and the
IDE still finds the compiler, linker, and standard library — no absolute
paths, no registry, no environment variables.

## Runtime prerequisite

**None for normal use.** `zanc` emits object code and links it in-process via
the bundled GNU `ld` + MinGW-w64 runtime in `toolchain\` (see
`docs/SELF_CONTAINED_TOOLCHAIN.md`), and `zan-dap` debugs with the bundled
`toolchain\debugger\bin\gdb.exe`. No LLVM/clang, MSVC, or system gdb install
is required on the target machine.

Fallbacks: if the linker files under `toolchain\` are removed, `zanc` falls
back to a system `clang` on `PATH`; if the bundled gdb is missing, `zan-dap`
falls back to a system/known gdb (`ZAN_GDB`).
