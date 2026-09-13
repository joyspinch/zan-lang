# -*- coding: utf-8 -*-
"""生成 examples/html_gallery/App.html —— HTML 组件全量画廊设计稿。

规则（2026-09 第二轮重设计）：
  - 真 HTML 语义优先：button/label/input/textarea/img 五种有原生标签的
    组件一律用原生标签 + 原生属性/文本内容（src、placeholder、元素文本），
    其余组件用 div + data-kind 扩展（设计稿协议的 custom-element 通道）。
  - 演示尺寸按组件自身特性配置，按 1560×920 逻辑窗口 4 列大卡布局，
    每页行高打包并断言不超出内容区（页面不滚动、不裁切）。
  - 每张卡片 = 无标题 Panel + 自放标题 label + 演示节点（标题位置
    完全可预测，绕开 Panel 标题的 padT 预留）；卡片边框走内联 style。
  - 9 个无法纯声明喂活的组件（泛型/需模型/无零参构造）在 HTML 里落
    Panel 占位，由 App.zan code-behind 构造真控件填进去（CODE_FED）。
  - 顶部 Tabs 分类条（带选中高亮），页签由 code-behind 喂。
"""
import json, io, base64, struct, zlib

import os
ROOT = os.path.normpath(os.path.join(os.path.dirname(
    os.path.abspath(__file__)), "..", "..", ".."))
OUT = os.path.join(ROOT, "examples", "html_gallery", "App.html")
CATALOG = [l.strip() for l in io.open(
    os.path.join(ROOT, "tools", "mcp_server", "zform.controls.txt"),
    encoding="utf-8").read().splitlines() if l.strip()]
EXCLUDED = {"WebViewBox", "CefBrowserBox", "ChoiceGroup"}
# 由 code-behind 构造真控件的占位壳（HTML 里是 Panel）
CODE_FED = {"Wizard", "Popover", "FormField", "ListView", "AlarmBanner",
            "AlarmList", "EquipPanel", "DeviceCard", "ButtonGroup"}

WIN_W, WIN_H = 1560, 920
HEAD_H = 44
MARGIN = 16
GAP = 16
COLS = 4
CONTENT_X = MARGIN
CONTENT_Y = HEAD_H + MARGIN             # 60
CONTENT_W = WIN_W - MARGIN * 2          # 1528
CONTENT_H = WIN_H - CONTENT_Y - MARGIN  # 844
CARD_PAD = 16
TITLE_H = 24
DEMO_DY = 40                            # 卡内演示节点 y
CARD_EXTRA = DEMO_DY + 14               # card_h = demo_h + CARD_EXTRA

# ---- Image 演示用渐变 PNG（纯标准库编码，data-uri） -------------------
def png_uri():
    w, h = 640, 360
    rows = []
    for y in range(h):
        row = bytearray([0])  # PNG filter 0
        u = y / (h - 1)
        for x in range(w):
            t = x / (w - 1)
            row += bytes((int(47 + 40 * u), int(111 + 60 * t),
                          int(237 - 60 * t)))
        rows.append(bytes(row))
    raw = b"".join(rows)
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    return "data:image/png;base64," + base64.b64encode(png).decode()

def P(**kw):
    return {k: str(v) for k, v in kw.items()}

# 每项：(页, kind, 卡标题, demo_dw, demo_h, 属性字典)。dw=0 → 卡宽自适应。
# 原生标签：Button/Label/Input/TextArea/Image 由 emit_demo 特判。
D = []
def add(page, kind, cap, dw, dh, a=None):
    D.append((page, kind, cap, dw, dh, a or {}))

# ---- 基础控件 13 ----
add("基础控件", "Button", "Button 按钮", 0, 48,
    {"native": "button", "text": "点我", "class": "primary"})
add("基础控件", "Label", "Label 标签", 0, 32,
    {"native": "label", "text": "普通标签文本"})
add("基础控件", "Typography", "Typography 排版", 0, 88,
    {"native": "p", "text": "标题与正文排版层级"})
add("基础控件", "Divider", "Divider 分割线", 0, 24,
    {"native": "hr", "text": "分割线"})
