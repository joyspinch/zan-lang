#!/usr/bin/env python3
"""Normalize gen1 (self-hosted compiler) LLVM IR text for golden comparison.

The golden gate uses --fnhash raw function fingerprints, not normalize().
Each fingerprint includes the original function signature and body text;
SSA names, labels, comments and whitespace inside that text remain sensitive.
Module-level globals, declarations, types and attributes are not fingerprinted.

The separate normalized-text modes attempt to reduce naming noise:

  - every local SSA name becomes %vN, numbered by first *definition* inside
    its function (parameters first, in signature order) -- a pure rename
    vanishes, any change to the instruction sequence survives. Two passes per
    function: loop back-edge phis reference values defined later in file
    order, so defs must be collected before rewriting;
  - every block label becomes LN by block order, value and label names share
    the `%` prefix but are disjoint sets, so one rewrite pass covers plain
    references, `label %x` branches and phi `[ %v, %pred ]` pairs alike;
  - compiler-internal globals (@.xxx) become @.gN by first appearance
    (user-visible @identifiers and runtime symbols keep their names);
  - `declare` lines are sorted and deduplicated, comments and blank lines are
    dropped, the attributes section is kept with group ids renumbered.

Usage:
  python scripts/normalize_ir.py <in.ll> <out.lln>
  python scripts/normalize_ir.py --check <in.ll> <golden.lln>   (exit 1 = diff)
  python scripts/normalize_ir.py --fnhash <in.ll> <out.hashes>

--fnhash emits one "<sha16> <function name>" line per defined function,
sorted by the complete output line. sha16 is the first 16 hex digits of
SHA-256 over raw function lines joined with LF, without a trailing LF.
This compact, unversioned golden format does not establish semantic equivalence.
"""
import os
import re
import sys
import tempfile

NAME = r'([A-Za-z_.$][\w.$]*)'


def _collect_function(lines, start):
    """Collect def/label order for the function whose header is lines[start].

    Returns (end_index, value_map, label_map). value_map: name -> index,
    signature params first then `%x =` definitions in file order. label_map:
    label -> block order.
    """
    vmap = {}
    lmap = {}
    sig = lines[start]
    p0 = sig.find("(")
    p1 = sig.rfind(")")
    for pname in re.findall(r'%' + NAME, sig[p0:p1 + 1]):
        if pname not in vmap:
            vmap[pname] = len(vmap)
    i = start + 1
    while i < len(lines):
        s = lines[i].strip()
        if s == "}":
            return i, vmap, lmap
        if s.startswith("define "):
            raise ValueError("nested function definition at line %d" % (i + 1))
        m = re.match(r'^' + NAME + r':\s*$', s)
        if m:
            if m.group(1) not in lmap:
                lmap[m.group(1)] = len(lmap)
        else:
            m = re.match(r'^%([\w.$]+)\s*=', s)
            if m and m.group(1) not in vmap:
                vmap[m.group(1)] = len(vmap)
        i += 1
    raise ValueError("unterminated function at line %d" % (start + 1))


def _rewrite_body(line, vmap, lmap, tmap):
    def repl(m):
        name = m.group(1)
        if name in vmap:
            return "%%v%d" % vmap[name]
        if name in lmap:
            return "L%d" % lmap[name]
        if name in tmap:
            return "%%t%d" % tmap[name]
        return m.group(0)
    return re.sub(r'%' + NAME, repl, line)


