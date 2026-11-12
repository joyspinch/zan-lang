# Linux x86-64 musl target runtime

This directory contains generated target objects and static archives used by
Zan's `linux-x64` and `linux-musl` targets. The newly generated `libc++.a`,
`libc++abi.a`, `libunwind.a`, and `libcompiler_rt.a` were produced using **Zig
0.13.0**, installed in WSL at `/opt/zig/0.13.0`, for `x86_64-linux-musl`.
The Windows Zig 0.15.1 installation was not their producer.

The rebuild script links a static C++ exception probe with the same target
and selects libc, startup objects, libc++, libc++abi, libunwind, and compiler
runtime inputs from that link. These are target libraries, not host libraries.
The generated input manifest is
`build/test-out/linux-runtime/linux-musl/zig-sysroot-inputs.json`; cache paths
in it are local build evidence, not redistributable source locations.

Rebuild from the repository root with:

```sh
sh scripts/build_linux_rt.sh /opt/zig/0.13.0/zig linux-musl
```

See [Linux static cross runtimes](../../docs/BUILD_TOOLCHAIN.md#linux-静态交叉运行时)
for the build gate, prerequisites, staging, and target execution checks.

## Upstream licenses and source locations

Third-party components retain their upstream licenses. The repository's Zan
license does not replace these notices. Paths below are relative to the actual
producer installation, `/opt/zig/0.13.0/`.

| Artifacts | Bundled source | Installed license / copyright notice |
|---|---|---|
| `libc++.a` | `lib/libcxx/` | `lib/libcxx/LICENSE.TXT` — Apache License 2.0 with LLVM Exceptions, including the additional terms retained in that file |
| `libc++abi.a` | `lib/libcxxabi/` | `lib/libcxxabi/LICENSE.TXT` — Apache License 2.0 with LLVM Exceptions, including the additional terms retained in that file |
| `libunwind.a` | `lib/libunwind/` | `lib/libunwind/LICENSE.TXT` — Apache License 2.0 with LLVM Exceptions, including the additional terms retained in that file |
| `libcompiler_rt.a` | `lib/compiler_rt.zig`, `lib/compiler_rt/` | Zig's `LICENSE` (MIT/Expat); preserve any applicable notices in individual source files as well |
| `libc.a`, `crt1.o`, `crti.o`, `crtn.o` | `lib/libc/musl/` | `lib/libc/musl/COPYRIGHT` — musl's MIT license, contributor list, and component notices |

[LICENSE-Zig.txt](LICENSE-Zig.txt) reproduces the complete 1,080-byte root
`LICENSE` text read from the local Zig 0.13.0 installation. The complete LLVM
and musl notices below were retrieved from **Zig's upstream `0.13.0` tag**
on 2026-11-11. They are tagged producer-source copies, not copies read from
WSL: local full-file reads remained unavailable, so local byte-for-byte
identity has not been established. No Zig 0.15.1 license was substituted.

Each link identifies the exact upstream raw source. Because raw downloads
failed or were incomplete, retrieval used GitHub's Contents API at
`https://api.github.com/repos/ziglang/zig/contents/<source-path>?ref=0.13.0`.
All four API responses returned HTTP 200. Their base64 contents were decoded,
checked against the reported byte size and Git blob SHA-1, and copied without
text normalization. The full files retain LLVM Exceptions, legacy terms,
and musl contributor/component notices, rather than only their headers.

| Local notice | Tagged upstream source | Bytes | SHA-256 |
|---|---|---:|---|
| [LICENSE-libcxx.txt](LICENSE-libcxx.txt) | [lib/libcxx/LICENSE.TXT](https://raw.githubusercontent.com/ziglang/zig/0.13.0/lib/libcxx/LICENSE.TXT) | 16703 | `539dd7aed86e8a4f12cbdd0e6c50c189c7d74847e4fecc64ce2c6ee3a01da38b` |
| [LICENSE-libcxxabi.txt](LICENSE-libcxxabi.txt) | [lib/libcxxabi/LICENSE.TXT](https://raw.githubusercontent.com/ziglang/zig/0.13.0/lib/libcxxabi/LICENSE.TXT) | 16706 | `e2b35be49f7284a45b7baca8fc7b3ab7440e7902392b2528a457816b5bb2a15c` |
| [LICENSE-libunwind.txt](LICENSE-libunwind.txt) | [lib/libunwind/LICENSE.TXT](https://raw.githubusercontent.com/ziglang/zig/0.13.0/lib/libunwind/LICENSE.TXT) | 16706 | `b5efebcaca80879234098e52d1725e6d9eb8fb96a19fce625d39184b705f7b6d` |
| [COPYRIGHT-musl.txt](COPYRIGHT-musl.txt) | [lib/libc/musl/COPYRIGHT](https://raw.githubusercontent.com/ziglang/zig/0.13.0/lib/libc/musl/COPYRIGHT) | 6204 | `f9bc4423732350eb0b3f7ed7e91d530298476f8fec0c6c427a1c04ade22655af` |

These LLVM and musl notices accompany the Zig MIT notice; that notice alone
does not cover LLVM or musl. Individual compiler-runtime source notices have
not yet been audited.

## Optional libraries

Existing optional `libfreetype.a`, `libfontconfig.a`, and `libexpat.a` may be
present and are preserved by the runtime refresh. They retain their respective
FreeType, Fontconfig, and Expat upstream licenses and notices; this Zig runtime
provenance does not establish their producer or replace those notices. Any
existing `libgcc.a` is likewise a separately supplied archive, outside the four
new archives documented here.