add("基础控件", "IconView", "IconView 图标", 96, 96,
    {"props": P(name="home", box="48")})
add("基础控件", "Image", "Image 图片", 0, 180,
    {"native": "img", "src": png_uri(), "fit": "contain"})
add("基础控件", "Avatar", "Avatar 头像", 96, 96,
    {"props": P(text="ZK", size="64")})
add("基础控件", "Badge", "Badge 徽标", 120, 40,
    {"props": P(count="8")})
add("基础控件", "Tag", "Tag 标签", 160, 36,
    {"props": P(text="可关闭标签", closable="true")})
add("基础控件", "DynamicTags", "DynamicTags 动态标签", 0, 48, {})
add("基础控件", "Watermark", "Watermark 水印", 0, 120,
    {"props": P(content="Zan · 水印")})
add("基础控件", "Card", "Card 卡片", 0, 140,
    {"props": P(title="卡片标题"), "kid_label": "Card 内嵌内容"})
add("基础控件", "Panel", "Panel 面板", 0, 140,
    {"props": P(title="面板标题", style="2"), "kid_label": "Panel 内嵌内容"})
# ---- 输入 17 ----
add("输入", "Input", "Input 输入框", 0, 40,
    {"native": "input", "placeholder": "请输入用户名"})
add("输入", "TextArea", "TextArea 多行文本", 0, 110,
    {"native": "textarea", "placeholder": "请输入简介", "showCount": "true"})
add("输入", "InputNumber", "InputNumber 数字", 0, 40,
    {"native": "input_t", "itype": "number",
     "tattrs": ' min="0" max="100" step="1" value="42"'})
add("输入", "InputOtp", "InputOtp 验证码", 0, 48,
    {"props": P(length="6")})
add("输入", "Checkbox", "Checkbox 多选", 200, 32,
    {"native": "input_t", "itype": "checkbox",
     "tattrs": ' value="记住我" checked="true"'})
add("输入", "Switch", "Switch 开关", 140, 32, {"defOn": True})
add("输入", "Radio", "Radio 单选", 200, 32,
    {"native": "input_t", "itype": "radio", "tattrs": ' value="已选中"'})
add("输入", "RadioButton", "RadioButton 按钮单选", 200, 36,
    {"props": P(label="选项 A")})
add("输入", "RadioGroup", "RadioGroup 组单选", 0, 36,
    {"options": ["北京", "上海", "广州"]})
add("输入", "RadioButtonGroup", "RadioButtonGroup 按钮组", 0, 40,
    {"options": ["日", "周", "月"]})
add("输入", "CheckboxGroup", "CheckboxGroup 多选组", 0, 36,
    {"options": ["苹果", "香蕉", "橙子"]})
add("输入", "Slider", "Slider 滑块", 0, 40,
    {"native": "input_t", "itype": "range",
     "tattrs": ' min="0" max="100" value="40"'})
add("输入", "Rate", "Rate 评分", 200, 36, {"props": P(value="3")})
add("输入", "DatePicker", "DatePicker 日期", 0, 40,
    {"native": "input_t", "itype": "date",
     "tattrs": ' value="2026-09-13"'})
add("输入", "ColorPicker", "ColorPicker 颜色", 0, 40,
    {"native": "input_t", "itype": "color",
     "tattrs": ' value="#378add"'})
add("输入", "Upload", "Upload 上传", 0, 110,
    {"props": P(triggerText="点击上传", tip="单个文件不超过 10 MB")})
add("输入", "SelectBox", "SelectBox 下拉选择", 0, 40,
    {"native": "select",
     "options": ["Web 前端", "桌面客户端", "服务端"]})
# ---- 数据展示 12（行内高低搭配，压缩纵向空间） ----
add("数据展示", "Statistic", "Statistic 统计", 0, 88,
    {"props": P(text="活跃用户", value="12,480")})
add("数据展示", "Countdown", "Countdown 倒计时", 0, 48,
    {"props": P(duration="90000", format="mm:ss", active="true")})
add("数据展示", "NumberAnimation", "NumberAnimation 数字动画", 0, 40,
    {"props": {"from": "0"}})
