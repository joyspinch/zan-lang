#!/usr/bin/env python3
"""Seed-mutation differential fuzzer for the self-hosted compiler (B-ID186).

Every twin bug the bootstrap alignment effort fixed (B-ID169/180/181/182/183)
was found by a hand-written probe. This generator mass-produces such probes:
it takes the differential-gate cases as seeds and applies boundary-oriented
mutations, so each mutant can be compiled by both the C host and gen1 and the
two runs compared (stdout + exit code). A mismatch is a prey: reduce it to a
minimal case, land it as a conformance golden pair, fix per WC §10.

Mutations (each targets a family the twins already bit on):
  - INTLIT   integer literals -> boundary values (0, MIN/MAX int/long, -1)
  - FPIT     floating literals -> 0.0/-0.0/huge/tiny/precision edges
  - NULLIN   object/array literals -> null (deref-guard paths)
  - IDXFOLD  array/string index -> off-by-one in both directions
  - CASTW    int -> long / long -> int (width-flip at boundaries)
  - TRYEI    a random top-level statement block wrapped in try/catch

Deterministic: given the same seed file, count and --seed, the same mutant
stream is produced, so a prey reproduces from its log line.

Usage:
  python scripts/fuzz_mutate.py <seed.zan> <count> [--seed N] [--out DIR]
Mutants are written to DIR as <seedbase>_m<i>.zan.
"""
import os
import random
import re
import sys

INT_BOUNDARIES = ["0", "1", "-1", "2147483647", "-2147483648",
                  "9223372036854775807", "-9223372036854775807", "255", "256"]
FP_BOUNDARIES = ["0.0", "-0.0", "1e308", "-1e308", "4.9e-324", "0.1",
                 "16777217.0", "2147483648.0"]


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
    cand = [(i, m) for i in range(len(lines))
            for m in re.finditer(r'(?<![\w."])\d{2,}(?![\w."])', lines[i])
            if not lines[i].strip().startswith("using")]
    if not cand:
        return None
    i, m = rng.choice(cand)
    val = rng.choice(INT_BOUNDARIES)
    if val.startswith("-") and m.start() > 0 and lines[i][m.start() - 1] == "-":
        # a negative boundary right after a unary minus would form "--"
        # (decrement): keep the boundary, drop its sign
        val = val[1:]
    lines[i] = lines[i][:m.start()] + val + lines[i][m.end():]
    return lines


def _mut_fplit(lines, rng):
    cand = [(i, m) for i in range(len(lines))
            for m in re.finditer(r'(?<![\w."])\d+\.\d+(?![\w."])', lines[i])
            if not lines[i].strip().startswith("using")]
    if not cand:
        return None
    i, m = rng.choice(cand)
    lines[i] = lines[i][:m.start()] + rng.choice(FP_BOUNDARIES) + lines[i][m.end():]
    return lines


def _mut_nullin(lines, rng):
    cand = [(i, m) for i in range(len(lines))
            for m in re.finditer(r'new\s+\w+\s*\(', lines[i])
            if not lines[i].strip().startswith("using")]
    if not cand:
        return None
    i, m = rng.choice(cand)
    lines[i] = lines[i][:m.start()] + "null" + lines[i][m.end():]
    return lines


def _mut_idxfold(lines, rng):
    cand = [(i, m) for i in range(len(lines))
            for m in re.finditer(r'\[\s*(\w+)\s*\]', lines[i])
            if m.group(1) not in ("StackTrace",)
            and not lines[i].strip().startswith("using")]
    if not cand:
        return None
    i, m = rng.choice(cand)
    idx = m.group(1)
    delta = rng.choice(["-1", "+1"])
    expr = "%s%s" % (idx, delta)
    lines[i] = lines[i][:m.start()] + "[" + expr + "]" + lines[i][m.end():]
    return lines


def _mut_castw(lines, rng):
    cand = [(i, m) for i in range(len(lines))
            for m in re.finditer(r'\bint\b(?![])["\w])', lines[i])
            if "int]" not in lines[i]
            and not lines[i].strip().startswith("using")]
    if not cand:
        return None
    i, m = rng.choice(cand)
    flip = rng.choice(["long", "int"])
    lines[i] = lines[i][:m.start()] + flip + lines[i][m.end():]
    return lines


def _mut_tryei(lines, rng):
    body = [i for i, l in enumerate(lines)
            if l.strip() and not l.strip().startswith("using")
            and not l.strip().startswith("namespace")
            and not l.strip().startswith("class")
            and not l.strip().startswith("static void Main")
            and l.strip() != "{" and l.strip() != "}" and l.strip() != ";"]
    if len(body) < 2:
        return None
    start = rng.choice(body[:-1])
    span = min(len(body) - body.index(start), rng.randint(1, 4))
    end = body[body.index(start) + span - 1]
    indent = re.match(r'\s*', lines[start]).group(0)
    inner = ["    " + l for l in lines[start:end + 1]]
    lines[start:end + 1] = (
        [indent + "try {"] + inner +
        [indent + "} catch (System.Exception e) { Console.WriteLine(\"caught:\" + e.GetType().Name); }"])
    return lines


MUTATIONS = [_mut_intlit, _mut_fplit, _mut_nullin, _mut_idxfold, _mut_castw,
             _mut_tryei]


def main():
    args = sys.argv[1:]
    seed_n = 1
    outdir = "_scratch/fuzz_mutants"
    if "--seed" in args:
        k = args.index("--seed")
        seed_n = int(args[k + 1])
        args = args[:k] + args[k + 2:]
    if "--out" in args:
        k = args.index("--out")
        outdir = args[k + 1]
        args = args[:k] + args[k + 2:]
    if len(args) != 2:
        print(__doc__)
        return 2
    seed_path, count = args[0], int(args[1])
    rng = random.Random(seed_n)
    with open(seed_path, "r", encoding="utf-8", errors="replace") as f:
        base = f.read()

    os.makedirs(outdir, exist_ok=True)
    base_name = os.path.splitext(os.path.basename(seed_path))[0]
    made = 0
    attempts = 0
    while made < count and attempts < count * 20:
        attempts += 1
        lines, _safe = _stable_positions(base)
        mut = rng.choice(MUTATIONS)
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
