#!/usr/bin/env python3
"""Generate complete Zan wrappers for Senparc WeChat Pay API surfaces.

The wrappers deliberately keep request/response DTOs as JSON/XML strings, like the
other migrated WeChat product lines. Merchant credentials live in
WechatPayV2Config / WechatPayV3Config and are applied by the clients, not repeated
on every generated method.
"""
from pathlib import Path
import json
import re

from generate_wechat_product_apis import (
    ROOT, strip_comments, parse_methods, expanded_body, endpoint_literals,
    endpoint_constants, base_path,
)

VENDORED = ROOT / 'stdlib/Sdk/WeiXinMPSDK-master/src/Senparc.Weixin.TenPay'
OUTPUT = ROOT / 'stdlib/Sdk/Wechat/TenPay'
MARKER = 'Generated from Senparc WeChat Pay APIs'

SOURCE_GROUPS = [
    ('LegacyV2', VENDORED / 'Senparc.Weixin.TenPay/V2/TenPay', 'legacy_mp'),
    ('LegacyRights', VENDORED / 'Senparc.Weixin.TenPay/V2/TenPayRights', 'legacy_mp'),
    ('LegacyUniversal', VENDORED / 'Senparc.Weixin.TenPay/V3/Universal', 'v2'),
    ('V3', VENDORED / 'Senparc.Weixin.TenPayV3/Apis', 'v3'),
]

LEGACY_PATHS = {
    'UnifiedorderAsync': '/pay/unifiedorder',
    'Html5OrderAsync': '/pay/unifiedorder',
    'OrderQueryAsync': '/pay/orderquery',
    'CloseOrderAsync': '/pay/closeorder',
    'RefundAsync': '/secapi/pay/refund',
    'RefundQueryAsync': '/pay/refundquery',
    'DownloadBillAsync': '/pay/downloadbill',
    'MicroPayAsync': '/pay/micropay',
    'ReverseAsync': '/secapi/pay/reverse',
    'ShortUrlAsync': '/tools/shorturl',
    'TransfersAsync': '/mmpaymkttransfers/promotion/transfers',
    'GetTransferInfoAsync': '/mmpaymkttransfers/gettransferinfo',
    'PayBankAsync': '/mmpaysptrans/pay_bank',
    'QueryBankAsync': '/mmpaysptrans/query_bank',
    'GetPublicKeyAsync': '/risk/getpublickey',
}


def is_public_task(method):
    return method['access'] == 'public' and (
        'Task' in method['ret'] or 'ValueTask' in method['ret'] or
        'await ' in method['body']
    )


def pascal(text):
    words = re.findall(r'[A-Za-z0-9]+', text)
    return ''.join(w[:1].upper() + w[1:] for w in words if w)


def class_name(group, root, path):
    rel = path.relative_to(root).with_suffix('')
    parts = list(rel.parts)
    stem_parts = parts[-1].split('.')
    parts = parts[:-1] + stem_parts
    cleaned = []
    for part in parts:
        value = pascal(part)
        if value.lower() in ('apis', 'api', 'current', 'async'):
            if value.lower() == 'current': cleaned.append('Current')
            continue
        cleaned.append(value)
    prefix = 'WechatPayV3' if group == 'V3' else 'WechatPayLegacy'
    return prefix + ''.join(cleaned) + 'Api'


def normalize_path(path):
    p = base_path(path).strip()
    p = re.sub(r'^/\s+', '/', p)
    p = p.replace('/{0}v3/', '/v3/').replace('/{0}v3', '/v3')
    p = p.replace('//', '/')
    p = re.sub(r'\{[^{}]*(?:Escape|UrlQueryHelper|ToParams)[^{}]*$', '{value}', p)
    return p


def infer_verb(text):
    checks = [
        ('DELETE', r'ApiRequestMethod\.DELETE|HttpMethod\.Delete|CommonJsonSendType\.DELETE|\bDELETE\b'),
        ('PATCH', r'ApiRequestMethod\.PATCH|HttpMethod\.Patch|CommonJsonSendType\.PATCH|\bPATCH\b'),
        ('PUT', r'ApiRequestMethod\.PUT|HttpMethod\.Put|CommonJsonSendType\.PUT|\bPUT\b'),
        ('GET', r'ApiRequestMethod\.GET|HttpMethod\.Get|CommonJsonSendType\.GET|HttpGet|\.GetAsync\s*\('),
    ]
    for verb, pattern in checks:
        if re.search(pattern, text): return verb
    return 'POST'


