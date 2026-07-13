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
3. Copies the toolchain and standard library into it.
4. Writes a `README.txt` describing prerequisites and usage.

## Layout of `dist\`

| Path            | Purpose                                                              |
| --------------- | ------------------------------------------------------------------- |
| `ZanIDE.exe`    | The IDE.                                                             |
| `zanc.exe`      | The Zan compiler. Kept next to the IDE.                              |
| `stdlib\`       | Standard library sources. `zanc` auto-includes what it needs.       |
| `examples\`     | Sample programs for the IDE's Examples pane (optional).             |
| `README.txt`    | Prerequisites + how to run.                                          |

## Why the stdlib lives inside the release folder

The IDE resolves the compiler relative to **its own executable directory**
(`IdeDemo.ExeDir()` → `zanc.exe`), and `zanc` in turn resolves its stdlib
relative to **its own** executable (`<exe_dir>/stdlib` or `<exe_dir>/../stdlib`).

Because `ZanIDE.exe`, `zanc.exe` and `stdlib\` all sit together in `dist\`,
the release is fully relocatable: copy the folder anywhere and the IDE still
finds the compiler and standard library — no absolute paths, no registry, no
environment variables.

## Runtime prerequisite

An **LLVM toolchain** (`clang`, `llvm-lib`) must be on `PATH` on the target
machine. `zanc` emits LLVM IR and shells out to `clang` to link the final
executable; without it, Build/Run/Debug inside the IDE will fail.
