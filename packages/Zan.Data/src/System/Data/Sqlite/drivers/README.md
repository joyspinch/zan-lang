# SQLite native driver

`System.Data.Sqlite` links against the `sqlite3` C library, which is **not**
present on an arbitrary target. `zanc` resolves it from this directory —
relative to the stdlib module that owns it — and, on `--publish`, bundles the
runtime library next to the produced executable, giving a self-contained
program.

## Layout

```
System/Data/Sqlite/drivers/
  <target>/                 # win-x64, linux-x64, linux-arm64, macos-x64, macos-arm64
    sqlite3.bundle          # manifest: runtime files to copy next to the exe (one per line)
    libsqlite3-0.dll        # (Windows) runtime shared library
    libsqlite3.dll.a        # (Windows) import library used at link time
    libsqlite3.so           # (Linux)   runtime shared library (also used at link time)
    static/
      libsqlite3.a          # static archive used with --link-mode static
```

`<target>` mirrors the cross-compile toolchain names, so publishing for a
target reads the driver built for that target. `zanc` derives this path as
`<stdlib>/System/Data/Sqlite/drivers/<target>/`; override with `--driver-dir`.

## Link modes (`--link-mode`, `--publish`)

- **shared** (default): link against the import/shared library and copy the
  runtime shared libraries (from `sqlite3.bundle`, or a default name if absent)
  next to the executable.
- **static**: link the archive from `<target>/static/` directly into the exe.

## Committed bundles

The manifests and compiled driver binaries are tracked per target so the
stdlib works without a separate artifact download. The drivers workflow
refreshes these bundles.
