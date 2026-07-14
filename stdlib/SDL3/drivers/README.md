# SDL3 driver bundle

`zanc` resolves `[DllImport("zan_sdl3")]` from:

```text
stdlib/SDL3/drivers/<target>/
```

The Windows x64 bundle contains:

```text
libzan_sdl3.dll.a   link-time import library
zan_sdl3.dll        Zan ABI bridge
SDL3.dll            upstream SDL runtime
SDL3-LICENSE.txt    upstream SDL license
zan_sdl3.bundle     publish manifest
```

Use `scripts/stage_sdl3.ps1` to assemble the Windows x64 bundle from the
official SDL 3.4.12 MinGW development package.

Linux and macOS directories are reserved for native builds of the same bridge.
They must use the same exported `zan_sdl_*` ABI.
