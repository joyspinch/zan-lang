#!/usr/bin/env python3
"""Generate Work, WxOpen and Open JSON API wrappers from the vendored C# SDK.

Run from the repository root:
    python scripts/generate_wechat_product_apis.py

Only endpoints that resolve to one unambiguous JSON HTTP request are emitted.
Multipart, streams, binary/plain-text responses, WebSockets, signed payment
requests, and ambiguous dynamic endpoints are written to an audit manifest in
_scratch instead of being represented by a misleading wrapper.
"""

from pathlib import Path
from urllib.parse import urlsplit
import json
import re

ROOT = Path.cwd()
MARKER = 'Generated from Senparc WeChat product APIs'

PRODUCTS = {
    'Work': {
        'source': ROOT / 'stdlib/Sdk/WeiXinMPSDK-master/src/Senparc.Weixin.Work/Senparc.Weixin.Work/AdvancedAPIs',
        'output': ROOT / 'stdlib/Sdk/Wechat/Work',
        'namespace': 'Sdk.Wechat.Work',
        'prefix': 'WechatWork',
        'client': 'WechatWorkClient',
    },
    'WxOpen': {
        'source': ROOT / 'stdlib/Sdk/WeiXinMPSDK-master/src/Senparc.Weixin.WxOpen/src/Senparc.Weixin.WxOpen/Senparc.Weixin.WxOpen/AdvancedAPIs',
        'output': ROOT / 'stdlib/Sdk/Wechat/WxOpen',
        'namespace': 'Sdk.Wechat.WxOpen',
        'prefix': 'WechatWxOpen',
        'client': 'WechatWxOpenClient',
    },
    'Open': {
        'source': ROOT / 'stdlib/Sdk/WeiXinMPSDK-master/src/Senparc.Weixin.Open/Senparc.Weixin.Open',
        'output': ROOT / 'stdlib/Sdk/Wechat/Open',
        'namespace': 'Sdk.Wechat.Open',
        'prefix': 'WechatOpen',
        'client': 'WechatOpenClient',
    },
}


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


def parse_methods(text, source_name, id_start):
    pattern = re.compile(
        r'(?P<access>public|private|internal|protected)\s+'
        r'(?:static\s+)?(?:async\s+)?'
        r'(?P<ret>[\w<>,.?\[\]\s]+?)\s+'
        r'(?P<name>\w+)\s*'
        r'(?:<[^>{}]+>)?\s*'
        r'\((?P<params>[^)]*)\)\s*'
        r'(?:where[^\{]+)?\{',
        re.M,
    )
    methods = []
    next_id = id_start
    for match in pattern.finditer(text):
        start = match.end()
        depth = 1
        i = start
        in_string = False
        verbatim = False
        escaped = False
        while i < len(text) and depth:
            c = text[i]
            if in_string:
                if verbatim:
                    if c == '"' and i + 1 < len(text) and text[i + 1] == '"':
                        i += 2
                        continue
                    if c == '"':
                        in_string = False
                        verbatim = False
                else:
                    if escaped:
                        escaped = False
                    elif c == '\\':
                        escaped = True
                    elif c == '"':
                        in_string = False
            else:
                if c == '@' and i + 1 < len(text) and text[i + 1] == '"':
                    in_string = True
                    verbatim = True
                    i += 1
                elif c == '"':
                    in_string = True
                elif c == '{':
                    depth += 1
                elif c == '}':
                    depth -= 1
            i += 1
        methods.append({
            'id': next_id,
            'source': source_name,
            'access': match.group('access'),
            'name': match.group('name'),
            'ret': ' '.join(match.group('ret').split()),
            'params': ' '.join(match.group('params').split()),
            'body': text[start:i - 1],
        })
        next_id += 1
    return methods, next_id


def expanded_body(method, by_name, depth=0, seen=None):
    if seen is None:
        seen = set()
    if method['id'] in seen or depth > 5:
        return ''
    seen = set(seen)
    seen.add(method['id'])
    body = method['body']
    called = set(re.findall(r'\b([A-Za-z_]\w*)\s*(?:<[^;{}()]*>)?\s*\(', body))
    pieces = [body]
    for name in called:
        for callee in by_name.get(name, []):
            if callee['id'] not in seen:
                pieces.append(expanded_body(callee, by_name, depth + 1, seen))
    return '\n'.join(pieces)


def endpoint_literals(text):
    values = []
    for literal in re.findall(r'"([^"\r\n]+)"', text):
        value = literal.strip().replace('{{', '{').replace('}}', '}')
        value = re.sub(r'/\s+(?=[A-Za-z])', '/', value)
        if re.match(r'https?://', value, re.I):
            parsed = urlsplit(value.replace('{', 'x').replace('}', 'x'))
            if parsed.path and parsed.path != '/':
                original_start = value.find('/', value.find('://') + 3)
                values.append(value[original_start:])
            continue
        match = re.search(r'/(?!/|\s)(?:[A-Za-z0-9_.-]+/)+[A-Za-z0-9_{}.:\-]+(?:\?[^"\s]*)?', value)
        if match:
            values.append(match.group(0))
    return values


