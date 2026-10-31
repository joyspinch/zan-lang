#!/usr/bin/env python3
"""组件契约审计：Gui 组件必须满足的几条硬约定。

约定（每条都对应一次真实返工，见 TASKS.md 与 git 历史）：

R1 名单一致   同一份"控件 kind 名单"在三处副本里必须对得上：
              ① 运行期重建注册表（ControlBootstrap.Names + 各族 bootstrap）
              ② 编译期限定名单（GenForm.IsStdWidgetKind / IsHmiKind）
              ③ 控件自己声明的 Kind()
              两个方向都要对：
              - 注册表里有、编译期不认 → 生成代码发裸名，用户项目的同名
                全局类会压过 stdlib；
              - 控件声明了 Kind()、编译期不认 → 导出成设计稿后编译不过
                （或退化成用户自己的同名类）。

R2 size 通道   声明了 `Binding<string> Size` 的控件必须覆写 SizeOf()
              （设计器靠它发现尺寸档），反之亦然。

R3 档位齐全   支持尺寸档的控件，其 CSS 类型必须在 base.css 里有
              .tiny/.small/.large 三档规则（medium 是默认档，可以没有）。

R4 样式入口   控件的**实例方法**不得直接调 Style.Of / Style.Full——
              一律走基类入口（Control.Box / BoxState / BareBox /
              ResolveStyleAs），否则行内样式不生效、用户写的 Class 失效、
              每帧多克隆一份 StyleBox。

用法：python scripts/component_audit.py [--all] [--list]
退出码 0 = 没有违规。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUI = os.path.join(ROOT, 'packages/Zan.Gui/src/Gui')
INDUSTRIAL = os.path.join(ROOT, 'packages/Zan.Industrial/src/Gui/Hmi')
BASE_CSS = os.path.join(GUI, 'skins/base.css')
BOOTSTRAP = os.path.join(GUI, 'Core/ControlBootstrap.zan')
GENFORM = os.path.join(ROOT, 'stdlib/System/Compiler/GenForm.zan')

# 审计范围：核心控件库（Widget/Component/Core/Layout）+ 工控件。
# Designer 是应用、Charts/CodeEditor/DataTable 是独立家族，各有各的
# 样式写法，不在本契约范围内。
SCAN_SUBDIRS = ('Widget', 'Component', 'Core', 'Layout')

# 内部件：不进注册表、也不作为设计器面板项——它们由代码直接拼装。
INTERNAL = {
    'BoxContent', 'ChoiceGroup', 'Column', 'Message', 'QrEncoder',
    'WidgetDelegates', 'FormBuilder', 'Tooltip', 'ContextMenu', 'Menu',
    'Popover', 'Prompt', 'Ellipsis', 'Layer', 'Ribbon', 'ScrollView',
    'Split', 'Table',
}
# 尺寸档不在自己身上、而是转给子控件的容器。
SIZE_PASSED_DOWN = {'RadioGroup', 'RadioButtonGroup'}

# 借用其他类型外观的控件：它自己的 Kind 不是 CSS 类型，档位规则要查
# 借用的那一族。key=控件名，value=借用的 CSS 类型。
BORROWED_TYPE = {
    'DynamicTags': 'tag',
    'InputOtp': 'otp',
    'SelectBox': 'select',
    'Dropdown': 'select',
    'InputNumber': 'input',
    'DatePicker': 'input',
    'Typography': 'label',
    'RichText': 'label',
    'RadioButtonGroup': 'radiogroup',
}

RE_KIND = re.compile(
    r'override\s+string\s+Kind\(\)\s*(?:\{\s*return\s*"([A-Za-z0-9]+)"|;\s*return\s+([A-Za-z0-9]+))')
RE_METHOD_DECL = re.compile(
    r'^[ \t]*(?:public |private |protected )?(static )?(?:override |virtual |abstract )*'
    r'[A-Za-z_][\w<>\[\], .]*\s+([A-Za-z_]\w*)\s*\(', re.M)
RE_STYLETYPE = re.compile(r'override\s+string\s+StyleType\(\)\s*\{?\s*return\s*"([a-z0-9_-]+)"')
RE_SIZEFIELD = re.compile(r'Binding<string>\s+Size\s*;')
RE_SIZEOF = re.compile(r'override\s+Binding<string>\s+SizeOf\(\)')
RE_STYLE_DIRECT = re.compile(r'Style\.(?:Of|Full)\(\s*app\b')
RE_CONTROL_KEYWORDS = {
    'if', 'for', 'while', 'switch', 'catch', 'return', 'else', 'do',
    'foreach', 'using', 'lock', 'new', 'typeof',
}
# GenForm 的编译期名单（stdlib/System/Compiler/GenForm.zan）
RE_GENFORM_LIST = re.compile(
    r'public static bool (IsStdWidgetKind|IsHmiKind)\(string kind\)\s*\{(.*?)\n    \}',
    re.S)
RE_GENFORM_QUALIFY = re.compile(
    r'public static string QualifyKind\(string kind\)\s*\{(.*?)\n    \}', re.S)
RE_NONCONSTRUCTIBLE = re.compile(
    r'static List<string> NonConstructibleKinds\(\)\s*\{(.*?)\n\}', re.S)
# 各族 bootstrap 的注册点（运行期可重建的 kind）
RE_REGISTER = re.compile(r'HeavyControls\.Register\(\s*"([A-Za-z0-9]+)"')
RE_NAMES = re.compile(r'static List<string> Names\(\)\s*\{(.*?)\n    \}', re.S)
RE_MAKE_BRANCH = re.compile(r'if \(kind == "([A-Za-z0-9]+)"\)')


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def widget_files():
    """核心控件库 + 工控件的全部 .zan。"""
    out = []
    for sub in SCAN_SUBDIRS:
        d = os.path.join(GUI, sub)
        if not os.path.isdir(d):
            continue
        for root, _dirs, names in os.walk(d):
            for name in sorted(names):
                if name.endswith('.zan'):
                    out.append(os.path.join(root, name))
    if os.path.isdir(INDUSTRIAL):
        for name in sorted(os.listdir(INDUSTRIAL)):
            if name.endswith('.zan'):
                out.append(os.path.join(INDUSTRIAL, name))
    return out


def strip_code(text):
    """去掉注释与字符串字面量——括号配对不能被它们里的花括号带偏。"""
    text = re.sub(r'/\*.*?\*/', lambda m: ' ' * len(m.group(0)), text, flags=re.S)
    text = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), text)
    text = re.sub(r'"(?:[^"\\]|\\.)*"', lambda m: ' ' * len(m.group(0)), text)
    return text


def method_spans(text):
    """每个方法体的字符区间 [(start, end, name, is_static)]。"""
    s = strip_code(text)
    spans = []
    for m in RE_METHOD_DECL.finditer(s):
        name = m.group(2)
        if name in RE_CONTROL_KEYWORDS:
            continue
        line_end = s.find('\n', m.end())
        seg = s[m.end():line_end if line_end >= 0 else len(s)]
        if ';' in seg or '{' in seg:
            # 声明行里出现 ';' → 是表达式；出现 '{' → 同行已是函数体，
            # 仍由下面的花括号配对处理。
            if ';' in seg:
                continue
        i = s.find('{', m.end())
        if i < 0:
            continue
        depth = 0
        end = len(s)
        j = i
        while j < len(s):
            if s[j] == '{':
                depth += 1
            elif s[j] == '}':
                depth -= 1
                if depth == 0:
                    end = j
                    break
            j += 1
        spans.append((m.start(), end, name, m.group(1) is not None))
    return spans


def enclosing_method(spans, pos):
    """返回 (方法名, 是否 static)；不在任何方法体内时按 static 处理。"""
    best = None
    for start, end, name, is_static in spans:
        if start <= pos <= end:
            if best is None or start >= best[0]:
                best = (start, name, is_static)
    if best is None:
        return ('', True)
    return (best[1], best[2])


def css_types():
    """base.css 里出现过的 `type.class` 组合。"""
    css = read(BASE_CSS)
    css = re.sub(r'/\*.*?\*/', '', css, flags=re.S)
    pairs = set()
    for m in re.finditer(r'([^{}]+)\{', css):
        for sel in m.group(1).split(','):
            sel = sel.strip()
            if not sel:
                continue
            for part in sel.split():
                if '.' in part:
                    t, c = part.split('.', 1)
                    t = t.split('::')[0].split(':')[0]
                    c = c.split('::')[0].split(':')[0]
                    if t and c:
                        pairs.add((t, c))
    return pairs


def registered_kinds():
    """运行期可重建的 kind 集合（主注册表 + 各族 bootstrap 的注册点）。"""
    out = set()
    text = read(BOOTSTRAP)
    m = RE_NAMES.search(text)
    if m:
        out |= set(re.findall(r'"([A-Za-z0-9]+)"', m.group(1)))
    for root, _dirs, names in os.walk(os.path.join(ROOT, 'packages')):
        for name in names:
            if not name.endswith('.zan'):
                continue
            path = os.path.join(root, name)
            if not os.path.basename(path).endswith('Bootstrap.zan'):
                continue
            out |= set(RE_REGISTER.findall(read(path)))
    return out


def genform_kinds():
    """编译期限定名单：已随编译期 HTML 通道删除废除（文件若存在仍读）。"""
    if not os.path.exists(GENFORM):
        return set()
    text = read(GENFORM)
    out = set()
    for _name, body in RE_GENFORM_LIST.findall(text):
        out |= set(re.findall(r'"([A-Za-z0-9]+)"', body))
    m = RE_GENFORM_QUALIFY.search(text)
    if m:
        out |= set(re.findall(r'kind == "([A-Za-z0-9]+)"', m.group(1)))
    return out


def nonconstructible_kinds():
    """GenForm 里明令不可实例化的 kind（抽象基类 / 内部字段记录）。"""
    if not os.path.exists(GENFORM):
        return set()
    text = read(GENFORM)
    m = RE_NONCONSTRUCTIBLE.search(text)
    if not m:
        return set()
    return set(re.findall(r'"([A-Za-z0-9]+)"', m.group(1)))


def control_kinds(files):
    """控件自己声明的 Kind() → 文件名。"""
    out = {}
    for path in files:
        text = read(path)
        m = RE_KIND.search(text)
        if m:
            kind = m.group(1) or m.group(2)
            out.setdefault(kind, os.path.basename(path)[:-4])
    return out


def effective_type(name, text, kind):
    """控件解析样式时用的 CSS 类型。"""
    if name in BORROWED_TYPE:
        return BORROWED_TYPE[name]
    m = RE_STYLETYPE.search(text)
    if m:
        return m.group(1)
    return (kind or name).lower()


def main():
    args = sys.argv[1:]
    verbose = '--all' in args
    listing = '--list' in args
    problems = []
    files = widget_files()
    reg = registered_kinds()
    comp = genform_kinds()
    nonconstructible = nonconstructible_kinds()
    kinds = control_kinds(files)

    # R1：一份 kind 名单，三处副本必须对得上（编译期 GenForm 存在时）
    if comp:
        for kind in sorted(reg - comp):
            problems.append('R1 运行期可重建的 kind "%s" 不在编译期名单里'
                            '（生成代码发裸名，用户项目的同名全局类会压过 stdlib）'
                            % kind)
        for kind, name in sorted(kinds.items()):
            if kind in comp or kind in nonconstructible:
                continue
            problems.append('R1 控件 %s 声明了 Kind()="%s"，但编译期名单里没有它'
                            '（导出成设计稿后编译不过，或退化成用户自己的同名类）'
                            % (name, kind))

    # R2：Size 字段 ↔ SizeOf() 覆写
    sizes = {}
    for path in files:
        text = read(path)
        name = os.path.basename(path)[:-4]
        has_field = bool(RE_SIZEFIELD.search(text))
        has_override = bool(RE_SIZEOF.search(text))
        sizes[name] = (has_field, text)
        if has_field and not has_override:
            problems.append('R2 %s 声明了 Size 字段却没覆写 SizeOf()'
                            '（设计器发现不到尺寸档）' % name)
        if has_override and not has_field:
            problems.append('R2 %s 覆写了 SizeOf() 却没声明 Size 字段' % name)

    # R3：支持尺寸档的控件，四档规则要齐（medium 可以没有）
    pairs = css_types()
    for path in files:
        text = read(path)
        name = os.path.basename(path)[:-4]
        if not RE_SIZEFIELD.search(text):
            continue
        if name in SIZE_PASSED_DOWN:
            continue
        t = effective_type(name, text, kinds.get(name, ''))
        if not t:
            continue
        for tier in ('tiny', 'small', 'large'):
            if (t, tier) not in pairs:
                problems.append('R3 %s 支持尺寸档，但 base.css 没有 %s.%s 规则'
                                '（档位设了不生效）' % (name, t, tier))

    # R4：实例方法不得直接调 Style.Of / Style.Full
    calls = []
    for path in files:
        text = read(path)
        name = os.path.basename(path)[:-4]
        if name == 'Control':      # 基类就是入口本身
            continue
        spans = method_spans(text)
        for m in RE_STYLE_DIRECT.finditer(text):
            meth, is_static = enclosing_method(spans, m.start())
            line = text[:m.start()].count('\n') + 1
            calls.append((name, line, meth, is_static))
            if is_static:
                continue
            problems.append('R4 %s:%d 实例方法 %s 直接调 %s——走基类入口'
                            '（Control.Box / BoxState / BareBox / ResolveStyleAs）'
                            % (name, line, meth, m.group(0).split('(')[0]))

    if verbose:
        print('扫描控件文件 %d 个，注册表 kind %d 个，编译期名单 %d 个'
              % (len(files), len(reg), len(comp)))
    if listing:
        for name, line, meth, is_static in calls:
            print('CALL %s:%d %s%s' % (name, line, meth,
                                       ' (static)' if is_static else ''))
    for p in problems:
        print('VIOLATION ' + p)
    if problems:
        print('组件契约违规 %d 条' % len(problems))
        return 1
    print('组件契约：0 违规（控件 %d 个，注册表 kind %d 个，编译期名单 %d 个）'
          % (len(files), len(reg), len(comp)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
