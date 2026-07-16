# SDL3 driver bundle

`zanc` resolves `[DllImport("zan_sdl3")]` from:

```text
stdlib/SDL3/drivers/<target>/
```

`Game.Arpg.ArpgUiRenderer` uses this bundle. The drivers workflow builds and
uploads each compiled target bundle as a GitHub Actions artifact; tagged driver
builds also publish the same bundles on the GitHub Release.

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

The macOS bundles (`macos-arm64`, `macos-x64`) each contain:

```text
libzan_sdl3.dylib   Zan ABI bridge (SDL_GPU sprite pipeline)
libSDL3.0.dylib     upstream SDL runtime
zan_sdl3.bundle     publish manifest
```

Use `scripts/stage_sdl3_macos.sh [macos-arm64|macos-x64]` to build the bridge
and stage it next to its SDL3 dependency. All install names are rewritten to
`@rpath` and re-signed ad-hoc so the published bundle is relocatable;
`zanc --publish` reads `zan_sdl3.bundle` to copy the runtime next to the
executable.

- `macos-arm64` builds natively: the bridge from the CMake `zan_sdl3` target,
  SDL3 from the Homebrew (pkg-config) install.
- `macos-x64` cross-compiles from an Apple Silicon host. Homebrew's SDL3 is
  arm64-only, so an x86_64 SDL3 runtime is provided via `SDL3_X64_LIB` (a
  prebuilt `libSDL3.0.dylib`) or `SDL3_SRC` (an SDL3 source tree the script
  builds for x86_64). The x86_64 bundle was verified under Rosetta 2
  (`SDL_GetVersion` == 3.4.10; the bridge `dlopen`s and exports the
  `zan_gpu_*` ABI).

The `*.dylib` / `*.so` / `*.dll` binaries are git-ignored per target; only the
committed `zan_sdl3.bundle` manifest records what to bundle. Each platform
directory must export the same `zan_sdl_*` / `zan_gpu_*` ABI. Linux (Vulkan)
and Windows (D3D12) native builds are still pending; macOS Metal (arm64 native,
x64 under Rosetta) is currently verified.