def transport_kind(method, text):
    params = method['params']
    if re.search(r'Multipart|PostFile|StreamContent|ByteArrayContent|FormData', text, re.I):
        return 'multipart'
    if re.search(r'(?:Stream\s+(?:destination|fileStream)|Download(?:File)?Async)', params + '\n' + text, re.I):
        return 'download'
    return 'json'


def certificate_required(path, method, text):
    joined = (path + '\n' + method['params'] + '\n' + text).lower()
    return any(token in joined for token in (
        '/secapi/', 'mmpaymkttransfers', 'mmpaysptrans', 'sendredpack',
        'sendgroupredpack', 'sendworkwxredpack', 'pay_bank', 'certpath',
        'certpassword', 'x509certificate', 'clientcertificate'))


def support_files(path):
    prefix = path.stem.split('.')[0]
    return sorted(set([path] + list(path.parent.glob(prefix + '*.cs'))))


def parse_family(path):
    methods = []
    constants = {}
    next_id = 0
    for file in support_files(path):
        clean = strip_comments(file.read_text(encoding='utf-8-sig', errors='ignore'))
        parsed, next_id = parse_methods(clean, file.as_posix(), next_id)
        methods.extend(parsed)
        for name, values in endpoint_constants(clean).items():
            constants.setdefault(name, []).extend(values)
    by_name = {}
    for method in methods:
        by_name.setdefault(method['name'], []).append(method)
    return methods, by_name, constants


def choose_path(mode, method, expanded, constants):
    if mode == 'v2' and method['name'] in LEGACY_PATHS:
        return LEGACY_PATHS[method['name']], False, [LEGACY_PATHS[method['name']]]
    endpoints = endpoint_literals(expanded)
    if not endpoints:
        for name, values in constants.items():
            if re.search(r'\b' + re.escape(name) + r'\b', expanded):
                endpoints.extend(values)
    normalized = []
    for endpoint in endpoints:
        p = normalize_path(endpoint)
        if p.startswith('/') and p not in normalized:
            normalized.append(p)
    if mode == 'v3':
        preferred = [p for p in normalized if '/v3/' in p or p.startswith('/v3')]
        if len(preferred) == 1: normalized = preferred
    if len(normalized) == 1:
        p = normalized[0]
        dynamic = '{' in p or '}' in p or ' ' in p
        return p, dynamic, normalized
    return '', True, normalized


