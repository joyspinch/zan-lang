#!/usr/bin/env python3
"""Seed-mutation differential fuzzer for the self-hosted compiler (B-ID186).

Every twin bug the bootstrap alignment effort fixed (B-ID169/180/181/182/183)
was found by a hand-written probe. This generator mass-produces such probes:
it takes the differential-gate cases as seeds and applies boundary-oriented
mutations, so each mutant can be compiled by both the C host and gen1 and the
two runs compared (stdout + exit code). A mismatch is a prey: reduce it to a
minimal case, land it as a conformance golden pair, fix per WC §10.

Mutations (each targets a family the twins already bit on):
  - INTLIT    integer literals -> boundary values (0, MIN/MAX int/long, -1, 255/256)
  - FPLIT     floating literals -> 0.0/-0.0/huge/tiny/NaN/+Inf/-Inf/precision edges
  - NULLIN    balanced `new Type(...)` / `new T[...]` expressions -> `null`
  - IDXFOLD   array/string index -> off-by-one in both directions
  - CASTW     integer/float width flips (int/long/short/ushort/byte/sbyte/uint/ulong/float/double)
  - TRY_WRAP  wrap a random statement span in try/catch
  - TRY_STRIP strip a balanced `try { ... } catch (...) { ... }` wrapper to expose uncaught paths

Deterministic: given the same seed file, count and --seed, the same mutant
stream is produced, so a prey reproduces from its log line.

Usage:
  python scripts/fuzz_mutate.py <seed.zan> <count> [--seed N] [--out DIR] [--mutation NAME]
  python scripts/fuzz_mutate.py --list-mutations
Mutants are written to DIR as <seedbase>_m<i>.zan.
"""
import os
import random
import re
import sys

INT_BOUNDARIES = [
    "0",
    "1",
    "-1",
    "2147483647",
    "-2147483648",
    "9223372036854775807",
    "-9223372036854775807",
    "255",
    "256",
]

FP_BOUNDARIES = [
    "0.0",
    "-0.0",
    "1e308",
    "-1e308",
    "4.9e-324",
    "0.1",
    "16777217.0",
    "2147483648.0",
    "double.NaN",
    "double.PositiveInfinity",
    "double.NegativeInfinity",
    "(0.0 / 0.0)",
    "(1.0 / 0.0)",
    "(-1.0 / 0.0)",
]

INT_WIDTH_FLIPS = {
    "int": ["long", "short", "uint", "byte", "sbyte"],
    "long": ["int", "ulong", "short"],
    "short": ["int", "ushort", "sbyte", "long"],
    "ushort": ["int", "short", "uint"],
    "byte": ["sbyte", "short", "int"],
    "sbyte": ["byte", "short", "int"],
    "uint": ["int", "ulong", "ushort"],
    "ulong": ["long", "uint"],
    "double": ["float"],
    "float": ["double"],
}

_NEW_PREFIX_RE = re.compile(
    r"\bnew\s+[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*(?:\s*<[^<>]+>)?\s*([(\[])"
)


def _find_balanced_close(text, open_idx, open_ch, close_ch):
    """Return index right after matching close_ch, or -1 if unbalanced/multiline."""
    depth = 0
    in_str = False
    in_char = False
    esc = False
    for idx in range(open_idx, len(text)):
        ch = text[idx]
        if esc:
            esc = False
            continue
        if in_str:
            if ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if in_char:
            if ch == "\\":
                esc = True
            elif ch == "'":
                in_char = False
            continue
        if ch == '"':
            in_str = True
        elif ch == "'":
            in_char = True
        elif ch == open_ch:
            depth += 1
        elif ch == close_ch:
            depth -= 1
            if depth == 0:
                return idx + 1
    return -1


def _stable_positions(text):
    """Line spans that are safe to mutate: skip using/namespace/class headers
    and string literals are mutated only as whole-string swaps (rare)."""
    lines = text.splitlines()
    safe = []
    for i, l in enumerate(lines):
        s = l.strip()
        if s.startswith("using ") or s.startswith("namespace "):
            continue
        safe.append(i)
    return lines, safe


