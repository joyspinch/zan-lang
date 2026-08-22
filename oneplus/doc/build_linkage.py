# -*- coding: utf-8 -*-
"""v2: MD 解析 + 门面方法映射 + 跨节复用匹配 + 同步任务关联 → 菜单→接口 脉络数据"""
import os, re, json, io

MAIN = r"D:/群爆款/群爆款优化神器2/群爆款优化神器"
MD = MAIN + "/docs/业务功能结构导图-XMind.md"

api = json.load(io.open(r"D:/project/38max/oneplus/api_data.json", encoding="utf-8"))
usage = api["usage"]; requests_meta = api["requests"]

# ---------- 0. 门面方法 → Request 映射 ----------
def strip_comments(t):
    t = re.sub(r'"(?:[^"\\]|\\.)*"', '""', t)          # 字符串字面量（防 URL 内 // 误判）
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    t = re.sub(r'//[^\n]*', '', t)
    return t

facade_map = {}   # "OneApi.Get_Campaign_List" -> ["CampaignGetRequest", ...]
# 支持嵌套泛型返回类型：Task<List<X>> / Task<ApiResponseEntity<JObject>>
re_sig = re.compile(r"Task<(?:[^<>]|<[^<>]*>)*>\s+(\w+)\s*\(\s*(\w+)\s+request\s*\)")
for facade_file, cls in [(MAIN + "/Api/OneApi.cs", "OneApi"), (MAIN + "/Api/DmpApi.cs", "DmpApi"),
                         (MAIN + "/Api/ShopApi.cs", "ShopApi"), (MAIN + "/Api/SycmApi.cs", "SycmApi")]:
    t = strip_comments(io.open(facade_file, encoding="utf-8", errors="replace").read())
    for m in re_sig.finditer(t):
        facade_map.setdefault("%s.%s" % (cls, m.group(1)), []).append(m.group(2))

# ---------- 1. 窗体类名 → 文件索引 ----------
form_files = {}
for dirpath, dirnames, filenames in os.walk(MAIN):
    dirnames[:] = [d for d in dirnames if d not in ("obj", "bin", ".vs", "docs")]
    for fn in filenames:
        if fn.endswith(".cs") and not fn.endswith(".Designer.cs"):
            name = fn[:-3].rstrip(".")
            rel = os.path.join(dirpath, fn).replace("\\", "/").replace(MAIN + "/", "")
            form_files.setdefault(name, []).append(rel)

def resolve_form(fname, context):
    if not fname:
        return None
    cands = form_files.get(fname, [])
    if not cands:
        return None
    if len(cands) == 1:
        return cands[0]
    pref = {"Search": ["One/Search", "One/Common", "One/Download", "Forms", "One"],
            "Display": ["One/Display", "One/Common", "One/Download", "Forms", "One"],
            "Comm": ["One/Common", "Forms"]}
    for p in pref.get(context, []):
        for c in cands:
            if c.startswith(p):
                return c
    return cands[0]

# ---------- 2. 解析 MD ----------
text = io.open(MD, encoding="utf-8").read()
lines = text.split("\n")
sections = []; cur_top = cur_mod = cur_win = None; cur_sec = None; mode = None
re_form_in_text = re.compile(r"`(Form\w+|Notify\w+|One\.[\w.]+)`")

for ln in lines:
    s = ln.strip()
    if s.startswith("######"):
        title = s[6:].strip()
        fm = re.search(r"[：:]\s*`?([\w.]+)`?$", title)
        cur_sec = {"title": title, "form": fm.group(1) if fm else "", "top": cur_top,
                   "mod": cur_mod, "win": cur_win, "menus": [], "downstream": [],
                   "funcs": [], "data": []}
        sections.append(cur_sec); mode = None; continue
    if s.startswith("#####") and not s.startswith("######"):
        cur_win = s[5:].strip(); continue
    if s.startswith("####") and not s.startswith("#####"):
        cur_mod = s[4:].strip(); continue
    if s.startswith("###") and not s.startswith("####"):
        cur_top = s[3:].strip(); cur_mod = None; continue
    if cur_sec is None:
        continue
    if "右键菜单" in s: mode = "menu"; continue
    if "下游功能模块" in s or "下游动作" in s: mode = "downstream"; continue
    if s.startswith("- 功能模块") or s.startswith("- 功能入口"): mode = "func"; continue
    if "表格数据" in s: mode = "data"; continue
    if s.startswith("## ") or s.startswith("# "): mode = None; continue
    m = re.match(r"^-\s+(.*)", s)
    if not m or not mode: continue
    item = m.group(1).strip()
    if mode == "menu":
        cur_sec["menus"].append(item.strip("`"))
    elif mode == "downstream":
        dm = re.match(r"(.*?)[：:]\s*`?(\w+)`?$", item)
        if dm and dm.group(2).startswith("Form"):
            cur_sec["downstream"].append({"name": dm.group(1), "form": dm.group(2)})
        else:
            cur_sec["downstream"].append({"name": item.strip("`"), "form": None})
    elif mode == "func":
        cur_sec["funcs"].append(item.strip("`"))
    elif mode == "data":
        cur_sec["data"].append(item)

