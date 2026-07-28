#!/usr/bin/env python3
"""Regenerate Sdk.Wechat.Mp JSON wrappers from the vendored Senparc C# SDK.

Run from the repository root:
    python scripts/generate_wechat_mp.py

The generated audit manifest is written to _scratch/ and is intentionally not
version-controlled. Multipart, binary, stream, and non-JSON response methods
remain explicitly skipped until the shared HTTP standard library supports them.
"""

from pathlib import Path
import re, json

from wechat_typed_codegen import ModelIndex, SharedModelRegistry, emit_typed_classes

ROOT = Path.cwd()
src = (ROOT / 'stdlib/Sdk/WeiXinMPSDK-master/src/Senparc.Weixin.MP/Senparc.Weixin.MP/AdvancedAPIs').resolve()
out = ROOT / 'stdlib/Sdk/Wechat/Mp'
out.mkdir(parents=True, exist_ok=True)

MARKER = 'Generated from Senparc.Weixin.MP.AdvancedAPIs'
for p in out.glob('*.zan'):
    text = p.read_text(encoding='utf-8', errors='ignore')
    if MARKER in text:
        p.unlink()


def strip_comments(text):
    out = []
    i = 0
    state = 'code'
    escaped = False
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ''
        if state == 'line':
            if c in '\r\n':
                out.append(c)
                state = 'code'
            else:
                out.append(' ')
        elif state == 'block':
            if c == '*' and n == '/':
                out.extend([' ', ' '])
                i += 1
                state = 'code'
            elif c in '\r\n':
                out.append(c)
            else:
                out.append(' ')
        elif state == 'string':
            out.append(c)
            if escaped:
                escaped = False
            elif c == '\\':
                escaped = True
            elif c == '"':
                state = 'code'
        elif state == 'verbatim':
            out.append(c)
            if c == '"' and n == '"':
                out.append(n)
                i += 1
            elif c == '"':
                state = 'code'
        elif state == 'char':
            out.append(c)
            if escaped:
                escaped = False
            elif c == '\\':
                escaped = True
            elif c == "'":
                state = 'code'
        else:
            if c == '/' and n == '/':
                out.extend([' ', ' '])
                i += 1
                state = 'line'
            elif c == '/' and n == '*':
                out.extend([' ', ' '])
                i += 1
                state = 'block'
            elif c == '@' and n == '"':
                out.extend([c, n])
                i += 1
                state = 'verbatim'
            elif c == '"':
                out.append(c)
                state = 'string'
            elif c == "'":
                out.append(c)
                state = 'char'
            else:
                out.append(c)
        i += 1
    return ''.join(out)

def parse_methods(text):
    pat = re.compile(
        r'(?P<access>public|private|internal|protected)\s+'
        r'(?:static\s+)?(?:async\s+)?'
        r'(?P<ret>[\w<>,.?\[\]\s]+?)\s+'
        r'(?P<name>\w+)\s*'
        r'(?:<[^>{}]+>)?\s*'
        r'\((?P<params>[^)]*)\)\s*'
        r'(?:where[^\{]+)?\{',
        re.M,
    )
    out_methods = []
    for index, m in enumerate(pat.finditer(text)):
        start = m.end()
        depth = 1
        i = start
        in_str = False
        verbatim = False
        escaped = False
        while i < len(text) and depth:
            c = text[i]
            if in_str:
                if verbatim:
                    if c == '"' and i + 1 < len(text) and text[i + 1] == '"':
                        i += 2
                        continue
                    if c == '"':
                        in_str = False
                        verbatim = False
                else:
                    if escaped:
                        escaped = False
                    elif c == '\\':
                        escaped = True
                    elif c == '"':
                        in_str = False
            else:
                if c == '@' and i + 1 < len(text) and text[i + 1] == '"':
                    in_str = True
                    verbatim = True
                    i += 1
                elif c == '"':
                    in_str = True
                elif c == '{':
                    depth += 1
                elif c == '}':
                    depth -= 1
            i += 1
        out_methods.append({
            'id': index,
            'access': m.group('access'),
            'name': m.group('name'),
            'ret': ' '.join(m.group('ret').split()),
            'params': ' '.join(m.group('params').split()),
            'body': text[start:i - 1],
        })
    return out_methods