def _mut_intlit(lines, rng):
    cand = [
        (i, m)
        for i in range(len(lines))
        for m in re.finditer(r'(?<![\w."])\d{2,}(?![\w."])', lines[i])
        if not lines[i].strip().startswith("using")
    ]
    if not cand:
        return None
    i, m = rng.choice(cand)
    val = rng.choice(INT_BOUNDARIES)
    if val.startswith("-") and m.start() > 0 and lines[i][m.start() - 1] == "-":
        # a negative boundary right after a unary minus would form "--"
        # (decrement): keep the boundary, drop its sign
        val = val[1:]
    lines[i] = lines[i][: m.start()] + val + lines[i][m.end() :]
    return lines


def _mut_fplit(lines, rng):
    cand = [
        (i, m)
        for i in range(len(lines))
        for m in re.finditer(r'(?<![\w."])\d+\.\d+(?![\w."])', lines[i])
        if not lines[i].strip().startswith("using")
    ]
    if not cand:
        return None
    i, m = rng.choice(cand)
    lines[i] = lines[i][: m.start()] + rng.choice(FP_BOUNDARIES) + lines[i][m.end() :]
    return lines


def _mut_nullin(lines, rng):
    cand = []
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("using ") or stripped.startswith("//"):
            continue
        for m in _NEW_PREFIX_RE.finditer(line):
            open_ch = m.group(1)
            close_ch = ")" if open_ch == "(" else "]"
            open_idx = m.end() - 1
            end_idx = _find_balanced_close(line, open_idx, open_ch, close_ch)
            if end_idx == -1:
                continue
            # Also absorb optional single-line collection/object initializer `{ ... }`
            rest = line[end_idx:]
            ws_len = len(rest) - len(rest.lstrip(" \t"))
            if end_idx + ws_len < len(line) and line[end_idx + ws_len] == "{":
                brace_end = _find_balanced_close(line, end_idx + ws_len, "{", "}")
                if brace_end == -1:
                    continue
                end_idx = brace_end
            # Skip `throw new ...` (throwing null is uninteresting vs deref paths)
            prefix_before = line[: m.start()].rstrip()
            if prefix_before.endswith("throw"):
                continue
            cand.append((i, m.start(), end_idx))
    if not cand:
        return None
    i, start, end = rng.choice(cand)
    lines[i] = lines[i][:start] + "null" + lines[i][end:]
    return lines


def _mut_idxfold(lines, rng):
    cand = [
        (i, m)
        for i in range(len(lines))
        for m in re.finditer(r"\[\s*(\w+)\s*\]", lines[i])
        if m.group(1) not in ("StackTrace",)
        and not lines[i].strip().startswith("using")
    ]
    if not cand:
        return None
    i, m = rng.choice(cand)
    idx = m.group(1)
    delta = rng.choice(["-1", "+1"])
    expr = "%s%s" % (idx, delta)
    lines[i] = lines[i][: m.start()] + "[" + expr + "]" + lines[i][m.end() :]
    return lines


_WIDTH_TOKEN_RE = re.compile(
    r"\b(int|long|short|ushort|byte|sbyte|uint|ulong|double|float)\b(?![\]\"'\w])"
)


def _mut_castw(lines, rng):
    cand = []
    for i, line in enumerate(lines):
        s = line.strip()
        if (
            s.startswith("using ")
            or s.startswith("namespace ")
            or s.startswith("//")
            or "Main(" in line
        ):
            continue
        for m in _WIDTH_TOKEN_RE.finditer(line):
            # Avoid corrupting array declarations `int[]` or `new int[`
            after = line[m.end() :].lstrip()
            if after.startswith("["):
                continue
            cand.append((i, m))
    if not cand:
        return None
    i, m = rng.choice(cand)
    src_ty = m.group(1)
    flip = rng.choice(INT_WIDTH_FLIPS[src_ty])
    lines[i] = lines[i][: m.start()] + flip + lines[i][m.end() :]
    return lines


def _mut_tryei(lines, rng):
    body = [
        i
        for i, l in enumerate(lines)
        if l.strip()
        and not l.strip().startswith("using")
        and not l.strip().startswith("namespace")
        and not l.strip().startswith("class")
        and not l.strip().startswith("static void Main")
        and l.strip() != "{"
        and l.strip() != "}"
        and l.strip() != ";"
    ]
    if len(body) < 2:
        return None
    start = rng.choice(body[:-1])
    span = min(len(body) - body.index(start), rng.randint(1, 4))
    end = body[body.index(start) + span - 1]
    indent = re.match(r"\s*", lines[start]).group(0)
    inner = ["    " + l for l in lines[start : end + 1]]
    lines[start : end + 1] = (
        [indent + "try {"]
        + inner
        + [indent + '} catch (Exception e) { Console.WriteLine("caught:" + e.GetType().Name); }']
    )
    return lines


