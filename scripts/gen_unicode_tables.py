#!/usr/bin/env python3
"""Generate stdlib/System/Text/UnicodeTables.zan from Python's unicodedata."""

import unicodedata

def main():
    # 1. Combining classes (CCC > 0)
    ccc_list = []
    for cp in range(1, 0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        c = unicodedata.combining(chr(cp))
        if c > 0:
            ccc_list.append((cp, c))
    ccc_list.sort(key=lambda x: x[0])

    # 2. Canonical single-step decomposition (excluding Hangul 0xAC00..0xD7A3)
    decomp_list = []
    for cp in range(1, 0x110000):
        if 0xD800 <= cp <= 0xDFFF or 0xAC00 <= cp <= 0xD7A3:
            continue
        ch = chr(cp)
        dec = unicodedata.decomposition(ch)
        if dec and not dec.startswith('<'):
            parts = [int(p, 16) for p in dec.split()]
            p0 = parts[0]
            p1 = parts[1] if len(parts) > 1 else 0
            decomp_list.append((cp, p0, p1))
    decomp_list.sort(key=lambda x: x[0])

    # 3. Canonical composition (L, C) -> P
    comp_list = []
    for cp, p0, p1 in decomp_list:
        if p1 != 0:
            ch = chr(cp)
            p_str = chr(p0) + chr(p1)
            if unicodedata.normalize('NFC', p_str) == ch:
                comp_list.append(((p0, p1), cp))
    comp_list.sort(key=lambda x: (x[0][0], x[0][1]))

    # 4. Case folding (UAX #44)
    cf_1to1 = []
    cf_multi = []
    for cp in range(1, 0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        ch = chr(cp)
        cf = ch.casefold()
        if cf != ch:
            if len(cf) == 1:
                cf_1to1.append((cp, ord(cf)))
            else:
                cf_multi.append((cp, cf))
    cf_1to1.sort(key=lambda x: x[0])
    cf_multi.sort(key=lambda x: x[0])

    # 5. Unicode ToUpper 1:N expansions (e.g. ß -> SS)
    up_multi = []
    for cp in range(1, 0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        ch = chr(cp)
        u = ch.upper()
        if len(u) > 1:
            up_multi.append((cp, u))
    up_multi.sort(key=lambda x: x[0])

    out = []
    out.append('using System;')
    out.append('')
    out.append('namespace System.Text;')
    out.append('')
    out.append('// 由 scripts/gen_unicode_tables.py 自动生成的 Unicode 规范化与大小写映射表（UAX #15 与 UAX #44）。')
    out.append('class UnicodeTables {')

    # CCC
    out.append(f'    // Combining classes (cp, ccc), 共 {len(ccc_list)} 项')
    out.append('    public static int[] ccc = new int[] {')
    chunk = []
    for cp, c in ccc_list:
        chunk.append(f'{cp}, {c}')
        if len(chunk) == 8:
            out.append('        ' + ', '.join(chunk) + ',')
            chunk = []
    if chunk:
        out.append('        ' + ', '.join(chunk) + ',')
    out.append('    };')
    out.append('')

    # Decomp
    out.append(f'    // Canonical single-step decomposition (cp, p0, p1), 共 {len(decomp_list)} 项')
    out.append('    public static int[] decomp = new int[] {')
    chunk = []
    for cp, p0, p1 in decomp_list:
        chunk.append(f'{cp}, {p0}, {p1}')
        if len(chunk) == 4:
            out.append('        ' + ', '.join(chunk) + ',')
            chunk = []
    if chunk:
        out.append('        ' + ', '.join(chunk) + ',')
    out.append('    };')
    out.append('')

    # Comp keys
    out.append(f'    // Canonical composition keys ((long)p0 << 32) | (long)p1, 共 {len(comp_list)} 项')
    out.append('    public static long[] compKeys = new long[] {')
    chunk = []
    for (p0, p1), cp in comp_list:
        val = (p0 << 32) | p1
        chunk.append(f'{val}L')
        if len(chunk) == 4:
            out.append('        ' + ', '.join(chunk) + ',')
            chunk = []
    if chunk:
        out.append('        ' + ', '.join(chunk) + ',')
    out.append('    };')
    out.append('')

    # Comp values
    out.append(f'    // Canonical composition values, 共 {len(comp_list)} 项')
    out.append('    public static int[] compValues = new int[] {')
    chunk = []
    for (p0, p1), cp in comp_list:
        chunk.append(f'{cp}')
        if len(chunk) == 12:
            out.append('        ' + ', '.join(chunk) + ',')
            chunk = []
    if chunk:
        out.append('        ' + ', '.join(chunk) + ',')
    out.append('    };')
    out.append('')

    # CaseFold 1:1
    out.append(f'    // CaseFold 1:1 mappings (from_cp, to_cp), 共 {len(cf_1to1)} 项')
    out.append('    public static int[] caseFold1to1 = new int[] {')
    chunk = []
    for f_cp, t_cp in cf_1to1:
        chunk.append(f'{f_cp}, {t_cp}')
        if len(chunk) == 6:
            out.append('        ' + ', '.join(chunk) + ',')
            chunk = []
    if chunk:
        out.append('        ' + ', '.join(chunk) + ',')
    out.append('    };')
    out.append('')

    # CaseFold multi helper
    def to_escaped_string(s):
        parts = []
        for ch in s:
            co = ord(ch)
            if co < 128 and chr(co) not in ('\\', '"'):
                parts.append(chr(co))
            else:
                parts.append(f'\\u{co:04X}')
        return ''.join(parts)

    out.append('    // CaseFold 1:N multi-character expansions')
    out.append('    public static string CaseFoldMulti(int cp) {')
    out.append('        switch (cp) {')
    for cp, cf in cf_multi:
        esc = to_escaped_string(cf)
        out.append(f'            case {cp}: return "{esc}";')
    out.append('            default: return "";')
    out.append('        }')
    out.append('    }')
    out.append('')

    out.append('    // ToUpper 1:N multi-character expansions')
    out.append('    public static string ToUpperMulti(int cp) {')
    out.append('        switch (cp) {')
    for cp, u in up_multi:
        esc = to_escaped_string(u)
        out.append(f'            case {cp}: return "{esc}";')
    out.append('            default: return "";')
    out.append('        }')
    out.append('    }')
    out.append('}')
    out.append('')

    with open('stdlib/System/Text/UnicodeTables.zan', 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))

    print(f'Wrote stdlib/System/Text/UnicodeTables.zan successfully, total lines: {len(out)}')

if __name__ == '__main__':
    main()
