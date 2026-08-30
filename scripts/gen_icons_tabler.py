#!/usr/bin/env python3
"""Generate stdlib/Gui/icons/tabler.json — the curated Tabler icon pack for
Gui.IconSvgData.

Selection: a hand-picked core list of everyday app-UI names is always kept
(dropped silently if Tabler renamed them); "-filled" twins of picked outline
icons are excluded; the rest is ranked by UI-relevance keywords with at most
--family-cap icons per name prefix (so alert-*/arrow-*/... cannot flood the
table); the remainder of the requested count is filled with an even stride
over the untouched leftovers.

Bodies are fetched from the Iconify API (https://api.iconify.design), with
double quotes rewritten to single quotes so the generated Zan string
literals need no escaping. Tabler is uniformly 24x24, which lets the runtime
wrapper hardcode the viewBox.

Usage:
    python scripts/gen_icons_tabler.py --count 1000 \
        --out stdlib/Gui/icons/tabler.json

Tabler Icons is MIT licensed: https://github.com/tabler/tabler-icons
"""

import argparse
import json
import sys
import urllib.parse
import urllib.request

API = "https://api.iconify.design"

# Everyday names that must exist in the table regardless of scoring. Only
# names Tabler actually has land in the output; renames degrade gracefully.
CORE = [
    "home", "heart", "star", "settings", "search", "user", "users",
    "user-plus", "bell", "bell-x", "calendar", "calendar-event", "clock",
    "hourglass", "mail", "mail-opened", "send", "message", "message-circle",
    "message-2", "phone", "phone-call", "phone-off", "camera", "photo",
    "image", "images", "music", "video", "player-play", "player-pause",
    "player-stop", "player-skip-forward", "player-skip-back", "player-record",
    "volume", "volume-2", "volume-3", "plus", "minus", "x", "check", "checks",
    "trash", "trash-off", "edit", "pencil", "pencil-plus", "copy", "clipboard",
    "clipboard-check", "clipboard-text", "paste", "scissors",
    "download", "upload", "cloud", "cloud-upload", "cloud-download",
    "cloud-off", "folder", "folders", "folder-plus", "folder-minus",
    "folder-open", "file", "file-text", "file-plus", "file-download",
    "file-upload", "files", "bookmark", "bookmarks", "tag", "tags", "link",
    "unlink", "share", "external-link", "lock", "lock-open", "unlock", "key",
    "shield", "shield-check", "shield-lock", "eye", "eye-off", "filter",
    "filter-off", "sort-ascending", "sort-descending", "refresh", "reload",
    "rotate", "rotate-clockwise", "zoom-in", "zoom-out", "zoom-check",
    "arrow-left", "arrow-right", "arrow-up", "arrow-down", "arrow-back-up",
    "arrow-forward-up", "arrow-up-right", "chevron-left", "chevron-right",
    "chevron-up", "chevron-down", "chevrons-left", "chevrons-right",
    "chevrons-up", "chevrons-down", "caret-down", "caret-up", "caret-left",
    "caret-right", "menu", "menu-2", "menu-3", "dots", "dots-vertical",
    "grip-vertical", "grip-horizontal", "layout-grid", "layout-2",
    "layout-dashboard", "layout-navbar", "layout-sidebar", "layout-rows",
    "layout-columns", "table", "list", "list-check", "list-details",
    "list-numbers", "grid-dots", "components", "box", "boxes", "package",
    "puzzle", "apps", "code", "code-plus", "terminal", "terminal-2",
    "braces", "brackets", "binary", "json", "html", "css", "markdown",
    "database", "database-export", "database-import", "server", "server-2",
    "server-off", "cpu", "device-desktop", "device-laptop", "device-mobile",
    "device-tablet", "device-tv", "device-watch", "device-gamepad",
    "device-speaker", "printer", "keyboard", "mouse", "bluetooth", "wifi",
    "wifi-off", "plug", "plug-connected", "plug-off", "battery",
    "battery-charging", "battery-off", "power", "logout", "login",
    "dashboard", "gauge", "chart-bar", "chart-line", "chart-area",
    "chart-pie", "chart-dots", "trending-up", "trending-down", "activity",
    "coin", "coins", "wallet", "credit-card", "cash", "cash-banknote",
    "receipt", "receipt-2", "shopping-cart", "shopping-bag", "discount-2",
    "gift", "ticket", "truck", "building", "building-store", "building-bank",
    "briefcase", "award", "trophy", "medal", "badge", "thumb-up", "thumb-down",
    "flag", "flag-2", "pin", "map-pin", "map", "compass", "navigation",
    "world", "sun", "moon", "stars", "cloud-rain", "cloud-snow", "cloud-storm",
    "temperature", "snowflake", "flame", "droplet", "wind", "leaf", "plant",
    "bug", "virus", "pill", "first-aid-kit", "stethoscope", "heartbeat",
    "mood-happy", "mood-smile", "mood-sad", "mood-angry", "ghost", "rocket",
    "sparkles", "wand", "bolt", "bolt-off", "circle-check", "circle-x",
    "circle-plus", "circle-minus", "circle-info", "alert-triangle",
    "help", "info-circle", "history", "archive", "inbox", "scan", "crop",
    "frame", "layers-intersect", "stack-2", "vector", "anchor", "boat",
    "car", "bike", "plane", "train", "bus", "bed", "bathtub", "chef-hat",
    "coffee", "cup", "tools", "tool", "hammer", "brush", "palette",
    "color-picker", "eraser", "test-pipe", "math", "calculator",
    "calendar-stats", "clipboard-list", "note", "notes", "book", "books",
    "book-2", "notebook", "rss", "antenna", "signal-5g", "broadcast",
    "satellite", "radar-2", "crosshair", "target", "shield-half",
    "hand-stop", "heart-rate-monitor", "legs", "run", "walk",
    # ---- 窗口/软件常用（标题栏、面板、状态）——语义必备，不靠打分 ----
    # minimize/maximize/restore 经 Iconify search 确认 Tabler 原生存在；
    # unlock 不存在（语义对应 lock-open，由 IconSvg 的别名表处理）。
    "minimize", "maximize", "restore", "window", "app-window",
    "app-window-filled", "window-minimize", "window-maximize",
    "fullscreen", "picture-in-picture",
    "arrows-minimize", "arrows-maximize", "collapse", "expand",
    "pinned", "pinned-off", "pin", "lock-open", "square-key",
    "layout-navbar-collapse", "fold", "unfold", "fold-up", "fold-down",
    "eye-off",
    # 语义图标集（Icon.SvgName 映射）需要的具体形态：
    "star-filled", "device-floppy", "align-left", "align-center",
    "align-right", "square", "typography", "select-all", "hand",
    "align-top", "align-middle", "align-bottom",
]