def path_literals(text):
    vals = []
    for value in re.findall(r'"([^"\r\n]+)"', text):
        candidate = value.strip()
        if candidate.startswith('/'):
            vals.append(candidate)
        elif re.match(r'https?://', candidate, re.I) and 'access_token' in candidate:
            vals.append(candidate)
    return vals

def base_path(url):
    if '://' in url:
        pos = url.find('/', url.find('://') + 3)
        url = url[pos:] if pos >= 0 else '/'
    url = url.replace('{{', '{').replace('}}', '}')
    return url.split('?', 1)[0]

def endpoint_constants(text):
    constants = {}
    pat = re.compile(
        r'(?:static\s+)?(?:readonly\s+)?string\s+(?P<name>\w+)\s*=\s*'
        r'(?P<expr>[^;]+);',
        re.M,
    )
    for m in pat.finditer(text):
        paths = path_literals(m.group('expr'))
        if paths:
            constants[m.group('name')] = paths[0]
    return constants

def expanded_body(method, by_name, depth=0, seen=None):
    if seen is None:
        seen = set()
    key = method['id']
    if key in seen or depth > 4:
        return ''
    seen = set(seen)
    seen.add(key)
    body = method['body']
    called = set(re.findall(r'\b([A-Za-z_]\w*)\s*(?:<[^;{}()]*>)?\s*\(', body))
    pieces = [body]
    for name in called:
        for callee in by_name.get(name, []):
            if callee['id'] not in seen:
                pieces.append(expanded_body(callee, by_name, depth + 1, seen))
    return '\n'.join(pieces)

def class_name(rel):
    parts = list(rel.parts)
    stem = Path(parts[-1]).stem
    if stem.lower().endswith('api'):
        stem = stem[:-3]
    names = []
    for part in parts[:-1] + [stem]:
        part = re.sub(r'[^A-Za-z0-9]', '', part)
        if part.lower() == 'merchant':
            part = 'Merchant'
        if part and (not names or names[-1].lower() != part.lower()):
            names.append(part[:1].upper() + part[1:])
    return 'WechatMp' + ''.join(names) + 'Api'

