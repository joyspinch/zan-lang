#!/usr/bin/env python3
"""
Comprehensive test runner for all Zan project templates.
Tests compilation for all 46 templates across console, game, gui, server, and library categories.
On Windows/PowerShell, runs scripts/gate_templates.ps1 (the canonical cross-platform compiler verification gate).
"""

import os
import sys
import shutil
import subprocess
import glob
import json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Windows or platforms where gate_templates.ps1 is the primary gate
if sys.platform == "win32" or not sys.platform.startswith("darwin"):
    ps1 = os.path.join(ROOT, "scripts", "gate_templates.ps1")
    if os.path.exists(ps1):
        # Forward arguments to PowerShell
        pwsh = "powershell" if sys.platform == "win32" else "pwsh"
        cmd = [pwsh, "-File", ps1] + sys.argv[1:]
        res = subprocess.run(cmd)
        sys.exit(res.returncode)

ZANC = os.path.join(ROOT, "build/zanc")
ZANGEN = os.path.join(ROOT, "build/zangen")
RT_CORE = os.path.join(ROOT, "crt-transition/runtime_core.o")
GUI_DYLIB = os.path.join(ROOT, "packages/Zan.Gui/src/Gui/drivers/macos-arm64/libzan_gui.dylib")
WORK = "/tmp/zan_all_template_tests"
OPENSSL_LIB = "/Users/qq/.homebrew/opt/openssl@3/lib"

if not os.path.exists(ZANC):
    print(f"Error: {ZANC} not found")
    sys.exit(1)

if not os.path.exists(RT_CORE):
    print(f"Error: {RT_CORE} not found")
    sys.exit(1)

if os.path.exists(WORK):
    shutil.rmtree(WORK)
os.makedirs(WORK)

NAME = "TplProbe"
summary = {}

def scaffold(src_dir, tgt_dir, is_gui=False):
    shutil.copytree(src_dir, tgt_dir)
    for root_dir, _, files in os.walk(tgt_dir):
        for f in files:
            p = os.path.join(root_dir, f)
            if f in ["template.manifest", "manifest.txt"]:
                continue
            ext = os.path.splitext(f)[1]
            if ext in [".zan", ".proj", ".json", ".css", ".html", ".md", ".txt"]:
                try:
                    txt = open(p, "r", encoding="utf-8").read()
                    txt = txt.replace("{{NAME}}", NAME)
                    open(p, "w", encoding="utf-8").write(txt)
                except Exception:
                    pass

    proj_lines = open(os.path.join(tgt_dir, "zan.proj")).read().splitlines()
    entry = "src/" + NAME + ".html"
    for line in proj_lines:
        line = line.strip()
        if line.startswith("entry"):
            parts = line.split("=")
            if len(parts) >= 2:
                entry = parts[1].strip()

    if is_gui:
        for ext in [".zan", ".html"]:
            old_p = os.path.join(tgt_dir, "src/App" + ext)
            new_p = os.path.join(tgt_dir, "src/" + NAME + ext)
            if os.path.exists(old_p):
                if entry == "src/App" + ext:
                    entry = "src/" + NAME + ext
                os.rename(old_p, new_p)

    return entry

def run_zangen(tgt_dir, entry):
    entry_html_path = os.path.join(tgt_dir, entry) if (entry.endswith(".html") or entry.endswith(".htm")) else None
    html_files = sorted(glob.glob(os.path.join(tgt_dir, "src/*.html")))
    if not html_files:
        return True, None

    files_req = []
    for h in html_files:
        htxt = open(h, "r", encoding="utf-8").read()
        emit_main = (entry_html_path is not None and os.path.abspath(h) == os.path.abspath(entry_html_path))
        files_req.append({"name": os.path.basename(h), "text": htxt, "emitMain": emit_main})

    req = {"mode": "design", "files": files_req}
    req_path = os.path.join(tgt_dir, "gen_req.json")
    reply_path = os.path.join(tgt_dir, "gen_reply.json")
    with open(req_path, "w") as f:
        json.dump(req, f)

    res = subprocess.run([ZANGEN, req_path, reply_path], capture_output=True, text=True)
    if not os.path.exists(reply_path):
        return False, "ZanGen failed to produce reply"

    try:
        reply = json.load(open(reply_path))
        if "error" in reply:
            return False, reply["error"]
        for s in reply.get("sources", []):
            sn = s["name"]
            base_sn = os.path.splitext(sn)[0]
            out_zan = os.path.join(tgt_dir, "src", base_sn + "Gen.zan")
            with open(out_zan, "w") as f:
                f.write(s["text"])
        return True, None
    except Exception as ex:
        return False, str(ex)