def base_path(endpoint):
    endpoint = re.sub(r'^/\s+', '/', endpoint.strip())
    return endpoint.split('?', 1)[0]


def endpoint_constants(text):
    result = {}
    pattern = re.compile(
        r'(?:static\s+)?(?:readonly\s+)?string\s+(?P<name>\w+)\s*=\s*'
        r'(?P<expr>[^;]+);',
        re.M,
    )
    for match in pattern.finditer(text):
        endpoints = endpoint_literals(match.group('expr'))
        if endpoints:
            result.setdefault(match.group('name'), []).extend(endpoints)
    return result


def selected_source(product, path, base):
    rel = str(path.relative_to(base)).replace('\\', '/')
    stem = path.stem.lower()
    if 'test' in rel.lower() or '/entities/' in '/' + rel.lower():
        return False
    if product == 'WxOpen' and rel == 'Sec/Order.cs':
        return True
    return 'api' in stem


def clean_part(part):
    chunks = re.split(r'[^A-Za-z0-9]+', part)
    result = ''
    for chunk in chunks:
        if not chunk:
            continue
        if chunk.lower() in ('advancedapis', 'wxaapis', 'mpapis'):
            continue
        if chunk.lower().endswith('apis') and len(chunk) > 4:
            chunk = chunk[:-4]
        elif chunk.lower().endswith('api') and len(chunk) > 3:
            chunk = chunk[:-3]
        result += chunk[:1].upper() + chunk[1:]
    return result


def class_name(prefix, rel):
    parts = list(rel.parts)
    stem_parts = parts[-1].split('.')[:-1]
    names = []
    for part in parts[:-1] + stem_parts:
        cleaned = clean_part(part)
        if not cleaned:
            continue
        if names and cleaned.lower().startswith(names[-1].lower()):
            names[-1] = cleaned
        elif not names or names[-1].lower() != cleaned.lower():
            names.append(cleaned)
    return prefix + ''.join(names) + 'Api'


def is_special(method, expanded):
    all_text = method['params'] + ' ' + method['ret'] + ' ' + expanded
    if 'WeixinObsoleteException' in expanded:
        return 'obsolete forwarding API'
    if re.search(r'\b(Stream|FileStream|MemoryStream|byte\[\]|IFormFile|HttpResponseMessage|WebSocket)\b', all_text):
        return 'stream/binary'
    if re.search(r'PostFile|GetFileStream|Multipart|multipart/form-data|RequestUtility\.', expanded, re.I):
        return 'special transport'
    if re.search(r'application/x-www-form-urlencoded|text/csv|octet-stream', expanded, re.I):
        return 'non-json transport/response'
    if re.search(r'\bTask\s*<\s*string\s*>', method['ret']):
        return 'plain-text response'
    return ''


def infer_verb(expanded):
    if re.search(r'CommonJsonSendType\.DELETE|HttpMethod\.Delete|\bDELETE\b', expanded):
        return 'DELETE'
    if re.search(r'CommonJsonSendType\.PATCH|HttpMethod\.Patch|\bPATCH\b', expanded):
        return 'PATCH'
    if re.search(r'CommonJsonSendType\.PUT|HttpMethod\.Put|\bPUT\b', expanded):
        return 'PUT'
    if re.search(r'CommonJsonSendType\.GET|Get\.GetJson|HttpMethod\.Get|HttpGet|\.GetAsync\s*\(', expanded):
        return 'GET'
    return 'POST'


def credential_name(product, endpoint, method, expanded):
    recognized = ('component_access_token', 'provider_access_token', 'suite_access_token', 'access_token')
    lowered = endpoint.lower()
    for name in recognized:
        if re.search(r'(?:\?|&)' + re.escape(name) + r'\s*=', lowered):
            return name
    params = method['params']
    param_names = set(re.findall(r'\b([A-Za-z_]\w*)\s*(?:=|,|$)', params))
    lower_names = {name.lower() for name in param_names}
    body_lower = expanded.lower()
    if product == 'Work':
        if 'provideraccesstoken' in lower_names:
            return 'provider_access_token'
        if 'suiteaccesstoken' in lower_names:
            return 'suite_access_token'
        if any(name in lower_names for name in ('accesstoken', 'accesstokenorappkey', 'accesstokenorappid')) \
                or 'apihandlerwapper' in body_lower:
            return 'access_token'
    elif product == 'WxOpen':
        if any(name in lower_names for name in ('accesstoken', 'accesstokenorappid')) \
                or 'apihandlerwapper' in body_lower:
            return 'access_token'
    elif product == 'Open':
        if 'componentaccesstoken' in lower_names:
            return 'component_access_token'
        if any(name in lower_names for name in ('accesstoken', 'authorizeraccesstoken', 'oauthaccesstoken')):
            return 'access_token'
    return ''


