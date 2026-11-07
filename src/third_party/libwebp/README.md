# Vendored libwebp — decode-only subset

Source: [libwebp v1.6.0](https://github.com/webmproject/libwebp/releases/tag/v1.6.0)
(BSD-3-Clause, see `COPYING`). Used by the independent `zan_image` driver for
WebP image decoding behind `zan_image_load_mem` (see `zan_image.c`).

This is **not** the full library. What was dropped and why:

- **Encoder** (`src/enc/*`, `src/dsp/lossless_enc*`, `cost*`,
  `huffman_encode_utils`, `quant_levels_utils`, `sharpyuv/`): we only decode.
- **Non-x86 SIMD** (`*_sse41.c`, `*_neon.c`, `*_msa.c`, `*_mips*`, avx2):
  the `*_sse2.c` files ARE carried — they self-disable unless
  `WEBP_HAVE_SSE2`, which `src/webp/config.h` sets only for x86-64 — so the
  same tree compiles scalar on arm64/riscv without per-file flags.
- **mux / demux / multithreading**: `thread_utils.c` is compiled without
  `WEBP_USE_THREAD`, giving the synchronous no-worker fallback.
- `src/webp/config.h` is a **hand stub** (not upstream, not generated):
  `HAVE_CONFIG_H` is defined by `zan_image.c` around the vendored includes;
  the stub turns SSE2 on for x86-64 and leaves SSE41/threads off (a single-TU
  compile cannot express per-file `-msse4.1`).

To upgrade: copy the same file set from the new tag (decoder `.c/.h` under
`src/dec`, the carried `.c` set under `src/dsp`, decode-path utils, public
headers `decode.h encode.h format_constants.h mux_types.h types.h` under
`src/webp`) and re-apply the local transformations below. This tree is
compiled **unity-build style**: `zan_image.c` `#include`s every `.c` file here
directly. The CMake `zan_image` target and native-driver recipes compile that
single translation unit independently of GUI.

Local transformations (re-apply on upgrade; all three bit on 1.4.0→1.6.0):

1. **Includes rewritten file-relative.** Upstream uses `"src/dec/..."`-style
   includes that rely on `-I<libwebp root>`; the runtime is compiled with no
   include flags at all (see the scripts above), so every such include must
   be rewritten to its file-relative form (`src/utils/utils.h` →
   `../utils/utils.h`, same-directory ones drop the prefix entirely).
2. **`src/utils/quant_levels_dec_utils.c`**: rename its `clip_8b` →
   `clip_8b_ql` (definition + call sites). Since 1.6.0 `src/dsp/dec.c` also
   defines a static-inline `clip_8b`; separate TUs never see the clash, the
   unity build does. (The 1.4.0 tree carried the same rename.)
3. **`src/dsp/lossless.h`**: 1.4.0 unconditionally included
   `"src/enc/histogram_enc.h"` (decode path uses no symbol from it) and had
   to be dropped; 1.6.0 no longer has that include — nothing to do unless it
   reappears.
