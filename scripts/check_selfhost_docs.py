#!/usr/bin/env python3
"""Self-host documentation closure check (B-ID189).

Three drift classes this guards against:

  1. a source listed in scripts/selfhost_sources.txt that no longer exists
     (the canonical list rotting);
  2. a .zan file under src/selfhost/ that is NOT in the canonical list --
     the exact way the fixed point rotted unnoticed once (a source added to
     the tree but missing from the closure silently dropped out of gen1,
     gen2 and the fixed-point assertion);
  3. docs/agent-kb/bootstrap.md's architecture table naming files that do
     not exist (the table once listed jsongen/dbgen/attrgen.zan, none of
     which are in the tree).

Run it after touching src/selfhost/, scripts/selfhost_sources.txt or
docs/agent-kb/bootstrap.md. Exit 0 = clean, 1 = findings above.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_LIST = os.path.join(ROOT, "scripts", "selfhost_sources.txt")
SELFHOST_DIR = os.path.join(ROOT, "src", "selfhost")
BOOTSTRAP_MD = os.path.join(ROOT, "docs", "agent-kb", "bootstrap.md")

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
for name in os.listdir(SELFHOST_DIR):
    if name.endswith(".zan"):
        on_disk.add(name[:-4])

for name in sorted(on_disk - listed_stems):
    findings.append(
        "on disk but not in scripts/selfhost_sources.txt: src/selfhost/%s.zan "
        "(it silently drops out of gen1/gen2 and the fixed point)" % name)

# ---- 3: bootstrap.md architecture table ----------------------------------
with open(BOOTSTRAP_MD, "r", encoding="utf-8") as f:
    md = f.read()
for m in re.finditer(r'^\|\s*`(\w+\.zan)`\s*\|', md, re.M):
    name = m.group(1)
    # selfhost table only names src/selfhost files; the doc's other tables
    # reference runtime/host files which are matched in the C tree too
    if os.path.isfile(os.path.join(SELFHOST_DIR, name)):
        continue
    if os.path.isfile(os.path.join(ROOT, "src", name)):
        continue
    if os.path.isfile(os.path.join(ROOT, "src", "compiler", name)):
        continue
    findings.append("bootstrap.md table names a nonexistent file: %s" % name)

if findings:
    for x in findings:
        print("SELfHOST DOCS: %s" % x)
    print("check_selfhost_docs: %d finding(s)" % len(findings))
    sys.exit(1)
print("check_selfhost_docs: clean (%d listed sources, %d on disk)"
      % (len(listed), len(on_disk)))
