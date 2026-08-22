# -*- coding: utf-8 -*-
"""补充非 *Request 命名的请求类（Shop 侧 *Async 等），并入 api_data.json"""
import os, re, io, json

BIZAN = r"D:/群爆款/BizanSoft/BizanSoft"
MAIN = r"D:/群爆款/群爆款优化神器2/群爆款优化神器"

api = json.load(io.open("api_data.json", encoding="utf-8"))
meta = api["requests"]
facade = json.load(io.open("linkage.json", encoding="utf-8"))["facadeMap"]

# 门面方法的所有参数类型
param_types = set()
for reqs in facade.values():
    param_types.update(reqs)

missing = [t for t in param_types if t not in meta]
print("缺失类型:", missing)

re_str = re.compile(r'"((?:[^"\\]|\\.)*)"')
defines = api["defines"]

def resolve_define(name):
    return defines.get(name, "${%s}" % name)

def norm_url(u):
    u = re.sub(r"^(https?://)https?://", r"\1", u)
    if u and not u.startswith("http") and not u.startswith("$"):
        u = "https://" + u
    return u

def extract_method_body(text, mname):
    m = re.search(r"\b" + mname + r"\s*\(\s*\)\s*\{", text)
    if not m:
        return ""
    body = text[m.end(): m.end() + 2000]
    rm = re.search(r"return\s+(.+?);", body, re.S)
    return rm.group(1).strip() if rm else ""

def resolve_expr(expr):
    if not expr:
        return ""
    fm = re.search(r'string\.Format\s*\(\s*"((?:[^"\\]|\\.)*)"', expr)
    if fm:
        fmt = fm.group(1)
        args = re.findall(r'(?:Define|P4pGlobalParam|AppDefine)\.(\w+)', expr)
        return re.sub(r"\{(\d+)\}", lambda mm: resolve_define(args[int(mm.group(1))]) if int(mm.group(1)) < len(args) else "{%s}" % mm.group(1), fmt)
    out = []
    for seg in re.split(r'\+', expr):
        seg = seg.strip()
        sm = re_str.search(seg)
        dm = re.search(r'(?:Define|P4pGlobalParam|AppDefine)\.(\w+)', seg)
        if sm:
            out.append(re.sub(r'\{(?:Define|P4pGlobalParam|AppDefine)\.(\w+)\}', lambda x: resolve_define(x.group(1)), sm.group(1)))
        elif dm:
            out.append(resolve_define(dm.group(1)))
    return "".join(out)

found = 0
for dirpath, dirnames, filenames in os.walk(BIZAN):
    dirnames[:] = [d for d in dirnames if d not in ("obj", "bin")]
    for fn in filenames:
        if not fn.endswith(".cs"):
            continue
        p = os.path.join(dirpath, fn).replace("\\", "/")
        try:
            text = io.open(p, encoding="utf-8", errors="replace").read()
        except Exception:
            continue
        for t in missing:
            if ("class " + t) in text and t not in meta:
                seg = text[text.index("class " + t):][:20000]
                url = norm_url(resolve_expr(extract_method_body(seg, "GetUrl")))
                method = resolve_expr(extract_method_body(seg, "GetMethod")) or "POST"
                referer = norm_url(resolve_expr(extract_method_body(seg, "GetReferer")))
                meta[t] = {"file": p, "url": url, "method": method, "referer": referer, "source": "BizanSoft"}
                found += 1
                print(" +", t, "=>", url)

json.dump(api, io.open("api_data.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print("补充:", found, " 总 Request 元数据:", len(meta))
