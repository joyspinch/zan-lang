# SDL3 native bridge

Zan's public `SDL3` module links to `zan_sdl3`, a small ABI-normalizing C
bridge. The bridge is required because Zan currently lowers `int` to 64-bit
values while SDL3 uses a mixture of 8, 16, 32 and 64-bit C values and exposes
`SDL_Event` as a platform ABI union.

The bridge deliberately:

- exposes opaque window, renderer and texture handles as Zan `int`;
- converts every scalar at the C boundary;
- owns the native `SDL_Event` union and provides typed accessors;
- keeps SDL3-specific layouts out of Zan source;
- allows the bridge API to remain stable when SDL3 adds fields.

On Windows, run:

```powershell
.\scripts\stage_sdl3.ps1
```

This downloads the pinned official SDL release, builds `zan_sdl3.dll`, and
stages the runtime and import libraries under
`stdlib/SDL3/drivers/win-x64/`. Driver binaries are intentionally gitignored.
