# Native database drivers

Zan programs that use a native-backed `System.Data` module (PostgreSQL via
`libpq`, SQLite via `sqlite3`) link against a third-party shared/static library.
Those libraries are **not** present on an arbitrary target machine, so `zanc`
resolves them from this directory and — on `--publish` — bundles them with the
produced executable, giving a self-contained program that runs on a clean
machine.

## Layout

```
drivers/
  <target>/                 # win-x64, linux-x64, linux-arm64, macos-x64, macos-arm64
    <driver>.bundle         # manifest: runtime files to copy next to the exe (one per line)
    libsqlite3.dll.a        # (Windows) import library used at link time
    sqlite3.dll             # (Windows) runtime shared library
    libsqlite3.so           # (Linux)   runtime shared library (also used at link time)
    static/
      libsqlite3.a          # static archive used with --link-mode static
```

`<target>` mirrors the cross-compile toolchain names, so publishing for a
target reads the drivers built for that target. `zanc` looks for this tree next
to the compiler executable (staged there by the build); override with
`--driver-dir <dir>`.

## Link modes (`--link-mode`, `--publish`)

- **shared** (default): link against the import/shared library and copy the
  driver's runtime shared libraries (from the `<driver>.bundle` manifest, or a
  default name if absent) next to the executable.
- **static**: link the archive from `<target>/static/` directly into the
  executable — a single self-contained file, nothing copied.

## Manifest (`<driver>.bundle`)

One file name per line, relative to `drivers/<target>/`. This lets a driver ship
its own dependencies (e.g. `libpq` alongside the OpenSSL DLLs). Example
`win-x64/libpq.bundle`:

```
libpq.dll
libssl-3-x64.dll
libcrypto-3-x64.dll
```

## Populating drivers

Binaries are gitignored (see `.gitignore`); only the layout + manifests are
tracked. Drop the per-target binaries in, or build them, e.g. SQLite from the
amalgamation:

```
# Windows (MinGW-w64)
gcc -O2 -shared -o sqlite3.dll sqlite3.c -Wl,--out-implib,libsqlite3.dll.a
gcc -O2 -c sqlite3.c -o sqlite3.o && ar rcs static/libsqlite3.a sqlite3.o

# Linux
cc -O2 -fPIC -shared -o libsqlite3.so sqlite3.c
cc -O2 -fPIC -c sqlite3.c -o sqlite3.o && ar rcs static/libsqlite3.a sqlite3.o
```

## Security notes

- Prefer static linking for immutable, self-contained deployments; it removes
  DLL/`.so` search-order hijacking risk at the cost of larger binaries and
  needing a rebuild to pick up a driver security fix.
- With shared linking, the runtime library sits next to the executable, so it is
  loaded from the program directory rather than an attacker-controlled search
  path — keep the output directory write-protected in production.
- Only vetted driver builds should be placed here; the publisher trusts these
  binaries completely.