# Substrings that add relevance for names outside the core list. Coarse on
# purpose — this is curation, not ranking.
KEYWORDS = [
    "arrow", "chevron", "caret", "sort", "filter", "search", "zoom",
    "check", "alert", "info", "help", "user", "mail", "message", "send",
    "phone", "bell", "calendar", "clock", "time", "file", "folder",
    "clipboard", "copy", "edit", "download", "upload", "cloud", "refresh",
    "reload", "play", "pause", "volume", "music", "video", "camera",
    "photo", "image", "home", "menu", "dots", "grip", "layout", "grid",
    "list", "table", "row", "column", "panel", "sidebar", "window",
    "browser", "tab", "bookmark", "star", "heart", "thumb", "tag", "flag",
    "pin", "link", "share", "lock", "key", "shield", "eye", "scan",
    "settings", "adjustments", "tool", "toggle", "switch", "slider",
    "text", "align", "indent", "color", "palette", "brush", "device",
    "desktop", "laptop", "mobile", "printer", "keyboard", "mouse",
    "battery", "wifi", "bluetooth", "plug", "power", "server", "database",
    "code", "terminal", "bug", "chart", "graph", "trending", "dashboard",
    "gauge", "wallet", "coin", "credit-card", "cash", "cart", "shopping",
    "package", "box", "truck", "gift", "ticket", "book", "note", "rss",
    "world", "map", "compass", "sun", "moon", "history", "archive", "inbox",
    "fullscreen", "picture-in-picture", "transfer", "exchange", "login",
    "logout", "stack", "layers", "component", "puzzle", "badge", "award",
    "trophy", "flame", "bolt", "droplet", "activity", "mood",
]


