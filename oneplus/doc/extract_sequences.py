# -*- coding: utf-8 -*-
"""提取主程序每个文件内 API 调用的行序，用于重建调用链。"""
import os, re, io, json

MAIN = r"D:/群爆款/群爆款优化神器2/群爆款优化神器"
re_call = re.compile(r"(?:await\s+)?(OneApi|DmpApi|ShopApi|SycmApi)\.(\w+)\s*\(|new\s+(\w*Request)\s*\(")
seqs = {}
for dirpath, dirnames, filenames in os.walk(MAIN):
    dirnames[:] = [d for d in dirnames if d not in ("obj", "bin", ".vs", "docs")]
    for fn in filenames:
        if not fn.endswith(".cs") or fn.endswith(".Designer.cs"):
            continue
        p = os.path.join(dirpath, fn).replace("\\", "/")
        rel = p.replace(MAIN + "/", "")
        try:
            text = io.open(p, encoding="utf-8", errors="replace").read()
        except Exception:
            continue
        calls = []
        for i, line in enumerate(text.split("\n"), 1):
            st = line.strip()
            if st.startswith("//") or st.startswith("*"):
                continue
            for m in re_call.finditer(line):
                if m.group(1):
                    calls.append([i, m.group(1) + "." + m.group(2)])
                else:
                    calls.append([i, "new " + m.group(3)])
        if len(calls) >= 3:
            seqs[rel] = calls

json.dump(seqs, io.open("sequences.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print("有>=3个接口调用的文件:", len(seqs))
for k in ["Downloader/One/CampDownloader.cs", "Downloader/One/AdGroupDownloader.cs",
          "One/Search/Camp/FormCampNew.cs", "One/Download/FormNotifyCamp.cs"]:
    if k in seqs:
        print("---", k)
        for ln, c in seqs[k][:18]:
            print("   L%-5d %s" % (ln, c))