# ---------- 3. 匹配 ----------
def norm(t):
    return t.replace("：", ":").split(":")[-1].strip().strip("`")

def match_in(menu_item, downstream):
    nm = norm(menu_item)
    best, bestd = None, 999
    for d in downstream:
        dn = norm(d["name"])
        if nm == dn or (len(nm) >= 3 and len(dn) >= 3 and (nm in dn or dn in nm)):
            dist = abs(len(dn) - len(nm))
            if dist < bestd:
                best, bestd = d, dist
    return best

def form_apis(rel):
    u = usage.get(rel or "", {})
    return u.get("apis", []), u.get("requests", [])

def expand_requests(apis, reqs):
    """OneApi 方法 → 背后的 Request 类"""
    out = list(reqs)
    for a in apis:
        for r in facade_map.get(a, []):
            if r not in out:
                out.append(r)
    return out

ctx_of = lambda sec: "Search" if "Search" in sec["form"] else ("Display" if "Display" in sec["form"] else "Comm")

lists_out = []
for sec in sections:
    ctx = ctx_of(sec)
    sec_form = sec["form"].split(".")[-1]
    sec_form_file = resolve_form(sec_form, ctx) if sec_form else None
    # 同步任务窗体（来自表格数据区）
    sync_forms = []
    for dline in sec["data"]:
        if "同步" in dline or "FormNotify" in dline:
            sync_forms += re.findall(r"FormNotify\w+", dline)
    menus_out = []
    for mi in sec["menus"]:
        d = match_in(mi, sec["downstream"])
        src = "section"
        if not d:  # 跨节回退：同体系其他节
            for other in sections:
                if other is sec or not other["downstream"]:
                    continue
                if ctx_of(other) != ctx and ctx in ("Search", "Display"):
                    continue
                d = match_in(mi, other["downstream"])
                if d:
                    src = "cross:" + other["title"].split("：")[0]
                    break
        act_form = d["form"] if d else None
        act_file = resolve_form(act_form, ctx) if act_form else None
        apis, reqs = form_apis(act_file)
        # 同步类菜单 → FormNotify*
        if not apis and not reqs and "同步" in mi and sync_forms:
            for sf in sync_forms:
                sf_file = resolve_form(sf, ctx)
                a2, r2 = form_apis(sf_file)
                if a2 or r2:
                    act_form, act_file, apis, reqs = sf, sf_file, a2, r2
                    src = "syncForm"
                    break
        menus_out.append({
            "item": mi, "action": d["name"] if d else "", "src": src,
            "actionForm": act_form or "", "actionFile": act_file or "",
            "apis": apis, "requests": expand_requests(apis, reqs),
        })
    self_apis, self_reqs = form_apis(sec_form_file)
    lists_out.append({
        "title": sec["title"], "form": sec["form"], "formFile": sec_form_file or "",
        "top": sec["top"], "mod": sec["mod"], "ctx": ctx,
        "selfApis": self_apis, "selfRequests": expand_requests(self_apis, self_reqs),
        "syncForms": sync_forms,
        "menus": menus_out,
        "funcs": [{"item": f} for f in sec["funcs"]],
    })

# ---------- 4. 反向索引 ----------
req_callers, api_callers = {}, {}
for rel, u in usage.items():
    for r in u["requests"]:
        req_callers.setdefault(r, []).append(rel)
    for a in u["apis"]:
        api_callers.setdefault(a, []).append(rel)

out = {"lists": lists_out, "reqCallers": req_callers, "apiCallers": api_callers,
       "facadeMap": facade_map}
io.open(r"D:/project/38max/oneplus/linkage.json", "w", encoding="utf-8").write(
    json.dumps(out, ensure_ascii=False, indent=1))

n_menu = sum(len(l["menus"]) for l in lists_out)
n_linked = sum(1 for l in lists_out for m in l["menus"] if m["requests"])
print("sections:", len(lists_out), " menus:", n_menu, " linked(有接口):", n_linked)
used_reqs = set()
for l in lists_out:
    used_reqs.update(l["selfRequests"])
    for m in l["menus"]:
        used_reqs.update(m["requests"])
print("菜单/列表直接关联到的 Request 种类:", len(used_reqs))