def fetch_json(url: str):
    req = urllib.request.Request(url, headers={"User-Agent": "zan-icon-gen"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


def score(name: str) -> int:
    s = 0
    for kw in KEYWORDS:
        if kw in name:
            s += 1
    return s


def family(name: str) -> str:
    return name.split("-")[0]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=1000)
    ap.add_argument("--family-cap", type=int, default=8)
    ap.add_argument("--out", default="stdlib/Gui/icons/tabler.json")
    args = ap.parse_args()

    col = fetch_json(f"{API}/collection?prefix=tabler")
    names = list(col.get("uncategorized") or [])
    for cat in (col.get("categories") or {}).values():
        names.extend(cat)
    names = sorted(set(names))
    if not names:
        print("error: no icon names returned", file=sys.stderr)
        return 1
    have = set(names)

    # 1. Core list (intersect with reality, report renames).
    picked = [n for n in CORE if n in have]
    dropped = [n for n in CORE if n not in have]
    if dropped:
        print(f"note: {len(dropped)} core names not in Tabler, e.g. "
              f"{dropped[:8]}")
    picked_set = set(picked)

    # 2. Keyword-ranked remainder, family-capped, no -filled duplicates.
    fam_count = {}
    candidates = [
        n for n in names
        if n not in picked_set and not n.endswith("-filled")
    ]
    candidates.sort(key=lambda n: (-score(n), n))
    for n in candidates:
        if len(picked) >= args.count:
            break
        f = family(n)
        if fam_count.get(f, 0) >= args.family_cap:
            continue
        fam_count[f] = fam_count.get(f, 0) + 1
        picked.append(n)
        picked_set.add(n)

    # 3. Even stride over whatever is left.
    if len(picked) < args.count:
        rest = [
            n for n in names
            if n not in picked_set and not n.endswith("-filled")
        ]
        need = args.count - len(picked)
        stride = max(1, len(rest) // need)
        for n in rest[::stride]:
            if len(picked) >= args.count:
                break
            picked.append(n)
    picked = sorted(picked[: args.count])

    # Fetch bodies in batches; rewrite " -> ' so Zan literals need no escapes.
    bodies = {}
    for i in range(0, len(picked), 100):
        batch = picked[i : i + 100]
        data = fetch_json(
            f"{API}/tabler.json?icons=" + urllib.parse.quote(",".join(batch))
        )
        for n, info in (data.get("icons") or {}).items():
            w = info.get("width", data.get("width", 24))
            h = info.get("height", data.get("height", 24))
            if w != 24 or h != 24:
                print(f"error: {n} is {w}x{h}, expected 24x24", file=sys.stderr)
                return 1
            bodies[n] = info["body"].replace('"', "'")
    missing = [n for n in picked if n not in bodies]
    if missing:
        print(f"error: {len(missing)} bodies missing, e.g. {missing[:5]}",
              file=sys.stderr)
        return 1
    picked = sorted(bodies)

    pack = {n: bodies[n] for n in picked}
    out_text = json.dumps(pack, ensure_ascii=False, separators=(",", ":")) + "\n"
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(out_text)
    total = sum(len(bodies[n]) for n in picked)
    print(f"wrote {args.out}: {len(picked)} icons, {total} body bytes, "
          f"pack {len(out_text)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