add("数据展示", "Marquee", "Marquee 横幅滚动", 0, 36,
    {"props": P(text="Zan GUI 标准库 · 这条横幅向左滚动并无缝循环",
                speed="60", autoFill="true")})
add("数据展示", "QrCode", "QrCode 二维码", 176, 176,
    {"props": P(text="https://zan-lang.dev", size="160")})
add("数据展示", "Calendar", "Calendar 日历", 300, 280, {})
add("数据展示", "Timeline", "Timeline 时间线", 0, 200, {})
add("数据展示", "ListItem", "ListItem 列表项", 0, 64,
    {"props": P(text="列表项标题", desc="辅助说明文字")})
add("数据展示", "Skeleton", "Skeleton 骨架屏", 0, 64,
    {"props": P(width="280", height="48")})
add("数据展示", "Spin", "Spin 加载中", 120, 96, {"props": P(tip="加载中")})
add("数据展示", "CodeBlock", "CodeBlock 代码面板", 0, 160, {})
add("数据展示", "Progress", "Progress 进度条", 0, 28,
    {"native": "progress", "value": "72"})
# ---- 导航 11 ----
add("导航", "Tabs", "Tabs 页签", 0, 140, {})
add("导航", "PageHeader", "PageHeader 页头", 0, 72,
    {"props": P(text="项目设置", subtitle="管理成员与权限", icon="settings")})
add("导航", "Breadcrumb", "Breadcrumb 面包屑", 0, 32, {})
add("导航", "Pagination", "Pagination 分页", 0, 36,
    {"props": P(total="115", pageSize="10")})
add("导航", "Steps", "Steps 步骤条", 0, 72, {})
add("导航", "Wizard", "Wizard 向导（模板列表 + 实时描述）", 0, 210, {})
add("导航", "FloatButton", "FloatButton 浮动按钮", 96, 56,
    {"props": P(icon="plus")})
add("导航", "SplitPanel", "SplitPanel 分栏", 0, 140, {})
add("导航", "Carousel", "Carousel 轮播", 0, 140, {})
add("导航", "Collapse", "Collapse 折叠面板", 0, 140, {})
add("导航", "TreeView", "TreeView 树", 0, 180, {})
# ---- 反馈 6 ----
add("反馈", "Empty", "Empty 空状态", 0, 120,
    {"props": P(text="暂无数据", icon="inbox")})
add("反馈", "Result", "Result 结果", 0, 130,
    {"props": P(text="操作成功", desc="配置已保存并生效。")})
add("反馈", "AlarmBanner", "AlarmBanner 报警条", 0, 44, {})
add("反馈", "Popover", "Popover 气泡", 0, 56, {})
add("反馈", "AlarmList", "AlarmList 报警列表", 0, 130, {})
add("反馈", "FormField", "FormField / FormBuilder 迷你表单", 0, 240, {})
# ---- 列表与表格 7 ----
add("列表与表格", "ListView", "ListView 列表", 0, 140, {})
add("列表与表格", "VirtualList", "VirtualList 虚拟列表", 0, 160, {})
add("列表与表格", "DataGrid", "DataGrid 数据表格", 0, 170,
    {"of": "DemoRow",
     "columns": [{"field": "name", "title": "组件", "width": 140},
                 {"field": "kind", "title": "分类", "width": 100},
                 {"field": "size", "title": "数量", "width": 70,
                  "type": "num"}]})
add("列表与表格", "Transfer", "Transfer 穿梭框", 0, 160, {})
add("列表与表格", "ConsoleView", "ConsoleView 控制台", 0, 140, {})
add("列表与表格", "BandGrid", "BandGrid 波段表", 0, 170, {})
add("列表与表格", "GraphView", "GraphView 关系图", 0, 170, {})
# ---- HMI 专用 11 ----
add("HMI 专用", "Led", "Led 指示灯", 120, 48,
    {"props": P(label="RUN", state="1")})
add("HMI 专用", "Digital", "Digital 数显", 0, 72,
    {"props": P(label="TIC101.PV", value="1523", scale="10", unit="°C")})
add("HMI 专用", "Bargraph", "Bargraph 棒图", 80, 160, {})
add("HMI 专用", "Gauge", "Gauge 仪表", 160, 170,
    {"props": P(label="PV", value="62", min="0", max="100", unit="%")})