def render_module(group, root, path, mode):
    methods, by_name, constants = parse_family(path)
    own = [m for m in methods if Path(m['source']).resolve() == path.resolve() and is_public_task(m)]
    if not own: return None
    seen = set()
    entries = []
    for method in own:
        if method['name'] in seen: continue
        seen.add(method['name'])
        expanded = expanded_body(method, by_name)
        endpoint, dynamic, candidates = choose_path(mode, method, expanded, constants)
        verb = infer_verb(expanded)
        kind = transport_kind(method, expanded)
        entries.append({
            'name': method['name'], 'verb': verb, 'path': endpoint,
            'dynamic': dynamic, 'candidates': candidates, 'kind': kind,
            'params': ' '.join(method['params'].split()),
            'certificate': certificate_required(endpoint, method, expanded) if mode == 'v2' else False,
        })
    if not entries: return None
    cls = class_name(group, root, path)
    rel = path.relative_to(root).as_posix()
    if mode == 'v3':
        client = 'WechatPayV3Client'
    elif mode == 'legacy_mp':
        client = 'WechatPayLegacyMpClient'
    else:
        client = 'WechatPayV2Client'
    lines = [
        'using System;',
        'using Sdk.Wechat;',
        '',
        'namespace Sdk.Wechat.TenPay;',
        '',
        '/// <summary>',
        f'/// {rel} 的 Zan 微信支付接口。',
        '/// 商户号、私钥、证书和 API 密钥由产品客户端配置统一提供。',
        f'/// {MARKER}; regenerate with scripts/generate_wechat_tenpay_apis.py.',
        '/// </summary>',
        f'class {cls} {{',
        f'    {client} client;',
        '',
        f'    public {cls}({client} client) {{ this.client = client; }}',
        '',
    ]
    for entry in entries:
        shown_path = entry['path'] if entry['path'] else '(调用方提供完整 path)'
        lines.append(f'    /// <summary>{entry["verb"]} {shown_path}; C# 参数：{entry["params"]}</summary>')
        if mode == 'v2':
            if entry['dynamic']:
                lines += [
                    f'    async WechatRawResponse {entry["name"]}(string path, WechatPayV2Request request) {{',
                    f'        return await this.client.RequestAsync(path, request, {str(entry["certificate"]).lower()});',
                    '    }', '',
                ]
            else:
                lines += [
                    f'    async WechatRawResponse {entry["name"]}(WechatPayV2Request request) {{',
                    f'        return await this.client.RequestAsync("{entry["path"]}", request, {str(entry["certificate"]).lower()});',
                    '    }', '',
                ]
        elif entry['kind'] == 'multipart':
            if entry['dynamic']:
                lines += [
                    f'    async WechatRawResponse {entry["name"]}(string path, string query, WechatMultipart multipart) {{',
                    '        return await this.client.RequestMultipartAsync("' + entry['verb'] + '", path, query, multipart);',
                    '    }', '',
                ]
            else:
                lines += [
                    f'    async WechatRawResponse {entry["name"]}(string query, WechatMultipart multipart) {{',
                    '        return await this.client.RequestMultipartAsync("' + entry['verb'] + '", "' + entry['path'] + '", query, multipart);',
                    '    }', '',
                ]
        elif entry['verb'] in ('GET', 'DELETE'):
            if entry['dynamic']:
                lines += [
                    f'    async WechatRawResponse {entry["name"]}(string path, string query) {{',
                    f'        return await this.client.RequestJsonAsync("{entry["verb"]}", path, query, "");',
                    '    }', '',
                ]
            else:
                lines += [
                    f'    async WechatRawResponse {entry["name"]}(string query) {{',
                    f'        return await this.client.RequestJsonAsync("{entry["verb"]}", "{entry["path"]}", query, "");',
                    '    }', '',
                ]
        else:
            if entry['dynamic']:
                lines += [
                    f'    async WechatRawResponse {entry["name"]}(string path, string query, string jsonBody) {{',
                    f'        return await this.client.RequestJsonAsync("{entry["verb"]}", path, query, jsonBody);',
                    '    }', '',
                ]
            else:
                lines += [
                    f'    async WechatRawResponse {entry["name"]}(string query, string jsonBody) {{',
                    f'        return await this.client.RequestJsonAsync("{entry["verb"]}", "{entry["path"]}", query, jsonBody);',
                    '    }', '',
                ]
    lines.append('}')
    return cls, entries, '\n'.join(lines) + '\n'


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for old in OUTPUT.glob('*.zan'):
        if MARKER in old.read_text(encoding='utf-8', errors='ignore'):
            old.unlink()
    modules = []
    for group, root, mode in SOURCE_GROUPS:
        for path in sorted(root.rglob('*.cs')):
            rendered = render_module(group, root, path, mode)
            if rendered is None: continue
            cls, entries, text = rendered
            filename = cls + '.zan'
            # A class name can collide for partial files with generic suffixes;
            # preserve every source surface by adding a stable numeric suffix.
            target = OUTPUT / filename
            suffix = 2
            while target.exists() and MARKER in target.read_text(encoding='utf-8', errors='ignore'):
                filename = cls + str(suffix) + '.zan'
                target = OUTPUT / filename
                suffix += 1
            target.write_text(text.replace(f'class {cls} {{', f'class {target.stem} {{')
                                   .replace(f'public {cls}(', f'public {target.stem}('),
                              encoding='utf-8', newline='\n')
            modules.append({'group': group, 'source': path.relative_to(root).as_posix(),
                            'file': filename, 'class': target.stem, 'mode': mode,
                            'methods': entries})
    manifest = {
        'modules': modules,
        'module_count': len(modules),
        'method_count': sum(len(m['methods']) for m in modules),
        'dynamic_count': sum(e['dynamic'] for m in modules for e in m['methods']),
        'multipart_count': sum(e['kind'] == 'multipart' for m in modules for e in m['methods']),
    }
    path = ROOT / '_scratch/wechat_tenpay_generated_manifest.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print('TenPay modules=' + str(manifest['module_count']),
          'methods=' + str(manifest['method_count']),
          'dynamic=' + str(manifest['dynamic_count']),
          'multipart=' + str(manifest['multipart_count']))


if __name__ == '__main__':
    main()
