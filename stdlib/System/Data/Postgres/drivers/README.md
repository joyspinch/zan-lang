# PostgreSQL native driver

`System.Data.Postgres` links against `libpq` (the PostgreSQL C client), which is
**not** present on an arbitrary target. `zanc` resolves it from this directory —
relative to the stdlib module that owns it — and, on `--publish`, bundles the
runtime library (and any dependencies listed in the manifest) next to the
produced executable.

## Layout

```
System/Data/Postgres/drivers/
  <target>/                 # win-x64, linux-x64, linux-arm64, macos-x64, macos-arm64
    pq.bundle               # manifest: runtime files to copy next to the exe (one per line;
                            #   lets libpq ship its OpenSSL / gssapi deps)
    libpq.dll               # (Windows) runtime shared library
    libpq.dll.a             # (Windows) import library used at link time
    libpq.so                # (Linux)   runtime shared library (also used at link time)
    static/
      libpq.a               # static archive used with --link-mode static
```

`zanc` derives this path as `<stdlib>/System/Data/Postgres/drivers/<target>/`;
override with `--driver-dir`. `<target>` mirrors the cross-compile toolchain
names. See the SQLite driver README for link-mode details — the behavior is
identical.

## Committed bundles

The manifests and compiled driver binaries are tracked per target so the
stdlib works without a separate artifact download. The drivers workflow
refreshes these bundles.