def generate_product(product, config):
    source = config['source'].resolve()
    output = config['output']
    output.mkdir(parents=True, exist_ok=True)
    for old in output.glob('*.zan'):
        if MARKER in old.read_text(encoding='utf-8', errors='ignore'):
            old.unlink()

    all_cs = sorted(source.rglob('*.cs'))
    selected = [p for p in all_cs if selected_source(product, p, source)]
    manifest = []
    skipped = []
    total = 0

    for path in selected:
        rel = path.relative_to(source)
        rel_text = str(rel).replace('\\', '/')
        support_files = sorted(path.parent.glob('*.cs'))
        all_methods = []
        target_methods = []
        constants = {}
        next_id = 0
        for support in support_files:
            clean = strip_comments(support.read_text(encoding='utf-8-sig', errors='ignore'))
            methods, next_id = parse_methods(clean, str(support.resolve()), next_id)
            all_methods.extend(methods)
            if support.resolve() == path.resolve():
                target_methods.extend(methods)
            for name, endpoints in endpoint_constants(clean).items():
                constants.setdefault(name, []).extend(endpoints)

        by_name = {}
        for method in all_methods:
            by_name.setdefault(method['name'], []).append(method)

        emitted = []
        seen_names = set()
        for method in target_methods:
            if method['access'] != 'public' or not method['name'].endswith('Async'):
                continue
            if method['name'] in seen_names:
                continue
            seen_names.add(method['name'])
            expanded = expanded_body(method, by_name)
            reason = is_special(method, expanded)
            if reason:
                skipped.append((rel_text, method['name'], reason))
                continue

            endpoints = endpoint_literals(expanded)
            if not endpoints:
                for name, values in constants.items():
                    if re.search(r'\b' + re.escape(name) + r'\b', expanded):
                        endpoints.extend(values)
            unique = []
            for endpoint in endpoints:
                path_only = base_path(endpoint)
                if path_only.startswith('/') and path_only not in unique:
                    unique.append(path_only)
            if not unique:
                skipped.append((rel_text, method['name'], 'endpoint unresolved'))
                continue
            if len(unique) != 1:
                skipped.append((rel_text, method['name'], 'multiple dynamic endpoints'))
                continue

            endpoint = endpoints[0]
            path_only = unique[0]
            if '{' in path_only or '}' in path_only or ' ' in path_only:
                skipped.append((rel_text, method['name'], 'dynamic path segment'))
                continue
            verb = infer_verb(expanded)
            credential = credential_name(product, endpoint, method, expanded)
            emitted.append({
                'name': method['name'],
                'path': path_only,
                'verb': verb,
                'credential': credential,
            })

        if not emitted:
            continue
        cls = class_name(config['prefix'], rel)
        lines = [
            'using System;',
            'using Sdk.Wechat;',
            '',
            f'namespace {config["namespace"]};',
            '',
            '/// <summary>',
            f'/// {rel_text} 的 Zan JSON 接口。',
            '/// query / JSON 参数由调用方提供；token 和网络传输由产品客户端统一处理。',
            f'/// {MARKER}; regenerate with scripts/generate_wechat_product_apis.py.',
            '/// </summary>',
            f'class {cls} {{',
            f'    {config["client"]} client;',
            '',
            f'    public {cls}({config["client"]} client) {{',
            '        this.client = client;',
            '    }',
            '',
        ]
        for entry in emitted:
            lines.append(f'    /// <summary>{entry["verb"]} {entry["path"]}</summary>')
            if entry['verb'] in ('GET', 'DELETE'):
                lines += [
                    f'    async WechatResponse {entry["name"]}(string query) {{',
                    f'        return await this.client.RequestAsync("{entry["verb"]}", "{entry["path"]}", query, "", "{entry["credential"]}");',
                    '    }',
                    '',
                ]
            else:
                lines += [
                    f'    async WechatResponse {entry["name"]}(string query, string jsonBody) {{',
                    f'        return await this.client.RequestAsync("{entry["verb"]}", "{entry["path"]}", query, jsonBody, "{entry["credential"]}");',
                    '    }',
                    '',
                ]
        lines.append('}')
        filename = cls + '.zan'
        (output / filename).write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
        manifest.append({
            'source': rel_text,
            'file': filename,
            'class': cls,
            'methods': emitted,
        })
        total += len(emitted)

    return {'modules': manifest, 'skipped': skipped, 'method_count': total}


def main():
    result = {}
    for product, config in PRODUCTS.items():
        generated = generate_product(product, config)
        result[product] = generated
        print(product, 'modules=' + str(len(generated['modules'])),
              'methods=' + str(generated['method_count']),
              'skipped=' + str(len(generated['skipped'])))

    manifest_path = ROOT / '_scratch/wechat_product_generated_manifest.json'
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
