# Zan IDE Architecture

## 1. Overview

The Zan IDE is a **lightweight, self-contained** development environment built on the existing `std.gui` framework from zan-new. No external GUI libraries (no Skia, no Qt, no Electron, no SDL). All rendering is **pure software pixel manipulation** using only OS-native APIs.

**Proven architecture from zan-new bootstrap:**
- **Software rendering** — Skia-compatible API implemented as pure C pixel operations
- **CSS Flexbox layout** — pure Zan layout engine
- **Vue-style reactive** — Signal<T> / Computed / Effect data binding
- **NaiveUI design** — 40+ production-quality widget components
- **OS-native window** — Win32 / X11 / Cocoa (thin shell, no framework)
- **OS-native text** — GDI DrawTextW / FreeType / CoreText for text rendering

**Targets:**
- Single executable < 20 MB
- Startup time < 500 ms
- Memory < 100 MB for typical project

---

## 2. Rendering Architecture

### 2.1 Pure Software Rendering Pipeline

```
Widget Tree → Layout (Flexbox) → Render Commands → Pixel Buffer → OS Present
```

1. **Widget tree**: reactive state changes trigger re-render of dirty subtrees
2. **Layout pass**: CSS Flexbox computes `Rect` for each widget
3. **Render pass**: widgets paint into a `DWORD*` pixel buffer
   - `fill_rect`, `fill_rounded_rect` — manual pixel fill with alpha blending
   - `draw_border_rect` — border rendering
   - `draw_text_buf` — OS-native text rendering into off-screen DIB, composited
   - `draw_path` — line/curve/polygon via Bresenham + anti-aliasing
4. **Present**: blit pixel buffer to window
   - Windows: `SetDIBitsToDevice()` via GDI
   - Linux: `XPutImage()` / Wayland shared memory buffer
   - macOS: `CGBitmapContext` → `CGImage` → layer

### 2.2 Rendering API (Skia-Compatible Naming)

The rendering layer exposes a Skia-like API for future upgrade path, but all implementations are pure C software rasterization:

```csharp
// Canvas API — all pure software rendering
Canvas.Clear(color);
Canvas.DrawRect(x, y, w, h, paint);
Canvas.DrawRoundRect(x, y, w, h, rx, ry, paint);
Canvas.DrawCircle(cx, cy, r, paint);
Canvas.DrawLine(x1, y1, x2, y2, paint);
Canvas.DrawPath(path, paint);
Canvas.DrawText(text, x, y, font, paint);
Canvas.Save();
Canvas.Restore();
Canvas.ClipRect(x, y, w, h);
Canvas.Translate(dx, dy);
```

### 2.3 Performance Strategy

- **Dirty region tracking**: only re-render changed widgets
- **Double buffering**: swap front/back pixel buffers to avoid flicker
- **Partial redraw**: clip rendering to dirty rectangles
- **Font glyph cache**: cache rendered glyphs to avoid repeated GDI calls
- **Batched present**: coalesce multiple updates within a frame (16ms / 60fps)

### 2.4 Future GPU Acceleration (Optional)

If performance demands it, the Skia-compatible API can be backed by:
- Direct2D on Windows
- Vulkan/OpenGL on Linux
- Metal on macOS

This is a **drop-in replacement** — widget code doesn't change, only the render backend.

---

## 3. IDE Layout

```
┌──────────────────────────────────────────────────────────────┐
│  Menu Bar  [File] [Edit] [View] [Build] [Debug] [Help]       │
├──────────────────────────────────────────────────────────────┤
│  Toolbar  [▶ Run] [⏸ Debug] [■ Stop] [🔨 Build]              │
├────────────┬─────────────────────────────────────────────────┤
│            │  Tab Bar  [main.zan ×] [utils.zan ×]            │
│  Project   ├─────────────────────────────────────────────────┤
│  Tree      │                                                 │
│            │  Code Editor                                    │
│  ▼ src/    │                                                 │
│    main.zan│  1│ using System;                                │
│    util.zan│  2│                                              │
│  ▼ lib/    │  3│ class Program {                              │
│    ...     │  4│     static void Main() {                     │
│            │  5│         Console.WriteLine("Hello");          │
│            │  6│     }                                        │
│            │  7│ }                                            │
│            │                                                 │
├────────────┴─────────────────────────────────────────────────┤
│  Output Panel  [Output] [Errors] [Debug Console]             │
│  > Building main.zan...                                      │
│  > Build succeeded. 0 errors, 0 warnings.                    │
│  > Running...                                                │
│  Hello                                                       │
└──────────────────────────────────────────────────────────────┘
```

Layout uses CSS Flexbox (same engine as `std.gui.layout`):
- **Main**: column direction (menu → toolbar → body → output)
- **Body**: row direction (project tree | editor area)
- **Editor area**: column direction (tab bar → code editor)
- All panels resizable via drag splitters

---

## 4. Core Components

### 4.1 Text Editor

Based on `std.gui.text` from zan-new (23KB of text editing logic):

| Feature | Implementation |
|---------|---------------|
| Text buffer | Piece table (efficient insert/delete) |
| Line rendering | Monospace font, fixed-width character grid |
| Syntax highlighting | Lexer-based (reuse `zanc` lexer as library) |
| Cursor | Blinking caret, multi-cursor support |
| Selection | Click-drag, Shift+Arrow, Ctrl+A |
| Undo/redo | Command stack with operation merging |
| Search/replace | Ctrl+F / Ctrl+H, regex support |
| Line numbers | Gutter with line numbers and breakpoint indicators |
| Auto-indent | Smart indent on Enter |
| Bracket matching | Highlight matching `()`, `{}`, `[]` |
| Word wrap | Optional, off by default for code |
| Scrolling | Smooth scroll with scrollbar widget |