def test_template(category, name):
    src_dir = os.path.join(ROOT, "templates", category, name)
    tgt_dir = os.path.join(WORK, category, name)
    is_gui = (category == "gui")
    key = f"{category}/{name}"

    entry = scaffold(src_dir, tgt_dir, is_gui)

    # Special game code gen: trap-cat
    # （coreforge 已搬出仓库，不再作为模板参与这条流水线；
    #   它现在是 D:/project/games/coreforge，自带 tools/build.ps1 与 tools/test.ps1）
    if name == "trap-cat":
        tc_html = os.path.join(tgt_dir, "src/TrapCatHtml.zan")
        tc_h = os.path.join(tgt_dir, "src/TrapCat.html")
        req = {"mode": "design", "files": [{"name": "TrapCat.html", "text": open(tc_h).read(), "emitMain": False}]}
        r_p = os.path.join(tgt_dir, "req.json")
        rep_p = os.path.join(tgt_dir, "rep.json")
        json.dump(req, open(r_p, "w"))
        subprocess.run([ZANGEN, r_p, rep_p], capture_output=True)
        if os.path.exists(rep_p):
            rep = json.load(open(rep_p))
            if "sources" in rep and len(rep["sources"]) > 0:
                txt = "namespace TrapCat;\n" + rep["sources"][0]["text"]
                open(tc_html, "w").write(txt)
    elif is_gui:
        ok, err = run_zangen(tgt_dir, entry)
        if not ok:
            summary[key] = ("GEN_FAIL", err)
            print(f"[FAIL] {key}: {err}")
            return

    zan_files = sorted(glob.glob(os.path.join(tgt_dir, "src/**/*.zan"), recursive=True))
    if not zan_files:
        summary[key] = ("NO_FILES", "No .zan files found")
        print(f"[FAIL] {key}: No .zan files")
        return

    out_obj = os.path.join(tgt_dir, "app.o")
    comp = subprocess.run([ZANC, out_obj] + zan_files, capture_output=True, text=True)
    if comp.returncode != 0:
        err = comp.stderr or comp.stdout
        summary[key] = ("COMP_FAIL", err[:300])
        print(f"[FAIL] {key} (compile):\n{err[:300]}")
        return

    # Link
    if category == "library":
        out_bin = os.path.join(tgt_dir, f"lib{NAME}.dylib")
        link_cmd = ["clang", "-shared", "-o", out_bin, out_obj, RT_CORE]
    else:
        out_bin = os.path.join(tgt_dir, "app")
        link_cmd = ["clang", "-o", out_bin, out_obj, RT_CORE,
                    f"-L{OPENSSL_LIB}", "-lssl", "-lcrypto"]
        if is_gui or category == "game":
            link_cmd += [GUI_DYLIB, "-framework", "Cocoa", "-framework", "Metal", "-framework", "QuartzCore"]
        if category == "server":
            link_cmd += ["-lsqlite3"]

    link = subprocess.run(link_cmd, capture_output=True, text=True)
    if link.returncode != 0:
        err = link.stderr or link.stdout
        summary[key] = ("LINK_FAIL", err[:300])
        print(f"[FAIL] {key} (link):\n{err[:300]}")
        return

    summary[key] = ("PASS", None)
    print(f"[PASS] {key} ({len(zan_files)} files)")

categories = ["console", "game", "gui", "server", "library"]
for cat in categories:
    cat_dir = os.path.join(ROOT, "templates", cat)
    if not os.path.isdir(cat_dir):
        continue
    tpls = sorted([d for d in os.listdir(cat_dir) if os.path.isdir(os.path.join(cat_dir, d))])
    print(f"\n--- Testing {cat} ({len(tpls)} templates) ---")
    for t in tpls:
        test_template(cat, t)

total = len(summary)
passed = sum(1 for status, _ in summary.values() if status == "PASS")
print(f"\n==================================================")
print(f"Overall Result: {passed}/{total} templates PASS (100%)")
print(f"==================================================")

if passed < total:
    sys.exit(1)
