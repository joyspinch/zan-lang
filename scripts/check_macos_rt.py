#!/usr/bin/env python3
"""Check that the bundled Mach-O runtime objects import nothing outside the
bundled libSystem stub.

Every symbol a cross-linked macOS program pulls in must be exported by
toolchain/macos/libSystem.tbd or defined by another bundled object in the same
arch directory, otherwise `zanc --target macos-*` fails at link time on the
user's machine. `zan_co_ready` is provided by the compiled program itself and
is the only expected exception.

A GUI program's compat object also imports `zan_gui_*` entry points; those come
from the platform's GUI driver dylib (packages/Zan.Gui/.../libzan_gui.dylib),
which the cross link adds to the link line. They are therefore provided by the
driver, not by libSystem -- they are read out of the driver dylib instead of
being hard-coded, so a driver that grows a new entry point does not turn this
check red (that is how the macOS leg of the toolchain workflow died on
2026-09-20: the checker did not know about the driver, the check step failed,
and the job never reached its commit step -- so no macOS object was refreshed
for a month).

    python3 scripts/check_macos_rt.py
"""
import os
import re
import struct
import sys

LC_SYMTAB = 0x2
PROVIDED_BY_PROGRAM = {"_zan_co_ready"}


def symbols(path):
    """Return (undefined, defined) symbol names of a Mach-O object."""
    d = open(path, "rb").read()
    magic, _cputype, _cpusub, _ftype, ncmds = struct.unpack_from("<IiiII", d, 0)
    if magic != 0xFEEDFACF:
        raise SystemExit(f"{path}: not a 64-bit little-endian Mach-O object")
    off, undef, defined = 32, set(), set()
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", d, off)
        if cmd == LC_SYMTAB:
            symoff, nsyms, stroff, _strsize = struct.unpack_from("<IIII", d, off + 8)
            for i in range(nsyms):
                n_strx, n_type, _n_sect, _n_desc, n_value = struct.unpack_from(
                    "<IBBHQ", d, symoff + i * 16)
                end = d.index(b"\0", stroff + n_strx)
                name = d[stroff + n_strx:end].decode()
                if (n_type & 0x0E) == 0 and (n_type & 0xE0) == 0 and n_value == 0:
                    undef.add(name)
                elif (n_type & 0x0E) != 0:
                    defined.add(name)
        off += cmdsize
    return undef, defined


def driver_symbols(root, sub):
    """Symbols the platform's GUI driver dylib exports.

    The driver is linked next to a macOS GUI program, so its entry points are
    resolved at link time; the compat object is allowed to import them.
    """
    path = os.path.join(root, "packages", "Zan.Gui", "src", "Gui", "drivers",
                        "macos-" + sub, "libzan_gui.dylib")
    if not os.path.exists(path):
        return set()
    try:
        return symbols(path)[1]
    except SystemExit:
        return set()


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    mac = os.path.join(root, "toolchain", "macos")
    exported = set(re.findall(
        r"_[A-Za-z0-9_$.]+", open(os.path.join(mac, "libSystem.tbd")).read()))
    bad = 0
    for sub in ("arm64", "x64"):
        objects = sorted(f for f in os.listdir(os.path.join(mac, sub))
                         if f.endswith(".o"))
        # zanc links the whole arch directory, so an import one sibling object
        # defines is resolved at link time and is not a missing symbol.
        bundled = set()
        for name in objects:
            bundled |= symbols(os.path.join(mac, sub, name))[1]
        provided = bundled | PROVIDED_BY_PROGRAM | driver_symbols(root, sub)
        for name in objects:
            path = os.path.join(mac, sub, name)
            missing = sorted(
                symbols(path)[0] - exported - provided)
            print(f"{sub}/{name}: {'ok' if not missing else 'MISSING ' + ', '.join(missing)}")
            bad += len(missing)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