files = sorted({p.resolve() for p in src.rglob('*.cs') if p.name.lower().endswith('api.cs')})
model_index = ModelIndex(src.parent, strip_comments)
models = SharedModelRegistry(model_index, 'WechatMp')
manifest = []
skipped = []
total = 0
for p in files:
    source_text = p.read_text(encoding='utf-8-sig', errors='ignore')
    text = strip_comments(source_text)
    rel = p.relative_to(src)
    rel_text = str(rel).replace('\\', '/')
    if rel_text == 'OAuth/OAuthApi.cs':
        continue
    methods = parse_methods(text)
    for method in methods: method['source'] = p.as_posix()
    by_name = {}
    for method in methods:
        by_name.setdefault(method['name'], []).append(method)
    constants = endpoint_constants(text)
    emitted = []
    seen_names = set()
    for method in methods:
        if method['access'] != 'public' or not method['name'].endswith('Async'):
            continue
        if method['name'] in seen_names:
            continue
        seen_names.add(method['name'])
        if re.search(r'\b(Stream|FileStream|byte\[\])\b', method['params'] + ' ' + method['ret']) \
                or 'Post.PostFile' in method['body'] \
                or 'UploadCustomHeadimg' in method['name']:
            skipped.append((rel_text, method['name'], 'multipart/binary'))
            continue

        expanded = expanded_body(method, by_name)
        if 'FileHelper.GetFileStream' in expanded or (
                'RequestUtility.HttpPost' in expanded and 'CommonJsonSend' not in expanded):
            skipped.append((rel_text, method['name'], 'special transport/response'))
            continue
        urls = path_literals(expanded)
        resolution = 'method/helper'
        if not urls:
            for name, value in constants.items():
                if re.search(r'\b' + re.escape(name) + r'\b', expanded):
                    urls = [value]
                    resolution = 'named endpoint constant'
                    break
        if not urls:
            skipped.append((rel_text, method['name'], 'endpoint unresolved'))
            continue
        endpoint = urls[0]
        path = base_path(endpoint)
        if not path.startswith('/'):
            skipped.append((rel_text, method['name'], 'invalid endpoint'))
            continue

        is_get = bool(re.search(
            r'CommonJsonSendType\.GET|Get\.GetJson|HttpGet|\.GetAsync\s*\(',
            expanded,
        ))
        if re.search(r'CommonJsonSend\.Send|Post\.PostGetJson|HttpPost', expanded) \
                and 'CommonJsonSendType.GET' not in expanded:
            is_get = False
        emitted.append({
            'name': method['name'],
            'path': path,
            'endpoint': endpoint,
            'verb': 'GET' if is_get else 'POST',
            'resolution': resolution,
            'method': method,
        })

    if not emitted:
        continue
    cls = class_name(rel)
    typed = []
    meta = []
    for entry in emitted:
        req, resp, declarations = emit_typed_classes(
            cls, entry['method'], entry['endpoint'], entry['verb'],
            'access_token', model_index, entry['method']['source'], models)
        typed.extend(declarations)
        meta.append((entry, req, resp))
    lines = [
        'using System;',
        'using System.Json;',
        'using Sdk.Wechat;',
        'using Sdk.Wechat.Models.Mp;',
        '',
        'namespace Sdk.Wechat.Mp;',
        '',
        '/// <summary>Method request/response contracts; reusable DTO entities live in Sdk.Wechat.Models.Mp.</summary>',
    ]
    lines.extend(typed)
    lines += [
        '/// <summary>',
        f'/// {rel_text} 的 Zan 强类型 JSON 接口。',
        f'/// {MARKER}; do not hand-copy HTTP logic here.',
        '/// </summary>',
        f'class {cls} {{',
        '    WechatClient client;',
        '',
        f'    public {cls}(WechatClient client) {{',
        '        this.client = client;',
        '    }',
        '',
    ]
    for entry, req, resp in meta:
        lines.append(f'    /// <summary>{entry["verb"]} {entry["path"]}</summary>')
        arg = f'{req} request' if req else ''
        lines.append(f'    async {resp} {entry["name"]}({arg}) {{')
        if req:
            lines.append(f'        if (request == null) {{ request = new {req}(); }}')
            query = 'request.QueryText()'
            body = 'request.JsonBody()' if entry['verb'] != 'GET' else '""'
        else:
            query = '""'; body = '""'
        if entry['verb'] == 'GET':
            lines.append(f'        WechatResponse raw = await WechatMpRequest.GetAsync(this.client, "{entry["path"]}", {query});')
        else:
            lines.append(f'        WechatResponse raw = await WechatMpRequest.PostAsync(this.client, "{entry["path"]}", {query}, {body});')
        lines += [f'        {resp} result = Json.Deserialize<{resp}>(raw.Data);',
                  '        result.Raw = raw.Raw;', '        return result;', '    }', '']
        raw_name = re.sub(r'Async$', 'RawAsync', entry['name'])
        if entry['verb'] == 'GET':
            lines += [f'    async WechatResponse {raw_name}(string query) {{',
                      f'        return await WechatMpRequest.GetAsync(this.client, "{entry["path"]}", query);',
                      '    }', '']
        else:
            lines += [f'    async WechatResponse {raw_name}(string query, string jsonBody) {{',
                      f'        return await WechatMpRequest.PostAsync(this.client, "{entry["path"]}", query, jsonBody);',
                      '    }', '']
    lines.append('}')
    filename = cls + '.zan'
    (out / filename).write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    manifest.append({
        'source': rel_text,
        'file': 'Mp/' + filename,
        'class': cls,
        'methods': [{k:v for k,v in entry.items() if k != 'method'} for entry in emitted],
    })
    total += len(emitted)

models_dir = ROOT / 'stdlib/Sdk/Wechat/Models/Mp'
models_dir.mkdir(parents=True, exist_ok=True)
for old in models_dir.glob('*.zan'):
    if MARKER in old.read_text(encoding='utf-8', errors='ignore'):
        old.unlink()
(models_dir / 'WechatMpModels.zan').write_text(
    models.render('Sdk.Wechat.Models.Mp', MARKER, 'scripts/generate_wechat_mp.py'),
    encoding='utf-8', newline='\n')

(ROOT / '_scratch/wechat_mp_generated_manifest.json').write_text(
    json.dumps({'modules': manifest, 'skipped': skipped,
                'shared_models': len(models.models)}, ensure_ascii=False, indent=2),
    encoding='utf-8',
)
print(f'modules={len(manifest)} methods={total} skipped={len(skipped)}')
for item in skipped:
    print('SKIP', *item, sep=' | ')
