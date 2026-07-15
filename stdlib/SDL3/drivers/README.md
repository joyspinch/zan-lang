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

The macOS arm64 bundle contains:

```text
libzan_sdl3.dylib   Zan ABI bridge (SDL_GPU sprite pipeline)
libSDL3.0.dylib     upstream SDL runtime
zan_sdl3.bundle     publish manifest
```

Use `scripts/stage_sdl3_macos.sh` to build the bridge with the CMake
`zan_sdl3` target and stage it next to its SDL3 dependency. All install
names are rewritten to `@rpath` and re-signed ad-hoc so the published bundle
is relocatable; `zanc --publish` reads `zan_sdl3.bundle` to copy the runtime
next to the executable.

The `*.dylib` / `*.so` / `*.dll` binaries are git-ignored per target; only the
committed `zan_sdl3.bundle` manifest records what to bundle. Each platform
directory must export the same `zan_sdl_*` / `zan_gpu_*` ABI. Linux (Vulkan)
and Windows (D3D12) native builds are still pending; only macOS Metal is
currently verified.