def _find_block_end_line(lines, open_line):
    """Find the line index whose closing `}` balances the first `{` on open_line."""
    start_col = lines[open_line].find("{")
    if start_col == -1:
        return -1
    depth = 0
    for idx in range(open_line, len(lines)):
        segment = lines[idx][start_col:] if idx == open_line else lines[idx]
        for ch in segment:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return idx
    return -1


def _mut_try_strip(lines, rng):
    """Strip a balanced `try { body } catch (...) { ... }` block, leaving `body`."""
    cand = []
    for i, line in enumerate(lines):
        if not re.match(r"^\s*try\s*\{\s*$", line):
            continue
        try_end = _find_block_end_line(lines, i)
        if try_end <= i:
            continue
        # Locate one or more attached catch blocks; abort if finally is present
        cur = try_end
        catch_end = -1
        has_finally = False
        while cur < len(lines):
            tail = lines[cur].strip()
            if re.match(r"^\}\s*catch\b.*\{\s*$", tail):
                end_c = _find_block_end_line(lines, cur)
                if end_c <= cur:
                    catch_end = -1
                    break
                catch_end = end_c
                cur = end_c
                continue
            if re.match(r"^\}\s*finally\b", tail):
                has_finally = True
                break
            if cur + 1 < len(lines) and re.match(r"^\s*catch\b.*\{\s*$", lines[cur + 1]):
                end_c = _find_block_end_line(lines, cur + 1)
                if end_c <= cur + 1:
                    catch_end = -1
                    break
                catch_end = end_c
                cur = end_c
                continue
            if cur + 1 < len(lines) and re.match(r"^\s*finally\b", lines[cur + 1]):
                has_finally = True
            break
        if catch_end > try_end and not has_finally:
            cand.append((i, try_end, catch_end))

    if not cand:
        return None
    try_start, try_end, catch_end = rng.choice(cand)
    inner = lines[try_start + 1 : try_end]
    if not inner:
        return None
    lines[try_start : catch_end + 1] = inner
    return lines


MUTATION_MAP = {
    "INTLIT": _mut_intlit,
    "FPLIT": _mut_fplit,
    "NULLIN": _mut_nullin,
    "IDXFOLD": _mut_idxfold,
    "CASTW": _mut_castw,
    "TRY_WRAP": _mut_tryei,
    "TRY_STRIP": _mut_try_strip,
}

MUTATIONS = list(MUTATION_MAP.values())


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    if "--list-mutations" in args:
        for name in MUTATION_MAP:
            print(name)
        return 0

    seed_n = 1
    outdir = "_scratch/fuzz_mutants"
    only_mut = None
    if "--seed" in args:
        k = args.index("--seed")
        seed_n = int(args[k + 1])
        args = args[:k] + args[k + 2 :]
    if "--out" in args:
        k = args.index("--out")
        outdir = args[k + 1]
        args = args[:k] + args[k + 2 :]
    if "--mutation" in args:
        k = args.index("--mutation")
        only_mut = args[k + 1].upper()
        if only_mut not in MUTATION_MAP:
            print("unknown mutation '%s'; choose from %s" % (only_mut, ", ".join(MUTATION_MAP)), file=sys.stderr)
            return 2
        args = args[:k] + args[k + 2 :]
    if len(args) != 2:
        print(__doc__)
        return 2

    seed_path, count = args[0], int(args[1])
    rng = random.Random(seed_n)
    with open(seed_path, "r", encoding="utf-8", errors="replace") as f:
        base = f.read()

    pool = [MUTATION_MAP[only_mut]] if only_mut else MUTATIONS
    os.makedirs(outdir, exist_ok=True)
    base_name = os.path.splitext(os.path.basename(seed_path))[0]
    made = 0
    attempts = 0
    while made < count and attempts < count * 20:
        attempts += 1
        lines, _safe = _stable_positions(base)
        mut = rng.choice(pool)
        new_lines = mut(list(lines), rng)
        if new_lines is None or new_lines == lines:
            continue
        made += 1
        out = os.path.join(outdir, "%s_m%d.zan" % (base_name, made))
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(new_lines) + "\n")
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