add("HMI 专用", "Trend", "Trend 趋势", 0, 150, {})
add("HMI 专用", "NumPad", "NumPad 数字键盘", 200, 230, {})
add("HMI 专用", "EquipPanel", "EquipPanel 设备墙", 0, 210, {})
add("HMI 专用", "DeviceCard", "DeviceCard 设备卡", 240, 90, {})
add("HMI 专用", "ButtonGroup", "ButtonGroup 按钮组", 0, 44, {})
add("HMI 专用", "ToolStrip", "ToolStrip 工具条", 0, 40,
    {"options": ["新建", "打开", "保存"]})
add("HMI 专用", "StatusBar", "StatusBar 状态栏", 0, 32,
    {"options": ["就绪", "UTF-8", "第 1 页"]})

PAGES = []
for item in D:
    if not PAGES or PAGES[-1][0] != item[0]:
        PAGES.append((item[0], []))
    PAGES[-1][1].append(item[1:])

# ---- 覆盖率与布局断言 ----
placed = [it[0] for (_t, items) in PAGES for it in items]
assert len(placed) == len(set(placed)), "kind 重复放置"
EXTRA = {"CheckboxGroup", "ListView"}   # 目录本身漏登的两个真组件
assert set(placed) == set(CATALOG) - EXCLUDED | EXTRA, (
    "覆盖面不齐: 缺 %s 多 %s" % (
        sorted((set(CATALOG) - EXCLUDED | EXTRA) - set(placed)),
        sorted(set(placed) - (set(CATALOG) - EXCLUDED | EXTRA))))

CARD_W = (CONTENT_W - (COLS - 1) * GAP) // COLS   # 370
DEMO_W = CARD_W - CARD_PAD * 2                     # 338

def esc(s):
    s = str(s)
    return s.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;")

def attr(name, val):
    return ' %s="%s"' % (name, esc(val))

def geom(fx, fy, fw, fh):
    return (attr("data-fx", fx) + attr("data-fy", fy)
            + attr("data-fw", fw) + attr("data-fh", fh))

CARD_STYLE = "background:#ffffff; border:1px solid #e5e7eb; border-radius:10px"
TITLE_STYLE = "color:#6b7280; font-size:13px"

def emit_demo(kind, dw, dh, a, ind):
    out = []
    did = "Demo" + kind
    fx, fy = CARD_PAD, DEMO_DY
    if a.get("native") == "button":
        out.append('%s<button id="%s" class="primary"%s>%s</button>' % (
            ind, did, geom(fx, fy, dw, dh), esc(a["text"])))
    elif a.get("native") == "label":
        out.append('%s<label id="%s"%s>%s</label>' % (
            ind, did, geom(fx, fy, dw, dh), esc(a["text"])))
    elif a.get("native") == "input":
        out.append('%s<input id="%s" placeholder="%s"%s />' % (
            ind, did, esc(a["placeholder"]), geom(fx, fy, dw, dh)))
    elif a.get("native") == "textarea":
        extra = ""
        if a.get("showCount"):
            extra = attr("data-show-count", a["showCount"])
        out.append('%s<textarea id="%s" placeholder="%s"%s%s></textarea>' % (
            ind, did, esc(a["placeholder"]), extra, geom(fx, fy, dw, dh)))
    elif a.get("native") == "hr":
        out.append('%s<hr id="%s" data-text="%s"%s />' % (
            ind, did, esc(a["text"]), geom(fx, fy, dw, dh)))
    elif a.get("native") == "p":
        out.append('%s<p id="%s"%s>%s</p>' % (
            ind, did, geom(fx, fy, dw, dh), esc(a["text"])))
    elif a.get("native") == "progress":
        out.append(("%s<progress id=\"%s\" value=\"%s\" max=\"100\""
                    " data-x-props='{\"showIndicator\":\"true\"}'%s />") % (
            ind, did, a["value"], geom(fx, fy, dw, dh)))
    elif a.get("native") == "select":
        opts = "".join("<option>%s</option>" % esc(o) for o in a["options"])
        out.append('%s<select id="%s"%s>%s</select>' % (
            ind, did, geom(fx, fy, dw, dh), opts))
    elif a.get("native") == "input_t":
        out.append('%s<input id="%s" type="%s"%s%s />' % (
            ind, did, a["itype"], a["tattrs"], geom(fx, fy, dw, dh)))
    elif a.get("native") == "img":
        out.append('%s<img id="%s" src="%s" data-fit="%s"%s />' % (
            ind, did, a["src"], a.get("fit", "contain"),
            geom(fx, fy, dw, dh)))
    elif kind in CODE_FED:
        out.append('%s<div id="%s" data-kind="Panel"%s></div>' % (
            ind, did, geom(fx, fy, dw, dh)))
    else:
        head = '%s<div id="%s" data-kind="%s"' % (ind, did, kind)
        head += geom(fx, fy, dw, dh)
        if "of" in a:
            head += attr("data-of", a["of"])
        if "options" in a:
            head += attr("data-x-options",
                         json.dumps(a["options"], ensure_ascii=False))
        if "columns" in a:
            head += attr("data-x-columns",
                         json.dumps(a["columns"], ensure_ascii=False))
        if a.get("defOn"):
            head += attr("data-def-on", "true")
        if a.get("class"):
            head += attr("class", a["class"])
        if "props" in a:
            head += attr("data-x-props",
                         json.dumps(a["props"], ensure_ascii=False))
        head += ">"
        out.append(head)
        if "kid_label" in a:
            out.append('%s  <label%s%s>%s</label>' % (
                ind, geom(12, 34, dw - 24, 28),
                attr("style", "color:#6b7280"), esc(a["kid_label"])))
        out.append('%s</div>' % ind)
    return out

