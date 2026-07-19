# SDL3 for Zan

The `SDL3` standard-library module provides an ABI-safe, object-oriented Zan
surface over SDL3.

```zan
using System;
using SDL3;

namespace Demo;

class Program {
    static void Main() {
        if (!Sdl.Init(SdlInit.Video() + SdlInit.Events())) {
            Console.WriteLine(Sdl.Error());
            return;
        }

        SdlWindow window = SdlWindow.Create(
            "Demo", 1280, 720, SdlWindowFlags.Resizable());
        SdlRenderer renderer = SdlRenderer.Create(window);
        renderer.SetLogicalSize(640, 360, SdlLogicalPresentation.Letterbox());

        bool running = true;
        while (running) {
            while (SdlEvent.Poll()) {
                if (SdlEvent.IsQuit()) { running = false; }
            }

            renderer.SetColor(20, 24, 28, 255);
            renderer.Clear();
            renderer.Present();
        }

        renderer.Close();
        window.Close();
        Sdl.Quit();
    }
}
```

## Current surface

- process lifecycle, errors, version and timing;
- windows, size, title, visibility and fullscreen;
- renderer creation, VSync, logical presentation and primitive drawing;
- RGBA32 streaming/static textures, PNG/JPEG loading with preserved Alpha,
  legacy BMP loading, texture regions, render targets, color/alpha modulation
  and blend modes;
- event polling for window, keyboard, text and mouse events;
- keyboard state, scancodes, modifiers and mouse buttons;
- publish-time bundling through the native-driver mechanism.

The Windows x64 driver bundle is implemented and tested. Linux and macOS use
the same `zan_sdl_*` bridge ABI but still need native driver assembly scripts.

Run `scripts/stage_sdl3.ps1` after cloning or when updating SDL. Native binaries
are committed with the stdlib for supported targets.
