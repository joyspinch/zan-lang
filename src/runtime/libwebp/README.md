# Vendored libwebp — decode-only, scalar subset

Source: [libwebp v1.4.0](https://github.com/webmproject/libwebp/releases/tag/v1.4.0)
(BSD-3-Clause, see `COPYING`). Used by the `zan_gui` driver for WebP image
decoding behind `zan_gui_image_load_mem` (see `gui_runtime.c`).

This is **not** the full library. What was dropped and why:

- **Encoder** (`src/enc/*`, `src/dsp/lossless_enc*`, `cost*`, `huffman_encode_utils`,
  `quant_levels_utils`, `random_utils`, `sharpyuv/`): we only decode.
- **SIMD files** (`*_sse2.c`, `*_sse41.c`, `*_neon.c`, `*_msa.c`, `*_mips*`):
  scalar C paths only — keeps the build portable across the three zan_gui
  targets (Win32 / X11 / Cocoa) without per-arch flags.
- **mux / demux / multithreading**: `thread_utils.c` is compiled without
  `WEBP_USE_THREAD`, giving the synchronous no-worker fallback.
- `HAVE_CONFIG_H` is **not** defined; no `config.h` is generated.

To upgrade: copy the same file set from the new tag (decoder `.c/.h` under
`src/dec`, scalar `.c` under `src/dsp`, decode-path utils, public headers
`decode.h encode.h format_constants.h mux_types.h types.h` under `src/webp`) and
re-apply the two local transformations below. This tree is compiled
**unity-build style**: `gui_runtime.c` `#include`s every `.c` file here
directly, so `scripts/build_ide.ps1`, `build_gallery.ps1` and the CMake
`zan_gui` target all keep working from the single `gui_runtime.c` compile.

Local transformations (re-apply on upgrade):

1. **Includes rewritten file-relative.** Upstream uses `"src/dec/..."`-style
   includes that rely on `-I<libwebp root>`; the runtime is compiled with no
   include flags at all (see the scripts above), so every such include was
   rewritten to its file-relative form (`src/utils/utils.h` →
   `../utils/utils.h`, same-directory ones dropped the prefix entirely).
2. **`src/dsp/lossless.h`**: dropped the unconditional
   `#include "src/enc/histogram_enc.h"` — the decode path uses no symbol
   from it (upstream quirk in 1.4.0), and that header drags in the encoder.