def normalize(text):
    lines = text.splitlines()

    # ---- pass 1: collect module maps ----
    gmap = {}        # @.name -> index (internal globals)
    tmap = {}        # %name = type ... -> index (type aliases)
    amap = {}        # attribute group #N -> #A<N>
    fstarts = []     # indices of `define` lines
    for i, raw in enumerate(lines):
        s = raw.strip()
        m = re.match(r'^(@\.[\w.$]+)\s*=', s)
        if m and m.group(1) not in gmap:
            gmap[m.group(1)] = len(gmap)
        m = re.match(r'^%([\w.$]+)\s*=\s*type\b', s)
        if m and m.group(1) not in tmap:
            tmap[m.group(1)] = len(tmap)
        for g in re.findall(r'@(\.[\w.$]+)', s):
            if g not in gmap:
                gmap[g] = len(gmap)
        for a in re.findall(r'#(\d+)', s):
            if a not in amap:
                amap[a] = len(amap)
        if s.startswith("define "):
            fstarts.append(i)

    # per-function maps, collected up front (pass 1b)
    fmaps = {}
    for fi in fstarts:
        end, vmap, lmap = _collect_function(lines, fi)
        fmaps[fi] = (end, vmap, lmap)

    # ---- pass 2: rewrite ----
    out = []
    declares = []
    attr_defs = []
    fi_set = set(fstarts)
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        if s == "" or s.startswith(";") or s.startswith("!"):
            i += 1
            continue
        m = re.match(r'^attributes #(\d+) = \{(.*)\}\s*$', s)
        if m:
            attr_defs.append("attributes #A%s = {%s}" % (amap[m.group(1)], m.group(2)))
            i += 1
            continue
        if s.startswith("declare"):

            def _arepl(m):
                return "#A%s" % amap[m.group(1)]

            declares.append(re.sub(r'#(\d+)', _arepl, s))
            i += 1
            continue
        if i in fi_set:
            end, vmap, lmap = fmaps[i]
            out.append(_rewrite_body(s, vmap, lmap, tmap))
            for j in range(i + 1, end + 1):
                t = lines[j].strip()
                if t == "" or t.startswith(";"):
                    continue
                m = re.match(r'^' + NAME + r':\s*$', t)
                if m:
                    out.append(_rewrite_body("%" + m.group(1), vmap, lmap, tmap) + ":")
                else:
                    out.append("  " + _rewrite_body(t, vmap, lmap, tmap))
            i = end + 1
            continue
        # module-level global / type alias: rewrite type names then @.names
        s = _rewrite_body(s, {}, {}, tmap)
        out.append(re.sub(r'@(\.[\w.$]+)',
                          lambda m: "@.g%s" % gmap[m.group(1)]
                          if m.group(1) in gmap else m.group(0), s))
        i += 1

    result = sorted(set(declares)) + [""] + attr_defs + [""] + out
    return "\n".join(result) + "\n"


def _write_atomic(path, text):
    """Publish only a complete output; preserve any previous file on failure."""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", newline="\n", delete=False,
                dir=os.path.dirname(os.path.abspath(path)),
                prefix=".normalize-ir-", suffix=".tmp") as f:
            temporary = f.name
            f.write(text)
        os.replace(temporary, path)
    finally:
        if temporary is not None and os.path.exists(temporary):
            os.unlink(temporary)


def main():
    args = sys.argv[1:]
    mode = "emit"
    if args and args[0] == "--check":
        mode = "check"
        args = args[1:]
    if args and args[0] == "--fnhash":
        mode = "fnhash"
        args = args[1:]
    if len(args) != 2:
        print(__doc__)
        return 2
    with open(args[0], "r", encoding="utf-8") as f:
        text = f.read()
    if mode == "fnhash":
        import hashlib
        lines = text.splitlines()
        fstarts = [i for i, l in enumerate(lines)
                   if l.strip().startswith("define ")]
        rows = []
        names = set()
        for fi in fstarts:
            # Match the emitter's one-line, unquoted function-header subset.
            header = re.fullmatch(
                r'define\s+[^@\n]+@([A-Za-z_.$][\w.$]*|[0-9]+)\(.*\).*\{\s*',
                lines[fi].strip())
            if header is None:
                raise ValueError("unsupported function header at line %d" % (fi + 1))
            name = header.group(1)
            if name in names:
                raise ValueError("duplicate function: %s" % name)
            names.add(name)
            end, _v, _l = _collect_function(lines, fi)
            body = chr(10).join(lines[fi:end + 1])
            h = hashlib.sha256(body.encode("utf-8")).hexdigest()[:16]
            rows.append("%s %s" % (h, name))
        if not rows:
            raise ValueError("no function definitions found")
        rows.sort()
        _write_atomic(args[1], chr(10).join(rows) + chr(10))
        return 0
    norm = normalize(text)
    if mode == "check":
        with open(args[1], "r", encoding="utf-8") as f:
            golden = f.read()
        if norm != golden:
            a = norm.splitlines()
            b = golden.splitlines()
            shown = 0
            for i in range(max(len(a), len(b))):
                x = a[i] if i < len(a) else "<EOF>"
                y = b[i] if i < len(b) else "<EOF>"
                if x != y:
                    print("DIFF line %d:\n  got:    %s\n  golden: %s"
                          % (i + 1, x, y))
                    shown += 1
                    if shown >= 10:
                        print("... (%d vs %d lines total)" % (len(a), len(b)))
                        break
            return 1
        return 0
    with open(args[1], "w", encoding="utf-8", newline="\n") as f:
        f.write(norm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