### 4.2 Project Tree

| Feature | Implementation |
|---------|---------------|
| File tree | Recursive directory listing |
| Icons | File type icons (code, folder, image) |
| Context menu | New file, rename, delete, copy path |
| Drag & drop | Reorder files/folders |
| Filter | Search box to filter file names |
| Watch | Monitor filesystem for external changes |

### 4.3 Build Integration

| Feature | Implementation |
|---------|---------------|
| F5 | Compile + Run (debug build) |
| Ctrl+F5 | Compile + Run (release build) |
| F7 | Build only (no run) |
| Output panel | Compiler stdout/stderr |
| Error list | Parsed errors with file:line:col |
| Click to navigate | Click error → jump to source location |
| Build progress | Progress bar or spinner |

### 4.4 Code Intelligence (Phase 2)

Uses the compiler as a library (incremental parsing + binding):

| Feature | Data Source |
|---------|-----------|
| Autocomplete | Binder (symbols in scope) |
| Go to definition | Binder (symbol → declaration location) |
| Find references | Binder (all references to symbol) |
| Hover type info | Checker (resolved type of expression) |
| Signature help | Checker (function parameter types) |
| Rename symbol | Binder (all occurrences) |
| Error squiggles | Checker (real-time type errors) |

### 4.5 Debugger (Phase 3)

| Feature | Backend |
|---------|---------|
| Breakpoints | LLVM debug info + LLDB/WinDbg |
| Step over/into/out | Debug adapter protocol |
| Variable inspection | Read locals from debug info |
| Call stack | Stack frame enumeration |
| Watch expressions | Evaluate expressions at breakpoint |
| Conditional breakpoints | Expression evaluation |

---

## 5. Reactive Architecture

The IDE uses the same reactive system as `std.gui.reactive`:

```csharp
// IDE state management
var currentFile = new Signal<string>("");
var fileContent = new Signal<string>("");
var isDirty = new Computed<bool>(() => fileContent.Value != savedContent);
var errorCount = new Signal<int>(0);

// Reactive UI updates
Effect(() => {
    titleBar.Text = isDirty.Value 
        ? $"*{currentFile.Value} — Zan IDE" 
        : $"{currentFile.Value} — Zan IDE";
});

Effect(() => {
    statusBar.ErrorCount = errorCount.Value;
});
```

Widget tree re-renders automatically when reactive state changes — no manual invalidation needed.

---

## 6. Theme System

Reuses `std.gui.theme` NaiveUI design tokens:

```csharp
// Editor-specific tokens
struct EditorTheme {
    // Syntax colors
    Color Keyword;          // blue
    Color String;           // green
    Color Number;           // orange
    Color Comment;          // gray
    Color Type;             // cyan
    Color Function;         // yellow
    Color Operator;         // white
    Color Error;            // red underline
    
    // Editor chrome
    Color Background;       // editor background
    Color Gutter;           // line number area
    Color Selection;        // selection highlight
    Color CurrentLine;      // current line highlight
    Color Cursor;           // caret color
}
```

Light and dark themes are built-in. Custom themes via JSON configuration.

---

## 7. Window Management

### 7.1 Platform Shell

| Platform | Window API | Text Rendering | Present |
|----------|-----------|---------------|---------|
| Windows | Win32 `CreateWindowEx` | GDI `DrawTextW` → DIB | `SetDIBitsToDevice` |
| Linux | X11 `XCreateWindow` | FreeType + HarfBuzz | `XPutImage` |
| macOS | Cocoa `NSWindow` | CoreText | `CGBitmapContext` |

### 7.2 Borderless Custom Title Bar

From zan-new's `gui_runtime.c`:
- Custom `WM_NCCALCSIZE` handling removes native title bar
- Custom `WM_NCHITTEST` for resize borders and title bar drag
- Drawn entirely by the widget system (close/minimize/maximize buttons)
- Supports window snapping and maximize

### 7.3 DPI Awareness

- Windows: Per-monitor DPI awareness v2
- Linux: Read `Xft.dpi` from X resources
- macOS: Retina scaling via `backingScaleFactor`
- All coordinates in logical pixels, rendering scales to physical pixels

---

## 8. File Format

### 8.1 Project File (project.zan)

```
project MyApp {
    version = "1.0.0"
    target = "executable"
    entry = "src/main.zan"
}
```

### 8.2 IDE Settings (per-project)

```json
// .zan-ide/settings.json
{
    "editor": {
        "font_family": "Consolas",
        "font_size": 14,
        "tab_size": 4,
        "use_spaces": true,
        "show_line_numbers": true,
        "word_wrap": false
    },
    "theme": "dark",
    "window": {
        "width": 1280,
        "height": 800,
        "maximized": false
    }
}
```

---

## 9. Build & Distribution

### 9.1 Single Executable

The IDE is compiled into a single executable containing:
- Compiler (`zanc`) as a library
- Runtime library
- GUI framework (built from `std.gui`)
- Standard library sources (embedded as resources)
- Default theme

### 9.2 Size Budget

| Component | Size |
|-----------|------|
| Compiler core | ~3 MB |
| LLVM libraries (linked) | ~10 MB |
| GUI + widgets | ~2 MB |
| Runtime | ~500 KB |
| Stdlib sources (embedded) | ~1 MB |
| Icons + resources | ~500 KB |
| **Total** | **< 20 MB** |

### 9.3 Portable Mode

No installation required:
- Drop `zan-ide` executable anywhere
- It auto-detects stdlib in adjacent `stdlib/` directory
- Settings stored in `.zan-ide/` next to the executable
- Similar to aardio: single directory, fully self-contained
