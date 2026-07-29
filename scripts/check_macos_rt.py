#!/usr/bin/env python3
"""Check that the bundled Mach-O runtime objects import nothing outside the
bundled libSystem stub.

Every symbol a cross-linked macOS program pulls in must be exported by
toolchain/macos/libSystem.tbd, otherwise `zanc --target macos-*` fails at link
time on the user's machine. `zan_co_ready` is provided by the compiled program
itself and is the only expected exception.

    python3 scripts/check_macos_rt.py
"""
import os
import re
import struct
import sys

LC_SYMTAB = 0x2
PROVIDED_BY_PROGRAM = {"_zan_co_ready"}


def undefined_symbols(path):
    d = open(path, "rb").read()
    magic, _cputype, _cpusub, _ftype, ncmds = struct.unpack_from("<IiiII", d, 0)
    if magic != 0xFEEDFACF:
        raise SystemExit(f"{path}: not a 64-bit little-endian Mach-O object")
    off, out = 32, set()
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", d, off)
        if cmd == LC_SYMTAB:
            symoff, nsyms, stroff, _strsize = struct.unpack_from("<IIII", d, off + 8)
            for i in range(nsyms):
                n_strx, n_type, _n_sect, _n_desc, n_value = struct.unpack_from(
                    "<IBBHQ", d, symoff + i * 16)
                if (n_type & 0x0E) == 0 and (n_type & 0xE0) == 0 and n_value == 0:
                    end = d.index(b"\0", stroff + n_strx)
                    out.add(d[stroff + n_strx:end].decode())
        off += cmdsize
    return out


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    mac = os.path.join(root, "toolchain", "macos")
    exported = set(re.findall(
        r"_[A-Za-z0-9_$.]+", open(os.path.join(mac, "libSystem.tbd")).read()))
    bad = 0
    for sub in ("arm64", "x64"):
        for name in ("zanrt_io.o", "zanrt_io_mt.o", "zanrt_sync.o"):
            path = os.path.join(mac, sub, name)
            missing = sorted(
                undefined_symbols(path) - exported - PROVIDED_BY_PROGRAM)
            print(f"{sub}/{name}: {'ok' if not missing else 'MISSING ' + ', '.join(missing)}")
            bad += len(missing)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
