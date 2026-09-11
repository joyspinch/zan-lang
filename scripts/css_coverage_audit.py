#!/usr/bin/env python3
"""CSS 支持度审计：给定 CSS 语料，引擎能真正吃下多少（按出现次数计权）。

回答的是"抄一份外面的 CSS 进来，有多少会生效"这类问题，而不是"引擎实现了
CSS 规范的百分之几"——后者的分母（MDN 约 570 条属性索引）对写皮肤的人没有
意义：声明数才是影响观感的量。按语料计权，同一个数字会随语料完全不同（本项目
自带皮肤围着引擎写，接近满分；bootstrap 那种网页 CSS 低得多），所以**报数字
必须带语料**。

三个层次都报，不合并成一个"支持度"：

  选择器  接受且会匹配（含后代/组合器链与结构性伪类——它们在 retained
          控件树内解析）/ 语法接受但永不匹配（:has()、生成内容的伪元素、
          引擎观察不到的属性如 `[hidden]`）/ 语法拒绝（语法坏）
  声明    引擎认得（解析进样式盒）/ 认得但空转（Inert）/ 不认得
  取值    认得但值被静默强转——`%`/em/rem/vw/calc() 等都已有求值器，
          只剩白名单外的单位（拼错的、冷门的）会被裸数字吞掉

at-rule：`@supports`/`@layer`/`@media` 的内容引擎会解析（守卫/媒体条件的
真假属于值语义，不在本脚本的判定范围），这里递归计入；`@import` 语句
按展开成功计 accepted；`@keyframes` 等整块跳过，单独计数。

引擎侧的真值全部从源码现取（`k == "..."` / `Inert()` / 伪状态名），不维护
平行的手抄表：引擎加了属性，这里跟着变。

用法：
    python scripts/css_coverage_audit.py --corpus-a
    python scripts/css_coverage_audit.py _scratch/asset/css
    python scripts/css_coverage_audit.py --corpus-a --json out.json

一个已知的保守处：这里把"引擎认得"当作"生效"。更严的口径还要扣掉"值确实进了
样式盒、但绘制路径从不读那个字段"的键（white-space/vertical-align 已由 P2
行盒消费并移出名单；position/top/left/right/bottom/inset/z-index/order 已由
布局/渲染端消费并移出名单）。要报那一档，得先有 key -> 字段 -> 渲染处 的映射。
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STYLESHEET = os.path.join(ROOT, 'stdlib', 'Gui', 'StyleSheet.zan')
STYLE = os.path.join(ROOT, 'stdlib', 'Gui', 'Style.zan')

# 本项目自带的皮肤文件（不含 13 个图表主题包，那是另一条线）。
CORPUS_A = [
    'stdlib/Gui/skins/base.css',
    'stdlib/Gui/skins/*/skin.css',
    'src/ide_zan/assets/ide.css',
    'templates/game/*/skins/base.css',
    'templates/game/*/skins/*/skin.css',
]

# 浏览器/打印/原生表单控件专属：retained-mode 控件工具箱没有对应动作，把它们
# 算进"支持度"分母只会让数字难看而没有行动价值。默认单独归一类，`--all` 时并入。
# display/float/clear 自 WEB_GUI_ROADMAP P0 起是真实布局键（不再归入
# 网页专属类）；white-space/vertical-align 自 WEB_GUI_ROADMAP P2 起
# 由行盒/落位消费，同批移出。
FLOW_ONLY = re.compile(
    r'^(-webkit-|-moz-|-ms-|-o-|page-break|break-|content$|quotes$|counter-'
    r'|list-style|outline|orphans$|widows$)'
)

# 有求值器的单位（StyleSheet.HasUnresolvedUnit 的白名单 + 角度补充）。
# 数字后跟的单位不在名单里（拼错的、冷门的）才会被裸数字吞掉。
RESOLVED_UNITS = {'px', 'em', 'rem', 'ex', 'ch', 'vw', 'vh', 'vmin', 'vmax',
                  'pt', 'pc', 'cm', 'mm', 'in', 'q', 'ms', 's', 'deg',
                  'turn', 'rad', 'grad', 'fr', 'hz', 'khz'}


def coerced_value(val):
    """值里是否有"数字 + 白名单外单位"（会被当裸数字吞掉）。

    颜色（#hex、颜色函数）与 url()/var()/calc() 的内容不是单位，先剥掉；
    这与引擎 HasUnresolvedUnit 的放行口径一致。"""
    v = re.sub(r'"[^"]*"', '', val)
    v = re.sub(r"'[^']*'", '', v)
    v = re.sub(r'#[0-9a-fA-F]{3,8}\b', '', v)
    v = re.sub(r'\b(rgb|rgba|hsl|hsla|hwb|oklab|oklch|lab|lch|color-mix|url|var|calc|min|max|clamp)\s*\([^)]*\)',
               '', v)
    # CSS 里单位紧跟数字（`1px`），中间不允许空白——`1 solid`、
    # `0 1 auto` 的关键字不是单位。
    for m in re.finditer(r'\d(?:\.\d+)?([a-zA-Z]+)', v):
        if m.group(1).lower() not in RESOLVED_UNITS:
            return True
    return False

# 引擎会匹配的伪状态（stdlib/Gui/Style.zan:92-97 的 name 分派）。
STATE_NAMES = {'hover', 'active', 'focus', 'focus-visible', 'disabled',
               'selected', 'checked'}

# "解析进样式盒、但绘制路径从不读那个字段"的候选键。这是**输入**不是本脚本
# 推导出来的事实：名单来自上一轮报告（那轮把 bootstrap 的 12% 声明、14.5% 规则
# 归到这一类，是"静默失败比不支持更糟"的依据）。position/top/right/bottom/
# left/inset/z-index/order 曾在此名单——现在布局引擎（Arrange/ArrangePositioned、
# FlexKids 的 order 排序）与渲染/命中路径真实消费它们，css_test 的
# Positioned/Cascade 段是行为证据，已移出。要独立复核，得先建
# key -> StyleBox 字段 -> 渲染处 的映射，本脚本没做。`--no-consumer list`
# 可以换成你自己的名单。
DEFAULT_NO_CONSUMER = set()


def engine_surface():
    """从引擎源码取真值：认得的键、空转的键、会匹配的伪状态。"""
    src = open(STYLESHEET, encoding='utf-8').read()
    keys = set(re.findall(r'\bk\s*==\s*"([a-z0-9-]+)"', src))
    at = src.index('static bool Inert')
    end = src.index('static', at + 10)
    inert = set(re.findall(r'\bk\s*==\s*"([a-z0-9-]+)"', src[at:end]))
    ssrc = open(STYLE, encoding='utf-8').read()
    sb = ssrc.index('static int StateBit')
    states = set(re.findall(r'name\s*==\s*"([a-z-]+)"', ssrc[sb:sb + 700]))
    states |= STATE_NAMES
    if not keys or not inert:
        sys.exit('engine_surface: failed to read %s' % STYLESHEET)
    return keys, inert, states


def strip_comments(text):
    return re.sub(r'/\*.*?\*/', '', text, flags=re.S)


def split_top(text, sep):
    """按顶层分隔符切（括号/引号内的不算）。"""
    out, buf, depth, quote = [], '', 0, ''
    for ch in text:
        if quote:
            buf += ch
            if ch == quote:
                quote = ''
            continue
        if ch in '"\'':
            quote = ch
            buf += ch
            continue
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth = max(0, depth - 1)
        if ch == sep and depth == 0:
            out.append(buf)
            buf = ''
        else:
            buf += ch
    out.append(buf)
    return out


def match_brace(text, open_at):
    """与 open_at 处的 `{` 配对的 `}`（按嵌套深度，引号内的不算）。"""
    depth, quote = 0, ''
    for i in range(open_at, len(text)):
        ch = text[i]
        if quote:
            if ch == quote:
                quote = ''
            continue
        if ch in '"\'':
            quote = ch
        elif ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return i
    return -1


# 伪元素名（Css.Selector.IsPseudoElement）：能当部件名匹配。
PSEUDO_ELEMENTS = {'placeholder', 'selection', 'marker', 'backdrop',
                   'file-selector-button', 'caret', 'scrollbar', 'track', 'thumb'}
# 需要生成内容/行盒的伪元素（Css.Selector.IsGenerated）：永不匹配。
GENERATED = {'before', 'after', 'first-line', 'first-letter'}
# 布尔属性 -> 已有状态位（Css.Selector.AttrState）。
ATTR_STATE = {'disabled', 'checked', 'selected'}

# 结构性伪类（Css.Selector.StructCode）：在 retained 控件树内匹配。
STRUCT_PSEUDO = {'first-child', 'last-child', 'only-child', 'nth-child',
                 'nth-last-child', 'first-of-type', 'last-of-type',
                 'only-of-type', 'nth-of-type', 'nth-last-of-type',
                 'empty'}


def _skip_ws(s, i):
    while i < len(s) and s[i].isspace():
        i += 1
    return i


def _at_name(prelude):
    """at-rule 名字（去掉 `@`，取到空白/`(`/`{` 为止，转小写）。"""
    body = prelude[1:]
    end = len(body)
    for idx, ch in enumerate(body):
        if ch.isspace() or ch in '({':
            end = idx
            break
    return body[:end].strip().lower()


def _name_char(ch):
    """镜像 Css.Selector.NameChar。"""
    if ch in '.#:*' or ch in ',>+~[' or ch in '()]=' or ch in '"\'':
        return False
    return not ch.isspace()


def _match_paren(s, open_at):
    """与 open_at 处的 `(` 配对的 `)`（按嵌套深度）。"""
    depth = 0
    for i in range(open_at, len(s)):
        if s[i] == '(':
            depth += 1
        elif s[i] == ')':
            depth -= 1
            if depth == 0:
                return i
    return -1


def _classify_attr(inner, states):
    """镜像 Css.Selector.AddAttr：class / 布尔属性认，其余属性引擎观察
    不到，语法接受但永不匹配（'attr:<name>'）。"""
    k, m = 0, len(inner)
    name = ''
    while k < m and inner[k] not in '=~|^$*' and not inner[k].isspace():
        name += inner[k]
        k += 1
    name = name.lower()
    if name == '':
        return 'dead', 'attribute'
    while k < m and inner[k].isspace():
        k += 1
    if k < m:
        c = inner[k]
        if c == '=':
            k += 1
        elif c in '~|^$*' and k + 1 < m and inner[k + 1] == '=':
            k += 2
        else:
            return 'dead', 'attribute'
        while k < m and inner[k].isspace():
            k += 1
        val = ''
        if k < m and inner[k] in '"\'':
            q = inner[k]
            e = inner.find(q, k + 1)
            if e < 0:
                return 'dead', 'attribute'
            val = inner[k + 1:e]
            k = e + 1
        else:
            while k < m and not inner[k].isspace():
                val += inner[k]
                k += 1
        if val == '':
            return 'dead', 'attribute'
    if name == 'class' or name in ATTR_STATE:
        return 'live', ''
    return 'dead', 'attr:' + name


def _classify(s, states):
    """镜像 Css.Selector.Parse / ParseList。返回 (kind, why)：
    live=会参与匹配，dead=语法接受但永不匹配，rejected=整条规则被丢弃。"""
    if s == '':
        return 'rejected', 'empty'
    if s == ':root':
        return 'root', ''      # 变量块，不是被丢弃的规则
    never, why = False, ''
    i, n = 0, len(s)
    if n and s[0] == '*':
        i = 1                  # 通用选择器：现在支持
    else:
        while i < n and _name_char(s[i]):
            i += 1
    while i < n:
        ch = s[i]
        if ch.isspace():
            i = _skip_ws(s, i)      # 后代组合器：树上下文内会匹配
            continue
        if ch in '>+~':
            i += 1                  # 子/相邻/通用组合器：同上
            i = _skip_ws(s, i)
            continue
        if ch == '[':
            close = s.find(']', i)
            if close < 0:
                return 'rejected', 'attribute'
            k, w = _classify_attr(s[i + 1:close], states)
            if k == 'dead':
                never = True
                why = why or w
            i = close + 1
            continue
        if ch == ':':
            part = i + 1 < n and s[i + 1] == ':'
            j = i + 2 if part else i + 1
            name = ''
            while j < n and _name_char(s[j]):
                name += s[j]
                j += 1
            if name == '':
                return 'rejected', 'garbage'
            if part:
                if name in GENERATED:
                    never = True
                    why = why or ('generated:' + name)
                i = j
                continue
            if j < n and s[j] == '(':
                close = _match_paren(s, j)
                if close < 0:
                    return 'rejected', 'garbage'
                inner = s[j + 1:close]
                lo = name.lower()
                if lo == 'has':
                    never = True    # 引擎对 :has() 判 never
                    why = why or 'has'
                    i = close + 1
                    continue
                if lo == 'not':
                    ok = True
                    subs = split_top(inner, ',')
                    if not subs:
                        ok = False
                    for t in subs:
                        if _classify(t.strip(), states)[0] != 'live':
                            ok = False
                    if not ok:
                        never = True
                        why = why or 'not'
                elif lo in ('is', 'where'):
                    if not any(_classify(t.strip(), states)[0] == 'live'
                               for t in split_top(inner, ',')):
                        never = True
                        why = why or lo
                elif lo in STRUCT_PSEUDO:
                    pass            # 结构性伪类：树上下文内会匹配
                else:
                    never = True
                    why = why or ('pseudo-func:' + name.lower())
                i = close + 1
                continue
            if name in GENERATED:
                never = True
                why = why or ('generated:' + name)
            elif name in PSEUDO_ELEMENTS:
                pass               # 当部件
            elif name in states or name == 'enabled':
                pass
            elif name.lower() in STRUCT_PSEUDO:
                pass               # 结构性伪类：树上下文内会匹配
            else:
                never = True
                why = why or ('pseudo:' + name)
            i = j
            continue
        if ch == '*':
            i += 1                  # 链中间的通用选择器（`.a *`）
            continue
        if ch in '.#':
            j = i + 1
            name = ''
            while j < n and _name_char(s[j]):
                name += s[j]
                j += 1
            if name == '':
                return 'rejected', 'garbage'
            i = j
            continue
        if _name_char(ch):
            # 复合链延续的裸 type 名（`h1 small` 的 small、
            # `> thead` 的 thead）：引擎按复合块链解析，会匹配。
            while i < n and _name_char(s[i]):
                i += 1
            continue
        return 'rejected', 'garbage'
    if never:
        return 'dead', why
    return 'live', ''


def classify_selector(sel, states):
    """镜像 Css.Selector.Parse（stdlib/Gui/Css.zan）的接受条件，
    再判断伪状态/伪元素/属性是否会匹配。返回 (kind, why)。"""
    return _classify(sel.strip(), states)


def walk(css, keys, inert, states):
    text = strip_comments(css)
    st = {'live': 0, 'dead': 0, 'rejected': 0, 'root': 0}
    reasons = {}
    decl = {'ok': 0, 'inert': 0, 'unknown': 0, 'coerced': 0, 'custom': 0}
    ok_keys, unknown_keys, at_rules, at_stmts, rules = {}, {}, 0, 0, 0
    imports = 0
    i, n = 0, len(text)
    while i < n:
        brace = text.find('{', i)
        semi = text.find(';', i)
        if brace < 0:
            break
        if 0 <= semi < brace:
            stmt = text[i:semi].strip()
            if stmt.startswith('@'):
                at_stmts += 1
                if re.match(r'@import\s', stmt) or stmt.startswith('@import"'):
                    imports += 1     # 引擎按文件展开（缺文件静默跳过）
            i = semi + 1
            continue
        prelude = text[i:brace].strip()
        close = match_brace(text, brace)
        if close < 0:
            break
        if prelude.startswith('@'):
            name = _at_name(prelude)
            if name in ('supports', 'layer', 'media'):
                # 引擎会递归解析这三种 at-rule 的内容（@supports 守卫成立时、
                # @media 媒体条件成立时、@layer 一律摊平）。这里按"语法面"
                # 统计：条件真假属于值语义，不在本脚本的判定范围。
                sub = walk(text[brace + 1:close], keys, inert, states)
                for k in st:
                    st[k] += sub[0][k]
                for k, v in sub[1].items():
                    reasons[k] = reasons.get(k, 0) + v
                for k in decl:
                    decl[k] += sub[2][k]
                for k, v in sub[3].items():
                    ok_keys[k] = ok_keys.get(k, 0) + v
                for k, v in sub[4].items():
                    unknown_keys[k] = unknown_keys.get(k, 0) + v
                at_stmts += sub[6]
                rules += sub[7]
            else:
                at_rules += 1
            i = close + 1
            continue
        rules += 1
        for sel in split_top(prelude, ','):
            kind, why = classify_selector(sel, states)
            if kind == 'live':
                st['live'] += 1
            elif kind == 'dead':
                st['dead'] += 1
            elif kind == 'root':
                st['root'] += 1
            else:
                st['rejected'] += 1
                reasons[why] = reasons.get(why, 0) + 1
        if prelude != ':root':
            for d in split_top(text[brace + 1:close], ';'):
                d = d.strip()
                if not d or ':' not in d:
                    continue
                key, val = d.split(':', 1)
                key = key.strip().lower()
                val = val.split('!important')[0].strip()
                if key.startswith('--'):
                    decl['custom'] += 1
                    continue
                # 引擎 StripVendor：前缀剥离后按裸名分发。
                for vp in ('-webkit-', '-moz-', '-ms-', '-o-'):
                    if key.startswith(vp):
                        key = key[len(vp):]
                        break
                if key in keys:
                    if key in inert:
                        decl['inert'] += 1
                    else:
                        decl['ok'] += 1
                        ok_keys[key] = ok_keys.get(key, 0) + 1
                        if coerced_value(val):
                            decl['coerced'] += 1
                else:
                    decl['unknown'] += 1
                    unknown_keys[key] = unknown_keys.get(key, 0) + 1
        i = close + 1
    return st, reasons, decl, ok_keys, unknown_keys, at_rules, at_stmts, rules, imports


def files_for(args):
    if args.corpus_a:
        paths = []
        for pat in CORPUS_A:
            paths += glob.glob(os.path.join(ROOT, pat))
        return sorted(set(paths))
    out = []
    for d in args.dirs:
        out += glob.glob(os.path.join(d, '*.css'))
    return sorted(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('dirs', nargs='*', help='外部 CSS 目录（*.css）')
    ap.add_argument('--corpus-a', action='store_true', help='用仓内皮肤语料')
    ap.add_argument('--all', action='store_true', help='不单列网页/打印专属属性')
    ap.add_argument('--no-consumer', metavar='LIST',
                    help='逗号分隔：解析进盒子但没有绘制消费者的键（默认见脚本常量）')
    ap.add_argument('--json', metavar='PATH')
    args = ap.parse_args()
    if not args.corpus_a and not args.dirs:
        ap.error('either --corpus-a or one or more directories')

    keys, inert, states = engine_surface()
    files = files_for(args)
    if not files:
        sys.exit('no .css files matched')

    st = {'live': 0, 'dead': 0, 'rejected': 0, 'root': 0}
    reasons = {}
    decl = {'ok': 0, 'inert': 0, 'unknown': 0, 'coerced': 0, 'custom': 0}
    ok_keys, unknown_keys, at_rules, at_stmts, rules, lines = {}, {}, 0, 0, 0, 0
    imports = 0
    for p in files:
        css = open(p, encoding='utf-8', errors='replace').read()
        lines += css.count('\n') + 1
        a, rs, b, oks, u, at, ats, r, im = walk(css, keys, inert, states)
        for k in st:
            st[k] += a[k]
        for k, v in rs.items():
            reasons[k] = reasons.get(k, 0) + v
        for k in decl:
            decl[k] += b[k]
        for k, v in oks.items():
            ok_keys[k] = ok_keys.get(k, 0) + v
        for k, v in u.items():
            unknown_keys[k] = unknown_keys.get(k, 0) + v
        at_rules += at
        at_stmts += ats
        rules += r
        imports += im

    total = st['live'] + st['dead'] + st['rejected']
    dtotal = decl['ok'] + decl['inert'] + decl['unknown']
    pct = lambda a, b: 100.0 * a / max(1, b)

    print('corpus      : %s' % ('A (in-repo skins)' if args.corpus_a
                                else ' '.join(args.dirs)))
    print('files/lines : %d / %d   rules %d   at-rule blocks %d   at-statements %d   @imports %d'
          % (len(files), lines, rules, at_rules, at_stmts, imports))
    print('engine      : %d keys (%d inert), %d pseudo-states'
          % (len(keys), len(inert), len(states)))
    print()
    print('selectors   : %d total' % total)
    print('  live          %6d  %5.1f%%   (语法接受，伪状态会匹配)'
          % (st['live'], pct(st['live'], total)))
    print('  dead-state    %6d  %5.1f%%   (语法接受，伪状态永不匹配)'
          % (st['dead'], pct(st['dead'], total)))
    print('  rejected      %6d  %5.1f%%   (整条规则被丢弃)'
          % (st['rejected'], pct(st['rejected'], total)))
    print('  :root blocks  %6d            (变量表，不参与上面的分母)' % st['root'])
    for why, cnt in sorted(reasons.items(), key=lambda kv: -kv[1]):
        print('      %-12s %d' % (why, cnt))
    print()
    print('declarations: %d total (+%d custom properties)' % (dtotal, decl['custom']))
    print('  accepted      %6d  %5.1f%%' % (decl['ok'], pct(decl['ok'], dtotal)))
    print('  inert         %6d  %5.1f%%   (认得但无效果)'
          % (decl['inert'], pct(decl['inert'], dtotal)))
    print('  unknown       %6d  %5.1f%%' % (decl['unknown'], pct(decl['unknown'], dtotal)))
    print('  of accepted, value silently coerced: %d (%.1f%%)'
          % (decl['coerced'], pct(decl['coerced'], decl['ok'])))
    no_consumer = (set(x.strip().lower() for x in args.no_consumer.split(',') if x.strip())
                   if args.no_consumer else DEFAULT_NO_CONSUMER)
    silent = sum(c for k, c in ok_keys.items() if k in no_consumer)
    print('  of accepted, parsed but no drawing consumer: %d (%.1f%%)   [%s]'
          % (silent, pct(silent, decl['ok']), ', '.join(sorted(no_consumer))))
    print('      -> 这一档是"静默失败"：作者写对了、画面没变、也没有报错。')
    print()
    flow = sum(c for k, c in unknown_keys.items() if FLOW_ONLY.match(k))
    applicable = dtotal - flow - decl['inert']
    effective = decl['ok'] - silent
    print('effective: %d / %d applicable = %.1f%%   (第 1 档里的数字；分母排掉'
          % (effective, applicable, pct(effective, applicable)))
    print('           %d 条网页/打印专属属性与 %d 条 inert)' % (flow, decl['inert']))
    print('top unknown keys:')
    for k, c in sorted(unknown_keys.items(), key=lambda kv: -kv[1])[:15]:
        mark = ' [flow-only]' if (not args.all and FLOW_ONLY.match(k)) else ''
        print('  %-24s %4d%s' % (k, c, mark))

    if args.json:
        json.dump({'files': files, 'lines': lines, 'rules': rules,
                   'at_rules': at_rules, 'at_statements': at_stmts,
                   'selectors': st, 'reject_reasons': reasons, 'decls': decl,
                   'unknown': unknown_keys},
                  open(args.json, 'w', encoding='utf-8'), indent=1,
                  ensure_ascii=False)
        print('\nwrote %s' % args.json)


if __name__ == '__main__':
    main()