lines = []
lines.append('<!doctype html>')
lines.append('<html>')
lines.append('<head><meta charset="utf-8"><title>HtmlGallery</title></head>')
lines.append('<body data-zan-design id="HtmlGallery"%s%s%s%s%s>' % (
    attr("data-win-w", WIN_W), attr("data-win-h", WIN_H),
    attr("data-win-title", "HTML 组件全量画廊"), attr("data-win-center", "true"),
    attr("data-layout-mode", "1")))
# 分类条：Tabs（带选中高亮）；页签由 code-behind 喂。
lines.append('  <div id="Cats" data-kind="Tabs"%s></div>'
             % attr("data-fh", HEAD_H))

for pi, (ptitle, items) in enumerate(PAGES):
    lines.append('  <div id="Page%d" data-kind="Panel"%s>' % (
        pi, geom(CONTENT_X, CONTENT_Y, CONTENT_W, CONTENT_H)))
    y = 8
    for rs in range(0, len(items), COLS):
        row = items[rs:rs + COLS]
        row_h = 0
        for ci, (kind, cap, dw0, dh, a) in enumerate(row):
            dw = dw0 or DEMO_W
            card_h = dh + CARD_EXTRA
            row_h = max(row_h, card_h)
            x = ci * (CARD_W + GAP)
            lines.append('    <div id="Card%d_%d" data-kind="Panel"%s%s%s>' % (
                pi, rs + ci, geom(x, y, CARD_W, card_h),
                attr("class", "demo-card"), attr("style", CARD_STYLE)))
            lines.append('      <label%s%s>%s</label>' % (
                geom(CARD_PAD, 10, CARD_W - CARD_PAD * 2, TITLE_H),
                attr("style", TITLE_STYLE), esc(cap)))
            for l in emit_demo(kind, dw, dh, a, "      "):
                lines.append(l)
            lines.append('    </div>')
        y = y + row_h + GAP
    assert y - GAP <= CONTENT_H, (
        "Page%d 高度溢出: %d > %d" % (pi, y - GAP, CONTENT_H))
    lines.append('  </div>')
lines.append('</body>')
lines.append('</html>')

io.open(OUT, "w", encoding="utf-8", newline="\n").write(
    "\n".join(lines) + "\n")
print("WROTE %s: %d pages, %d demos, card=%d demo_w=%d" % (
    OUT, len(PAGES), len(placed), CARD_W, DEMO_W))
