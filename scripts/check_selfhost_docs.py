#!/usr/bin/env python3
"""Self-host documentation closure check (B-ID189).

Two drift classes this guards against:

  1. a source listed in scripts/selfhost_sources.txt that no longer exists
     (the canonical list rotting);
  2. a .zan file under src/selfhost/ that is NOT in the canonical list --
     the exact way the fixed point rotted unnoticed once (a source added to
     the tree but missing from the closure silently dropped out of gen1,
     gen2 and the fixed-point assertion).

Run it after touching src/selfhost/ or scripts/selfhost_sources.txt.
Exit 0 = clean, 1 = findings above.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_LIST = os.path.join(ROOT, "scripts", "selfhost_sources.txt")
SELFHOST_DIR = os.path.join(ROOT, "src", "selfhost")

findings = []

# ---- 1 + 2: the canonical list vs the tree --------------------------------
listed = set()
listed_stems = set()
with open(SRC_LIST, "r", encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        listed.add(line)
        stem = line.rsplit("/", 1)[-1]
        listed_stems.add(stem)
        path = os.path.join(ROOT, "src", line + ".zan")
        if not os.path.isfile(path):
            findings.append("listed but missing: src/%s.zan" % line)

on_disk = set()
for root_dir, _, files in os.walk(SELFHOST_DIR):
    for name in files:
        if name.endswith(".zan"):
            rel_path = os.path.relpath(os.path.join(root_dir, name), os.path.join(ROOT, "src")).replace("\\", "/")
            on_disk.add(rel_path[:-4])

for path in sorted(on_disk - listed):
    findings.append(
        "on disk but not in scripts/selfhost_sources.txt: src/%s.zan "
        "(it silently drops out of gen1/gen2 and the fixed point)" % path)

# ---- 3: removed with the docs cleanup (bootstrap.md no longer exists) -----

if findings:
    for x in findings:
        print("SELfHOST DOCS: %s" % x)
    print("check_selfhost_docs: %d finding(s)" % len(findings))
    sys.exit(1)
print("check_selfhost_docs: clean (%d listed sources, %d on disk)"
      % (len(listed), len(on_disk)))
