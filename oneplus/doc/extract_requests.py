# -*- coding: utf-8 -*-
"""v2: 提取 Request URL/方法/Referer，解析 string.Format 与 Define 域名常量。"""
import os, re, json, io

BIZAN = r"D:/群爆款/BizanSoft/BizanSoft"
MAIN = r"D:/群爆款/群爆款优化神器2/群爆款优化神器"

def walk_cs(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in ("obj", "bin", ".vs")]
        for fn in filenames:
            if fn.endswith(".cs"):
                yield os.path.join(dirpath, fn).replace("\\", "/")

def strip_comments(t):
    """剥离 // 与 /* */ 注释，但保留字符串/字符字面量内容（状态机）"""
    out = []; i = 0; n = len(t)
    while i < n:
        c = t[i]
        if c == '"':
            verbatim = i > 0 and t[i - 1] == '@'
            out.append(c); i += 1
            while i < n:
                if verbatim:
                    if t[i] == '"':
                        if i + 1 < n and t[i + 1] == '"':
                            out.append('""'); i += 2; continue
                        out.append('"'); i += 1; break
                    out.append(t[i]); i += 1
                else:
                    if t[i] == '\\':
                        out.append(t[i:i + 2]); i += 2; continue
                    out.append(t[i])
                    if t[i] == '"':
                        i += 1; break
                    i += 1
        elif c == "'":
            out.append(c); i += 1
            while i < n:
                if t[i] == '\\':
                    out.append(t[i:i + 2]); i += 2; continue
                out.append(t[i])
                if t[i] == "'":
                    i += 1; break
                i += 1
        elif c == '/' and i + 1 < n and t[i + 1] == '/':
            while i < n and t[i] != '\n':
                i += 1
        elif c == '/' and i + 1 < n and t[i + 1] == '*':
            i += 2
            while i + 1 < n and not (t[i] == '*' and t[i + 1] == '/'):
                i += 1
            i += 2
            out.append(' ')
        else:
            out.append(c); i += 1
    return ''.join(out)

# ---- Define 域名常量 ----
defines = {}
for df in [BIZAN + "/Define.cs", MAIN + "/AppDefine.cs"]:
    if os.path.exists(df):
        t = io.open(df, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r'string\s+(\w+)\s*=\s*"([^"]+)"', t):
            defines[m.group(1)] = m.group(2)

def resolve_define(name):
    v = defines.get(name, "")
    if not v:
        return "${%s}" % name
    return v  # 保留原样，scheme 由拼接上下文决定

def norm_url(u):
    u = re.sub(r"^(https?://)https?://", r"\1", u)
    if u and not u.startswith("http") and not u.startswith("$"):
        u = "https://" + u
    return u

re_class = re.compile(r"class\s+(\w*Request)\b")
re_str = re.compile(r'"((?:[^"\\]|\\.)*)"')

def extract_method_body(text, mname):
    """返回方法 GetXxx() 的 return 表达式字符串（取第一个 return 到分号，支持跨行）"""
    m = re.search(r"\b" + mname + r"\s*\(\s*\)\s*\{", text)
    if not m:
        return ""
    body = text[m.end(): m.end() + 2000]
    rm = re.search(r"return\s+(.+?);", body, re.S)
    if not rm:
        return ""
    return rm.group(1).strip()

def resolve_expr(expr):
    """把 return 表达式解析为 URL 字符串"""
    if not expr:
        return ""
    # string.Format("fmt", args...)
    fm = re.search(r'string\.Format\s*\(\s*"((?:[^"\\]|\\.)*)"', expr)
    if fm:
        fmt = fm.group(1)
        # 找到 Format 的参数
        args = re.findall(r'(?:Define|P4pGlobalParam|AppDefine)\.(\w+)', expr)
        def repl(mm):
            idx = int(mm.group(1))
            if idx < len(args):
                return resolve_define(args[idx])
            return "{%s}" % mm.group(1)
        return re.sub(r"\{(\d+)\}", repl, fmt)
    # Define.X + "..." 拼接 / 单个字符串 / 插值
    out = []
    # 先抓插值 $""
    for seg in re.split(r'\+', expr):
        seg = seg.strip()
        sm = re_str.search(seg)
        dm = re.search(r'(?:Define|P4pGlobalParam|AppDefine)\.(\w+)', seg)
        if sm:
            s = sm.group(1)
            # 插值内替换 {Define.X}
            s = re.sub(r'\{(?:Define|P4pGlobalParam|AppDefine)\.(\w+)\}', lambda x: resolve_define(x.group(1)), s)
            out.append(s)
        elif dm:
            out.append(resolve_define(dm.group(1)))
    res = "".join(out)
    if res and not res.startswith("http"):
        # 可能是相对路径，尝试保持原样
        pass
    return res

requests_meta = {}
for f in list(walk_cs(BIZAN)) + list(walk_cs(MAIN)):
    try:
        text = strip_comments(io.open(f, encoding="utf-8", errors="replace").read())
    except Exception:
        continue
    for m in re_class.finditer(text):
        name = m.group(1)
        seg = text[m.start(): m.start() + 20000]
        url = resolve_expr(extract_method_body(seg, "GetUrl"))
        method = resolve_expr(extract_method_body(seg, "GetMethod")) or "POST"
        referer = resolve_expr(extract_method_body(seg, "GetReferer"))
        cur = requests_meta.get(name)
        if cur is None or (url and not cur.get("url")):
            requests_meta[name] = {
                "file": f, "url": norm_url(url), "method": method, "referer": norm_url(referer),
                "source": "BizanSoft" if f.startswith(BIZAN) else "Main"
            }

# ---- 文件级使用（剥离注释，避免注释中的死代码误判；覆盖主工程+BizanSoft） ----
re_api = re.compile(r"\b(OneApi|DmpApi|ShopApi|SycmApi|SoftApi|JdkcApi)\.(\w+)")
re_newreq = re.compile(r"\bnew\s+(\w*Request)\s*\(")
usage = {}
for root in (MAIN, BIZAN):
    for f in walk_cs(root):
        try:
            text = strip_comments(io.open(f, encoding="utf-8", errors="replace").read())
        except Exception:
            continue
        apis = ["%s.%s" % (a, b) for a, b in re_api.findall(text)]
        reqs = re_newreq.findall(text)
        if apis or reqs:
            rel = f.replace(MAIN + "/", "").replace(BIZAN + "/", "BizanSoft/")
            usage[rel] = {"apis": sorted(set(apis)), "requests": sorted(set(reqs))}

with io.open(r"D:/project/38max/oneplus/api_data.json", "w", encoding="utf-8") as f:
    json.dump({"requests": requests_meta, "usage": usage, "defines": defines}, f, ensure_ascii=False, indent=1)

print("Requests total:", len(requests_meta))
no_url = [k for k, v in requests_meta.items() if not v["url"]]
print("No-URL:", len(no_url), sorted(no_url))
for k in ["CrowdBatchModifyRequest", "CampaignGetRequest", "LabelDmpConvertRequest", "SolutionAddRequest", "CampaignReportGetRequest", "WordpackageUpdatePriceRequest"]:
    print(k, "=>", requests_meta.get(k, {}).get("url"))
